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
 * centrée ; plus grande, elle est refusée.
 *
 * BANC_UNE_DALLE (option CMake) : le banc de la phase 5b, une liaison vers un
 * nœud qui pilote une seule dalle. Le canevas se réduit à cette dalle, si bien
 * que l'image entière y est visible. */
#if BANC_UNE_DALLE
#define CANEVAS_W    64
#define CANEVAS_H    64
#define NB_RANGEES    1
#else
#define CANEVAS_W   192
#define CANEVAS_H   192
#define NB_RANGEES    3   /* une rangée = un nœud = une liaison */
#endif
#define RANGEE_H   (CANEVAS_H / NB_RANGEES)

/* -------------------------------------------------------------- réseau */
/* L'écran émet son propre WiFi : les sources s'y connectent directement,
 * sans box entre elles et lui (décision du 29/09/2026). Nom et mot de passe
 * du réseau dans secrets.h (ECRAN_SSID, ECRAN_MOT_DE_PASSE) ; la tête est en
 * 192.168.4.1, et distribue aux sources des adresses de 192.168.4.16 à .23.
 *
 * Canal 2,4 GHz : 1, 6 ou 11 pour ne chevaucher qu'un minimum de voisins.
 * La box du banc émet sur le canal 4, qui déborde de 2 à 6 : 11 l'évite.
 *
 * WIFI_STATION (option CMake) : l'ancien mode, où la tête rejoint la box
 * (WIFI_SSID, WIFI_PASSWORD). Gardé pour comparer les deux. */
#ifndef WIFI_STATION
#define WIFI_STATION 0
#endif
#define ECRAN_CANAL_WIFI 11

/* Formats relayés vers les nœuds : ceux qu'un nœud sait développer. RGB565 et
 * IDX4 se découpent déjà, ils s'ajouteront quand le nœud saura les lire. */
#define FORMATS_ACCEPTES ((1u << 0) /* BGR888 */ | \
                          (1u << 2) /* IDX2   */ | \
                          (1u << 4) /* IDX8   */)

/* ----------------------------------------------- liaisons (plan §2.6) */
/* Par liaison : D0, D1, CLK, CS sur quatre GPIO contigus — `out pins, 2` pour
 * les données, CLK et CS en side-set. */
#define PIN_LIEN0_BASE   0  /* GP0..GP3  */
#define PIN_LIEN1_BASE   4  /* GP4..GP7  */
#define PIN_LIEN2_BASE   8  /* GP8..GP11 */
#define PIN_VSYNC       12  /* une sortie vers les trois nappes */
#define PIN_RDY_BASE    13  /* GP13..GP15, entrées ; PULL-UP ⚠️ errata E9 */

/* Horloge de la liaison. 2 bits par coup : 16 MHz donnent 32 Mbit/s, au-dessus
 * de ce que la tête reçoit par WiFi. Pour une première mise en route, la
 * baisser par l'option CMake du même nom (-DLIEN_HORLOGE_KHZ=1000). */
#ifndef LIEN_HORLOGE_KHZ
#define LIEN_HORLOGE_KHZ 16000
#endif

/* Au-delà, un nœud qui n'a pas levé RDY fait abandonner l'image aux trois :
 * mieux vaut sauter une image que déchirer l'écran. Une construction de plans
 * de bits prend jusqu'à ~17 ms pour une rangée de 192×64 (plan §2.2 bis). */
#define GARDE_RDY_US 40000

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
