/**
 * Module ÉCRAN — autotest des broches d'un Pico, sans multimètre
 *
 * Écrit pour un Pico dont GP1 restait à 2,6 V alors qu'on le commandait à
 * 0 V (moitié de dalle verte en continu), puis ressoudé. Le Pico se teste
 * seul et rend son verdict sur la console USB, une fois par seconde. Aucun
 * fil n'est nécessaire ; la nappe doit être débranchée.
 *
 * Chaque tour, pour chaque broche libre (GP0-22, GP26-28) :
 *   - au repos, tirée vers le haut par la résistance interne, elle doit lire
 *     1 : sinon quelque chose la tient à la masse (pont de soudure vers GND) ;
 *   - commandée à 0, elle doit relire 0, au plus faible courant (2 mA) puis
 *     au plus fort (12 mA) : sinon sa sortie ne sait plus descendre, le
 *     défaut d'origine ;
 *   - commandée à 1, elle doit relire 1 ;
 *   - pendant qu'elle est à 0, toute autre broche qui passe à 0 lui est
 *     reliée : un pont de soudure, ou un fil posé exprès.
 *
 * Ce que l'autotest ne voit pas : la soudure entre la puce et la patte. La
 * relecture se fait dans la puce, pas au bout de la patte. Pour éprouver la
 * patte, poser un fil entre elle et une broche voisine (broches 1 et 2 pour
 * GP0 et GP1) : la liaison doit apparaître, puis chaque paire reliée est
 * basculée des milliers de fois par seconde, et la moindre bascule ratée est
 * comptée. Remuer la patte pendant ce temps révèle une soudure capricieuse.
 *
 * GP23-25 et GP29 sont laissés de côté : sur un Pico 2 W, ils parlent à la
 * puce WiFi.
 *
 * Les courts-circuits restent brefs : une broche n'est commandée que 20 µs
 * de suite, et jamais si elle est tenue à la masse.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

static const uint BROCHES[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                               13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28};
/* Numéro de la patte sur le Pico, pour chaque entrée de BROCHES. */
static const uint PATTES[] = {1,  2,  4,  5,  6,  7,  9,  10, 11, 12, 14, 15, 16,
                              17, 19, 20, 21, 22, 24, 25, 26, 27, 29, 31, 32, 34};
#define NB (sizeof BROCHES / sizeof BROCHES[0])

#define BASCULES_PAR_TOUR 2000

static uint32_t tenue_a_la_masse[NB];
static uint32_t descend_pas_2ma[NB];
static uint32_t descend_pas_12ma[NB];
static uint32_t monte_pas[NB];
static uint32_t reliees[NB][NB];
static uint32_t bascules[NB][NB];
static uint32_t bascules_ratees[NB][NB];

static void au_repos(uint g) {
    gpio_set_dir(g, GPIO_IN);
    gpio_set_pulls(g, true, false);
}

static bool lire(uint32_t niveaux, uint i) { return (niveaux >> BROCHES[i]) & 1u; }

/* Commande la broche i à 0 avec le courant donné ; rend les niveaux relus. */
static uint32_t commander_bas(uint i, enum gpio_drive_strength courant) {
    const uint g = BROCHES[i];
    gpio_set_drive_strength(g, courant);
    gpio_put(g, 0);
    gpio_set_dir(g, GPIO_OUT);
    busy_wait_us(20);
    return gpio_get_all();
}

static void tour(void) {
    busy_wait_us(200);
    const uint32_t repos = gpio_get_all();
    bool masse[NB];
    for (uint i = 0; i < NB; ++i) {
        masse[i] = !lire(repos, i);
        if (masse[i]) tenue_a_la_masse[i]++;
    }

    for (uint i = 0; i < NB; ++i) {
        if (masse[i]) continue;
        const uint g = BROCHES[i];

        const uint32_t bas = commander_bas(i, GPIO_DRIVE_STRENGTH_2MA);
        if (lire(bas, i)) descend_pas_2ma[i]++;
        for (uint j = 0; j < NB; ++j)
            if (j != i && !masse[j] && !lire(bas, j)) reliees[i][j]++;
        au_repos(g);
        busy_wait_us(20);

        if (lire(commander_bas(i, GPIO_DRIVE_STRENGTH_12MA), i)) descend_pas_12ma[i]++;
        gpio_put(g, 1);
        busy_wait_us(20);
        if (!gpio_get(g)) monte_pas[i]++;
        au_repos(g);
        gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
        busy_wait_us(50);
    }

    /* Paires reliées (dans les deux sens) : i commande, j écoute sans tirage. */
    for (uint i = 0; i < NB; ++i) {
        for (uint j = i + 1; j < NB; ++j) {
            if (masse[i] || masse[j] || !reliees[i][j] || !reliees[j][i]) continue;
            const uint gi = BROCHES[i], gj = BROCHES[j];
            gpio_set_pulls(gj, false, false);
            gpio_put(gi, 0);
            gpio_set_dir(gi, GPIO_OUT);
            for (uint k = 0; k < BASCULES_PAR_TOUR; ++k) {
                const bool v = k & 1u;
                gpio_put(gi, v);
                busy_wait_us(1);
                if (gpio_get(gj) != v) bascules_ratees[i][j]++;
            }
            bascules[i][j] += BASCULES_PAR_TOUR;
            au_repos(gi);
            au_repos(gj);
        }
    }
}

static void rapport(uint32_t secondes, uint32_t tours) {
    printf("--- %lu s, %lu tours ---\n", (unsigned long)secondes, (unsigned long)tours);

    bool anomalie = false;
    for (uint i = 0; i < NB; ++i) {
        if (!tenue_a_la_masse[i] && !descend_pas_2ma[i] && !descend_pas_12ma[i] && !monte_pas[i])
            continue;
        anomalie = true;
        printf("  GP%u (patte %u) : tenue a la masse %lu  ne descend pas %lu (2 mA) %lu (12 mA)"
               "  ne monte pas %lu\n",
               BROCHES[i], PATTES[i], (unsigned long)tenue_a_la_masse[i],
               (unsigned long)descend_pas_2ma[i], (unsigned long)descend_pas_12ma[i],
               (unsigned long)monte_pas[i]);
    }
    if (!anomalie) printf("  toutes les broches : descendent, montent, aucune tenue a la masse\n");

    bool lien = false;
    for (uint i = 0; i < NB; ++i) {
        for (uint j = i + 1; j < NB; ++j) {
            if (!reliees[i][j] && !reliees[j][i]) continue;
            lien = true;
            printf("  GP%u (patte %u) <-> GP%u (patte %u) : reliees %lu/%lu tours,"
                   " bascules ratees %lu / %lu\n",
                   BROCHES[i], PATTES[i], BROCHES[j], PATTES[j],
                   (unsigned long)(reliees[i][j] < reliees[j][i] ? reliees[i][j] : reliees[j][i]),
                   (unsigned long)tours, (unsigned long)bascules_ratees[i][j],
                   (unsigned long)bascules[i][j]);
        }
    }
    if (!lien) printf("  aucune broche reliee a une autre\n");

    const uint i = 1; /* GP1, la broche réparée */
    const bool gp1_ok = !tenue_a_la_masse[i] && !descend_pas_2ma[i] && !descend_pas_12ma[i] &&
                        !monte_pas[i];
    printf("  GP1 : %s\n", gp1_ok ? "OK dans la puce" : "DEFAUT");
}

int main(void) {
    stdio_init_all();
    for (uint i = 0; i < NB; ++i) {
        gpio_init(BROCHES[i]);
        gpio_set_drive_strength(BROCHES[i], GPIO_DRIVE_STRENGTH_2MA);
        au_repos(BROCHES[i]);
    }

    uint32_t tours = 0;
    absolute_time_t prochain = make_timeout_time_ms(1000);
    for (uint32_t secondes = 1;; ) {
        tour();
        tours++;
        if (time_reached(prochain)) {
            rapport(secondes++, tours);
            prochain = delayed_by_ms(prochain, 1000);
        }
    }
}
