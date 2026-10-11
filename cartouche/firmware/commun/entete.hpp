/**
 * L'en-tête d'une cartouche Game Boy, et la signature qui la reconnaît.
 *
 * Partagé par le lecteur, qui vérifie sa lecture (logo, sommes de contrôle),
 * et par la doublure, qui reconnaît une cartouche déjà vue sans recopier sa
 * ROM. C++20 portable, sans pico-sdk : testé sur PC (test/test_mbc.cpp).
 *
 * Référence : Pan Docs, « The Cartridge Header »
 * (https://gbdev.io/pandocs/The_Cartridge_Header.html).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cartouche {

/* ---- CRC-32 (IEEE 802.3, celui de zlib et de la base No-Intro) ---------- */

namespace detail {
constexpr uint32_t crc_entree(uint32_t n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    return c;
}
struct TableCrc {
    uint32_t t[256];
    constexpr TableCrc() : t{} {
        for (uint32_t n = 0; n < 256; ++n) t[n] = crc_entree(n);
    }
};
inline constexpr TableCrc table_crc{};
} // namespace detail

/* Poursuit un CRC : crc32(0, a) puis crc32(ce résultat, b) == crc32(0, a+b). */
inline uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = detail::table_crc.t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* ---- Le logo, que la console vérifie au démarrage ------------------------ */

inline constexpr uint8_t LOGO[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

inline constexpr uint16_t ADR_LOGO = 0x0104;
inline constexpr uint16_t ADR_TITRE = 0x0134;
inline constexpr uint16_t ADR_CGB = 0x0143;
inline constexpr uint16_t ADR_TYPE = 0x0147;
inline constexpr uint16_t ADR_TAILLE_ROM = 0x0148;
inline constexpr uint16_t ADR_TAILLE_RAM = 0x0149;
inline constexpr uint16_t ADR_SOMME_ENTETE = 0x014D;
inline constexpr uint16_t ADR_SOMME_GLOBALE = 0x014E; /* big-endian, 2 octets */
inline constexpr uint16_t FIN_ENTETE = 0x0150;        /* tout l'en-tête est avant */

/* ---- Le type de cartouche (octet 0147) ----------------------------------- */

enum class Mbc : uint8_t {
    Aucun,          /* ROM seule, avec ou sans RAM */
    Mbc1,
    Mbc2,
    Mbc3,
    Mbc5,
    NonPrisEnCharge /* MMM01, MBC6, MBC7, caméra, TAMA5, HuC1, HuC3… (plan §1) */
};

struct Type {
    Mbc mbc = Mbc::NonPrisEnCharge;
    bool ram = false;      /* RAM externe (MBC2 : sa RAM interne, voir ram_mbc2) */
    bool pile = false;     /* la RAM et l'horloge survivent hors tension */
    bool horloge = false;  /* MBC3 + TIMER */
    bool vibreur = false;  /* MBC5 + RUMBLE */
};

inline Type decoder_type(uint8_t code) {
    Type t;
    switch (code) {
    case 0x00: t.mbc = Mbc::Aucun; break;
    case 0x08: t.mbc = Mbc::Aucun; t.ram = true; break;
    case 0x09: t.mbc = Mbc::Aucun; t.ram = true; t.pile = true; break;
    case 0x01: t.mbc = Mbc::Mbc1; break;
    case 0x02: t.mbc = Mbc::Mbc1; t.ram = true; break;
    case 0x03: t.mbc = Mbc::Mbc1; t.ram = true; t.pile = true; break;
    case 0x05: t.mbc = Mbc::Mbc2; t.ram = true; break;
    case 0x06: t.mbc = Mbc::Mbc2; t.ram = true; t.pile = true; break;
    case 0x0F: t.mbc = Mbc::Mbc3; t.horloge = true; t.pile = true; break;
    case 0x10: t.mbc = Mbc::Mbc3; t.horloge = true; t.ram = true; t.pile = true; break;
    case 0x11: t.mbc = Mbc::Mbc3; break;
    case 0x12: t.mbc = Mbc::Mbc3; t.ram = true; break;
    case 0x13: t.mbc = Mbc::Mbc3; t.ram = true; t.pile = true; break;
    case 0x19: t.mbc = Mbc::Mbc5; break;
    case 0x1A: t.mbc = Mbc::Mbc5; t.ram = true; break;
    case 0x1B: t.mbc = Mbc::Mbc5; t.ram = true; t.pile = true; break;
    case 0x1C: t.mbc = Mbc::Mbc5; t.vibreur = true; break;
    case 0x1D: t.mbc = Mbc::Mbc5; t.vibreur = true; t.ram = true; break;
    case 0x1E: t.mbc = Mbc::Mbc5; t.vibreur = true; t.ram = true; t.pile = true; break;
    default: break; /* NonPrisEnCharge */
    }
    return t;
}

/* Taille de la ROM d'après l'octet 0148 ; 0 si le code est inconnu. Les codes
 * 52 à 54 des documents non officiels ne sont portés par aucune cartouche
 * connue (Pan Docs) : refusés. */
inline uint32_t taille_rom(uint8_t code) {
    return code <= 0x08 ? (32u * 1024u) << code : 0u;
}

/* Taille de la RAM d'après l'octet 0149 et le type. Le MBC2 annonce 0 mais
 * porte 512 demi-octets : on les range un par octet, poids faible. */
inline constexpr uint32_t RAM_MBC2 = 512;
inline uint32_t taille_ram(uint8_t code, const Type &t) {
    if (t.mbc == Mbc::Mbc2) return RAM_MBC2;
    if (!t.ram) return 0;
    switch (code) {
    case 0x02: return 8u * 1024u;
    case 0x03: return 32u * 1024u;
    case 0x04: return 128u * 1024u;
    case 0x05: return 64u * 1024u;
    default: return 0; /* 0x01, « 2 Kio », n'a jamais existé ; 0 : pas de RAM */
    }
}

/* ---- Sommes de contrôle -------------------------------------------------- */

/* Celle que vérifie la console au démarrage (014D), sur 0134–014C. */
inline uint8_t calculer_somme_entete(const uint8_t *rom) {
    uint8_t s = 0;
    for (uint16_t a = ADR_TITRE; a <= 0x014C; ++a) s = (uint8_t)(s - rom[a] - 1);
    return s;
}

/* 014E–014F : tous les octets de la ROM, sauf ces deux-là. La console ne la
 * vérifie pas ; elle vérifie ici la lecture entière. */
inline uint16_t calculer_somme_globale(const uint8_t *rom, size_t taille) {
    uint16_t s = 0;
    for (size_t i = 0; i < taille; ++i)
        if (i != ADR_SOMME_GLOBALE && i != ADR_SOMME_GLOBALE + 1u) s = (uint16_t)(s + rom[i]);
    return s;
}

inline bool logo_juste(const uint8_t *rom) {
    return std::memcmp(rom + ADR_LOGO, LOGO, sizeof LOGO) == 0;
}

/* ---- L'en-tête, lu ------------------------------------------------------- */

struct Entete {
    char titre[16];        /* tel quel, complété de zéros ; 0143 inclus */
    uint8_t cgb = 0;       /* 0x80 : compatible, 0xC0 : Game Boy Color seulement */
    uint8_t code_type = 0;
    uint8_t code_rom = 0;
    uint8_t code_ram = 0;
    uint8_t somme_entete = 0;
    uint16_t somme_globale = 0;
    Type type;
    uint32_t octets_rom = 0;
    uint32_t octets_ram = 0;
};

/* `rom` doit couvrir au moins 0000–014F. */
inline Entete lire_entete(const uint8_t *rom) {
    Entete e{};
    std::memcpy(e.titre, rom + ADR_TITRE, 16);
    e.cgb = rom[ADR_CGB];
    e.code_type = rom[ADR_TYPE];
    e.code_rom = rom[ADR_TAILLE_ROM];
    e.code_ram = rom[ADR_TAILLE_RAM];
    e.somme_entete = rom[ADR_SOMME_ENTETE];
    e.somme_globale = (uint16_t)((rom[ADR_SOMME_GLOBALE] << 8) | rom[ADR_SOMME_GLOBALE + 1]);
    e.type = decoder_type(e.code_type);
    e.octets_rom = taille_rom(e.code_rom);
    e.octets_ram = taille_ram(e.code_ram, e.type);
    return e;
}

/* Le verdict sur l'en-tête, dans l'ordre où le lecteur le donne (plan §4.3). */
enum class Verdict : uint8_t {
    Bon,
    LogoFaux,         /* contacts sales, ou pas de cartouche */
    SommeFausse,      /* idem : la console refuserait de démarrer */
    NonPrisEnCharge,  /* type ou taille hors du périmètre du plan §1 */
};

inline Verdict juger_entete(const uint8_t *rom) {
    if (!logo_juste(rom)) return Verdict::LogoFaux;
    if (calculer_somme_entete(rom) != rom[ADR_SOMME_ENTETE]) return Verdict::SommeFausse;
    const Entete e = lire_entete(rom);
    if (e.type.mbc == Mbc::NonPrisEnCharge || e.octets_rom == 0) return Verdict::NonPrisEnCharge;
    return Verdict::Bon;
}

inline bool couleur_seulement(const Entete &e) { return e.cgb == 0xC0; }

/* ---- La signature : ce qui reconnaît une cartouche (plan §5.3, §7) -------
 *
 * Elle dit « même jeu, même ROM », pas « même exemplaire » : deux cartouches
 * du même jeu ont la même signature. C'est la sauvegarde qui les distingue
 * (voir lecteur.hpp, la sauvegarde parquée). */
struct Signature {
    char titre[16] = {};
    uint8_t code_type = 0;
    uint8_t code_rom = 0;
    uint8_t code_ram = 0;
    uint8_t somme_entete = 0;
    uint16_t somme_globale = 0;
    uint32_t crc_rom = 0;
    uint32_t octets_rom = 0;

    bool operator==(const Signature &o) const {
        return std::memcmp(titre, o.titre, 16) == 0 && code_type == o.code_type &&
               code_rom == o.code_rom && code_ram == o.code_ram &&
               somme_entete == o.somme_entete && somme_globale == o.somme_globale &&
               crc_rom == o.crc_rom && octets_rom == o.octets_rom;
    }
    bool operator!=(const Signature &o) const { return !(*this == o); }
};

inline Signature signer(const uint8_t *rom, size_t taille) {
    const Entete e = lire_entete(rom);
    Signature s;
    std::memcpy(s.titre, e.titre, 16);
    s.code_type = e.code_type;
    s.code_rom = e.code_rom;
    s.code_ram = e.code_ram;
    s.somme_entete = e.somme_entete;
    s.somme_globale = e.somme_globale;
    s.crc_rom = crc32(0, rom, taille);
    s.octets_rom = (uint32_t)taille;
    return s;
}

} // namespace cartouche
