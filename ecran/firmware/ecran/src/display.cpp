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

/* UN SEUL tampon, et c'est délibéré.
 *
 * Le pilote tient déjà son propre double tampon (frame_buffer1_/2_), basculé en
 * fin de trame : les mises à jour sont donc déjà sans déchirure. Un second double
 * tampon de notre côté n'apportait rien — et il était un piège : present()
 * basculait vers un tampon que l'appelant n'avait pas rempli, si bien qu'afficher
 * deux fois de suite la même image alternait image / noir. Scintillement à 60 Hz
 * garanti. Constaté le 18/09/2026.
 *
 * update_bgr() est synchrone : quand present() rend la main, le pilote a fini de
 * lire le tampon, qui est donc immédiatement réutilisable. */
alignas(4) uint8_t tampon[FB_OCTETS];

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

    /* Le cœur 1 possède le pilote et ne fait rien d'autre : il sert ses
     * interruptions DMA. Il doit rester vivant, sinon son NVIC est démonté et
     * les interruptions cessent — c'est documenté par l'amont.
     *
     * La conversion en plans de bits reste sur le cœur 0, comme chez l'amont :
     * elle coûte quelques centaines de microsecondes par trame, soit moins de
     * 2 % du cœur 0 à 60 Hz, et la laisser ici retarderait le service des
     * interruptions du pilote. */
    while (true)
        tight_loop_contents();
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
    /* Toujours le même tampon : son contenu persiste d'une publication à
     * l'autre. Republier sans rien redessiner réaffiche la même image. */
    return tampon;
}

void present() {
    present(tampon);
}

void present(const uint8_t *bgr) {
    gpio_xor_mask(1u << PIN_MESURE_FLIP);

    /* Le remaniement des pixels est synchrone — le tampon source est libre au
     * retour — mais la construction des plans de bits qu'il amorce ne l'est pas.
     * L'appelant doit avoir vérifié occupe() au préalable. */
    pilote.update_bgr(bgr);
}

bool occupe() {
    return pilote.occupe();
}

void set_brightness(uint8_t basis) {
    pilote.setBasisBrightness(basis ? basis : 1);
}

uint8_t node_id() {
    return id_noeud;
}

} // namespace display
