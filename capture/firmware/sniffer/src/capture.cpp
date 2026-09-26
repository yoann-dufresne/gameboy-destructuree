/**
 * Module CAPTURE — PIO, DMA et les deux interruptions
 *
 * Le CPU ne touche AUCUN pixel. Le PIO échantillonne, le DMA écrit, et le
 * processeur se contente de compter des lignes et de réarmer un pointeur une
 * fois par trame — dans une fenêtre de VBlank de 1,09 ms, soit 163 000 cycles
 * à 150 MHz pour un travail qui en demande quelques dizaines.
 */
#include "capture.hpp"

#include <cstdio>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "config.h"
#include "capture.pio.h"

namespace capture {
namespace {

/* Double tampon. Aligné sur 4 octets : le DMA écrit par mots de 32 bits. */
alignas(4) uint8_t trame[2][OCTETS_TRAME];

PIO pio = pio0;
uint sm = 0;
uint offset_pio = 0;
int canal = -1;

volatile uint8_t idx_capture = 0;   /* tampon en cours de remplissage */
volatile int8_t idx_pret = -1;      /* tampon complet, pas encore lu  */
volatile uint16_t id_trame = 0;
volatile uint16_t id_pret = 0;
volatile uint32_t t_vsync_pret = 0;   /* horodatage de la trame publiée   */
uint32_t t_vsync_rendu = 0;           /* … saisi par trame_prete()         */

volatile uint32_t lignes = 0;       /* impulsions de P2-ST depuis la VSYNC */
Stats compteurs{};

/* Cadence : moyennée depuis la remise à zéro. Une fenêtre d'une seconde ne
 * donne qu'un entier — 59 ou 60 — et ne permet pas de vérifier les 59,727
 * attendus. Sur 60 s, la résolution tombe à 0,017 img/s. */
uint64_t t_depart = 0;

/* ──────────────────────────────────────────────────────────── armement DMA */

void armer_dma(uint8_t idx) {
    dma_channel_set_write_addr(canal, trame[idx], false);
    dma_channel_set_trans_count(canal, MOTS_TRAME, true); /* true = démarre */
}

/* ─────────────────────────────────────────────────────────── interruptions */

/* Front montant de P2-S — le départ de trame.
 *
 * ⚠️ L'ORDRE de cette séquence n'est pas négociable :
 *   - vider le FIFO avant d'arrêter le PIO le laisserait le remplir à nouveau ;
 *   - basculer le tampon avant d'arrêter le DMA le laisserait écrire quelques
 *     mots dans la trame qu'on est en train de publier — une déchirure
 *     intermittente, donc pénible à trouver.
 */
void sur_vsync() {
    /* 1. intégrité : P2-ST doit avoir battu exactement 144 fois */
    compteurs.lignes_derniere = lignes;
    if (lignes != LIGNES_VISIBLES)
        compteurs.trames_douteuses++;

    /* 2. arrêter le DMA, et relever ce qu'il n'a pas eu le temps d'écrire */
    dma_channel_abort(canal);
    compteurs.mots_restants = dma_channel_hw_addr(canal)->transfer_count;

    /* 3. remettre le PIO à zéro : FIFO, mais aussi compteurs de décalage de
     *    l'ISR — une ligne tronquée y laisserait un mot partiel qui décalerait
     *    tout le reste de la trame suivante. */
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset_pio));

    if (pio->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm))) {
        compteurs.debordements++;
        pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    }

    /* 4. publier la trame qui vient de se terminer, et basculer */
    if (idx_pret >= 0)
        compteurs.perdues++;   /* la boucle n'a pas suivi : on écrase */
    idx_pret = (int8_t)idx_capture;
    t_vsync_pret = time_us_32();
    id_pret = id_trame;
    id_trame = id_trame + 1;
    idx_capture ^= 1u;

    /* 5. réarmer sur l'autre tampon et relancer */
    armer_dma(idx_capture);
    pio_sm_set_enabled(pio, sm, true);

    lignes = 0;
    compteurs.trames++;

    gpio_xor_mask(1u << PIN_MESURE_VSYNC);
}

void sur_gpio(uint gpio, uint32_t evenements) {
    if (gpio == PIN_LIGNE && (evenements & GPIO_IRQ_EDGE_RISE)) {
        lignes = lignes + 1;
        return;
    }
    if (gpio == PIN_VSYNC && (evenements & GPIO_IRQ_EDGE_RISE))
        sur_vsync();
}

} // namespace

/* ───────────────────────────────────────────────────────────────── façade */

void init() {
    std::memset(trame, 0, sizeof(trame));

    gpio_init(PIN_MESURE_VSYNC);
    gpio_set_dir(PIN_MESURE_VSYNC, GPIO_OUT);
    gpio_init(PIN_MESURE_EMIS);
    gpio_set_dir(PIN_MESURE_EMIS, GPIO_OUT);

    /* Les deux signaux de synchronisation sont lus par le CPU, pas par le PIO. */
    gpio_init(PIN_LIGNE);
    gpio_set_dir(PIN_LIGNE, GPIO_IN);
    gpio_init(PIN_VSYNC);
    gpio_set_dir(PIN_VSYNC, GPIO_IN);
    /* La réserve n'est pas utilisée par la capture, mais elle DOIT être
     * initialisée : sans gpio_init() l'entrée du pad reste désactivée et
     * gpio_get() renvoie 0 quoi qu'il arrive sur le fil. Le diagnostic
     * annonçait alors « fil non branché » sur un câblage sain. */
    gpio_init(PIN_LIGNE154);
    gpio_set_dir(PIN_LIGNE154, GPIO_IN);

    offset_pio = pio_add_program(pio, &gb_pixels_program);
    gb_pixels_init(pio, sm, offset_pio);

    /* Un SEUL canal : les lignes du framebuffer sont contiguës, donc les
     * frontières de ligne sont implicites et il n'y a pas d'adresse à
     * recalculer. C'est ce que l'émetteur agnostique a fait gagner. */
    canal = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(canal);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);   /* FIFO : adresse fixe   */
    channel_config_set_write_increment(&c, true);   /* framebuffer : avance  */
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    /* Décalage à gauche côté PIO + inversion des octets ici = pixel de gauche
     * dans les bits de poids fort. Démonstration en §D.3 du guide. */
    channel_config_set_bswap(&c, true);
    dma_channel_configure(canal, &c, trame[0], &pio->rxf[sm], MOTS_TRAME, false);

    gpio_set_irq_enabled_with_callback(PIN_LIGNE, GPIO_IRQ_EDGE_RISE, true,
                                       &sur_gpio);
    gpio_set_irq_enabled(PIN_VSYNC, GPIO_IRQ_EDGE_RISE, true);

    armer_dma(idx_capture);
    pio_sm_set_enabled(pio, sm, true);
    t_depart = time_us_64();
}

const uint8_t *trame_prete() {
    const int8_t idx = idx_pret;
    if (idx < 0)
        return nullptr;
    /* Saisir l'horodatage AVANT de libérer le créneau : sinon une VSYNC
     * survenant entre les deux le remplacerait, et la décomposition de
     * latence porterait sur la mauvaise trame. */
    t_vsync_rendu = t_vsync_pret;
    idx_pret = -1;
    return trame[idx];
}

uint16_t numero_trame() { return id_pret; }
uint32_t horodatage_trame() { return t_vsync_rendu; }

const Stats &stats() { return compteurs; }

float cadence() {
    const uint64_t ecoule = time_us_64() - t_depart;
    if (ecoule < 100000ull)
        return 0.0f;
    return (float)compteurs.trames * 1e6f / (float)ecoule;
}

float duree_observation() {
    return (float)(time_us_64() - t_depart) * 1e-6f;
}

void reinitialiser() {
    compteurs = Stats{};
    t_depart = time_us_64();
}

} // namespace capture
