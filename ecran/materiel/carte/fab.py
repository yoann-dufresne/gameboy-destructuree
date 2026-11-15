#!/usr/bin/env python3
"""
Fichiers de fabrication tirés de conception.py et de la carte :
  fab/bom_jlcpcb.csv          nomenclature des CMS, au format de l'assemblage JLCPCB
  fab/cpl_jlcpcb.csv          placement des CMS (à partir de l'export « pos » de KiCad)
  fab/nomenclature.csv        tout ce qu'il faut acheter pour la carte, CMS, traversants et vrac
  fab/nomenclature.md         la même, en tableau pour la documentation

    python3 fab.py build/positions.csv
"""
import csv
import os
import re
import sys
from collections import OrderedDict

import conception as K

ICI = os.path.dirname(os.path.abspath(__file__))
FAB = os.path.join(ICI, 'fab')

# Corrections de rotation pour l'assemblage JLCPCB, par empreinte : ses modèles n'ont pas tous le
# zéro de KiCad. Relevées sur l'aperçu d'assemblage de JLCPCB le 29/10/2026 : TSSOP couchés à 90°,
# SOT-23 retournés, réseaux de résistances couchés ; les CMS à deux pattes y étaient justes.
ROTATION_JLC = [(r'^TSSOP-', 270), (r'^SOT-23$', 180), (r'^R_Array_Convex_', 90)]


def rotation_jlc(empreinte, rot):
    nom = empreinte.split(':')[-1]
    for motif, corr in ROTATION_JLC:
        if re.match(motif, nom):
            return (rot + corr) % 360
    return rot % 360


CMS = ('Resistor_SMD:', 'Capacitor_SMD:', 'LED_SMD:', 'Package_SO:', 'Package_TO_SOT_SMD:',
       'Diode_SMD:', 'Inductor_SMD:', 'Fuse:Fuse_1812')

# Pièces qui ne sont pas des composants du schéma mais qu'il faut acheter pour la carte.
VRAC = [
    ('Barrette femelle 1×20, 2,54 mm (support de Pico)', 8, 'support des 4 Pico, 2 par Pico', ''),
    ('Fusible lame mini (ATM) 7,5 A, marron', 12, '9 en place + 3 de rechange', ''),
    ('Fiche Phoenix MSTB 2,5/2-ST-5,08 (1757019) ou 2EDGK 5,08 2 points', 9, 'une par câble de dalle', ''),
    ('Cavalier 2,54 mm', 3, 'sélection de G1 (JP1 à JP3)', ''),
]
# La carte se visse sur les plots du plateau imprimé : vis comptées dans ../cadre/debit.md.


def est_cms(cp):
    return cp.footprint.startswith(CMS)


def groupe(comps):
    g = OrderedDict()
    for cp in comps:
        cle = (cp.value, cp.footprint, cp.champs.get('LCSC', ''), cp.champs.get('MPN', ''))
        g.setdefault(cle, []).append(cp.ref)
    return g


def tri_ref(r):
    import re
    m = re.match(r'([A-Z]+)(\d+)', r)
    return (m.group(1), int(m.group(2))) if m else (r, 0)


def main(pos_csv):
    os.makedirs(FAB, exist_ok=True)
    comps = [cp for cp in K.COMPOSANTS if cp.footprint and not cp.lib_id.startswith(('power:', 'Mechanical:'))]
    cms = [cp for cp in comps if est_cms(cp)]
    tht = [cp for cp in comps if not est_cms(cp)]
    # --- BOM JLCPCB
    with open(os.path.join(FAB, 'bom_jlcpcb.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (val, fp, lcsc, mpn), refs in groupe(cms).items():
            com = val if (not mpn or mpn.split(' ')[0] == val) else f'{val} {mpn}'
            w.writerow([com, ','.join(sorted(refs, key=tri_ref)), fp.split(':')[1], lcsc])
    # --- CPL JLCPCB, depuis l'export de KiCad
    refs_cms = {cp.ref: cp.footprint for cp in cms}
    with open(pos_csv, newline='', encoding='utf-8') as f, \
            open(os.path.join(FAB, 'cpl_jlcpcb.csv'), 'w', newline='', encoding='utf-8') as g:
        r = csv.DictReader(f)
        w = csv.writer(g)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for row in r:
            if row['Ref'] in refs_cms:
                w.writerow([row['Ref'], f"{float(row['PosX']):.3f}mm", f"{float(row['PosY']):.3f}mm",
                            'Top' if row['Side'].lower().startswith('top') else 'Bottom',
                            f"{rotation_jlc(refs_cms[row['Ref']], float(row['Rot'])):.0f}"])
    # --- nomenclature complète
    lignes = []
    for (val, fp, lcsc, mpn), refs in groupe(cms).items():
        lignes.append(('CMS (assemblé par JLCPCB)', len(refs), val, mpn, lcsc, ', '.join(sorted(refs, key=tri_ref)), fp))
    for (val, fp, lcsc, mpn), refs in groupe(tht).items():
        lignes.append(('traversant (soudé à la main)', len(refs), val, mpn, lcsc, ', '.join(sorted(refs, key=tri_ref)), fp))
    for nom, q, note, lcsc in VRAC:
        lignes.append(('vrac', q, nom, note, lcsc, '', ''))
    with open(os.path.join(FAB, 'nomenclature.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Catégorie', 'Qté', 'Valeur', 'Référence fabricant / note', 'LCSC', 'Repères', 'Empreinte'])
        w.writerows(lignes)
    with open(os.path.join(FAB, 'nomenclature.md'), 'w', encoding='utf-8') as f:
        f.write('| Catégorie | Qté | Valeur | Référence fabricant / note | LCSC | Repères |\n|---|---|---|---|---|---|\n')
        for ligne in lignes:
            cat, q, val, mpn, lcsc, refs = (str(x).replace('|', '\\|') for x in ligne[:6])
            f.write(f'| {cat} | {q} | {val} | {mpn} | {lcsc} | {refs} |\n')
    print(f'{len(cms)} CMS en {len(groupe(cms))} lignes, {len(tht)} traversants, {len(VRAC)} lignes de vrac')


if __name__ == '__main__':
    main(sys.argv[1])
