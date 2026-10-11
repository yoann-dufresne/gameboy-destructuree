/**
 * L'automate de la manette : quand émettre, quelle couleur pour la LED, quand
 * se rendormir. Et la lecture des boutons et des molettes.
 *
 * C++ portable, sans pico-sdk : le même code tournera dans la manette et
 * tourne dans son test sur PC (test/test_manette.cpp). Il ne touche à aucune
 * broche : on lui donne ce que lisent les broches et l'heure, il rend le
 * paquet à émettre, la LED et le maintien de l'alimentation. Le plan
 * l'explique aux §4, §5.4 et §5.6.
 *
 * Trois états :
 *
 *   - PRÊTE, interrupteur sur arrêt : connectée, **rien n'est transmis**, ni
 *     boutons ni molettes ;
 *   - MARCHE : à chaque changement, un état part aussitôt, puis deux
 *     répétitions à 4 ms ; sans changement, un battement toutes les 50 ms ;
 *   - FIN, à la bascule sur arrêt : l'ordre « arrêt » part comme un
 *     changement, puis toutes les 50 ms jusqu'à son accusé, 2 s au plus ;
 *     puis PRÊTE.
 *
 * La LED : éteinte sur arrêt ; sur marche, rouge si un accusé est arrivé
 * depuis moins de 500 ms, bleue et pulsée sinon.
 *
 * Le maintien de l'alimentation : tenu en MARCHE et en FIN, et tant que
 * l'accéléromètre se dit éveillé. C'est l'ADXL362 qui compte la minute
 * d'immobilité et relâche sa ligne (plan §4.2) : interrupteur sur arrêt,
 * « arrêt » accusé, ligne relâchée, la manette se rendort.
 */
#pragma once

#include <cstdint>

#include "man1.h"

namespace manette {

enum class Led : uint8_t { ETEINTE, ROUGE, BLEU };

/* Les 8 boutons lus à 1 kHz : un changement part aussitôt, les rebonds des
 * 5 ms suivantes sont ignorés. L'anti-rebond ne retarde rien. */
class AntiRebond {
public:
    static constexpr uint32_t REBOND_MS = 5;

    uint8_t lire(uint8_t brut, uint32_t maintenant) {
        for (unsigned b = 0; b < 8; ++b) {
            const uint8_t masque = (uint8_t)(1u << b);
            if (((brut ^ etat_) & masque) && maintenant - change_ms_[b] >= REBOND_MS) {
                etat_ ^= masque;
                change_ms_[b] = maintenant;
            }
        }
        return etat_;
    }

private:
    uint8_t etat_ = 0;
    uint32_t change_ms_[8] = {};
};

/* Une molette lue par l'ADC (12 bits) : moyenne glissante sur 16 lectures,
 * ramenée à 256 crans de 16 pas, avec une hystérésis d'un cran, un demi de
 * chaque côté, pour que la valeur ne tremble pas entre deux crans. */
class Molette {
public:
    uint8_t lire(uint16_t adc) {
        somme_ = somme_ + adc - hist_[pos_];
        hist_[pos_] = adc;
        pos_ = (pos_ + 1) & 15u;
        if (n_ < 16) ++n_;
        const int32_t x = (int32_t)(somme_ / n_); /* 0..4095 */
        const int32_t c = cran_;
        if (!initialise_ || x >= (c + 1) * 16 + 8 || x < c * 16 - 8) {
            cran_ = (uint8_t)(x / 16);
            initialise_ = true;
        }
        return cran_;
    }

private:
    uint16_t hist_[16] = {};
    uint32_t somme_ = 0;
    unsigned pos_ = 0, n_ = 0;
    uint8_t cran_ = 0;
    bool initialise_ = false;
};

class Manette {
public:
    enum class Etat : uint8_t { PRETE, MARCHE, FIN };

    struct Entrees {
        bool interrupteur = false;  /* sur marche */
        uint8_t boutons = 0;        /* après anti-rebond, ordre MAN1 */
        uint8_t volume = 0, contraste = 0;
        uint16_t piles_mv = 0;
        bool accel_eveille = true;  /* la ligne de l'ADXL362 */
    };

    struct Sorties {
        bool emettre = false;       /* `paquet` part maintenant */
        man1_paquet paquet{};
        Led led = Led::ETEINTE;
        uint8_t lumiere = 0;        /* 0..255, pour le PWM */
        bool maintien = true;       /* GP11 */
    };

    /* `session` : tirée au hasard au démarrage. */
    explicit Manette(uint32_t session) : session_(session) {}

    const Sorties &avancer(const Entrees &e, uint32_t maintenant) {
        Sorties &s = sorties_;
        s.emettre = false;

        switch (etat_) {
        case Etat::PRETE:
            if (e.interrupteur) entrer_marche(e, maintenant);
            break;
        case Etat::MARCHE:
            if (!e.interrupteur) {
                etat_ = Etat::FIN;
                fin_ms_ = maintenant;
                arret_accuse_ = false;
                premier_arret_ = numero_ + 1;
                changement(e, maintenant);
            } else if (e.boutons != envoye_.boutons || e.volume != envoye_.volume ||
                       e.contraste != envoye_.contraste) {
                changement(e, maintenant);
            } else {
                cadence(e, maintenant);
            }
            break;
        case Etat::FIN:
            if (e.interrupteur) {
                entrer_marche(e, maintenant);
            } else if (arret_accuse_ || maintenant - fin_ms_ >= MAN1_ARRET_ABANDON_MS) {
                etat_ = Etat::PRETE; /* le silence */
            } else {
                cadence(e, maintenant);
            }
            break;
        }

        if (!e.interrupteur) {
            s.led = Led::ETEINTE;
            s.lumiere = 0;
        } else if (accuse_vu_ && maintenant - accuse_ms_ < MAN1_ACCUSE_ROUGE_MS) {
            s.led = Led::ROUGE;
            s.lumiere = 255;
        } else {
            /* Bleu, pulsé lentement : une respiration de 2 s, jamais tout à
             * fait éteinte. */
            const uint32_t phase = maintenant % 2000u;
            const uint32_t tri = phase < 1000u ? phase : 2000u - phase; /* 0..1000 */
            s.led = Led::BLEU;
            s.lumiere = (uint8_t)(25 + tri * 230u / 1000u);
        }

        s.maintien = etat_ != Etat::PRETE || e.accel_eveille;
        return s;
    }

    /* Un accusé reçu de l'injecteur. */
    void accuse(const man1_paquet &a, uint32_t maintenant) {
        if (a.magic != MAN1_MAGIC || a.type != MAN1_TYPE_ACCUSE || a.session != session_)
            return;
        accuse_vu_ = true;
        accuse_ms_ = maintenant;
        if (etat_ == Etat::FIN && (int32_t)(a.numero - premier_arret_) >= 0)
            arret_accuse_ = true;
    }

    Etat etat() const { return etat_; }
    uint32_t numero() const { return numero_; }

private:
    void entrer_marche(const Entrees &e, uint32_t maintenant) {
        etat_ = Etat::MARCHE;
        changement(e, maintenant);
    }

    /* Un changement : on émet aussitôt, et deux répétitions suivront. */
    void changement(const Entrees &e, uint32_t maintenant) {
        repetitions_ = MAN1_REPETITIONS;
        emettre(e, maintenant);
    }

    void cadence(const Entrees &e, uint32_t maintenant) {
        if (repetitions_ && maintenant - emis_ms_ >= MAN1_REPETITION_MS) {
            --repetitions_;
            emettre(e, maintenant);
        } else if (!repetitions_ && maintenant - emis_ms_ >= MAN1_BATTEMENT_MS) {
            emettre(e, maintenant);
        }
    }

    void emettre(const Entrees &e, uint32_t maintenant) {
        const bool marche = etat_ == Etat::MARCHE;
        man1_paquet &p = sorties_.paquet;
        p = man1_paquet{};
        p.magic = MAN1_MAGIC;
        p.type = MAN1_TYPE_ETAT;
        p.marche = marche ? 1 : 0;
        /* Sur arrêt, rien d'autre ne part : ni boutons ni molettes. */
        p.boutons = marche ? e.boutons : 0;
        p.volume = marche ? e.volume : 0;
        p.contraste = marche ? e.contraste : 0;
        p.piles_mv = e.piles_mv;
        p.session = session_;
        p.numero = ++numero_;
        p.temps_ms = maintenant;
        sorties_.emettre = true;
        envoye_ = p;
        emis_ms_ = maintenant;
    }

    uint32_t session_;
    Etat etat_ = Etat::PRETE;
    Sorties sorties_;
    man1_paquet envoye_{};
    uint32_t numero_ = 0;
    uint32_t emis_ms_ = 0;
    unsigned repetitions_ = 0;
    uint32_t fin_ms_ = 0;
    uint32_t premier_arret_ = 0;
    bool arret_accuse_ = false;
    bool accuse_vu_ = false;
    uint32_t accuse_ms_ = 0;
};

} // namespace manette
