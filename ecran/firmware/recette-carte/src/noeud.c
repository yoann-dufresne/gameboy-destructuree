/**
 * Recette de la carte de l'écran — un nœud, supports U2 à U4
 *
 * Pour un Pico 2 enfiché dans un support de nœud. Au démarrage, contrôle des
 * broches : ponts de soudure sur le support, /OE tiré vers le 3,3 V par R43
 * (R53, R63), CS par R45 (R55, R65). Puis, en permanence :
 *   - un code sur chaque sortie vers les tampons 74AHCT541 : la broche n de
 *     la nappe HUB75 porte 6 n %, soit 0,3 V × n sous 5,0 V ; GP15, le G1 de
 *     rechange, 96 %, qui n'arrive à la broche 2 que par le cavalier en 2-3 ;
 *   - GP17 et GP18 vers J7 : 30 et 60 %, soit ≈ 1,0 et 2,0 V ;
 *   - la mesure des cinq lignes venues de la tête (D0, D1, CLK, CS, VSYNC).
 *     Leurs codes disent de quelle liaison il s'agit, donc dans quel support
 *     est le nœud ;
 *   - un code sur RDY, qui rend compte à la tête : liaison k complète,
 *     liaison k avec un signal faux, ou aucune liaison reconnue.
 *
 * LED du Pico : fixe, tout va bien ; clignotement lent, rien d'anormal mais
 * pas de tête ; clignotement rapide, un défaut : voir la console.
 */

#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "banc.h"

#define MASQUE_ESSAI (0x007FFFFFu | (7u << 26)) /* GP0-GP22, GP26-GP28 */
#define SORTIES (0x0000FFFFu | (3u << 17))      /* GP0-GP15, GP17, GP18 */
#define PIN_OE 13
#define PIN_RDY 14
#define PIN_G1_RECHANGE 15
#define PIN_VSYNC 16
#define PIN_CS 22
#define PIN_VBUS 24 /* Pico 2 : 5 V de l'USB présent */
#define PIN_LED 25  /* Pico 2 */
#define PIN_VSYS 29 /* Pico 2 : VSYS / 3 */
#define ADC_VSYS 3

/* Broche de la nappe HUB75 de chaque sortie GP0-GP13 (plan §2.3 : E sur la 8). */
static const uint8_t BROCHE_NAPPE[14] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 12, 8, 13, 14, 15};

static const char *const NOM[NB_GPIO] = {
    [0] = "R1",      [1] = "G1 (JP 1)", [2] = "B1",    [3] = "R2",    [4] = "G2",
    [5] = "B2",      [6] = "A",         [7] = "B",     [8] = "C",     [9] = "D",
    [10] = "E",      [11] = "CLK",      [12] = "LAT",  [13] = "/OE",  [14] = "RDY",
    [15] = "G1 de rechange (JP 3)",     [16] = "VSYNC", [17] = "J7 .17",
    [18] = "J7 .18", [19] = "D0",       [20] = "D1",   [21] = "CLK de la liaison",
    [22] = "CS",     [26] = "libre",    [27] = "libre", [28] = "libre",
};

/* Lignes venues de la tête, dans l'ordre SIG_D0..SIG_CS, puis VSYNC. */
#define NB_LIGNES 5
#define LIGNE_VSYNC 4
static const uint8_t MESURES[NB_LIGNES] = {19, 20, 21, PIN_CS, PIN_VSYNC};
static const char *const SIGNAL[NB_LIGNES] = {"D0", "D1", "CLK", "CS", "VSYNC"};
static const char *const RANGEE[3] = {"du haut", "du milieu", "du bas"};

typedef enum { POSTE_TETE, POSTE_NOEUD, POSTE_HORS_CARTE } poste_t;
typedef enum { LED_FIXE, LED_LENTE, LED_RAPIDE } led_t;

static bilan_t bilan;
static poste_t poste;
static uint32_t hs;

static poste_t deviner_poste(const bilan_t *b) {
    const uint32_t vivantes = b->tiree_haut | b->pilotees;
    /* GP15 : R35 vers le 3,3 V sur la tête ; sur un nœud, il ne va qu'au
     * cavalier JP. */
    if (vivantes & (1u << PIN_G1_RECHANGE))
        return POSTE_TETE;
    /* R43 sur /OE, R45 sur CS, R44 sur VSYNC, vers le 3,3 V du nœud ; ou la
     * tête qui pilote CS et VSYNC. */
    if (vivantes & ((1u << PIN_OE) | (1u << PIN_CS) | (1u << PIN_VSYNC)))
        return POSTE_NOEUD;
    return POSTE_HORS_CARTE;
}

static void codes_noeud(uint8_t code[NB_GPIO]) {
    memset(code, 0, NB_GPIO);
    for (unsigned g = 0; g < 14; ++g)
        code[g] = CODE_HUB75(BROCHE_NAPPE[g]);
    code[PIN_RDY] = CODE_RDY_SANS_TETE;
    code[PIN_G1_RECHANGE] = CODE_G1_RECHANGE;
    code[17] = CODE_GP17;
    code[18] = CODE_GP18;
}

/* R43 (/OE) et R45 (CS) tirent ces broches vers le 3,3 V du nœud ; R53 et
 * R55 pour le nœud 1, R63 et R65 pour le nœud 2. Rend le nombre de tirages
 * manquants ; les écrit si afficher. */
static int attentes(int k, bool afficher) {
    static const struct {
        uint8_t gpio;
        uint8_t r;
        const char *role;
    } ATTENTE[2] = {{PIN_OE, 43, "/OE"}, {PIN_CS, 45, "CS"}};
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        const unsigned g = ATTENTE[i].gpio;
        if ((bilan.pilotees | bilan.tiree_haut | hs) & (1u << g))
            continue;
        n++;
        if (!afficher)
            continue;
        printf("  ✘ %s (GP%u, patte %u) n'est pas tiré vers le 3,3 V : ", ATTENTE[i].role, g,
               PATTE[g]);
        if (k >= 0)
            printf("R%d absente", ATTENTE[i].r + 10 * k);
        else
            printf("R%d, R%d ou R%d absente (selon le support)", ATTENTE[i].r,
                   ATTENTE[i].r + 10, ATTENTE[i].r + 20);
        printf(", ou patte %u du support non soudée\n", PATTE[g]);
    }
    return n;
}

typedef enum { RIEN, RECONNU, MELANGE } genre_t;
typedef struct {
    genre_t genre;
    int lien; /* 0..2, -1 pour VSYNC */
    int sig;  /* SIG_D0..SIG_CS, LIGNE_VSYNC */
} lecture_t;

static lecture_t lire(float d) {
    lecture_t l = {RIEN, -1, -1};
    if (d < 1.0f || d > 99.0f)
        return l; /* constant : pas de signal */
    for (int k = 0; k < 3; ++k)
        for (int s = 0; s < 4; ++s)
            if (proche(d, CODE_LIEN(k, s), TOLERANCE_LIEN)) {
                l.genre = RECONNU;
                l.lien = k;
                l.sig = s;
                return l;
            }
    if (proche(d, CODE_VSYNC, TOLERANCE_LIEN)) {
        l.genre = RECONNU;
        l.sig = LIGNE_VSYNC;
        return l;
    }
    l.genre = MELANGE;
    return l;
}

/* Écrit ce qui ne va pas sur la ligne i, attendue de la liaison k (-1 : on ne
 * sait pas laquelle). */
static void decrire_defaut(int i, int k, lecture_t l) {
    if (l.genre == MELANGE) {
        printf("✘ codes mêlés : court-circuit avec une ligne voisine ?\n");
        return;
    }
    if (l.genre == RECONNU) {
        if (l.sig == LIGNE_VSYNC)
            printf("✘ reçoit VSYNC : lignes croisées\n");
        else
            printf("✘ reçoit L%d %s : lignes croisées\n", l.lien, SIGNAL[l.sig]);
        return;
    }
    if (i == LIGNE_VSYNC) {
        printf("✘ pas de signal : coupure entre GP12 de la tête (patte 16), ");
        if (k >= 0)
            printf("R%d", 30 + k);
        else
            printf("R30 à R32");
        printf(" et la patte %u de ce support\n", PATTE[PIN_VSYNC]);
    } else if (k >= 0) {
        printf("✘ pas de signal : coupure entre GP%d de la tête (patte %u), RN%d et la "
               "patte %u de ce support\n",
               4 * k + i, PATTE[4 * k + i], 1 + k, PATTE[MESURES[i]]);
    } else {
        printf("✘ pas de signal\n");
    }
}

/* Ce que le nœud sait de sa liaison, relu toutes les 0,5 s. */
static struct {
    float pct[NB_LIGNES];
    lecture_t l[NB_LIGNES];
    int k;          /* liaison reconnue, -1 : aucune */
    bool attendu[NB_LIGNES];
    bool complete;  /* les cinq lignes portent le code attendu */
    bool anomalie;  /* une ligne porte un code faux, ou mêlé */
    int code_rdy;
} lu;
static int defauts; /* défauts du contrôle des broches, comptés une fois */

static bool est_attendu(int i, int k, lecture_t l) {
    if (l.genre != RECONNU)
        return false;
    if (i == LIGNE_VSYNC)
        return l.sig == LIGNE_VSYNC;
    return l.lien == k && l.sig == i;
}

/* Relève les codes, en déduit la liaison et le code de RDY, rend l'état de la
 * LED. Assez souvent pour que la tête lise un RDY à jour dès sa première
 * mesure. */
static led_t analyser(void) {
    motif_relever(lu.pct);
    for (int i = 0; i < NB_LIGNES; ++i)
        lu.l[i] = lire(lu.pct[i]);

    /* Le nœud se reconnaît aux codes de sa liaison : vote des quatre lignes. */
    int votes[3] = {0, 0, 0};
    for (int i = 0; i < 4; ++i)
        if (lu.l[i].genre == RECONNU && lu.l[i].lien >= 0)
            votes[lu.l[i].lien]++;
    lu.k = -1;
    int meilleur = 0;
    for (int j = 0; j < 3; ++j)
        if (votes[j] > meilleur) {
            meilleur = votes[j];
            lu.k = j;
        }

    lu.complete = lu.k >= 0;
    lu.anomalie = false;
    for (int i = 0; i < NB_LIGNES; ++i) {
        lu.attendu[i] = est_attendu(i, lu.k, lu.l[i]);
        if (lu.attendu[i])
            continue;
        lu.complete = false;
        if (!(lu.k < 0 && lu.l[i].genre == RIEN))
            lu.anomalie = true;
    }

    led_t led;
    if (lu.k < 0) {
        lu.code_rdy = CODE_RDY_SANS_TETE;
        led = lu.anomalie ? LED_RAPIDE : LED_LENTE;
    } else if (lu.complete) {
        lu.code_rdy = CODE_RDY_OK(lu.k);
        led = LED_FIXE;
    } else {
        lu.code_rdy = CODE_RDY_DEFAUT(lu.k);
        led = LED_RAPIDE;
    }
    motif_code(PIN_RDY, (uint8_t)lu.code_rdy);
    return defauts ? LED_RAPIDE : led;
}

static void afficher(void) {
    printf("\n── Recette de la carte · NŒUD · %lu s ──\n",
           (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
    if (poste == POSTE_TETE) {
        printf("✘ Ce Pico est dans le support de la tête (U1) : rien n'est piloté. "
               "Un nœud va en U2 à U4 ; la tête prend recette_tete.uf2.\n");
        return;
    }
    if (poste == POSTE_HORS_CARTE) {
        printf("✘ Ni /OE, ni CS, ni VSYNC tirés vers le 3,3 V : ce Pico n'est pas dans un "
               "support de nœud, ou R43 et R45 manquent. Rien n'est piloté.\n");
        return;
    }

    const int k = lu.k;
    printf("Broches, %d passes au démarrage :\n", bilan.passes);
    if (!(broches_afficher_defauts(&bilan, NOM) + attentes(k, true)))
        printf("  ✔ aucun court-circuit, aucune broche en défaut\n");
    if (bilan.pilotees) {
        printf("  · non essayées, un autre Pico les pilotait :");
        afficher_broches(bilan.pilotees);
        printf("\n");
    }
    if (hs & SORTIES) {
        printf("  ⚠ laissées en entrée, à cause d'un défaut :");
        afficher_broches(hs & SORTIES);
        printf("\n");
    }

    printf("Alimentation : VSYS ");
    afficher_volts(3.0f * adc_volts(ADC_VSYS));
    printf(gpio_get(PIN_VBUS) ? ", USB branché\n" : ", par le bus seul\n");
    printf("Nappe HUB75 : broche n ≈ 0,3 V × n sous 5,0 V, 4 et 16 à 0 V ; "
           "cavalier JP en 2-3 : broche 2 ≈ 4,8 V\n");
    printf("Sonde J7 : .17 ≈ 1,0 V, .18 ≈ 2,0 V\n");

    printf("Liaison depuis la tête :\n");
    for (int i = 0; i < NB_LIGNES; ++i) {
        printf("  GP%-2u %-5s %3d %%   ", MESURES[i], SIGNAL[i], (int)(lu.pct[i] + 0.5f));
        if (lu.attendu[i]) {
            if (i == LIGNE_VSYNC)
                printf("✔ VSYNC\n");
            else
                printf("✔ L%d %s\n", k, SIGNAL[i]);
        } else if (k < 0 && lu.l[i].genre == RIEN) {
            printf("pas de signal\n");
        } else {
            decrire_defaut(i, k, lu.l[i]);
        }
    }

    if (k < 0)
        printf("⇒ aucune liaison reconnue : pas de tête, ou tête muette. RDY renvoie %d %%.\n",
               lu.code_rdy);
    else if (lu.complete)
        printf("⇒ nœud %d, support U%d, rangée %s : liaison complète. RDY renvoie %d %%.\n", k,
               2 + k, RANGEE[k], lu.code_rdy);
    else
        printf("⇒ nœud %d, support U%d : liaison incomplète. RDY renvoie %d %%.\n", k, 2 + k,
               lu.code_rdy);
}

int main(void) {
    stdio_init_all();
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 1); /* allumée pendant le contrôle */
    gpio_init(PIN_VBUS);
    adc_init();
    adc_gpio_init(PIN_VSYS);

    /* Les Pico de la carte démarrent ensemble : un délai au hasard évite
     * qu'ils fassent leur contrôle exactement au même moment. */
    sleep_ms(50 + get_rand_32() % 300);
    broches_eprouver(MASQUE_ESSAI, 8, &bilan);
    poste = deviner_poste(&bilan);
    hs = broches_hs(&bilan);
    defauts = broches_compter_defauts(&bilan) + attentes(-1, false);
    led_t led = LED_RAPIDE;
    if (poste == POSTE_NOEUD) {
        uint8_t code[NB_GPIO];
        codes_noeud(code);
        motif_demarrer(SORTIES & ~hs, code, MESURES, NB_LIGNES);
        led = defauts ? LED_RAPIDE : LED_LENTE;
    }

    /* La liaison est relue toutes les 0,5 s, et RDY suit : la tête, démarrée
     * en même temps, le lit à jour dès sa première mesure. Compte rendu sur
     * la console toutes les 2 s. */
    absolute_time_t analyse = make_timeout_time_ms(300);
    absolute_time_t rapport = make_timeout_time_ms(2000);
    for (uint32_t tic = 0;; ++tic) {
        sleep_ms(50);
        if (poste == POSTE_NOEUD && time_reached(analyse)) {
            led = analyser();
            analyse = delayed_by_ms(analyse, 500);
        }
        if (time_reached(rapport)) {
            afficher();
            rapport = delayed_by_ms(rapport, 2000);
        }
        bool allumee = true;
        if (led == LED_LENTE)
            allumee = (tic / 10) % 2;
        else if (led == LED_RAPIDE)
            allumee = (tic / 2) % 2;
        gpio_put(PIN_LED, allumee);
    }
}
