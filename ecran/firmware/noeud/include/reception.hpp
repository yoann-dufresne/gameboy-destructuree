/**
 * Nœud — réception de la liaison
 *
 * Un PIO échantillonne la nappe, un DMA sans fin (RP2350) écrit les mots dans
 * un anneau de 32 ko, sans interruption : le pilote HUB75 prend les deux
 * interruptions DMA pour lui. La boucle principale lit les messages à son
 * rythme.
 */
#pragma once

#include <cstdint>

#include "liaison.h"

namespace reception {

struct Stats {
    uint32_t messages;
    uint64_t octets;
    uint32_t erreurs;    /* en-tête impossible ou CRC faux */
    uint32_t resynchros;
};

void init();

/* Extrait le prochain message complet. ERREUR : la réception est déjà
 * resynchronisée quand cette fonction rend la main. */
liaison::Lecteur::Resultat lire(uint32_t *message, liaison::Entete &e);

const Stats &stats();

} // namespace reception
