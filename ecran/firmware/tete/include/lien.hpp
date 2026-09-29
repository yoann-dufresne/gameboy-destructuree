/**
 * Tête — les liaisons vers les nœuds (plan §2.6, §4.6)
 *
 * Une liaison par rangée : un programme PIO, un canal DMA, un anneau de mots.
 * Les messages y sont écrits par la réception et l'entretien (cœur 0) et par
 * la synchronisation (cœur 1) ; un verrou matériel rend chaque ajout atomique.
 * Le DMA vide l'anneau vers le PIO sans le CPU ; une interruption de fin de
 * transfert relance la suite.
 */
#pragma once

#include <cstdint>

#include "config.h"
#include "liaison.h"

namespace lien {

struct Stats {
    uint32_t messages[NB_RANGEES];
    uint64_t octets[NB_RANGEES];     /* sur la nappe, en-têtes compris */
    uint32_t debordements[NB_RANGEES]; /* message refusé : anneau plein */
};

/* PIO, DMA, VSYNC en sortie, RDY en entrée. */
void init();

/* Encode le message et l'ajoute à l'anneau de la liaison k, depuis l'un ou
 * l'autre cœur. Faux si l'anneau est plein : le message est perdu, et l'image
 * avec lui. `fin`, s'il est donné, reçoit la position (en mots depuis le
 * démarrage) juste après ce message : de quoi savoir quand il sera parti. */
bool envoyer(uint8_t k, const liaison::Entete &e, const void *charge,
             uint32_t *fin = nullptr);

/* Vrai quand tous les mots jusqu'à `marque` ont quitté la tête. */
bool transmis(uint8_t k, uint32_t marque);

/* Le nœud k est prêt : RDY haut. Une nappe débranchée compte comme prête. */
bool pret(uint8_t k);

/* Impulsion VSYNC (active basse) : les nœuds publient sur le front descendant. */
void vsync();

const Stats &stats();

} // namespace lien
