/**
 * La cartouche imitée par la doublure : une copie de la ROM, la RAM vivante,
 * le contrôleur, l'horloge.
 *
 * Dans le firmware, les lectures de la console sont servies par le PIO et le
 * DMA, sans passer par ici (plan §5.2) ; le processeur ne reçoit que les
 * écritures. Cette classe fait les deux, pour le PC et pour les tests, et
 * donne au firmware la même logique d'écriture : le registre change, la page
 * modifiée est marquée pour le journal (plan §3.1).
 */
#pragma once

#include "mbc.hpp"

#include <cstdint>
#include <vector>

namespace cartouche {

inline constexpr uint32_t OCTETS_PAGE = 256; /* l'unité du journal (plan §3.1) */

inline uint32_t pages_de(uint32_t octets_ram) { return (octets_ram + OCTETS_PAGE - 1) / OCTETS_PAGE; }

/* Ce qu'une écriture de la console a provoqué. */
struct Effet {
    bool page = false;          /* une page de la RAM a changé de contenu */
    uint16_t numero_page = 0;
    bool horloge = false;       /* le jeu a réglé l'horloge */
    bool vibreur = false;       /* le moteur a changé d'état */
};

class Imitation {
  public:
    Imitation() = default;

    /* `rom` reste à l'appelant (la flash, dans le firmware) ; `ram` est
     * recopiée : c'est la sauvegarde vivante, que le jeu modifie. */
    void installer(const Config &c, const uint8_t *rom, const std::vector<uint8_t> &ram) {
        mbc_ = Controleur(c);
        rom_ = rom;
        ram_ = ram;
        ram_.resize(c.octets_ram, 0xFF);
    }

    uint8_t lire(uint16_t adr) const {
        if (adr < 0x8000) return rom_[mbc_.decalage_rom(adr)];
        if (adr >= 0xA000 && adr < 0xC000) {
            const AccesRam a = mbc_.acces_ram(adr);
            if (a.genre == AccesRam::Ram) {
                const uint8_t v = ram_[a.decalage];
                return mbc_.config().mbc == Mbc::Mbc2 ? (uint8_t)(v | 0xF0) : v;
            }
            if (a.genre == AccesRam::Horloge) return mbc_.horloge.lire(a.registre);
        }
        return 0xFF; /* bus ouvert ; hors de la fente, la doublure se tait */
    }

    Effet ecrire(uint16_t adr, uint8_t v) {
        Effet e;
        if (adr < 0x8000) {
            const bool avant = mbc_.vibreur_actif();
            mbc_.ecrire_registre(adr, v);
            e.vibreur = mbc_.vibreur_actif() != avant;
            return e;
        }
        if (adr < 0xA000 || adr >= 0xC000) return e;
        const AccesRam a = mbc_.acces_ram(adr);
        if (a.genre == AccesRam::Horloge) {
            mbc_.horloge.ecrire(a.registre, v);
            e.horloge = true;
            return e;
        }
        if (a.genre != AccesRam::Ram) return e;
        if (mbc_.config().mbc == Mbc::Mbc2) v &= 0x0F;
        /* Une page réécrite à l'identique ne part pas (plan §3.1). */
        if (ram_[a.decalage] == v) return e;
        ram_[a.decalage] = v;
        e.page = true;
        e.numero_page = (uint16_t)(a.decalage / OCTETS_PAGE);
        return e;
    }

    void avancer_ms(uint32_t ms) {
        if (mbc_.config().horloge) mbc_.horloge.avancer_ms(ms);
    }

    const std::vector<uint8_t> &ram() const { return ram_; }
    Controleur &controleur() { return mbc_; }
    const Controleur &controleur() const { return mbc_; }

  private:
    Controleur mbc_;
    const uint8_t *rom_ = nullptr;
    std::vector<uint8_t> ram_;
};

} // namespace cartouche
