/**
 * Module ÉCRAN — test de continuité au multimètre
 *
 * Met une seule broche du Pico à 3,3 V à la fois, toutes les autres à 0 V,
 * et annonce sur la console quelle broche du connecteur HUB75 doit être à
 * 3,3 V. À vérifier au multimètre **sur la dalle**, pas côté Pico : c'est
 * toute la liaison (soudure, embase, nappe) qui est testée.
 *
 * Un fil coupé donne 0 V ; deux fils inversés donnent 3,3 V sur la mauvaise
 * broche ; un court-circuit donne 3,3 V sur deux broches à la fois.
 *
 * Sécurité : le panneau est d'abord vidé (zéros décalés dans les registres
 * puis verrouillés), sinon /OE à 0 V allumerait des LED en continu.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define CLK_PIN    11
#define STROBE_PIN 12
#define OEN_PIN    13

typedef struct {
    uint gpio;
    const char *signal;
    const char *serigraphie;
    int broche_idc;
} liaison_t;

/* Ordre du plan §2.3. La sérigraphie de la dalle Seengreat diffère des noms
 * usuels : A-E y sont notés LA-LE, et /OE est noté CE. */
static const liaison_t liaisons[] = {
    { 0, "R1",  "R1",   1},
    { 1, "G1",  "G1",   2},
    { 2, "B1",  "B1",   3},
    { 3, "R2",  "R2",   5},
    { 4, "G2",  "G2",   6},
    { 5, "B2",  "B2",   7},
    { 6, "A",   "LA",   9},
    { 7, "B",   "LB",  10},
    { 8, "C",   "LC",  11},
    { 9, "D",   "LD",  12},
    {10, "E",   "LE",   8},
    {11, "CLK", "CLK", 13},
    {12, "LAT", "LAT", 14},
    {13, "/OE", "CE",  15},
};

#define NB_LIAISONS (sizeof(liaisons) / sizeof(liaisons[0]))

/* Décale des zéros dans les registres de la dalle et les verrouille, pour
 * qu'aucune LED ne puisse s'allumer pendant le test. */
static void vider_panneau(void) {
    for (uint i = 0; i <= 13; ++i) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
    gpio_put(OEN_PIN, 1); /* sorties désactivées (actif bas) */

    for (int i = 0; i < 256; ++i) { /* large : couvre une chaîne de 3 dalles */
        gpio_put(CLK_PIN, 1);
        sleep_us(1);
        gpio_put(CLK_PIN, 0);
        sleep_us(1);
    }
    gpio_put(STROBE_PIN, 1);
    sleep_us(10);
    gpio_put(STROBE_PIN, 0);
}

int main(void) {
    stdio_init_all();
    vider_panneau();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=============================================================\n");
    printf(" TEST DE CONTINUITE AU MULTIMETRE\n");
    printf("-------------------------------------------------------------\n");
    printf(" Pointe noire sur une masse de la dalle (broche 4 ou 16),\n");
    printf(" pointe rouge sur la broche annoncee, cote DALLE.\n");
    printf(" Attendu : 3,3 V sur celle-la, 0 V sur toutes les autres.\n");
    printf(" 6 s par broche, la sequence tourne en boucle.\n");
    printf("=============================================================\n\n");

    uint32_t n = 0;
    while (true) {
        const liaison_t *l = &liaisons[n % NB_LIAISONS];

        for (size_t i = 0; i < NB_LIAISONS; ++i)
            gpio_put(liaisons[i].gpio, 0);
        gpio_put(l->gpio, 1);

        printf("[%2u/%u] GP%-2u -> signal %-3s (serigraphie \"%s\") = broche IDC %d\n",
               (unsigned)(n % NB_LIAISONS) + 1, (unsigned)NB_LIAISONS,
               l->gpio, l->signal, l->serigraphie, l->broche_idc);
        printf("        mesure 3,3 V sur la broche %d de la dalle\n\n", l->broche_idc);

        sleep_ms(6000);
        ++n;
    }
}
