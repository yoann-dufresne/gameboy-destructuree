/**
 * Module CAPTURE — configuration
 *
 * Tout ce qui vient d'une mesure est ici, avec la mesure qui le justifie.
 * Le reste du firmware ne doit contenir aucune constante de brochage ni de
 * géométrie.
 *
 * Les valeurs 🔬 ont été relevées en phase 0 sur une carte MGB-ECPU-01, les
 * 25 et 26/09/2026. Voir docs/signaux-mgb.md.
 */
#pragma once

/* ─────────────────────────────────────────── brochage (signaux-mgb.md §3bis)
 *
 * ⚠️ Les noms sérigraphiés sur la carte ne sont PAS ceux de la spec du projet :
 * la spec s'est révélée fausse sur 5 signaux sur 6. Ce tableau est celui de la
 * carte réelle, établi par la mesure.
 *
 * GP0, GP1, GP2 doivent rester CONTIGUS : le PIO échantillonne un groupe
 * contigu à partir de IN_BASE, et repère l'horloge par `wait ... pin 2`. */
#define PIN_LD0        0  /* test point « P2-LD0 »  — donnée, bit 0          */
#define PIN_LD1        1  /* test point « P2-LD1 »  — donnée, bit 1          */
#define PIN_PIXCLK     2  /* test point « CP »      — HORLOGE PIXEL          */
#define PIN_LIGNE      3  /* test point « P2-ST »   — verrou, 144/trame      */
#define PIN_VSYNC      4  /* test point « P2-S »    — départ de trame        */
#define PIN_LIGNE154   5  /* test point « P2-CPL »  — réserve, 154/trame     */

#define PIN_IN_BASE    PIN_LD0
#define PIN_IN_N       3  /* LD0, LD1, horloge pixel : lus par le PIO */

/* Sorties de mesure, à mettre sur analyseur logique (phase 4). */
#define PIN_MESURE_VSYNC  20  /* bascule à chaque trame capturée */
#define PIN_MESURE_EMIS   21  /* bascule à chaque trame émise (phase 3) */

/* ⚠️ L'UART par défaut du SDK est sur GP0/GP1, occupés ici par les données.
 * La console passe donc par l'USB — voir CMakeLists.txt. */

/* ───────────────────────────────────────────────── géométrie de la SOURCE
 *
 * Le firmware ne sait RIEN de l'afficheur : il capture et émet la trame Game
 * Boy native. Recadrage, centrage et répartition appartiennent au module
 * ÉCRAN (plan §5.3). */
#define GB_L          160
#define GB_H          144
#define BITS_PAR_PX     2                       /* IDX2 */
#define PX_PAR_OCTET    (8 / BITS_PAR_PX)       /* 4 */

#define OCTETS_LIGNE  (GB_L / PX_PAR_OCTET)     /* 40  */
#define OCTETS_TRAME  (OCTETS_LIGNE * GB_H)     /* 5760 */
#define MOTS_TRAME    (OCTETS_TRAME / 4)        /* 1440 — un seul transfert DMA */

/* 🔬 Le PIO pousse un mot tous les 16 pixels (autopush 32 bits / 2 bits). */
#define PX_PAR_MOT      16
#define MOTS_LIGNE      (GB_L / PX_PAR_MOT)     /* 10 */

/* ───────────────────────────────────── timing de la console (mesuré phase 0)
 *
 * Tout découle du quartz X1 = 4,194304 MHz, visible sur la carte. */
#define F_MAITRE_HZ   4194304u
#define CYCLES_LIGNE      456u
#define LIGNES_TRAME      154u
#define LIGNES_VISIBLES   144u                  /* 🔬 confirmé : P2-ST bat 144 fois */

/* 🔬 Période minimale entre deux fronts de l'horloge pixel : 208 ns mesurés
 * (valeur vraie 238 ns = un cycle maître, quantifiée à 5 échantillons de
 * 41,7 ns). La boucle PIO fait 3 instructions, soit 20 ns à 150 MHz :
 * marge de 12×, confirmée sur matériel. */
#define PERIODE_PIXEL_MIN_NS  238

/* 🔬 `LD` change sur le front MONTANT de l'horloge pixel : 209 des 256
 * transitions mesurées tombent à +0 échantillon, 47 à +1, aucune au-delà.
 * Le temps haut de l'horloge est de 125 ns.
 *
 * ⇒ on échantillonne après le front DESCENDANT, au milieu de la fenêtre
 *    stable : 75 ns de marge avant, 113 ns après. Le programme PIO inverse donc
 *    ses deux `wait` par rapport à ce qu'envisageait le plan.
 *
 * 🔬 Mais pas pile dessus. Le 09/10/2026, lu au plus tôt (délai 0), environ un
 * pixel par seconde d'image prenait la valeur 3 en pleine zone uniforme : un
 * bit brièvement à 1, un parasite du front. Mesuré en alternance sur une même
 * scène : délai 0, 8 pixels faux sur 594 images ; 5, 9 et 13, aucun ; 17 en
 * bordure, et à partir de 19, le pixel suivant. 9 cycles, 60 ns, au milieu.
 * Réglable à chaud par les commandes « [ » et « ] ». */
#define DELAI_ECHANTILLON  9   /* cycles PIO après le front descendant, 0..31 */

/* 🔬 Délai de lecture du PREMIER pixel de chaque ligne, en cycles PIO (6,67 ns
 * à 150 MHz) après son front MONTANT, 0..31. Sa donnée n'est valide que
 * pendant que l'horloge est haute : elle passe à celle du pixel suivant sur le
 * front descendant (capture.pio). Balayé le 09/10/2026 sur une scène connue :
 * bonne valeur de 1 à 17 cycles, pixel suivant à partir de 18. 9 en est le
 * milieu. Réglable à chaud par les commandes « < » et « > ». */
#define DELAI_PREMIER_PIXEL  9

/* ─────────────────────────────────────────────────────────────── horloge
 *
 * 150 MHz, la valeur par défaut. Inutile de monter : la marge PIO est déjà de
 * 12×, et monter clk_sys obligerait à rediviser l'horloge SPI du CYW43 — ce
 * qui a coûté une journée au module écran. */
#define CLK_SYS_KHZ   150000

/* ─────────────────────────────────────────────────────── découpage en tranches
 *
 * Une tranche = un paquet UDP. 1400 octets sous la MTU, soit exactement
 * 35 lignes de 40 octets — le découpage tombe donc sur une frontière de ligne,
 * ce qui permet à l'interruption de ligne de savoir quand une tranche est
 * complète. */
#define OCTETS_TRANCHE      1400
#define LIGNES_PAR_TRANCHE  (OCTETS_TRANCHE / OCTETS_LIGNE)    /* 35 */
#define TRANCHES_PAR_TRAME  ((OCTETS_TRAME + OCTETS_TRANCHE - 1) / OCTETS_TRANCHE)

/* Émission pipelinée par défaut : une tranche part dès que ses 35 lignes sont
 * capturées, au lieu d'attendre la fin de la trame.
 *
 * 🔬 Mesuré le 26/09/2026, les deux modes dos à dos sur la même console :
 * latence moyenne 13,1 ms → 4,1 ms, aller-retour 4,50 → 3,53 ms, et son
 * maximum 84,79 → 46,52 ms. Zéro trame douteuse, zéro tranche perdue dans les
 * deux cas. Commutable à chaud par la commande « P ». */
#define PIPELINE_PAR_DEFAUT  1

/* ────────────────────────────────────────────────────────────────── réseau
 *
 * Un émetteur, un récepteur. Le sniffeur ne sait rien de l'afficheur : il
 * émet la trame native vers une adresse, point.
 *
 * Depuis le 29/09/2026, le module écran émet son propre réseau WiFi, et le
 * sniffeur s'y connecte en direct, sans box entre eux : la box faisait geler
 * l'image jusqu'à 120 ms. Son nom et son mot de passe vont dans secrets.h ;
 * la tête de l'écran est toujours en 192.168.4.1.
 *
 * Pour travailler avec l'écran virtuel (`tools/ecran_virtuel.py`) : secrets.h
 * vers le réseau du PC, et ici l'adresse du PC. */
#define PXL1_CIBLE_IP    "192.168.4.1"    /* ← la tête du module écran */
#define PXL1_NODE_ID     0                /* un seul récepteur */

/* Palette envoyée en CTRL, en B,G,R — les 4 teintes de la Game Boy.
 * 🔬 Polarité mesurée en phase 0 : l'indice 0 est le plus CLAIR.
 * Teintes DMG d'origine. */
#define PALETTE_BGR { {0x0F,0xBC,0x9B}, {0x0F,0xAC,0x8B}, \
                      {0x30,0x62,0x30}, {0x0F,0x38,0x0F} }

/* Palette et géométrie sont renvoyées à cette cadence, pour qu'un récepteur
 * redémarré retrouve seul de quoi interpréter le flux. Comportement déjà en
 * place sur le module écran. */
#define PERIODE_CTRL_MS  2000

/* ────────────────────────────────────────────────────────────── diagnostic */
/* Vidage ASCII : un pixel sur DECIMATION dans chaque direction.
 * 160/2 = 80 colonnes, ce qui entre dans un terminal. */
#define DECIMATION     2
