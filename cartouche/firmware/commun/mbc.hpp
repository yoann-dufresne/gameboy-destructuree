/**
 * Les contrôleurs de banques (MBC) des cartouches Game Boy.
 *
 * Un seul modèle sert les deux moitiés du module :
 *   - la doublure l'imite : une écriture dans 0000–7FFF change un registre,
 *     et chaque lecture se traduit en une adresse dans sa copie ;
 *   - le lecteur le pilote : `selection_rom()`, `selection_ram()`… donnent les
 *     écritures qui amènent une vraie cartouche dans l'état voulu.
 * Les tests vérifient que les deux sens s'accordent (test/test_mbc.cpp).
 *
 * Pris en charge (plan §1) : sans MBC, MBC1 (et le câblage MBC1M des
 * compilations), MBC2, MBC3 avec ou sans horloge (et MBC30), MBC5 avec ou sans
 * vibreur.
 *
 * Références : Pan Docs, pages MBC1, MBC2, MBC3, MBC5 (https://gbdev.io/pandocs/).
 * Là où elles se taisent, le comportement suit celui de l'émulateur SameBoy,
 * réputé fidèle au matériel ; chaque cas est signalé « (SameBoy) ».
 */
#pragma once

#include "entete.hpp"

#include <cstdint>

namespace cartouche {

/* ======================================================================== */
/* L'horloge du MBC3                                                         */
/* ======================================================================== */

/* Registres de l'horloge, sélectionnés par 08 à 0C dans 4000–5FFF. */
enum : uint8_t { RTC_S = 0, RTC_M = 1, RTC_H = 2, RTC_DL = 3, RTC_DH = 4, RTC_NB = 5 };
inline constexpr uint8_t RTC_ARRET = 0x40;   /* DH bit 6 : horloge arrêtée */
inline constexpr uint8_t RTC_RETENUE = 0x80; /* DH bit 7 : le compteur de jours a débordé */

struct Horloge {
    uint8_t reel[RTC_NB] = {};     /* les compteurs qui tournent */
    uint8_t verrou[RTC_NB] = {};   /* la copie figée que le jeu lit */
    uint32_t sous_seconde_ms = 0;  /* remis à zéro quand on écrit les secondes (SameBoy) */
    uint8_t dernier_verrou = 0xFF; /* pour la séquence 00 puis 01 */

    bool arretee() const { return reel[RTC_DH] & RTC_ARRET; }

    /* Une seconde. Les valeurs hors plage (secondes 60 à 63…) comptent jusqu'au
     * débordement de leurs bits puis repartent de 0, sans retenue : modèle
     * courant des émulateurs, non vérifié sur une vraie cartouche. */
    void seconde() {
        if (arretee()) return;
        uint8_t &s = reel[RTC_S], &m = reel[RTC_M], &h = reel[RTC_H];
        s = (uint8_t)((s + 1) & 0x3F);
        if (s != 60) return;
        s = 0;
        m = (uint8_t)((m + 1) & 0x3F);
        if (m != 60) return;
        m = 0;
        h = (uint8_t)((h + 1) & 0x1F);
        if (h != 24) return;
        h = 0;
        uint16_t j = (uint16_t)(reel[RTC_DL] | ((reel[RTC_DH] & 1u) << 8));
        j = (uint16_t)((j + 1) & 0x1FF);
        reel[RTC_DL] = (uint8_t)j;
        reel[RTC_DH] = (uint8_t)((reel[RTC_DH] & ~1u) | (j >> 8));
        if (j == 0) reel[RTC_DH] |= RTC_RETENUE; /* reste levée jusqu'à ce que le jeu l'efface */
    }

    void avancer_ms(uint32_t ms) {
        if (arretee()) return;
        sous_seconde_ms += ms;
        while (sous_seconde_ms >= 1000) {
            sous_seconde_ms -= 1000;
            seconde();
        }
    }

    /* Écriture 6000–7FFF : 00 puis 01 fige l'heure courante dans le verrou. */
    void ecrire_verrou(uint8_t v) {
        if (dernier_verrou == 0x00 && v == 0x01)
            for (int i = 0; i < RTC_NB; ++i) verrou[i] = reel[i];
        dernier_verrou = v;
    }

    /* Les bits qui existent : 6 pour S et M, 5 pour H, 8 pour DL, 3 pour DH (SameBoy). */
    static uint8_t masque(uint8_t r) {
        static constexpr uint8_t m[RTC_NB] = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};
        return m[r];
    }
    uint8_t lire(uint8_t r) const { return (uint8_t)(verrou[r] & masque(r)); }

    /* Le jeu règle l'heure : il écrit les compteurs, pas le verrou (SameBoy). */
    void ecrire(uint8_t r, uint8_t v) {
        reel[r] = (uint8_t)(v & masque(r));
        if (r == RTC_S) sous_seconde_ms = 0;
    }
};

/* ======================================================================== */
/* Le contrôleur                                                             */
/* ======================================================================== */

/* Ce qu'une adresse de A000–BFFF atteint. */
struct AccesRam {
    enum Genre : uint8_t { Rien, Ram, Horloge } genre = Rien;
    uint32_t decalage = 0; /* Ram : position dans la RAM */
    uint8_t registre = 0;  /* Horloge : RTC_S…RTC_DH */
};

struct Config {
    Mbc mbc = Mbc::Aucun;
    uint32_t octets_rom = 32 * 1024;
    uint32_t octets_ram = 0;
    bool horloge = false;
    bool vibreur = false;
    bool mbc1m = false; /* câblage des compilations MBC1 de 1 Mio */
    bool mbc30 = false; /* MBC3 à 8 bits de banque ROM et 8 banques de RAM */

    static Config depuis(const Entete &e, bool mbc1m = false) {
        Config c;
        c.mbc = e.type.mbc;
        c.octets_rom = e.octets_rom;
        c.octets_ram = e.octets_ram;
        c.horloge = e.type.horloge;
        c.vibreur = e.type.vibreur;
        c.mbc1m = mbc1m && c.mbc == Mbc::Mbc1;
        /* Le MBC30 ne se reconnaît qu'à ses tailles : plus de 2 Mio, ou 64 Kio de RAM. */
        c.mbc30 = c.mbc == Mbc::Mbc3 && (c.octets_rom > 2u * 1024u * 1024u || c.octets_ram > 32u * 1024u);
        return c;
    }
};

class Controleur {
  public:
    explicit Controleur(const Config &c = Config{}) : c_(c) { reinitialiser(); }

    const Config &config() const { return c_; }

    /* L'état à la mise sous tension. Une vraie cartouche y revient chaque fois
     * que la fente du lecteur est coupée : ses registres ne survivent pas. */
    void reinitialiser() {
        ram_active_ = false;
        bas_ = 0;
        haut_ = 0;
        mode_ = 0;
        mbc5_bas_ = 1;
        mbc5_bit8_ = 0;
        selection_ = 0;
        vibreur_ = false;
        horloge.dernier_verrou = 0xFF;
    }

    /* Écriture dans 0000–7FFF. */
    void ecrire_registre(uint16_t adr, uint8_t v) {
        switch (c_.mbc) {
        case Mbc::Aucun:
        case Mbc::NonPrisEnCharge:
            return;
        case Mbc::Mbc1:
            if (adr < 0x2000) ram_active_ = (v & 0x0F) == 0x0A;
            else if (adr < 0x4000) bas_ = v & 0x1F;
            else if (adr < 0x6000) haut_ = v & 0x03;
            else mode_ = v & 0x01;
            return;
        case Mbc::Mbc2:
            if (adr >= 0x4000) return;
            if (adr & 0x0100) bas_ = v & 0x0F; /* bit 8 de l'adresse : la banque */
            else ram_active_ = (v & 0x0F) == 0x0A;
            return;
        case Mbc::Mbc3:
            if (adr < 0x2000) ram_active_ = (v & 0x0F) == 0x0A;
            else if (adr < 0x4000) bas_ = v & (c_.mbc30 ? 0xFF : 0x7F);
            else if (adr < 0x6000) selection_ = v;
            else if (c_.horloge) horloge.ecrire_verrou(v);
            return;
        case Mbc::Mbc5:
            /* Pan Docs : le vrai MBC5 ne regarde que les 4 bits bas (0x1A active). */
            if (adr < 0x2000) ram_active_ = (v & 0x0F) == 0x0A;
            else if (adr < 0x3000) mbc5_bas_ = v;
            else if (adr < 0x4000) mbc5_bit8_ = v & 0x01;
            else if (adr < 0x6000) {
                if (c_.vibreur) {
                    vibreur_ = (v & 0x08) != 0; /* bit 3 : le moteur, plus la RAM */
                    selection_ = v & 0x07;
                } else {
                    selection_ = v & 0x0F;
                }
            }
            return;
        }
    }

    /* Lecture dans 0000–7FFF : la position dans la ROM. */
    uint32_t decalage_rom(uint16_t adr) const {
        const uint32_t banque = adr < 0x4000 ? banque_basse() : banque_haute();
        return (banque & masque_rom()) * 0x4000u + (adr & 0x3FFFu);
    }

    /* Lecture ou écriture dans A000–BFFF. */
    AccesRam acces_ram(uint16_t adr) const {
        AccesRam a;
        const bool sans_registre = c_.mbc == Mbc::Aucun; /* ROM+RAM : toujours active */
        if (!sans_registre && !ram_active_) return a;     /* bus ouvert, souvent FF */
        switch (c_.mbc) {
        case Mbc::Mbc2:
            a.genre = AccesRam::Ram;
            a.decalage = adr & 0x01FFu; /* 512 demi-octets, en écho sur A000–BFFF */
            return a;
        case Mbc::Mbc3:
            if (selection_ >= 0x08) {
                if (c_.horloge && selection_ <= 0x0C) {
                    a.genre = AccesRam::Horloge;
                    a.registre = (uint8_t)(selection_ - 0x08);
                }
                return a;
            }
            if (selection_ > (c_.mbc30 ? 7 : 3)) return a; /* FF (SameBoy) */
            break;
        default:
            break;
        }
        if (c_.octets_ram == 0) return a;
        a.genre = AccesRam::Ram;
        a.decalage = (banque_ram() & masque_ram()) * 0x2000u + (adr & 0x1FFFu);
        a.decalage &= c_.octets_ram - 1u; /* une RAM de 2 Kio se répète (inexistante, Pan Docs) */
        return a;
    }

    bool ram_active() const { return ram_active_; }
    bool vibreur_actif() const { return vibreur_; }

    Horloge horloge;

  private:
    uint32_t banque_basse() const {
        if (c_.mbc == Mbc::Mbc1 && mode_) return (uint32_t)haut_ << (c_.mbc1m ? 4 : 5);
        return 0;
    }
    uint32_t banque_haute() const {
        switch (c_.mbc) {
        case Mbc::Aucun:
        case Mbc::NonPrisEnCharge:
            return 1;
        case Mbc::Mbc1: {
            /* La règle 0 → 1 regarde les 5 bits du registre, même en MBC1M. */
            const uint32_t bas = c_.mbc1m ? (bas_ & 0x0Fu) : bas_;
            uint32_t b = ((uint32_t)haut_ << (c_.mbc1m ? 4 : 5)) | bas;
            if ((bas_ & 0x1F) == 0) b |= 1;
            return b;
        }
        case Mbc::Mbc2:
            return bas_ ? bas_ : 1u;
        case Mbc::Mbc3:
            return bas_ ? bas_ : 1u;
        case Mbc::Mbc5:
            return (uint32_t)mbc5_bas_ | ((uint32_t)mbc5_bit8_ << 8); /* 0 donne bien 0 */
        }
        return 1;
    }
    uint32_t banque_ram() const {
        switch (c_.mbc) {
        case Mbc::Mbc1: return (mode_ && !c_.mbc1m) ? haut_ : 0u;
        case Mbc::Mbc3:
        case Mbc::Mbc5: return selection_;
        default: return 0;
        }
    }
    uint32_t masque_rom() const {
        const uint32_t n = c_.octets_rom / 0x4000u;
        return n ? n - 1u : 0u;
    }
    uint32_t masque_ram() const {
        const uint32_t n = c_.octets_ram / 0x2000u;
        return n ? n - 1u : 0u;
    }

    Config c_;
    bool ram_active_ = false;
    uint8_t bas_ = 0, haut_ = 0, mode_ = 0;
    uint8_t mbc5_bas_ = 1, mbc5_bit8_ = 0;
    uint8_t selection_ = 0;
    bool vibreur_ = false;
};

/* ======================================================================== */
/* Le lecteur : les écritures qui amènent une vraie cartouche dans un état   */
/* ======================================================================== */

struct Ecriture {
    uint16_t adr;
    uint8_t val;
};

struct Sequence {
    Ecriture e[4];
    uint8_t n = 0;
    uint16_t fenetre = 0; /* selection_rom : où lire la banque, 0000 ou 4000 */
    void ajouter(uint16_t adr, uint8_t val) { e[n++] = {adr, val}; }
};

/* La banque ROM `b`, lisible à `fenetre` … `fenetre + 3FFF`. */
inline Sequence selection_rom(const Config &c, uint32_t b) {
    Sequence s;
    switch (c.mbc) {
    case Mbc::Aucun:
    case Mbc::NonPrisEnCharge:
        s.fenetre = b == 0 ? 0x0000 : 0x4000;
        break;
    case Mbc::Mbc1: {
        const unsigned dec = c.mbc1m ? 4 : 5;
        const uint8_t bas = (uint8_t)(b & ((1u << dec) - 1u));
        const uint8_t haut = (uint8_t)((b >> dec) & 0x03);
        if (bas == 0) {
            /* 00, 20, 40, 60 (ou 10, 20, 30 en MBC1M) ne passent pas à 4000 :
             * on les lit à 0000, en mode 1. */
            s.ajouter(0x6000, 0x01);
            s.ajouter(0x4000, haut);
            s.fenetre = 0x0000;
        } else {
            s.ajouter(0x6000, 0x00);
            s.ajouter(0x4000, haut);
            s.ajouter(0x2000, bas);
            s.fenetre = 0x4000;
        }
        break;
    }
    case Mbc::Mbc2:
        if (b == 0) {
            s.fenetre = 0x0000;
        } else {
            s.ajouter(0x2100, (uint8_t)(b & 0x0F)); /* bit 8 de l'adresse à 1 */
            s.fenetre = 0x4000;
        }
        break;
    case Mbc::Mbc3:
        if (b == 0) {
            s.fenetre = 0x0000;
        } else {
            s.ajouter(0x2000, (uint8_t)b);
            s.fenetre = 0x4000;
        }
        break;
    case Mbc::Mbc5:
        s.ajouter(0x2000, (uint8_t)(b & 0xFF));
        s.ajouter(0x3000, (uint8_t)((b >> 8) & 0x01));
        s.fenetre = 0x4000;
        break;
    }
    return s;
}

/* Active la RAM et choisit la banque `b`, lisible et inscriptible à A000. */
inline Sequence selection_ram(const Config &c, uint32_t b) {
    Sequence s;
    if (c.mbc != Mbc::Aucun) s.ajouter(0x0000, 0x0A);
    switch (c.mbc) {
    case Mbc::Mbc1:
        s.ajouter(0x6000, 0x01);
        s.ajouter(0x4000, (uint8_t)(b & 0x03));
        break;
    case Mbc::Mbc3:
        s.ajouter(0x4000, (uint8_t)(b & 0x07));
        break;
    case Mbc::Mbc5:
        s.ajouter(0x4000, (uint8_t)(b & (c.vibreur ? 0x07 : 0x0F))); /* moteur arrêté */
        break;
    default:
        break;
    }
    return s;
}

/* Referme la RAM : une cartouche qu'on arrache ensuite ne risque plus rien. */
inline Sequence fermeture_ram(const Config &c) {
    Sequence s;
    if (c.mbc != Mbc::Aucun) s.ajouter(0x0000, 0x00);
    return s;
}

/* Fige l'horloge dans le verrou ; ensuite, pour chaque registre r, écrire
 * 08+r dans 4000 et lire A000. */
inline Sequence verrouillage_horloge() {
    Sequence s;
    s.ajouter(0x0000, 0x0A);
    s.ajouter(0x6000, 0x00);
    s.ajouter(0x6000, 0x01);
    return s;
}

/* Le vibreur des MBC5 : bit 3 du registre 4000. */
inline Sequence commande_vibreur(bool marche) {
    Sequence s;
    s.ajouter(0x4000, marche ? 0x08 : 0x00);
    return s;
}

/* Nombre de banques à lire pour une ROM, de 16 Kio chacune. */
inline uint32_t banques_rom(const Config &c) { return c.octets_rom / 0x4000u; }
inline uint32_t banques_ram(const Config &c) {
    if (c.mbc == Mbc::Mbc2) return 1;
    return c.octets_ram ? (c.octets_ram + 0x1FFFu) / 0x2000u : 0;
}

/* Une compilation MBC1M (1 Mio) porte un second logo au début de la banque 10.
 * Heuristique des émulateurs, applicable à une copie ; sur une vraie
 * cartouche, la lecture en câblage MBC1 ordinaire donne une copie fausse :
 * hors périmètre tant qu'aucune n'est à éprouver. */
inline bool ressemble_mbc1m(const uint8_t *rom, size_t taille) {
    if (taille != 1024u * 1024u) return false;
    return std::memcmp(rom + 0x10u * 0x4000u + ADR_LOGO, LOGO, sizeof LOGO) == 0;
}

} // namespace cartouche
