#!/usr/bin/env python3
"""Le banc de mesure du module son, vu du PC : il donne le verdict en clair.

Il parle au firmware `firmware/mesure`, qui envoie une ligne de mesure toutes
les 50 ms (commande « f »), et en tire ce qu'il faut savoir sans être
spécialiste du son.

    ./tools/mesure_son.py niveau                  # quelle résistance Rb monter ?
    ./tools/mesure_son.py niveau --duree 15 --sortie avant.json
    ./tools/mesure_son.py cotes                   # quelle voie est la gauche ?
    ./tools/mesure_son.py comparer avant.json apres.json

Le montage supposé est celui du banc de la phase 0, Rs = Rb = 10 kΩ
(docs/prise-son-mgb.md §3). S'il change, le dire par --rs et --rb : les
tensions au point de prise en dépendent.

Dépendance : pyserial.
"""
import argparse
import collections
import json
import math
import os
import select
import sys
import termios
import time
import tty

VREF = 3.3          # pleine échelle de l'ADC, en volts
PAS = 4096          # pas de l'ADC
FENETRE_S = 0.05    # une ligne de mesure toutes les 50 ms
CC_SON = 40         # crête-à-crête minimal, en pas, pour parler de son (≈ 32 mV au Pico)

# Le choix de Rb d'après le son le plus fort au point de prise :
# docs/prise-son-mgb.md §3, qui fait foi. (plafond en V, Rb, Caa)
PALIERS = [(2.5, "100 kΩ", "820 pF"),
           (3.8, "15 kΩ", "1,5 nF"),
           (4.6, "10 kΩ", "1,5 nF")]

FILS = {"A": "GP26, le fil de la broche 5 de VR1",
        "B": "GP27, le fil de la broche 4 de VR1"}


def fr(x, decimales=2):
    """Un nombre à la française : virgule décimale."""
    return f"{x:.{decimales}f}".replace(".", ",")


# ─────────────────────────────────────────────────────────────── les mesures

class Mesure:
    """Une voie sur une fenêtre de 50 ms, en pas d'ADC."""
    __slots__ = ("min", "max", "moyenne", "efficace", "saturations")

    def __init__(self, mn, mx, moy, eff, sat):
        self.min, self.max = int(mn), int(mx)
        self.moyenne, self.efficace = float(moy), float(eff)
        self.saturations = int(sat)

    @property
    def cc(self):
        return self.max - self.min


def lire_ligne(ligne):
    """« F n minA maxA moyA effA satA minB maxB moyB effB satB » → (n, A, B)."""
    morceaux = ligne.split()
    if len(morceaux) != 12 or morceaux[0] != "F":
        return None
    try:
        n = int(morceaux[1])
        return n, Mesure(*morceaux[2:7]), Mesure(*morceaux[7:12])
    except ValueError:
        return None


class Montage:
    def __init__(self, rs, rb):
        self.rs, self.rb = rs, rb
        self.gain = rb / (rs + rb)

    @staticmethod
    def au_pico(pas):
        return pas * VREF / PAS

    def au_point(self, pas):
        """Tension au point de prise sur la console, à travers le diviseur."""
        return self.au_pico(pas) / self.gain

    def __str__(self):
        return (f"Rs = {self.rs / 1000:g} kΩ, Rb = {self.rb / 1000:g} kΩ, "
                f"gain {fr(self.gain)}")


class Cumul:
    """Ce qu'une voie a montré sur toute la séance."""

    def __init__(self):
        self.moyennes = []
        self.efficaces = []
        self.cc_max = 0
        self.saturations = 0

    def ajouter(self, m):
        self.moyennes.append(m.moyenne)
        self.efficaces.append(m.efficace)
        self.cc_max = max(self.cc_max, m.cc)
        self.saturations += m.saturations

    def moyenne(self):
        return sum(self.moyennes) / len(self.moyennes) if self.moyennes else 0.0

    def bruit(self):
        """Le bruit de fond : la valeur efficace du moment le plus calme. Le 5ᵉ
        centile plutôt que le minimum, pour qu'une fenêtre isolée ne décide pas."""
        if not self.efficaces:
            return 0.0
        tri = sorted(self.efficaces)
        return tri[len(tri) // 20]


def verdict_polarisation(v):
    """(correcte ?, explication) pour la tension moyenne d'une entrée."""
    if v < 0.2:
        return False, "n'arrive pas : vérifier le pont de 100 kΩ et Rb"
    if v > 3.1:
        return False, "bloquée au 3,3 V : entrée en l'air, ou reliée au 3,3 V"
    if abs(v - 1.65) > 0.25:
        return False, "décalée, 1,65 V attendus : vérifier le pont de 100 kΩ"
    return True, "correcte"


def recommander(cc_point):
    """Le palier de prise-son-mgb.md §3 qui convient, en une phrase."""
    if cc_point < 0.5:
        return ("Moins de 0,5 V : le signal est faible. Avant de souder, voir "
                "docs/prise-son-mgb.md §5.")
    for plafond, rb, caa in PALIERS:
        if cc_point <= plafond:
            return f"Rb = {rb}, et Caa = {caa} (docs/prise-son-mgb.md §3)."
    return ("Plus de 4,6 V : il faut un Rb plus petit que 10 kΩ. Voir "
            "docs/prise-son-mgb.md §5.")


# ─────────────────────────────────────────────────────────── la liaison USB

class Liaison:
    """La console du firmware de mesure, flux de mesures en marche."""

    def __init__(self, port):
        try:
            import serial
        except ImportError:
            sys.exit("pyserial est requis :  pip install pyserial")
        try:
            self.s = serial.Serial(port, 115200, timeout=0.05)
        except Exception as e:
            sys.exit(f"{port} : {e}\nLe Pico est-il branché, avec firmware/mesure flashé ?")
        self.s.dtr = True            # sans DTR, le firmware n'écrit rien
        self.s.rts = True
        time.sleep(0.8)
        self.s.reset_input_buffer()
        self.reste = b""
        self.basculer_flux(True)

    def basculer_flux(self, voulu):
        """La commande « f » bascule : on la répète jusqu'à l'état voulu."""
        for _ in range(3):
            self.s.write(b"f")
            texte = ""
            fin = time.monotonic() + 2
            while time.monotonic() < fin:
                texte += self.s.read(self.s.in_waiting or 1).decode("utf-8", "replace")
                if "flux MARCHE" in texte or "flux ARRET" in texte:
                    marche = texte.rfind("flux MARCHE") > texte.rfind("flux ARRET")
                    if marche == voulu:
                        return
                    break
        if voulu:
            sys.exit("Le firmware ne répond pas à « f » : est-ce bien firmware/mesure ?")

    def fileno(self):
        return self.s.fileno()

    def lignes(self):
        """Les lignes complètes arrivées depuis le dernier appel."""
        self.reste += self.s.read(self.s.in_waiting or 1)
        *completes, self.reste = self.reste.split(b"\n")
        return [l.decode("utf-8", "replace") for l in completes]

    def fermer(self):
        try:
            self.basculer_flux(False)
        finally:
            self.s.close()


def fenetres_depuis(args):
    """Les fenêtres de mesure, du Pico ou d'un fichier rejoué (--fichier)."""
    if args.fichier:
        with open(args.fichier, encoding="utf-8") as f:
            for ligne in f:
                r = lire_ligne(ligne)
                if r:
                    yield r
        return
    liaison = Liaison(args.port)
    try:
        while True:
            for ligne in liaison.lignes():
                r = lire_ligne(ligne)
                if r:
                    yield r
    finally:
        liaison.fermer()


def barre(volts, plein, largeur=24):
    n = max(0, min(largeur, round(volts / plein * largeur)))
    return "█" * n + "·" * (largeur - n)


# ──────────────────────────────────────────────────────────────── niveau

def niveau(args):
    montage = Montage(args.rs, args.rb)
    cumul = {"A": Cumul(), "B": Cumul()}
    perdues, precedente, n_fen = 0, None, 0
    plein = VREF / montage.gain           # ce que le banc peut voir au point de prise

    if not args.fichier:
        print(f"Montage : {montage}. Le banc voit jusqu'à {fr(plein, 1)} V "
              "crête-à-crête au point de prise.")
        print("Allume la console, puis joue les passages les plus forts que tu "
              "connais.")
        print(f"Mesure pendant {args.duree} s ; Ctrl-C pour arrêter plus tôt.\n")
    debut = time.monotonic()
    affiche = 0.0
    source = fenetres_depuis(args)
    try:
        for n, a, b in source:
            if precedente is not None and n != precedente + 1:
                perdues += max(0, n - precedente - 1)
            precedente = n
            n_fen += 1
            cumul["A"].ajouter(a)
            cumul["B"].ajouter(b)
            maintenant = time.monotonic()
            if not args.fichier and maintenant - affiche > 0.2:
                affiche = maintenant
                va, vb = montage.au_point(a.cc), montage.au_point(b.cc)
                sys.stdout.write(
                    f"\r  A {barre(va, plein)} {fr(va)} V   "
                    f"B {barre(vb, plein)} {fr(vb)} V   "
                    f"max {fr(montage.au_point(max(cumul['A'].cc_max, cumul['B'].cc_max)))} V ")
                sys.stdout.flush()
            if not args.fichier and maintenant - debut >= args.duree:
                break
    except KeyboardInterrupt:
        pass
    finally:
        source.close()      # coupe le flux du firmware
    print("\n")

    if not n_fen:
        sys.exit("Aucune mesure reçue.")
    print(f"{n_fen} mesures de 50 ms, soit {fr(n_fen * FENETRE_S, 1)} s."
          + (f" {perdues} ligne{'s' if perdues > 1 else ''} perdue"
             f"{'s' if perdues > 1 else ''} en route (USB) : sans conséquence "
             "sur le verdict." if perdues else ""))

    resume = {}
    montage_ok = True
    for voie in ("A", "B"):
        c = cumul[voie]
        pol = montage.au_pico(c.moyenne())
        cc = montage.au_point(c.cc_max)
        bruit_mv = montage.au_point(c.bruit()) * 1000
        resume[voie] = {"polarisation_v": round(pol, 3),
                        "cc_max_point_v": round(cc, 3),
                        "bruit_point_mv": round(bruit_mv, 2),
                        "saturations": c.saturations}
        pol_ok, pol_texte = verdict_polarisation(pol)
        montage_ok &= pol_ok
        print(f"\nVoie {voie} ({FILS[voie]})")
        print(f"  polarisation        {fr(pol)} V, {pol_texte}"
              + ("" if pol_ok else "  ⚠️"))
        if c.cc_max < CC_SON:
            print("  son                 aucun : la console était-elle allumée, "
                  "ce fil branché ?")
        else:
            print(f"  son le plus fort    {fr(cc)} V crête-à-crête au point de prise")
        print(f"  bruit de fond       {fr(bruit_mv, 1)} mV, au moment le plus calme "
              "(bruit du banc compris)")
        if c.saturations and pol_ok:
            print(f"  ⚠️ SATURÉ          {c.saturations} échantillons au bout de la "
                  f"plage : le vrai maximum dépasse {fr(plein, 1)} V. Refaire avec "
                  "un Rb plus petit sur le banc.")

    pire = max(resume["A"]["cc_max_point_v"], resume["B"]["cc_max_point_v"])
    print("\nVerdict")
    if not montage_ok:
        print("  Le montage est à corriger d'abord : tant que la polarisation est "
              "fausse, les tensions mesurées ne veulent rien dire. La commande « g » "
              "de la console aide à le vérifier.")
    elif max(cumul["A"].cc_max, cumul["B"].cc_max) < CC_SON:
        print("  Aucun son mesuré : rien à conclure. Vérifier le montage avec la "
              "commande « g » de la console.")
    else:
        print(f"  Son le plus fort : {fr(pire)} V crête-à-crête.")
        print(f"  → {recommander(pire)}")
        print("  Ce chiffre ne vaut que pour ce qui a été joué : s'il existe des "
              "passages plus forts, refaire la mesure dessus.")

    if args.sortie:
        os.makedirs(os.path.dirname(args.sortie) or ".", exist_ok=True)
        with open(args.sortie, "w", encoding="utf-8") as f:
            json.dump({"date": time.strftime("%Y-%m-%d %H:%M:%S"),
                       "montage": {"rs": args.rs, "rb": args.rb},
                       "duree_s": round(n_fen * FENETRE_S, 2),
                       "voies": resume}, f, ensure_ascii=False, indent=2)
        print(f"\nRésultat enregistré dans {args.sortie}")


# ───────────────────────────────────────────────────────────── comparer

def comparer(args):
    with open(args.avant, encoding="utf-8") as f:
        avant = json.load(f)
    with open(args.apres, encoding="utf-8") as f:
        apres = json.load(f)
    ok = True
    for voie in ("A", "B"):
        a, b = avant["voies"][voie], apres["voies"][voie]
        if a["cc_max_point_v"] <= 0 or b["cc_max_point_v"] <= 0:
            print(f"Voie {voie} : pas de son dans l'une des deux mesures, rien à comparer.")
            ok = False
            continue
        ecart = 20 * math.log10(b["cc_max_point_v"] / a["cc_max_point_v"])
        print(f"Voie {voie} : son le plus fort {fr(a['cc_max_point_v'])} V → "
              f"{fr(b['cc_max_point_v'])} V, écart {fr(ecart, 1)} dB ; "
              f"bruit de fond {fr(a['bruit_point_mv'], 1)} → "
              f"{fr(b['bruit_point_mv'], 1)} mV")
        if abs(ecart) >= 0.5:
            ok = False
    if avant["montage"] != apres["montage"]:
        print("⚠️ Les deux mesures n'ont pas été faites avec le même montage : "
              "les tensions au point de prise restent comparables, le bruit non.")
    print("\nVerdict : " + ("moins de 0,5 dB d'écart, la console n'a rien senti."
                            if ok else "écart de 0,5 dB ou plus : à regarder de près."))
    print("(Un dB, c'est un écart de niveau à peine audible ; 0,5 dB ne s'entend pas.)")


# ─────────────────────────────────────────────────────────────── côtés

def voie_dominante(fenetres, rapport=3.0):
    """Sur ces fenêtres, une voie a-t-elle sonné nettement plus fort que
    l'autre ? Rend « A », « B » ou None."""
    if not fenetres:
        return None
    ea = sum(a.efficace ** 2 for a, _ in fenetres)
    eb = sum(b.efficace ** 2 for _, b in fenetres)
    cc_a = max(a.cc for a, _ in fenetres)
    cc_b = max(b.cc for _, b in fenetres)
    if cc_a >= CC_SON and ea >= (rapport ** 2) * max(eb, 1e-9):
        return "A"
    if cc_b >= CC_SON and eb >= (rapport ** 2) * max(ea, 1e-9):
        return "B"
    return None


def conclure(reponses):
    """reponses : liste de (voie dominante, oreille). Rend (conclusion, détail)."""
    a_gauche = sum(1 for v, o in reponses if (v, o) in (("A", "g"), ("B", "d")))
    a_droite = len(reponses) - a_gauche
    if a_gauche >= 3 and a_droite == 0:
        return "A", a_gauche, a_droite
    if a_droite >= 3 and a_gauche == 0:
        return "B", a_gauche, a_droite
    return None, a_gauche, a_droite


def cotes(args):
    if not sys.stdin.isatty():
        sys.exit("Ce mode se joue au clavier : lance-le dans un terminal.")
    montage = Montage(args.rs, args.rb)
    plein = VREF / montage.gain
    print("""Gauche ou droite ?

Branche des écouteurs ou un casque sur la console, pour écouter seulement.
Joue. Quand tu entends un son d'UN SEUL côté, appuie aussitôt sur :
    g  si c'était dans l'oreille gauche
    d  si c'était dans l'oreille droite
L'outil regarde quelle voie a sonné dans la seconde et demie précédente.
Il signale aussi de lui-même les sons qui ne passent que sur une voie : à ce
moment-là, demande-toi de quel côté tu l'as entendu.
Trois réponses concordantes suffisent. q pour quitter.
""")
    historique = collections.deque(maxlen=30)    # 1,5 s
    reponses = []
    suite = {"A": 0, "B": 0}
    dernier_signal = 0.0
    affiche = 0.0
    liaison = Liaison(args.port)
    entree = sys.stdin.fileno()
    reglages = termios.tcgetattr(entree)
    try:
        tty.setcbreak(entree)
        while True:
            pret, _, _ = select.select([liaison, entree], [], [], 0.1)
            if liaison in pret:
                for ligne in liaison.lignes():
                    r = lire_ligne(ligne)
                    if not r:
                        continue
                    _, a, b = r
                    historique.append((a, b))
                    for v, m, autre in (("A", a, b), ("B", b, a)):
                        seule = m.cc >= CC_SON and m.efficace >= 4 * max(autre.efficace, 0.5)
                        suite[v] = suite[v] + 1 if seule else 0
                        if suite[v] == 2 and time.monotonic() - dernier_signal > 1:
                            dernier_signal = time.monotonic()
                            sys.stdout.write(f"\r\a  ▶ son sur la voie {v} seulement — "
                                             "de quel côté l'as-tu entendu ? (g / d)"
                                             + " " * 20 + "\n")
                    maintenant = time.monotonic()
                    if maintenant - affiche > 0.2:
                        affiche = maintenant
                        va, vb = montage.au_point(a.cc), montage.au_point(b.cc)
                        sys.stdout.write(f"\r  A {barre(va, plein, 16)}   "
                                         f"B {barre(vb, plein, 16)}   "
                                         f"réponses : {len(reponses)} ")
                        sys.stdout.flush()
            if entree in pret:
                touche = os.read(entree, 1).decode("utf-8", "replace").lower()
                if touche == "q":
                    break
                if touche not in ("g", "d"):
                    continue
                v = voie_dominante(list(historique))
                cote = "gauche" if touche == "g" else "droite"
                if v is None:
                    print(f"\r  ({cote}) je n'ai vu aucune voie dominer dans la "
                          "dernière seconde et demie : réponse ignorée." + " " * 10)
                    continue
                reponses.append((v, touche))
                print(f"\r  noté : la voie {v} sonnait seule, entendue à {cote}."
                      + " " * 20)
                gauche, n_g, n_d = conclure(reponses)
                if gauche:
                    break
                if n_g and n_d:
                    print(f"  ⚠️ réponses contradictoires ({n_g} contre {n_d}) : "
                          "continue, ou vérifie que tu écoutes bien la console.")
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(entree, termios.TCSADRAIN, reglages)
        liaison.fermer()

    gauche, n_g, n_d = conclure(reponses)
    print("\n")
    if gauche is None:
        print(f"Pas de conclusion : {len(reponses)} réponse(s) utile(s), "
              f"{n_g} pour « A = gauche », {n_d} pour « A = droite ».")
        return
    droite = "B" if gauche == "A" else "A"
    print(f"Conclusion : la voie {gauche} est la GAUCHE, la voie {droite} la droite "
          f"({max(n_g, n_d)} réponses concordantes).")
    if gauche == "A":
        print("C'est ce que dit le schéma : rien à changer.")
    else:
        print("C'est l'inverse du schéma. Rien à ressouder : dans le firmware, "
              "échanger PIN_VOIE_A et PIN_VOIE_B, et le noter dans la fiche.")


# ──────────────────────────────────────────────────────────────── entrée

def main():
    ap = argparse.ArgumentParser(description="Le banc de mesure du module son, vu du PC.")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--rs", type=float, default=10e3,
                    help="résistance série côté console, en ohms (défaut : 10 000)")
    ap.add_argument("--rb", type=float, default=10e3,
                    help="résistance de polarisation Rb, en ohms (défaut : 10 000)")
    sous = ap.add_subparsers(dest="mode", required=True)

    n = sous.add_parser("niveau", help="le son le plus fort, et la résistance Rb à monter")
    n.add_argument("--duree", type=float, default=60, help="en secondes (défaut : 60)")
    n.add_argument("--sortie", help="enregistrer le résultat (JSON), pour « comparer »")
    n.add_argument("--fichier", help=argparse.SUPPRESS)   # rejouer des lignes F enregistrées

    sous.add_parser("cotes", help="quelle voie est la gauche, avec des écouteurs sur la console")

    c = sous.add_parser("comparer", help="deux mesures « niveau --sortie », avant et après")
    c.add_argument("avant")
    c.add_argument("apres")

    args = ap.parse_args()
    {"niveau": niveau, "cotes": cotes, "comparer": comparer}[args.mode](args)


if __name__ == "__main__":
    main()
