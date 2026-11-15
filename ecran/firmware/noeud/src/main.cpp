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
 *
 * Mode secours (26/10/2026) : après VALIDATIONS_AVANT_SECOURS images validées
 * sans un VSYNC, le nœud publie lui-même et tient RDY haut. Un fil VSYNC
 * débranché ne fige plus la rangée ; elle n'est simplement plus synchrone avec
 * les autres. Le premier VSYNC rétablit le fonctionnement normal.
 *
 * Écran noir (29/10/2026) : EFFACER vide les deux tampons de réception, et la
 * dalle publie aussitôt du noir. La tête l'envoie quand sa source se tait
 * (Game Boy éteinte) et à chaque changement de géométrie. La mire d'attente,
 * elle, reste réservée à une tête muette : noir = liaison vivante, rien à
 * montrer ; mire = plus de liaison.
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
#if MIRE_COLONNES
    /* Diagnostic (option CMake MIRE_COLONNES) : sur chaque dalle, quatre
     * bandes de 16 colonnes, rouge, vert, bleu, blanc, de la gauche logique
     * vers la droite. Des bandes rayées révèlent des colonnes entrelacées. */
    static constexpr uint8_t BANDES[4][3] = {{0, 0, 40}, {0, 40, 0}, {40, 0, 0}, {30, 30, 30}};
    for (int y = 0; y < DISPLAY_H; ++y)
        for (int x = 0; x < DISPLAY_W; ++x)
            std::memcpy(&fb[(y * DISPLAY_W + x) * 3], BANDES[(x % PANEL_W) / 16], 3);
    return;
#endif
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

/* Sorties de la dalle qui ne suivent pas leur commande, relues dans la puce
 * (bit n = GPn). Vérifiées au démarrage, avant que le pilote les prenne, /OE
 * tenu haut : aucune LED ne s'allume. /OE lui-même n'est commandé qu'à 1.
 *
 * Chaque sortie est commandée à 1 puis à 0, tirée vers le haut, et doit
 * redescendre. L'ordre compte : le 29/10/2026, GP1 du Pico abîmé partait bien
 * de 0, montait à 1, puis restait à 1 quoi qu'on lui commande (tirage haut,
 * bas ou aucun) — d'où la moitié haute de la rangée verte en continu. Un
 * contrôle « 0 puis 1 » le déclarait sain. */
uint32_t verifier_sorties() {
    uint32_t hs = 0;
    gpio_init(PIN_OE);
    gpio_put(PIN_OE, 1);
    gpio_set_dir(PIN_OE, GPIO_OUT);
    busy_wait_us(20);
    if (!gpio_get(PIN_OE))
        hs |= 1u << PIN_OE;
    for (uint g = PIN_DATA_BASE; g < PIN_OE; ++g) { /* GP0..GP12 : couleurs, adresse, CLK, LAT */
        gpio_init(g);
        gpio_set_pulls(g, true, false);
        gpio_put(g, 1);
        gpio_set_dir(g, GPIO_OUT);
        busy_wait_us(20);
        if (!gpio_get(g))
            hs |= 1u << g;
        gpio_put(g, 0);
        busy_wait_us(20);
        if (gpio_get(g))
            hs |= 1u << g;
        gpio_set_pulls(g, false, true); /* tirage de la mise sous tension */
    }
    return hs;
}

/* Au-delà de ce nombre de sorties en défaut, ce n'est plus une broche abîmée
 * mais la charge : une dalle hors tension tire toutes ses entrées vers 0 par
 * ses diodes de protection (vu le 29/10/2026 : GP0 à GP13 « HS » sur un Pico
 * sain). Le contrôle ne dit alors rien du Pico. */
constexpr int SORTIES_HS_MAX_CREDIBLE = 2;

void signaler_sorties(uint32_t hs) {
    constexpr uint32_t G1 = 1u << (PIN_DATA_BASE + 1);
    if (!hs)
        return;
    if (__builtin_popcount(hs) > SORTIES_HS_MAX_CREDIBLE) {
        printf("    controle des sorties non concluant : %d broches ne suivent pas leur "
               "commande, dalle hors tension au demarrage ?\n", __builtin_popcount(hs));
        return;
    }
    printf("    ⚠ sorties HS :");
    for (uint g = 0; g < 32; ++g)
        if (hs & (1u << g))
            printf(" GP%u", g);
    printf("\n");
    if (hs & G1)
        printf("      G1 : deplacer le fil du trou 2 de la nappe sur GP%d (patte %d), qui "
               "recopie G1\n", PIN_G1_RECHANGE, PATTE_G1_RECHANGE);
    if (hs & ~G1)
        printf("      autre sortie que G1 : pas de rechange, Pico a ecarter\n");
}

struct Compteurs {
    uint32_t publiees;
    uint32_t publiees_secours; /* dont sans VSYNC, en mode secours */
    uint32_t vsync_vides;   /* VSYNC sans image validée */
    uint32_t vsync_occupe;  /* VSYNC pendant une construction : ne devrait pas */
    uint32_t noirs;         /* écrans noirs publiés sur EFFACER */
};

} // namespace

int main() {
    set_sys_clock_khz(CLK_SYS_KHZ, true);
    stdio_init_all();

    const uint32_t sorties_hs = verifier_sorties();
    display::init();
    dessiner_attente(display::backbuffer());
    display::present();
    bool attente_affichee = true;
    bool noir_demande = false; /* EFFACER reçu, noir pas encore publié */
    bool noir_affiche = false; /* la dalle montre déjà du noir : inutile d'y revenir */

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
    printf(" G1          : GP%d, recopie sur GP%d (patte %d)\n", PIN_DATA_BASE + 1,
           PIN_G1_RECHANGE, PATTE_G1_RECHANGE);
    signaler_sorties(sorties_hs);
    printf("=================================================================\n\n");

    uint32_t message[liaison::MOTS_MAX];
    liaison::Entete e;
    uint64_t dernier_message = time_us_64();
    Compteurs c{};
    uint32_t validations_sans_vsync = 0;
    bool secours = false;

    constexpr uint32_t PERIODE_RAPPORT_MS = 10000;
    absolute_time_t prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
    rangee::Stats r_avant = r.stats();
    reception::Stats rx_avant = reception::stats();

    while (true) {
        /* 1. VSYNC */
        if (vsync_recu) {
            vsync_recu = false;
            validations_sans_vsync = 0;
            if (secours) {
                secours = false;
                printf("  VSYNC revenu : fin du mode secours, la rangee est de nouveau "
                       "synchrone\n");
            }
            if (r.valide() < 0) {
                c.vsync_vides++;
            } else if (display::occupe()) {
                c.vsync_occupe++;
                r.prendre_valide();
            } else {
                display::present(r.prendre_valide());
                c.publiees++;
                attente_affichee = false;
                noir_affiche = false;
            }
        }

        /* 2. RDY — masqué : un VSYNC arrivé entre le calcul et l'écriture ne
         * doit pas voir son RDY bas écrasé par un RDY haut périmé. */
        {
            const uint32_t s = save_and_disable_interrupts();
            if (!vsync_recu)
                gpio_put(PIN_RDY, secours || (r.valide() >= 0 && !display::occupe()));
            restore_interrupts(s);
        }

        /* 3. Un message */
        const auto res = reception::lire(message, e);
        if (res == liaison::Lecteur::Resultat::MESSAGE) {
            dernier_message = time_us_64();
            const auto effet = r.appliquer(
                e, reinterpret_cast<const uint8_t *>(message) + sizeof(liaison::Entete));
            switch (effet) {
            case rangee::Effet::VALIDATION:
                if (!secours && ++validations_sans_vsync >= VALIDATIONS_AVANT_SECOURS) {
                    secours = true;
                    printf("  %d images validees sans VSYNC : mode secours, la rangee "
                           "publie seule (fil VSYNC ?)\n", VALIDATIONS_AVANT_SECOURS);
                }
                break;
            case rangee::Effet::LUMINOSITE:
                display::set_brightness(r.luminosite());
                break;
            case rangee::Effet::EFFACEMENT:
                noir_demande = true;
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

        /* 4. Mode secours : publier sans attendre un VSYNC qui ne vient pas. */
        if (secours && r.valide() >= 0 && !display::occupe()) {
            display::present(r.prendre_valide());
            c.publiees++;
            c.publiees_secours++;
            attente_affichee = false;
            noir_affiche = false;
        }

        /* 5. Écran noir demandé par la tête (EFFACER). */
        if (noir_demande && !display::occupe()) {
            noir_demande = false;
            if (!noir_affiche) {
                std::memset(display::backbuffer(), 0, FB_OCTETS);
                display::present();
                noir_affiche = true;
                attente_affichee = false;
                c.noirs++;
            }
        }

        /* Tête muette : la mire le dit sur la dalle elle-même. */
        if (!attente_affichee && time_us_64() - dernier_message > SILENCE_MS * 1000ull &&
            !display::occupe()) {
            dessiner_attente(display::backbuffer());
            display::present();
            attente_affichee = true;
            noir_affiche = false;
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
            printf("    erreurs %lu  inacheves %lu  resynchros %lu  validations %lu  refus %lu  "
                   "vsync vides %lu  vsync pilote occupe %lu\n",
                   (unsigned long)(rx.erreurs - rx_avant.erreurs),
                   (unsigned long)(rx.inacheves - rx_avant.inacheves),
                   (unsigned long)(rx.resynchros - rx_avant.resynchros),
                   (unsigned long)(rs.validations - r_avant.validations),
                   (unsigned long)(rs.refus - r_avant.refus), (unsigned long)c.vsync_vides,
                   (unsigned long)c.vsync_occupe);
            printf("    pixels %lu (hors dalle %lu)  sans hello %lu  effacements %lu  "
                   "perimees %lu\n",
                   (unsigned long)(rs.pixels - r_avant.pixels),
                   (unsigned long)(rs.hors_champ - r_avant.hors_champ),
                   (unsigned long)(rs.sans_hello - r_avant.sans_hello),
                   (unsigned long)(rs.effacements - r_avant.effacements),
                   (unsigned long)(rs.perimees - r_avant.perimees));
            if (c.noirs)
                printf("    ecran noir sur ordre de la tete (source muette) : %lu fois\n",
                       (unsigned long)c.noirs);
            if (c.publiees_secours)
                printf("    ⚠ MODE SECOURS : %lu images publiees sans VSYNC — verifier le "
                       "fil VSYNC (GP%d)\n", (unsigned long)c.publiees_secours, PIN_VSYNC);
            signaler_sorties(sorties_hs);
            printf("\n");
            r_avant = rs;
            rx_avant = rx;
            c = Compteurs{};
            prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
        }
    }
}
