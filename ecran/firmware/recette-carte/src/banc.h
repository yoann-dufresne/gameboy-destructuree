/**
 * Recette de la carte de l'écran — l'outillage commun à la tête et aux nœuds
 *
 *   - le contrôle des broches, au démarrage : courts-circuits, ponts de
 *     soudure, broches qui ne montent ou ne descendent plus, résistances de
 *     tirage présentes ou non ;
 *   - le motif de contrôle, sur le cœur 1 : il émet un code sur chaque sortie
 *     et mesure le code de chaque entrée.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carte.h"

/* ------------------------------------------------ contrôle des broches */
typedef struct {
    uint32_t essayees;       /* broches du contrôle */
    uint32_t pilotees;       /* un autre Pico les faisait basculer : non essayées */
    uint32_t ne_descend_pas; /* commandée à 0, relue à 1 */
    uint32_t ne_monte_pas;   /* commandée à 1, relue à 0 */
    uint32_t tiree_haut;     /* une résistance la tire vers le 3,3 V */
    uint32_t tiree_bas;      /* une résistance la tire vers la masse */
    uint32_t reliee[NB_GPIO]; /* pour chaque broche, celles qui lui sont reliées */
    int passes;
} bilan_t;

/* Plusieurs passes à intervalles aléatoires : un défaut n'est retenu que s'il
 * revient à au moins trois passes sur quatre. Un autre Pico qui fait son
 * propre contrôle en même temps ne laisse ainsi pas de faux défaut. */
void broches_eprouver(uint32_t masque, int passes, bilan_t *b);

/* Broches en court-circuit, reliées à une autre, ou qui ne suivent plus leur
 * commande : on ne les pilote pas. */
uint32_t broches_hs(const bilan_t *b);

/* Écrit les défauts, un par ligne ; rend leur nombre. */
int broches_afficher_defauts(const bilan_t *b, const char *const nom[NB_GPIO]);

/* Le même nombre, sans rien écrire. */
int broches_compter_defauts(const bilan_t *b);

/* Écrit « GPa GPb … » pour les broches du masque. */
void afficher_broches(uint32_t masque);

/* ------------------------------------------- motif, sur le cœur 1 */
/* Les sorties passent en 2 mA : de quoi piloter des entrées à 10 kHz, en
 * limitant le courant si une sortie rencontre un court-circuit. Les entrées
 * mesurées sont tirées vers le haut : en l'air, elles lisent un 1 constant. */
void motif_demarrer(uint32_t sorties, const uint8_t code[NB_GPIO], const uint8_t *mesures,
                    unsigned nb_mesures);

/* Change le code d'une sortie, sans arrêter le motif. */
void motif_code(unsigned gpio, uint8_t code);

/* Rapport cyclique de chaque entrée mesurée, en %, depuis le relevé
 * précédent. */
void motif_relever(float pourcent[]);

static inline bool proche(float mesure, float code, float tolerance) {
    return mesure > code - tolerance && mesure < code + tolerance;
}

/* ------------------------------------------------------ petits outils */
/* Tension sur une entrée analogique (0 = GP26 … 3 = GP29), moyenne de 64
 * lectures. adc_init() et adc_gpio_init() d'abord. */
float adc_volts(unsigned entree);

/* « 4,78 V » */
void afficher_volts(float v);
