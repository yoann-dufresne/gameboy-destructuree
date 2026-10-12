/**
 * Éprouve l'automate de l'injecteur sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_injecteur.cpp -o test_injecteur
 *     ./test_injecteur
 *
 * On lui donne des états de manette à des heures choisies, et on vérifie ce
 * qu'il pose sur les photoMOS, ce qu'il fait de la console, et ce qu'il
 * demande au sniffer : les règles des §4.3, §4.4 et §6 du plan.
 */

#include "injecteur.hpp"

#include <cstdio>

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

using manette::Injecteur;

struct FausseManette {
    uint32_t session;
    uint32_t numero = 0;
    man1_paquet etat(bool marche, uint8_t boutons, uint8_t volume = 128, uint8_t contraste = 64) {
        man1_paquet p{};
        p.magic = MAN1_MAGIC;
        p.type = MAN1_TYPE_ETAT;
        p.marche = marche;
        p.boutons = boutons;
        p.volume = volume;
        p.contraste = contraste;
        p.piles_mv = 2600;
        p.session = session;
        p.numero = ++numero;
        return p;
    }
};

/* Une manette en marche qui émet toutes les 50 ms, de t0 à t1 exclu. */
void battre(Injecteur &inj, FausseManette &m, unsigned i, uint32_t t0, uint32_t t1,
            uint8_t boutons = 0) {
    for (uint32_t t = t0; (int32_t)(t1 - t) > 0; t += 50) {
        inj.recevoir(i, m.etat(true, boutons), t, nullptr);
        for (uint32_t u = t; u < t + 50 && (int32_t)(t1 - u) > 0; ++u) inj.avancer(u);
    }
}

void marche_et_boutons() {
    Injecteur inj;
    FausseManette m{1};
    man1_paquet acc{};
    VERIFIER(inj.recevoir(0, m.etat(true, MAN1_A), 1000, &acc), "état refusé");
    VERIFIER(inj.sorties().console, "la console ne s'allume pas sur « marche »");
    VERIFIER(inj.sorties().boutons == MAN1_A, "A n'est pas posé");
    VERIFIER(acc.type == MAN1_TYPE_ACCUSE && acc.marche == 1 && acc.numero == 1 && acc.session == 1,
             "accusé mal formé");
    VERIFIER(inj.sorties().volume == 128 && inj.sorties().contraste == 64, "molettes perdues");
    std::printf("  marche, boutons, accusé : vérifiés\n");
}

void relache_de_securite() {
    Injecteur inj;
    FausseManette m{2};
    inj.recevoir(0, m.etat(true, MAN1_A), 5000, nullptr);
    VERIFIER(inj.avancer(5199).boutons == MAN1_A, "A relâché trop tôt");
    VERIFIER(inj.avancer(5200).boutons == 0, "A encore enfoncé 200 ms après le dernier paquet");
    VERIFIER(inj.avancer(5200).console, "la console s'éteint avec la relâche");
    VERIFIER(inj.compteurs().relaches == 1, "relâche non comptée");
    std::printf("  relâche de sécurité : A relâché à 200 ms, console allumée\n");
}

void message_et_extinction() {
    Injecteur inj(30000); /* 30 s, comme au banc (T3.5) */
    FausseManette m{3};
    battre(inj, m, 0, 0, 1000);
    const uint32_t dernier = 950;
    VERIFIER(!inj.avancer(dernier + 1999).message, "message avant 2 s");
    const auto &s = inj.avancer(dernier + 2000);
    VERIFIER(s.message && s.absence_ms == 2000 && s.extinction_dans_ms == 28000,
             "message à 2 s : %d, absence %u, extinction dans %u", s.message, s.absence_ms,
             s.extinction_dans_ms);
    /* La manette revient avant l'extinction : tout reprend. */
    battre(inj, m, 0, dernier + 10000, dernier + 11000, MAN1_B);
    VERIFIER(!inj.sorties().message && inj.sorties().console, "le retour n'efface pas le message");
    VERIFIER(inj.sorties().boutons == MAN1_B, "B non posé au retour");
    /* Puis plus rien : extinction au bout de 30 s. */
    const uint32_t vu = dernier + 10950;
    VERIFIER(inj.avancer(vu + 29999).console, "éteinte trop tôt");
    VERIFIER(!inj.avancer(vu + 30000).console, "pas éteinte après l'absence");
    VERIFIER(!inj.sorties().message, "message console éteinte");
    VERIFIER(inj.compteurs().extinctions == 1, "extinction non comptée");
    std::printf("  « aucune manette » à 2 s, retour, extinction après l'absence : vérifiés\n");
}

void quinze_minutes() {
    Injecteur inj;
    FausseManette m{4};
    inj.recevoir(0, m.etat(true, 0), 0, nullptr);
    VERIFIER(inj.avancer(15u * 60u * 1000u - 1).console, "éteinte avant 15 min");
    VERIFIER(!inj.avancer(15u * 60u * 1000u).console, "allumée après 15 min");
    std::printf("  extinction à 15 min exactement : vérifiée\n");
}

void arret_et_seconde_minimale() {
    Injecteur inj;
    FausseManette m{5};
    man1_paquet acc{};
    inj.recevoir(0, m.etat(true, MAN1_START), 0, nullptr);
    inj.recevoir(0, m.etat(false, MAN1_START), 100, &acc);
    VERIFIER(!inj.sorties().console, "« arrêt » n'éteint pas");
    VERIFIER(inj.sorties().boutons == 0, "boutons posés sur arrêt");
    VERIFIER(acc.marche == 0, "l'accusé de l'arrêt dit la console allumée");
    /* Rallumée 300 ms après : elle attend la seconde d'arrêt. */
    inj.recevoir(0, m.etat(true, 0), 400, nullptr);
    VERIFIER(!inj.avancer(1099).console, "rallumée avant une seconde d'arrêt");
    battre(inj, m, 0, 450, 1200);
    VERIFIER(inj.sorties().console, "pas rallumée après une seconde");
    /* Arrêt, puis silence : ni message ni extinction comptée. */
    inj.recevoir(0, m.etat(false, 0), 2000, nullptr);
    VERIFIER(!inj.avancer(10000).message && !inj.sorties().console, "message après un arrêt");
    VERIFIER(inj.compteurs().extinctions == 0, "un arrêt compté comme extinction");
    std::printf("  arrêt, une seconde d'arrêt au moins, silence après l'arrêt : vérifiés\n");
}

void numerotation() {
    Injecteur inj;
    FausseManette m{6};
    inj.recevoir(0, m.etat(true, MAN1_A), 0, nullptr);         /* n° 1 */
    const man1_paquet vieux = m.etat(true, MAN1_A);             /* n° 2, en retard */
    m.etat(true, 0);                                            /* n° 3, perdu */
    inj.recevoir(0, m.etat(true, MAN1_B), 10, nullptr);         /* n° 4 */
    man1_paquet acc{};
    VERIFIER(inj.recevoir(0, vieux, 12, &acc), "un vieux paquet n'est pas accusé");
    VERIFIER(inj.sorties().boutons == MAN1_B, "un vieux paquet a écrasé l'état");
    VERIFIER(inj.compteurs().perdus == 2 && inj.compteurs().anciens == 1,
             "perdus %u, anciens %u", inj.compteurs().perdus, inj.compteurs().anciens);
    /* La manette redémarre : nouvelle session, numéros depuis 1. */
    FausseManette neuve{7};
    inj.recevoir(0, neuve.etat(true, MAN1_SELECT), 20, nullptr);
    VERIFIER(inj.sorties().boutons == MAN1_SELECT, "manette redémarrée ignorée");
    VERIFIER(inj.compteurs().redemarrages == 1, "redémarrage non compté");
    std::printf("  numérotation : pertes, paquets en retard, manette redémarrée : vérifiés\n");
}

void heure_qui_fait_le_tour() {
    Injecteur inj;
    FausseManette m{8};
    const uint32_t t = 0xFFFFFF00u;
    inj.recevoir(0, m.etat(true, MAN1_A), t, nullptr);
    VERIFIER(inj.avancer(t + 199).boutons == MAN1_A, "relâché trop tôt au passage par zéro");
    VERIFIER(inj.avancer(t + 200).boutons == 0, "coincé au passage par zéro de l'heure");
    std::printf("  heure qui fait le tour (49 jours) : vérifiée\n");
}

void plusieurs_manettes() {
    /* Les règles de fusion.hpp ; le OU des boutons reste provisoire. */
    Injecteur inj;
    FausseManette m0{10}, m1{11};
    inj.recevoir(0, m0.etat(true, MAN1_A, 10, 20), 0, nullptr);
    inj.recevoir(1, m1.etat(true, MAN1_B, 200, 31), 5, nullptr);
    VERIFIER(inj.sorties().boutons == (MAN1_A | MAN1_B), "boutons des deux manettes : %02x",
             inj.sorties().boutons);
    VERIFIER(inj.sorties().volume == 105 && inj.sorties().contraste == 26,
             "molettes : pas la moyenne, volume %u, contraste %u", inj.sorties().volume,
             inj.sorties().contraste);
    inj.recevoir(0, m0.etat(false, 0), 10, nullptr);
    VERIFIER(inj.sorties().console, "une manette sur arrêt éteint la console de l'autre");
    VERIFIER(inj.sorties().boutons == MAN1_B, "la manette arrêtée appuie encore");
    VERIFIER(inj.sorties().volume == 200, "les molettes d'une manette sur arrêt comptent encore");
    inj.recevoir(1, m1.etat(false, 0), 20, nullptr);
    VERIFIER(!inj.sorties().console, "les deux sur arrêt, console allumée");
    VERIFIER(inj.sorties().volume == 200, "molettes changées sans manette en marche");
    VERIFIER(!inj.recevoir(4, m0.etat(true, 0), 30, nullptr), "une cinquième manette acceptée");
    std::printf("  plusieurs manettes : OU des boutons, moyenne des molettes : vérifiés\n");
}

void retour_apres_extinction() {
    /* Éteinte faute de manette : celle qui revient sur marche la rallume. */
    Injecteur inj(30000);
    FausseManette m{12};
    inj.recevoir(0, m.etat(true, 0), 0, nullptr);
    VERIFIER(!inj.avancer(30000).console, "pas éteinte après l'absence");
    inj.recevoir(0, m.etat(true, MAN1_A), 60000, nullptr);
    VERIFIER(inj.sorties().console, "la manette revenue ne rallume pas la console");
    VERIFIER(inj.sorties().boutons == MAN1_A, "A non posé au retour");
    std::printf("  retour d'une manette après l'extinction : console rallumée\n");
}

} // namespace

int main() {
    std::printf("Automate de l'injecteur\n");
    marche_et_boutons();
    relache_de_securite();
    message_et_extinction();
    quinze_minutes();
    arret_et_seconde_minimale();
    numerotation();
    heure_qui_fait_le_tour();
    plusieurs_manettes();
    retour_apres_extinction();
    if (echecs) {
        std::printf("ÉCHEC : %d vérification(s)\n", echecs);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
