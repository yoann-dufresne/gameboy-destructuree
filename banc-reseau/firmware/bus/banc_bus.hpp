/**
 * Banc du bus de la console (test R3) — le contenu des ordres
 *
 * Au banc, le contenu de chaque ordre se déduit de son numéro : l'esclave
 * recalcule ce qu'il aurait dû recevoir et compte les écarts. Le CRC dit qu'un
 * message est intact ; cette vérification le prouve indépendamment de lui.
 */
#pragma once

#include "bus_console.h"

namespace banc_bus {

inline void remplir(bus_ordre &o, uint8_t seq) {
    o.seq = seq;
    o.drapeaux = (uint8_t)(((seq & 1) ? BUS_MARCHE : 0) |
                           ((seq % 7 == 0) ? BUS_MANETTE_ABSENTE : 0));
    o.luminosite = (uint8_t)(seq * 37u);
    o.volume = (uint8_t)(seq * 101u + 13u);
    o.absence_s = (uint16_t)(seq * 3u);
    o.reste_s = (seq % 5 == 0) ? (uint16_t)BUS_RESTE_INCONNU : (uint16_t)(900u - seq);
}

inline bool conforme(const bus_ordre &o) {
    bus_ordre attendu{};
    remplir(attendu, o.seq);
    return o.drapeaux == attendu.drapeaux && o.luminosite == attendu.luminosite &&
           o.volume == attendu.volume && o.absence_s == attendu.absence_s &&
           o.reste_s == attendu.reste_s;
}

} // namespace banc_bus
