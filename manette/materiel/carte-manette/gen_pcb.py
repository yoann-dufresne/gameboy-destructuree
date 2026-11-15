#!/usr/bin/env python3
"""
Écrit le circuit imprimé de la carte (carte-manette.kicad_pcb) : contour, trous, équipotentielles,
pièces reprises de YAPoco à leur place exacte, Pico 2 W, LED d'alimentation.

État au 15/11/2026 (étape C2 de carte.md) : la géométrie seulement. Les autres pièces attendent,
rangées à droite de la carte, que les relevés (C0) et le gabarit en carton (C3) fixent la place des
LED sous les membranes ; le placement et le routage viendront en C4.

Repère : vue de face de la carte (côté boutons), x vers la droite, y vers le bas (convention KiCad),
origine au coin haut-gauche du contour de YAPoco, posée en ORIGINE sur la feuille.

    python3 gen_pcb.py        (après gen_schema.py et l'export de la netlist : voir construire.sh)
"""
import json
import os
import re
import uuid

import pcbnew

import conception as K

ICI = os.path.dirname(os.path.abspath(__file__))
FPD = os.environ.get('KICAD_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
NS = uuid.UUID('c5ac4164-70c7-5821-be2d-f3a17d453d02')     # le même que gen_schema.py
PCB = os.path.join(ICI, K.PROJET + '.kicad_pcb')
ORIGINE = (100.0, 40.0)        # coin haut-gauche du contour sur la feuille A4
EPAISSEUR = 1.0                # à confirmer sur la carte d'origine (C0.2) ; YAPoco demande 1 mm

CONTOUR = json.load(open(os.path.join(ICI, 'yapoco', 'contour.json')))
PIECES = json.load(open(os.path.join(ICI, 'yapoco', 'pieces.json')))['pieces']
BORD_HAUT = 0.05               # y du bord haut du contour
BORD_DROIT = 69.4551           # x du bord droit, au-dessus des encoches
BORD_GAUCHE = 0.05


def uid(*parts):
    return str(uuid.uuid5(NS, '/'.join(str(p) for p in parts)))


def mm(v):
    return pcbnew.FromMM(float(v))


def pt(x, y):
    """Point de la carte (repère du contour) vers la feuille."""
    return pcbnew.VECTOR2I(mm(ORIGINE[0] + x), mm(ORIGINE[1] + y))


def depuis(v):
    """Point de la feuille vers le repère du contour, en mm."""
    return (round(pcbnew.ToMM(v.x) - ORIGINE[0], 4), round(pcbnew.ToMM(v.y) - ORIGINE[1], 4))


# ------------------------------------------------------------------ netlist KiCad
def lit_netlist():
    s = re.sub(r'\s+', ' ', open(os.path.join(ICI, 'build', K.PROJET + '.net'), encoding='utf-8').read())
    pads = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)', s):
        e = s.find('(net ', m.end())
        blk = s[m.start(): e if e > 0 else len(s)]
        for ref, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk):
            pads[(ref, pin)] = m.group(1).lstrip('/')
    return pads


def charge_empreinte(fpid):
    lib, nom = fpid.split(':')
    rep = os.path.join(ICI, lib + '.pretty') if lib == K.PROJET else os.path.join(FPD, lib + '.pretty')
    fp = pcbnew.FootprintLoad(rep, nom)
    if fp is None:
        raise KeyError(fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, nom))
    return fp


# ------------------------------------------------------------------ contour
def contour(b):
    for f in CONTOUR['formes']:
        s = pcbnew.PCB_SHAPE(b)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        t = f['type']
        if t == 'segment':
            s.SetShape(pcbnew.SHAPE_T_SEGMENT)
            s.SetStart(pt(*f['debut']))
            s.SetEnd(pt(*f['fin']))
        elif t == 'arc':
            s.SetShape(pcbnew.SHAPE_T_ARC)
            s.SetArcGeometry(pt(*f['debut']), pt(*f['milieu']), pt(*f['fin']))
        elif t == 'cercle':
            s.SetShape(pcbnew.SHAPE_T_CIRCLE)
            s.SetCenter(pt(*f['centre']))
            s.SetEnd(pt(f['centre'][0] + f['rayon'], f['centre'][1]))
        elif t == 'bezier':
            s.SetShape(pcbnew.SHAPE_T_BEZIER)
            s.SetStart(pt(*f['debut']))
            s.SetBezierC1(pt(*f['c1']))
            s.SetBezierC2(pt(*f['c2']))
            s.SetEnd(pt(*f['fin']))
            s.RebuildBezierToSegmentsPointsList(mm(0.01)) if hasattr(s, 'RebuildBezierToSegmentsPointsList') else None
        else:
            raise ValueError(t)
        b.Add(s)


# ------------------------------------------------------------------ placement
def place_yapoco(fp, piece):
    """Pose une empreinte reprise de YAPoco exactement à la place qu'elle y occupe."""
    fp.SetOrientationDegrees(piece['angle_face'])
    fp.SetPosition(pt(piece['x'], piece['y']))
    if piece['dos']:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)


def verifie_yapoco(fp, piece):
    """Écart le plus grand, en mm, entre nos pastilles et celles de YAPoco (comparées par position)."""
    nous = sorted(depuis(pd.GetPosition()) for pd in fp.Pads())
    eux = sorted((x, y) for _, x, y in piece['pastilles'])
    assert len(nous) == len(eux), (fp.GetReference(), len(nous), len(eux))
    return max(max(abs(a[0] - b[0]), abs(a[1] - b[1])) for a, b in zip(nous, eux))


def centre_tetons(fp):
    """Centre des deux tétons traversants d'une molette (pastilles sans numéro, percées)."""
    t = [depuis(pd.GetPosition()) for pd in fp.Pads()
         if pd.GetNumber() == '' and pd.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    assert len(t) == 2, t
    return ((t[0][0] + t[1][0]) / 2, (t[0][1] + t[1][1]) / 2)


def place_molette_droite(fp):
    """La molette de droite : la même pièce que celle de gauche, tournée d'un demi-tour, ses tétons
    à la même distance du bord droit que ceux de la gauche du bord gauche, centrés sur la fenêtre
    de droite (centre des tétons de l'interrupteur à molette J1 de YAPoco). Provisoire : C0.4."""
    rv1 = PIECES['RV1']
    fp.SetOrientationDegrees(rv1['angle_face'] + 180)
    fp.SetPosition(pt(0, 0))
    fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    gauche = [(x, y) for n, x, y in rv1['pastilles']]      # tétons de RV1 : les deux plus à gauche
    x_teton_g = min(x for x, _ in gauche)
    cible = (BORD_DROIT - (x_teton_g - BORD_GAUCHE), PIECES['RV2_cible']['y_tetons'])
    cx, cy = centre_tetons(fp)
    fp.Move(pcbnew.VECTOR2I(mm(cible[0] - cx), mm(cible[1] - cy)))
    return cible


def place_pico(fp):
    """Debout, antenne en haut : le bout du module (51 mm) au ras du bord haut, centré en largeur."""
    fp.SetOrientationDegrees(180)
    fp.SetPosition(pt(BORD_DROIT / 2, BORD_HAUT + 25.5))


def construit():
    b = pcbnew.BOARD()
    ds = b.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.SetBoardThickness(mm(EPAISSEUR))
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_MinClearance = mm(0.15)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleClearance = mm(0.25)
    ds.m_HoleToHoleMin = mm(0.3)
    ds.m_MinSilkTextHeight = mm(0.8)
    ds.m_MinSilkTextThickness = mm(0.15)

    pads = lit_netlist()
    nets = {}
    for n in sorted(set(pads.values())):
        ni = pcbnew.NETINFO_ITEM(b, n)
        b.Add(ni)
        nets[n] = ni

    contour(b)
    ecarts = {}
    parking = 0
    for cp in K.COMPOSANTS:
        if not cp.footprint:
            continue
        fp = charge_empreinte(cp.footprint)
        fp.SetReference(cp.ref)
        fp.SetValue(cp.value)
        fp.SetPath(pcbnew.KIID_PATH('/' + uid('sym', cp.ref)))
        if cp.dnp:
            fp.SetDNP(True) if hasattr(fp, 'SetDNP') else None
        b.Add(fp)
        if cp.ref in PIECES:
            place_yapoco(fp, PIECES[cp.ref])
            ecarts[cp.ref] = verifie_yapoco(fp, PIECES[cp.ref])
        elif cp.ref == 'RV2':
            place_molette_droite(fp)
        elif cp.ref == 'U1':
            place_pico(fp)
        elif cp.ref == 'D1':
            c = PIECES['LED_ALIM_cible']
            fp.SetPosition(pt(c['x'], c['y']))
        else:
            # en attente du placement (C4) : rangées hors de la feuille, à droite
            col, lig = divmod(parking, 14)
            fp.SetPosition(pt(210 + 9 * col, 4 + 8 * lig))
            parking += 1
        for pad in fp.Pads():
            n = pads.get((cp.ref, pad.GetNumber()))
            if n:
                pad.SetNet(nets[n])

    # sérigraphie du dos : ce qu'est la carte, et d'où vient sa géométrie
    for i, ligne in enumerate([f'MANETTE GAME BOY POCKET  v{K.REVISION}',
                               'CC BY-SA 4.0 - geometrie : YAPoco, Natalie the Nerd']):
        t = pcbnew.PCB_TEXT(b)
        t.SetText(ligne)
        t.SetLayer(pcbnew.B_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
        t.SetTextThickness(mm(0.15))
        t.SetMirrored(True)
        t.SetPosition(pt(BORD_DROIT / 2, 56.5 + 1.6 * i))
        b.Add(t)

    b.Save(PCB)
    pire = max(ecarts.values())
    print(f'carte écrite : {os.path.basename(PCB)} ; {len(ecarts)} pièces de YAPoco, écart le plus grand '
          f'{pire:.4f} mm ; {parking} pièces en attente de placement')
    assert pire < 0.01, ecarts


if __name__ == '__main__':
    construit()
