#!/usr/bin/env python3
"""Haut-parleur virtuel — le récepteur `SON1` de référence, sur le PC.

    ./tools/hautparleur_virtuel.py                       # le PC sur le réseau gb-son
    ./tools/hautparleur_virtuel.py --source 127.0.0.1    # contre son_envoi.py, sur ce PC
    ./tools/hautparleur_virtuel.py --wav ecoute.wav --muet --duree 60
    ./tools/hautparleur_virtuel.py --taire 10,3          # T4.1 : se taire 3 s à t = 10 s

Il fait ce que fera un haut-parleur : il se signale à la source par un
battement deux fois par seconde, reçoit les blocs, les range dans sa réserve
d'après leur numéro, les joue avec le retard de la réserve, et accuse quelques
blocs par seconde pour que la source mesure l'aller-retour.

Et il rend compte, en clair :

- les **pertes** : des blocs jamais arrivés, à comparer au critère de T4.2 ;
- les **retards** : combien chaque bloc a attendu de plus que le plus rapide.
  C'est d'eux que se déduit la **réserve conseillée** : assez pour que même
  le pire bloc sur mille arrive à l'heure (T4.2) ;
- les **coupures** : les blocs absents à l'heure de les jouer, avec la
  réserve choisie ;
- la **dérive** : de combien l'horloge de la source avance ou retarde sur
  celle du PC, en millionièmes (T5.2) ;
- la **veille** : la console est éteinte, l'amplificateur serait coupé.

Il joue le son sur la carte son du PC avec `sounddevice` s'il est installé,
sinon par `pw-play` ou `aplay`. `--wav` enregistre ce qu'il joue : un bloc
perdu y est un silence, comme à l'écoute.

Le format fait foi dans firmware/commun/son1.h. Dépendance : numpy.
"""
import argparse
import math
import queue
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import wave

import numpy as np

# firmware/commun/son1.h
PORT = 4243
MAGIC = b"SON1"
ENTETE = 16
TYPE_BLOC, TYPE_ACCUSE, TYPE_BATTEMENT, TYPE_VEILLE = 0, 1, 2, 3
FMT_PCM16 = 0
FREQUENCE = 48000
BLOC = 240
BATTEMENT_S = 0.5
BLOC_S = BLOC / FREQUENCE
CRITERE_PERTES = 0.1        # %, T4.2


def fr(x, decimales=2):
    return f"{x:.{decimales}f}".replace(".", ",")


def paquet(type_, echantillon=0):
    return MAGIC + struct.pack("<BBBBII", type_, 0, 0, 0, 0,
                               echantillon & 0xFFFFFFFF)


# ─────────────────────────────────────────────────────────────── la lecture

class Lecteur:
    """La carte son du PC, ou rien. Un fil à part écrit, pour que la carte
    son ne bloque jamais la réception."""

    def __init__(self, choix):
        self.file = queue.Queue(maxsize=40)
        self.flux = self.proc = None
        self.nom = "aucun"
        if choix in ("auto", "sounddevice"):
            try:
                import sounddevice
                self.flux = sounddevice.RawOutputStream(
                    samplerate=FREQUENCE, channels=2, dtype="int16")
                self.flux.start()
                self.nom = "sounddevice"
            except Exception:
                if choix == "sounddevice":
                    sys.exit("sounddevice indisponible")
        if self.flux is None and choix != "aucun":
            commandes = {
                "pw-play": ["pw-play", "--format", "s16", "--rate",
                            str(FREQUENCE), "--channels", "2", "-"],
                "aplay": ["aplay", "-q", "-t", "raw", "-f", "S16_LE", "-c",
                          "2", "-r", str(FREQUENCE), "-"],
            }
            for nom in (("pw-play", "aplay") if choix == "auto" else (choix,)):
                if nom in commandes and shutil.which(nom):
                    try:
                        self.proc = subprocess.Popen(
                            commandes[nom], stdin=subprocess.PIPE,
                            stderr=subprocess.DEVNULL)
                        self.nom = nom
                        break
                    except OSError:
                        pass
        if self.nom != "aucun":
            threading.Thread(target=self._ecrire, daemon=True).start()

    def _ecrire(self):
        while True:
            octets = self.file.get()
            if octets is None:
                return
            try:
                if self.flux is not None:
                    self.flux.write(octets)
                else:
                    self.proc.stdin.write(octets)
            except Exception:
                print("  la carte son a refusé : lecture abandonnée",
                      file=sys.stderr)
                self.nom = "aucun"
                return

    def jouer(self, octets):
        if self.nom == "aucun":
            return
        try:
            self.file.put_nowait(octets)
        except queue.Full:
            pass                        # la carte son traîne : on ne bloque pas

    def fermer(self):
        if self.nom != "aucun":
            self.file.put(None)
        if self.proc is not None:
            try:
                self.proc.stdin.close()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()


# ─────────────────────────────────────────────────────────── le haut-parleur

class HautParleur:
    def __init__(self, sock, args):
        self.sock, self.args = sock, args
        self.source = (args.source, args.port)
        self.verrou = threading.Lock()
        self.fini = threading.Event()
        # La réception
        self.arrivees = []              # (instant, numéro déroulé)
        self.vus = set()
        self.reserve = {}               # numéro déroulé → octets du bloc
        self.ref = None                 # dernier numéro brut, pour dérouler
        self.ref_deroule = 0
        self.max_vu = None
        self.recus = self.doublons = self.desordre = self.malformes = 0
        self.volume = 255
        self.veilles = 0
        self.derniere_veille = self.dernier_bloc = None
        # La lecture
        self.debut = None               # (numéro déroulé, instant de départ)
        self.joues = self.coupures = 0
        self.trop_tard = 0

    def derouler(self, n):
        """Numéro roulant sur 32 bits → entier qui ne fait pas le tour."""
        if self.ref is None:
            self.ref, self.ref_deroule = n, n
            return n
        ecart = (n - self.ref) & 0xFFFFFFFF
        if ecart >= 1 << 31:
            ecart -= 1 << 32
        d = self.ref_deroule + ecart
        if ecart > 0:
            self.ref, self.ref_deroule = n, d
        return d

    def recevoir(self):
        accuser_tous = max(1, round(1 / BLOC_S / max(self.args.accuses, 0.1)))
        while not self.fini.is_set():
            try:
                data, de = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                continue
            arrivee = time.monotonic()
            if len(data) < ENTETE or data[:4] != MAGIC:
                self.malformes += 1
                continue
            type_, fmt, voies, volume, freq, n = struct.unpack_from(
                "<BBBBII", data, 4)
            if type_ == TYPE_VEILLE:
                with self.verrou:
                    self.veilles += 1
                    self.derniere_veille = arrivee
                    self.volume = volume
                continue
            if type_ != TYPE_BLOC:
                continue
            charge = len(data) - ENTETE
            if (fmt != FMT_PCM16 or voies != 2 or freq != FREQUENCE
                    or charge != BLOC * 4):
                # Ce récepteur de référence ne lit que le format de la
                # source ; un autre format serait une erreur de l'émetteur.
                self.malformes += 1
                continue
            with self.verrou:
                d = self.derouler(n)
                self.recus += 1
                self.volume = volume
                self.dernier_bloc = arrivee
                if d in self.vus:
                    self.doublons += 1
                    continue
                self.vus.add(d)
                if self.max_vu is not None and d < self.max_vu:
                    self.desordre += 1
                self.max_vu = d if self.max_vu is None else max(self.max_vu, d)
                self.arrivees.append((arrivee, d))
                if self.debut is None:
                    self.debut = (d, arrivee + self.args.reserve / 1000)
                elif d < self.lecture_actuelle():
                    self.trop_tard += 1
                    continue
                self.reserve[d] = data[ENTETE:]
                compte = len(self.vus)
            if compte % accuser_tous == 0:
                try:
                    self.sock.sendto(paquet(TYPE_ACCUSE, n), de)
                except OSError:
                    pass

    def lecture_actuelle(self):
        """Le numéro du prochain bloc à jouer (appelé sous le verrou)."""
        return self.debut[0] + self.joues * BLOC

    def battre(self):
        t0 = time.monotonic()
        taire = self.args.taire
        while not self.fini.is_set():
            t = time.monotonic() - t0
            muet = taire and taire[0] <= t < taire[0] + taire[1]
            if not muet:
                try:
                    self.sock.sendto(paquet(TYPE_BATTEMENT), self.source)
                except OSError:
                    pass
            self.fini.wait(BATTEMENT_S)

    def jouer(self, lecteur, wav):
        """Joue la réserve au rythme du PC : le bloc j part à l'instant de
        départ + j × 5 ms. Absent à son heure, c'est une coupure : un
        silence. (La dérive entre la carte son et la source n'est pas
        rattrapée ici : c'est le travail du vrai haut-parleur, éprouvé par
        firmware/commun/test/test_rattrapage.cpp.)"""
        silence = bytes(BLOC * 4)
        while not self.fini.is_set():
            with self.verrou:
                if self.debut is None:
                    attente = 0.005
                else:
                    heure = self.debut[1] + self.joues * BLOC_S
                    attente = heure - time.monotonic()
            if attente > 0:
                self.fini.wait(min(attente, 0.005))
                continue
            with self.verrou:
                d = self.lecture_actuelle()
                octets = self.reserve.pop(d, None)
                self.joues += 1
                if octets is None:
                    self.coupures += 1
                volume = self.volume
                en_veille = (self.derniere_veille is not None and
                             (self.dernier_bloc is None or
                              self.derniere_veille > self.dernier_bloc))
                # La réserve ne garde rien de plus vieux que la lecture.
                for k in [k for k in self.reserve if k < d]:
                    del self.reserve[k]
            if octets is None or en_veille:
                octets = silence
            elif volume != 255:
                s = np.frombuffer(octets, dtype="<i2").astype(np.int32)
                octets = (s * volume // 255).astype("<i2").tobytes()
            lecteur.jouer(octets)
            if wav is not None:
                wav.writeframes(octets)

    # ───────────────────────────────────────────────────────── le compte rendu

    def analyse(self):
        """Pertes, retards, dérive. Les retards se mesurent sans horloge
        commune : chaque bloc est comparé au plus rapide de sa période, et
        la pente de ces plus rapides donne la dérive."""
        with self.verrou:
            arr = list(self.arrivees)
            recus, doublons = self.recus, self.doublons
            desordre, coupures = self.desordre, self.coupures
            joues, trop_tard = self.joues, self.trop_tard
            uniques = len(self.vus)
            lo, hi = (min(self.vus), max(self.vus)) if self.vus else (0, -1)
        r = {"recus": recus, "uniques": uniques, "doublons": doublons,
             "desordre": desordre, "coupures": coupures, "joues": joues,
             "trop_tard": trop_tard}
        attendus = (hi - lo) // BLOC + 1 if uniques else 0
        r["attendus"] = attendus
        r["perdus"] = max(0, attendus - uniques)
        if len(arr) < 2:
            return r
        y = np.array([a for a, _ in arr])
        x = np.array([d for _, d in arr], dtype=np.float64) / FREQUENCE
        r["trou_max"] = float(np.max(np.diff(np.sort(y))))
        r["duree"] = float(y[-1] - y[0])
        ecart = y - x
        # L'enveloppe basse : le bloc le plus rapide de chaque tranche de 2 s.
        tranches = np.floor((x - x.min()) / 2.0).astype(np.int64)
        xs, ms = [], []
        for t in np.unique(tranches):
            sel = tranches == t
            i = np.argmin(ecart[sel])
            xs.append(x[sel][i])
            ms.append(ecart[sel][i])
        if len(xs) >= 5 and r["duree"] >= 20:
            pente, origine = np.polyfit(xs, ms, 1)
            r["derive_ppm"] = -pente * 1e6
        else:
            pente, origine = 0.0, float(np.min(ecart))
        retard = ecart - (origine + pente * x)
        retard -= retard.min()
        r["retards"] = np.sort(retard)
        return r


def quantile(trie, q):
    return float(trie[min(len(trie) - 1, int(math.ceil(q * len(trie))) - 1)])


def ligne_etat(hp):
    r = hp.analyse()
    if not r["uniques"]:
        return "  en attente du premier bloc…"
    pertes = 100 * r["perdus"] / max(r["attendus"], 1)
    texte = (f"  reçus {r['uniques']}   perdus {r['perdus']} ({fr(pertes)} %)   "
             f"désordre {r['desordre']}   coupures {r['coupures']}")
    if "retards" in r:
        texte += (f"   retard p99 {fr(quantile(r['retards'], 0.99) * 1000, 1)} ms"
                  f"   trou max {fr(r['trou_max'] * 1000, 0)} ms")
    with hp.verrou:
        if hp.derniere_veille and (not hp.dernier_bloc
                                   or hp.derniere_veille > hp.dernier_bloc):
            texte += "   EN VEILLE"
        texte += f"   volume {hp.volume}"
    return texte


def verdict(hp, args):
    r = hp.analyse()
    print()
    if not r["uniques"]:
        if hp.veilles:
            print(f"✅ Veille reçue {hp.veilles} fois, aucun bloc : la source dit "
                  f"que la console est éteinte. Un vrai haut-parleur couperait "
                  f"son amplificateur.")
            return 0
        print("❌ Aucun bloc reçu.")
        print(f"   La source tourne-t-elle, et ce PC vise-t-il la bonne adresse "
              f"({args.source}:{args.port}) ?")
        return 1
    ok = True
    pertes = 100 * r["perdus"] / max(r["attendus"], 1)
    signe = "✅" if pertes < CRITERE_PERTES else "❌"
    ok &= pertes < CRITERE_PERTES
    print(f"{signe} Pertes : {r['perdus']} blocs sur {r['attendus']}, "
          f"{fr(pertes, 3)} % (critère T4.2 : moins de "
          f"{fr(CRITERE_PERTES, 1)} %).")
    if r["doublons"] or r["desordre"]:
        print(f"   {r['doublons']} doublons, {r['desordre']} arrivés dans le "
              f"désordre : la réserve les a remis en place.")
    if "retards" in r:
        ret = r["retards"] * 1000
        p999 = quantile(ret, 0.999)
        conseil = 5 * math.ceil((p999 + BLOC_S * 1000) / 5)
        print(f"   Retards, par rapport au bloc le plus rapide : médiane "
              f"{fr(quantile(ret, 0.5), 1)} ms, 1 bloc sur 100 au-delà de "
              f"{fr(quantile(ret, 0.99), 1)} ms, 1 sur 1000 au-delà de "
              f"{fr(p999, 1)} ms, le pire {fr(ret[-1], 1)} ms.")
        print(f"   Plus long silence du réseau : "
              f"{fr(r['trou_max'] * 1000, 0)} ms.")
        print(f"→  Réserve conseillée : {conseil} ms (le pire bloc sur mille, "
              f"plus un bloc). Celle de ce test : {fr(args.reserve, 0)} ms.")
    if r["joues"]:
        signe = "✅" if r["coupures"] == 0 else "⚠️ "
        print(f"{signe} Coupures entendues : {r['coupures']} blocs sur "
              f"{r['joues']} joués"
              + (f", dont {r['trop_tard']} arrivés trop tard pour la réserve"
                 if r["trop_tard"] else "") + ".")
    if "derive_ppm" in r:
        d = r["derive_ppm"]
        sens = "avance sur" if d > 0 else "retarde sur"
        print(f"   Dérive : l'horloge de la source {sens} celle de ce PC de "
              f"{fr(abs(d), 0)} millionièmes ({fr(abs(d) * 0.048, 2)} "
              f"échantillons par seconde). Un vrai haut-parleur la rattrape.")
    else:
        print("   Dérive : test trop court pour la mesurer (20 s au moins).")
    if hp.veilles:
        print(f"   Veille reçue {hp.veilles} fois.")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="Haut-parleur virtuel SON1.")
    ap.add_argument("--source", default="192.168.5.1",
                    help="adresse de la source (défaut 192.168.5.1)")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--port-local", type=int, default=PORT,
                    help="pris au hasard s'il est occupé")
    ap.add_argument("--reserve", type=float, default=40.0,
                    help="ms de réserve avant de jouer (défaut 40)")
    ap.add_argument("--accuses", type=float, default=10.0,
                    help="accusés par seconde, pour l'aller-retour")
    ap.add_argument("--wav", help="enregistrer ce qui est joué")
    ap.add_argument("--muet", action="store_true", help="ne pas jouer")
    ap.add_argument("--lecteur", default="auto",
                    choices=("auto", "sounddevice", "pw-play", "aplay"))
    ap.add_argument("--taire", help="A,B : cesser les battements à A s, "
                                    "pendant B s (T4.1)")
    ap.add_argument("--duree", type=float, default=0.0,
                    help="secondes (0 = sans fin)")
    args = ap.parse_args()
    if args.taire:
        a, _, b = args.taire.partition(",")
        args.taire = (float(a), float(b or 3))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("0.0.0.0", args.port_local))
    except OSError:
        sock.bind(("0.0.0.0", 0))
    sock.settimeout(0.2)
    print(f"  écoute sur le port {sock.getsockname()[1]}, battements vers "
          f"{args.source}:{args.port}, réserve {fr(args.reserve, 0)} ms",
          file=sys.stderr)

    hp = HautParleur(sock, args)
    lecteur = Lecteur("aucun" if args.muet else args.lecteur)
    if not args.muet:
        print(f"  lecture : {lecteur.nom}", file=sys.stderr)
    wav = None
    if args.wav:
        wav = wave.open(args.wav, "wb")
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(FREQUENCE)

    fils = [threading.Thread(target=hp.recevoir, daemon=True),
            threading.Thread(target=hp.battre, daemon=True),
            threading.Thread(target=hp.jouer, args=(lecteur, wav), daemon=True)]
    for f in fils:
        f.start()
    t0 = time.monotonic()
    try:
        while not args.duree or time.monotonic() - t0 < args.duree:
            time.sleep(min(2.0, max(0.05, args.duree - (time.monotonic() - t0))
                           if args.duree else 2.0))
            print(ligne_etat(hp), file=sys.stderr)
    except KeyboardInterrupt:
        pass
    hp.fini.set()
    for f in fils:
        f.join(timeout=1)
    lecteur.fermer()
    if wav is not None:
        wav.close()
    sys.exit(verdict(hp, args))


if __name__ == "__main__":
    main()
