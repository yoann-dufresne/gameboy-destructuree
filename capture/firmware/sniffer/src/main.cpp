/**
 * Module CAPTURE — recette de la phase 2
 *
 * Pas de réseau : on prouve par l'image. Un vidage ASCII qui ne demande aucun
 * outil, un vidage hexadécimal pour en faire un PNG, et les compteurs qui
 * disent si la chaîne est saine.
 *
 * Contrairement au module écran — dont le compteur de trames annonçait 788 Hz
 * parfaitement stables pendant que la dalle était noire — ici l'instrument ne
 * peut pas mentir : si l'image est reconnaissable, la chaîne est juste.
 */
#include <cstdio>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "capture.hpp"
#include "config.h"

namespace {

/* Le pixel `x` de la ligne `y`, sur 2 bits.
 * Disposition IDX2 : pixel de gauche dans les bits de poids fort. */
inline uint8_t pixel(const uint8_t *t, int x, int y) {
    const uint8_t octet = t[y * OCTETS_LIGNE + x / PX_PAR_OCTET];
    return (octet >> (6 - 2 * (x & 3))) & 0x3;
}

/* 🔬 Polarité mesurée en phase 0 : `00` = blanc. L'indice le plus faible est
 * donc le plus clair, et cet ordre tombe juste. */
const char NIVEAUX[4] = {' ', '.', ':', '#'};

void vidage_ascii(const uint8_t *t) {
    printf("\n--- trame %u, %d x %d (1 pixel sur %d) ---\n",
           capture::numero_trame(), GB_L / DECIMATION, GB_H / DECIMATION,
           DECIMATION);
    for (int y = 0; y < GB_H; y += DECIMATION) {
        for (int x = 0; x < GB_L; x += DECIMATION)
            putchar(NIVEAUX[pixel(t, x, y)]);
        putchar('\n');
    }
    printf("--- fin ---\n");
}

/* Vidage hexadécimal, consommé par tools/gbdump.py pour produire un PNG.
 * Encadré par des marqueurs pour que l'outil se cale sans ambiguïté. */
void vidage_hex(const uint8_t *t) {
    printf("\n>>>GBDUMP %d %d %d %u\n", GB_L, GB_H, BITS_PAR_PX,
           capture::numero_trame());
    for (int i = 0; i < OCTETS_TRAME; ++i) {
        printf("%02x", t[i]);
        if ((i % 40) == 39)
            putchar('\n');   /* une ligne de texte = une ligne d'image */
    }
    printf("<<<GBDUMP\n");
}

void afficher_stats() {
    const capture::Stats &s = capture::stats();
    printf("\n  cadence          %6.3f img/s   (attendu 59,727)  sur %.0f s\n",
           (double)capture::cadence(), (double)capture::duree_observation());
    printf("  trames           %6lu\n", (unsigned long)s.trames);
    printf("  lignes/trame     %6lu        (attendu %d)\n",
           (unsigned long)s.lignes_derniere, LIGNES_VISIBLES);
    printf("  trames douteuses %6lu %s\n", (unsigned long)s.trames_douteuses,
           s.trames_douteuses ? "  <-- lignes != 144" : "");
    printf("  mots restants    %6lu %s\n", (unsigned long)s.mots_restants,
           s.mots_restants ? "  <-- fronts d'horloge pixel manquants" : "");
    printf("  debordements FIFO%6lu %s\n", (unsigned long)s.debordements,
           s.debordements ? "  <-- le DMA ne suit pas" : "");
    printf("  trames perdues   %6lu        (non lues par la boucle)\n",
           (unsigned long)s.perdues);
}

/* Lit l'état brut des 6 entrées pendant 100 ms. Répond à la question « le fil
 * est-il branché ? » sans rien supposer du PIO ni du DMA — c'est le premier
 * diagnostic à lancer quand les compteurs restent à zéro. */
void diagnostic_broches() {
    struct { uint8_t gpio; const char *nom; } E[] = {
        {PIN_LD0,      "GP0  P2-LD0   donnee 0"},
        {PIN_LD1,      "GP1  P2-LD1   donnee 1"},
        {PIN_PIXCLK,   "GP2  CP       HORLOGE PIXEL"},
        {PIN_LIGNE,    "GP3  P2-ST    ligne visible"},
        {PIN_VSYNC,    "GP4  P2-S     vsync"},
        {PIN_LIGNE154, "GP5  P2-CPL   reserve"},
    };
    const int N = 6;
    uint32_t haut[N] = {0}, transitions[N] = {0}, total = 0;
    uint32_t masque = 0;
    for (int i = 0; i < N; ++i) masque |= 1u << E[i].gpio;

    uint32_t prec = gpio_get_all() & masque;
    const uint64_t fin = time_us_64() + 100000;   /* 100 ms = 6 trames */
    while (time_us_64() < fin) {
        const uint32_t v = gpio_get_all() & masque;
        const uint32_t chg = v ^ prec;
        for (int i = 0; i < N; ++i) {
            const uint32_t bit = 1u << E[i].gpio;
            if (v & bit) haut[i]++;
            if (chg & bit) transitions[i]++;
        }
        prec = v;
        total++;
    }

    printf("\n--- etat des entrees, %lu lectures sur 100 ms ---\n",
           (unsigned long)total);
    for (int i = 0; i < N; ++i) {
        const float pct = total ? 100.0f * (float)haut[i] / (float)total : 0.0f;
        const char *verdict;
        if (transitions[i] == 0)
            verdict = (haut[i] == 0) ? "<-- FIGE A 0 : fil non branche ?"
                                     : "<-- FIGE A 1";
        else
            verdict = "actif";
        printf("  %-26s niveau %5.1f %%  %7lu transitions  %s\n",
               E[i].nom, (double)pct, (unsigned long)transitions[i], verdict);
    }
    printf("  (l'horloge pixel est sous-echantillonnee par cette boucle :\n"
           "   on cherche « ca bouge », pas un comptage exact)\n");
}

void aide() {
    printf("\n  a = vidage ASCII    p = vidage PNG (hex)    s = compteurs\n"
           "  g = etat brut des 6 entrees (le fil est-il branche ?)\n"
           "  r = remise a zero des compteurs\n"
           "  h = cette aide\n\n");
}

} // namespace

int main() {
    stdio_init_all();
    set_sys_clock_khz(CLK_SYS_KHZ, true);

    /* Laisser le temps à la console USB de s'ouvrir. */
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n=== Module CAPTURE — phase 2 ===\n");
    printf("  source %d x %d, %d bpp, %d octets par trame\n",
           GB_L, GB_H, BITS_PAR_PX, OCTETS_TRAME);
    printf("  horloge pixel sur GP%d, ligne GP%d, vsync GP%d\n",
           PIN_PIXCLK, PIN_LIGNE, PIN_VSYNC);
    printf("  echantillonnage sur front DESCENDANT, delai %d\n",
           DELAI_ECHANTILLON);

    capture::init();
    aide();

    const uint8_t *derniere = nullptr;
    uint64_t t_stats = time_us_64();

    while (true) {
        const uint8_t *t = capture::trame_prete();
        if (t != nullptr)
            derniere = t;

        const int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (derniere == nullptr && (c == 'a' || c == 'p'))
                printf("\n  aucune trame capturee pour l'instant\n");
            else if (c == 'a')
                vidage_ascii(derniere);
            else if (c == 'p')
                vidage_hex(derniere);
            else if (c == 's')
                afficher_stats();
            else if (c == 'g')
                diagnostic_broches();
            else if (c == 'r') {
                capture::reinitialiser();
                printf("\n  compteurs remis a zero — si une erreur reapparait\n"
                       "  maintenant, ce n'est PAS un transitoire de demarrage\n");
            }
            else if (c == 'h')
                aide();
        }

        /* Relevé périodique, pour voir la chaîne vivre sans rien taper. */
        if (time_us_64() - t_stats >= 5000000ull) {
            t_stats = time_us_64();
            afficher_stats();
        }
    }
}
