/**
 * Module CAPTURE — émission `PXL1`
 *
 * ⚠️ Tout tourne sur le cœur 0, contrairement à ce qu'annonçait le plan §5.1.
 * La capture n'a pas besoin d'un cœur — PIO et DMA travaillent seuls, et les
 * deux interruptions s'exécutent sur le cœur qui les a armées. Le second cœur
 * n'apportait donc rien, et `cyw43_arch_init` appelé depuis le cœur 1 bloquait
 * le démarrage : `multicore_launch_core1` ne rend la main que si le cœur 1
 * signale son départ, et rien ne sortait plus, pas même la bannière.
 *
 * C'est aussi ce que fait le module écran, qui sert lwIP sur le cœur 0 et
 * réserve le cœur 1 à son pilote d'affichage.
 *
 * L'émetteur ne sait RIEN de l'afficheur. Il envoie la trame Game Boy native
 * 160×144 en `IDX2` vers une adresse, et c'est tout. Le récepteur de référence
 * est `tools/ecran_virtuel.py`.
 */
#include "net/reseau.hpp"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

#include "capture.hpp"
#include "config.h"
#include "pxl1.h"
#include "secrets.h"

namespace reseau {
namespace {

udp_pcb *pcb = nullptr;
ip_addr_t cible;
bool associe = false;
char ip_texte[16] = "0.0.0.0";

Stats compteurs{};

void noter(Mesure &m, uint32_t us) {
    if (!m.n || us < m.min_us)
        m.min_us = us;
    if (us > m.max_us)
        m.max_us = us;
    m.somme_us += us;
    m.n++;
}

/* Horodatage d'émission, indexé par les 8 bits de poids faible du numéro de
 * trame. Sert à mesurer l'aller-retour sur NOTRE horloge, sans supposer la
 * moindre synchronisation avec le récepteur. */
uint32_t t_emission_us[256];

/* ─────────────────────────────────────────────────────── accusés reçus */

void sur_accuse(void *, udp_pcb *, pbuf *p, const ip_addr_t *, u16_t) {
    if (p == nullptr)
        return;
    if (p->tot_len >= PXL1_ENTETE) {
        pxl1_entete e;
        pbuf_copy_partial(p, &e, PXL1_ENTETE, 0);
        if (e.magic == PXL1_MAGIC && e.type == PXL1_TYPE_PING) {
            const uint32_t t0 = t_emission_us[e.frame_id & 0xFF];
            if (t0 != 0) {
                const uint32_t dt = (uint32_t)time_us_32() - t0;
                /* L'index ne porte que 8 bits : un créneau se recycle toutes
                 * les 256 trames, soit 4,3 s. Au-delà de 500 ms, l'accusé ne
                 * correspond donc plus à l'horodatage qu'on lit. On l'écarte
                 * ET on le compte : un instrument qui jette des mesures en
                 * silence n'est pas un instrument. */
                if (dt < 500000u) {
                    compteurs.aller_retour_us = dt;
                    noter(compteurs.aller_retour, dt);
                    const uint32_t seau = dt / 1000u;
                    compteurs.histo[seau < RESEAU_HISTO_SEAUX - 1
                                        ? seau
                                        : RESEAU_HISTO_SEAUX - 1]++;
                } else {
                    compteurs.aller_retour_ecartes++;
                }
                t_emission_us[e.frame_id & 0xFF] = 0;
            }
            compteurs.accuses++;
        }
    }
    pbuf_free(p);
}

/* ───────────────────────────────────────────────────────────── émission */

bool envoyer(const void *entete, uint16_t n_entete,
             const void *charge, uint16_t n_charge) {
    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(n_entete + n_charge), PBUF_RAM);
    if (p == nullptr) {
        compteurs.echecs++;
        return false;
    }
    std::memcpy(p->payload, entete, n_entete);
    if (n_charge)
        std::memcpy((uint8_t *)p->payload + n_entete, charge, n_charge);

    cyw43_arch_lwip_begin();
    const err_t r = udp_sendto(pcb, p, &cible, PXL1_PORT);
    cyw43_arch_lwip_end();
    pbuf_free(p);

    if (r != ERR_OK) {
        compteurs.echecs++;
        return false;
    }
    compteurs.paquets++;
    compteurs.octets += n_charge;
    return true;
}

void envoyer_trame(const uint8_t *trame, uint16_t frame_id) {
    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_FRAME;
    e.node_id = PXL1_NODE_ID;
    e.frame_id = frame_id;
    e.format = PXL1_FMT_IDX2;

    /* Horodater AVANT le premier paquet, et pas après le dernier : l'accusé
     * peut revenir en 3 ms, donc avant qu'on ait fini de pousser les cinq.
     * La mesure inclut alors le temps d'émission, ce qui est plus honnête —
     * c'est bien le délai entre « on commence à envoyer cette trame » et
     * « le récepteur l'a affichée ». */
    t_emission_us[frame_id & 0xFF] = time_us_32();

    uint16_t offset = 0;
    while (offset < OCTETS_TRAME) {
        const uint16_t n = (OCTETS_TRAME - offset > PXL1_CHARGE_MAX)
                               ? PXL1_CHARGE_MAX
                               : (uint16_t)(OCTETS_TRAME - offset);
        e.offset = offset;
        e.flags = (offset + n == OCTETS_TRAME) ? PXL1_FLAG_DERNIERE : 0;
        envoyer(&e, PXL1_ENTETE, trame + offset, n);
        offset = (uint16_t)(offset + n);
    }
    compteurs.trames++;
    gpio_xor_mask(1u << PIN_MESURE_EMIS);
}

/* Palette et géométrie : hors du chemin critique des pixels, renvoyées
 * périodiquement pour qu'un récepteur redémarré retrouve seul de quoi
 * interpréter le flux. */
void envoyer_commandes() {
    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_CTRL;
    e.node_id = PXL1_NODE_ID;
    e.format = PXL1_FMT_IDX2;

    /* Géométrie de la SOURCE — extension du 26/09/2026, voir pxl1.h. */
    uint8_t geo[1 + PXL1_GEOMETRIE_OCTETS];
    geo[0] = PXL1_CTRL_GEOMETRIE;
    const uint16_t l = GB_L, h = GB_H;
    std::memcpy(geo + 1, &l, 2);
    std::memcpy(geo + 3, &h, 2);
    geo[5] = PXL1_FMT_IDX2;
    if (envoyer(&e, PXL1_ENTETE, geo, sizeof(geo)))
        compteurs.ctrl++;

    /* Palette : 256 entrées B,G,R, dont seules les 4 premières servent en
     * IDX2. C'est le format existant, on ne le change pas pour si peu. */
    static uint8_t pal[1 + PXL1_PALETTE_OCTETS];
    static bool prete = false;
    if (!prete) {
        const uint8_t teintes[4][3] = PALETTE_BGR;
        std::memset(pal, 0, sizeof(pal));
        pal[0] = PXL1_CTRL_PALETTE;
        for (int i = 0; i < 4; ++i)
            std::memcpy(pal + 1 + i * 3, teintes[i], 3);
        prete = true;
    }
    if (envoyer(&e, PXL1_ENTETE, pal, sizeof(pal)))
        compteurs.ctrl++;
}

bool associer() {
    printf("  cyw43 : initialisation...\n");
    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();

    /* ⚠️ LE levier de latence. Le module écran l'a mesuré : sans cet appel,
     * 10 à 100 ms s'ajoutent à chaque trame. */
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

    printf("  association a « %s »...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("  association : ECHEC (SSID, mot de passe, ou hors de portee ?)\n");
        return false;
    }
    std::snprintf(ip_texte, sizeof(ip_texte), "%s",
                  ip4addr_ntoa(netif_ip4_addr(netif_default)));

    if (!ip4addr_aton(PXL1_CIBLE_IP, &cible)) {
        printf("  PXL1_CIBLE_IP illisible : « %s »\n", PXL1_CIBLE_IP);
        return false;
    }

    pcb = udp_new();
    if (pcb == nullptr) {
        printf("  udp_new : ECHEC\n");
        return false;
    }
    /* On se lie au port PXL1 pour que les accusés reviennent à une adresse
     * connue plutôt qu'à un port éphémère. */
    if (udp_bind(pcb, IP_ANY_TYPE, PXL1_PORT) != ERR_OK) {
        printf("  udp_bind sur %d : ECHEC\n", PXL1_PORT);
        return false;
    }
    udp_recv(pcb, sur_accuse, nullptr);

    printf("  associe : %s  ->  %s:%d\n", ip_texte, PXL1_CIBLE_IP, PXL1_PORT);
    return true;
}

} // namespace

/* ───────────────────────────────────────────────────────────────── façade */

bool init() {
    std::memset(t_emission_us, 0, sizeof(t_emission_us));
    associe = associer();
    return associe;
}

void emettre(const uint8_t *trame, uint16_t frame_id, uint32_t t_vsync_us) {
    if (!associe)
        return;
    const uint32_t t_debut = time_us_32();
    /* Ce qui s'est écoulé depuis la fin de capture appartient au Pico, pas au
     * réseau. Le distinguer est tout l'intérêt de la décomposition. */
    noter(compteurs.attente, t_debut - t_vsync_us);
    envoyer_trame(trame, frame_id);
    noter(compteurs.emission, (uint32_t)time_us_32() - t_debut);
}

void servir() {
    if (!associe)
        return;

    static uint32_t t_ctrl = 0;
    const uint32_t maintenant = to_ms_since_boot(get_absolute_time());
    if (maintenant - t_ctrl >= PERIODE_CTRL_MS) {
        t_ctrl = maintenant;
        envoyer_commandes();
    }
}

bool pret() { return associe; }
const char *adresse_ip() { return ip_texte; }
const Stats &stats() { return compteurs; }

void reinitialiser() {
    /* Tout, y compris la latence : conserver une moyenne à travers une remise
     * à zéro mélange deux régimes et produit un chiffre que rien ne recoupe.
     * C'est ce qui a donné « instantané 3,11 ms, moyenne 29,49 ms » le
     * 26/09/2026. */
    compteurs = Stats{};
    std::memset(t_emission_us, 0, sizeof(t_emission_us));
}

} // namespace reseau
