/**
 * PROVISOIRE — comment l'injecteur combine plusieurs manettes.
 *
 * Jusqu'à 4 manettes peuvent rejoindre le réseau des manettes (décision de
 * Yoann, 11/10/2026, reseau.md §8). Qui gagne quand deux joueurs appuient en
 * même temps, et qui allume ou éteint la console, n'est PAS décidé : ce
 * fichier isole ce choix, pour qu'il se change ici et nulle part ailleurs.
 *
 * Avec une seule manette, ces règles redonnent exactement celles du plan
 * (§4) ; avec plusieurs, elles ne sont qu'un point de départ :
 *
 *   - les boutons : un OU. Un bouton est enfoncé si une manette au moins
 *     l'enfonce ;
 *   - la console : allumée tant qu'une manette au moins la veut, c'est-à-dire
 *     a son interrupteur sur marche et n'est pas absente depuis la durée
 *     d'extinction ;
 *   - les molettes : celles de la manette en marche entendue en dernier.
 */
#pragma once

#include <cstdint>

namespace manette::fusion {

struct Vue {
    bool fraiche;   /* entendue depuis moins de 200 ms */
    bool veut;      /* interrupteur sur marche, pas absente trop longtemps */
    bool marche;    /* interrupteur sur marche, d'après son dernier état */
    uint32_t vu_ms; /* quand elle a été entendue */
    uint8_t boutons, volume, contraste;
};

inline uint8_t boutons(const Vue *v, unsigned n) {
    uint8_t b = 0;
    for (unsigned i = 0; i < n; ++i)
        if (v[i].fraiche && v[i].marche) b |= v[i].boutons;
    return b;
}

inline bool console_voulue(const Vue *v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (v[i].veut) return true;
    return false;
}

/* Rend l'indice de la manette dont on prend les molettes, ou -1. */
inline int molettes(const Vue *v, unsigned n) {
    int choisie = -1;
    for (unsigned i = 0; i < n; ++i)
        if (v[i].veut && (choisie < 0 || (int32_t)(v[i].vu_ms - v[choisie].vu_ms) > 0))
            choisie = (int)i;
    return choisie;
}

} // namespace manette::fusion
