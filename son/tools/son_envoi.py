#!/usr/bin/env python3
"""Émetteur `SON1` de test — tient le rôle de la source, depuis le PC.

    ./tools/son_envoi.py                              # sinus à 1 kHz
    ./tools/son_envoi.py --source balayage            # de 20 Hz à 20 kHz en 10 s
    ./tools/son_envoi.py --fichier musique.wav
    ./tools/son_envoi.py --source carre               # des notes en ondes carrées
    ./tools/son_envoi.py --perte 1 --retard 20 --derive 300
    ./tools/son_envoi.py --veille                     # la console est éteinte

Comme la vraie source, il attend que les haut-parleurs se signalent (un
battement deux fois par seconde), envoie une copie de chaque bloc à chacun,
quatre au plus, et oublie celui qu'il n'entend plus depuis 2 s. Il répond à
l'adresse et au port d'où vient le battement.

Il sert à éprouver un haut-parleur sans console ni source (T5.1), et le
haut-parleur virtuel sans réseau : on simule ce que le WiFi fera subir au son.

    --perte 1      1 % des copies jamais émises
    --retard 20    chaque copie retardée au hasard de 0 à 20 ms : elles
                   arrivent avec de la gigue, parfois dans le désordre
    --derive 300   l'horloge de la source va 300 millionièmes plus vite que
                   celle du PC : le haut-parleur doit le voir, et le rattraper

Le format fait foi dans firmware/commun/son1.h. Dépendance : numpy ; scipy
pour un WAV qui n'est pas à 48 kHz.
"""
import argparse
import heapq
import math
import socket
import struct
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
VOIES = 2
BLOC = 240                  # échantillons par voie : 5 ms
OUBLI_S = 2.0
VEILLE_S = 0.5
HAUTPARLEURS_MAX = 4


def entete(type_, volume, echantillon, frequence=FREQUENCE):
    return MAGIC + struct.pack("<BBBBII", type_, FMT_PCM16, VOIES, volume,
                               frequence, echantillon & 0xFFFFFFFF)


def fr(x, decimales=2):
    return f"{x:.{decimales}f}".replace(".", ",")


# ───────────────────────────────────────────────────────────── les sources

class Source:
    """Rend le bloc suivant : (BLOC, 2) en int16."""

    def __init__(self, args):
        self.n = 0                      # échantillons déjà rendus, par voie
        self.amplitude = 32767 * 10 ** (args.niveau / 20)
        self.args = args
        self.wav = None
        if args.fichier:
            self.wav = lire_wav(args.fichier)
            self.pos = 0

    def bloc(self):
        t = (self.n + np.arange(BLOC)) / FREQUENCE
        a, s = self.args, None
        if self.wav is not None:
            fin = self.pos + BLOC
            if fin > len(self.wav):
                self.pos = 0
                fin = BLOC
            s = self.wav[self.pos:fin].astype(np.float64)
            self.pos = fin
        elif a.source == "sinus":
            s = self.amplitude * np.sin(2 * np.pi * a.frequence * t)
        elif a.source == "balayage":
            # Balayage logarithmique de 20 Hz à 20 kHz, recommencé : la phase
            # est l'intégrale de la fréquence, pour ne jamais sauter.
            periode, f0, f1 = 10.0, 20.0, 20000.0
            k = math.log(f1 / f0)
            u = np.mod(t, periode)
            phase = 2 * np.pi * f0 * periode / k * (np.exp(u / periode * k) - 1)
            s = self.amplitude * np.sin(phase)
        elif a.source == "carre":
            # Comme le canal carré d'une Game Boy : un arpège, rapport
            # cyclique 50 %, une note toutes les 150 ms.
            notes = np.array((262.0, 330.0, 392.0, 523.0))
            f = notes[(t / 0.15).astype(np.int64) % len(notes)]
            s = self.amplitude * 0.5 * np.sign(np.sin(2 * np.pi * f * t))
        else:
            s = np.zeros(BLOC)
        self.n += BLOC
        if s.ndim == 1:
            s = np.stack([s, s], axis=1)
        return np.clip(np.round(s), -32768, 32767).astype("<i2")


def lire_wav(chemin):
    with wave.open(chemin, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{chemin} : seul le PCM 16 bits est pris en charge")
        voies, freq = w.getnchannels(), w.getframerate()
        brut = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    brut = brut.reshape(-1, voies)
    if voies == 1:
        brut = np.repeat(brut, 2, axis=1)
    elif voies > 2:
        brut = brut[:, :2]
    if freq != FREQUENCE:
        try:
            from scipy.signal import resample_poly
        except ImportError:
            sys.exit(f"{chemin} est à {freq} Hz : il faut scipy pour le "
                     f"ramener à {FREQUENCE} Hz")
        g = math.gcd(FREQUENCE, freq)
        brut = np.clip(resample_poly(brut.astype(np.float64), FREQUENCE // g,
                                     freq // g, axis=0),
                       -32768, 32767).astype("<i2")
    if len(brut) < BLOC:
        sys.exit(f"{chemin} est trop court")
    return brut


# ───────────────────────────────────────────────────────────── l'émission

class Emetteur:
    """Les haut-parleurs connus, les envois retardés, les accusés."""

    def __init__(self, sock, args):
        self.sock, self.args = sock, args
        self.verrou = threading.Lock()
        self.hp = {}                    # (adresse, port) → dernier battement
        self.fixes = []
        if args.cible:
            hote, _, port = args.cible.partition(":")
            self.fixes.append((hote, int(port or PORT)))
        self.file = []                  # (instant, ordre, paquet, destination)
        self.ordre = 0
        self.envois = {}                # numéro d'échantillon → instant d'envoi
        self.allers_retours = []
        self.accuses = self.battements = self.refuses = 0
        self.fini = threading.Event()
        self.reveil = threading.Condition(self.verrou)

    def destinations(self, maintenant):
        with self.verrou:
            for d in [d for d, t in self.hp.items() if maintenant - t > OUBLI_S]:
                del self.hp[d]
                print(f"  {d[0]}:{d[1]} oublié : muet depuis 2 s",
                      file=sys.stderr)
            return list(self.hp) + self.fixes

    def ecouter(self):
        while not self.fini.is_set():
            try:
                data, de = self.sock.recvfrom(64)
            except (BlockingIOError, InterruptedError):
                time.sleep(0.0005)
                continue
            except OSError:
                time.sleep(0.0005)
                continue
            arrivee = time.monotonic()
            if len(data) < ENTETE or data[:4] != MAGIC:
                continue
            type_ = data[4]
            if type_ == TYPE_BATTEMENT:
                with self.verrou:
                    self.battements += 1
                    if de not in self.hp and len(self.hp) >= HAUTPARLEURS_MAX:
                        self.refuses += 1
                        continue
                    if de not in self.hp:
                        print(f"  {de[0]}:{de[1]} s'est signalé", file=sys.stderr)
                    self.hp[de] = arrivee
            elif type_ == TYPE_ACCUSE:
                (n,) = struct.unpack_from("<I", data, 12)
                with self.verrou:
                    self.accuses += 1
                    t = self.envois.pop(n, None)
                    if t is not None:
                        self.allers_retours.append(arrivee - t)

    def poster(self, quand, paquet, destination, numero=None):
        with self.reveil:
            heapq.heappush(self.file, (quand, self.ordre, paquet, destination,
                                       numero))
            self.ordre += 1
            self.reveil.notify()

    def expedier(self):
        """Fil dédié aux envois : chaque copie part à son heure, retard
        simulé compris. Deux copies retardées différemment peuvent arriver
        dans le désordre, comme sur un vrai réseau."""
        while not self.fini.is_set():
            with self.reveil:
                if not self.file:
                    self.reveil.wait(0.05)
                    continue
                quand = self.file[0][0]
                reste = quand - time.monotonic()
                if reste > 0:
                    self.reveil.wait(min(reste, 0.05))
                    continue
                _, _, paquet, destination, numero = heapq.heappop(self.file)
            try:
                self.sock.sendto(paquet, destination)
                if numero is not None:
                    with self.verrou:
                        self.envois.setdefault(numero, time.monotonic())
                        if len(self.envois) > 2000:
                            for k in list(self.envois)[:500]:
                                self.envois.pop(k, None)
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser(description="Émetteur SON1 de test.")
    ap.add_argument("--source", choices=("sinus", "balayage", "carre",
                                         "silence"), default="sinus")
    ap.add_argument("--fichier", help="un WAV 16 bits à jouer en boucle")
    ap.add_argument("--frequence", type=float, default=1000.0,
                    help="du sinus, en Hz")
    ap.add_argument("--niveau", type=float, default=-12.0,
                    help="en dB sous la pleine échelle (défaut -12)")
    ap.add_argument("--volume", type=int, default=255,
                    help="le volume transmis au haut-parleur, 0..255")
    ap.add_argument("--veille", action="store_true",
                    help="n'envoyer que la veille : la console est éteinte")
    ap.add_argument("--cible", help="un haut-parleur fixe, hôte[:port], en "
                                    "plus de ceux qui se signalent")
    ap.add_argument("--port-local", type=int, default=PORT)
    ap.add_argument("--perte", type=float, default=0.0,
                    help="%% de copies jamais émises")
    ap.add_argument("--retard", type=float, default=0.0,
                    help="retard ajouté au hasard à chaque copie, de 0 à "
                         "RETARD ms")
    ap.add_argument("--derive", type=float, default=0.0,
                    help="avance de l'horloge de la source, en millionièmes "
                         "(ppm) ; négatif : elle retarde")
    ap.add_argument("--depart", type=lambda s: int(s, 0), default=0,
                    help="premier numéro d'échantillon ; 0xFFF00000 éprouve "
                         "le passage par zéro")
    ap.add_argument("--duree", type=float, default=0.0,
                    help="secondes (0 = sans fin)")
    args = ap.parse_args()
    if args.fichier:
        args.source = "fichier"
    if not 0 <= args.volume <= 255:
        sys.exit("--volume va de 0 à 255")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("0.0.0.0", args.port_local))
    except OSError as e:
        sys.exit(f"port {args.port_local} indisponible ({e}) : un autre "
                 f"émetteur tourne-t-il ? sinon --port-local")
    sock.setblocking(False)

    em = Emetteur(sock, args)
    src = Source(args)
    rng = np.random.default_rng()
    fils = [threading.Thread(target=em.ecouter, daemon=True),
            threading.Thread(target=em.expedier, daemon=True)]
    for f in fils:
        f.start()

    # Le bloc k part à t0 + k × 5 ms, mesuré à l'horloge de la source : si
    # elle avance de `derive` ppm, ses 5 ms durent un peu moins pour le PC.
    periode = BLOC / FREQUENCE / (1 + args.derive * 1e-6)
    print(f"  écoute des battements sur le port {args.port_local} ; "
          f"{'VEILLE' if args.veille else args.source}"
          f"{f' à {fr(args.frequence, 0)} Hz' if args.source == 'sinus' else ''}"
          f", volume {args.volume}", file=sys.stderr)
    if args.perte or args.retard or args.derive:
        print(f"  simulé : {fr(args.perte)} % de pertes, retard 0 à "
              f"{fr(args.retard, 0)} ms, dérive {fr(args.derive, 0)} ppm",
              file=sys.stderr)

    t0 = time.monotonic()
    t_stat = t_veille = t0
    k = 0
    blocs = copies = perdues = 0
    numero = args.depart
    try:
        while True:
            maintenant = time.monotonic()
            if args.duree and maintenant - t0 >= args.duree:
                break
            dest = em.destinations(maintenant)
            if args.veille:
                if maintenant - t_veille >= VEILLE_S:
                    t_veille = maintenant
                    for d in dest:
                        em.poster(maintenant, entete(TYPE_VEILLE, args.volume,
                                                     0), d)
            else:
                charge = src.bloc().tobytes()
                paquet = entete(TYPE_BLOC, args.volume, numero) + charge
                for d in dest:
                    if args.perte and rng.random() * 100 < args.perte:
                        perdues += 1
                        continue
                    retard = rng.random() * args.retard / 1000
                    em.poster(maintenant + retard, paquet, d, numero)
                    copies += 1
                blocs += 1
                numero = (numero + BLOC) & 0xFFFFFFFF

            if maintenant - t_stat >= 2.0:
                t_stat = maintenant
                with em.verrou:
                    ar = em.allers_retours[-200:]
                    acc = em.accuses
                lat = (f"{fr(sum(ar) / len(ar) * 1000)} ms" if ar else "—")
                print(f"  {len(dest)} haut-parleur(s)   {blocs} blocs   "
                      f"{copies} copies   perdues {perdues}   accusés {acc}   "
                      f"aller-retour {lat}", file=sys.stderr)

            k += 1
            reste = t0 + k * periode - time.monotonic()
            if reste > 0:
                time.sleep(reste)
    except KeyboardInterrupt:
        pass
    time.sleep(args.retard / 1000 + 0.1)
    em.fini.set()

    d = max(time.monotonic() - t0, 1e-9)
    with em.verrou:
        ar = list(em.allers_retours)
        acc, refuses = em.accuses, em.refuses
    print(f"\n  {blocs} blocs en {fr(d, 1)} s = {fr(blocs / d, 1)} blocs/s   "
          f"{copies} copies   {perdues} perdues exprès   {acc} accusés",
          file=sys.stderr)
    if refuses:
        print(f"  {refuses} battements refusés : plus de "
              f"{HAUTPARLEURS_MAX} haut-parleurs", file=sys.stderr)
    if ar:
        ar.sort()
        print(f"  aller-retour : médiane {fr(ar[len(ar) // 2] * 1000)} ms   "
              f"pire {fr(ar[-1] * 1000)} ms", file=sys.stderr)
    if not copies and not args.veille:
        print("  aucun haut-parleur ne s'est signalé : le haut-parleur "
              "est-il lancé, et vise-t-il ce PC ?", file=sys.stderr)


if __name__ == "__main__":
    main()
