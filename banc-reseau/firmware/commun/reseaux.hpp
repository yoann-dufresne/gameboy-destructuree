/**
 * Banc de coexistence — les quatre réseaux de reseau.md §2.1
 *
 * Le nom, le sous-réseau, le port et le canal de chacun. Le mot de passe est
 * le même pour les quatre, dans secrets.h : un banc, pas une installation.
 */
#pragma once

#include <cstdint>

#include "banc.h"

namespace banc {

struct Reseau {
    const char *nom;      /* pour la console et l'outil                       */
    const char *ssid;     /* reseau.md §4                                      */
    uint8_t     octet3;   /* 192.168.<octet3>.0/24                             */
    uint16_t    port;     /* celui du vrai protocole : 4242 à 4245             */
    uint8_t     canal3;   /* plan à trois canaux (reseau.md §3)                */
};

/* Plan à un canal : les quatre sur le 11, celui de l'image. */
constexpr uint8_t CANAL_UNIQUE = 11;

constexpr Reseau RESEAUX[BANC_NB_RESEAUX] = {
    {"image",     "gb-image",     4, 4242, 11},
    {"son",       "gb-son",       5, 4243, 1},
    {"manettes",  "gb-manettes",  6, 4244, 6},
    {"cartouche", "gb-cartouche", 7, 4245, 6},
};

/* Le point d'accès est en .1, le périphérique numéro n en .(1 + n). */
constexpr uint8_t OCTET_POINT_ACCES = 1;
constexpr uint8_t octet_peripherique(uint8_t numero) { return (uint8_t)(1 + numero); }

} // namespace banc
