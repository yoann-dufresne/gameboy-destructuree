/**
 * L'automate du lecteur : la fente, la copie, le journal appliqué, la
 * sauvegarde parquée (plan §3, §4, §6, §7).
 *
 * Aucune entrée-sortie réelle ici : la fente, le réseau et la flash sont des
 * interfaces que le firmware branchera sur le matériel, et que la simulation
 * branche sur une fausse cartouche (test/simulation.cpp). L'automate avance
 * par événements : tic() régulier, paquet reçu.
 *
 * Ce que le lecteur garde en flash (MemoireLecteur), pour survivre à un
 * redémarrage :
 *   - le contexte de sauvegarde : la cartouche dont il tient la sauvegarde, les
 *     numéros d'insertion qui la désignent, le CRC de chacune de ses pages
 *     telles qu'il les sait dans la cartouche, et les pages parquées ;
 *   - pour chaque session de la doublure, le dernier numéro traité.
 *
 * Un seul contexte à la fois : c'est « une seule sauvegarde parquée à la
 * fois » du plan §3.4. Une autre cartouche le remplace.
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
#include <vector>

namespace cartouche {

using Page = std::array<uint8_t, OCTETS_PAGE>;

/* ---- Ce que le firmware fournit ------------------------------------------ */

struct FenteES {
    virtual ~FenteES() = default;
    virtual bool presente() = 0;  /* le microrupteur */
    virtual void alimenter(bool marche) = 0;
    /* false : la cartouche est partie pendant l'opération */
    virtual bool lire(uint16_t adr, uint8_t &v) = 0;
    virtual bool ecrire(uint16_t adr, uint8_t v) = 0;
};

struct ReseauES {
    virtual ~ReseauES() = default;
    virtual void envoyer(const std::vector<uint8_t> &paquet) = 0;
};

/* Une page qui attend d'être écrite dans la cartouche. */
struct PageParquee {
    uint16_t page = 0;
    Page neuf{};                 /* le dernier contenu voulu */
    /* Une écriture de cette page a été entamée sans être confirmée : la page
     * peut porter un début d'un contenu tenté sur la fin de l'ancien (règle
     * stricte, voir ecrite_dans_l_ordre()). */
    bool tentee = false;
    Page ancien{};
    std::vector<Page> tentes;
    uint32_t origine = 0;        /* insertion de la copie qui l'a produite */
};

struct Contexte {
    bool valide = false;
    bool presente = false;       /* la cartouche est dans la fente, lue */
    Signature signature;
    uint32_t octets_ram = 0;
    std::vector<uint32_t> insertions;   /* les numéros qui la désignent */
    std::vector<uint32_t> crc_pages;    /* sa sauvegarde, telle qu'on la sait dans la cartouche */
    std::vector<PageParquee> parquees;
};

struct Flux {
    uint32_t session = 0;
    uint32_t dernier = 0;        /* dernier numéro traité */
};

struct MemoireLecteur {
    Contexte ctx;
    std::vector<Flux> flux;      /* les sessions de la doublure, les plus récentes en fin */
};

struct StockageLecteurES {
    virtual ~StockageLecteurES() = default;
    virtual void sauver(const MemoireLecteur &m) = 0;
    virtual bool charger(MemoireLecteur &m) = 0;
};

/* Pour les tests et le journal de bord : chaque page écrite dans une cartouche. */
struct ObservateurLecteur {
    virtual ~ObservateurLecteur() = default;
    virtual void page_ecrite(uint16_t page, uint32_t insertion_origine) = 0;
    virtual void contexte_remplace(bool avait_des_pages_parquees) = 0;
};

struct ParametresLecteur {
    uint32_t appairage = 0;
    uint32_t delai_insertion_ms = 300;  /* plan §4.3 : la cartouche s'enfonce */
    uint32_t periode_annonce_ms = 250;
    size_t flux_max = 8;
    size_t insertions_max = 16;
};

/* ======================================================================== */

class Lecteur {
  public:
    Lecteur(FenteES &f, ReseauES &r, StockageLecteurES &s, const ParametresLecteur &p)
        : fente_(f), reseau_(r), stockage_(s), p_(p) {}

    ObservateurLecteur *observateur = nullptr;

    /* `graine` : tirée au hasard par le firmware à chaque démarrage. */
    void demarrer(uint32_t maintenant, uint32_t graine) {
        insertion_ = graine ? graine : 1;
        if (!stockage_.charger(m_)) m_ = MemoireLecteur{};
        m_.ctx.presente = false; /* rien n'est sûr avant une nouvelle lecture */
        etat_ = CAR1_FENTE_VIDE;
        vue_ = false;
        derniere_annonce_ = maintenant;
        annoncer();
    }

    void tic(uint32_t maintenant) {
        maintenant_ = maintenant;
        const bool la = fente_.presente();
        if (la && !vue_) {
            /* Le microrupteur vient de se fermer : on l'annonce aussitôt, avant
             * même de lire. Une console qu'on allume maintenant attend la copie
             * au lieu de démarrer sur un rectangle noir (plan §5.6). */
            vue_ = true;
            t_vue_ = maintenant;
            insertion_ = insertion_ + 1 ? insertion_ + 1 : 1;
            etat_ = CAR1_LECTURE;
            annoncer();
        } else if (!la && vue_) {
            vue_ = false;
            retirer();
        }
        if (vue_ && etat_ == CAR1_LECTURE && !lue_ && maintenant - t_vue_ >= p_.delai_insertion_ms) inserer();
        if (maintenant - derniere_annonce_ >= p_.periode_annonce_ms) annoncer();
    }

    void recevoir(const uint8_t *d, size_t n, uint32_t maintenant) {
        maintenant_ = maintenant;
        car1_entete e;
        size_t lg;
        const uint8_t *c = deballer(d, n, p_.appairage, e, lg);
        if (!c) return;
        if (e.type == CAR1_ECRITURE) recevoir_ecriture(e, c, lg);
        else if (e.type == CAR1_DEMANDE) recevoir_demande(e, c, lg);
    }

    /* ---- Observation ---- */
    uint32_t insertion() const { return insertion_; }
    uint8_t etat() const { return etat_; }
    const std::vector<uint8_t> &miroir() const { return miroir_; }
    const MemoireLecteur &memoire() const { return m_; }

  private:
    enum class Lecture { Bonne, Partie, Refusee };

    /* ---------------------------------------------------------------- */
    /* Insertion et retrait                                               */
    /* ---------------------------------------------------------------- */

    void inserer() {
        lue_ = true; /* une seule lecture par insertion */
        uint8_t refus = CAR1_CONTACTS_SALES;
        const Lecture l = lire_tout(refus);
        if (l == Lecture::Partie) {
            relire_plus_tard();
            return;
        }
        if (l == Lecture::Refusee) {
            etat_ = refus;
            annoncer();
            return;
        }
        const Signature sig = signer(rom_.data(), rom_.size());
        Contexte &ctx = m_.ctx;
        const bool meme = ctx.valide && !ctx.presente && ctx.signature == sig &&
                          ctx.octets_ram == miroir_.size() && compatible();
        if (meme) {
            /* La même cartouche, telle qu'on l'avait laissée : la sauvegarde
             * parquée y est écrite avant que la copie parte (plan §3.4). */
            if (!ctx.parquees.empty()) {
                std::vector<PageParquee> lot = ctx.parquees;
                if (!ecrire_lot(lot)) {
                    relire_plus_tard(); /* repartie pendant l'écriture : tout reste parqué */
                    return;
                }
            }
        } else {
            if (observateur && ctx.valide) observateur->contexte_remplace(!ctx.parquees.empty());
            ctx = Contexte{};
            ctx.valide = true;
            ctx.signature = sig;
            ctx.octets_ram = (uint32_t)miroir_.size();
            ctx.crc_pages.resize(pages_de(ctx.octets_ram));
            for (uint32_t p = 0; p < ctx.crc_pages.size(); ++p) ctx.crc_pages[p] = crc_page_miroir((uint16_t)p);
        }
        ctx.presente = true;
        ctx.insertions.push_back(insertion_);
        if (ctx.insertions.size() > p_.insertions_max) ctx.insertions.erase(ctx.insertions.begin());
        stockage_.sauver(m_);
        crc_miroir_ = crc32(0, miroir_.data(), miroir_.size());
        etat_ = CAR1_PRETE;
        annoncer();
    }

    /* Une opération a échoué : la cartouche est partie, ou un contact a lâché.
     * On oublie la lecture ; si elle est encore là, le prochain tic la revoit
     * et la relit, sous un nouveau numéro d'insertion. */
    void relire_plus_tard() {
        fente_.alimenter(false);
        vibreur_ = false;
        vue_ = false;
        lue_ = false;
        etat_ = CAR1_FENTE_VIDE;
        annoncer();
    }

    void retirer() {
        if (m_.ctx.presente) {
            m_.ctx.presente = false;
            stockage_.sauver(m_);
        }
        vibreur_ = false;
        lue_ = false;
        fente_.alimenter(false);
        etat_ = CAR1_FENTE_VIDE;
        annoncer();
    }

    /* La cartouche lue correspond-elle au contexte, telle qu'on l'avait laissée ?
     * La règle stricte (plan §3.4, décision de Yoann du 11/10/2026) : même ROM,
     * et chaque page de la sauvegarde telle que le lecteur la savait, pages
     * parquées comprises. Une page dont l'écriture a été interrompue ne doit
     * porter, octet par octet, que l'ancien contenu ou un contenu tenté. Comparer
     * aussi les pages parquées refuse une cartouche jouée ailleurs dont le jeu
     * n'a réécrit que celles-là (README.md, « Ce que la simulation a montré »). */
    bool compatible() const {
        const Contexte &ctx = m_.ctx;
        for (uint32_t p = 0; p < ctx.crc_pages.size(); ++p) {
            const PageParquee *pp = parquee((uint16_t)p);
            if (!pp) {
                if (crc_page_miroir((uint16_t)p) != ctx.crc_pages[p]) return false;
                continue;
            }
            if (!pp->tentee) {
                if (crc_page_miroir((uint16_t)p) != ctx.crc_pages[p]) return false;
                continue;
            }
            if (!ecrite_dans_l_ordre(*pp, miroir_.data() + p * OCTETS_PAGE,
                                     std::min<uint32_t>(OCTETS_PAGE, (uint32_t)miroir_.size() - p * OCTETS_PAGE)))
                return false;
        }
        return true;
    }

    /* Une page dont l'écriture a été interrompue. Le lecteur écrit une page du
     * premier octet au dernier, et chaque tentative repart du début : la page
     * ne peut porter qu'un début de la dernière tentative, puis la suite d'une
     * tentative plus ancienne, … puis la fin de l'ancien contenu. Autrement
     * dit, en lisant la page du début à la fin, la source de chaque octet ne
     * peut que reculer dans le temps. Une modification faite ailleurs casse
     * presque toujours cette structure. */
    static bool ecrite_dans_l_ordre(const PageParquee &pp, const uint8_t *lu, uint32_t n) {
        /* sources : 0 = l'ancien contenu, 1…m = les tentatives, dans l'ordre */
        const size_t m = pp.tentes.size();
        auto source = [&](size_t s, uint32_t i) { return s == 0 ? pp.ancien[i] : pp.tentes[s - 1][i]; };
        /* possible[s] : les octets 0…i peuvent venir de sources non croissantes, l'octet i de s */
        std::vector<bool> possible(m + 1), suivant(m + 1);
        for (size_t s = 0; s <= m; ++s) possible[s] = lu[0] == source(s, 0);
        for (uint32_t i = 1; i < n; ++i) {
            bool plus_recente = false; /* une source >= s était possible à l'octet précédent */
            for (size_t s = m + 1; s-- > 0;) {
                plus_recente = plus_recente || possible[s];
                suivant[s] = plus_recente && lu[i] == source(s, i);
            }
            possible.swap(suivant);
        }
        return std::find(possible.begin(), possible.end(), true) != possible.end();
    }

    /* ---------------------------------------------------------------- */
    /* Lire la cartouche (plan §4.3)                                      */
    /* ---------------------------------------------------------------- */

    bool appliquer(const Sequence &s) {
        for (uint8_t i = 0; i < s.n; ++i)
            if (!fente_.ecrire(s.e[i].adr, s.e[i].val)) return false;
        return true;
    }

    bool lire_bloc(uint16_t adr, uint8_t *dst, uint32_t n, bool demi_octets = false) {
        for (uint32_t i = 0; i < n; ++i) {
            uint8_t v;
            if (!fente_.lire((uint16_t)(adr + i), v)) return false;
            dst[i] = demi_octets ? (uint8_t)(v & 0x0F) : v;
        }
        return true;
    }

    Lecture lire_tout(uint8_t &refus) {
        uint8_t en_tete[FIN_ENTETE];
        Verdict v = Verdict::LogoFaux;
        for (int essai = 0; essai < 3; ++essai) {
            fente_.alimenter(true);
            if (!lire_bloc(0x0000, en_tete, FIN_ENTETE)) return Lecture::Partie;
            v = juger_entete(en_tete);
            if (v == Verdict::Bon || v == Verdict::NonPrisEnCharge) break;
            fente_.alimenter(false);
        }
        if (v != Verdict::Bon) {
            fente_.alimenter(false);
            refus = v == Verdict::NonPrisEnCharge ? CAR1_NON_PRISE_EN_CHARGE : CAR1_CONTACTS_SALES;
            return Lecture::Refusee;
        }
        entete_ = lire_entete(en_tete);
        cfg_ = Config::depuis(entete_);

        /* La ROM, deux fois si la somme globale ne tombe pas juste : certaines
         * cartouches ont une somme fausse, deux lectures identiques tranchent. */
        uint32_t crc_precedent = 0;
        for (int essai = 0; essai < 3; ++essai) {
            if (!lire_rom()) return Lecture::Partie;
            if (calculer_somme_globale(rom_.data(), rom_.size()) == entete_.somme_globale) break;
            const uint32_t crc = crc32(0, rom_.data(), rom_.size());
            if (essai > 0 && crc == crc_precedent) break;
            crc_precedent = crc;
            if (essai == 2) {
                fente_.alimenter(false);
                refus = CAR1_CONTACTS_SALES;
                return Lecture::Refusee;
            }
        }

        miroir_.assign(cfg_.octets_ram, 0);
        for (uint32_t b = 0; b < banques_ram(cfg_); ++b) {
            if (!appliquer(selection_ram(cfg_, b))) return Lecture::Partie;
            const uint32_t n = cfg_.mbc == Mbc::Mbc2 ? RAM_MBC2 : 0x2000u;
            if (!lire_bloc(0xA000, miroir_.data() + b * 0x2000u, n, cfg_.mbc == Mbc::Mbc2)) return Lecture::Partie;
        }
        horloge_lue_ = false;
        if (cfg_.horloge) {
            if (!appliquer(verrouillage_horloge())) return Lecture::Partie;
            for (uint8_t r = 0; r < RTC_NB; ++r) {
                if (!fente_.ecrire(0x4000, (uint8_t)(0x08 + r))) return Lecture::Partie;
                if (!fente_.lire(0xA000, horloge_[r])) return Lecture::Partie;
            }
            horloge_lue_ = true;
            t_horloge_ = maintenant_;
        }
        if (!appliquer(fermeture_ram(cfg_))) return Lecture::Partie;
        fente_.alimenter(false);
        return Lecture::Bonne;
    }

    bool lire_rom() {
        rom_.assign(cfg_.octets_rom, 0);
        for (uint32_t b = 0; b < banques_rom(cfg_); ++b) {
            const Sequence s = selection_rom(cfg_, b);
            if (!appliquer(s)) return false;
            if (!lire_bloc(s.fenetre, rom_.data() + b * 0x4000u, 0x4000u)) return false;
        }
        return true;
    }

    /* ---------------------------------------------------------------- */
    /* Écrire dans la cartouche                                           */
    /* ---------------------------------------------------------------- */

    bool ecrire_page(uint16_t p, const Page &d) {
        const uint32_t off = (uint32_t)p * OCTETS_PAGE;
        const uint32_t n = std::min<uint32_t>(OCTETS_PAGE, (uint32_t)miroir_.size() - off);
        const bool mbc2 = cfg_.mbc == Mbc::Mbc2;
        const uint32_t banque = mbc2 ? 0 : off / 0x2000u;
        const uint16_t base = (uint16_t)(0xA000u + (mbc2 ? off : off % 0x2000u));
        if (!appliquer(selection_ram(cfg_, banque))) return false;
        for (uint32_t i = 0; i < n; ++i)
            if (!fente_.ecrire((uint16_t)(base + i), d[i])) return false;
        for (uint32_t i = 0; i < n; ++i) { /* relire : confirmer */
            uint8_t v;
            if (!fente_.lire((uint16_t)(base + i), v)) return false;
            if ((mbc2 ? (uint8_t)(v & 0x0F) : v) != d[i]) return false;
        }
        return true;
    }

    PageParquee *parquee(uint16_t p) {
        for (auto &pp : m_.ctx.parquees)
            if (pp.page == p) return &pp;
        return nullptr;
    }
    const PageParquee *parquee(uint16_t p) const {
        for (const auto &pp : m_.ctx.parquees)
            if (pp.page == p) return &pp;
        return nullptr;
    }

    /* Parque une page : elle attend la cartouche, ou son écriture. */
    PageParquee &parquer(uint16_t p, const Page &d, uint32_t origine) {
        PageParquee *pp = parquee(p);
        if (!pp) {
            m_.ctx.parquees.push_back(PageParquee{});
            pp = &m_.ctx.parquees.back();
            pp->page = p;
        }
        pp->neuf = d;
        pp->origine = origine;
        return *pp;
    }

    /* Écrit un lot de pages dans la cartouche présente. Avant d'en toucher
     * une, toutes sont notées en flash comme parquées et tentées : un
     * redémarrage ou un retrait au milieu laisse un état que compatible() sait
     * juger. Chaque page confirmée quitte le parquage. */
    bool ecrire_lot(const std::vector<PageParquee> &lot) {
        for (const PageParquee &x : lot) {
            PageParquee &pp = parquer(x.page, x.neuf, x.origine);
            if (!pp.tentee) {
                pp.tentee = true;
                std::copy_n(miroir_.begin() + x.page * OCTETS_PAGE,
                            std::min<size_t>(OCTETS_PAGE, miroir_.size() - x.page * OCTETS_PAGE), pp.ancien.begin());
            }
            if (pp.tentes.size() < 4) pp.tentes.push_back(x.neuf);
        }
        stockage_.sauver(m_);
        fente_.alimenter(true);
        bool ok = true;
        for (const PageParquee &x : lot) {
            if (observateur) observateur->page_ecrite(x.page, x.origine);
            if (!ecrire_page(x.page, x.neuf)) {
                ok = false;
                break;
            }
            std::copy_n(x.neuf.begin(), std::min<size_t>(OCTETS_PAGE, miroir_.size() - x.page * OCTETS_PAGE),
                        miroir_.begin() + x.page * OCTETS_PAGE);
            m_.ctx.crc_pages[x.page] = crc_page_miroir(x.page);
            auto &v = m_.ctx.parquees;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const PageParquee &q) { return q.page == x.page; }),
                    v.end());
        }
        if (ok) appliquer(fermeture_ram(cfg_));
        if (!vibreur_) fente_.alimenter(false);
        if (!ok) m_.ctx.presente = false;
        crc_miroir_ = crc32(0, miroir_.data(), miroir_.size());
        stockage_.sauver(m_);
        return ok;
    }

    uint32_t crc_page_miroir(uint16_t p) const {
        const uint32_t off = (uint32_t)p * OCTETS_PAGE;
        return crc32(0, miroir_.data() + off, std::min<uint32_t>(OCTETS_PAGE, (uint32_t)miroir_.size() - off));
    }

    /* ---------------------------------------------------------------- */
    /* Le journal de la doublure                                          */
    /* ---------------------------------------------------------------- */

    Flux &flux(uint32_t session, uint32_t base) {
        auto &v = m_.flux;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].session == session) {
                Flux f = v[i]; /* le plus récent en fin */
                v.erase(v.begin() + (long)i);
                v.push_back(f);
                return v.back();
            }
        /* Session inconnue : elle commence à `base`, le plus vieux numéro que
         * la doublure n'a pas vu accusé. */
        v.push_back(Flux{session, base ? base - 1 : 0});
        if (v.size() > p_.flux_max) v.erase(v.begin());
        return v.back();
    }

    void recevoir_ecriture(const car1_entete &e, const uint8_t *c, size_t lg) {
        car1_ecriture w;
        if (!extraire(c, lg, w)) return;
        Flux &f = flux(w.session, w.base);
        const uint32_t session = w.session;
        if (w.numero <= f.dernier) {
            accuser(session, f.dernier, CAR1_APPLIQUEE);
            return;
        }
        if (w.numero != f.dernier + 1) { /* en avance : la doublure renverra dans l'ordre */
            accuser(session, f.dernier, CAR1_APPLIQUEE);
            return;
        }
        Contexte &ctx = m_.ctx;
        const bool sienne = ctx.valide && std::find(ctx.insertions.begin(), ctx.insertions.end(), e.insertion) !=
                                              ctx.insertions.end();
        uint8_t sort = CAR1_OUBLIEE;
        if (sienne) {
            const uint8_t *q = c + sizeof w;
            const size_t reste = lg - sizeof w;
            if (w.genre == CAR1_PAGES) {
                std::vector<PageParquee> lot;
                bool forme = w.nombre <= CAR1_PAGES_MAX && reste >= (size_t)w.nombre * (2 + OCTETS_PAGE);
                for (uint8_t i = 0; forme && i < w.nombre; ++i) {
                    PageParquee x;
                    std::memcpy(&x.page, q + i * (2 + OCTETS_PAGE), 2);
                    std::memcpy(x.neuf.data(), q + i * (2 + OCTETS_PAGE) + 2, OCTETS_PAGE);
                    x.origine = e.insertion;
                    if (x.page >= ctx.crc_pages.size()) forme = false;
                    lot.push_back(x);
                }
                if (!forme) return; /* paquet abîmé : pas d'accusé, il reviendra */
                if (ctx.presente && etat_ == CAR1_PRETE) {
                    sort = CAR1_APPLIQUEE;
                    if (!ecrire_lot(lot)) {
                        sort = CAR1_PARQUEE;
                        relire_plus_tard();
                    }
                } else {
                    for (const PageParquee &x : lot) parquer(x.page, x.neuf, x.origine);
                    sort = CAR1_PARQUEE;
                }
            } else if (w.genre == CAR1_REGLAGE_HORLOGE && reste >= RTC_NB) {
                /* Jamais parqué : rejoué plus tard, il remettrait l'heure en arrière. */
                if (ctx.presente && etat_ == CAR1_PRETE && cfg_.horloge) regler_horloge(q);
                sort = ctx.presente ? CAR1_APPLIQUEE : CAR1_OUBLIEE;
            } else if (w.genre == CAR1_VIBREUR && reste >= 1) {
                if (ctx.presente && etat_ == CAR1_PRETE && cfg_.vibreur) commander_vibreur(q[0] != 0);
                sort = ctx.presente ? CAR1_APPLIQUEE : CAR1_OUBLIEE;
            }
        }
        f.dernier = w.numero;
        stockage_.sauver(m_);
        accuser(session, w.numero, sort);
    }

    void regler_horloge(const uint8_t *r) {
        fente_.alimenter(true);
        bool ok = fente_.ecrire(0x0000, 0x0A);
        ok = ok && fente_.ecrire(0x4000, 0x08 + RTC_DH) && fente_.ecrire(0xA000, (uint8_t)(r[RTC_DH] | RTC_ARRET));
        for (uint8_t i = RTC_S; ok && i <= RTC_DL; ++i)
            ok = fente_.ecrire(0x4000, (uint8_t)(0x08 + i)) && fente_.ecrire(0xA000, r[i]);
        ok = ok && fente_.ecrire(0x4000, 0x08 + RTC_DH) && fente_.ecrire(0xA000, r[RTC_DH]);
        if (ok) appliquer(fermeture_ram(cfg_));
        if (!vibreur_) fente_.alimenter(false);
    }

    /* Le moteur vit dans la cartouche : la fente reste alimentée tant qu'il tourne (plan §3.3). */
    void commander_vibreur(bool marche) {
        fente_.alimenter(true);
        appliquer(commande_vibreur(marche));
        vibreur_ = marche;
        if (!marche) fente_.alimenter(false);
    }

    void accuser(uint32_t session, uint32_t numero, uint8_t sort) {
        car1_accuse a{};
        a.session = session;
        a.numero = numero;
        a.sort = sort;
        reseau_.envoyer(emballer(CAR1_ACCUSE, p_.appairage, insertion_, &a, sizeof a));
    }

    /* ---------------------------------------------------------------- */
    /* La copie                                                           */
    /* ---------------------------------------------------------------- */

    void recevoir_demande(const car1_entete &e, const uint8_t *c, size_t lg) {
        car1_demande dm;
        if (!extraire(c, lg, dm)) return;
        if (e.insertion != insertion_ || etat_ != CAR1_PRETE) return;
        if (dm.nombre > CAR1_DEMANDE_MAX || lg < sizeof dm + dm.nombre * 2u) return;
        const std::vector<uint8_t> &src = dm.zone == CAR1_ZONE_ROM ? rom_ : miroir_;
        for (uint8_t i = 0; i < dm.nombre; ++i) {
            uint16_t idx;
            std::memcpy(&idx, c + sizeof dm + i * 2u, 2);
            const size_t off = (size_t)idx * CAR1_OCTETS_BLOC;
            if (off >= src.size()) continue;
            car1_bloc b{};
            b.zone = dm.zone;
            b.index = idx;
            b.octets = (uint16_t)std::min<size_t>(CAR1_OCTETS_BLOC, src.size() - off);
            reseau_.envoyer(emballer(CAR1_BLOC, p_.appairage, insertion_, &b, sizeof b, src.data() + off, b.octets));
        }
    }

    void annoncer() {
        derniere_annonce_ = maintenant_;
        car1_annonce a{};
        a.etat = etat_;
        if (etat_ == CAR1_PRETE) {
            a.code_type = entete_.code_type;
            a.code_rom = entete_.code_rom;
            a.code_ram = entete_.code_ram;
            a.somme_entete = entete_.somme_entete;
            a.somme_globale = entete_.somme_globale;
            std::memcpy(a.titre, entete_.titre, 16);
            a.octets_rom = (uint32_t)rom_.size();
            a.octets_ram = (uint32_t)miroir_.size();
            a.crc_rom = m_.ctx.signature.crc_rom;
            a.crc_sauvegarde = crc_miroir_;
            a.horloge_lue = horloge_lue_;
            std::memcpy(a.horloge, horloge_, RTC_NB);
            a.age_horloge_ms = horloge_lue_ ? maintenant_ - t_horloge_ : 0;
        }
        reseau_.envoyer(emballer(CAR1_ANNONCE, p_.appairage, insertion_, &a, sizeof a));
    }

    FenteES &fente_;
    ReseauES &reseau_;
    StockageLecteurES &stockage_;
    ParametresLecteur p_;

    MemoireLecteur m_;
    uint32_t insertion_ = 1;
    uint8_t etat_ = CAR1_FENTE_VIDE;
    bool vue_ = false;
    bool lue_ = false;
    uint32_t t_vue_ = 0;
    uint32_t crc_miroir_ = 0;
    uint32_t maintenant_ = 0;
    uint32_t derniere_annonce_ = 0;

    Entete entete_{};
    Config cfg_;
    std::vector<uint8_t> rom_;    /* la copie ; dans le firmware, en flash ou relue (voir README) */
    std::vector<uint8_t> miroir_; /* la sauvegarde de la cartouche en place, tenue à jour */
    uint8_t horloge_[RTC_NB] = {};
    bool horloge_lue_ = false;
    uint32_t t_horloge_ = 0;
    bool vibreur_ = false;
};

} // namespace cartouche
