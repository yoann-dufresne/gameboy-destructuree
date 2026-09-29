/**
 * Nœud (v2) — reçoit sa rangée de la tête et la publie au VSYNC
 *
 * Boucle principale, dans cet ordre à chaque tour (plan §2.6) :
 *
 *   1. VSYNC d'abord. Juste après son impulsion, la tête peut reprendre le
 *      tampon qu'elle vient de faire publier : il faut l'avoir publié avant de
 *      lire le moindre message de plus.
 *   2. RDY = une image validée attend, et le pilote est libre. L'interruption
 *      du VSYNC abaisse RDY elle-même, en une microseconde : la tête ne peut
 *      pas prendre un RDY périmé pour une réponse à sa validation suivante.
 *   3. Un message de la liaison, appliqué à la rangée.
 *
 * Le cœur 1 appartient au pilote HUB75 (display.cpp) ; tout le reste tourne
 * sur le cœur 0, sans interruption DMA — le pilote les prend toutes les deux.
 */

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "config.h"
#include "display.hpp"
#include "liaison.h"
#include "rangee.hpp"
#include "reception.hpp"

namespace {

alignas(4) uint8_t tampon0[FB_OCTETS];
alignas(4) uint8_t tampon1[FB_OCTETS];
rangee::Rangee r(DISPLAY_W, DISPLAY_H, tampon0, tampon1);

volatile bool vsync_recu = false;

void sur_vsync(uint gpio, uint32_t evenements) {
    if (gpio != PIN_VSYNC || !(evenements & GPIO_IRQ_EDGE_FALL))
        return;
    gpio_put(PIN_RDY, 0);
    vsync_recu = true;
}

/* Mire « pas de liaison » : un cadre bleu sombre et une diagonale. De quoi
 * savoir, sans console, que la dalle et le nœud vivent. */
void dessiner_attente(uint8_t *fb) {
    std::memset(fb, 0, FB_OCTETS);
    auto px = [fb](int x, int y) {
        uint8_t *p = &fb[(y * DISPLAY_W + x) * 3];
        p[0] = 60; /* B */
        p[1] = 10;
        p[2] = 0;
    };
    for (int x = 0; x < DISPLAY_W; ++x) {
        px(x, 0);
        px(x, DISPLAY_H - 1);
    }
    for (int y = 0; y < DISPLAY_H; ++y) {
        px(0, y);
        px(DISPLAY_W - 1, y);
        if (y < DISPLAY_W)
            px(y, y);
    }
}

struct Compteurs {
    uint32_t publiees;
    uint32_t vsync_vides;   /* VSYNC sans image validée */
    uint32_t vsync_occupe;  /* VSYNC pendant une construction : ne devrait pas */
};

} // namespace

int main() {
    set_sys_clock_khz(CLK_SYS_KHZ, true);
    stdio_init_all();

    display::init();
    dessiner_attente(display::backbuffer());
    display::present();
    bool attente_affichee = true;

    gpio_init(PIN_RDY);
    gpio_set_dir(PIN_RDY, GPIO_OUT);
    gpio_put(PIN_RDY, 0);
    gpio_init(PIN_MESURE_RX);
    gpio_set_dir(PIN_MESURE_RX, GPIO_OUT);
    gpio_init(PIN_VSYNC);
    gpio_set_dir(PIN_VSYNC, GPIO_IN);
    gpio_pull_up(PIN_VSYNC); /* nappe débranchée : pas de front parasite */
    gpio_set_irq_enabled_with_callback(PIN_VSYNC, GPIO_IRQ_EDGE_FALL, true, sur_vsync);

    reception::init();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);
    printf("\n");
    printf("=================================================================\n");
    printf(" MODULE ECRAN — NOEUD : liaison -> %d dalle(s), %d x %d\n", CHAIN_LEN, DISPLAY_W,
           DISPLAY_H);
    printf("-----------------------------------------------------------------\n");
    printf(" liaison     : GP%d..GP%d (D0 D1 CLK CS), VSYNC GP%d, RDY GP%d\n", PIN_LIEN_BASE,
           PIN_LIEN_BASE + 3, PIN_VSYNC, PIN_RDY);
    printf(" rendu       : %d plans BCM, luminosite de base %d\n", BCM_PLANES, BASIS_BRIGHTNESS);
    printf("=================================================================\n\n");

    uint32_t message[liaison::MOTS_MAX];
    liaison::Entete e;
    uint64_t dernier_message = time_us_64();
    Compteurs c{};

    constexpr uint32_t PERIODE_RAPPORT_MS = 10000;
    absolute_time_t prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
    rangee::Stats r_avant = r.stats();
    reception::Stats rx_avant = reception::stats();

    while (true) {
        /* 1. VSYNC */
        if (vsync_recu) {
            vsync_recu = false;
            if (r.valide() < 0) {
                c.vsync_vides++;
            } else if (display::occupe()) {
                c.vsync_occupe++;
                r.prendre_valide();
            } else {
                display::present(r.prendre_valide());
                c.publiees++;
                attente_affichee = false;
            }
        }

        /* 2. RDY — masqué : un VSYNC arrivé entre le calcul et l'écriture ne
         * doit pas voir son RDY bas écrasé par un RDY haut périmé. */
        {
            const uint32_t s = save_and_disable_interrupts();
            if (!vsync_recu)
                gpio_put(PIN_RDY, r.valide() >= 0 && !display::occupe());
            restore_interrupts(s);
        }

        /* 3. Un message */
        const auto res = reception::lire(message, e);
        if (res == liaison::Lecteur::Resultat::MESSAGE) {
            dernier_message = time_us_64();
            const auto effet = r.appliquer(
                e, reinterpret_cast<const uint8_t *>(message) + sizeof(liaison::Entete));
            switch (effet) {
            case rangee::Effet::LUMINOSITE:
                display::set_brightness(r.luminosite());
                break;
            case rangee::Effet::HELLO:
                printf("  HELLO : rangee %u, canevas de %u px de large, %u px de haut%s\n",
                       r.numero(), r.canevas_w(), r.rangee_h(),
                       r.rangee_h() != DISPLAY_H ? "  ⚠ hauteur differente de la dalle" : "");
                break;
            default:
                break;
            }
            if (e.type == liaison::VALIDER)
                gpio_xor_mask(1u << PIN_MESURE_RX);
        }

        /* Tête muette : la mire le dit sur la dalle elle-même. */
        if (!attente_affichee && time_us_64() - dernier_message > SILENCE_MS * 1000ull &&
            !display::occupe()) {
            dessiner_attente(display::backbuffer());
            display::present();
            attente_affichee = true;
            printf("  plus de message de la tete depuis %d ms : mire d'attente\n", SILENCE_MS);
        }

        if (time_reached(prochain_rapport)) {
            const rangee::Stats &rs = r.stats();
            const reception::Stats &rx = reception::stats();
            printf("  rangee %u : %lu images publiees (%lu,%01lu/s)   %lu messages  "
                   "%lu,%02lu Mbit/s\n",
                   r.numero(), (unsigned long)c.publiees,
                   (unsigned long)(c.publiees / 10), (unsigned long)(c.publiees % 10),
                   (unsigned long)(rx.messages - rx_avant.messages),
                   (unsigned long)((rx.octets - rx_avant.octets) * 8u / 100000u / 100u),
                   (unsigned long)((rx.octets - rx_avant.octets) * 8u / 100000u % 100u));
            printf("    erreurs %lu  resynchros %lu  validations %lu  refus %lu  "
                   "vsync vides %lu  vsync pilote occupe %lu\n",
                   (unsigned long)(rx.erreurs - rx_avant.erreurs),
                   (unsigned long)(rx.resynchros - rx_avant.resynchros),
                   (unsigned long)(rs.validations - r_avant.validations),
                   (unsigned long)(rs.refus - r_avant.refus), (unsigned long)c.vsync_vides,
                   (unsigned long)c.vsync_occupe);
            printf("    pixels %lu (hors dalle %lu)  sans hello %lu  effacements %lu\n\n",
                   (unsigned long)(rs.pixels - r_avant.pixels),
                   (unsigned long)(rs.hors_champ - r_avant.hors_champ),
                   (unsigned long)(rs.sans_hello - r_avant.sans_hello),
                   (unsigned long)(rs.effacements - r_avant.effacements));
            r_avant = rs;
            rx_avant = rx;
            c = Compteurs{};
            prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
        }
    }
}
