/**
 * Éprouve la découpe de la tête sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I../include test_decoupe.cpp -o test_decoupe
 *     ./test_decoupe
 *
 * Principe : on fabrique une image source aléatoire, on l'emballe dans son
 * format, on la débite en tranches comme le ferait un émetteur, on découpe
 * chaque tranche, puis on rejoue les segments comme le feraient les nœuds :
 * chacun déballe ses pixels dans sa rangée. Le canevas reconstitué doit être
 * exactement l'image, centrée — et rien ne doit déborder d'une rangée.
 */

#include "decoupe.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using decoupe::Canevas;
using decoupe::Geometrie;
using decoupe::Segment;

int echecs = 0;

#define VERIFIER(cond, ...)                                  \
    do {                                                     \
        if (!(cond)) {                                       \
            if (++echecs <= 20) {                            \
                std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__); \
                std::printf(__VA_ARGS__);                    \
                std::printf("\n");                           \
            }                                                \
        }                                                    \
    } while (0)

/* Emballe des valeurs de pixels dans le format : pixel de gauche en poids
 * fort sous l'octet, octets dans l'ordre au-delà. Chaque ligne commence sur
 * un octet. */
std::vector<uint8_t> emballer(const Geometrie &g, const std::vector<uint32_t> &px) {
    std::vector<uint8_t> o(g.taille, 0);
    for (uint32_t y = 0; y < g.h; ++y) {
        for (uint32_t x = 0; x < g.w; ++x) {
            const uint32_t v = px[y * g.w + x];
            if (g.bits >= 8) {
                const uint32_t opp = g.bits / 8u;
                for (uint32_t k = 0; k < opp; ++k)
                    o[y * g.pas + x * opp + k] = (uint8_t)(v >> (8 * k));
            } else {
                const uint32_t bit = x * g.bits;
                const uint32_t dec = 8u - g.bits - (bit % 8u);
                o[y * g.pas + bit / 8u] |= (uint8_t)(v << dec);
            }
        }
    }
    return o;
}

/* Ce que fait un nœud d'un segment : déballer n pixels à partir de pos. */
void deballer(const Geometrie &g, const Segment &s, const uint8_t *d,
              std::vector<int64_t> &rangee) {
    for (uint32_t i = 0; i < s.n; ++i) {
        uint32_t v = 0;
        if (g.bits >= 8) {
            const uint32_t opp = g.bits / 8u;
            for (uint32_t k = 0; k < opp; ++k)
                v |= (uint32_t)d[i * opp + k] << (8 * k);
        } else {
            const uint32_t bit = i * g.bits;
            const uint32_t dec = 8u - g.bits - (bit % 8u);
            v = (d[bit / 8u] >> dec) & ((1u << g.bits) - 1u);
        }
        rangee[s.pos + i] = v;
    }
}

struct Bilan {
    uint32_t segments = 0;
    uint32_t tranches = 0;
};

/* Une image complète, débitée en tranches de `pas_tranche` octets au plus. */
Bilan eprouver(const Canevas &c, uint16_t w, uint16_t h, uint8_t format,
               uint32_t pas_tranche, std::mt19937 &rng, bool bavard = false) {
    Bilan b;
    Geometrie g;
    if (!decoupe::placer(c, w, h, format, g)) {
        VERIFIER(false, "placer() refuse %ux%u fmt %u", w, h, format);
        return b;
    }

    std::vector<uint32_t> px(w * h);
    std::uniform_int_distribution<uint32_t> tirage(0, (g.bits >= 32) ? ~0u : (1u << g.bits) - 1u);
    for (auto &v : px)
        v = tirage(rng);
    const std::vector<uint8_t> octets = emballer(g, px);

    const uint32_t nb_rangees = c.h / c.rangee_h;
    const uint32_t taille_rangee = (uint32_t)c.w * c.rangee_h;
    std::vector<std::vector<int64_t>> rangees(nb_rangees,
                                              std::vector<int64_t>(taille_rangee, -1));

    /* Un émetteur ne coupe jamais un pixel : multiple de la taille d'un pixel. */
    const uint32_t opp = g.bits >= 8 ? g.bits / 8u : 1u;
    pas_tranche -= pas_tranche % opp;

    for (uint32_t off = 0; off < g.taille; off += pas_tranche) {
        const uint32_t lg = std::min(pas_tranche, g.taille - off);
        VERIFIER(decoupe::tranche_valide(g, off, lg), "tranche %u+%u refusée", off, lg);
        ++b.tranches;

        uint32_t attendu = 0; /* debut du prochain segment dans la tranche */
        const uint32_t nb = decoupe::decouper(c, g, off, lg, [&](const Segment &s) {
            ++b.segments;
            VERIFIER(s.rangee < nb_rangees, "rangée %u hors canevas", s.rangee);
            VERIFIER(s.debut == attendu, "segment non jointif : %u au lieu de %u",
                     s.debut, attendu);
            VERIFIER(s.n > 0, "segment vide");
            VERIFIER((uint32_t)s.pos + s.n <= taille_rangee,
                     "déborde de la rangée : pos %u + n %u > %u", s.pos, s.n, taille_rangee);
            VERIFIER(s.n * g.bits <= s.octets * 8u, "plus de pixels que d'octets");
            attendu += s.octets;
            if (bavard)
                std::printf("    tranche %6u  → rangée %u  pos %5u  n %5u  octets %4u\n",
                            off, s.rangee, s.pos, s.n, s.octets);
            if (s.rangee < nb_rangees && (uint32_t)s.pos + s.n <= taille_rangee)
                deballer(g, s, octets.data() + off + s.debut, rangees[s.rangee]);
        });
        VERIFIER(attendu == lg, "les segments couvrent %u octets sur %u", attendu, lg);
        (void)nb;
    }

    /* Le canevas reconstitué : l'image centrée, et -1 (jamais écrit) ailleurs. */
    uint32_t faux = 0;
    for (uint32_t cy = 0; cy < c.h; ++cy) {
        for (uint32_t cx = 0; cx < c.w; ++cx) {
            const int64_t v = rangees[cy / c.rangee_h][(cy % c.rangee_h) * c.w + cx];
            const bool dedans = cx >= g.x0 && cx < g.x0 + w && cy >= g.y0 && cy < g.y0 + h;
            const int64_t attendu = dedans ? (int64_t)px[(cy - g.y0) * w + (cx - g.x0)] : -1;
            if (v != attendu)
                ++faux;
        }
    }
    VERIFIER(faux == 0, "%ux%u fmt %u tranches de %u : %u pixels faux",
             w, h, format, pas_tranche, faux);
    return b;
}

} // namespace

int main() {
    std::mt19937 rng(20260929);
    const Canevas c{192, 192, 64};

    std::printf("Cas nominaux, tranches de 1400 octets (1398 en BGR888) :\n");
    struct Cas { uint16_t w, h; uint8_t fmt; const char *nom; };
    const Cas nominaux[] = {
        {192, 192, 4, "192x192 IDX8   : pixelpush plein cadre"},
        {192, 192, 0, "192x192 BGR888"},
        {192, 192, 2, "192x192 IDX2"},
        {160, 144, 2, "160x144 IDX2   : Game Boy, centree"},
        {160, 144, 4, "160x144 IDX8"},
        {64, 64, 0,   " 64x64  BGR888 : la dalle de test"},
    };
    for (const Cas &k : nominaux) {
        const Bilan b = eprouver(c, k.w, k.h, k.fmt, 1400, rng);
        std::printf("  %-40s %3u tranches → %4u segments\n", k.nom, b.tranches, b.segments);
    }

    /* Lignes jointives : une tranche plein cadre ne doit se couper qu'à la
     * frontière d'une rangée, jamais à chaque ligne. */
    {
        const Bilan b = eprouver(c, 192, 192, 4, 1400, rng);
        VERIFIER(b.segments <= b.tranches + 2,
                 "plein cadre : %u segments pour %u tranches", b.segments, b.tranches);
    }

    std::printf("\nDétail : Game Boy, première tranche\n");
    {
        Geometrie g;
        decoupe::placer(c, 160, 144, 2, g);
        std::printf("    placement (%u,%u), pas %u o, %u o par image\n",
                    g.x0, g.y0, g.pas, g.taille);
        /* Le plan le fixe : marges de 16 et 24 px (§4.2). */
        VERIFIER(g.x0 == 16 && g.y0 == 24, "Game Boy placée en (%u,%u)", g.x0, g.y0);
        VERIFIER(g.pas == 40 && g.taille == 5760, "Game Boy : pas %u, taille %u",
                 g.pas, g.taille);
        uint32_t n = 0;
        decoupe::decouper(c, g, 0, 1400, [&](const Segment &s) {
            if (n++ < 3)
                std::printf("    rangée %u  pos %5u (y %2u, x %3u)  n %3u  octets %u\n",
                            s.rangee, s.pos, s.pos / c.w, s.pos % c.w, s.n, s.octets);
        });
        std::printf("    … %u segments pour 1400 octets (une ligne = 40 o)\n", n);
    }

    /* Plusieurs canevas : 192 px de large n'a jamais de bourrage en fin de
     * ligne (192 est multiple de 4), il faut des largeurs impaires pour
     * éprouver le cas « pleine largeur mais lignes non jointives ». */
    std::printf("\nBalayage aléatoire : canevas, géométries, formats, tailles de tranche :\n");
    const Canevas canevas[] = {c, {64, 64, 64}, {65, 64, 32}, {63, 60, 20}, {6, 9, 3}};
    const uint8_t formats[] = {0, 1, 2, 3, 4};
    uint32_t essais = 0;
    for (const Canevas &cv : canevas) {
        for (int i = 0; i < 1000; ++i) {
            const uint16_t w = (uint16_t)(1 + rng() % cv.w);
            const uint16_t h = (uint16_t)(1 + rng() % cv.h);
            const uint8_t fmt = formats[rng() % 5];
            const uint32_t pas = 3 + rng() % 1600;
            eprouver(cv, (i % 4 == 0) ? cv.w : w, h, fmt, pas, rng);
            ++essais;
        }
    }
    /* Les bords : largeur pleine avec bourrage (IDX2, 191 px), une seule
     * ligne, une seule colonne, tranches d'un octet. */
    eprouver(c, 191, 192, 2, 1400, rng);
    eprouver(c, 192, 1, 0, 1400, rng);
    eprouver(c, 1, 192, 2, 1, rng);
    eprouver(c, 192, 192, 3, 1, rng);
    std::printf("  %u images, tous formats\n", essais + 4);

    /* Refus attendus. */
    Geometrie g;
    VERIFIER(!decoupe::placer(c, 193, 10, 4, g), "193 px de large accepté");
    VERIFIER(!decoupe::placer(c, 10, 10, 5, g), "RLE8 accepté");
    VERIFIER(!decoupe::placer(c, 0, 10, 4, g), "largeur nulle acceptée");
    decoupe::placer(c, 192, 192, 0, g);
    VERIFIER(!decoupe::tranche_valide(g, 1, 3), "pixel BGR888 coupé accepté");
    VERIFIER(!decoupe::tranche_valide(g, 0, 4), "longueur non multiple de 3 acceptée");
    VERIFIER(!decoupe::tranche_valide(g, g.taille - 3, 6), "débordement accepté");
    VERIFIER(decoupe::tranche_valide(g, g.taille, 0), "tranche vide finale refusée");

    std::printf("\n%s — %d échec(s)\n", echecs ? "ÉCHEC" : "OK", echecs);
    return echecs ? 1 : 0;
}
