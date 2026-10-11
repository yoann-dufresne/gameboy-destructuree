/**
 * L'automate de la doublure : la copie, le service de la console, le
 * journal, le silence (plan §3, §5, §6, §7).
 *
 * Comme pour le lecteur, aucune entrée-sortie réelle : le réseau et la flash
 * sont des interfaces. Le bus de la console arrive par console_ecrit() — dans
 * le firmware, le PIO remonte les écritures au processeur — et console_lit(),
 * qui ne sert qu'au PC : dans le firmware, le PIO et le DMA servent les
 * lectures sans passer par ici (plan §5.2).
 *
 * Règles tenues (plan §3.1, §3.4, §3.5, §5.6, §6) :
 *   - les pages modifiées partent en journal 200 ms après la dernière écriture,
 *     au plus tard 1 s après la première ;
 *   - le journal est numéroté (session, numéro) et renvoyé tant qu'il n'est pas
 *     accusé ;
 *   - une annonce qui dit la fente vide, ou une autre insertion, fige la
 *     console : la doublure se tait et n'enregistre plus rien jusqu'à ce que
 *     la console s'éteigne ;
 *   - console éteinte, un journal non accusé est écrit en flash, jamais
 *     console allumée (l'écriture de la flash gênerait le service de la ROM) ;
 *   - une nouvelle copie n'est préparée qu'une fois le journal vide : le
 *     lecteur a ainsi appliqué, parqué ou oublié toutes les écritures
 *     précédentes avant de donner la sauvegarde de la cartouche suivante.
 */
#pragma once

#include "car1.h"
#include "entete.hpp"
#include "imitation.hpp"
#include "mbc.hpp"
#include "paquet.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace cartouche {

struct EntreeJournal {
    uint32_t insertion = 0;
    uint32_t session = 0;
    uint32_t numero = 0;
    uint8_t genre = CAR1_PAGES;
    std::vector<std::pair<uint16_t, std::array<uint8_t, OCTETS_PAGE>>> pages;
    uint8_t horloge[RTC_NB] = {};
    uint8_t vibreur = 0;
};

struct DoublureES {
    virtual ~DoublureES() = default;
    virtual void envoyer(const std::vector<uint8_t> &paquet) = 0;
    /* La flash : les ROM déjà vues, rangées par signature. */
    virtual bool rom_connue(const Signature &s, std::vector<uint8_t> &rom) = 0;
    virtual void rom_ranger(const Signature &s, const std::vector<uint8_t> &rom) = 0;
    /* La flash : le journal en lieu sûr (plan §3.5). */
    virtual void journal_sauver(const std::deque<EntreeJournal> &j) = 0;
    virtual bool journal_charger(std::deque<EntreeJournal> &j) = 0;
    virtual void journal_effacer() = 0;
};

struct ParametresDoublure {
    uint32_t appairage = 0;
    uint32_t silence_journal_ms = 200;  /* plan §3.1 */
    uint32_t delai_journal_max_ms = 1000;
    uint32_t renvoi_journal_ms = 100;
    uint32_t periode_etat_ms = 250;
    uint32_t renvoi_demande_ms = 60;
    size_t entrees_par_envoi = 8;
};

/* ======================================================================== */

class Doublure {
  public:
    Doublure(DoublureES &es, const ParametresDoublure &p) : es_(es), p_(p) {}

    void demarrer(uint32_t maintenant, uint32_t graine_session) {
        session_ = graine_session ? graine_session : 1;
        prochain_ = 1;
        journal_.clear();
        en_flash_ = es_.journal_charger(journal_);
        dernier_tic_ = maintenant;
        t_demarrage_ = maintenant;
    }

    /* ---- La console ---- */

    void console(bool allumee, uint32_t maintenant) {
        if (allumee == allumee_) return;
        allumee_ = allumee;
        if (allumee) {
            if (copie_servable()) {
                imitation_.installer(copie_.cfg, copie_.rom.data(), copie_.sauvegarde);
                imitation_.controleur().horloge = copie_.horloge;
                sert_ = true;
                muette_ = false;
                insertion_servie_ = copie_.insertion;
            } else {
                sert_ = false;
                muette_ = true; /* fente vide : un rectangle noir (plan §5.6) */
            }
            return;
        }
        if (sert_ && !muette_) journaliser(maintenant);
        if (sert_ && copie_.insertion == insertion_servie_) {
            copie_.sauvegarde = imitation_.ram(); /* la sauvegarde vivante, pour le prochain allumage */
            copie_.horloge = imitation_.controleur().horloge;
        }
        sert_ = false;
        muette_ = false;
        if (!journal_.empty()) { /* plan §3.5 : seulement console éteinte */
            es_.journal_sauver(journal_);
            en_flash_ = true;
        }
    }

    /* Une lecture de la console. Dans le firmware, le PIO et le DMA. */
    uint8_t console_lit(uint16_t adr) const {
        if (!sert_ || muette_) return 0xFF; /* une fente vide se lit FF */
        return imitation_.lire(adr);
    }

    void console_ecrit(uint16_t adr, uint8_t v, uint32_t maintenant) {
        if (!sert_ || muette_) return; /* figée : ce que le jeu écrit est du bruit */
        const Effet e = imitation_.ecrire(adr, v);
        if (e.page) {
            if (marques_.empty()) t_premiere_marque_ = maintenant;
            t_derniere_ecriture_ = maintenant;
            if (std::find(marques_.begin(), marques_.end(), e.numero_page) == marques_.end())
                marques_.push_back(e.numero_page);
        }
        if (e.horloge) {
            EntreeJournal j = nouvelle_entree(CAR1_REGLAGE_HORLOGE);
            std::copy_n(imitation_.controleur().horloge.reel, RTC_NB, j.horloge);
            journal_.push_back(j);
        }
        if (e.vibreur) {
            EntreeJournal j = nouvelle_entree(CAR1_VIBREUR);
            j.vibreur = imitation_.controleur().vibreur_actif();
            journal_.push_back(j);
            envoyer_journal(maintenant); /* le moteur n'attend pas */
        }
    }

    /* L'injecteur de la manette n'allume la console qu'avec ce feu vert :
     * cartouche dans la fente, sa copie doit être prête (plan §5.6). */
    bool feu_vert() const {
        /* Juste après un démarrage, laisser une seconde à la première annonce. */
        if (!annonce_connue_) return dernier_tic_ - t_demarrage_ >= 1000;
        if (annonce_.etat == CAR1_LECTURE) return false;
        if (annonce_.etat == CAR1_PRETE) return copie_servable();
        return true;
    }

    /* ---- Le temps et le réseau ---- */

    void tic(uint32_t maintenant) {
        const uint32_t dt = maintenant - dernier_tic_;
        dernier_tic_ = maintenant;
        if (sert_ && !muette_) imitation_.avancer_ms(dt);
        else if (copie_.prete && copie_.cfg.horloge) copie_.horloge.avancer_ms(dt);

        if (!marques_.empty() && (maintenant - t_derniere_ecriture_ >= p_.silence_journal_ms ||
                                  maintenant - t_premiere_marque_ >= p_.delai_journal_max_ms))
            journaliser(maintenant);
        if (!journal_.empty() && maintenant - t_envoi_journal_ >= p_.renvoi_journal_ms) envoyer_journal(maintenant);
        if (maintenant - t_etat_ >= p_.periode_etat_ms) envoyer_etat(maintenant);
        preparer_copie(maintenant);
    }

    void recevoir(const uint8_t *d, size_t n, uint32_t maintenant) {
        car1_entete e;
        size_t lg;
        const uint8_t *c = deballer(d, n, p_.appairage, e, lg);
        if (!c) return;
        switch (e.type) {
        case CAR1_ANNONCE: recevoir_annonce(e, c, lg, maintenant); break;
        case CAR1_BLOC: recevoir_bloc(e, c, lg); break;
        case CAR1_ACCUSE: recevoir_accuse(c, lg, maintenant); break;
        default: break;
        }
    }

    /* ---- Observation ---- */
    bool sert() const { return sert_; }
    bool muette() const { return muette_; }
    bool vivante() const { return sert_ && !muette_; } /* le jeu tourne sur une copie */
    uint32_t insertion_servie() const { return insertion_servie_; }
    const std::vector<uint8_t> &ram_vivante() const { return imitation_.ram(); }
    bool copie_prete() const { return copie_.prete; }
    uint32_t insertion_copie() const { return copie_.insertion; }
    const std::vector<uint8_t> &sauvegarde_copie() const { return copie_.sauvegarde; }
    size_t en_attente() const { return journal_.size() + (marques_.empty() ? 0 : 1); }
    const std::deque<EntreeJournal> &journal() const { return journal_; }
    /* Une coupure de courant maintenant perdrait-elle des écritures ? */
    bool perte_si_coupure() const {
        if (sert_ && !muette_) return true; /* le jeu peut écrire à tout instant */
        if (!marques_.empty()) return true;
        return !journal_.empty() && (allumee_ || !en_flash_);
    }

  private:
    struct Copie {
        uint32_t insertion = 0;
        car1_annonce annonce{};
        Signature signature;
        Config cfg;
        std::vector<uint8_t> rom;
        std::vector<uint8_t> sauvegarde;
        std::vector<bool> rom_recue, sauvegarde_recue;
        bool rom_ok = false, sauvegarde_ok = false, prete = false;
        Horloge horloge;
        uint32_t t_annonce = 0;
    };

    bool copie_servable() const {
        return copie_.prete && annonce_connue_ && annonce_.etat == CAR1_PRETE &&
               insertion_annoncee_ == copie_.insertion;
    }

    EntreeJournal nouvelle_entree(uint8_t genre) {
        EntreeJournal j;
        j.insertion = insertion_servie_;
        j.session = session_;
        j.numero = prochain_++;
        j.genre = genre;
        return j;
    }

    void journaliser(uint32_t) {
        std::sort(marques_.begin(), marques_.end());
        const std::vector<uint8_t> &ram = imitation_.ram();
        for (size_t i = 0; i < marques_.size(); i += CAR1_PAGES_MAX) {
            EntreeJournal j = nouvelle_entree(CAR1_PAGES);
            for (size_t k = i; k < std::min(marques_.size(), i + CAR1_PAGES_MAX); ++k) {
                std::array<uint8_t, OCTETS_PAGE> page{};
                const size_t off = (size_t)marques_[k] * OCTETS_PAGE;
                std::copy_n(ram.begin() + (long)off, std::min<size_t>(OCTETS_PAGE, ram.size() - off), page.begin());
                j.pages.emplace_back(marques_[k], page);
            }
            journal_.push_back(j);
        }
        marques_.clear();
    }

    void envoyer_journal(uint32_t maintenant) {
        t_envoi_journal_ = maintenant;
        size_t n = 0;
        for (const EntreeJournal &j : journal_) {
            if (n++ >= p_.entrees_par_envoi) break;
            car1_ecriture w{};
            w.session = j.session;
            w.numero = j.numero;
            w.base = base(j.session);
            w.genre = j.genre;
            std::vector<uint8_t> suite;
            if (j.genre == CAR1_PAGES) {
                w.nombre = (uint8_t)j.pages.size();
                for (const auto &pg : j.pages) {
                    const uint8_t *q = (const uint8_t *)&pg.first;
                    suite.insert(suite.end(), q, q + 2);
                    suite.insert(suite.end(), pg.second.begin(), pg.second.end());
                }
            } else if (j.genre == CAR1_REGLAGE_HORLOGE) {
                suite.assign(j.horloge, j.horloge + RTC_NB);
            } else {
                suite.push_back(j.vibreur);
            }
            es_.envoyer(emballer(CAR1_ECRITURE, p_.appairage, j.insertion, &w, sizeof w, suite.data(), suite.size()));
        }
    }

    uint32_t base(uint32_t session) const {
        for (const EntreeJournal &j : journal_)
            if (j.session == session) return j.numero;
        return 0;
    }

    void envoyer_etat(uint32_t maintenant) {
        t_etat_ = maintenant;
        car1_etat s{};
        s.copie_prete = copie_.prete;
        s.console_allumee = allumee_;
        s.muette = muette_;
        s.session = session_;
        s.en_attente = (uint16_t)std::min<size_t>(journal_.size(), 0xFFFF);
        es_.envoyer(emballer(CAR1_ETAT, p_.appairage, copie_.insertion, &s, sizeof s));
    }

    void recevoir_annonce(const car1_entete &e, const uint8_t *c, size_t lg, uint32_t maintenant) {
        car1_annonce a;
        if (!extraire(c, lg, a)) return;
        annonce_ = a;
        insertion_annoncee_ = e.insertion;
        annonce_connue_ = true;
        annonce_neuve_ = true;
        t_annonce_ = maintenant;
        /* La fente est vide, ou c'est une autre cartouche : on se tait (plan §5.6). */
        if (sert_ && !muette_ && (a.etat != CAR1_PRETE || e.insertion != insertion_servie_)) {
            journaliser(maintenant); /* ce qui restait du journal part au lecteur, qui le parquera */
            muette_ = true;
        }
    }

    /* ---- La copie ---- */

    void preparer_copie(uint32_t maintenant) {
        if (!annonce_connue_ || annonce_.etat != CAR1_PRETE) return;
        if (sert_ && !muette_) return;                      /* on joue : rien à préparer */
        if (!journal_.empty() || !marques_.empty()) return; /* le journal d'abord */
        if (copie_.insertion != insertion_annoncee_) commencer_copie(maintenant);
        /* Une annonce antérieure au dernier accusé ne compte pas : elle ne voit
         * pas encore les écritures qu'il vient de confirmer. */
        if (annonce_neuve_ && copie_.prete && copie_.sauvegarde_ok && !sert_ && t_annonce_ > t_dernier_accuse_ &&
            crc32(0, copie_.sauvegarde.data(), copie_.sauvegarde.size()) != annonce_.crc_sauvegarde) {
            /* La sauvegarde de la cartouche a changé : la cartouche fait foi. */
            copie_.sauvegarde_ok = false;
            copie_.prete = false;
            std::fill(copie_.sauvegarde_recue.begin(), copie_.sauvegarde_recue.end(), false);
        }
        annonce_neuve_ = false;
        if (copie_.prete) return;
        if (maintenant - t_demande_ < p_.renvoi_demande_ms) return;
        t_demande_ = maintenant;
        if (!copie_.rom_ok) demander(CAR1_ZONE_ROM, copie_.rom_recue);
        else if (!copie_.sauvegarde_ok) demander(CAR1_ZONE_SAUVEGARDE, copie_.sauvegarde_recue);
        terminer_si_complete();
    }

    void commencer_copie(uint32_t maintenant) {
        copie_ = Copie{};
        copie_.insertion = insertion_annoncee_;
        copie_.annonce = annonce_;
        copie_.t_annonce = maintenant;
        const car1_annonce &a = annonce_;
        std::memcpy(copie_.signature.titre, a.titre, 16);
        copie_.signature.code_type = a.code_type;
        copie_.signature.code_rom = a.code_rom;
        copie_.signature.code_ram = a.code_ram;
        copie_.signature.somme_entete = a.somme_entete;
        copie_.signature.somme_globale = a.somme_globale;
        copie_.signature.crc_rom = a.crc_rom;
        copie_.signature.octets_rom = a.octets_rom;
        Entete en{};
        en.code_type = a.code_type;
        en.type = decoder_type(a.code_type);
        en.octets_rom = a.octets_rom;
        en.octets_ram = a.octets_ram;
        copie_.cfg = Config::depuis(en);
        if (es_.rom_connue(copie_.signature, copie_.rom)) {
            copie_.rom_ok = true; /* déjà vue : rien à recopier (plan §5.3) */
        } else {
            copie_.rom.assign(a.octets_rom, 0);
            copie_.rom_recue.assign((a.octets_rom + CAR1_OCTETS_BLOC - 1) / CAR1_OCTETS_BLOC, false);
        }
        copie_.sauvegarde.assign(a.octets_ram, 0);
        copie_.sauvegarde_recue.assign((a.octets_ram + CAR1_OCTETS_BLOC - 1) / CAR1_OCTETS_BLOC, false);
        if (a.octets_ram == 0) copie_.sauvegarde_ok = true;
    }

    void demander(uint8_t zone, const std::vector<bool> &recus) {
        std::vector<uint16_t> idx;
        for (size_t i = 0; i < recus.size() && idx.size() < CAR1_DEMANDE_MAX; ++i)
            if (!recus[i]) idx.push_back((uint16_t)i);
        if (idx.empty()) return;
        car1_demande d{};
        d.zone = zone;
        d.nombre = (uint8_t)idx.size();
        es_.envoyer(emballer(CAR1_DEMANDE, p_.appairage, copie_.insertion, &d, sizeof d, idx.data(), idx.size() * 2));
    }

    void recevoir_bloc(const car1_entete &e, const uint8_t *c, size_t lg) {
        car1_bloc b;
        if (!extraire(c, lg, b)) return;
        if (e.insertion != copie_.insertion || copie_.prete) return;
        if (lg < sizeof b + b.octets) return;
        std::vector<uint8_t> &dst = b.zone == CAR1_ZONE_ROM ? copie_.rom : copie_.sauvegarde;
        std::vector<bool> &recus = b.zone == CAR1_ZONE_ROM ? copie_.rom_recue : copie_.sauvegarde_recue;
        if (b.index >= recus.size()) return;
        const size_t off = (size_t)b.index * CAR1_OCTETS_BLOC;
        if (off + b.octets > dst.size()) return;
        std::memcpy(dst.data() + off, c + sizeof b, b.octets);
        recus[b.index] = true;
        terminer_si_complete();
    }

    void terminer_si_complete() {
        if (!copie_.rom_ok && !copie_.rom_recue.empty() &&
            std::all_of(copie_.rom_recue.begin(), copie_.rom_recue.end(), [](bool x) { return x; })) {
            if (crc32(0, copie_.rom.data(), copie_.rom.size()) == copie_.signature.crc_rom) {
                copie_.rom_ok = true;
                es_.rom_ranger(copie_.signature, copie_.rom); /* console éteinte : la flash est libre */
            } else {
                std::fill(copie_.rom_recue.begin(), copie_.rom_recue.end(), false); /* jamais une copie fausse */
            }
        }
        if (copie_.rom_ok && !copie_.sauvegarde_ok && !copie_.sauvegarde_recue.empty() &&
            std::all_of(copie_.sauvegarde_recue.begin(), copie_.sauvegarde_recue.end(), [](bool x) { return x; })) {
            if (crc32(0, copie_.sauvegarde.data(), copie_.sauvegarde.size()) == annonce_.crc_sauvegarde) {
                copie_.sauvegarde_ok = true;
            } else {
                std::fill(copie_.sauvegarde_recue.begin(), copie_.sauvegarde_recue.end(), false);
            }
        }
        if (copie_.rom_ok && copie_.sauvegarde_ok && !copie_.prete) {
            copie_.prete = true;
            if (copie_.cfg.mbc == Mbc::Mbc1 && ressemble_mbc1m(copie_.rom.data(), copie_.rom.size()))
                copie_.cfg.mbc1m = true;
            /* L'horloge : celle que le lecteur a lue, plus le temps écoulé depuis. */
            if (copie_.cfg.horloge && annonce_.horloge_lue) {
                std::copy_n(annonce_.horloge, RTC_NB, copie_.horloge.reel);
                copie_.horloge.avancer_ms(annonce_.age_horloge_ms + (dernier_tic_ - t_annonce_));
            }
        }
    }

    void recevoir_accuse(const uint8_t *c, size_t lg, uint32_t maintenant) {
        car1_accuse a;
        if (!extraire(c, lg, a)) return;
        const size_t avant = journal_.size();
        t_dernier_accuse_ = maintenant;
        journal_.erase(std::remove_if(journal_.begin(), journal_.end(),
                                      [&](const EntreeJournal &j) { return j.session == a.session && j.numero <= a.numero; }),
                       journal_.end());
        if (journal_.size() == avant || allumee_) return; /* la flash, seulement console éteinte */
        if (journal_.empty()) {
            if (en_flash_) es_.journal_effacer();
            en_flash_ = false;
        } else if (en_flash_) {
            es_.journal_sauver(journal_);
        }
    }

    DoublureES &es_;
    ParametresDoublure p_;

    uint32_t session_ = 1;
    uint32_t prochain_ = 1;
    std::deque<EntreeJournal> journal_;
    bool en_flash_ = false;
    std::vector<uint16_t> marques_;
    uint32_t t_premiere_marque_ = 0, t_derniere_ecriture_ = 0;
    uint32_t t_envoi_journal_ = 0, t_etat_ = 0, t_demande_ = 0, dernier_tic_ = 0;
    uint32_t t_demarrage_ = 0, t_dernier_accuse_ = 0;

    car1_annonce annonce_{};
    uint32_t insertion_annoncee_ = 0;
    bool annonce_connue_ = false;
    bool annonce_neuve_ = false; /* pas encore comparée à la copie */
    uint32_t t_annonce_ = 0;

    Copie copie_;
    Imitation imitation_;
    bool allumee_ = false, sert_ = false, muette_ = false;
    uint32_t insertion_servie_ = 0;
};

} // namespace cartouche
