/**
 * La réserve du haut-parleur, et le rattrapage de la dérive des horloges.
 *
 * C++ portable, sans pico-sdk : le même code tourne dans le haut-parleur et
 * dans son test sur PC (test/test_rattrapage.cpp). Le plan l'explique au §6.
 *
 * Le problème, sans jargon : la source compte ses échantillons au rythme de
 * son quartz, le haut-parleur les joue au rythme du sien. Deux quartz
 * diffèrent de quelques dizaines de millionièmes : à 50 ppm, la réserve
 * gagne ou perd 2,4 échantillons par seconde, et finit par déborder ou se
 * vider. Jeter ou doubler un échantillon de temps en temps s'entendrait sur
 * une note tenue.
 *
 * La solution : le haut-parleur lit la réserve un tout petit peu plus vite
 * ou plus lentement que 48 kHz, d'un pas réglé en continu d'après son
 * remplissage, et calcule les échantillons qui tombent entre deux par
 * interpolation (Catmull-Rom, sur quatre points). Le pas ne s'écarte jamais
 * de plus de 1000 ppm de 1 : 1,7 centième de demi-ton, inaudible.
 *
 * Ce que fait la réserve :
 *
 *   - **elle range chaque bloc d'après son numéro** (son1.h) : l'ordre
 *     d'arrivée ne compte pas, un bloc en retard prend sa place ;
 *   - **elle masque les trous** : un échantillon absent à l'heure de le jouer
 *     est remplacé par le dernier joué, qui s'éteint doucement (4 ms) ; au
 *     retour du son, un fondu de 2 ms le rejoint. Ni silence brutal, ni
 *     claquement ;
 *   - **elle repart** si elle se vide (la source s'est tue) ou si la
 *     numérotation saute (la source a redémarré) : elle attend d'avoir de
 *     nouveau sa réserve pleine avant de rejouer.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "son1.h"

namespace son {

class Rattrapage {
public:
    static constexpr uint32_t CAPACITE = 8192; /* trames : 170 ms, puissance de 2 */
    static constexpr uint32_t PERIODE_REGLAGE = 48; /* trames : 1 ms */
    static constexpr double PAS_MAX = 1000e-6;

    struct Compteurs {
        uint32_t masquees = 0;      /* trames remplacées faute d'échantillon */
        uint32_t trop_tard = 0;     /* blocs arrivés après leur heure */
        uint32_t debordements = 0;  /* réserve trop pleine : lecture avancée */
        uint32_t reprises = 0;      /* réserve vidée, ou numérotation qui saute */
    };

    /* `cible` : la réserve visée, en trames ; 1920 = 40 ms à 48 kHz. */
    explicit Rattrapage(uint32_t cible = 1920) : cible_(cible) { reinitialiser(); }

    void reinitialiser() {
        for (auto &p : present_) p = 0;
        etat_ = Etat::VIDE;
        frac_ = 0.0f;
        pas_ = 1.0f;
        integrale_ = 0.0;
        niveau_lisse_ = 0.0;
        compte_reglage_ = 0;
        fondu_ = 0;
        for (int v = 0; v < 2; ++v) masque_[v] = 0.0f;
    }

    /* Un bloc reçu : `numero` est celui de son premier échantillon, `gd` ses
     * `n` trames entrelacées G, D. */
    void deposer(uint32_t numero, const int16_t *gd, uint32_t n) {
        if (n == 0 || n > CAPACITE / 2) return;
        if (etat_ == Etat::VIDE) {
            demarrer(numero);
        } else {
            const int32_t ecart = son1_ecart(lecture_, numero);
            if (ecart > (int32_t)(4 * CAPACITE) || ecart < -(int32_t)(4 * CAPACITE)) {
                /* La source a redémarré, ou s'est tue longtemps. On garde le
                 * pas : les deux quartz n'ont pas changé ; et le dernier
                 * échantillon joué, qui s'éteint doucement. */
                ++compteurs_.reprises;
                for (auto &p : present_) p = 0;
                demarrer(numero);
            } else if (ecart + (int32_t)n <= 0) {
                ++compteurs_.trop_tard;
                return;
            } else if (ecart + (int32_t)n > (int32_t)(CAPACITE - 8)) {
                /* Trop d'avance : on avance la lecture pour garder la cible. */
                ++compteurs_.debordements;
                const uint32_t nouvelle = numero + n - cible_;
                /* Ce qui passe derrière la lecture est oublié ; le reste,
                 * déjà reçu, est gardé. */
                int32_t oublis = son1_ecart(lecture_, nouvelle);
                if (oublis > (int32_t)CAPACITE) oublis = CAPACITE;
                for (int32_t i = 0; i < oublis; ++i)
                    present_[(lecture_ - 1 + i) & (CAPACITE - 1)] = 0;
                lecture_ = nouvelle;
                frac_ = 0.0f;
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t k = numero + i;
            const int32_t d = son1_ecart(lecture_, k);
            if (d < -1 || d >= (int32_t)(CAPACITE - 2)) continue;
            const uint32_t j = k & (CAPACITE - 1);
            tampon_[j][0] = gd[2 * i];
            tampon_[j][1] = gd[2 * i + 1];
            present_[j] = 1;
        }
        if (son1_ecart(fin_, numero + n) > 0) fin_ = numero + n;
    }

    /* `n` trames G, D au rythme de la sortie : c'est elle qui donne l'heure. */
    void produire(int16_t *sortie, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            float y[2];
            trame(y);
            for (int v = 0; v < 2; ++v) {
                const float r = y[v] + (y[v] >= 0 ? 0.5f : -0.5f);
                sortie[2 * i + v] = (int16_t)(r > 32767.f ? 32767 : (r < -32768.f ? -32768 : (int)r));
            }
            if (++compte_reglage_ == PERIODE_REGLAGE) {
                compte_reglage_ = 0;
                regler();
            }
        }
    }

    /* La dérive estimée : de combien la source avance sur la sortie, en
     * millionièmes. C'est le pas, moins 1. */
    double derive_ppm() const { return (pas_ - 1.0) * 1e6; }
    float pas() const { return pas_; }
    bool joue() const { return etat_ == Etat::LECTURE; }
    /* Remplissage de la réserve, en trames. */
    double niveau() const {
        return etat_ == Etat::VIDE ? 0.0 : (double)son1_ecart(lecture_, fin_) - frac_;
    }
    double niveau_lisse() const { return niveau_lisse_; }
    const Compteurs &compteurs() const { return compteurs_; }

private:
    enum class Etat { VIDE, AMORCE, LECTURE };

    void demarrer(uint32_t numero) {
        lecture_ = numero;
        fin_ = numero;
        frac_ = 0.0f;
        etat_ = Etat::AMORCE;
        niveau_lisse_ = cible_;
    }

    bool la(uint32_t k) const { return present_[k & (CAPACITE - 1)]; }
    float val(uint32_t k, int v) const { return tampon_[k & (CAPACITE - 1)][v]; }

    void trame(float y[2]) {
        if (etat_ == Etat::AMORCE && niveau() >= cible_) {
            etat_ = Etat::LECTURE;
            fondu_ = 1;
        }
        if (etat_ != Etat::LECTURE) {
            eteindre(y);
            return;
        }
        if (niveau() <= 3.0) {
            /* La réserve est vide : la source s'est tue. On s'éteint
             * doucement et on attend de nouveau la réserve pleine. */
            ++compteurs_.reprises;
            etat_ = Etat::AMORCE;
            niveau_lisse_ = cible_;
            eteindre(y);
            return;
        }
        const uint32_t l = lecture_;
        if (la(l - 1) && la(l) && la(l + 1) && la(l + 2)) {
            const float t = frac_;
            for (int v = 0; v < 2; ++v) {
                const float p0 = val(l - 1, v), p1 = val(l, v), p2 = val(l + 1, v),
                            p3 = val(l + 2, v);
                float s = p1 + 0.5f * t *
                                   (p2 - p0 +
                                    t * (2.f * p0 - 5.f * p1 + 4.f * p2 - p3 +
                                         t * (3.f * (p1 - p2) + p3 - p0)));
                if (fondu_) {
                    /* Rejoindre le son depuis le masquage, en 2 ms. */
                    const float g = fondu_ / (float)FONDU;
                    s = g * s + (1.f - g) * masque_[v] * DECLIN;
                }
                y[v] = s;
                masque_[v] = s;
            }
            if (fondu_ && ++fondu_ > FONDU) fondu_ = 0;
        } else {
            ++compteurs_.masquees;
            eteindre(y);
            fondu_ = 1;
        }
        avancer();
    }

    /* Le dernier échantillon joué, qui s'éteint en 4 ms environ. */
    void eteindre(float y[2]) {
        for (int v = 0; v < 2; ++v) {
            masque_[v] *= DECLIN;
            y[v] = masque_[v];
        }
    }

    void avancer() {
        frac_ += pas_;
        while (frac_ >= 1.0f) {
            frac_ -= 1.0f;
            present_[(lecture_ - 1) & (CAPACITE - 1)] = 0;
            ++lecture_;
        }
    }

    /* Une fois par milliseconde : un régulateur proportionnel et intégral
     * sur le remplissage lissé. Réglé pour une période propre d'une
     * vingtaine de secondes, bien amortie : la gigue du WiFi, lissée sur une
     * seconde, ne fait presque pas bouger le pas. */
    void regler() {
        if (etat_ != Etat::LECTURE) return;
        niveau_lisse_ += LISSAGE * (niveau() - niveau_lisse_);
        const double e = niveau_lisse_ - cible_;
        integrale_ += KI * e;
        if (integrale_ > PAS_MAX) integrale_ = PAS_MAX;
        if (integrale_ < -PAS_MAX) integrale_ = -PAS_MAX;
        double p = KP * e + integrale_;
        if (p > PAS_MAX) p = PAS_MAX;
        if (p < -PAS_MAX) p = -PAS_MAX;
        pas_ = (float)(1.0 + p);
    }

    static constexpr float DECLIN = 0.995f; /* par trame : −63 % en 4,2 ms */
    static constexpr uint32_t FONDU = 96;   /* trames : 2 ms */
    static constexpr double LISSAGE = 0.001; /* par réglage : 1 s */
    static constexpr double KP = 1.05e-5;    /* par trame d'écart */
    static constexpr double KI = 2.06e-9;    /* par trame d'écart et par réglage */

    uint32_t cible_;
    Etat etat_;
    uint32_t lecture_ = 0; /* numéro de la trame lue, partie entière */
    uint32_t fin_ = 0;     /* numéro qui suit le bloc le plus récent reçu */
    float frac_;
    float pas_;
    double integrale_;
    double niveau_lisse_;
    uint32_t compte_reglage_;
    uint32_t fondu_;
    float masque_[2];
    int16_t tampon_[CAPACITE][2];
    uint8_t present_[CAPACITE];
    Compteurs compteurs_;
};

} // namespace son
