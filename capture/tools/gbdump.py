#!/usr/bin/env python3
"""Récupère une trame du sniffer et en fait un PNG.

    ./tools/gbdump.py                          # demande une trame au firmware
    ./tools/gbdump.py --palette dmg            # les 4 verts d'origine
    ./tools/gbdump.py --echelle 4              # 640 x 576 au lieu de 160 x 144
    ./tools/gbdump.py --fichier journal.txt    # relit un vidage déjà capturé

L'outil envoie « p » sur la console USB, attend le bloc encadré par
`>>>GBDUMP` et `<<<GBDUMP`, et écrit un PNG.

⚠️ `cat /dev/ttyACM0` ne fonctionne pas : le firmware n'émet que lorsque DTR
est asserté. Ce script l'assère, comme console.py.

C'est l'instrument de la phase 2, et il a une propriété que le compteur de
trames du module écran n'avait pas : **il ne peut pas mentir**. Si l'image est
reconnaissable, la chaîne est juste.

Dépendances : pyserial, Pillow.
"""
import argparse
import re
import sys
import time

DEBUT = ">>>GBDUMP"
FIN = "<<<GBDUMP"

# 🔬 Polarité mesurée en phase 0 : « 00 » = blanc. L'indice 0 est donc le plus
# clair, et l'ordre naturel tombe juste.
PALETTES = {
    "gris": [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)],
    "dmg": [(0x9B, 0xBC, 0x0F), (0x8B, 0xAC, 0x0F),
            (0x30, 0x62, 0x30), (0x0F, 0x38, 0x0F)],
    # Chaque indice d'une couleur franche : pour la mise au point, ça rend
    # une erreur d'ordre de bits immédiatement visible.
    "diag": [(255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0)],
}


def lire_depuis_port(port, delai):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial est requis :  pip install pyserial")

    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.dtr = True
        s.rts = True
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"p")
        s.flush()

        morceaux, limite = [], time.monotonic() + delai
        while time.monotonic() < limite:
            data = s.read(8192)
            if data:
                morceaux.append(data.decode("utf-8", "replace"))
                if FIN in "".join(morceaux[-4:]):
                    break
        return "".join(morceaux)


def extraire(texte):
    """→ (largeur, hauteur, bpp, numero, octets) ou sortie en erreur."""
    i = texte.rfind(DEBUT)
    if i < 0:
        sys.exit(f"aucun « {DEBUT} » dans la sortie — le firmware a-t-il "
                 "répondu ? Essaie ./tools/console.py pour voir ce qu'il dit.")
    j = texte.find(FIN, i)
    if j < 0:
        sys.exit(f"« {DEBUT} » trouvé mais pas « {FIN} » : vidage tronqué, "
                 "rallonge --delai")

    bloc = texte[i:j]
    entete = bloc.splitlines()[0]
    m = re.match(rf"{re.escape(DEBUT)}\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", entete)
    if not m:
        sys.exit(f"en-tête illisible : {entete!r}")
    largeur, hauteur, bpp, numero = (int(x) for x in m.groups())

    hexa = "".join(bloc.splitlines()[1:]).strip()
    hexa = re.sub(r"[^0-9a-fA-F]", "", hexa)
    attendu = largeur * hauteur * bpp // 8
    if len(hexa) != attendu * 2:
        sys.exit(f"{len(hexa) // 2} octets reçus, {attendu} attendus "
                 f"({largeur}x{hauteur}, {bpp} bpp) — vidage incomplet")
    return largeur, hauteur, bpp, numero, bytes.fromhex(hexa)


def en_image(largeur, hauteur, bpp, octets, palette, echelle):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow est requis :  pip install pillow")

    if bpp != 2:
        sys.exit(f"{bpp} bits par pixel : seul IDX2 est géré")

    par_octet = 8 // bpp
    octets_ligne = largeur // par_octet
    img = Image.new("RGB", (largeur, hauteur))
    px = img.load()
    for y in range(hauteur):
        base = y * octets_ligne
        for x in range(largeur):
            # Pixel de gauche dans les bits de poids fort (§D.3).
            v = (octets[base + x // par_octet] >> (6 - 2 * (x & 3))) & 0x3
            px[x, y] = palette[v]
    if echelle > 1:
        img = img.resize((largeur * echelle, hauteur * echelle), Image.NEAREST)
    return img


def main():
    ap = argparse.ArgumentParser(
        description="Trame du sniffer → PNG. Instrument de la phase 2.")
    ap.add_argument("sortie", nargs="?", default=None,
                    help="fichier PNG (défaut : trame-<numero>.png)")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--fichier", help="relire un vidage déjà capturé")
    ap.add_argument("--palette", choices=sorted(PALETTES), default="gris")
    ap.add_argument("--echelle", type=int, default=4)
    ap.add_argument("--delai", type=float, default=8.0,
                    help="secondes d'attente du vidage (défaut : 8)")
    args = ap.parse_args()

    if args.fichier:
        with open(args.fichier, "r", encoding="utf-8", errors="replace") as f:
            texte = f.read()
    else:
        texte = lire_depuis_port(args.port, args.delai)

    largeur, hauteur, bpp, numero, octets = extraire(texte)
    img = en_image(largeur, hauteur, bpp, octets, PALETTES[args.palette],
                   args.echelle)
    chemin = args.sortie or f"trame-{numero}.png"
    img.save(chemin)

    # Répartition des 4 niveaux : une image plausible les utilise tous.
    comptes = [0, 0, 0, 0]
    for o in octets:
        for d in (6, 4, 2, 0):
            comptes[(o >> d) & 3] += 1
    total = sum(comptes)
    print(f"écrit : {chemin}  ({largeur}x{hauteur}, trame {numero}, "
          f"palette {args.palette}, x{args.echelle})")
    print("  répartition des niveaux : " + "  ".join(
        f"{i}={c * 100 / total:5.1f} %" for i, c in enumerate(comptes)))
    if comptes.count(0) >= 3:
        print("  ⚠️  un seul niveau utilisé : image uniforme, ou capture morte")


if __name__ == "__main__":
    main()
