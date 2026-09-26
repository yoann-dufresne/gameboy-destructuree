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
    printf("\n  cadence          %6.2f img/s   (attendu 59,73)\n",
           (double)capture::cadence());
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

void aide() {
    printf("\n  a = vidage ASCII    p = vidage PNG (hex)    s = compteurs\n"
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
            else if (c == 'h')
                aide();
        }

        /* Relevé périodique, pour voir la chaîne vivre sans rien taper. */
        if (time_us_64() - t_stats >= 5000000ull) {
            t_stats = time_us_64();
            afficher_stats();
        }
        capture::cadence();   /* entretient la fenêtre de mesure */
    }
}
