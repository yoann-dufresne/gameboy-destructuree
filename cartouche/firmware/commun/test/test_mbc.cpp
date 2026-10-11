/**
 * Éprouve l'en-tête, les contrôleurs de banques et l'imitation sur PC —
 * aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I.. test_mbc.cpp -o /tmp/test_mbc && /tmp/test_mbc
 *
 * Trois familles de vérifications :
 *   - les cas que décrivent les Pan Docs, un par un (banque 0 → 1 du MBC1,
 *     bit 9 du MBC5, verrou de l'horloge du MBC3…) ;
 *   - l'aller-retour lecteur → cartouche : pour chaque type et chaque banque,
 *     la séquence d'écritures du lecteur, appliquée à un contrôleur laissé dans
 *     un état quelconque, doit montrer exactement cette banque ;
 *   - l'imitation de la doublure : marquage des pages, demi-octets du MBC2.
 */

#include "entete.hpp"
#include "imitation.hpp"
#include "mbc.hpp"

#include <cstdio>
#include <random>
#include <vector>

using namespace cartouche;

namespace {

int echecs = 0;
int verifications = 0;

#define VERIFIER(cond, ...)                                                  \
    do {                                                                     \
        ++verifications;                                                     \
        if (!(cond)) {                                                       \
            if (++echecs <= 30) {                                            \
                std::printf("  ECHEC %s:%d  ", __FILE__, __LINE__);          \
                std::printf(__VA_ARGS__);                                    \
                std::printf("\n");                                           \
            }                                                                \
        }                                                                    \
    } while (0)

uint32_t banque_a(const Controleur &c, uint16_t adr) { return c.decalage_rom(adr) / 0x4000u; }

Config cfg(Mbc m, uint32_t rom, uint32_t ram, bool horloge = false, bool vibreur = false, bool mbc1m = false) {
    Config c;
    c.mbc = m;
    c.octets_rom = rom;
    c.octets_ram = ram;
    c.horloge = horloge;
    c.vibreur = vibreur;
    c.mbc1m = mbc1m;
    c.mbc30 = m == Mbc::Mbc3 && (rom > 2u * 1024u * 1024u || ram > 32u * 1024u);
    return c;
}

constexpr uint32_t K = 1024;

/* ---------------------------------------------------------------------- */

void tester_crc_et_entete() {
    const char *s = "123456789";
    VERIFIER(crc32(0, (const uint8_t *)s, 9) == 0xCBF43926u, "CRC-32 de 123456789");
    VERIFIER(crc32(crc32(0, (const uint8_t *)s, 4), (const uint8_t *)s + 4, 5) == 0xCBF43926u,
             "CRC-32 poursuivi");

    std::vector<uint8_t> rom(64 * K);
    std::mt19937 h(1);
    for (auto &o : rom) o = (uint8_t)h();
    std::memcpy(rom.data() + ADR_LOGO, LOGO, sizeof LOGO);
    std::memcpy(rom.data() + ADR_TITRE, "POKEMON RED\0\0\0\0\0", 16);
    rom[ADR_TYPE] = 0x13;
    rom[ADR_TAILLE_ROM] = 0x01;
    rom[ADR_TAILLE_RAM] = 0x03;
    rom[ADR_SOMME_ENTETE] = calculer_somme_entete(rom.data());
    const uint16_t g = calculer_somme_globale(rom.data(), rom.size());
    rom[ADR_SOMME_GLOBALE] = (uint8_t)(g >> 8);
    rom[ADR_SOMME_GLOBALE + 1] = (uint8_t)g;
    VERIFIER(calculer_somme_globale(rom.data(), rom.size()) == g, "la somme globale ignore 014E-014F");

    VERIFIER(juger_entete(rom.data()) == Verdict::Bon, "en-tête juste");
    const Entete e = lire_entete(rom.data());
    VERIFIER(e.type.mbc == Mbc::Mbc3 && e.type.ram && e.type.pile && !e.type.horloge, "type 13");
    VERIFIER(e.octets_rom == 64 * K && e.octets_ram == 32 * K, "tailles");
    VERIFIER(e.somme_globale == g, "somme globale lue en big-endian");

    auto r2 = rom;
    r2[ADR_LOGO + 7] ^= 1;
    VERIFIER(juger_entete(r2.data()) == Verdict::LogoFaux, "logo faux");
    r2 = rom;
    r2[ADR_TITRE] ^= 1;
    VERIFIER(juger_entete(r2.data()) == Verdict::SommeFausse, "somme d'en-tête fausse");
    r2 = rom;
    r2[ADR_TYPE] = 0xFC; /* caméra */
    r2[ADR_SOMME_ENTETE] = calculer_somme_entete(r2.data());
    VERIFIER(juger_entete(r2.data()) == Verdict::NonPrisEnCharge, "caméra refusée");

    VERIFIER(taille_rom(0x00) == 32 * K && taille_rom(0x05) == 1024 * K && taille_rom(0x08) == 8192 * K,
             "tailles ROM");
    VERIFIER(taille_rom(0x52) == 0, "code 52 refusé");
    const Type t5 = decoder_type(0x1E);
    VERIFIER(t5.mbc == Mbc::Mbc5 && t5.vibreur && t5.ram && t5.pile, "type 1E");
    VERIFIER(taille_ram(0x05, t5) == 64 * K && taille_ram(0x04, t5) == 128 * K, "tailles RAM");
    VERIFIER(taille_ram(0x00, decoder_type(0x06)) == RAM_MBC2, "MBC2 : 512 demi-octets");
    VERIFIER(decoder_type(0x10).horloge && decoder_type(0x0F).horloge && !decoder_type(0x13).horloge,
             "horloge");

    const Signature a = signer(rom.data(), rom.size());
    auto r3 = rom;
    r3[0x7000] ^= 0x40;
    VERIFIER(signer(r3.data(), r3.size()) != a, "un octet changé change la signature");
    VERIFIER(signer(rom.data(), rom.size()) == a, "signature stable");
}

/* ---------------------------------------------------------------------- */

void tester_mbc1() {
    Controleur c(cfg(Mbc::Mbc1, 2048 * K, 8 * K));
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC1 : banque 1 à la mise sous tension");
    c.ecrire_registre(0x2000, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC1 : 0 donne 1");
    c.ecrire_registre(0x2000, 0x20); /* seuls 5 bits : 0 → 1 */
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC1 : 0x20 masqué à 0, donne 1");
    c.ecrire_registre(0x4000, 0x01);
    c.ecrire_registre(0x2000, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 0x21, "MBC1 : 20 impossible à 4000, donne 21");
    VERIFIER(banque_a(c, 0x0000) == 0x00, "MBC1 mode 0 : 0000 fixe sur la banque 0");
    c.ecrire_registre(0x6000, 0x01);
    VERIFIER(banque_a(c, 0x0000) == 0x20, "MBC1 mode 1 : 0000 montre la banque 20");
    c.ecrire_registre(0x2000, 0x05);
    VERIFIER(banque_a(c, 0x4000) == 0x25, "MBC1 : (haut << 5) | bas");

    /* Petite ROM : le bit 4 n'existe pas, 0x10 donne la banque 0 (Pan Docs). */
    Controleur p(cfg(Mbc::Mbc1, 256 * K, 0));
    p.ecrire_registre(0x2000, 0x10);
    VERIFIER(banque_a(p, 0x4000) == 0, "MBC1 256 Kio : 0x10 donne la banque 0");
    p.ecrire_registre(0x2000, 0x13);
    VERIFIER(banque_a(p, 0x4000) == 3, "MBC1 256 Kio : 0x13 donne 3");

    /* RAM 32 Kio : banque seulement en mode 1. */
    Controleur r(cfg(Mbc::Mbc1, 512 * K, 32 * K));
    VERIFIER(r.acces_ram(0xA000).genre == AccesRam::Rien, "MBC1 : RAM fermée au départ");
    r.ecrire_registre(0x0000, 0x1A); /* 4 bits bas = A */
    VERIFIER(r.acces_ram(0xA000).genre == AccesRam::Ram, "MBC1 : 0x1A active la RAM");
    r.ecrire_registre(0x4000, 0x02);
    VERIFIER(r.acces_ram(0xA123).decalage == 0x0123, "MBC1 mode 0 : RAM banque 0");
    r.ecrire_registre(0x6000, 0x01);
    VERIFIER(r.acces_ram(0xA123).decalage == 2 * 0x2000 + 0x123, "MBC1 mode 1 : RAM banque 2");
    r.ecrire_registre(0x0000, 0x0B);
    VERIFIER(r.acces_ram(0xA000).genre == AccesRam::Rien, "MBC1 : 0x0B ferme la RAM");
    r.reinitialiser();
    VERIFIER(!r.ram_active() && banque_a(r, 0x4000) == 1, "MBC1 : coupure, retour à l'état initial");
}

void tester_mbc1m() {
    Controleur c(cfg(Mbc::Mbc1, 1024 * K, 0, false, false, true));
    c.ecrire_registre(0x4000, 0x01);
    c.ecrire_registre(0x2000, 0x12);
    VERIFIER(banque_a(c, 0x4000) == 0x12, "MBC1M : le bit 4 du registre bas est ignoré");
    c.ecrire_registre(0x2000, 0x10);
    VERIFIER(banque_a(c, 0x4000) == 0x10, "MBC1M : 0x10 n'est pas 0 sur 5 bits, pas de +1");
    c.ecrire_registre(0x2000, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 0x11, "MBC1M : 0 donne 1");
    c.ecrire_registre(0x6000, 0x01);
    c.ecrire_registre(0x4000, 0x03);
    VERIFIER(banque_a(c, 0x0000) == 0x30, "MBC1M mode 1 : 0000 montre 30");
}

void tester_mbc2() {
    Controleur c(cfg(Mbc::Mbc2, 256 * K, RAM_MBC2));
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC2 : banque 1 au départ");
    c.ecrire_registre(0x2100, 0x05);
    VERIFIER(banque_a(c, 0x4000) == 5, "MBC2 : bit 8 à 1, la banque");
    c.ecrire_registre(0x0100, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC2 : 0 donne 1");
    c.ecrire_registre(0x2000, 0x0A); /* bit 8 à 0 : la RAM, pas la banque */
    VERIFIER(c.ram_active() && banque_a(c, 0x4000) == 1, "MBC2 : bit 8 à 0, la RAM");
    VERIFIER(c.acces_ram(0xA203).decalage == 0x003 && c.acces_ram(0xBFFF).decalage == 0x1FF,
             "MBC2 : écho tous les 512 octets");

    Imitation im;
    std::vector<uint8_t> rom(256 * K, 0);
    im.installer(cfg(Mbc::Mbc2, 256 * K, RAM_MBC2), rom.data(), std::vector<uint8_t>(RAM_MBC2, 0));
    im.ecrire(0x0000, 0x0A);
    const Effet e = im.ecrire(0xA010, 0xB7);
    VERIFIER(e.page && im.ram()[0x10] == 0x07, "MBC2 : seul le demi-octet bas est gardé");
    VERIFIER(im.lire(0xA010) == 0xF7, "MBC2 : les 4 bits hauts se lisent à 1");
    VERIFIER(!im.ecrire(0xA210, 0x57).page, "MBC2 : même demi-octet, par l'écho : rien de marqué");
}

void tester_mbc3() {
    Controleur c(cfg(Mbc::Mbc3, 2048 * K, 32 * K, true));
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC3 : banque 1");
    c.ecrire_registre(0x2000, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 1, "MBC3 : 0 donne 1");
    c.ecrire_registre(0x2000, 0xFF);
    VERIFIER(banque_a(c, 0x4000) == 0x7F, "MBC3 : 7 bits");
    c.ecrire_registre(0x0000, 0x0A);
    c.ecrire_registre(0x4000, 0x03);
    VERIFIER(c.acces_ram(0xA001).decalage == 3 * 0x2000 + 1, "MBC3 : RAM banque 3");
    c.ecrire_registre(0x4000, 0x05);
    VERIFIER(c.acces_ram(0xA001).genre == AccesRam::Rien, "MBC3 : banque 5 inexistante, bus ouvert");
    c.ecrire_registre(0x4000, 0x08);
    VERIFIER(c.acces_ram(0xA000).genre == AccesRam::Horloge && c.acces_ram(0xA000).registre == RTC_S,
             "MBC3 : 08 sélectionne les secondes");
    c.ecrire_registre(0x4000, 0x0D);
    VERIFIER(c.acces_ram(0xA000).genre == AccesRam::Rien, "MBC3 : 0D ne sélectionne rien");

    Horloge &h = c.horloge;
    h.ecrire(RTC_S, 59);
    h.ecrire(RTC_M, 59);
    h.ecrire(RTC_H, 23);
    h.ecrire(RTC_DL, 0xFF);
    h.ecrire(RTC_DH, 0x01); /* jour 511 */
    h.avancer_ms(999);
    VERIFIER(h.reel[RTC_S] == 59, "horloge : 999 ms ne font pas une seconde");
    h.avancer_ms(1);
    VERIFIER(h.reel[RTC_S] == 0 && h.reel[RTC_M] == 0 && h.reel[RTC_H] == 0 && h.reel[RTC_DL] == 0,
             "horloge : retenues en cascade");
    VERIFIER((h.reel[RTC_DH] & 0x81) == 0x80, "horloge : jour 511 → 0, retenue levée");
    h.avancer_ms(86400u * 1000u);
    VERIFIER(h.reel[RTC_DL] == 1 && (h.reel[RTC_DH] & RTC_RETENUE), "horloge : la retenue reste levée");
    VERIFIER(c.horloge.lire(RTC_S) == 0, "horloge : rien lu avant le verrou");
    c.ecrire_registre(0x6000, 0x01); /* 01 seul ne verrouille pas */
    VERIFIER(c.horloge.lire(RTC_DL) == 0, "horloge : 01 seul ne verrouille pas");
    c.ecrire_registre(0x6000, 0x00);
    c.ecrire_registre(0x6000, 0x01);
    VERIFIER(c.horloge.lire(RTC_DL) == 1, "horloge : 00 puis 01 verrouille");
    h.ecrire(RTC_DH, (uint8_t)(h.reel[RTC_DH] | RTC_ARRET));
    const uint8_t s = h.reel[RTC_S];
    h.avancer_ms(10000);
    VERIFIER(h.reel[RTC_S] == s, "horloge : arrêtée, elle ne tourne pas");
    h.ecrire(RTC_DH, 0);
    h.ecrire(RTC_S, 62); /* hors plage : compte jusqu'à 63 puis 0, sans retenue */
    const uint8_t m = h.reel[RTC_M];
    h.avancer_ms(2000);
    VERIFIER(h.reel[RTC_S] == 0 && h.reel[RTC_M] == m, "horloge : 62 → 63 → 0 sans retenue");
    h.sous_seconde_ms = 700;
    h.ecrire(RTC_S, 10);
    VERIFIER(h.sous_seconde_ms == 0, "horloge : écrire les secondes remet la sous-seconde à 0");
}

void tester_mbc5() {
    Controleur c(cfg(Mbc::Mbc5, 8192 * K, 128 * K));
    c.ecrire_registre(0x2000, 0x00);
    VERIFIER(banque_a(c, 0x4000) == 0, "MBC5 : 0 donne bien 0");
    c.ecrire_registre(0x3000, 0x01);
    c.ecrire_registre(0x2000, 0x23);
    VERIFIER(banque_a(c, 0x4000) == 0x123, "MBC5 : 9e bit");
    c.ecrire_registre(0x3000, 0xFE);
    VERIFIER(banque_a(c, 0x4000) == 0x023, "MBC5 : seul le bit 0 de 3000 compte");
    c.ecrire_registre(0x0000, 0x0A);
    c.ecrire_registre(0x4000, 0x0F);
    VERIFIER(c.acces_ram(0xA000).decalage == 15 * 0x2000, "MBC5 : 16 banques de RAM");

    Controleur v(cfg(Mbc::Mbc5, 1024 * K, 32 * K, false, true));
    v.ecrire_registre(0x0000, 0x0A);
    v.ecrire_registre(0x4000, 0x0B);
    VERIFIER(v.vibreur_actif(), "MBC5 vibreur : bit 3 allume le moteur");
    VERIFIER(v.acces_ram(0xA000).decalage == 3 * 0x2000, "MBC5 vibreur : bit 3 hors de la banque");
    v.ecrire_registre(0x4000, 0x01);
    VERIFIER(!v.vibreur_actif(), "MBC5 vibreur : moteur coupé");

    Imitation im;
    std::vector<uint8_t> rom(1024 * K, 0);
    im.installer(cfg(Mbc::Mbc5, 1024 * K, 32 * K, false, true), rom.data(), std::vector<uint8_t>(32 * K, 0));
    const Effet e1 = im.ecrire(0x4000, 0x08);
    VERIFIER(e1.vibreur, "imitation : le moteur change, l'effet le dit");
    VERIFIER(!im.ecrire(0x4000, 0x08).vibreur, "imitation : pas de changement, pas d'effet");
}

void tester_sans_mbc() {
    Controleur c(cfg(Mbc::Aucun, 32 * K, 8 * K));
    VERIFIER(banque_a(c, 0x7FFF) == 1 && banque_a(c, 0x0000) == 0, "sans MBC : 32 Kio à plat");
    VERIFIER(c.acces_ram(0xA000).genre == AccesRam::Ram, "ROM+RAM : toujours active");
    c.ecrire_registre(0x2000, 0x05);
    VERIFIER(banque_a(c, 0x4000) == 1, "sans MBC : les écritures ne changent rien");
}

/* ---------------------------------------------------------------------- */
/* L'aller-retour : la séquence du lecteur, vue par le contrôleur            */
/* ---------------------------------------------------------------------- */

void appliquer(Controleur &c, const Sequence &s) {
    for (uint8_t i = 0; i < s.n; ++i) c.ecrire_registre(s.e[i].adr, s.e[i].val);
}

void desordonner(Controleur &c, std::mt19937 &h) {
    for (int i = 0; i < 6; ++i) c.ecrire_registre((uint16_t)(h() & 0x7FFF), (uint8_t)h());
}

void tester_aller_retour() {
    std::mt19937 h(7);
    const std::vector<Config> configs = {
        cfg(Mbc::Aucun, 32 * K, 8 * K),
        cfg(Mbc::Mbc1, 64 * K, 0),
        cfg(Mbc::Mbc1, 512 * K, 32 * K),
        cfg(Mbc::Mbc1, 2048 * K, 8 * K),
        cfg(Mbc::Mbc1, 1024 * K, 0, false, false, true),
        cfg(Mbc::Mbc2, 256 * K, RAM_MBC2),
        cfg(Mbc::Mbc3, 2048 * K, 32 * K, true),
        cfg(Mbc::Mbc3, 4096 * K, 64 * K, true), /* MBC30 */
        cfg(Mbc::Mbc5, 8192 * K, 128 * K),
        cfg(Mbc::Mbc5, 1024 * K, 32 * K, false, true),
    };
    for (const Config &cf : configs) {
        for (uint32_t b = 0; b < banques_rom(cf); ++b) {
            Controleur c(cf);
            desordonner(c, h);
            const Sequence s = selection_rom(cf, b);
            appliquer(c, s);
            const uint16_t x = (uint16_t)(h() & 0x3FFF);
            VERIFIER(c.decalage_rom((uint16_t)(s.fenetre + x)) == b * 0x4000u + x,
                     "aller-retour ROM : mbc %d, banque %u", (int)cf.mbc, b);
        }
        for (uint32_t b = 0; b < banques_ram(cf); ++b) {
            Controleur c(cf);
            desordonner(c, h);
            appliquer(c, selection_ram(cf, b));
            const uint16_t x = (uint16_t)(h() & 0x1FFF);
            const AccesRam a = c.acces_ram((uint16_t)(0xA000 + x));
            const uint32_t attendu = cf.mbc == Mbc::Mbc2 ? (x & 0x1FFu) : b * 0x2000u + x;
            VERIFIER(a.genre == AccesRam::Ram && a.decalage == attendu,
                     "aller-retour RAM : mbc %d, banque %u", (int)cf.mbc, b);
            appliquer(c, fermeture_ram(cf));
            if (cf.mbc != Mbc::Aucun)
                VERIFIER(c.acces_ram(0xA000).genre == AccesRam::Rien, "fermeture : mbc %d", (int)cf.mbc);
        }
    }
    /* L'horloge : verrouiller, sélectionner, lire. */
    Controleur c(cfg(Mbc::Mbc3, 1024 * K, 8 * K, true));
    c.horloge.ecrire(RTC_H, 13);
    desordonner(c, h);
    appliquer(c, verrouillage_horloge());
    c.ecrire_registre(0x4000, 0x08 + RTC_H);
    const AccesRam a = c.acces_ram(0xA000);
    VERIFIER(a.genre == AccesRam::Horloge && c.horloge.lire(a.registre) == 13, "lecture de l'horloge");
}

void tester_imitation() {
    std::vector<uint8_t> rom(512 * K);
    std::mt19937 h(3);
    for (auto &o : rom) o = (uint8_t)h();
    Imitation im;
    im.installer(cfg(Mbc::Mbc1, 512 * K, 32 * K), rom.data(), std::vector<uint8_t>(32 * K, 0x00));
    im.ecrire(0x2000, 0x07);
    VERIFIER(im.lire(0x4005) == rom[7 * 0x4000 + 5], "imitation : lecture de la banque 7");
    VERIFIER(im.lire(0xA000) == 0xFF, "imitation : RAM fermée, FF");
    VERIFIER(!im.ecrire(0xA000, 0x55).page, "imitation : RAM fermée, l'écriture se perd");
    im.ecrire(0x0000, 0x0A);
    im.ecrire(0x6000, 0x01);
    im.ecrire(0x4000, 0x02);
    const Effet e = im.ecrire(0xA1FF, 0x55);
    VERIFIER(e.page && e.numero_page == (2 * 0x2000 + 0x1FF) / 256, "imitation : la page marquée");
    VERIFIER(!im.ecrire(0xA1FF, 0x55).page, "imitation : même valeur, rien de marqué");
    VERIFIER(im.lire(0xA1FF) == 0x55, "imitation : relue");
}

} // namespace

int main() {
    tester_crc_et_entete();
    tester_mbc1();
    tester_mbc1m();
    tester_mbc2();
    tester_mbc3();
    tester_mbc5();
    tester_sans_mbc();
    tester_aller_retour();
    tester_imitation();
    if (echecs) {
        std::printf("test_mbc : %d échec(s) sur %d vérifications\n", echecs, verifications);
        return 1;
    }
    std::printf("test_mbc : %d vérifications, toutes justes\n", verifications);
    return 0;
}
