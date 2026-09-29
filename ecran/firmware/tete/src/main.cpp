/**
 * Tête — réception, relais vers les nœuds, synchronisation
 *
 * La réception (reseau.cpp) place et découpe chaque image, et relaie ses
 * segments vers les nœuds au fil de l'eau. La synchronisation (plan §2.6) :
 * une image complète est validée auprès des nœuds (VALIDER), puis la tête
 * attend que tous lèvent RDY et impulse VSYNC — ils publient ensemble. Un
 * nœud en retard au-delà de GARDE_RDY_US fait abandonner l'image à tous : un
 * saut d'image plutôt qu'un déchirement.
 *
 * Deux cœurs :
 *   cœur 0 — WiFi et lwIP (en arrière-plan), entretien des liaisons, console ;
 *   cœur 1 — la synchronisation, et rien d'autre.
 *
 * La synchronisation a quitté le cœur 0 le 29/09/2026 : sous charge, un
 * printf vers la console USB y bloquait la boucle jusqu'à 160 ms — la pile
 * WiFi occupe le processeur pendant les rafales et l'USB n'avance plus.
 * Pendant ce temps, des images complètes étaient supplantées avant d'être
 * validées. Mesuré : ~15 images sur 600 par tranche de 10 s.
 *
 * Sans nœud branché, les RDY sont tirés haut : la tête tourne seule, comme en
 * phase 5a, et l'accusé part au VSYNC.
 */

#include <cstdio>
#include <cstdint>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "config.h"
#include "lien.hpp"
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

/* Tenu par le cœur 1, lu par le cœur 0. Les compteurs sont cumulés et le
 * rapport calcule des différences ; seuls les maxima sont relevés puis remis
 * à zéro par le cœur 0. Une lecture à cheval sur une mise à jour fausse un
 * relevé d'une unité, sans plus : ce ne sont que des statistiques. */
struct Synchro {
    volatile uint32_t vsync;          /* images publiées */
    volatile uint32_t abandons;       /* un nœud n'a pas levé RDY à temps */
    volatile uint64_t attente_somme;  /* de VALIDER au VSYNC, en µs */
    volatile uint32_t attente_max;
    volatile uint64_t assemblage_somme; /* première → dernière tranche, en µs */
    volatile uint32_t assemblage_max;
    volatile uint32_t boucle_max;     /* plus long tour de boucle du cœur 1, en µs */
    reseau::Image derniere;
};

Synchro synchro{};

/* ----------------------------------------------------------- cœur 1 */

void coeur1() {
    enum class Etat { REPOS, ATTENTE } etat = Etat::REPOS;
    reseau::Validation v{};
    uint64_t t_validation = 0;
    uint64_t t_tour = time_us_64();

    while (true) {
        const uint64_t maintenant = time_us_64();
        if ((uint32_t)(maintenant - t_tour) > synchro.boucle_max)
            synchro.boucle_max = (uint32_t)(maintenant - t_tour);
        t_tour = maintenant;

        if (etat == Etat::REPOS) {
            if (reseau::valider_prochaine(v)) {
                etat = Etat::ATTENTE;
                t_validation = time_us_64();
            }
            continue;
        }

        /* Le VALIDER doit avoir quitté la tête sur chaque liaison, et chaque
         * nœud l'avoir accepté : alors seulement RDY a un sens. */
        bool prets = true;
        for (uint8_t k = 0; k < NB_RANGEES && prets; ++k)
            prets = lien::transmis(k, v.marques[k]) && lien::pret(k);
        const uint64_t t = time_us_64();
        if (prets) {
            lien::vsync();
            reseau::conclure(v, true);
            const uint32_t attente = (uint32_t)(t - t_validation);
            const uint32_t assemblage = (uint32_t)(v.img.t_dernier_us - v.img.t_premier_us);
            synchro.attente_somme = synchro.attente_somme + attente;
            if (attente > synchro.attente_max)
                synchro.attente_max = attente;
            synchro.assemblage_somme = synchro.assemblage_somme + assemblage;
            if (assemblage > synchro.assemblage_max)
                synchro.assemblage_max = assemblage;
            synchro.derniere = v.img;
            synchro.vsync = synchro.vsync + 1;
            etat = Etat::REPOS;
        } else if (t - t_validation > GARDE_RDY_US) {
            reseau::conclure(v, false);
            synchro.abandons = synchro.abandons + 1;
            etat = Etat::REPOS;
        }
    }
}

/* ------------------------------------------------------------ rapport */

struct Releve {
    uint32_t vsync, abandons;
    uint64_t attente_somme, assemblage_somme;
};

Releve relever() {
    return {synchro.vsync, synchro.abandons, synchro.attente_somme, synchro.assemblage_somme};
}

void rapport(const reseau::Stats &st, const reseau::Stats &avant, const lien::Stats &li,
             const lien::Stats &li_avant, const Releve &sy, const Releve &sy_avant,
             uint32_t duree_ms) {
    const uint32_t images = st.images - avant.images;
    const uint32_t incompl = st.incompletes - avant.incompletes;
    const uint64_t octets = st.octets - avant.octets;
    const uint32_t vsync = sy.vsync - sy_avant.vsync;
    char debit[16], cadence[16], perte[16];
    deux_decimales(debit, sizeof(debit), octets * 8u * 100u / 1000u / duree_ms); /* Mbit/s */
    deux_decimales(cadence, sizeof(cadence), (uint64_t)vsync * 100000u / duree_ms);
    /* Perte : images abandonnées sur images tentées, en centièmes de %. */
    deux_decimales(perte, sizeof(perte),
                   (images + incompl) ? (uint64_t)incompl * 10000u / (images + incompl) : 0);

    printf("  %lu images recues, %lu publiees (%s/s)  %s Mbit/s  perte %s %%   "
           "paquets %lu (PXL1 %lu)\n",
           (unsigned long)images, (unsigned long)vsync, cadence, debit, perte,
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

    printf("    synchro : abandons %lu  supplantees %lu+%lu  relais perdus %lu  "
           "attente RDY (us) moy %lu max %lu  boucle max %lu us\n",
           (unsigned long)(sy.abandons - sy_avant.abandons),
           (unsigned long)(st.supplantees_reception - avant.supplantees_reception),
           (unsigned long)(st.supplantees_validation - avant.supplantees_validation),
           (unsigned long)(st.relais_perdus - avant.relais_perdus),
           (unsigned long)(vsync ? (sy.attente_somme - sy_avant.attente_somme) / vsync : 0),
           (unsigned long)synchro.attente_max, (unsigned long)synchro.boucle_max);
    printf("    liaisons :");
    for (int k = 0; k < NB_RANGEES; ++k) {
        char d[16];
        deux_decimales(d, sizeof(d),
                       (li.octets[k] - li_avant.octets[k]) * 8u * 100u / 1000u / duree_ms);
        printf("  L%d %lu msg %s Mbit/s", k,
               (unsigned long)(li.messages[k] - li_avant.messages[k]), d);
        if (li.debordements[k] != li_avant.debordements[k])
            printf(" ⚠ %lu debordements",
                   (unsigned long)(li.debordements[k] - li_avant.debordements[k]));
        printf("  RDY %s", lien::pret((uint8_t)k) ? "haut" : "bas");
    }
    printf("\n");
    synchro.attente_max = 0;
    synchro.boucle_max = 0;

    if (vsync) {
        printf("    assemblage (us) moy %lu max %lu\n",
               (unsigned long)((sy.assemblage_somme - sy_avant.assemblage_somme) / vsync),
               (unsigned long)synchro.assemblage_max);
        synchro.assemblage_max = 0;
    }
    if (images) {
        /* Ce que chaque nœud a reçu. Pour une source 192×192 : 12 288 pixels
         * par rangée ; pour la Game Boy centrée : 6 400 / 10 240 / 6 400. */
        printf("    pixels par image et par rangee :");
        for (int r = 0; r < NB_RANGEES; ++r)
            printf(" %s%lu", r ? "/ " : "",
                   (unsigned long)((st.pixels_rangee[r] - avant.pixels_rangee[r]) / images));
        char seg[16];
        deux_decimales(seg, sizeof(seg),
                       (uint64_t)(st.segments - avant.segments) * 100u / images);
        const reseau::Image d = synchro.derniere;
        printf("   segments par image %s\n    source : %ux%u %s en PXL%u\n", seg, d.largeur,
               d.hauteur, nom_format(d.format), d.protocole);
    }
    printf("\n");
}

} // namespace

int main() {
    set_sys_clock_khz(CLK_SYS_KHZ, true);
    stdio_init_all();

    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n");
    printf("=================================================================\n");
    printf(" MODULE ECRAN — TETE : reception, decoupe, relais, synchronisation\n");
    printf("-----------------------------------------------------------------\n");
    printf(" canevas     : %d x %d, %d rangee(s) de %d px%s\n", CANEVAS_W, CANEVAS_H,
           NB_RANGEES, RANGEE_H, BANC_UNE_DALLE ? "  — BANC UNE DALLE" : "");
    printf(" formats     :");
    for (uint8_t f = 0; f < 16; ++f)
        if ((FORMATS_ACCEPTES >> f) & 1u)
            printf(" %s", nom_format(f));
    printf("\n protocoles  : PXL2, et PXL1 + CTRL_GEOMETRIE en compatibilite\n");
    printf(" clk_sys     : %lu MHz\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
#if WIFI_STATION
    printf(" wifi        : station — la tete rejoint la box\n");
#else
    printf(" wifi        : point d'acces, canal %d, adresse 192.168.4.1\n", ECRAN_CANAL_WIFI);
#endif
    printf(" liaisons    : %d, horloge %d kHz (%d Mbit/s), garde RDY %d ms\n", NB_RANGEES,
           LIEN_HORLOGE_KHZ, 2 * LIEN_HORLOGE_KHZ / 1000, GARDE_RDY_US / 1000);
    printf("=================================================================\n\n");

    lien::init();

    if (!reseau::connecter()) {
        printf("  pas de reseau : arret.\n");
        while (true)
            tight_loop_contents();
    }
    printf("  ecoute sur  %s:%d\n\n", reseau::adresse_ip(), PXL2_PORT);
    reseau::annoncer();

    multicore_launch_core1(coeur1);

    constexpr uint32_t PERIODE_RAPPORT_MS = 10000;
    absolute_time_t prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
    reseau::Stats avant = reseau::stats();
    lien::Stats li_avant = lien::stats();
    Releve sy_avant = relever();

    while (true) {
        reseau::entretenir();
        reseau::entretenir_liaisons();

        uint16_t w, h;
        uint8_t f;
        if (reseau::geometrie_changee(w, h, f))
            printf("  nouvelle source : %ux%u %s — placee en (%d, %d)\n", w, h, nom_format(f),
                   (CANEVAS_W - w) / 2, (CANEVAS_H - h) / 2);

        if (const uint8_t basis = reseau::luminosite_demandee())
            printf("  luminosite demandee : %u, relayee aux noeuds\n", basis);

        if (time_reached(prochain_rapport)) {
            const reseau::Stats st = reseau::stats();
            const lien::Stats li = lien::stats();
            const Releve sy = relever();
            rapport(st, avant, li, li_avant, sy, sy_avant, PERIODE_RAPPORT_MS);
            avant = st;
            li_avant = li;
            sy_avant = sy;
            prochain_rapport = make_timeout_time_ms(PERIODE_RAPPORT_MS);
        }
    }
}
