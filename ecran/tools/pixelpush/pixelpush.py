#!/usr/bin/env python3
"""Émetteur PXL1 — pousse des images vers le module ÉCRAN en UDP.

Le fichier de disposition décrit les nœuds et leur rectangle dans l'image
complète. Le protocole étant tuile-conscient, passer de 1 à 3 ou 9 nœuds ne
change que ce fichier — ni le firmware, ni ce script.

    ./pixelpush.py --layout layout-1x1.toml --source anim
    ./pixelpush.py --layout layout-1x1.toml --source image --fichier photo.png
    ./pixelpush.py --layout layout-1x1.toml --source mire --fps 1
"""

import argparse
import math
import socket
import struct
import sys
import time
import tomllib
from pathlib import Path

import numpy as np

# --- protocole (miroir de firmware/ecran/include/pxl1.h) ---------------------

MAGIC = b"PXL1"
ENTETE = 12
CHARGE_MAX = 1400

TYPE_FRAME = 0
FMT_BGR888 = 0
FLAG_DERNIERE = 0x01

# Les tranches font un multiple de 3 octets pour ne jamais couper un pixel en
# deux : plus lisible côté firmware, et sans coût.
CHARGE_UTILE = (CHARGE_MAX // 3) * 3


def entete(node_id: int, frame_id: int, offset: int, derniere: bool) -> bytes:
    return MAGIC + struct.pack(
        "<BBHBBH",
        TYPE_FRAME,
        node_id,
        frame_id & 0xFFFF,
        FMT_BGR888,
        FLAG_DERNIERE if derniere else 0,
        offset,
    )


class Noeud:
    def __init__(self, d: dict):
        self.id = int(d["id"])
        self.ip = str(d["ip"])
        self.port = int(d.get("port", 4242))
        self.x0, self.y0 = int(d["x0"]), int(d["y0"])
        self.w, self.h = int(d["w"]), int(d["h"])

    def __repr__(self):
        return (f"noeud {self.id} → {self.ip}:{self.port} "
                f"rect ({self.x0},{self.y0}) {self.w}×{self.h}")


class Emetteur:
    def __init__(self, chemin_layout: Path):
        with open(chemin_layout, "rb") as f:
            conf = tomllib.load(f)
        self.largeur = int(conf["image"]["largeur"])
        self.hauteur = int(conf["image"]["hauteur"])
        self.noeuds = [Noeud(n) for n in conf["noeud"]]
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.frame_id = 0
        self.octets_envoyes = 0
        self.paquets = 0

    def envoyer(self, image_rgb: np.ndarray) -> None:
        """image_rgb : (hauteur, largeur, 3) en uint8, ordre R G B."""
        if image_rgb.shape != (self.hauteur, self.largeur, 3):
            raise ValueError(
                f"image {image_rgb.shape}, attendu "
                f"({self.hauteur}, {self.largeur}, 3)")

        # Le protocole transporte du BGR : c'est l'ordre qu'attend la dalle,
        # ce qui permet au firmware d'écrire la charge utile en place.
        image_bgr = image_rgb[:, :, ::-1]

        for n in self.noeuds:
            tuile = image_bgr[n.y0:n.y0 + n.h, n.x0:n.x0 + n.w]
            charge = np.ascontiguousarray(tuile).tobytes()

            offset = 0
            total = len(charge)
            while offset < total:
                bout = min(CHARGE_UTILE, total - offset)
                derniere = (offset + bout) >= total
                paquet = entete(n.id, self.frame_id, offset, derniere) + \
                    charge[offset:offset + bout]
                self.sock.sendto(paquet, (n.ip, n.port))
                self.octets_envoyes += len(paquet)
                self.paquets += 1
                offset += bout

        self.frame_id = (self.frame_id + 1) & 0xFFFF


# --- sources d'image --------------------------------------------------------

def source_mire(w: int, h: int):
    """Mire fixe : cadre, dégradé, damier, barres de couleur."""
    img = np.zeros((h, w, 3), np.uint8)
    img[2:h // 4] = np.linspace(0, 255, w, dtype=np.uint8)[None, :, None]
    d = (np.add.outer(np.arange(h), np.arange(w)) & 1).astype(np.uint8) * 255
    img[h // 4 + 2:h // 2] = d[h // 4 + 2:h // 2, :, None]
    bandes = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    for i, c in enumerate(bandes):
        x0, x1 = i * w // 4, (i + 1) * w // 4
        img[h // 2 + 2:3 * h // 4, x0:x1] = c
    img[0, :] = img[-1, :] = img[:, 0] = img[:, -1] = 255
    while True:
        yield img


def source_anim(w: int, h: int):
    """Animation : prouve que le flux est vivant et mesure la fluidité."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    t = 0.0
    while True:
        img = np.zeros((h, w, 3), np.uint8)
        v = (np.sin(xx / 6 + t) + np.sin(yy / 7 - t * 1.3) +
             np.sin((xx + yy) / 9 + t * 0.7))
        img[:, :, 0] = ((v + 3) * 42).astype(np.uint8)
        img[:, :, 1] = ((np.sin(v * 2 + t) + 1) * 127).astype(np.uint8)
        img[:, :, 2] = ((np.cos(v + t * 0.5) + 1) * 127).astype(np.uint8)
        # Curseur : un pixel qui fait le tour du cadre, repère de fluidité.
        p = int(t * 30) % (2 * (w + h) - 4)
        if p < w:            img[0, p] = 255
        elif p < w + h - 1:  img[p - w + 1, w - 1] = 255
        elif p < 2 * w + h - 2: img[h - 1, w - 1 - (p - w - h + 2)] = 255
        else:                img[h - 1 - (p - 2 * w - h + 3), 0] = 255
        yield img
        t += 0.06


def source_image(w: int, h: int, chemin: str):
    from PIL import Image
    im = Image.open(chemin).convert("RGB").resize((w, h), Image.LANCZOS)
    img = np.asarray(im, np.uint8)
    while True:
        yield img


# --- programme principal ----------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Émetteur PXL1")
    ap.add_argument("--layout", default="layout-1x1.toml", type=Path)
    ap.add_argument("--source", default="anim",
                    choices=["anim", "mire", "image"])
    ap.add_argument("--fichier", help="image source, pour --source image")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--duree", type=float, default=0.0,
                    help="secondes, 0 = sans fin")
    args = ap.parse_args()

    if not args.layout.exists():
        print(f"disposition introuvable : {args.layout}", file=sys.stderr)
        return 1

    em = Emetteur(args.layout)
    print(f"image {em.largeur}×{em.hauteur}, {len(em.noeuds)} nœud(s)")
    for n in em.noeuds:
        print(f"  {n}")

    if args.source == "image":
        if not args.fichier:
            print("--source image exige --fichier", file=sys.stderr)
            return 1
        gen = source_image(em.largeur, em.hauteur, args.fichier)
    elif args.source == "mire":
        gen = source_mire(em.largeur, em.hauteur)
    else:
        gen = source_anim(em.largeur, em.hauteur)

    periode = 1.0 / args.fps if args.fps > 0 else 0.0
    debut = time.monotonic()
    prochain = debut
    trames = 0
    dernier_point = debut
    # Détection des décrochages côté PC : si l'envoi lui-même se bloque, le
    # problème n'est ni le firmware ni l'air, mais cette machine (balayage WiFi
    # de NetworkManager, ordonnancement, ramasse-miettes).
    dernier_envoi = debut
    pire_ecart = 0.0
    decrochages = 0

    print(f"\némission à {args.fps:g} img/s — Ctrl-C pour arrêter\n")
    try:
        while True:
            em.envoyer(next(gen))
            trames += 1

            maintenant = time.monotonic()
            ecart = maintenant - dernier_envoi
            dernier_envoi = maintenant
            if ecart > pire_ecart:
                pire_ecart = ecart
            if ecart > 3 * periode and trames > 2:
                decrochages += 1
                print(f"    décrochage émetteur : {ecart * 1000:.0f} ms "
                      f"à t+{maintenant - debut:.1f} s")
            if maintenant - dernier_point >= 5.0:
                dt = maintenant - dernier_point
                debit = em.octets_envoyes * 8 / dt / 1e6
                print(f"  {trames} trames  {trames / dt:.1f} img/s  "
                      f"{debit:.2f} Mbit/s  {em.paquets} paquets  "
                      f"pire écart {pire_ecart * 1000:.0f} ms  "
                      f"{decrochages} décrochages")
                em.octets_envoyes = em.paquets = 0
                trames = 0
                pire_ecart = 0.0
                decrochages = 0
                dernier_point = maintenant

            if args.duree and maintenant - debut >= args.duree:
                break

            prochain += periode
            retard = prochain - time.monotonic()
            if retard > 0:
                time.sleep(retard)
            else:
                prochain = time.monotonic()
    except KeyboardInterrupt:
        print("\narrêt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
