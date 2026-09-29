/**
 * Tête — WiFi, réception PXL2 / PXL1, placement et découpe
 *
 * Reprise de la réception des phases 2–4 (firmware/ecran/src/net/reseau.cpp),
 * dont elle garde les trois choix de latence : économie d'énergie WiFi COUPÉE,
 * API raw de lwIP, chaque tranche traitée dès son arrivée. Ce qui change :
 *
 *   - Deux protocoles sur le même port, distingués par le magic. PXL2 porte
 *     la géométrie de la source dans chaque en-tête ; PXL1 l'apprend par
 *     CTRL_GEOMETRIE, comme le sniffer l'émet déjà (plan §4.3).
 *   - Plus de tampon d'image. La tête ne garde pas les pixels : elle place
 *     l'image dans le canevas et découpe chaque tranche en segments, un par
 *     rangée touchée. En phase 5a les segments sont comptés ; en phase 5b ils
 *     partiront sur les liaisons, au fil de l'eau.
 *   - Une image est complète quand TOUTES ses tranches sont arrivées, dans
 *     n'importe quel ordre. La v1 publiait à la tranche marquée « dernière »,
 *     si bien qu'une tranche retardée derrière elle faisait perdre l'image ;
 *     ici elle la complète.
 *   - Les doublons sont reconnus à leur offset et écartés. Le WiFi en livre :
 *     2 à 3 par tranche de 10 s en IDX8 192×192, mesuré le 29/09/2026. Compter
 *     les octets seulement, comme la v1, aurait déclaré complète une image à
 *     laquelle il manquait une tranche.
 */

#include "reseau.hpp"
#include "config.h"
#include "decoupe.hpp"
#include "pxl1.h"
#include "pxl2.h"
#include "secrets.h"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

namespace {

constexpr decoupe::Canevas CANEVAS{CANEVAS_W, CANEVAS_H, RANGEE_H};

udp_pcb *pcb = nullptr;
reseau::Stats compteurs{};
char ip_texte[16] = "0.0.0.0";

bool associe = false;
uint32_t t_surveillance_ms = 0;

/* ------------------------------------------------ état hors pixels */

/* Palette B,G,R. Par défaut une rampe de gris : une image indexée s'affiche de
 * façon sensée même avant la première palette. Relayée aux nœuds en 5b. */
uint8_t palette[PXL2_PALETTE_OCTETS];
volatile uint8_t demande_luminosite = 0;

/* Géométrie annoncée par PXL1_CTRL_GEOMETRIE. Tant qu'elle manque, une tranche
 * PXL1 est ininterprétable : sa taille ne dit rien de la largeur de l'image. */
bool geo_pxl1_connue = false;
uint16_t geo_pxl1_w = 0, geo_pxl1_h = 0;

/* ------------------------------------------------------ réassemblage */

/* Faux jusqu'à la première tranche. Sans lui, une image numéro 0 reçue en
 * premier — ce qu'envoie tout émetteur qui démarre — passait pour une
 * retardataire de l'image 0 « déjà soldée » : défaut de la v1. */
bool amorce = false;
uint16_t image_courante = 0;
bool image_en_cours = false;
decoupe::Geometrie geo_courante;
uint8_t proto_courant = 2;
uint32_t octets_image = 0;
uint64_t t_premier_courant = 0;

/* Offsets des tranches déjà reçues pour l'image courante. 128 couvrent
 * largement le pire cas, 192×192 en BGR888 : 80 tranches. Au-delà, les
 * doublons ne sont plus reconnus — compté dans `non_suivies`. */
constexpr uint32_t TRANCHES_SUIVIES = 128;
uint32_t offsets_recus[TRANCHES_SUIVIES];
uint32_t nb_offsets = 0;

/* Comptes de l'image courante, versés aux compteurs globaux seulement si elle
 * se complète : une image abandonnée ne fausse pas les moyennes par image. */
uint32_t pixels_image[NB_RANGEES];
uint32_t segments_image = 0;

/* Dernière géométrie vue, pour signaler un changement. */
decoupe::Geometrie geo_connue;
bool geo_a_signaler = false;

/* Dernière image complète, en attente de la boucle principale. */
bool image_prete = false;
reseau::Image pret{};

/* Adresse de l'émetteur de la dernière image complète : l'accusé lui revient
 * sans qu'aucune adresse soit à configurer. */
ip_addr_t ack_ip;
u16_t ack_port = 0;
bool ack_possible = false;

/* Une tranche d'image, quel que soit son protocole. */
struct Tranche {
    uint8_t proto;
    uint8_t format;
    uint16_t frame_id;
    uint16_t largeur, hauteur;
    uint32_t offset;
    uint8_t flags;
    uint16_t entete; /* début de la charge utile dans le pbuf */
    uint16_t charge;
};

void palette_par_defaut() {
    for (int i = 0; i < PXL2_PALETTE_ENTREES; ++i) {
        palette[i * 3 + 0] = (uint8_t)i;
        palette[i * 3 + 1] = (uint8_t)i;
        palette[i * 3 + 2] = (uint8_t)i;
    }
}

void envoyer(const void *donnees, uint16_t n, const ip_addr_t *ip, u16_t port) {
    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
    if (p == nullptr)
        return;
    std::memcpy(p->payload, donnees, n);
    udp_sendto(pcb, p, ip, port);
    pbuf_free(p);
}

/* « Qui es-tu ? » — la source découvre le canevas au lieu de le connaître. */
void repondre_pong(const ip_addr_t *source, u16_t port) {
    struct __attribute__((packed)) {
        pxl2_entete e;
        pxl2_pong d;
    } m{};
    m.e.magic = PXL2_MAGIC;
    m.e.type = PXL2_TYPE_PONG;
    m.e.largeur = CANEVAS_W;
    m.e.hauteur = CANEVAS_H;
    m.d.largeur = CANEVAS_W;
    m.d.hauteur = CANEVAS_H;
    m.d.formats = FORMATS_ACCEPTES;
    m.d.charge_max = PXL2_CHARGE_MAX;
    envoyer(&m, sizeof(m), source, port);
}

/* Commandes : palette, luminosité, et géométrie pour PXL1. Hors du chemin
 * critique des pixels. Les numéros de sous-commande sont communs. */
void traiter_ctrl(pbuf *p, uint16_t entete, uint16_t charge, uint8_t proto) {
    if (charge < 1) {
        compteurs.rejets++;
        return;
    }
    uint8_t cmd;
    pbuf_copy_partial(p, &cmd, 1, entete);
    const uint16_t reste = (uint16_t)(charge - 1);

    if (cmd == PXL2_CTRL_PALETTE && reste >= 3) {
        /* Palette partielle acceptée : IDX2 n'en lit que 4 entrées. */
        const uint16_t n = reste < PXL2_PALETTE_OCTETS ? (uint16_t)(reste - reste % 3)
                                                       : (uint16_t)PXL2_PALETTE_OCTETS;
        pbuf_copy_partial(p, palette, n, entete + 1);
        compteurs.ctrl++;
    } else if (cmd == PXL2_CTRL_LUMINOSITE && reste >= 1) {
        uint8_t basis;
        pbuf_copy_partial(p, &basis, 1, entete + 1);
        demande_luminosite = basis ? basis : 1;
        compteurs.ctrl++;
    } else if (proto == 1 && cmd == PXL1_CTRL_GEOMETRIE && reste >= PXL1_GEOMETRIE_OCTETS) {
        uint8_t geo[PXL1_GEOMETRIE_OCTETS];
        pbuf_copy_partial(p, geo, sizeof(geo), entete + 1);
        std::memcpy(&geo_pxl1_w, geo + 0, 2);
        std::memcpy(&geo_pxl1_h, geo + 2, 2);
        /* geo[4], le format, n'est pas retenu : chaque tranche porte le sien. */
        geo_pxl1_connue = true;
        compteurs.ctrl++;
    } else {
        compteurs.rejets++;
    }
}

bool lire_pxl2(pbuf *p, const ip_addr_t *source, u16_t port, Tranche &t) {
    if (p->tot_len < PXL2_ENTETE || p->tot_len > PXL2_ENTETE + PXL2_CHARGE_MAX) {
        compteurs.rejets++;
        return false;
    }
    pxl2_entete e;
    pbuf_copy_partial(p, &e, PXL2_ENTETE, 0);
    const uint16_t charge = (uint16_t)(p->tot_len - PXL2_ENTETE);

    switch (e.type) {
    case PXL2_TYPE_FRAME:
        break;
    case PXL2_TYPE_PING:
        compteurs.pings++;
        if (source != nullptr)
            repondre_pong(source, port);
        return false;
    case PXL2_TYPE_CTRL:
        traiter_ctrl(p, PXL2_ENTETE, charge, 2);
        return false;
    default:
        compteurs.rejets++;
        return false;
    }

    t = Tranche{2, e.format, e.frame_id, e.largeur, e.hauteur, e.offset, e.flags,
                PXL2_ENTETE, charge};
    return true;
}

bool lire_pxl1(pbuf *p, Tranche &t) {
    compteurs.pxl1++;
    if (p->tot_len < PXL1_ENTETE || p->tot_len > PXL1_ENTETE + PXL1_CHARGE_MAX) {
        compteurs.rejets++;
        return false;
    }
    pxl1_entete e;
    pbuf_copy_partial(p, &e, PXL1_ENTETE, 0);
    const uint16_t charge = (uint16_t)(p->tot_len - PXL1_ENTETE);

    /* node_id est ignoré : il n'y a plus qu'un récepteur (plan §4.3). */
    if (e.type == PXL1_TYPE_CTRL) {
        traiter_ctrl(p, PXL1_ENTETE, charge, 1);
        return false;
    }
    if (e.type != PXL1_TYPE_FRAME) {
        compteurs.rejets++;
        return false;
    }
    if (!geo_pxl1_connue) {
        compteurs.sans_geometrie++;
        return false;
    }

    t = Tranche{1, e.format, e.frame_id, geo_pxl1_w, geo_pxl1_h, e.offset, e.flags,
                PXL1_ENTETE, charge};
    return true;
}

void commencer_image(const Tranche &t, const decoupe::Geometrie &g) {
    image_courante = t.frame_id;
    image_en_cours = true;
    geo_courante = g;
    proto_courant = t.proto;
    octets_image = 0;
    nb_offsets = 0;
    segments_image = 0;
    for (uint32_t &n : pixels_image)
        n = 0;
    t_premier_courant = time_us_64();
    gpio_xor_mask(1u << PIN_MESURE_RX);

    if (!(g == geo_connue)) {
        /* Phase 5b : c'est ici que partira EFFACER, avant le premier segment
         * de la nouvelle géométrie — ses marges ne seront écrites par rien. */
        geo_connue = g;
        geo_a_signaler = true;
        compteurs.geometries++;
    }
}

/* `p` ne sert qu'à partir de la phase 5b, pour lire les octets à relayer. */
void recevoir_tranche([[maybe_unused]] pbuf *p, const Tranche &t, const ip_addr_t *source,
                      u16_t port) {
    if (t.format >= 16 || !((FORMATS_ACCEPTES >> t.format) & 1u)) {
        compteurs.format_refuse++;
        return;
    }
    decoupe::Geometrie g;
    if (!decoupe::placer(CANEVAS, t.largeur, t.hauteur, t.format, g)) {
        compteurs.hors_canevas++;
        return;
    }
    if (!decoupe::tranche_valide(g, t.offset, t.charge)) {
        compteurs.rejets++;
        return;
    }

    /* Âge de la tranche par rapport à l'image courante. La soustraction en
     * entier signé sur 16 bits gère le bouclage du compteur. */
    const int16_t age = (int16_t)(t.frame_id - image_courante);

    /* Au-delà de cette ancienneté, ce n'est plus du désordre : c'est un
     * émetteur qui a redémarré. Sans cette porte de sortie, la réception se
     * bloquait définitivement — constaté en v1 le 18/09/2026. */
    constexpr int16_t RESYNC = -8;

    if (!amorce) {
        amorce = true;
        commencer_image(t, g);
    } else if (age == 0 && image_en_cours && !(g == geo_courante)) {
        compteurs.rejets++; /* géométrie ou format changé en cours d'image */
        return;
    } else if (age < 0 && age > RESYNC) {
        compteurs.retardataires++;
        return;
    } else if (age == 0 && !image_en_cours) {
        /* Image déjà complète : toutes ses tranches sont là, celle-ci en est
         * forcément une seconde copie. */
        compteurs.doublons++;
        return;
    } else if (age <= RESYNC) {
        compteurs.resynchros++;
        commencer_image(t, g);
    } else if (age > 0) {
        if (image_en_cours)
            compteurs.incompletes++; /* abandonnée pour une plus récente */
        commencer_image(t, g);
    }

    for (uint32_t i = 0; i < nb_offsets; ++i) {
        if (offsets_recus[i] == t.offset) {
            compteurs.doublons++;
            return;
        }
    }
    if (nb_offsets < TRANCHES_SUIVIES)
        offsets_recus[nb_offsets++] = t.offset;
    else
        compteurs.non_suivies++;

    segments_image += decoupe::decouper(
        CANEVAS, geo_courante, t.offset, t.charge, [&](const decoupe::Segment &s) {
            pixels_image[s.rangee] += s.n;
            /* Phase 5b : recopier ici l'en-tête de message (§4.6) puis les
             * s.octets octets de la charge, lus dans le pbuf à partir de
             * t.entete + s.debut, dans l'anneau de la liaison s.rangee. */
        });
    octets_image += t.charge;
    compteurs.octets += t.charge;

    if (octets_image < geo_courante.taille)
        return;

    /* Toutes les tranches sont là. */
    image_en_cours = false;
    compteurs.images++;
    compteurs.segments += segments_image;
    for (int r = 0; r < NB_RANGEES; ++r)
        compteurs.pixels_rangee[r] += pixels_image[r];
    gpio_xor_mask(1u << PIN_MESURE_IMAGE);

    pret.id = image_courante;
    pret.largeur = geo_courante.w;
    pret.hauteur = geo_courante.h;
    pret.format = geo_courante.format;
    pret.protocole = proto_courant;
    pret.t_premier_us = t_premier_courant;
    pret.t_dernier_us = time_us_64();
    image_prete = true;

    if (source != nullptr) {
        ack_ip = *source;
        ack_port = port;
        ack_possible = true;
    }
}

/* Appelé par lwIP à chaque datagramme. Contexte d'interruption en mode
 * `threadsafe_background` : rester court. */
void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *source, u16_t port) {
    if (p == nullptr)
        return;
    compteurs.paquets++;

    uint32_t magic = 0;
    Tranche t;
    bool tranche = false;
    if (p->tot_len >= 4)
        pbuf_copy_partial(p, &magic, 4, 0);

    if (magic == PXL2_MAGIC)
        tranche = lire_pxl2(p, source, port, t);
    else if (magic == PXL1_MAGIC)
        tranche = lire_pxl1(p, t);
    else
        compteurs.rejets++;

    if (tranche)
        recevoir_tranche(p, t, source, port);
    pbuf_free(p);
}

void noter_ip() {
    std::snprintf(ip_texte, sizeof(ip_texte), "%s",
                  ip4addr_ntoa(netif_ip4_addr(netif_default)));
}

} // namespace

namespace reseau {

bool connecter() {
    palette_par_defaut();

    gpio_init(PIN_MESURE_IMAGE);
    gpio_set_dir(PIN_MESURE_IMAGE, GPIO_OUT);
    gpio_init(PIN_MESURE_RX);
    gpio_set_dir(PIN_MESURE_RX, GPIO_OUT);

    if (cyw43_arch_init_with_country(WIFI_PAYS)) {
        printf("  cyw43_arch_init : ECHEC\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();

    /* LE levier de latence. Sans cela, 10 à 100 ms s'ajoutent à chaque image. */
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

    cyw43_arch_lwip_begin();
    pcb = udp_new();
    const bool lie = pcb != nullptr && udp_bind(pcb, IP_ANY_TYPE, PXL2_PORT) == ERR_OK;
    if (lie)
        udp_recv(pcb, sur_paquet, nullptr);
    cyw43_arch_lwip_end();
    if (!lie) {
        printf("  udp_bind sur %d : ECHEC\n", PXL2_PORT);
        return false;
    }

    printf("  association a « %s »...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        /* Pas fatal : entretenir() relancera l'association chaque seconde. */
        printf("  association : ECHEC (SSID, mot de passe, ou hors de portee ?)\n"
               "  nouvelle tentative chaque seconde\n");
        return true;
    }
    noter_ip();
    associe = true;
    return true;
}

/* Surveille le lien et relance une association quand il tombe — repris du
 * module capture (26/09/2026). L'association est ASYNCHRONE : la version
 * bloquante gèle la boucle principale jusqu'à 30 s. */
void entretenir() {
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
            compteurs.reconnexions++;
            printf("\n  reseau OPERATIONNEL : %s:%d\n", ip_texte, PXL2_PORT);
        }
        return;
    }
    if (associe) {
        associe = false;
        compteurs.deconnexions++;
        printf("\n  reseau PERDU (etat %d) — reassociation chaque seconde\n", etat);
    }
    if (etat == CYW43_LINK_JOIN || etat == CYW43_LINK_NOIP)
        return; /* une association est déjà en cours */
    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
}

bool image_complete(Image &out) {
    cyw43_arch_lwip_begin();
    const bool nouvelle = image_prete;
    if (nouvelle) {
        out = pret;
        image_prete = false;
    }
    cyw43_arch_lwip_end();
    return nouvelle;
}

void acquitter(const Image &img) {
    cyw43_arch_lwip_begin();
    if (ack_possible && pcb != nullptr) {
        if (img.protocole == 1) {
            /* Le récepteur v1 accusait en PXL1 type PING : le sniffer mesure
             * son aller-retour ainsi, on ne change rien pour lui. */
            pxl1_entete e{};
            e.magic = PXL1_MAGIC;
            e.type = PXL1_TYPE_PING;
            e.frame_id = img.id;
            envoyer(&e, PXL1_ENTETE, &ack_ip, ack_port);
        } else {
            pxl2_entete e{};
            e.magic = PXL2_MAGIC;
            e.type = PXL2_TYPE_ACK;
            e.format = img.format;
            e.frame_id = img.id;
            e.largeur = img.largeur;
            e.hauteur = img.hauteur;
            envoyer(&e, PXL2_ENTETE, &ack_ip, ack_port);
        }
    }
    cyw43_arch_lwip_end();
}

bool geometrie_changee(uint16_t &largeur, uint16_t &hauteur, uint8_t &format) {
    cyw43_arch_lwip_begin();
    const bool change = geo_a_signaler;
    if (change) {
        largeur = geo_connue.w;
        hauteur = geo_connue.h;
        format = geo_connue.format;
        geo_a_signaler = false;
    }
    cyw43_arch_lwip_end();
    return change;
}

uint8_t luminosite_demandee() {
    const uint8_t v = demande_luminosite;
    demande_luminosite = 0;
    return v;
}

Stats stats() {
    cyw43_arch_lwip_begin();
    const Stats s = compteurs;
    cyw43_arch_lwip_end();
    return s;
}

const char *adresse_ip() { return ip_texte; }

} // namespace reseau
