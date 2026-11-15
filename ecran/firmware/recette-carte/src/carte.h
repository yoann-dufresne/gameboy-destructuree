/**
 * Recette de la carte de l'écran — ce que la carte relie, et les codes émis
 *
 * Tiré de materiel/carte/conception.py, la description électrique de la
 * carte, et des config.h de la tête et des nœuds, qu'elle reprend sans
 * changement.
 *
 * Chaque Pico émet sur ses sorties un motif de 100 pas de 1 µs (10 kHz). Le
 * « code » d'une sortie est son nombre de pas à 1, donc son rapport cyclique
 * en %. Un multimètre en continu en donne la moyenne : code × 3,3 V sur une
 * sortie de Pico, code × 5 V après les tampons 74AHCT541. Un Pico qui reçoit
 * la ligne mesure le même code. Chaque fil porte ainsi sa signature : un fil
 * coupé n'en porte plus, deux fils croisés échangent la leur, deux fils en
 * court-circuit en montrent un mélange.
 */
#pragma once

#include <stdint.h>

#define NB_GPIO 30
#define PAS_PAR_PERIODE 100

/* Patte du Pico de chaque GPIO ; 0 : pas sur les barrettes. */
extern const uint8_t PATTE[NB_GPIO];

/* ------------------------------------------------------------- liaisons */
/* Émis par la tête sur GP0-GP11, mesurés par les nœuds sur GP19-GP22 :
 * code = 10 + 20 × liaison + 5 × signal. L0 : 10 15 20 25 ; L1 : 30 35 40
 * 45 ; L2 : 50 55 60 65. */
#define SIG_D0 0
#define SIG_D1 1
#define SIG_CLK 2
#define SIG_CS 3
#define CODE_LIEN(k, s) (10 + 20 * (k) + 5 * (s))
#define CODE_VSYNC 80 /* GP12 de la tête, GP16 des nœuds */
#define TOLERANCE_LIEN 2.0f

/* RDY, émis par un nœud sur GP14 et mesuré par la tête sur GP13-GP15 : le
 * nœud y dit ce qu'il a reconnu de la liaison qui lui arrive. */
#define CODE_RDY_OK(k) (20 + 25 * (k))     /* 20 45 70 : liaison k complète */
#define CODE_RDY_DEFAUT(k) (30 + 25 * (k)) /* 30 55 80 : liaison k, un signal faux */
#define CODE_RDY_SANS_TETE 90              /* aucun code de liaison reconnu */
#define TOLERANCE_RDY 3.0f

/* Sonde J7 : GP17 et GP18 de chaque Pico. */
#define CODE_GP17 30 /* ≈ 1,0 V */
#define CODE_GP18 60 /* ≈ 2,0 V */

/* Nappe HUB75 d'un nœud : 6 % par numéro de broche, soit 0,3 V par numéro
 * sous 5,0 V après les tampons. GP15, le G1 de rechange, ne rejoint la broche
 * 2 que par le cavalier JP en 2-3. */
#define CODE_HUB75(broche) (6 * (broche))
#define CODE_G1_RECHANGE 96 /* 4,8 V sur la broche 2, cavalier en 2-3 */
