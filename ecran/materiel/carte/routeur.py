"""
Routeur de la carte : A* sur une grille à deux couches, règles de dégagement de KiCad.

Pourquoi un routeur maison : la carte doit pouvoir se regénérer avec KiCad et Python seuls.

Principe
  - Grille de 0,254 mm (pas des embases 2,54 / 10) : entre deux broches traversantes au pas de
    2,54 mm, le milieu tombe sur une case, et une piste de 0,25 mm y passe à 0,29 mm des pastilles.
  - Trois cartes d'occupation par couche, une par gabarit de nouvelle pièce de cuivre : piste fine
    (0,25), piste d'alimentation (0,5), via (0,6) ; plus une carte des perçages pour l'écart entre
    trous. Chaque cuivre existant y est gonflé de dégagement + demi-largeur du gabarit + marge.
    Une case vaut 0 (libre), le numéro de l'équipotentielle qui l'occupe, ou -1 (interdite).
  - Les pastilles trop étroites pour contenir une case (TSSOP, réseaux de résistances) reçoivent
    une amorce, créée avant tout routage : un court segment vers l'extérieur du boîtier, qui finit
    sur une case.
  - A* multi-sources : chaque pastille rejoint l'arbre déjà routé de son équipotentielle (Prim).
    Coûts : 1 par case, √2 en diagonale, via 10, face arrière ×2,5 (elle porte le plan de masse),
    virage 0,4.
  - Arracher et rerouter : une liaison qui échoue fait arracher les équipotentielles qui
    encombrent son voisinage ; on la route en premier, puis on reroute les autres.
"""
import heapq
import math

import numpy as np
import pcbnew
import shapely
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

G = 0.254              # pas de la grille (mm)
CLR = 0.2              # dégagement cuivre-cuivre
MARGE = 0.05           # marge de quantification
BORD = 0.45            # dégagement au bord de la carte
GABARITS = {'p': 0.25, 'a': 0.5, 'v': 0.6}   # piste fine, piste d'alim, via (diamètre)
VIA_PERCAGE = 0.3
TROU_TROU = 0.3        # écart minimal entre perçages
COUT_VIA = 10.0
COUT_DESSOUS = 2.5
COUT_VIRAGE = 0.4
COUCHES = (pcbnew.F_Cu, pcbnew.B_Cu)
RAC2 = math.sqrt(2)
VOISINS = [(0, 1, 1.0), (0, -1, 1.0), (1, 0, 1.0), (-1, 0, 1.0),
           (1, 1, RAC2), (1, -1, RAC2), (-1, 1, RAC2), (-1, -1, RAC2)]


def tomm(v):
    return pcbnew.ToMM(v)


def poly_kicad(sps):
    """SHAPE_POLY_SET -> géométrie shapely (mm)."""
    polys = []
    for i in range(sps.OutlineCount()):
        o = sps.Outline(i)
        pts = [(tomm(o.CPoint(j).x), tomm(o.CPoint(j).y)) for j in range(o.PointCount())]
        if len(pts) >= 3:
            trous = []
            for h in range(sps.HoleCount(i)):
                ho = sps.Hole(i, h)
                trous.append([(tomm(ho.CPoint(j).x), tomm(ho.CPoint(j).y)) for j in range(ho.PointCount())])
            polys.append(Polygon(pts, trous))
    return unary_union(polys) if polys else None


class Pastille:
    """Ce que le routeur retient d'une pastille KiCad."""
    def __init__(self, pad):
        fp = pad.GetParentFootprint()
        self.ref = fp.GetReference()
        self.num = pad.GetNumber()
        self.net = pad.GetNetname()
        self.code = pad.GetNetCode()
        p = pad.GetPosition()
        self.x, self.y = tomm(p.x), tomm(p.y)
        f = fp.GetPosition()
        self.fx, self.fy = tomm(f.x), tomm(f.y)
        self.couches = [i for i, L in enumerate(COUCHES) if pad.IsOnLayer(L)]
        self.geom = {}
        for i in self.couches:
            g = poly_kicad(pad.GetEffectivePolygon(COUCHES[i], pcbnew.ERROR_INSIDE))
            if g is not None:
                self.geom[i] = g
        self.cms = pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD
        self.npth = pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH
        d = pad.GetDrillSize()
        self.percage = tomm(d.x) if d.x > 0 else 0.0
        sz = pad.GetSize(pcbnew.F_Cu)
        self.lx, self.ly = tomm(sz.x), tomm(sz.y)
        self.angle = pad.GetOrientation().AsDegrees()
        self.amorce = None          # (couche, points, largeur, case finale)

    @property
    def nom(self):
        return f'{self.ref}.{self.num}'


class Grille:
    def __init__(self, larg, haut):
        self.nx = int(math.ceil(larg / G)) + 1
        self.ny = int(math.ceil(haut / G)) + 1
        self.larg, self.haut = larg, haut
        self.occ = {c: np.zeros((2, self.ny, self.nx), np.int32) for c in GABARITS}
        self.trou = np.zeros((self.ny, self.nx), bool)
        xs = np.arange(self.nx) * G
        ys = np.arange(self.ny) * G
        self.X, self.Y = np.meshgrid(xs, ys)
        for c, w in GABARITS.items():
            d = BORD + w / 2 + MARGE
            for l in (0, 1):
                o = self.occ[c][l]
                o[(self.Y < d) | (self.Y > haut - d) | (self.X < d) | (self.X > larg - d)] = -1

    def cases(self, geom):
        x0, y0, x1, y1 = geom.bounds
        i0, i1 = max(0, int(math.floor(x0 / G))), min(self.nx - 1, int(math.ceil(x1 / G)))
        j0, j1 = max(0, int(math.floor(y0 / G))), min(self.ny - 1, int(math.ceil(y1 / G)))
        if i1 < i0 or j1 < j0:
            return None
        m = shapely.contains_xy(geom, self.X[j0:j1 + 1, i0:i1 + 1], self.Y[j0:j1 + 1, i0:i1 + 1])
        return j0, i0, m

    def marque(self, geom, code, couche, propre=False):
        """Cuivre `geom` (code d'équipotentielle, 0 = aucune) sur `couche` (0 dessus, 1 dessous)."""
        for c, w in GABARITS.items():
            r = self.cases(geom.buffer(CLR + w / 2 + MARGE, quad_segs=6))
            if r is None:
                continue
            j0, i0, m = r
            vue = self.occ[c][couche, j0:j0 + m.shape[0], i0:i0 + m.shape[1]]
            if code <= 0:
                vue[m] = -1
            else:
                vue[m & (vue != 0) & (vue != code)] = -1
                vue[m & (vue == 0)] = code
            if propre and code > 0 and c != 'v':
                dedans = geom.buffer(-w / 2 - 0.01)
                if not dedans.is_empty:
                    r2 = self.cases(dedans)
                    if r2 is not None:
                        j2, i2, m2 = r2
                        vue2 = self.occ[c][couche, j2:j2 + m2.shape[0], i2:i2 + m2.shape[1]]
                        vue2[m2 & (vue2 != -1)] = code

    def marque_trou(self, x, y, diam):
        r = self.cases(Point(x, y).buffer(diam / 2 + TROU_TROU + VIA_PERCAGE / 2 + MARGE))
        if r:
            j0, i0, m = r
            self.trou[j0:j0 + m.shape[0], i0:i0 + m.shape[1]][m] = True

    def interdit(self, geom, couches=(0, 1), gabarits=('p', 'a', 'v')):
        for c in gabarits:
            w = GABARITS[c]
            r = self.cases(geom.buffer(w / 2 + MARGE))
            if r is None:
                continue
            j0, i0, m = r
            for l in couches:
                self.occ[c][l, j0:j0 + m.shape[0], i0:i0 + m.shape[1]][m] = -1

    def case(self, x, y):
        return int(round(y / G)), int(round(x / G))


# ------------------------------------------------------------------ recherche
def astar(grille, code, gab, sources, cibles, limite=900000, via_libre=True):
    """`cibles` : ensemble de cases, ou None pour « n'importe quelle case où poser un via »."""
    nx, ny = grille.nx, grille.ny
    occ = grille.occ[gab]
    occv = grille.occ['v']
    libre = [((occ[l] == 0) | (occ[l] == code)) for l in (0, 1)]
    via_ok = ((occv[0] == 0) | (occv[0] == code)) & ((occv[1] == 0) | (occv[1] == code)) & ~grille.trou
    if not via_libre:
        via_ok[:] = False
    if cibles is None:
        cible_set = None
        tj = ti = None
    else:
        cible_set = set(cibles)
        tj = sum(c[1] for c in cibles) / len(cibles)
        ti = sum(c[2] for c in cibles) / len(cibles)

    def h(j, i):
        if tj is None:
            return 0.0
        dj, di = abs(j - tj), abs(i - ti)
        return (max(dj, di) - min(dj, di)) + RAC2 * min(dj, di)

    gcout, parent, direc, tas = {}, {}, {}, []
    for s in sources:
        if not libre[s[0]][s[1], s[2]]:
            continue
        gcout[s] = 0.0
        parent[s] = None
        direc[s] = None
        heapq.heappush(tas, (h(s[1], s[2]), 0.0, s))
    n = 0
    while tas:
        f, gc, u = heapq.heappop(tas)
        if gc > gcout.get(u, 1e18) + 1e-9:
            continue
        l, j, i = u
        fini = (u in cible_set) if cible_set is not None else (gc > 0 and via_ok[j, i])
        if fini:
            chemin = [u]
            while parent[chemin[-1]] is not None:
                chemin.append(parent[chemin[-1]])
            return chemin[::-1]
        n += 1
        if n > limite:
            return None
        mult = COUT_DESSOUS if l == 1 else 1.0
        dpar = direc[u]
        lib = libre[l]
        for dj, di, c in VOISINS:
            jj, ii = j + dj, i + di
            if jj < 0 or ii < 0 or jj >= ny or ii >= nx or not lib[jj, ii]:
                continue
            if dj and di and not (lib[j, ii] and lib[jj, i]):
                continue
            cout = c * mult
            if dpar is not None and dpar != (dj, di):
                cout += COUT_VIRAGE
            v = (l, jj, ii)
            ng = gc + cout
            if ng < gcout.get(v, 1e18):
                gcout[v] = ng
                parent[v] = u
                direc[v] = (dj, di)
                heapq.heappush(tas, (ng + h(jj, ii), ng, v))
        if via_ok[j, i] and libre[1 - l][j, i]:
            v = (1 - l, j, i)
            ng = gc + COUT_VIA
            if ng < gcout.get(v, 1e18):
                gcout[v] = ng
                parent[v] = u
                direc[v] = None
                heapq.heappush(tas, (ng + h(j, i), ng, v))
    return None


def segments(chemin):
    """Chemin de cases -> [(couche, [(x, y)...])] par tronçon, et positions des vias."""
    morceaux, vias = [], []
    cour = [chemin[0]]
    for a, b_ in zip(chemin, chemin[1:]):
        if a[0] != b_[0]:
            morceaux.append(cour)
            vias.append((a[2] * G, a[1] * G))
            cour = [b_]
        else:
            cour.append(b_)
    morceaux.append(cour)
    res = []
    for m in morceaux:
        pts = [(m_[2] * G, m_[1] * G) for m_ in m]
        simp = [pts[0]]
        for k in range(1, len(pts) - 1):
            ax, ay = simp[-1]
            bx, by = pts[k]
            cx, cy = pts[k + 1]
            if abs((bx - ax) * (cy - by) - (by - ay) * (cx - bx)) > 1e-9:
                simp.append(pts[k])
        if len(pts) > 1:
            simp.append(pts[-1])
        res.append((m[0][0], simp))
    return res, vias


# ------------------------------------------------------------------ le routeur
class Routeur:
    def __init__(self, board, larg, haut, classe_de, interdits=()):
        """`classe_de(net)` -> 'p' ou 'a'. `interdits` : [(géométrie, couches)] où rien ne passe."""
        self.larg, self.haut = larg, haut
        self.classe_de = classe_de
        self.pastilles = [Pastille(p) for fp in board.GetFootprints() for p in fp.Pads()]
        self.par_net = {}
        for p in self.pastilles:
            if p.net:
                self.par_net.setdefault(p.net, []).append(p)
        self.codes = {p.net: p.code for p in self.pastilles if p.net}
        self.interdits = list(interdits)
        # cuivre fixe déjà posé (pistes et vias de puissance, zones autres que la masse)
        self.fixe = []           # (code, couche, géométrie, axe) ; axe : où ancrer une piste qui s'y raccorde
        self.vias_fixes = []     # (x, y, diam)
        for t in board.GetTracks():
            if t.GetClass() == 'PCB_VIA':
                p = t.GetPosition()
                d = tomm(t.GetWidth(pcbnew.F_Cu))
                c = Point(tomm(p.x), tomm(p.y))
                g = c.buffer(d / 2)
                self.fixe += [(t.GetNetCode(), 0, g, c), (t.GetNetCode(), 1, g, c)]
                self.vias_fixes.append((tomm(p.x), tomm(p.y), tomm(t.GetDrillValue())))
            else:
                s, e = t.GetStart(), t.GetEnd()
                axe = LineString([(tomm(s.x), tomm(s.y)), (tomm(e.x), tomm(e.y))])
                g = axe.buffer(tomm(t.GetWidth()) / 2)
                self.fixe.append((t.GetNetCode(), 0 if t.GetLayer() == pcbnew.F_Cu else 1, g, axe))
        for z in board.Zones():
            g = poly_kicad(z.Outline())
            if g is None:
                continue
            if z.GetIsRuleArea():
                self.interdits.append((g, [i for i, L in enumerate(COUCHES) if z.IsOnLayer(L)]))
            elif z.GetNetname() != 'GND':
                for i, L in enumerate(COUCHES):
                    if z.IsOnLayer(L):
                        self.fixe.append((z.GetNetCode(), i, g, None))   # une zone se raccorde par recouvrement
        for fp in board.GetFootprints():
            for z in fp.Zones():
                if z.GetIsRuleArea():
                    g = poly_kicad(z.Outline())
                    if g is not None:
                        self.interdits.append((g, [i for i, L in enumerate(COUCHES) if z.IsOnLayer(L)]))
        self.routes = {}         # net -> [(couche, pts, largeur)], vias [(x, y)]
        self.echecs = []

    # -------------------------------------------------------------- grille
    def grille(self, sauf=()):
        g = Grille(self.larg, self.haut)
        for geom, couches in self.interdits:
            g.interdit(geom, couches)
        for code, l, geom, axe in self.fixe:
            g.marque(geom, code, l)
        for x, y, d in self.vias_fixes:
            g.marque_trou(x, y, d)
        for p in self.pastilles:
            for l, geom in p.geom.items():
                g.marque(geom, p.code, l, propre=True)
            if p.percage:
                g.marque_trou(p.x, p.y, p.percage)
            if p.npth:
                for l in (0, 1):
                    g.marque(Point(p.x, p.y).buffer(p.percage / 2 + 0.05), 0, l)
            if p.amorce:
                l, pts, larg, case = p.amorce
                g.marque(LineString(pts).buffer(larg / 2), p.code, l)
        for net, (pistes, vias) in self.routes.items():
            if net in sauf:
                continue
            code = self.codes[net]
            for l, pts, larg in pistes:
                g.marque(LineString(pts).buffer(larg / 2), code, l)
            for x, y in vias:
                gv = Point(x, y).buffer(GABARITS['v'] / 2)
                g.marque(gv, code, 0)
                g.marque(gv, code, 1)
                g.marque_trou(x, y, VIA_PERCAGE)
        # les bouts d'amorce sont toujours des cases de l'équipotentielle
        for p in self.pastilles:
            if p.amorce:
                l, pts, larg, (j, i) = p.amorce
                for c in ('p', 'a'):
                    if g.occ[c][l, j, i] != -1:
                        g.occ[c][l, j, i] = p.code
        return g

    # -------------------------------------------------------------- accès
    def cases_dedans(self, g, p, gab):
        res = []
        for l, geom in p.geom.items():
            d = geom.buffer(-GABARITS[gab] / 2 - 0.01)
            if d.is_empty:
                continue
            r = g.cases(d)
            if r is None:
                continue
            j0, i0, m = r
            for dj, di in zip(*np.nonzero(m)):
                j, i = j0 + dj, i0 + di
                if g.occ[gab][l, j, i] in (0, p.code):
                    res.append((l, j, i))
        return res

    def prepare_amorces(self, nets):
        """Crée, avant tout routage, les amorces des pastilles où aucune case ne tient."""
        g = self.grille()
        n = 0
        for net in nets:
            gab = self.classe_de(net)
            fixe = self.existant(net)
            for p in self.par_net.get(net, []):
                if not p.cms or self.cases_dedans(g, p, gab):
                    continue
                if any(l in p.geom and p.geom[l].intersects(geom) for l, geom, axe in fixe):
                    continue        # déjà relié par du cuivre posé à la main
                lx, ly = p.lx, p.ly
                a = math.radians(p.angle)
                if lx >= ly:
                    ux, uy, demi = math.cos(a), -math.sin(a), lx / 2
                else:
                    ux, uy, demi = math.sin(a), math.cos(a), ly / 2
                if (p.x - p.fx) * ux + (p.y - p.fy) * uy < 0:
                    ux, uy = -ux, -uy
                ux, uy = round(ux), round(uy)
                ex, ey = p.x + ux * (demi + 0.30), p.y + uy * (demi + 0.30)
                fx, fy = ex + ux * 0.40, ey + uy * 0.40
                j, i = g.case(fx, fy)
                pts = [(p.x, p.y), (ex, ey), (i * G, j * G)]
                l = p.couches[0]
                p.amorce = (l, pts, GABARITS[gab], (j, i))
                n += 1
        return n

    def acces(self, g, p, gab):
        if p.amorce:
            l, pts, larg, (j, i) = p.amorce
            return [(l, j, i)]
        return self.cases_dedans(g, p, gab)

    # -------------------------------------------------------------- une équipotentielle
    def route_net(self, g, net, existant):
        """Relie toutes les pastilles de `net` et tous les îlots de cuivre déjà posé (`existant` :
        [(couche, géométrie, axe)]). Chaque îlot est une cible comme une pastille ; une piste qui y
        aboutit part de son axe (centre du via, axe de la piste), sans quoi KiCad ne la relie pas."""
        gab = self.classe_de(net)
        larg = GABARITS[gab]
        code = self.codes[net]
        # îlots du cuivre posé : recouvrement sur une couche, ou via (même axe sur les deux couches)
        n = len(existant)
        parent = list(range(n))

        def racine(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a
        for a in range(n):
            for b_ in range(a + 1, n):
                la, ga, xa = existant[a]
                lb, gb, xb = existant[b_]
                if (la == lb and ga.intersects(gb)) or (xa is not None and xa is xb):
                    parent[racine(a)] = racine(b_)
        ilots = {}
        for a in range(n):
            ilots.setdefault(racine(a), []).append(existant[a])
        # cibles : îlots (avec les pastilles qu'ils touchent) puis pastilles isolées
        cibles_ = []
        prises = set()
        for morceaux_ilot in ilots.values():
            cases, ancre = set(), {}
            # un îlot qui porte un via (pré-routage sous un boîtier) se raccorde par le via, côté
            # dessous : sinon le via resterait relié d'un seul côté
            a_via = [m_ for m_ in morceaux_ilot if m_[2] is not None and m_[2].geom_type == 'Point']
            if a_via:
                morceaux_ilot_acces = [m_ for m_ in a_via if m_[0] == 1]
            else:
                morceaux_ilot_acces = morceaux_ilot
            for l, geom, axe in morceaux_ilot_acces:
                d = geom.buffer(larg / 2 - 0.02)
                if d.is_empty:
                    continue
                r = g.cases(d)
                if not r:
                    continue
                j0, i0, m = r
                for dj, di in zip(*np.nonzero(m)):
                    c = (l, j0 + dj, i0 + di)
                    if g.occ[gab][c] in (0, code):
                        cases.add(c)
                        if axe is not None and c not in ancre:
                            q = (axe.interpolate(axe.project(Point(c[2] * G, c[1] * G)))
                                 if axe.geom_type == 'LineString' else axe)
                            ancre[c] = (q.x, q.y)
            for p in self.par_net.get(net, []):
                if any(l in p.geom and p.geom[l].intersects(geom) for l, geom, axe in morceaux_ilot):
                    prises.add(id(p))
            if cases:
                cx = np.mean([c[2] for c in cases]) * G
                cy = np.mean([c[1] for c in cases]) * G
                cibles_.append(dict(cases=cases, ancre=ancre, x=cx, y=cy, nom='cuivre posé', pad=None))
        for p in self.par_net.get(net, []):
            if id(p) in prises:
                continue
            cs = set(self.acces(g, p, gab))
            if p.amorce:
                cs.add((p.amorce[0],) + p.amorce[3])
            cibles_.append(dict(cases=cs, ancre={}, x=p.x, y=p.y, nom=p.nom, pad=p))
        pistes, vias, echecs = [], [], []
        if len(cibles_) < 2:
            return pistes, vias, echecs
        # Prim : on part de la première cible, on rattache la plus proche à chaque tour
        arbre = set(cibles_[0]['cases'])
        ancre = dict(cibles_[0]['ancre'])
        restants = cibles_[1:]
        while restants:
            ax = np.array([c[2] for c in arbre], float) * G
            ay = np.array([c[1] for c in arbre], float) * G
            restants.sort(key=lambda t: float(np.min((ax - t['x']) ** 2 + (ay - t['y']) ** 2)))
            t = restants.pop(0)
            chemin = astar(g, code, gab, list(arbre), list(t['cases'])) if t['cases'] else None
            if chemin is None:
                if t['pad'] is not None:
                    echecs.append(t['pad'])
                else:
                    echecs.append(type('Ilot', (), {'nom': 'cuivre posé', 'x': t['x'], 'y': t['y']})())
                continue
            morceaux, vs = segments(chemin)
            if chemin[0] in ancre and morceaux and morceaux[0][0] == chemin[0][0]:
                morceaux[0] = (morceaux[0][0], [ancre[chemin[0]]] + morceaux[0][1])
            if chemin[-1] in t['ancre'] and morceaux and morceaux[-1][0] == chemin[-1][0]:
                morceaux[-1] = (morceaux[-1][0], morceaux[-1][1] + [t['ancre'][chemin[-1]]])
            for l, pts in morceaux:
                if len(pts) >= 2:
                    pistes.append((l, pts, larg))
                    g.marque(LineString(pts).buffer(larg / 2), code, l)
            for x, y in vs:
                vias.append((x, y))
                gv = Point(x, y).buffer(GABARITS['v'] / 2)
                g.marque(gv, code, 0)
                g.marque(gv, code, 1)
                g.marque_trou(x, y, VIA_PERCAGE)
            arbre.update(chemin)
            arbre.update(t['cases'])
            ancre.update(t['ancre'])
        return pistes, vias, echecs

    def existant(self, net):
        """Cuivre fixe déjà posé pour `net` (bus, pistes de puissance). Les amorces n'en sont pas :
        elles servent d'accès à leur pastille, qu'il faut encore relier."""
        code = self.codes[net]
        return [(l, geom, axe) for c, l, geom, axe in self.fixe if c == code]

    # -------------------------------------------------------------- tout
    def route(self, ordre, passes=6, journal=print, aussi=('GND',)):
        self.prepare_amorces(list(ordre) + [n for n in aussi if n in self.codes])
        a_faire = list(ordre)
        meilleur = None
        for passe in range(passes):
            g = self.grille(sauf=set(a_faire))
            echecs = []
            for net in a_faire:
                pistes, vias, ech = self.route_net(g, net, self.existant(net))
                self.routes[net] = (pistes, vias)
                echecs += [(net, p) for p in ech]
            journal(f'  passe {passe + 1} : {len(a_faire)} équipotentielles routées, {len(echecs)} échecs')
            if meilleur is None or len(echecs) < meilleur[0]:
                meilleur = (len(echecs), {k: v for k, v in self.routes.items()}, echecs)
            if not echecs:
                break
            # arracher : les équipotentielles qui passent près des pastilles en échec, et le long
            # de la ligne droite vers le reste de leur équipotentielle ; le rayon grandit à chaque passe
            nets_echec = [n for n, _ in echecs]
            gene = set()
            rayon = 2.5 + 1.5 * passe
            for net, p in echecs:
                autres = [q for q in self.par_net[net] if q is not p and (q.x, q.y) != (p.x, p.y)]
                q = min(autres, key=lambda q: (q.x - p.x) ** 2 + (q.y - p.y) ** 2) if autres else p
                zonep = LineString([(p.x, p.y), (q.x, q.y)]).buffer(rayon) if q is not p else Point(p.x, p.y).buffer(rayon)
                for autre, (pistes, vias) in self.routes.items():
                    if autre == net:
                        continue
                    if any(LineString(pts).intersects(zonep) for l, pts, larg in pistes):
                        gene.add(autre)
            prem = list(dict.fromkeys(nets_echec))
            import random
            rnd = random.Random(passe)
            reste = [n for n in ordre if n in gene and n not in prem]
            rnd.shuffle(reste)
            a_faire = prem + reste
            for n in a_faire:
                self.routes.pop(n, None)
        n_ech, routes, echecs = meilleur
        self.routes = routes
        self.echecs = echecs
        return echecs

    def vias_de_masse(self, net='GND', limite=40000):
        """Relie chaque pastille CMS de masse au plan du dessous par un via, au plus court."""
        g = self.grille()
        code = self.codes[net]
        pistes, vias, manque = [], [], []
        fixe = self.existant(net)
        for p in self.par_net.get(net, []):
            if not p.cms:
                continue
            if any(l in p.geom and p.geom[l].intersects(geom) for l, geom, axe in fixe):
                continue            # déjà descendue par un via posé (sous un boîtier)
            src = self.acces(g, p, 'p')
            if not src:
                manque.append(p)
                continue
            src = [s for s in src if s[0] == 0]
            chemin = astar(g, code, 'p', src, None, limite=limite, via_libre=True)
            if chemin is None:
                manque.append(p)
                continue
            fin = chemin[-1]
            x, y = fin[2] * G, fin[1] * G
            pts = [(p.x, p.y)] + [(c[2] * G, c[1] * G) for c in chemin if c[0] == 0]
            if len(pts) >= 2:
                pistes.append((0, pts, 0.25))
                g.marque(LineString(pts).buffer(0.125), code, 0)
            vias.append((x, y))
            gv = Point(x, y).buffer(GABARITS['v'] / 2)
            g.marque(gv, code, 0)
            g.marque(gv, code, 1)
            g.marque_trou(x, y, VIA_PERCAGE)
        self.routes.setdefault(net, ([], []))
        self.routes[net][0].extend(pistes)
        self.routes[net][1].extend(vias)
        return len(vias), manque

    def couture(self, net='GND', pas=5.08, eviter=(), voisinage=1):
        """Vias de couture entre les deux plans de masse, sur une maille régulière."""
        g = self.grille()
        code = self.codes[net]
        occv = g.occ['v']
        ok = ((occv[0] == 0) | (occv[0] == code)) & ((occv[1] == 0) | (occv[1] == code)) & ~g.trou
        # loin de tout autre cuivre : la case et son voisinage doivent être libres de toute autre
        n = 0
        vias = []
        y = pas / 2
        while y < self.haut:
            x = pas / 2
            while x < self.larg:
                if not any(geom.contains(Point(x, y)) for geom in eviter):
                    j, i = g.case(x, y)
                    v = voisinage
                    if v <= j < g.ny - v and v <= i < g.nx - v and ok[j - v:j + v + 1, i - v:i + v + 1].all():
                        vias.append((i * G, j * G))
                        n += 1
                x += pas
            y += pas
        self.routes.setdefault(net, ([], []))
        self.routes[net][1].extend(vias)
        return n

    def ecrit(self, board, nets, piste, via):
        for net, (pistes, vias) in self.routes.items():
            n = nets[net]
            for l, pts, larg in pistes:
                piste(board, n, pts, larg, COUCHES[l])
            for x, y in vias:
                via(board, n, x, y)
        for p in self.pastilles:
            if p.amorce:
                l, pts, larg, case = p.amorce
                piste(board, nets[p.net], pts, larg, COUCHES[l])


def repare_ilots(board, larg, haut, isoles, net='GND', classe_de=lambda n: 'p'):
    """Relie au plan principal les pastilles `isoles` [(ref, numéro)] de `net`, coupées de lui par
    les pistes : une piste du plus court vers la plus grande surface coulée de l'une des deux faces.
    La carte doit avoir ses zones remplies. Retourne les pistes et vias ajoutés."""
    rt = Routeur(board, larg, haut, classe_de)
    g = rt.grille()
    code = rt.codes[net]
    cibles = set()
    for z in board.Zones():
        if z.GetNetname() != net or z.GetIsRuleArea():
            continue
        for l, L in enumerate(COUCHES):
            if not z.IsOnLayer(L):
                continue
            plein = z.GetFilledPolysList(L)
            meilleur, aire = None, 0
            for i in range(plein.OutlineCount()):
                o = plein.Outline(i)
                pts = [(tomm(o.CPoint(k).x), tomm(o.CPoint(k).y)) for k in range(o.PointCount())]
                if len(pts) < 3:
                    continue
                pg = Polygon(pts)
                if pg.area > aire:
                    meilleur, aire = pg, pg.area
            if meilleur is None:
                continue
            r = g.cases(meilleur.buffer(-0.3))
            if r:
                j0, i0, m = r
                for dj, di in zip(*np.nonzero(m)):
                    c = (l, j0 + dj, i0 + di)
                    if g.occ['p'][c] in (0, code):
                        cibles.add(c)
    ajouts = []
    for ref, num in isoles:
        ps = [p for p in rt.par_net.get(net, []) if p.ref == ref and p.num == num]
        if not ps:
            continue
        src = rt.acces(g, ps[0], 'p')
        chemin = astar(g, code, 'p', src, list(cibles), limite=400000) if src else None
        if chemin is None:
            continue
        morceaux, vs = segments(chemin)
        for l, pts in morceaux:
            if len(pts) >= 2:
                ajouts.append(('piste', l, pts))
                g.marque(LineString(pts).buffer(0.125), code, l)
        for x, y in vs:
            ajouts.append(('via', x, y))
    return ajouts
