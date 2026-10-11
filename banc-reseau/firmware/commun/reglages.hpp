/**
 * Banc de coexistence — les réglages gardés en flash
 *
 * Le canal d'un point d'accès, le numéro d'un client, son mode d'économie :
 * réglés par la console série (ou par tools/banc.py, qui tape les commandes à
 * ta place), gardés dans le dernier secteur de la flash, et appliqués au
 * redémarrage. Le firmware ne change pas : un seul .uf2 par rôle.
 *
 * Écrire la flash bloque sa lecture, et le WiFi tourne : chaque réglage est
 * donc suivi d'un redémarrage, sans chercher à reprendre là où on en était.
 */
#pragma once

#include <cstdint>

namespace reglages {

struct Reglages {
    uint8_t canal;    /* point d'accès : 1..13 ; 0 = celui du plan à trois canaux */
    uint8_t numero;   /* client : 1..4, adresse .2 à .5                           */
    uint8_t economie; /* client : 0 aucune, 1 performance, 2 défaut, 3 agressive   */
};

/* Les réglages gardés, ou ceux par défaut si la flash n'en a pas de valides. */
Reglages lire();

/* Écrit, puis redémarre la carte. Ne rend pas la main. */
[[noreturn]] void ecrire_et_redemarrer(const Reglages &r);

} // namespace reglages
