/**
 * Protocole PXL1 — transport d'images sur UDP
 *
 * Conçu tuile-conscient et multi-format dès la v1 : c'est la seule décision du
 * projet qui coûterait cher à prendre en retard (plan §4). Un nœud ne reçoit que
 * les pixels de SON rectangle ; le protocole est donc indépendant du nombre de
 * nœuds — 1, 3 ou 9, seul le fichier de disposition de l'émetteur change.
 *
 * Tous les champs multi-octets sont en little-endian, comme le RP2350 : la
 * structure se lit donc directement, sans conversion.
 */
#pragma once

#include <cstdint>
#include <cstddef>

#define PXL1_PORT         4242
#define PXL1_ENTETE         12
#define PXL1_CHARGE_MAX   1400 /* sous la MTU : pas de fragmentation IP */

/* 'P','X','L','1' lus en little-endian */
#define PXL1_MAGIC 0x314C5850u

enum : uint8_t {
    PXL1_TYPE_FRAME = 0,
    PXL1_TYPE_CTRL  = 1,
    PXL1_TYPE_PING  = 2,
};

enum : uint8_t {
    /* Ordre des octets B, G, R — celui qu'attend la dalle. Permet d'écrire la
     * charge utile directement dans le tampon d'affichage, sans recopie. */
    PXL1_FMT_BGR888 = 0,
    PXL1_FMT_RGB565 = 1,
    PXL1_FMT_IDX2   = 2, /* Game Boy natif */
    PXL1_FMT_IDX4   = 3,
    PXL1_FMT_IDX8   = 4,
    PXL1_FMT_RLE8   = 5,
};

enum : uint8_t {
    PXL1_FLAG_DERNIERE = 0x01, /* dernière tranche de la trame → présenter */
};

/* Sous-commandes d'un paquet PXL1_TYPE_CTRL, premier octet de la charge utile.
 * Les commandes voyagent hors du flux de pixels : rien d'autre que des pixels
 * ne transite par le chemin critique. */
enum : uint8_t {
    PXL1_CTRL_PALETTE    = 0, /* + 256 x 3 octets B,G,R */
    PXL1_CTRL_LUMINOSITE = 1, /* + 1 octet, luminosité de base 1..255 */

    /* ── EXTENSION du module CAPTURE, 26/09/2026 ────────────────────────
     * + uint16 largeur, uint16 hauteur, uint8 format
     *
     * Jusqu'ici le récepteur déduisait la taille d'une trame de SA PROPRE
     * géométrie. Un émetteur agnostique de l'afficheur — ce qu'est le
     * sniffeur depuis le 25/09/2026 — doit donc lui annoncer celle de la
     * source.
     *
     * Par CTRL et non par l'en-tête : celui-ci fait 12 octets, il est figé
     * et déjà déployé sur le module écran. Et la géométrie change au plus
     * une fois par démarrage : elle n'a rien à faire dans le chemin
     * critique des pixels.
     *
     * ⚠️ Le protocole est possédé par ../ecran. Cette extension est à y
     * reporter avant que les deux modules ne se parlent. */
    PXL1_CTRL_GEOMETRIE  = 2,
};

#define PXL1_GEOMETRIE_OCTETS 5   /* largeur(2) + hauteur(2) + format(1) */

#define PXL1_PALETTE_ENTREES 256
#define PXL1_PALETTE_OCTETS  (PXL1_PALETTE_ENTREES * 3)

struct __attribute__((packed)) pxl1_entete {
    uint32_t magic;    /*  0 */
    uint8_t  type;     /*  4 */
    uint8_t  node_id;  /*  5 */
    uint16_t frame_id; /*  6 */
    uint8_t  format;   /*  8 */
    uint8_t  flags;    /*  9 */
    uint16_t offset;   /* 10 : position de la tranche dans la charge du nœud */
};

static_assert(sizeof(pxl1_entete) == PXL1_ENTETE, "en-tete PXL1 non compact");
