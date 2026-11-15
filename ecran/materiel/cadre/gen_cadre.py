#!/usr/bin/env python3
"""
Cadre arrière de l'écran 3×3 — profilés aluminium 2020 et pièces imprimées (v4).

Ce qui change par rapport au cadre v3 tout imprimé (OneDrive, 21/09/2026) : les dalles ne se tiennent
plus entre elles par des plaques de PLA ; chaque rangée est vissée sur un profilé 2020, et les trois
profilés de rangée sur deux montants 2020. L'aluminium porte le poids, le plastique ne fait que relier.

Repère d'assemblage : vue de DOS, x vers la droite, y vers le haut, z vers l'arrière (hors des dalles).
z = 0 au plan d'appui du dos des dalles ; face avant des LED en z = -14,5. Dalle (i, j), i et j dans
{-1, 0, 1}, centrée en (192 i, 192 j) ; numérotation du cadre v3 : 7 8 9 en haut, 4 5 6, 1 2 3.

Cotes des dalles (inserts M3, ergots, connecteurs) : celles du générateur v3, mesurées par Yoann.

Sorties (dans ce dossier) :
  stl/*.stl               pièces à imprimer, dans leur orientation d'impression
  cadre.glb               l'assemblage complet, à ouvrir dans la Visionneuse 3D de Windows ou Blender
  ../docs/cadre_*.png     vues d'assemblage et plan coté
  debit.md                débit des profilés, quincaillerie, pièces imprimées
  faisceau.md             fiche de coupe des câbles, d'après leur cheminement calculé

    python3 gen_cadre.py            (contrôle les interférences avant d'exporter)
"""
import json
import math
import os
import sys

import numpy as np
import trimesh
from shapely import affinity
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

ICI = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(ICI, '..', 'docs')
STL = os.path.join(ICI, 'stl')

# ===================================================================================== PARAMÈTRES
P = dict(
    PAS=192.0, COQUE=190.0, EP_DALLE=14.5,
    # inserts M3 de la dalle (repère local, centre de la dalle) : cotes du cadre v3
    INS_X=86.75, INS_Y=78.75, TM_X=7.6, TM_Y=86.75,
    ERGOT=(63.0, 88.0), ERGOT_D=8.0,
    ERGOT_H=8.0,                       # hauteur d'un ergot au-dessus du dos : non mesurée, majorant
    # profilés
    PROF=20.0, Y_PROF=-40.0,           # profilé de rangée : axe à 40 mm sous le centre des dalles
    L_RANGEE=570.0,
    # montants : dans la bande libre entre l'entrée HUB75 et l'alim des dalles de bord (à 22 mm à
    # gauche de leur centre), ~20 mm de chaque connecteur ; du profilé du bas à au-dessus de celui du haut
    X_MONTANTS=(-214.0, 170.0), Y_MONTANT=(-242.0, 188.0),
    # brides de rangée
    EP_SEMELLE=6.0, JOUE=4.8, JEU=0.2, H_JOUE=18.0,
    M3_PASSAGE=3.4, M3_FRAISURE=6.6, M5_PASSAGE=5.5, M5_TETE=9.0,
    M5_DK=8.5, M5_K=5.0,               # tête des vis M5 du cadre : DIN 912 / ISO 4762, dessous plat
    # rainure B 6 des profilés Motedis : écrou marteau M5 de 3,2 mm de haut, collerette comprise
    # (plan S6BHASNM5) ; fond de la rainure, le noyau, à 5,5 mm de la face (modèle STEP du profilé)
    ECROU_H=3.2, FOND_RAINURE=5.5,
    # liaison de terre : cosse à œillet M5 (0,8 mm) sur rondelle à dents DIN 6798 A M5 (0,6 mm)
    EP_TERRE=1.4,
    # alimentation, cotes relevées par Yoann le 29/10/2026 ; berceaux et sangles en découlent
    # hauteur reprise le 08/11/2026 : 43 mm exactement, au lieu des 45 relevés d'abord
    ALIM_L=199.0, ALIM_W=98.0, ALIM_H=43.0, ALIM_X0=-44.0,   # sur le profilé du bas, bornier à gauche
    SEMELLE_ALIM=7.5, EP_SANGLE=4.0,   # la tête de la vis M5 de chaque berceau se noie dans sa semelle
    # jeu de l'alim entre les joues, de chaque côté (2 mm à l'origine : 4 mm de trop, constaté le
    # 08/11/2026) ; les joues s'arrêtent SERRAGE_ALIM sous le dessus de l'alim, la sangle la plaque
    JEU_ALIM=0.3, SERRAGE_ALIM=0.3,
    # trous taraudés M3 du flanc de l'alim tourné vers le bas de l'écran (y < 0) : (x depuis son bout
    # gauche, côté bornier ; hauteur au-dessus de son dessous), mesurés le 12/11/2026. Une vis M3
    # fraisée par trou, à travers la joue : la sangle seule ne suffisait pas, l'alim glissait dans ses
    # berceaux
    ALIM_TROUS=((22.5, 21.0), (178.0, 29.0), (178.0, 11.0)),
    # berceaux de 24 mm : (milieu en x depuis le bout gauche de l'alim, avec sangle). Chacun se centre
    # sur ses trous en glissant sur le profilé ; son milieu porte la vis M5 et la sangle. Le gauche n'a
    # pas de sangle : centrée sur son trou, elle couvrirait le bornier. Sa vis M3 tient l'alim
    BERCEAUX=((22.5, False), (178.0, True)),
    M3_VIS_ALIM=8,                     # longueur des vis M3 fraisées des joues : 3 mm dans l'alim
    # carte : coin haut-gauche (x, y) ; taille, trous et connecteurs lus dans ../carte/fixation.json
    CARTE_XY=(-200.0, 25.0),
    X_PIEDS=250.0, X_CROCHETS=120.0, MARGE_ACCES=8.0,
    # plan arrière : le mur quand l'écran est accroché, le sol quand il est posé à plat, LED en haut.
    # Les patins arrière, sur les montants à ces y, y appuient ; les pieds servent sur une table.
    Z_MUR=105.0, Y_PATINS=(-164.0, 176.0), PATIN_L=24.0, GARDE_SOL=25.0, INCLINAISON=10.0,
    EP_PATIN=3.0,                      # patins TPU collés sous les patins arrière et les pieds
)
PAS, H = P['PAS'], P['COQUE'] / 2
EP = P['EP_SEMELLE']
Z_RANGEE = (EP, EP + P['PROF'])              # profilés de rangée
Z_MONTANT = (EP + P['PROF'], EP + 2 * P['PROF'])
ROUGE, BLEU, VERT, JAUNE, GRIS, NOIR = '#d9534f', '#4a90d9', '#5cb85c', '#f0ad4e', '#9aa4ad', '#2b2b2b'
FIX = json.load(open(os.path.join(ICI, '..', 'carte', 'fixation.json')))
CARTE = (P['CARTE_XY'][0], P['CARTE_XY'][1], FIX['largeur'], FIX['hauteur'])
Y_BAS = -PAS + P['Y_PROF']                   # axe du profilé de la rangée du bas
Y_HAUT = PAS + P['Y_PROF']


def vers_assemblage(bx, by):
    """Coordonnées carte (KiCad, y vers le bas) -> assemblage (vue de dos, y vers le haut)."""
    return CARTE[0] + bx, CARTE[1] - by


# ===================================================================================== DALLES
DALLES = {}
n = 1
for j in (-1, 0, 1):
    for i in (-1, 0, 1):
        DALLES[n] = (i * PAS, j * PAS)
        n += 1
NUM_RANGEE = {1: (7, 8, 9), 0: (4, 5, 6), -1: (1, 2, 3)}


def inserts(cx, cy):
    a, b = P['INS_X'], P['INS_Y']
    return {'TL': (cx - a, cy + b), 'TR': (cx + a, cy + b), 'LM': (cx - a, cy), 'RM': (cx + a, cy),
            'BL': (cx - a, cy - b), 'BR': (cx + a, cy - b),
            'TM': (cx + P['TM_X'], cy + P['TM_Y']), 'BM': (cx - P['TM_X'], cy - P['TM_Y'])}


def ergots(cx, cy):
    ex, ey = P['ERGOT']
    return [(cx + ex, cy + ey), (cx - ex, cy - ey)]


# connecteurs du dos de chaque dalle (entrée, alim, sortie), marge de 2 mm : à ne jamais couvrir
CONNECTEURS = {'entrée HUB75': (-64.0, 37.5, -53.0, 66.5), 'alim VH': (8.5, 42.5, 21.5, 62.0),
               'sortie HUB75': (47.5, 39.5, 59.5, 66.5)}


def zones_connecteurs():
    res = []
    for num, (cx, cy) in DALLES.items():
        for nom, (x0, y0, x1, y1) in CONNECTEURS.items():
            res.append((f'dalle {num} {nom}', box(cx + x0 - 2, cy + y0 - 2, cx + x1 + 2, cy + y1 + 2)))
    return res


# ===================================================================================== OUTILS 3D
def extrude(poly, h, z0=0.0):
    m = trimesh.creation.extrude_polygon(poly, h)
    m.apply_translation([0, 0, z0])
    return m


def cyl(r, h, x, y, z0=0.0, axe='z', sections=48):
    m = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    m.apply_translation([0, 0, h / 2])
    if axe == 'x':
        m.apply_transform(trimesh.transformations.rotation_matrix(math.pi / 2, [0, 1, 0]))
    elif axe == 'y':
        m.apply_transform(trimesh.transformations.rotation_matrix(-math.pi / 2, [1, 0, 0]))
    m.apply_translation([x, y, z0])
    return m


def cone(r0, r1, h, x, y, z0):
    """Tronc de cône de rayon r0 en z0 à r1 en z0 + h (fraisure)."""
    sec = 48
    ang = np.linspace(0, 2 * np.pi, sec, endpoint=False)
    bas = np.c_[x + r0 * np.cos(ang), y + r0 * np.sin(ang), np.full(sec, z0)]
    haut = np.c_[x + r1 * np.cos(ang), y + r1 * np.sin(ang), np.full(sec, z0 + h)]
    return trimesh.convex.convex_hull(np.vstack([bas, haut]))


def fraisure_m3(x, y, z_haut, ep, oblong=0.0):
    """Perçage M3 à tête fraisée 90°, tête affleurante en z_haut ; oblong le long de x."""
    r = P['M3_PASSAGE'] / 2
    rf = P['M3_FRAISURE'] / 2
    pf = rf - r
    pieces = []
    for dx in ((-oblong, oblong) if oblong else (0.0,)):
        pieces.append(cyl(r, ep + 2, x + dx, y, z_haut - ep - 1))
        pieces.append(cone(r, rf + 0.5, pf + 0.5, x + dx, y, z_haut - pf))
    if oblong:
        pieces.append(extrude(LineString([(x - oblong, y), (x + oblong, y)]).buffer(r), ep + 2, z_haut - ep - 1))
        bas = LineString([(x - oblong, y), (x + oblong, y)]).buffer(r)
        haut = LineString([(x - oblong, y), (x + oblong, y)]).buffer(rf + 0.5)
        pts = [(px, py, z_haut - pf) for px, py in bas.exterior.coords] + \
              [(px, py, z_haut + 0.5) for px, py in haut.exterior.coords]
        pieces.append(trimesh.convex.convex_hull(np.array(pts)))
    return union(pieces)


def union(ms):
    ms = [m for m in ms if m is not None]
    return trimesh.boolean.union(ms, engine='manifold') if len(ms) > 1 else ms[0]


def moins(a, bs):
    bs = [b for b in bs if b is not None]
    return trimesh.boolean.difference([a] + bs, engine='manifold') if bs else a


def arrondi(poly, r):
    return poly.buffer(-r, join_style=1).buffer(r, join_style=1)


def place(m, tx=0, ty=0, tz=0, rx=0, ry=0, rz=0, miroir_x=False):
    m = m.copy()
    if miroir_x:
        m.apply_transform(np.diag([-1, 1, 1, 1]))     # trimesh retourne lui-même les faces
    for ang, axe in ((rx, [1, 0, 0]), (ry, [0, 1, 0]), (rz, [0, 0, 1])):
        if ang:
            m.apply_transform(trimesh.transformations.rotation_matrix(math.radians(ang), axe))
    m.apply_translation([tx, ty, tz])
    return m


# ===================================================================================== PIÈCES
# Brides longues (08/11/2026) : celles des rangées du haut et du milieu descendent par-dessus le joint
# horizontal jusqu'aux inserts TL/TR de la rangée du dessous. Les 4 inserts de chaque jointure de
# 4 dalles (2 au bord de l'écran) sont alors sur une même pièce, qui aligne les rangées entre elles.
# Celles de la rangée du bas, qui n'a rien dessous, restent courtes.
def bride_longue(j):
    return j > -1


def trous_bride(longue):
    """Ordonnées des inserts d'une bride, repère de sa rangée : LM/RM et BL/BR de sa rangée, plus, pour
    une bride longue, TL/TR de la rangée du dessous (y = -113,25)."""
    return (0.0, -P['INS_Y']) + ((-(PAS - P['INS_Y']),) if longue else ())


def bas_bride(longue):
    """Bas de la semelle d'une bride, repère de sa rangée : 9,25 mm sous BL/BR, ou 8 mm sous les TL/TR
    du dessous ; le haut est toujours à 8 mm au-dessus de LM/RM."""
    return trous_bride(longue)[-1] - 8.0 if longue else -88.0


def bride_interieure(longue=False):
    """Bride de joint vertical : relie deux dalles voisines (LM+BL d'une, RM+BR de l'autre) au profilé
    de leur rangée ; longue, aussi TR et TL des deux dalles du dessous. Repère : milieu du joint, centre
    de rangée en y = 0 ; posée semelle sur le plateau."""
    ys = P['Y_PROF']
    semelle = arrondi(box(-20, bas_bride(longue), 20, 8), 3)
    corps = extrude(semelle, EP)
    joues = []
    for y0, y1 in ((ys - 10 - P['JEU'] - P['JOUE'], ys - 10 - P['JEU']), (ys + 10 + P['JEU'], ys + 10 + P['JEU'] + P['JOUE'])):
        joues.append(extrude(arrondi(box(-20, y0, 20, y1), 1.0), P['H_JOUE'], EP - 0.01))
    m = union([corps] + joues)
    trous = []
    for x in (-(P['PAS'] / 2 - P['INS_X']), P['PAS'] / 2 - P['INS_X']):
        for y in trous_bride(longue):
            trous.append(fraisure_m3(x, y, EP, EP))
    zt = EP + P['PROF'] / 2
    trous.append(cyl(P['M5_PASSAGE'] / 2, 60, 0, ys - 30, zt, axe='y'))
    return moins(m, trous)


def bride_bord(longue=False):
    """Bride de bord droit (la gauche est son miroir) : la dalle de bord, LM/RM et BL/BR, au profilé ;
    longue, aussi TL/TR de la dalle du dessous. Repère : bord de l'écran en x = 0 (coque à x = -1),
    centre de rangée en y = 0."""
    ys = P['Y_PROF']
    xi = -(H - P['INS_X']) - 1.0                       # insert à 8,25 mm du bord de coque (x = -9,25)
    x0, x1 = -23.0, -1.5
    corps = extrude(arrondi(box(x0, bas_bride(longue), x1, 8), 3), EP)
    joues = []
    for y0, y1 in ((ys - 10 - P['JEU'] - P['JOUE'], ys - 10 - P['JEU']), (ys + 10 + P['JEU'], ys + 10 + P['JEU'] + P['JOUE'])):
        joues.append(extrude(arrondi(box(x0, y0, x1, y1), 1.0), P['H_JOUE'], EP - 0.01))
    m = union([corps] + joues)
    trous = [fraisure_m3(xi, y, EP, EP) for y in trous_bride(longue)]
    trous.append(cyl(P['M5_PASSAGE'] / 2, 60, (x0 + x1) / 2, ys - 30, EP + P['PROF'] / 2, axe='y'))
    return moins(m, trous), xi


def agrafe():
    """Agrafe de joint horizontal : TM de la dalle du dessous et BM de celle du dessus. Trous oblongs
    de ±1 mm le long du joint (position de TM/BM connue à ±0,6 mm). Repère : point de joint."""
    ep = 5.0
    corps = extrude(arrondi(box(-16, -16, 16, 16), 4), ep)
    d = P['PAS'] / 2 - P['TM_Y']
    trous = [fraisure_m3(P['TM_X'], -d, ep, ep, oblong=1.0), fraisure_m3(-P['TM_X'], d, ep, ep, oblong=1.0)]
    return moins(corps, trous)


def gabarit_bride():
    """Gabarit d'essai : la semelle de la bride intérieure longue en 1,2 mm, trous seuls. À imprimer
    d'abord, à poser sur une jointure de 4 dalles."""
    corps = extrude(arrondi(box(-20, bas_bride(True), 20, 8), 3), 1.2)
    trous = []
    for x in (-(P['PAS'] / 2 - P['INS_X']), P['PAS'] / 2 - P['INS_X']):
        for y in trous_bride(True):
            trous.append(cyl(P['M3_PASSAGE'] / 2, 4, x, y, -1))
    return moins(corps, trous)


def trous_carte():
    """Trous de fixation de la carte, repère d'assemblage (lus dans ../carte/fixation.json)."""
    return [vers_assemblage(bx, by) for bx, by in FIX['trous']]


def plateau_carte():
    """Plateau qui porte la carte, vissé sur le profilé de la rangée du milieu (3 vis M5 dans sa
    rainure arrière), une patte appuyée contre le montant gauche, sous l'embase HUB75 du bas. Repère
    d'assemblage ; semelle en z = 26."""
    x0, y0, l, h = CARTE
    z0 = Z_RANGEE[1]
    ep = 4.0
    trous = trous_carte()
    pourtour = box(x0, y0 - h, x0 + l, y0).difference(box(x0 + 7, y0 - h + 7, x0 + l - 7, y0 - 7))
    yp = P['Y_PROF']
    epine = box(x0, yp - 12, x0 + l, yp + 12)
    lignes = [LineString([trous[i], trous[j]]) for i, j in ((0, 3), (1, 4), (2, 5), (3, 4), (4, 5))]
    lignes += [LineString([(x, yp), t]) for t in trous for x in (t[0],)]
    plan = unary_union([pourtour, epine] + [li.buffer(4) for li in lignes] + [Point(t).buffer(7) for t in trous])
    plan = plan.intersection(box(x0, y0 - h, x0 + l, y0))
    m = extrude(plan, ep, z0)
    plots = [cyl(4.0, 6.0, x, y, z0 + ep - 0.01) for x, y in trous]
    xm = P['X_MONTANTS'][0] + P['PROF'] / 2
    yb0, yb1 = y0 - h + 4, y0 - h + 22
    patte = union([extrude(box(xm, yb0, x0 + 8, yb1), ep, z0),
                   extrude(box(xm, yb0, xm + 3.4, yb1), P['PROF'], z0)])     # paroi contre le montant
    m = union([m] + plots + [patte])
    perc = [cyl(1.35, 12, x, y, z0 + 2) for x, y in trous]              # vis M3 auto-taraudeuses
    perc += [cyl(P['M5_PASSAGE'] / 2, 20, x, yp, z0 - 5) for x in (x0 + 22, x0 + l / 2, x0 + l - 22)]
    # trou de la patte, laissé vide : une tête de vis M5 y toucherait le bord de la carte. Gardé pour
    # que le fichier reste celui des plateaux déjà imprimés
    perc += [cyl(P['M5_PASSAGE'] / 2, 30, xm - 5, (yb0 + yb1) / 2, z0 + P['PROF'] / 2, axe='x')]
    return moins(m, perc)


def uvw(coupe, ep, T):
    """Extrude une coupe 2D (u, v) sur `ep` selon w, puis applique T : (u, v, w) -> (x, y, z)."""
    m = extrude(coupe, ep)
    m.apply_transform(np.array(T, float))
    return m


L_BERCEAU = 24.0       # longueur d'un berceau le long du profilé, celle de la sangle
OBLONG_ALIM = 1.0      # trous M3 des joues oblongs de ±1 mm en hauteur : le berceau glisse sur le profilé
                       # pour se mettre face aux trous, mais rien ne rattrape une erreur sur leur hauteur
PROF_M4 = 8.5          # trous des vis M4 × 12 de la sangle : 7,7 mm de vis dans la joue, sous 4,3 mm de sangle


def trous_alim(k):
    """Trous M3 de l'alim que couvre le berceau k : (x depuis son milieu, hauteur au-dessus de sa semelle)."""
    xc, _ = P['BERCEAUX'][k]
    return [(hx - xc, hz) for hx, hz in P['ALIM_TROUS'] if abs(hx - xc) <= L_BERCEAU / 2]


def berceau_alim(k):
    """Berceau de l'alimentation, k = 0 le gauche (côté bornier), 1 le droit : chevauche le profilé de
    la rangée du bas (encoche), deux joues tiennent l'alim par les flancs à JEU_ALIM près. Des vis M3
    fraisées à travers la joue côté bas de l'écran la vissent par les trous taraudés de son flanc. Le
    droit porte aussi une sangle vissée par-dessus, qui la plaque : les joues s'arrêtent SERRAGE_ALIM
    sous le dessus de l'alim. La tête de sa vis M5 se noie dans la semelle : l'alim repose sur la
    semelle, pas sur la vis. Repère : milieu du berceau (vis M5, sangle) sur l'axe du profilé ; x le
    long du profilé, z vers l'arrière."""
    Hh = P['ALIM_H']
    _, avec_sangle = P['BERCEAUX'][k]
    a, b = -L_BERCEAU / 2, L_BERCEAU / 2
    yb0, yb1 = ailes_alim()
    zb = Z_RANGEE[1]
    es = P['SEMELLE_ALIM']
    zh = zb + es + Hh - P['SERRAGE_ALIM']                    # haut des joues
    coupe = unary_union([
        box(yb0, zb - 10, yb1, zb + es),                      # semelle épaisse, descend de 10 mm
        box(yb0, zb + es - 0.01, yb0 + 5, zh),                # joues
        box(yb1 - 5, zb + es - 0.01, yb1, zh),
    ]).difference(box(-10 - P['JEU'], zb - 11, 10 + P['JEU'], zb))   # encoche du profilé
    # (u = y, v = z, w) -> (x = w + a, y = u, z = v)
    m = uvw(coupe, b - a, [[0, 0, 1, a], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]])
    perc = [cyl(P['M5_PASSAGE'] / 2, 30, 0, 0, zb - 5),              # vis M5 dans la rainure arrière
            cyl(P['M5_TETE'] / 2 + 0.25, 10, 0, 0, zb + es - P['M5_K'] - 0.5)]   # et le lamage de sa tête
    if avec_sangle:                                           # vis M4 autotaraudeuses de la sangle
        perc += [cyl(1.6, 12, 0, yb0 + 2.5, zh - PROF_M4), cyl(1.6, 12, 0, yb1 - 2.5, zh - PROF_M4)]
    rf = P['M3_FRAISURE'] / 2 + 0.5                           # fraisure à la face de la joue, avec sa marge
    z_m4 = Hh - P['SERRAGE_ALIM'] - PROF_M4                   # fond du trou de la vis M4, sur la semelle
    for hx, hz in trous_alim(k):
        assert a + rf + 1.5 <= hx <= b - rf - 1.5, f'trou M3 ({hx}, {hz}) au bord de la joue'
        assert rf + 1.5 <= hz - OBLONG_ALIM and hz + OBLONG_ALIM + rf + 1.5 <= Hh - P['SERRAGE_ALIM'], \
            f'trou M3 ({hx}, {hz}) trop bas ou trop haut pour la joue'
        assert not avec_sangle or abs(hx) >= 1.6 + 2.4 + 1.0 or hz + OBLONG_ALIM + 2.4 + 1.0 <= z_m4, \
            f'trou M3 ({hx}, {hz}) contre la vis M4 de la sangle'
        # fraisure tête à la face extérieure de la joue (y = yb0), tige vers l'alim, oblong en hauteur :
        # x local -> z, y local -> -x, z local -> -y
        f = fraisure_m3(0, 0, 0, 5.0, oblong=OBLONG_ALIM)
        f.apply_transform(np.array([[0, -1, 0, hx], [0, 0, -1, yb0], [1, 0, 0, zb + es + hz], [0, 0, 0, 1]], float))
        perc.append(f)
    return moins(m, perc)


def ailes_alim():
    """Faces extérieures des joues du berceau, en y : joues de 5 mm, à JEU_ALIM des flancs de l'alim."""
    a = P['ALIM_W'] / 2 + P['JEU_ALIM'] + 5
    return -a, a


def sangle_alim():
    """Sangle posée en travers de l'alim, vissée sur le haut des deux joues du berceau."""
    yb0, yb1 = ailes_alim()
    m = extrude(arrondi(box(-12, yb0, 12, yb1), 3), P['EP_SANGLE'])
    perc = [cyl(1.8, 10, 0, yb0 + 2.5, -1), cyl(1.8, 10, 0, yb1 - 2.5, -1)]
    return moins(m, perc)


def support_prise():
    """Support de l'embase secteur IEC C14 3-en-1 (interrupteur + fusible), à clipser dans une découpe
    de 47,5 × 27,5 mm (2 mm d'épaisseur autour de la découpe). Posé sur le bout gauche du profilé de
    la rangée du bas (2 vis M5) ; l'embase regarde vers la gauche, sous l'entrée HUB75 de la dalle 1.
    Repère d'assemblage."""
    yp = Y_BAS
    z0 = Z_RANGEE[1]
    xe = -287.0 + 3.0                        # face de l'embase, 3 mm en retrait du bord
    yc, zc = yp + 40.0, z0 + 24.0            # centre de la découpe
    T_plan = [[0, 0, 1, xe], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]]       # (u=y, v=z, w=x-xe)
    contour = arrondi(box(yp - 10, z0, yp + 72, z0 + 46), 3)
    decoupe = box(yc - 23.75, zc - 13.75, yc + 23.75, zc + 13.75)
    plaque = uvw(contour.difference(decoupe), 2.0, T_plan)
    raid = uvw(contour.difference(decoupe.buffer(6, join_style=2)), 6.0,
               [[0, 0, 1, xe + 1.99], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]])
    semelle = extrude(arrondi(box(xe, yp - 10, xe + 20, yp + 10), 2), 4.0, z0)   # laisse la place au pied
    gousset = uvw(Polygon([(0, 0), (18, 0), (0, 40)]), 3.0,
                  [[1, 0, 0, xe], [0, 0, 1, yp - 10], [0, 1, 0, z0], [0, 0, 0, 1]])
    m = union([plaque, raid, semelle, gousset])
    perc = [cyl(P['M5_PASSAGE'] / 2, 20, xe + 7, yp, z0 - 5), cyl(P['M5_PASSAGE'] / 2, 20, xe + 15, yp, z0 - 5)]
    perc.append(boite(xe + 2.0, yp - 5, z0 + 4, xe + 8.5, yp + 5, z0 + 50))   # tête et clé de la 1re vis
    return moins(m, perc), (xe, yc, zc)


def pied(inclinaison=0.0):
    """Pied, sous le profilé de la rangée du bas, à 250 mm du milieu (deux pieds symétriques) :
    le profilé repose sur la jambe, une lèvre avant (à 1 mm du dos des dalles) et une plaque arrière
    vissée dans sa rainure (2 vis M5) l'emprisonnent. Patin de 240 mm au sol, 35 mm devant la face
    des LED ; écran à 25 mm du sol. Coupe dans le plan y-z extrudée sur 24 mm en x (impression
    couchée, sans support). Une rainure sur toute la largeur dégage l'ergot bas de la dalle 1, sous
    le pied gauche ; elle est verticale à l'impression, et sans effet à droite, même pièce.
    `inclinaison` : écran penché en arrière, en degrés. Repère : x centré."""
    z0, z1 = Z_RANGEE
    yh0 = Y_BAS - P['PROF'] / 2                  # dessous du profilé du bas (-242)
    yh1 = Y_BAS + P['PROF'] / 2
    ybas = -3 * PAS / 2 - P['GARDE_SOL']         # dessus du patin (-313)
    jambe = box(ybas, 1.0, yh0, z1 + 6)
    levre = box(yh0 - 0.01, 1.0, yh0 + 12, z0)                 # devant le profilé
    dos = box(yh0 - 0.01, z1, yh1 + 4, z1 + 6)                 # derrière, vissé dans la rainure
    coupe = unary_union([jambe, levre, dos])
    zf, zr = -P['EP_DALLE'] - 35.0, 190.0
    patin = box(ybas - 12, zf, ybas, zr)
    gousset = Polygon([(ybas, z1 + 6), (ybas, z1 + 110), (ybas + 55, z1 + 6)])
    gousset_av = Polygon([(ybas, 1.0), (ybas, zf + 10), (ybas + 22, 1.0)])
    sol = unary_union([patin, gousset, gousset_av])
    if inclinaison:
        sol = affinity.rotate(sol, -inclinaison, origin=(ybas, 1.0), use_radians=False)
    coupe = arrondi(unary_union([coupe, sol]), 1.0).difference(box(yh0, z0, yh1, z1))   # le profilé
    de = P['ERGOT_D'] / 2 + 1.0
    for i in (-1, 0, 1):                         # ergots de la rangée du bas sous l'un des deux pieds
        for ex, ey in ergots(i * PAS, -PAS):
            if any(abs(ex - s * P['X_PIEDS']) < 12.0 + de for s in (-1, 1)):
                coupe = coupe.difference(box(ey - de, 0.0, ey + de, P['ERGOT_H'] + 1.0))
    m = extrude(coupe, 24.0)
    m.apply_transform(np.array([[0, 0, 1, -12.0], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]], float))
    perc = []
    for x in (-6.0, 6.0):
        perc.append(cyl(P['M5_PASSAGE'] / 2, 20, x, Y_BAS, z1 - 5))
        perc.append(cyl(P['M5_TETE'] / 2, 10, x, Y_BAS, z1 + 6 - 0.01))   # tête affleurante : chambrage
    return moins(m, perc)


X_VIS_CROCHET = (-6.0, 6.0)        # vis M5 d'un crochet : leurs têtes passent à 0,75 mm des flancs


def crochet_mural():
    """Crochet à trou de serrure, coulissant sur le profilé du haut (2 vis M5) : on accroche l'écran à
    deux vis du mur, à 240 mm l'une de l'autre par défaut (crochets à ±120 mm du milieu) ; le crochet
    glisse entre les équerres des montants, de x = -167 à +123, soit jusqu'à 246 mm d'entraxe
    symétrique. Plan du mur en z = Z_MUR. Repère : axe du profilé du haut en (0, 0)."""
    zb = Z_RANGEE[1]
    zm = P['Z_MUR']
    flancs = [extrude(box(sx - 3, -14, sx + 3, 22), zm - zb, zb) for sx in (-14, 14)]
    semelle = extrude(arrondi(box(-17, -14, 17, 22), 2), 5, zb)
    face = extrude(arrondi(box(-17, -14, 17, 22), 3), 6, zm - 6)
    m = union([semelle, face] + flancs)
    perc = [cyl(P['M5_PASSAGE'] / 2, 20, x, 0, zb - 5) for x in X_VIS_CROCHET]   # têtes entre les flancs
    perc.append(cyl(5.2, 20, 0, -2, zm - 10))                                   # tête de vis
    perc.append(extrude(LineString([(0, -2), (0, 12)]).buffer(2.75), 20, zm - 10))   # fente
    perc.append(extrude(LineString([(0, -2), (0, 12)]).buffer(5.2), 3.5, zm - 9.5))  # logement de tête
    return moins(m, perc)


def patin_arriere():
    """Patin arrière, à cheval sur un montant : deux joues l'enserrent, une vis M5 dans chacune de ses
    rainures latérales. Quatre en tout, près des bouts des montants, toujours en place ; leur face
    est au plan arrière z = Z_MUR, un patin TPU collé dessus. Ils portent l'écran posé à
    plat sur le sol, LED vers le haut, et l'écartent du mur quand il est accroché. Repère : axe du
    montant en (0, 0), z d'assemblage."""
    z0, z1 = Z_MONTANT
    a = P['PROF'] / 2 + P['JEU']
    e = P['JOUE']
    lg = P['PATIN_L']
    corps = extrude(arrondi(box(-a - e, -lg / 2, a + e, lg / 2), 2), P['Z_MUR'] - (z0 + 4), z0 + 4)
    montant = boite(-a, -lg, z0 - 1, a, lg, z1 + P['JEU'])
    vis = cyl(P['M5_PASSAGE'] / 2, 2 * (a + e) + 2, -a - e - 1, 0, (z0 + z1) / 2, axe='x')
    return moins(corps, [montant, vis])


def patin_tpu():
    """Patin antidérapant en TPU, collé au double face : un sur la face de chaque patin arrière
    (30 × 24 mm), deux sous chaque pied (semelle de 24 mm de large), 1 mm en retrait des bords.
    Son épaisseur, EP_PATIN, est celle que l'assemblage compte entre le plan arrière et le sol."""
    a = P['PROF'] / 2 + P['JEU'] + P['JOUE'] - 1.0
    b = P['PATIN_L'] / 2 - 1.0
    return extrude(arrondi(box(-a, -b, a, b), 3), P['EP_PATIN'])


def ancre_collier():
    """Ancrage de collier de serrage, à glisser dans une rainure de 2020 puis tourner d'un quart de tour."""
    tete = extrude(arrondi(box(-5.6, -2.9, 5.6, 2.9), 1.2), 2.0, 0)
    cou = extrude(box(-2.9, -2.9, 2.9, 2.9), 2.2, 1.99)
    selle = extrude(arrondi(box(-8, -5, 8, 5), 2), 4.0, 4.18)
    m = union([tete, cou, selle])
    return moins(m, [extrude(box(-8.5, -2.0, 8.5, 2.0), 2.4, 5.0)])   # passage du collier


# ===================================================================================== ASSEMBLAGE
def profiles():
    """Profilés 2020 (boîtes simplifiées) : 3 de rangée, 2 montants."""
    res = []
    for j in (-1, 0, 1):
        yc = j * PAS + P['Y_PROF']
        m = trimesh.creation.box(extents=[P['L_RANGEE'], P['PROF'], P['PROF']])
        m.apply_translation([0, yc, Z_RANGEE[0] + P['PROF'] / 2])
        res.append((f'profilé de rangée {NUM_RANGEE[j]}', m, GRIS))
    y0, y1 = P['Y_MONTANT']
    for k, xm in enumerate(P['X_MONTANTS']):
        m = trimesh.creation.box(extents=[P['PROF'], y1 - y0, P['PROF']])
        m.apply_translation([xm, (y0 + y1) / 2, Z_MONTANT[0] + P['PROF'] / 2])
        res.append((f'montant {"gauche" if k == 0 else "droit"}', m, GRIS))
    return res


# (rangée j, montant k : 0 gauche, 1 droit) -> côtés du montant (-1 à gauche, 1 à droite)
EQUERRES = {(1, 0): (-1, 1), (1, 1): (-1, 1), (0, 0): (-1,), (0, 1): (-1, 1), (-1, 0): (1,), (-1, 1): (1,)}


def equerre():
    """Équerre 2020 en fonte (20 × 20 × 20, ailes de 3 mm), simplifiée. Repère : arête au coin.
    Dessinée sans ergots : la pièce Motedis en a deux par aile, alignés dans le sens de l'aile. Ceux
    de l'aile contre le montant tombent en travers de sa rainure et sont limés au montage."""
    a = trimesh.creation.box(extents=[20, 20, 3]); a.apply_translation([10, 0, 1.5])
    b = trimesh.creation.box(extents=[3, 20, 20]); b.apply_translation([1.5, 0, 10])
    return union([a, b])


def equerres():
    res = []
    e = equerre()
    for (j, k), cotes in EQUERRES.items():
        yc = j * PAS + P['Y_PROF']
        xm = P['X_MONTANTS'][k]
        for c in cotes:
            # aile horizontale sur le dos du profilé de rangée, aile verticale contre le flanc du montant
            m = place(e, miroir_x=(c < 0))
            m.apply_translation([xm + c * P['PROF'] / 2, yc, Z_RANGEE[1]])
            res.append(('équerre 2020', m, '#5c636a'))
    return res


def boite(x0, y0, z0, x1, y1, z1):
    m = trimesh.creation.box(extents=[x1 - x0, y1 - y0, z1 - z0])
    m.apply_translation([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    return m


def assemblage(inclinaison=0.0, mode='table'):
    """Tout l'écran monté. mode 'table' : sur ses deux pieds ; 'mur' ou 'plat' : sans les pieds, sur
    les patins arrière — accroché au mur, ou posé à plat sur le sol, LED vers le haut : c'est la
    même géométrie, le sol prend la place du mur."""
    elems = []
    for num, (cx, cy) in DALLES.items():
        m = trimesh.creation.box(extents=[P['COQUE'], P['COQUE'], P['EP_DALLE']])
        m.apply_translation([cx, cy, -P['EP_DALLE'] / 2])
        elems.append((f'dalle {num}', m, NOIR))
    elems += profiles()
    elems += equerres()
    for j in (-1, 0, 1):
        lg = bride_longue(j)
        bi, (bb, _) = bride_interieure(lg), bride_bord(lg)
        suffixe = ' longue' if lg else ''
        for xs in (-PAS / 2, PAS / 2):
            elems.append(('bride intérieure' + suffixe, place(bi, xs, j * PAS), ROUGE))
        elems.append(('bride de bord droite' + suffixe, place(bb, 3 * PAS / 2, j * PAS), ROUGE))
        elems.append(('bride de bord gauche' + suffixe, place(place(bb, miroir_x=True), -3 * PAS / 2, j * PAS), ROUGE))
    ag = agrafe()
    for i in (-1, 0, 1):
        for ys in (-PAS / 2, PAS / 2):
            elems.append(('agrafe', place(ag, i * PAS, ys), JAUNE))
    # carte, sur son plateau ; ses fiches et ses Pico en volumes simples
    elems.append(('plateau de carte', plateau_carte(), BLEU))
    x0, y0, l, h = CARTE
    zc = Z_RANGEE[1] + 4 + 6                 # dessous de la carte
    elems.append(('carte', boite(x0, y0 - h, zc, x0 + l, y0, zc + 1.6), '#2e7d32'))
    for k, ycl in enumerate((22.5, 59.5, 96.5)):
        a = vers_assemblage(42, ycl - 11.6)
        b = vers_assemblage(96, ycl + 11.6)
        elems.append(('Pico nœud', boite(a[0], b[1], zc + 1.6, b[0], a[1], zc + 14), '#1b5e20'))
    a, b = vers_assemblage(127.3, 81.0), vers_assemblage(150.5, 135.0)
    elems.append(('Pico tête', boite(a[0], b[1], zc + 1.6, b[0], a[1], zc + 14), '#1b5e20'))
    for n, (bx, by) in FIX['departs'].items():
        # embase MSTBA tournée de 90° : corps de by − 8,62 à by + 3,54 (12,16 mm), fiche de 10,16 mm
        a, b = vers_assemblage(195.0, by - 8.62), vers_assemblage(212.0, by + 3.54)
        elems.append(('fiche de départ', boite(a[0], b[1], zc + 1.6, b[0], a[1], zc + 14), '#2f9e44'))
    ex, ey = FIX['entree']
    a, b = vers_assemblage(ex - 3, ey + 6), vers_assemblage(ex + 18.5, ey + 20)
    elems.append(('fils d\'entrée', boite(a[0], b[1], zc + 1.6, b[0], a[1], zc + 12), '#c92a2a'))
    for k, (bx, by) in FIX['hub75'].items():
        a, b = vers_assemblage(bx - 3.7, by - 5.6), vers_assemblage(bx + 6.3, by + 23.4)
        elems.append(('fiche HUB75 de la carte', boite(a[0], b[1], zc + 1.6, b[0], a[1], zc + 24), '#adb5bd'))
    # alimentation, sur le profilé du bas
    L, W, Hh, X0 = P['ALIM_L'], P['ALIM_W'], P['ALIM_H'], P['ALIM_X0']
    za = Z_RANGEE[1] + P['SEMELLE_ALIM']
    elems.append(('alimentation 5 V 30 A', boite(X0, Y_BAS - W / 2, za, X0 + L, Y_BAS + W / 2, za + Hh), '#b0b7bf'))
    sa = sangle_alim()
    for k, (xc, avec_sangle) in enumerate(P['BERCEAUX']):
        xb = X0 + xc
        elems.append(('berceau alim', place(berceau_alim(k), xb, Y_BAS), BLEU))
        # posée sur l'alim, à SERRAGE_ALIM au-dessus des joues : les vis M4 l'y plaquent en la serrant
        if avec_sangle:
            elems.append(('sangle alim', place(sa, xb, Y_BAS, za + Hh), BLEU))
    sp, (xe, yc, zc_) = support_prise()
    elems.append(('support de prise', sp, BLEU))
    elems.append(('embase IEC', boite(xe, yc - 23.5, zc_ - 13.5, xe + 32, yc + 23.5, zc_ + 13.5), NOIR))
    cr = crochet_mural()
    for x in (-P['X_CROCHETS'], P['X_CROCHETS']):
        elems.append(('crochet mural', place(cr, x, Y_HAUT), JAUNE))
    pa = patin_arriere()
    for xm in P['X_MONTANTS']:
        for yp in P['Y_PATINS']:
            elems.append(('patin arrière', place(pa, xm, yp), '#f76707'))
    if mode == 'table':
        pd = pied(inclinaison)
        for x in (-P['X_PIEDS'], P['X_PIEDS']):
            elems.append(('pied', place(pd, x), VERT))
    return elems


def vis_m5(pieds=True):
    """Vis M5 vers les profilés (DIN 912, tête Ø M5_DK × M5_K) : (pièce, longueur, épaisseur de pièce
    sous la tête, tête, tige). La tête est posée sur sa face d'appui, côté clé ; la tige, 1,5 mm sous
    cette face. La tête ne doit entrer dans aucune pièce ; la tige doit tomber dans un trou, ce qui
    garantit que la vis est bien là où la pièce l'attend. La patte du plateau de carte n'est pas vissée."""
    dk, k, lt = P['M5_DK'], P['M5_K'], 1.5
    res = []

    def vis(nom, lg, ep, x, y, z, axe, sens):
        d, dt = (0.0, -lt) if sens > 0 else (-k, 0.0)
        if axe == 'z':
            tt = cyl(dk / 2, k, x, y, z + d), cyl(2.0, lt, x, y, z + dt)
        elif axe == 'x':
            tt = cyl(dk / 2, k, x + d, y, z, axe='x'), cyl(2.0, lt, x + dt, y, z, axe='x')
        else:
            tt = cyl(dk / 2, k, x, y + d, z, axe='y'), cyl(2.0, lt, x, y + dt, z, axe='y')
        res.append((nom, lg, ep) + tt)

    zt = EP + P['PROF'] / 2                                      # brides : rainures latérales du profilé
    yj = (P['Y_PROF'] - 10 - P['JEU'] - P['JOUE'], P['Y_PROF'] + 10 + P['JEU'] + P['JOUE'])
    xb = 3 * PAS / 2 - (23.0 + 1.5) / 2                          # milieu d'une bride de bord
    for j in (-1, 0, 1):
        for x in (-PAS / 2, PAS / 2, -xb, xb):
            vis('brides', 10, P['JOUE'], x, j * PAS + yj[0], zt, 'y', -1)
            vis('brides', 10, P['JOUE'], x, j * PAS + yj[1], zt, 'y', 1)
    x0, y0, l, h = CARTE
    z0 = Z_RANGEE[1]
    for x in (x0 + 22, x0 + l / 2, x0 + l - 22):
        vis('plateau de carte', 8, 4.0, x, P['Y_PROF'], z0 + 4, 'z', 1)
    eb = P['SEMELLE_ALIM'] - k - 0.5                             # semelle sous le lamage
    for xc, *_ in P['BERCEAUX']:
        vis('berceaux alim', 6, eb, P['ALIM_X0'] + xc, Y_BAS, z0 + eb, 'z', 1)
    xe = -287.0 + 3.0
    for x in (xe + 7, xe + 15):
        vis('support de prise', 8, 4.0, x, Y_BAS, z0 + 4, 'z', 1)
    for xc in (-P['X_CROCHETS'], P['X_CROCHETS']):
        for dx in X_VIS_CROCHET:
            vis('crochets muraux', 10, 5.0, xc + dx, Y_HAUT, z0 + 5, 'z', 1)
    a = P['PROF'] / 2 + P['JEU'] + P['JOUE']
    zm = sum(Z_MONTANT) / 2
    for xm in P['X_MONTANTS']:
        for yp in P['Y_PATINS']:
            vis('patins arrière', 10, P['JOUE'], xm - a, yp, zm, 'x', -1)
            vis('patins arrière', 10, P['JOUE'], xm + a, yp, zm, 'x', 1)
    if pieds:
        for xp in (-P['X_PIEDS'], P['X_PIEDS']):
            for dx in (-6.0, 6.0):
                vis('pieds', 10, 6.0, xp + dx, Y_BAS, z0 + 6, 'z', 1)
    return res


def vis_terre():
    """Vis M5 de la liaison de terre, seule dans la rainure arrière du profilé du bas : (emploi,
    longueur, épaisseur sous la tête). Elle ne serre que la cosse et sa rondelle à dents, trop peu
    pour une M5 × 10, qui buterait au fond de la rainure avant de serrer la cosse."""
    return 'liaison de terre', 6, P['EP_TERRE']


def couche_au_sol(elems):
    """L'écran posé à plat, LED vers le haut, pour la vue : rotation qui envoie -z vers le haut du
    tracé, et un sol sous les patins (l'épaisseur des patins TPU)."""
    R = np.array([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], float)
    res = []
    for nom, m, c in elems:
        m = m.copy()
        m.apply_transform(R)
        res.append((nom, m, c))
    zs = -P['Z_MUR'] - P['EP_PATIN']
    res.append(('sol', boite(-330, zs - 3, -330, 330, zs, 330), '#dee2e6'))
    return res


def masse(nom, m):
    """Masse estimée d'un élément, en kg : dalles, alimentation et carte au forfait ; aluminium et
    PETG d'après leur volume (profilé creux, 40 % de matière ; impression, 60 %)."""
    forfait = {'dalle': 0.50, 'alimentation': 0.70, 'carte': 0.15, 'embase IEC': 0.05, 'équerre': 0.025}
    for k, v in forfait.items():
        if nom.startswith(k):
            return v
    vol = m.volume / 1000.0                                      # cm³
    if nom.startswith(('profilé', 'montant')):
        return vol * 2.7 * 0.4 / 1000
    if nom.startswith(('fiche', 'fils', 'Pico')):
        return vol * 0.3 / 1000
    return vol * 1.27 * 0.6 / 1000


def stabilite(elems, mode):
    """Centre de gravité et appuis. 'plat' : il tombe dans le rectangle des 4 patins arrière, à
    50 mm au moins de ses bords, et l'alimentation garde 20 mm d'air devant le sol ou le mur.
    'table' : angles de basculement sur les deux pieds droits."""
    ms = [(masse(n, m), np.asarray(m.center_mass)) for n, m, c in elems]
    mt = sum(a for a, _ in ms)
    g = sum(a * c for a, c in ms) / mt
    ok = True
    print(f'  masse estimée {fr(mt)} kg ; centre de gravité en x = {g[0]:.0f}, y = {g[1]:.0f}, z = {g[2]:.0f} mm')
    if mode == 'table':
        h = g[1] - (-3 * PAS / 2 - P['GARDE_SOL'] - 12)          # hauteur au-dessus du sol
        zf, zr = -P['EP_DALLE'] - 35.0, 190.0                    # bouts des patins des pieds
        av = math.degrees(math.atan2(g[2] - zf, h))
        ar = math.degrees(math.atan2(zr - g[2], h))
        co = math.degrees(math.atan2(P['X_PIEDS'] + 12 - abs(g[0]), h))
        print(f'  sur ses pieds, il bascule si on le penche de {av:.0f}° en avant, {ar:.0f}° en arrière, '
              f'{co:.0f}° sur le côté')
        if av < 8:
            ok = False
            print('  ✗ trop près de basculer en avant')
    else:
        xs, ys = P['X_MONTANTS'], P['Y_PATINS']
        marge = min(g[0] - min(xs), max(xs) - g[0], g[1] - min(ys), max(ys) - g[1])
        print(f'  à plat : centre de gravité à {marge:.0f} mm du bord du rectangle des patins '
              f'({max(xs) - min(xs):.0f} × {max(ys) - min(ys):.0f} mm)')
        if marge < 50:
            ok = False
            print('  ✗ trop près du bord des appuis')
        corps = max(m.bounds[1][2] for n, m, c in elems if n.startswith('alimentation'))
        haut = max(m.bounds[1][2] for n, m, c in elems if n.startswith(('alimentation', 'sangle')))
        print(f'  air entre l\'alimentation et le sol, ou le mur : {P["Z_MUR"] - corps:.0f} mm, '
              f'{P["Z_MUR"] - haut:.0f} mm sous sa sangle')
        if P['Z_MUR'] - haut < 20:
            ok = False
            print('  ✗ moins de 20 mm d\'air pour l\'alimentation')
    return ok


# ===================================================================================== CONTRÔLES
def controles(elems):
    """Ce que le cadre v3 a appris à vérifier : chaque insert reçoit une vis, aucune pièce ne couvre
    un connecteur ni un ergot, aucune pièce n'en traverse une autre."""
    ok = True
    # 1. inserts utilisés : LM/RM/BL/BR de chaque dalle, TL/TR sous une rangée de brides longues ; et
    # chaque trou de bride tombe sur un insert
    utiles, tous = set(), set()
    for j in (-1, 0, 1):
        for i in (-1, 0, 1):
            ins = inserts(i * PAS, j * PAS)
            noms = ('LM', 'RM', 'BL', 'BR') + (('TL', 'TR') if j < 1 and bride_longue(j + 1) else ())
            for nom, (x, y) in ins.items():
                tous.add((round(x, 2), round(y, 2)))
                if nom in noms:
                    utiles.add((round(x, 2), round(y, 2)))
    vis = set()
    for j in (-1, 0, 1):
        ys_ = trous_bride(bride_longue(j))
        for xs in (-PAS / 2, PAS / 2):
            for sx in (-1, 1):
                for y in ys_:
                    vis.add((round(xs + sx * (PAS / 2 - P['INS_X']), 2), round(j * PAS + y, 2)))
        for s in (-1, 1):
            for y in ys_:
                vis.add((round(s * (3 * PAS / 2 - (PAS / 2 - P['INS_X'])), 2), round(j * PAS + y, 2)))
    manque, hors = utiles - vis, vis - tous
    print(f'  inserts des brides (LM/RM/BL/BR, TL/TR sous les brides longues) : {len(utiles)}, '
          f'vissés : {len(vis & utiles)}')
    if manque:
        ok = False
        print('  ✗ inserts sans vis :', sorted(manque))
    if hors:
        ok = False
        print('  ✗ trous de bride sans insert dessous :', sorted(hors))
    # 2. connecteurs et ergots : aucune pièce ne les couvre au ras du dos des dalles (coupe à z = 3)
    zones = zones_connecteurs()
    for num, (cx, cy) in DALLES.items():
        for e in ergots(cx, cy):
            zones.append((f'ergot dalle {num}', Point(e).buffer(P['ERGOT_D'] / 2)))
    coupees = 0
    for nom, m, c in elems:
        if nom.startswith(('dalle', 'carte', 'Pico', 'alimentation', 'embase')):
            continue
        sec = m.section(plane_origin=[0, 0, 3.0], plane_normal=[0, 0, 1])
        if sec is None:
            continue
        try:
            pl, _ = sec.to_2D(to_2D=np.eye(4))
            emprise = unary_union([Polygon(p.exterior.coords) for p in pl.polygons_full])
        except Exception as e:                  # ne pas sauter la pièce en silence
            ok = False
            print(f'  ✗ {nom} : coupe à z = 3 impossible ({type(e).__name__} : {e})')
            continue
        coupees += 1
        for zn, g in zones:
            if emprise.intersects(g) and emprise.intersection(g).area > 0.5:
                ok = False
                print(f'  ✗ {nom} couvre {zn}')
    print(f'  connecteurs et ergots : {coupees} pièces contrôlées au ras du dos des dalles')
    # 2 bis. accès aux connecteurs des dalles : au-dessus de chacun, sur toute la profondeur jusqu'au
    # plan du mur, et avec 8 mm de marge autour pour les doigts, rien ne doit se trouver
    m_ = P['MARGE_ACCES']
    inutiles = {(3, 'sortie HUB75'), (6, 'sortie HUB75'), (9, 'sortie HUB75')}   # bouts de chaîne
    for num, (cx, cy) in DALLES.items():
        for cn, (x0, y0, x1, y1) in CONNECTEURS.items():
            zone = boite(cx + x0 - m_, cy + y0 - m_, 0.5, cx + x1 + m_, cy + y1 + m_, P['Z_MUR'])
            for nom, m, c in elems:
                if nom.startswith('dalle') or not _boites(zone, m):
                    continue
                try:
                    v = trimesh.boolean.intersection([zone, m], engine='manifold').volume
                except Exception:
                    v = 0
                if v > 1.0:
                    if (num, cn) in inutiles:
                        print(f'  · {nom} au-dessus de la {cn} de la dalle {num} (inutilisée, bout de chaîne)')
                    else:
                        ok = False
                        print(f'  ✗ {nom} gêne l\'accès à la {cn} de la dalle {num} ({v:.0f} mm³)')
    # 3. interférences entre pièces (hors dalles)
    solides = [(nom, m) for nom, m, c in elems if not nom.startswith('dalle')]
    for a in range(len(solides)):
        for b in range(a + 1, len(solides)):
            na, ma = solides[a]
            nb, mb = solides[b]
            if not _boites(ma, mb):
                continue
            try:
                inter = trimesh.boolean.intersection([ma, mb], engine='manifold')
                v = inter.volume if inter is not None and len(inter.faces) else 0
            except Exception:
                v = 0
            if v > 1.0:
                ok = False
                print(f'  ✗ {na} et {nb} s\'interpénètrent ({v:.0f} mm³)')
    # 3 bis. vis M5 : chaque tête a sa place, sans toucher aucune pièce, la sienne comprise ; chaque
    # tige tombe dans un trou ; et la vis dépasse de sa pièce assez pour traverser tout l'écrou
    # marteau, pas assez pour buter au fond de la rainure avant de serrer (tolérance de longueur
    # des vis : ±0,3 mm)
    toutes = vis_m5(pieds=any(nom.startswith('pied') for nom, m, c in elems))
    pmin, pmax = P['ECROU_H'], P['FOND_RAINURE'] - 0.3
    for nv, lg, ep, tete, tige in toutes:
        for quoi, s in (('tête', tete), ('tige', tige)):
            for nom, m, c in elems:
                if not _boites(s, m):
                    continue
                v = trimesh.boolean.intersection([s, m], engine='manifold').volume
                if v > 0.5:
                    ok = False
                    print(f'  ✗ la {quoi} d\'une vis M5 des {nv} entre dans : {nom} ({v:.0f} mm³)')
    for nv, lg, ep in sorted({(nv, lg, ep) for nv, lg, ep, *_ in toutes} | {vis_terre()}):
        if not pmin - 1e-6 <= lg - ep <= pmax + 1e-6:
            ok = False
            print(f'  ✗ vis M5 × {lg} des {nv} : dépasse de {fr(lg - ep)} mm dans la rainure, '
                  f'il faut de {fr(pmin)} à {fr(pmax)} mm')
    print(f'  vis M5 DIN 912 : {len(toutes)} contrôlées, tête, trou et longueur ; '
          f'vis de la liaison de terre : longueur')
    # 3 ter. chaque trou taraudé du flanc de l'alim tombe dans la joue d'un berceau (les marges autour
    # de chaque trou, berceau_alim les vérifie en le dessinant)
    couverts = sum(len(trous_alim(k)) for k in range(len(P['BERCEAUX'])))
    print(f'  trous M3 du flanc de l\'alim : {len(P["ALIM_TROUS"])}, dans une joue de berceau : {couverts}')
    if couverts != len(P['ALIM_TROUS']):
        ok = False
        print('  ✗ un trou M3 de l\'alim n\'est sous aucune joue, ou sous deux')
    # 4. rien devant le dos des dalles, sauf les pieds
    for nom, m, c in elems:
        if nom.startswith('pied') or nom.startswith('dalle'):
            continue
        if m.bounds[0][2] < -0.01:
            ok = False
            print(f'  ✗ {nom} dépasse devant le dos des dalles (z = {m.bounds[0][2]:.1f})')
    # 5. rien au-delà du plan du mur
    for nom, m, c in elems:
        if nom.startswith('pied'):
            continue
        if m.bounds[1][2] > P['Z_MUR'] + 0.01:
            ok = False
            print(f'  ✗ {nom} dépasse le plan du mur (z = {m.bounds[1][2]:.1f} > {P["Z_MUR"]})')
    return ok


def _boites(a, b):
    (a0, a1), (b0, b1) = a.bounds, b.bounds
    return all(a0[k] < b1[k] - 0.01 and b0[k] < a1[k] - 0.01 for k in range(3))


# ===================================================================================== SORTIES
def a_plat(m, axe='z', signe=1):
    """Pose une pièce pour l'impression : la face `signe * axe` minimale sur le plateau (z = 0)."""
    m = m.copy()
    if axe == 'x':
        m.apply_transform(trimesh.transformations.rotation_matrix(signe * math.pi / 2, [0, 1, 0]))
    elif axe == 'zinv':
        m.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1, 0, 0]))
    b = m.bounds
    m.apply_translation([-(b[0][0] + b[1][0]) / 2, -(b[0][1] + b[1][1]) / 2, -b[0][2]])
    return m


PIECES = [  # nom de fichier, fabrique, pose, quantité, matière/remarque
    ('bride_interieure', lambda: bride_interieure(), 'z', 2, 'rangée du bas : joints verticaux intérieurs'),
    ('bride_interieure_longue', lambda: bride_interieure(True), 'z', 4,
     'rangées du haut et du milieu : descend sur les TL/TR des 2 dalles du dessous'),
    ('bride_bord_droite', lambda: bride_bord()[0], 'z', 1, 'rangée du bas, bord droit (vue de dos)'),
    ('bride_bord_gauche', lambda: place(bride_bord()[0], miroir_x=True), 'z', 1, 'rangée du bas, bord gauche'),
    ('bride_bord_droite_longue', lambda: bride_bord(True)[0], 'z', 2,
     'rangées du haut et du milieu, bord droit ; descend sur le TR de la dalle du dessous'),
    ('bride_bord_gauche_longue', lambda: place(bride_bord(True)[0], miroir_x=True), 'z', 2,
     'rangées du haut et du milieu, bord gauche ; descend sur le TL de la dalle du dessous'),
    ('agrafe_joint', lambda: agrafe(), 'z', 6, 'joints horizontaux, TM/BM'),
    ('gabarit_bride', lambda: gabarit_bride(), 'z', 1, 'à imprimer en premier : ses 6 trous sur une jointure de 4 dalles'),
    ('plateau_carte', lambda: plateau_carte(), 'z', 1, 'porte la carte de l\'écran'),
    ('berceau_alim_gauche', lambda: berceau_alim(0), 'x', 1, 'côté bornier, 1 trou M3, sans sangle ; couché sur le flanc'),
    ('berceau_alim_droit', lambda: berceau_alim(1), 'x', 1, '2 trous M3 ; couché sur le flanc'),
    ('sangle_alim', lambda: sangle_alim(), 'z', sum(s for _, s in P['BERCEAUX']), 'sur le berceau droit'),
    ('support_prise', lambda: support_prise()[0], 'x', 1, 'face de l\'embase sur le plateau'),
    ('pied', lambda: pied(0.0), 'x', 2, 'écran droit ; couché sur le flanc'),
    ('pied_incline10', lambda: pied(P['INCLINAISON']), 'x', 2, 'variante : écran penché de 10° en arrière'),
    ('crochet_mural', lambda: crochet_mural(), 'zinv', 2, 'face murale sur le plateau'),
    ('patin_arriere', lambda: patin_arriere(), 'zinv', 4, 'toujours en place ; patin TPU collé sur la face'),
    ('patin_tpu', lambda: patin_tpu(), 'z', 8, '**TPU**, collé au double face : 1 sur chaque patin arrière, 2 sous chaque pied'),
    ('ancre_collier', lambda: ancre_collier(), 'z', 20, 'dans les rainures, pour les colliers'),
]


def exporte_stl():
    os.makedirs(STL, exist_ok=True)
    noms = {nom + '.stl' for nom, *_ in PIECES}
    for f in os.listdir(STL):
        if f.endswith('.stl') and f not in noms:
            os.remove(os.path.join(STL, f))            # pièce renommée ou supprimée
    lignes = []
    for nom, fab, pose, qte, rem in PIECES:
        m = fab()
        if pose == 'x':
            m = a_plat(m, 'x', 1)
        elif pose == 'x-':
            m = a_plat(m, 'x', -1)
        else:
            m = a_plat(m, pose)
        assert m.is_watertight and m.volume > 0, nom
        m.export(os.path.join(STL, nom + '.stl'))
        e = m.bounds[1] - m.bounds[0]
        lignes.append((nom, qte, e, m.volume / 1000.0, rem))
        assert max(e[0], e[1]) <= 256 and e[2] <= 256, f'{nom} ne tient pas sur le plateau de l\'A1 : {e}'
    return lignes


def exporte_glb(elems):
    sc = trimesh.Scene()
    for k, (nom, m, c) in enumerate(elems):
        mm = m.copy()
        rgb = [int(c[i:i + 2], 16) for i in (1, 3, 5)]
        mm.visual.face_colors = rgb + [255]
        sc.add_geometry(mm, node_name=f'{k:03d} {nom}')
    sc.export(os.path.join(ICI, 'cadre.glb'))


def rendu(elems, fichier, elev, azim, titre, masquer=(), roi=None, taille=(13, 10)):
    """Vue 3D ombrée (matplotlib, peintre trié triangle par triangle). Repère du tracé, direct :
    X = x, Y = -z (l'arrière vers -Y), Z = y (haut) ; azim = -90 regarde depuis l'arrière."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    fig = plt.figure(figsize=taille, dpi=110)
    ax = fig.add_subplot(111, projection='3d', computed_zorder=False, proj_type='ortho')
    lum = np.array([0.35, -0.8, 0.5])
    lum /= np.linalg.norm(lum)
    tris, cols = [], []
    for nom, m, c in elems:
        if any(nom.startswith(x) for x in masquer):
            continue
        if len(m.faces) < 400:
            v, f = trimesh.remesh.subdivide_to_size(m.vertices, m.faces, max_edge=30.0)
            m = trimesh.Trimesh(v, f, process=False)
        t = m.triangles[:, :, [0, 2, 1]] * np.array([1.0, -1.0, 1.0])
        n_ = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        n_ /= np.linalg.norm(n_, axis=1)[:, None] + 1e-12
        base = np.array([int(c[i:i + 2], 16) / 255 for i in (1, 3, 5)])
        k = 0.35 + 0.65 * np.abs(n_ @ lum)
        tris.append(t)
        cols.append(np.clip(base[None, :] * k[:, None], 0, 1))
    tris = np.concatenate(tris)
    cols = np.concatenate(cols)
    pc = Poly3DCollection(tris, facecolors=cols, edgecolors='none', linewidths=0)
    ax.add_collection3d(pc)
    lim = roi or ((-330, 330), (-260, 80), (-345, 300))
    ax.set_xlim(*lim[0]); ax.set_ylim(*lim[1]); ax.set_zlim(*lim[2])
    ax.set_box_aspect([lim[0][1] - lim[0][0], lim[1][1] - lim[1][0], lim[2][1] - lim[2][0]])
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    ax.set_title(titre, fontsize=13)
    plt.tight_layout()
    fig.savefig(fichier, dpi=110)
    plt.close(fig)


# ===================================================================================== CÂBLAGE
def _grille(pas):
    xs = np.arange(-286.0, 286.0 + 0.1, pas)
    ys = np.arange(-300.0, 286.0 + 0.1, pas)
    X, Y = np.meshgrid(xs, ys, indexing='ij')
    return xs, ys, X, Y


def _astar(cout, bloque, depart, dir0, but, xs, ys, pas, virage=30.0):
    """A* 4-connexe sur la grille : coût = longueur × coût de la case, + `virage` cases par
    changement de direction (en mm). `but` : masque des cases d'arrivée. Rend la liste des cases."""
    import heapq
    nx, ny = bloque.shape
    gi = (int(round((depart[0] - xs[0]) / pas)), int(round((depart[1] - ys[0]) / pas)))
    cibles = np.argwhere(but)
    cx_, cy_ = cibles[:, 0].mean(), cibles[:, 1].mean()
    DIRS = ((1, 0), (-1, 0), (0, 1), (0, -1))

    def h(i, j):
        return max(0.0, abs(i - cx_) + abs(j - cy_) - 3)

    d0 = DIRS.index(dir0)
    ouv = [(h(*gi), 0.0, gi[0], gi[1], d0)]
    meilleur = {(gi[0], gi[1], d0): 0.0}
    pere = {}
    while ouv:
        f, g, i, j, d = heapq.heappop(ouv)
        if g > meilleur.get((i, j, d), 1e18):
            continue
        if but[i, j]:
            chemin = [(i, j)]
            k = (i, j, d)
            while k in pere:
                k = pere[k]
                chemin.append(k[:2])
            return chemin[::-1]
        for nd, (di, dj) in enumerate(DIRS):
            a, b = i + di, j + dj
            if not (0 <= a < nx and 0 <= b < ny) or bloque[a, b]:
                continue
            g2 = g + (cout[i, j] + cout[a, b]) / 2 + (virage if nd != d else 0.0)
            if g2 < meilleur.get((a, b, nd), 1e18) - 1e-9:
                meilleur[(a, b, nd)] = g2
                pere[(a, b, nd)] = (i, j, d)
                heapq.heappush(ouv, (g2 + h(a, b), g2, a, b, nd))
    return None


def _polyligne(cases, xs, ys):
    pts = [(float(xs[i]), float(ys[j])) for i, j in cases]
    res = [pts[0]]
    for k in range(1, len(pts) - 1):
        (xa, ya), (xb, yb), (xc, yc) = res[-1], pts[k], pts[k + 1]
        if (xb - xa) * (yc - yb) - (yb - ya) * (xc - xb) != 0:
            res.append(pts[k])
    res.append(pts[-1])
    return res


def cheminement(pas=4.0):
    """Cheminement des câbles, vue de dos, sur une grille de `pas` mm (A*) :
      - les 9 câbles de dalle, de leur fiche de départ (bord droit de la carte) à la prise VH ;
      - le câble 5 V, du bornier de l'alimentation (bout gauche, bornes continues en haut) à J1 ;
      - le câble secteur, de l'embase IEC au bornier de l'alimentation (bornes L, N, PE en bas).
    Ils contournent les connecteurs des dalles avec leur marge d'accès, la carte, l'alimentation, les
    croisements montant / profilé (équerres), les crochets et les pieds ; passer par-dessus un profilé
    de rangée coûte un détour, et les nappes HUB75 entre dalles sont évitées tant que possible.
    Rend {clef: dict(points, longueur, profiles)} ; clef = numéro de dalle, 'continu' ou 'secteur'."""
    xs, ys, X, Y = _grille(pas)

    def dans(x0, y0, x1, y1):
        return (X >= min(x0, x1)) & (X <= max(x0, x1)) & (Y >= min(y0, y1)) & (Y <= max(y0, y1))

    m_ = P['MARGE_ACCES']
    x0, y0, l, h = CARTE
    L, W, X0 = P['ALIM_L'], P['ALIM_W'], P['ALIM_X0']
    fixe = dans(x0 - 2, y0 - h - 2, x0 + 214, y0 + 2)                  # carte et fiches de départ
    fixe |= dans(X0 - 2, Y_BAS - W / 2 - 2, X0 + L + 2, Y_BAS + W / 2 + 2)   # alimentation
    croisements = np.zeros_like(fixe)
    for j in (-1, 0, 1):
        yc = j * PAS + P['Y_PROF']
        for xm in P['X_MONTANTS']:
            croisements |= dans(xm - 32, yc - 12, xm + 32, yc + 12)   # croisement et ses équerres
    sans_croisements = fixe.copy()          # les nappes, qui sortent de la carte par le haut, les enjambent
    fixe = fixe | croisements
    for x in (-P['X_CROCHETS'], P['X_CROCHETS']):
        fixe |= dans(x - 19, Y_HAUT - 16, x + 19, Y_HAUT + 24)
    for x in (-P['X_PIEDS'], P['X_PIEDS']):
        fixe |= dans(x - 14, -3 * PAS / 2 - 20, x + 14, Y_BAS - 10)
    sp, (xe, yce, zce) = support_prise()
    fixe |= dans(-290, yce - 26, xe + 34, yce + 26)                    # embase et son support
    sans_croisements |= fixe & ~croisements
    zones = {}
    for num, (cx, cy) in DALLES.items():
        for cn, (a0, b0, a1, b1) in CONNECTEURS.items():
            zones[(num, cn)] = dans(cx + a0 - m_, cy + b0 - m_, cx + a1 + m_, cy + b1 + m_)
    tout_conn = np.zeros_like(fixe)
    for z in zones.values():
        tout_conn |= z
    cout = np.full(fixe.shape, pas)
    profils = []
    for j in (-1, 0, 1):
        yc = j * PAS + P['Y_PROF']
        profils.append((yc - P['PROF'] / 2, yc + P['PROF'] / 2))
        cout[dans(-1e9, yc - 10, 1e9, yc + 10)] += 2.0 * pas             # par-dessus : ~40 mm de plus
        for sg in (-1, 1):                                                  # le long : attaché aux ancres
            cout[dans(-1e9, yc + sg * 11, 1e9, yc + sg * 19)] *= 0.6
    for num, (cx, cy) in DALLES.items():                                   # nappes HUB75 entre dalles
        if num % 3:
            cout[dans(cx + 59.5, cy + 37.5, cx + PAS - 64, cy + 66.5)] += 3.0 * pas

    def franchis(pts):
        n = 0
        for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
            for p0, p1 in profils:
                if min(ya, yb) < p0 and max(ya, yb) > p1:
                    n += 1
        return n

    def longueur(pts):
        return sum(math.hypot(xb - xa, yb - ya) for (xa, ya), (xb, yb) in zip(pts, pts[1:]))

    res = {}
    for n, (bx, by) in sorted(FIX['departs'].items(), key=lambda t: int(t[0])):
        n = int(n)
        cx, cy = DALLES[n]
        a0, b0, a1, b1 = CONNECTEURS['alim VH']
        but = dans(cx + a0, cy + b0, cx + a1, cy + b1)
        bloque = fixe | (tout_conn & ~zones[(n, 'alim VH')])
        xd, yd = vers_assemblage(212.0, by - 2.54)             # bout de la fiche, au milieu des 2 pôles
        cases = _astar(cout, bloque, (xd + 6, yd), (1, 0), but, xs, ys, pas)
        if cases is None:
            raise RuntimeError(f'pas de chemin pour le câble de la dalle {n}')
        pl = _polyligne(cases, xs, ys)
        pts = [(xd, pl[0][1])] + pl
        lg = longueur(pts)
        k = franchis(pts)
        # coupe : tracé + remontée de la fiche (50) + descente sur la VH et mou (80) + 40 par profilé
        res[n] = dict(points=pts, trace=lg, profiles=k,
                      longueur=50 * math.ceil((1.10 * (lg + 50 + 80 + 40 * k)) / 50))
    # nappes HUB75 : embase J4/J5/J6 (bord gauche de la carte) -> entrée de la 1re dalle de la rangée
    for k, (bx, by) in sorted(FIX['hub75'].items()):
        num = (7, 4, 1)[int(k)]
        cx, cy = DALLES[num]
        a0, b0, a1, b1 = CONNECTEURS['entrée HUB75']
        but = dans(cx + a0, cy + b0, cx + a1, cy + b1)
        bloque = sans_croisements | (tout_conn & ~zones[(num, 'entrée HUB75')])
        xd, yd = vers_assemblage(bx + 1.27, by + 8.89)          # milieu de l'embase 2 × 8
        cases = _astar(cout, bloque, (x0 - 6, yd), (-1, 0), but, xs, ys, pas)
        if cases is None:
            raise RuntimeError(f'pas de chemin pour la nappe J{4 + int(k)}')
        pl = _polyligne(cases, xs, ys)
        pts = [(xd, pl[0][1])] + pl
        lg = longueur(pts)
        res[f'J{4 + int(k)}'] = dict(points=pts, trace=lg, profiles=franchis(pts), dalle=num,
                                     longueur=50 * math.ceil((1.10 * (lg + 60 + 40 + 40 * franchis(pts))) / 50))
    # 5 V : bornes continues du bornier (haut du bout gauche de l'alim) -> J1 (fils par le bas de la carte)
    ex, ey = FIX['entree']
    xj, yj = vers_assemblage(ex + 7.62, ey)
    but = dans(xj - 8, y0 - h - 10, xj + 8, y0 - h - 4)
    dep = (X0 - 8, Y_BAS + W / 2 - 20)
    cote_secteur = dans(X0 - 40, Y_BAS - W / 2 - 30, X0 - 1, Y_BAS - 6)
    cases = _astar(cout, fixe | tout_conn | cote_secteur, dep, (-1, 0), but, xs, ys, pas)
    pl = _polyligne(cases, xs, ys)
    pts = [(X0, pl[0][1])] + pl + [(xj, y0 - h)]
    lg = longueur(pts)
    res['continu'] = dict(points=pts, trace=lg, profiles=franchis(pts),
                          longueur=50 * math.ceil((1.15 * (lg + 60)) / 50))
    # secteur : arrière de l'embase IEC -> bornes L, N, PE (bas du bout gauche de l'alim)
    but = dans(X0 - 14, Y_BAS - W / 2 + 6, X0 - 4, Y_BAS - W / 2 + 26)
    dep = (xe + 40, yce)
    libre = fixe.copy()
    libre &= ~dans(xe + 33, yce - 26, xe + 50, yce + 26)
    ligne_5v = LineString(res['continu']['points'])
    proches = ligne_5v.buffer(25.0)
    from shapely import contains_xy
    pres_5v = contains_xy(proches, X, Y)
    cases = _astar(cout, libre | tout_conn | pres_5v, dep, (1, 0), but, xs, ys, pas)
    pl = _polyligne(cases, xs, ys)
    pts = [(xe + 32, pl[0][1])] + pl + [(X0, Y_BAS - W / 2 + 16)]
    lg = longueur(pts)
    res['secteur'] = dict(points=pts, trace=lg, profiles=franchis(pts),
                          longueur=50 * math.ceil((1.15 * (lg + 60)) / 50))
    # contrôle : aucun tracé ne passe sur la zone d'accès d'un connecteur qui n'est pas le sien
    for clef, r in res.items():
        ligne = LineString(r['points'])
        for (num, cn), (a0, b0, a1, b1) in ((k_, CONNECTEURS[k_[1]]) for k_ in zones):
            if clef == num and cn == 'alim VH':
                continue
            if r.get('dalle') == num and cn == 'entrée HUB75':
                continue
            cx, cy = DALLES[num]
            zb = box(cx + a0 - m_ + 0.5, cy + b0 - m_ + 0.5, cx + a1 + m_ - 0.5, cy + b1 + m_ - 0.5)
            if ligne.intersects(zb):
                raise RuntimeError(f'le câble {clef} passe sur la {cn} de la dalle {num}')
    return res


def fr(x, d=1):
    """Nombre à la française : virgule décimale."""
    return f'{x:.{d}f}'.replace('.', ',')


def faisceau(ch):
    """Fiche de coupe du faisceau, tirée du cheminement calculé : faisceau.md."""
    rho = 0.0172                                    # Ω·mm²/m, cuivre
    t = ['# Faisceau d\'alimentation — fiche de coupe', '',
         'Généré par `gen_cadre.py` à partir du cheminement calculé (tracés sur `../docs/cadre_plan.png`). '
         'Longueur de coupe = tracé + remontée à la fiche (50 mm) + descente sur l\'embase de la dalle et mou '
         '(80 mm) + 40 mm par profilé enjambé, + 10 %, arrondie aux 50 mm supérieurs. Aucun câble ne passe '
         'sur un connecteur de dalle ni sur sa marge d\'accès de 8 mm.', '',
         '## Les 9 câbles de dalle', '',
         'Constitution : **4 conducteurs souples de 0,75 mm² (18 AWG)**, 2 rouges et 2 noirs. Côté dalle, '
         'boîtier JST **VHR-4N** et 4 contacts **SVH-21T-P1.1** (embase VH 3.96 de la dalle, sérigraphiée '
         '`+ + - -`) — ou le câble d\'origine de la dalle s\'il est assez long, cosses coupées. Côté carte, '
         'fiche **MSTB 2,5/2-ST-5,08** : les 2 rouges dans un **embout double 2 × 0,75 mm²** sur la borne 1 (+), '
         'les 2 noirs dans un autre sur la borne 2 (−). Repérer chaque câble à ses deux bouts (gaine '
         'thermo ou bague numérotée).', '',
         '| Câble | Départ sur la carte | Arrivée | Tracé | Profilés enjambés | **Coupe** | Chute de tension à 4 A |',
         '|---|---|---|---|---|---|---|']
    total = 0
    for n in range(1, 10):
        r = ch[n]
        dv = 4.0 * 2 * rho * r['longueur'] / 1000 / 1.5
        total += r['longueur']
        t.append(f'| D{n} | J{10 + n}, fusible F{n} | dalle {n} | {r["trace"]:.0f} mm | {r["profiles"]} | '
                 f'**{r["longueur"]} mm** | {1000 * dv:.0f} mV |')
    t += ['', f'Total : {fr(total / 1000, 2)} m de câble 4 conducteurs, soit {fr(4 * total / 1000)} m de fil 0,75 mm² '
          f'({fr(2 * total / 1000)} m de rouge et autant de noir) ; compter 10 % de plus pour les essais de sertissage.', '',
          'Chute de tension : aller et retour, 2 × 0,75 mm² par pôle. S\'y ajoutent ~40 mV dans le fusible '
          'et ~30 mV dans le bus de la carte : la dalle la plus éloignée voit au pire ~0,15 V de moins que '
          'l\'alimentation. Régler celle-ci à **5,15 V** à vide (≤ 5,25 V).', '']
    t += ['## Les 3 nappes HUB75 de la carte', '',
          'Nappe IDC 16 points, de l\'embase de la carte à l\'**entrée** de la première dalle de la rangée ; '
          'les 6 nappes entre dalles, sortie → entrée voisine, restent celles fournies avec les dalles. '
          'Longueur utile minimale (tracé + 60 mm de remontée de l\'embase + 40 mm de descente, + 10 %) :', '',
          '| Nappe | De | Vers | Tracé | Longueur minimale |', '|---|---|---|---|---|']
    for k in (4, 5, 6):
        r = ch[f'J{k}']
        t.append(f'| J{k} | carte, nœud {k - 4} | entrée de la dalle {r["dalle"]} | {r["trace"]:.0f} mm | '
                 f'**{r["longueur"]} mm** |')
    t += ['', 'Une nappe fournie plus courte se remplace par une nappe IDC 16 points du commerce de la longueur '
          'standard au-dessus (30 ou 50 cm). Détrompeur vers le repère ▷ de l\'embase de la carte, broche 1 = R1.', '']
    r = ch['continu']
    dv = 36.0 * 2 * rho * r['longueur'] / 1000 / 5.0
    t += ['## Câble 5 V : alimentation → carte (J1)', '',
          f'**4 conducteurs souples de 2,5 mm² (H07V-K)**, 2 rouges et 2 noirs, coupés à **{r["longueur"]} mm** '
          f'(tracé {r["trace"]:.0f} mm). Chute de tension à 36 A : {1000 * dv:.0f} mV.', '',
          '- Côté alimentation : **une cosse fourche isolée par fil** (bleue, 1,5–2,5 mm², M3,5 ou M4 selon les '
          'vis du bornier), chaque fil sur **sa propre borne** : 2 rouges sur deux bornes +V, 2 noirs sur deux '
          'bornes −V. Plus aucune cosse empilée.',
          '- Côté carte : **embouts 2,5 mm²** dans le bornier J1 (MKDS 3/4-5,08) : bornes 1 et 2 = +5 V, '
          '3 et 4 = masse (sérigraphie de la carte).',
          '- Torsader légèrement rouge et noir ensemble ; un collier sur l\'ancre du profilé du bas.', '']
    r = ch['secteur']
    t += ['## Secteur : embase IEC → alimentation', '',
          'Embase **IEC C14 3-en-1** (fiche, interrupteur, porte-fusible 5 × 20) avec **fusible T 3,15 A** '
          '(temporisé). Conducteurs **H05V-K 1 mm²** : marron (L), bleu (N), vert-jaune (PE), coupés à '
          f'**{r["longueur"]} mm** (tracé {r["trace"]:.0f} mm), passés ensemble dans une gaine tressée ou une '
          'gaine PVC (double isolation le long du cadre métallique), colliers tous les 10 cm.', '',
          '- Côté embase : **cosses femelles 6,3 mm entièrement isolées** (L et N sur les cosses de '
          'l\'interrupteur, côté charge, pas sur la fiche). Sur la languette de terre, une cosse '
          '**« piggyback »** pour en repartir vers le cadre.',
          '- Côté alimentation : cosses fourche isolées (rouges, 0,5–1,5 mm²) sur L, N et ⏚.',
          '- **Liaison du cadre à la terre** : du piggyback de l\'embase, un fil vert-jaune 1,5 mm² terminé par '
          'une cosse à œillet M5, vissée **seule** dans la rainure arrière du profilé du bas (**vis M5 × 6**, '
          'écrou marteau, **rondelle à dents** DIN 6798 A sous la cosse pour percer l\'anodisation). Pas plus '
          f'longue : sous la tête, cosse et rondelle ne font que {fr(P["EP_TERRE"])} mm, et une M5 × 8 ou × 10 '
          'buterait au fond de la rainure avant de serrer la cosse.',
          '- Contrôle à l\'ohmmètre, prise débranchée : **< 0,5 Ω** entre la broche de terre de l\'embase et '
          'chacun des 5 profilés. Si un profilé n\'y est pas, rondelle à dents sous une vis de ses équerres.',
          '- Le secteur reste à **25 mm au moins** du câble 5 V sur tout son tracé (bornes L, N, ⏚ en bas du '
          'bornier, sorties continues en haut). Bornier de l\'alimentation **sous son capot** ; une '
          'alimentation sans capot n\'a pas sa place au dos d\'un écran qu\'on touche.', '',
          '## À acheter pour le faisceau', '',
          '| Pièce | Qté |', '|---|---|',
          f'| Fil souple 0,75 mm² rouge / noir | {fr(2 * total * 1.1 / 1000)} m de chaque |',
          '| Boîtier JST VHR-4N + contacts SVH-21T-P1.1 (si câbles neufs) | 9 + 36 (+ 10 % de contacts) |',
          '| Fiche Phoenix MSTB 2,5/2-ST-5,08 (1757019) | 9 (déjà comptées avec la carte) |',
          '| Embout double 2 × 0,75 mm² (gris, ex. Phoenix AI-TWIN 2 × 0,75-10 GY 3200807) | 18 (+ 5) |',
          f'| Fil H07V-K 2,5 mm² rouge / noir | {fr(2 * ch["continu"]["longueur"] * 1.2 / 1000)} m de chaque |',
          '| Cosse fourche isolée bleue 1,5–2,5 mm² M3,5/M4 | 4 (+ 2) |',
          '| Embout 2,5 mm² | 4 (+ 2) |',
          f'| Fil H05V-K 1 mm² marron, bleu, vert-jaune | {fr(ch["secteur"]["longueur"] * 1.2 / 1000)} m de chaque |',
          '| Fil H07V-K 1,5 mm² vert-jaune (liaison du cadre) | 0,3 m |',
          '| Cosse femelle 6,3 mm entièrement isolée + 1 piggyback | 3 + 1 |',
          '| Cosse fourche isolée rouge 0,5–1,5 mm² M3,5 | 3 (+ 2) |',
          '| Cosse à œillet M5 1,5 mm² + rondelle à dents M5 DIN 6798 A | 1 + 1 |',
          '| Embase IEC C14 3-en-1 avec interrupteur et fusible (découpe 47,5 × 27,5) | 1 |',
          '| Fusible 5 × 20 T 3,15 A | 2 (1 de rechange) |',
          '| Gaine tressée Ø 6–10 mm, gaine thermo, colliers 2,5 × 100 | ~1 m, ~1 m, 50 |',
          '| Pince à sertir embouts (carrée) et pince pour contacts VH (0,5–1 mm²) | outillage |', '']
    open(os.path.join(ICI, 'faisceau.md'), 'w', encoding='utf-8').write('\n'.join(t))


def plan(fichier):
    """Plan coté, vue de dos : dalles et connecteurs (avec leur marge d'accès), profilés, brides,
    carte, alimentation, pieds, crochets, et le cheminement des câbles d'alimentation."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle
    fig, ax = plt.subplots(figsize=(15, 15), dpi=100)

    def rect(x0, y0, x1, y1, **kw):
        ax.add_patch(Rectangle((min(x0, x1), min(y0, y1)), abs(x1 - x0), abs(y1 - y0), **kw))

    m_ = P['MARGE_ACCES']
    for num, (cx, cy) in DALLES.items():
        rect(cx - H, cy - H, cx + H, cy + H, fc='#f4f4f4', ec='#888', lw=0.8)
        ax.text(cx - 80, cy + 80, f'dalle {num}', fontsize=10, color='#666')
        for nom, (x0, y0, x1, y1) in CONNECTEURS.items():
            rect(cx + x0 - m_, cy + y0 - m_, cx + x1 + m_, cy + y1 + m_, fc='none', ec='#e67700', lw=0.6, ls=':')
            rect(cx + x0, cy + y0, cx + x1, cy + y1, fc='#ffd8a8', ec='#e67700', lw=0.6)
        brides_ = ('LM', 'RM', 'BL', 'BR') + (('TL', 'TR') if cy < PAS / 2 and bride_longue(round(cy / PAS) + 1) else ())
        for k_, (x, y) in inserts(cx, cy).items():
            ax.plot(x, y, 'o', ms=4, mfc='#e03131' if k_ in brides_ else '#ffa94d', mec='k', mew=0.4)
        ax.annotate('', xy=(cx, cy + 30), xytext=(cx, cy + 5), arrowprops=dict(arrowstyle='->', color='#aaa'))
    for j in (-1, 0, 1):
        yc = j * PAS + P['Y_PROF']
        rect(-P['L_RANGEE'] / 2, yc - 10, P['L_RANGEE'] / 2, yc + 10, fc='#adb5bd', ec='#495057', alpha=0.85)
        yb = j * PAS + bas_bride(bride_longue(j))
        for xs in (-PAS / 2, PAS / 2):
            rect(xs - 20, yb, xs + 20, j * PAS + 8, fc='#ff8787', ec='#c92a2a', alpha=0.75)
        for sg in (-1, 1):
            rect(sg * 286.5, yb, sg * 265, j * PAS + 8, fc='#ff8787', ec='#c92a2a', alpha=0.75)
    y0m, y1m = P['Y_MONTANT']
    for xm in P['X_MONTANTS']:
        rect(xm - 10, y0m, xm + 10, y1m, fc='#868e96', ec='#212529', alpha=0.85)
    for i in (-1, 0, 1):
        for ys in (-PAS / 2, PAS / 2):
            rect(i * PAS - 16, ys - 16, i * PAS + 16, ys + 16, fc='#ffe066', ec='#e67700', alpha=0.9)
    x0, y0, l, h = CARTE
    rect(x0, y0 - h, x0 + l, y0, fc='#69db7c', ec='#2b8a3e', alpha=0.85)
    ax.text(x0 + l / 2 - 10, y0 - h / 2, f'CARTE {l:.0f} × {h:.0f}\n(plateau imprimé)', ha='center', va='center',
            fontsize=11, weight='bold')
    for n, (bx, by) in FIX['departs'].items():
        xa, ya = vers_assemblage(195, by - 2.5)
        rect(xa, ya - 6, xa + 15, ya + 6, fc='#2f9e44', ec='k', lw=0.5)
        ax.text(xa + 17, ya, f'D{n}', fontsize=8, va='center')
    L, W, X0 = P['ALIM_L'], P['ALIM_W'], P['ALIM_X0']
    rect(X0, Y_BAS - W / 2, X0 + L, Y_BAS + W / 2, fc='#ced4da', ec='#343a40', alpha=0.95)
    ax.text(X0 + L / 2, Y_BAS, 'ALIM 5 V 30 A\n(bornier à gauche)', ha='center', va='center', fontsize=11, weight='bold')
    sp, (xe, yc_, zc) = support_prise()
    rect(xe, yc_ - 24, xe + 32, yc_ + 24, fc='#343a40', ec='k')
    ax.text(xe, yc_ + 28, 'prise\nsecteur', fontsize=9)
    for x in (-P['X_CROCHETS'], P['X_CROCHETS']):
        rect(x - 17, Y_HAUT - 14, x + 17, Y_HAUT + 22, fc='#fab005', ec='#e67700')
    for x in (-P['X_PIEDS'], P['X_PIEDS']):
        rect(x - 12, -3 * PAS / 2 - P['GARDE_SOL'] - 12, x + 12, Y_BAS + 10, fc='#51cf66', ec='#2b8a3e', alpha=0.9)
    for xm in P['X_MONTANTS']:
        for yp in P['Y_PATINS']:
            rect(xm - 15, yp - P['PATIN_L'] / 2, xm + 15, yp + P['PATIN_L'] / 2, fc='#f76707', ec='#a63c06')
    # cheminement des câbles, calculé (cheminement()) : longueurs de coupe en mm
    ch = cheminement()
    for n in range(1, 10):
        r = ch[n]
        dk = (n - 5) * 1.3                       # décalage pour distinguer les câbles qui se suivent
        xs_, ys_ = zip(*[(x + dk, y + dk) for x, y in r['points']])
        ax.plot(xs_, ys_, '-', color='#c92a2a', lw=1.3, alpha=0.75)
        xf, yf = r['points'][-1]
        ax.text(xf + 9, yf - 22, f'{n} : {r["longueur"]}', fontsize=8, color='#c92a2a', weight='bold',
                backgroundcolor='#ffffffb0')
    for k in (4, 5, 6):
        r = ch[f'J{k}']
        xs_, ys_ = zip(*r['points'])
        ax.plot(xs_, ys_, '-', color='#495057', lw=4, alpha=0.45, solid_capstyle='butt')
        xf, yf = r['points'][-1]
        ax.text(xf + 12, yf + 6, f'J{k} : {r["longueur"]}', fontsize=8, color='#343a40', weight='bold',
                backgroundcolor='#ffffffb0')
    for clef, coul, txt in (('continu', '#1971c2', '5 V : 4 × 2,5 mm²'), ('secteur', '#9c36b5', 'secteur : 3 × 1 mm²')):
        r = ch[clef]
        xs_, ys_ = zip(*r['points'])
        ax.plot(xs_, ys_, '-', color=coul, lw=3, alpha=0.6)
        xm_, ym_ = r['points'][len(r['points']) // 2]
        ax.text(xm_ + 4, ym_ + 4, f'{txt}, {r["longueur"]}', color=coul, fontsize=8, weight='bold',
                backgroundcolor='#ffffffb0')
    def cote(xa, ya, xb, yb, t):
        ax.annotate('', xy=(xa, ya), xytext=(xb, yb), arrowprops=dict(arrowstyle='<->', lw=0.9))
        ax.text((xa + xb) / 2, (ya + yb) / 2 + 3, t, ha='center', va='bottom', fontsize=10, backgroundcolor='w')
    cote(-288, 300, 288, 300, '576 (3 × 192)')
    cote(P['X_MONTANTS'][0], -332, P['X_MONTANTS'][1], -332,
         f'montants : x = {P["X_MONTANTS"][0]:.0f} et {P["X_MONTANTS"][1]:+.0f}, entraxe {P["X_MONTANTS"][1] - P["X_MONTANTS"][0]:.0f}')
    cote(-P['X_PIEDS'], -352, P['X_PIEDS'], -352, f'pieds : entraxe {2 * P["X_PIEDS"]:.0f}')
    ax.text(-300, 312, 'VUE DE DOS — flèches des dalles vers le haut', fontsize=12, weight='bold')
    leg = [('#ff8787', 'brides de rangée (imprimées)'), ('#ffe066', 'agrafes de joint'),
           ('#adb5bd', 'profilés 2020 de rangée (570)'), ('#868e96', f'montants 2020 ({y1m - y0m:.0f})'),
           ('#69db7c', 'carte'), ('#2f9e44', 'départs vers les dalles'), ('#ffd8a8', 'connecteurs des dalles'),
           ('none', 'marge d\'accès de 8 mm (pointillés)'), ('#fab005', 'crochets muraux'), ('#f76707', 'patins arrière (à plat, au mur)'),
           ('#51cf66', 'pieds, sur une table'),
           ('#c92a2a', 'câbles des dalles (n° : coupe en mm)'), ('#1971c2', 'alim → carte (J1)'),
           ('#9c36b5', 'secteur → alim'), ('#868e96', 'nappes HUB75 carte → 1re dalle')]
    for k_, (c, t) in enumerate(leg):
        rect(300, 280 - 17 * k_, 312, 290 - 17 * k_, fc=c, ec='k', lw=0.5)
        ax.text(316, 285 - 17 * k_, t, fontsize=9, va='center')
    ax.set_xlim(-340, 420)
    ax.set_ylim(-368, 325)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.savefig(fichier, bbox_inches='tight')
    plt.close(fig)


def debit(lignes):
    nb_rangee, nb_montant = 3, 2
    l_montant = P['Y_MONTANT'][1] - P['Y_MONTANT'][0]
    nb_eq = sum(len(c) for c in EQUERRES.values())
    # par rangée : 2 brides intérieures à 2 rangs de trous, 2 de bord à 1 rang
    n_m3 = sum(6 * len(trous_bride(bride_longue(j))) for j in (-1, 0, 1))
    vm = vis_m5(pieds=True)
    par_lg = {}                                  # longueur -> {pièce: nombre de vis}
    for nv, lg, *_ in vm + [vis_terre()]:
        par_lg.setdefault(lg, {}).setdefault(nv, 0)
        par_lg[lg][nv] += 1
    lignes_m5 = [f'| Vis M5 × {lg} tête cylindrique (DIN 912 / ISO 4762) | {sum(c.values())} | '
                 + ', '.join(f'{nv} {n}' for nv, n in c.items()) + ' |' for lg, c in sorted(par_lg.items(), reverse=True)]
    t = ['# Cadre arrière v4 — débit et quincaillerie', '',
         'Généré par `gen_cadre.py` ; cotes des dalles reprises du cadre v3. Les quantités de visserie '
         'comptent chaque emploi, prévoir 10 % de plus.', '',
         '## Profilés aluminium 2020, rainure B 6 mm, coupés à longueur', '',
         '| Repère | Longueur | Qté | Rôle |', '|---|---|---|---|',
         f'| P1 | {P["L_RANGEE"]:.0f} mm | {nb_rangee} | profilé de rangée, axe à 40 mm sous le centre des dalles |',
         f'| P2 | {l_montant:.0f} mm | {nb_montant} | montant, en x = {P["X_MONTANTS"][0]:.0f} et {P["X_MONTANTS"][1]:+.0f} '
         '(vue de dos), derrière les profilés de rangée, dans les bandes libres entre les connecteurs des dalles |',
         '', f'Total : {fr((nb_rangee * P["L_RANGEE"] + nb_montant * l_montant) / 1000, 2)} m. Coupe d\'équerre, sans taraudage.', '',
         '## Quincaillerie', '', '| Pièce | Qté | Emploi |', '|---|---|---|',
         f'| Équerre 2020 (fonte d\'alu, 20 × 20) | {nb_eq + 1} | croisements montant / profilé : 2 en haut et au '
         'milieu droit, 1 au milieu gauche (plateau de la carte) et en bas (pied, alimentation) ; 1 de réserve. '
         '**Limer les 2 ergots de centrage d\'une aile** : contre le flanc du montant, ils tombent en travers '
         'de sa rainure |',
         f'| Écrou marteau M5 pour rainure B 6 | {len(vm) + 1} | un par vis M5 |',
         *lignes_m5,
         f'| Vis M4 × 8 tête cylindrique basse (DIN 7984) et écrou marteau M4 | {2 * nb_eq} de chaque | équerres, '
         'avec leur kit de fixation |',
         f'| Vis M3 × 10 tête fraisée (DIN 965 / ISO 10642) | {n_m3 + 12} | dans les inserts des dalles : '
         f'brides {n_m3}, agrafes 12 |',
         f'| Vis M3 × {P["M3_VIS_ALIM"]} tête fraisée (DIN 965 / ISO 10642) | {len(P["ALIM_TROUS"])} | '
         'alimentation, à travers la joue de ses berceaux côté bas de l\'écran, dans les trous taraudés de '
         f'son flanc ; elles y entrent de {fr(P["M3_VIS_ALIM"] - 5.0)} mm |',
         '| Vis M3 × 8 autotaraudeuse pour plastique, ou M3 × 8 | 6 | carte sur son plateau |',
         f'| Vis M4 × 12 autotaraudeuse pour plastique | {2 * sum(s for _, s in P["BERCEAUX"])} | '
         'sangle de l\'alimentation |',
         '| Adhésif double face (type 3M VHB) | 8 morceaux de 28 × 22 mm | colle les patins TPU : 4 sous les pieds, '
         '1 sur chaque patin arrière. À défaut de TPU : 8 patins caoutchouc adhésifs Ø 15 à 20 mm, 3 mm d\'épaisseur |',
         '| Vis Ø 5 × 50 + chevilles adaptées au mur | 2 | accrochage mural |',
         '', f'Vis M5 : toutes à tête cylindrique, dessous plat ; aucune pièce n\'a de fraisure M5. Leur longueur '
         f'dépend de l\'épaisseur de pièce sous la tête : la vis doit en dépasser de {fr(P["ECROU_H"])} mm au moins, '
         f'pour traverser tout l\'écrou marteau, et de {fr(P["FOND_RAINURE"] - 0.3)} mm au plus, car le fond de la '
         f'rainure est à {fr(P["FOND_RAINURE"])} mm de la face : une vis plus longue y bute avant de serrer. '
         'Le générateur le vérifie pour chaque vis.',
         '', '## Pièces imprimées (PETG conseillé : tenue en température et au fluage ; patins en TPU)', '',
         '| Fichier | Qté | Encombrement (mm) | Volume (cm³) | Remarque |', '|---|---|---|---|---|']
    for nom, qte, e, vol, rem in lignes:
        t.append(f'| `stl/{nom}.stl` | {qte} | {e[0]:.0f} × {e[1]:.0f} × {e[2]:.0f} | {vol:.0f} | {rem} |')
    t += ['', 'Réglages : 4 périmètres, 30 à 40 % de remplissage gyroïde, buse 0,4. Tout tient sur le plateau '
          'de 256 mm de l\'A1, sans support.', '',
          f'Patins TPU : TPU 95A, {fr(P["EP_PATIN"])} mm d\'épaisseur, remplissage 100 %, 20 à 30 mm/s, depuis la '
          'bobine externe de l\'A1 (l\'AMS lite ne prend pas le TPU). Les coller sur une face dégraissée à '
          'l\'alcool. Garder cette épaisseur : l\'assemblage la compte entre le plan arrière et le sol.', '']
    open(os.path.join(ICI, 'debit.md'), 'w', encoding='utf-8').write('\n'.join(t))


if __name__ == '__main__':
    elems = assemblage(mode='table')
    print('contrôles, écran sur ses pieds, sur une table :')
    ok_sol = controles(elems)
    ok_sol = stabilite(elems, 'table') and ok_sol
    elems_plat = assemblage(mode='plat')
    print('contrôles, écran à plat sur le sol ou au mur, sur ses patins arrière :')
    ok_mur = controles(elems_plat)
    ok_mur = stabilite(elems_plat, 'plat') and ok_mur
    if not (ok_sol and ok_mur):
        print('⚠ contrôles en échec')
        if '--forcer' not in sys.argv:
            sys.exit(1)
    else:
        print('  tout est en ordre')
    lignes = exporte_stl()
    print(f'{len(lignes)} pièces exportées dans stl/')
    debit(lignes)
    faisceau(cheminement())
    exporte_glb(elems)
    os.makedirs(DOCS, exist_ok=True)
    plan(os.path.join(DOCS, 'cadre_plan.png'))
    rendu(elems, os.path.join(DOCS, 'cadre_dos.png'), 22, -58, 'Cadre v4 — vue de dos : alimentation, carte, pieds de table')
    rendu(elems, os.path.join(DOCS, 'cadre_profil.png'), 4, 0, 'Profil (côté droit, vue de dos) : pied, montant, profilés, dalles')
    rendu(elems_plat, os.path.join(DOCS, 'cadre_mur.png'), 18, -122, 'Au mur : crochets en haut, patins arrière contre le mur')
    rendu(couche_au_sol(elems_plat), os.path.join(DOCS, 'cadre_plat.png'), 16, -38,
          'À plat sur le sol, LED vers le haut : posé sur les 4 patins arrière',
          roi=((-340, 340), (-340, 340), (-115, 25)))
    rendu(elems, os.path.join(DOCS, 'cadre_face.png'), 12, 70, 'Vue de face : seuls les pieds dépassent, de 35 mm')
    print('vues et plan dans ../docs/')
