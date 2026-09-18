#!/usr/bin/env python3
"""Émetteur PXL1 — pousse des images vers le module ÉCRAN en UDP.

Le fichier de disposition décrit les nœuds et leur rectangle dans l'image
complète. Le protocole étant tuile-conscient, passer de 1 à 3 ou 9 nœuds ne
change que ce fichier — ni le firmware, ni ce script.

    ./pixelpush.py --source anim
    ./pixelpush.py --source horloge --fps 10
    ./pixelpush.py --source gif --fichier boucle.gif
    ./pixelpush.py --source ecran --region 100,100,512,512
    ./pixelpush.py --source video --fichier film.mp4        (nécessite ffmpeg)

Éprouver le réassemblage du firmware :

    ./pixelpush.py --source anim --perte 2        # 2 % de paquets non émis
    ./pixelpush.py --source anim --desordre 5     # 5 % de paquets retardés
"""

from __future__ import annotations

import argparse
import random
import select
import socket
import struct
import sys
import time
import tomllib
from pathlib import Path

import numpy as np

import sources

# --- protocole (miroir de firmware/ecran/include/pxl1.h) ---------------------

MAGIC = b"PXL1"
ENTETE = 12
CHARGE_MAX = 1400

TYPE_FRAME = 0
TYPE_PING = 2
FMT_BGR888 = 0
FLAG_DERNIERE = 0x01

# Tranches multiples de 3 octets : jamais un pixel coupé en deux.
CHARGE_UTILE = (CHARGE_MAX // 3) * 3


def entete(node_id: int, frame_id: int, offset: int, derniere: bool) -> bytes:
    return MAGIC + struct.pack(
        "<BBHBBH", TYPE_FRAME, node_id, frame_id & 0xFFFF,
        FMT_BGR888, FLAG_DERNIERE if derniere else 0, offset)


class Noeud:
    def __init__(self, d: dict):
        self.id = int(d["id"])
        self.ip = str(d["ip"])
        self.port = int(d.get("port", 4242))
        self.x0, self.y0 = int(d["x0"]), int(d["y0"])
        self.w, self.h = int(d["w"]), int(d["h"])

    def __repr__(self) -> str:
        return (f"nœud {self.id} → {self.ip}:{self.port} "
                f"rect ({self.x0},{self.y0}) {self.w}×{self.h}")


class Emetteur:
    def __init__(self, chemin: Path, perte: float = 0.0, desordre: float = 0.0):
        with open(chemin, "rb") as f:
            conf = tomllib.load(f)
        self.largeur = int(conf["image"]["largeur"])
        self.hauteur = int(conf["image"]["hauteur"])
        self.noeuds = [Noeud(n) for n in conf["noeud"]]
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.frame_id = 0
        # Mesure de latence : le firmware renvoie un accusé au moment où la
        # trame est affichée. L'aller-retour est donc chronométré sur la seule
        # horloge de cette machine — aucune synchronisation à supposer.
        self.envois: dict[int, float] = {}
        self.latences: list[float] = []
        self.octets = 0
        self.paquets = 0
        # Injection de défauts, pour éprouver le réassemblage du firmware.
        self.perte = perte / 100.0
        self.desordre = desordre / 100.0
        self.perdus = 0
        self.retardes = 0

    def envoyer(self, image_rgb: np.ndarray) -> None:
        """image_rgb : (hauteur, largeur, 3) uint8, ordre R G B."""
        if image_rgb.shape != (self.hauteur, self.largeur, 3):
            raise ValueError(f"image {image_rgb.shape}, attendu "
                             f"({self.hauteur}, {self.largeur}, 3)")

        # Le protocole transporte du BGR : c'est l'ordre qu'attend la dalle, ce
        # qui permet au firmware d'écrire la charge utile en place.
        image_bgr = image_rgb[:, :, ::-1]
        differes: list[tuple[bytes, tuple[str, int]]] = []

        for n in self.noeuds:
            tuile = image_bgr[n.y0:n.y0 + n.h, n.x0:n.x0 + n.w]
            charge = np.ascontiguousarray(tuile).tobytes()
            dest = (n.ip, n.port)

            offset, total = 0, len(charge)
            while offset < total:
                bout = min(CHARGE_UTILE, total - offset)
                derniere = (offset + bout) >= total
                paquet = (entete(n.id, self.frame_id, offset, derniere) +
                          charge[offset:offset + bout])
                offset += bout

                if self.perte and random.random() < self.perte:
                    self.perdus += 1
                    continue
                if self.desordre and random.random() < self.desordre:
                    # Retardé après la dernière tranche : le firmware doit le
                    # traiter comme du désordre, pas comme une nouvelle trame.
                    differes.append((paquet, dest))
                    self.retardes += 1
                    continue

                self.sock.sendto(paquet, dest)
                self.octets += len(paquet)
                self.paquets += 1

        for paquet, dest in differes:
            self.sock.sendto(paquet, dest)
            self.octets += len(paquet)
            self.paquets += 1

        self.envois[self.frame_id] = time.monotonic()
        if len(self.envois) > 512:  # borne mémoire : on oublie les vieux
            for k in sorted(self.envois)[:256]:
                del self.envois[k]

        self.frame_id = (self.frame_id + 1) & 0xFFFF

    def relever_accuses(self) -> None:
        """Vide la file des accusés arrivés, sans bloquer."""
        while True:
            try:
                data, _ = self.sock.recvfrom(64)
            except BlockingIOError:
                return
            except OSError:
                return
            if len(data) < ENTETE or data[:4] != MAGIC:
                continue
            type_, _node, fid = struct.unpack_from("<BBH", data, 4)
            if type_ != TYPE_PING:
                continue
            t0 = self.envois.pop(fid, None)
            if t0 is not None:
                self.latences.append(time.monotonic() - t0)


def choisir_source(nom: str, w: int, h: int, args):
    if nom == "mire":
        return sources.mire(w, h)
    if nom == "anim":
        return sources.animation(w, h)
    if nom == "horloge":
        return sources.horloge(w, h)
    if nom == "ecran":
        region = None
        if args.region:
            x, y, rw, rh = (int(v) for v in args.region.split(","))
            region = (x, y, x + rw, y + rh)
        return sources.capture_ecran(w, h, region)
    if not args.fichier:
        sys.exit(f"--source {nom} exige --fichier")
    if nom == "image":
        return sources.image_fixe(w, h, args.fichier)
    if nom == "gif":
        return sources.gif(w, h, args.fichier)
    if nom == "video":
        return sources.video(w, h, args.fichier)
    sys.exit(f"source inconnue : {nom}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Émetteur PXL1 pour le module ÉCRAN",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--layout", default="layout-1x1.toml", type=Path)
    ap.add_argument("--source", default="anim",
                    choices=["anim", "mire", "horloge", "image", "gif",
                             "ecran", "video"])
    ap.add_argument("--fichier", help="source, pour image / gif / video")
    ap.add_argument("--region", help="capture d'écran : x,y,largeur,hauteur")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--duree", type=float, default=0.0, help="s, 0 = sans fin")
    ap.add_argument("--perte", type=float, default=0.0,
                    help="%% de paquets volontairement non émis")
    ap.add_argument("--desordre", type=float, default=0.0,
                    help="%% de paquets volontairement retardés")
    args = ap.parse_args()

    if not args.layout.exists():
        sys.exit(f"disposition introuvable : {args.layout}")

    em = Emetteur(args.layout, args.perte, args.desordre)
    print(f"image {em.largeur}×{em.hauteur}, {len(em.noeuds)} nœud(s)")
    for n in em.noeuds:
        print(f"  {n}")
    if args.perte or args.desordre:
        print(f"  ⚠ injection : {args.perte:g} % de perte, "
              f"{args.desordre:g} % de désordre")

    try:
        gen = choisir_source(args.source, em.largeur, em.hauteur, args)
    except RuntimeError as e:
        sys.exit(str(e))

    periode = 1.0 / args.fps if args.fps > 0 else 0.0
    debut = prochain = dernier_point = dernier_envoi = time.monotonic()
    trames = 0
    pire_ecart = 0.0
    decrochages = 0

    print(f"\némission « {args.source} » à {args.fps:g} img/s — Ctrl-C pour arrêter\n")
    try:
        while True:
            em.envoyer(next(gen))
            trames += 1

            maintenant = time.monotonic()
            # Décrochage de l'émetteur lui-même : si l'envoi se bloque, le
            # problème n'est ni le firmware ni l'air, mais cette machine.
            ecart = maintenant - dernier_envoi
            dernier_envoi = maintenant
            pire_ecart = max(pire_ecart, ecart)
            if ecart > 3 * periode and trames > 2:
                decrochages += 1
                print(f"    décrochage émetteur : {ecart * 1000:.0f} ms "
                      f"à t+{maintenant - debut:.1f} s")

            if maintenant - dernier_point >= 5.0:
                dt = maintenant - dernier_point
                ligne = (f"  {trames} trames  {trames / dt:.1f} img/s  "
                         f"{em.octets * 8 / dt / 1e6:.2f} Mbit/s  "
                         f"{em.paquets} paquets  "
                         f"pire écart {pire_ecart * 1000:.0f} ms  "
                         f"{decrochages} décrochages")
                if em.perdus or em.retardes:
                    ligne += f"  [injecté : {em.perdus} perdus, {em.retardes} retardés]"
                print(ligne)

                if em.latences:
                    lat = sorted(em.latences)
                    n = len(lat)
                    print(f"    aller-retour jusqu'à l'affichage : "
                          f"moy {sum(lat) / n * 1000:.1f} ms  "
                          f"min {lat[0] * 1000:.1f}  "
                          f"médiane {lat[n // 2] * 1000:.1f}  "
                          f"p95 {lat[int(n * 0.95)] * 1000:.1f}  "
                          f"max {lat[-1] * 1000:.1f}   ({n} accusés)")
                    em.latences.clear()
                em.octets = em.paquets = em.perdus = em.retardes = 0
                trames = 0
                pire_ecart = 0.0
                decrochages = 0
                dernier_point = maintenant

            if args.duree and maintenant - debut >= args.duree:
                break

            prochain += periode
            # On attend la prochaine échéance en RELEVANT LES ACCUSÉS au fil de
            # leur arrivée. Les relever une seule fois par tour quantifierait la
            # mesure à la période de trame : la médiane vaudrait exactement
            # 16,7 ms quelle que soit la latence réelle. Constaté le 18/09/2026.
            while True:
                reste = prochain - time.monotonic()
                if reste <= 0:
                    break
                pret, _, _ = select.select([em.sock], [], [], reste)
                if pret:
                    em.relever_accuses()
            em.relever_accuses()
            if prochain < time.monotonic() - periode:
                # La source ne suit pas la cadence : on n'accumule pas de dette.
                prochain = time.monotonic()
    except KeyboardInterrupt:
        print("\narrêt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
