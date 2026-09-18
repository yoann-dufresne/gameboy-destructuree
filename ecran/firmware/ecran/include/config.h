/**
 * Module ÉCRAN — configuration matérielle et géométrie
 *
 * Tout ce qui change entre la phase 1 (une dalle) et la phase 5 (3 chaînes de 3)
 * est ici. Le reste du firmware ne doit contenir aucune constante de brochage ni
 * de dimension.
 */
#pragma once

/* ---------------------------------------------------- brochage (plan §2.3) */
/* Identique à pico-examples/pio/hub75. Ne pas réordonner : le PIO exige des
 * groupes de broches CONTIGUS (`out pins, 6` et `out pins, 5`). */
#define PIN_DATA_BASE    0  /* GP0..GP5  : R1 G1 B1 R2 G2 B2 */
#define PIN_DATA_N       6
#define PIN_ROWSEL_BASE  6  /* GP6..GP10 : A B C D E         */
#define PIN_ROWSEL_N     5  /* 5 lignes d'adresse = scan 1/32 */
#define PIN_CLK         11
#define PIN_LAT         12
#define PIN_OE          13  /* actif bas */

/* Straps d'identité de nœud, lus au démarrage.
 * ⚠️ Errata RP2350-E9 : câbler en PULL-UP, strap vers la masse. */
#define PIN_NODE_ID_0   14
#define PIN_NODE_ID_1   15

/* Sorties de mesure (phase 4) : à mettre sur analyseur logique. */
#define PIN_MESURE_FLIP 17  /* bascule à chaque trame publiée */
#define PIN_MESURE_RX   18  /* bascule à la réception du 1er octet (phase 2) */

/* ------------------------------------------------------------- géométrie */
#define PANEL_W      64
#define PANEL_H      64
#define CHAIN_LEN     1  /* dalles par chaîne — 3 en phase 5 */
#define NODE_COUNT    1  /* nœuds sur la grille — 3 en phase 5 */

#define DISPLAY_W  (PANEL_W * CHAIN_LEN)
#define DISPLAY_H   PANEL_H
#define FB_OCTETS  (DISPLAY_W * DISPLAY_H * 3) /* BGR, 3 octets par pixel */

/* ---------------------------------------------------------------- rendu */
#define BCM_PLANES         10   /* 8 ou 10 */
#define CIE_SEPARATE        1   /* canaux CIE séparés : meilleur rendu, plus de RAM */
#define BASIS_BRIGHTNESS    6   /* compromis luminosité / rafraîchissement */
/* ⚠️ Si cette valeur change, ajuster CYW43_PIO_CLOCK_DIV_INT dans CMakeLists.txt :
 * la liaison SPI de la puce WiFi en derive, et elle decroche si elle part trop
 * vite. Regle : garder clk_sys / DIV autour de 70 MHz. */
#define CLK_SYS_KHZ    266000   /* horloge pixel = clk_sys / 9 = 29,6 MHz */

/* Le pilote annonce son rafraîchissement sur la console — depuis une
 * interruption, donc coûteux et générateur de gigue. À n'activer que pour un
 * diagnostic, jamais en régime nominal ni en phase 2.
 *
 * ⚠️ Et il ne prouve rien sur le contenu : il a annoncé 788 Hz parfaitement
 * stables pendant que l'écran était noir, puis pendant qu'il clignotait. */
#define DEBUG_FRAME_RATE    0
