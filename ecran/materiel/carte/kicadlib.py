"""
Accès aux bibliothèques KiCad installées : blocs de symboles (S-expressions) et broches.

Chemins : variables KICAD9_SYMBOL_DIR / KICAD9_FOOTPRINT_DIR si elles existent, sinon
l'installation standard (/usr/share/kicad).
"""
import os
import re

SYM_DIR = os.environ.get('KICAD9_SYMBOL_DIR', '/usr/share/kicad/symbols')
FP_DIR = os.environ.get('KICAD9_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
ICI = os.path.dirname(os.path.abspath(__file__))
LIB_LOCALE = 'carte-ecran'


def fin_parenthese(s, i):
    """Indice de la parenthèse fermante qui répond à celle ouverte en s[i]."""
    prof = 0
    dans_chaine = False
    j = i
    while j < len(s):
        ch = s[j]
        if dans_chaine:
            if ch == '\\':
                j += 2
                continue
            if ch == '"':
                dans_chaine = False
        elif ch == '"':
            dans_chaine = True
        elif ch == '(':
            prof += 1
        elif ch == ')':
            prof -= 1
            if prof == 0:
                return j
        j += 1
    raise ValueError('parenthèses déséquilibrées')


_textes = {}


def texte_lib(lib):
    if lib not in _textes:
        chemin = (os.path.join(ICI, lib + '.kicad_sym') if lib == LIB_LOCALE
                  else os.path.join(SYM_DIR, lib + '.kicad_sym'))
        _textes[lib] = open(chemin, encoding='utf-8').read()
    return _textes[lib]


def bloc_brut(lib, nom):
    s = texte_lib(lib)
    m = re.search(r'\n\t\(symbol "%s"\n' % re.escape(nom), s)
    if not m:
        raise KeyError(f'{lib}:{nom}')
    i = m.start() + 2
    return s[i:fin_parenthese(s, i) + 1]


def _sous_symboles(blk):
    """Liste (début, fin) des sous-symboles (symbol "Nom_u_s" ...) d'un bloc."""
    out = []
    for m in re.finditer(r'\(symbol "', blk):
        if m.start() == 0:
            continue
        out.append((m.start(), fin_parenthese(blk, m.start())))
    # garde les sous-blocs de premier niveau seulement
    prem = []
    for d, f in out:
        if not any(d > d2 and f < f2 for d2, f2 in out):
            prem.append((d, f))
    return prem


def bloc_aplati(lib, nom):
    """Bloc du symbole, avec l'héritage (extends) résolu : ce qu'un schéma embarque."""
    blk = bloc_brut(lib, nom)
    m = re.search(r'\(extends "([^"]+)"\)', blk)
    if not m:
        return blk
    parent = bloc_aplati(lib, m.group(1))
    # propriétés de l'enfant
    props_enfant = {}
    for pm in re.finditer(r'\(property "([^"]+)"', blk):
        e = fin_parenthese(blk, pm.start())
        props_enfant[pm.group(1)] = blk[pm.start():e + 1]
    # reconstruit : en-tête du parent renommé, propriétés remplacées, sous-symboles renommés
    out = parent.replace(f'(symbol "{m.group(1)}"', f'(symbol "{nom}"', 1)
    for pm in list(re.finditer(r'\(property "([^"]+)"', out)):
        pass
    for clef, txt in props_enfant.items():
        pm = re.search(r'\(property "%s"' % re.escape(clef), out)
        if pm:
            e = fin_parenthese(out, pm.start())
            out = out[:pm.start()] + txt + out[e + 1:]
        else:
            i = out.find('\n', 1)
            out = out[:i + 1] + '\t\t' + txt + out[i:]
    out = re.sub(r'\(symbol "%s_(\d+_\d+)"' % re.escape(m.group(1)), r'(symbol "%s_\1"' % nom, out)
    return out


def broches(lib, nom):
    blk = bloc_aplati(lib, nom)
    res = []
    for m in re.finditer(r'\(pin (\w+) (\w+)\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s*\(length ([\d.]+)\)', blk):
        e = fin_parenthese(blk, m.start())
        sub = blk[m.start():e + 1]
        res.append(dict(type=m.group(1), x=float(m.group(3)), y=float(m.group(4)), ang=float(m.group(5)),
                        longueur=float(m.group(6)),
                        nom=re.search(r'\(name "([^"]*)"', sub).group(1),
                        numero=re.search(r'\(number "([^"]*)"', sub).group(1),
                        cachee='(hide yes)' in sub[:sub.find('(name')]))
    return res


def bloc_pour_schema(lib_id):
    """Le bloc tel qu'embarqué dans lib_symbols d'un schéma : nom préfixé par la bibliothèque."""
    lib, nom = lib_id.split(':')
    blk = bloc_aplati(lib, nom)
    return blk.replace(f'(symbol "{nom}"', f'(symbol "{lib_id}"', 1)


def empreintes_locales():
    """Bibliothèque d'empreintes de la carte : le Pico des nœuds, sans la zone interdite d'antenne
    de l'empreinte d'origine (les nœuds n'émettent pas, et des pistes passent sous leur bout)."""
    rep = os.path.join(ICI, LIB_LOCALE + '.pretty')
    os.makedirs(rep, exist_ok=True)
    src = open(os.path.join(FP_DIR, 'Module.pretty', 'RaspberryPi_Pico_Common_THT.kicad_mod'), encoding='utf-8').read()
    i = src.find('(name "Antenna Copper Keep Out")')
    debut = src.rfind('(zone', 0, i)
    fin = fin_parenthese(src, debut)
    out = src[:debut] + src[fin + 1:]
    out = out.replace('(footprint "RaspberryPi_Pico_Common_THT"', '(footprint "RaspberryPi_Pico_THT_noeud"', 1)
    out = re.sub(r'\(descr "([^"]*)"\)', r'(descr "\1 — sans zone interdite d\'antenne : Pico des nœuds, radio éteinte")', out, count=1)
    open(os.path.join(rep, 'RaspberryPi_Pico_THT_noeud.kicad_mod'), 'w', encoding='utf-8').write(out)
    open(os.path.join(ICI, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n\t(version 7)\n\t(lib (name "carte-ecran")(type "KiCad")'
        '(uri "${KIPRJMOD}/carte-ecran.pretty")(options "")(descr "Empreintes propres à la carte"))\n)\n')
