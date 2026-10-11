/**
 * Éprouve la décimation de la source sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_decimation.cpp -o test_decimation
 *     ./test_decimation
 *
 * On fabrique ce que l'ADC lirait (des sinus arrondis à 12 bits, deux voies
 * entrelacées), on le passe dans le code de la source, et on mesure ce qui
 * sort : le gain dans la bande utile, ce qui se replie depuis au-delà de
 * 28 kHz, l'indépendance des voies, le continu, et l'absence de débordement
 * en virgule fixe, contre un calcul de référence en double précision.
 */

#include "decimation.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int echecs = 0;

#define VERIFIER(cond, ...)                                          \
    do {                                                             \
        if (!(cond)) {                                               \
            if (++echecs <= 20) {                                    \
                std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__);  \
                std::printf(__VA_ARGS__);                            \
                std::printf("\n");                                   \
            }                                                        \
        }                                                            \
    } while (0)

constexpr double FE = 192000.0; /* entrée, par voie */
constexpr double FS = 48000.0;  /* sortie */
constexpr double PI = 3.14159265358979323846;
constexpr double AMPLITUDE = 1800.0;            /* pas d'ADC, ≈ −1,1 dB */
constexpr double ATTENDU = AMPLITUDE * 16.0;    /* en PCM 16 bits */

/* n_sortie échantillons de sortie, pour des sinus de fréquences fg et fd. */
std::vector<int16_t> passer(double fg, double fd, size_t n_sortie,
                            son::Decimation &d, double ag = AMPLITUDE,
                            double ad = AMPLITUDE) {
    const size_t n_paires = 4 * n_sortie;
    std::vector<uint16_t> adc(2 * n_paires);
    for (size_t k = 0; k < n_paires; ++k) {
        const double t = k / FE;
        adc[2 * k] = (uint16_t)std::lround(2048.0 + ag * std::sin(2 * PI * fg * t));
        adc[2 * k + 1] = (uint16_t)std::lround(2048.0 + ad * std::sin(2 * PI * fd * t + 1.0));
    }
    std::vector<int16_t> s(2 * n_sortie);
    d.traiter(adc.data(), n_paires, s.data());
    return s;
}

/* Amplitude, dans la voie v, de la composante à la fréquence f : moindres
 * carrés sur sinus et cosinus, après `saut` échantillons de mise en route. */
double amplitude(const std::vector<int16_t> &s, int v, double f, size_t saut) {
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for (size_t i = saut; i < s.size() / 2; ++i) {
        const double ph = 2 * PI * f * i / FS;
        const double si = std::sin(ph), co = std::cos(ph), y = s[2 * i + v];
        ss += si * si; cc += co * co; sc += si * co; ys += y * si; yc += y * co;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    return std::hypot(a, b);
}

double db(double x) { return 20 * std::log10(x + 1e-12); }

/* Fréquence où retombe f après réduction à 48 kHz. */
double repliee(double f) {
    double r = std::fmod(f, FS);
    return r > FS / 2 ? FS - r : r;
}

/* Le même calcul en double précision, sans arrondi : la référence. */
struct Reference {
    std::vector<double> h1, h2;
    std::vector<double> x1, x2;
    double xp = 0, y = 0;
    static std::vector<double> deplier(const int16_t *c, size_t nc) {
        const size_t centre = 2 * nc - 1;
        std::vector<double> h(4 * nc - 1, 0.0);
        h[centre] = 0.5;
        for (size_t j = 0; j < nc; ++j)
            h[centre - (2 * j + 1)] = h[centre + (2 * j + 1)] = c[j] / 32768.0;
        return h;
    }
    Reference() : h1(deplier(son::ETAGE1, 4)), h2(deplier(son::ETAGE2, 14)) {}
    static double filtrer(std::vector<double> &x, const std::vector<double> &h, double a, double b) {
        x.push_back(a);
        x.push_back(b);
        double acc = 0;
        const size_t n = x.size();
        for (size_t k = 0; k < h.size(); ++k)
            acc += h[k] * (n >= h.size() - k ? x[n - h.size() + k] : 0.0);
        if (x.size() > 4096) x.erase(x.begin(), x.begin() + 2048);
        return acc;
    }
    double pousser(const uint16_t *e, int v) {
        auto in = [&](int i) { return (e[2 * i + v] - 2048.0) * 8.0; };
        const double a = filtrer(x1, h1, in(0), in(1));
        const double b = filtrer(x1, h1, in(2), in(3));
        const double x = filtrer(x2, h2, a, b);
        y = x - xp + (1.0 - 1.0 / 4096.0) * y;
        xp = x;
        const double s = 2 * y;
        return s > 32767 ? 32767 : (s < -32768 ? -32768 : s);
    }
};

void bande_utile() {
    double pire = 0, f_pire = 0;
    for (double f = 20.0; f <= 20000.0 * 1.0001; f *= std::pow(1000.0, 1.0 / 60.0)) {
        son::Decimation d;
        const auto s = passer(f, f, 48000 + 12000, d);
        const double g = db(amplitude(s, 0, f, 12000) / ATTENDU);
        if (std::fabs(g) > std::fabs(pire)) { pire = g; f_pire = f; }
        VERIFIER(std::fabs(g) <= 0.1, "bande utile : %.0f Hz → %+.3f dB", f, g);
    }
    std::printf("  bande utile 20 Hz–20 kHz : écart de gain au pire %+.3f dB (à %.0f Hz)\n",
                pire, f_pire);
}

void repliement() {
    double pire = -400, f_pire = 0, pire_inaudible = -400;
    for (double f = 20000.0; f <= 96000.0; f += 250.0) {
        const double fr = repliee(f);
        if (fr < 50.0) continue; /* retombe sur le continu, que le passe-haut retire */
        son::Decimation d;
        const auto s = passer(f, f, 24000, d);
        const double a = db(amplitude(s, 0, fr, 2400) / ATTENDU);
        if (f >= 28000.0 && fr <= 20000.0) {
            if (a > pire) { pire = a; f_pire = f; }
            VERIFIER(a <= -70.0, "%.0f Hz retombe à %.0f Hz à %.1f dB", f, fr, a);
        } else if (f > 24000.0 && a > pire_inaudible) {
            pire_inaudible = a;
        }
    }
    std::printf("  repliement de 28 à 96 kHz dans la bande utile : %.1f dB au pire (depuis %.0f Hz)\n",
                pire, f_pire);
    std::printf("  de 24 à 28 kHz et de 68 à 72 kHz, retombant entre 20 et 24 kHz : %.1f dB au pire\n",
                pire_inaudible);
}

void voies_independantes() {
    son::Decimation d;
    const auto s = passer(1000.0, 3000.0, 24000, d);
    const double fuite = db(amplitude(s, 1, 1000.0, 2400) / ATTENDU);
    VERIFIER(fuite < -90.0, "la voie 0 s'entend dans la voie 1 : %.1f dB", fuite);
    std::printf("  voie 0 dans la voie 1 : %.1f dB\n", fuite);
}

void continu() {
    son::Decimation d; /* milieu supposé 2048, l'étage d'entrée est à 2100 */
    std::vector<uint16_t> adc(2 * 4 * 96000, 2100);
    std::vector<int16_t> s(2 * 96000);
    d.traiter(adc.data(), 4 * 96000, s.data());
    /* 52 pas d'écart, 832 en sortie : la constante de temps est de 85 ms. */
    int pire = 0;
    for (size_t i = 48000; i < 96000; ++i)
        for (int v = 0; v < 2; ++v)
            pire = std::max(pire, std::abs((int)s[2 * i + v]));
    VERIFIER(pire <= 1, "continu restant après 1 s : %d", pire);
    std::printf("  continu (ADC au repos à 2100 au lieu de 2048, 832 en sortie) après 1 s : %d au plus\n",
                pire);
}

void decoupage() {
    std::mt19937 alea(7);
    std::vector<uint16_t> adc(2 * 4 * 6000);
    for (auto &x : adc) x = alea() % 4096;
    son::Decimation un, morceaux;
    std::vector<int16_t> a(2 * 6000), b(2 * 6000);
    un.traiter(adc.data(), 4 * 6000, a.data());
    size_t fait = 0;
    while (fait < 6000) {
        size_t n = 1 + alea() % 300;
        if (fait + n > 6000) n = 6000 - fait;
        morceaux.traiter(adc.data() + 2 * 4 * fait, 4 * n, b.data() + 2 * fait);
        fait += n;
    }
    size_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff += a[i] != b[i];
    VERIFIER(diff == 0, "le découpage des appels change %zu échantillons", diff);
    std::printf("  découpage des appels : sortie identique au bit près\n");
}

void contre_reference() {
    /* Bruit pleine échelle et ondes carrées pleine échelle : les pires cas
     * pour la virgule fixe. */
    std::mt19937 alea(11);
    const size_t n = 48000;
    std::vector<uint16_t> adc(2 * 4 * n);
    for (size_t k = 0; k < 4 * n; ++k) {
        adc[2 * k] = alea() % 4096;
        adc[2 * k + 1] = ((k / 96) % 2) ? 4095 : 0; /* carré à 1 kHz, 0 à 4095 */
    }
    son::Decimation d;
    std::vector<int16_t> s(2 * n);
    d.traiter(adc.data(), 4 * n, s.data());
    Reference r[2];
    double pire = 0;
    for (size_t i = 0; i < n; ++i)
        for (int v = 0; v < 2; ++v)
            pire = std::max(pire, std::fabs(s[2 * i + v] - r[v].pousser(&adc[8 * i], v)));
    VERIFIER(pire <= 8.0, "écart à la référence : %.1f", pire);
    std::printf("  bruit et carré pleine échelle, contre la référence en double : %.1f pas au pire\n",
                pire);
}

} // namespace

int main() {
    std::printf("Décimation 192 → 48 kHz\n");
    bande_utile();
    repliement();
    voies_independantes();
    continu();
    decoupage();
    contre_reference();
    std::printf("  calcul : %zu multiplications par échantillon de sortie et par voie, "
                "%.1f millions par seconde pour deux voies\n",
                (size_t)(2 * 4 + 14), (2 * 4 + 14) * 48000.0 * 2 / 1e6);
    if (echecs) {
        std::printf("ÉCHEC : %d vérification(s)\n", echecs);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
