#!/usr/bin/env python3
"""Fabrique une capture .sr synthétique du bus LCD d'une Game Boy.

    ./tools/simuler_bus_gb.py /tmp/essai.sr
    ./tools/simuler_bus_gb.py /tmp/blanc.sr --uni 0
    ./tools/simuler_bus_gb.py /tmp/noir.sr  --uni 3

⚠️ **Ce script ne valide rien du projet.** Il sert à éprouver `analyse_sr.py`
sans avoir la console ouverte sous la main : on sait ce qu'on a mis dedans, on
vérifie que le dépouilleur le retrouve. C'est un banc de test d'outil.

Le plan (§5.6) écarte explicitement le stimulus synthétique comme moyen de
valider le **firmware** : on y validerait nos propres hypothèses de timing. Ce
qui vaut pour le firmware vaut ici — la seule chose que ce fichier prouve, c'est
que `analyse_sr.py` sait lire un .sr et dérouler son arbre de décision.

Pour éprouver le firmware, c'est la capture RÉELLE de la phase 0 qu'il faut
rejouer (§D.11).

Dépendance : numpy.
"""
import argparse
import sys
import zipfile

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("numpy est requis :  pip install numpy")

F_MAITRE = 4_194_304
CYCLES_LIGNE = 456
LIGNES_TRAME = 154
LIGNES_VISIBLES = 144
PX_PAR_LIGNE = 160

# Position des événements dans la ligne, en cycles maître (modèle du §A.1).
CYCLE_MODE3 = 80                      # fin du balayage OAM, début des pixels
CYCLE_CPL = CYCLE_MODE3 + PX_PAR_LIGNE + 12   # verrou, après le dernier pixel

VOIES = ["LD0", "LD1", "CPG", "CPL", "ST", "CP", "D6", "D7"]


def _creneaux(n, debuts, fins):
    """Signal 0/1 de longueur n, haut sur chaque intervalle [debut, fin)."""
    delta = np.zeros(n + 1, dtype=np.int32)
    d = np.clip(debuts, 0, n)
    f = np.clip(fins, 0, n)
    np.add.at(delta, d, 1)
    np.add.at(delta, f, -1)
    return (np.cumsum(delta)[:n] > 0).astype(np.uint8)


def _ech(cycles, taux):
    """Cycles maître → indice d'échantillon."""
    return np.round(np.asarray(cycles, dtype=np.float64) / F_MAITRE * taux
                    ).astype(np.int64)


CYCLES_TRAME = CYCLES_LIGNE * LIGNES_TRAME


def construire(taux, n_trames, uni=None, graine=0, decalage=7919):
    n = int(round(n_trames * CYCLES_TRAME / F_MAITRE * taux))
    # Une capture réelle ne commence ni ne finit sur une frontière de trame.
    # On décale donc l'origine, et on fabrique une trame de plus pour couvrir
    # la fenêtre : ça exerce les chemins de troncature du dépouilleur.
    n_ev = int(np.ceil(n_trames)) + 1
    trames = np.arange(n_ev)

    # ── CPG : 160 impulsions par ligne visible, une par cycle maître.
    lignes = np.arange(LIGNES_VISIBLES)
    pixels = np.arange(PX_PAR_LIGNE)
    c_px = (trames[:, None, None] * CYCLES_TRAME
            + lignes[None, :, None] * CYCLES_LIGNE
            + CYCLE_MODE3 + pixels[None, None, :]).ravel() - decalage
    i_px = _ech(c_px, taux)
    cpg = _creneaux(n, i_px, _ech(c_px + 0.5, taux))

    # ── LD0/LD1 : la valeur tient du début d'un pixel au début du suivant.
    if uni is None:
        rng = np.random.default_rng(graine)
        x = np.tile(pixels, LIGNES_VISIBLES)
        y = np.repeat(lignes, PX_PAR_LIGNE)
        motif = ((x // 8 + y // 8) % 4).astype(np.uint8)      # damier 4 niveaux
        bruit = rng.integers(0, 4, motif.size, dtype=np.uint8)
        motif = np.where(rng.random(motif.size) < 0.15, bruit, motif)
        valeurs = np.tile(motif, n_ev)
    else:
        valeurs = np.full(c_px.size, uni, dtype=np.uint8)

    fin_px = np.concatenate([i_px[1:], i_px[-1:] + 1])
    ld = []
    for bit in (0, 1):
        actif = (valeurs >> bit) & 1
        ld.append(_creneaux(n, i_px[actif == 1], fin_px[actif == 1]))

    # ── CPL : une impulsion par ligne VISIBLE — se tait donc en VBlank.
    c_cpl = (trames[:, None] * CYCLES_TRAME
             + lignes[None, :] * CYCLES_LIGNE + CYCLE_CPL).ravel() - decalage
    cpl = _creneaux(n, _ech(c_cpl, taux), _ech(c_cpl + 4, taux))

    # ── CP : une impulsion par ligne, VBlank comprise.
    toutes = np.arange(LIGNES_TRAME)
    c_cp = (trames[:, None] * CYCLES_TRAME
            + toutes[None, :] * CYCLES_LIGNE).ravel() - decalage
    cp = _creneaux(n, _ech(c_cp, taux), _ech(c_cp + 4, taux))

    # ── ST : une impulsion brève par trame.
    c_st = trames * CYCLES_TRAME - decalage
    st = _creneaux(n, _ech(c_st, taux), _ech(c_st + 8, taux))

    voies = [ld[0], ld[1], cpg, cpl, st, cp,
             np.zeros(n, np.uint8), np.zeros(n, np.uint8)]
    octets = np.zeros(n, dtype=np.uint8)
    for i, v in enumerate(voies):
        octets |= (v << i).astype(np.uint8)
    return octets


def ecrire_sr(chemin, octets, taux, noms):
    meta = ["[global]", "sigrok version=0.5.2", "",
            "[device 1]", "capturefile=logic-1",
            f"total probes={len(noms)}", f"samplerate={int(taux)} Hz",
            "total analog=0"]
    meta += [f"probe{i + 1}={nom}" for i, nom in enumerate(noms)]
    meta += ["unitsize=1", ""]
    with zipfile.ZipFile(chemin, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("version", "2")
        z.writestr("metadata", "\n".join(meta))
        z.writestr("logic-1-1", octets.tobytes())


def main():
    ap = argparse.ArgumentParser(
        description="Capture .sr synthétique du bus LCD, pour éprouver "
                    "analyse_sr.py.")
    ap.add_argument("sortie", help="fichier .sr à écrire")
    ap.add_argument("--taux", type=float, default=24e6,
                    help="échantillonnage en Hz (défaut : 24e6)")
    ap.add_argument("--trames", type=float, default=4.0,
                    help="nombre de trames à générer (défaut : 4)")
    ap.add_argument("--uni", type=int, choices=(0, 1, 2, 3), default=None,
                    help="image uniforme de cette valeur : 0 pour le blanc, "
                         "3 pour le noir (test §B.5)")
    ap.add_argument("--graine", type=int, default=0)
    ap.add_argument("--decalage", type=int, default=7919,
                    help="origine de la capture, en cycles maître dans la "
                         "trame (défaut : 7919, soit ni le début ni la fin)")
    args = ap.parse_args()

    octets = construire(args.taux, args.trames, args.uni, args.graine,
                        args.decalage)
    ecrire_sr(args.sortie, octets, args.taux, VOIES)
    print(f"écrit : {args.sortie}  "
          f"({octets.size} échantillons, {octets.size / args.taux * 1e3:.2f} ms)")
    print(f"vérité terrain : {', '.join(f'D{i}={n}' for i, n in enumerate(VOIES))}")


if __name__ == "__main__":
    main()
