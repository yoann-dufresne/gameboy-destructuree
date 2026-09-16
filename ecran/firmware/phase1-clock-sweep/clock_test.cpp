/**
 * Module ÉCRAN — mesure de la fréquence d'horloge pixel maximale stable
 *
 * UNE fréquence par binaire : le diviseur est un `constexpr`, donc le pilote est
 * initialisé proprement à cette cadence et rien n'est touché ensuite.
 *
 * Ce programme est le test minimal `smoke.cpp` — dont on sait qu'il affiche —
 * avec UN SEUL écart : le diviseur d'horloge. Tout le reste est identique à la
 * configuration que le README amont publie pour cette dalle. La mire change
 * aussi, mais c'est du contenu de tampon : sans effet sur le pilote.
 *
 * Le programme PIO `hub75_bitplane_stream` consomme 9 cycles par pixel :
 *
 *     out pins, 6  [3]   side 0    -> 4 cycles
 *     out null, 2  [3]   side 1    -> 4 cycles
 *     jmp x--, loop      side 0    -> 1 cycle
 *
 * D'où  horloge pixel = clk_sys / (9 x sm_clockdiv_factor).
 *
 * Les gardes de verrou et d'adressage sont converties en cycles PIO à la
 * création, à partir du clk_sys et du diviseur réels : elles valent donc leurs
 * 80 ns / 160 ns nominaux à toutes les fréquences. La comparaison entre binaires
 * est donc honnête.
 *
 * Ce qu'on cherche est SPATIAL : des rayures de 1 px qui se brouillent, bavent
 * ou grisonnent, d'abord sur le bord DROIT de la dalle — les derniers pixels
 * décalés dans le registre.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "hub75.hpp"

static constexpr uint32_t CLK_SYS_KHZ = 266000;

#ifndef PIXEL_CLOCK_KHZ
#define PIXEL_CLOCK_KHZ 28000
#endif

/* diviseur = clk_sys / (9 x cible), evalue a la compilation */
static constexpr float DIVISEUR =
    (float)(CLK_SYS_KHZ * 1000ull) / (9.0f * (float)(PIXEL_CLOCK_KHZ * 1000ull));
static_assert(DIVISEUR >= 1.0f, "cible trop haute pour ce clk_sys");

static constexpr Hub75Config CFG = {
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = DIVISEUR,
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

/* Mire conçue pour révéler une horloge trop rapide, pas pour être jolie. */
static void construire_mire(void) {
    std::memset(image, 0, sizeof(image));

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (y < 24) {
                /* Rayures verticales de 1 px : les lignes de données basculent
                 * à chaque coup d'horloge. Cas le plus exigeant. */
                if (x & 1) px(x, y, 255, 255, 255);
            } else if (y < 40) {
                /* Colonnes rouge / vert alternées : les six lignes de données
                 * basculent en opposition de phase. Révèle la diaphonie. */
                if (x & 1) px(x, y, 255, 0, 0);
                else       px(x, y, 0, 255, 0);
            } else if (y < 52) {
                /* Blanc plein : référence d'uniformité et de luminosité. */
                px(x, y, 255, 255, 255);
            } else {
                /* Pixels isolés tous les 8 : une bavure apparaît comme un
                 * fantôme juste à droite du pixel allumé. */
                if ((x % 8) == 0) px(x, y, 255, 255, 255);
            }
        }
    }
}

int main(void) {
    set_sys_clock_khz(CLK_SYS_KHZ, true);

    stdio_init_all();
    construire_mire();

    /* Cœur 0, chemin le plus simple de l'amont. */
    driver.create();
    driver.start();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    const uint32_t clk_hz = clock_get_hz(clk_sys);
    const uint32_t reel_khz = (uint32_t)((double)clk_hz / (9.0 * (double)DIVISEUR) / 1000.0);

    printf("\n");
    printf("==============================================================\n");
    printf(" HORLOGE PIXEL : %lu,%01lu MHz\n",
           (unsigned long)(reel_khz / 1000u), (unsigned long)((reel_khz % 1000u) / 100u));
    printf("--------------------------------------------------------------\n");
    printf(" clk_sys %lu MHz, diviseur %lu,%03lu, %lu plans BCM, coeur 0\n",
           (unsigned long)(clk_hz / 1000000u),
           (unsigned long)DIVISEUR,
           (unsigned long)((DIVISEUR - (float)(unsigned long)DIVISEUR) * 1000.0f),
           (unsigned long)CFG.color.bitplanes);
    printf("--------------------------------------------------------------\n");
    printf(" A REGARDER : la nettete des rayures de 1 px, bord DROIT surtout\n");
    printf("   sain   : rayures franches, contraste constant\n");
    printf("   defaut : rayures brouillees / grisonnantes / qui bavent a droite\n");
    printf("==============================================================\n\n");

    while (true) {
        driver.update_bgr(image);
        sleep_ms(100);
    }
}
