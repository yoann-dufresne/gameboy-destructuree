/**
 * Bus de la console — le format de ses messages (reseau.md §5)
 *
 * Les microcontrôleurs de la console ne partagent pas de réseau : ils se
 * parlent sur un bus I²C. L'injecteur (module manette) en est le maître : c'est
 * lui qui reçoit les manettes et allume la console. Le sniffer (capture), la
 * source de son (son) et la doublure (cartouche) sont ses esclaves.
 *
 * Cinquante fois par seconde, l'injecteur écrit à chaque esclave le même
 * ORDRE, puis lit son ÉTAT. Chacun prend dans l'ordre ce qui le concerne :
 *
 *   sniffer   marche ; luminosité, qu'il transmet aux écrans ; le message
 *             « aucune manette », qu'il incruste, avec le temps qui reste ;
 *   son       marche ; volume, qu'il transmet aux haut-parleurs ;
 *   doublure  marche. Son état porte le feu vert : la copie est prête.
 *
 * Ce fichier FAIT FOI. Les modules en tiennent une copie, comme pxl1.h ; il
 * est éprouvé par le banc R3 (banc-reseau/firmware/bus/) et sur PC
 * (banc-reseau/firmware/test/test_bus_console.cpp). Se compile en C comme en
 * C++, sur le Pico comme sur le PC.
 *
 * Câblage : SDA, SCL et la masse, reliés entre les quatre cartes ; UNE paire
 * de résistances de tirage de 4,7 kΩ vers 3,3 V pour tout le bus. Les tirages
 * internes du RP2350, autour de 50 kΩ, sont trop faibles à 400 kHz.
 */
#ifndef BUS_CONSOLE_H
#define BUS_CONSOLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BUS_VERSION      1
#define BUS_FREQUENCE_HZ 400000u  /* mode rapide : 7 % du bus à 50 interrogations/s */
#define BUS_PERIODE_US   20000u   /* 50 interrogations par seconde et par esclave */

/* I2C1 sur GP6 (SDA) et GP7 (SCL) : libres sur le sniffer (capture,
 * plan-firmware §4). À reprendre sur les autres cartes si possible, pour un
 * seul câblage à retenir. */
#define BUS_BROCHE_SDA 6
#define BUS_BROCHE_SCL 7

/* Adresses 7 bits des esclaves, hors des plages réservées par la norme. */
#define BUS_ADRESSE_SNIFFER  0x41
#define BUS_ADRESSE_SON      0x42
#define BUS_ADRESSE_DOUBLURE 0x43

/* ─────────────────────────────────────────────── l'ordre, maître → esclave */

#define BUS_MARCHE          0x01u /* la console est allumée                          */
#define BUS_MANETTE_ABSENTE 0x02u /* afficher « aucune manette » : absente depuis 2 s  */

#define BUS_RESTE_INCONNU 0xFFFFu /* aucune extinction prévue */

typedef struct __attribute__((packed)) {
    uint8_t  version;    /* BUS_VERSION                                            */
    uint8_t  seq;        /* numéro de l'interrogation, renvoyé dans l'état         */
    uint8_t  drapeaux;   /* BUS_MARCHE, BUS_MANETTE_ABSENTE                         */
    uint8_t  luminosite; /* 0..255 : la molette de contraste de la manette          */
    uint8_t  volume;     /* 0..255 : la molette de volume                           */
    uint16_t absence_s;  /* secondes depuis le dernier paquet de manette, 0 si présente */
    uint16_t reste_s;    /* secondes avant l'extinction, BUS_RESTE_INCONNU si aucune */
    uint8_t  crc;        /* bus_crc8, initialisé à l'adresse de l'esclave           */
} bus_ordre;

/* ─────────────────────────────────────────────── l'état, esclave → maître */

#define BUS_PRET        0x01u /* l'esclave tourne et a compris le dernier ordre       */
#define BUS_COPIE_PRETE 0x02u /* doublure : la copie de la cartouche est prête,
                               * l'injecteur peut allumer la console (cartouche §5.6) */

typedef struct __attribute__((packed)) {
    uint8_t version;  /* BUS_VERSION                                               */
    uint8_t seq;      /* celui du dernier ordre VALIDE reçu                        */
    uint8_t drapeaux; /* BUS_PRET, BUS_COPIE_PRETE                                 */
    uint8_t refus;    /* ordres refusés depuis le démarrage (CRC, version, taille),
                       * plafonné à 255 : l'injecteur voit un bus malade           */
    uint8_t crc;
} bus_etat;

/* ───────────────────────────────────────────────────────────── l'intégrité
 *
 * CRC-8 de polynôme 0x07 (celui du SMBus), initialisé à l'ADRESSE de
 * l'esclave : un message lu ou écrit à la mauvaise adresse est refusé comme
 * un message abîmé. Valeur de contrôle, initialisé à 0 : « 123456789 » → 0xF4. */
static inline uint8_t bus_crc8(uint8_t init, const uint8_t *d, size_t n) {
    uint8_t crc = init;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    }
    return crc;
}

static inline void bus_sceller_ordre(bus_ordre *o, uint8_t adresse) {
    o->version = BUS_VERSION;
    o->crc = bus_crc8(adresse, (const uint8_t *)o, sizeof(*o) - 1);
}

static inline bool bus_ordre_valide(const bus_ordre *o, uint8_t adresse) {
    return o->version == BUS_VERSION &&
           o->crc == bus_crc8(adresse, (const uint8_t *)o, sizeof(*o) - 1);
}

static inline void bus_sceller_etat(bus_etat *e, uint8_t adresse) {
    e->version = BUS_VERSION;
    e->crc = bus_crc8(adresse, (const uint8_t *)e, sizeof(*e) - 1);
}

static inline bool bus_etat_valide(const bus_etat *e, uint8_t adresse) {
    return e->version == BUS_VERSION &&
           e->crc == bus_crc8(adresse, (const uint8_t *)e, sizeof(*e) - 1);
}

#ifdef __cplusplus
static_assert(sizeof(bus_ordre) == 10, "ordre du bus non compact");
static_assert(sizeof(bus_etat) == 5, "etat du bus non compact");
#else
_Static_assert(sizeof(bus_ordre) == 10, "ordre du bus non compact");
_Static_assert(sizeof(bus_etat) == 5, "etat du bus non compact");
#endif

#endif /* BUS_CONSOLE_H */
