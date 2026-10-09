#!/usr/bin/env python3
"""Mesure le début des lignes et des images sur le bus LCD, face au sniffer.

Deux défauts du sniffer, constatés le 08/10/2026 (journal du firmware) :

  1. le PREMIER pixel d'une ligne est parfois lu avec la valeur du dernier pixel
     de la ligne précédente ;
  2. une image entière est parfois décalée d'un pixel vers la gauche.

L'outil dépouille une capture des voies du bus et répond à trois questions :

  A. la forme de l'horloge pixel en début de ligne : 160 fronts entre deux ST ?
     le premier isolé, et à quelle distance de ST ?
  B. où tombe le changement de donnée du premier pixel par rapport au front
     descendant où le PIO l'échantillonne — comparé aux pixels ordinaires —, et
     combien de lignes ce front lit-il faux ;
  C. si la voie de mesure du sniffer est branchée (GP20, bascule à la fin de
     sur_vsync()) : combien de temps après le départ d'image S le PIO repart-il,
     avant ou après le premier front d'horloge de l'image ?

    ./tools/debut_ligne.py docs/releves/debut-ligne.sr
    ./tools/debut_ligne.py docs/releves/debut-ligne.sr --vsync 6

Voies attendues, celles du câblage de la phase 1 : D0 = LD0, D1 = LD1, D2 = CP,
D3 = ST, D4 = S. La question B n'a de réponse que sur les lignes où le premier
pixel diffère du dernier de la ligne précédente : choisir une scène où la
colonne 0 change d'une ligne à l'autre.

Dépendance : numpy.
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyse_sr import lire_sr  # noqa: E402

LD0, LD1, CP, ST, S = 0, 1, 2, 3, 4
PIXELS = 160
LIGNES = 144


def fronts(v, montant):
    """Indices du premier échantillon après chaque front."""
    if montant:
        return np.flatnonzero((v[:-1] == 0) & (v[1:] == 1)) + 1
    return np.flatnonzero((v[:-1] == 1) & (v[1:] == 0)) + 1


def histo(c, unite):
    return "  ".join(f"{k}{unite}×{n}" for k, n in sorted(c.items()))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("capture", help="fichier .sr")
    ap.add_argument("--vsync", type=int, metavar="VOIE",
                    help="voie de l'analyseur branchée sur GP20 du sniffer")
    args = ap.parse_args()

    cap = lire_sr(args.capture)
    ns = 1e9 / cap.taux
    ld = cap.bits(LD0) | (cap.bits(LD1) << 1)
    cp, st, s = cap.bits(CP), cap.bits(ST), cap.bits(S)
    cp_d, cp_m = fronts(cp, False), fronts(cp, True)
    st_m = fronts(st, True)
    s_m = fronts(s, True)
    chg = np.flatnonzero(ld[1:] != ld[:-1]) + 1
    print(f"{args.capture} : {cap.duree * 1000:.0f} ms à {cap.taux / 1e6:.0f} MS/s "
          f"(1 échantillon = {ns:.1f} ns), {len(st_m)} lignes, {len(s_m)} départs d'image")

    # ───────────────────────────────────────── A. l'horloge en début de ligne
    lignes = []
    for a, b in zip(st_m[:-1], st_m[1:]):
        f = cp_d[(cp_d >= a) & (cp_d < b)]
        if len(f) >= 2:
            lignes.append((a, f))
    n = Counter(len(f) for _, f in lignes)
    isole = Counter(int(f[1] - f[0]) for _, f in lignes)
    depuis_st = Counter(int(f[0] - a) for a, f in lignes)
    print("\nA. Horloge pixel en début de ligne")
    print(f"   fronts descendants entre deux ST : {histo(n, '')}")
    print(f"   ST → 1er front (éch.)            : {histo(depuis_st, '')}")
    print(f"   1er → 2e front (éch.)            : {histo(isole, '')}"
          f"   — un pixel ordinaire en est à ~{np.median(np.diff(cp_d[:2000])):.0f}")

    # ──────────────────────────── B. la donnée du premier pixel face à son front
    # Pixels ordinaires : la donnée change sur le front montant qui précède le
    # front descendant. Marge avant = front - dernier changement, après =
    # prochain changement - front.
    def marges(f):
        i = np.searchsorted(chg, f, side="right")
        avant = f - chg[i - 1] if i > 0 else None
        apres = chg[i] - f if i < len(chg) else None
        return avant, apres

    ord_av, ord_ap = Counter(), Counter()
    for _, f in lignes[:400]:
        for k in range(2, len(f) - 1):
            a, b = marges(f[k])
            if a is not None and a <= 6:
                ord_av[int(a)] += 1
            if b is not None and b <= 6:
                ord_ap[int(b)] += 1

    # Premier pixel : changements de LD entre ST et le front montant qui précède
    # le 2e front descendant (celui-là appartient au 2e pixel).
    avant_front, apres_front, faux, informatives = Counter(), Counter(), 0, 0
    for a, f in lignes:
        r1 = cp_m[np.searchsorted(cp_m, f[1]) - 1]
        dans = chg[(chg >= a) & (chg < r1 - 1)]
        if len(dans) == 0:
            continue
        informatives += 1
        for t in dans:
            d = int(t - f[0])
            (avant_front if d <= 0 else apres_front)[d] += 1
        # Ce que lit le PIO : ~13 ns après le front, soit le premier échantillon
        # bas de CP, à un échantillon près (41,7 ns à 24 MS/s).
        lu = ld[f[0]]
        etabli = ld[r1 - 2]      # la valeur établie du premier pixel, juste avant le 2e
        if lu != etabli:
            faux += 1

    print("\nB. Donnée du premier pixel face à son front d'échantillonnage")
    print(f"   pixels ordinaires — dernier changement AVANT le front (éch.) : {histo(ord_av, '')}")
    print(f"                       premier changement APRÈS le front (éch.) : {histo(ord_ap, '')}")
    if informatives == 0:
        print("   premier pixel — aucune ligne où sa donnée change : la colonne 0 de la scène "
              "est uniforme.\n   ⚠️ Pas de réponse possible : capturer une scène où la colonne 0 "
              "change d'une ligne à l'autre.")
    else:
        print(f"   premier pixel — {informatives} lignes informatives sur {len(lignes)}")
        print(f"     changement AVANT le front (éch., ≤ 0) : {histo(avant_front, '')}")
        print(f"     changement APRÈS le front (éch., > 0) : {histo(apres_front, '')}")
        print(f"     ⇒ le front lit une valeur FAUSSE sur {faux} lignes "
              f"({100 * faux / informatives:.1f} % des informatives)")

    # ─────────────────────────────────────── C. le départ d'image et le PIO
    print("\nC. Départ d'image")
    trames = Counter()
    for a, b in zip(s_m[:-1], s_m[1:]):
        trames[int(np.count_nonzero((cp_d >= a) & (cp_d < b)))] += 1
    print(f"   fronts d'horloge par image : {histo(trames, '')}   (attendu {PIXELS * LIGNES})")
    premier = [cp_d[np.searchsorted(cp_d, a)] - a for a in s_m if np.searchsorted(cp_d, a) < len(cp_d)]
    if premier:
        print(f"   S → 1er front d'horloge : {np.min(premier) * ns / 1000:.2f} à "
              f"{np.max(premier) * ns / 1000:.2f} µs")
    if args.vsync is None:
        print("   voie de GP20 non donnée (--vsync) : délai de sur_vsync() non mesuré")
        return
    v = cap.bits(args.vsync)
    bascules = np.flatnonzero(v[1:] != v[:-1]) + 1
    retards, rates = [], 0
    for a in s_m:
        j = np.searchsorted(bascules, a)
        k = np.searchsorted(cp_d, a)
        if j >= len(bascules) or k >= len(cp_d):
            continue
        r = bascules[j] - a
        retards.append(r)
        if bascules[j] > cp_d[k]:
            rates += 1
    if retards:
        r = np.array(retards) * ns / 1000
        print(f"   S → fin de sur_vsync() (GP20) : médiane {np.median(r):.2f} µs, "
              f"max {np.max(r):.2f} µs, sur {len(r)} images")
        print(f"   ⇒ PIO relancé APRÈS le premier pixel sur {rates} image(s) : "
              "chacune décalée d'un pixel")


if __name__ == "__main__":
    main()
