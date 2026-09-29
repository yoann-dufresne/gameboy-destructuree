/**
 * Nœud — réception de la liaison, mise en œuvre
 */

#include "reception.hpp"
#include "config.h"

#include "hardware/dma.h"
#include "hardware/pio.h"

#include "lien_rx.pio.h"

namespace {

constexpr uint32_t ANNEAU_BITS = 15; /* 32 ko : ~ 8 ms de liaison à 32 Mbit/s */
constexpr uint32_t ANNEAU_OCTETS = 1u << ANNEAU_BITS;
constexpr uint32_t ANNEAU_MOTS = ANNEAU_OCTETS / 4;

/* Le DMA boucle sur l'anneau par son adresse d'écriture : l'anneau doit être
 * aligné sur sa taille. */
alignas(ANNEAU_OCTETS) uint32_t anneau[ANNEAU_MOTS];

PIO pio;
uint sm, offset;
uint dma;
liaison::Lecteur lecteur(anneau, ANNEAU_MOTS);
reception::Stats compteurs{};

/* Position, en mots, où le DMA écrira le prochain mot. */
uint32_t ecriture() {
    return ((uint32_t)dma_hw->ch[dma].write_addr - (uint32_t)anneau) / 4u & (ANNEAU_MOTS - 1u);
}

/* Reprend l'alignement : la machine d'état repart de `debut`, qui attend un
 * CS haut puis bas — le début du prochain message. Ce qui n'a pas été lu est
 * oublié. */
void resynchroniser() {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + lien_rx_offset_debut));
    lecteur.recaler(ecriture());
    pio_sm_set_enabled(pio, sm, true);
    compteurs.resynchros++;
}

} // namespace

namespace reception {

void init() {
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(
        &lien_rx_program, &pio, &sm, &offset, PIN_LIEN_BASE, 4, true));
    for (uint i = 0; i < 4; ++i)
        pio_gpio_init(pio, PIN_LIEN_BASE + i); /* lève aussi l'isolation du pad (RP2350) */
    /* CS tiré haut : nappe débranchée = repos, la réception attend sans rien
     * lire. ⚠️ E9 : jamais de pull-down. */
    gpio_pull_up(PIN_LIEN_BASE + 3);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_LIEN_BASE, 4, false);

    pio_sm_config c = lien_rx_config(offset, PIN_LIEN_BASE);
    pio_sm_init(pio, sm, offset + lien_rx_offset_debut, &c);

    dma = (uint)dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_ring(&d, true /* écriture */, ANNEAU_BITS);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm, false));
    dma_channel_configure(dma, &d, anneau, &pio->rxf[sm], dma_encode_endless_transfer_count(),
                          true);

    pio_sm_set_enabled(pio, sm, true);
}

liaison::Lecteur::Resultat lire(uint32_t *message, liaison::Entete &e) {
    const auto r = lecteur.lire(ecriture(), message, e);
    if (r == liaison::Lecteur::Resultat::MESSAGE) {
        compteurs.messages++;
        compteurs.octets += liaison::mots(e.lg) * 4u;
    } else if (r == liaison::Lecteur::Resultat::ERREUR) {
        compteurs.erreurs++;
        resynchroniser();
    }
    return r;
}

const Stats &stats() { return compteurs; }

} // namespace reception
