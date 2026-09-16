/**
 * Module ÉCRAN — phase 0 : validation du câblage
 *
 * Fait défiler des mires de diagnostic sur une dalle HUB75 64x64.
 * Chaque mire isole un groupe de signaux : si quelque chose est mal câblé,
 * la mire qui échoue désigne le fautif.
 *
 * Le nom de la mire courante est écrit sur la console USB (USB CDC), avec
 * ce qu'elle valide et le symptôme attendu en cas de défaut.
 *
 * Brochage : voir docs/plan-firmware.md §2.3
 * PIO      : hub75.pio de pico-examples (BSD-3-Clause), non modifié
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"

#include "hub75.pio.h"

/* ---------------------------------------------------------------- brochage */

#define DATA_BASE_PIN   0   /* GP0..GP5  : R1 G1 B1 R2 G2 B2 */
#define DATA_N_PINS     6
#define ROWSEL_BASE_PIN 6   /* GP6..GP10 : A B C D E         */
#define ROWSEL_N_PINS   5
#define CLK_PIN         11
#define STROBE_PIN      12  /* LAT */
#define OEN_PIN         13  /* /OE, actif bas */

/* --------------------------------------------------------------- géométrie */

#define WIDTH      64
#define HEIGHT     64
#define HALF       (HEIGHT / 2) /* 32 adresses de ligne, scan 1/32 */
#define BCM_PLANES 8

/* Format de pixel imposé par hub75.pio : les bits 0, 8 et 16 du mot sont
 * prélevés pour R, G et B. Le mot vaut donc 0x00BBGGRR. */
#define RGB(r, g, b) (((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

#define NOIR  RGB(0, 0, 0)
#define BLANC RGB(255, 255, 255)

static uint32_t fb[HEIGHT][WIDTH];

/* ------------------------------------------------------------- rendu (cœur 1) */

static PIO pio = pio0;
static uint sm_data = 0, sm_row = 1;
static uint data_prog_offs, row_prog_offs;

static void __not_in_flash_func(core1_display)(void) {
    while (true) {
        for (int rowsel = 0; rowsel < HALF; ++rowsel) {
            for (int bit = 0; bit < BCM_PLANES; ++bit) {
                hub75_data_rgb888_set_shift(pio, sm_data, data_prog_offs, bit);
                for (int x = 0; x < WIDTH; ++x) {
                    /* Entrelacement imposé par le PIO : pixel pair sur
                     * R1/G1/B1 (moitié haute), impair sur R2/G2/B2 (basse). */
                    pio_sm_put_blocking(pio, sm_data, fb[rowsel][x]);
                    pio_sm_put_blocking(pio, sm_data, fb[rowsel + HALF][x]);
                }
                /* Pixel factice : l'horloge du dernier pixel utile tombe au
                 * milieu du suivant. */
                pio_sm_put_blocking(pio, sm_data, 0);
                pio_sm_put_blocking(pio, sm_data, 0);

                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);

                /* Verrouille la ligne, puis impulsion /OE de largeur 2^bit. */
                pio_sm_put_blocking(pio, sm_row, rowsel | (100u * (1u << bit) << 5));
            }
        }
    }
}

/* ------------------------------------------------------------------- mires */

static void remplir(uint32_t c) {
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            fb[y][x] = c;
}

static void bloc(int x0, int y0, int w, int h, uint32_t c) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
                fb[y][x] = c;
}

/* Cadre 1 px + coins colorés : géométrie complète et orientation. */
static void mire_cadre(uint32_t phase) {
    (void)phase;
    remplir(NOIR);
    for (int x = 0; x < WIDTH; ++x) {
        fb[0][x] = BLANC;
        fb[HEIGHT - 1][x] = BLANC;
    }
    for (int y = 0; y < HEIGHT; ++y) {
        fb[y][0] = BLANC;
        fb[y][WIDTH - 1] = BLANC;
    }
    bloc(2, 2, 5, 5, RGB(255, 0, 0));                   /* haut gauche  : rouge */
    bloc(WIDTH - 7, 2, 5, 5, RGB(0, 255, 0));           /* haut droite  : vert  */
    bloc(2, HEIGHT - 7, 5, 5, RGB(0, 0, 255));          /* bas  gauche  : bleu  */
    bloc(WIDTH - 7, HEIGHT - 7, 5, 5, BLANC);           /* bas  droite  : blanc */
    bloc(WIDTH / 2 - 4, HEIGHT / 2, 8, 1, BLANC);       /* croix centrale */
    bloc(WIDTH / 2, HEIGHT / 2 - 4, 1, 8, BLANC);
}

static void mire_rouge(uint32_t phase) { (void)phase; remplir(RGB(255, 0, 0)); }
static void mire_vert(uint32_t phase)  { (void)phase; remplir(RGB(0, 255, 0)); }
static void mire_bleu(uint32_t phase)  { (void)phase; remplir(RGB(0, 0, 255)); }

/* Moitié haute rouge / moitié basse bleue : sépare R1G1B1 de R2G2B2. */
static void mire_moities(uint32_t phase) {
    (void)phase;
    for (int y = 0; y < HEIGHT; ++y) {
        uint32_t c = (y < HALF) ? RGB(255, 0, 0) : RGB(0, 0, 255);
        for (int x = 0; x < WIDTH; ++x)
            fb[y][x] = c;
    }
}

/* Dégradé horizontal : profondeur de modulation BCM. */
static void mire_degrade_h(uint32_t phase) {
    (void)phase;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x) {
            uint8_t v = (uint8_t)(x * 4);
            fb[y][x] = RGB(v, v, v);
        }
}

/* Dégradé vertical : ordre des lignes d'adresse A-E. Un fil d'adresse
 * inversé transforme la rampe lisse en bandes désordonnées. */
static void mire_degrade_v(uint32_t phase) {
    (void)phase;
    for (int y = 0; y < HEIGHT; ++y) {
        uint8_t v = (uint8_t)(y * 4);
        for (int x = 0; x < WIDTH; ++x)
            fb[y][x] = RGB(v, v, v);
    }
}

/* Damier 1 px : révélateur de ghosting. */
static void mire_damier(uint32_t phase) {
    (void)phase;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            fb[y][x] = ((x + y) & 1) ? BLANC : NOIR;
}

/* Balayage : une seule ligne allumée qui descend, adresse par adresse. */
static void mire_balayage(uint32_t phase) {
    remplir(NOIR);
    int y = (int)(phase % HEIGHT);
    for (int x = 0; x < WIDTH; ++x)
        fb[y][x] = BLANC;
}

typedef struct {
    const char *nom;
    const char *valide;
    const char *si_defaut;
    void (*dessine)(uint32_t phase);
    uint32_t duree_ms;
    bool anime;
} mire_t;

static const mire_t mires[] = {
    {"CADRE + COINS", "geometrie 64x64, orientation, toutes les adresses",
     "coin manquant = ligne d'adresse morte ; bord coupe = largeur mal reglee",
     mire_cadre, 5000, false},
    {"ROUGE PLEIN", "lignes R1 et R2",
     "moitie haute noire = R1 (GP0) ; moitie basse noire = R2 (GP3)",
     mire_rouge, 3000, false},
    {"VERT PLEIN", "lignes G1 et G2",
     "moitie haute noire = G1 (GP1) ; moitie basse noire = G2 (GP4)",
     mire_vert, 3000, false},
    {"BLEU PLEIN", "lignes B1 et B2",
     "moitie haute noire = B1 (GP2) ; moitie basse noire = B2 (GP5)",
     mire_bleu, 3000, false},
    {"MOITIES R/B", "separation des deux demi-ecrans",
     "melange ou inversion = permutation entre groupes R1G1B1 et R2G2B2",
     mire_moities, 4000, false},
    {"DEGRADE HORIZONTAL", "profondeur de modulation BCM (8 plans)",
     "marches franches ou scintillement = probleme de /OE (GP13) ou de LAT (GP12)",
     mire_degrade_h, 4000, false},
    {"DEGRADE VERTICAL", "ordre des lignes d'adresse A-E (GP6-GP10)",
     "bandes desordonnees = deux fils d'adresse permutes",
     mire_degrade_v, 4000, false},
    {"DAMIER 1 PX", "ghosting et integrite du signal d'horloge",
     "trainees horizontales = ghosting ; flou = CLK (GP11) ou nappe trop longue",
     mire_damier, 4000, false},
    {"BALAYAGE LIGNE", "chaque adresse de ligne, une par une",
     "ligne qui saute ou qui double = fil d'adresse en l'air",
     mire_balayage, 8000, true},
};

#define NB_MIRES (sizeof(mires) / sizeof(mires[0]))

/* -------------------------------------------------------------------- main */

int main(void) {
    stdio_init_all();

    data_prog_offs = pio_add_program(pio, &hub75_data_rgb888_program);
    row_prog_offs  = pio_add_program(pio, &hub75_row_program);
    hub75_data_rgb888_program_init(pio, sm_data, data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);

    mire_cadre(0);
    multicore_launch_core1(core1_display);

    /* Laisse une chance à l'hôte d'ouvrir le port avant le premier message. */
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=====================================================\n");
    printf(" Module ECRAN - phase 0 : validation du cablage\n");
    printf(" Dalle 64x64, scan 1/32, %d plans BCM\n", BCM_PLANES);
    printf("-----------------------------------------------------\n");
    printf(" GP%d-GP%-2d  R1 G1 B1 R2 G2 B2\n", DATA_BASE_PIN, DATA_BASE_PIN + DATA_N_PINS - 1);
    printf(" GP%d-GP%-2d  A B C D E\n", ROWSEL_BASE_PIN, ROWSEL_BASE_PIN + ROWSEL_N_PINS - 1);
    printf(" GP%-2d      CLK\n", CLK_PIN);
    printf(" GP%-2d      LAT\n", STROBE_PIN);
    printf(" GP%-2d      /OE\n", OEN_PIN);
    printf("=====================================================\n\n");

    uint32_t n = 0;
    while (true) {
        const mire_t *m = &mires[n % NB_MIRES];

        printf("[%lu/%u] %s\n", (unsigned long)(n % NB_MIRES) + 1, (unsigned)NB_MIRES, m->nom);
        printf("        valide    : %s\n", m->valide);
        printf("        si defaut : %s\n\n", m->si_defaut);

        if (m->anime) {
            uint32_t ecoule = 0, phase = 0;
            while (ecoule < m->duree_ms) {
                m->dessine(phase++);
                sleep_ms(60);
                ecoule += 60;
            }
        } else {
            m->dessine(0);
            sleep_ms(m->duree_ms);
        }
        ++n;
    }
}
