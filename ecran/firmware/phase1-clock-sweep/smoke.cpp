/**
 * Module ÉCRAN — test minimal du pilote JuPfu
 *
 * Objectif : obtenir un état connu qui fonctionne, rien de plus. Ce programme
 * est volontairement aussi proche que possible de l'usage documenté par l'amont :
 *
 *   - configuration EXACTEMENT celle que le README amont publie pour cette dalle
 *     (« P3QD-64x64-21 / P3-64x64-2012-21A-1.0 ») : 10 plans, canaux CIE séparés ;
 *   - clk_sys à 266 MHz, comme la démo amont ;
 *   - pilote sur le cœur 0, chemin le plus simple — pas de multicœur ;
 *   - update_bgr() en boucle continue ;
 *   - mire en gros aplats, la plus facile à voir qui soit.
 *
 * Une fois cet état obtenu, les écarts (multicœur, 8 plans, diviseur d'horloge)
 * seront réintroduits un par un.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "hub75.hpp"

static constexpr Hub75Config CFG = {
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .pins = {
        .data_base_pin = 0,
        .data_n_pins = 6,
        .rowsel_base_pin = 6,
        .rowsel_n_pins = 5,
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = true,
        .balanced_light_output = true,
    },
    .frame_rate_debug = true,
};

using Panel = Hub75Driver<CFG>;
static Panel driver;

static constexpr int W = 64;
static constexpr int H = 64;

static uint8_t image[W * H * 3]; /* BGR */

static inline void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = &image[(y * W + x) * 3];
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

/* Quatre quadrants pleins : rouge, vert, bleu, blanc. Impossible à rater,
 * et chaque quadrant désigne un groupe de lignes de données. */
static void construire_mire(void) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const bool droite = (x >= W / 2);
            const bool bas = (y >= H / 2);
            if (!droite && !bas)     px(x, y, 255, 0, 0);
            else if (droite && !bas) px(x, y, 0, 255, 0);
            else if (!droite && bas) px(x, y, 0, 0, 255);
            else                     px(x, y, 255, 255, 255);
        }
}

int main(void) {
    set_sys_clock_khz(266000, true);

    stdio_init_all();
    construire_mire();

    /* Cœur 0, chemin le plus simple de l'amont. */
    driver.create();
    driver.start();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    const uint32_t clk_hz = clock_get_hz(clk_sys);
    printf("\n");
    printf("=============================================\n");
    printf(" TEST MINIMAL DU PILOTE — attendu a l'ecran :\n");
    printf("   haut gauche ROUGE    haut droite VERT\n");
    printf("   bas  gauche BLEU     bas  droite BLANC\n");
    printf("---------------------------------------------\n");
    printf(" clk_sys %lu MHz, %lu plans, coeur 0, mono-instance\n",
           (unsigned long)(clk_hz / 1000000u), (unsigned long)CFG.color.bitplanes);
    printf("=============================================\n\n");

    while (true) {
        driver.update_bgr(image);
        sleep_ms(100);
    }
}
