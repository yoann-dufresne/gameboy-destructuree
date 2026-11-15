/**
 * Recette de la carte de l'écran — l'outillage commun (voir banc.h)
 */

#include "banc.h"

#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

const uint8_t PATTE[NB_GPIO] = {
    1,  2,  4,  5,  6,  7,  9,  10, 11, 12, 14, 15, 16, 17, 19,
    20, 21, 22, 24, 25, 26, 27, 29, 0,  0,  0,  31, 32, 34, 0,
};

/* ================================================= contrôle des broches */

/* Compteurs, une passe après l'autre. */
static uint8_t essais[NB_GPIO];      /* passes où la broche était au repos */
static uint8_t essais_haut[NB_GPIO]; /* … et lue à 1, tirée vers le haut */
static uint8_t n_ne_descend_pas[NB_GPIO];
static uint8_t n_ne_monte_pas[NB_GPIO];
static uint8_t n_tiree_haut[NB_GPIO];
static uint8_t n_tiree_bas[NB_GPIO];
static uint8_t n_lien[NB_GPIO][NB_GPIO]; /* [i][j] : i commandée à 0, j suit */

static void au_repos(unsigned g) {
    gpio_set_dir(g, GPIO_IN);
    gpio_set_pulls(g, true, false);
    gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
    gpio_put(g, 0);
}

static void commander(unsigned g, bool niveau) {
    gpio_put(g, niveau);
    gpio_set_dir(g, GPIO_OUT);
    busy_wait_us(20);
}

static void une_passe(uint32_t masque) {
    for (unsigned g = 0; g < NB_GPIO; ++g)
        if (masque & (1u << g))
            au_repos(g);
    busy_wait_us(200);

    /* 1 ms d'écoute : une broche qui bascule est pilotée d'ailleurs, par un
     * autre Pico de la carte. On ne la touche pas pendant cette passe. */
    uint32_t et = ~0u, ou = 0;
    const uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < 1000) {
        const uint32_t v = gpio_get_all();
        et &= v;
        ou |= v;
    }
    const uint32_t bascule = (et ^ ou) & masque;
    const uint32_t haut = et & masque; /* à 1 tout du long */
    const uint32_t bas = ~ou & masque; /* à 0 tout du long */

    for (unsigned g = 0; g < NB_GPIO; ++g) {
        const uint32_t m = 1u << g;
        if (!(masque & m) || (bascule & m))
            continue;
        essais[g]++;
        if (haut & m) {
            essais_haut[g]++;
            /* Commandée à 0, au plus faible courant : toute broche au repos
             * qui la suit lui est reliée. Puis au plus fort courant : une
             * sortie qui ne descend plus (GP1, 29/10/2026). */
            commander(g, 0);
            const uint32_t v = gpio_get_all();
            bool descend = !(v & m);
            const uint32_t suivent = haut & ~v & ~m;
            for (unsigned j = 0; j < NB_GPIO; ++j)
                if (suivent & (1u << j))
                    n_lien[g][j]++;
            gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_12MA);
            busy_wait_us(20);
            if (gpio_get(g))
                descend = false;
            gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
            if (!descend)
                n_ne_descend_pas[g]++;
            /* Relâchée depuis 0, tirée vers le bas : seule une résistance vers
             * le 3,3 V la fait remonter. Partir de 0 évite l'errata E9 (une
             * broche tirée vers le bas après avoir été à 1 peut rester à 1). */
            gpio_set_pulls(g, false, true);
            gpio_set_dir(g, GPIO_IN);
            busy_wait_us(30);
            if (gpio_get(g))
                n_tiree_haut[g]++;
            commander(g, 1);
            if (!gpio_get(g))
                n_ne_monte_pas[g]++;
        } else if (bas & m) {
            /* Lue à 0 malgré le tirage interne vers le haut : une résistance
             * vers la masse, si elle se laisse monter, sinon un court-circuit. */
            commander(g, 1);
            if (gpio_get(g))
                n_tiree_bas[g]++;
            else
                n_ne_monte_pas[g]++;
        }
        au_repos(g);
        busy_wait_us(30);
    }
}

/* Au moins trois passes sur quatre, et au moins trois passes. */
static bool majorite(unsigned compte, unsigned sur) {
    return sur >= 3 && 4 * compte >= 3 * sur;
}

void broches_eprouver(uint32_t masque, int passes, bilan_t *b) {
    memset(essais, 0, sizeof essais);
    memset(essais_haut, 0, sizeof essais_haut);
    memset(n_ne_descend_pas, 0, sizeof n_ne_descend_pas);
    memset(n_ne_monte_pas, 0, sizeof n_ne_monte_pas);
    memset(n_tiree_haut, 0, sizeof n_tiree_haut);
    memset(n_tiree_bas, 0, sizeof n_tiree_bas);
    memset(n_lien, 0, sizeof n_lien);

    for (unsigned g = 0; g < NB_GPIO; ++g)
        if (masque & (1u << g))
            gpio_init(g);
    for (int p = 0; p < passes; ++p) {
        une_passe(masque);
        busy_wait_us(5000 + get_rand_32() % 20000);
    }

    memset(b, 0, sizeof *b);
    b->essayees = masque;
    b->passes = passes;
    for (unsigned g = 0; g < NB_GPIO; ++g) {
        const uint32_t m = 1u << g;
        if (!(masque & m))
            continue;
        if (essais[g] < 3) {
            b->pilotees |= m;
            continue;
        }
        if (majorite(n_ne_descend_pas[g], essais[g]))
            b->ne_descend_pas |= m;
        if (majorite(n_ne_monte_pas[g], essais[g]))
            b->ne_monte_pas |= m;
        if (majorite(n_tiree_haut[g], essais_haut[g]))
            b->tiree_haut |= m;
        if (majorite(n_tiree_bas[g], essais[g] - essais_haut[g]))
            b->tiree_bas |= m;
    }
    for (unsigned i = 0; i < NB_GPIO; ++i)
        for (unsigned j = i + 1; j < NB_GPIO; ++j) {
            const unsigned sur = essais_haut[i] < essais_haut[j] ? essais_haut[i] : essais_haut[j];
            if (majorite(n_lien[i][j], sur) && majorite(n_lien[j][i], sur)) {
                b->reliee[i] |= 1u << j;
                b->reliee[j] |= 1u << i;
            }
        }
}

uint32_t broches_hs(const bilan_t *b) {
    uint32_t hs = b->ne_descend_pas | b->ne_monte_pas;
    for (unsigned g = 0; g < NB_GPIO; ++g)
        if (b->reliee[g])
            hs |= 1u << g;
    return hs;
}

static const char *nom_de(const char *const nom[NB_GPIO], unsigned g) {
    return nom[g] ? nom[g] : "?";
}

int broches_afficher_defauts(const bilan_t *b, const char *const nom[NB_GPIO]) {
    int n = 0;
    for (unsigned g = 0; g < NB_GPIO; ++g) {
        const uint32_t m = 1u << g;
        if (b->ne_monte_pas & m) {
            printf("  ✘ GP%u (patte %u, %s) ne monte pas : court-circuit à la masse, "
                   "ou sortie du Pico abîmée\n",
                   g, PATTE[g], nom_de(nom, g));
            n++;
        }
        if (b->ne_descend_pas & m) {
            printf("  ✘ GP%u (patte %u, %s) ne descend pas : court-circuit vers une "
                   "tension, ou sortie du Pico abîmée\n",
                   g, PATTE[g], nom_de(nom, g));
            n++;
        }
    }
    for (unsigned i = 0; i < NB_GPIO; ++i)
        for (unsigned j = i + 1; j < NB_GPIO; ++j)
            if (b->reliee[i] & (1u << j)) {
                printf("  ✘ GP%u (patte %u, %s) et GP%u (patte %u, %s) reliées : "
                       "pont de soudure ?\n",
                       i, PATTE[i], nom_de(nom, i), j, PATTE[j], nom_de(nom, j));
                n++;
            }
    return n;
}

int broches_compter_defauts(const bilan_t *b) {
    int n = __builtin_popcount(b->ne_monte_pas) + __builtin_popcount(b->ne_descend_pas);
    for (unsigned g = 0; g < NB_GPIO; ++g)
        n += __builtin_popcount(b->reliee[g] & ~((2u << g) - 1u)); /* chaque paire une fois */
    return n;
}

void afficher_broches(uint32_t masque) {
    for (unsigned g = 0; g < NB_GPIO; ++g)
        if (masque & (1u << g))
            printf(" GP%u", g);
}

/* ====================================================== motif (cœur 1) */

#define MESURES_MAX 8

/* Deux tables : le cœur 0 prépare l'une pendant que le cœur 1 joue l'autre. */
static uint32_t table[2][PAS_PAR_PERIODE];
static const uint32_t *volatile table_active;
static uint8_t codes[NB_GPIO];
static uint32_t sorties;
static uint8_t mesures[MESURES_MAX];
static unsigned nb_mesures;

static volatile bool demande;
static uint32_t releve_hauts[MESURES_MAX];
static uint32_t releve_total;

static void construire(uint32_t *t) {
    for (unsigned j = 0; j < PAS_PAR_PERIODE; ++j) {
        uint32_t v = 0;
        for (unsigned g = 0; g < NB_GPIO; ++g)
            if (((sorties >> g) & 1u) && j < codes[g])
                v |= 1u << g;
        t[j] = v;
    }
}

/* Un pas par microseconde, cadencé par le timer : la période ne dépend pas du
 * code. Entre deux pas, la boucle échantillonne les entrées mesurées, plusieurs
 * fois par microseconde. Le code est en RAM : pas d'attente du cache de la
 * flash. Accès directs au SIO, partagé par les deux cœurs ; le basculement ne
 * touche que les bits des sorties du motif (la LED reste au cœur 0). */
static void __not_in_flash_func(coeur1)(void) {
    uint32_t hauts[MESURES_MAX] = {0};
    uint32_t total = 0;
    uint32_t t = time_us_32();
    for (;;) {
        const uint32_t *tab = table_active;
        for (unsigned j = 0; j < PAS_PAR_PERIODE; ++j) {
            sio_hw->gpio_togl = (sio_hw->gpio_out ^ tab[j]) & sorties;
            const uint32_t fin = t + j + 1;
            do {
                const uint32_t e = sio_hw->gpio_in;
                for (unsigned m = 0; m < nb_mesures; ++m)
                    hauts[m] += (e >> mesures[m]) & 1u;
                ++total;
            } while ((int32_t)(time_us_32() - fin) < 0);
        }
        t += PAS_PAR_PERIODE;
        if (demande) {
            for (unsigned m = 0; m < nb_mesures; ++m) {
                releve_hauts[m] = hauts[m];
                hauts[m] = 0;
            }
            releve_total = total;
            total = 0;
            __dmb();
            demande = false;
        }
    }
}

void motif_demarrer(uint32_t s, const uint8_t code[NB_GPIO], const uint8_t *m, unsigned nb) {
    sorties = s;
    memcpy(codes, code, NB_GPIO);
    nb_mesures = nb < MESURES_MAX ? nb : MESURES_MAX;
    memcpy(mesures, m, nb_mesures);
    construire(table[0]);
    table_active = table[0];

    for (unsigned g = 0; g < NB_GPIO; ++g) {
        if (!(sorties & (1u << g)))
            continue;
        gpio_init(g);
        gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_dir(g, GPIO_OUT);
    }
    for (unsigned i = 0; i < nb_mesures; ++i) {
        gpio_init(mesures[i]);
        gpio_set_pulls(mesures[i], true, false);
    }
    multicore_launch_core1(coeur1);
}

void motif_code(unsigned g, uint8_t code) {
    if (g >= NB_GPIO || codes[g] == code)
        return;
    codes[g] = code;
    uint32_t *libre = (table_active == table[0]) ? table[1] : table[0];
    construire(libre);
    __dmb();
    table_active = libre;
}

void motif_relever(float pourcent[]) {
    demande = true;
    while (demande)
        tight_loop_contents();
    __dmb();
    for (unsigned m = 0; m < nb_mesures; ++m)
        pourcent[m] = releve_total ? 100.0f * (float)releve_hauts[m] / (float)releve_total : 0.0f;
}

/* ======================================================= petits outils */

float adc_volts(unsigned entree) {
    adc_select_input(entree);
    uint32_t somme = 0;
    for (int i = 0; i < 64; ++i)
        somme += adc_read();
    return (float)somme / 64.0f * 3.3f / 4096.0f;
}

void afficher_volts(float v) {
    if (v < 0.0f)
        v = 0.0f;
    const int c = (int)(v * 100.0f + 0.5f);
    printf("%d,%02d V", c / 100, c % 100);
}
