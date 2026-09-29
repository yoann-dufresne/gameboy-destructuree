/**
 * Tête — placement et découpe d'une image source sur les rangées de l'écran
 *
 * La source envoie son image à SA taille ; la tête la centre dans le canevas,
 * puis découpe chaque tranche reçue en segments, un par rangée touchée
 * (plan §2.2 bis, §4.6). Un segment, c'est « n pixels à écrire à partir de la
 * position linéaire pos de la rangée r » — ce qu'un nœud sait faire sans rien
 * connaître de la source.
 *
 * Aucun octet de pixel n'est lu ni converti ici : la tête ne fait que compter
 * des positions. Les pixels gardent le format dans lequel ils sont arrivés.
 *
 * C++ portable, sans dépendance au SDK : éprouvé sur PC par test/test_decoupe.cpp.
 */
#pragma once

#include <cstdint>

namespace decoupe {

/* Bits par pixel d'un format PXL ; 0 s'il n'est pas découpable (RLE8, inconnu).
 * Les numéros de format sont communs à PXL1 et PXL2. */
constexpr uint8_t bits_par_pixel(uint8_t format) {
    switch (format) {
        case 0: return 24; /* BGR888 */
        case 1: return 16; /* RGB565 */
        case 2: return 2;  /* IDX2 */
        case 3: return 4;  /* IDX4 */
        case 4: return 8;  /* IDX8 */
        default: return 0;
    }
}

struct Canevas {
    uint16_t w, h;     /* tout l'écran */
    uint16_t rangee_h; /* hauteur d'une rangée, donc d'un nœud */
};

/* Une image source, placée dans le canevas. */
struct Geometrie {
    uint16_t w = 0, h = 0;   /* image source, en pixels */
    uint8_t format = 0;
    uint8_t bits = 0;
    uint16_t x0 = 0, y0 = 0; /* coin haut-gauche dans le canevas */
    uint32_t pas = 0;        /* octets par ligne source */
    uint32_t taille = 0;     /* octets par image */

    /* Vrai quand deux lignes successives de la source sont aussi jointives
     * dans le canevas : image de toute la largeur, sans bourrage en fin de
     * ligne. Un segment peut alors couvrir plusieurs lignes d'un coup. */
    bool contigue = false;

    bool operator==(const Geometrie &) const = default;
};

/* Centre une image w×h dans le canevas. Faux si elle n'y entre pas ou si son
 * format n'est pas découpable : la mise à l'échelle est le travail de la source. */
inline bool placer(const Canevas &c, uint16_t w, uint16_t h, uint8_t format,
                   Geometrie &g) {
    const uint8_t bits = bits_par_pixel(format);
    if (bits == 0 || w == 0 || h == 0 || w > c.w || h > c.h)
        return false;
    g.w = w;
    g.h = h;
    g.format = format;
    g.bits = bits;
    g.x0 = (uint16_t)((c.w - w) / 2);
    g.y0 = (uint16_t)((c.h - h) / 2);
    g.pas = ((uint32_t)w * bits + 7u) / 8u;
    g.taille = g.pas * h;
    g.contigue = (w == c.w) && (g.pas * 8u == (uint32_t)w * bits);
    return true;
}

/* Une tranche est recevable si elle tient dans l'image et ne coupe aucun pixel.
 * Sous l'octet, tout octet commence sur un pixel (8 est multiple de 2 et de 4) ;
 * au-delà, offset et longueur doivent être multiples de la taille d'un pixel. */
inline bool tranche_valide(const Geometrie &g, uint32_t offset, uint32_t lg) {
    if (offset > g.taille || lg > g.taille - offset)
        return false;
    if (g.bits >= 8) {
        const uint32_t opp = g.bits / 8u;
        if (offset % opp != 0 || lg % opp != 0)
            return false;
    }
    return true;
}

struct Segment {
    uint8_t rangee;  /* nœud destinataire */
    uint16_t pos;    /* premier pixel : y_local × canevas.w + x */
    uint16_t n;      /* nombre de pixels */
    uint32_t debut;  /* position du premier octet dans la tranche */
    uint32_t octets; /* longueur, en octets */
};

/* Découpe la tranche [offset, offset + lg) de l'image en segments, et appelle
 * emettre(const Segment &) pour chacun, dans l'ordre. Rend leur nombre.
 * Précondition : tranche_valide(g, offset, lg). */
template <typename F>
inline uint32_t decouper(const Canevas &c, const Geometrie &g, uint32_t offset,
                         uint32_t lg, F &&emettre) {
    uint32_t nb = 0;
    uint32_t pos = offset;
    const uint32_t fin = offset + lg;

    while (pos < fin) {
        const uint32_t ligne = pos / g.pas;       /* ligne source */
        const uint32_t px = (pos % g.pas) * 8u / g.bits; /* pixel dans la ligne */
        const uint32_t cy = g.y0 + ligne;         /* ligne du canevas */
        const uint32_t rangee = cy / c.rangee_h;

        /* Jusqu'où ce segment peut aller dans la source, en octets : la fin
         * de la ligne, ou — lignes jointives — la dernière ligne source qui
         * tombe encore dans cette rangée. */
        uint32_t limite;
        if (g.contigue) {
            uint32_t lignes = (rangee + 1u) * c.rangee_h - g.y0;
            if (lignes > g.h)
                lignes = g.h;
            limite = lignes * g.pas;
        } else {
            limite = (ligne + 1u) * g.pas;
        }

        const uint32_t octets = (fin < limite ? fin : limite) - pos;
        uint32_t n = octets * 8u / g.bits;
        if (!g.contigue && n > g.w - px)
            n = g.w - px; /* bourrage de fin de ligne, sous l'octet */

        const Segment s{
            (uint8_t)rangee,
            (uint16_t)((cy % c.rangee_h) * c.w + g.x0 + px),
            (uint16_t)n,
            pos - offset,
            octets,
        };
        emettre(s);
        ++nb;
        pos += octets;
    }
    return nb;
}

} // namespace decoupe
