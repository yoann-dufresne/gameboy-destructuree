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
#include "net/reseau.hpp"
#include "pxl1.h"

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

/* min / moyenne / max sur N échantillons. Le nombre d'échantillons est
 * affiché : une moyenne sans son effectif ne veut rien dire. */
void ligne_mesure(const char *nom, const reseau::Mesure &m) {
    if (!m.n) {
        printf("  %s      —\n", nom);
        return;
    }
    printf("  %s  min %6.2f  moy %6.2f  max %7.2f ms  sur %lu\n", nom,
           m.min_us / 1000.0, (double)m.somme_us / m.n / 1000.0,
           m.max_us / 1000.0, (unsigned long)m.n);
}

void afficher_reseau() {
    const reseau::Stats &r = reseau::stats();
    if (!reseau::pret()) {
        printf("\n  reseau           NON ASSOCIE (%s) — la capture continue,\n"
               "                   les vidages par la console restent utilisables\n",
               reseau::etat_lien());
        return;
    }
    printf("\n  reseau           %s  ->  %s:%d\n",
           reseau::adresse_ip(), PXL1_CIBLE_IP, PXL1_PORT);
    printf("  trames emises    %6lu\n", (unsigned long)r.trames);
    printf("  paquets          %6lu        (%d par trame)\n",
           (unsigned long)r.paquets,
           (OCTETS_TRAME + PXL1_CHARGE_MAX - 1) / PXL1_CHARGE_MAX);
    printf("  echecs d'envoi   %6lu %s\n", (unsigned long)r.echecs,
           r.echecs ? "  <-- file lwIP pleine ?" : "");
    printf("  commandes        %6lu        (palette + geometrie)\n",
           (unsigned long)r.ctrl);
    printf("  lien             %s\n", reseau::etat_lien());
    if (r.deconnexions || r.reconnexions)
        printf("  deconnexions     %6lu   reconnexions %lu\n",
               (unsigned long)r.deconnexions, (unsigned long)r.reconnexions);
    printf("  accuses recus    %6lu        (%.0f %% des trames)\n",
           (unsigned long)r.accuses,
           r.trames ? 100.0 * r.accuses / r.trames : 0.0);
    ligne_mesure("attente  capture->envoi", r.attente);
    ligne_mesure("envoi    5 paquets      ", r.emission);
    ligne_mesure("aller-retour  envoi->acc", r.aller_retour);
    if (r.aller_retour_ecartes)
        printf("  mesures ecartees %6lu        (horodatage recycle)\n",
               (unsigned long)r.aller_retour_ecartes);
}

/* Un maximum ne dit pas s'il est une valeur isolée ou une queue de
 * distribution. L'histogramme, si. */
void afficher_histogramme() {
    const reseau::Stats &r = reseau::stats();
    uint32_t sommet = 0, total = 0;
    for (int i = 0; i < RESEAU_HISTO_SEAUX; ++i) {
        total += r.histo[i];
        if (r.histo[i] > sommet)
            sommet = r.histo[i];
    }
    if (!total) {
        printf("\n  aucune mesure d'aller-retour\n");
        return;
    }
    printf("\n  histogramme de l'aller-retour, %lu mesures\n",
           (unsigned long)total);
    for (int i = 0; i < RESEAU_HISTO_SEAUX; ++i) {
        if (!r.histo[i])
            continue;
        char barre[41];
        const int n = (int)((uint64_t)r.histo[i] * 40 / sommet);
        for (int k = 0; k < n; ++k)
            barre[k] = '#';
        barre[n] = 0;
        if (i == RESEAU_HISTO_SEAUX - 1)
            printf("   >=%2d ms |%-40s %6lu  %5.2f %%\n", i, barre,
                   (unsigned long)r.histo[i], 100.0 * r.histo[i] / total);
        else
            printf("     %2d ms |%-40s %6lu  %5.2f %%\n", i, barre,
                   (unsigned long)r.histo[i], 100.0 * r.histo[i] / total);
    }
}

void aide() {
    printf("\n  a = vidage ASCII    p = vidage PNG (hex)    s = compteurs\n"
           "  g = etat brut des 6 entrees (le fil est-il branche ?)\n"
           "  r = remise a zero des compteurs\n"
           "  n = etat du reseau\n"
           "  l = histogramme de la latence\n"
           "  d = rompre l'association (essai de reconnexion)\n"
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

    /* ⚠️ Porte de sortie. Le 26/09/2026, une initialisation réseau bloquante a
     * emporté l'USB CDC avec elle : plus de console, plus de bascule 1200
     * bauds, et il a fallu un BOOTSEL physique pour reprendre la main.
     *
     * Une touche pendant ces 3 secondes démarre SANS réseau. La capture, elle,
     * fonctionne toujours — donc on garde les vidages et les compteurs, et on
     * garde surtout de quoi reflasher. */
    printf("\n  [ une touche dans les 3 s = demarrer SANS reseau ]\n");
    bool sans_reseau = false;
    for (int i = 0; i < 30 && !sans_reseau; ++i) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
            sans_reseau = true;
        else
            sleep_ms(100);
    }

    if (sans_reseau)
        printf("  reseau VOLONTAIREMENT desactive — capture seule\n");
    else
        reseau::init();

    aide();

    const uint8_t *derniere = nullptr;
    uint64_t t_stats = time_us_64();

    while (true) {
        /* L'émission consomme la trame prête ; on garde une copie du
         * pointeur pour les vidages de la console. */
        const uint8_t *t = capture::trame_prete();
        if (t != nullptr) {
            derniere = t;
            reseau::emettre(t, capture::numero_trame(),
                            capture::horodatage_trame());
        }
        reseau::servir();

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
            else if (c == 'n')
                afficher_reseau();
            else if (c == 'l')
                afficher_histogramme();
            else if (c == 'd')
                reseau::rompre_pour_essai();
            else if (c == 'g')
                diagnostic_broches();
            else if (c == 'r') {
                capture::reinitialiser();
                reseau::reinitialiser();
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
            afficher_reseau();
        }
    }
}
