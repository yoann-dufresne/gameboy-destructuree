#include "radio.hpp"

#include <cstdio>
#include <cstring>

/* cyw43_arch.h d'abord : c'est lui qui inclut cyw43.h dans un bloc extern "C",
 * que cyw43.h n'a pas. Dans l'autre ordre, l'édition de liens échoue. */
#include "pico/cyw43_arch.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "pico/stdlib.h"

#include "secrets.hpp"

/* Serveur DHCP vendorisé par le module écran, écrit en C sans garde extern "C". */
extern "C" {
#include "dhcpserver.h"
}

namespace radio {
namespace {

bool en_point_acces = false;
bool associe = false;
const char *ssid_client = nullptr;
const char *mdp_client = nullptr;
uint32_t t_surveillance_ms = 0;
uint32_t nb_reconnexions = 0;
int nb_stations = 0;
int8_t dernier_rssi = 0;
dhcp_server_t serveur_dhcp;

int etat_lien() {
    cyw43_arch_lwip_begin();
    const int e = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();
    return e;
}

uint32_t mode_economie(uint8_t economie) {
    switch (economie) {
    case 1:  return CYW43_PERFORMANCE_PM;
    case 2:  return CYW43_DEFAULT_PM;
    case 3:  return CYW43_AGGRESSIVE_PM;
    default: return CYW43_NONE_PM;
    }
}

} // namespace

void adresse(ip_addr_t &ip, uint8_t octet3, uint8_t octet4) {
    IP4_ADDR(ip_2_ip4(&ip), 192, 168, octet3, octet4);
}

bool point_acces(const char *ssid, const char *mot_de_passe, uint8_t canal,
                 uint8_t octet3) {
    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }
    en_point_acces = true;
    cyw43_wifi_ap_set_channel(&cyw43_state, canal);
    cyw43_arch_enable_ap_mode(ssid, mot_de_passe, CYW43_AUTH_WPA2_AES_PSK);

    /* Le pilote met toujours son point d'accès en 192.168.4.1 : on le déplace
     * dans le sous-réseau de son type de périphérique. */
    ip_addr_t ip, masque;
    adresse(ip, octet3, 1);
    IP4_ADDR(ip_2_ip4(&masque), 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], ip_2_ip4(&ip),
                   ip_2_ip4(&masque), ip_2_ip4(&ip));
    dhcp_server_init(&serveur_dhcp, &cyw43_state.netif[CYW43_ITF_AP], &ip, &masque);
    cyw43_arch_lwip_end();

    associe = true;
    printf("  reseau « %s » emis sur le canal %u, en 192.168.%u.1\n", ssid,
           canal, octet3);
    return true;
}

bool client(const char *ssid, const char *mot_de_passe, uint8_t octet3,
            uint8_t octet4, uint8_t economie) {
    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, mode_economie(economie));

    /* Adresse fixe : le pilote a lancé un client DHCP à l'activation de
     * l'interface ; on l'arrête avant qu'il ne serve. Le lien passe à « UP »
     * dès l'association, sans attendre de bail. */
    ip_addr_t ip, masque, passerelle;
    adresse(ip, octet3, octet4);
    adresse(passerelle, octet3, 1);
    IP4_ADDR(ip_2_ip4(&masque), 255, 255, 255, 0);
    netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    cyw43_arch_lwip_begin();
    dhcp_stop(n);
    netif_set_addr(n, ip_2_ip4(&ip), ip_2_ip4(&masque), ip_2_ip4(&passerelle));
    cyw43_arch_lwip_end();

    ssid_client = ssid;
    mdp_client = mot_de_passe;
    printf("  association a « %s » en 192.168.%u.%u, economie %u...\n", ssid,
           octet3, octet4, economie);
    /* Asynchrone : la version bloquante gèle la boucle jusqu'à 30 s (module
     * capture, 26/09/2026). entretenir() surveille la suite. */
    cyw43_arch_wifi_connect_async(ssid, mot_de_passe, CYW43_AUTH_WPA2_AES_PSK);
    return true;
}

void entretenir() {
    const uint32_t maintenant = to_ms_since_boot(get_absolute_time());
    if (maintenant - t_surveillance_ms < 1000u)
        return;
    t_surveillance_ms = maintenant;

    if (en_point_acces) {
        /* ⚠️ Avec un tableau d'adresses nul, le pilote rend le nombre MAXIMAL
         * de stations, pas celui des connectées (module écran, 29/09/2026). */
        uint8_t macs[6 * 8];
        int n = 8;
        cyw43_arch_lwip_begin();
        cyw43_wifi_ap_get_stas(&cyw43_state, &n, macs);
        cyw43_arch_lwip_end();
        nb_stations = n;
        return;
    }

    const int e = etat_lien();
    if (e == CYW43_LINK_UP) {
        if (!associe) {
            associe = true;
            nb_reconnexions++;
            printf("\n  reseau OPERATIONNEL\n");
        }
        int32_t r = 0;
        cyw43_arch_lwip_begin();
        if (cyw43_wifi_get_rssi(&cyw43_state, &r) == 0)
            dernier_rssi = (int8_t)r;
        cyw43_arch_lwip_end();
        return;
    }
    if (associe) {
        associe = false;
        printf("\n  reseau PERDU (etat %d) — nouvelle association chaque seconde\n", e);
    }
    if (e == CYW43_LINK_JOIN || e == CYW43_LINK_NOIP)
        return; /* une association est en cours */
    cyw43_arch_wifi_connect_async(ssid_client, mdp_client, CYW43_AUTH_WPA2_AES_PSK);
}

bool pret() { return associe; }
int stations() { return nb_stations; }
int8_t rssi() { return dernier_rssi; }
uint32_t reconnexions() { return nb_reconnexions; }

const char *etat() {
    if (en_point_acces)
        return "point d'acces";
    switch (etat_lien()) {
    case CYW43_LINK_DOWN:    return "DOWN (hors ligne)";
    case CYW43_LINK_JOIN:    return "JOIN (association en cours)";
    case CYW43_LINK_NOIP:    return "NOIP (associe, sans adresse)";
    case CYW43_LINK_UP:      return "UP (operationnel)";
    case CYW43_LINK_FAIL:    return "FAIL (echec)";
    case CYW43_LINK_NONET:   return "NONET (reseau introuvable)";
    case CYW43_LINK_BADAUTH: return "BADAUTH (mot de passe ?)";
    default:                 return "?";
    }
}

udp_pcb *ouvrir(uint16_t port, udp_recv_fn cb) {
    cyw43_arch_lwip_begin();
    udp_pcb *pcb = udp_new();
    const bool lie = pcb != nullptr && udp_bind(pcb, IP_ANY_TYPE, port) == ERR_OK;
    if (lie)
        udp_recv(pcb, cb, nullptr);
    cyw43_arch_lwip_end();
    if (!lie) {
        printf("  udp_new / udp_bind sur %u : ECHEC\n", port);
        return nullptr;
    }
    return pcb;
}

bool envoyer(udp_pcb *pcb, const ip_addr_t &ip, uint16_t port,
             const void *entete, uint16_t n_entete,
             const void *charge, uint16_t n_charge, uint16_t n_total) {
    if (pcb == nullptr)
        return false;
    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n_total, PBUF_RAM);
    if (p == nullptr)
        return false;
    std::memcpy(p->payload, entete, n_entete);
    if (charge != nullptr && n_charge)
        std::memcpy((uint8_t *)p->payload + n_entete, charge, n_charge);
    const err_t r = udp_sendto(pcb, p, &ip, port);
    pbuf_free(p);
    return r == ERR_OK;
}

} // namespace radio
