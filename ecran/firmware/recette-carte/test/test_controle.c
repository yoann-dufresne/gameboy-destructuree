/* Simulation électrique de la carte, pour éprouver broches_eprouver() sur PC.
 *
 * Chaque broche est un nœud résistif : sortie du Pico (150 Ω à 2 mA, 40 Ω à
 * 12 mA), tirages internes de 50 kΩ, résistances de la carte, pont de
 * soudure, court-circuit, sortie qui ne descend plus, autre Pico qui pilote
 * la ligne ou fait son propre contrôle. Le faux pico-sdk de test/faux/ ne sert
 * qu'à compiler banc.c tel quel. Depuis firmware/recette-carte :
 *
 *   gcc -std=gnu11 -O2 -Wall -Wextra -Itest/faux -Isrc \
 *       test/test_controle.c src/banc.c -o /tmp/test_controle && /tmp/test_controle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "faux_sdk.h"
#include "banc.h"

faux_sio_t faux_sio;

/* -------------------------------------------- broches, côté firmware */
typedef struct {
    bool sortie, niveau, pu, pd;
    int courant;
} pad_t;
static pad_t pad[NB_GPIO];

/* -------------------------------------------------------- la carte */
typedef struct {
    double r_haut;     /* résistance vers 3,3 V, Ω ; 0 = aucune */
    double r_bas;      /* résistance vers la masse */
    bool masse;        /* court-circuit franc à la masse */
    bool colle_haut;   /* sortie qui ne descend plus */
    int pont;          /* broche reliée par un pont de soudure ; -1 = aucune */
    uint32_t periode;  /* pilotée par un autre Pico : carré de cette période, µs */
    bool impulsions;   /* un autre Pico qui fait son contrôle : 40 µs à 0 toutes les 3 ms */
} ext_t;
static ext_t ext[NB_GPIO];
static double v_mem[NB_GPIO];
static uint64_t t_us;

static void carte_vide(void) {
    memset(pad, 0, sizeof pad);
    for (int g = 0; g < NB_GPIO; ++g) {
        ext[g] = (ext_t){0, 0, false, false, -1, 0, false};
        v_mem[g] = 0;
        pad[g].pd = true; /* état de la mise sous tension */
    }
}

static void pont(int a, int b) {
    ext[a].pont = b;
    ext[b].pont = a;
}

static void contributions(int g, double *G, double *GV) {
    const double VDD = 3.3;
    if (pad[g].sortie) {
        const double r = pad[g].courant == GPIO_DRIVE_STRENGTH_12MA ? 40.0 : 150.0;
        const double v = (pad[g].niveau || ext[g].colle_haut) ? VDD : 0.0;
        *G += 1.0 / r;
        *GV += v / r;
    }
    if (pad[g].pu) {
        *G += 1.0 / 50e3;
        *GV += VDD / 50e3;
    }
    if (pad[g].pd)
        *G += 1.0 / 50e3;
    if (ext[g].r_haut > 0) {
        *G += 1.0 / ext[g].r_haut;
        *GV += VDD / ext[g].r_haut;
    }
    if (ext[g].r_bas > 0)
        *G += 1.0 / ext[g].r_bas;
    if (ext[g].masse)
        *G += 1.0;
    if (ext[g].periode) {
        const bool haut = (t_us % ext[g].periode) < ext[g].periode / 2;
        *G += 1.0 / 100.0;
        *GV += (haut ? VDD : 0.0) / 100.0;
    }
    if (ext[g].impulsions && (t_us % 3000) < 40)
        *G += 1.0 / 100.0; /* tirée à 0 par l'autre Pico */
}

static double tension(int g) {
    double G = 0, GV = 0;
    contributions(g, &G, &GV);
    if (ext[g].pont >= 0)
        contributions(ext[g].pont, &G, &GV);
    const double v = G > 0 ? GV / G : v_mem[g];
    v_mem[g] = v;
    if (ext[g].pont >= 0)
        v_mem[ext[g].pont] = v;
    return v;
}

/* ------------------------------------------------------ faux SDK */
void gpio_init(uint g) {
    pad[g].sortie = false;
    pad[g].niveau = false;
    pad[g].courant = GPIO_DRIVE_STRENGTH_4MA;
}
void gpio_set_dir(uint g, bool s) { pad[g].sortie = s; }
void gpio_set_pulls(uint g, bool h, bool b) {
    pad[g].pu = h;
    pad[g].pd = b;
}
void gpio_set_drive_strength(uint g, enum gpio_drive_strength f) { pad[g].courant = f; }
void gpio_put(uint g, bool v) { pad[g].niveau = v; }
bool gpio_get(uint g) { return tension(g) > 1.65; }
uint32_t gpio_get_all(void) {
    uint32_t v = 0;
    for (int g = 0; g < NB_GPIO; ++g)
        if (tension(g) > 1.65)
            v |= 1u << g;
    return v;
}
void busy_wait_us(uint32_t us) { t_us += us; }
uint32_t time_us_32(void) { return (uint32_t)(t_us += 1); }
uint32_t get_rand_32(void) { return (uint32_t)rand(); }
void multicore_launch_core1(void (*f)(void)) { (void)f; }
void adc_select_input(uint e) { (void)e; }
uint16_t adc_read(void) { return 0; }

/* ------------------------------------------------------- scénarios */
#define MASQUE_NOEUD (0x007FFFFFu | (7u << 26))
#define MASQUE_TETE (0x007FFFFFu | (1u << 28))
#define B(g) (1u << (g))

static int echecs;
#define VERIFIE(cond, msg)                                                                   \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            printf("  ÉCHEC : %s\n", msg);                                                   \
            echecs++;                                                                        \
        }                                                                                    \
    } while (0)

static void noeud_seul(void) {
    carte_vide();
    ext[13].r_haut = 10e3; /* R43 */
    ext[22].r_haut = 10e3; /* R45 */
    ext[16].r_haut = 10e3; /* R44 … */
    ext[16].r_bas = 5.5e3; /* … contre les supports vides des deux autres nœuds */
}

static void tete_seule(void) {
    carte_vide();
    for (int k = 0; k < 3; ++k) {
        ext[4 * k + 3].r_bas = 11e3; /* R45 + R41, support de nœud vide */
        ext[13 + k].r_haut = 10e3;   /* R33-R35 */
    }
    ext[12].r_bas = 3.67e3; /* VSYNC : trois fois R44 + R41 */
}

static void bilan_ecrit(const char *titre, const bilan_t *b) {
    printf("%s\n  hs=%08x pilotees=%08x haut=%08x bas=%08x nd=%08x nm=%08x\n", titre,
           (unsigned)broches_hs(b), (unsigned)b->pilotees, (unsigned)b->tiree_haut,
           (unsigned)b->tiree_bas, (unsigned)b->ne_descend_pas, (unsigned)b->ne_monte_pas);
}

int main(void) {
    static bilan_t b;
    srand(1);

    noeud_seul();
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("1. nœud seul", &b);
    VERIFIE(broches_hs(&b) == 0, "aucun défaut attendu");
    VERIFIE(broches_compter_defauts(&b) == 0, "aucun défaut compté");
    VERIFIE((b.tiree_haut & (B(13) | B(22))) == (B(13) | B(22)), "R43 et R45 vues");
    VERIFIE(b.pilotees == 0, "rien de piloté");

    tete_seule();
    broches_eprouver(MASQUE_TETE, 8, &b);
    bilan_ecrit("2. tête seule", &b);
    VERIFIE(broches_hs(&b) == 0, "aucun défaut attendu");
    VERIFIE((b.tiree_bas & (B(3) | B(7) | B(11) | B(12))) == (B(3) | B(7) | B(11) | B(12)),
            "CS et VSYNC tirés vers la masse");
    VERIFIE((b.tiree_haut & (7u << 13)) == (7u << 13), "R33-R35 vues");

    noeud_seul();
    pont(4, 5);
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("3. nœud, pont GP4-GP5", &b);
    VERIFIE(b.reliee[4] == B(5) && b.reliee[5] == B(4), "pont 4-5 vu, et lui seul");
    VERIFIE(broches_hs(&b) == (B(4) | B(5)), "seules 4 et 5 écartées");
    VERIFIE(broches_compter_defauts(&b) == 1, "un pont compté une fois");

    noeud_seul();
    ext[2].masse = true;
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("4. nœud, GP2 à la masse", &b);
    VERIFIE(b.ne_monte_pas == B(2), "GP2 ne monte pas");
    VERIFIE(broches_hs(&b) == B(2), "seule GP2 écartée");
    VERIFIE(broches_compter_defauts(&b) == 1, "un défaut compté");

    noeud_seul();
    ext[13].r_haut = 0;
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("5. nœud, R43 absente", &b);
    VERIFIE(!(b.tiree_haut & B(13)) && (b.tiree_haut & B(22)), "R43 manque, R45 vue");

    noeud_seul();
    ext[1].colle_haut = true;
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("6. nœud, GP1 ne descend plus", &b);
    VERIFIE(b.ne_descend_pas == B(1), "GP1 ne descend pas");

    noeud_seul();
    for (int g = 19; g <= 22; ++g)
        ext[g].periode = 100;
    ext[16].periode = 100;
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("7. nœud, la tête pilote la liaison", &b);
    VERIFIE(b.pilotees == (B(16) | B(19) | B(20) | B(21) | B(22)), "liaison pilotée");
    VERIFIE(broches_hs(&b) == 0, "aucun défaut");

    tete_seule();
    for (int k = 0; k < 3; ++k) {
        ext[4 * k + 3].r_bas = 0;
        ext[4 * k + 3].r_haut = 10e3; /* nœud alimenté : R45 vers son 3,3 V */
        ext[13 + k].periode = 100;    /* son RDY bat */
    }
    ext[12].r_bas = 0;
    ext[12].r_haut = 3.3e3; /* VSYNC : trois R44 vers les 3,3 V des nœuds */
    for (int g = 0; g < 12; ++g)
        if (g % 4 != 3)
            ext[g].r_haut = 50e3; /* tirage interne de l'entrée du nœud */
    broches_eprouver(MASQUE_TETE, 8, &b);
    bilan_ecrit("8. tête, trois nœuds en marche", &b);
    VERIFIE(b.pilotees == (7u << 13), "RDY pilotés");
    VERIFIE((b.tiree_haut & (B(3) | B(7) | B(11))) == (B(3) | B(7) | B(11)), "nœuds présents");
    VERIFIE(broches_hs(&b) == 0, "aucun défaut");

    noeud_seul();
    pont(12, 13);
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("9. nœud, pont GP12-GP13 (GP13 tirée par R43)", &b);
    VERIFIE(b.reliee[12] == B(13), "pont 12-13 vu");

    noeud_seul();
    for (int g = 0; g < 23; ++g)
        if (g != 13 && g != 22 && g != 16)
            ext[g].impulsions = true; /* l'autre Pico fait son contrôle en même temps */
    broches_eprouver(MASQUE_NOEUD, 8, &b);
    bilan_ecrit("10. nœud, un autre Pico fait son contrôle en même temps", &b);
    VERIFIE(broches_hs(&b) == 0, "pas de faux défaut");

    printf("\n%s (%d échec%s)\n", echecs ? "ÉCHEC" : "OK", echecs, echecs > 1 ? "s" : "");
    return echecs != 0;
}
