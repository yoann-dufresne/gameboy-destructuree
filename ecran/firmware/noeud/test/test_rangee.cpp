/**
 * Éprouve la chaîne tête → nœud sur PC, sans carte ni nappe.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I../include -I../../commun \
 *         -I../../tete/include test_rangee.cpp -o test_rangee && ./test_rangee
 *
 * Côté tête : placement et découpe réels (decoupe.hpp), messages encodés
 * comme sur la nappe (liaison.h), HELLO, PALETTE, EFFACER, PIXELS, VALIDER.
 * Les mots passent par un anneau qui boucle, lu par le même Lecteur que le
 * nœud. Côté nœud : la vraie Rangee. À chaque VALIDER, le tampon validé doit
 * être la rangée attendue, au pixel près — palette appliquée, marges noires,
 * et coupée à la largeur de la dalle.
 */

#include "decoupe.hpp"
#include "liaison.h"
#include "rangee.hpp"

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

/* Un nœud simulé : l'anneau que remplirait le DMA, le lecteur, la rangée. */
struct Noeud {
    static constexpr uint32_t ANNEAU = 4096; /* petit : il boucle souvent */
    std::vector<uint32_t> anneau = std::vector<uint32_t>(ANNEAU);
    uint32_t ecriture = 0;
    liaison::Lecteur lecteur{anneau.data(), ANNEAU};
    std::vector<uint8_t> t0, t1;
    rangee::Rangee r;
    uint32_t erreurs = 0;
    int validations = 0;
    std::vector<uint8_t> derniere; /* copie du dernier tampon validé */

    Noeud(uint16_t w, uint16_t h)
        : t0((size_t)w * h * 3), t1((size_t)w * h * 3), r(w, h, t0.data(), t1.data()) {}

    /* Ce que ferait la nappe : les mots arrivent dans l'anneau. */
    void recevoir(const uint32_t *mots, uint32_t nb) {
        for (uint32_t i = 0; i < nb; ++i)
            anneau[(ecriture + i) % ANNEAU] = mots[i];
        ecriture = (ecriture + nb) % ANNEAU;
        traiter();
    }

    /* Ce que fait la boucle principale du nœud. */
    void traiter() {
        uint32_t msg[liaison::MOTS_MAX];
        liaison::Entete e;
        while (true) {
            const auto res = lecteur.lire(ecriture, msg, e);
            if (res == liaison::Lecteur::Resultat::RIEN)
                return;
            if (res == liaison::Lecteur::Resultat::ERREUR) {
                ++erreurs;
                lecteur.recaler(ecriture); /* la vraie réception relance son PIO */
                return;
            }
            const auto effet = r.appliquer(e, reinterpret_cast<const uint8_t *>(msg) +
                                                  sizeof(liaison::Entete));
            if (effet == rangee::Effet::VALIDATION) {
                ++validations;
                const int b = r.valide();
                derniere.assign(b ? t1.begin() : t0.begin(), b ? t1.end() : t0.end());
            }
        }
    }
};

/* La tête simulée : même découpe que le firmware, même encodage. */
struct Tete {
    decoupe::Canevas c;
    std::vector<Noeud *> noeuds;
    uint8_t image = 0;
    uint8_t tampon = 0;
    int corrompre = -1; /* index du message PIXELS à abîmer, -1 : aucun */
    int nb_pixels_msg = 0;

    void envoyer(uint8_t k, liaison::Entete e, const void *charge) {
        uint32_t mots[liaison::MOTS_MAX];
        const uint32_t nb = liaison::encoder(e, charge, mots);
        if (e.type == liaison::PIXELS && nb_pixels_msg++ == corrompre)
            mots[nb - 1] ^= 0x00010000u; /* un bit de la charge ou du bourrage */
        noeuds[k]->recevoir(mots, nb);
    }

    void a_tous(uint8_t type, const void *charge, uint16_t lg) {
        for (uint8_t k = 0; k < noeuds.size(); ++k) {
            liaison::Entete e{};
            e.type = type;
            e.lg = lg;
            envoyer(k, e, charge);
        }
    }

    void hello() {
        for (uint8_t k = 0; k < noeuds.size(); ++k) {
            liaison::Hello h{c.w, c.rangee_h, k, liaison::VERSION, 0};
            liaison::Entete e{};
            e.type = liaison::HELLO;
            e.lg = sizeof(h);
            envoyer(k, e, &h);
        }
    }

    /* Une image : tranches de `pas` octets, découpées puis relayées. */
    void image_complete(const decoupe::Geometrie &g, const std::vector<uint8_t> &octets,
                        uint32_t pas) {
        std::vector<uint32_t> pixels(noeuds.size(), 0);
        const uint32_t opp = g.bits >= 8 ? g.bits / 8u : 1u;
        pas -= pas % opp;
        for (uint32_t off = 0; off < g.taille; off += pas) {
            const uint32_t lg = std::min(pas, g.taille - off);
            decoupe::decouper(c, g, off, lg, [&](const decoupe::Segment &s) {
                liaison::Entete e{};
                e.type = liaison::PIXELS;
                e.image = image;
                e.lg = (uint16_t)s.octets;
                e.pos = s.pos;
                e.n = s.n;
                e.format = g.format;
                e.tampon = tampon;
                pixels[s.rangee] += s.n;
                envoyer(s.rangee, e, octets.data() + off + s.debut);
            });
        }
        for (uint8_t k = 0; k < noeuds.size(); ++k) {
            liaison::Valider v{pixels[k]};
            liaison::Entete e{};
            e.type = liaison::VALIDER;
            e.image = image;
            e.lg = sizeof(v);
            e.tampon = tampon;
            envoyer(k, e, &v);
        }
        ++image;
        tampon ^= 1u;
    }
};

std::vector<uint8_t> emballer(const decoupe::Geometrie &g, const std::vector<uint32_t> &px) {
    std::vector<uint8_t> o(g.taille, 0);
    for (uint32_t y = 0; y < g.h; ++y)
        for (uint32_t x = 0; x < g.w; ++x) {
            const uint32_t v = px[y * g.w + x];
            if (g.bits == 24) {
                o[y * g.pas + x * 3 + 0] = (uint8_t)v;
                o[y * g.pas + x * 3 + 1] = (uint8_t)(v >> 8);
                o[y * g.pas + x * 3 + 2] = (uint8_t)(v >> 16);
            } else if (g.bits == 8) {
                o[y * g.pas + x] = (uint8_t)v;
            } else {
                const uint32_t bit = x * 2;
                o[y * g.pas + bit / 8] |= (uint8_t)(v << (6 - bit % 8));
            }
        }
    return o;
}

/* B, G, R attendus d'un pixel source. */
void couleur(const decoupe::Geometrie &g, uint32_t v, const std::vector<uint8_t> &pal,
             uint8_t out[3]) {
    if (g.bits == 24) {
        out[0] = (uint8_t)v;
        out[1] = (uint8_t)(v >> 8);
        out[2] = (uint8_t)(v >> 16);
    } else {
        out[0] = pal[v * 3 + 0];
        out[1] = pal[v * 3 + 1];
        out[2] = pal[v * 3 + 2];
    }
}

struct Banc {
    decoupe::Canevas c;
    uint16_t dalle_w; /* largeur de la dalle de chaque nœud, ≤ canevas */
};

/* Une séquence d'images de géométries variées, sur un banc donné. */
void eprouver(const Banc &banc, std::mt19937 &rng, int nb_images, bool bavard) {
    const uint32_t nb_noeuds = banc.c.h / banc.c.rangee_h;
    std::vector<Noeud> noeuds;
    noeuds.reserve(nb_noeuds);
    for (uint32_t k = 0; k < nb_noeuds; ++k)
        noeuds.emplace_back(banc.dalle_w, banc.c.rangee_h);
    Tete t{banc.c, {}};
    for (auto &n : noeuds)
        t.noeuds.push_back(&n);

    t.hello();
    std::vector<uint8_t> pal(768);
    for (auto &v : pal)
        v = (uint8_t)rng();
    t.a_tous(liaison::PALETTE, pal.data(), 768);

    const uint8_t formats[] = {0, 2, 4};
    decoupe::Geometrie g{};
    for (int i = 0; i < nb_images; ++i) {
        if (i % 3 == 0) { /* nouvelle géométrie toutes les trois images */
            const uint8_t fmt = formats[rng() % 3];
            const uint16_t w = (i % 6 == 0) ? banc.c.w : (uint16_t)(1 + rng() % banc.c.w);
            const uint16_t h = (uint16_t)(1 + rng() % banc.c.h);
            decoupe::placer(banc.c, w, h, fmt, g);
            t.a_tous(liaison::EFFACER, nullptr, 0);
        }
        std::vector<uint32_t> px((size_t)g.w * g.h);
        const uint32_t max = g.bits == 24 ? 0xFFFFFFu : (1u << g.bits) - 1u;
        for (auto &v : px)
            v = rng() % (max + 1u);
        const auto octets = emballer(g, px);
        const int valides_avant = noeuds[0].validations;
        t.image_complete(g, octets, 100 + rng() % 1301);

        for (uint32_t k = 0; k < nb_noeuds; ++k) {
            Noeud &n = noeuds[k];
            VERIFIER(n.erreurs == 0, "nœud %u : %u erreurs de lecture", k, n.erreurs);
            VERIFIER(n.r.valide() >= 0, "nœud %u, image %d : pas de validation", k, i);
            uint32_t faux = 0;
            for (uint32_t y = 0; y < banc.c.rangee_h; ++y)
                for (uint32_t x = 0; x < banc.dalle_w; ++x) {
                    const uint32_t cy = k * banc.c.rangee_h + y;
                    uint8_t att[3] = {0, 0, 0};
                    if (x >= g.x0 && x < g.x0 + g.w && cy >= g.y0 && cy < g.y0 + g.h)
                        couleur(g, px[(cy - g.y0) * g.w + (x - g.x0)], pal, att);
                    const uint8_t *o = &n.derniere[(y * banc.dalle_w + x) * 3];
                    if (o[0] != att[0] || o[1] != att[1] || o[2] != att[2])
                        ++faux;
                }
            VERIFIER(faux == 0, "banc %ux%u dalle %u, image %d (%ux%u fmt %u), nœud %u : "
                     "%u pixels faux", banc.c.w, banc.c.h, banc.dalle_w, i, g.w, g.h,
                     g.format, k, faux);
            n.r.prendre_valide(); /* le VSYNC */
        }
        (void)valides_avant;
    }
    if (bavard)
        for (uint32_t k = 0; k < nb_noeuds; ++k) {
            const auto &s = noeuds[k].r.stats();
            std::printf("    nœud %u : %5u messages, %6u pixels (%u hors champ), "
                        "%d validations, %u refus\n", k, s.messages, s.pixels, s.hors_champ,
                        noeuds[k].validations, s.refus);
        }
}

} // namespace

int main() {
    std::mt19937 rng(20260929);

    std::printf("Écran 3×3 : canevas 192×192, trois nœuds de 192 px :\n");
    eprouver({{192, 192, 64}, 192}, rng, 60, true);

    std::printf("\nBanc de la phase 5b : canevas 64×64, un nœud, une dalle :\n");
    eprouver({{64, 64, 64}, 64}, rng, 60, true);

    std::printf("\nBanc, canevas 192×192 sur trois nœuds de 64 px (tiers gauche) :\n");
    eprouver({{192, 192, 64}, 64}, rng, 60, true);

    /* Un bit abîmé en route : le CRC le voit, le message est perdu, et le
     * VALIDER doit être refusé faute de pixels — jamais une image fausse. */
    std::printf("\nMessage corrompu :\n");
    {
        Noeud n(64, 64);
        Tete t{{64, 64, 64}, {&n}};
        t.hello();
        decoupe::Geometrie g;
        decoupe::placer(t.c, 64, 64, 4, g);
        std::vector<uint8_t> octets(g.taille, 7);
        t.corrompre = 1;
        t.image_complete(g, octets, 1400);
        std::printf("    erreurs de lecture %u, validation %s, refus %u\n", n.erreurs,
                    n.r.valide() >= 0 ? "ACCORDÉE" : "refusée", n.r.stats().refus);
        VERIFIER(n.erreurs == 1, "corruption non vue par le CRC");
        VERIFIER(n.r.valide() < 0, "image incomplète validée");
        /* L'image suivante, intacte, passe. */
        t.corrompre = -1;
        t.image_complete(g, octets, 1400);
        VERIFIER(n.r.valide() >= 0, "l'image suivante n'est pas validée");
    }

    /* Vecteur de référence du CRC-16/CCITT-FALSE. */
    const uint8_t ref[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    VERIFIER(liaison::crc16(0xFFFF, ref, 9) == 0x29B1, "CRC de référence faux : %04X",
             liaison::crc16(0xFFFF, ref, 9));

    std::printf("\n%s — %d échec(s)\n", echecs ? "ÉCHEC" : "OK", echecs);
    return echecs ? 1 : 0;
}
