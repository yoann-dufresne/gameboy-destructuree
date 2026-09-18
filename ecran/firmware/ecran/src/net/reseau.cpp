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
volatile uint16_t id_pret = 0;
volatile uint64_t t_premier_pret = 0, t_dernier_pret = 0;

uint64_t t_premier_courant = 0;

/* Adresse de l'émetteur, apprise du dernier paquet reçu : l'accusé lui revient
 * sans qu'aucune adresse soit à configurer. */
ip_addr_t emetteur_ip;
u16_t emetteur_port = 0;
bool emetteur_connu = false;

uint8_t mon_node_id = 0;

udp_pcb *pcb = nullptr;

reseau::Stats compteurs{};

uint16_t frame_courante = 0;
uint8_t format_courant = PXL1_FMT_BGR888;
bool frame_en_cours = false;
uint32_t octets_frame = 0;
volatile uint8_t format_pret = PXL1_FMT_BGR888;

/* Palette pour le format indexé, en B,G,R comme la dalle. Par défaut une rampe
 * de gris : une trame indexée s'affiche donc de façon sensée même si la palette
 * n'a pas encore été reçue. */
uint8_t palette[PXL1_PALETTE_OCTETS];

void palette_par_defaut() {
    for (int i = 0; i < PXL1_PALETTE_ENTREES; ++i) {
        palette[i * 3 + 0] = (uint8_t)i;
        palette[i * 3 + 1] = (uint8_t)i;
        palette[i * 3 + 2] = (uint8_t)i;
    }
}

/* Taille utile d'une trame selon son format. */
inline uint32_t taille_trame(uint8_t format) {
    return (format == PXL1_FMT_IDX8) ? (DISPLAY_W * DISPLAY_H) : (uint32_t)FB_OCTETS;
}

char ip_texte[16] = "0.0.0.0";

/* Changement de luminosité demandé par une commande. Appliqué par la boucle
 * principale : setBasisBrightness reconstruit les commandes de ligne, ce qui
 * n'a rien à faire dans un contexte d'interruption. */
volatile uint8_t demande_luminosite = 0;

/* Paquet de commande : palette, luminosité. Hors du chemin critique des pixels. */
void traiter_ctrl(pbuf *p, uint16_t charge) {
    if (charge < 1)
        return;
    uint8_t commande;
    pbuf_copy_partial(p, &commande, 1, PXL1_ENTETE);

    if (commande == PXL1_CTRL_PALETTE && charge >= 1 + PXL1_PALETTE_OCTETS) {
        pbuf_copy_partial(p, palette, PXL1_PALETTE_OCTETS, PXL1_ENTETE + 1);
        compteurs.ctrl++;
    } else if (commande == PXL1_CTRL_LUMINOSITE && charge >= 2) {
        uint8_t basis;
        pbuf_copy_partial(p, &basis, 1, PXL1_ENTETE + 1);
        demande_luminosite = basis ? basis : 1;
        compteurs.ctrl++;
    }
}

/* Appelé par lwIP à chaque datagramme. Contexte d'interruption en mode
 * `threadsafe_background` : rester court. La publication elle-même est laissée
 * à la boucle principale, qui ne fait qu'attendre. */
void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *source, u16_t port) {
    if (p == nullptr)
        return;

    compteurs.paquets++;
    if (source != nullptr) {
        emetteur_ip = *source;
        emetteur_port = port;
        emetteur_connu = true;
    }

    if (p->tot_len < PXL1_ENTETE || p->tot_len > PXL1_ENTETE + PXL1_CHARGE_MAX) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    pxl1_entete e;
    pbuf_copy_partial(p, &e, PXL1_ENTETE, 0);

    if (e.magic != PXL1_MAGIC || e.node_id != mon_node_id) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    const uint16_t charge = (uint16_t)(p->tot_len - PXL1_ENTETE);

    if (e.type == PXL1_TYPE_CTRL) {
        traiter_ctrl(p, charge);
        pbuf_free(p);
        return;
    }

    if (e.type != PXL1_TYPE_FRAME ||
        (e.format != PXL1_FMT_BGR888 && e.format != PXL1_FMT_IDX8)) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    if (e.offset + charge > taille_trame(e.format)) {
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    /* Âge de la tranche par rapport à la trame courante. La soustraction en
     * entier signé sur 16 bits gère correctement le bouclage du compteur. */
    const int16_t age = (int16_t)(e.frame_id - frame_courante);

    /* Au-delà de cette ancienneté, ce n'est plus du désordre : c'est un émetteur
     * qui a redémarré et remis son compteur à zéro. Sans cette porte de sortie,
     * la réception se bloquerait définitivement — constaté le 18/09/2026.
     * 8 trames valent 133 ms à 60 Hz, très au-delà de tout désordre plausible. */
    constexpr int16_t RESYNC = -8;

    if (age == 0 && frame_en_cours && e.format != format_courant) {
        /* Changement de format en cours de trame : incohérent, on écarte. */
        compteurs.rejets++;
        pbuf_free(p);
        return;
    }

    if (age < 0 && age > RESYNC) {
        /* Tranche d'une trame déjà soldée, arrivée dans le désordre. L'écarter
         * est essentiel : l'écrire polluerait la trame EN COURS d'assemblage
         * avec le contenu de la précédente, à un offset arbitraire. */
        compteurs.retardataires++;
        pbuf_free(p);
        return;
    }

    if (age == 0 && !frame_en_cours) {
        compteurs.retardataires++;
        pbuf_free(p);
        return;
    }

    if (age <= RESYNC) {
        /* Resynchronisation : on adopte le compteur de l'émetteur. */
        compteurs.resynchros++;
        frame_courante = e.frame_id;
        format_courant = e.format;
        frame_en_cours = true;
        octets_frame = 0;
        t_premier_courant = time_us_64();
    }

    else if (age > 0) {
        /* Nouvelle trame : on solde la précédente. */
        if (frame_en_cours && octets_frame < taille_trame(format_courant))
            compteurs.trames_incompletes++;
        frame_courante = e.frame_id;
        format_courant = e.format;
        frame_en_cours = true;
        octets_frame = 0;
        t_premier_courant = time_us_64();
    }

    /* Le format BGR888 du protocole est déjà celui de la dalle : simple recopie. */
    pbuf_copy_partial(p, tampons[idx_reception] + e.offset, charge, PXL1_ENTETE);
    octets_frame += charge;

    if (e.flags & PXL1_FLAG_DERNIERE) {
        if (octets_frame < taille_trame(format_courant))
            compteurs.trames_incompletes++;
        compteurs.trames++;
        frame_en_cours = false; /* frame_courante reste la référence d'âge */

        /* Une trame non encore publiée est abandonnée : on affiche toujours la
         * plus récente. C'est le bon comportement pour un afficheur, et c'est
         * aussi ce qui protège le pilote d'un rappel trop rapproché. */
        if (idx_pret >= 0)
            compteurs.ecartees++;

        id_pret = e.frame_id;
        format_pret = format_courant;
        t_premier_pret = t_premier_courant;
        t_dernier_pret = time_us_64();
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
    palette_par_defaut();

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

bool trame_a_afficher(Trame &out) {
    const int8_t idx = idx_pret;
    if (idx < 0)
        return false;
    out.pixels = tampons[idx];
    out.format = format_pret;
    out.id = id_pret;
    out.t_premier_us = t_premier_pret;
    out.t_dernier_us = t_dernier_pret;
    idx_pret = -1;
    return true;
}

void acquitter(uint16_t frame_id) {
    if (!emetteur_connu || pcb == nullptr)
        return;

    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_PING;
    e.node_id = mon_node_id;
    e.frame_id = frame_id;

    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, PXL1_ENTETE, PBUF_RAM);
    if (p == nullptr)
        return;
    std::memcpy(p->payload, &e, PXL1_ENTETE);
    udp_sendto(pcb, p, &emetteur_ip, emetteur_port);
    pbuf_free(p);
}

const Stats &stats() { return compteurs; }

void developper_idx8(const uint8_t *indices, uint8_t *sortie) {
    const uint32_t n = DISPLAY_W * DISPLAY_H;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *c = &palette[indices[i] * 3];
        sortie[i * 3 + 0] = c[0];
        sortie[i * 3 + 1] = c[1];
        sortie[i * 3 + 2] = c[2];
    }
}

uint8_t luminosite_demandee() {
    const uint8_t v = demande_luminosite;
    demande_luminosite = 0;
    return v;
}

const char *adresse_ip() { return ip_texte; }

} // namespace reseau
