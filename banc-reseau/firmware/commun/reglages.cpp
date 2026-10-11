#include "reglages.hpp"

#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

namespace reglages {
namespace {

constexpr uint32_t MAGIC = 0x4C474552u; /* 'R','E','G','L' */
constexpr uint32_t DECALAGE = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct __attribute__((packed)) Bloc {
    uint32_t magic;
    Reglages r;
    uint8_t  somme; /* complément à 0xFF de la somme des octets de `r` */
};

uint8_t somme(const Reglages &r) {
    const auto *o = reinterpret_cast<const uint8_t *>(&r);
    uint8_t s = 0;
    for (unsigned i = 0; i < sizeof(r); ++i)
        s = (uint8_t)(s + o[i]);
    return (uint8_t)(0xFF - s);
}

/* Une page entière : la flash s'écrit par pages de 256 octets. */
uint8_t page[FLASH_PAGE_SIZE];

void effacer_et_ecrire(void *) {
    flash_range_erase(DECALAGE, FLASH_SECTOR_SIZE);
    flash_range_program(DECALAGE, page, FLASH_PAGE_SIZE);
}

} // namespace

Reglages lire() {
    const auto *b = reinterpret_cast<const Bloc *>(XIP_BASE + DECALAGE);
    if (b->magic == MAGIC && b->somme == somme(b->r))
        return b->r;
    return Reglages{0, 1, 0};
}

void ecrire_et_redemarrer(const Reglages &r) {
    std::memset(page, 0xFF, sizeof(page));
    Bloc b{MAGIC, r, somme(r)};
    std::memcpy(page, &b, sizeof(b));
    printf("\n  reglages ecrits : canal %u, numero %u, economie %u — redemarrage\n",
           r.canal, r.numero, r.economie);
    sleep_ms(50); /* laisser partir la ligne sur l'USB */
    const int e = flash_safe_execute(effacer_et_ecrire, nullptr, 100);
    if (e != PICO_OK)
        printf("  ECHEC de l'ecriture en flash (%d) : reglages non gardes\n", e);
    sleep_ms(50);
    watchdog_reboot(0, 0, 10);
    while (true)
        tight_loop_contents();
}

} // namespace reglages
