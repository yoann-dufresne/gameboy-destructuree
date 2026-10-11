/**
 * La décimation de la source : 192 kHz → 48 kHz, en virgule fixe.
 *
 * C++ portable, sans pico-sdk : le même code tourne dans la source et dans
 * son test sur PC (test/test_decimation.cpp).
 *
 * Deux étages « demi-bande » en cascade, 192 → 96 kHz puis 96 → 48 kHz. Dans
 * un demi-bande, le coefficient central vaut 1/2 et un coefficient sur deux
 * est nul : il ne reste que 4 multiplications au premier étage et 14 au
 * second, soit 22 par échantillon de sortie et par voie (2,1 millions par
 * seconde pour deux voies). Les coefficients viennent de
 * tools/decimation_conception.py, qui en donne la justification :
 *
 *   - bande utile, 0 à 20 kHz : à ±0,002 dB (−0,04 dB à 20 Hz, à cause du
 *     passe-haut ci-dessous) ;
 *   - ce qui se replierait dans la bande utile : atténué de 72 dB au moins,
 *     mesuré par le test sur ce code (74 dB en théorie) ;
 *   - de 20 à 28 kHz, ce qui se replie tombe entre 20 et 24 kHz, où personne
 *     n'entend rien ;
 *   - retard : 7 échantillons à 192 kHz et 27 à 96 kHz, 0,32 ms ;
 *   - coût sur le RP2350 : compilé pour le Cortex-M33, environ 250
 *     instructions par échantillon de sortie et par voie, 24 millions par
 *     seconde pour deux voies : de l'ordre de 20 % d'un cœur à 150 MHz. Une
 *     estimation, à mesurer en phase 2 ; le cœur 1 n'a que ça à faire.
 *
 * Un filtre passe-haut d'un ordre, à 1,9 Hz, retire ensuite le continu : la
 * polarisation de l'étage d'entrée n'est jamais exactement au milieu de
 * l'ADC, et le continu ne doit pas atteindre le haut-parleur.
 *
 * Échelle : la pleine échelle de l'ADC, ±2048 pas autour du milieu, devient
 * la pleine échelle du PCM 16 bits, ±32768.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace son {

/* tools/decimation_conception.py — Q15, côté droit du centre, distances 1, 3,
 * 5… ; le centre vaut 1/2. */
inline constexpr int16_t ETAGE1[4] = {9946, -2240, 572, -86};
inline constexpr int16_t ETAGE2[14] = {10389, -3349, 1879, -1212, 821, -564, 384,
                                       -256,  165,   -102, 59,    -31,  15,  -6};

/* Un étage demi-bande : il reçoit deux échantillons, en rend un. */
template <size_t NC>
class DemiBande {
public:
    static constexpr size_t LONGUEUR = 4 * NC - 1;
    static constexpr size_t CENTRE = 2 * NC - 1;

    explicit DemiBande(const int16_t (&coef)[NC]) : coef_(coef) { vider(); }

    void vider() {
        for (auto &v : hist_) v = 0;
        pos_ = 0;
    }

    /* Deux échantillons d'entrée, le plus ancien d'abord ; rend la sortie. */
    int32_t pousser(int32_t a, int32_t b) {
        ajouter(a);
        ajouter(b);
        /* w[0] est le plus ancien des LONGUEUR derniers, w[LONGUEUR-1] le
         * plus récent : l'historique est doublé, pas de modulo. */
        const int32_t *w = &hist_[pos_];
        int32_t acc = w[CENTRE] * (1 << 14);
#pragma GCC unroll 16
        for (size_t j = 0; j < NC; ++j)
            acc += coef_[j] * (w[CENTRE - (2 * j + 1)] + w[CENTRE + (2 * j + 1)]);
        return (acc + (1 << 14)) >> 15;
    }

private:
    void ajouter(int32_t x) {
        hist_[pos_] = hist_[pos_ + LONGUEUR] = x;
        pos_ = (pos_ + 1 == LONGUEUR) ? 0 : pos_ + 1;
    }

    const int16_t (&coef_)[NC];
    int32_t hist_[2 * LONGUEUR];
    size_t pos_;
};

class Decimation {
public:
    static constexpr size_t FACTEUR = 4;

    /* `milieu` : la valeur de l'ADC au repos, 2048 en théorie. Le passe-haut
     * retire de toute façon l'écart ; la donner juste raccourcit seulement
     * la mise en route. */
    explicit Decimation(uint16_t milieu = 2048, bool retirer_continu = true)
        : milieu_(milieu), retirer_continu_(retirer_continu) {
        vider();
    }

    void vider() {
        for (auto &v : voies_) {
            v.e1.vider();
            v.e2.vider();
            v.x_prec = 0;
            v.y = 0;
        }
    }

    /* `adc` : paires entrelacées (voie 0, voie 1), telles que le DMA les
     * range quand l'ADC alterne ses deux entrées ; `n_paires` multiple de 4.
     * Écrit n_paires / 4 paires dans `sortie`, et le rend. L'état est gardé
     * d'un appel à l'autre : le découpage des appels ne change rien. */
    size_t traiter(const uint16_t *adc, size_t n_paires, int16_t *sortie) {
        const size_t n = n_paires / FACTEUR;
        for (size_t i = 0; i < n; ++i) {
            const uint16_t *e = adc + 2 * FACTEUR * i;
            for (size_t v = 0; v < 2; ++v) {
                Voie &voie = voies_[v];
                const int32_t a = voie.e1.pousser(entree(e[v]), entree(e[2 + v]));
                const int32_t b = voie.e1.pousser(entree(e[4 + v]), entree(e[6 + v]));
                const int32_t x = voie.e2.pousser(a, b);
                sortie[2 * i + v] = saturer(retirer_continu_ ? passe_haut(voie, x) : x);
            }
        }
        return n;
    }

private:
    struct Voie {
        DemiBande<4> e1{ETAGE1};
        DemiBande<14> e2{ETAGE2};
        int32_t x_prec;
        int32_t y; /* sortie du passe-haut, × 2^12 */
    };

    /* ±2048 pas → ±16384 : la marge au-dessus garde les dépassements des
     * filtres sur une onde carrée à pleine échelle. */
    int32_t entree(uint16_t s) const { return ((int32_t)s - milieu_) * 8; }

    /* y[n] = x[n] − x[n−1] + (1 − 2^-12) y[n−1] : coupure à 1,9 Hz. La fuite
     * est arrondie au plus proche : tronquée, elle s'arrêterait avant zéro
     * et laisserait un reste de continu. */
    static int32_t passe_haut(Voie &v, int32_t x) {
        v.y += (x - v.x_prec) * (1 << 12) - ((v.y + (1 << 11)) >> 12);
        v.x_prec = x;
        return (v.y + (1 << 11)) >> 12;
    }

    /* ±16384 → ±32768 */
    static int16_t saturer(int32_t x) {
        x *= 2;
        return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }

    int32_t milieu_;
    bool retirer_continu_;
    Voie voies_[2];
};

} // namespace son
