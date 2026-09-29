/**
 * Tête — façade réseau
 *
 * Associe la carte au WiFi, écoute le port 4242 en PXL2 et en PXL1, réassemble
 * les images, les place dans le canevas et les découpe par rangée. Le reste du
 * firmware n'a qu'à demander « une image est-elle complète ? ».
 */
#pragma once

#include <cstdint>

#include "config.h"

namespace reseau {

struct Stats {
    uint32_t paquets;          /* datagrammes reçus, valides ou non */
    uint32_t pxl1;             /* dont PXL1, en compatibilité */
    uint32_t rejets;           /* en-tête invalide, tranche hors bornes, pixel coupé */
    uint32_t hors_canevas;     /* image plus grande que le canevas */
    uint32_t format_refuse;    /* format que les nœuds ne savent pas lire */
    uint32_t sans_geometrie;   /* tranche PXL1 reçue avant sa géométrie */
    uint32_t images;           /* images complètes */
    uint32_t incompletes;      /* image abandonnée : une plus récente a commencé
                                * avant qu'elle soit complète */
    uint32_t retardataires;    /* tranches d'une image abandonnée, arrivées après
                                * le début de la suivante : écartées */
    uint32_t doublons;         /* tranche déjà reçue pour cette image : écartée */
    uint32_t non_suivies;      /* tranches au-delà du suivi des doublons */
    uint32_t resynchros;       /* compteur de l'émetteur reparti en arrière */
    uint32_t geometries;       /* changements de géométrie de la source */
    uint32_t ctrl;             /* palette, luminosité, géométrie */
    uint32_t pings;
    uint32_t envois_echoues;   /* accusés et PONG que la tête n'a pas pu émettre */
    uint64_t octets;           /* charge utile des tranches acceptées */
    uint32_t segments;         /* messages vers les nœuds, images complètes seules */
    uint64_t pixels_rangee[NB_RANGEES]; /* pixels par rangée, images complètes seules */
    uint32_t deconnexions, reconnexions;
};

/* Associe au WiFi et arme la réception. Faux si l'association échoue. */
bool connecter();

/* À appeler souvent depuis la boucle principale : surveille le lien et relance
 * l'association s'il tombe. Ne bloque jamais. */
void entretenir();

/* Une image complète. */
struct Image {
    uint16_t id;
    uint16_t largeur, hauteur;
    uint8_t format;
    uint8_t protocole;     /* 1 ou 2 */
    uint64_t t_premier_us; /* arrivée de la première tranche */
    uint64_t t_dernier_us; /* arrivée de la dernière */
};

/* Rend la dernière image complète, si elle est nouvelle. */
bool image_complete(Image &out);

/* Accusé à l'émetteur, dans son protocole : c'est lui qui mesure l'aller-retour,
 * sur sa propre horloge. */
void acquitter(const Image &img);

/* Vrai une fois après chaque changement de géométrie de la source. En phase 5b,
 * c'est là que les nœuds recevront EFFACER : les marges ne sont plus écrites. */
bool geometrie_changee(uint16_t &largeur, uint16_t &hauteur, uint8_t &format);

/* Luminosité demandée par un paquet de commande, 0 si aucune. */
uint8_t luminosite_demandee();

/* Instantané cohérent des compteurs : la réception tourne en interruption. */
Stats stats();
const char *adresse_ip();

} // namespace reseau
