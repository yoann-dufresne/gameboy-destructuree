/**
 * Module CAPTURE — façade de la capture
 *
 * Expose ce dont le reste du firmware a besoin : « une trame est-elle prête ? »
 * et « comment ça se passe ? ». Toute la mécanique PIO / DMA / interruptions
 * reste dans capture.cpp.
 */
#pragma once

#include <cstdint>

namespace capture {

struct Stats {
    uint32_t trames;          /* trames complètes capturées                  */
    uint32_t trames_douteuses;/* lignes != 144 sur la trame — intégrité      */
    uint32_t lignes_derniere; /* compte de lignes de la dernière trame       */
    uint32_t mots_restants;   /* mots que le DMA n'a pas écrits à la VSYNC.
                               * ≠ 0 ⇒ des fronts d'horloge pixel manquent   */
    uint32_t debordements;    /* RX FIFO du PIO saturé — ne devrait jamais   */
    uint32_t perdues;         /* trames prêtes jamais lues par la boucle     */
};

/* Démarre PIO, DMA et les deux interruptions. Ne rend pas la main tant que
 * ce n'est pas armé. */
void init();

/* Rend un pointeur sur la dernière trame complète, ou nullptr s'il n'y en a
 * pas de nouvelle. Le tampon reste stable jusqu'au prochain appel : la
 * capture remplit l'autre. */
const uint8_t *trame_prete();

/* Numéro de la dernière trame rendue par trame_prete(). */
uint16_t numero_trame();

const Stats &stats();

/* Cadence mesurée sur la dernière fenêtre d'observation, en trames/seconde. */
float cadence();

} // namespace capture
