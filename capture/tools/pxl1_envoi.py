#!/usr/bin/env python3
"""Émetteur `PXL1` de test — tient le rôle du sniffeur, depuis le PC.

    ./tools/pxl1_envoi.py --source anim
    ./tools/pxl1_envoi.py --fichier docs/releves/phase2-premiere-trame.png
    ./tools/pxl1_envoi.py --source anim --perte 2      # 2 % de paquets perdus

Il sert à **éprouver l'écran virtuel sans le Pico**. Le jour où le sniffeur
émettra pour de bon, l'écran virtuel aura déjà été validé contre un émetteur
dont on maîtrise tout — et il ne restera qu'un seul suspect.

C'est la même précaution que le §E.1 du guide : ne jamais mettre au point deux
inconnues à la fois.

Dépendances : numpy, Pillow.
"""
import argparse
import socket
import struct
import sys
import threading
import time

import numpy as np

PORT = 4242
MAGIC = b"PXL1"
CHARGE_MAX = 1400

TYPE_FRAME, TYPE_CTRL, TYPE_PING = 0, 1, 2
FMT_IDX2 = 2
FLAG_DERNIERE = 0x01
CTRL_PALETTE, CTRL_LUMINOSITE, CTRL_GEOMETRIE = 0, 1, 2

GB_L, GB_H = 160, 144

PALETTES = {
    "gris": [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)],
    "dmg": [(0x9B, 0xBC, 0x0F), (0x8B, 0xAC, 0x0F),
            (0x30, 0x62, 0x30), (0x0F, 0x38, 0x0F)],
}


def entete(node, fid, fmt, derniere, offset, type_=TYPE_FRAME):
    return MAGIC + struct.pack("<BBHBBH", type_, node, fid & 0xFFFF, fmt,
                               FLAG_DERNIERE if derniere else 0, offset)


def empaqueter_idx2(indices):
    """(h, w) d'indices 0..3 → octets, pixel de gauche dans les bits de poids
    fort. C'est la disposition démontrée au §D.3 et vérifiée sur matériel."""
    a = indices.reshape(-1, 4).astype(np.uint8)
    return ((a[:, 0] << 6) | (a[:, 1] << 4) | (a[:, 2] << 2) | a[:, 3]).tobytes()


def depuis_image(chemin, largeur, hauteur):
    from PIL import Image
    im = Image.open(chemin).convert("L").resize((largeur, hauteur),
                                                Image.NEAREST)
    lum = np.asarray(im, dtype=np.uint8)
    # 🔬 Polarité mesurée en phase 0 : « 00 » = blanc. L'indice croît donc
    # quand la luminance décroît.
    return (3 - (lum.astype(np.uint16) * 4 // 256)).clip(0, 3).astype(np.uint8)


def mire(largeur, hauteur):
    y, x = np.mgrid[0:hauteur, 0:largeur]
    return ((x // 8 + y // 8) % 4).astype(np.uint8)


def animation(largeur, hauteur, t):
    """Plasma plus un curseur qui fait le tour du cadre : l'œil repère une
    saccade bien mieux sur un point qui se déplace que sur un fond animé."""
    y, x = np.mgrid[0:hauteur, 0:largeur]
    v = (np.sin(x / 9.0 + t * 2.0) + np.sin(y / 7.0 - t * 1.5)
         + np.sin((x + y) / 11.0 + t))
    img = ((v + 3.0) / 6.0 * 3.9).astype(np.uint8).clip(0, 3)

    perimetre = 2 * (largeur + hauteur) - 4
    p = int(t * 120) % perimetre
    if p < largeur:
        cx, cy = p, 0
    elif p < largeur + hauteur:
        cx, cy = largeur - 1, p - largeur
    elif p < 2 * largeur + hauteur:
        cx, cy = largeur - 1 - (p - largeur - hauteur), hauteur - 1
    else:
        cx, cy = 0, hauteur - 1 - (p - 2 * largeur - hauteur)
    img[max(0, cy - 2):cy + 3, max(0, cx - 2):cx + 3] = 3
    return img


def main():
    ap = argparse.ArgumentParser(description="Émetteur PXL1 de test.")
    ap.add_argument("--cible", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--noeud", type=int, default=0)
    ap.add_argument("--source", choices=("anim", "mire", "image"),
                    default="anim")
    ap.add_argument("--fichier", help="PNG à envoyer (implique --source image)")
    ap.add_argument("--largeur", type=int, default=GB_L)
    ap.add_argument("--hauteur", type=int, default=GB_H)
    ap.add_argument("--fps", type=float, default=59.727)
    ap.add_argument("--palette", choices=sorted(PALETTES), default="gris")
    ap.add_argument("--perte", type=float, default=0.0,
                    help="%% de paquets jamais émis, pour éprouver le "
                         "réassemblage du récepteur")
    ap.add_argument("--duree", type=float, default=0.0,
                    help="secondes (0 = sans fin)")
    args = ap.parse_args()

    if args.fichier:
        args.source = "image"
    if args.source == "image" and not args.fichier:
        sys.exit("--source image demande --fichier")

    L, H = args.largeur, args.hauteur
    if (L % 4) or (L * H) % 4:
        sys.exit("largeur et surface doivent être multiples de 4 en IDX2")
    taille = L * H // 4

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    cible = (args.cible, args.port)

    fixe = None
    if args.source == "image":
        fixe = empaqueter_idx2(depuis_image(args.fichier, L, H))
    elif args.source == "mire":
        fixe = empaqueter_idx2(mire(L, H))

    pal = PALETTES[args.palette]
    # 256 entrées B,G,R ; le récepteur n'utilise que les 4 premières en IDX2.
    corps_pal = bytearray()
    for i in range(256):
        r, g, b = pal[i] if i < 4 else (0, 0, 0)
        corps_pal += bytes((b, g, r))
    paquet_pal = (entete(args.noeud, 0, FMT_IDX2, False, 0, TYPE_CTRL)
                  + bytes([CTRL_PALETTE]) + bytes(corps_pal))
    paquet_geo = (entete(args.noeud, 0, FMT_IDX2, False, 0, TYPE_CTRL)
                  + bytes([CTRL_GEOMETRIE]) + struct.pack("<HHB", L, H, FMT_IDX2))

    rng = np.random.default_rng()
    fid = 0
    envois = {}
    latences = []
    compteur = {"accuses": 0}
    verrou = threading.Lock()
    fini = threading.Event()

    def recevoir_accuses():
        """Fil dédié : l'accusé est horodaté À SON ARRIVÉE.

        Le relever une fois par trame, après l'envoi, quantifierait la mesure
        à une période de trame — 17 ms sur boucle locale, où le vrai
        aller-retour est de l'ordre de 0,1 ms. C'est un artefact de
        l'instrument, et il masquerait entièrement la latence réelle qu'on
        cherche à mesurer en phase 4."""
        ecoute = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ecoute.settimeout(0.2)
        while not fini.is_set():
            try:
                data, _ = sock.recvfrom(64)
            except (BlockingIOError, OSError):
                time.sleep(0.0005)
                continue
            arrivee = time.monotonic()
            if len(data) >= 12 and data[:4] == MAGIC:
                t_, _n, f_ = struct.unpack_from("<BBH", data, 4)
                if t_ == TYPE_PING:
                    with verrou:
                        compteur["accuses"] += 1
                        d = envois.pop(f_, None)
                        if d is not None:
                            latences.append(arrivee - d)
        ecoute.close()

    fil_accuses = threading.Thread(target=recevoir_accuses, daemon=True)
    fil_accuses.start()
    t_ctrl = 0.0
    t0 = time.monotonic()
    t_stat = t0
    paquets = octets = perdus = 0
    periode = 1.0 / args.fps

    print(f"  émission vers {args.cible}:{args.port}, nœud {args.noeud}, "
          f"{L}x{H} IDX2, {args.fps:.3f} img/s", file=sys.stderr)
    print(f"  {taille} octets par trame, "
          f"{-(-taille // CHARGE_MAX)} paquets, "
          f"{taille * args.fps * 8 / 1e6:.2f} Mbit/s", file=sys.stderr)

    try:
        while True:
            debut = time.monotonic()
            if args.duree and debut - t0 >= args.duree:
                break

            if debut - t_ctrl >= 2.0:
                t_ctrl = debut
                for p in (paquet_geo, paquet_pal):
                    try:
                        sock.sendto(p, cible)
                    except OSError:
                        pass

            if fixe is not None:
                charge = fixe
            else:
                charge = empaqueter_idx2(animation(L, H, debut - t0))

            offset = 0
            while offset < taille:
                n = min(CHARGE_MAX, taille - offset)
                derniere = (offset + n == taille)
                if args.perte and rng.random() * 100 < args.perte:
                    perdus += 1
                else:
                    try:
                        sock.sendto(entete(args.noeud, fid, FMT_IDX2, derniere,
                                           offset) + charge[offset:offset + n],
                                    cible)
                        paquets += 1
                        octets += n + 12
                    except OSError:
                        pass
                offset += n
            with verrou:
                envois[fid & 0xFFFF] = time.monotonic()
                if len(envois) > 256:          # bornée : un accusé perdu ne fuit pas
                    for k in list(envois)[:64]:
                        envois.pop(k, None)
            fid += 1

            if debut - t_stat >= 2.0:
                t_stat = debut
                d = debut - t0
                with verrou:
                    recents = latences[-200:]
                    acc = compteur["accuses"]
                lat = (f"{sum(recents) / len(recents) * 1000:.2f} ms"
                       if recents else "—")
                print(f"  {fid / d:6.2f} img/s   {paquets} paquets   "
                      f"{octets * 8 / d / 1e6:5.2f} Mbit/s   "
                      f"perdus {perdus}   accusés {acc}   "
                      f"aller-retour {lat}", file=sys.stderr)

            reste = periode - (time.monotonic() - debut)
            if reste > 0:
                time.sleep(reste)
    except KeyboardInterrupt:
        pass
    fini.set()
    time.sleep(0.25)

    d = max(time.monotonic() - t0, 1e-9)
    with verrou:
        acc, lats = compteur["accuses"], list(latences)
    print(f"\n  {fid} trames en {d:.1f} s = {fid / d:.2f} img/s   "
          f"{paquets} paquets   perdus {perdus}   accusés {acc}",
          file=sys.stderr)
    if lats:
        print(f"  aller-retour : min {min(lats) * 1000:.2f} ms   "
              f"moy {sum(lats) / len(lats) * 1000:.2f} ms   "
              f"max {max(lats) * 1000:.2f} ms", file=sys.stderr)


if __name__ == "__main__":
    main()
