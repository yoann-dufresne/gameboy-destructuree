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
bool trame_suspecte = false;        /* PIO relancé trop tard pour l'image en cours */
uint8_t delai_premier_courant = DELAI_PREMIER_PIXEL;

/* File de tranches : un producteur (les interruptions), un consommateur (la
 * boucle principale). Huit créneaux pour cinq tranches par trame — largement
 * de quoi absorber une boucle momentanément occupée. */
constexpr uint8_t FILE_N = 8;
Tranche file[FILE_N];
volatile uint8_t file_tete = 0;   /* écrit par l'IRQ    */
volatile uint8_t file_queue = 0;  /* écrit par la boucle */
volatile bool pipeline_on = PIPELINE_PAR_DEFAUT;
Stats compteurs{};

/* Cadence : moyennée depuis la remise à zéro. Une fenêtre d'une seconde ne
 * donne qu'un entier — 59 ou 60 — et ne permet pas de vérifier les 59,727
 * attendus. Sur 60 s, la résolution tombe à 0,017 img/s. */
uint64_t t_depart = 0;

void pousser(const uint8_t *d, uint16_t offset, uint16_t taille,
             uint16_t fid, bool derniere) {
    const uint8_t suivant = (uint8_t)((file_tete + 1) % FILE_N);
    if (suivant == file_queue) {
        compteurs.tranches_perdues++;
        return;
    }
    file[file_tete] = Tranche{d, offset, taille, fid, derniere};
    file_tete = suivant;
}

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
 *     intermittente, donc pénible à trouver ;
 *   - et le PIO doit repartir AVANT tout le reste : le premier pixel de
 *     l'image tombe 19,5 µs après le front de S. Jusqu'au 08/10/2026 il
 *     repartait en dernier, après la tranche finale et la publication, et une
 *     image sur dix environ perdait son premier pixel — décalée d'un cran.
 */
void sur_vsync() {
    /* 1. intégrité : P2-ST doit avoir battu exactement 144 fois. Une de plus :
     *    l'interruption a été servie si tard que le ST de la ligne 0 de l'image
     *    SUIVANTE l'a précédée — le gestionnaire du SDK traite les broches dans
     *    l'ordre, et ST (GP3) passe avant S (GP4). */
    const bool ligne0_deja_vue = lignes == LIGNES_VISIBLES + 1;
    compteurs.lignes_derniere = ligne0_deja_vue ? LIGNES_VISIBLES : lignes;
    if (compteurs.lignes_derniere != LIGNES_VISIBLES)
        compteurs.trames_douteuses++;

    /* 2. arrêter le DMA, et relever ce qu'il n'a pas eu le temps d'écrire */
    dma_channel_abort(canal);
    compteurs.mots_restants = dma_channel_hw_addr(canal)->transfer_count;
    if (compteurs.mots_restants)
        compteurs.trames_incompletes++;

    /* Une image mal capturée n'est pas émise : relancée trop tard, son PIO en
     * a manqué le début ; incomplète, des fronts d'horloge lui manquent.
     * Sans sa dernière tranche, le module écran ne la voit jamais complète et
     * garde la précédente — une image sautée plutôt qu'une image fausse. */
    const bool rejetee = trame_suspecte || compteurs.mots_restants != 0;

    /* 3. remettre le PIO à zéro : FIFO, mais aussi compteurs de décalage de
     *    l'ISR — une ligne tronquée y laisserait un mot partiel qui décalerait
     *    tout le reste de la trame suivante. */
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset_pio + gb_pixels_offset_entree));

    if (pio->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm))) {
        compteurs.debordements++;
        pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    }

    /* 4. réarmer sur l'autre tampon et relancer — tout de suite */
    const uint8_t pleine = idx_capture;
    idx_capture ^= 1u;
    armer_dma(idx_capture);
    pio_sm_set_enabled(pio, sm, true);

    /* Trop tard si ST a déjà levé : la ligne 0 a commencé sans le PIO. Son
     * interruption est alors en attente — on est dans le même gestionnaire —,
     * ou déjà traitée (cas ci-dessus). L'image qui commence sera rejetée. */
    trame_suspecte = ligne0_deja_vue ||
                     (gpio_get_irq_event_mask(PIN_LIGNE) & GPIO_IRQ_EDGE_RISE);
    if (trame_suspecte)
        compteurs.relances_tardives++;

    if (rejetee) {
        compteurs.rejetees++;
    } else {
        /* 5. tranche finale : elle pointe dans le tampon qui vient d'être
         *    rempli, et porte le même frame_id que les précédentes. */
        if (pipeline_on) {
            const uint16_t off = (uint16_t)((TRANCHES_PAR_TRAME - 1) * OCTETS_TRANCHE);
            pousser(trame[pleine] + off, off,
                    (uint16_t)(OCTETS_TRAME - off), id_trame, true);
        }

        /* 6. publier la trame qui vient de se terminer */
        if (idx_pret >= 0)
            compteurs.perdues++;   /* la boucle n'a pas suivi : on écrase */
        idx_pret = (int8_t)pleine;
        t_vsync_pret = time_us_32();
        id_pret = id_trame;
    }
    /* Le numéro avance même pour une image rejetée : ses premières tranches
     * sont déjà parties sous ce numéro. */
    id_trame = id_trame + 1;

    lignes = ligne0_deja_vue ? 1u : 0u;
    compteurs.trames++;

    gpio_xor_mask(1u << PIN_MESURE_VSYNC);
}

void sur_gpio(uint gpio, uint32_t evenements) {
    if (gpio == PIN_LIGNE && (evenements & GPIO_IRQ_EDGE_RISE)) {
        lignes = lignes + 1;
        /* Une tranche est complète dès que ses 35 lignes sont capturées : on
         * n'attend pas la fin de la trame pour l'émettre. Le découpage tombe
         * sur une frontière de ligne, d'où ce test. */
        if (pipeline_on && (lignes % LIGNES_PAR_TRANCHE) == 0) {
            const uint32_t k = lignes / LIGNES_PAR_TRANCHE - 1;
            if (k < TRANCHES_PAR_TRAME - 1) {
                const uint16_t off = (uint16_t)(k * OCTETS_TRANCHE);
                pousser(trame[idx_capture] + off, off, OCTETS_TRANCHE,
                        id_trame, false);
            }
        }
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
    delai_premier(DELAI_PREMIER_PIXEL);
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

bool tranche_prete(Tranche &out) {
    if (file_queue == file_tete)
        return false;
    out = file[file_queue];
    file_queue = (uint8_t)((file_queue + 1) % FILE_N);
    return true;
}

void pipeline(bool actif) {
    /* Vider la file : les tranches en attente appartiennent à l'ancien mode. */
    file_queue = file_tete;
    pipeline_on = actif;
}

bool pipeline_actif() { return pipeline_on; }

/* Le délai vit dans le champ « delay » de l'instruction qui attend le front
 * montant du premier pixel : la réécrire en mémoire d'instructions suffit,
 * même machine d'état en marche — un mot de 16 bits, écrit d'un coup. */
void delai_premier(uint8_t cycles) {
    if (cycles > 31)
        cycles = 31;
    delai_premier_courant = cycles;
    pio->instr_mem[offset_pio + gb_pixels_offset_premier] =
        (uint16_t)(pio_encode_wait_pin(true, 2) | pio_encode_delay(cycles));
}

uint8_t delai_premier() { return delai_premier_courant; }

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
