/**
 * Module ÉCRAN — mesure de la fréquence d'horloge pixel maximale stable
 *
 * Le programme PIO de flux de bits consomme 9 cycles par pixel :
 *
 *     out pins, 6  [3]   side 0    -> 4 cycles
 *     out null, 2  [3]   side 1    -> 4 cycles
 *     jmp x--, loop      side 0    -> 1 cycle
 *
 * Avec un diviseur d'horloge de 1,0, l'horloge pixel vue par la dalle vaut donc
 * exactement clk_sys / 9. On balaie la fréquence en changeant clk_sys, ce qui a
 * l'avantage de tout mettre à l'échelle ensemble : horloge pixel, garde de verrou
 * et garde d'adressage restent dans le même rapport.
 *
 * Le balayage est DESCENDANT, et c'est délibéré : le pilote est créé à la
 * fréquence la plus haute, donc ses gardes valent exactement leur valeur nominale
 * (80 ns pour le verrou, 160 ns pour l'adressage) à ce point-là. En descendant,
 * elles ne peuvent que devenir plus généreuses. Un défaut observé en haut du
 * balayage est donc bien un défaut de fréquence, pas une garde trop courte.
 *
 * Ce qu'on cherche est SPATIAL, pas temporel : des rayures de 1 px qui se
 * brouillent, bavent ou grisonnent, d'abord sur le bord DROIT de la dalle (les
 * derniers pixels décalés dans le registre). Le scintillement en bas du balayage
 * est normal — le rafraîchissement chute avec clk_sys — et ne compte pas.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "hub75.hpp"

/* ------------------------------------------------------------ configuration */

static constexpr Hub75Config CFG = {
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::Standard, /* scan 1/32 */
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f, /* horloge pixel = clk_sys / 9 */
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_0,
    },
    .pins = { /* plan §2.3 */
        .data_base_pin = 0,
        .data_n_pins = 6,
        .rowsel_base_pin = 6,
        .rowsel_n_pins = 5,
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 8,
        .separate_cie_channels = false,
        .balanced_light_output = true,
    },
    .frame_rate_debug = false,
};

using Panel = Hub75Driver<CFG>;
static Panel driver;

static constexpr int W = 64;
static constexpr int H = 64;
static constexpr uint32_t PIO_CYCLES_PAR_PIXEL = 9;

/* Tampon BGR, 3 octets par pixel. */
static uint8_t image[W * H * 3];

/* -------------------------------------------------------------- points de mesure */

struct Palier {
    uint32_t clk_sys_khz;
    uint32_t pixel_khz; /* = clk_sys_khz / 9, pré-calculé pour l'affichage */
};

/* Descendant. 252 MHz reste sous les 266 MHz que la démo amont fait tourner,
 * ce qui garde le diviseur de la flash QSPI dans un domaine éprouvé. */
static constexpr Palier paliers[] = {
    {252000, 28000}, {234000, 26000}, {216000, 24000}, {198000, 22000},
    {180000, 20000}, {162000, 18000}, {144000, 16000}, {126000, 14000},
    {108000, 12000}, { 90000, 10000}, { 72000,  8000}, { 54000,  6000},
};
static constexpr size_t NB_PALIERS = sizeof(paliers) / sizeof(paliers[0]);
static constexpr uint32_t DUREE_PALIER_MS = 5000;

/* ------------------------------------------------------------------- mire */

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
                 * à chaque coup d'horloge. C'est le cas le plus exigeant. */
                if (x & 1) px(x, y, 255, 255, 255);
            } else if (y < 40) {
                /* Colonnes rouge / vert en alternance : les six lignes de
                 * données basculent en opposition de phase. Révèle la diaphonie. */
                if (x & 1) px(x, y, 255, 0, 0);
                else       px(x, y, 0, 255, 0);
            } else if (y < 52) {
                /* Blanc plein : référence d'uniformité et de luminosité. */
                px(x, y, 255, 255, 255);
            } else {
                /* Pixels isolés tous les 8 : une bavure se voit immédiatement
                 * comme un pixel fantôme juste à droite du pixel allumé. */
                if ((x % 8) == 0) px(x, y, 255, 255, 255);
            }
        }
    }
}

/* ------------------------------------------------------------------ cœur 1 */

static void core1_entry(void) {
    driver.create();
    driver.start();

    /* Garder le cœur 1 vivant : sans cela son NVIC est démonté et DMA_IRQ_1
     * cesse de se déclencher. */
    while (true)
        tight_loop_contents();
}

/* -------------------------------------------------------------------- main */

int main(void) {
    /* La fréquence la plus haute d'abord : le pilote est créé là, ses gardes
     * valent donc leur valeur nominale au point le plus contraignant. */
    bool ok_max = set_sys_clock_khz(paliers[0].clk_sys_khz, false);

    stdio_init_all();

    construire_mire();

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    sleep_ms(500);
    driver.update_bgr(image);

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=================================================================\n");
    printf(" MESURE DE L'HORLOGE PIXEL MAXIMALE STABLE\n");
    printf("-----------------------------------------------------------------\n");
    printf(" Dalle 64x64, scan 1/32, %lu plans BCM\n",
           (unsigned long)CFG.color.bitplanes);
    printf(" horloge pixel = clk_sys / %lu  (diviseur SM = 1,0)\n",
           (unsigned long)PIO_CYCLES_PAR_PIXEL);
    if (!ok_max)
        printf(" !! %lu kHz refuse par le SDK : mesure a partir du palier suivant\n",
               (unsigned long)paliers[0].clk_sys_khz);
    printf("-----------------------------------------------------------------\n");
    printf(" A REGARDER : la nettete des rayures de 1 px, surtout sur le\n");
    printf(" bord DROIT (derniers pixels decales dans le registre).\n");
    printf("   sain   : rayures franches, contraste constant de gauche a droite\n");
    printf("   defaut : rayures qui se brouillent, grisonnent ou bavent a droite\n");
    printf("            pixels isoles du bas suivis d'un fantome\n");
    printf(" Le scintillement en bas du balayage est NORMAL (rafraichissement\n");
    printf(" proportionnel a clk_sys) et ne compte pas.\n");
    printf("-----------------------------------------------------------------\n");
    printf(" Balayage DESCENDANT, %lu s par palier, en boucle.\n",
           (unsigned long)(DUREE_PALIER_MS / 1000));
    printf(" Note le palier le plus HAUT encore net.\n");
    printf("=================================================================\n\n");

    uint32_t n = 0;
    while (true) {
        const Palier *p = &paliers[n % NB_PALIERS];

        bool ok = set_sys_clock_khz(p->clk_sys_khz, false);

        printf("[%2u/%u]  horloge pixel %2lu,%01lu MHz   (clk_sys %3lu MHz)%s\n",
               (unsigned)(n % NB_PALIERS) + 1, (unsigned)NB_PALIERS,
               (unsigned long)(p->pixel_khz / 1000),
               (unsigned long)((p->pixel_khz % 1000) / 100),
               (unsigned long)(p->clk_sys_khz / 1000),
               ok ? "" : "   << REFUSE PAR LE SDK, palier ignore");

        if (n % NB_PALIERS == NB_PALIERS - 1)
            printf("\n        --- fin de balayage, on repart du haut ---\n\n");

        sleep_ms(DUREE_PALIER_MS);
        ++n;
    }
}
