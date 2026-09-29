/**
 * Tête — configuration matérielle et géométrie
 *
 * La tête reçoit par WiFi, place, découpe et relaie vers les nœuds d'affichage
 * (plan §2.2 bis). Elle ne porte aucune dalle. Tout ce qui la décrit est ici ;
 * le reste du firmware ne contient aucune constante de brochage ni de dimension.
 */
#pragma once

/* ------------------------------------------------------------- canevas */
/* Ce que l'écran annonce au monde dans son PONG. Une source plus petite est
 * centrée ; plus grande, elle est refusée. */
#define CANEVAS_W   192
#define CANEVAS_H   192
#define NB_RANGEES    3   /* une rangée = un nœud = une liaison */
#define RANGEE_H   (CANEVAS_H / NB_RANGEES)

/* Formats relayés vers les nœuds : ceux qu'un nœud sait développer. RGB565 et
 * IDX4 se découpent déjà, ils s'ajouteront quand le nœud saura les lire. */
#define FORMATS_ACCEPTES ((1u << 0) /* BGR888 */ | \
                          (1u << 2) /* IDX2   */ | \
                          (1u << 4) /* IDX8   */)

/* ----------------------------------------------- liaisons (plan §2.6) */
/* Phase 5b. Par liaison : D0, D1, CLK, CS sur quatre GPIO contigus —
 * `out pins, 2` pour les données, CLK et CS en side-set. */
#define PIN_LIEN0_BASE   0  /* GP0..GP3  */
#define PIN_LIEN1_BASE   4  /* GP4..GP7  */
#define PIN_LIEN2_BASE   8  /* GP8..GP11 */
#define PIN_VSYNC       12  /* une sortie vers les trois nappes */
#define PIN_RDY_BASE    13  /* GP13..GP15, entrées ; PULL-UP ⚠️ errata E9 */

/* ------------------------------------------------ mesure (phase 4, 5) */
#define PIN_MESURE_IMAGE 17 /* bascule à chaque image complète */
#define PIN_MESURE_RX    18 /* bascule au premier octet d'une image */

/* ------------------------------------------------------------ horloge */
/* 266 MHz : la configuration où les 24,6 Mbit/s ont été mesurés en phase 4.
 * La tête n'a pas d'horloge pixel à tenir ; descendre à 150 MHz ne se fera
 * qu'une fois son débit établi (plan §5, phase 5a).
 * ⚠️ Si cette valeur change, ajuster CYW43_PIO_CLOCK_DIV_INT dans
 * CMakeLists.txt : garder clk_sys / DIV autour de 70 MHz. */
#define CLK_SYS_KHZ 266000

static_assert(CANEVAS_H % NB_RANGEES == 0, "rangées de hauteur inégale");
static_assert(CANEVAS_W * RANGEE_H <= 65536, "position dans une rangée hors de 16 bits");
