#!/usr/bin/env python3
"""
Écrit le circuit imprimé de la carte (carte-ecran.kicad_pcb) : contour, placement, équipotentielles,
pistes de puissance, routage (routeur.py), plans de masse, sérigraphie, contrôle.

Repère : vue de dessus de la carte = vue de DOS de l'écran (la carte est vissée composants vers
l'arrière). x vers la droite, y vers le bas (convention KiCad), origine au coin haut-gauche.

    python3 gen_pcb.py              tout : placement, puissance, routage, zones
    python3 gen_pcb.py --placement  placement seul, pour regarder
"""
import os
import re
import sys
import uuid

import pcbnew

import conception as K
import kicadlib

ICI = os.path.dirname(os.path.abspath(__file__))
FPD = os.environ.get('KICAD9_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
NS = uuid.UUID('6f1c2b4e-9a57-4d0b-8f3e-2c1d0e5a7b90')
PCB = os.path.join(ICI, K.PROJET + '.kicad_pcb')

# Carte de 198 × 135 mm, logée entre deux bandes de connecteurs des dalles (rangée du milieu au-dessus,
# rangée du bas au-dessous) : rien ne dépasse de ses bords haut et bas. Les départs vers les dalles sont
# sur le bord droit, dont les câbles partent dans une zone sans connecteur.
LARG, HAUT = 198.0, 135.0
COULOIRS = {0: 4.0, 1: 41.0, 2: 78.0}     # haut de chaque couloir de nœud (37 mm)
ORDRE_DEPARTS = [7, 8, 9, 4, 5, 6, 1, 2, 3]   # de haut en bas : les rangées du haut, du milieu, du bas
Y_DEPART = [10.2 + 13.3 * i for i in range(9)]
TROUS = [(4.0, 131.0), (52.0, 131.0), (100.0, 131.5), (60.0, 3.0), (118.0, 39.0), (150.5, 60.0)]


def cale(v, pas=0.254):
    return round(round(v / pas) * pas, 4)


def uid(*parts):
    return str(uuid.uuid5(NS, '/'.join(str(p) for p in parts)))


def mm(v):
    return pcbnew.FromMM(float(v))


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# ------------------------------------------------------------------ placement
def placement():
    """{ref: (x, y, orientation)}, tout sur la face du dessus."""
    P = {}
    # --- départs, sur le bord droit : fusible horizontal (borne 1 dans le bus), bornier enfiché
    #     par la droite, voyant de fusible fondu entre deux rangées
    for i, n in enumerate(ORDRE_DEPARTS):
        y = Y_DEPART[i]
        P[f'J{10 + n}'] = (187.0, y, 90)
        P[f'F{n}'] = (167.0, y - 4.2, 0)
        P[f'R{10 + n}'] = (171.5, y + 4.2, 0)
        P[f'LED{10 + n}'] = (175.5, y + 4.2, 180)
    P['J1'] = (162.0, 128.3, 0)                       # entrée : fils par le bas, l'alim est dessous
    P['C1'] = (157.0, 8.0, 180)                       # + dans le bus, − dans la masse à gauche
    P['C2'] = (157.0, 20.0, 180)
    P['D1'] = (156.0, 30.5, 180)
    P['R1'] = (150.5, 30.5, 0)
    P['LED1'] = (150.5, 34.0, 180)
    # --- alimentation des Pico -----------------------------------------------------------
    P['F10'] = (152.5, 40.0, 180)
    P['FB1'] = (146.5, 40.0, 0)
    P['C4'] = (145.0, 46.5, 0)
    P['C5'] = (140.0, 40.0, 90)
    # --- couloirs des nœuds -------------------------------------------------------------
    for k, y0 in COULOIRS.items():
        yc = y0 + 18.5
        P[f'J{4 + k}'] = (5.08, y0 + 6.5, 0)
        for rang, g_ in enumerate([1, 0, 3, 2]):
            P[f'RN{4 + 4 * k + g_}'] = (19.0, y0 + 5.0 + 6.4 * rang, 180)
        P[f'U{5 + 2 * k}'] = (30.5, y0 + 8.0, 180)
        P[f'U{6 + 2 * k}'] = (30.5, y0 + 21.5, 180)
        P[f'C{20 + 2 * k}'] = (25.5, y0 + 14.0, 90)
        P[f'C{21 + 2 * k}'] = (25.5, y0 + 27.5, 90)
        P[f'U{2 + k}'] = (93.0, yc - 8.89, 270)        # Pico couché, USB vers la droite
        P[f'JP{1 + k}'] = (36.0, y0 + 2.2, 90)
        base = 40 + 10 * k
        P[f'Q{1 + k}'] = (35.5, y0 + 32.0, 0)
        P[f'R{base + 0}'] = (35.5, y0 + 28.0, 0)
        P[f'R{base + 1}'] = (39.5, y0 + 32.0, 90)
        P[f'R{base + 2}'] = (14.5, y0 + 32.5, 0)
        P[f'R{base + 3}'] = (39.5, y0 + 25.0, 90)
        P[f'R{base + 4}'] = (42.0, y0 + 33.5, 0)
        P[f'R{base + 5}'] = (60.0, y0 + 33.5, 0)
        P[f'R{base + 6}'] = (104.0, y0 + 28.5, 0)
        P[f'D{3 + k}'] = (104.0, y0 + 33.0, 180)
        P[f'C{7 + k}'] = (98.0, y0 + 33.0, 0)
        P[f'SW{2 + k}'] = (110.0, y0 + 23.5, 0)
    # --- tête : debout, USB en haut, antenne au bord bas ----------------------------------
    yt = HAUT - 50.73
    P['U1'] = (130.0, yt, 0)
    for k in range(3):
        P[f'RN{1 + k}'] = (123.0, yt + 4.5 + 7.62 * k, 180)
    for k in range(3):
        P[f'R{30 + k}'] = (121.5, yt + 25.7 + 3.0 * k, 0)
        P[f'R{33 + k}'] = (116.0, yt + 25.7 + 3.0 * k, 0)
    P['D2'] = (145.0, 79.0, 0)
    P['C6'] = (141.5, 74.5, 0)
    P['R2'] = (151.3, 101.5, 90)
    P['R3'] = (151.3, 105.5, 90)
    P['C3'] = (151.3, 109.5, 90)
    P['SW1'] = (128.5, 72.5, 0)
    P['J7'] = (128.5, 3.0, 0)
    P['J3'] = (136.0, 3.0, 0)
    P['J2'] = (146.0, 3.0, 0)
    for i, (x, y) in enumerate(TROUS):
        P[f'H{1 + i}'] = (x, y, 0)
    return P


# ------------------------------------------------------------------ netlist KiCad
def lit_netlist():
    chemin = os.path.join(ICI, 'build', K.PROJET + '.net')
    s = open(chemin, encoding='utf-8').read()
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


def construit():
    b = pcbnew.BOARD()
    ds = b.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_MinClearance = mm(0.2)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.4)
    ds.m_HoleClearance = mm(0.25)
    ds.m_HoleToHoleMin = mm(0.3)
    ds.m_SolderMaskMinWidth = mm(0)
    # sérigraphie : minima de JLCPCB (texte de 1 mm de haut, trait de 0,15 mm), vérifiés par le DRC
    ds.m_MinSilkTextHeight = mm(1.0)
    ds.m_MinSilkTextThickness = mm(0.15)
    # classes d'équipotentielles
    nc = ds.m_NetSettings
    defaut = nc.GetDefaultNetclass()
    defaut.SetTrackWidth(mm(0.25))
    defaut.SetClearance(mm(0.2))
    defaut.SetViaDiameter(mm(0.6))
    defaut.SetViaDrill(mm(0.3))
    for nom, larg, via, perc, clr in [('Alim', 0.5, 0.6, 0.3, 0.2), ('Puissance', 2.0, 1.0, 0.5, 0.2)]:
        cl = pcbnew.NETCLASS(nom)
        cl.SetTrackWidth(mm(larg))
        cl.SetClearance(mm(clr))
        cl.SetViaDiameter(mm(via))
        cl.SetViaDrill(mm(perc))
        nc.SetNetclass(nom, cl)
    # équipotentielles
    pads = lit_netlist()
    noms = sorted(set(pads.values()))
    nets = {}
    for n in noms:
        ni = pcbnew.NETINFO_ITEM(b, n)
        b.Add(ni)
        nets[n] = ni
    for n in noms:
        if n in ('+5V', 'GND') or n.startswith('+5V_D'):
            nc.SetNetclassPatternAssignment(n, 'Puissance') if hasattr(nc, 'SetNetclassPatternAssignment') else None
    P = placement()
    for cp in K.COMPOSANTS:
        if not cp.footprint:
            continue
        fp = charge_empreinte(cp.footprint)
        fp.SetReference(cp.ref)
        fp.SetValue(cp.value)
        fp.SetPath(pcbnew.KIID_PATH('/' + uid('sym', cp.ref)))
        x, y, o = P[cp.ref]
        if any(pd.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for pd in fp.Pads()):
            x, y = cale(x), cale(y)        # traversants sur la grille du routeur
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(o)
        for pad in fp.Pads():
            n = pads.get((cp.ref, pad.GetNumber()))
            if n:
                pad.SetNet(nets[n])
        b.Add(fp)
    # contour
    for (x0, y0, x1, y1) in [(0, 0, LARG, 0), (LARG, 0, LARG, HAUT), (LARG, HAUT, 0, HAUT), (0, HAUT, 0, 0)]:
        s = pcbnew.PCB_SHAPE(b)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x0, y0))
        s.SetEnd(pt(x1, y1))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        b.Add(s)
    return b, nets



# ------------------------------------------------------------------ cuivre de puissance
def piste(b, net, pts, larg, couche=pcbnew.F_Cu):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pt(x0, y0))
        t.SetEnd(pt(x1, y1))
        t.SetWidth(mm(larg))
        t.SetLayer(couche)
        t.SetNet(net)
        b.Add(t)


def via(b, net, x, y, diam=0.6, percage=0.3):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pt(x, y))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetWidth(mm(diam))
    v.SetDrill(mm(percage))
    v.SetNet(net)
    b.Add(v)
    return v


def pastille(b, ref, num):
    fp = b.FindFootprintByReference(ref)
    res = [p for p in fp.Pads() if p.GetNumber() == num]
    return [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in res]


BUS = (154.0, 1.0, 168.6, 131.5)        # bus +5 V vertical, face avant, de J1 (en bas) aux fusibles
BUS_DESSOUS = (154.0, 58.0, 168.6, 131.5)  # renfort du bus sur la face arrière, côté entrée
VIAS_PICO = []                          # vias de masse des Pico : la face arrière reste libre autour


def zone(b, net, couche, rect=None, poly=None, priorite=0, solide=False, clr=0.3, nom=''):
    z = pcbnew.ZONE(b)
    z.SetLayer(couche)
    if net is not None:
        z.SetNet(net)
    o = z.Outline()
    o.NewOutline()
    pts = poly or [(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3])]
    for x, y in pts:
        o.Append(mm(x), mm(y))
    z.SetAssignedPriority(priorite)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solide else pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.4))
    z.SetThermalReliefSpokeWidth(mm(0.6))
    z.SetMinThickness(mm(0.25))
    z.SetLocalClearance(mm(clr))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetZoneName(nom)
    b.Add(z)
    return z


def regle_antenne(b):
    """Zone interdite sous l'antenne du Pico 2 W de la tête : ni piste, ni via, ni cuivre coulé."""
    fp = b.FindFootprintByReference('U1')
    x0 = pcbnew.ToMM(fp.GetPosition().x)
    y0 = pcbnew.ToMM(fp.GetPosition().y)
    z = pcbnew.ZONE(b)
    z.SetIsRuleArea(True)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowCopperPour(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    z.SetZoneName('antenne tete')
    o = z.Outline()
    o.NewOutline()
    for x, y in [(x0 + 1.6, y0 + 40.0), (x0 + 16.2, y0 + 40.0), (x0 + 16.2, HAUT), (x0 + 1.6, HAUT)]:
        o.Append(mm(x), mm(y))
    b.Add(z)


def libre_de_pastilles(b, x, y, r):
    """Vrai si aucune pastille ne passe à moins de r mm de (x, y)."""
    import routeur as R
    from shapely.geometry import Point
    pt_ = Point(x, y)
    for fp in b.GetFootprints():
        for pd in fp.Pads():
            for L in R.COUCHES:
                if pd.IsOnLayer(L):
                    g = R.poly_kicad(pd.GetEffectivePolygon(L, pcbnew.ERROR_INSIDE))
                    if g is not None and g.distance(pt_) < r:
                        return False
            if pd.GetDrillSize().x > 0:
                p = pd.GetPosition()
                if pt_.distance(Point(pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))) < r + pcbnew.ToMM(pd.GetDrillSize().x) / 2:
                    return False
    return True


def puissance(b, nets):
    """Liaisons fusible -> bornier, masse des borniers, couture du bus : posés avant le routage."""
    for n in K.DALLES:
        f2 = sorted(pastille(b, f'F{n}', '2'), key=lambda p: p[1])
        o1 = pastille(b, f'J{10 + n}', '1')[0]
        net = nets[f'+5V_D{n}']
        piste(b, net, [f2[0], f2[1]], 2.0)
        xa, ya = f2[1]
        piste(b, net, [(xa, ya), (o1[0] - 3.0, ya), (o1[0], o1[1])], 2.0)
        o2 = pastille(b, f'J{10 + n}', '2')[0]
        via(b, nets['GND'], o2[0] - 4.6, o2[1], 0.8, 0.4)
        via(b, nets['GND'], o2[0] - 4.6, o2[1] - 1.6, 0.8, 0.4)
    # Sous chaque tampon 74AHCT541 (broches au pas de 0,65 mm) : /OE1 (1) à la masse par un via sous
    # la puce, /OE2 (19) et la masse (10, et 8-9 pour le tampon B) descendus par un via sous le
    # boîtier. /OE2 est coincée entre une sortie et VCC : sans ce via, on ne l'atteint pas.
    for k in range(3):
        for ref, est_b in ((f'U{5 + 2 * k}', False), (f'U{6 + 2 * k}', True)):
            fp = b.FindFootprintByReference(ref)
            xc = pcbnew.ToMM(fp.GetPosition().x)
            P_ = {pd.GetNumber(): (pcbnew.ToMM(pd.GetPosition().x), pcbnew.ToMM(pd.GetPosition().y))
                  for pd in fp.Pads()}
            net_ = lambda num: nets[[pd for pd in fp.Pads() if pd.GetNumber() == num][0].GetNetname()]
            x1_, y1_ = P_['1']
            sens = 1 if P_['2'][1] > y1_ else -1                    # vers l'intérieur de la rangée
            piste(b, nets['GND'], [(x1_, y1_), (xc + 1.9, y1_), (xc + 1.0, y1_ + sens * 0.55)], 0.25)
            via(b, nets['GND'], xc + 1.0, y1_ + sens * 0.55)
            x19, y19 = P_['19']
            piste(b, net_('19'), [(x19, y19), (xc - 1.9, y19), (xc - 0.9, y19 - 0.6)], 0.25)
            via(b, net_('19'), xc - 0.9, y19 - 0.6)
            gnd = ['10', '9', '8'] if est_b else ['10']
            x10, y10 = P_['10']
            for num in gnd:
                xg, yg = P_[num]
                piste(b, nets['GND'], [(xg, yg), (xc + 1.9, yg)], 0.25)
            ys = [P_[n_][1] for n_ in gnd]
            if len(gnd) > 1:
                piste(b, nets['GND'], [(xc + 1.9, min(ys)), (xc + 1.9, max(ys))], 0.25)
            if len(gnd) == 1:
                # tampon A : la broche 10 est en bout de rangée, le via se loge au-dessus d'elle
                piste(b, nets['GND'], [(xc + 1.9, y10), (xc + 1.0, y10 - 0.55)], 0.25)
                via(b, nets['GND'], xc + 1.0, y10 - 0.55)
            else:
                yv = (min(ys) + max(ys)) / 2
                piste(b, nets['GND'], [(xc + 1.9, yv), (xc + 1.0, yv)], 0.25)
                via(b, nets['GND'], xc + 1.0, yv)
    # Chaque broche de masse de chaque Pico descend au plan par son propre via, posé avant le
    # routage, sous le module (entre ses deux rangées) : sans cela, les pistes qui passent entre les
    # broches peuvent encercler une masse sur les deux faces. Côté antenne de la tête, vers l'extérieur.
    for ref in ('U1', 'U2', 'U3', 'U4'):
        fp = b.FindFootprintByReference(ref)
        pads = {pd.GetNumber(): pd for pd in fp.Pads()}
        x1, y1 = [pcbnew.ToMM(v) for v in pads['1'].GetPosition()]
        x40, y40 = [pcbnew.ToMM(v) for v in pads['40'].GetPosition()]
        for num in K.PICO_GND:
            pd = pads[num]
            px, py = pcbnew.ToMM(pd.GetPosition().x), pcbnew.ToMM(pd.GetPosition().y)
            # vers l'autre rangée : de la rangée de la broche 1 vers celle de la broche 40, ou l'inverse
            if int(num) <= 20:
                ux, uy = x40 - x1, y40 - y1
            else:
                ux, uy = x1 - x40, y1 - y40
            n_ = (ux * ux + uy * uy) ** 0.5
            ux, uy = ux / n_, uy / n_
            if ref == 'U1' and num in ('18', '23'):
                ux, uy = -ux, -uy                      # zone d'antenne : on sort vers l'extérieur
            vx, vy = px + 2.0 * ux, py + 2.0 * uy
            piste(b, nets['GND'], [(px, py), (vx, vy)], 0.4)
            via(b, nets['GND'], vx, vy)
            VIAS_PICO.append((vx, vy))
    # couture du bus avant / arrière, hors des pastilles
    y = BUS_DESSOUS[1] + 2
    while y < BUS_DESSOUS[3] - 2:
        for x in (156.2, 159.6):
            if libre_de_pastilles(b, x, y, 0.9):
                via(b, nets['+5V'], x, y, 1.0, 0.5)
        y += 5.0


def plans(b, nets):
    contour = (0.0, 0.0, LARG, HAUT)
    zone(b, nets['+5V'], pcbnew.F_Cu, BUS, priorite=2, solide=True, clr=0.3, nom='bus 5V')
    zone(b, nets['+5V'], pcbnew.B_Cu, BUS_DESSOUS, priorite=2, solide=True, clr=0.3, nom='bus 5V dessous')
    zone(b, nets['GND'], pcbnew.F_Cu, contour, priorite=0, solide=False, clr=0.3, nom='masse dessus')
    zone(b, nets['GND'], pcbnew.B_Cu, contour, priorite=0, solide=False, clr=0.3, nom='masse dessous')


# ------------------------------------------------------------------ routage
# +5V_LOG reste en piste fine : il dessert les broches au pas de 0,65 mm des tampons, et ses
# 0,6 A tiennent dans 0,25 mm de cuivre de 70 µm.
CLASSE_ALIM = {'+5V_PF', 'VSYS_T', 'VSYS_N0', 'VSYS_N1', 'VSYS_N2'}


def classe_de(net):
    return 'a' if net in CLASSE_ALIM else 'p'


def ordre_routage():
    nets = K.nets()

    def cle(n):
        if n.startswith(('N0_', 'N1_', 'N2_')):
            return (0, n)
        if n in CLASSE_ALIM:
            return (1, n)
        if n.startswith(('L0_', 'L1_', 'L2_', 'T_L', 'VSYNC', 'RDY')):
            return (2, n)
        return (3, n)
    return sorted([n for n, v in nets.items() if n != 'GND' and len(v) >= 2], key=cle)


def remplit(chemin):
    """Remplit les zones d'une carte enregistrée : rechargée par LoadBoard, qui initialise la
    connectivité (le remplissage d'une carte construite en mémoire fait planter pcbnew)."""
    b = pcbnew.LoadBoard(chemin)
    b.BuildConnectivity()
    f = pcbnew.ZONE_FILLER(b)
    f.Fill(b.Zones())
    pcbnew.SaveBoard(chemin, b)
    return b


SERI_H, SERI_TRAIT = 1.0, 0.15        # minima de sérigraphie de JLCPCB


def texte(b, t, x, y, h=1.0, angle=0, gras=False, couche=pcbnew.F_SilkS, justif=None):
    h = max(h, SERI_H)
    tx = pcbnew.PCB_TEXT(b)
    tx.SetText(t)
    tx.SetPosition(pt(x, y))
    tx.SetLayer(couche)
    tx.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
    tx.SetTextThickness(mm(max(SERI_TRAIT, h * (0.2 if gras else 0.15))))
    tx.SetTextAngleDegrees(angle)
    if justif == 'g':
        tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    elif justif == 'd':
        tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    b.Add(tx)
    return tx


def serigraphie(b):
    """Repères des petits composants sur le calque de montage (F.Fab) ; marquages utiles en
    sérigraphie : dalles, polarités, rôles des Pico, sonde, avertissements. Le titre et les notes
    vont au dos, qui n'a que le plan de masse."""
    for fp in b.GetFootprints():
        for g in fp.GraphicalItems():          # traits des empreintes de bibliothèque : 0,12 -> 0,15 mm
            if g.GetClass() == 'PCB_SHAPE' and g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) \
                    and 0 < g.GetWidth() < mm(SERI_TRAIT):
                g.SetWidth(mm(SERI_TRAIT))
        ref = fp.Reference()
        r = fp.GetReference()
        if r.startswith('H'):
            ref.SetVisible(False)
        elif ((r.startswith(('U', 'J')) and not r.startswith('JP') and not (r[1:].isdigit() and int(r[1:]) >= 10))
              or (r.startswith('F') and r[1:].isdigit() and int(r[1:]) <= 9)):
            ref.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
            ref.SetTextThickness(mm(0.15))
        else:
            ref.SetLayer(pcbnew.F_Fab)
    # départs : numéro de dalle et polarité, entre le fusible et son bornier
    for n in K.DALLES:
        x1, y1 = pastille(b, f'J{10 + n}', '1')[0]
        x2, y2 = pastille(b, f'J{10 + n}', '2')[0]
        texte(b, '+', x1 - 3.7, y1, 1.0, gras=True)
        texte(b, '−', x2 - 3.7, y2, 1.0, gras=True)
        texte(b, f'D{n}', x1 - 3.7, (y1 + y2) / 2, 0.9, gras=True)
    # entrée, le long du bus
    texte(b, 'ENTRÉE 5 V — 30 A', 156.0, 100.0, 1.1, angle=90, gras=True)
    texte(b, 'J1 : + + − −  (de gauche à droite)', 158.4, 100.0, 0.9, angle=90)
    texte(b, 'ALIM RÉGLÉE ≤ 5,25 V', 160.8, 100.0, 0.9, angle=90, gras=True)
    # nœuds et tête
    noms = {0: ('NŒUD 0 · rangée du HAUT', 'DALLE 7 (haut)'), 1: ('NŒUD 1 · rangée du MILIEU', 'DALLE 4 (milieu)'),
            2: ('NŒUD 2 · rangée du BAS', 'DALLE 1 (bas)')}
    for k, (titre, dalle) in noms.items():
        x1, y1 = pastille(b, f'U{2 + k}', '1')[0]
        x40, y40 = pastille(b, f'U{2 + k}', '40')[0]
        cx, cy = x1 - 28.0, (y1 + y40) / 2
        texte(b, titre, cx, cy - 1.4, 1.2, gras=True)
        texte(b, 'Pico 2 · USB à droite →', cx, cy + 1.3, 1.0)
        jx, jy = pastille(b, f'J{4 + k}', '1')[0]
        texte(b, f'HUB75 → {dalle}', 13.4, jy + 8.9, 0.9, angle=90)
        xa, ya = pastille(b, f'JP{1 + k}', '1')[0]
        xc, yc = pastille(b, f'JP{1 + k}', '3')[0]
        texte(b, 'G1', min(xa, xc) - 1.6, ya, 0.9, gras=True, justif='d')
        texte(b, 'GP1', xa, ya + 2.4, 1.0)
        texte(b, 'GP15', xc, yc + 2.4, 1.0)
        sx, sy = pastille(b, f'SW{2 + k}', '1')[0]
        texte(b, f'RESET N{k}', sx + 3.25, sy + 6.4, 0.8)
    x1, y1 = pastille(b, 'U1', '1')[0]
    texte(b, 'TÊTE · Pico 2 W · USB en haut', x1 + 8.9, y1 + 21.0, 1.1, angle=90, gras=True)
    sx, sy = pastille(b, 'SW1', '1')[0]
    texte(b, 'RESET TÊTE', sx + 3.25, sy - 2.0, 0.8)
    # sonde, extension, potentiomètre
    noms_sonde = ['VSYNC', 'RDY0', 'RDY1', 'RDY2', 'T.17', 'T.18', 'N0.17', 'N0.18', 'N1.17', 'N1.18', 'N2.17', 'N2.18']
    for i, nom in enumerate(noms_sonde):
        x, y = pastille(b, 'J7', str(2 * i + 1))[0]
        texte(b, nom, x - 1.6, y, 0.8, justif='d')
    for i, nom in enumerate(['3V3', 'GP16', 'GP19', 'GP20', 'GP21', 'GP22', 'GP28', 'GND']):
        x, y = pastille(b, 'J3', str(i + 1))[0]
        texte(b, nom, x + 1.5, y, 0.8, justif='g')
    for i, nom in enumerate(['3V3', 'POT', 'GND']):
        x, y = pastille(b, 'J2', str(i + 1))[0]
        texte(b, nom, x - 1.5, y, 0.8, justif='d')
    # dos : titre et notes (texte en miroir, lisible depuis le dos)
    dos = [(f"CARTE ÉCRAN 3×3 — rév. {K.REVISION} — {K.DATE}", 1.6, True),
           ('Game Boy Pocket déstructurée · module écran · regénérée par construire.sh', 1.0, False),
           ('Fusibles lame mini (ATM) 7,5 A : voyant rouge allumé = fusible fondu', 1.0, False),
           ('Alimentation : régler la sortie entre 5,05 et 5,25 V, jamais au-delà', 1.0, False),
           ('Cuivre 70 µm (2 oz) : le bus porte jusqu\'à 36 A', 1.0, False),
           ('Antenne de la tête (bas droite) : ni cuivre ni métal dessous', 1.0, False)]
    # entre les deux rangées du Pico du nœud 0, où le dos n'a que le plan de masse
    y = 55.0
    for t, h, gras in dos:
        tx = texte(b, t, 70.0, y, h, gras=gras, couche=pcbnew.B_SilkS)
        tx.SetMirrored(True)
        y += h * 1.85


def regles_drc():
    """Règles propres à la carte : un seul rayon thermique suffit aux pastilles de masse cernées."""
    open(os.path.join(ICI, K.PROJET + '.kicad_dru'), 'w').write(
        '(version 1)\n'
        '(rule "rayons thermiques"\n\t(constraint min_resolved_spokes 1))\n')


if __name__ == '__main__':
    import time
    import routeur as R
    from shapely.geometry import box as rect_
    t0 = time.time()
    kicadlib.empreintes_locales()
    b, nets = construit()
    if '--placement' in sys.argv:
        pcbnew.SaveBoard(PCB, b)
        print('placement écrit :', PCB)
        sys.exit(0)
    puissance(b, nets)
    plans(b, nets)
    regles_drc()
    from shapely.geometry import Point as Pt_
    rt = R.Routeur(b, LARG, HAUT, classe_de,
                   interdits=[(Pt_(x, y).buffer(2.0), [1]) for x, y in VIAS_PICO])
    echecs = rt.route(ordre_routage(), passes=8)
    print(f'routage : {time.time() - t0:.0f} s, {len(echecs)} échecs')
    for net, p in echecs:
        print('  échec :', net, p.nom)
    n, manque = rt.vias_de_masse()
    print(f'{n} vias de masse, {len(manque)} sans via :', [p.nom for p in manque])
    nc = rt.couture(eviter=[rect_(*BUS)])
    print(f'{nc} vias de couture')
    rt.ecrit(b, nets, piste, via)
    serigraphie(b)
    pcbnew.SaveBoard(PCB, b)
    remplit(PCB)
    # réparation des îlots de masse : contrôle KiCad, puis une piste vers le plan pour chaque
    # pastille de masse que les pistes ont isolée
    import json
    import subprocess
    cli = os.environ.get('KICAD_CLI', 'kicad-cli')
    for tour in range(3):
        rapport = os.path.join(ICI, 'build', 'drc_masse.json')
        subprocess.run([cli, 'pcb', 'drc', '--format', 'json', '-o', rapport, PCB],
                       capture_output=True, check=False)
        d = json.load(open(rapport))
        isoles = []
        for x in d.get('unconnected_items', []):
            for it in x['items']:
                m = re.match(r'(?:PTH )?[Pp]ad (\S+) \[GND\] of (\S+)', it['description'])
                if m:
                    isoles.append((m.group(2), m.group(1)))
        isoles = list(dict.fromkeys(isoles))
        if not isoles:
            break
        b = pcbnew.LoadBoard(PCB)
        ajouts = R.repare_ilots(b, LARG, HAUT, isoles)
        gnd = b.FindNet('GND')
        deja = [(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y))
                for t in b.GetTracks() if t.GetClass() == 'PCB_VIA']
        for a in ajouts:
            if a[0] == 'piste':
                piste(b, gnd, a[2], 0.25, R.COUCHES[a[1]])
            elif all((a[1] - x) ** 2 + (a[2] - y) ** 2 > 0.36 for x, y in deja):
                via(b, gnd, a[1], a[2])
                deja.append((a[1], a[2]))
        print(f'  îlots de masse : {len(isoles)} pastille(s) isolée(s), {len(ajouts)} ajout(s)')
        pcbnew.SaveBoard(PCB, b)
        remplit(PCB)
    # vias de masse restés reliés d'un seul côté (couture tombée dans un îlot vide) : retirés
    b = pcbnew.LoadBoard(PCB)
    plein = {}
    for z in b.Zones():
        if z.GetNetname() == 'GND' and not z.GetIsRuleArea():
            for L in R.COUCHES:
                if z.IsOnLayer(L):
                    plein.setdefault(L, []).append(z.GetFilledPolysList(L))
    morts = []
    for t in b.GetTracks():
        if t.GetClass() != 'PCB_VIA' or t.GetNetname() != 'GND':
            continue
        p = t.GetPosition()
        ok = all(any(sps.Contains(pcbnew.VECTOR2I(p.x, p.y)) for sps in plein.get(L, [])) for L in R.COUCHES)
        relie = sum(1 for u in b.GetTracks() if u.GetClass() != 'PCB_VIA' and u.GetNetname() == 'GND'
                    and (u.GetStart() == p or u.GetEnd() == p))
        if not ok and relie == 0:
            morts.append(t)
    for t in morts:
        b.Remove(t)
    if morts:
        print(f'  {len(morts)} via(s) de couture isolé(s) retiré(s)')
        pcbnew.SaveBoard(PCB, b)
        remplit(PCB)
    # ce que le cadre doit savoir de la carte : taille, trous, départs, entrée, embases HUB75
    import json as _json
    b = pcbnew.LoadBoard(PCB)
    def _pos(ref, num):
        return [round(v, 3) for v in pastille(b, ref, num)[0]]
    _json.dump({'largeur': LARG, 'hauteur': HAUT, 'trous': TROUS,
                'departs': {str(n): _pos(f'J{10 + n}', '1') for n in K.DALLES},
                'entree': _pos('J1', '1'),
                'hub75': {str(k): _pos(f'J{4 + k}', '1') for k in range(3)}},
               open(os.path.join(ICI, 'fixation.json'), 'w'), indent=1)
    print(f'carte écrite en {time.time() - t0:.0f} s :', PCB)
