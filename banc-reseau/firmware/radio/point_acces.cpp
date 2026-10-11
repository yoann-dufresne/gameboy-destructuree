/**
 * Banc de coexistence — le point d'accès d'un réseau (test R1)
 *
 * Il tient le rôle du microcontrôleur de la console : il émet le réseau de son
 * type de périphérique (BANC_RESEAU, fixé à la compilation) et en produit le
 * trafic réel, au plus fort :
 *
 *   image      une copie par écran, quatre au plus : 5 tranches par image,
 *              59,73 images/s, chacune partie quand ses lignes sont capturées.
 *              Les écrans accusent chaque image.
 *   son        quatre copies d'un bloc de 976 octets toutes les 5 ms.
 *   manettes   il accuse chaque paquet des manettes, aussitôt.
 *   cartouche  toutes les 20 s, il demande 1 Mo au lecteur, par fenêtres de
 *              32 blocs, et chronomètre la copie.
 *
 * Au banc, chaque réseau n'a qu'un client : les quatre copies vont toutes à
 * lui, et l'air est occupé comme avec quatre périphériques.
 *
 * Une ligne `BNC role=ap …` par seconde sur la console USB, et une ligne
 * `BNC role=client …` pour chaque bilan reçu d'un client : tools/banc.py les
 * lit. Commandes, une par ligne : « plan 3 », « plan 1 », « canal N »,
 * « r » (remise à zéro), « s » (état), « h » (aide).
 */
#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "banc.h"
#include "outils.hpp"
#include "radio.hpp"
#include "reglages.hpp"
#include "reseaux.hpp"
#include "secrets.hpp"

#ifndef BANC_RESEAU
#error "BANC_RESEAU doit etre defini a la compilation (0 image, 1 son, 2 manettes, 3 cartouche)"
#endif

namespace {

constexpr uint8_t RES = BANC_RESEAU;
constexpr const banc::Reseau &R = banc::RESEAUX[RES];

/* Période entre deux tentatives de copie de cartouche : le pire cas n'est pas
 * l'insertion rare, c'est la rafale qui tombe pendant qu'un autre réseau joue. */
constexpr uint64_t PERIODE_COPIE_US = 20'000'000;
constexpr uint64_t GARDE_FENETRE_US = 30'000; /* sans bloc depuis 30 ms : redemander */
constexpr uint64_t ABANDON_COPIE_US = 20'000'000;

udp_pcb *pcb = nullptr;
uint8_t canal = 0;

/* ─────────────────────────────────────────────────────────── les clients */

struct Client {
    bool actif = false;
    ip_addr_t ip{};
    uint32_t vu_ms = 0;
    banc_bilan bilan{};
    bool bilan_nouveau = false;
    bool a_annoncer = false; /* nouveau : la boucle l'imprime, pas le rappel */
};
Client clients[BANC_CLIENTS_MAX];

uint32_t maintenant_ms() { return to_ms_since_boot(get_absolute_time()); }

/* Rappel lwIP : le client qui vient de parler, enregistré s'il est nouveau. */
Client *noter_client(const ip_addr_t *ip) {
    Client *libre = nullptr;
    for (auto &c : clients) {
        if (c.actif && ip_addr_cmp(&c.ip, ip)) {
            c.vu_ms = maintenant_ms();
            return &c;
        }
        if (!c.actif && libre == nullptr)
            libre = &c;
    }
    if (libre == nullptr)
        return nullptr;
    libre->actif = true;
    ip_addr_copy(libre->ip, *ip);
    libre->vu_ms = maintenant_ms();
    libre->bilan_nouveau = false;
    libre->a_annoncer = true;
    return libre;
}

/* La destination de la copie `k` : les clients actifs, à tour de rôle. */
const Client *destination(int k) {
    const Client *actifs[BANC_CLIENTS_MAX];
    int n = 0;
    for (const auto &c : clients)
        if (c.actif)
            actifs[n++] = &c;
    return n ? actifs[k % n] : nullptr;
}

/* ──────────────────────────────────────────────────────────── les comptes */

/* Cumulés depuis la remise à zéro. Écrits par le rappel lwIP et par la boucle,
 * lus sous le verrou de lwIP. */
struct Comptes {
    uint32_t emis, echecs, recus, perdus, doublons, accuses, sans_client, retards;
    outils::Histo rtt;
    /* cartouche */
    uint32_t copies, abandons, redemandes, blocs_faux, duree_ms, duree_max_ms;
};
Comptes cpt{};
outils::Suivi suivi_manettes[BANC_COPIES];

void emettre(const ip_addr_t &ip, uint8_t type, uint8_t flux, uint8_t drapeaux,
             uint32_t seq, const void *charge, uint16_t n_charge, uint16_t n_total) {
    banc_entete e{BANC_MAGIC, type, RES, flux, drapeaux, seq, time_us_32()};
    if (radio::envoyer(pcb, ip, R.port, &e, BANC_ENTETE, charge, n_charge, n_total))
        cpt.emis++;
    else
        cpt.echecs++;
}

/* ─────────────────────────────────────────────────────────── la cartouche */

struct Copie {
    bool en_cours = false;
    uint64_t t_debut = 0, t_activite = 0, t_suivante = 0;
    uint32_t recus_bits[BANC_CARTOUCHE_BLOCS / 32]{};
    uint32_t nb_recus = 0;
    uint16_t fenetre[BANC_CARTOUCHE_FENETRE]{};
    uint8_t nb_fenetre = 0;
};
Copie copie;

bool bloc_recu(uint32_t i) { return copie.recus_bits[i / 32] & (1u << (i % 32)); }

/* Demande les 32 premiers blocs qui manquent encore. */
void demander(const Client &c) {
    copie.nb_fenetre = 0;
    for (uint32_t i = 0; i < BANC_CARTOUCHE_BLOCS && copie.nb_fenetre < BANC_CARTOUCHE_FENETRE; ++i)
        if (!bloc_recu(i))
            copie.fenetre[copie.nb_fenetre++] = (uint16_t)i;
    emettre(c.ip, BANC_DEMANDE, 0, 0, copie.nb_fenetre, copie.fenetre,
            (uint16_t)(2 * copie.nb_fenetre), (uint16_t)(BANC_ENTETE + 2 * BANC_CARTOUCHE_FENETRE));
    copie.t_activite = time_us_64();
}

bool fenetre_complete() {
    for (uint8_t k = 0; k < copie.nb_fenetre; ++k)
        if (!bloc_recu(copie.fenetre[k]))
            return false;
    return true;
}

void recevoir_bloc(const banc_entete &e, pbuf *p) {
    if (!copie.en_cours || e.seq >= BANC_CARTOUCHE_BLOCS)
        return;
    uint32_t motif = 0;
    if (p->tot_len >= BANC_ENTETE + 4)
        pbuf_copy_partial(p, &motif, 4, BANC_ENTETE);
    if (motif != e.seq)
        cpt.blocs_faux++;
    if (bloc_recu(e.seq)) {
        cpt.doublons++;
        return;
    }
    copie.recus_bits[e.seq / 32] |= 1u << (e.seq % 32);
    copie.nb_recus++;
    copie.t_activite = time_us_64();
}

void servir_cartouche(uint64_t t) {
    static uint64_t t_annonce = 0;
    const Client *c = destination(0);
    if (c == nullptr)
        return;
    if (t - t_annonce >= BANC_CARTOUCHE_PERIODE_ANNONCE_US) {
        t_annonce = t;
        emettre(c->ip, BANC_ANNONCE, 0, 0, 0, nullptr, 0, BANC_CARTOUCHE_ANNONCE_OCTETS);
    }
    if (!copie.en_cours) {
        if (t < copie.t_suivante)
            return;
        std::memset(copie.recus_bits, 0, sizeof(copie.recus_bits));
        copie.nb_recus = 0;
        copie.en_cours = true;
        copie.t_debut = t;
        demander(*c);
        return;
    }
    if (copie.nb_recus == BANC_CARTOUCHE_BLOCS) {
        const uint32_t ms = (uint32_t)((t - copie.t_debut) / 1000);
        cpt.copies++;
        cpt.duree_ms = ms;
        if (ms > cpt.duree_max_ms)
            cpt.duree_max_ms = ms;
        copie.en_cours = false;
        copie.t_suivante = t + PERIODE_COPIE_US;
        return;
    }
    if (t - copie.t_debut > ABANDON_COPIE_US) {
        cpt.abandons++;
        copie.en_cours = false;
        copie.t_suivante = t + PERIODE_COPIE_US;
        return;
    }
    if (fenetre_complete())
        demander(*c);
    else if (t - copie.t_activite > GARDE_FENETRE_US) {
        cpt.redemandes++;
        demander(*c);
    }
}

/* ───────────────────────────────────────────────────────────── réception */

void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *source, u16_t) {
    if (p == nullptr)
        return;
    banc_entete e;
    if (p->tot_len >= BANC_ENTETE && pbuf_copy_partial(p, &e, BANC_ENTETE, 0) == BANC_ENTETE &&
        e.magic == BANC_MAGIC && e.reseau == RES) {
        Client *c = noter_client(source);
        switch (e.type) {
        case BANC_ACCUSE: {
            const uint32_t dt = time_us_32() - e.t_us;
            if (dt < 2'000'000u) {
                cpt.accuses++;
                cpt.rtt.noter(dt);
            }
            break;
        }
        case BANC_BILAN:
            if (c != nullptr && p->tot_len >= BANC_ENTETE + sizeof(banc_bilan)) {
                pbuf_copy_partial(p, &c->bilan, sizeof(banc_bilan), BANC_ENTETE);
                c->bilan_nouveau = true;
            }
            break;
        case BANC_DONNEE:
            if (RES == BANC_MANETTES) {
                /* L'accusé repart aussitôt, comme celui de l'injecteur. */
                cpt.recus++;
                suivi_manettes[e.flux % BANC_COPIES].noter(e.seq, cpt.perdus, cpt.doublons);
                e.type = BANC_ACCUSE;
                if (radio::envoyer(pcb, *source, R.port, &e, BANC_ENTETE, nullptr, 0,
                                   BANC_MANETTE_OCTETS))
                    cpt.emis++;
                else
                    cpt.echecs++;
            }
            break;
        case BANC_BLOC:
            if (RES == BANC_CARTOUCHE) {
                cpt.recus++;
                recevoir_bloc(e, p);
            }
            break;
        default:
            break; /* signal, annonce : le client est noté, c'est tout */
        }
    }
    pbuf_free(p);
}

/* ─────────────────────────────────────────────────────────────── émission */

/* Image : l'instant de départ de chaque tranche dans l'image, en µs. Une
 * tranche part quand ses 35 lignes sont capturées ; la dernière, de 4 lignes,
 * à la 144e. */
uint32_t depart_tranche_us(int k) {
    int lignes = (k + 1) * BANC_IMAGE_LIGNES_TRANCHE;
    if (lignes > BANC_IMAGE_LIGNES_VISIBLES)
        lignes = BANC_IMAGE_LIGNES_VISIBLES;
    return (uint32_t)((uint64_t)lignes * BANC_IMAGE_LIGNE_NS / 1000);
}

void emettre_tranche(uint32_t image, int k) {
    const bool derniere = (k == BANC_IMAGE_TRANCHES - 1);
    const uint16_t taille = derniere ? BANC_IMAGE_OCTETS_DERNIERE : BANC_IMAGE_OCTETS_TRANCHE;
    const uint8_t drapeaux = (uint8_t)(k | (derniere ? BANC_TRANCHE_DERNIERE : 0));
    for (int copie_k = 0; copie_k < BANC_COPIES; ++copie_k) {
        const Client *c = destination(copie_k);
        if (c == nullptr) {
            cpt.sans_client++;
            return;
        }
        emettre(c->ip, BANC_DONNEE, (uint8_t)copie_k, drapeaux, image, nullptr, 0, taille);
    }
}

void emettre_bloc_son(uint32_t bloc) {
    for (int copie_k = 0; copie_k < BANC_COPIES; ++copie_k) {
        const Client *c = destination(copie_k);
        if (c == nullptr) {
            cpt.sans_client++;
            return;
        }
        emettre(c->ip, BANC_DONNEE, (uint8_t)copie_k, 0, bloc, nullptr, 0, BANC_SON_OCTETS);
    }
}

/* Une échéance périodique qui ne rattrape pas son retard en rafale : si la
 * boucle a pris plus d'une période de retard, elle repart de maintenant et le
 * compte. Un banc qui émettrait en rafale pour rattraper mesurerait autre
 * chose que le vrai trafic. */
struct Echeance {
    uint64_t t_ns = 0;
    bool due(uint64_t t_us, uint64_t periode_ns) {
        if (t_ns == 0)
            t_ns = t_us * 1000;
        if (t_us * 1000 < t_ns)
            return false;
        t_ns += periode_ns;
        if (t_us * 1000 > t_ns + periode_ns) {
            cpt.retards++;
            t_ns = t_us * 1000 + periode_ns;
        }
        return true;
    }
};

void servir_trafic() {
    const uint64_t t = time_us_64();
    if (RES == BANC_IMAGE) {
        static Echeance image;
        static uint64_t t_image = 0;
        static uint32_t numero = 0;
        static int prochaine = BANC_IMAGE_TRANCHES;
        if (prochaine == BANC_IMAGE_TRANCHES && image.due(t, BANC_IMAGE_PERIODE_NS)) {
            t_image = t;
            prochaine = 0;
            numero++;
        }
        while (prochaine < BANC_IMAGE_TRANCHES && t >= t_image + depart_tranche_us(prochaine)) {
            cyw43_arch_lwip_begin();
            emettre_tranche(numero, prochaine);
            cyw43_arch_lwip_end();
            prochaine++;
        }
    } else if (RES == BANC_SON) {
        static Echeance son;
        static uint32_t bloc = 0;
        if (son.due(t, (uint64_t)BANC_SON_PERIODE_US * 1000)) {
            cyw43_arch_lwip_begin();
            emettre_bloc_son(bloc++);
            cyw43_arch_lwip_end();
        }
    } else if (RES == BANC_CARTOUCHE) {
        cyw43_arch_lwip_begin();
        servir_cartouche(t);
        cyw43_arch_lwip_end();
    }
    /* Manettes : le point d'accès ne fait que répondre, depuis le rappel. */
}

/* ────────────────────────────────────────────────────────────── console */

void imprimer() {
    static char l[1024];
    cyw43_arch_lwip_begin();
    const Comptes c = cpt;
    const bool rafale = copie.en_cours;
    int nb_clients = 0;
    for (const auto &k : clients)
        nb_clients += k.actif;
    cyw43_arch_lwip_end();

    size_t n = (size_t)std::snprintf(
        l, sizeof(l),
        "BNC role=ap res=%s t=%lu canal=%u sta=%d cli=%d emis=%lu echecs=%lu "
        "recus=%lu perdus=%lu doublons=%lu acc=%lu sans_client=%lu retards=%lu",
        R.nom, (unsigned long)maintenant_ms(), canal, radio::stations(), nb_clients,
        (unsigned long)c.emis, (unsigned long)c.echecs, (unsigned long)c.recus,
        (unsigned long)c.perdus, (unsigned long)c.doublons, (unsigned long)c.accuses,
        (unsigned long)c.sans_client, (unsigned long)c.retards);
    if (RES == BANC_CARTOUCHE)
        n += (size_t)std::snprintf(
            l + n, sizeof(l) - n,
            " copies=%lu abandons=%lu redemandes=%lu faux=%lu duree_ms=%lu duree_max_ms=%lu rafale=%d",
            (unsigned long)c.copies, (unsigned long)c.abandons, (unsigned long)c.redemandes,
            (unsigned long)c.blocs_faux, (unsigned long)c.duree_ms,
            (unsigned long)c.duree_max_ms, rafale ? 1 : 0);
    outils::ecrire_histo(l + n, sizeof(l) - n, "rtt", c.rtt.n);
    printf("%s\n", l);

    /* Les bilans des clients, relayés tels quels. */
    for (auto &k : clients) {
        cyw43_arch_lwip_begin();
        const bool nouveau = k.actif && k.bilan_nouveau;
        const banc_bilan b = k.bilan;
        k.bilan_nouveau = false;
        cyw43_arch_lwip_end();
        if (!nouveau)
            continue;
        n = (size_t)std::snprintf(
            l, sizeof(l),
            "BNC role=client res=%s t=%lu num=%u ct=%lu eco=%u rssi=%d recus=%lu perdus=%lu "
            "doublons=%lu completes=%lu incompletes=%lu trous50=%lu envoyes=%lu echecs=%lu "
            "acc=%lu reco=%lu gigue_us=%lu trou_max_us=%lu",
            R.nom, (unsigned long)maintenant_ms(), b.numero, (unsigned long)b.t_ms, b.economie,
            b.rssi, (unsigned long)b.recus, (unsigned long)b.perdus, (unsigned long)b.doublons,
            (unsigned long)b.completes, (unsigned long)b.incompletes,
            (unsigned long)b.trous_50ms, (unsigned long)b.envoyes, (unsigned long)b.echecs,
            (unsigned long)b.accuses, (unsigned long)b.reconnexions, (unsigned long)b.gigue_us,
            (unsigned long)b.trou_max_us);
        outils::ecrire_histo(l + n, sizeof(l) - n, "rtt", b.histo);
        printf("%s\n", l);
    }
}

void expirer_clients() {
    const uint32_t t = maintenant_ms();
    cyw43_arch_lwip_begin();
    for (auto &c : clients) {
        if (c.actif && c.a_annoncer) {
            c.a_annoncer = false;
            printf("\n  client %s enregistre\n", ipaddr_ntoa(&c.ip));
        }
        if (c.actif && t - c.vu_ms > BANC_OUBLI_MS) {
            c.actif = false;
            printf("\n  client %s oublie : silencieux depuis %u ms\n", ipaddr_ntoa(&c.ip),
                   BANC_OUBLI_MS);
        }
    }
    cyw43_arch_lwip_end();
}

void aide() {
    printf("\n  point d'acces du reseau « %s », canal %u\n"
           "  plan 3 / plan 1 = canal du plan a trois canaux / du plan a un canal\n"
           "  canal N = canal N, de 1 a 13\n"
           "  (chaque reglage est garde en flash, puis la carte redemarre)\n"
           "  r = remise a zero    s = etat    h = cette aide\n\n",
           R.ssid, canal);
}

void commandes() {
    char l[64];
    if (!outils::ligne(l, sizeof(l)))
        return;
    int v = 0;
    reglages::Reglages r = reglages::lire();
    if (outils::commande(l, "plan", v) && (v == 1 || v == 3)) {
        r.canal = (v == 1) ? banc::CANAL_UNIQUE : R.canal3;
        reglages::ecrire_et_redemarrer(r);
    } else if (outils::commande(l, "canal", v) && v >= 1 && v <= 13) {
        r.canal = (uint8_t)v;
        reglages::ecrire_et_redemarrer(r);
    } else if (!std::strcmp(l, "r")) {
        cyw43_arch_lwip_begin();
        cpt = Comptes{};
        for (auto &s : suivi_manettes)
            s = outils::Suivi{};
        cyw43_arch_lwip_end();
        printf("BNC role=ap res=%s evenement=remise_a_zero\n", R.nom);
    } else if (!std::strcmp(l, "s")) {
        printf("\n  %s, %d station(s) associee(s)\n", radio::etat(), radio::stations());
        imprimer();
    } else {
        aide();
    }
}

} // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    const reglages::Reglages r = reglages::lire();
    canal = r.canal ? r.canal : R.canal3;
    printf("\n=== Banc de coexistence — point d'acces « %s » ===\n", R.nom);
    printf("BNC role=ap res=%s evenement=demarrage canal=%u\n", R.nom, canal);

    if (!radio::point_acces(R.ssid, BANC_MOT_DE_PASSE, canal, R.octet3)) {
        printf("  la radio ne demarre pas : rien a mesurer\n");
        while (true)
            sleep_ms(1000);
    }
    pcb = radio::ouvrir(R.port, sur_paquet);
    aide();

    uint64_t t_ligne = time_us_64();
    while (true) {
        servir_trafic();
        radio::entretenir();
        commandes();
        if (time_us_64() - t_ligne >= 1'000'000) {
            t_ligne += 1'000'000;
            expirer_clients();
            imprimer();
        }
    }
}
