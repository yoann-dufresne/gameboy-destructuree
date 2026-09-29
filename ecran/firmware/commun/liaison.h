/**
 * Liaison tête → nœud — format des messages (plan §4.6)
 *
 * Interne à l'écran : aucune source ne le voit. Un message = un en-tête de
 * 12 octets, une charge utile, du bourrage jusqu'au mot de 32 bits. Sur la
 * nappe, les mots se suivent sans séparateur : c'est l'en-tête qui donne la
 * longueur, et le CRC qui dit si l'on est resté aligné.
 *
 * C++ portable, sans dépendance au SDK : la tête, le nœud et les tests sur PC
 * l'incluent tel quel.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace liaison {

constexpr uint8_t VERSION = 1;

enum Type : uint8_t {
    PIXELS     = 1, /* n pixels au format donné, à partir de la position pos */
    VALIDER    = 2, /* l'image est complète côté tête : charge Valider */
    PALETTE    = 3, /* 256 × B,G,R */
    LUMINOSITE = 4, /* 1 octet, luminosité de base 1..255 */
    EFFACER    = 5, /* les deux tampons au noir : la géométrie a changé */
    HELLO      = 6, /* charge Hello : qui est ce nœud, quelle géométrie */
};

struct __attribute__((packed)) Entete {
    uint8_t type;
    uint8_t image;   /* numéro d'image attribué par la tête */
    uint16_t lg;     /* octets de charge utile, bourrage non compris */
    uint16_t pos;    /* PIXELS : premier pixel, y × largeur du canevas + x */
    uint16_t n;      /* PIXELS : nombre de pixels */
    uint8_t format;  /* PIXELS : format PXL — BGR888, IDX2 ou IDX8 */
    uint8_t tampon;  /* PIXELS, VALIDER : tampon de réception du nœud, 0 ou 1 */
    uint16_t crc;    /* CRC-16/CCITT de l'en-tête (crc à 0), puis de la charge */
};
static_assert(sizeof(Entete) == 12, "en-tete de liaison non compact");

struct __attribute__((packed)) Hello {
    uint16_t canevas_w; /* largeur d'une ligne : pos = y × canevas_w + x */
    uint16_t rangee_h;  /* hauteur d'une rangée */
    uint8_t rangee;     /* numéro de ce nœud : son port sur la tête */
    uint8_t version;
    uint16_t reserve;
};

struct __attribute__((packed)) Valider {
    /* Pixels envoyés à CE nœud pour l'image. Le nœud compare avec ce qu'il a
     * reçu : un message perdu en route se voit ici, même sans erreur de CRC. */
    uint32_t pixels;
};

constexpr uint32_t CHARGE_MAX = 1400; /* une tranche WiFi au plus */
constexpr uint32_t ENTETE_MOTS = sizeof(Entete) / 4;

constexpr uint32_t mots(uint32_t lg) { return (uint32_t)(sizeof(Entete) + lg + 3u) / 4u; }
constexpr uint32_t MOTS_MAX = mots(CHARGE_MAX);

/* ------------------------------------------------------------------ CRC */

/* CRC-16/CCITT-FALSE : polynôme 0x1021, départ 0xFFFF. Table calculée à la
 * compilation. */
struct TableCrc {
    uint16_t v[256];
};

constexpr TableCrc calculer_table_crc() {
    TableCrc t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; ++b)
            c = (uint16_t)((c & 0x8000u) ? (c << 1) ^ 0x1021u : (c << 1));
        t.v[i] = c;
    }
    return t;
}

inline constexpr TableCrc TABLE_CRC = calculer_table_crc();

inline uint16_t crc16(uint16_t crc, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; ++i)
        crc = (uint16_t)((crc << 8) ^ TABLE_CRC.v[((crc >> 8) ^ d[i]) & 0xFFu]);
    return crc;
}

/* ------------------------------------------------------ écrire, vérifier */

/* Écrit le message complet dans `sortie` : en-tête et son CRC, charge,
 * bourrage à zéro. Rend le nombre de mots, soit mots(e.lg). */
inline uint32_t encoder(Entete e, const void *charge, uint32_t *sortie) {
    const uint32_t nb = mots(e.lg);
    sortie[nb - 1] = 0; /* bourrage */
    e.crc = 0;
    uint8_t *o = reinterpret_cast<uint8_t *>(sortie);
    std::memcpy(o, &e, sizeof(e));
    if (e.lg && charge)
        std::memcpy(o + sizeof(e), charge, e.lg);
    const uint16_t crc = crc16(0xFFFF, o, sizeof(e) + e.lg);
    std::memcpy(o + offsetof(Entete, crc), &crc, sizeof(crc));
    return nb;
}

/* Premier filtre, avant même d'attendre la charge : un type inconnu ou une
 * longueur absurde trahissent une perte d'alignement. */
inline bool entete_plausible(const Entete &e) {
    return e.type >= PIXELS && e.type <= HELLO && e.lg <= CHARGE_MAX;
}

/* Vérifie le CRC d'un message complet, lu à plat. */
inline bool verifier(const uint32_t *message, Entete &e) {
    const uint8_t *o = reinterpret_cast<const uint8_t *>(message);
    std::memcpy(&e, o, sizeof(e));
    Entete z = e;
    z.crc = 0;
    uint16_t crc = crc16(0xFFFF, reinterpret_cast<const uint8_t *>(&z), sizeof(z));
    crc = crc16(crc, o + sizeof(e), e.lg);
    return crc == e.crc;
}

/* --------------------------------------------------- lire dans un anneau */

/* Lit les messages d'un anneau de mots que remplit un DMA. `taille` est une
 * puissance de 2 ; `ecriture` est la position (en mots, modulo taille) où le
 * DMA écrira le prochain mot. */
class Lecteur {
public:
    enum class Resultat { RIEN, MESSAGE, ERREUR };

    Lecteur(const volatile uint32_t *anneau, uint32_t taille)
        : anneau_(anneau), masque_(taille - 1) {}

    /* Extrait le prochain message complet dans `dest` (MOTS_MAX mots).
     * ERREUR : en-tête impossible ou CRC faux — l'alignement est perdu, il
     * faut resynchroniser la réception puis appeler recaler(). */
    Resultat lire(uint32_t ecriture, uint32_t *dest, Entete &e) {
        const uint32_t dispo = (ecriture - lecture_) & masque_;
        if (dispo < ENTETE_MOTS)
            return Resultat::RIEN;
        copier(dest, ENTETE_MOTS);
        std::memcpy(&e, dest, sizeof(e));
        if (!entete_plausible(e))
            return Resultat::ERREUR;
        const uint32_t nb = mots(e.lg);
        if (dispo < nb)
            return Resultat::RIEN;
        copier(dest, nb);
        lecture_ = (lecture_ + nb) & masque_;
        return verifier(dest, e) ? Resultat::MESSAGE : Resultat::ERREUR;
    }

    /* Oublie tout ce qui n'a pas été lu. */
    void recaler(uint32_t ecriture) { lecture_ = ecriture & masque_; }

    uint32_t position() const { return lecture_; }

private:
    void copier(uint32_t *dest, uint32_t nb) const {
        for (uint32_t i = 0; i < nb; ++i)
            dest[i] = anneau_[(lecture_ + i) & masque_];
    }

    const volatile uint32_t *anneau_;
    uint32_t masque_;
    uint32_t lecture_ = 0;
};

} // namespace liaison
