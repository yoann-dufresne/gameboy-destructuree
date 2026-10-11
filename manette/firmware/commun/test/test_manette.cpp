/**
 * Éprouve l'automate de la manette sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_manette.cpp -o test_manette
 *     ./test_manette
 *
 * On le fait tourner à 1 kHz, comme dans la manette, et on vérifie quand il
 * émet, ce qu'il émet, la LED et le maintien de l'alimentation (plan §4,
 * §5.4, §5.6). Puis la lecture des boutons et des molettes. Enfin, la
 * manette et l'injecteur bout à bout, à travers un réseau simulé qui perd et
 * retarde : aucun bouton ne doit rester coincé.
 */

#include "injecteur.hpp"
#include "manette.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <random>
#include <vector>

namespace {

int echecs = 0;

#define VERIFIER(cond, ...)                                          \
    do {                                                             \
        if (!(cond)) {                                               \
            if (++echecs <= 30) {                                    \
                std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__);  \
                std::printf(__VA_ARGS__);                            \
                std::printf("\n");                                   \
            }                                                        \
        }                                                            \
    } while (0)

using manette::Led;
using manette::Manette;

man1_paquet accuse_de(const man1_paquet &p, bool console = true) {
    man1_paquet a{};
    a.magic = MAN1_MAGIC;
    a.type = MAN1_TYPE_ACCUSE;
    a.marche = console;
    a.session = p.session;
    a.numero = p.numero;
    return a;
}

/* Fait tourner la manette de t0 à t1 exclu ; rend les heures d'émission. */
std::vector<uint32_t> tourner(Manette &m, Manette::Entrees &e, uint32_t t0, uint32_t t1,
                              std::vector<man1_paquet> *paquets = nullptr) {
    std::vector<uint32_t> heures;
    for (uint32_t t = t0; t < t1; ++t) {
        const auto &s = m.avancer(e, t);
        if (s.emettre) {
            heures.push_back(t);
            if (paquets) paquets->push_back(s.paquet);
        }
    }
    return heures;
}

void rien_sur_arret() {
    Manette m(42);
    Manette::Entrees e;
    e.accel_eveille = true;
    std::vector<uint32_t> h;
    for (uint32_t t = 0; t < 10000; ++t) {
        e.boutons = (t / 100) & 0xFF;
        e.volume = (uint8_t)(t / 40);
        if (m.avancer(e, t).emettre) h.push_back(t);
    }
    VERIFIER(h.empty(), "%zu paquets émis sur arrêt", h.size());
    VERIFIER(m.avancer(e, 10000).led == Led::ETEINTE, "LED allumée sur arrêt");
    VERIFIER(m.avancer(e, 10001).maintien, "maintien relâché, l'accéléromètre éveillé");
    e.accel_eveille = false;
    VERIFIER(!m.avancer(e, 10002).maintien, "maintien tenu, immobile et sur arrêt");
    std::printf("  sur arrêt : rien n'est émis, LED éteinte, sommeil quand l'accéléromètre se tait\n");
}

void cadence_marche() {
    Manette m(43);
    Manette::Entrees e;
    e.interrupteur = true;
    std::vector<man1_paquet> p;
    auto h = tourner(m, e, 1000, 1200, &p);
    /* aussitôt, +4, +8, puis toutes les 50 ms depuis la dernière émission */
    const std::vector<uint32_t> attendu = {1000, 1004, 1008, 1058, 1108, 1158};
    VERIFIER(h == attendu, "cadence : %zu émissions, la 4e à %u", h.size(),
             h.size() > 3 ? h[3] : 0);
    VERIFIER(p[0].marche == 1 && p[0].temps_ms == 1000 && p[0].numero == 1,
             "premier paquet : marche %u, temps %u, numéro %u", p[0].marche, p[0].temps_ms,
             p[0].numero);
    VERIFIER(p.back().numero == 6, "numérotation : %u", p.back().numero);
    /* Un appui : aussitôt, puis deux répétitions, puis le battement. */
    e.boutons = MAN1_A;
    p.clear();
    h = tourner(m, e, 1200, 1300, &p);
    VERIFIER(h.size() >= 4 && h[0] == 1200 && h[1] == 1204 && h[2] == 1208 && h[3] == 1258,
             "appui : %zu émissions", h.size());
    VERIFIER(p[0].boutons == MAN1_A, "A absent du paquet");
    /* Un appui pendant les répétitions les relance. */
    e.boutons = 0;
    h = tourner(m, e, 1300, 1302);
    e.boutons = MAN1_B;
    auto h2 = tourner(m, e, 1302, 1320);
    VERIFIER(h.size() == 1 && h2.size() == 3 && h2[0] == 1302 && h2[1] == 1306 && h2[2] == 1310,
             "appui pendant les répétitions");
    std::printf("  en marche : aussitôt, +4 ms, +8 ms, puis un battement toutes les 50 ms\n");
}

void led() {
    Manette m(44);
    Manette::Entrees e;
    e.interrupteur = true;
    uint8_t min = 255, max = 0;
    for (uint32_t t = 0; t < 4000; ++t) {
        const auto &s = m.avancer(e, t);
        VERIFIER(s.led == Led::BLEU, "pas bleue sans accusé, à %u", t);
        min = std::min(min, s.lumiere);
        max = std::max(max, s.lumiere);
    }
    VERIFIER(min < 40 && max > 240, "le bleu ne pulse pas : %u à %u", min, max);
    man1_paquet p{};
    p.session = 44;
    p.numero = 1;
    m.accuse(accuse_de(p), 4000);
    VERIFIER(m.avancer(e, 4000).led == Led::ROUGE, "pas rouge après un accusé");
    VERIFIER(m.avancer(e, 4499).led == Led::ROUGE, "plus rouge avant 500 ms");
    VERIFIER(m.avancer(e, 4500).led == Led::BLEU, "encore rouge 500 ms après le dernier accusé");
    p.session = 99;
    m.accuse(accuse_de(p), 4600);
    VERIFIER(m.avancer(e, 4600).led == Led::BLEU, "accusé d'une autre session pris");
    std::printf("  LED : bleu pulsé (%u à %u) sans accusé, rouge 500 ms après chaque accusé\n", min,
                max);
}

void arret() {
    /* Arrêt accusé : l'arrêt part, puis plus rien, boutons et molettes
     * compris. */
    Manette m(45);
    Manette::Entrees e;
    e.interrupteur = true;
    e.boutons = MAN1_START;
    e.volume = 100;
    tourner(m, e, 0, 500);
    e.interrupteur = false;
    e.accel_eveille = false;
    std::vector<man1_paquet> p;
    auto h = tourner(m, e, 500, 506, &p);
    VERIFIER(!h.empty() && h[0] == 500, "l'arrêt ne part pas aussitôt");
    VERIFIER(p[0].marche == 0 && p[0].boutons == 0 && p[0].volume == 0,
             "l'arrêt porte des boutons ou des molettes");
    VERIFIER(m.avancer(e, 506).maintien, "maintien relâché avant l'accusé de l'arrêt");
    m.accuse(accuse_de(p[0], false), 506);
    e.boutons = MAN1_A;
    e.volume = 200;
    h = tourner(m, e, 507, 5000);
    VERIFIER(h.empty(), "%zu paquets après l'arrêt accusé", h.size());
    VERIFIER(m.etat() == Manette::Etat::PRETE, "pas revenue à PRÊTE");
    VERIFIER(!m.avancer(e, 5000).maintien, "maintien tenu après l'arrêt accusé, immobile");

    /* Arrêt jamais accusé : répété 2 s, puis abandonné. */
    Manette n(46);
    Manette::Entrees f;
    f.interrupteur = true;
    tourner(n, f, 0, 100);
    f.interrupteur = false;
    f.accel_eveille = false;
    h = tourner(n, f, 100, 5000);
    VERIFIER(!h.empty() && h.back() < 2100 && h.size() >= 40 && h.size() <= 45,
             "arrêt sans accusé : %zu émissions, la dernière à %u", h.size(),
             h.empty() ? 0 : h.back());
    VERIFIER(!n.avancer(f, 5000).maintien, "maintien tenu après l'abandon");

    /* Retour sur marche pendant la fin. */
    Manette r(47);
    Manette::Entrees g;
    g.interrupteur = true;
    tourner(r, g, 0, 100);
    g.interrupteur = false;
    tourner(r, g, 100, 150);
    g.interrupteur = true;
    std::vector<man1_paquet> q;
    h = tourner(r, g, 150, 152, &q);
    VERIFIER(r.etat() == Manette::Etat::MARCHE && h.size() == 1 && q[0].marche == 1,
             "retour sur marche pendant la fin");
    std::printf("  arrêt : part aussitôt, accusé puis silence ; sans accusé, abandonné à 2 s ; "
                "maintien tenu jusque-là\n");
}

void anti_rebond() {
    manette::AntiRebond ar;
    /* Appui sur A à 100 ms, qui rebondit pendant 3 ms. */
    uint8_t sortie[120];
    for (uint32_t t = 0; t < 120; ++t) {
        uint8_t brut = 0;
        if (t >= 100) brut = MAN1_A;
        if (t == 101 || t == 103) brut = 0;
        sortie[t] = ar.lire(brut, t);
    }
    VERIFIER(sortie[99] == 0 && sortie[100] == MAN1_A, "l'appui est retardé");
    bool stable = true;
    for (uint32_t t = 100; t < 120; ++t) stable &= sortie[t] == MAN1_A;
    VERIFIER(stable, "les rebonds passent");
    /* Relâché à 200 ms, rebond à 202. */
    uint8_t a = 0, b = 0, c = 0;
    for (uint32_t t = 120; t < 220; ++t) {
        uint8_t brut = (t < 200 || t == 202) ? MAN1_A : 0;
        const uint8_t s = ar.lire(brut, t);
        if (t == 199) a = s;
        if (t == 200) b = s;
        if (t == 202) c = s;
    }
    VERIFIER(a == MAN1_A && b == 0 && c == 0, "relâche : %u %u %u", a, b, c);
    std::printf("  anti-rebond : le premier front passe aussitôt, les rebonds de 5 ms sont ignorés\n");
}

void molettes() {
    std::mt19937 alea(3);
    manette::Molette mol;
    uint8_t v = 0;
    bool stable = true;
    for (int i = 0; i < 2000; ++i) {
        const uint16_t adc = (uint16_t)(2056 + (int)(alea() % 13) - 6); /* ±6 pas de bruit */
        v = mol.lire(adc);
        if (i > 16 && v != 128) stable = false;
    }
    VERIFIER(stable && v == 128, "la molette tremble sous le bruit : %u", v);
    /* Une montée lente : des crans un par un, jamais en arrière. */
    manette::Molette m2;
    uint8_t prec = 0;
    bool monotone = true, sauts = false;
    for (int x = 0; x <= 4095; ++x) {
        const uint8_t c = m2.lire((uint16_t)x);
        if (x > 16 && c < prec) monotone = false;
        if (x > 16 && c > prec + 1) sauts = true;
        prec = c;
    }
    for (int i = 0; i < 20; ++i) prec = m2.lire(4095);
    VERIFIER(monotone && !sauts && prec == 255, "montée : monotone %d, sauts %d, fin %u", monotone,
             sauts, prec);
    std::printf("  molettes : stables sous ±6 pas de bruit, 0 à 255 sans saut\n");
}

/* La manette et l'injecteur, bout à bout, à travers un réseau simulé. */
void bout_a_bout() {
    std::mt19937_64 alea(5);
    std::uniform_real_distribution<double> u(0, 1);
    Manette m(77);
    manette::Injecteur inj;
    struct Vol {
        uint32_t t;
        man1_paquet p;
        bool vers_injecteur;
    };
    std::deque<Vol> air;
    Manette::Entrees e;
    e.interrupteur = true;
    const double perte = 0.05;
    uint32_t relache_ms = 0;
    std::vector<uint32_t> retards; /* manette → photoMOS, appuis et relâches */
    uint8_t voulu = 0;
    uint32_t change_ms = 0;
    bool en_attente = false;
    uint32_t coince_max = 0;
    for (uint32_t t = 0; t < 10u * 60u * 1000u; ++t) {
        /* Le joueur : un changement de boutons au hasard, en moyenne 3 par seconde. */
        if (u(alea) < 0.003) {
            e.boutons = (uint8_t)(alea() & 0xFF);
            voulu = e.boutons;
            change_ms = t;
            en_attente = true;
        }
        const auto &s = m.avancer(e, t);
        if (s.emettre && u(alea) >= perte)
            air.push_back({t + 2 + (uint32_t)(u(alea) * 18), s.paquet, true});
        /* Le réseau livre ce qui est arrivé (pas forcément dans l'ordre). */
        std::vector<Vol> retours;
        for (auto it = air.begin(); it != air.end();) {
            if (it->t <= t) {
                if (it->vers_injecteur) {
                    man1_paquet acc{};
                    if (inj.recevoir(0, it->p, t, &acc) && u(alea) >= perte)
                        retours.push_back({t + 2 + (uint32_t)(u(alea) * 18), acc, false});
                } else {
                    m.accuse(it->p, t);
                }
                it = air.erase(it);
            } else {
                ++it;
            }
        }
        air.insert(air.end(), retours.begin(), retours.end());
        const auto &so = inj.avancer(t);
        if (en_attente && so.boutons == voulu) {
            retards.push_back(t - change_ms);
            en_attente = false;
        }
        /* Un bouton posé que la manette n'appuie plus : combien de temps ? */
        if (so.boutons & ~e.boutons) {
            if (!relache_ms) relache_ms = t;
            coince_max = std::max(coince_max, t - relache_ms);
        } else {
            relache_ms = 0;
        }
    }
    std::sort(retards.begin(), retards.end());
    const uint32_t med = retards[retards.size() / 2];
    const uint32_t p99 = retards[retards.size() * 99 / 100];
    VERIFIER(med <= 12 && p99 <= 30, "retards : médiane %u ms, 99 %% sous %u ms", med, p99);
    VERIFIER(coince_max < 250, "un bouton coincé %u ms", coince_max);
    VERIFIER(m.avancer(e, 600000).led == Led::ROUGE, "LED pas rouge en fonctionnement");
    std::printf("  bout à bout, 5 %% de pertes dans chaque sens, 2 à 20 ms de retard, 10 min : "
                "%zu changements, retard médian %u ms, 99 %% sous %u ms, le pire %u ms ; "
                "bouton relâché en retard au plus %u ms ; %u relâches de sécurité\n",
                retards.size(), med, p99, retards.back(), coince_max,
                inj.compteurs().relaches);
}

} // namespace

int main() {
    std::printf("Automate de la manette\n");
    rien_sur_arret();
    cadence_marche();
    led();
    arret();
    anti_rebond();
    molettes();
    bout_a_bout();
    if (echecs) {
        std::printf("ÉCHEC : %d vérification(s)\n", echecs);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
