/**
 * Tête — les liaisons vers les nœuds, mise en œuvre
 *
 * Par liaison : un anneau de 32 ko, un canal DMA qui le vide vers la FIFO du
 * PIO, une machine d'état PIO qui sérialise sur la nappe. Chaque message est
 * précédé dans l'anneau d'un mot « nombre de mots − 1 », consommé par le PIO
 * pour tenir CS bas le temps du message (src/lien.pio).
 *
 * Producteur (envoyer) et consommateur (DMA, relancé par pousser) partagent
 * deux compteurs de mots qui ne font que croître : `ecrit` et `lance`. Leurs
 * différences restent justes quand ils bouclent sur 32 bits.
 *
 * Deux cœurs y touchent : la réception, l'entretien et l'interruption DMA sur
 * le cœur 0, la synchronisation (VALIDER, `transmis`) sur le cœur 1. Masquer
 * les interruptions ne protège qu'un cœur : un verrou matériel sérialise
 * l'ajout d'un message, `lance` et la relance du DMA. Il n'est tenu que
 * quelques microsecondes — la synchronisation ne doit jamais attendre le
 * verrou de lwIP, que la réception garde jusqu'à 140 ms sous charge.
 */

#include "lien.hpp"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/sync.h"

#include "lien.pio.h"

namespace {

constexpr uint32_t ANNEAU_BITS = 15; /* 32 ko : plusieurs images IDX8 d'avance */
constexpr uint32_t ANNEAU_OCTETS = 1u << ANNEAU_BITS;
constexpr uint32_t ANNEAU_MOTS = ANNEAU_OCTETS / 4;

/* Le DMA boucle sur l'anneau par son adresse de lecture : l'anneau doit être
 * aligné sur sa taille. */
alignas(ANNEAU_OCTETS) uint32_t anneaux[NB_RANGEES][ANNEAU_MOTS];

constexpr uint BASES[3] = {PIN_LIEN0_BASE, PIN_LIEN1_BASE, PIN_LIEN2_BASE};
static_assert(NB_RANGEES <= 3, "trois liaisons câblées au plus");

struct Liaison {
    PIO pio;
    uint sm;
    uint offset;
    uint dma;
    volatile uint32_t ecrit; /* mots écrits dans l'anneau, depuis le démarrage */
    volatile uint32_t lance; /* mots confiés au DMA */
};

Liaison liaisons[NB_RANGEES];
lien::Stats compteurs{};
spin_lock_t *verrou = nullptr;

/* Message en cours d'encodage, un brouillon par cœur : l'encodage (le CRC)
 * se fait hors verrou. Sur le cœur 0, la réception et la boucle principale se
 * succèdent sous le verrou de lwIP ; le cœur 1 a le sien. */
uint32_t brouillons[2][liaison::MOTS_MAX];

/* Mots déjà lus par le DMA. À appeler sous le verrou. */
uint32_t consommes(const Liaison &l) {
    const uint32_t reste = dma_channel_is_busy(l.dma)
                               ? (dma_hw->ch[l.dma].transfer_count & 0x0FFFFFFFu)
                               : 0;
    return l.lance - reste;
}

/* Confie au DMA tout ce qui attend, s'il est libre. Sous le verrou. */
void pousser_verrouille(Liaison &l) {
    if (dma_channel_is_busy(l.dma))
        return;
    const uint32_t n = l.ecrit - l.lance;
    if (n) {
        l.lance = l.lance + n;
        /* L'adresse de lecture reprend où le transfert précédent s'est
         * arrêté, et boucle d'elle-même sur l'anneau. */
        dma_channel_set_trans_count(l.dma, n, true);
    }
}

void sur_fin_dma() {
    for (uint k = 0; k < NB_RANGEES; ++k) {
        if (dma_channel_get_irq0_status(liaisons[k].dma)) {
            dma_channel_acknowledge_irq0(liaisons[k].dma);
            const uint32_t s = spin_lock_blocking(verrou);
            pousser_verrouille(liaisons[k]);
            spin_unlock(verrou, s);
        }
    }
}

} // namespace

namespace lien {

void init() {
    verrou = spin_lock_init(spin_lock_claim_unused(true));

    /* Deux cycles PIO par couple de bits : horloge du PIO = 2 × celle de la
     * liaison. */
    const float clkdiv = (float)clock_get_hz(clk_sys) / (2.0f * LIEN_HORLOGE_KHZ * 1000.0f);

    for (uint k = 0; k < NB_RANGEES; ++k) {
        Liaison &l = liaisons[k];
        const uint base = BASES[k];
        hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(
            &lien_tx_program, &l.pio, &l.sm, &l.offset, base, 4, true));

        for (uint i = 0; i < 4; ++i)
            pio_gpio_init(l.pio, base + i);
        /* Repos avant même le démarrage : CS haut, CLK et données bas. */
        pio_sm_set_pins_with_mask(l.pio, l.sm, 1u << (base + 3), 0xFu << base);
        pio_sm_set_consecutive_pindirs(l.pio, l.sm, base, 4, true);

        pio_sm_config c = lien_tx_config(l.offset, base, clkdiv);
        pio_sm_init(l.pio, l.sm, l.offset + lien_tx_offset_attente, &c);
        pio_sm_set_enabled(l.pio, l.sm, true);

        l.dma = (uint)dma_claim_unused_channel(true);
        dma_channel_config d = dma_channel_get_default_config(l.dma);
        channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
        channel_config_set_read_increment(&d, true);
        channel_config_set_write_increment(&d, false);
        channel_config_set_ring(&d, false /* lecture */, ANNEAU_BITS);
        channel_config_set_dreq(&d, pio_get_dreq(l.pio, l.sm, true));
        dma_channel_configure(l.dma, &d, &l.pio->txf[l.sm], anneaux[k], 0, false);
        dma_channel_set_irq0_enabled(l.dma, true);

        l.ecrit = 0;
        l.lance = 0;

        gpio_init(PIN_RDY_BASE + k);
        gpio_set_dir(PIN_RDY_BASE + k, GPIO_IN);
        gpio_pull_up(PIN_RDY_BASE + k); /* ⚠️ E9 : jamais de pull-down ici */
    }

    irq_add_shared_handler(DMA_IRQ_0, sur_fin_dma, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    gpio_init(PIN_VSYNC);
    gpio_set_dir(PIN_VSYNC, GPIO_OUT);
    gpio_put(PIN_VSYNC, 1); /* actif bas */
}

bool envoyer(uint8_t k, const liaison::Entete &e, const void *charge, uint32_t *fin) {
    Liaison &l = liaisons[k];
    uint32_t *brouillon = brouillons[get_core_num()];
    const uint32_t nb = liaison::encoder(e, charge, brouillon);
    const uint32_t total = nb + 1; /* + le mot de longueur pour le PIO */

    /* Sous le verrou : la place, la copie (≤ 354 mots, ~ 2 µs), les compteurs
     * et la relance du DMA. Un message n'est jamais entrelacé avec un autre. */
    const uint32_t s = spin_lock_blocking(verrou);
    const uint32_t libre = ANNEAU_MOTS - (l.ecrit - consommes(l));
    if (total > libre) {
        compteurs.debordements[k]++;
        spin_unlock(verrou, s);
        return false;
    }
    uint32_t *a = anneaux[k];
    const uint32_t p = l.ecrit;
    a[p & (ANNEAU_MOTS - 1)] = nb - 1;
    for (uint32_t i = 0; i < nb; ++i)
        a[(p + 1 + i) & (ANNEAU_MOTS - 1)] = brouillon[i];
    __dmb(); /* les mots avant le compteur : le DMA ne doit pas les devancer */
    l.ecrit = p + total;
    compteurs.messages[k]++;
    compteurs.octets[k] += nb * 4u;
    pousser_verrouille(l);
    spin_unlock(verrou, s);

    if (fin)
        *fin = p + total;
    return true;
}

bool transmis(uint8_t k, uint32_t marque) {
    const Liaison &l = liaisons[k];
    const uint32_t s = spin_lock_blocking(verrou);
    const int32_t avance = (int32_t)(consommes(l) - marque);
    spin_unlock(verrou, s);
    if (avance < 0)
        return false;
    /* Ce que le DMA a lu est parti, sauf ce qui attend dans la FIFO et le mot
     * en cours de décalage. */
    if (avance >= (int32_t)(pio_sm_get_tx_fifo_level(l.pio, l.sm) + 1))
        return true;
    return pio_sm_is_tx_fifo_empty(l.pio, l.sm) &&
           pio_sm_get_pc(l.pio, l.sm) == l.offset + lien_tx_offset_attente;
}

bool pret(uint8_t k) { return gpio_get(PIN_RDY_BASE + k); }

void vsync() {
    gpio_put(PIN_VSYNC, 0);
    busy_wait_us_32(2);
    gpio_put(PIN_VSYNC, 1);
}

const Stats &stats() { return compteurs; }

} // namespace lien
