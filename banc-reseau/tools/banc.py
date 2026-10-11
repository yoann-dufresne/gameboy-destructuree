#!/usr/bin/env python3
"""Le banc de coexistence, vu du PC : il règle les cartes, enregistre, et donne
le verdict en clair (reseau.md §7, tests R1 à R3).

    ./tools/banc.py ports                         # les cartes branchées, et qui elles sont
    ./tools/banc.py plan 3                        # points d'accès sur le plan à trois canaux
    ./tools/banc.py plan 1                        #   … ou à un seul canal
    ./tools/banc.py num /dev/ttyACM4 2            # un client ou un faux écran devient le n° 2
    ./tools/banc.py eco /dev/ttyACM5 2            # économie de la radio d'un client (0 à 3)
    ./tools/banc.py mesure --duree 600 --sortie r1-plan3.log
    ./tools/banc.py verdict r1-plan3.log
    ./tools/banc.py comparer r1-plan3.log r1-plan1.log
    ./tools/banc.py simuler r1-bon --sortie /tmp/essai.log   # un enregistrement fabriqué

`mesure` lit toutes les cartes Raspberry Pi branchées en USB, remet leurs
compteurs à zéro, puis enregistre leurs lignes `BNC …` avec l'heure du PC.
`verdict` reconnaît seul le test enregistré, R1, R2 ou R3, à ce qu'il contient.

Les seuils sont écrits ici, avant la mesure, avec ce qui les justifie : ce sont
des propositions, que la première mesure peut corriger, pas le résultat qu'on
espérait.

Dépendance : pyserial, pour ports, plan, num et mesure.
"""
import argparse
import os
import queue
import random
import sys
import threading
import time

# ⚠️ La copie de BANC_BORNES_US de firmware/commun/banc.h : bornes supérieures
# des seaux, en µs ; le dernier seau prend le reste. tools/test_banc.py vérifie
# qu'elles restent identiques.
BORNES_US = [500, 1000, 1500, 2000, 3000, 4000, 5000, 6000,
             8000, 10000, 12000, 15000, 20000, 25000, 30000, 40000,
             50000, 60000, 80000, 100000, 150000, 200000, 300000, 500000]

# ─────────────────────────────────────────────────── les exigences des modules
#
# Image (reseau.md §2.1, module écran) : quelques millisecondes et aucun gel.
IMAGE_PERTE_MAX = 0.0001     # une image sur 10 000 : ce que la tête a vu, via une box, le 29/09
IMAGE_GELS_MAX = 0           # aucun intervalle de plus de 50 ms entre deux images d'une copie
IMAGE_RTT_MEDIANE_MS = 5     # le sniffer mesure 1,85 ms en moyenne, en direct (08/10/2026)
IMAGE_RTT_P99_MS = 20
# Son (module son, plan-de-tests T4.2 et §7) : moins de 0,1 % de pertes ; le
# tampon du haut-parleur, 20 à 50 ms, doit absorber la gigue.
SON_PERTE_MAX = 0.001
SON_TAMPON_COURT_MS = 20
SON_TAMPON_LONG_MS = 50
# Manettes (module manette, T3.4) : médiane 10 ms, 99 % sous 30 ms, d'un bout
# à l'autre. Le banc mesure l'ALLER-RETOUR, qui majore l'aller : un aller-retour
# dans les seuils les tient a fortiori.
MANETTE_MEDIANE_MS = 10
MANETTE_P99_MS = 30
MANETTE_PERTE_MAX = 0.01
# Cartouche (module cartouche §9) : 1 Mo en une seconde environ.
CARTOUCHE_BON_MS = 2000
CARTOUCHE_ACCEPTABLE_MS = 5000
# R2 : une tranche part toutes les 35 lignes, 3,8 ms. Émettre ses copies doit
# prendre moins, sinon la file de tranches déborde et la capture perd des images.
TRANCHE_BUDGET_US = 3805

CADENCE_IMAGE = 59.727
COPIES = 4

PLAN_TROIS = {"image": 11, "son": 1, "manettes": 6, "cartouche": 6}


def fr(x, decimales=1):
    """Un nombre à la française : virgule décimale."""
    return f"{x:.{decimales}f}".replace(".", ",")


def pct(x, decimales=3):
    return fr(100 * x, decimales) + " %"


# ─────────────────────────────────────────────────────────────── les lignes

def lire_histo(texte):
    """« 3:12,4:5 » → [0, 0, 0, 12, 5, 0…] ; « - » → que des zéros."""
    h = [0] * (len(BORNES_US) + 1)
    if texte and texte != "-":
        for paire in texte.split(","):
            i, n = paire.split(":")
            h[int(i)] += int(n)
    return h


def lire_valeur(cle, v):
    if cle == "rtt":
        return lire_histo(v)
    if ":" in v and "," in v:  # bus_maitre : « ok:12,ecr:0,… »
        return {k: int(x) for k, x in (p.split(":") for p in v.split(","))}
    try:
        return int(v)
    except ValueError:
        return v


def lire_ligne(texte):
    """« BNC role=ap res=image t=12 … » → dict, ou None."""
    i = texte.find("BNC ")
    if i < 0:
        return None
    d = {}
    for jeton in texte[i + 4:].split():
        if "=" in jeton:
            cle, v = jeton.split("=", 1)
            d[cle] = lire_valeur(cle, v)
    return d if "role" in d else None


class Enregistrement:
    """Les lignes d'une séance, rangées par carte."""

    def __init__(self, lignes):
        self.series = {}         # clé → [(t_pc, dict)]
        self.demarrages = []     # (t_pc, carte) : un redémarrage pendant la mesure
        self.t0 = self.t1 = None
        vus = set()
        for t, port, d in lignes:
            if self.t0 is None:
                self.t0 = t
            self.t1 = t
            if "evenement" in d:
                if d["evenement"] == "demarrage":
                    self.demarrages.append((t, f"{d['role']} {d.get('res', '')} {port}"))
                continue
            role = d["role"]
            if role == "client":
                # Un client relié au PC imprime son bilan, que son point d'accès
                # relaie aussi : on ne le compte qu'une fois.
                cle_unique = (d.get("res"), d.get("num"), d.get("ct"))
                if cle_unique in vus:
                    continue
                vus.add(cle_unique)
                cle = ("client", d.get("res"), d.get("num"))
            elif role == "ap":
                cle = ("ap", d.get("res"))
            elif role == "faux_ecran":
                cle = ("faux_ecran", d.get("num"))
            elif role == "sniffer_ecran":
                cle = ("sniffer_ecran", d.get("ip"))
            elif role == "bus_esclave":
                cle = ("bus_esclave", d.get("a"))
            else:
                cle = (role,)
            self.series.setdefault(cle, []).append((t, d))

    @property
    def duree(self):
        return (self.t1 - self.t0) if self.t0 is not None else 0.0

    def serie(self, *cle):
        return self.series.get(tuple(cle), [])

    def cles(self, role):
        return sorted((k for k in self.series if k[0] == role), key=str)


def lire_fichier(chemin):
    lignes = []
    with open(chemin, encoding="utf-8", errors="replace") as f:
        for brut in f:
            morceaux = brut.rstrip("\n").split(" ", 2)
            if len(morceaux) < 3:
                continue
            try:
                t = float(morceaux[0])
            except ValueError:
                continue
            d = lire_ligne(morceaux[2])
            if d is not None:
                lignes.append((t, morceaux[1], d))
    return Enregistrement(lignes)


# ───────────────────────────────────────────────────────────── les calculs

def cumul(serie, cle):
    """L'accroissement d'un compteur cumulé sur la séance. Une baisse est une
    remise à zéro ou un redémarrage : on repart de la nouvelle valeur."""
    total, avant = 0, None
    for _, d in serie:
        v = d.get(cle)
        if not isinstance(v, int):
            continue
        if avant is not None:
            total += v - avant if v >= avant else v
        avant = v
    return total


def cumul_histo(serie, cle="rtt"):
    h = [0] * (len(BORNES_US) + 1)
    avant = None
    for _, d in serie:
        v = d.get(cle)
        if not isinstance(v, list):
            continue
        if avant is not None:
            baisse = any(a > b for a, b in zip(avant, v))
            for i, (a, b) in enumerate(zip(avant, v)):
                h[i] += b if baisse else b - a
        avant = v
    return h


def centile(h, p):
    """Le retard sous lequel tombe la fraction p des mesures, en ms : la borne
    haute de son seau, donc une majoration. None si aucune mesure."""
    total = sum(h)
    if not total:
        return None
    seuil, cumule = p * total, 0
    for i, n in enumerate(h):
        cumule += n
        if cumule >= seuil:
            return BORNES_US[i] / 1000 if i < len(BORNES_US) else float("inf")
    return float("inf")


def ms(x):
    if x is None:
        return "—"
    if x == float("inf"):
        return "> 500 ms"
    return f"≤ {fr(x, 1)} ms"


class Verdict:
    """Une liste de constats : ✅ tenu, ⚠️ à surveiller, ❌ non tenu."""

    def __init__(self, titre):
        self.titre = titre
        self.constats = []
        self.chiffres = {}

    def ok(self, texte):
        self.constats.append(("✅", texte))

    def attention(self, texte):
        self.constats.append(("⚠️ ", texte))

    def echec(self, texte):
        self.constats.append(("❌", texte))

    def selon(self, tenu, texte):
        (self.ok if tenu else self.echec)(texte)

    @property
    def tenu(self):
        return all(s != "❌" for s, _ in self.constats)

    def imprimer(self):
        print(f"\n  {self.titre}")
        for symbole, texte in self.constats:
            print(f"    {symbole} {texte}")


# ─────────────────────────────────────────────────────────────── R1

def rafales(enr):
    """Les instants (heure du PC) où la cartouche copiait."""
    return [t for t, d in enr.serie("ap", "cartouche") if d.get("rafale") == 1]


def pendant(t, instants, avant=1.5, apres=0.5):
    return any(t - avant <= x <= t + apres for x in instants)


def r1_image(enr, v, instants_rafale):
    ap = enr.serie("ap", "image")
    clients = enr.cles("client")
    clients = [k for k in clients if k[1] == "image"]
    if not ap:
        v.echec("aucune ligne du point d'accès image : est-il branché au PC ?")
        return
    if not clients:
        v.echec(f"aucun bilan de l'écran : le client n'est pas associé "
                f"(stations vues par le point d'accès : {ap[-1][1].get('sta', '?')})")
        return
    completes = sum(cumul(enr.series[k], "completes") for k in clients)
    incompletes = sum(cumul(enr.series[k], "incompletes") for k in clients)
    gels = sum(cumul(enr.series[k], "trous50") for k in clients)
    images = completes + incompletes
    perte = incompletes / images if images else 1.0
    h = cumul_histo(ap)
    med, p99 = centile(h, 0.5), centile(h, 0.99)
    v.chiffres.update(images=images, perte=perte, gels=gels, mediane=med, p99=p99)
    attendu = CADENCE_IMAGE * COPIES * enr.duree
    if images < 0.9 * attendu:
        v.attention(f"{images} images reçues pour {int(attendu)} attendues en "
                    f"{fr(enr.duree, 0)} s : le trafic n'a pas tourné tout le temps")
    v.selon(perte <= IMAGE_PERTE_MAX,
            f"images perdues : {incompletes} sur {images}, {pct(perte)} "
            f"(au plus {pct(IMAGE_PERTE_MAX, 2)})")
    v.selon(gels <= IMAGE_GELS_MAX,
            f"gels de plus de 50 ms : {gels} (aucun admis)")
    v.selon(med is not None and med <= IMAGE_RTT_MEDIANE_MS and p99 <= IMAGE_RTT_P99_MS,
            f"aller-retour : médiane {ms(med)}, 99 % {ms(p99)} "
            f"(au plus {IMAGE_RTT_MEDIANE_MS} et {IMAGE_RTT_P99_MS} ms)")
    if cumul(ap, "retards"):
        v.attention(f"le point d'accès a pris du retard {cumul(ap, 'retards')} fois : "
                    "le trafic émis est en dessous du vrai")
    if instants_rafale:
        for k in clients:
            serie = enr.series[k]
            perdues = [0, 0]
            secondes = [0, 0]
            for (t, d), (_, d0) in zip(serie[1:], serie):
                i = 1 if pendant(t, instants_rafale) else 0
                secondes[i] += 1
                perdues[i] += max(0, d.get("incompletes", 0) - d0.get("incompletes", 0))
                perdues[i] += max(0, d.get("trous50", 0) - d0.get("trous50", 0))
            v.chiffres["pendant_rafales"] = perdues[1]
            texte = (f"pendant les copies de cartouche ({secondes[1]} s) : "
                     f"{perdues[1]} image(s) perdue(s) ou gel(s) ; hors copies : {perdues[0]}")
            (v.ok if perdues[1] == 0 else v.attention)(texte)


def r1_son(enr, v, instants_rafale):
    clients = [k for k in enr.cles("client") if k[1] == "son"]
    if not enr.serie("ap", "son"):
        v.echec("aucune ligne du point d'accès son : est-il branché au PC ?")
        return
    if not clients:
        v.echec("aucun bilan du haut-parleur : le client n'est pas associé")
        return
    recus = sum(cumul(enr.series[k], "recus") for k in clients)
    perdus = sum(cumul(enr.series[k], "perdus") for k in clients)
    total = recus + perdus
    perte = perdus / total if total else 1.0
    gigues = sorted(d.get("gigue_us", 0) for k in clients for _, d in enr.series[k][1:])
    gigue_max = gigues[-1] / 1000 if gigues else None
    v.chiffres.update(blocs=total, perte=perte, gigue_max=gigue_max)
    attendu = COPIES * 200 * enr.duree
    if total < 0.9 * attendu:
        v.attention(f"{total} blocs pour {int(attendu)} attendus : le trafic n'a pas "
                    "tourné tout le temps")
    v.selon(perte < SON_PERTE_MAX,
            f"blocs perdus : {perdus} sur {total}, {pct(perte)} (moins de {pct(SON_PERTE_MAX, 1)})")
    if gigue_max is None:
        v.echec("aucune mesure de gigue")
    elif gigue_max <= SON_TAMPON_COURT_MS:
        v.ok(f"gigue la plus forte sur une seconde : {fr(gigue_max)} ms : "
             f"un tampon de {SON_TAMPON_COURT_MS} ms suffit")
    elif gigue_max <= SON_TAMPON_LONG_MS:
        v.attention(f"gigue la plus forte sur une seconde : {fr(gigue_max)} ms : "
                    f"il faut un tampon de {SON_TAMPON_LONG_MS} ms, le haut du budget")
    else:
        v.echec(f"gigue la plus forte sur une seconde : {fr(gigue_max)} ms : "
                f"au-delà des {SON_TAMPON_LONG_MS} ms du budget")
    if instants_rafale and gigues:
        pire = [0.0, 0.0]
        for k in clients:
            for t, d in enr.series[k][1:]:
                i = 1 if pendant(t, instants_rafale) else 0
                pire[i] = max(pire[i], d.get("gigue_us", 0) / 1000)
        texte = (f"gigue pendant les copies de cartouche : {fr(pire[1])} ms ; "
                 f"hors copies : {fr(pire[0])} ms")
        (v.ok if pire[1] <= SON_TAMPON_COURT_MS else v.attention)(texte)


def r1_manettes(enr, v):
    ap = enr.serie("ap", "manettes")
    clients = [k for k in enr.cles("client") if k[1] == "manettes"]
    if not ap:
        v.echec("aucune ligne du point d'accès manettes : est-il branché au PC ?")
        return
    if not clients:
        v.echec("aucun bilan des manettes : le client n'est pas associé")
        return
    h = [0] * (len(BORNES_US) + 1)
    for k in clients:
        h = [a + b for a, b in zip(h, cumul_histo(enr.series[k]))]
    med, p99 = centile(h, 0.5), centile(h, 0.99)
    recus, perdus = cumul(ap, "recus"), cumul(ap, "perdus")
    perte_aller = perdus / (recus + perdus) if recus + perdus else 1.0
    accuses_emis = cumul(ap, "emis")
    accuses_recus = sum(cumul(enr.series[k], "acc") for k in clients)
    perte_retour = (1 - accuses_recus / accuses_emis) if accuses_emis else 1.0
    eco = enr.series[clients[0]][-1][1].get("eco", 0)
    v.chiffres.update(mediane=med, p99=p99, perte=perte_aller)
    v.selon(med is not None and med <= MANETTE_MEDIANE_MS and p99 <= MANETTE_P99_MS,
            f"aller-retour : médiane {ms(med)}, 99 % {ms(p99)} "
            f"(au plus {MANETTE_MEDIANE_MS} et {MANETTE_P99_MS} ms, majorant de l'aller)")
    v.selon(perte_aller < MANETTE_PERTE_MAX,
            f"paquets perdus vers la console : {perdus} sur {recus + perdus}, {pct(perte_aller)}")
    (v.ok if perte_retour < MANETTE_PERTE_MAX else v.attention)(
        f"accusés perdus vers la manette : {pct(max(0.0, perte_retour))}")
    if eco:
        v.attention(f"radio de la manette en économie (mode {eco}) : c'est l'accusé, vers "
                    "la manette, qui attend ; l'aller seul est plus court")


def r1_cartouche(enr, v):
    ap = enr.serie("ap", "cartouche")
    if not ap:
        v.echec("aucune ligne du point d'accès cartouche : est-il branché au PC ?")
        return
    durees, avant = [], None
    for _, d in ap:
        n = d.get("copies", 0)
        if avant is not None and n > avant:
            durees.append(d.get("duree_ms", 0))
        avant = n
    abandons, faux = cumul(ap, "abandons"), cumul(ap, "faux")
    v.chiffres.update(copies=len(durees), duree_max=max(durees) if durees else None)
    if not durees:
        v.echec("aucune copie de 1 Mo terminée : le lecteur est-il associé ?")
        return
    pire = max(durees)
    texte = (f"{len(durees)} copies de 1 Mo : de {fr(min(durees) / 1000, 2)} à "
             f"{fr(pire / 1000, 2)} s (environ 1 s attendue)")
    if pire <= CARTOUCHE_BON_MS:
        v.ok(texte)
    elif pire <= CARTOUCHE_ACCEPTABLE_MS:
        v.attention(texte + " : acceptable, la console attend plus longtemps")
    else:
        v.echec(texte)
    v.selon(abandons == 0, f"copies abandonnées : {abandons}")
    v.selon(faux == 0, f"blocs abîmés : {faux}")
    redem = cumul(ap, "redemandes")
    if redem:
        v.attention(f"{redem} fenêtre(s) redemandée(s) : des blocs se sont perdus en route")


def plan_vu(enr):
    canaux = {}
    for res in PLAN_TROIS:
        s = enr.serie("ap", res)
        if s:
            canaux[res] = s[-1][1].get("canal")
    if canaux and len(set(canaux.values())) == 1:
        return f"plan à un canal ({next(iter(canaux.values()))})", canaux
    if canaux == PLAN_TROIS:
        return "plan à trois canaux (11, 1, 6, 6)", canaux
    return "canaux " + ", ".join(f"{k} {c}" for k, c in canaux.items()), canaux


def verdict_r1(enr):
    instants = rafales(enr)
    resultats = []
    for nom, fonction in (("image", r1_image), ("son", r1_son)):
        v = Verdict(f"Réseau {nom}")
        fonction(enr, v, instants)
        resultats.append(v)
    for nom, fonction in (("manettes", r1_manettes), ("cartouche", r1_cartouche)):
        v = Verdict(f"Réseau {nom}")
        fonction(enr, v)
        resultats.append(v)
    return resultats


# ─────────────────────────────────────────────────────────────── R2

def verdict_r2(enr):
    resultats = []
    sn = enr.serie("sniffer")
    v = Verdict("Le sniffer, point d'accès du réseau image")
    if not sn:
        v.echec("aucune ligne du sniffer : compilé avec -DPOINT_ACCES=ON, et branché au PC ?")
    else:
        dernier = sn[-1][1]
        for cle, texte in (("tranches_perdues", "tranches perdues, file pleine : la boucle n'a pas suivi"),
                           ("perdues", "images capturées jamais lues par la boucle"),
                           ("douteuses", "images douteuses à la capture"),
                           ("echecs", "émissions refusées par lwIP")):
            n = cumul(sn, cle)
            v.selon(n == 0, f"{texte} : {n}")
        emission = max(d.get("emission_max_us", 0) for _, d in sn)
        marge = 1 - emission / TRANCHE_BUDGET_US
        texte = (f"émettre une tranche et ses copies : {fr(emission / 1000, 2)} ms au plus, "
                 f"pour {fr(TRANCHE_BUDGET_US / 1000, 2)} ms entre deux tranches "
                 f"(marge {pct(marge, 0)})")
        if emission < TRANCHE_BUDGET_US / 2:
            v.ok(texte)
        elif emission < TRANCHE_BUDGET_US:
            v.attention(texte + " : marge mince")
        else:
            v.echec(texte)
        v.chiffres.update(ecrans=dernier.get("ecrans"), emission_max_us=emission)
        if dernier.get("refuses"):
            v.attention(f"{dernier['refuses']} écran(s) refusé(s) : plus de quatre se sont signalés")
    resultats.append(v)

    for k in enr.cles("faux_ecran"):
        serie = enr.series[k]
        v = Verdict(f"Faux écran n° {k[1]}")
        completes, incompletes = cumul(serie, "completes"), cumul(serie, "incompletes")
        gels = cumul(serie, "trous50")
        images = completes + incompletes
        perte = incompletes / images if images else 1.0
        attendu = CADENCE_IMAGE * enr.duree
        if images < 0.9 * attendu:
            v.attention(f"{images} images reçues pour {int(attendu)} attendues : "
                        "associé tout le temps ?")
        v.selon(perte <= IMAGE_PERTE_MAX,
                f"images perdues : {incompletes} sur {images}, {pct(perte)}")
        v.selon(gels <= IMAGE_GELS_MAX, f"gels de plus de 50 ms : {gels}")
        v.chiffres.update(images=images, perte=perte, gels=gels)
        resultats.append(v)

    for k in enr.cles("sniffer_ecran"):
        d = enr.series[k][-1][1]
        v = Verdict(f"Aller-retour vers {k[1]}, mesuré par le sniffer")
        moy = d.get("rtt_moy_us", 0) / 1000
        v.selon(d.get("rtt_n", 0) > 0 and moy <= IMAGE_RTT_MEDIANE_MS,
                f"moyenne {fr(moy, 2)} ms, au pire {fr(d.get('rtt_max_us', 0) / 1000, 1)} ms, "
                f"sur {d.get('rtt_n', 0)} accusés")
        resultats.append(v)
    return resultats


# ─────────────────────────────────────────────────────────────── R3

def verdict_r3(enr):
    resultats = []
    maitre = enr.serie("bus_maitre")
    v = Verdict("Le maître du bus (l'injecteur)")
    if not maitre:
        v.echec("aucune ligne du maître : est-il branché au PC ?")
        resultats.append(v)
        return resultats
    cycles = cumul(maitre, "cycles")
    attendu = enr.duree * 1e6 / 20000
    if cycles < 0.95 * attendu:
        v.attention(f"{cycles} interrogations pour {int(attendu)} attendues")
    for esclave in ("sniffer", "son", "doublure"):
        serie = [(t, d[esclave]) for t, d in maitre if isinstance(d.get(esclave), dict)]
        if not serie:
            v.echec(f"{esclave} : jamais interrogé")
            continue
        ok = cumul(serie, "ok")
        erreurs = {c: cumul(serie, c) for c in ("ecr", "lec", "crc", "seq")}
        refus = serie[-1][1].get("refus", 0)
        detail = ", ".join(f"{c} {n}" for c, n in erreurs.items())
        v.selon(sum(erreurs.values()) == 0 and refus == 0 and ok > 0,
                f"{esclave} : {ok} échanges réussis ; erreurs : {detail} ; "
                f"ordres refusés par l'esclave : {refus}")
        if esclave == "doublure":
            feux = cumul(serie, "feux")
            v.selon(feux > 0, f"le feu vert de la doublure a basculé {feux} fois")
    resultats.append(v)
    for k in enr.cles("bus_esclave"):
        serie = enr.series[k]
        v = Verdict(f"Esclave {k[1]}")
        inc, refus = cumul(serie, "incoherents"), cumul(serie, "refus")
        v.selon(inc == 0 and refus == 0,
                f"{cumul(serie, 'recus')} ordres reçus ; refusés : {refus} ; "
                f"intacts au CRC mais faux : {inc}")
        resultats.append(v)
    return resultats


# ─────────────────────────────────────────────────────────────── verdict

def analyser(enr):
    """(nom du test, résultats) pour chaque test présent dans l'enregistrement."""
    tests = []
    roles = {k[0] for k in enr.series}
    if roles & {"ap", "client"}:
        tests.append(("R1, quatre radios côte à côte", verdict_r1(enr)))
    if roles & {"sniffer", "faux_ecran", "sniffer_ecran"}:
        tests.append(("R2, le sniffer et ses écrans", verdict_r2(enr)))
    if roles & {"bus_maitre", "bus_esclave"}:
        tests.append(("R3, le bus de la console", verdict_r3(enr)))
    return tests


def commande_verdict(args):
    enr = lire_fichier(args.fichier)
    if not enr.series:
        sys.exit(f"{args.fichier} : aucune ligne BNC. Est-ce un enregistrement de « mesure » ?")
    print(f"\n{args.fichier} : {fr(enr.duree / 60, 1)} min enregistrées")
    if enr.demarrages:
        print("  ⚠️  carte(s) redémarrée(s) pendant la mesure :")
        for t, carte in enr.demarrages:
            print(f"      à {fr(t - enr.t0, 0)} s : {carte}")
    tous_tenus = True
    for nom, resultats in analyser(enr):
        print(f"\n═══ {nom}")
        if nom.startswith("R1"):
            print(f"  {plan_vu(enr)[0]}")
        for v in resultats:
            v.imprimer()
        tenu = all(v.tenu for v in resultats)
        tous_tenus &= tenu
        print(f"\n  → {nom.split(',')[0]} {'TENU' if tenu else 'NON TENU'}")
    print()
    return 0 if tous_tenus else 1


def commande_comparer(args):
    a, b = lire_fichier(args.a), lire_fichier(args.b)
    va, vb = verdict_r1(a), verdict_r1(b)

    def plan_court(enr):
        texte = plan_vu(enr)[0]
        return texte.split(" (")[0] if texte.startswith("plan") else texte[:28]

    print(f"\n{'':26}{os.path.basename(args.a)[:28]:>30}{os.path.basename(args.b)[:28]:>30}")
    print(f"{'':26}{plan_court(a):>30}{plan_court(b):>30}")

    def f(v, cle, forme):
        x = v.chiffres.get(cle)
        return "—" if x is None else forme(x)

    lignes = [
        ("image : perdues", 0, "perte", pct),
        ("image : gels", 0, "gels", str),
        ("image : aller-retour 99 %", 0, "p99", ms),
        ("son : perdus", 1, "perte", pct),
        ("son : gigue max", 1, "gigue_max", lambda x: f"{fr(x)} ms"),
        ("manettes : médiane", 2, "mediane", ms),
        ("manettes : 99 %", 2, "p99", ms),
        ("cartouche : copie max", 3, "duree_max", lambda x: f"{fr(x / 1000, 2)} s"),
    ]
    for titre, i, cle, forme in lignes:
        print(f"{titre:26}{f(va[i], cle, forme):>30}{f(vb[i], cle, forme):>30}")
    tenu = ["oui" if all(v.tenu for v in x) else "non" for x in (va, vb)]
    print(f"{'R1 tenu':26}{tenu[0]:>30}{tenu[1]:>30}\n")
    return 0


# ───────────────────────────────────────────────────────────── les cartes

def ports_pico(demandes=None):
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("pyserial est requis :  pip install pyserial")
    if demandes:
        return demandes
    return sorted(p.device for p in list_ports.comports() if p.vid == 0x2E8A)


class Carte(threading.Thread):
    """Une carte en USB : ses lignes, horodatées par le PC, versées dans une file."""

    def __init__(self, port, file):
        super().__init__(daemon=True)
        import serial
        self.port, self.file = port, file
        self.s = serial.Serial(port, 115200, timeout=0.2)
        self.s.dtr = True   # sans DTR, le firmware n'écrit rien
        self.s.rts = True
        self.vivante = True

    def run(self):
        reste = b""
        while self.vivante:
            try:
                reste += self.s.read(self.s.in_waiting or 1)
            except Exception:
                self.file.put((time.time(), self.port, "BNC role=? evenement=deconnexion"))
                return
            *completes, reste = reste.split(b"\n")
            for l in completes:
                self.file.put((time.time(), self.port, l.decode("utf-8", "replace").rstrip("\r")))

    def ecrire(self, texte):
        self.s.write(texte.encode() + b"\n")

    def fermer(self):
        self.vivante = False
        try:
            self.s.close()
        except Exception:
            pass


def identifier(ports, duree=3.0):
    """port → la première ligne BNC qu'il a dite, et ses Carte ouvertes."""
    file = queue.Queue()
    cartes = {}
    for p in ports:
        try:
            cartes[p] = Carte(p, file)
            cartes[p].start()
        except Exception as e:
            print(f"  {p} : {e}")
    qui = {}
    fin = time.time() + duree
    while time.time() < fin and len(qui) < len(cartes):
        try:
            _, port, texte = file.get(timeout=0.2)
        except queue.Empty:
            continue
        d = lire_ligne(texte)
        if d and port not in qui and d["role"] != "?":
            qui[port] = d
    return qui, cartes


def nom_carte(d):
    if d is None:
        return "muette (pas un firmware du banc, ou rien ne sort)"
    role = d["role"]
    return " ".join(str(x) for x in (role, d.get("res", ""), f"n° {d['num']}" if "num" in d else "",
                                      d.get("a", "")) if x)


def commande_ports(args):
    ports = ports_pico(args.ports)
    if not ports:
        print("Aucune carte Raspberry Pi branchée en USB.")
        return 1
    qui, cartes = identifier(ports)
    for p in ports:
        print(f"  {p:16} {nom_carte(qui.get(p))}")
    for c in cartes.values():
        c.fermer()
    return 0


def commande_plan(args):
    qui, cartes = identifier(ports_pico(args.ports))
    aps = [p for p, d in qui.items() if d["role"] == "ap"]
    if not aps:
        print("Aucun point d'accès du banc trouvé en USB.")
    for p in aps:
        cartes[p].ecrire(f"plan {args.plan}")
        print(f"  {p} : {nom_carte(qui[p])} → plan à {args.plan} canal(aux), redémarrage")
    time.sleep(0.5)
    for c in cartes.values():
        c.fermer()
    print("Les clients rejoignent seuls leur réseau sur son nouveau canal. "
          "Vérifier dans une dizaine de secondes : ./tools/banc.py ports")
    return 0 if aps else 1


def commande_num(args):
    qui, cartes = identifier([args.port])
    d = qui.get(args.port)
    if d is None or d["role"] not in ("client", "faux_ecran"):
        print(f"{args.port} : {nom_carte(d)} — « num » ne vaut que pour un client ou un faux écran")
        code = 1
    else:
        cartes[args.port].ecrire(f"num {args.numero}")
        print(f"  {args.port} : {nom_carte(d)} → n° {args.numero}, adresse .{1 + args.numero}, "
              "redémarrage")
        code = 0
    time.sleep(0.5)
    for c in cartes.values():
        c.fermer()
    return code


def commande_eco(args):
    qui, cartes = identifier([args.port])
    d = qui.get(args.port)
    if d is None or d["role"] != "client":
        print(f"{args.port} : {nom_carte(d)} — « eco » ne vaut que pour un client")
        code = 1
    else:
        cartes[args.port].ecrire(f"eco {args.mode}")
        print(f"  {args.port} : {nom_carte(d)} → économie {args.mode}, redémarrage")
        code = 0
    time.sleep(0.5)
    for c in cartes.values():
        c.fermer()
    return code


def resume_direct(lignes):
    """Une ligne par carte, pendant la mesure : de quoi voir que tout tourne."""
    enr = Enregistrement(lignes)
    morceaux = []
    for k, serie in sorted(enr.series.items(), key=lambda x: str(x[0])):
        d = serie[-1][1]
        if k[0] == "ap":
            morceaux.append(f"{k[1]}:sta{d.get('sta')}")
        elif k[0] == "client":
            morceaux.append(f"{k[1]}{k[2]}:{d.get('recus', 0) or d.get('acc', 0)}")
        elif k[0] == "faux_ecran":
            morceaux.append(f"écran{k[1]}:{d.get('completes')}")
        elif k[0] == "sniffer":
            morceaux.append(f"sniffer:{d.get('ecrans')} écrans")
        elif k[0] == "bus_maitre":
            morceaux.append(f"bus:{d.get('cycles')}")
    return "  ".join(morceaux)


def commande_mesure(args):
    ports = ports_pico(args.ports)
    if not ports:
        sys.exit("Aucune carte Raspberry Pi branchée en USB.")
    file = queue.Queue()
    cartes = []
    for p in ports:
        try:
            c = Carte(p, file)
            c.start()
            cartes.append(c)
        except Exception as e:
            print(f"  {p} : {e}")
    print(f"{len(cartes)} carte(s) : {', '.join(c.port for c in cartes)}")
    print("Trois secondes pour s'installer, puis remise à zéro de tous les compteurs.")
    time.sleep(3)
    while not file.empty():
        file.get()
    for c in cartes:
        c.ecrire("r")
    debut = time.time()
    lignes = []
    t_resume = debut
    with open(args.sortie, "w", encoding="utf-8") as sortie:
        try:
            while time.time() - debut < args.duree:
                try:
                    t, port, texte = file.get(timeout=0.5)
                except queue.Empty:
                    continue
                sortie.write(f"{t:.3f} {port} {texte}\n")
                d = lire_ligne(texte)
                if d is not None:
                    lignes.append((t, port, d))
                if time.time() - t_resume >= 10:
                    t_resume = time.time()
                    sortie.flush()
                    print(f"  {fr(time.time() - debut, 0):>5} s  {resume_direct(lignes)}")
        except KeyboardInterrupt:
            print("\n  interrompu : l'enregistrement est gardé jusqu'ici")
    for c in cartes:
        c.fermer()
    print(f"Enregistré dans {args.sortie}. Le verdict :  ./tools/banc.py verdict {args.sortie}")
    return 0


# ─────────────────────────────────────────────────────── l'enregistrement fabriqué
#
# Pour éprouver l'outil sans carte : des lignes au format exact des firmwares,
# avec des compteurs cumulés qui avancent à la cadence réelle. Les scénarios
# « mauvais » dérèglent chacun un réseau d'une façon qui doit être vue.

def histo_texte(h):
    return ",".join(f"{i}:{n}" for i, n in enumerate(h) if n) or "-"


def tirer(h, us, alea):
    us = max(0, int(us * (0.6 + 0.8 * alea.random())))
    i = 0
    while i < len(BORNES_US) and us > BORNES_US[i]:
        i += 1
    h[i] += 1


def simuler_r1(duree, mauvais, alea):
    lignes = []
    t0 = 1_700_000_000.0
    canaux = PLAN_TROIS
    etat = {res: dict(emis=0, recus=0, perdus=0, acc=0, h=[0] * 25, copies=0, duree_ms=0)
            for res in canaux}
    cli = {res: dict(recus=0, perdus=0, completes=0, incompletes=0, trous50=0, envoyes=0,
                     acc=0, h=[0] * 25) for res in canaux}
    for s in range(duree + 1):
        t = t0 + s
        rafale = 1 if s % 20 < 1 else 0
        # image : 4 copies × 59,73 images/s
        n = int(CADENCE_IMAGE * COPIES)
        perdues = (3 if mauvais and s % 30 == 7 else 0)
        cli["image"]["completes"] += n - perdues
        cli["image"]["incompletes"] += perdues
        cli["image"]["recus"] += 5 * n - perdues
        cli["image"]["perdus"] += perdues
        cli["image"]["trous50"] += 1 if mauvais and s % 45 == 9 else 0
        etat["image"]["emis"] += 5 * n
        for _ in range(n - perdues):
            tirer(etat["image"]["h"], 1800 if not mauvais else 7000, alea)
        etat["image"]["acc"] += n - perdues
        # son : 4 × 200 blocs/s
        perdus = (6 if mauvais else 0) if s % 3 == 0 else 0
        cli["son"]["recus"] += 800 - perdus
        cli["son"]["perdus"] += perdus
        cli["son"]["completes"] += 800 - perdus
        etat["son"]["emis"] += 800
        # manettes : 4 × 20 états/s et quelques appuis
        envoyes = 80 + 3
        etat["manettes"]["recus"] += envoyes
        etat["manettes"]["emis"] += envoyes
        cli["manettes"]["envoyes"] += envoyes
        cli["manettes"]["acc"] += envoyes
        for _ in range(envoyes):
            tirer(cli["manettes"]["h"], 3500 if not mauvais else 18000, alea)
        # cartouche : une copie toutes les 20 s
        if s % 20 == 1 and s > 1:
            etat["cartouche"]["copies"] += 1
            etat["cartouche"]["duree_ms"] = 900 + alea.randint(0, 300) if not mauvais else 6200
        gigue = 3000 + alea.randint(0, 4000) + (40000 if mauvais and rafale else 0)
        for res in canaux:
            e = etat[res]
            ligne = (f"BNC role=ap res={res} t={s * 1000} canal={canaux[res]} sta=1 cli=1 "
                     f"emis={e['emis']} echecs=0 recus={e['recus']} perdus={e['perdus']} "
                     f"doublons=0 acc={e['acc']} sans_client=0 retards=0")
            if res == "cartouche":
                ligne += (f" copies={e['copies']} abandons=0 redemandes=0 faux=0 "
                          f"duree_ms={e['duree_ms']} duree_max_ms={e['duree_ms']} rafale={rafale}")
            ligne += f" rtt={histo_texte(e['h'])}"
            lignes.append(f"{t:.3f} /dev/ttyACM{list(canaux).index(res)} {ligne}")
            c = cli[res]
            lignes.append(
                f"{t + 0.01:.3f} /dev/ttyACM{list(canaux).index(res)} BNC role=client res={res} "
                f"t={s * 1000} num=1 ct={s * 1000 + 7} eco=0 rssi=-41 recus={c['recus']} "
                f"perdus={c['perdus']} doublons=0 completes={c['completes']} "
                f"incompletes={c['incompletes']} trous50={c['trous50']} envoyes={c['envoyes']} "
                f"echecs=0 acc={c['acc']} reco=1 gigue_us={gigue if res == 'son' else 0} "
                f"trou_max_us=9000 rtt={histo_texte(c['h'])}")
    return lignes


def simuler_r2(duree, mauvais, alea):
    lignes = []
    t0 = 1_700_000_000.0
    ecr = {n: dict(completes=0, incompletes=0, trous50=0) for n in (1, 2, 3)}
    trames = 0
    for s in range(0, duree + 1):
        t = t0 + s
        trames += 60
        for n, e in ecr.items():
            perdues = 2 if mauvais and n == 2 and s % 20 == 5 else 0
            e["completes"] += 60 - perdues
            e["incompletes"] += perdues
            e["trous50"] += 1 if perdues else 0
            lignes.append(f"{t + n / 100:.3f} /dev/ttyACM{n} BNC role=faux_ecran res=image "
                          f"t={s * 1000} num={n} rssi=-45 reco=1 lien=1 paquets={trames * 5} "
                          f"ctrl={s} rejets=0 completes={e['completes']} "
                          f"incompletes={e['incompletes']} doublons=0 trous50={e['trous50']} "
                          f"acc={e['completes']} trou_max_us=8000 ecart_max_us=1200")
        if s % 5 == 0:
            emission = 1400 if not mauvais else 4100
            lignes.append(f"{t:.3f} /dev/ttyACM0 BNC role=sniffer res=image t={s * 1000} ecrans=3 "
                          f"refuses=0 trames={trames} paquets={trames * 15} echecs=0 "
                          f"acc={trames * 3} emission_max_us={emission} attente_max_us=300 "
                          f"capture_trames={trames} douteuses=0 incompletes=0 rejetees=0 "
                          f"debordements=0 perdues=0 "
                          f"tranches_perdues={(s // 50) if mauvais else 0}")
            for n in (1, 2, 3):
                lignes.append(f"{t:.3f} /dev/ttyACM0 BNC role=sniffer_ecran res=image "
                              f"t={s * 1000} ip=192.168.4.{1 + n} bail=0 acc={trames} "
                              f"rtt_n={trames} rtt_moy_us={2100 if not mauvais else 9000} "
                              f"rtt_max_us=14000")
    return lignes


def simuler_r3(duree, mauvais, alea):
    lignes = []
    t0 = 1_700_000_000.0
    cpt = {e: dict(ok=0, ecr=0, lec=0, crc=0, seq=0, refus=0, feux=0) for e in ("sniffer", "son", "doublure")}
    for s in range(duree + 1):
        t = t0 + s
        for e, c in cpt.items():
            c["ok"] += 50
            if mauvais and e == "son" and s % 60 == 30:
                c["crc"] += 1
                c["ok"] -= 1
            if e == "doublure" and s % 5 == 0 and s:
                c["feux"] += 1
        champs = " ".join(f"{e}=" + ",".join(f"{k}:{v}" for k, v in c.items()) +
                          ",duree_max_us:410" for e, c in cpt.items())
        lignes.append(f"{t:.3f} /dev/ttyACM0 BNC role=bus_maitre t={s * 1000} cycles={50 * s} {champs}")
        for i, a in enumerate(("0x41", "0x42", "0x43")):
            lignes.append(f"{t:.3f} /dev/ttyACM{i + 1} BNC role=bus_esclave a={a} t={s * 1000} "
                          f"recus={50 * s} refus=0 incoherents=0 lectures={50 * s}")
    return lignes


SCENARIOS = {
    "r1-bon": (simuler_r1, False), "r1-mauvais": (simuler_r1, True),
    "r2-bon": (simuler_r2, False), "r2-mauvais": (simuler_r2, True),
    "r3-bon": (simuler_r3, False), "r3-mauvais": (simuler_r3, True),
}


def commande_simuler(args):
    fonction, mauvais = SCENARIOS[args.scenario]
    lignes = fonction(args.duree, mauvais, random.Random(args.graine))
    with open(args.sortie, "w", encoding="utf-8") as f:
        f.write("\n".join(lignes) + "\n")
    print(f"{len(lignes)} lignes, scénario {args.scenario}, dans {args.sortie}")
    return 0


# ─────────────────────────────────────────────────────────────── main

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = p.add_subparsers(dest="commande", required=True)

    s = sous.add_parser("ports", help="les cartes branchées, et qui elles sont")
    s.add_argument("ports", nargs="*")
    s.set_defaults(f=commande_ports)

    s = sous.add_parser("plan", help="régler les points d'accès : plan à 3 canaux ou à 1")
    s.add_argument("plan", type=int, choices=(1, 3))
    s.add_argument("--ports", nargs="*")
    s.set_defaults(f=commande_plan)

    s = sous.add_parser("num", help="numéro d'un client ou d'un faux écran (1 à 4)")
    s.add_argument("port")
    s.add_argument("numero", type=int, choices=(1, 2, 3, 4))
    s.set_defaults(f=commande_num)

    s = sous.add_parser("eco", help="économie de la radio d'un client : 0 aucune, "
                                    "1 performance, 2 défaut du pilote, 3 agressive")
    s.add_argument("port")
    s.add_argument("mode", type=int, choices=(0, 1, 2, 3))
    s.set_defaults(f=commande_eco)

    s = sous.add_parser("mesure", help="enregistrer toutes les cartes branchées")
    s.add_argument("--duree", type=float, default=600, help="en secondes (600 par défaut)")
    s.add_argument("--sortie", required=True)
    s.add_argument("--ports", nargs="*")
    s.set_defaults(f=commande_mesure)

    s = sous.add_parser("verdict", help="le verdict d'un enregistrement")
    s.add_argument("fichier")
    s.set_defaults(f=commande_verdict)

    s = sous.add_parser("comparer", help="deux enregistrements R1 côte à côte")
    s.add_argument("a")
    s.add_argument("b")
    s.set_defaults(f=commande_comparer)

    s = sous.add_parser("simuler", help="fabriquer un enregistrement, pour éprouver l'outil")
    s.add_argument("scenario", choices=sorted(SCENARIOS))
    s.add_argument("--sortie", required=True)
    s.add_argument("--duree", type=int, default=600)
    s.add_argument("--graine", type=int, default=1)
    s.set_defaults(f=commande_simuler)

    args = p.parse_args(argv)
    return args.f(args)


if __name__ == "__main__":
    sys.exit(main())
