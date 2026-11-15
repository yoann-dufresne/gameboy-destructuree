#!/usr/bin/env python3
"""
Écrit le schéma KiCad de la carte (carte-ecran.kicad_sch), sa bibliothèque locale et son projet,
à partir de conception.py.

Chaque broche reçoit un court fil et une étiquette au nom de son équipotentielle : le schéma se lit
par blocs (alimentation, départs, tête, nœuds, mesure). Les UUID sont dérivés des références, si bien
que regénérer ne casse pas le lien entre le schéma et le circuit imprimé.

    python3 gen_schema.py          (puis : kicad-cli sch erc carte-ecran.kicad_sch)
"""
import json
import math
import os
import re
import uuid

import conception as K
import kicadlib as L

ICI = os.path.dirname(os.path.abspath(__file__))
NS = uuid.UUID('6f1c2b4e-9a57-4d0b-8f3e-2c1d0e5a7b90')


def uid(*parts):
    return str(uuid.uuid5(NS, '/'.join(str(p) for p in parts)))


RACINE = uid('racine')
GRILLE = 1.27


def g(v):
    """Arrondit sur la grille de 1,27 mm."""
    return round(round(v / GRILLE) * GRILLE, 4)


def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


# ------------------------------------------------------------------ bibliothèque locale
def ecrit_lib_locale():
    """Symbole Pico2 : celui de KiCad, broches de masse et VBUS en entrées/passives.

    Le symbole d'origine déclare GND et AGND en sorties d'alimentation : quatre Pico sur la même
    masse feraient autant de conflits « sortie contre sortie » à l'ERC."""
    blk = L.bloc_aplati('MCU_Module', 'RaspberryPi_Pico')
    blk = blk.replace('(symbol "RaspberryPi_Pico"', '(symbol "Pico2"', 1)
    blk = re.sub(r'\(symbol "RaspberryPi_Pico_(\d+_\d+)"', r'(symbol "Pico2_\1"', blk)

    def retype(txt):
        num = re.search(r'\(number "([^"]+)"', txt).group(1)
        if num in K.PICO_GND or num == '33':
            txt = re.sub(r'^\(pin \w+', '(pin power_in', txt)
        elif num == '40':
            txt = re.sub(r'^\(pin \w+', '(pin passive', txt)
        return txt

    out, i = [], 0
    for m in re.finditer(r'\(pin \w+ \w+', blk):
        e = L.fin_parenthese(blk, m.start())
        out.append(blk[i:m.start()])
        out.append(retype(blk[m.start():e + 1]))
        i = e + 1
    out.append(blk[i:])
    blk = ''.join(out)
    blk = re.sub(r'\(property "Value" "[^"]*"', '(property "Value" "Pico2"', blk, count=1)
    blk = re.sub(r'\(property "Footprint" "[^"]*"', f'(property "Footprint" "{K.FP["pico"]}"', blk, count=1)
    blk = re.sub(r'\(property "Description" "[^"]*"',
                 '(property "Description" "Raspberry Pi Pico 2 ou Pico 2 W sur support (2 barrettes 1×20). '
                 'Masses en entrées d\'alimentation pour l\'ERC multi-Pico."', blk, count=1)
    # 74AHCT541, tampon unidirectionnel : entrées A, sorties Y, typées comme telles.
    tb = L.bloc_aplati('74xx', '74LS541')
    tb = tb.replace('(symbol "74LS541"', '(symbol "74AHCT541"', 1)
    tb = re.sub(r'\(symbol "74LS541_(\d+_\d+)"', r'(symbol "74AHCT541_\1"', tb)
    out2, i = [], 0
    for m in re.finditer(r'\(pin \w+ \w+', tb):
        e = L.fin_parenthese(tb, m.start())
        sub = tb[m.start():e + 1]
        nom = re.search(r'\(name "([^"]+)"', sub).group(1)
        if re.fullmatch(r'A\d', nom):
            sub = re.sub(r'^\(pin \w+', '(pin input', sub)
        elif re.fullmatch(r'Y\d', nom):
            sub = re.sub(r'^\(pin \w+', '(pin output', sub)
        out2.append(tb[i:m.start()])
        out2.append(sub)
        i = e + 1
    out2.append(tb[i:])
    tb = ''.join(out2)
    tb = re.sub(r'\(property "Value" "[^"]*"', '(property "Value" "74AHCT541"', tb, count=1)
    tb = re.sub(r'\(property "Footprint" "[^"]*"', f'(property "Footprint" "{K.FP["tssop20"]}"', tb, count=1)
    tb = re.sub(r'\(property "Description" "[^"]*"',
                '(property "Description" "Tampon 8 bits 74AHCT541 : entrées A en 3,3 V, tolérantes, sans '
                'diode vers VCC ; sorties Y en 5 V vers la dalle ; /OE1 à la masse"', tb, count=1)
    txt = ('(kicad_symbol_lib\n\t(version 20241209)\n\t(generator "gen_schema.py")\n'
           '\t(generator_version "9.0")\n\t' + blk + '\n\t' + tb + '\n)\n')
    open(os.path.join(ICI, L.LIB_LOCALE + '.kicad_sym'), 'w', encoding='utf-8').write(txt)
    open(os.path.join(ICI, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n\t(version 7)\n\t(lib (name "carte-ecran")(type "KiCad")'
        '(uri "${KIPRJMOD}/carte-ecran.kicad_sym")(options "")(descr "Symboles propres à la carte"))\n)\n')
    L._textes.pop(L.LIB_LOCALE, None)


def lib_id(cp):
    return 'carte-ecran:Pico2' if cp.lib_id == 'MCU_Module:RaspberryPi_Pico' else cp.lib_id


# ------------------------------------------------------------------ géométrie des broches
def rot(vx, vy, angle):
    """Vecteur écran (y vers le bas) tourné de `angle` degrés dans le sens trigonométrique visuel."""
    a = math.radians(angle)
    c, s = round(math.cos(a)), round(math.sin(a))
    return (vx * c + vy * s, -vx * s + vy * c)


def broches_placees(cp, x, y, angle):
    """[(numéro, net, px, py, dirx, diry)] : point de connexion et direction vers l'extérieur."""
    res = []
    lib, nom = lib_id(cp).split(':')
    for p in L.broches(lib, nom):
        vx, vy = rot(p['x'], -p['y'], angle)
        a = math.radians(p['ang'])
        dx, dy = rot(-math.cos(a), math.sin(a), angle)   # vers l'extérieur du corps, écran
        res.append((p['numero'], cp.pins.get(p['numero']), x + vx, y + vy, round(dx), round(dy)))
    return res


def boite(cp, angle):
    pts = [(px, py) for _, _, px, py, _, _ in broches_placees(cp, 0, 0, angle)]
    xs = [p[0] for p in pts] or [0]
    ys = [p[1] for p in pts] or [0]
    marge = {'g': 0.0, 'd': 0.0, 'h': 0.0, 'b': 0.0}
    for _, net, px, py, dx, dy in broches_placees(cp, 0, 0, angle):
        lg = 4 + 1.1 * len(net or 'x')
        if dx < 0: marge['g'] = max(marge['g'], lg)
        if dx > 0: marge['d'] = max(marge['d'], lg)
        if dy < 0: marge['h'] = max(marge['h'], lg)
        if dy > 0: marge['b'] = max(marge['b'], lg)
    corps_h = 6 if cp.lib_id.startswith(('Device:R', 'Device:C', 'Device:LED', 'Device:Fuse', 'Device:Poly',
                                          'Device:Ferr', 'Diode', 'Device:D_')) else 4
    return (min(xs) - marge['g'] - 2, min(ys) - marge['h'] - corps_h,
            max(xs) + marge['d'] + 2, max(ys) + marge['b'] + corps_h + 5)


ANGLES = {'Device:R': 90, 'Device:C': 90, 'Device:C_Polarized': 90, 'Device:Fuse': 90,
          'Device:Polyfuse': 90, 'Device:FerriteBead': 90, 'Device:R_Pack04': 90}

BLOCS = {  # nom -> (x0, y0, x1, y1, titre)
    'alim':    (15, 15, 225, 150, 'ALIMENTATION — entrée 5 V 30 A, mesure du bus'),
    'departs': (230, 15, 826, 150, 'DÉPARTS — un fusible et un bornier par dalle'),
    'logique': (15, 155, 225, 300, 'ALIMENTATION DES PICO ET DES TAMPONS'),
    'tete':    (230, 155, 600, 300, 'TÊTE — Pico 2 W, liaisons L0 L1 L2, VSYNC, RDY'),
    'mesure':  (605, 155, 826, 300, 'MESURE — analyseur logique ; fixations'),
    'noeud0':  (15, 305, 283, 535, 'NŒUD 0 — rangée du haut, dalles 7-8-9'),
    'noeud1':  (288, 305, 556, 535, 'NŒUD 1 — rangée du milieu, dalles 4-5-6'),
    'noeud2':  (561, 305, 826, 535, 'NŒUD 2 — rangée du bas, dalles 1-2-3'),
}
BLOC_DE = {'meca': 'mesure'}


def place_tout():
    """Disposition en lignes dans chaque bloc. Retourne {ref: (x, y, angle)}."""
    pos = {}
    for nom_bloc, (x0, y0, x1, y1, _) in BLOCS.items():
        comps = [cp for cp in K.COMPOSANTS if BLOC_DE.get(cp.bloc, cp.bloc) == nom_bloc]
        cx, cy, haut_ligne = x0 + 4, y0 + 12, 0
        for cp in comps:
            angle = ANGLES.get(cp.lib_id, 0)
            bx0, by0, bx1, by1 = boite(cp, angle)
            w, h = bx1 - bx0, by1 - by0
            if cx + w > x1 - 2 and cx > x0 + 4:
                cx, cy, haut_ligne = x0 + 4, cy + haut_ligne + 3, 0
            pos[cp.ref] = (g(cx - bx0), g(cy - by0), angle)
            cx += w + 3
            haut_ligne = max(haut_ligne, h)
        if cy + haut_ligne > y1:
            print(f'⚠ bloc {nom_bloc} déborde de {cy + haut_ligne - y1:.0f} mm')
    return pos


# ------------------------------------------------------------------ écriture
def propriete(nom, val, x, y, cachee=False, justif=None, angle=0):
    eff = '(effects (font (size 1.27 1.27))'
    if justif:
        eff += f' (justify {justif})'
    if cachee:
        eff += ' (hide yes)'
    eff += ')'
    return f'\t\t(property {q(nom)} {q(val)} (at {g(x)} {g(y)} {angle}) {eff})\n'


def symbole(cp, x, y, angle):
    lid = lib_id(cp)
    u = uid('sym', cp.ref)
    lib, nom = lid.split(':')
    pins = L.broches(lib, nom)
    xs = [x + rot(p['x'], -p['y'], angle)[0] for p in pins] or [x]
    ys = [y + rot(p['x'], -p['y'], angle)[1] for p in pins] or [y]
    alim = cp.lib_id.startswith('power:')
    txt = (f'\t(symbol (lib_id {q(lid)}) (at {g(x)} {g(y)} {angle}) (unit 1) (exclude_from_sim no) '
           f'(in_bom {"no" if alim or cp.lib_id.startswith("Mechanical") else "yes"}) '
           f'(on_board {"no" if alim else "yes"}) (dnp no) (uuid {q(u)})\n')
    ref_y = min(ys) - 1.5 if not alim else y + 3
    txt += propriete('Reference', cp.ref, x + 1.5, ref_y, cachee=alim, justif='left')
    txt += propriete('Value', cp.value, x + 1.5, ref_y + 2.5 if alim else max(ys) + 3.5, justif='left')
    txt += propriete('Footprint', cp.footprint, x, y, cachee=True)
    txt += propriete('Datasheet', '~', x, y, cachee=True)
    txt += propriete('Description', '', x, y, cachee=True)
    for k_, v in cp.champs.items():
        txt += propriete(k_, v, x, y, cachee=True)
    for p in pins:
        txt += f'\t\t(pin {q(p["numero"])} (uuid {q(uid("pin", cp.ref, p["numero"]))}))\n'
    txt += (f'\t\t(instances (project {q(K.PROJET)} (path {q("/" + RACINE)} '
            f'(reference {q(cp.ref)}) (unit 1))))\n\t)\n')
    return txt


def etiquettes(cp, x, y, angle):
    out = []
    vus = set()
    for num, net, px, py, dx, dy in broches_placees(cp, x, y, angle):
        px, py = g(px), g(py)
        if (px, py) in vus:
            continue                     # broches empilées (masses du Pico) : un seul raccord
        vus.add((px, py))
        if net is None:
            out.append(f'\t(no_connect (at {px} {py}) (uuid {q(uid("nc", cp.ref, num))}))\n')
            continue
        ex, ey = g(px + 2.54 * dx), g(py + 2.54 * dy)
        out.append(f'\t(wire (pts (xy {px} {py}) (xy {ex} {ey})) (stroke (width 0) (type default)) '
                   f'(uuid {q(uid("fil", cp.ref, num))}))\n')
        if dx < 0:
            a, j = 180, 'right bottom'
        elif dx > 0:
            a, j = 0, 'left bottom'
        elif dy < 0:
            a, j = 90, 'left bottom'
        else:
            a, j = 270, 'right bottom'
        # Étiquette globale : l'équipotentielle porte son nom nu dans la netlist et sur la carte.
        out.append(f'\t(global_label {q(net)} (shape passive) (at {ex} {ey} {a}) (fields_autoplaced yes) '
                   f'(effects (font (size 1.27 1.27)) (justify {j.split()[0]})) (uuid {q(uid("et", cp.ref, num))})\n'
                   f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {ex} {ey} 0) '
                   f'(effects (font (size 1.27 1.27)) (hide yes)))\n\t)\n')
    return out


def cadres():
    out = []
    for nom, (x0, y0, x1, y1, titre) in BLOCS.items():
        out.append(f'\t(rectangle (start {x0} {y0}) (end {x1} {y1}) (stroke (width 0.3) (type dash) '
                   f'(color 72 72 72 1)) (fill (type none)) (uuid {q(uid("cadre", nom))}))\n')
        out.append(f'\t(text {q(titre)} (exclude_from_sim no) (at {x0 + 3} {y0 + 6} 0) '
                   f'(effects (font (size 2.5 2.5) (thickness 0.5) bold) (justify left bottom)) '
                   f'(uuid {q(uid("titre", nom))}))\n')
    notes = [
        (235, 296, 'Liaisons : D0 D1 CLK CS sur GP 4k..4k+3 de la tête → GP19..GP22 du nœud k ; 33 Ω au départ (plan §2.6).'),
        (20, 296, 'Diode Schottky par Pico : le bus ne remonte pas dans l\'USB, un Pico en USB n\'alimente pas le bus.'),
        (20, 531, 'Cavalier JP : G1 sur GP1 (1-2) ou sur GP15 (2-3, Pico à GP1 mort). Sans Pico : tampons en haute impédance, dalle éteinte.'),
    ]
    for i, (x, y, t) in enumerate(notes):
        out.append(f'\t(text {q(t)} (exclude_from_sim no) (at {x} {y} 0) '
                   f'(effects (font (size 1.5 1.5) italic) (justify left bottom)) (uuid {q(uid("note", i))}))\n')
    return out


def ecrit_schema():
    pos = place_tout()
    libs = sorted({lib_id(cp) for cp in K.COMPOSANTS})
    t = ['(kicad_sch\n\t(version 20250114)\n\t(generator "eeschema")\n\t(generator_version "9.0")\n',
         f'\t(uuid {q(RACINE)})\n\t(paper "A1")\n',
         f'\t(title_block\n\t\t(title {q(K.TITRE)})\n\t\t(date {q(K.DATE)})\n\t\t(rev {q(K.REVISION)})\n'
         f'\t\t(company "Game Boy Pocket déstructurée — module écran")\n'
         f'\t\t(comment 1 "Généré par gen_schema.py depuis conception.py : modifier la conception, pas ce fichier")\n'
         f'\t\t(comment 2 "Brochages identiques au firmware : tete/include/config.h, noeud/include/config.h")\n\t)\n',
         '\t(lib_symbols\n']
    for lid in libs:
        blk = L.bloc_pour_schema(lid)
        t.append('\t\t' + blk.replace('\n', '\n\t') + '\n')
    t.append('\t)\n')
    t += cadres()
    for cp in K.COMPOSANTS:
        x, y, a = pos[cp.ref]
        t += etiquettes(cp, x, y, a)
    for cp in K.COMPOSANTS:
        x, y, a = pos[cp.ref]
        t.append(symbole(cp, x, y, a))
    t.append(f'\t(sheet_instances (path "/" (page "1")))\n\t(embedded_fonts no)\n)\n')
    open(os.path.join(ICI, K.PROJET + '.kicad_sch'), 'w', encoding='utf-8').write(''.join(t))
    return pos


def ecrit_projet():
    chemin = os.path.join(ICI, K.PROJET + '.kicad_pro')
    pro = json.load(open(chemin)) if os.path.exists(chemin) else {}
    pro.setdefault('meta', {'filename': K.PROJET + '.kicad_pro', 'version': 3})
    sch = pro.setdefault('schematic', {})
    sch.setdefault('legacy_lib_dir', '')
    sch.setdefault('legacy_lib_list', [])
    pro.setdefault('sheets', [[RACINE, 'Root']])
    pro.setdefault('boards', [])
    pro.setdefault('text_variables', {})
    json.dump(pro, open(chemin, 'w'), indent=2, ensure_ascii=False)


if __name__ == '__main__':
    ecrit_lib_locale()
    L.empreintes_locales()
    ecrit_schema()
    ecrit_projet()
    print('schéma écrit :', K.PROJET + '.kicad_sch')
