/**
 * Protocole PXL2 — transport d'images sur UDP, agnostique de l'afficheur
 *
 * La source envoie son image ENTIÈRE, à sa taille native, à une seule adresse.
 * Placement et répartition sur les dalles sont l'affaire de l'écran (plan §4).
 * Rien ici ne dit combien de dalles, de rangées ou de contrôleurs se trouvent
 * derrière l'adresse : c'est tout l'objet de la révision du 29/09/2026.
 *
 * Ce qui change par rapport à PXL1 :
 *   - plus de node_id : il n'y a qu'un destinataire ;
 *   - la géométrie de la source voyage dans CHAQUE en-tête : une tête
 *     redémarrée interprète le flux dès le paquet suivant ;
 *   - l'offset passe à 32 bits : 192×192 en BGR888 fait 110 592 octets.
 *
 * Tous les champs multi-octets sont en little-endian, comme le RP2350 : la
 * structure se lit donc directement, sans conversion.
 *
 * Ce fichier fait foi pour le module écran comme pour ses sources.
 */
#pragma once

#include <cstdint>

/* Même port que PXL1 : la tête distingue les deux par le magic. */
#define PXL2_PORT         4242
#define PXL2_ENTETE         18
#define PXL2_CHARGE_MAX   1400 /* sous la MTU : pas de fragmentation IP */

/* 'P','X','L','2' lus en little-endian */
#define PXL2_MAGIC 0x324C5850u

enum : uint8_t {
    PXL2_TYPE_FRAME = 0, /* source → écran : une tranche d'image */
    PXL2_TYPE_CTRL  = 1, /* source → écran : palette, luminosité */
    PXL2_TYPE_PING  = 2, /* source → écran : « qui es-tu ? » */
    PXL2_TYPE_PONG  = 3, /* écran → source : charge pxl2_pong */
    PXL2_TYPE_ACK   = 4, /* écran → source : image frame_id présentée */
};

/* Mêmes numéros que PXL1.
 *
 * Balayage ligne par ligne ; chaque ligne commence sur un octet, d'où un pas
 * de ⌈largeur × bits / 8⌉ octets. Sous l'octet (IDX2, IDX4), le pixel de
 * gauche occupe les bits de POIDS FORT — l'ordre du sniffer. */
enum : uint8_t {
    PXL2_FMT_BGR888 = 0, /* 24 bits, ordre B, G, R — celui de la dalle */
    PXL2_FMT_RGB565 = 1, /* 16 bits */
    PXL2_FMT_IDX2   = 2, /*  2 bits + palette : Game Boy natif */
    PXL2_FMT_IDX4   = 3, /*  4 bits + palette */
    PXL2_FMT_IDX8   = 4, /*  8 bits + palette */
    PXL2_FMT_RLE8   = 5, /* réservé */
};

enum : uint8_t {
    PXL2_FLAG_DERNIERE = 0x01, /* dernière tranche de l'image */
};

/* Sous-commandes d'un paquet CTRL, premier octet de la charge utile. */
enum : uint8_t {
    PXL2_CTRL_PALETTE    = 0, /* + 256 × B,G,R ; IDX2 n'en lit que 4 */
    PXL2_CTRL_LUMINOSITE = 1, /* + 1 octet, luminosité de base 1..255 */
    /* 2 : réservé — PXL1_CTRL_GEOMETRIE, inutile ici */
};

#define PXL2_PALETTE_ENTREES 256
#define PXL2_PALETTE_OCTETS  (PXL2_PALETTE_ENTREES * 3)

struct __attribute__((packed)) pxl2_entete {
    uint32_t magic;    /*  0 */
    uint8_t  type;     /*  4 */
    uint8_t  format;   /*  5 */
    uint16_t frame_id; /*  6 */
    uint16_t largeur;  /*  8 : de l'image source, en pixels */
    uint16_t hauteur;  /* 10 */
    uint32_t offset;   /* 12 : position de la tranche dans l'image, en octets */
    uint8_t  flags;    /* 16 */
    uint8_t  reserve;  /* 17 : 0 */
};

static_assert(sizeof(pxl2_entete) == PXL2_ENTETE, "en-tete PXL2 non compact");

/* Charge utile d'un PONG : ce que l'écran dit de lui-même. */
struct __attribute__((packed)) pxl2_pong {
    uint16_t largeur;    /* canevas */
    uint16_t hauteur;
    uint16_t formats;    /* bit n = format n accepté */
    uint16_t charge_max; /* octets de charge utile par paquet */
};

static_assert(sizeof(pxl2_pong) == 8, "PONG PXL2 non compact");
