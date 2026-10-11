/**
 * Banc du bus de la console (test R3) — le maître, à la place de l'injecteur
 *
 * Cinquante fois par seconde, il écrit un ordre à chacun des trois esclaves
 * (sniffer, son, doublure) et lit son état, comme le fera l'injecteur. Il
 * compte, esclave par esclave, ce qui n'est pas passé :
 *
 *   ecr   écriture refusée ou hors délai (esclave absent, bus bloqué)
 *   lec   lecture refusée ou hors délai
 *   crc   état reçu abîmé
 *   seq   état intact, mais qui ne renvoie pas le numéro de l'ordre écrit
 *   refus ordres que l'esclave dit avoir refusés
 *
 * Une ligne `BNC role=bus_maitre …` par seconde sur la console USB.
 * Commandes : « r » (remise à zéro), « h ».
 */
#include <cstdio>
#include <cstring>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "banc_bus.hpp"
#include "bus_console.h"
#include "outils.hpp"

namespace {

constexpr uint8_t ADRESSES[3] = {BUS_ADRESSE_SNIFFER, BUS_ADRESSE_SON, BUS_ADRESSE_DOUBLURE};
constexpr const char *NOMS[3] = {"sniffer", "son", "doublure"};
constexpr uint32_t DELAI_US = 2000; /* une transaction de 10 octets dure 0,25 ms à 400 kHz */

struct Comptes {
    uint32_t ok, ecr, lec, crc, seq, refus, feux, duree_max_us;
    bool copie_prete;
};
Comptes cpt[3];
uint32_t cycles = 0;

void interroger(int k, uint8_t seq) {
    Comptes &c = cpt[k];
    bus_ordre o{};
    banc_bus::remplir(o, seq);
    bus_sceller_ordre(&o, ADRESSES[k]);

    const uint32_t t0 = time_us_32();
    if (i2c_write_timeout_us(i2c1, ADRESSES[k], (const uint8_t *)&o, sizeof(o), false,
                             DELAI_US) != (int)sizeof(o)) {
        c.ecr++;
        return;
    }
    bus_etat e{};
    if (i2c_read_timeout_us(i2c1, ADRESSES[k], (uint8_t *)&e, sizeof(e), false, DELAI_US) !=
        (int)sizeof(e)) {
        c.lec++;
        return;
    }
    const uint32_t dt = time_us_32() - t0;
    if (dt > c.duree_max_us)
        c.duree_max_us = dt;
    if (!bus_etat_valide(&e, ADRESSES[k])) {
        c.crc++;
        return;
    }
    if (e.seq != seq) {
        c.seq++;
        return;
    }
    c.ok++;
    c.refus = e.refus;
    const bool prete = e.drapeaux & BUS_COPIE_PRETE;
    if (prete != c.copie_prete)
        c.feux++;
    c.copie_prete = prete;
}

void imprimer() {
    static char l[512];
    size_t n = (size_t)std::snprintf(l, sizeof(l), "BNC role=bus_maitre t=%lu cycles=%lu",
                                     (unsigned long)to_ms_since_boot(get_absolute_time()),
                                     (unsigned long)cycles);
    for (int k = 0; k < 3; ++k) {
        const Comptes &c = cpt[k];
        n += (size_t)std::snprintf(
            l + n, sizeof(l) - n,
            " %s=ok:%lu,ecr:%lu,lec:%lu,crc:%lu,seq:%lu,refus:%lu,feux:%lu,duree_max_us:%lu",
            NOMS[k], (unsigned long)c.ok, (unsigned long)c.ecr, (unsigned long)c.lec,
            (unsigned long)c.crc, (unsigned long)c.seq, (unsigned long)c.refus,
            (unsigned long)c.feux, (unsigned long)c.duree_max_us);
    }
    printf("%s\n", l);
}

} // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    i2c_init(i2c1, BUS_FREQUENCE_HZ);
    gpio_set_function(BUS_BROCHE_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BUS_BROCHE_SCL, GPIO_FUNC_I2C);
    /* Les tirages internes ne suffisent pas à 400 kHz : les 4,7 kΩ du bus font
     * le travail. Les activer ne coûte rien et évite un bus flottant si on les
     * oublie — au prix de fronts lents, que `duree_max_us` trahirait. */
    gpio_pull_up(BUS_BROCHE_SDA);
    gpio_pull_up(BUS_BROCHE_SCL);

    printf("\n=== Banc du bus de la console — maitre ===\n"
           "  I2C1, SDA GP%d, SCL GP%d, %u kHz, %u interrogations/s par esclave\n"
           "  r = remise a zero    h = cette aide\n\n",
           BUS_BROCHE_SDA, BUS_BROCHE_SCL, BUS_FREQUENCE_HZ / 1000, 1000000u / BUS_PERIODE_US);
    printf("BNC role=bus_maitre evenement=demarrage\n");

    uint8_t seq = 0;
    uint64_t t_cycle = time_us_64(), t_ligne = time_us_64();
    while (true) {
        const uint64_t t = time_us_64();
        if (t >= t_cycle) {
            t_cycle += BUS_PERIODE_US;
            seq++;
            for (int k = 0; k < 3; ++k)
                interroger(k, seq);
            cycles++;
        }
        if (t - t_ligne >= 1'000'000) {
            t_ligne += 1'000'000;
            imprimer();
        }
        char l[64];
        if (outils::ligne(l, sizeof(l))) {
            if (!std::strcmp(l, "r")) {
                std::memset(cpt, 0, sizeof(cpt));
                cycles = 0;
                printf("BNC role=bus_maitre evenement=remise_a_zero\n");
            } else {
                printf("\n  r = remise a zero    h = cette aide\n");
            }
        }
    }
}
