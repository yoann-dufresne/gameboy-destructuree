/**
 * Comment l'injecteur combine plusieurs manettes.
 *
 * Jusqu'à 4 manettes peuvent rejoindre le réseau des manettes (décision de
 * Yoann, 11/10/2026, reseau.md §8). Ce fichier isole les règles qui les
 * combinent, pour qu'elles se changent ici et nulle part ailleurs. Avec une
 * seule manette, elles redonnent exactement celles du plan (§4) :
 *
 *   - les boutons : un OU. Un bouton est enfoncé si une manette au moins
 *     l'enfonce. PROVISOIRE : qui gagne quand deux joueurs appuient en même
 *     temps se décidera plus tard ;
 *   - la console : allumée tant qu'une manette au moins la veut, c'est-à-dire
 *     a son interrupteur sur marche et n'est pas absente depuis la durée
 *     d'extinction. Une manette qui revient, interrupteur sur marche, rallume
 *     une console éteinte faute de manette (décision de Yoann, 11/10/2026) ;
 *   - les molettes : la moyenne de celles des manettes qui veulent la console
 *     (décision de Yoann, 11/10/2026).
 */
#pragma once

#include <cstdint>

namespace manette::fusion {

struct Vue {
    bool fraiche;   /* entendue depuis moins de 200 ms */
    bool veut;      /* interrupteur sur marche, pas absente trop longtemps */
    bool marche;    /* interrupteur sur marche, d'après son dernier état */
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

/* La moyenne des molettes, arrondie. Rend faux si aucune manette ne veut la
 * console : les dernières valeurs restent alors en place. */
inline bool molettes(const Vue *v, unsigned n, uint8_t *volume, uint8_t *contraste) {
    unsigned k = 0, vol = 0, con = 0;
    for (unsigned i = 0; i < n; ++i)
        if (v[i].veut) {
            ++k;
            vol += v[i].volume;
            con += v[i].contraste;
        }
    if (!k) return false;
    *volume = (uint8_t)((vol + k / 2) / k);
    *contraste = (uint8_t)((con + k / 2) / k);
    return true;
}

} // namespace manette::fusion
