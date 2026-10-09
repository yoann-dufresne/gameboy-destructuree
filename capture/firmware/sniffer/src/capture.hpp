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

/* Une tranche prête à partir, pointant dans le tampon EN COURS de remplissage.
 * Ses octets ne seront plus touchés d'ici la fin de la trame. */
struct Tranche {
    const uint8_t *donnees;
    uint16_t offset;      /* position dans la trame */
    uint16_t taille;
    uint16_t frame_id;
    bool derniere;
};

struct Stats {
    uint32_t trames;          /* trames complètes capturées                  */
    uint32_t trames_douteuses;/* lignes != 144 sur la trame — intégrité      */
    uint32_t lignes_derniere; /* compte de lignes de la dernière trame       */
    uint32_t mots_restants;   /* mots que le DMA n'a pas écrits à la VSYNC.
                               * ≠ 0 ⇒ des fronts d'horloge pixel manquent   */
    uint32_t trames_incompletes; /* trames où mots_restants ≠ 0, cumulées   */
    uint32_t relances_tardives;  /* PIO relancé après le début de la ligne 0 :
                                  * premier pixel de l'image manqué          */
    uint32_t rejetees;           /* images mal capturées, ni émises ni publiées */
    uint32_t debordements;    /* RX FIFO du PIO saturé — ne devrait jamais   */
    uint32_t perdues;         /* trames prêtes jamais lues par la boucle     */
    uint32_t tranches_perdues;/* file pleine : la boucle n'a pas suivi        */
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

/* Rend la prochaine tranche à émettre, s'il y en a une. Alimentée par les
 * interruptions de ligne et de trame ; drainée par la boucle principale. */
bool tranche_prete(Tranche &out);

/* Émission pipelinée : une tranche part dès que ses 35 lignes sont capturées,
 * au lieu d'attendre la fin de la trame. Commutable à chaud pour pouvoir
 * comparer les deux modes dans les mêmes conditions. */
void pipeline(bool actif);
bool pipeline_actif();

/* Délai de lecture du premier pixel de chaque ligne, en cycles PIO après son
 * front montant (0..31). Réglable à chaud : sert à retrouver sa fenêtre valide
 * sans analyseur logique, par tools/balaye_premier_pixel.py. */
void delai_premier(uint8_t cycles);
uint8_t delai_premier();

/* Délai de lecture des autres pixels, en cycles PIO après leur front
 * descendant (0..31). Réglable à chaud, pour la même raison. */
void delai_echantillon(uint8_t cycles);
uint8_t delai_echantillon();

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
