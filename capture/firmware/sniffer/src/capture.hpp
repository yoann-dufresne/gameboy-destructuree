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

/* Horodatage (µs, horloge du Pico) de la VSYNC qui a clos la trame rendue par
 * le dernier `trame_prete()`. Saisi au même instant que le pointeur, donc
 * cohérent avec lui même si une VSYNC survient entre les deux appels.
 *
 * Sert à décomposer la latence : ce qui se passe ENTRE la fin de capture et le
 * départ des paquets appartient au Pico, pas au réseau. */
uint32_t horodatage_trame();

const Stats &stats();

/* Remet les compteurs et la mesure de cadence à zéro. Sert à démontrer qu'une
 * erreur est un transitoire de démarrage : après remise à zéro, elle ne doit
 * plus jamais réapparaître. */
void reinitialiser();

/* Secondes écoulées depuis la dernière remise à zéro. */
float duree_observation();

/* Cadence MOYENNE depuis la dernière remise à zéro, en trames/seconde.
 * Moyennée sur toute la durée : une fenêtre d'une seconde ne donne que des
 * entiers (59 ou 60) et ne permet pas de vérifier les 59,73 attendus. */
float cadence();

} // namespace capture
