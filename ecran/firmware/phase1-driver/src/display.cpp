/**
 * Module ÉCRAN — façade d'affichage, mise en œuvre
 *
 * Enveloppe le pilote HUB75 vendorisé (JuPfu, MIT — voir vendor/hub75-jupfu).
 */

#include "display.hpp"
#include "config.h"

#include <cstring>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "hub75.hpp"

namespace {

/* ⚠️ Piège de nommage dans le pilote amont : son README dit que `chain_rows`
 * compte les dalles côte à côte, mais son code fait
 *     DISPLAY_WIDTH  = matrix_panel_width  * chain_cols
 *     DISPLAY_HEIGHT = matrix_panel_height * chain_rows
 * C'est donc `chain_cols` qui compte l'horizontale. On suit le code, pas le
 * commentaire. Sans effet tant que CHAIN_LEN vaut 1, déterminant en phase 5. */
constexpr Hub75Config CFG = {
    .panel = {
        .matrix_panel_width = PANEL_W,
        .matrix_panel_height = PANEL_H,
        .chain_rows = 1,
        .chain_cols = CHAIN_LEN,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::Standard, /* scan 1/32, deux lignes allumées */
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_0,
    },
    .pins = {
        .data_base_pin = PIN_DATA_BASE,
        .data_n_pins = PIN_DATA_N,
        .rowsel_base_pin = PIN_ROWSEL_BASE,
        .rowsel_n_pins = PIN_ROWSEL_N,
        .clk_pin = PIN_CLK,
        .strobe_pin = PIN_LAT,
        .oen_pin = PIN_OE,
    },
    .color = {
        .bitplanes = BCM_PLANES,
        .separate_cie_channels = (CIE_SEPARATE != 0),
        .balanced_light_output = true,
    },
    .frame_rate_debug = (DEBUG_FRAME_RATE != 0),
};

Hub75Driver<CFG> pilote;

/* Double tampon strict. Le cœur 0 écrit dans l'un, le cœur 1 convertit l'autre.
 * Jamais de troisième : chaque tampon supplémentaire est une trame de latence
 * de plus (plan §5, phase 1). */
alignas(4) uint8_t tampon[2][FB_OCTETS];

constexpr uint8_t AUCUNE = 0xff;

volatile uint8_t idx_ecriture = 0;
volatile uint8_t idx_a_publier = AUCUNE; /* index en attente de conversion */
volatile bool pilote_pret = false;

uint8_t id_noeud = 0;

void core1_entry() {
    pilote.create();
    pilote.start();

    /* ⚠️ Indispensable sur le cœur 1. create() remplit un tampon de commandes de
     * ligne pendant que le DMA en diffuse un autre, jamais rempli ; la bascule
     * dépend d'une interruption qui ne survient pas ici au démarrage. Sans cet
     * appel, le PIO de ligne ne reçoit que des zéros : adresse figée à 0,
     * lit_cycles à 0, panneau noir — alors que le compteur de trames tourne
     * normalement. Mesuré le 18/09/2026, voir phase1-clock-sweep/DIAGNOSTIC.md. */
    pilote.setBasisBrightness(BASIS_BRIGHTNESS);

    pilote_pret = true;

    /* Le cœur 1 possède le pilote : il doit rester vivant, sinon son NVIC est
     * démonté et les interruptions DMA cessent. Il consomme les trames publiées
     * par le cœur 0 — la conversion en plans de bits lui incombe, le flux vers
     * la dalle est en PIO + DMA. */
    while (true) {
        const uint8_t idx = idx_a_publier;
        if (idx != AUCUNE) {
            pilote.update_bgr(tampon[idx]);
            idx_a_publier = AUCUNE; /* acquittement */
        } else {
            tight_loop_contents();
        }
    }
}

uint8_t lire_straps() {
    /* Pull-up, strap vers la masse : une broche tirée à la masse vaut 1.
     * ⚠️ Errata RP2350-E9 — ne jamais utiliser de pull-down interne ici. */
    const uint pins[] = {PIN_NODE_ID_0, PIN_NODE_ID_1};
    uint8_t id = 0;
    for (uint i = 0; i < count_of(pins); ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
    sleep_us(100); /* laisse les pull-ups établir le niveau */
    for (uint i = 0; i < count_of(pins); ++i)
        if (!gpio_get(pins[i]))
            id |= (1u << i);
    return id;
}

} // namespace

namespace display {

void init() {
    id_noeud = lire_straps();

    gpio_init(PIN_MESURE_FLIP);
    gpio_set_dir(PIN_MESURE_FLIP, GPIO_OUT);
    gpio_put(PIN_MESURE_FLIP, 0);

    std::memset(tampon, 0, sizeof(tampon));

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    while (!pilote_pret)
        tight_loop_contents();
}

uint8_t *backbuffer() {
    return tampon[idx_ecriture];
}

void present() {
    /* Attend que le cœur 1 ait fini la trame précédente. La conversion coûte
     * quelques centaines de microsecondes pour une dalle : négligeable devant
     * les 16,7 ms d'une trame Game Boy. Attendre plutôt qu'écraser garantit
     * qu'on n'écrit jamais dans un tampon en cours de lecture. */
    while (idx_a_publier != AUCUNE)
        tight_loop_contents();

    gpio_xor_mask(1u << PIN_MESURE_FLIP);

    idx_a_publier = idx_ecriture;
    idx_ecriture ^= 1u;
}

void set_brightness(uint8_t basis) {
    pilote.setBasisBrightness(basis ? basis : 1);
}

uint8_t node_id() {
    return id_noeud;
}

} // namespace display
