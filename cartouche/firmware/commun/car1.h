/**
 * Protocole CAR1 — la cartouche entre le lecteur et la doublure, sur UDP
 *
 * Proposition du 11/10/2026 (plan §7). Ce fichier fera foi pour les deux
 * moitiés du module et pour l'outil du PC.
 *
 * Mêmes conventions que PXL1, SON1 et MAN1 : tous les champs multi-octets en
 * little-endian, comme le RP2350 ; les structures se lisent directement.
 *
 * Chaque paquet commence par le même en-tête de 16 octets, puis la charge du
 * type. Le lecteur et la doublure ignorent tout paquet dont l'appairage n'est
 * pas le leur.
 *
 *   type        sens                 quand
 *   ANNONCE     lecteur → doublure   4 fois par seconde, et aussitôt à chaque
 *                                    insertion, retrait, ou changement d'état
 *   DEMANDE     doublure → lecteur   les blocs qui lui manquent, par 32 au plus
 *   BLOC        lecteur → doublure   1 Kio de ROM ou de sauvegarde
 *   ECRITURE    doublure → lecteur   une entrée du journal, numérotée
 *   ACCUSE      lecteur → doublure   jusqu'où le journal est traité
 *   ETAT        doublure → lecteur   4 fois par seconde
 */
#pragma once

#include <cstddef>
#include <cstdint>

#define CAR1_PORT 4245
#define CAR1_VERSION 1
#define CAR1_ENTETE 16
#define CAR1_OCTETS_BLOC 1024   /* un bloc de copie */
#define CAR1_OCTETS_PAGE 256    /* une page du journal (plan §3.1) */
#define CAR1_DEMANDE_MAX 32     /* blocs par demande : la fenêtre du plan §7 */
#define CAR1_PAGES_MAX 4        /* pages par écriture : 16 + 16 + 4 × 258 = 1064 octets */

/* 'C','A','R','1' lus en little-endian */
#define CAR1_MAGIC 0x31524143u

enum : uint8_t {
    CAR1_ANNONCE = 0,
    CAR1_DEMANDE = 1,
    CAR1_BLOC = 2,
    CAR1_ECRITURE = 3,
    CAR1_ACCUSE = 4,
    CAR1_ETAT = 5,
};

/* L'en-tête commun. `insertion` désigne la cartouche dont il s'agit : celle
 * qu'annonce le lecteur, celle dont la doublure copie ou sert la copie, celle
 * à qui une écriture est destinée. 0 : aucune. */
struct __attribute__((packed)) car1_entete {
    uint32_t magic;      /*  0 */
    uint8_t type;        /*  4 */
    uint8_t version;     /*  5 */
    uint16_t longueur;   /*  6 : octets de charge après l'en-tête */
    uint32_t appairage;  /*  8 : réglé dans les deux firmwares */
    uint32_t insertion;  /* 12 : tiré au hasard au démarrage du lecteur, puis +1 */
};
static_assert(sizeof(car1_entete) == CAR1_ENTETE, "en-tete CAR1 non compact");

/* ---- ANNONCE ------------------------------------------------------------ */

enum : uint8_t {
    CAR1_FENTE_VIDE = 0,
    CAR1_LECTURE = 1,           /* insérée, lecture en cours */
    CAR1_PRETE = 2,             /* lue, vérifiée : la copie peut partir */
    CAR1_CONTACTS_SALES = 3,    /* logo ou sommes faux après trois essais */
    CAR1_NON_PRISE_EN_CHARGE = 4,
};

struct __attribute__((packed)) car1_annonce {
    uint8_t etat;               /*  0 : CAR1_FENTE_VIDE… */
    uint8_t code_type;          /*  1 : octet 0147 */
    uint8_t code_rom;           /*  2 : octet 0148 */
    uint8_t code_ram;           /*  3 : octet 0149 */
    uint8_t somme_entete;       /*  4 : octet 014D */
    uint8_t horloge_lue;        /*  5 : 1 si `horloge` est valable */
    uint16_t somme_globale;     /*  6 : 014E–014F */
    char titre[16];             /*  8 : 0134–0143 */
    uint32_t octets_rom;        /* 24 */
    uint32_t octets_ram;        /* 28 : MBC2 : 512 */
    uint32_t crc_rom;           /* 32 : CRC-32 de la ROM entière */
    uint32_t crc_sauvegarde;    /* 36 : CRC-32 de la sauvegarde, telle que le lecteur la sait */
    uint8_t horloge[5];         /* 40 : S, M, H, DL, DH lus à l'insertion */
    uint8_t reserve;            /* 45 */
    uint16_t reserve2;          /* 46 */
    uint32_t age_horloge_ms;    /* 48 : depuis la lecture de l'horloge */
};
static_assert(sizeof(car1_annonce) == 52, "annonce CAR1 non compacte");

/* ---- DEMANDE et BLOC ---------------------------------------------------- */

enum : uint8_t {
    CAR1_ZONE_ROM = 0,
    CAR1_ZONE_SAUVEGARDE = 1,
};

struct __attribute__((packed)) car1_demande {
    uint8_t zone;               /* 0 */
    uint8_t nombre;             /* 1 : 1 à CAR1_DEMANDE_MAX */
    uint16_t reserve;           /* 2 */
    /* suivis de `nombre` indices de bloc, uint16_t chacun */
};
static_assert(sizeof(car1_demande) == 4, "demande CAR1 non compacte");

struct __attribute__((packed)) car1_bloc {
    uint8_t zone;               /* 0 */
    uint8_t reserve;            /* 1 */
    uint16_t index;             /* 2 : en blocs de 1 Kio */
    uint16_t octets;            /* 4 : 1024, moins pour une sauvegarde plus courte */
    uint16_t reserve2;          /* 6 */
    /* suivis de `octets` octets */
};
static_assert(sizeof(car1_bloc) == 8, "bloc CAR1 non compact");

/* ---- ECRITURE et ACCUSE ------------------------------------------------- */

enum : uint8_t {
    CAR1_PAGES = 0,             /* des pages de sauvegarde */
    CAR1_REGLAGE_HORLOGE = 1,   /* le jeu a réglé l'horloge : 5 octets, jamais parqués */
    CAR1_VIBREUR = 2,           /* 1 octet : 1 marche, 0 arrêt */
};

/* Une écriture est désignée par (session, numero). La session est tirée au
 * hasard au démarrage de la doublure ; le numéro repart de 1 à chaque session,
 * et le lecteur traite les numéros d'une session dans l'ordre, sans trou. Un
 * journal rechargé de la flash garde la session où il est né.
 *
 * `base` est le plus petit numéro que la doublure n'a pas vu accusé dans cette
 * session : un lecteur qui ne connaît pas encore la session (première
 * rencontre, ou mémoire perdue) sait ainsi où elle commence, sans prendre une
 * écriture arrivée en avance pour la première. */
struct __attribute__((packed)) car1_ecriture {
    uint32_t session;           /*  0 */
    uint32_t numero;            /*  4 */
    uint32_t base;              /*  8 */
    uint8_t genre;              /* 12 : CAR1_PAGES… */
    uint8_t nombre;             /* 13 : pages, 1 à CAR1_PAGES_MAX ; 0 sinon */
    uint16_t reserve;           /* 14 */
    /* CAR1_PAGES : `nombre` fois { uint16_t page ; 256 octets }
     * CAR1_REGLAGE_HORLOGE : 5 octets S, M, H, DL, DH
     * CAR1_VIBREUR : 1 octet */
};
static_assert(sizeof(car1_ecriture) == 16, "ecriture CAR1 non compacte");

enum : uint8_t {
    CAR1_APPLIQUEE = 0,         /* écrite dans la cartouche */
    CAR1_PARQUEE = 1,           /* gardée pour la cartouche partie (plan §3.4) */
    CAR1_OUBLIEE = 2,           /* une autre cartouche est passée : la doublure peut la jeter */
};

/* Cumulatif : toutes les écritures de `session` jusqu'à `numero` inclus sont
 * traitées. Un accusé en retard ou en double ne fait jamais de mal. */
struct __attribute__((packed)) car1_accuse {
    uint32_t session;           /* 0 */
    uint32_t numero;            /* 4 */
    uint8_t sort;               /* 8 : celui de la dernière, CAR1_APPLIQUEE… */
    uint8_t reserve[3];         /* 9 */
};
static_assert(sizeof(car1_accuse) == 12, "accuse CAR1 non compact");

/* ---- ETAT --------------------------------------------------------------- */

struct __attribute__((packed)) car1_etat {
    uint8_t copie_prete;        /*  0 : pour l'insertion de l'en-tête */
    uint8_t console_allumee;    /*  1 */
    uint8_t muette;             /*  2 : la doublure se tait, console figée ou fente vide */
    uint8_t reserve;            /*  3 */
    uint32_t session;           /*  4 */
    uint16_t en_attente;        /*  8 : écritures non accusées */
    uint16_t reserve2;          /* 10 */
};
static_assert(sizeof(car1_etat) == 12, "etat CAR1 non compact");
