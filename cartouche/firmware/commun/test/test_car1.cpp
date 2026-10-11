/**
 * Éprouve l'emballage des paquets CAR1 sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_car1.cpp -o /tmp/test_car1 && /tmp/test_car1
 *
 * Les échanges eux-mêmes sont éprouvés de bout en bout par simulation.cpp ;
 * ici, seulement ce qu'un paquet abîmé ou étranger doit provoquer : rien.
 */

#include "car1.h"
#include "paquet.hpp"

#include <cstdio>
#include <cstring>

using namespace cartouche;

namespace {

int echecs = 0, verifications = 0;

#define VERIFIER(cond, ...)                                                  \
    do {                                                                     \
        ++verifications;                                                     \
        if (!(cond)) {                                                       \
            ++echecs;                                                        \
            std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__);              \
            std::printf(__VA_ARGS__);                                        \
            std::printf("\n");                                               \
        }                                                                    \
    } while (0)

} // namespace

int main() {
    const uint32_t app = 0x12345678;
    car1_accuse a{};
    a.session = 0xCAFE;
    a.numero = 42;
    a.sort = CAR1_PARQUEE;
    std::vector<uint8_t> p = emballer(CAR1_ACCUSE, app, 7, &a, sizeof a);

    VERIFIER(p.size() == CAR1_ENTETE + sizeof a, "taille");
    VERIFIER(p[0] == 'C' && p[1] == 'A' && p[2] == 'R' && p[3] == '1', "nombre magique lisible en clair");

    car1_entete e;
    size_t lg = 0;
    const uint8_t *c = deballer(p.data(), p.size(), app, e, lg);
    car1_accuse b{};
    VERIFIER(c && lg == sizeof a && extraire(c, lg, b), "déballé");
    VERIFIER(e.type == CAR1_ACCUSE && e.insertion == 7 && b.session == 0xCAFE && b.numero == 42 &&
                 b.sort == CAR1_PARQUEE,
             "champs relus");

    VERIFIER(!deballer(p.data(), p.size(), app + 1, e, lg), "un autre appairage est ignoré");
    VERIFIER(!deballer(p.data(), p.size() - 1, app, e, lg), "un paquet tronqué est ignoré");
    auto q = p;
    q.push_back(0);
    VERIFIER(!deballer(q.data(), q.size(), app, e, lg), "un paquet trop long est ignoré");
    q = p;
    q[0] = 'X';
    VERIFIER(!deballer(q.data(), q.size(), app, e, lg), "un nombre magique faux est ignoré");
    q = p;
    q[5] = CAR1_VERSION + 1;
    VERIFIER(!deballer(q.data(), q.size(), app, e, lg), "une autre version est ignorée");
    VERIFIER(!deballer(p.data(), 3, app, e, lg), "trois octets ne font pas un paquet");

    /* Le plus gros paquet tient sous la MTU : pas de fragmentation IP. */
    const size_t ecriture_max = CAR1_ENTETE + sizeof(car1_ecriture) + CAR1_PAGES_MAX * (2 + CAR1_OCTETS_PAGE);
    const size_t bloc_max = CAR1_ENTETE + sizeof(car1_bloc) + CAR1_OCTETS_BLOC;
    VERIFIER(ecriture_max <= 1472 && bloc_max <= 1472, "sous la MTU : %zu et %zu octets", ecriture_max, bloc_max);

    if (echecs) {
        std::printf("test_car1 : %d échec(s) sur %d vérifications\n", echecs, verifications);
        return 1;
    }
    std::printf("test_car1 : %d vérifications, toutes justes\n", verifications);
    return 0;
}
