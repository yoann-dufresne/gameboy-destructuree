/**
 * L'automate de l'injecteur : ce qu'il fait des états reçus des manettes.
 *
 * C++ portable, sans pico-sdk : le même code tournera dans l'injecteur et
 * tourne dans son test sur PC (test/test_injecteur.cpp). Il ne touche à
 * aucune broche : on lui donne les paquets reçus et l'heure, il rend ce
 * qu'il faut poser sur les photoMOS, l'alimentation de la console, et ce
 * qu'il faut dire au reste de la console. Le plan l'explique aux §4 et §6.
 *
 *   - **relâche de sécurité** : sans nouvelles d'une manette depuis 200 ms,
 *     ses boutons sont relâchés. Un bouton ne reste jamais coincé ;
 *   - **« aucune manette »** : console allumée et aucune manette qui la veut
 *     entendue depuis 2 s, le message est demandé au sniffer, avec le temps
 *     restant avant l'extinction ;
 *   - **extinction** : sans nouvelles depuis 15 minutes, la console s'éteint,
 *     comme sur un ordre « arrêt » ;
 *   - **marche et arrêt** : un état « marche » allume la console, un état
 *     « arrêt » l'éteint ; une seconde d'arrêt au moins entre deux, quoi que
 *     demande la manette (§4.3) ;
 *   - **jusqu'à 4 manettes**, une par adresse, .2 à .5 ; leur combinaison est
 *     isolée, provisoire, dans fusion.hpp.
 *
 * Les heures sont en ms, sur 32 bits, et peuvent faire le tour.
 */
#pragma once

#include <cstdint>

#include "fusion.hpp"
#include "man1.h"

namespace manette {

class Injecteur {
public:
    static constexpr uint32_t ARRET_MIN_MS = 1000;

    struct Manette {
        bool connue = false;
        uint32_t vu_ms = 0;
        uint32_t session = 0;
        uint32_t numero = 0;
        bool marche = false;
        uint8_t boutons = 0, volume = 0, contraste = 0;
        uint16_t piles_mv = 0;
    };

    struct Compteurs {
        uint32_t recus = 0;
        uint32_t perdus = 0;      /* sauts dans la numérotation */
        uint32_t anciens = 0;     /* arrivés après un plus récent : ignorés */
        uint32_t relaches = 0;    /* relâches de sécurité, boutons enfoncés */
        uint32_t redemarrages = 0;/* nouvelle session d'une manette connue */
        uint32_t extinctions = 0; /* par absence */
    };

    struct Sorties {
        uint8_t boutons = 0;          /* à poser sur les photoMOS */
        bool console = false;         /* le 3,0 V de la prise DC */
        bool message = false;         /* « aucune manette » */
        uint32_t absence_ms = 0;      /* depuis quand, si message */
        uint32_t extinction_dans_ms = 0;
        uint8_t volume = 0, contraste = 0; /* à transmettre par le bus */
    };

    /* `extinction_ms` : 15 minutes ; 30 s pour le banc (T3.5). */
    explicit Injecteur(uint32_t extinction_ms = MAN1_EXTINCTION_MS)
        : extinction_ms_(extinction_ms) {}

    /* Un paquet reçu de la manette `i` (0 à 3 : adresse .2 + i), à l'heure
     * `maintenant`. Rend vrai et remplit `accuse` s'il faut répondre. */
    bool recevoir(unsigned i, const man1_paquet &p, uint32_t maintenant, man1_paquet *accuse) {
        if (i >= MAN1_MANETTES_MAX || p.magic != MAN1_MAGIC || p.type != MAN1_TYPE_ETAT)
            return false;
        Manette &m = manettes_[i];
        ++compteurs_.recus;
        bool appliquer = true;
        if (!m.connue || p.session != m.session) {
            if (m.connue) ++compteurs_.redemarrages;
            m.connue = true;
            m.session = p.session;
        } else {
            const int32_t saut = (int32_t)(p.numero - m.numero);
            if (saut <= 0) {
                /* Un paquet plus vieux que le dernier appliqué : son état
                 * est périmé. On l'accuse quand même. */
                ++compteurs_.anciens;
                appliquer = false;
            } else if (saut > 1) {
                compteurs_.perdus += (uint32_t)(saut - 1);
            }
        }
        if (appliquer) {
            m.numero = p.numero;
            m.vu_ms = maintenant;
            m.marche = p.marche != 0;
            m.boutons = m.marche ? p.boutons : 0;
            m.volume = p.volume;
            m.contraste = p.contraste;
            m.piles_mv = p.piles_mv;
            fraiche_[i] = true;
        }
        avancer(maintenant);
        if (accuse) {
            *accuse = man1_paquet{};
            accuse->magic = MAN1_MAGIC;
            accuse->type = MAN1_TYPE_ACCUSE;
            accuse->marche = sorties_.console ? 1 : 0;
            accuse->session = p.session;
            accuse->numero = p.numero;
            accuse->temps_ms = maintenant;
        }
        return true;
    }

    /* À appeler souvent, 1 kHz : rend ce qu'il faut faire maintenant. */
    const Sorties &avancer(uint32_t maintenant) {
        fusion::Vue vues[MAN1_MANETTES_MAX];
        bool une_veut = false;
        uint32_t absence = UINT32_MAX;
        for (unsigned i = 0; i < MAN1_MANETTES_MAX; ++i) {
            const Manette &m = manettes_[i];
            const uint32_t depuis = maintenant - m.vu_ms;
            const bool fraiche = m.connue && depuis < MAN1_RELACHE_MS;
            if (fraiche_[i] && !fraiche && m.connue) {
                fraiche_[i] = false;
                if (m.marche && m.boutons) ++compteurs_.relaches;
            }
            const bool veut = m.connue && m.marche && depuis < extinction_ms_;
            if (m.connue && m.marche && !veut && voulait_[i]) ++compteurs_.extinctions;
            voulait_[i] = veut;
            vues[i] = {fraiche, veut, m.marche, m.vu_ms, m.boutons, m.volume, m.contraste};
            if (veut) {
                une_veut = true;
                if (depuis < absence) absence = depuis;
            }
        }

        Sorties &s = sorties_;
        const bool voulue = fusion::console_voulue(vues, MAN1_MANETTES_MAX);
        if (voulue && !s.console) {
            if (!jamais_eteinte_ && maintenant - eteinte_ms_ < ARRET_MIN_MS) {
                /* Une seconde d'arrêt au moins : on attendra. */
            } else {
                s.console = true;
            }
        } else if (!voulue && s.console) {
            s.console = false;
            eteinte_ms_ = maintenant;
            jamais_eteinte_ = false;
        }

        s.boutons = s.console ? fusion::boutons(vues, MAN1_MANETTES_MAX) : 0;
        const int j = fusion::molettes(vues, MAN1_MANETTES_MAX);
        if (j >= 0) {
            s.volume = vues[j].volume;
            s.contraste = vues[j].contraste;
        }
        s.message = s.console && une_veut && absence >= MAN1_MESSAGE_MS;
        s.absence_ms = s.message ? absence : 0;
        s.extinction_dans_ms = s.message ? extinction_ms_ - absence : 0;
        return s;
    }

    const Manette &manette(unsigned i) const { return manettes_[i]; }
    const Compteurs &compteurs() const { return compteurs_; }
    const Sorties &sorties() const { return sorties_; }

private:
    uint32_t extinction_ms_;
    Manette manettes_[MAN1_MANETTES_MAX];
    bool fraiche_[MAN1_MANETTES_MAX] = {};
    bool voulait_[MAN1_MANETTES_MAX] = {};
    Sorties sorties_;
    Compteurs compteurs_;
    uint32_t eteinte_ms_ = 0;
    bool jamais_eteinte_ = true;
};

} // namespace manette
