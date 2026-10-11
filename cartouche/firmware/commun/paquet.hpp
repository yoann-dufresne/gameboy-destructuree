/**
 * Emballer et déballer les paquets CAR1 — partagé par le lecteur et la doublure.
 */
#pragma once

#include "car1.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace cartouche {

/* Un paquet : l'en-tête, puis jusqu'à deux morceaux de charge. */
inline std::vector<uint8_t> emballer(uint8_t type, uint32_t appairage, uint32_t insertion, const void *a,
                                     size_t na, const void *b = nullptr, size_t nb = 0) {
    std::vector<uint8_t> p(CAR1_ENTETE + na + nb);
    car1_entete e{};
    e.magic = CAR1_MAGIC;
    e.type = type;
    e.version = CAR1_VERSION;
    e.longueur = (uint16_t)(na + nb);
    e.appairage = appairage;
    e.insertion = insertion;
    std::memcpy(p.data(), &e, CAR1_ENTETE);
    if (na) std::memcpy(p.data() + CAR1_ENTETE, a, na);
    if (nb) std::memcpy(p.data() + CAR1_ENTETE + na, b, nb);
    return p;
}

/* Vérifie un paquet reçu : nombre magique, version, longueur, appairage.
 * Rend la charge et sa longueur, ou nullptr. */
inline const uint8_t *deballer(const uint8_t *d, size_t n, uint32_t appairage, car1_entete &e, size_t &charge) {
    if (n < CAR1_ENTETE) return nullptr;
    std::memcpy(&e, d, CAR1_ENTETE);
    if (e.magic != CAR1_MAGIC || e.version != CAR1_VERSION) return nullptr;
    if (e.appairage != appairage) return nullptr;
    if ((size_t)e.longueur + CAR1_ENTETE != n) return nullptr;
    charge = e.longueur;
    return d + CAR1_ENTETE;
}

/* Lit une structure en tête de charge, si elle y tient. */
template <class T> inline bool extraire(const uint8_t *charge, size_t n, T &t) {
    if (n < sizeof(T)) return false;
    std::memcpy(&t, charge, sizeof(T));
    return true;
}

} // namespace cartouche
