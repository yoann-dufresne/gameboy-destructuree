/**
 * Éprouve le format du bus de la console sur PC — aucune carte nécessaire.
 *
 *     g++ -std=c++20 -O2 -Wall -Wextra -I../commun -I../bus test_bus_console.cpp -o /tmp/test_bus_console
 *     /tmp/test_bus_console
 *
 * Ce qu'on vérifie : le CRC est bien le CRC-8 du SMBus ; un ordre ou un état
 * scellé passe ; tout bit retourné, n'importe où, le fait refuser ; un message
 * lu à une autre adresse est refusé ; le contenu du banc R3 se recalcule à
 * l'identique d'après son numéro, pour les 256 numéros.
 */
#include "banc_bus.hpp"
#include "bus_console.h"

#include <cstdio>
#include <cstring>

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

constexpr uint8_t ADRESSES[3] = {BUS_ADRESSE_SNIFFER, BUS_ADRESSE_SON, BUS_ADRESSE_DOUBLURE};

void valeur_de_controle() {
    const char *texte = "123456789";
    const uint8_t crc = bus_crc8(0, reinterpret_cast<const uint8_t *>(texte), 9);
    VERIFIER(crc == 0xF4, "CRC-8/SMBUS(\"123456789\") = 0x%02X, attendu 0xF4", crc);
}

template <typename M, typename Sceller, typename Valide>
void bits_retournes(const char *nom, M m, uint8_t adresse, Sceller sceller, Valide valide) {
    sceller(&m, adresse);
    VERIFIER(valide(&m, adresse), "%s scelle refuse", nom);
    for (unsigned octet = 0; octet < sizeof(m); ++octet)
        for (int bit = 0; bit < 8; ++bit) {
            M abime = m;
            reinterpret_cast<uint8_t *>(&abime)[octet] ^= (uint8_t)(1u << bit);
            VERIFIER(!valide(&abime, adresse), "%s : bit %d de l'octet %u retourne, accepte",
                     nom, bit, octet);
        }
    for (uint8_t autre : ADRESSES)
        if (autre != adresse)
            VERIFIER(!valide(&m, autre), "%s pour 0x%02X accepte a 0x%02X", nom, adresse, autre);
}

void ordres_et_etats() {
    for (uint8_t a : ADRESSES)
        for (int seq = 0; seq < 256; ++seq) {
            bus_ordre o{};
            banc_bus::remplir(o, (uint8_t)seq);
            bits_retournes("ordre", o, a, bus_sceller_ordre, bus_ordre_valide);

            bus_etat e{};
            e.seq = (uint8_t)seq;
            e.drapeaux = (uint8_t)(BUS_PRET | ((seq & 1) ? BUS_COPIE_PRETE : 0));
            e.refus = (uint8_t)(seq / 3);
            bits_retournes("etat", e, a, bus_sceller_etat, bus_etat_valide);
        }
}

void version_refusee() {
    bus_ordre o{};
    banc_bus::remplir(o, 42);
    bus_sceller_ordre(&o, BUS_ADRESSE_SON);
    o.version = BUS_VERSION + 1;
    o.crc = bus_crc8(BUS_ADRESSE_SON, reinterpret_cast<const uint8_t *>(&o), sizeof(o) - 1);
    VERIFIER(!bus_ordre_valide(&o, BUS_ADRESSE_SON), "ordre d'une autre version accepte");
}

void contenu_du_banc() {
    for (int seq = 0; seq < 256; ++seq) {
        bus_ordre o{};
        banc_bus::remplir(o, (uint8_t)seq);
        VERIFIER(banc_bus::conforme(o), "contenu du numero %d non conforme", seq);
        bus_ordre change = o;
        change.volume ^= 1;
        VERIFIER(!banc_bus::conforme(change), "volume change non vu, numero %d", seq);
    }
}

} // namespace

int main() {
    std::printf("bus de la console : ordre %zu octets, etat %zu octets\n", sizeof(bus_ordre),
                sizeof(bus_etat));
    valeur_de_controle();
    ordres_et_etats();
    version_refusee();
    contenu_du_banc();
    if (echecs) {
        std::printf("ECHEC : %d verification(s) en defaut\n", echecs);
        return 1;
    }
    std::printf("OK : CRC conforme ; 3 adresses x 256 numeros, chaque bit retourne refuse, "
                "chaque adresse croisee refusee\n");
    return 0;
}
