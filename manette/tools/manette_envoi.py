#!/usr/bin/env python3
"""Fausse manette — émet `MAN1` comme la vraie, depuis le PC.

    ./tools/manette_envoi.py tour                    # chaque bouton tour à tour (T3.1)
    ./tools/manette_envoi.py hasard --duree 60       # des appuis au hasard
    ./tools/manette_envoi.py maintenir A             # A enfoncé 2 s, puis le silence (T3.3)
    ./tools/manette_envoi.py marche-arret --fois 5   # l'interrupteur, 5 fois (T3.5)
    ./tools/manette_envoi.py molettes                # les deux molettes, d'un bout à l'autre
    ./tools/manette_envoi.py silence --apres 5       # en marche, puis plus rien : piles retirées

Elle suit les règles de la vraie manette (plan §5.4) : un état part à chaque
changement, puis deux répétitions à 4 ms ; sans changement, un battement
toutes les 50 ms ; interrupteur sur arrêt, rien ne part, sauf l'ordre
« arrêt », répété jusqu'à son accusé, 2 s au plus.

Elle écoute les accusés de l'injecteur, et dit en clair ce que montrerait la
LED de la vraie manette : rouge quand l'injecteur répond, bleu sinon. Elle
mesure l'aller-retour.

    --cible 127.0.0.1    contre manette_ecoute.py sur ce PC
    --depuis 127.0.0.2   une deuxième fausse manette sur ce PC : l'injecteur
                         reconnaît les manettes à leur adresse
    --perte 5            5 % des paquets jamais émis

Le format fait foi dans firmware/commun/man1.h.
"""
import argparse
import random
import socket
import struct
import sys
import threading
import time

# firmware/commun/man1.h
PORT = 4244
MAGIC = b"MAN1"
TAILLE = 24
TYPE_ETAT, TYPE_ACCUSE = 0, 1
REPETITIONS, REPETITION_S, BATTEMENT_S = 2, 0.004, 0.050
ACCUSE_ROUGE_S, ARRET_ABANDON_S = 0.5, 2.0
BOUTONS = {"DROITE": 1, "GAUCHE": 2, "HAUT": 4, "BAS": 8,
           "A": 16, "B": 32, "SELECT": 64, "START": 128}
NOMS = list(BOUTONS)


def fr(x, d=2):
    return f"{x:.{d}f}".replace(".", ",")


def paquet(etat, session, numero, temps_ms):
    marche = etat["marche"]
    return MAGIC + struct.pack(
        "<BBBBBBHIII", TYPE_ETAT,
        etat["boutons"] if marche else 0,
        etat["volume"] if marche else 0,
        etat["contraste"] if marche else 0,
        1 if marche else 0, 0, etat["piles"],
        session, numero & 0xFFFFFFFF, temps_ms & 0xFFFFFFFF)


# ───────────────────────────────────────────────────── les gestes du joueur

def scenario(args):
    """Rend une fonction t (s) → état voulu, et la durée totale."""
    base = {"marche": True, "boutons": 0, "volume": 128, "contraste": 128,
            "piles": 2600}

    def piles(t):
        # --piles : de 2,8 V à 2,0 V sur la durée, comme une nuit de T2.5.
        if not args.piles or not args.duree:
            return 2600
        return int(2800 - 800 * min(1.0, t / args.duree))

    if args.geste == "tour":
        # Chaque bouton enfoncé 300 ms puis relâché 300 ms, tour à tour.
        def f(t):
            i = int(t / 0.6)
            appui = (t % 0.6) < 0.3
            return dict(base, boutons=(1 << (i % 8)) if appui else 0,
                        piles=piles(t))
        return f, args.duree or 8 * 0.6 * 3
    if args.geste == "hasard":
        rng = random.Random(args.graine)
        changements, t = [], 0.0
        while t < (args.duree or 60):
            t += rng.expovariate(4.0)
            changements.append((t, rng.randrange(256)))

        def f(t):
            b = 0
            for tc, v in changements:
                if tc > t:
                    break
                b = v
            return dict(base, boutons=b, piles=piles(t))
        return f, args.duree or 60
    if args.geste == "maintenir":
        masque = 0
        for nom in args.boutons or ["A"]:
            if nom.upper() not in BOUTONS:
                sys.exit(f"bouton inconnu : {nom} ({', '.join(NOMS)})")
            masque |= BOUTONS[nom.upper()]
        tenir = args.apres or 2.0
        # Puis le silence : la manette s'éteint d'un coup, sans « arrêt ».
        return (lambda t: dict(base, boutons=masque, piles=piles(t))
                if t < tenir else None), tenir + 1.0
    if args.geste == "marche-arret":
        def f(t):
            cycle = int(t / 2.0)
            if cycle >= args.fois:
                return dict(base, marche=False)
            return dict(base, marche=(t % 2.0) < 1.0, piles=piles(t))
        return f, 2.0 * args.fois + 2.5
    if args.geste == "molettes":
        def f(t):
            x = int(255 * abs(((t / 4.0) % 2.0) - 1.0))
            return dict(base, volume=x, contraste=255 - x, piles=piles(t))
        return f, args.duree or 8.0
    if args.geste == "silence":
        apres = args.apres or 5.0
        return (lambda t: dict(base, piles=piles(t)) if t < apres else None), \
            args.duree or apres + 20.0
    sys.exit(f"geste inconnu : {args.geste}")


def main():
    ap = argparse.ArgumentParser(description="Fausse manette MAN1.")
    ap.add_argument("geste", choices=("tour", "hasard", "maintenir",
                                      "marche-arret", "molettes", "silence"))
    ap.add_argument("boutons", nargs="*", help="pour « maintenir » : A, B, "
                                               "START…")
    ap.add_argument("--cible", default="192.168.6.1",
                    help="l'injecteur (défaut 192.168.6.1)")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--depuis", default="0.0.0.0",
                    help="adresse locale ; 127.0.0.2, 127.0.0.3… pour plusieurs "
                         "fausses manettes sur ce PC")
    ap.add_argument("--duree", type=float, default=0.0)
    ap.add_argument("--apres", type=float, default=0.0,
                    help="« maintenir », « silence » : se taire après tant "
                         "de secondes")
    ap.add_argument("--fois", type=int, default=3, help="« marche-arret »")
    ap.add_argument("--perte", type=float, default=0.0,
                    help="%% de paquets jamais émis")
    ap.add_argument("--piles", action="store_true",
                    help="les piles se vident de 2,8 à 2,0 V sur la durée")
    ap.add_argument("--graine", type=int, default=1)
    args = ap.parse_args()

    voulu, duree = scenario(args)
    session = random.getrandbits(32)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.depuis, 0))
    sock.settimeout(0.05)
    cible = (args.cible, args.port)
    rng = random.Random()

    verrou = threading.Lock()
    envois = {}                   # numéro → instant
    ar = []                       # allers-retours
    etat_accuse = {"dernier": None, "console": None, "numero": 0, "n": 0}
    fini = threading.Event()

    def ecouter():
        while not fini.is_set():
            try:
                data, _ = sock.recvfrom(64)
            except (socket.timeout, OSError):
                continue
            t = time.monotonic()
            if len(data) != TAILLE or data[:4] != MAGIC or data[4] != TYPE_ACCUSE:
                continue
            marche = data[8]
            s, n, _ = struct.unpack_from("<III", data, 12)
            if s != session:
                continue
            with verrou:
                etat_accuse.update(dernier=t, console=marche,
                                   numero=max(etat_accuse["numero"], n))
                etat_accuse["n"] += 1
                d = envois.pop(n, None)
                if d is not None:
                    ar.append(t - d)

    threading.Thread(target=ecouter, daemon=True).start()
    print(f"  manette de session {session:08x} vers {args.cible}:{args.port}, "
          f"geste « {args.geste} », {fr(duree, 0)} s", file=sys.stderr)

    t0 = time.monotonic()
    etat = "PRETE"
    envoye = None
    emis_t = -1.0
    repetitions = 0
    fin_t = premier_arret = 0
    numero = 0
    emis = perdus = 0
    t_affiche = 0.0
    precedent = None

    def emettre(e, t):
        nonlocal numero, emis, perdus, envoye, emis_t
        numero += 1
        p = paquet(e, session, numero, int(t * 1000))
        envoye, emis_t = dict(e), t
        if args.perte and rng.random() * 100 < args.perte:
            perdus += 1
            return
        with verrou:
            envois[numero] = time.monotonic()
            if len(envois) > 4000:
                for k in list(envois)[:1000]:
                    envois.pop(k, None)
        try:
            sock.sendto(p, cible)
            emis += 1
        except OSError:
            pass

    try:
        while True:
            t = time.monotonic() - t0
            if t >= duree:
                break
            e = voulu(t)
            if e is None:                 # la manette s'est tue d'un coup
                time.sleep(0.01)
                continue
            if etat == "PRETE":
                if e["marche"]:
                    etat, repetitions = "MARCHE", REPETITIONS
                    emettre(e, t)
            elif etat == "MARCHE":
                if not e["marche"]:
                    etat, fin_t, premier_arret = "FIN", t, numero + 1
                    repetitions = REPETITIONS
                    emettre(e, t)
                elif any(e[k] != envoye[k] for k in ("boutons", "volume",
                                                     "contraste")):
                    repetitions = REPETITIONS
                    emettre(e, t)
                elif repetitions and t - emis_t >= REPETITION_S:
                    repetitions -= 1
                    emettre(e, t)
                elif not repetitions and t - emis_t >= BATTEMENT_S:
                    emettre(e, t)
            elif etat == "FIN":
                with verrou:
                    accuse = etat_accuse["numero"] >= premier_arret
                if e["marche"]:
                    etat, repetitions = "MARCHE", REPETITIONS
                    emettre(e, t)
                elif accuse or t - fin_t >= ARRET_ABANDON_S:
                    etat = "PRETE"
                    print(f"  {fr(t, 1)} s  arrêt "
                          f"{'accusé' if accuse else 'jamais accusé : abandon'}"
                          f", silence", file=sys.stderr)
                elif repetitions and t - emis_t >= REPETITION_S:
                    repetitions -= 1
                    emettre(e, t)
                elif not repetitions and t - emis_t >= BATTEMENT_S:
                    emettre(e, t)

            if t - t_affiche >= 1.0 or (precedent is not None
                                         and precedent != (etat, e["boutons"])):
                t_affiche = t
                with verrou:
                    dern = etat_accuse["dernier"]
                    console = etat_accuse["console"]
                if not e["marche"]:
                    led = "éteinte"
                elif dern is not None and time.monotonic() - dern < ACCUSE_ROUGE_S:
                    led = "ROUGE"
                else:
                    led = "bleu pulsé"
                boutons = " ".join(n for n, b in BOUTONS.items()
                                   if e["boutons"] & b) or "—"
                print(f"  {fr(t, 1):>6} s  {etat:<6}  boutons {boutons:<24} "
                      f"volume {e['volume']:3}  LED {led:<10}  console "
                      f"{'—' if console is None else ('allumée' if console else 'éteinte')}",
                      file=sys.stderr)
            precedent = (etat, e["boutons"])
            time.sleep(0.0005)
    except KeyboardInterrupt:
        pass
    time.sleep(0.1)
    fini.set()

    with verrou:
        lats = sorted(ar)
        n_acc = etat_accuse["n"]
    print(f"\n  {emis} paquets émis, {perdus} perdus exprès, {n_acc} accusés "
          f"({fr(100 * n_acc / max(emis, 1), 1)} %)")
    if lats:
        print(f"  aller-retour : médiane {fr(lats[len(lats) // 2] * 1000)} ms, "
              f"99 % sous {fr(lats[min(len(lats) - 1, len(lats) * 99 // 100)] * 1000)} ms, "
              f"le pire {fr(lats[-1] * 1000)} ms")
    else:
        print("  aucun accusé : l'injecteur est-il lancé, et vise-t-on la bonne "
              "adresse ?")


if __name__ == "__main__":
    main()
