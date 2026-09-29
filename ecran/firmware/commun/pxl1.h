/**
 * Protocole PXL1 — transport d'images sur UDP, version 1
 *
 * Conçu tuile-conscient et multi-format : un nœud ne reçoit que les pixels de
 * SON rectangle, et l'émetteur connaît la grille. Remplacé par PXL2 depuis la
 * révision du 29/09/2026 (plan §4) ; la tête l'accepte encore, en
 * compatibilité, parce que le sniffer du module capture l'émet (plan §4.3).
 *
 * Ce fichier fait foi pour les deux sous-projets : le module capture en tient
 * une copie.
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

    /* + uint16 largeur, uint16 hauteur, uint8 format — géométrie de la SOURCE.
     *
     * Extension du module capture (25/09/2026), reportée ici le 29/09/2026 :
     * un émetteur agnostique de l'afficheur doit annoncer la taille de son
     * image, que l'en-tête de 12 octets ne porte pas. Renvoyée toutes les 2 s
     * avec la palette. PXL2 la porte dans chaque en-tête. */
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
