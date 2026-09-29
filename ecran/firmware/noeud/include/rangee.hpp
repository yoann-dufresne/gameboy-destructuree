/**
 * Nœud — reconstruction d'une rangée à partir des messages de la tête
 *
 * Deux tampons de réception BGR888, à la taille de ce que la dalle affiche.
 * La tête écrit une image dans l'un pendant que l'autre attend son VSYNC ;
 * c'est elle qui choisit le tampon, et elle ne touche jamais à celui qu'elle
 * a validé tant que le VSYNC n'est pas passé (plan §2.6, §4.6).
 *
 * La géométrie vient du HELLO : les positions sont exprimées dans la largeur
 * du CANEVAS, et le nœud ne garde que ce qui tombe sur sa dalle. Au banc, une
 * dalle de 64 px sous un canevas de 192 montre ainsi le tiers gauche.
 *
 * C++ portable, sans dépendance au SDK : éprouvé sur PC par
 * test/test_rangee.cpp, de la découpe de la tête jusqu'aux tampons.
 */
#pragma once

#include <cstdint>
#include <cstring>

#include "liaison.h"

namespace rangee {

struct Stats {
    uint32_t messages;
    uint32_t pixels;         /* pixels reçus, hors champ compris */
    uint32_t hors_champ;     /* pixels au-delà de la dalle : ignorés */
    uint32_t sans_hello;     /* PIXELS reçus avant le HELLO : ignorés */
    uint32_t format_inconnu;
    uint32_t validations;    /* VALIDER acceptés */
    uint32_t refus;          /* VALIDER refusés : pixels manquants */
    uint32_t effacements;
    uint32_t hellos;
};

enum class Effet { AUCUN, VALIDATION, LUMINOSITE, EFFACEMENT, HELLO };

class Rangee {
public:
    /* w × h : ce que la dalle affiche. Tampons de w × h × 3 octets chacun. */
    Rangee(uint16_t w, uint16_t h, uint8_t *tampon0, uint8_t *tampon1)
        : w_(w), h_(h), tampons_{tampon0, tampon1} {
        for (int i = 0; i < 256; ++i)
            palette_[i * 3 + 0] = palette_[i * 3 + 1] = palette_[i * 3 + 2] = (uint8_t)i;
        effacer();
    }

    Effet appliquer(const liaison::Entete &e, const uint8_t *charge) {
        stats_.messages++;
        switch (e.type) {
        case liaison::PIXELS:
            pixels(e, charge);
            return Effet::AUCUN;
        case liaison::VALIDER:
            return valider(e, charge);
        case liaison::PALETTE:
            std::memcpy(palette_, charge, e.lg < sizeof(palette_) ? e.lg : sizeof(palette_));
            return Effet::AUCUN;
        case liaison::LUMINOSITE:
            if (e.lg < 1)
                return Effet::AUCUN;
            luminosite_ = charge[0] ? charge[0] : 1;
            return Effet::LUMINOSITE;
        case liaison::EFFACER:
            effacer();
            stats_.effacements++;
            return Effet::EFFACEMENT;
        case liaison::HELLO: {
            if (e.lg < sizeof(liaison::Hello))
                return Effet::AUCUN;
            liaison::Hello h;
            std::memcpy(&h, charge, sizeof(h));
            const bool nouveau = !hello_ || h.canevas_w != canevas_w_ || h.rangee != numero_;
            canevas_w_ = h.canevas_w;
            rangee_h_ = h.rangee_h;
            numero_ = h.rangee;
            hello_ = true;
            stats_.hellos++;
            return nouveau ? Effet::HELLO : Effet::AUCUN;
        }
        default:
            return Effet::AUCUN;
        }
    }

    /* Tampon validé qui attend son VSYNC, ou -1. */
    int valide() const { return valide_; }

    /* Rend le tampon validé et oublie la validation : appelé au VSYNC. */
    const uint8_t *prendre_valide() {
        if (valide_ < 0)
            return nullptr;
        const uint8_t *t = tampons_[valide_];
        valide_ = -1;
        return t;
    }

    uint8_t luminosite() const { return luminosite_; }
    bool configuree() const { return hello_; }
    uint8_t numero() const { return numero_; }
    uint16_t canevas_w() const { return canevas_w_; }
    uint16_t rangee_h() const { return rangee_h_; }
    const Stats &stats() const { return stats_; }

private:
    void effacer() {
        std::memset(tampons_[0], 0, (size_t)w_ * h_ * 3);
        std::memset(tampons_[1], 0, (size_t)w_ * h_ * 3);
        for (int b = 0; b < 2; ++b) {
            image_[b] = -1;
            recus_[b] = 0;
        }
        valide_ = -1;
    }

    void pixels(const liaison::Entete &e, const uint8_t *d) {
        const uint8_t b = e.tampon & 1u;
        if (image_[b] != e.image) {
            image_[b] = e.image; /* première tranche de cette image ici */
            recus_[b] = 0;
        }
        recus_[b] += e.n;
        stats_.pixels += e.n;
        if (!hello_) {
            stats_.sans_hello++;
            return;
        }

        uint8_t *t = tampons_[b];
        uint32_t x = e.pos % canevas_w_;
        uint32_t y = e.pos / canevas_w_;
        for (uint32_t i = 0; i < e.n; ++i) {
            const uint8_t *c; /* B, G, R */
            uint8_t bgr[3];
            switch (e.format) {
            case 0: /* BGR888 */
                c = d + i * 3;
                break;
            case 4: /* IDX8 */
                c = &palette_[d[i] * 3];
                break;
            case 2: { /* IDX2 : pixel de gauche en poids fort */
                const uint8_t v = (uint8_t)((d[i >> 2] >> (6 - 2 * (i & 3))) & 3u);
                c = &palette_[v * 3];
                break;
            }
            default:
                stats_.format_inconnu++;
                return;
            }
            if (x < w_ && y < h_) {
                bgr[0] = c[0];
                bgr[1] = c[1];
                bgr[2] = c[2];
                std::memcpy(&t[(y * w_ + x) * 3], bgr, 3);
            } else {
                stats_.hors_champ++;
            }
            if (++x == canevas_w_) {
                x = 0;
                ++y;
            }
        }
    }

    Effet valider(const liaison::Entete &e, const uint8_t *charge) {
        if (e.lg < sizeof(liaison::Valider))
            return Effet::AUCUN;
        liaison::Valider v;
        std::memcpy(&v, charge, sizeof(v));
        const uint8_t b = e.tampon & 1u;
        /* Une rangée que l'image ne touche pas n'a rien reçu, et rien n'était
         * attendu : c'est une validation légitime. */
        const uint32_t recus = (image_[b] == e.image) ? recus_[b] : 0;
        if (recus != v.pixels) {
            stats_.refus++;
            valide_ = -1;
            return Effet::AUCUN;
        }
        stats_.validations++;
        valide_ = b;
        return Effet::VALIDATION;
    }

    uint16_t w_, h_;
    uint8_t *tampons_[2];
    int image_[2];
    uint32_t recus_[2];
    int valide_ = -1;

    bool hello_ = false;
    uint16_t canevas_w_ = 0;
    uint16_t rangee_h_ = 0;
    uint8_t numero_ = 0;

    uint8_t palette_[256 * 3];
    uint8_t luminosite_ = 0;
    Stats stats_{};
};

} // namespace rangee
