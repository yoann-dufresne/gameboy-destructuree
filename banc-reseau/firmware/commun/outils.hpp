/**
 * Banc de coexistence — petits outils communs aux firmwares
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "banc.h"

namespace outils {

/* Lit la console USB sans bloquer. Rend true quand une ligne complète est
 * arrivée, copiée sans sa fin de ligne dans `buf`. */
bool ligne(char *buf, size_t n);

/* Un histogramme des retards, en seaux de banc.h. */
struct Histo {
    uint32_t n[BANC_SEAUX];
    void noter(uint32_t us) { n[banc_seau(us)]++; }
};

/* Ajoute « cle=i:c,i:c » à `ligne`, seulement les seaux non vides : c'est le
 * format que lit tools/banc.py. Rend la longueur écrite. */
size_t ecrire_histo(char *ligne, size_t place, const char *cle,
                    const uint32_t n[BANC_SEAUX]);

/* Interprète « mot nombre » : rend true si `l` commence par `mot` suivi d'un
 * nombre, rangé dans `v`. */
bool commande(const char *l, const char *mot, int &v);

/* Suit la numérotation d'un flux : compte les manquants et les doublons. Un
 * paquet en retard, arrivé après un plus récent, a déjà été compté manquant :
 * il est compté doublon. Le WiFi en livre peu (module écran, 29/09/2026). */
struct Suivi {
    bool amorce = false;
    uint32_t attendu = 0;

    void noter(uint32_t seq, uint32_t &perdus, uint32_t &doublons) {
        if (!amorce) {
            amorce = true;
            attendu = seq + 1;
        } else if (seq == attendu) {
            attendu++;
        } else if ((int32_t)(seq - attendu) > 0) {
            perdus += seq - attendu;
            attendu = seq + 1;
        } else {
            doublons++;
        }
    }
};

/* Générateur pseudo-aléatoire minimal (xorshift32) : les appuis des fausses
 * manettes, sans dépendre d'une source d'entropie. */
inline uint32_t hasard(uint32_t &etat) {
    etat ^= etat << 13;
    etat ^= etat >> 17;
    etat ^= etat << 5;
    return etat;
}

} // namespace outils
