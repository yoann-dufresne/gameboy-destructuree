/**
 * La sauvegarde, de bout en bout, sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. simulation.cpp -o /tmp/simulation && /tmp/simulation
 *     /tmp/simulation --tirages 5000    # plus de tirages aléatoires
 *     /tmp/simulation --verbeux         # le récit de chaque scénario écrit
 *
 * Le monde simulé : des cartouches (ROM, RAM avec pile, contrôleur), la fente
 * du lecteur, le lecteur et la doublure tels qu'ils tourneront dans les
 * firmwares (lecteur.hpp, doublure.hpp), un réseau qui perd, double et
 * retarde, une console qu'on allume et éteint, et un jeu qui sauvegarde.
 * Le temps avance d'une milliseconde à la fois.
 *
 * Les règles de Yoann, vérifiées à chaque instant ou à la fin :
 *   R1  la cartouche fait foi : une copie prête porte la sauvegarde de la
 *       cartouche, telle qu'elle est ;
 *   R2  jamais d'écriture sur la mauvaise cartouche : une page n'est écrite que
 *       dans la cartouche d'où vient la copie qui l'a produite, et seulement si
 *       cette cartouche n'a pas été jouée ailleurs depuis ;
 *   R3  rien de perdu, sauf les cas que le plan admet : une autre cartouche
 *       passée avant le retour de la sienne, une cartouche jouée ailleurs, une
 *       doublure privée de courant pendant que le jeu tourne ou que son journal
 *       n'est pas en flash ;
 *   R4  rien de bloqué : à la fin, le journal de la doublure est vide.
 * S'y ajoute une cohérence interne : le miroir du lecteur est la RAM de la
 * cartouche en place.
 *
 * Deux règles de comparaison à la réinsertion sont éprouvées (lecteur.hpp) :
 * celle du plan, et la règle stricte proposée.
 */

#include "doublure.hpp"
#include "lecteur.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <vector>

using namespace cartouche;

namespace {

constexpr uint32_t K = 1024;
constexpr uint32_t APPAIRAGE = 0x5A17C0DE;

/* ======================================================================== */
/* Les cartouches                                                            */
/* ======================================================================== */

struct Cart {
    int id = 0;
    std::string nom;
    std::vector<uint8_t> rom, ram;
    Config cfg;
    Controleur puce;
    uint32_t generation = 0; /* +1 chaque fois qu'on la joue ailleurs */
};

std::vector<uint8_t> fabriquer_rom(std::mt19937 &h, uint8_t code_type, uint8_t code_rom, uint8_t code_ram,
                                   const char *titre) {
    std::vector<uint8_t> rom(taille_rom(code_rom));
    for (auto &o : rom) o = (uint8_t)h();
    std::memcpy(rom.data() + ADR_LOGO, LOGO, sizeof LOGO);
    std::memset(rom.data() + ADR_TITRE, 0, 16);
    std::memcpy(rom.data() + ADR_TITRE, titre, std::min<size_t>(15, std::strlen(titre)));
    rom[ADR_CGB] = 0x00;
    rom[ADR_TYPE] = code_type;
    rom[ADR_TAILLE_ROM] = code_rom;
    rom[ADR_TAILLE_RAM] = code_ram;
    rom[ADR_SOMME_ENTETE] = calculer_somme_entete(rom.data());
    const uint16_t g = calculer_somme_globale(rom.data(), rom.size());
    rom[ADR_SOMME_GLOBALE] = (uint8_t)(g >> 8);
    rom[ADR_SOMME_GLOBALE + 1] = (uint8_t)g;
    return rom;
}

std::unique_ptr<Cart> fabriquer_cart(int id, const std::string &nom, const std::vector<uint8_t> &rom,
                                     std::mt19937 &h) {
    auto c = std::make_unique<Cart>();
    c->id = id;
    c->nom = nom;
    c->rom = rom;
    const Entete e = lire_entete(rom.data());
    c->cfg = Config::depuis(e);
    c->puce = Controleur(c->cfg);
    c->ram.resize(c->cfg.octets_ram);
    for (auto &o : c->ram) o = (uint8_t)h(); /* une SRAM jamais écrite porte n'importe quoi */
    if (c->cfg.mbc == Mbc::Mbc2)
        for (auto &o : c->ram) o &= 0x0F;
    return c;
}

/* ======================================================================== */
/* Le monde                                                                  */
/* ======================================================================== */

struct Verite {
    std::vector<uint8_t> attendu; /* ce que la cartouche doit finir par porter */
    bool perte_admise = false;
    std::string raison;
};

struct Monde : ObservateurLecteur {
    Regle regle;
    bool verbeux = false;
    std::mt19937 h;
    uint32_t t = 1000;

    std::vector<std::unique_ptr<Cart>> carts;
    Cart *dans_fente = nullptr;
    std::vector<Cart *> dehors;

    /* ---- La fente ---- */
    struct Fente : FenteES {
        Monde *w = nullptr;
        bool alim = false;
        long ops_avant_arrachage = -1;
        bool presente() override { return w->dans_fente != nullptr; }
        void alimenter(bool m) override {
            if (alim && !m && w->dans_fente) w->dans_fente->puce.reinitialiser();
            alim = m;
        }
        bool op() {
            if (!w->dans_fente) return false;
            if (ops_avant_arrachage == 0) {
                ops_avant_arrachage = -1;
                w->noter("  la cartouche est arrachée au milieu d'une opération du lecteur");
                w->retirer_physiquement();
                return false;
            }
            if (ops_avant_arrachage > 0) --ops_avant_arrachage;
            return true;
        }
        bool lire(uint16_t adr, uint8_t &v) override {
            if (!op()) return false;
            Cart &c = *w->dans_fente;
            v = 0xFF;
            if (!alim) return true;
            if (adr < 0x8000) {
                v = c.rom[c.puce.decalage_rom(adr)];
            } else if (adr >= 0xA000 && adr < 0xC000) {
                const AccesRam a = c.puce.acces_ram(adr);
                if (a.genre == AccesRam::Ram) v = c.cfg.mbc == Mbc::Mbc2 ? (uint8_t)(c.ram[a.decalage] | 0xF0) : c.ram[a.decalage];
                else if (a.genre == AccesRam::Horloge) v = c.puce.horloge.lire(a.registre);
            }
            return true;
        }
        bool ecrire(uint16_t adr, uint8_t v) override {
            if (!op()) return false;
            Cart &c = *w->dans_fente;
            if (!alim) return true;
            if (adr < 0x8000) {
                c.puce.ecrire_registre(adr, v);
            } else if (adr >= 0xA000 && adr < 0xC000) {
                const AccesRam a = c.puce.acces_ram(adr);
                if (a.genre == AccesRam::Ram) c.ram[a.decalage] = c.cfg.mbc == Mbc::Mbc2 ? (uint8_t)(v & 0x0F) : v;
                else if (a.genre == AccesRam::Horloge) c.puce.horloge.ecrire(a.registre, v);
            }
            return true;
        }
    } fente;

    /* ---- Le réseau ---- */
    struct Envoi {
        uint32_t t;
        uint64_t ordre;
        int vers; /* 0 : le lecteur, 1 : la doublure */
        std::vector<uint8_t> d;
        bool operator>(const Envoi &o) const { return t != o.t ? t > o.t : ordre > o.ordre; }
    };
    std::priority_queue<Envoi, std::vector<Envoi>, std::greater<Envoi>> file;
    uint64_t ordre = 0;
    double perte = 0.0, doublon = 0.0;
    uint32_t retard_min = 1, retard_max = 5;
    bool coupe = false;

    void poster(int vers, const std::vector<uint8_t> &d) {
        if (coupe) return;
        std::uniform_real_distribution<double> u(0, 1);
        const int fois = u(h) < doublon ? 2 : 1;
        for (int i = 0; i < fois; ++i) {
            if (u(h) < perte) continue;
            const uint32_t r = retard_min + (uint32_t)(h() % (retard_max - retard_min + 1));
            file.push(Envoi{t + r, ordre++, vers, d});
        }
    }

    struct VersDoublure : ReseauES {
        Monde *w = nullptr;
        void envoyer(const std::vector<uint8_t> &p) override { w->poster(1, p); }
    } vers_doublure;

    /* ---- Les flashs ---- */
    struct StockLecteur : StockageLecteurES {
        MemoireLecteur m;
        bool a = false;
        void sauver(const MemoireLecteur &x) override {
            m = x;
            a = true;
        }
        bool charger(MemoireLecteur &x) override {
            if (a) x = m;
            return a;
        }
    } stock_lecteur;

    struct ESDoublure : DoublureES {
        Monde *w = nullptr;
        std::vector<std::pair<Signature, std::vector<uint8_t>>> roms;
        std::deque<EntreeJournal> j;
        bool a = false;
        void envoyer(const std::vector<uint8_t> &p) override { w->poster(0, p); }
        bool rom_connue(const Signature &s, std::vector<uint8_t> &rom) override {
            for (auto &r : roms)
                if (r.first == s) {
                    rom = r.second;
                    return true;
                }
            return false;
        }
        void rom_ranger(const Signature &s, const std::vector<uint8_t> &rom) override {
            std::vector<uint8_t> x;
            if (!rom_connue(s, x)) roms.emplace_back(s, rom);
        }
        void journal_sauver(const std::deque<EntreeJournal> &x) override {
            if (w->console) w->violation("la doublure a écrit sa flash console allumée");
            j = x;
            a = true;
        }
        bool journal_charger(std::deque<EntreeJournal> &x) override {
            if (a) x = j;
            return a;
        }
        void journal_effacer() override {
            j.clear();
            a = false;
        }
    } es_doublure;

    std::unique_ptr<Lecteur> lecteur;
    std::unique_ptr<Doublure> doublure;

    /* ---- La console et l'injecteur ---- */
    bool console = false;
    bool veut_allumer = false;
    uint32_t t_demande = 0;

    /* ---- La vérité ---- */
    std::map<int, Verite> verite;
    std::map<uint32_t, std::pair<int, uint32_t>> insertions; /* insertion → (cartouche, génération) */
    int cart_du_contexte = -1;
    bool etait_vivante = false;
    uint32_t servie_avant = 0;
    bool prete_avant = false;
    uint32_t copie_avant = 0;

    /* ---- Les constats ---- */
    std::vector<std::string> violations;
    std::vector<std::string> recit;
    int pages_mauvaise_cartouche = 0;

    void noter(const std::string &s) {
        if (verbeux) recit.push_back("[" + std::to_string(t) + " ms] " + s);
    }
    void violation(const std::string &s) {
        for (const auto &v : violations) /* chaque constat une fois */
            if (v.substr(v.find("] ") + 2) == s) return;
        violations.push_back("[" + std::to_string(t) + " ms] " + s);
    }

    /* ---- Le jeu ---- */
    std::deque<std::pair<uint32_t, uint8_t>> a_ecrire;

    explicit Monde(Regle r, uint32_t graine, bool v = false) : regle(r), verbeux(v), h(graine) {
        fente.w = this;
        vers_doublure.w = this;
        es_doublure.w = this;
        demarrer_lecteur();
        demarrer_doublure();
    }

    Cart *ajouter_cart(std::unique_ptr<Cart> c) {
        Cart *p = c.get();
        verite[p->id].attendu = p->ram;
        carts.push_back(std::move(c));
        dehors.push_back(p);
        return p;
    }

    Cart *cart(int id) {
        for (auto &c : carts)
            if (c->id == id) return c.get();
        return nullptr;
    }

    void demarrer_lecteur() {
        ParametresLecteur p;
        p.appairage = APPAIRAGE;
        p.regle = regle;
        lecteur = std::make_unique<Lecteur>(fente, vers_doublure, stock_lecteur, p);
        lecteur->observateur = this;
        lecteur->demarrer(t, (uint32_t)h());
    }

    void demarrer_doublure() {
        ParametresDoublure p;
        p.appairage = APPAIRAGE;
        doublure = std::make_unique<Doublure>(es_doublure, p);
        doublure->demarrer(t, (uint32_t)h());
        if (console) doublure->console(true, t); /* elle trouve la console allumée, sans copie */
        etait_vivante = false;
        observer();
    }

    /* ---- ObservateurLecteur ---- */
    void page_ecrite(uint16_t page, uint32_t origine) override {
        auto it = insertions.find(origine);
        if (it == insertions.end()) {
            violation("page écrite d'une copie inconnue");
            return;
        }
        Cart *c = dans_fente;
        const Cart *source = cart(it->second.first);
        if (!c || c->id != it->second.first || c->generation != it->second.second) {
            ++pages_mauvaise_cartouche;
            char m[200];
            std::snprintf(m, sizeof m, "R2 : page %u de la copie de « %s » (gén. %u) écrite dans « %s » (gén. %u)", page,
                          source ? source->nom.c_str() : "?", it->second.second, c ? c->nom.c_str() : "rien",
                          c ? c->generation : 0);
            violation(m);
        }
    }
    void contexte_remplace(bool avait) override {
        noter(std::string("  le lecteur oublie le contexte précédent") + (avait ? ", et sa sauvegarde parquée" : ""));
        if (cart_du_contexte >= 0 && en_cours(cart_du_contexte))
            admettre_perte(cart_du_contexte, "une autre cartouche est passée avant son retour");
    }

    /* Des écritures pour cette cartouche sont-elles encore en route ? */
    bool en_cours(int id) {
        Cart *c = cart(id);
        if (verite[id].attendu != c->ram) return true;
        if (doublure->vivante() && carte_de(doublure->insertion_servie()) == id) return true;
        for (const EntreeJournal &j : doublure->journal())
            if (carte_de(j.insertion) == id) return true;
        return false;
    }
    int carte_de(uint32_t insertion) {
        auto it = insertions.find(insertion);
        return it == insertions.end() ? -1 : it->second.first;
    }
    void admettre_perte(int id, const std::string &raison) {
        if (!verite[id].perte_admise) noter("  perte admise pour « " + cart(id)->nom + " » : " + raison);
        verite[id].perte_admise = true;
        verite[id].raison = raison;
    }

    /* Après chaque événement : les transitions que la vérité doit suivre. */
    void observer() {
        /* Le lecteur vient de lire une cartouche : à qui est ce numéro d'insertion ? */
        if (lecteur->etat() == CAR1_PRETE && dans_fente) {
            const uint32_t ins = lecteur->insertion();
            if (!insertions.count(ins)) {
                insertions[ins] = {dans_fente->id, dans_fente->generation};
                cart_du_contexte = dans_fente->id;
                noter("  le lecteur a lu « " + dans_fente->nom + " », insertion " + std::to_string(ins));
            }
            if (verifier_miroir && lecteur->miroir() != dans_fente->ram)
                violation("le miroir du lecteur n'est pas la RAM de la cartouche");
        }
        /* Le jeu s'arrête de tourner sur une copie : sa sauvegarde, à cet instant, est celle que la cartouche doit finir par porter. */
        const bool vivante = doublure->vivante();
        if (etait_vivante && !vivante) {
            const int id = carte_de(servie_avant);
            if (id >= 0) {
                Cart *c = cart(id);
                const bool meme_generation = insertions[servie_avant].second == c->generation;
                const auto &ctx = lecteur->memoire().ctx;
                const bool contexte_sien = std::find(ctx.insertions.begin(), ctx.insertions.end(), servie_avant) !=
                                           ctx.insertions.end();
                if (!meme_generation) admettre_perte(id, "jouée ailleurs pendant que la console jouait sa copie");
                else if (!contexte_sien) admettre_perte(id, "une autre cartouche est passée pendant que la console jouait sa copie");
                else verite[id].attendu = doublure->ram_vivante();
                noter(std::string("  la console ne joue plus sur la copie de « ") + c->nom + " » (" +
                      (doublure->muette() ? "figée" : "éteinte") + ")");
            }
        }
        etait_vivante = vivante;
        servie_avant = doublure->insertion_servie();
        /* R1 : une copie qui devient prête porte la sauvegarde de la cartouche. */
        if (doublure->copie_prete() && (!prete_avant || doublure->insertion_copie() != copie_avant)) {
            const uint32_t ins = doublure->insertion_copie();
            auto it = insertions.find(ins);
            if (it != insertions.end()) {
                Cart *c = cart(it->second.first);
                if (c->generation == it->second.second && doublure->sauvegarde_copie() != c->ram)
                    violation("R1 : la copie prête ne porte pas la sauvegarde de la cartouche « " + c->nom + " »");
                noter("  copie prête : « " + c->nom + " »");
            }
        }
        prete_avant = doublure->copie_prete();
        copie_avant = doublure->insertion_copie();
    }

    /* ---- Une milliseconde ---- */
    bool verifier_miroir = false;
    void pas() {
        while (!file.empty() && file.top().t <= t) {
            Envoi e = file.top();
            file.pop();
            verifier_miroir = e.vers == 0; /* le miroir ne change que sur un paquet reçu, ou une lecture */
            if (e.vers == 0) lecteur->recevoir(e.d.data(), e.d.size(), t);
            else doublure->recevoir(e.d.data(), e.d.size(), t);
            observer();
            verifier_miroir = false;
        }
        const uint8_t etat_avant = lecteur->etat();
        lecteur->tic(t);
        verifier_miroir = lecteur->etat() != etat_avant || t % 100 == 0;
        observer();
        verifier_miroir = false;
        doublure->tic(t);
        observer();
        if (veut_allumer && !console && (doublure->feu_vert() || t - t_demande > 15000)) {
            console = true;
            veut_allumer = false;
            doublure->console(true, t);
            noter(doublure->vivante() ? "  la console démarre sur le jeu" : "  la console démarre : rectangle noir");
            observer();
        }
        jouer();
        ++t;
    }

    void attendre(uint32_t ms) {
        for (uint32_t i = 0; i < ms; ++i) pas();
    }

    /* ---- Le jeu ---- */
    bool sauvegardes_auto = true;
    double p_sauvegarde = 1.0 / 2500;
    const Cart *cart_jouee() {
        const int id = carte_de(doublure->insertion_servie());
        return id >= 0 ? cart(id) : nullptr;
    }

    void ecrire_console(uint16_t adr, uint8_t v) {
        doublure->console_ecrit(adr, v, t);
    }

    void ecrire_octet_ram(const Config &c, uint32_t off, uint8_t v) {
        switch (c.mbc) {
        case Mbc::Aucun: ecrire_console((uint16_t)(0xA000 + off), v); return;
        case Mbc::Mbc2:
            ecrire_console(0x0000, 0x0A);
            ecrire_console((uint16_t)(0xA000 + (off & 0x1FF)), (uint8_t)(v & 0x0F));
            return;
        case Mbc::Mbc1:
            ecrire_console(0x0000, 0x0A);
            ecrire_console(0x6000, 0x01);
            ecrire_console(0x4000, (uint8_t)(off / 0x2000));
            break;
        default:
            ecrire_console(0x0000, 0x0A);
            ecrire_console(0x4000, (uint8_t)(off / 0x2000));
            break;
        }
        ecrire_console((uint16_t)(0xA000 + off % 0x2000), v);
    }

    /* Une sauvegarde : la plupart du temps l'emplacement de sauvegarde (les
     * premières pages), parfois ailleurs ; une partie des octets change. */
    void planifier_sauvegarde(bool emplacement = true) {
        const Cart *c = cart_jouee();
        if (!c || c->cfg.octets_ram == 0) return;
        const std::vector<uint8_t> &ram = doublure->ram_vivante();
        const uint32_t pages = pages_de(c->cfg.octets_ram);
        std::vector<uint32_t> choix;
        if (emplacement) {
            for (uint32_t p = 0; p < std::min<uint32_t>(6, pages); ++p) choix.push_back(p);
        } else {
            for (int i = 0; i < 3; ++i) choix.push_back(h() % pages);
        }
        for (uint32_t p : choix)
            for (uint32_t i = 0; i < OCTETS_PAGE && p * OCTETS_PAGE + i < ram.size(); ++i) {
                const uint32_t off = p * OCTETS_PAGE + i;
                const uint8_t v = (h() % 10 < 3) ? (uint8_t)h() : ram[off];
                a_ecrire.emplace_back(off, v);
            }
    }

    void jouer() {
        if (!console) return;
        if (doublure->vivante()) {
            const Cart *c = cart_jouee();
            if (!c) return;
            if (sauvegardes_auto && a_ecrire.empty() && std::uniform_real_distribution<double>(0, 1)(h) < p_sauvegarde)
                planifier_sauvegarde(h() % 4 != 0);
            for (int k = 0; k < 64 && !a_ecrire.empty(); ++k) {
                ecrire_octet_ram(c->cfg, a_ecrire.front().first, a_ecrire.front().second);
                a_ecrire.pop_front();
            }
            if (h() % 3000 == 0) ecrire_console(0x2000, (uint8_t)h()); /* le jeu change de banque */
            if (c->cfg.horloge && h() % 20000 == 0) { /* le jeu règle l'horloge */
                ecrire_console(0x0000, 0x0A);
                ecrire_console(0x4000, 0x08 + RTC_H);
                ecrire_console(0xA000, (uint8_t)(h() % 24));
            }
            if (c->cfg.vibreur && h() % 2000 == 0) ecrire_console(0x4000, (uint8_t)(h() & 0x08));
            observer();
        } else if (doublure->sert() && doublure->muette()) {
            a_ecrire.clear();
            for (int k = 0; k < 16; ++k) { /* figé, le jeu écrit n'importe où */
                const uint16_t adr = (h() & 1) ? (uint16_t)(h() & 0x7FFF) : (uint16_t)(0xA000 + (h() & 0x1FFF));
                ecrire_console(adr, (uint8_t)h());
            }
        } else {
            a_ecrire.clear();
        }
    }

    /* ---- Les gestes ---- */
    void inserer(Cart *c) {
        if (dans_fente) {
            retirer();
            attendre(400); /* une main ne change pas de cartouche en une milliseconde */
        }
        dehors.erase(std::remove(dehors.begin(), dehors.end(), c), dehors.end());
        dans_fente = c;
        c->puce.reinitialiser();
        fente.alim = false;
        noter("on insère « " + c->nom + " »");
    }
    void retirer_physiquement() {
        fente.ops_avant_arrachage = -1; /* un arrachage armé ne vise que la cartouche en place */
        if (!dans_fente) return;
        dehors.push_back(dans_fente);
        dans_fente = nullptr;
    }
    void retirer() {
        if (!dans_fente) return;
        noter("on retire « " + dans_fente->nom + " »");
        retirer_physiquement();
    }
    void allumer() {
        if (console || veut_allumer) return;
        veut_allumer = true;
        t_demande = t;
        noter("on bascule l'interrupteur de la manette sur marche");
    }
    void eteindre() {
        veut_allumer = false;
        if (!console) return;
        noter("on éteint la console");
        console = false;
        doublure->console(false, t);
        observer();
    }
    void redemarrer_console() {
        eteindre();
        attendre(1000);
        allumer();
    }
    void wifi(bool marche) {
        coupe = !marche;
        noter(marche ? "le WiFi revient" : "le WiFi se coupe");
    }
    void redemarrer_lecteur() {
        noter("le lecteur redémarre");
        demarrer_lecteur();
        observer();
    }
    void couper_doublure() {
        noter("la doublure perd son courant");
        if (doublure->perte_si_coupure()) {
            if (doublure->vivante()) {
                const int id = carte_de(doublure->insertion_servie());
                if (id >= 0) admettre_perte(id, "la doublure a perdu son courant pendant que le jeu tournait");
            }
            for (const EntreeJournal &j : doublure->journal()) {
                const int id = carte_de(j.insertion);
                if (id >= 0) admettre_perte(id, "la doublure a perdu son courant, journal pas en flash");
            }
        }
        etait_vivante = false;
        demarrer_doublure();
    }
    /* On joue la cartouche sur une autre console : sa sauvegarde change. */
    void jouer_ailleurs(Cart *c, bool emplacement) {
        if (c == dans_fente) return;
        if (en_cours(c->id)) admettre_perte(c->id, "jouée ailleurs avant que sa sauvegarde ne lui revienne");
        const uint32_t pages = pages_de((uint32_t)c->ram.size());
        if (pages == 0) return;
        if (emplacement) {
            for (uint32_t p = 0; p < std::min<uint32_t>(6, pages); ++p)
                for (uint32_t i = 0; i < OCTETS_PAGE && p * OCTETS_PAGE + i < c->ram.size(); ++i)
                    if (h() % 10 < 3) c->ram[p * OCTETS_PAGE + i] = (uint8_t)h();
        } else {
            const uint32_t p = 6 + h() % std::max<uint32_t>(1, pages - 6);
            if (p < pages) c->ram[p * OCTETS_PAGE] ^= 0x5A;
            else c->ram[0] ^= 0x5A;
        }
        if (c->cfg.mbc == Mbc::Mbc2)
            for (auto &o : c->ram) o &= 0x0F;
        ++c->generation;
        verite[c->id].attendu = c->ram;
        noter("« " + c->nom + " » est jouée sur une autre console" + (emplacement ? ", dans son emplacement de sauvegarde" : ""));
    }

    /* La fin : réseau parfait, console éteinte, la cartouche du contexte
     * remise si elle est dehors, puis on laisse tout se poser. */
    void conclure() {
        coupe = false;
        perte = doublon = 0;
        fente.ops_avant_arrachage = -1;
        a_ecrire.clear();
        attendre(500);
        eteindre();
        attendre(3000);
        const auto &ctx = lecteur->memoire().ctx;
        if (!dans_fente && ctx.valide && cart_du_contexte >= 0) {
            inserer(cart(cart_du_contexte));
            attendre(4000);
        }
        attendre(3000);
        if (doublure->en_attente()) violation("R4 : le journal de la doublure ne se vide pas");
        for (auto &c : carts) {
            const Verite &v = verite[c->id];
            if (v.perte_admise) continue;
            if (c->ram != v.attendu) {
                size_t diff = 0;
                for (size_t i = 0; i < c->ram.size(); ++i) diff += c->ram[i] != v.attendu[i];
                violation("R3 : « " + c->nom + " » ne porte pas sa dernière sauvegarde (" + std::to_string(diff) +
                          " octets diffèrent)");
            }
        }
    }
};

/* ======================================================================== */
/* Les scénarios écrits                                                      */
/* ======================================================================== */

struct Jeux {
    std::vector<uint8_t> rom_a, rom_b, rom_c, rom_d;
    explicit Jeux(std::mt19937 &h) {
        rom_a = fabriquer_rom(h, 0x1B, 0x02, 0x03, "JEU A MBC5"); /* MBC5, 128 Kio, 32 Kio de RAM */
        rom_b = fabriquer_rom(h, 0x10, 0x02, 0x02, "JEU B MBC3");  /* MBC3 + horloge, 8 Kio */
        rom_c = fabriquer_rom(h, 0x03, 0x01, 0x03, "JEU C MBC1");  /* MBC1, 64 Kio, 32 Kio */
        rom_d = fabriquer_rom(h, 0x06, 0x01, 0x00, "JEU D MBC2");  /* MBC2, 512 demi-octets */
    }
};

struct Resultat {
    std::string nom;
    bool reussi;
    std::string detail;
};

using Scenario = std::function<std::string(Monde &, Jeux &)>; /* rend "" si l'attente est tenue */

std::string attendre_violations(Monde &w, size_t n_attendu, const std::string &quoi) {
    if (w.violations.size() == n_attendu) return "";
    std::string s = quoi + " : " + std::to_string(w.violations.size()) + " constat(s)";
    for (auto &v : w.violations) s += "\n      " + v;
    return s;
}

/* Une partie, une sauvegarde, la console éteinte : la sauvegarde arrive. */
std::string sc_sauvegarde_simple(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    if (!w.doublure->vivante()) return "la console n'a pas démarré sur le jeu";
    w.planifier_sauvegarde();
    w.attendre(3000);
    if (a->ram != w.doublure->ram_vivante()) return "la sauvegarde n'est pas arrivée en moins de 3 s";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Retirée 50 ms après une sauvegarde : la console se fige, la sauvegarde est
 * parquée, puis écrite au retour de la même cartouche. */
std::string sc_retrait_apres_sauvegarde(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150); /* les écritures du jeu : 1 536 octets à 64 par ms */
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    w.attendre(50);
    w.retirer();
    w.attendre(100);
    if (!w.doublure->muette()) return "la console ne s'est pas figée au retrait";
    if (a->ram == partie) return "la sauvegarde était déjà arrivée : le scénario ne teste rien";
    w.attendre(1000);
    if (w.lecteur->memoire().ctx.parquees.empty()) return "rien n'est parqué";
    w.inserer(a);
    w.attendre(2000);
    if (a->ram != partie) return "la sauvegarde parquée n'a pas été écrite au retour";
    w.redemarrer_console();
    w.attendre(3000);
    if (!w.doublure->vivante()) return "la console n'a pas redémarré sur le jeu";
    if (w.doublure->ram_vivante() != partie) return "la nouvelle copie n'a pas la partie";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Arrachée au milieu d'une écriture du lecteur : une page à moitié écrite. */
std::string sc_retrait_pendant_ecriture(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    w.fente.ops_avant_arrachage = 300; /* au milieu de la deuxième page du premier lot */
    w.attendre(1500);
    if (w.dans_fente) return "la cartouche n'a pas été arrachée";
    if (a->ram == partie) return "l'écriture était finie : le scénario ne teste rien";
    w.attendre(500);
    w.inserer(a);
    w.attendre(2000);
    if (a->ram != partie) return "la page à moitié écrite n'a pas été reprise";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* WiFi coupé, sauvegarde, console éteinte, doublure privée de courant puis
 * rallumée, WiFi rétabli : le journal sort de la flash et arrive (T4.8). */
std::string sc_journal_en_flash(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.wifi(false);
    w.planifier_sauvegarde();
    w.attendre(1500);
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    w.eteindre();
    w.attendre(200);
    if (!w.es_doublure.a) return "le journal n'a pas été écrit en flash";
    w.couper_doublure();
    w.attendre(500);
    w.wifi(true);
    w.attendre(3000);
    if (a->ram != partie) return "le journal rechargé n'est pas arrivé dans la cartouche";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Une autre cartouche passe avant le retour de la première : la sauvegarde
 * parquée est oubliée ; la seconde n'en reçoit rien. */
std::string sc_autre_cartouche(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    Cart *b = w.ajouter_cart(fabriquer_cart(2, "B", j.rom_b, w.h));
    const std::vector<uint8_t> b_avant = b->ram;
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    const std::vector<uint8_t> a_avant = a->ram;
    w.planifier_sauvegarde();
    w.attendre(150);
    w.retirer();
    w.attendre(1000);
    w.inserer(b);
    w.attendre(2000);
    if (!w.lecteur->memoire().ctx.parquees.empty()) return "la sauvegarde parquée n'est pas oubliée";
    w.inserer(a);
    w.attendre(2000);
    if (a->ram != a_avant) return "la cartouche A a changé alors que sa sauvegarde devait être oubliée";
    if (b->ram != b_avant) return "la cartouche B a reçu quelque chose";
    if (!w.verite[1].perte_admise) return "la simulation n'a pas compté la perte admise";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Jouée ailleurs, hors de son emplacement de sauvegarde : oubliée, aux deux règles. */
std::string sc_jouee_ailleurs_hors(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    w.retirer();
    w.attendre(1000);
    w.jouer_ailleurs(a, false);
    const std::vector<uint8_t> ailleurs = a->ram;
    w.inserer(a);
    w.attendre(2000);
    if (a->ram != ailleurs) return "la sauvegarde parquée a écrasé la partie jouée ailleurs";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Jouée ailleurs, et le jeu n'a réécrit que les pages parquées : la règle du
 * plan les compare pas, et écrase la partie jouée ailleurs. Le trou. */
std::string sc_jouee_ailleurs_dans_le_parquage(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    w.retirer();
    w.attendre(1000);
    w.jouer_ailleurs(a, true); /* même emplacement de sauvegarde que la partie parquée */
    const std::vector<uint8_t> ailleurs = a->ram;
    w.inserer(a);
    w.attendre(2000);
    const bool ecrasee = a->ram != ailleurs;
    if (w.regle == Regle::Plan) {
        if (!ecrasee) return "le trou attendu ne s'est pas montré";
        return w.pages_mauvaise_cartouche > 0 ? "" : "écrasée sans que R2 le voie";
    }
    if (ecrasee) return "la règle stricte a laissé écraser la partie jouée ailleurs";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Le lecteur redémarre, cartouche en place, journal en route : rien ne se perd. */
std::string sc_lecteur_redemarre(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    w.redemarrer_lecteur(); /* avant l'envoi du journal */
    w.attendre(3000);
    if (!w.doublure->muette()) return "la console devait se figer (nouveau numéro d'insertion)";
    if (a->ram != partie) return "le journal n'est pas arrivé après le redémarrage du lecteur";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Le lecteur redémarre au milieu d'une écriture : la flash dit quelles pages
 * étaient en vol, la règle stricte les accepte à moitié écrites. */
std::string sc_lecteur_redemarre_pendant_ecriture(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    /* On arrache au milieu d'une page, puis le lecteur redémarre avant le retour. */
    w.fente.ops_avant_arrachage = 300;
    w.attendre(1500);
    w.redemarrer_lecteur();
    w.attendre(500);
    w.inserer(a);
    w.attendre(3000);
    if (a->ram != partie) return "la sauvegarde n'est pas reprise après le redémarrage du lecteur";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Deux exemplaires du même jeu, à la sauvegarde identique : indiscernables.
 * La sauvegarde parquée du premier va dans le second. Limite connue. */
std::string sc_deux_exemplaires(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    Cart *a2 = w.ajouter_cart(fabriquer_cart(2, "A bis", j.rom_a, w.h));
    a2->ram = a->ram;
    w.verite[2].attendu = a2->ram;
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(150);
    w.retirer();
    w.attendre(1000);
    w.inserer(a2);
    w.attendre(2000);
    return w.pages_mauvaise_cartouche > 0 ? "" : "le second exemplaire n'a pas reçu la sauvegarde : la limite ne se montre pas";
}

/* Retirée pendant que le WiFi est coupé : la console continue, puis se fige au
 * retour du WiFi ; tout ce que le jeu a sauvegardé entre-temps est parqué. */
std::string sc_retrait_wifi_coupe(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.wifi(false);
    w.attendre(100);
    w.retirer();
    w.attendre(500);
    if (!w.doublure->vivante()) return "la console devait continuer : la doublure ne sait rien";
    w.planifier_sauvegarde();
    w.attendre(1500);
    const std::vector<uint8_t> partie = w.doublure->ram_vivante();
    w.wifi(true);
    w.attendre(1000);
    if (!w.doublure->muette()) return "la console ne s'est pas figée au retour du WiFi";
    w.inserer(a);
    w.attendre(3000);
    if (a->ram != partie) return "la partie jouée pendant la coupure n'est pas arrivée";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Figé, le jeu écrit n'importe quoi : rien n'en arrive à la cartouche. */
std::string sc_bruit_du_jeu_fige(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.sauvegardes_auto = false;
    w.inserer(a);
    w.attendre(2000);
    w.allumer();
    w.attendre(2000);
    w.planifier_sauvegarde();
    w.attendre(3000);
    const std::vector<uint8_t> partie = a->ram;
    w.retirer();
    w.attendre(3000); /* figée : 16 écritures au hasard par ms, 48 000 en tout */
    w.inserer(a);
    w.attendre(3000);
    if (a->ram != partie) return "le bruit du jeu figé est arrivé dans la cartouche";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* La console attend la copie ; sans cartouche, elle démarre aussitôt. */
std::string sc_feu_vert(Monde &w, Jeux &j) {
    Cart *a = w.ajouter_cart(fabriquer_cart(1, "A", j.rom_a, w.h));
    w.attendre(1500);
    w.allumer();
    w.attendre(10);
    if (!w.console) return "fente vide : la console devait démarrer aussitôt";
    if (w.doublure->vivante()) return "fente vide : la console ne doit rien servir";
    w.eteindre();
    w.inserer(a);
    w.attendre(100);
    w.allumer();
    w.attendre(50);
    if (w.console) return "la console a démarré avant la copie";
    w.attendre(3000);
    if (!w.doublure->vivante()) return "la console n'a pas démarré sur le jeu une fois la copie prête";
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* Les autres contrôleurs : MBC3 avec horloge, MBC1 à 32 Kio, MBC2. */
std::string sc_types(Monde &w, Jeux &j) {
    Cart *b = w.ajouter_cart(fabriquer_cart(2, "B (MBC3)", j.rom_b, w.h));
    Cart *c = w.ajouter_cart(fabriquer_cart(3, "C (MBC1)", j.rom_c, w.h));
    Cart *d = w.ajouter_cart(fabriquer_cart(4, "D (MBC2)", j.rom_d, w.h));
    w.sauvegardes_auto = false;
    for (Cart *x : {b, c, d}) {
        w.inserer(x);
        w.attendre(2500);
        w.allumer();
        w.attendre(2500);
        if (!w.doublure->vivante()) return "la console n'a pas démarré sur « " + x->nom + " »";
        w.planifier_sauvegarde(false);
        w.attendre(200);
        w.planifier_sauvegarde(true);
        w.attendre(2500);
        if (x->ram != w.doublure->ram_vivante()) return "« " + x->nom + " » : la sauvegarde n'est pas arrivée";
        w.eteindre();
        w.attendre(500);
    }
    w.conclure();
    return attendre_violations(w, 0, "aucun constat attendu");
}

/* ======================================================================== */
/* Les tirages aléatoires                                                    */
/* ======================================================================== */

struct Bilan {
    int tirages = 0, reussis = 0;
    std::map<std::string, int> par_regle;
    std::vector<std::string> exemples;
};

void tirage(Regle regle, uint32_t graine, Jeux &jeux, Bilan &b, bool verbeux = false) {
    Monde w(regle, graine, verbeux);
    std::mt19937 &h = w.h;
    const int n_carts = 2 + (int)(h() % 3);
    const std::vector<uint8_t> *roms[4] = {&jeux.rom_a, &jeux.rom_b, &jeux.rom_c, &jeux.rom_d};
    const char *noms[5] = {"A", "B", "C", "D", "A bis"};
    for (int i = 0; i < n_carts; ++i) {
        const int jeu = i == 4 ? 0 : (int)(h() % 4);
        w.ajouter_cart(fabriquer_cart(i + 1, std::string(noms[i]) + "#" + std::to_string(jeu), *roms[jeu], h));
    }
    w.perte = (h() % 3 == 0) ? 0.0 : (double)(h() % 20) / 100.0;
    w.doublon = (double)(h() % 5) / 100.0;
    w.retard_min = 1 + h() % 3;
    w.retard_max = w.retard_min + h() % 40;
    w.p_sauvegarde = 1.0 / (1000 + h() % 4000);

    const uint32_t duree = 40000 + h() % 40000;
    const uint32_t fin = w.t + duree;
    while (w.t < fin) {
        w.attendre(200 + h() % 3000);
        const int geste = (int)(h() % 100);
        if (geste < 22) {
            if (!w.dans_fente && !w.dehors.empty()) w.inserer(w.dehors[h() % w.dehors.size()]);
        } else if (geste < 34) {
            w.retirer();
        } else if (geste < 40) {
            if (w.dans_fente) w.fente.ops_avant_arrachage = (long)(h() % 3000);
        } else if (geste < 55) {
            w.allumer();
        } else if (geste < 63) {
            w.eteindre();
        } else if (geste < 68) {
            w.redemarrer_console();
        } else if (geste < 76) {
            w.wifi(false);
            w.attendre(500 + h() % 8000);
            w.wifi(true);
        } else if (geste < 84) {
            if (!w.dehors.empty()) w.jouer_ailleurs(w.dehors[h() % w.dehors.size()], h() % 2);
        } else if (geste < 88) {
            w.redemarrer_lecteur();
        } else if (geste < 92) {
            w.couper_doublure();
        } else {
            w.perte = (double)(h() % 25) / 100.0;
        }
    }
    w.conclure();
    if (verbeux) {
        for (const auto &l : w.recit) std::printf("  %s\n", l.c_str());
        for (const auto &v : w.violations) std::printf("  CONSTAT %s\n", v.c_str());
    }

    ++b.tirages;
    if (w.violations.empty()) {
        ++b.reussis;
        return;
    }
    for (const auto &v : w.violations) {
        const std::string cle = v.substr(v.find("] ") + 2, 2);
        ++b.par_regle[cle];
    }
    if (b.exemples.size() < 3) b.exemples.push_back("graine " + std::to_string(graine) + " : " + w.violations[0]);
}

} // namespace

int main(int argc, char **argv) {
    int tirages = 1000;
    bool verbeux = false;
    long graine_seule = -1;
    Regle regle_seule = Regle::Stricte;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--tirages") && i + 1 < argc) tirages = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--verbeux")) verbeux = true;
        else if (!std::strcmp(argv[i], "--graine") && i + 1 < argc) graine_seule = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--plan")) regle_seule = Regle::Plan;
    }
    if (graine_seule >= 0) { /* rejouer un tirage, avec son récit */
        std::mt19937 hj(42);
        Jeux jeux(hj);
        Bilan b;
        tirage(regle_seule, (uint32_t)graine_seule, jeux, b, true);
        return b.reussis == b.tirages ? 0 : 1;
    }

    const std::vector<std::pair<std::string, Scenario>> scenarios = {
        {"une sauvegarde arrive dans la cartouche", sc_sauvegarde_simple},
        {"retirée juste après une sauvegarde : parquée, puis écrite au retour", sc_retrait_apres_sauvegarde},
        {"arrachée au milieu d'une écriture du lecteur", sc_retrait_pendant_ecriture},
        {"WiFi coupé, console éteinte, doublure sans courant : le journal sort de la flash", sc_journal_en_flash},
        {"une autre cartouche passe avant le retour : sauvegarde parquée oubliée", sc_autre_cartouche},
        {"jouée ailleurs, hors de l'emplacement parqué : oubliée", sc_jouee_ailleurs_hors},
        {"jouée ailleurs, dans les pages parquées seulement", sc_jouee_ailleurs_dans_le_parquage},
        {"le lecteur redémarre, cartouche en place", sc_lecteur_redemarre},
        {"le lecteur redémarre après une écriture interrompue", sc_lecteur_redemarre_pendant_ecriture},
        {"deux exemplaires du même jeu, sauvegardes identiques (limite connue)", sc_deux_exemplaires},
        {"retirée pendant que le WiFi est coupé", sc_retrait_wifi_coupe},
        {"figé, le jeu écrit n'importe quoi", sc_bruit_du_jeu_fige},
        {"la console attend la copie ; fente vide, elle démarre aussitôt", sc_feu_vert},
        {"MBC3 avec horloge, MBC1 32 Kio, MBC2", sc_types},
    };

    int echecs = 0;
    for (Regle regle : {Regle::Plan, Regle::Stricte}) {
        std::printf("\n=== Règle %s ===\n", regle == Regle::Plan ? "du plan" : "stricte (proposée)");
        int n = 0;
        for (const auto &[nom, sc] : scenarios) {
            std::mt19937 hj(42);
            Jeux jeux(hj);
            Monde w(regle, 1000 + (uint32_t)n++, verbeux);
            const std::string r = sc(w, jeux);
            std::printf("  %s  %s\n", r.empty() ? "ok  " : "ÉCHEC", nom.c_str());
            if (!r.empty()) {
                ++echecs;
                std::printf("      %s\n", r.c_str());
            }
            if (verbeux)
                for (const auto &l : w.recit) std::printf("        %s\n", l.c_str());
        }

        std::mt19937 hj(42);
        Jeux jeux(hj);
        Bilan b;
        for (int i = 0; i < tirages; ++i) tirage(regle, 50000 + (uint32_t)i, jeux, b);
        std::printf("  tirages aléatoires : %d, sans aucun constat : %d\n", b.tirages, b.reussis);
        for (const auto &[k, v] : b.par_regle) std::printf("      constats %s : %d\n", k.c_str(), v);
        for (const auto &e : b.exemples) std::printf("      ex. %s\n", e.c_str());
        if (regle == Regle::Stricte && b.reussis != b.tirages) ++echecs;
    }
    std::printf("\n%s\n", echecs ? "simulation : des attentes ne sont pas tenues" : "simulation : toutes les attentes tenues");
    return echecs ? 1 : 0;
}
