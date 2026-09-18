/**
 * Module ÉCRAN — quel écart rendait l'affichage noir ?
 *
 * Quatre écarts séparaient le programme qui affiche (`smoke.cpp`) de celui qui
 * restait noir. Ce programme les isole un par un, et **mesure le résultat sans
 * regarder l'écran**.
 *
 * Principe : les six lignes de données R1 G1 B1 R2 G2 B2 sont pilotées par le
 * PIO, mais leurs pads restent lisibles par le CPU (`gpio_get_all`). Si le
 * tampon de trame est noir, ces six broches restent à zéro en permanence. Si une
 * image est affichée, elles basculent. Le taux d'échantillons non nuls est donc
 * un verdict objectif, sans œil humain dans la boucle.
 *
 * Les lignes d'adresse GP6–GP10 sont échantillonnées aussi : elles distinguent
 * « le pilote ne tourne pas du tout » de « le pilote tourne mais diffuse du noir ».
 *
 * Écarts pilotés à la compilation :
 *   PROBE_CORE1        0 = pilote sur le cœur 0   1 = sur le cœur 1
 *   PROBE_BITPLANES    8 ou 10
 *   PROBE_CIE_SEPARATE 0 = canaux CIE communs     1 = séparés
 *   PROBE_CLK_KHZ      252000 ou 266000
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "hub75.hpp"

#ifndef PROBE_CORE1
#define PROBE_CORE1 0
#endif
#ifndef PROBE_BITPLANES
#define PROBE_BITPLANES 10
#endif
#ifndef PROBE_CIE_SEPARATE
#define PROBE_CIE_SEPARATE 1
#endif
#ifndef PROBE_CLK_KHZ
#define PROBE_CLK_KHZ 266000
#endif
#ifndef PROBE_NOM
#define PROBE_NOM "sans nom"
#endif
#ifndef PROBE_KICK
#define PROBE_KICK 0 /* 1 = force une reconstruction des commandes de ligne apres start() */
#endif

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
        .bitplanes = PROBE_BITPLANES,
        .separate_cie_channels = (PROBE_CIE_SEPARATE != 0),
        .balanced_light_output = true,
    },
    .frame_rate_debug = false, /* la sonde ci-dessous est plus parlante */
};

using Panel = Hub75Driver<CFG>;
static Panel driver;

static constexpr int W = 64;
static constexpr int H = 64;

static constexpr uint32_t MASQUE_DATA = 0x003fu; /* GP0..GP5  */
static constexpr uint32_t MASQUE_ADDR = 0x07c0u; /* GP6..GP10 */

static uint8_t image[W * H * 3]; /* BGR */
static volatile bool pilote_pret = false;

static inline void px(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = &image[(y * W + x) * 3];
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

/* Quadrants pleins : chacune des six lignes de données est sollicitée.
 * Haut = R1 G1 B1, bas = R2 G2 B2. */
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

static void core1_entry(void) {
    driver.create();
    driver.start();
#if PROBE_KICK
    /* create() remplit row_cmd_buffer2_ alors que le DMA lit row_cmd_buffer1_.
     * setBasisBrightness reconstruit et doit provoquer la bascule. */
    driver.setBasisBrightness(6);
#endif
    pilote_pret = true;
    while (true)
        tight_loop_contents();
}

/* Échantillonne les pads pendant duree_ms et rend le taux de non-nuls. */
struct Sonde {
    uint32_t total;
    uint32_t data_non_nuls;
    uint32_t data_vus;   /* OU de tous les échantillons */
    uint32_t addr_non_nuls;
    uint32_t addr_vus;
};

static Sonde echantillonner(uint32_t duree_ms) {
    Sonde s = {0, 0, 0, 0, 0};
    absolute_time_t fin = make_timeout_time_ms(duree_ms);
    do {
        const uint32_t brut = gpio_get_all();
        const uint32_t d = brut & MASQUE_DATA;
        const uint32_t a = brut & MASQUE_ADDR;
        s.total++;
        s.data_vus |= d;
        s.addr_vus |= a;
        if (d) s.data_non_nuls++;
        if (a) s.addr_non_nuls++;
    } while (!time_reached(fin));
    return s;
}

static void afficher_broches(const char *etiquette, uint32_t vus, uint32_t base, uint32_t n,
                             const char *const *noms) {
    printf("  %s : ", etiquette);
    for (uint32_t i = 0; i < n; ++i)
        printf("%s%s ", noms[i], (vus & (1u << (base + i))) ? "=1" : "=0");
    printf("\n");
}

int main(void) {
    set_sys_clock_khz(PROBE_CLK_KHZ, true);

    stdio_init_all();
    construire_mire();

#if PROBE_CORE1
    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    while (!pilote_pret)
        tight_loop_contents();
#else
    driver.create();
    driver.start();
#endif

    sleep_ms(300);
    driver.update_bgr(image);

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    static const char *const noms_data[] = {"R1", "G1", "B1", "R2", "G2", "B2"};
    static const char *const noms_addr[] = {"A", "B", "C", "D", "E"};

    printf("\n");
    printf("=================================================================\n");
    printf(" SONDE : %s\n", PROBE_NOM);
    printf("-----------------------------------------------------------------\n");
    printf(" coeur %d | %d plans | canaux CIE %s | clk_sys %lu MHz | kick %d\n",
           PROBE_CORE1 ? 1 : 0, PROBE_BITPLANES,
           PROBE_CIE_SEPARATE ? "separes" : "communs",
           (unsigned long)(clock_get_hz(clk_sys) / 1000000u), PROBE_KICK);
    printf("=================================================================\n");

    for (int tour = 0; tour < 3; ++tour) {
        driver.update_bgr(image);
        sleep_ms(200);

        Sonde s = echantillonner(200);

        const uint32_t pct_data = s.total ? (uint32_t)((uint64_t)s.data_non_nuls * 100u / s.total) : 0;
        const uint32_t pct_addr = s.total ? (uint32_t)((uint64_t)s.addr_non_nuls * 100u / s.total) : 0;

        printf("\n[tour %d] %lu echantillons\n", tour + 1, (unsigned long)s.total);
        printf("  donnees R1..B2 non nulles : %lu %%\n", (unsigned long)pct_data);
        printf("  adresses A..E non nulles  : %lu %%\n", (unsigned long)pct_addr);
        afficher_broches("lignes donnees vues a 1", s.data_vus, 0, 6, noms_data);
        afficher_broches("lignes adresse vues a 1", s.addr_vus, 6, 5, noms_addr);

        if (s.addr_non_nuls == 0)
            printf("  >>> VERDICT : le pilote ne balaie pas — rien ne tourne\n");
        else if (s.data_non_nuls == 0)
            printf("  >>> VERDICT : NOIR — le pilote balaie mais ne diffuse que des zeros\n");
        else
            printf("  >>> VERDICT : AFFICHE — les six lignes de donnees travaillent\n");
    }

    printf("\n--- fin de sonde ---\n");

    while (true) {
        driver.update_bgr(image);
        sleep_ms(100);
    }
}
