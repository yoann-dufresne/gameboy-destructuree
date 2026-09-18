/**
 * Module ÉCRAN — WiFi et réception PXL1
 *
 * Trois choix dictés par la latence (plan §5, phase 2) :
 *
 *   1. L'économie d'énergie WiFi est COUPÉE. C'est de très loin le premier levier :
 *      laissée active, elle ajoute 10 à 100 ms. Tout le reste est cosmétique à côté.
 *   2. API raw de lwIP (`udp_recv`), pas l'API sockets : pas de copie
 *      supplémentaire ni de réveil de tâche.
 *   3. Chaque tranche est écrite dans le tampon de réception dès son arrivée, au
 *      lieu d'attendre la trame complète. En BGR888 la charge utile a déjà le bon
 *      format : c'est une simple recopie, sans conversion.
 *
 * DOUBLE TAMPON, et ici il est nécessaire — contrairement à celui qu'on avait
 * superposé côté affichage. La réception court en contexte d'interruption : sans
 * double tampon, elle écrirait dans le tampon que l'affichage est en train de
 * convertir, et l'image se déchirerait. Mesuré le 18/09/2026 : 31 écritures
 * concurrentes en 40 s.
 */

#include "reseau.hpp"
#include "config.h"
#include "pxl1.h"
#include "secrets.h"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

namespace {

/* Deux tampons : la réception remplit l'un pendant que l'affichage lit l'autre. */
alignas(4) uint8_t tampons[2][FB_OCTETS];

volatile uint8_t idx_reception = 0;
volatile int8_t idx_pret = -1; /* index d'une trame complète en attente */

uint8_t mon_node_id = 0;

udp_pcb *pcb = nullptr;

reseau::Stats compteurs{};

uint16_t frame_courante = 0;
bool frame_en_cours = false;
uint32_t octets_frame = 0;

char ip_texte[16] = "0.0.0.0";

/* Appelé par lwIP à chaque datagramme. Contexte d'interruption en mode
 * `threadsafe_background` : rester court. La publication elle-même est laissée
 * à la boucle principale, qui ne fait qu'attendre. */
void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *, u16_t) {
    if (p == nullptr)
        return;

    compteurs.paquets++;

    if (p->tot_len < PXL1_ENTETE || p->tot_len > PXL1_ENTETE + PXL1_CHARGE_MAX) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    pxl1_entete e;
    pbuf_copy_partial(p, &e, PXL1_ENTETE, 0);

    if (e.magic != PXL1_MAGIC || e.type != PXL1_TYPE_FRAME ||
        e.node_id != mon_node_id || e.format != PXL1_FMT_BGR888) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    const uint16_t charge = (uint16_t)(p->tot_len - PXL1_ENTETE);

    if (e.offset + charge > FB_OCTETS) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    /* Nouvelle trame ? On solde la précédente. */
    if (!frame_en_cours || e.frame_id != frame_courante) {
        if (frame_en_cours && octets_frame < FB_OCTETS)
            compteurs.trames_incompletes++;
        frame_courante = e.frame_id;
        frame_en_cours = true;
        octets_frame = 0;
    }

    /* Le format BGR888 du protocole est déjà celui de la dalle : simple recopie. */
    pbuf_copy_partial(p, tampons[idx_reception] + e.offset, charge, PXL1_ENTETE);
    octets_frame += charge;

    if (e.flags & PXL1_FLAG_DERNIERE) {
        if (octets_frame < FB_OCTETS)
            compteurs.trames_incompletes++;
        compteurs.trames++;
        frame_en_cours = false;

        /* Une trame non encore publiée est abandonnée : on affiche toujours la
         * plus récente. C'est le bon comportement pour un afficheur, et c'est
         * aussi ce qui protège le pilote d'un rappel trop rapproché. */
        if (idx_pret >= 0)
            compteurs.ecartees++;

        idx_pret = (int8_t)idx_reception;
        idx_reception ^= 1u;
    }

    pbuf_free(p);
}

} // namespace

namespace reseau {

bool connecter(uint8_t node_id) {
    mon_node_id = node_id;
    std::memset(tampons, 0, sizeof(tampons));

    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }

    cyw43_arch_enable_sta_mode();

    /* LE levier de latence. Sans cela, 10 à 100 ms s'ajoutent à chaque trame. */
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

    printf("  association a « %s »...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("  association : ECHEC (SSID, mot de passe, ou hors de portee ?)\n");
        return false;
    }

    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    std::snprintf(ip_texte, sizeof(ip_texte), "%s", ip4addr_ntoa(ip));

    pcb = udp_new();
    if (pcb == nullptr) {
        printf("  udp_new : ECHEC\n");
        return false;
    }
    if (udp_bind(pcb, IP_ANY_TYPE, PXL1_PORT) != ERR_OK) {
        printf("  udp_bind sur %d : ECHEC\n", PXL1_PORT);
        return false;
    }
    udp_recv(pcb, sur_paquet, nullptr);

    return true;
}

const uint8_t *trame_a_afficher() {
    const int8_t idx = idx_pret;
    if (idx < 0)
        return nullptr;
    idx_pret = -1;
    return tampons[idx];
}

const Stats &stats() { return compteurs; }

const char *adresse_ip() { return ip_texte; }

} // namespace reseau
