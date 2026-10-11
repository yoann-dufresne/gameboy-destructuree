/**
 * Banc de coexistence — un faux écran pour le sniffer (test R2)
 *
 * Le sniffer, compilé en point d'accès (option POINT_ACCES du module
 * capture), émet le réseau image et envoie une copie de chaque tranche PXL1
 * à chaque écran qui s'est signalé. Ce firmware joue un de ces écrans, sans
 * dalle : il rejoint gb-image avec son adresse fixe, reconnaît les images
 * complètes et les accuse comme la tête (PXL1, type PING, même numéro
 * d'image), et compte ce qui manque.
 *
 * Il se signale par un PING marqué PXL1_FLAG_SIGNAL chaque seconde : un écran
 * à adresse fixe n'a pas de bail DHCP par lequel le sniffer le découvrirait.
 * La vraie tête, elle, est trouvée par son bail.
 *
 * Une ligne `BNC role=faux_ecran …` par seconde sur la console USB, que
 * tools/banc.py lit : relier les faux écrans au PC. Commandes : « num N »
 * (1 à 4, adresse .2 à .5), « r », « s », « h ».
 */
#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "outils.hpp"
#include "pxl1.h"
#include "radio.hpp"
#include "reglages.hpp"
#include "reseaux.hpp"
#include "secrets.hpp"

/* Extension de banc du 11/10/2026, la même que dans le sniffer
 * (capture/firmware/sniffer/src/net/reseau.cpp) : un PING ainsi marqué n'est
 * pas un accusé, seulement « je suis là ». À reporter dans ecran/firmware/
 * commun/pxl1.h si la règle de reseau.md §2.3 est gardée pour l'image. */
#define PXL1_FLAG_SIGNAL 0x80

namespace {

constexpr const banc::Reseau &R = banc::RESEAUX[BANC_IMAGE];
constexpr uint32_t GEL_US = 50'000;
constexpr uint32_t PERIODE_IMAGE_US = BANC_IMAGE_PERIODE_NS / 1000;

udp_pcb *pcb = nullptr;
reglages::Reglages reg{};
ip_addr_t sniffer{};

struct Comptes {
    uint32_t paquets, ctrl, rejets, completes, incompletes, doublons, trous50, accuses;
    uint32_t trou_max_us, ecart_max_us; /* sur la dernière seconde */
};
Comptes cpt{};

/* L'image en cours de réassemblage : offsets reçus, octets, taille connue à
 * la dernière tranche. */
bool amorce = false, soldee = true, vue = false;
uint16_t image = 0;
uint16_t offsets[16];
uint8_t nb_offsets = 0;
uint32_t octets = 0, total = 0;
uint32_t t_complete = 0, t_paquet = 0;

void signaler() {
    pxl1_entete e{};
    e.magic = PXL1_MAGIC;
    e.type = PXL1_TYPE_PING;
    e.flags = PXL1_FLAG_SIGNAL;
    radio::envoyer(pcb, sniffer, PXL1_PORT, &e, PXL1_ENTETE, nullptr, 0, PXL1_ENTETE);
}

void nouvelle_image(uint16_t id) {
    if (!soldee)
        cpt.incompletes++;
    /* Des numéros sautés : des images entières n'ont rien livré. */
    const uint16_t saut = (uint16_t)(id - image);
    if (amorce && saut > 1 && saut < 0x8000)
        cpt.incompletes += saut - 1u;
    image = id;
    nb_offsets = 0;
    octets = 0;
    total = 0;
    soldee = false;
}

void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *source, u16_t port) {
    if (p == nullptr)
        return;
    pxl1_entete e;
    if (p->tot_len < PXL1_ENTETE || pbuf_copy_partial(p, &e, PXL1_ENTETE, 0) != PXL1_ENTETE ||
        e.magic != PXL1_MAGIC) {
        cpt.rejets++;
        pbuf_free(p);
        return;
    }
    const uint32_t t = time_us_32();
    if (e.type == PXL1_TYPE_CTRL) {
        cpt.ctrl++;
    } else if (e.type == PXL1_TYPE_FRAME) {
        cpt.paquets++;
        if (t_paquet && t - t_paquet > cpt.trou_max_us)
            cpt.trou_max_us = t - t_paquet;
        t_paquet = t;
        if (!amorce) {
            amorce = true;
            image = e.frame_id;
            soldee = true; /* la première image est prise en route */
        }
        const int16_t ecart = (int16_t)(e.frame_id - image);
        if (ecart < 0) {
            cpt.doublons++;
        } else {
            if (ecart > 0)
                nouvelle_image(e.frame_id);
            bool deja = false;
            for (uint8_t i = 0; i < nb_offsets; ++i)
                deja |= offsets[i] == e.offset;
            const uint32_t lg = p->tot_len - PXL1_ENTETE;
            if (deja) {
                cpt.doublons++;
            } else if (!soldee) {
                if (nb_offsets < 16)
                    offsets[nb_offsets++] = e.offset;
                octets += lg;
                if (e.flags & PXL1_FLAG_DERNIERE)
                    total = e.offset + lg;
                if (total && octets == total) {
                    soldee = true;
                    cpt.completes++;
                    if (vue) {
                        const uint32_t dt = t - t_complete;
                        if (dt > GEL_US)
                            cpt.trous50++;
                        const uint32_t ec = dt > PERIODE_IMAGE_US ? dt - PERIODE_IMAGE_US
                                                                  : PERIODE_IMAGE_US - dt;
                        if (ec > cpt.ecart_max_us)
                            cpt.ecart_max_us = ec;
                    }
                    vue = true;
                    t_complete = t;
                    /* L'accusé de la tête : PXL1, type PING, même numéro. */
                    pxl1_entete a{};
                    a.magic = PXL1_MAGIC;
                    a.type = PXL1_TYPE_PING;
                    a.frame_id = e.frame_id;
                    if (radio::envoyer(pcb, *source, port, &a, PXL1_ENTETE, nullptr, 0,
                                       PXL1_ENTETE))
                        cpt.accuses++;
                }
            }
        }
    }
    pbuf_free(p);
}

void imprimer() {
    static char l[512];
    cyw43_arch_lwip_begin();
    const Comptes c = cpt;
    cpt.trou_max_us = 0;
    cpt.ecart_max_us = 0;
    cyw43_arch_lwip_end();
    std::snprintf(l, sizeof(l),
                  "BNC role=faux_ecran res=image t=%lu num=%u rssi=%d reco=%lu lien=%d "
                  "paquets=%lu ctrl=%lu rejets=%lu completes=%lu incompletes=%lu doublons=%lu "
                  "trous50=%lu acc=%lu trou_max_us=%lu ecart_max_us=%lu",
                  (unsigned long)to_ms_since_boot(get_absolute_time()), reg.numero,
                  radio::rssi(), (unsigned long)radio::reconnexions(), radio::pret() ? 1 : 0,
                  (unsigned long)c.paquets, (unsigned long)c.ctrl, (unsigned long)c.rejets,
                  (unsigned long)c.completes, (unsigned long)c.incompletes,
                  (unsigned long)c.doublons, (unsigned long)c.trous50, (unsigned long)c.accuses,
                  (unsigned long)c.trou_max_us, (unsigned long)c.ecart_max_us);
    printf("%s\n", l);
}

void aide() {
    printf("\n  faux ecran %u du reseau « %s » (192.168.%u.%u)\n"
           "  num N = numero 1 a 4, adresse .2 a .5 (garde en flash, puis redemarrage)\n"
           "  r = remise a zero    s = etat    h = cette aide\n\n",
           reg.numero, R.ssid, R.octet3, banc::octet_peripherique(reg.numero));
}

void commandes() {
    char l[64];
    if (!outils::ligne(l, sizeof(l)))
        return;
    int v = 0;
    if (outils::commande(l, "num", v) && v >= 1 && v <= BANC_CLIENTS_MAX) {
        reglages::Reglages r = reg;
        r.numero = (uint8_t)v;
        reglages::ecrire_et_redemarrer(r);
    } else if (!std::strcmp(l, "r")) {
        cyw43_arch_lwip_begin();
        cpt = Comptes{};
        amorce = false;
        soldee = true;
        vue = false;
        cyw43_arch_lwip_end();
        printf("BNC role=faux_ecran res=image evenement=remise_a_zero\n");
    } else if (!std::strcmp(l, "s")) {
        printf("\n  lien %s, rssi %d dBm\n", radio::etat(), radio::rssi());
    } else {
        aide();
    }
}

} // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    reg = reglages::lire();
    if (reg.numero < 1 || reg.numero > BANC_CLIENTS_MAX)
        reg.numero = 1;
    printf("\n=== Banc de coexistence — faux ecran %u ===\n", reg.numero);
    printf("BNC role=faux_ecran res=image evenement=demarrage num=%u\n", reg.numero);

    radio::adresse(sniffer, R.octet3, banc::OCTET_POINT_ACCES);
    if (!radio::client(R.ssid, BANC_MOT_DE_PASSE, R.octet3,
                       banc::octet_peripherique(reg.numero), 0)) {
        printf("  la radio ne demarre pas : rien a mesurer\n");
        while (true)
            sleep_ms(1000);
    }
    pcb = radio::ouvrir(PXL1_PORT, sur_paquet);
    aide();

    uint64_t t_signal = time_us_64(), t_ligne = time_us_64();
    while (true) {
        radio::entretenir();
        commandes();
        const uint64_t t = time_us_64();
        if (radio::pret() && t - t_signal >= 1'000'000) {
            t_signal = t;
            cyw43_arch_lwip_begin();
            signaler();
            cyw43_arch_lwip_end();
        }
        if (t - t_ligne >= 1'000'000) {
            t_ligne += 1'000'000;
            imprimer();
        }
    }
}
