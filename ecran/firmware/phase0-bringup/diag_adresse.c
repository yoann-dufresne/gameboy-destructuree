/**
 * Module ÉCRAN — diagnostic des lignes d'adresse A-E
 *
 * Fige l'adresse de ligne sur une valeur connue et allume tout le framebuffer
 * en blanc. La ligne qui s'allume révèle directement quels bits d'adresse
 * arrivent réellement à la dalle.
 *
 *   adresse 0  (aucun bit)  -> lignes  0 et 32
 *   adresse 1  (A seul)     -> lignes  1 et 33
 *   adresse 2  (B seul)     -> lignes  2 et 34
 *   adresse 4  (C seul)     -> lignes  4 et 36
 *   adresse 8  (D seul)     -> lignes  8 et 40
 *   adresse 16 (E seul)     -> lignes 16 et 48
 *   adresse 31 (tous)       -> lignes 31 et 63  (dernière de chaque moitié)
 *
 * Un bit mort laisse l'affichage sur les lignes 0 et 32.
 *
 * ⚠️ En fonctionnement normal une ligne n'est allumée que 1/32 du temps.
 * Ici l'adresse est figée : la largeur d'impulsion /OE est donc divisée par
 * ~32 pour que le courant moyen dans ces LED reste celui du régime normal.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"

#include "hub75.pio.h"

#define DATA_BASE_PIN   0
#define DATA_N_PINS     6
#define ROWSEL_BASE_PIN 6
#define ROWSEL_N_PINS   5
#define CLK_PIN         11
#define STROBE_PIN      12
#define OEN_PIN         13

#define WIDTH      64
#define HEIGHT     64
#define HALF       (HEIGHT / 2)
#define BCM_PLANES 8

/* 100 en régime balayé ; /32 ici puisque la ligne reste allumée en permanence. */
#define OE_UNITE 3

#define RGB(r, g, b) (((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

static uint32_t fb[HEIGHT][WIDTH];

static PIO pio = pio0;
static uint sm_data = 0, sm_row = 1;
static uint data_prog_offs, row_prog_offs;

/* Adresse figée, lue par le cœur 1 à chaque cycle. */
static volatile uint32_t adresse_figee = 0;

static void __not_in_flash_func(core1_display)(void) {
    while (true) {
        uint32_t rowsel = adresse_figee & 0x1f;
        for (int bit = 0; bit < BCM_PLANES; ++bit) {
            hub75_data_rgb888_set_shift(pio, sm_data, data_prog_offs, bit);
            for (int x = 0; x < WIDTH; ++x) {
                pio_sm_put_blocking(pio, sm_data, fb[rowsel][x]);
                pio_sm_put_blocking(pio, sm_data, fb[rowsel + HALF][x]);
            }
            pio_sm_put_blocking(pio, sm_data, 0);
            pio_sm_put_blocking(pio, sm_data, 0);
            hub75_wait_tx_stall(pio, sm_data);
            hub75_wait_tx_stall(pio, sm_row);
            pio_sm_put_blocking(pio, sm_row, rowsel | (OE_UNITE * (1u << bit) << 5));
        }
    }
}

typedef struct {
    uint32_t adresse;
    const char *bit;
    int ligne_haute;
    int ligne_basse;
} etape_t;

static const etape_t etapes[] = {
    { 0, "aucun (reference)",  0,  32},
    { 1, "A seul  (GP6)",      1,  33},
    { 2, "B seul  (GP7)",      2,  34},
    { 4, "C seul  (GP8)",      4,  36},
    { 8, "D seul  (GP9)",      8,  40},
    {16, "E seul  (GP10)",    16,  48},
    {31, "A+B+C+D+E",         31,  63},
};

#define NB_ETAPES (sizeof(etapes) / sizeof(etapes[0]))

int main(void) {
    stdio_init_all();

    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            fb[y][x] = RGB(255, 255, 255);

    data_prog_offs = pio_add_program(pio, &hub75_data_rgb888_program);
    row_prog_offs  = pio_add_program(pio, &hub75_row_program);
    hub75_data_rgb888_program_init(pio, sm_data, data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);

    multicore_launch_core1(core1_display);

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=========================================================\n");
    printf(" DIAGNOSTIC DES LIGNES D'ADRESSE A-E\n");
    printf("---------------------------------------------------------\n");
    printf(" L'ecran est entierement blanc, mais une seule paire de\n");
    printf(" lignes s'allume : celle designee par l'adresse.\n");
    printf(" Note a chaque etape QUELLE ligne s'allume reellement.\n");
    printf(" Si elle ne bouge pas de 0/32, le bit teste n'arrive pas.\n");
    printf("=========================================================\n\n");

    uint32_t n = 0;
    while (true) {
        const etape_t *e = &etapes[n % NB_ETAPES];
        adresse_figee = e->adresse;

        printf("[%u/%u] adresse = %2lu   bit teste : %s\n",
               (unsigned)(n % NB_ETAPES) + 1, (unsigned)NB_ETAPES,
               (unsigned long)e->adresse, e->bit);
        printf("        attendu : lignes %d (moitie haute) et %d (moitie basse)\n\n",
               e->ligne_haute, e->ligne_basse);

        sleep_ms(5000);
        ++n;
    }
}
