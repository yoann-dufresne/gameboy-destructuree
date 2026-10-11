/**
 * Protocole SON1 — transport du son sur UDP, version 1
 *
 * Ce fichier fait foi pour la source, le haut-parleur et les outils du PC
 * (tools/son_envoi.py, tools/hautparleur_virtuel.py), qui en reprennent les
 * valeurs. L'analyse est au plan, §5.
 *
 * Mêmes conventions que PXL1 : champs en little-endian, comme le RP2350, et
 * nombre magique en tête. La structure se lit donc directement, sans
 * conversion.
 *
 * Qui envoie quoi :
 *
 *   haut-parleur ── BATTEMENT, 2 par seconde ──────────────► source
 *   source ──────── BLOC, 200 par seconde, une copie par ──► chaque haut-parleur
 *                   haut-parleur qui s'est signalé depuis
 *                   moins de 2 s ; 4 au plus
 *   haut-parleur ── ACCUSE, quelques-uns par seconde ──────► source
 *   source ──────── VEILLE, 2 par seconde, console éteinte ► chaque haut-parleur
 *
 * La source répond à l'adresse ET au port d'où vient le battement : un
 * haut-parleur peut écouter sur n'importe quel port. Les firmwares utilisent
 * SON1_PORT des deux côtés.
 *
 * L'en-tête porte tout le format, à chaque paquet : un haut-parleur redémarré
 * reprend dès le premier paquet reçu, sans rien demander.
 */
#pragma once

#include <cstdint>
#include <cstddef>

#define SON1_PORT            4243
#define SON1_ENTETE            16

/* 'S','O','N','1' lus en little-endian */
#define SON1_MAGIC     0x314E4F53u

/* Le format retenu (plan §4) : PCM 16 bits signé, stéréo, 48 kHz, blocs de
 * 5 ms. Un récepteur se fie à l'en-tête, pas à ces valeurs : elles décrivent
 * ce qu'émet la source. */
#define SON1_FREQUENCE      48000u
#define SON1_VOIES              2u
#define SON1_BLOC_ECHANTILLONS 240u                    /* 5 ms, par voie */
#define SON1_BLOC_OCTETS (SON1_BLOC_ECHANTILLONS * SON1_VOIES * 2u)  /* 960 */
#define SON1_PAQUET_MAX  (SON1_ENTETE + SON1_BLOC_OCTETS)            /* 976 */

/* Battements et veille (plan §5.2). */
#define SON1_BATTEMENT_MS     500u  /* le haut-parleur se signale */
#define SON1_OUBLI_MS        2000u  /* la source oublie un haut-parleur muet */
#define SON1_VEILLE_MS        500u  /* console éteinte : la source le redit */
#define SON1_HAUTPARLEURS_MAX   4u  /* les places d'un Pico en point d'accès */

enum : uint8_t {
    /* Source → haut-parleur : des échantillons. */
    SON1_TYPE_BLOC      = 0,
    /* Haut-parleur → source : sans charge. `echantillon` reprend celui du
     * bloc accusé ; la source en tire l'aller-retour, comme PXL1_TYPE_PING. */
    SON1_TYPE_ACCUSE    = 1,
    /* Haut-parleur → source : sans charge, « je suis là ». Les champs après
     * le type valent 0. Décision de Yoann, 11/10/2026. */
    SON1_TYPE_BATTEMENT = 2,
    /* Source → haut-parleur : sans charge, console éteinte. Le haut-parleur
     * coupe son amplificateur ; un bloc le rallume. Décision de Yoann,
     * 11/10/2026. */
    SON1_TYPE_VEILLE    = 3,
};

enum : uint8_t {
    SON1_FMT_PCM16 = 0, /* signé, little-endian, voies entrelacées G, D */
};

struct __attribute__((packed)) son1_entete {
    uint32_t magic;       /*  0 */
    uint8_t  type;        /*  4 */
    uint8_t  format;      /*  5 */
    uint8_t  voies;       /*  6 */
    /* Volume 0..255, reçu de l'injecteur par le bus de la console. Le
     * haut-parleur l'applique, sous son propre plafond (plan §8, T5.1).
     * Décision de Yoann, 11/10/2026. */
    uint8_t  volume;      /*  7 */
    uint32_t frequence;   /*  8 : en Hz */
    /* Numéro du premier échantillon du bloc, par voie, roulant : il fait le
     * tour en 24,8 h à 48 kHz. Un trou dans la numérotation est une perte ;
     * le numéro place le bloc dans la réserve même s'il arrive dans le
     * désordre. */
    uint32_t echantillon; /* 12 */
};

static_assert(sizeof(son1_entete) == SON1_ENTETE, "en-tete SON1 non compact");

/* Nombre d'échantillons par voie d'un bloc, d'après sa taille. 0 si le paquet
 * n'est pas un bloc bien formé. */
static inline uint32_t son1_echantillons(const son1_entete *e, size_t taille) {
    if (taille < SON1_ENTETE || e->magic != SON1_MAGIC ||
        e->type != SON1_TYPE_BLOC || e->format != SON1_FMT_PCM16 ||
        e->voies == 0)
        return 0;
    const size_t par_echantillon = 2u * e->voies;
    const size_t charge = taille - SON1_ENTETE;
    if (charge % par_echantillon)
        return 0;
    return (uint32_t)(charge / par_echantillon);
}

/* Écart signé entre deux numéros d'échantillon roulants : b − a, juste tant
 * que les deux sont à moins de 12 h l'un de l'autre. */
static inline int32_t son1_ecart(uint32_t a, uint32_t b) {
    return (int32_t)(b - a);
}
