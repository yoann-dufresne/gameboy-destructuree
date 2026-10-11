/**
 * Éprouve la réserve et le rattrapage du haut-parleur sur PC — aucune carte.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_rattrapage.cpp -o test_rattrapage
 *     ./test_rattrapage
 *
 * On simule ce que vivra le haut-parleur : une source dont l'horloge avance
 * ou retarde de quelques centaines de millionièmes, qui envoie un bloc de
 * 240 échantillons toutes les 5 ms ; un réseau qui retarde chaque bloc au
 * hasard, parfois beaucoup, en perd quelques-uns, se coupe, ou voit la
 * source redémarrer. Le haut-parleur joue au rythme de sa propre horloge,
 * par paquets de 1 ms, comme le fera la sortie I2S.
 *
 * La source joue un sinus à 1 kHz. Ce qui sort doit rester un sinus : un
 * échantillon jeté ou doublé s'y verrait comme un pic dans le « résidu »
 * y[n+1] − 2 cos(ω) y[n] + y[n−1], nul pour un sinus pur. Les critères :
 *
 *   - aucun claquement : résidu sous 0,2 % de l'amplitude ;
 *   - la réserve ne se vide ni ne déborde, sur 2 heures (T5.2) ;
 *   - la dérive estimée est la dérive simulée, à quelques ppm près ;
 *   - un trou (perte, coupure) est masqué sans saut brutal.
 */

#include "rattrapage.hpp"

#include <cmath>
#include <cstdio>
#include <queue>
#include <random>
#include <vector>

namespace {

int echecs = 0;

#define VERIFIER(cond, ...)                                          \
    do {                                                             \
        if (!(cond)) {                                               \
            if (++echecs <= 30) {                                    \
                std::printf("    ECHEC %s:%d  ", __FILE__, __LINE__); \
                std::printf(__VA_ARGS__);                            \
                std::printf("\n");                                   \
            }                                                        \
        }                                                            \
    } while (0)

constexpr double FS = 48000.0;
constexpr double PI = 3.14159265358979323846;
constexpr double A = 16384.0;    /* −6 dB */
constexpr double F = 1000.0;     /* le sinus, en Hz de la source */
constexpr uint32_t BLOC = 240;
constexpr uint32_t PAQUET_SORTIE = 48; /* 1 ms d'I2S */
constexpr uint32_t CIBLE = 1920;       /* 40 ms */

struct Scenario {
    const char *nom;
    double derive_ppm;
    double duree_s;
    double gigue_ms;        /* retard ajouté, au hasard de 0 à gigue */
    double pointes;         /* proportion de blocs très retardés */
    double pointe_ms;       /* jusqu'à */
    double perte;           /* proportion de blocs perdus */
    double coupure_t = -1;  /* le réseau se coupe à t (s)… */
    double coupure_s = 0;   /* …pendant tant de secondes */
    double redemarrage_t = -1; /* la numérotation saute à t (s) */
    bool propre = true;     /* aucun trou attendu : résidu et zéro masquage */
};

struct Arrivee {
    double t; /* en trames de sortie */
    uint32_t premier;
    uint64_t index; /* échantillon de la source, sans tour */
    bool operator>(const Arrivee &o) const { return t > o.t; }
};

void jouer(const Scenario &s) {
    std::mt19937_64 alea(1234);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    son::Rattrapage r(CIBLE);
    std::priority_queue<Arrivee, std::vector<Arrivee>, std::greater<Arrivee>> reseau;

    const double rapport = 1.0 + s.derive_ppm * 1e-6; /* trames de source par trame jouée */
    const uint64_t total = (uint64_t)(s.duree_s * FS);
    const double echauffement = std::min(120.0, s.duree_s / 4) * FS;
    uint32_t depart = 0xFFF00000u; /* la numérotation passe par zéro après 22 s */
    uint64_t prochain = 0;         /* prochain bloc à émettre */
    std::vector<int16_t> bloc(2 * BLOC), sortie(2 * PAQUET_SORTIE);

    double y_1[2] = {0, 0}, y_2[2] = {0, 0};
    double residu_max = 0, saut_max = 0, niveau_min = 1e9, niveau_max = -1e9;
    double somme_derive = 0;
    uint64_t n_derive = 0;
    son::Rattrapage::Compteurs au_debut{};
    bool compte_pris = false;
    uint64_t perdus = 0;

    for (uint64_t t = 0; t < total; t += PAQUET_SORTIE) {
        /* La source émet chaque bloc quand il est complet, à son horloge. */
        while ((prochain + 1) * BLOC / rapport <= t + PAQUET_SORTIE) {
            const double emis = (prochain + 1) * BLOC / rapport;
            const double ts = emis / FS;
            if (s.redemarrage_t >= 0 && ts >= s.redemarrage_t && depart == 0xFFF00000u)
                depart += 0x80000000u; /* la source a redémarré ailleurs */
            double retard = 96.0 + u(alea) * s.gigue_ms * 48.0;
            if (u(alea) < s.pointes) retard += u(alea) * s.pointe_ms * 48.0;
            const bool coupe = s.coupure_t >= 0 && ts >= s.coupure_t &&
                               ts < s.coupure_t + s.coupure_s;
            if (coupe || u(alea) < s.perte) {
                ++perdus;
            } else {
                reseau.push({emis + retard, (uint32_t)(depart + prochain * BLOC),
                             prochain * BLOC});
            }
            ++prochain;
        }
        while (!reseau.empty() && reseau.top().t <= t) {
            const Arrivee a = reseau.top();
            reseau.pop();
            for (uint32_t i = 0; i < BLOC; ++i) {
                const double ph = 2 * PI * F * (double)(a.index + i) / FS;
                bloc[2 * i] = (int16_t)std::lround(A * std::sin(ph));
                bloc[2 * i + 1] = (int16_t)std::lround(A * std::cos(ph));
            }
            r.deposer(a.premier, bloc.data(), BLOC);
        }

        const float pas = r.pas();
        r.produire(sortie.data(), PAQUET_SORTIE);

        const double w = 2 * PI * F / FS * pas;
        const double c2 = 2 * std::cos(w);
        const bool compter = t >= echauffement;
        for (uint32_t i = 0; i < PAQUET_SORTIE; ++i)
            for (int v = 0; v < 2; ++v) {
                const double y = sortie[2 * i + v];
                if (compter) {
                    residu_max = std::max(residu_max, std::fabs(y - c2 * y_1[v] + y_2[v]));
                    saut_max = std::max(saut_max, std::fabs(y - y_1[v]));
                }
                y_2[v] = y_1[v];
                y_1[v] = y;
            }
        if (!compter) continue;
        if (!compte_pris) {
            au_debut = r.compteurs();
            compte_pris = true;
        }
        if (r.joue()) {
            niveau_min = std::min(niveau_min, r.niveau());
            niveau_max = std::max(niveau_max, r.niveau());
        }
        if (t > total * 3 / 4) {
            somme_derive += r.derive_ppm();
            ++n_derive;
        }
    }

    const auto &c = r.compteurs();
    const uint32_t masquees = c.masquees - au_debut.masquees;
    const double derive = somme_derive / std::max<uint64_t>(n_derive, 1);
    const double saut_sinus = 2 * PI * F / FS * A; /* la plus grande pente du sinus */

    std::printf("  %s\n", s.nom);
    std::printf("    dérive simulée %+.0f ppm, estimée %+.1f ppm ; réserve de %.1f à %.1f ms "
                "(cible %.0f) ; résidu au pire %.1f pas (%.3f %%) ; plus grand saut %.0f %% "
                "de la pente du sinus\n",
                s.derive_ppm, derive, niveau_min / 48.0, niveau_max / 48.0, CIBLE / 48.0,
                residu_max, 100 * residu_max / A, 100 * saut_max / saut_sinus);
    std::printf("    blocs perdus %llu ; trames masquées %u ; trop tard %u ; débordements %u ; "
                "reprises %u\n",
                (unsigned long long)perdus, masquees, c.trop_tard - au_debut.trop_tard,
                c.debordements - au_debut.debordements, c.reprises - au_debut.reprises);

    VERIFIER(std::fabs(derive - s.derive_ppm) <= 5.0, "dérive estimée %.1f au lieu de %.0f",
             derive, s.derive_ppm);
    /* Aucun saut au-delà de la pente naturelle du sinus, trous compris. */
    VERIFIER(saut_max <= 1.05 * saut_sinus, "saut de %.0f pas, la pente du sinus est %.0f",
             saut_max, saut_sinus);
    if (s.propre) {
        VERIFIER(residu_max <= 0.002 * A, "claquement : résidu de %.1f pas", residu_max);
        VERIFIER(masquees == 0, "%u trames masquées sans perte", masquees);
        VERIFIER(niveau_min > 0 && niveau_max < son::Rattrapage::CAPACITE,
                 "réserve hors bornes : %.0f à %.0f trames", niveau_min, niveau_max);
        VERIFIER(c.reprises == au_debut.reprises && c.debordements == au_debut.debordements,
                 "reprise ou débordement sans raison");
    }
}

} // namespace

int main() {
    std::printf("Réserve et rattrapage du haut-parleur\n");
    /* nom, dérive, durée, gigue, pointes, pointe, perte */
    jouer({"sans dérive, gigue de 0 à 10 ms, 10 min", 0, 600, 10, 0, 0, 0});
    jouer({"+300 ppm, gigue de 0 à 20 ms, 1 bloc sur 1000 retardé jusqu'à 10 ms de plus, 30 min",
           300, 1800, 20, 0.001, 10, 0});
    jouer({"−300 ppm, même réseau, 30 min", -300, 1800, 20, 0.001, 10, 0});
    jouer({"+500 ppm, 15 min", 500, 900, 20, 0, 0, 0});
    jouer({"−500 ppm, 15 min", -500, 900, 20, 0, 0, 0});
    jouer({"T5.2 : 2 heures à +100 ppm, gigue de 0 à 20 ms", 100, 7200, 20, 0.001, 10, 0});

    /* Des retards plus longs que la réserve : les blocs concernés arrivent
     * après leur heure. C'est ce que T4.2 évite en dimensionnant la réserve
     * sur le pire bloc sur mille. */
    Scenario pointes{"1 bloc sur 1000 retardé jusqu'à 40 ms de plus que la gigue, +100 ppm, 10 min",
                     100, 600, 20, 0.001, 40, 0};
    pointes.propre = false;
    jouer(pointes);

    Scenario pertes{"0,5 % de blocs perdus, +200 ppm, 10 min", 200, 600, 20, 0, 0, 0.005};
    pertes.propre = false;
    jouer(pertes);

    Scenario coupure{"le réseau se coupe 300 ms à t = 150 s, +50 ppm, 5 min", 50, 300, 10, 0, 0, 0};
    coupure.coupure_t = 150;
    coupure.coupure_s = 0.3;
    coupure.propre = false;
    jouer(coupure);

    Scenario redemarrage{"la source redémarre à t = 150 s, 5 min", 50, 300, 10, 0, 0, 0};
    redemarrage.redemarrage_t = 150;
    redemarrage.propre = false;
    jouer(redemarrage);

    if (echecs) {
        std::printf("ÉCHEC : %d vérification(s)\n", echecs);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
