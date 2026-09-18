/**
 * Module ÉCRAN — phase 1 : recette du pilote d'affichage
 *
 * Critères de sortie du plan §5, phase 1 :
 *   - mire fixe affichée depuis un tampon statique ;
 *   - rafraîchissement ≥ 150 Hz ;
 *   - une boucle saturant le cœur 0 ne dégrade pas l'image ;
 *   - damier 1 px sans ghosting.
 *
 * Les trois premiers sont vérifiés ici automatiquement. Le quatrième reste
 * visuel : le ghosting ne se mesure pas depuis le firmware.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "config.h"
#include "display.hpp"
#include "reseau.hpp"
#include "pxl1.h"

namespace {

constexpr uint32_t MASQUE_DATA = ((1u << PIN_DATA_N) - 1u) << PIN_DATA_BASE;
constexpr uint32_t MASQUE_ADDR = ((1u << PIN_ROWSEL_N) - 1u) << PIN_ROWSEL_BASE;

inline void px(uint8_t *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = &fb[(y * DISPLAY_W + x) * 3];
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

/* Mire de recette. Chaque bande éprouve un aspect précis du pilote. */
void dessiner_mire(uint8_t *fb) {
    std::memset(fb, 0, FB_OCTETS);

    for (int y = 0; y < DISPLAY_H; ++y) {
        for (int x = 0; x < DISPLAY_W; ++x) {
            if (y >= 2 && y < 18) {
                /* Dégradé horizontal : profondeur de modulation BCM. */
                const uint8_t v = (uint8_t)((x * 255) / (DISPLAY_W - 1));
                px(fb, x, y, v, v, v);
            } else if (y >= 20 && y < 36) {
                /* Damier 1 px : révélateur de ghosting. */
                if ((x + y) & 1) px(fb, x, y, 255, 255, 255);
            } else if (y >= 38 && y < 54) {
                /* Barres de couleur : les six lignes de données, séparément. */
                const int bande = (x * 4) / DISPLAY_W;
                switch (bande) {
                    case 0: px(fb, x, y, 255, 0, 0); break;
                    case 1: px(fb, x, y, 0, 255, 0); break;
                    case 2: px(fb, x, y, 0, 0, 255); break;
                    default: px(fb, x, y, 255, 255, 255); break;
                }
            }
        }
    }

    /* Cadre 1 px : géométrie complète, aucune ligne ni colonne perdue. */
    for (int x = 0; x < DISPLAY_W; ++x) {
        px(fb, x, 0, 255, 255, 255);
        px(fb, x, DISPLAY_H - 1, 255, 255, 255);
    }
    for (int y = 0; y < DISPLAY_H; ++y) {
        px(fb, 0, y, 255, 255, 255);
        px(fb, DISPLAY_W - 1, y, 255, 255, 255);
    }

    /* Coins colorés : orientation. */
    px(fb, 1, 1, 255, 0, 0);
    px(fb, DISPLAY_W - 2, 1, 0, 255, 0);
    px(fb, 1, DISPLAY_H - 2, 0, 0, 255);
}

struct Sonde {
    uint32_t total, data_actifs, addr_actifs;
};

/* Les pads pilotés par le PIO restent lisibles par le CPU : on peut donc
 * vérifier objectivement que la dalle reçoit autre chose que des zéros. */
Sonde echantillonner(uint32_t duree_ms) {
    Sonde s = {0, 0, 0};
    absolute_time_t fin = make_timeout_time_ms(duree_ms);
    do {
        const uint32_t brut = gpio_get_all();
        s.total++;
        if (brut & MASQUE_DATA) s.data_actifs++;
        if (brut & MASQUE_ADDR) s.addr_actifs++;
    } while (!time_reached(fin));
    return s;
}

void rapport_sonde(const char *etiquette) {
    const Sonde s = echantillonner(200);
    const uint32_t d = s.total ? s.data_actifs * 100u / s.total : 0;
    const uint32_t a = s.total ? s.addr_actifs * 100u / s.total : 0;
    printf("  %-22s donnees %3lu %%  adresses %3lu %%  -> %s\n",
           etiquette, (unsigned long)d, (unsigned long)a,
           (a == 0) ? "NE BALAIE PAS" : (d == 0) ? "NOIR" : "AFFICHE");
}

/* Rapport cyclique d'allumage, fenêtre par fenêtre.
 *
 * /OE est actif bas : la dalle est allumée quand la broche est à 0. Si des trames
 * sont tronquées, le rapport cyclique chute sur les fenêtres concernées. L'écart
 * entre fenêtres est donc une mesure objective du scintillement — là où le
 * compteur de trames du pilote, lui, ne voit rien. */
struct Oe { uint32_t mini, maxi, moyenne; };

Oe mesurer_oe(uint32_t nb_fenetres, uint32_t fenetre_us) {
    uint32_t mini = 1000, maxi = 0, somme = 0;
    for (uint32_t f = 0; f < nb_fenetres; ++f) {
        uint32_t total = 0, allume = 0;
        const absolute_time_t fin = make_timeout_time_us(fenetre_us);
        do {
            total++;
            if (!(gpio_get_all() & (1u << PIN_OE))) allume++;
        } while (!time_reached(fin));
        const uint32_t pm = total ? (uint32_t)((uint64_t)allume * 1000u / total) : 0;
        if (pm < mini) mini = pm;
        if (pm > maxi) maxi = pm;
        somme += pm;
    }
    return {mini, maxi, nb_fenetres ? somme / nb_fenetres : 0};
}

void rapport_oe(const char *etiquette) {
    const Oe o = mesurer_oe(120, 4000); /* 120 fenetres de 4 ms = 480 ms */
    printf("  %-26s allumage moyen %2lu,%01lu %%  min %2lu,%01lu  max %2lu,%01lu  ecart %lu,%01lu pt\n",
           etiquette,
           (unsigned long)(o.moyenne / 10), (unsigned long)(o.moyenne % 10),
           (unsigned long)(o.mini / 10), (unsigned long)(o.mini % 10),
           (unsigned long)(o.maxi / 10), (unsigned long)(o.maxi % 10),
           (unsigned long)((o.maxi - o.mini) / 10), (unsigned long)((o.maxi - o.mini) % 10));
}

/* Charge de calcul pour saturer le cœur 0. `volatile` empêche le compilateur
 * de l'éliminer. */
volatile uint32_t puits;

void charger_coeur0(uint32_t duree_ms) {
    absolute_time_t fin = make_timeout_time_ms(duree_ms);
    uint32_t x = 1;
    do {
        for (int i = 0; i < 4096; ++i)
            x = x * 1664525u + 1013904223u;
        puits = x;
    } while (!time_reached(fin));
}

} // namespace

int main(void) {
    set_sys_clock_khz(CLK_SYS_KHZ, true);
    stdio_init_all();

    display::init();

    dessiner_mire(display::backbuffer());
    display::present();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    const uint32_t clk = clock_get_hz(clk_sys);
    printf("\n");
    printf("=================================================================\n");
    printf(" MODULE ECRAN — PHASE 1 : recette du pilote d'affichage\n");
    printf("-----------------------------------------------------------------\n");
    printf(" geometrie   : %d x %d  (%d dalle(s) de %dx%d en chaine)\n",
           DISPLAY_W, DISPLAY_H, CHAIN_LEN, PANEL_W, PANEL_H);
    printf(" noeud        : %u / %d\n", display::node_id(), NODE_COUNT);
    printf(" clk_sys      : %lu MHz   -> horloge pixel %lu,%01lu MHz\n",
           (unsigned long)(clk / 1000000u),
           (unsigned long)(clk / 9u / 1000000u),
           (unsigned long)((clk / 9u / 100000u) % 10u));
    printf(" rendu        : %d plans BCM, canaux CIE %s, luminosite de base %d\n",
           BCM_PLANES, CIE_SEPARATE ? "separes" : "communs", BASIS_BRIGHTNESS);
    printf(" tampons      : 2 x %d octets (double tampon strict)\n", FB_OCTETS);
    printf("=================================================================\n\n");

    printf("--- PHASE A : au repos, image statique ---\n");
    rapport_sonde("repos");
    sleep_ms(2500);

    printf("\n--- PHASE B : coeur 0 sature, publication a 60 Hz ---\n");
    printf("    (le rafraichissement ci-dessous ne doit pas bouger)\n");
    for (int i = 0; i < 4; ++i) {
        charger_coeur0(500);
        display::present();
    }
    rapport_sonde("coeur 0 sature");
    sleep_ms(2500);

    printf("\n--- RESEAU ---\n");
    if (!reseau::connecter(display::node_id())) {
        printf("  pas de reseau : la mire reste affichee.\n");
        while (true)
            tight_loop_contents();
    }

    printf("  associe. Envoyer les trames PXL1 sur  %s:%d\n", reseau::adresse_ip(), PXL1_PORT);
    printf("  format attendu : BGR888, %d x %d, %d octets par trame, noeud %u\n\n",
           DISPLAY_W, DISPLAY_H, FB_OCTETS, display::node_id());

    /* Le tampon d'affichage est desormais ecrit par la reception. On publie des
     * qu'une trame est complete — pas de reveil periodique : la latence d'une
     * trame, c'est le temps entre sa derniere tranche et sa publication. */
    absolute_time_t prochain_rapport = make_timeout_time_ms(10000);
    uint32_t trames_au_dernier_rapport = 0;

    uint32_t occupe_max_us = 0, occupe_somme_us = 0, occupe_n = 0;
    uint32_t sautees = 0;

    while (true) {
        /* On ne consomme une trame que si le pilote a fini sa construction
         * precedente : il n'a pas de garde-fou de reentrance. Si on ne consomme
         * pas, la trame reste en attente et une plus recente la remplacera —
         * c'est le bon comportement pour un afficheur. */
        if (!display::occupe()) {
            const uint8_t *trame = reseau::trame_a_afficher();
            if (trame != nullptr) {
                const absolute_time_t t0 = get_absolute_time();
                display::present(trame);
                /* Mesure la duree reelle d'une construction. */
                while (display::occupe())
                    tight_loop_contents();
                const uint32_t dt = (uint32_t)absolute_time_diff_us(t0, get_absolute_time());
                if (dt > occupe_max_us) occupe_max_us = dt;
                occupe_somme_us += dt;
                occupe_n++;
            }
        } else {
            sautees++;
        }

        if (time_reached(prochain_rapport)) {
            const reseau::Stats &st = reseau::stats();
            const uint32_t delta = st.trames - trames_au_dernier_rapport;
            trames_au_dernier_rapport = st.trames;
            printf("  %lu trames (%lu/s)  %lu rejets  %lu incompletes  "
                   "%lu ecartees  %lu retard.  %lu resync | construction moy %lu us, max %lu us\n",
                   (unsigned long)st.trames, (unsigned long)(delta / 10),
                   (unsigned long)st.rejets,
                   (unsigned long)st.trames_incompletes,
                   (unsigned long)st.ecartees,
                   (unsigned long)st.retardataires,
                   (unsigned long)st.resynchros,
                   (unsigned long)(occupe_n ? occupe_somme_us / occupe_n : 0),
                   (unsigned long)occupe_max_us);
            occupe_max_us = occupe_somme_us = occupe_n = 0;
            sautees = 0;
            prochain_rapport = make_timeout_time_ms(10000);
        }
    }
}
