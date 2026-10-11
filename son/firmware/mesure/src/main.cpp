/**
 * Module SON — firmware de mesure, le banc de la phase 0
 *
 * L'ADC convertit sans arrêt, A et B en alternance. Le DMA range les
 * échantillons dans un anneau, sans fin, et sans le processeur. La boucle
 * principale les relit et en tire, toutes les 50 ms, le minimum, le maximum,
 * la moyenne et la valeur efficace de chaque voie. Aucun échantillon n'est
 * gardé : seulement ces chiffres, que la console donne en clair (`g`, `s`) ou
 * en flux pour tools/mesure_son.py (`f`).
 */
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#include "config.h"

namespace {

constexpr uint32_t OCTETS_ANNEAU = 1u << ANNEAU_BITS;
constexpr uint32_t N_ANNEAU = OCTETS_ANNEAU / sizeof(uint16_t);
constexpr uint32_t MASQUE = N_ANNEAU - 1;
constexpr uint32_t PAIRES_PAR_FENETRE = F_VOIE_HZ * FENETRE_MS / 1000;  /* 9 600 */
constexpr uint64_t DUREE_ANNEAU_US =
    (uint64_t)N_ANNEAU * 1000000u / (2u * F_VOIE_HZ);                    /* 42 666 */
constexpr uint32_t FENETRES_PAR_SECONDE = 1000 / FENETRE_MS;

/* 20 pas d'ADC, 16 mV : en dessous, ce n'est que le bruit du montage. */
constexpr uint16_t CC_SILENCE = 20;

/* Aligné sur sa taille : le bouclage d'adresse du DMA l'exige. Un indice pair
 * est un échantillon de la voie A, un impair de la voie B. */
alignas(OCTETS_ANNEAU) uint16_t anneau[N_ANNEAU];
int canal = -1;
uint32_t lu = 0;   /* prochain échantillon à relire */

inline float volts(float pas) { return pas * ADC_VREF_V / (ADC_MAX + 1); }

/* ───────────────────────────────────────────────────────────── mesures */

/* Une fenêtre terminée, en pas d'ADC. */
struct Mesure {
    uint16_t min, max;
    float moyenne, efficace;   /* efficace : la valeur efficace du son seul, sans la moyenne */
    uint32_t saturations;
    uint16_t cc() const { return max - min; }   /* crête-à-crête */
};

/* L'accumulateur d'une voie, le temps d'une fenêtre. */
struct Voie {
    uint32_t n, saturations;
    uint16_t min, max;
    uint64_t somme, carres;

    void vider() {
        n = saturations = 0;
        min = 0xffff;
        max = 0;
        somme = carres = 0;
    }
    void ajouter(uint16_t v) {
        ++n;
        somme += v;
        carres += (uint32_t)v * v;
        if (v < min) min = v;
        if (v > max) max = v;
        if (v == 0 || v == ADC_MAX) ++saturations;
    }
    Mesure clore() const {
        const double moy = (double)somme / n;
        const double var = (double)carres / n - moy * moy;
        return {min, max, (float)moy, (float)std::sqrt(var > 0 ? var : 0),
                saturations};
    }
};

/* Ce qu'une voie a montré sur plusieurs fenêtres. */
struct Cumul {
    uint32_t fenetres, saturations;
    double somme_moyennes;
    uint16_t cc_max;
    float efficace_min, efficace_max;

    void vider() {
        fenetres = saturations = 0;
        somme_moyennes = 0;
        cc_max = 0;
        efficace_min = 1e9f;
        efficace_max = 0;
    }
    void ajouter(const Mesure &m) {
        ++fenetres;
        saturations += m.saturations;
        somme_moyennes += (double)m.moyenne;
        if (m.cc() > cc_max) cc_max = m.cc();
        if (m.efficace < efficace_min) efficace_min = m.efficace;
        if (m.efficace > efficace_max) efficace_max = m.efficace;
    }
    float moyenne() const { return fenetres ? (float)(somme_moyennes / fenetres) : 0; }
};

struct Compteurs {
    uint32_t debordements;  /* la FIFO de l'ADC a débordé : un échantillon perdu */
    uint32_t relances;      /* capture relancée, pour que A et B ne s'échangent pas */
    uint32_t retards;       /* la boucle a relu l'anneau trop tard : fenêtre jetée */
    uint32_t erreurs;       /* conversions signalées fautives par l'ADC */
    uint64_t ecart_max_us;  /* le plus long intervalle entre deux relectures */
};

Voie voie_a, voie_b;
Cumul cumul_a, cumul_b;   /* depuis la remise à zéro, pour `s` */
Cumul diag_a, diag_b;     /* la seconde en cours, pour `g` */
uint32_t diag_restant = 0;
Compteurs cpt;
uint32_t numero_fenetre = 0;
bool fenetre_douteuse = false;  /* vraie si la fenêtre en cours a pu être réécrite */
bool flux = false;
uint64_t t_relecture = 0;

/* ────────────────────────────────────────────────────── capture : ADC + DMA */

void demarrer() {
    adc_run(false);
    if (canal >= 0)
        dma_channel_abort(canal);
    adc_fifo_drain();                      /* attend la conversion en cours */
    adc_select_input(ADC_ENTREE_A);        /* le tourniquet repart de A */
    adc_hw->fcs |= ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS;   /* acquitter */

    lu = 0;
    voie_a.vider();
    voie_b.vider();
    fenetre_douteuse = false;
    t_relecture = time_us_64();

    dma_channel_set_write_addr(canal, anneau, false);
    dma_channel_set_trans_count(canal, dma_encode_endless_transfer_count(), true);
    adc_run(true);
}

void init_capture() {
    adc_init();
    adc_gpio_init(PIN_VOIE_A);   /* coupe l'entrée numérique : errata RP2350-E9 */
    adc_gpio_init(PIN_VOIE_B);
    adc_set_round_robin((1u << ADC_ENTREE_A) | (1u << ADC_ENTREE_B));
    /* FIFO vers le DMA dès un échantillon ; bit 15 = conversion fautive. */
    adc_fifo_setup(true, true, 1, true, false);
    adc_set_clkdiv(ADC_PERIODE - 1);      /* une conversion toutes les 125 périodes */

    canal = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(canal);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, ANNEAU_BITS);   /* l'écriture boucle sur l'anneau */
    channel_config_set_dreq(&c, DREQ_ADC);
    dma_channel_configure(canal, &c, anneau, &adc_hw->fifo,
                          dma_encode_endless_transfer_count(), false);
    demarrer();
}

uint32_t position_dma() {
    const uintptr_t adr = (uintptr_t)dma_hw->ch[canal].write_addr;
    return (uint32_t)((adr - (uintptr_t)anneau) / sizeof(uint16_t)) & MASQUE;
}

/* ─────────────────────────────────────────────────────────── restitution */

void clore_fenetre() {
    const Mesure a = voie_a.clore(), b = voie_b.clore();
    voie_a.vider();
    voie_b.vider();
    ++numero_fenetre;
    if (fenetre_douteuse) {
        fenetre_douteuse = false;
        return;
    }
    cumul_a.ajouter(a);
    cumul_b.ajouter(b);

    if (flux)
        printf("F %lu %u %u %.2f %.2f %lu %u %u %.2f %.2f %lu\n",
               (unsigned long)numero_fenetre,
               a.min, a.max, (double)a.moyenne, (double)a.efficace,
               (unsigned long)a.saturations,
               b.min, b.max, (double)b.moyenne, (double)b.efficace,
               (unsigned long)b.saturations);

    if (diag_restant) {
        diag_a.ajouter(a);
        diag_b.ajouter(b);
        if (--diag_restant == 0) {
            auto verdict = [](const char *nom, int gpio, const Cumul &d) {
                const float moy = volts(d.moyenne());
                const float cc = volts(d.cc_max);
                printf("  voie %s (GP%d)  moyenne %.2f V  ", nom, gpio, (double)moy);
                if (moy < 0.2f)
                    printf("<-- A 0 V : la polarisation n'arrive pas (pont de 100 kOhm, Rb ?)\n");
                else if (moy > 3.1f)
                    printf("<-- A 3,3 V : entree en l'air, ou reliee au 3,3 V\n");
                else if (std::fabs(moy - 1.65f) > 0.25f)
                    printf("<-- decalee : 1,65 V attendus, verifier le pont de 100 kOhm\n");
                else
                    printf("polarisation correcte\n");
                printf("                 ");
                if (d.saturations)
                    printf("SATURE : trop fort pour ce montage, il faut un Rb plus petit\n");
                else if (d.cc_max < CC_SILENCE)
                    printf("aucun son : console eteinte ou silencieuse, ou fil non branche\n");
                else
                    printf("le son passe : %.2f V crete-a-crete au Pico, "
                           "environ %.2f V au point de prise\n",
                           (double)cc, (double)(cc / GAIN_MONTAGE));
            };
            printf("\n--- les deux entrees, sur 1 s ---\n");
            verdict("A", PIN_VOIE_A, diag_a);
            verdict("B", PIN_VOIE_B, diag_b);
        }
    }
}

/* Relit tout ce que le DMA a écrit depuis la dernière fois. */
void relire() {
    const uint64_t t = time_us_64();
    const uint64_t ecart = t - t_relecture;
    t_relecture = t;
    if (ecart > cpt.ecart_max_us)
        cpt.ecart_max_us = ecart;
    if (ecart > DUREE_ANNEAU_US * 8 / 10) {
        /* L'anneau a pu être réécrit sous nos yeux : la fenêtre en cours mélange
         * deux époques. On la jette, en le comptant. */
        ++cpt.retards;
        fenetre_douteuse = true;
    }

    /* Un échantillon perdu décalerait l'alternance : A deviendrait B. Plutôt
     * que de le deviner, on repart de zéro. */
    if (adc_hw->fcs & ADC_FCS_OVER_BITS) {
        ++cpt.debordements;
        ++cpt.relances;
        demarrer();
        return;
    }

    const uint32_t ecrit = position_dma();
    while (lu != ecrit) {
        uint16_t v = anneau[lu];
        if (v & 0x8000)
            ++cpt.erreurs;
        v &= 0x0fff;
        if ((lu & 1) == 0)
            voie_a.ajouter(v);
        else {
            voie_b.ajouter(v);
            if (voie_b.n == PAIRES_PAR_FENETRE)
                clore_fenetre();
        }
        lu = (lu + 1) & MASQUE;
    }
}

void releve() {
    const uint32_t n = cumul_a.fenetres;
    printf("\n--- releve depuis la remise a zero : %lu mesures de %d ms (%.1f s) ---\n",
           (unsigned long)n, FENETRE_MS, n * FENETRE_MS / 1000.0);
    if (!n) {
        printf("  rien encore\n");
        return;
    }
    printf("           moyenne   son le plus fort    le plus calme    saturations\n"
           "                     (crete-a-crete)     (efficace)\n");
    auto ligne = [](const char *nom, const Cumul &c) {
        printf("  voie %s   %.3f V   %.3f V au Pico     %6.2f mV        %lu\n", nom,
               (double)volts(c.moyenne()), (double)volts(c.cc_max),
               (double)(volts(c.efficace_min) * 1000), (unsigned long)c.saturations);
    };
    ligne("A", cumul_a);
    ligne("B", cumul_b);
    printf("\n  debordements FIFO %lu   relances %lu   retards %lu "
           "(plus long ecart %.1f ms)   erreurs ADC %lu\n",
           (unsigned long)cpt.debordements, (unsigned long)cpt.relances,
           (unsigned long)cpt.retards, (double)cpt.ecart_max_us / 1000.0,
           (unsigned long)cpt.erreurs);
}

void remise_a_zero() {
    cumul_a.vider();
    cumul_b.vider();
    cpt = {};
}

void aide() {
    printf("\n  g = les deux entrees, verdict sur 1 s (le montage est-il bon ?)\n"
           "  s = releve depuis la remise a zero\n"
           "  r = remise a zero\n"
           "  f = flux de mesures pour tools/mesure_son.py, marche / arret\n"
           "  h = cette aide\n\n");
}

} // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    printf("\n=== Module SON — banc de mesure ===\n");
    printf("  voie A = GP%d, voie B = GP%d, %lu echantillons/s par voie\n",
           PIN_VOIE_A, PIN_VOIE_B, (unsigned long)F_VOIE_HZ);
    printf("  une mesure toutes les %d ms ; radio eteinte\n", FENETRE_MS);

    remise_a_zero();
    init_capture();
    aide();

    while (true) {
        relire();

        const int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == 'g') {
            diag_a.vider();
            diag_b.vider();
            diag_restant = FENETRES_PAR_SECONDE;
        } else if (c == 's')
            releve();
        else if (c == 'r') {
            remise_a_zero();
            printf("\n  remis a zero\n");
        } else if (c == 'f') {
            flux = !flux;
            printf("\n  flux %s\n", flux ? "MARCHE" : "ARRET");
        } else if (c == 'h')
            aide();
    }
}
