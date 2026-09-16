/**
 * Module ÉCRAN — mesure de la fréquence d'horloge pixel maximale stable
 *
 * UNE fréquence par binaire. Le diviseur est un constexpr, donc le pilote est
 * initialisé proprement à cette cadence-là et rien n'est touché ensuite.
 *
 * Une première version balayait les fréquences à chaud en changeant le diviseur
 * des machines PIO. Elle est abandonnée : le pilote utilise TROIS machines, dont
 * une qui construit les plans de bits, et la perturber en plein travail fait
 * chuter le rafraîchissement d'un facteur 3. Mesures non reproductibles.
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
 * création, à partir de clk_sys et du diviseur réels : elles valent donc leurs
 * 80 ns / 160 ns nominaux quelle que soit la fréquence testée. La comparaison
 * entre binaires est donc honnête.
 *
 * Ce qu'on cherche est SPATIAL, pas temporel : des rayures de 1 px qui se
 * brouillent, bavent ou grisonnent, d'abord sur le bord DROIT de la dalle (les
 * derniers pixels décalés dans le registre).
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "hub75.hpp"

/* ------------------------------------------------------------ configuration */
static constexpr uint32_t CLK_SYS_KHZ = 252000;

#ifndef PIXEL_CLOCK_KHZ
#define PIXEL_CLOCK_KHZ 28000
#endif
static constexpr uint32_t CIBLE_KHZ = PIXEL_CLOCK_KHZ;

/* diviseur = clk_sys / (9 x cible), evalue a la compilation */
static constexpr float DIVISEUR =
    (float)(CLK_SYS_KHZ * 1000ull) / (9.0f * (float)(CIBLE_KHZ * 1000ull));
static_assert(DIVISEUR >= 1.0f, "cible trop haute pour ce clk_sys");


/* Conforme à la configuration documentée par l'amont pour cette dalle exacte :
 * « P3QD-64x64-21 / P3-64x64-2012-21A-1.0 », RowMapping::Standard. */
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
        .sm_clockdiv_factor = DIVISEUR,
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
    .frame_rate_debug = true, /* le pilote annonce son rafraîchissement réel */
};

using Panel = Hub75Driver<CFG>;
static Panel driver;

static constexpr int W = 64;
static constexpr int H = 64;
static constexpr uint32_t PIO_CYCLES_PAR_PIXEL = 9;


static uint8_t image[W * H * 3]; /* BGR, 3 octets par pixel */

static volatile bool pilote_pret = false;

/* ------------------------------------------------------------------- mire */

static inline void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = &image[(y * W + x) * 3];
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

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

/* ------------------------------------------------------------------ cœur 1 */

static void core1_entry(void) {
    driver.create();
    driver.start();
    pilote_pret = true;

    /* Garder le cœur 1 vivant : sans cela son NVIC est démonté et DMA_IRQ_1
     * cesse de se déclencher. */
    while (true)
        tight_loop_contents();
}

/* -------------------------------------------------------------------- main */

int main(void) {
    /* UNE SEULE FOIS, avant tout le reste. */
    bool clk_ok = set_sys_clock_khz(CLK_SYS_KHZ, false);

    stdio_init_all();
    construire_mire();

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    while (!pilote_pret)
        tight_loop_contents();
    sleep_ms(200); /* laisse la premiere trame du pilote s'etablir */

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    const uint32_t clk_hz = clock_get_hz(clk_sys);
    const uint32_t reel_khz =
        (uint32_t)((double)clk_hz / (9.0 * (double)DIVISEUR) / 1000.0);

    printf("\n");
    printf("=================================================================\n");
    printf(" HORLOGE PIXEL : %lu,%01lu MHz\n",
           (unsigned long)(reel_khz / 1000u), (unsigned long)((reel_khz % 1000u) / 100u));
    printf("-----------------------------------------------------------------\n");
    printf(" clk_sys  : %lu,%03lu MHz%s\n",
           (unsigned long)(clk_hz / 1000000u), (unsigned long)((clk_hz / 1000u) % 1000u),
           clk_ok ? "" : "  (valeur demandee refusee, repli du SDK)");
    printf(" diviseur : %lu,%03lu   (fixe a la compilation)\n",
           (unsigned long)DIVISEUR,
           (unsigned long)((DIVISEUR - (float)(unsigned long)DIVISEUR) * 1000.0f));
    printf(" dalle 64x64, scan 1/32, %lu plans BCM\n",
           (unsigned long)CFG.color.bitplanes);
    printf("-----------------------------------------------------------------\n");
    printf(" A REGARDER : la nettete des rayures de 1 px, surtout sur le\n");
    printf(" bord DROIT (derniers pixels decales dans le registre).\n");
    printf("   sain   : rayures franches, contraste constant de gauche a droite\n");
    printf("   defaut : rayures qui se brouillent, grisonnent ou bavent a droite\n");
    printf("            pixels isoles du bas suivis d'un fantome\n");
    printf("=================================================================\n\n");

    /* L'amont alimente le pilote en continu depuis le coeur 0. Un appel unique
     * ne suffit pas : s'il tombe pendant le demarrage du pilote, la construction
     * des plans de bits est perdue et le tampon reste noir indefiniment.
     * Le pilote annonce son rafraichissement reel entre deux mises a jour. */
    while (true)
    {
        driver.update_bgr(image);
        sleep_ms(100);
    }
}
