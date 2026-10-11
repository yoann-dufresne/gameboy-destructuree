/**
 * Banc du bus de la console (test R3) — un esclave
 *
 * Il tient la place du sniffer, de la source de son ou de la doublure, selon
 * BUS_ADRESSE fixée à la compilation. Il reçoit chaque ordre, le vérifie deux
 * fois — son CRC, puis son contenu, recalculé d'après son numéro (banc_bus.hpp)
 * — et prépare l'état que le maître lira juste après. La doublure fait
 * basculer son feu vert toutes les 5 s, pour que le maître le voie passer.
 *
 * Une ligne `BNC role=bus_esclave …` par seconde sur la console USB.
 */
#include <atomic>
#include <cstdio>
#include <cstring>

#include "hardware/i2c.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"

#include "banc_bus.hpp"
#include "bus_console.h"

#ifndef BUS_ADRESSE
#error "BUS_ADRESSE doit etre defini a la compilation (BUS_ADRESSE_SNIFFER, _SON ou _DOUBLURE)"
#endif

namespace {

/* Écrits par l'interruption de l'esclave, lus par la boucle. */
std::atomic<uint32_t> recus{0}, refus{0}, incoherents{0}, lectures{0};
std::atomic<bool> copie_prete{false};

uint8_t rx[16];
uint8_t rx_n = 0;
bool rx_trop_long = false;
bus_etat etat{};
uint8_t tx_i = 0;

void traiter() {
    bus_ordre o;
    if (rx_trop_long || rx_n != sizeof(o)) {
        refus++;
    } else {
        std::memcpy(&o, rx, sizeof(o));
        if (!bus_ordre_valide(&o, BUS_ADRESSE)) {
            refus++;
        } else {
            recus++;
            if (!banc_bus::conforme(o))
                incoherents++;
            etat.seq = o.seq;
        }
    }
    etat.drapeaux = (uint8_t)(BUS_PRET | (copie_prete ? BUS_COPIE_PRETE : 0));
    const uint32_t n_refus = refus;
    etat.refus = n_refus > 255 ? 255 : (uint8_t)n_refus;
    bus_sceller_etat(&etat, BUS_ADRESSE);
}

void sur_evenement(i2c_inst_t *i2c, i2c_slave_event_t ev) {
    switch (ev) {
    case I2C_SLAVE_RECEIVE:
        while (i2c_get_read_available(i2c)) {
            const uint8_t b = i2c_read_byte_raw(i2c);
            if (rx_n < sizeof(rx))
                rx[rx_n++] = b;
            else
                rx_trop_long = true;
        }
        break;
    case I2C_SLAVE_REQUEST: {
        const uint8_t *e = reinterpret_cast<const uint8_t *>(&etat);
        i2c_write_byte_raw(i2c, tx_i < sizeof(etat) ? e[tx_i] : 0xFF);
        if (tx_i == 0)
            lectures++;
        tx_i++;
        break;
    }
    case I2C_SLAVE_FINISH:
        if (rx_n || rx_trop_long)
            traiter();
        rx_n = 0;
        rx_trop_long = false;
        tx_i = 0;
        break;
    default:
        break;
    }
}

} // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    bus_sceller_etat(&etat, BUS_ADRESSE);
    gpio_init(BUS_BROCHE_SDA);
    gpio_init(BUS_BROCHE_SCL);
    gpio_set_function(BUS_BROCHE_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BUS_BROCHE_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BUS_BROCHE_SDA);
    gpio_pull_up(BUS_BROCHE_SCL);
    i2c_init(i2c1, BUS_FREQUENCE_HZ);
    i2c_slave_init(i2c1, BUS_ADRESSE, sur_evenement);

    printf("\n=== Banc du bus de la console — esclave 0x%02X ===\n", BUS_ADRESSE);
    printf("BNC role=bus_esclave a=0x%02X evenement=demarrage\n", BUS_ADRESSE);

    uint64_t t_ligne = time_us_64(), t_feu = time_us_64();
    while (true) {
        const uint64_t t = time_us_64();
        if (BUS_ADRESSE == BUS_ADRESSE_DOUBLURE && t - t_feu >= 5'000'000) {
            t_feu = t;
            copie_prete = !copie_prete.load();
        }
        if (t - t_ligne >= 1'000'000) {
            t_ligne += 1'000'000;
            printf("BNC role=bus_esclave a=0x%02X t=%lu recus=%lu refus=%lu incoherents=%lu "
                   "lectures=%lu\n",
                   BUS_ADRESSE, (unsigned long)to_ms_since_boot(get_absolute_time()),
                   (unsigned long)recus.load(), (unsigned long)refus.load(),
                   (unsigned long)incoherents.load(), (unsigned long)lectures.load());
        }
        if (getchar_timeout_us(0) == 'r') {
            recus = 0;
            refus = 0;
            incoherents = 0;
            lectures = 0;
            printf("BNC role=bus_esclave a=0x%02X evenement=remise_a_zero\n", BUS_ADRESSE);
        }
    }
}
