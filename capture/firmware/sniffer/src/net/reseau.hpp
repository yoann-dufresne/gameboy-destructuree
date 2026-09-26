/**
 * Module CAPTURE — façade réseau
 *
 * Associe la carte au WiFi et émet les trames en `PXL1`.
 *
 * ⚠️ **Tout tourne sur le cœur 0**, contrairement à ce qu'annonçait le plan.
 * La capture n'a pas besoin d'un cœur : elle est en PIO + DMA, et ses deux
 * interruptions s'exécutent de toute façon sur le cœur qui les a armées. Un
 * second cœur n'aurait rien apporté — et `cyw43_arch_init` appelé depuis le
 * cœur 1 bloquait le démarrage (26/09/2026).
 *
 * Les interruptions de capture préemptent la pile réseau, et la VBlank laisse
 * 1,09 ms de marge au gestionnaire de trame.
 */
#pragma once

#include <cstdint>

namespace reseau {

/* Une grandeur observée : min, vraie moyenne, max, sur un nombre
 * d'échantillons affiché. Pas de filtre glissant — il mélange les régimes et
 * rend un chiffre que rien ne recoupe (constaté le 26/09/2026). */
struct Mesure {
    uint32_t n;
    uint64_t somme_us;
    uint32_t min_us;
    uint32_t max_us;
};

#define RESEAU_HISTO_SEAUX 33   /* 0..31 ms, puis « >= 32 » */

struct Stats {
    uint32_t trames;      /* trames émises                                  */
    uint32_t paquets;     /* datagrammes émis                               */
    uint32_t octets;      /* octets de charge utile émis                    */
    uint32_t echecs;      /* udp_sendto en échec — file lwIP pleine ?        */
    uint32_t ctrl;        /* paquets de commande émis                       */
    uint32_t accuses;     /* accusés reçus du récepteur                     */
    /* ── Décomposition de la latence, sur NOTRE horloge ────────────────
     * Aucune synchronisation supposée avec le récepteur. */
    Mesure attente;      /* fin de capture → départ du 1er paquet.
                          * C'est la réactivité de la boucle principale. */
    Mesure emission;     /* départ du 1er paquet → fin du 5e.
                          * C'est le coût de lwIP + CYW43. */
    Mesure aller_retour; /* départ du 1er paquet → accusé reçu. */

    uint32_t aller_retour_us;      /* dernier mesuré, pour l'affichage      */
    uint32_t aller_retour_ecartes; /* hors bornes : horodatage recyclé      */

    /* Histogramme de l'aller-retour, un seau par milliseconde. min/moy/max
     * ne disent pas si un maximum est une valeur isolée ou une queue de
     * distribution — c'est la question que pose un « max 54 ms ». */
    uint32_t histo[RESEAU_HISTO_SEAUX];
};

/* Associe au WiFi et arme l'émission. Rend false si l'association échoue —
 * auquel cas la capture continue et les vidages par la console restent
 * utilisables, ce qui permet de distinguer un problème de capture d'un
 * problème d'émission. */
bool init();

/* Émet une trame. Appelé par la boucle principale, qui possède déjà le
 * pointeur — c'est elle qui consomme `capture::trame_prete()`, pour pouvoir
 * aussi le vider sur la console. */
void emettre(const uint8_t *trame, uint16_t frame_id,
             uint32_t t_vsync_us);

/* À appeler en boucle : envoie les commandes à leur cadence. Ne bloque pas. */
void servir();

/* Associé au WiFi et prêt à émettre ? */
bool pret();

/* Adresse IP obtenue, ou "0.0.0.0". */
const char *adresse_ip();

const Stats &stats();
void reinitialiser();

} // namespace reseau
