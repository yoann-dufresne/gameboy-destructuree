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
};

/* Associe au WiFi et arme la réception. `tampon` est le tampon d'affichage :
 * les tranches y sont écrites en place. Rend false si l'association échoue. */
bool connecter(uint8_t node_id, uint8_t *tampon, uint32_t taille);

/* Vrai une seule fois par trame complète reçue : consomme le drapeau. */
bool trame_prete();

const Stats &stats();
const char *adresse_ip();

} // namespace reseau
