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
#include "cyw43.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

#include "capture.hpp"
#include "config.h"
#include "pxl1.h"
#include "secrets.h"

#if POINT_ACCES
#include "lwip/netif.h"
/* Serveur DHCP vendorisé par le module écran, écrit en C sans garde extern "C". */
extern "C" {
#include "dhcpserver.h"
}
#ifndef IMAGE_SSID
#define IMAGE_SSID "gb-image"
#endif
#ifndef IMAGE_MOT_DE_PASSE
#error "POINT_ACCES : definir IMAGE_MOT_DE_PASSE dans include/secrets.h (voir secrets.h.example)"
#endif
#endif

namespace reseau {
namespace {

udp_pcb *pcb = nullptr;
ip_addr_t cible;
bool associe = false;
bool association_en_cours = false;
char ip_texte[16] = "0.0.0.0";
uint32_t t_surveillance_ms = 0;

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

#if POINT_ACCES
/* ─────────────────────── point d'accès du réseau image (banc R2, reseau.md)
 *
 * Le sniffer émet le réseau, et chaque tranche part en une copie par écran.
 * Un écran est une destination tant que :
 *   - il est associé au réseau et a reçu un bail DHCP : c'est ainsi qu'on
 *     trouve la vraie tête, qui n'accuse une image qu'après l'avoir reçue ;
 *   - ou il s'est fait entendre depuis moins de 2 s : un accusé, ou un PING
 *     marqué PXL1_FLAG_SIGNAL, que les écrans à adresse fixe envoient chaque
 *     seconde (reseau.md §2.3).
 * Quatre au plus, la limite d'un Pico 2 W en point d'accès. */

/* Extension de banc du 11/10/2026 : un PING ainsi marqué dit « je suis là »,
 * ce n'est pas un accusé. À reporter dans ecran/firmware/commun/pxl1.h si la
 * règle est gardée pour l'image. */
#define PXL1_FLAG_SIGNAL 0x80

struct Ecran {
    bool actif;
    bool par_bail;   /* associé, avec un bail : gardé même muet */
    ip_addr_t ip;
    uint32_t vu_ms;  /* dernier paquet reçu de lui */
    uint32_t accuses;
    Mesure aller_retour;
};
Ecran ecrans[IMAGE_ECRANS_MAX];
uint32_t ecrans_refuses = 0;   /* un cinquième écran s'est signalé */
uint32_t t_ecrans_ms = 0;
dhcp_server_t serveur_dhcp;

uint32_t ms_maintenant() { return to_ms_since_boot(get_absolute_time()); }

/* L'écran d'adresse `ip`, ajouté s'il est nouveau et qu'il reste une place. */
Ecran *ecran(const ip_addr_t &ip) {
    Ecran *libre = nullptr;
    for (auto &e : ecrans) {
        if (e.actif && ip_addr_cmp(&e.ip, &ip))
            return &e;
        if (!e.actif && libre == nullptr)
            libre = &e;
    }
    if (libre == nullptr) {
        ecrans_refuses++;
        return nullptr;
    }
    *libre = Ecran{};
    libre->actif = true;
    ip_addr_copy(libre->ip, ip);
    libre->vu_ms = ms_maintenant();
    return libre;
}

/* Rappel lwIP en point d'accès : accusés et signaux des écrans. L'horodatage
 * d'émission n'est PAS remis à zéro après un accusé, contrairement au mode
 * station : les autres copies de la même image en ont encore besoin. */
void sur_paquet_ecran(void *, udp_pcb *, pbuf *p, const ip_addr_t *source, u16_t) {
    if (p == nullptr)
        return;
    if (p->tot_len >= PXL1_ENTETE) {
        pxl1_entete e;
        pbuf_copy_partial(p, &e, PXL1_ENTETE, 0);
        if (e.magic == PXL1_MAGIC && e.type == PXL1_TYPE_PING) {
            Ecran *ec = ecran(*source);
            if (ec != nullptr) {
                ec->vu_ms = ms_maintenant();
                if (!(e.flags & PXL1_FLAG_SIGNAL)) {
                    ec->accuses++;
                    compteurs.accuses++;
                    const uint32_t t0 = t_emission_us[e.frame_id & 0xFF];
                    const uint32_t dt = (uint32_t)time_us_32() - t0;
                    if (t0 != 0 && dt < 500000u) {
                        noter(ec->aller_retour, dt);
                        noter(compteurs.aller_retour, dt);
                        compteurs.aller_retour_us = dt;
                        const uint32_t seau = dt / 1000u;
                        compteurs.histo[seau < RESEAU_HISTO_SEAUX - 1
                                            ? seau
                                            : RESEAU_HISTO_SEAUX - 1]++;
                    } else {
                        compteurs.aller_retour_ecartes++;
                    }
                }
            }
        }
    }
    pbuf_free(p);
}

/* Toutes les 500 ms : les stations associées qui ont un bail deviennent des
 * écrans ; les écrans muets depuis 2 s sans bail sont oubliés. */
void tenir_ecrans() {
    const uint32_t maintenant = ms_maintenant();
    if (maintenant - t_ecrans_ms < 500u)
        return;
    t_ecrans_ms = maintenant;

    /* ⚠️ Avec un tableau d'adresses nul, le pilote rend le nombre MAXIMAL de
     * stations, pas celui des connectées (module écran, 29/09/2026). */
    uint8_t macs[6 * 8];
    int n = 8;
    cyw43_arch_lwip_begin();
    cyw43_wifi_ap_get_stas(&cyw43_state, &n, macs);
    for (auto &e : ecrans)
        e.par_bail = false;
    for (int s = 0; s < n; ++s)
        for (int i = 0; i < DHCPS_MAX_IP; ++i) {
            if (serveur_dhcp.lease[i].expiry == 0 ||
                std::memcmp(serveur_dhcp.lease[i].mac, macs + 6 * s, 6) != 0)
                continue;
            ip_addr_t ip;
            IP4_ADDR(ip_2_ip4(&ip), 192, 168, 4, DHCPS_BASE_IP + i);
            Ecran *ec = ecran(ip);
            if (ec != nullptr)
                ec->par_bail = true;
        }
    for (auto &e : ecrans)
        if (e.actif && !e.par_bail && maintenant - e.vu_ms > IMAGE_OUBLI_MS)
            e.actif = false;
    cyw43_arch_lwip_end();
}

/* Émet le réseau image : le point d'accès est en 192.168.4.1, l'adresse par
 * défaut du pilote, celle que visait jusqu'ici le sniffer. */
bool emettre_reseau() {
    printf("  cyw43 : initialisation...\n");
    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }
    cyw43_wifi_ap_set_channel(&cyw43_state, IMAGE_CANAL_WIFI);
    cyw43_arch_enable_ap_mode(IMAGE_SSID, IMAGE_MOT_DE_PASSE, CYW43_AUTH_WPA2_AES_PSK);

    ip_addr_t passerelle, masque;
    IP4_ADDR(ip_2_ip4(&passerelle), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&masque), 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    dhcp_server_init(&serveur_dhcp, &cyw43_state.netif[CYW43_ITF_AP], &passerelle, &masque);
    pcb = udp_new();
    const bool lie = pcb != nullptr && udp_bind(pcb, IP_ANY_TYPE, PXL1_PORT) == ERR_OK;
    if (lie)
        udp_recv(pcb, sur_paquet_ecran, nullptr);
    cyw43_arch_lwip_end();
    if (!lie) {
        printf("  udp_new / udp_bind sur %d : ECHEC\n", PXL1_PORT);
        return false;
    }
    std::snprintf(ip_texte, sizeof(ip_texte), "192.168.4.1");
    printf("  reseau « %s » emis sur le canal %d, une copie par ecran (%d au plus)\n",
           IMAGE_SSID, IMAGE_CANAL_WIFI, IMAGE_ECRANS_MAX);
    return true;
}
#endif

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
#if POINT_ACCES
    /* Une copie par écran, chacune dans son pbuf. */
    bool tout = true;
    cyw43_arch_lwip_begin();
    for (const auto &ec : ecrans) {
        if (!ec.actif)
            continue;
        pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(n_entete + n_charge), PBUF_RAM);
        if (q == nullptr) {
            compteurs.echecs++;
            tout = false;
            continue;
        }
        std::memcpy(q->payload, entete, n_entete);
        if (n_charge)
            std::memcpy((uint8_t *)q->payload + n_entete, charge, n_charge);
        const err_t r = udp_sendto(pcb, q, &ec.ip, PXL1_PORT);
        pbuf_free(q);
        if (r != ERR_OK) {
            compteurs.echecs++;
            tout = false;
            continue;
        }
        compteurs.paquets++;
        compteurs.octets += n_charge;
    }
    cyw43_arch_lwip_end();
    return tout;
#else
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
#endif
}

void envoyer_trame(const uint8_t *trame, uint16_t frame_id) {
    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_FRAME;
    e.node_id = PXL1_NODE_ID;
    e.frame_id = frame_id;
    e.format = PXL1_FMT_IDX2;

    uint16_t offset = 0;
    while (offset < OCTETS_TRAME) {
        const uint16_t n = (OCTETS_TRAME - offset > PXL1_CHARGE_MAX)
                               ? PXL1_CHARGE_MAX
                               : (uint16_t)(OCTETS_TRAME - offset);
        e.offset = offset;
        const bool derniere = (offset + n == OCTETS_TRAME);
        e.flags = derniere ? PXL1_FLAG_DERNIERE : 0;
        /* Horodater juste avant le DERNIER paquet : c'est lui qui déclenche
         * l'affichage et donc l'accusé. C'est aussi la seule référence
         * comparable entre mode simple et mode pipeliné, où les quatre
         * premières tranches partent étalées sur la trame. */
        if (derniere)
            t_emission_us[frame_id & 0xFF] = time_us_32();
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

void noter_ip() {
    std::snprintf(ip_texte, sizeof(ip_texte), "%s",
                  ip4addr_ntoa(netif_ip4_addr(netif_default)));
}

/* Surveille le lien et relance une association quand il tombe.
 *
 * ⚠️ Cette logique n'existait pas jusqu'au 26/09/2026 : si l'AP disparaissait,
 * `udp_sendto` échouait indéfiniment et rien ne reprenait jamais. Dans une
 * installation qui tourne des heures, ça arrive.
 *
 * L'association se fait en ASYNCHRONE : `cyw43_arch_wifi_connect_timeout_ms`
 * bloque jusqu'à 30 s, et pendant ce temps la boucle principale ne consomme
 * plus les trames — c'est exactement ce qui a fait perdre 641 trames au
 * démarrage. La capture, elle, continue quoi qu'il arrive : PIO, DMA et
 * interruptions ne dépendent pas de cette boucle. */
void surveiller_lien() {
#if POINT_ACCES
    tenir_ecrans();
#else
    const uint32_t maintenant = to_ms_since_boot(get_absolute_time());
    if (maintenant - t_surveillance_ms < 1000u)
        return;
    t_surveillance_ms = maintenant;

    cyw43_arch_lwip_begin();
    const int etat = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();

    if (etat == CYW43_LINK_UP) {
        if (!associe) {
            noter_ip();
            associe = true;
            association_en_cours = false;
            compteurs.reconnexions++;
            printf("\n  reseau RETABLI : %s\n", ip_texte);
        }
        return;
    }

    if (associe) {
        associe = false;
        compteurs.deconnexions++;
        printf("\n  reseau PERDU (etat %d) — la capture continue,\n"
               "  tentative de reassociation toutes les secondes\n", etat);
    }

    /* CYW43_LINK_JOIN / NOIP : une association est déjà en cours, on attend.
     * Les états négatifs sont des échecs : on relance. */
    if (etat == CYW43_LINK_JOIN || etat == CYW43_LINK_NOIP) {
        association_en_cours = true;
        return;
    }
    association_en_cours = false;
    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD,
                                  CYW43_AUTH_WPA2_AES_PSK);
#endif
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

    if (!ip4addr_aton(PXL1_CIBLE_IP, &cible)) {
        printf("  PXL1_CIBLE_IP illisible : « %s »\n", PXL1_CIBLE_IP);
        return false;
    }

    /* Le pcb UDP AVANT l'association, et quel qu'en soit le résultat.
     *
     * ⚠️ Jusqu'au 08/10/2026 il n'était créé qu'après une première association
     * réussie. Si le réseau manquait au démarrage — le module écran l'émet
     * lui-même, et la Game Boy peut s'allumer avant lui —, surveiller_lien()
     * finissait par associer, mais l'émission partait sur un pcb nul :
     * « udp_sendto: invalid pcb » à chaque tranche, 20 190 échecs en 109 s,
     * aucune image reçue. */
    cyw43_arch_lwip_begin();
    pcb = udp_new();
    /* On se lie au port PXL1 pour que les accusés reviennent à une adresse
     * connue plutôt qu'à un port éphémère. */
    const bool lie = pcb != nullptr && udp_bind(pcb, IP_ANY_TYPE, PXL1_PORT) == ERR_OK;
    if (lie)
        udp_recv(pcb, sur_accuse, nullptr);
    cyw43_arch_lwip_end();
    if (!lie) {
        printf("  udp_new / udp_bind sur %d : ECHEC\n", PXL1_PORT);
        return false;
    }

    printf("  association a « %s »...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        /* Pas fatal : surveiller_lien() relance l'association chaque seconde,
         * et le pcb est prêt pour le jour où elle aboutit. */
        printf("  association : ECHEC (SSID, mot de passe, ou hors de portee ?)\n"
               "  nouvelle tentative chaque seconde\n");
        return false;
    }
    noter_ip();

    printf("  associe : %s  ->  %s:%d\n", ip_texte, PXL1_CIBLE_IP, PXL1_PORT);
    return true;
}

} // namespace

/* ───────────────────────────────────────────────────────────────── façade */

bool init() {
    std::memset(t_emission_us, 0, sizeof(t_emission_us));
#if POINT_ACCES
    associe = emettre_reseau();
#else
    associe = associer();
#endif
    return associe;
}

void emettre_tranche(const capture::Tranche &tr) {
    if (!associe)
        return;
    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_FRAME;
    e.node_id = PXL1_NODE_ID;
    e.frame_id = tr.frame_id;
    e.format = PXL1_FMT_IDX2;
    e.offset = tr.offset;
    e.flags = tr.derniere ? PXL1_FLAG_DERNIERE : 0;

    const uint32_t t_debut = time_us_32();
    if (tr.derniere)
        t_emission_us[tr.frame_id & 0xFF] = t_debut;
    envoyer(&e, PXL1_ENTETE, tr.donnees, tr.taille);
    noter(compteurs.emission, (uint32_t)time_us_32() - t_debut);

    if (tr.derniere) {
        compteurs.trames++;
        gpio_xor_mask(1u << PIN_MESURE_EMIS);
    }
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
    surveiller_lien();
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

void rompre_pour_essai() {
#if POINT_ACCES
    printf("\n  point d'acces : pas d'association a rompre\n");
    return;
#endif
    printf("\n  rupture VOLONTAIRE de l'association (essai de reconnexion)\n");
    cyw43_arch_lwip_begin();
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();
}

const char *etat_lien() {
#if POINT_ACCES
    static char texte[48];
    int n = 0;
    for (const auto &e : ecrans)
        n += e.actif;
    std::snprintf(texte, sizeof(texte), "POINT D'ACCES (%d ecran(s))", n);
    return texte;
#endif
    cyw43_arch_lwip_begin();
    const int e = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();
    switch (e) {
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
const char *adresse_ip() { return ip_texte; }
const Stats &stats() { return compteurs; }

#if POINT_ACCES
/* Les lignes que lit banc-reseau/tools/banc.py pour le banc R2. */
void bilan_banc() {
    const capture::Stats &c = capture::stats();
    int n = 0;
    for (const auto &e : ecrans)
        n += e.actif;
    printf("BNC role=sniffer res=image t=%lu ecrans=%d refuses=%lu trames=%lu paquets=%lu "
           "echecs=%lu acc=%lu emission_max_us=%lu attente_max_us=%lu capture_trames=%lu "
           "douteuses=%lu incompletes=%lu rejetees=%lu debordements=%lu perdues=%lu "
           "tranches_perdues=%lu\n",
           (unsigned long)ms_maintenant(), n, (unsigned long)ecrans_refuses,
           (unsigned long)compteurs.trames, (unsigned long)compteurs.paquets,
           (unsigned long)compteurs.echecs, (unsigned long)compteurs.accuses,
           (unsigned long)compteurs.emission.max_us, (unsigned long)compteurs.attente.max_us,
           (unsigned long)c.trames, (unsigned long)c.trames_douteuses,
           (unsigned long)c.trames_incompletes, (unsigned long)c.rejetees,
           (unsigned long)c.debordements, (unsigned long)c.perdues,
           (unsigned long)c.tranches_perdues);
    for (const auto &e : ecrans) {
        if (!e.actif)
            continue;
        const Mesure &m = e.aller_retour;
        printf("BNC role=sniffer_ecran res=image t=%lu ip=%s bail=%d acc=%lu rtt_n=%lu "
               "rtt_moy_us=%lu rtt_max_us=%lu\n",
               (unsigned long)ms_maintenant(), ipaddr_ntoa(&e.ip), e.par_bail ? 1 : 0,
               (unsigned long)e.accuses, (unsigned long)m.n,
               (unsigned long)(m.n ? m.somme_us / m.n : 0), (unsigned long)m.max_us);
    }
}
#endif

void reinitialiser() {
    /* Tout, y compris la latence : conserver une moyenne à travers une remise
     * à zéro mélange deux régimes et produit un chiffre que rien ne recoupe.
     * C'est ce qui a donné « instantané 3,11 ms, moyenne 29,49 ms » le
     * 26/09/2026. */
    compteurs = Stats{};
    std::memset(t_emission_us, 0, sizeof(t_emission_us));
#if POINT_ACCES
    for (auto &e : ecrans) {
        e.accuses = 0;
        e.aller_retour = Mesure{};
    }
    ecrans_refuses = 0;
#endif
}

} // namespace reseau
