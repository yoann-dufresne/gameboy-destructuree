/**
 * Protocole MAN1 — l'état d'une manette, sur UDP, version 1
 *
 * Ce fichier fait foi pour la manette, l'injecteur et les outils du PC
 * (tools/manette_envoi.py, tools/manette_ecoute.py), qui en reprennent les
 * valeurs. L'analyse est au plan, §6.
 *
 * Mêmes conventions que PXL1 et SON1 : champs en little-endian, comme le
 * RP2350, et nombre magique en tête.
 *
 *   manette ── ETAT, à chaque changement, puis 2 répétitions à 4 ms, et un
 *              battement toutes les 50 ms ; rien sur arrêt, sauf l'ordre
 *              « arrêt » ─────────────────────────────────────► injecteur
 *   injecteur ─ ACCUSE, un par état reçu ──────────────────────► manette
 *
 * L'injecteur répond à l'adresse et au port d'où vient l'état. Jusqu'à 4
 * manettes, aux adresses fixes .2 à .5 du réseau des manettes
 * (reseau.md §4) ; l'injecteur les distingue à leur adresse.
 *
 * Chaque paquet porte l'état complet : un paquet perdu ne coûte rien, le
 * suivant dit tout.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#define MAN1_PORT        4244
#define MAN1_TAILLE        24

/* 'M','A','N','1' lus en little-endian */
#define MAN1_MAGIC 0x314E414Du

/* Les cadences de la manette (plan §5.4) et les seuils de l'injecteur
 * (plan §4.4). */
#define MAN1_REPETITIONS          2u   /* après un changement */
#define MAN1_REPETITION_MS        4u
#define MAN1_BATTEMENT_MS        50u
#define MAN1_RELACHE_MS         200u   /* sans nouvelles : tout relâcher */
#define MAN1_MESSAGE_MS        2000u   /* sans nouvelles : « aucune manette » */
#define MAN1_EXTINCTION_MS (15u * 60u * 1000u) /* sans nouvelles : éteindre */
#define MAN1_ACCUSE_ROUGE_MS    500u   /* LED rouge tant que les accusés arrivent */
#define MAN1_ARRET_ABANDON_MS  2000u   /* « arrêt » sans accusé : la manette renonce */
#define MAN1_MANETTES_MAX         4u

enum : uint8_t {
    MAN1_TYPE_ETAT   = 0, /* manette → injecteur */
    MAN1_TYPE_ACCUSE = 1, /* injecteur → manette */
};

/* Les boutons, 1 = enfoncé, dans l'ordre du registre P1 de la console :
 * bits 0 à 3 lus quand P14 est à 0, bits 4 à 7 quand P15 est à 0. */
enum : uint8_t {
    MAN1_DROITE = 1u << 0,
    MAN1_GAUCHE = 1u << 1,
    MAN1_HAUT   = 1u << 2,
    MAN1_BAS    = 1u << 3,
    MAN1_A      = 1u << 4,
    MAN1_B      = 1u << 5,
    MAN1_SELECT = 1u << 6,
    MAN1_START  = 1u << 7,
};

struct __attribute__((packed)) man1_paquet {
    uint32_t magic;    /*  0 */
    uint8_t  type;     /*  4 */
    uint8_t  boutons;  /*  5 : dans un accusé, 0 */
    uint8_t  volume;   /*  6 : 0..255 ; dans un accusé, 0 */
    uint8_t  contraste;/*  7 : 0..255 ; dans un accusé, 0 */
    /* Dans un état : l'interrupteur de la manette, marche (1) ou arrêt (0).
     * Un état à 0 est l'ordre « arrêt ». Dans un accusé : la console,
     * allumée (1) ou éteinte (0). */
    uint8_t  marche;   /*  8 */
    uint8_t  reserve;  /*  9 : 0 */
    uint16_t piles_mv; /* 10 : tension des piles ; dans un accusé, 0 */
    /* Tirée au hasard au démarrage de la manette : elle fait reconnaître une
     * manette redémarrée, dont la numérotation repart de 0. L'accusé la
     * recopie. */
    uint32_t session;  /* 12 */
    /* Chaque émission a son numéro, répétitions comprises. Dans un accusé :
     * celui du paquet acquitté. */
    uint32_t numero;   /* 16 */
    /* Temps depuis le démarrage de l'émetteur, en ms : la manette dans un
     * état, l'injecteur dans un accusé. */
    uint32_t temps_ms; /* 20 */
};

static_assert(sizeof(man1_paquet) == MAN1_TAILLE, "paquet MAN1 non compact");

static inline bool man1_valide(const void *octets, size_t taille) {
    const man1_paquet *p = (const man1_paquet *)octets;
    return taille == MAN1_TAILLE && p->magic == MAN1_MAGIC &&
           (p->type == MAN1_TYPE_ETAT || p->type == MAN1_TYPE_ACCUSE);
}
