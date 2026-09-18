/**
 * Module ÉCRAN — façade réseau
 *
 * Associe la carte au WiFi, écoute le port PXL1 et écrit chaque tranche reçue
 * directement dans le tampon d'affichage. Le reste du firmware n'a qu'à demander
 * « une trame est-elle prête ? » et publier.
 */
#pragma once

#include <cstdint>

namespace reseau {

struct Stats {
    uint32_t paquets;            /* datagrammes reçus, valides ou non */
    uint32_t rejets;             /* en-tête invalide, mauvais nœud, hors bornes */
    uint32_t trames;             /* trames complètes présentées */
    uint32_t trames_incompletes; /* dernière tranche reçue mais octets manquants */
    uint32_t collisions;         /* tranche écrite PENDANT que l'affichage lisait
                                  * le tampon : déchirure garantie */
    uint32_t ecartees;           /* trames complètes abandonnées parce qu'une plus
                                  * récente est arrivée avant qu'on ait pu publier */
};

/* Associe au WiFi et arme la réception. Rend false si l'association échoue. */
bool connecter(uint8_t node_id);

/* Rend la dernière trame complète reçue, ou nullptr s'il n'y en a pas de
 * nouvelle. Le tampon rendu reste stable jusqu'au prochain appel : la réception
 * remplit l'autre. */
const uint8_t *trame_a_afficher();

const Stats &stats();
const char *adresse_ip();

} // namespace reseau
