#!/usr/bin/env python3
"""Écoute des manettes — tient le rôle de l'injecteur, sur le PC.

    ./tools/manette_ecoute.py                        # jusqu'à 4 manettes, en direct
    ./tools/manette_ecoute.py --extinction 30        # extinction après 30 s d'absence (T3.5)
    ./tools/manette_ecoute.py --muet                 # n'accuse rien : console injoignable
    ./tools/manette_ecoute.py --journal nuit.csv     # chaque paquet, pour T2.5

Il reçoit `MAN1`, accuse chaque état comme l'injecteur, et applique ses
règles (plan §4.3, §4.4) : boutons relâchés après 200 ms sans nouvelles,
message « aucune manette » après 2 s, console éteinte après l'absence, une
seconde d'arrêt au moins. Il affiche l'état reçu en direct, et dit à la fin,
en clair :

- les **pertes** (des trous dans la numérotation) et le **plus long silence**
  d'une manette en marche, contre le seuil de sécurité de 200 ms (T2.3) ;
- les **relâches de sécurité** : un bouton enfoncé, puis plus de nouvelles ;
- **rien sans marche** : sur arrêt, la manette ne doit plus rien envoyer,
  passé l'arrêt et son accusé (T2.6) ;
- le **temps de connexion** d'une manette réveillée par son interrupteur,
  que porte son premier paquet (T2.7) ;
- avec `--journal`, la tension des piles au fil du temps (T2.5).

Jusqu'à 4 manettes, reconnues à leur adresse IP, comme le fera l'injecteur. Les règles à plusieurs sont
celles de firmware/commun/fusion.hpp : un OU des boutons, provisoire.

Le format fait foi dans firmware/commun/man1.h.
"""
import argparse
import csv
import socket
import struct
import sys
import time

# firmware/commun/man1.h
PORT = 4244
MAGIC = b"MAN1"
TAILLE = 24
TYPE_ETAT, TYPE_ACCUSE = 0, 1
RELACHE_S, MESSAGE_S, ARRET_MIN_S = 0.2, 2.0, 1.0
MANETTES_MAX = 4
BOUTONS = {"→": 1, "←": 2, "↑": 4, "↓": 8, "A": 16, "B": 32,
           "Sel": 64, "Sta": 128}
CRITERE_PERTES = 1.0        # %, phase 2


def fr(x, d=2):
    return f"{x:.{d}f}".replace(".", ",")


def boutons_texte(b):
    return " ".join(n if b & v else "·" * len(n) for n, v in BOUTONS.items())


class Manette:
    def __init__(self, numero, adresse):
        self.numero, self.adresse = numero, adresse
        self.session = None
        self.dernier = 0
        self.vu = None
        self.marche = False
        self.boutons = self.volume = self.contraste = 0
        self.piles = 0
        self.recus = self.perdus = self.anciens = 0
        self.sessions = 0
        self.trou_max = 0.0
        self.relaches = 0
        self.fuites = 0             # sur arrêt, des boutons ou des molettes
        self.apres_arret = 0        # des arrêts encore répétés après 2 s
        self.debut_arret = None     # le premier « arrêt » de la bascule
        self.connexions = []        # temps de connexion, réveil par l'interrupteur
        self.fraiche = False


class Injecteur:
    def __init__(self, args):
        self.args = args
        self.manettes = {}
        self.console = False
        self.jamais_eteinte = True
        self.eteinte = 0.0
        self.extinctions = 0
        self.message = False
        self.voulait = {}

    def recevoir(self, data, adresse, t):
        if len(data) != TAILLE or data[:4] != MAGIC or data[4] != TYPE_ETAT:
            return None
        (_, boutons, volume, contraste, marche, _, piles, session, numero,
         temps) = struct.unpack_from("<BBBBBBHIII", data, 4)
        # Comme l'injecteur : une manette par adresse IP (.2 à .5 sur le
        # réseau des manettes), quel que soit son port.
        m = self.manettes.get(adresse[0])
        if m is None:
            if len(self.manettes) >= MANETTES_MAX:
                return None
            m = self.manettes[adresse[0]] = Manette(len(self.manettes) + 1, adresse)
            print(f"  manette {m.numero} : {adresse[0]}", file=sys.stderr)
        m.recus += 1
        appliquer = True
        nouvelle = m.session != session
        if nouvelle:
            if m.session is not None:
                print(f"  manette {m.numero} : redémarrée (session {session:08x})",
                      file=sys.stderr)
            m.session, m.sessions = session, m.sessions + 1
            m.debut_arret = None
            if marche and temps < 10000:
                m.connexions.append(temps / 1000)
        else:
            saut = (numero - m.dernier) & 0xFFFFFFFF
            if saut == 0 or saut >= 1 << 31:
                m.anciens += 1
                appliquer = False
            elif saut > 1:
                m.perdus += saut - 1
        if not marche and (boutons or volume or contraste):
            m.fuites += 1
        # La manette répète « arrêt » jusqu'à l'accusé, et renonce à 2 s.
        if not marche and m.debut_arret is not None and t - m.debut_arret > 2.3:
            m.apres_arret += 1
        if appliquer:
            # Le silence du réseau, dans une même session : un redémarrage
            # de la manette n'est pas un trou du réseau.
            if m.vu is not None and m.marche and not nouvelle:
                m.trou_max = max(m.trou_max, t - m.vu)
            m.dernier, m.vu = numero, t
            if marche:
                m.debut_arret = None
            elif m.debut_arret is None:
                m.debut_arret = t
            m.marche = bool(marche)
            m.boutons = boutons if marche else 0
            m.volume, m.contraste, m.piles = volume, contraste, piles
            m.fraiche = True
        self.avancer(t)
        if self.args.journal:
            self.args.journal.writerow([f"{t:.4f}", m.numero, f"{session:08x}",
                                        numero, marche, boutons, volume,
                                        contraste, piles, temps])
        return MAGIC + struct.pack("<BBBBBBHIII", TYPE_ACCUSE, 0, 0, 0,
                                   1 if self.console else 0, 0, 0, session,
                                   numero, int(t * 1000) & 0xFFFFFFFF)

    def avancer(self, t):
        voulue, absence = False, None
        for m in self.manettes.values():
            if m.vu is None:
                continue
            depuis = t - m.vu
            fraiche = depuis < RELACHE_S
            if m.fraiche and not fraiche:
                m.fraiche = False
                if m.marche and m.boutons:
                    m.relaches += 1
                    print(f"  manette {m.numero} : RELÂCHE DE SÉCURITÉ, plus de "
                          f"nouvelles depuis 200 ms", file=sys.stderr)
            veut = m.marche and depuis < self.args.extinction
            if self.voulait.get(m.numero) and m.marche and not veut:
                self.extinctions += 1
            self.voulait[m.numero] = veut
            if veut:
                voulue = True
                absence = depuis if absence is None else min(absence, depuis)
        if voulue and not self.console:
            if self.jamais_eteinte or t - self.eteinte >= ARRET_MIN_S:
                self.console = True
        elif not voulue and self.console:
            self.console, self.eteinte, self.jamais_eteinte = False, t, False
        self.message = bool(self.console and absence is not None
                            and absence >= MESSAGE_S)
        b = 0
        if self.console:
            for m in self.manettes.values():
                if m.vu is not None and m.marche and t - m.vu < RELACHE_S:
                    b |= m.boutons
        return b, absence


def main():
    ap = argparse.ArgumentParser(description="Écoute des manettes MAN1.")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--extinction", type=float, default=15 * 60,
                    help="secondes d'absence avant d'éteindre (défaut 900)")
    ap.add_argument("--muet", action="store_true",
                    help="n'accuser aucun paquet")
    ap.add_argument("--journal", help="CSV de chaque paquet reçu")
    ap.add_argument("--duree", type=float, default=0.0)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("0.0.0.0", args.port))
    except OSError as e:
        sys.exit(f"port {args.port} indisponible : {e}")
    sock.settimeout(0.01)
    fichier = None
    if args.journal:
        fichier = open(args.journal, "w", newline="")
        args.journal = csv.writer(fichier)
        args.journal.writerow(["heure", "manette", "session", "numero", "marche",
                               "boutons", "volume", "contraste", "piles_mv",
                               "temps_ms"])
    inj = Injecteur(args)
    print(f"  écoute sur le port {args.port}"
          f"{', sans accuser' if args.muet else ''} ; extinction après "
          f"{fr(args.extinction, 0)} s d'absence", file=sys.stderr)

    t0 = time.monotonic()
    affiche = None
    t_stat = 0.0
    try:
        while not args.duree or time.monotonic() - t0 < args.duree:
            try:
                data, de = sock.recvfrom(64)
            except socket.timeout:
                data = None
            t = time.monotonic() - t0
            if data is not None:
                accuse = inj.recevoir(data, de, t)
                if accuse is not None and not args.muet:
                    try:
                        sock.sendto(accuse, de)
                    except OSError:
                        pass
            b, absence = inj.avancer(t)
            vue = (b, inj.console, inj.message,
                   tuple((m.marche, m.volume, m.contraste)
                         for m in inj.manettes.values()))
            if vue != affiche or t - t_stat >= 5.0:
                if t - t_stat >= 5.0:
                    t_stat = t
                affiche = vue
                ligne = (f"  {fr(t, 2):>8} s  console "
                         f"{'ALLUMÉE' if inj.console else 'éteinte'}  "
                         f"photoMOS {boutons_texte(b)}")
                for m in inj.manettes.values():
                    ligne += (f"  | m{m.numero} {'marche' if m.marche else 'arrêt'}"
                              f" vol {m.volume:3} con {m.contraste:3}"
                              f" {fr(m.piles / 1000, 2)} V")
                if inj.message:
                    reste = args.extinction - (absence or 0)
                    ligne += (f"  « AUCUNE MANETTE » depuis {fr(absence, 0)} s, "
                              f"extinction dans {fr(reste, 0)} s")
                print(ligne, file=sys.stderr)
    except KeyboardInterrupt:
        pass
    if fichier:
        fichier.close()
    sys.exit(verdict(inj, t))


def verdict(inj, duree):
    print()
    if not inj.manettes:
        print("❌ Aucune manette entendue. La manette vise-t-elle ce PC, sur le "
              "port 4244 ?")
        return 1
    ok = True
    for m in inj.manettes.values():
        attendus = m.recus - m.anciens + m.perdus
        pertes = 100 * m.perdus / max(attendus, 1)
        print(f"Manette {m.numero} ({m.adresse[0]}) : {m.recus} paquets, "
              f"{m.sessions} session(s)")
        signe = "✅" if pertes < CRITERE_PERTES else "❌"
        ok &= pertes < CRITERE_PERTES
        print(f"{signe} Pertes : {m.perdus} sur {attendus}, {fr(pertes, 3)} % "
              f"(critère : moins de 1 %).")
        if m.anciens:
            print(f"   {m.anciens} paquets arrivés après un plus récent, "
                  f"ignorés.")
        trou = m.trou_max * 1000
        signe = "✅" if trou < 100 else ("⚠️ " if trou < 200 else "❌")
        ok &= trou < 200
        print(f"{signe} Plus long silence en marche : {fr(trou, 0)} ms. Le seuil "
              f"de sécurité est de 200 ms ; le critère, rester sous sa moitié.")
        if m.relaches:
            print(f"⚠️  {m.relaches} relâche(s) de sécurité : un bouton enfoncé, "
                  f"puis plus rien pendant 200 ms.")
        if m.fuites or m.apres_arret:
            ok = False
            print(f"❌ Rien sans marche : {m.fuites} paquets d'arrêt portaient "
                  f"des boutons ou des molettes, {m.apres_arret} arrêts répétés "
                  f"plus de 2 s (T2.6).")
        elif not m.marche:
            print("✅ Rien sans marche : sur arrêt, seul l'ordre « arrêt » est "
                  "arrivé, ni boutons ni molettes, et pas au-delà de 2 s.")
        if m.connexions:
            pire = max(m.connexions)
            signe = "✅" if pire <= 3.0 else "⚠️ "
            print(f"{signe} Réveil par l'interrupteur : connectée en "
                  + ", ".join(fr(c, 1) for c in m.connexions)
                  + " s (proposé : 3 s au plus, T2.7).")
        if m.piles:
            print(f"   Piles au dernier paquet : {fr(m.piles / 1000, 2)} V, "
                  f"après {fr(duree / 3600, 2)} h d'écoute.")
    if inj.extinctions:
        print(f"   Console éteinte {inj.extinctions} fois faute de manette.")
    return 0 if ok else 1


if __name__ == "__main__":
    main()
