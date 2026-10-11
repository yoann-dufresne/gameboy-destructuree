/**
 * Banc de coexistence — le format de ses paquets, `BNC1`
 *
 * Un format de banc, pas un protocole du projet : il imite la taille et le
 * rythme des vrais flux (PXL1, SON1, MAN1, CAR1) sans en reprendre le contenu.
 * Ce qui compte ici, c'est l'air qu'ils occupent et le temps qu'ils mettent.
 *
 * Tous les champs multi-octets sont en little-endian, comme le RP2350 : les
 * structures se lisent directement. Le fichier se compile en C++ sur le Pico
 * comme sur le PC (test/test_banc.cpp).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#define BANC_MAGIC 0x31434E42u /* 'B','N','C','1' lus en little-endian */
#define BANC_ENTETE 16

/* Les quatre réseaux de reseau.md §2.1. L'indice est aussi l'écart de port
 * (4242 + indice) et de sous-réseau (192.168.4 + indice). */
enum : uint8_t {
    BANC_IMAGE     = 0,
    BANC_SON       = 1,
    BANC_MANETTES  = 2,
    BANC_CARTOUCHE = 3,
    BANC_NB_RESEAUX = 4,
};

enum : uint8_t {
    BANC_DONNEE  = 0, /* le trafic du profil : tranche d'image, bloc de son, état de manette */
    BANC_ACCUSE  = 1, /* écho de `seq`, `flux` et `t_us` : l'émetteur en tire l'aller-retour */
    BANC_SIGNAL  = 2, /* battement du client, deux fois par seconde : « je suis là »         */
    BANC_BILAN   = 3, /* les comptes du client, une fois par seconde (banc_bilan)            */
    BANC_DEMANDE = 4, /* cartouche : la liste des blocs que le point d'accès veut recevoir   */
    BANC_BLOC    = 5, /* cartouche : un bloc de 1 Ko, du client vers le point d'accès        */
    BANC_ANNONCE = 6, /* cartouche : annonce et état, 4 fois par seconde dans chaque sens    */
};

/* Drapeaux d'une tranche d'image : son rang dans l'image, et « dernière ». */
#define BANC_TRANCHE_MASQUE  0x0Fu
#define BANC_TRANCHE_DERNIERE 0x80u

struct __attribute__((packed)) banc_entete {
    uint32_t magic;    /*  0 */
    uint8_t  type;     /*  4 */
    uint8_t  reseau;   /*  5 : BANC_IMAGE… */
    uint8_t  flux;     /*  6 : la copie (image, son) ou la manette, 0..3 */
    uint8_t  drapeaux; /*  7 */
    uint32_t seq;      /*  8 : numéro dans le flux */
    uint32_t t_us;     /* 12 : horloge de l'ÉMETTEUR, renvoyée telle quelle dans l'accusé */
};
static_assert(sizeof(banc_entete) == BANC_ENTETE, "en-tete BNC1 non compact");

/* ───────────────────────────────────────────────────────── les profils
 *
 * Les tailles sont celles des datagrammes réels, en-tête compris : l'air ne
 * voit pas la différence entre un en-tête PXL1 de 12 octets et un en-tête
 * BNC1 de 16. */

/* Image : 5 tranches par image, 4 de 1412 octets et une de 172 (PXL1, IDX2
 * 160 × 144), à 59,73 images/s, une copie par écran. Chaque tranche part
 * quand ses 35 lignes sont capturées, comme le sniffer en mode pipeliné. */
#define BANC_IMAGE_TRANCHES        5
#define BANC_IMAGE_OCTETS_TRANCHE  1412
#define BANC_IMAGE_OCTETS_DERNIERE 172
#define BANC_IMAGE_PERIODE_NS      16742706u /* 154 lignes × 456 / 4,194304 MHz */
#define BANC_IMAGE_LIGNE_NS        108719u   /* 456 / 4,194304 MHz */
#define BANC_IMAGE_LIGNES_TRANCHE  35
#define BANC_IMAGE_LIGNES_VISIBLES 144

/* Son : un bloc de 5 ms, 976 octets (SON1 : 16 d'en-tête + 960 de PCM). Le
 * haut-parleur accuse un bloc sur 20, pour l'aller-retour. */
#define BANC_SON_OCTETS     976
#define BANC_SON_PERIODE_US 5000u
#define BANC_SON_ACCUSE_1_SUR 20u

/* Manettes : l'état complet, 24 octets (MAN1), toutes les 50 ms ; à chaque
 * changement, tout de suite puis deux fois à 4 ms d'écart. Chaque paquet est
 * accusé. */
#define BANC_MANETTE_OCTETS       24
#define BANC_MANETTE_PERIODE_US   50000u
#define BANC_MANETTE_REPETITION_US 4000u

/* Cartouche : 1 Mo en blocs de 1 Ko, demandés par fenêtres de 32 (CAR1). */
#define BANC_CARTOUCHE_BLOC      1024
#define BANC_CARTOUCHE_BLOCS     1024
#define BANC_CARTOUCHE_FENETRE   32
#define BANC_CARTOUCHE_ANNONCE_OCTETS 64
#define BANC_CARTOUCHE_PERIODE_ANNONCE_US 250000u

/* Un client se signale deux fois par seconde ; le point d'accès l'oublie
 * après 2 s de silence (reseau.md §2.3). */
#define BANC_SIGNAL_PERIODE_US 500000u
#define BANC_OUBLI_MS          2000u
#define BANC_CLIENTS_MAX       4  /* un Pico 2 W en point d'accès n'en accepte pas plus */
#define BANC_COPIES            4  /* une copie par périphérique, quatre au plus */

/* ───────────────────────────────────────────── histogramme des retards
 *
 * Bornes supérieures des seaux, en µs ; le dernier seau prend tout ce qui
 * dépasse. Serrées sous 10 ms, où se jouent l'image et la manette, lâches
 * au-delà. ⚠️ tools/banc.py en tient une copie : les garder identiques. */
#define BANC_SEAUX 25
static constexpr uint32_t BANC_BORNES_US[BANC_SEAUX - 1] = {
    500,    1000,   1500,   2000,   3000,   4000,   5000,   6000,
    8000,   10000,  12000,  15000,  20000,  25000,  30000,  40000,
    50000,  60000,  80000,  100000, 150000, 200000, 300000, 500000,
};

static inline int banc_seau(uint32_t us) {
    int i = 0;
    while (i < BANC_SEAUX - 1 && us > BANC_BORNES_US[i])
        ++i;
    return i;
}

/* ───────────────────────────────────────────────── le bilan d'un client
 *
 * Envoyé une fois par seconde à son point d'accès, qui l'imprime sur sa
 * console USB : seuls les quatre points d'accès sont reliés au PC, les
 * clients peuvent être posés à 3 m sur un simple chargeur.
 *
 * Les compteurs sont CUMULÉS depuis la remise à zéro : un bilan perdu ne fait
 * rien perdre, le suivant porte tout. Seuls `gigue_us` et `trou_max_us` valent
 * pour la dernière seconde.
 *
 * Pas de `packed` : chaque champ tombe naturellement aligné, ce que vérifie le
 * static_assert, et le firmware peut en tenir les compteurs en place. */
struct banc_bilan {
    uint32_t t_ms;        /* horloge du client                                    */
    uint8_t  numero;      /* 1..4 : adresse .2 à .5                               */
    uint8_t  economie;    /* mode d'économie de la radio, 0 = aucune              */
    int8_t   rssi;        /* dBm, mesuré par le client                            */
    uint8_t  reserve;
    uint32_t recus;       /* paquets de données reçus                             */
    uint32_t perdus;      /* paquets manquants d'après la numérotation            */
    uint32_t doublons;    /* paquets reçus deux fois                              */
    uint32_t completes;   /* image : images complètes ; son : blocs à l'heure     */
    uint32_t incompletes; /* image : images où il manque au moins une tranche     */
    uint32_t trous_50ms;  /* image : intervalles de plus de 50 ms entre deux images
                           * complètes d'une même copie, c'est-à-dire des gels   */
    uint32_t envoyes;     /* paquets émis par le client                           */
    uint32_t echecs;      /* émissions refusées par lwIP                          */
    uint32_t accuses;     /* manettes : accusés reçus                             */
    uint32_t reconnexions;/* associations réussies depuis le démarrage            */
    uint32_t gigue_us;    /* son : écart entre le bloc le plus en avance et le plus
                           * en retard sur la dernière seconde                    */
    uint32_t trou_max_us; /* plus long silence sur la dernière seconde            */
    uint32_t histo[BANC_SEAUX]; /* manettes : aller-retour mesuré par le client   */
};
static_assert(sizeof(banc_bilan) == 56 + 4 * BANC_SEAUX, "bilan BNC1 : remplissage inattendu");
