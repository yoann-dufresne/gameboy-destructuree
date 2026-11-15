#!/usr/bin/env python3
"""
Écrit le schéma KiCad de la carte (carte-manette.kicad_sch), sa bibliothèque de symboles et son
projet, à partir de conception.py.

Repris de la carte de l'écran (ecran/materiel/carte/gen_schema.py) : chaque broche reçoit un court fil
et une étiquette au nom de son équipotentielle ; le schéma se lit par blocs. Les UUID sont dérivés des
références : regénérer ne casse pas le lien entre le schéma et le circuit imprimé.

    python3 gen_schema.py          (puis : kicad-cli sch erc carte-manette.kicad_sch)
"""
import json
import math
import os
import re
import uuid

import conception as K
import kicadlib as L

ICI = os.path.dirname(os.path.abspath(__file__))
NS = uuid.UUID('c5ac4164-70c7-5821-be2d-f3a17d453d02')


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
LOCAUX = {  # symbole de conception.py -> symbole de la bibliothèque locale
    'MCU_Module:RaspberryPi_Pico_W_Debug': 'carte-manette:Pico2W',
    'Sensor_Motion:LIS2DE12': 'carte-manette:LIS2DH12',
}


def renomme(blk, ancien, nouveau):
    blk = blk.replace(f'(symbol "{ancien}"', f'(symbol "{nouveau}"', 1)
    return re.sub(r'\(symbol "%s_(\d+_\d+)"' % re.escape(ancien), r'(symbol "%s_\1"' % nouveau, blk)


def retype_broches(blk, regle):
    """regle(numéro, nom, type) -> nouveau type, ou None pour garder."""
    out, i = [], 0
    for m in re.finditer(r'\(pin (\w+) \w+', blk):
        e = L.fin_parenthese(blk, m.start())
        sub = blk[m.start():e + 1]
        num = re.search(r'\(number "([^"]+)"', sub).group(1)
        nom = re.search(r'\(name "([^"]*)"', sub).group(1)
        t = regle(num, nom, m.group(1))
        if t:
            sub = re.sub(r'^\(pin \w+', f'(pin {t}', sub)
        out.append(blk[i:m.start()])
        out.append(sub)
        i = e + 1
    out.append(blk[i:])
    return ''.join(out)


def propriete_lib(blk, nom, valeur):
    return re.sub(r'\(property "%s" "[^"]*"' % nom, f'(property "{nom}" {q(valeur)}', blk, count=1)


def symbole_molette():
    """Potentiomètre double à 5 broches, celui de Silent Modding : 1 et 2 les hauts des deux pistes,
    4 le curseur de la piste de la broche 1, 3 celui de l'autre, 5 le commun."""
    broches = [('1', 'HAUT_A', -7.62, 5.08, 0), ('2', 'HAUT_B', -7.62, 2.54, 0),
               ('3', 'CURSEUR_B', -7.62, 0, 0), ('4', 'CURSEUR_A', -7.62, -2.54, 0),
               ('5', 'COMMUN', -7.62, -5.08, 0)]
    pins = ''.join(
        f'\t\t\t(pin passive line (at {x} {y} {a}) (length 2.54) (name {q(n)} (effects (font (size 1.27 1.27)))) '
        f'(number {q(num)} (effects (font (size 1.27 1.27)))))\n'
        for num, n, x, y, a in broches)
    return ('(symbol "Molette_10k_double"\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n'
            '\t\t(property "Reference" "RV" (at 0 8.89 0) (effects (font (size 1.27 1.27))))\n'
            '\t\t(property "Value" "Molette_10k_double" (at 0 -8.89 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t(property "Footprint" {q(K.FP["molette"])} (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
            '\t\t(property "Datasheet" "~" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
            '\t\t(property "Description" "Potentiomètre double 10 kohm à molette, Game Boy (rechange Silent Modding) : '
            '1, 2 hauts ; 4, 3 curseurs ; 5 commun" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
            '\t\t(symbol "Molette_10k_double_0_1"\n'
            '\t\t\t(rectangle (start -5.08 7.62) (end 5.08 -7.62) (stroke (width 0.254) (type default)) '
            '(fill (type background)))\n'
            '\t\t)\n'
            '\t\t(symbol "Molette_10k_double_1_1"\n' + pins + '\t\t)\n'
            '\t\t(embedded_fonts no)\n\t)')


def ecrit_lib_locale():
    # Pico 2 W : le symbole "Debug" de KiCad (pastilles SWD D1 à D3). Masses en entrées d'alimentation
    # (une seule source pour l'ERC : le drapeau PWR_FLAG), ADC_VREF laissée en l'air.
    pico = renomme(L.bloc_aplati('MCU_Module', 'RaspberryPi_Pico_W_Debug'), 'RaspberryPi_Pico_W_Debug', 'Pico2W')
    pico = retype_broches(pico, lambda num, nom, t: 'power_in' if num in K.PICO_GND
                          else ('passive' if num == '35' else None))
    pico = propriete_lib(pico, 'Value', 'Pico2W')
    pico = propriete_lib(pico, 'Footprint', K.FP['pico'])
    pico = propriete_lib(pico, 'Description', 'Raspberry Pi Pico 2 W soudé à plat (castellations), '
                                              'pastilles SWD traversantes')
    # LIS2DH12 : même boîtier et même brochage que le LIS2DE12 de KiCad (carte.md §4.9).
    lis = renomme(L.bloc_aplati('Sensor_Motion', 'LIS2DE12'), 'LIS2DE12', 'LIS2DH12')
    lis = propriete_lib(lis, 'Value', 'LIS2DH12')
    lis = propriete_lib(lis, 'Description', 'Accéléromètre 3 axes ST LIS2DH12, LGA-12 2 x 2 mm, '
                                            'brochage du LIS2DE12')
    txt = ('(kicad_symbol_lib\n\t(version 20241209)\n\t(generator "gen_schema.py")\n'
           '\t(generator_version "9.0")\n\t' + pico + '\n\t' + lis + '\n\t' + symbole_molette() + '\n)\n')
    open(os.path.join(ICI, L.LIB_LOCALE + '.kicad_sym'), 'w', encoding='utf-8').write(txt)
    # Tables du projet : la bibliothèque de la carte seulement, comme pour la carte de l'écran. Celles de
    # KiCad viennent des tables globales (les tables par défaut, acceptées au premier lancement).
    open(os.path.join(ICI, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n\t(version 7)\n'
        '\t(lib (name "carte-manette")(type "KiCad")(uri "${KIPRJMOD}/carte-manette.kicad_sym")'
        '(options "")(descr "Symboles propres à la carte"))\n)\n')
    open(os.path.join(ICI, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n\t(version 7)\n'
        '\t(lib (name "carte-manette")(type "KiCad")(uri "${KIPRJMOD}/carte-manette.pretty")(options "")'
        '(descr "Empreintes propres à la carte, dont celles reprises de YAPoco (CC BY-SA 4.0)"))\n)\n')
    L._textes.pop(L.LIB_LOCALE, None)


def lib_id(cp):
    return LOCAUX.get(cp.lib_id, cp.lib_id)


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
    corps_h = 6 if cp.lib_id.startswith(('Device:R', 'Device:C', 'Device:L', 'Device:Battery')) else 4
    return (min(xs) - marge['g'] - 2, min(ys) - marge['h'] - corps_h,
            max(xs) + marge['d'] + 2, max(ys) + marge['b'] + corps_h + 5)


ANGLES = {'Device:R': 90, 'Device:C': 90, 'Device:L': 90}

BLOCS = {  # nom -> (x0, y0, x1, y1, titre)
    'alim':      (15, 15, 340, 210, 'ALIMENTATION : piles, inversion, maintien, interrupteur, USB'),
    'pico':      (345, 15, 600, 210, 'PICO 2 W : brochage de carte.md §5'),
    'acc':       (605, 15, 826, 210, 'ACCÉLÉROMÈTRE ET SON ADAPTATEUR'),
    'led':       (15, 215, 520, 420, 'LUMIÈRES : élévateur, cavalier, neuf LED en chaîne'),
    'commandes': (525, 215, 826, 420, 'COMMANDES : croix, boutons, molettes'),
    'test':      (15, 425, 826, 500, 'POINTS DE TEST, au dos'),
}


def place_tout():
    """Disposition en lignes dans chaque bloc. Retourne {ref: (x, y, angle)}."""
    pos = {}
    for nom_bloc, (x0, y0, x1, y1, _) in BLOCS.items():
        comps = [cp for cp in K.COMPOSANTS if cp.bloc == nom_bloc]
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
            print(f'ATTENTION : bloc {nom_bloc} déborde de {cy + haut_ligne - y1:.0f} mm')
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
    ys = [y + rot(p['x'], -p['y'], angle)[1] for p in pins] or [y]
    alim = cp.lib_id.startswith('power:')
    txt = (f'\t(symbol (lib_id {q(lid)}) (at {g(x)} {g(y)} {angle}) (unit 1) (exclude_from_sim no) '
           f'(in_bom {"no" if alim else "yes"}) (on_board {"no" if alim else "yes"}) '
           f'(dnp {"yes" if cp.dnp else "no"}) (uuid {q(u)})\n')
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
            continue                     # broches empilées : un seul raccord
        vus.add((px, py))
        if net is None:
            out.append(f'\t(no_connect (at {px} {py}) (uuid {q(uid("nc", cp.ref, num))}))\n')
            continue
        ex, ey = g(px + 2.54 * dx), g(py + 2.54 * dy)
        out.append(f'\t(wire (pts (xy {px} {py}) (xy {ex} {ey})) (stroke (width 0) (type default)) '
                   f'(uuid {q(uid("fil", cp.ref, num))}))\n')
        if dx < 0:
            a, j = 180, 'right'
        elif dx > 0:
            a, j = 0, 'left'
        elif dy < 0:
            a, j = 90, 'left'
        else:
            a, j = 270, 'right'
        # Étiquette globale : l'équipotentielle porte son nom nu dans la netlist et sur la carte.
        out.append(f'\t(global_label {q(net)} (shape passive) (at {ex} {ey} {a}) (fields_autoplaced yes) '
                   f'(effects (font (size 1.27 1.27)) (justify {j})) (uuid {q(uid("et", cp.ref, num))})\n'
                   f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {ex} {ey} 0) '
                   f'(effects (font (size 1.27 1.27)) (hide yes)))\n\t)\n')
    return out


NOTES = [
    (20, 206, "Dormir, c'est être hors tension : Q2 ne conduit que si Q3 (interrupteur), Q4 (mouvement) "
              "ou Q5 (le Pico) tire sa grille. JP1 fermé : maintien forcé."),
    (20, 416, "JP2 : 1-2 (par défaut) les LED sur l'élévateur, 4,48 V ; 2-3 sur le 3,3 V du Pico. "
               "Les LED -V6 consomment au plus 1 uA éteintes."),
    (530, 416, "Molettes alimentées par GP21 le temps d'une mesure. R13 à R20 non posées (T0.5)."),
]


def cadres():
    out = []
    for nom, (x0, y0, x1, y1, titre) in BLOCS.items():
        out.append(f'\t(rectangle (start {x0} {y0}) (end {x1} {y1}) (stroke (width 0.3) (type dash) '
                   f'(color 72 72 72 1)) (fill (type none)) (uuid {q(uid("cadre", nom))}))\n')
        out.append(f'\t(text {q(titre)} (exclude_from_sim no) (at {x0 + 3} {y0 + 6} 0) '
                   f'(effects (font (size 2.5 2.5) (thickness 0.5) bold) (justify left bottom)) '
                   f'(uuid {q(uid("titre", nom))}))\n')
    for i, (x, y, t) in enumerate(NOTES):
        out.append(f'\t(text {q(t)} (exclude_from_sim no) (at {x} {y} 0) '
                   f'(effects (font (size 1.5 1.5) italic) (justify left bottom)) (uuid {q(uid("note", i))}))\n')
    return out


def ecrit_schema():
    pos = place_tout()
    libs = sorted({lib_id(cp) for cp in K.COMPOSANTS})
    t = ['(kicad_sch\n\t(version 20250114)\n\t(generator "eeschema")\n\t(generator_version "9.0")\n',
         f'\t(uuid {q(RACINE)})\n\t(paper "A1")\n',
         f'\t(title_block\n\t\t(title {q(K.TITRE)})\n\t\t(date {q(K.DATE)})\n\t\t(rev {q(K.REVISION)})\n'
         f'\t\t(company "Game Boy Pocket déstructurée : module manette")\n'
         f'\t\t(comment 1 "Généré par gen_schema.py depuis conception.py : modifier la conception, pas ce fichier")\n'
         f'\t\t(comment 2 "Brochage du Pico : manette/docs/carte.md §5, qui fait foi pour le firmware")\n'
         f'\t\t(comment 3 "Licence CC BY-SA 4.0 : géométrie reprise de YAPoco, de Natalie the Nerd")\n\t)\n',
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
    t.append('\t(sheet_instances (path "/" (page "1")))\n\t(embedded_fonts no)\n)\n')
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
    ecrit_schema()
    ecrit_projet()
    print('schéma écrit :', K.PROJET + '.kicad_sch')
