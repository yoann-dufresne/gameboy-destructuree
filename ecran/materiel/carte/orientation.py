#!/usr/bin/env python3
"""
Guide de sens de montage, tiré de la carte : fab/orientation_cms.png.

En rouge, le repère de chaque CMS polarisé, à retrouver dans l'aperçu d'assemblage de JLCPCB
(leurs bibliothèques ne tournent pas toutes comme celles de KiCad) ; en cyan, celui des
traversants polarisés, à souder à la main.

    python3 orientation.py          (après fabrication.sh, qui écrit fab/cpl_jlcpcb.csv)
"""
import csv
import os

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly, Rectangle
import pcbnew

ICI = os.path.dirname(os.path.abspath(__file__))
mm = pcbnew.ToMM


def repere(ref):
    """(pastille marquée, ce qu'on y cherche) pour une pièce polarisée, None sinon."""
    if ref in ('C1', 'C2', 'C4'):
        return '1', '+'
    if ref == 'J1':
        return '1', 'borne 1 (+)'
    pre = ref.rstrip('0123456789')
    num = ref[len(pre):]
    if not num:
        return None
    if pre in ('LED', 'D'):
        return '1', 'cathode'                                 # LED, TVS D1, Schottky D2 à D5
    if pre == 'Q':
        return '3', 'patte seule'                             # drain des 2N7002
    if pre == 'U' and int(num) >= 5:
        return '1', 'broche 1 (point)'                        # tampons 74AHCT541
    return None


def main():
    b = pcbnew.LoadBoard(os.path.join(ICI, 'carte-ecran.kicad_pcb'))
    cms = {row['Designator'] for row in csv.DictReader(open(os.path.join(ICI, 'fab', 'cpl_jlcpcb.csv'), encoding='utf-8'))}
    bord = b.GetBoardEdgesBoundingBox()
    larg, haut = mm(bord.GetWidth()), mm(bord.GetHeight())
    fig, ax = plt.subplots(figsize=(22, 15.5))
    ax.add_patch(Rectangle((0, 0), larg, haut, fc='#2b4a2b', ec='k', lw=1.5))
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        c = fp.GetCourtyard(pcbnew.F_CrtYd)
        if c.OutlineCount():
            o = c.Outline(0)
            ax.add_patch(MPoly([(mm(o.CPoint(i).x), mm(o.CPoint(i).y)) for i in range(o.PointCount())], closed=True,
                               fc='none', ec='#dee2e6' if ref in cms else '#74c0fc', lw=0.6))
        r = repere(ref)
        for p in fp.Pads():
            x, y = mm(p.GetPosition().x), mm(p.GetPosition().y)
            w, h = mm(p.GetBoundingBox().GetWidth()), mm(p.GetBoundingBox().GetHeight())
            marque = r is not None and p.GetNumber() == r[0]
            col = ('#ff2020' if ref in cms else '#00e5ff') if marque else '#c9a227'
            ax.add_patch(Rectangle((x - w / 2, y - h / 2), w, h, fc=col, ec='none', zorder=3 if marque else 2))
        if r is not None:
            x, y = mm(fp.GetPosition().x), mm(fp.GetPosition().y)
            ax.text(x, y - 2.6 if ref.startswith(('LED', 'Q')) else y, ref, fontsize=6.5, color='w', ha='center',
                    va='center', zorder=4, bbox=dict(fc='#00000080', ec='none', pad=0.5))
    ax.set_xlim(-2, larg + 2)
    ax.set_ylim(haut + 2, -2)
    ax.set_aspect('equal')
    ax.axis('off')
    ax.set_title("Sens de montage — carte vue de dessus. En ROUGE : le repère de chaque CMS polarisé, à retrouver "
                 "dans l'aperçu JLCPCB (cathode des LED, de D1 et des SS14, + de C4, broche 1 des 74AHCT541, "
                 "patte seule des 2N7002).\nEn CYAN : le repère des traversants à souder à la main (+ de C1 et C2, "
                 "borne 1 de J1). Contours gris : CMS posés par JLCPCB ; bleus : traversants.", fontsize=11)
    fig.savefig(os.path.join(ICI, 'fab', 'orientation_cms.png'), dpi=110, bbox_inches='tight')
    print('guide de sens de montage : fab/orientation_cms.png')


if __name__ == '__main__':
    main()
