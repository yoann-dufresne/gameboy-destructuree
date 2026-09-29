/**
 * Nœud (v2) — configuration matérielle et géométrie
 *
 * Un nœud reçoit sa rangée de la tête par la liaison filaire et la publie sur
 * sa chaîne de dalles au VSYNC (plan §2.2 bis, §2.6). Pas de WiFi, pas
 * d'identité propre : son numéro vient du HELLO de la tête.
 */
#pragma once

/* ---------------------------------------------------- dalle (plan §2.3) */
/* Identique au nœud v1. Ne pas réordonner : le PIO exige des groupes de
 * broches CONTIGUS (`out pins, 6` et `out pins, 5`). */
#define PIN_DATA_BASE    0  /* GP0..GP5  : R1 G1 B1 R2 G2 B2 */
#define PIN_DATA_N       6
#define PIN_ROWSEL_BASE  6  /* GP6..GP10 : A B C D E         */
#define PIN_ROWSEL_N     5  /* 5 lignes d'adresse = scan 1/32 */
#define PIN_CLK         11
#define PIN_LAT         12
#define PIN_OE          13  /* actif bas */

/* ------------------------------------------------ liaison (plan §2.6) */
/* D0, D1, CLK, CS sur quatre GPIO contigus : `in pins, 2`, et CLK et CS lus
 * par `wait pin` relativement à la même base. */
#define PIN_LIEN_BASE   19  /* GP19..GP22 */
#define PIN_VSYNC       16  /* entrée, active basse, pull-up : l'ancien fil de synchro */
#define PIN_RDY         14  /* sortie vers la tête — l'ancien strap d'identité */

/* Sorties de mesure : à mettre sur analyseur logique, avec GP18 de la tête. */
#define PIN_MESURE_FLIP 17  /* bascule à chaque publication (display.cpp) */
#define PIN_MESURE_RX   18  /* bascule à chaque VALIDER reçu */

/* ------------------------------------------------------------- géométrie */
#define PANEL_W      64
#define PANEL_H      64
#define CHAIN_LEN     1  /* dalles par chaîne — 1 au banc de la phase 5b, 3 en 5c */

#define DISPLAY_W  (PANEL_W * CHAIN_LEN)
#define DISPLAY_H   PANEL_H
#define FB_OCTETS  (DISPLAY_W * DISPLAY_H * 3) /* BGR, 3 octets par pixel */

/* ---------------------------------------------------------------- rendu */
/* Repris du nœud v1, mesuré en phase 1 : 788 Hz en 10 plans sur une dalle. */
#define BCM_PLANES         10
#define CIE_SEPARATE        1
#define BASIS_BRIGHTNESS    6
#define CLK_SYS_KHZ    266000   /* horloge pixel = clk_sys / 9 = 29,6 MHz */
#define DEBUG_FRAME_RATE    0

/* Sans message de la tête pendant ce délai, le nœud affiche sa mire
 * « pas de liaison ». La tête envoie un HELLO par seconde. */
#define SILENCE_MS 2500
