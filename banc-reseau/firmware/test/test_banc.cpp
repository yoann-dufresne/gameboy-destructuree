/**
 * Éprouve sur PC ce que le banc calcule sans radio — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I../commun test_banc.cpp -o /tmp/test_banc
 *     /tmp/test_banc
 *
 * Ce qu'on vérifie : les formats ont la taille annoncée ; chaque retard tombe
 * dans le bon seau, bornes comprises ; le suivi de numérotation compte juste
 * les manquants et les doublons ; le départ des tranches d'image suit les
 * lignes capturées.
 */
#include "banc.h"
#include "outils.hpp"

#include <cstdio>

namespace {

int echecs = 0;

#define VERIFIER(cond, ...)                                          \
    do {                                                             \
        if (!(cond)) {                                               \
            if (++echecs <= 20) {                                    \
                std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__);  \
                std::printf(__VA_ARGS__);                            \
                std::printf("\n");                                   \
            }                                                        \
        }                                                            \
    } while (0)

void tailles() {
    VERIFIER(sizeof(banc_entete) == 16, "en-tete : %zu octets", sizeof(banc_entete));
    VERIFIER(sizeof(banc_bilan) == 156, "bilan : %zu octets", sizeof(banc_bilan));
    /* Un bilan tient dans un datagramme sans fragmentation. */
    VERIFIER(BANC_ENTETE + sizeof(banc_bilan) < 1400, "bilan trop gros");
    VERIFIER(BANC_ENTETE + BANC_CARTOUCHE_BLOC < 1400, "bloc trop gros");
}

void seaux() {
    VERIFIER(banc_seau(0) == 0, "0 us");
    for (int i = 0; i < BANC_SEAUX - 1; ++i) {
        const uint32_t b = BANC_BORNES_US[i];
        VERIFIER(banc_seau(b) == i, "borne %u us dans le seau %d, attendu %d", b, banc_seau(b), i);
        VERIFIER(banc_seau(b + 1) == i + 1, "%u us dans le seau %d, attendu %d", b + 1,
                 banc_seau(b + 1), i + 1);
        if (i)
            VERIFIER(BANC_BORNES_US[i] > BANC_BORNES_US[i - 1], "bornes non croissantes en %d", i);
    }
    VERIFIER(banc_seau(0xFFFFFFFFu) == BANC_SEAUX - 1, "le plus grand retard hors du dernier seau");
}

void suivi() {
    uint32_t perdus = 0, doublons = 0;
    outils::Suivi s;
    /* 0 1 2, 5 (3 et 4 manquent), 5 (doublon), 4 (en retard : doublon), 6,
     * puis un passage par zéro de la numérotation sur 32 bits. */
    const uint32_t suite[] = {0, 1, 2, 5, 5, 4, 6};
    for (uint32_t x : suite)
        s.noter(x, perdus, doublons);
    VERIFIER(perdus == 2, "perdus %u, attendu 2", perdus);
    VERIFIER(doublons == 2, "doublons %u, attendu 2", doublons);

    outils::Suivi t;
    perdus = doublons = 0;
    const uint32_t tour[] = {0xFFFFFFFEu, 0xFFFFFFFFu, 0, 2};
    for (uint32_t x : tour)
        t.noter(x, perdus, doublons);
    VERIFIER(perdus == 1 && doublons == 0, "passage par zero : perdus %u doublons %u", perdus,
             doublons);
}

void tranches() {
    /* Le sniffer en mode pipeliné : une tranche part quand ses 35 lignes sont
     * capturées, la dernière à la 144e ligne. Doit tenir dans l'image. */
    uint32_t avant = 0;
    for (int k = 0; k < BANC_IMAGE_TRANCHES; ++k) {
        int lignes = (k + 1) * BANC_IMAGE_LIGNES_TRANCHE;
        if (lignes > BANC_IMAGE_LIGNES_VISIBLES)
            lignes = BANC_IMAGE_LIGNES_VISIBLES;
        const uint32_t us = (uint32_t)((uint64_t)lignes * BANC_IMAGE_LIGNE_NS / 1000);
        VERIFIER(us > avant, "tranche %d avant la precedente", k);
        VERIFIER(us < BANC_IMAGE_PERIODE_NS / 1000, "tranche %d hors de l'image", k);
        avant = us;
    }
    /* 4 tranches de 1412 et une de 172, quatre copies, 59,73 images/s. */
    const double mbit = 4.0 * (4 * BANC_IMAGE_OCTETS_TRANCHE + BANC_IMAGE_OCTETS_DERNIERE) * 8 *
                        1e9 / BANC_IMAGE_PERIODE_NS / 1e6;
    VERIFIER(mbit > 11.0 && mbit < 11.4, "image : %.2f Mbit/s, attendu 11,2", mbit);
    const double son = 4.0 * BANC_SON_OCTETS * 8 * 1e6 / BANC_SON_PERIODE_US / 1e6;
    VERIFIER(son > 6.1 && son < 6.3, "son : %.2f Mbit/s, attendu 6,2", son);
}

} // namespace

int main() {
    tailles();
    seaux();
    suivi();
    tranches();
    if (echecs) {
        std::printf("ECHEC : %d verification(s) en defaut\n", echecs);
        return 1;
    }
    std::printf("OK : formats, seaux, suivi de numerotation, tranches et debits conformes\n");
    return 0;
}
