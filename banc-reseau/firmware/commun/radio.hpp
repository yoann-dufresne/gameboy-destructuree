/**
 * Banc de coexistence — la radio, point d'accès ou client
 *
 * Repris des modules : le point d'accès de la tête de l'écran (canal, DHCP
 * vendorisé) et la surveillance du lien du sniffer (association asynchrone,
 * relancée chaque seconde). Une différence : le client prend une adresse FIXE,
 * comme le feront les périphériques (reseau.md §4), sans attendre de DHCP.
 */
#pragma once

#include <cstdint>

#include "lwip/ip_addr.h"
#include "lwip/udp.h"

namespace radio {

/* Émet le réseau `ssid` sur `canal`, en 192.168.<octet3>.1/24, et distribue
 * .16 à .23 par DHCP à qui en demande, le PC par exemple. */
bool point_acces(const char *ssid, const char *mot_de_passe, uint8_t canal,
                 uint8_t octet3);

/* Rejoint `ssid` avec l'adresse fixe 192.168.<octet3>.<octet4>. `economie` :
 * 0 aucune (le levier de latence des modules), 1 performance, 2 défaut du
 * pilote, 3 agressive. L'association est asynchrone : ne bloque pas. */
bool client(const char *ssid, const char *mot_de_passe, uint8_t octet3,
            uint8_t octet4, uint8_t economie);

/* À appeler en boucle. Client : relance l'association quand le lien tombe. */
void entretenir();

bool pret();                 /* client : associé, avec son adresse ; point d'accès : émis */
int stations();              /* point d'accès : stations associées (mis à jour chaque seconde) */
int8_t rssi();               /* client : puissance reçue, dBm (mise à jour chaque seconde) */
uint32_t reconnexions();     /* client : associations réussies */
const char *etat();          /* libellé pour la console */
void adresse(ip_addr_t &ip, uint8_t octet3, uint8_t octet4);

/* Ouvre un pcb UDP lié à `port`, rappel `cb` à chaque datagramme. */
udp_pcb *ouvrir(uint16_t port, udp_recv_fn cb);

/* Envoie `n_total` octets : l'en-tête, puis `charge` si elle est donnée, le
 * reste sans contenu utile. SANS verrou : à appeler depuis un rappel lwIP ou
 * entre cyw43_arch_lwip_begin() et _end(). */
bool envoyer(udp_pcb *pcb, const ip_addr_t &ip, uint16_t port,
             const void *entete, uint16_t n_entete,
             const void *charge, uint16_t n_charge, uint16_t n_total);

} // namespace radio
