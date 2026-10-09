#!/usr/bin/env python3
"""Balaye le délai de lecture du premier pixel de chaque ligne, sans analyseur.

    ./tools/balaye_premier_pixel.py --reference dumps/     # PNG de gbdump.py
    ./tools/balaye_premier_pixel.py                        # sans référence

Le firmware lit le premier pixel d'une ligne DELAI_PREMIER_PIXEL cycles PIO après
son front montant (capture.pio), et les commandes « < » et « > » de la console
changent ce délai à chaud. L'outil le fait varier de 0 à 31 et, pour chaque valeur,
vide quelques images et regarde la colonne 0 :

  - face à la référence : des images de la même scène, immobile, dont la colonne 0
    est connue juste (prises avec un délai déjà validé) ;
  - face à la colonne 1 de la même image : trop tard, la colonne 0 lit la donnée du
    pixel 1 et la recopie presque partout ;
  - d'une image à l'autre : une lecture au bord de la fenêtre hésite.

Choisir une scène où la colonne 0 change d'une ligne à l'autre et diffère de la
colonne 1 : la pièce aux bibliothèques de Pokémon Rouge, par exemple. À la fin, le
délai est remis à la valeur de config.h, celle du démarrage.

Dépendances : pyserial, Pillow (pour --reference).
"""
import argparse
import re
import sys
import time
from collections import Counter
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gbdump import DEBUT, FIN, PALETTES  # noqa: E402

L, H = 160, 144
CONFIG = Path(__file__).resolve().parent.parent / "firmware/sniffer/include/config.h"


def vider(s, attente=2.0):
    """Une image du sniffer, en indices 0..3, ou None si le vidage est tronqué."""
    s.reset_input_buffer()
    s.write(b"p")
    s.flush()
    morceaux, limite = [], time.monotonic() + attente
    while time.monotonic() < limite:
        d = s.read(8192)
        if d:
            morceaux.append(d.decode("utf-8", "replace"))
            if FIN in "".join(morceaux[-4:]):
                break
    texte = "".join(morceaux)
    i = texte.rfind(DEBUT)
    j = texte.find(FIN, i)
    if i < 0 or j < 0:
        return None
    hexa = re.sub(r"[^0-9a-fA-F]", "", "".join(texte[i:j].splitlines()[1:]))
    if len(hexa) != L * H // 2:
        return None
    o = bytes.fromhex(hexa)
    return [[(o[y * 40 + x // 4] >> (6 - 2 * (x & 3))) & 3 for x in range(L)]
            for y in range(H)]


def colonne0_reference(dossier):
    """Colonne 0 majoritaire des PNG de gbdump.py, quelles que soient palette et échelle."""
    from PIL import Image
    couleurs = {c: i for p in PALETTES.values() for i, c in enumerate(p)}
    votes = [Counter() for _ in range(H)]
    fichiers = sorted(Path(dossier).glob("*.png"))
    if not fichiers:
        sys.exit(f"aucun PNG dans {dossier}")
    for f in fichiers:
        im = Image.open(f).convert("RGB")
        e = im.width // L
        px = im.load()
        for y in range(H):
            votes[y][couleurs[px[e // 2, y * e + e // 2]]] += 1
    return [v.most_common(1)[0][0] for v in votes]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--reference", metavar="DOSSIER",
                    help="PNG de gbdump.py de la même scène, colonne 0 juste")
    ap.add_argument("--images", type=int, default=6, help="images par délai (défaut : 6)")
    args = ap.parse_args()

    ref = colonne0_reference(args.reference) if args.reference else None
    m = re.search(r"#define\s+DELAI_PREMIER_PIXEL\s+(\d+)", CONFIG.read_text())
    defaut = int(m.group(1))

    print(" délai    ns  ≠référence  =col1  instables   (lignes, cumulées sur les images)")
    justes = []
    with serial.Serial(args.port, 115200, timeout=0.05) as s:
        s.dtr = s.rts = True
        time.sleep(0.5)
        s.write(b"<" * 32)
        for d in range(32):
            if d:
                s.write(b">")
            time.sleep(0.15)          # quelques images avec le nouveau délai
            images = [im for im in (vider(s) for _ in range(args.images)) if im]
            c0 = [[im[y][0] for y in range(H)] for im in images]
            faux = sum(c[y] != ref[y] for c in c0 for y in range(H)) if ref else None
            copie = sum(im[y][0] == im[y][1] for im in images for y in range(H))
            instables = sum(len({c[y] for c in c0}) > 1 for y in range(H))
            if images and faux == 0 and instables == 0:
                justes.append(d)
            print(f"  {d:3}  {d * 1000 / 150:5.0f}  {'—' if ref is None else faux:>10}"
                  f"  {copie:5}  {instables:9}"
                  + ("" if len(images) == args.images else f"   ({len(images)} images)"))
        s.write(b"<" * 32 + b">" * defaut)

    print(f"\ndélai remis à {defaut} (config.h)")
    if ref is None:
        print("sans référence : la fenêtre s'arrête là où « =col1 » bondit — la colonne 0 "
              "y lit le pixel 1")
    elif justes:
        # la plus longue suite de délais consécutifs : la fenêtre valide
        suites, debut = [], justes[0]
        for a, b in zip(justes, justes[1:] + [None]):
            if b != a + 1:
                suites.append((debut, a))
                debut = b
        lo, hi = max(suites, key=lambda t: t[1] - t[0])
        print(f"colonne 0 juste et stable de {lo} à {hi} cycles")
        print(f"milieu : {(lo + hi) / 2:g} — à comparer à DELAI_PREMIER_PIXEL ({defaut})")
    else:
        print("aucun délai ne lit la colonne 0 juste : la scène a-t-elle bougé ?")


if __name__ == "__main__":
    main()
