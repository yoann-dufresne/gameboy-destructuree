#!/usr/bin/env python3
"""
Reprend de YAPoco ce dont la carte de la manette a besoin, une fois pour toutes :

  yapoco/contour.json        le contour de la carte et ses trous (Edge.Cuts), origine au coin haut-gauche
  yapoco/pieces.json         la place des pièces reprises : boutons, piles, interrupteur, molettes, LED
  carte-manette.pretty/      les empreintes reprises, renommées YAPoco_*

YAPoco ("Yet Another Pocket Color") est une carte de Game Boy Color au format de la Game Boy Pocket,
de Natalie the Nerd, sous licence CC BY-SA 4.0 : https://github.com/nataliethenerd/YAPoco. Ce qui en
est tiré garde cette licence, et la carte de la manette avec (yapoco/PROVENANCE.txt).

    python3 extrait_yapoco.py chemin/vers/YAPoco.kicad_pcb

Il n'a pas à être relancé : ses sorties sont dans le dépôt. Il sert à refaire l'extraction si YAPoco
change, ou à vérifier ce qui a été pris.
"""
import json
import os
import sys

import pcbnew

ICI = os.path.dirname(os.path.abspath(__file__))
SORTIE = os.path.join(ICI, 'yapoco')
BIBLIO = os.path.join(ICI, 'carte-manette.pretty')
SOURCE = 'YAPoco (Natalie the Nerd), CC BY-SA 4.0, https://github.com/nataliethenerd/YAPoco'

# Empreinte de YAPoco -> nom dans la bibliothèque de la carte, et l'instance qui sert de modèle.
EMPREINTES = {
    'YAPoco_Croix': 'SW2',
    'YAPoco_AB': 'SW6',
    'YAPoco_Select': 'SW8',
    'YAPoco_Start': 'SW9',
    'YAPoco_Piles': 'BT1',
    'YAPoco_Interrupteur': 'SW1',
    'YAPoco_Molette': 'VR1',
}
# Les pièces dont la place est reprise : référence de YAPoco -> (référence de la manette, empreinte).
PIECES = {
    'SW2': ('SW2', 'YAPoco_Croix'), 'SW3': ('SW3', 'YAPoco_Croix'), 'SW4': ('SW4', 'YAPoco_Croix'),
    'SW5': ('SW5', 'YAPoco_Croix'), 'SW6': ('SW6', 'YAPoco_AB'), 'SW7': ('SW7', 'YAPoco_AB'),
    'SW8': ('SW8', 'YAPoco_Select'), 'SW9': ('SW9', 'YAPoco_Start'), 'BT1': ('BT1', 'YAPoco_Piles'),
    'SW1': ('SW1', 'YAPoco_Interrupteur'), 'VR1': ('RV1', 'YAPoco_Molette'),
}
# L'interrupteur de YAPoco (une pièce de Game Boy Color) : C est le commun, 1 la position marche,
# 3 l'arrêt, 2 ne sert pas. Renuméroté pour le symbole SW_SPDT : 1 marche, 2 commun, 3 arrêt.
RENOMME_PASTILLES = {'YAPoco_Interrupteur': {'C': '2', '2': ''}}
# Contours de pièces que la manette n'a pas, mais dont YAPoco a découpé la carte : les prises DC et
# casque. Les découpes sont gardées, la coque n'a jamais rien eu à cet endroit.
DECOUPES = {'P4', 'P5'}


def mm(v):
    return round(pcbnew.ToMM(v), 4)


def forme(s, ox, oy):
    """Une forme Edge.Cuts en dictionnaire, coordonnées relatives à (ox, oy)."""
    p = lambda v: [mm(v.x - ox), mm(v.y - oy)]
    t = s.GetShape()
    if t == pcbnew.SHAPE_T_SEGMENT:
        return {'type': 'segment', 'debut': p(s.GetStart()), 'fin': p(s.GetEnd())}
    if t == pcbnew.SHAPE_T_ARC:
        return {'type': 'arc', 'debut': p(s.GetStart()), 'milieu': p(s.GetArcMid()), 'fin': p(s.GetEnd())}
    if t == pcbnew.SHAPE_T_CIRCLE:
        return {'type': 'cercle', 'centre': p(s.GetCenter()), 'rayon': mm(s.GetRadius())}
    if t == pcbnew.SHAPE_T_BEZIER:
        return {'type': 'bezier', 'debut': p(s.GetStart()), 'c1': p(s.GetBezierC1()),
                'c2': p(s.GetBezierC2()), 'fin': p(s.GetEnd())}
    if t == pcbnew.SHAPE_T_RECTANGLE if hasattr(pcbnew, 'SHAPE_T_RECTANGLE') else pcbnew.SHAPE_T_RECT:
        return {'type': 'rectangle', 'debut': p(s.GetStart()), 'fin': p(s.GetEnd())}
    raise ValueError(f'forme non prise en charge : {s.GetShapeStr()}')


def pastilles(fp, ox, oy):
    return [[pd.GetNumber(), mm(pd.GetPosition().x - ox), mm(pd.GetPosition().y - oy)] for pd in fp.Pads()]


def main(chemin):
    b = pcbnew.LoadBoard(chemin)
    bb = b.GetBoardEdgesBoundingBox()
    ox, oy = bb.GetLeft(), bb.GetTop()
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}
    os.makedirs(SORTIE, exist_ok=True)
    os.makedirs(BIBLIO, exist_ok=True)

    # --- le contour, et les découpes des prises
    formes = [forme(d, ox, oy) for d in b.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]
    for ref in sorted(DECOUPES):
        formes += [forme(g, ox, oy) for g in fps[ref].GraphicalItems()
                   if g.GetLayer() == pcbnew.Edge_Cuts]
    json.dump({'source': SOURCE, 'largeur': mm(bb.GetWidth()), 'hauteur': mm(bb.GetHeight()),
               'formes': formes}, open(os.path.join(SORTIE, 'contour.json'), 'w'), indent=1)

    # --- les empreintes, ramenées à leur définition de bibliothèque (face avant, angle nul)
    retournees = {}
    for nom, ref in EMPREINTES.items():
        fp = pcbnew.FOOTPRINT(fps[ref])
        if fp.IsFlipped():
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        retournees[nom] = fp.GetOrientationDegrees()
        fp.SetOrientationDegrees(0)
        fp.SetPosition(pcbnew.VECTOR2I(0, 0))
        for pd in fp.Pads():
            n = RENOMME_PASTILLES.get(nom, {}).get(pd.GetNumber())
            if n is not None:
                pd.SetNumber(n)
            pd.SetNetCode(0)
        fp.Models().clear()
        fp.SetFPID(pcbnew.LIB_ID('carte-manette', nom))
        fp.SetReference('REF**')
        fp.SetValue(nom)
        fp.SetLibDescription(f'{SOURCE}, empreinte \"{fps[ref].GetFPID().GetLibItemName()}\"')
        chemin_fp = os.path.join(BIBLIO, nom + '.kicad_mod')
        if os.path.exists(chemin_fp):
            os.remove(chemin_fp)
        pcbnew.PCB_IO_KICAD_SEXPR().FootprintSave(BIBLIO, fp)

    # --- la place des pièces : pour regénérer la carte, et pour la vérifier pastille par pastille
    pieces = {}
    for ref_y, (ref, nom) in PIECES.items():
        fp = fps[ref_y]
        copie = pcbnew.FOOTPRINT(fp)
        if copie.IsFlipped():
            copie.Flip(copie.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        pieces[ref] = {'yapoco': ref_y, 'empreinte': nom, 'dos': fp.IsFlipped(),
                       'x': mm(fp.GetPosition().x - ox), 'y': mm(fp.GetPosition().y - oy),
                       'angle_face': copie.GetOrientationDegrees(),
                       'pastilles': pastilles(fp, ox, oy)}
    # La molette de droite : YAPoco y met un interrupteur à molette (J1) ; on relève le centre de ses
    # deux tétons, sur lequel se centre la seconde molette. La LED d'alimentation : son centre.
    j1 = [pd for pd in fps['J1'].Pads() if pd.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH]
    pieces['RV2_cible'] = {'yapoco': 'J1', 'dos': fps['J1'].IsFlipped(),
                           'x_tetons': mm(sum(pd.GetPosition().x for pd in j1) / len(j1) - ox),
                           'y_tetons': mm(sum(pd.GetPosition().y for pd in j1) / len(j1) - oy)}
    d4 = list(fps['D4'].Pads())
    pieces['LED_ALIM_cible'] = {'yapoco': 'D4', 'diametre': 3.0,
                                'x': mm(sum(pd.GetPosition().x for pd in d4) / len(d4) - ox),
                                'y': mm(sum(pd.GetPosition().y for pd in d4) / len(d4) - oy)}
    json.dump({'source': SOURCE, 'pieces': pieces}, open(os.path.join(SORTIE, 'pieces.json'), 'w'), indent=1)
    print(f'{len(formes)} formes de contour, {len(EMPREINTES)} empreintes, {len(pieces)} places')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
