/**
 * Banc de coexistence — le client d'un réseau (test R1)
 *
 * Il tient le rôle d'un périphérique, posé à 3 m de la console : il rejoint le
 * réseau de son type (BANC_RESEAU, fixé à la compilation) avec son adresse
 * fixe, et en joue le rôle :
 *
 *   image      un écran : il reçoit les quatre copies, reconnaît les images
 *              complètes et les accuse ; il compte les tranches perdues et
 *              les gels, plus de 50 ms entre deux images d'une même copie.
 *   son        un haut-parleur : il compte les blocs perdus et mesure la
 *              gigue, ce que son tampon devra absorber ; il accuse un bloc
 *              sur 20.
 *   manettes   quatre manettes : chacune envoie son état toutes les 50 ms, et
 *              à chaque appui, tout de suite puis deux fois à 4 ms ; il mesure
 *              l'aller-retour de chaque paquet.
 *   cartouche  le lecteur : il envoie les blocs qu'on lui demande.
 *
 * Deux fois par seconde il se signale, une fois par seconde il envoie son
 * bilan à son point d'accès, qui l'imprime : un client n'a besoin que d'un
 * chargeur USB. Relié au PC, il imprime aussi son bilan lui-même.
 *
 * Commandes, une par ligne : « num N » (1 à 4, adresse .2 à .5), « eco N »
 * (0 aucune économie, 1 performance, 2 défaut, 3 agressive), « r », « s », « h ».
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
constexpr uint32_t GEL_US = 50'000;

udp_pcb *pcb = nullptr;
ip_addr_t point_acces{};
reglages::Reglages reg{};

/* Le bilan en cours : cumulé, sauf gigue et trou, remis à zéro chaque seconde. */
banc_bilan bilan{};
outils::Histo rtt{};
uint32_t t_derniere_arrivee = 0;
bool une_arrivee = false;

void emettre(uint8_t type, uint8_t flux, uint8_t drapeaux, uint32_t seq, uint32_t t_us,
             const void *charge, uint16_t n_charge, uint16_t n_total) {
    banc_entete e{BANC_MAGIC, type, RES, flux, drapeaux, seq, t_us};
    if (radio::envoyer(pcb, point_acces, R.port, &e, BANC_ENTETE, charge, n_charge, n_total))
        bilan.envoyes++;
    else
        bilan.echecs++;
}

/* Le plus long silence de la seconde, entre deux paquets de données. */
void noter_arrivee() {
    const uint32_t t = time_us_32();
    if (une_arrivee && t - t_derniere_arrivee > bilan.trou_max_us)
        bilan.trou_max_us = t - t_derniere_arrivee;
    t_derniere_arrivee = t;
    une_arrivee = true;
}

/* ──────────────────────────────────────────────────────────────── image */

struct FluxImage {
    bool amorce = false;
    bool soldee = false;    /* l'image courante est comptée, complète ou non */
    uint32_t image = 0;
    uint8_t masque = 0;
    bool vue = false;       /* une image complète a déjà été vue */
    uint32_t t_complete = 0;
};
FluxImage images[BANC_COPIES];

constexpr uint8_t TOUTES = (1u << BANC_IMAGE_TRANCHES) - 1;

uint8_t compter(uint8_t m) {
    uint8_t n = 0;
    for (; m; m &= (uint8_t)(m - 1))
        n++;
    return n;
}

void recevoir_image(const banc_entete &e) {
    FluxImage &f = images[e.flux % BANC_COPIES];
    const uint8_t k = e.drapeaux & BANC_TRANCHE_MASQUE;
    if (k >= BANC_IMAGE_TRANCHES)
        return;
    if (!f.amorce) {
        /* La première image est prise en route : on ne la compte pas. */
        f.amorce = true;
        f.image = e.seq;
        f.masque = 0;
        f.soldee = true;
    }
    if (e.seq != f.image) {
        if ((int32_t)(e.seq - f.image) < 0) {
            bilan.doublons++; /* une tranche d'une image déjà passée */
            return;
        }
        if (!f.soldee) {
            bilan.incompletes++;
            bilan.perdus += BANC_IMAGE_TRANCHES - compter(f.masque);
        }
        const uint32_t sautees = e.seq - f.image - 1;
        bilan.incompletes += sautees;
        bilan.perdus += sautees * BANC_IMAGE_TRANCHES;
        f.image = e.seq;
        f.masque = 0;
        f.soldee = false;
    }
    if (f.masque & (1u << k)) {
        bilan.doublons++;
        return;
    }
    f.masque |= (uint8_t)(1u << k);
    bilan.recus++;
    if (f.masque == TOUTES && !f.soldee) {
        f.soldee = true;
        bilan.completes++;
        const uint32_t t = time_us_32();
        if (f.vue && t - f.t_complete > GEL_US)
            bilan.trous_50ms++;
        f.vue = true;
        f.t_complete = t;
        /* L'accusé de l'image, comme la tête : il renvoie l'horodatage de la
         * tranche qui l'a complétée. */
        emettre(BANC_ACCUSE, e.flux, 0, e.seq, e.t_us, nullptr, 0, BANC_ENTETE);
    }
}

/* ────────────────────────────────────────────────────────────────── son */

struct FluxSon {
    outils::Suivi suivi;
    bool reference = false;
    uint32_t r0 = 0;
    int32_t min = 0, max = 0;
    bool fenetre = false;
};
FluxSon sons[BANC_COPIES];

void recevoir_son(const banc_entete &e) {
    FluxSon &f = sons[e.flux % BANC_COPIES];
    const uint32_t avant = bilan.doublons;
    f.suivi.noter(e.seq, bilan.perdus, bilan.doublons);
    if (bilan.doublons != avant)
        return;
    bilan.recus++;
    bilan.completes++;
    /* Le retard RELATIF du bloc : son arrivée moins son heure théorique. Les
     * deux horloges ne sont pas synchronisées, mais sur une seconde leur
     * dérive est de quelques µs : l'écart entre le plus en avance et le plus en
     * retard est ce que le tampon du haut-parleur devra absorber. */
    const uint32_t decalage = time_us_32() - e.seq * BANC_SON_PERIODE_US;
    if (!f.reference) {
        f.reference = true;
        f.r0 = decalage;
    }
    const int32_t d = (int32_t)(decalage - f.r0);
    if (!f.fenetre) {
        f.min = f.max = d;
        f.fenetre = true;
    } else {
        if (d < f.min) f.min = d;
        if (d > f.max) f.max = d;
    }
    if (e.seq % BANC_SON_ACCUSE_1_SUR == 0)
        emettre(BANC_ACCUSE, e.flux, 0, e.seq, e.t_us, nullptr, 0, BANC_ENTETE);
}

void clore_fenetre_son() {
    uint32_t gigue = 0;
    for (auto &f : sons) {
        if (f.fenetre && (uint32_t)(f.max - f.min) > gigue)
            gigue = (uint32_t)(f.max - f.min);
        f.fenetre = false;
    }
    bilan.gigue_us = gigue;
}

/* ───────────────────────────────────────────────────────────── manettes */

struct Manette {
    uint32_t seq = 0;
    uint64_t t_etat = 0;      /* prochain état périodique   */
    uint64_t t_appui = 0;     /* prochain appui             */
    uint8_t repetitions = 0;  /* répétitions encore à faire */
    uint64_t t_repetition = 0;
};
Manette manettes[BANC_COPIES];
uint32_t alea = 0x2545F491u;

void envoyer_etat(uint8_t k) {
    emettre(BANC_DONNEE, k, 0, manettes[k].seq++, time_us_32(), nullptr, 0, BANC_MANETTE_OCTETS);
}

void servir_manettes(uint64_t t) {
    for (uint8_t k = 0; k < BANC_COPIES; ++k) {
        Manette &m = manettes[k];
        if (m.t_etat == 0) {
            m.t_etat = t + k * 12'500; /* les quatre manettes décalées */
            m.t_appui = t + 500'000 + outils::hasard(alea) % 1'500'000;
        }
        if (t >= m.t_appui) {
            /* Un appui : tout de suite, puis deux répétitions à 4 ms. */
            envoyer_etat(k);
            m.repetitions = 2;
            m.t_repetition = t + BANC_MANETTE_REPETITION_US;
            m.t_etat = t + BANC_MANETTE_PERIODE_US;
            m.t_appui = t + 500'000 + outils::hasard(alea) % 1'500'000;
        } else if (m.repetitions && t >= m.t_repetition) {
            envoyer_etat(k);
            m.repetitions--;
            m.t_repetition = t + BANC_MANETTE_REPETITION_US;
        } else if (t >= m.t_etat) {
            envoyer_etat(k);
            m.t_etat += BANC_MANETTE_PERIODE_US;
            if (m.t_etat < t)
                m.t_etat = t + BANC_MANETTE_PERIODE_US;
        }
    }
}

void recevoir_accuse(const banc_entete &e) {
    const uint32_t dt = time_us_32() - e.t_us;
    if (dt < 2'000'000u) {
        bilan.accuses++;
        rtt.noter(dt);
    }
}

/* ──────────────────────────────────────────────────────────── cartouche */

/* Les blocs demandés, en attente d'envoi : la demande arrive dans le rappel,
 * l'envoi se fait dans la boucle. */
uint16_t file[2 * BANC_CARTOUCHE_FENETRE];
uint8_t file_debut = 0, file_n = 0;
uint8_t bloc[BANC_CARTOUCHE_BLOC];

void recevoir_demande(const banc_entete &e, pbuf *p) {
    const uint32_t n = e.seq < BANC_CARTOUCHE_FENETRE ? e.seq : BANC_CARTOUCHE_FENETRE;
    uint16_t blocs[BANC_CARTOUCHE_FENETRE];
    if (p->tot_len < BANC_ENTETE + 2 * n)
        return;
    pbuf_copy_partial(p, blocs, (u16_t)(2 * n), BANC_ENTETE);
    /* Une nouvelle demande remplace l'ancienne : le point d'accès redemande
     * ce qui lui manque encore, rien de ce qui reste en file n'est utile. */
    file_debut = 0;
    file_n = 0;
    for (uint32_t i = 0; i < n; ++i)
        file[file_n++] = blocs[i];
}

void servir_cartouche(uint64_t t) {
    static uint64_t t_annonce = 0;
    if (t - t_annonce >= BANC_CARTOUCHE_PERIODE_ANNONCE_US) {
        t_annonce = t;
        emettre(BANC_ANNONCE, 0, 0, 0, time_us_32(), nullptr, 0, BANC_CARTOUCHE_ANNONCE_OCTETS);
    }
    /* Huit blocs par tour de boucle au plus : la console et le bilan passent. */
    for (int i = 0; i < 8 && file_n; ++i) {
        const uint16_t b = file[file_debut];
        file_debut = (uint8_t)((file_debut + 1) % (2 * BANC_CARTOUCHE_FENETRE));
        file_n--;
        const uint32_t motif = b; /* le point d'accès vérifie les 4 premiers octets */
        std::memcpy(bloc, &motif, 4);
        emettre(BANC_BLOC, 0, 0, b, time_us_32(), bloc, BANC_CARTOUCHE_BLOC,
                BANC_ENTETE + BANC_CARTOUCHE_BLOC);
    }
}

/* ───────────────────────────────────────────────────────────── réception */

void sur_paquet(void *, udp_pcb *, pbuf *p, const ip_addr_t *, u16_t) {
    if (p == nullptr)
        return;
    banc_entete e;
    if (p->tot_len >= BANC_ENTETE && pbuf_copy_partial(p, &e, BANC_ENTETE, 0) == BANC_ENTETE &&
        e.magic == BANC_MAGIC && e.reseau == RES) {
        switch (e.type) {
        case BANC_DONNEE:
            noter_arrivee();
            if (RES == BANC_IMAGE)
                recevoir_image(e);
            else if (RES == BANC_SON)
                recevoir_son(e);
            break;
        case BANC_ACCUSE:
            if (RES == BANC_MANETTES)
                recevoir_accuse(e);
            break;
        case BANC_DEMANDE:
            if (RES == BANC_CARTOUCHE) {
                noter_arrivee();
                recevoir_demande(e, p);
            }
            break;
        default:
            break;
        }
    }
    pbuf_free(p);
}

/* ─────────────────────────────────────────────────────────── le bilan */

void envoyer_bilan() {
    static char l[1024];
    cyw43_arch_lwip_begin();
    if (RES == BANC_SON)
        clore_fenetre_son();
    bilan.t_ms = to_ms_since_boot(get_absolute_time());
    bilan.numero = reg.numero;
    bilan.economie = reg.economie;
    bilan.rssi = radio::rssi();
    bilan.reconnexions = radio::reconnexions();
    std::memcpy(bilan.histo, rtt.n, sizeof(bilan.histo));
    const banc_bilan b = bilan;
    if (radio::pret())
        emettre(BANC_BILAN, 0, 0, 0, time_us_32(), &b, sizeof(b), BANC_ENTETE + sizeof(b));
    bilan.gigue_us = 0;
    bilan.trou_max_us = 0;
    cyw43_arch_lwip_end();

    size_t n = (size_t)std::snprintf(
        l, sizeof(l),
        "BNC role=client res=%s t=%lu num=%u ct=%lu eco=%u rssi=%d recus=%lu perdus=%lu "
        "doublons=%lu completes=%lu incompletes=%lu trous50=%lu envoyes=%lu echecs=%lu "
        "acc=%lu reco=%lu gigue_us=%lu trou_max_us=%lu",
        R.nom, (unsigned long)b.t_ms, b.numero, (unsigned long)b.t_ms, b.economie, b.rssi,
        (unsigned long)b.recus, (unsigned long)b.perdus, (unsigned long)b.doublons,
        (unsigned long)b.completes, (unsigned long)b.incompletes, (unsigned long)b.trous_50ms,
        (unsigned long)b.envoyes, (unsigned long)b.echecs, (unsigned long)b.accuses,
        (unsigned long)b.reconnexions, (unsigned long)b.gigue_us, (unsigned long)b.trou_max_us);
    outils::ecrire_histo(l + n, sizeof(l) - n, "rtt", b.histo);
    printf("%s\n", l);
}

void aide() {
    printf("\n  client %u du reseau « %s » (192.168.%u.%u), economie %u\n"
           "  num N = numero 1 a 4, adresse .2 a .5\n"
           "  eco N = economie de la radio : 0 aucune, 1 performance, 2 defaut, 3 agressive\n"
           "  (chaque reglage est garde en flash, puis la carte redemarre)\n"
           "  r = remise a zero    s = etat    h = cette aide\n\n",
           reg.numero, R.ssid, R.octet3, banc::octet_peripherique(reg.numero), reg.economie);
}

void commandes() {
    char l[64];
    if (!outils::ligne(l, sizeof(l)))
        return;
    int v = 0;
    reglages::Reglages r = reg;
    if (outils::commande(l, "num", v) && v >= 1 && v <= BANC_CLIENTS_MAX) {
        r.numero = (uint8_t)v;
        reglages::ecrire_et_redemarrer(r);
    } else if (outils::commande(l, "eco", v) && v >= 0 && v <= 3) {
        r.economie = (uint8_t)v;
        reglages::ecrire_et_redemarrer(r);
    } else if (!std::strcmp(l, "r")) {
        cyw43_arch_lwip_begin();
        bilan = banc_bilan{};
        rtt = outils::Histo{};
        for (auto &f : images)
            f = FluxImage{};
        for (auto &f : sons)
            f = FluxSon{};
        cyw43_arch_lwip_end();
        printf("BNC role=client res=%s evenement=remise_a_zero\n", R.nom);
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
    printf("\n=== Banc de coexistence — client %u du reseau « %s » ===\n", reg.numero, R.nom);
    printf("BNC role=client res=%s evenement=demarrage num=%u eco=%u\n", R.nom, reg.numero,
           reg.economie);

    radio::adresse(point_acces, R.octet3, banc::OCTET_POINT_ACCES);
    if (!radio::client(R.ssid, BANC_MOT_DE_PASSE, R.octet3,
                       banc::octet_peripherique(reg.numero), reg.economie)) {
        printf("  la radio ne demarre pas : rien a mesurer\n");
        while (true)
            sleep_ms(1000);
    }
    pcb = radio::ouvrir(R.port, sur_paquet);
    aide();

    uint64_t t_signal = time_us_64(), t_bilan = time_us_64();
    while (true) {
        radio::entretenir();
        commandes();
        const uint64_t t = time_us_64();
        if (t - t_bilan >= 1'000'000) {
            t_bilan += 1'000'000;
            envoyer_bilan(); /* imprimé même sans lien : on voit qu'il manque */
        }
        if (!radio::pret())
            continue;
        cyw43_arch_lwip_begin();
        if (t - t_signal >= BANC_SIGNAL_PERIODE_US) {
            t_signal = t;
            emettre(BANC_SIGNAL, 0, 0, 0, time_us_32(), nullptr, 0, 24);
        }
        if (RES == BANC_MANETTES)
            servir_manettes(t);
        else if (RES == BANC_CARTOUCHE)
            servir_cartouche(t);
        cyw43_arch_lwip_end();
    }
}
