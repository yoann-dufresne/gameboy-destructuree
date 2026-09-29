/**
 * Tête — phase 5a : la réception seule
 *
 * La tête reçoit, réassemble, place et découpe — sans rien relayer encore :
 * les segments destinés aux nœuds sont comptés, rangée par rangée. C'est le
 * banc du seul chiffre qui peut remettre l'architecture en cause : une image
 * 192×192 en IDX8 à 60 img/s sur une seule antenne (plan §4.4, §5).
 *
 * Critère de sortie : < 0,1 % de perte sur 10 minutes, et des pixels par image
 * et par rangée conformes à la géométrie de la source.
 */

#include <cstdio>
#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "config.h"
#include "reseau.hpp"
#include "pxl2.h"

namespace {

const char *nom_format(uint8_t f) {
    switch (f) {
        case 0: return "BGR888";
        case 1: return "RGB565";
        case 2: return "IDX2";
        case 3: return "IDX4";
        case 4: return "IDX8";
        default: return "?";
    }
}

/* Écrit v / 100 avec deux décimales et une virgule : « 17,68 ». */
void deux_decimales(char *out, size_t n, uint64_t centiemes) {
    std::snprintf(out, n, "%lu,%02lu", (unsigned long)(centiemes / 100),
                  (unsigned long)(centiemes % 100));
}

struct Stat {
    uint32_t n, somme, mini, maxi;
    void noter(uint32_t v) {
        n++;
        somme += v;
        if (v < mini) mini = v;
        if (v > maxi) maxi = v;
    }
    uint32_t moy() const { return n ? somme / n : 0; }
    void zero() { *this = Stat{0, 0, ~0u, 0}; }
};

void rapport(const reseau::Stats &st, const reseau::Stats &avant, uint32_t duree_ms,
             Stat &assemblage, const reseau::Image &derniere) {
    const uint32_t images = st.images - avant.images;
    const uint32_t incompl = st.incompletes - avant.incompletes;
    const uint64_t octets = st.octets - avant.octets;
    char debit[16], cadence[16], perte[16];
    deux_decimales(debit, sizeof(debit), octets * 8u * 100u / 1000u / duree_ms); /* Mbit/s */
    deux_decimales(cadence, sizeof(cadence), (uint64_t)images * 100000u / duree_ms);
    /* Perte : images abandonnées sur images tentées, en centièmes de %. */
    deux_decimales(perte, sizeof(perte),
                   (images + incompl) ? (uint64_t)incompl * 10000u / (images + incompl) : 0);

    printf("  %lu images (%s/s)  %s Mbit/s  perte %s %%   paquets %lu (PXL1 %lu)\n",
           (unsigned long)images, cadence, debit, perte,
           (unsigned long)(st.paquets - avant.paquets), (unsigned long)(st.pxl1 - avant.pxl1));
    printf("    rejets %lu  hors canevas %lu  format refuse %lu  sans geometrie %lu  "
           "doublons %lu  retard. %lu  resync %lu  ctrl %lu  ping %lu\n",
           (unsigned long)(st.rejets - avant.rejets),
           (unsigned long)(st.hors_canevas - avant.hors_canevas),
           (unsigned long)(st.format_refuse - avant.format_refuse),
           (unsigned long)(st.sans_geometrie - avant.sans_geometrie),
           (unsigned long)(st.doublons - avant.doublons),
           (unsigned long)(st.retardataires - avant.retardataires),
           (unsigned long)(st.resynchros - avant.resynchros),
           (unsigned long)(st.ctrl - avant.ctrl), (unsigned long)(st.pings - avant.pings));
    if (st.envois_echoues != avant.envois_echoues)
        printf("    ⚠ %lu accuses non emis par la tete\n",
               (unsigned long)(st.envois_echoues - avant.envois_echoues));
    if (st.non_suivies != avant.non_suivies)
        printf("    ⚠ %lu tranches hors du suivi des doublons : tranches trop petites\n",
               (unsigned long)(st.non_suivies - avant.non_suivies));
    if (images == 0) {
        printf("\n");
        return;
    }
    printf("    assemblage (us)   moy %5lu  min %5lu  max %5lu\n",
           (unsigned long)assemblage.moy(), (unsigned long)assemblage.mini,
           (unsigned long)assemblage.maxi);

    /* Ce que chaque nœud aurait reçu. Pour une source 192×192 : 12 288 pixels
     * par rangée ; pour la Game Boy centrée : 6 400 / 10 240 / 6 400. */
    printf("    pixels par image et par rangee :");
    for (int r = 0; r < NB_RANGEES; ++r)
        printf(" %s%lu", r ? "/ " : "",
               (unsigned long)((st.pixels_rangee[r] - avant.pixels_rangee[r]) / images));
    char seg[16];
    deux_decimales(seg, sizeof(seg), (uint64_t)(st.segments - avant.segments) * 100u / images);
    printf("   segments par image %s\n", seg);
    printf("    source : %ux%u %s en PXL%u\n\n", derniere.largeur, derniere.hauteur,
           nom_format(derniere.format), derniere.protocole);
}

} // namespace

int main() {
    set_sys_clock_khz(CLK_SYS_KHZ, true);
    stdio_init_all();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=================================================================\n");
    printf(" MODULE ECRAN — TETE — phase 5a : reception, placement, decoupe\n");
    printf("-----------------------------------------------------------------\n");
    printf(" canevas     : %d x %d, %d rangees de %d px\n", CANEVAS_W, CANEVAS_H,
           NB_RANGEES, RANGEE_H);
    printf(" formats     :");
    for (uint8_t f = 0; f < 16; ++f)
        if ((FORMATS_ACCEPTES >> f) & 1u)
            printf(" %s", nom_format(f));
    printf("\n protocoles  : PXL2, et PXL1 + CTRL_GEOMETRIE en compatibilite\n");
    printf(" clk_sys     : %lu MHz\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
    printf(" liaisons    : pas encore (phase 5b) — segments comptes seulement\n");
    printf("=================================================================\n\n");

    if (!reseau::connecter()) {
        printf("  pas de reseau : arret.\n");
        while (true)
            tight_loop_contents();
    }
    printf("  ecoute sur  %s:%d\n\n", reseau::adresse_ip(), PXL2_PORT);

    constexpr uint32_t PERIODE_RAPPORT_MS = 10000;
    absolute_time_t prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
    reseau::Stats avant = reseau::stats();
    reseau::Image derniere{};
    Stat assemblage;
    assemblage.zero();

    while (true) {
        reseau::entretenir();

        reseau::Image img;
        if (reseau::image_complete(img)) {
            /* En 5a l'image est « présentée » dès qu'elle est complète : l'accusé
             * mesure donc l'air et le réassemblage. En 5b il partira au VSYNC. */
            reseau::acquitter(img);
            assemblage.noter((uint32_t)(img.t_dernier_us - img.t_premier_us));
            derniere = img;
        }

        uint16_t w, h;
        uint8_t f;
        if (reseau::geometrie_changee(w, h, f))
            printf("  nouvelle source : %ux%u %s — placee en (%d, %d)\n", w, h, nom_format(f),
                   (CANEVAS_W - w) / 2, (CANEVAS_H - h) / 2);

        if (const uint8_t basis = reseau::luminosite_demandee())
            printf("  luminosite demandee : %u (relayee aux noeuds en phase 5b)\n", basis);

        if (time_reached(prochain_rapport)) {
            const reseau::Stats st = reseau::stats();
            rapport(st, avant, PERIODE_RAPPORT_MS, assemblage, derniere);
            avant = st;
            assemblage.zero();
            prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
        }
    }
}
