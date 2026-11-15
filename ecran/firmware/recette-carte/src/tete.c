/**
 * Recette de la carte de l'écran — la tête, support U1
 *
 * Pour le Pico 2 W de la tête, enfiché en U1 et alimenté par le bus. Sans
 * radio : construit pour la carte « pico2 », ce firmware ne touche jamais
 * GP23 à GP25 ni GP29, les broches de la puce WiFi.
 *
 * Au démarrage, contrôle des broches : ponts de soudure sur le support, RDY
 * tirés vers le 3,3 V par R33 à R35, CS de chaque liaison tiré vers la masse
 * par R45 et R41 (support de nœud vide) ou vers le 3,3 V (nœud alimenté).
 * Puis, en permanence :
 *   - un code sur chaque ligne de liaison (L0 10-25 %, L1 30-45 %, L2
 *     50-65 %, VSYNC 80 %), que les nœuds reconnaissent ;
 *   - un code sur chaque broche de J3 (0,4 V par numéro de broche) et sur
 *     GP17, GP18 (J7.9 ≈ 1,0 V, J7.11 ≈ 2,0 V), pour le multimètre ;
 *   - la mesure du code que chaque nœud renvoie sur son RDY : il dit s'il
 *     reçoit sa liaison complète ;
 *   - la tension du bus, par R2/R3 sur GP27, et celle de J2.2 (GP26).
 * Compte rendu sur la console USB toutes les 2 s.
 */

#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "banc.h"

/* GP26 et GP27 sont des entrées analogiques : on ne les commande pas. */
#define MASQUE_ESSAI (0x007FFFFFu | (1u << 28))             /* GP0-GP22, GP28 */
#define SORTIES (0x00001FFFu | 0x007F0000u | (1u << 28))    /* GP0-12, GP16-22, GP28 */
#define PIN_RDY0 13
#define PIN_CS(k) (4 * (k) + SIG_CS)
#define PIN_POT 26
#define PIN_BUS 27
#define ADC_POT 0
#define ADC_BUS 1

static const char *const NOM[NB_GPIO] = {
    [0] = "L0 D0",  [1] = "L0 D1",  [2] = "L0 CLK",  [3] = "L0 CS",  [4] = "L1 D0",
    [5] = "L1 D1",  [6] = "L1 CLK", [7] = "L1 CS",   [8] = "L2 D0",  [9] = "L2 D1",
    [10] = "L2 CLK", [11] = "L2 CS", [12] = "VSYNC", [13] = "RDY0",  [14] = "RDY1",
    [15] = "RDY2",  [16] = "J3.2",  [17] = "J7.9",   [18] = "J7.11", [19] = "J3.3",
    [20] = "J3.4",  [21] = "J3.5",  [22] = "J3.6",   [28] = "J3.7",
};

typedef enum { POSTE_TETE, POSTE_NOEUD, POSTE_HORS_CARTE } poste_t;

static bilan_t bilan;
static poste_t poste;
static uint32_t hs;

static poste_t deviner_poste(const bilan_t *b) {
    const uint32_t vivantes = b->tiree_haut | b->pilotees;
    /* GP22 : J3.6, en l'air sur la tête ; sur un nœud, le CS de sa liaison,
     * tiré par R45, R55 ou R65. */
    if (vivantes & (1u << 22))
        return POSTE_NOEUD;
    /* GP13-GP15 : R33-R35 vers le 3,3 V, ou les RDY des nœuds qui battent. */
    if (vivantes & (7u << PIN_RDY0))
        return POSTE_TETE;
    return POSTE_HORS_CARTE;
}

static void codes_tete(uint8_t code[NB_GPIO]) {
    memset(code, 0, NB_GPIO);
    for (int k = 0; k < 3; ++k)
        for (int s = 0; s < 4; ++s)
            code[4 * k + s] = CODE_LIEN(k, s);
    code[12] = CODE_VSYNC;
    code[17] = CODE_GP17;
    code[18] = CODE_GP18;
    /* J3 : 0,4 V par numéro de broche, sous 3,3 V */
    code[16] = 24; /* J3.2 */
    code[19] = 36; /* J3.3 */
    code[20] = 48; /* J3.4 */
    code[21] = 61; /* J3.5 */
    code[22] = 73; /* J3.6 */
    code[28] = 85; /* J3.7 */
}

static void controle_afficher(void) {
    printf("Broches, %d passes au démarrage :\n", bilan.passes);
    int defauts = broches_afficher_defauts(&bilan, NOM);
    for (int k = 0; k < 3; ++k) {
        const unsigned g = PIN_RDY0 + k;
        const uint32_t m = 1u << g;
        if ((bilan.pilotees | bilan.tiree_haut | hs) & m)
            continue;
        printf("  ✘ RDY%d (GP%u, patte %u) n'est pas tiré vers le 3,3 V : R%d absente, "
               "ou patte %u du support non soudée\n",
               k, g, PATTE[g], 33 + k, PATTE[g]);
        defauts++;
    }
    for (int k = 0; k < 3; ++k) {
        const unsigned g = PIN_CS(k);
        const uint32_t m = 1u << g;
        if ((bilan.pilotees | bilan.tiree_haut | bilan.tiree_bas | hs) & m)
            continue;
        printf("  ✘ CS de L%d (GP%u, patte %u) en l'air : ni R%d et R%d (support U%d "
               "vide) ni un nœud alimenté ne le tirent ; piste coupée ?\n",
               k, g, PATTE[g], 45 + 10 * k, 41 + 10 * k, 2 + k);
        defauts++;
    }
    if (!defauts)
        printf("  ✔ aucun court-circuit, aucune broche en défaut\n");

    printf("  Supports des nœuds, d'après le CS de leur liaison :");
    for (int k = 0; k < 3; ++k) {
        const uint32_t m = 1u << PIN_CS(k);
        printf("%s U%d ", k ? "," : "", 2 + k);
        if (bilan.tiree_bas & m)
            printf("vide");
        else if (bilan.tiree_haut & m)
            printf("occupé");
        else
            printf("?");
    }
    printf("\n");
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
}

static void alimentation_afficher(void) {
    const float bus = 2.0f * adc_volts(ADC_BUS);
    printf("Bus +5 V, par R2/R3 sur GP27 : ");
    afficher_volts(bus);
    if (bus < 4.5f)
        printf("  ✘ absent ou trop bas : bus éteint, Pico sur USB seul ?");
    else if (bus > 5.35f)
        printf("  ✘ trop haut : 5,0 à 5,15 V attendus");
    else
        printf("  ✔");
    printf("\nJ2.2 (GP26, potentiomètre) : ");
    afficher_volts(adc_volts(ADC_POT));
    printf("\n");
}

static void noeuds_afficher(void) {
    float rdy[3];
    motif_relever(rdy);
    printf("Nœuds, d'après le code de leur RDY :\n");
    for (int k = 0; k < 3; ++k) {
        const float d = rdy[k];
        printf("  L%d → U%d   RDY%d %3d %%   ", k, 2 + k, k, (int)(d + 0.5f));
        if (d >= 99.0f) {
            printf("aucun nœud ne répond : support vide, ou nœud sans le firmware de recette\n");
            continue;
        }
        if (d <= 1.0f) {
            printf("✘ RDY tenu à 0 : nœud hors tension, ou court-circuit\n");
            continue;
        }
        if (proche(d, CODE_RDY_SANS_TETE, TOLERANCE_RDY)) {
            printf("✘ un nœud est là, mais ne reconnaît pas sa liaison : voir sa console\n");
            continue;
        }
        int j;
        for (j = 0; j < 3; ++j) {
            if (proche(d, CODE_RDY_OK(j), TOLERANCE_RDY)) {
                if (j == k)
                    printf("✔ nœud %d : les 5 signaux de sa liaison lui arrivent\n", k);
                else
                    printf("✘ le nœud de U%d reçoit la liaison L%d : liaisons croisées\n", 2 + k, j);
                break;
            }
            if (proche(d, CODE_RDY_DEFAUT(j), TOLERANCE_RDY)) {
                printf("✘ nœud %d : un signal de sa liaison manque ou est faux, voir sa console%s\n",
                       j, j == k ? "" : " (liaisons croisées)");
                break;
            }
        }
        if (j == 3)
            printf("✘ code inconnu : RDY mêlé à un autre signal ?\n");
    }
}

int main(void) {
    stdio_init_all();
    adc_init();
    adc_gpio_init(PIN_POT);
    adc_gpio_init(PIN_BUS);

    /* Les Pico de la carte démarrent ensemble : un délai au hasard évite
     * qu'ils fassent leur contrôle exactement au même moment. */
    sleep_ms(50 + get_rand_32() % 300);
    broches_eprouver(MASQUE_ESSAI, 8, &bilan);
    poste = deviner_poste(&bilan);
    hs = broches_hs(&bilan);
    if (poste == POSTE_TETE) {
        uint8_t code[NB_GPIO];
        codes_tete(code);
        static const uint8_t mesures[3] = {PIN_RDY0, PIN_RDY0 + 1, PIN_RDY0 + 2};
        motif_demarrer(SORTIES & ~hs, code, mesures, 3);
        /* Les nœuds, démarrés en même temps, fixent leur code de RDY dans la
         * seconde qui suit : ce premier relevé, à cheval, est jeté. */
        sleep_ms(1500);
        float jete[3];
        motif_relever(jete);
    }

    absolute_time_t prochain = make_timeout_time_ms(2000);
    for (;;) {
        sleep_until(prochain);
        prochain = delayed_by_ms(prochain, 2000);
        printf("\n── Recette de la carte · TÊTE (U1) · %lu s ──\n",
               (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
        if (poste == POSTE_NOEUD) {
            printf("✘ Ce Pico est dans un support de nœud (U2 à U4) : rien n'est piloté. "
                   "La tête va en U1 ; un nœud prend recette_noeud.uf2.\n");
            continue;
        }
        if (poste == POSTE_HORS_CARTE) {
            printf("✘ Ni R33-R35 ni RDY qui bat sur GP13-GP15 : ce Pico n'est pas dans le "
                   "support U1, ou R33 à R35 manquent. Rien n'est piloté.\n");
            continue;
        }
        controle_afficher();
        alimentation_afficher();
        noeuds_afficher();
        printf("Au multimètre : J7.1 (VSYNC) ≈ 2,6 V ; J7.9 ≈ 1,0 V ; J7.11 ≈ 2,0 V ; "
               "J3.n ≈ 0,4 V × n\n");
    }
}
