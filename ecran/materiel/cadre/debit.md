# Cadre arrière v4 — débit et quincaillerie

Généré par `gen_cadre.py` ; cotes des dalles reprises du cadre v3. Les quantités de visserie comptent chaque emploi, prévoir 10 % de plus.

## Profilés aluminium 2020, rainure B 6 mm, coupés à longueur

| Repère | Longueur | Qté | Rôle |
|---|---|---|---|
| P1 | 570 mm | 3 | profilé de rangée, axe à 40 mm sous le centre des dalles |
| P2 | 430 mm | 2 | montant, en x = -214 et +170 (vue de dos), derrière les profilés de rangée, dans les bandes libres entre les connecteurs des dalles |

Total : 2,57 m. Coupe d'équerre, sans taraudage.

## Quincaillerie

| Pièce | Qté | Emploi |
|---|---|---|
| Équerre 2020 (fonte d'alu, 20 × 20) | 10 | croisements montant / profilé : 2 en haut et au milieu droit, 1 au milieu gauche (plateau de la carte) et en bas (pied, alimentation) ; 1 de réserve. **Limer les 2 ergots de centrage d'une aile** : contre le flanc du montant, ils tombent en travers de sa rainure |
| Écrou marteau M5 pour rainure B 6 | 48 | un par vis M5 |
| Vis M5 × 10 tête cylindrique (DIN 912 / ISO 4762) | 40 | brides 24, crochets muraux 4, patins arrière 8, pieds 4 |
| Vis M5 × 8 tête cylindrique (DIN 912 / ISO 4762) | 5 | plateau de carte 3, support de prise 2 |
| Vis M5 × 6 tête cylindrique (DIN 912 / ISO 4762) | 3 | berceaux alim 2, liaison de terre 1 |
| Vis M4 × 8 tête cylindrique basse (DIN 7984) et écrou marteau M4 | 18 de chaque | équerres, avec leur kit de fixation |
| Vis M3 × 10 tête fraisée (DIN 965 / ISO 10642) | 60 | dans les inserts des dalles : brides 48, agrafes 12 |
| Vis M3 × 8 tête fraisée (DIN 965 / ISO 10642) | 3 | alimentation, à travers la joue de ses berceaux côté bas de l'écran, dans les trous taraudés de son flanc ; elles y entrent de 3,0 mm |
| Vis M3 × 8 autotaraudeuse pour plastique, ou M3 × 8 | 6 | carte sur son plateau |
| Vis M4 × 12 autotaraudeuse pour plastique | 2 | sangle de l'alimentation |
| Adhésif double face (type 3M VHB) | 8 morceaux de 28 × 22 mm | colle les patins TPU : 4 sous les pieds, 1 sur chaque patin arrière. À défaut de TPU : 8 patins caoutchouc adhésifs Ø 15 à 20 mm, 3 mm d'épaisseur |
| Vis Ø 5 × 50 + chevilles adaptées au mur | 2 | accrochage mural |

Vis M5 : toutes à tête cylindrique, dessous plat ; aucune pièce n'a de fraisure M5. Leur longueur dépend de l'épaisseur de pièce sous la tête : la vis doit en dépasser de 3,2 mm au moins, pour traverser tout l'écrou marteau, et de 5,2 mm au plus, car le fond de la rainure est à 5,5 mm de la face : une vis plus longue y bute avant de serrer. Le générateur le vérifie pour chaque vis.

## Pièces imprimées (PETG conseillé : tenue en température et au fluage ; patins en TPU)

| Fichier | Qté | Encombrement (mm) | Volume (cm³) | Remarque |
|---|---|---|---|---|
| `stl/bride_interieure.stl` | 2 | 40 × 96 × 24 | 29 | rangée du bas : joints verticaux intérieurs |
| `stl/bride_interieure_longue.stl` | 4 | 40 × 129 × 24 | 37 | rangées du haut et du milieu : descend sur les TL/TR des 2 dalles du dessous |
| `stl/bride_bord_droite.stl` | 1 | 22 × 96 × 24 | 16 | rangée du bas, bord droit (vue de dos) |
| `stl/bride_bord_gauche.stl` | 1 | 22 × 96 × 24 | 16 | rangée du bas, bord gauche |
| `stl/bride_bord_droite_longue.stl` | 2 | 22 × 129 × 24 | 20 | rangées du haut et du milieu, bord droit ; descend sur le TR de la dalle du dessous |
| `stl/bride_bord_gauche_longue.stl` | 2 | 22 × 129 × 24 | 20 | rangées du haut et du milieu, bord gauche ; descend sur le TL de la dalle du dessous |
| `stl/agrafe_joint.stl` | 6 | 32 × 32 × 5 | 5 | joints horizontaux, TM/BM |
| `stl/gabarit_bride.stl` | 1 | 40 × 129 × 1 | 6 | à imprimer en premier : ses 6 trous sur une jointure de 4 dalles |
| `stl/plateau_carte.stl` | 1 | 202 × 136 × 20 | 53 | porte la carte de l'écran |
| `stl/berceau_alim_gauche.stl` | 1 | 60 × 109 × 24 | 50 | côté bornier, 1 trou M3, sans sangle ; couché sur le flanc |
| `stl/berceau_alim_droit.stl` | 1 | 60 × 109 × 24 | 50 | 2 trous M3 ; couché sur le flanc |
| `stl/sangle_alim.stl` | 1 | 24 × 109 × 4 | 10 | sur le berceau droit |
| `stl/support_prise.stl` | 1 | 46 × 82 × 20 | 12 | face de l'embase sur le plateau |
| `stl/pied.stl` | 2 | 240 × 107 × 24 | 204 | écran droit ; couché sur le flanc |
| `stl/pied_incline10.stl` | 2 | 238 × 115 × 24 | 195 | variante : écran penché de 10° en arrière |
| `stl/crochet_mural.stl` | 2 | 34 × 36 × 79 | 42 | face murale sur le plateau |
| `stl/patin_arriere.stl` | 4 | 30 × 24 × 75 | 46 | toujours en place ; patin TPU collé sur la face |
| `stl/patin_tpu.stl` | 8 | 28 × 22 × 3 | 2 | **TPU**, collé au double face : 1 sur chaque patin arrière, 2 sous chaque pied |
| `stl/ancre_collier.stl` | 20 | 16 × 10 × 8 | 1 | dans les rainures, pour les colliers |

Réglages : 4 périmètres, 30 à 40 % de remplissage gyroïde, buse 0,4. Tout tient sur le plateau de 256 mm de l'A1, sans support.

Patins TPU : TPU 95A, 3,0 mm d'épaisseur, remplissage 100 %, 20 à 30 mm/s, depuis la bobine externe de l'A1 (l'AMS lite ne prend pas le TPU). Les coller sur une face dégraissée à l'alcool. Garder cette épaisseur : l'assemblage la compte entre le plan arrière et le sol.
