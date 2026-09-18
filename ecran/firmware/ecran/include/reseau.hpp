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
    uint32_t retardataires;      /* tranches d'une trame déjà soldée, arrivées
                                  * dans le désordre : écartées sans dommage */
    uint32_t resynchros;         /* compteur de l'émetteur reparti en arrière :
                                  * redémarrage, on se recale sur lui */
    uint32_t ctrl;               /* paquets de commande reçus (palette, luminosité) */
};

/* Associe au WiFi et arme la réception. Rend false si l'association échoue. */
bool connecter(uint8_t node_id);

/* Une trame complète, avec les horodatages de son assemblage. */
struct Trame {
    const uint8_t *pixels;  /* BGR888, ou indices si format == PXL1_FMT_IDX8 */
    uint8_t format;
    uint16_t id;
    uint64_t t_premier_us; /* arrivée de la première tranche */
    uint64_t t_dernier_us; /* arrivée de la dernière */
};

/* Rend la dernière trame complète reçue. Faux s'il n'y en a pas de nouvelle.
 * Le tampon reste stable jusqu'au prochain appel : la réception remplit l'autre. */
bool trame_a_afficher(Trame &out);

/* Développe une trame indexée dans `sortie` (BGR888) en appliquant la palette
 * courante. `sortie` doit faire DISPLAY_W * DISPLAY_H * 3 octets. */
void developper_idx8(const uint8_t *indices, uint8_t *sortie);

/* Renvoie un accusé à l'émetteur : c'est lui qui mesure l'aller-retour, sur sa
 * propre horloge, sans qu'aucune synchronisation soit à supposer. */
void acquitter(uint16_t frame_id);

/* Luminosité demandée par un paquet de commande, 0 si aucune. À appliquer
 * depuis la boucle principale : la reconstruction des commandes de ligne n'a
 * rien à faire dans un contexte d'interruption. */
uint8_t luminosite_demandee();

const Stats &stats();
const char *adresse_ip();

} // namespace reseau
