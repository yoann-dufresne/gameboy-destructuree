# Liste de matériel — montage définitif de l'écran 3 × 3

*Établie le 29/10/2026, avec le [dossier de montage](README.md).* Tout ce qu'il faut pour
passer du montage de preuve de concept au montage définitif : la carte, le cadre, le
faisceau, le secteur et l'outillage. Les quantités sont celles des nomenclatures générées,
qui font foi si un paramètre change :
[`carte/fab/nomenclature.md`](carte/fab/nomenclature.md),
[`cadre/debit.md`](cadre/debit.md), [`cadre/faisceau.md`](cadre/faisceau.md).

Coche la colonne **Là ?** avant de commander.

---

## Ce que valent les liens et les prix

Comme pour la [liste d'achats du projet](../../achats.md) :

- les liens AliExpress sont des **liens de recherche**, stables, pas des annonces vérifiées.
  Vérifie la variante, le vendeur et le stock ;
- **les prix sont des ordres de grandeur**, pas des relevés ;
- les pièces dont la marque compte (connecteurs Phoenix, porte-fusibles, fusibles) se
  prennent chez un **distributeur** : Mouser, DigiKey ou Reichelt. Les références
  fabricant sont données ; LCSC en a une partie, à ajouter à la commande JLCPCB.

**Trois commandes suffisent** : JLCPCB pour la carte, un distributeur pour les traversants
et le petit matériel électrique, AliExpress (ou un vendeur de profilés) pour le cadre.
Profilés, écrous marteau et équerres **chez le même vendeur** : la rainure « 6 mm » n'a pas
exactement la même forme partout.

---

## 0. Déjà là

| Là ? | Pièce | Qté | Note |
|---|---|---|---|
| ☐ | Dalle Seengreat RGB Matrix P3.0-64x64 | 9 | |
| ☐ | Raspberry Pi Pico 2 W (tête) et Pico 2 (nœuds) | 1 + 3 | **avec leurs barrettes mâles** : ils s'enfichent sur la carte |
| ☐ | Alimentation 5 V 30 A | 1 | 199 × 98 × 43 mm, cotes relevées : berceaux et sangles sont dessinés pour elle ; **capot de bornier obligatoire** |
| ☐ | Nappes HUB75 16 points fournies avec les dalles | 9 | 6 entre dalles, et J5 → dalle 4, J6 → dalle 1 (≥ 25 cm) |
| ☐ | Cordon secteur IEC C13 | 1 | celui d'un PC ou d'un écran |
| ☐ | Imprimante 3D (Bambu Lab A1) | — | toutes les pièces tiennent sur son plateau de 256 mm |

---

## 1. La carte

### 1a. Circuit imprimé et CMS — JLCPCB

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | Circuit imprimé **198 × 135 mm, 2 couches, 1,6 mm, cuivre 2 oz** (70 µm, pour les 30 A), HASL sans plomb : [`carte/fab/carte-ecran-gerber.zip`](carte/fab/) | 5, minimum de commande | JLCPCB | 90–150 € avec l'assemblage, livraison et TVA comprises |
| ☐ | Peuplement des CMS, face dessus : 93 composants, 16 références. **Pas dans le zip** : `bom_jlcpcb.csv` et `cpl_jlcpcb.csv`, à téléverser à l'étape « PCB Assembly » | 2 cartes, une de secours | JLCPCB (pièces LCSC) | (compris ci-dessus) |

Marche à suivre au §3 du [dossier de montage](README.md). Vérifie dans l'aperçu de JLCPCB
**l'orientation** des diodes, des LED, des SOT-23, du condensateur C4 et des TSSOP avant de
valider : leurs bibliothèques ne tournent pas toujours
comme celles de KiCad. Les pièces « étendues » coûtent environ 3 $ de plus chacune.

> Souder les CMS soi-même reste possible : pochoir (*stencil*) commandé avec la carte, pâte à
> braser, air chaud. Six TSSOP au pas de 0,65 mm : c'est faisable, pas reposant.

### 1b. Traversants, soudés à la main

Une seule commande, chez **DigiKey France** : livraison gratuite à partir de 75 €, 25 € en
dessous. Les liens ouvrent la recherche par référence fabricant : prendre la ligne dont la
référence est exactement celle du tableau. Stocks relevés le 29/10/2026.

| Là ? | Pièce | Qté | Référence | ~Prix |
|---|---|---|---|---|
| ☐ | Bornier à vis J1 : **Phoenix MKDS 3/4-5,08**, 4 bornes, pas de 5,08 mm, 24 A par borne | 1 | [1712805](https://www.digikey.fr/fr/products/result?keywords=1712805) | 2,5 € |
| ☐ | Embase de départ : **Phoenix MSTBA 2,5/2-G-5,08** | 9 | [1757242](https://www.digikey.fr/fr/products/result?keywords=1757242) | 0,5–0,9 € pièce |
| ☐ | Fiche de câble de dalle : **Phoenix MSTB 2,5/2-ST-5,08** | 10, dont 1 de rechange | [1757019](https://www.digikey.fr/fr/products/result?keywords=1757019) | 1,5 € pièce |
| ☐ | Porte-fusible lame mini **Keystone 3568** | 9 | [3568](https://www.digikey.fr/fr/products/detail/keystone-electronics/3568/2137306) | 1 € pièce |
| ☐ | Fusible lame mini 7,5 A, marron, **Littelfuse** | 12 : 9 + 3 de rechange | [029707.5WXNV](https://www.digikey.fr/fr/products/result?keywords=029707.5WXNV) | 0,4 € pièce |
| ☐ | Condensateur 1000 µF 16 V, Ø 10 × 20 mm, pas de 5 mm, **Nichicon** | 2 | [UHE1C102MPD](https://www.digikey.fr/fr/products/result?keywords=UHE1C102MPD) | 0,9 € pièce |
| ☐ | Embase HUB75 IDC 2 × 8 détrompée, droite, **Würth** | 3 | [61201621621](https://www.digikey.fr/fr/products/result?keywords=61201621621) | 0,5 € pièce |
| ☐ | Barrette femelle 1 × 20, 8,5 mm, **Sullins** : supports des Pico | 8 | [PPTC201LFBN-RC](https://www.digikey.fr/fr/products/result?keywords=PPTC201LFBN-RC) | 0,9 € pièce |
| ☐ | Barrette mâle 1 × 40, à couper, **Sullins** : J2, J3, JP1 à JP3 | 1 | [PRPC040SAAN-RC](https://www.digikey.fr/fr/products/result?keywords=PRPC040SAAN-RC) | 0,6 € |
| ☐ | Barrette mâle 2 × 12, **Sullins** : J7, la sonde | 1 | [PRPC012DAAN-RC](https://www.digikey.fr/fr/products/result?keywords=PRPC012DAAN-RC) | 0,6 € |
| ☐ | Cavalier 2,54 mm, **Sullins** : choix de G1 | 3 | [SPC02SYAN](https://www.digikey.fr/fr/products/result?keywords=SPC02SYAN) | 0,1 € pièce |
| ☐ | Bouton 6 × 6 mm traversant, 4,3 mm de haut, **Omron** : RESET | 4 | [B3F-1000](https://www.digikey.fr/fr/products/result?keywords=B3F-1000) | 0,3 € pièce |

Environ 55 € : sous le seuil de livraison gratuite. Les pièces du faisceau (§3) peuvent
s'ajouter à la même commande pour le franchir.

### 1c. Option

| Là ? | Pièce | Qté | Note |
|---|---|---|---|
| ☐ | Potentiomètre 10 kΩ linéaire et 3 fils, sur J2 | 1 | luminosité par GP26 de la tête : **le firmware ne le lit pas encore** |

---

## 2. Le cadre

### 2a. Profilés et visserie — une commande chez Motedis

[Motedis](https://www.motedis.fr/fr/) coupe au millimètre, frais de coupe compris, et livre en
France. Tout est en **rainure 6, type B** : ne rien mélanger avec du « type I » ou de la rainure 5.
Prix relevés le 29/10/2026, hors TVA ; le stock réel ne s'affiche qu'au panier.

| Là ? | Pièce | Qté | Lien | ~Prix HT |
|---|---|---|---|---|
| ☐ | Profilé 20 × 20 rainure 6 type B, coupé à **570 mm**, coupe droite sans usinage | 3 | [Profilé aluminium 20x20 Type B rainure 6](https://www.motedis.fr/fr/Profile-aluminium-20x20-Type-B-rainure-6) | 4,07 €/m |
| ☐ | Le même, coupé à **430 mm** | 2 | (même page) | |
| ☐ | Équerre 20 × 20 rainure 6 type B | 10 | [Équerre d'assemblage 20x20 B-type rainure 6](https://www.motedis.fr/fr/Equerre-dassemblage-20x20-B-type-rainure-6) | 3 € les 10 |
| ☐ | Écrou en T rainure 6 type B **M4**, fixation des équerres | 20 | [Écrou à tête marteau B-Type rainure 6 M4](https://www.motedis.fr/fr/Ecrou-a-tete-marteau-B-Type-rainure-6-M4) | 0,14 € pièce |
| ☐ | Vis DIN 7984 **M4 × 8**, fixation des équerres | 20 | [Vis DIN 7984 M4x8](https://www.motedis.fr/fr/Vis-DIN-7984/M4x8) | 0,04 € pièce |
| ☐ | Écrou en T rainure 6 type B **M5**, tout le reste | 100 (sachet ; 48 utiles) | [Écrou à tête marteau B-Type rainure 6 M5](https://www.motedis.fr/fr/Ecrou-a-tete-marteau-B-Type-rainure-6-M5) | 15,55 € les 100 |
| ☐ | Vis DIN 912 **M5 × 10** : brides, crochets, patins arrière, pieds | 100 (sachet ; 40 utiles) | [Vis DIN 912 M5x10](https://www.motedis.fr/fr/Vis-DIN-912/M5x10) | 3,33 € les 100 |
| ☐ | Vis DIN 912 **M5 × 8** : plateau de la carte, support de prise | 5 (sachet de 100) | [Vis DIN 912 M5x8](https://www.motedis.fr/fr/Vis-DIN-912/M5x8) | 16,66 € les 100 (07/11/2026) |
| ☐ | Vis DIN 912 **M5 × 6** : berceaux de l'alimentation, cosse de terre | 3 (sachet de 100) | [Vis DIN 912 M5x6](https://www.motedis.fr/fr/Vis-DIN-912/M5x6) | 10,00 € les 100 (07/11/2026) |

L'équerre se fixe avec son propre kit M4 : deux écrous et deux vis M4 × 8 par équerre. Elle
a deux ergots de centrage sur chaque aile, prévus pour un profilé qui bute par son bout. Ici
les montants croisent les rangées, donc **les ergots d'une aile sont à limer** : étape 5 du
[dossier de montage](README.md). Les
écrous M5 servent aux brides (24), au plateau (3), aux berceaux (2), à la prise (2), aux pieds
(4), aux crochets (4), aux patins (8) et à la cosse de terre (1). Toutes ces vis ont la tête cylindrique de la DIN 912,
dessous plat : les pièces imprimées n'ont aucune fraisure M5.

**Longueur des vis M5** : une vis doit dépasser de la pièce qu'elle serre de 3,2 mm au moins,
pour traverser tout l'écrou marteau, et de 5,2 mm au plus. Le fond de la rainure est à 5,5 mm
de la face, et une vis plus longue y bute avant de serrer (plan de l'écrou et modèle du profilé
Motedis). D'où trois longueurs. Les **M5 × 12** d'abord prévues pour les pieds dépassaient de
6 mm : elles ne servent plus, les pieds prennent des M5 × 10. La cosse de terre ne met que
1,4 mm sous la tête, cosse et rondelle à dents comprises : elle prend une M5 × 6, une M5 × 10
buterait au fond de la rainure avant de la serrer. Le 07/11/2026, Motedis affichait
les M5 × 8 et M5 × 6 en « stock actuellement insuffisant » : une quincaillerie a les mêmes, en
DIN 912 ou ISO 4762.

**Hors Motedis** : rondelles à dents M5 DIN 6798 A (6), vis M3 × 10 fraisées (60), vis M3 × 8
fraisées (3, alimentation dans ses berceaux), vis M3 × 8
et M4 × 12 autotaraudeuses pour plastique (6 et 2), adhésif double face type 3M VHB pour les
8 patins TPU, vis et chevilles murales (2) : assortiments en magasin de bricolage ou sur
AliExpress.

### 2b. Pièces imprimées

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | **PETG**, 1,75 mm : ≈ 0,9 kg pour toutes les pièces, 1,2 kg avec la variante de pieds inclinés | 1 bobine de 1 kg, 2 avec les pieds inclinés | n'importe où | 20–25 € la bobine |
| ☐ | **TPU** 95A, 1,75 mm : ≈ 20 g pour les 8 patins | un reste de bobine | n'importe où | — |

Les 19 fichiers à imprimer, leurs quantités et leurs réglages sont dans
[`cadre/debit.md`](cadre/debit.md). Le PLA du cadre v3 a flué sous le poids : **PETG**, au
moins pour les brides, les patins arrière, les berceaux de l'alimentation et les pieds. Les 8
patins antidérapants, eux, s'impriment en **TPU** : ils remplacent les patins caoutchouc
adhésifs.

---

## 3. Faisceau et secteur

Longueurs de coupe, câble par câble : [`cadre/faisceau.md`](cadre/faisceau.md).

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | Fil souple **0,75 mm²** (18 AWG) rouge | 9,3 m | [AliExpress](https://fr.aliexpress.com/w/wholesale-18awg-silicone-wire-red-black.html) ou magasin d'électricité | 4–6 € |
| ☐ | Fil souple 0,75 mm² noir | 9,3 m | idem | 4–6 € |
| ☐ | Boîtier JST **VHR-4N** (côté dalle) | 9 + 2 | distributeur, ou [AliExpress](https://fr.aliexpress.com/w/wholesale-VH3.96-4p-housing-crimp.html) | 2 € |
| ☐ | Contact JST **SVH-21T-P1.1** (AWG 22–18) | 40 | idem | 3 € |
| ☐ | Embout double **2 × 0,75 mm²**, gris, ex. Phoenix AI-TWIN 2 × 0,75-10 GY (3200807) | 23 : 18 + 5 | distributeur, ou [AliExpress](https://fr.aliexpress.com/w/wholesale-twin-ferrule-2x0.75.html) | 3–5 € |
| ☐ | Fil souple **2,5 mm²** (H07V-K) rouge et noir, alimentation → carte | 0,6 m de chaque | magasin d'électricité | 2 € |
| ☐ | Cosse fourche isolée bleue 1,5–2,5 mm², M3,5 ou M4 (selon les vis du bornier de l'alim) | 6 : 4 + 2 | assortiment de cosses | — |
| ☐ | Embout 2,5 mm² | 6 : 4 + 2 | assortiment d'embouts | — |
| ☐ | Fil souple **1 mm²** (H05V-K) marron, bleu, vert-jaune : embase → alimentation | 0,5 m de chaque | magasin d'électricité | 2 € |
| ☐ | Fil souple 1,5 mm² vert-jaune : liaison du cadre à la terre | 0,3 m | idem | 1 € |
| ☐ | Cosse femelle 6,3 mm **entièrement isolée**, et une cosse « piggyback » pour la terre | 3 + 1 | assortiment | — |
| ☐ | Cosse fourche isolée rouge 0,5–1,5 mm², M3,5 (L, N, ⏚ de l'alimentation) | 5 : 3 + 2 | assortiment | — |
| ☐ | Cosse à œillet M5, 1,5 mm² (terre sur le profilé du bas, vis M5 × 6) | 1 | assortiment | — |
| ☐ | Assortiment de cosses isolées et d'embouts, si tu n'en as pas | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-insulated-crimp-terminal-ferrule-kit.html) | 8–12 € |
| ☐ | **Embase IEC C14 3-en-1** : fiche, interrupteur, porte-fusible 5 × 20, découpe 47,5 × 27,5 mm | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-iec-c14-inlet-switch-fuse-3-in-1.html), ou distributeur | 3–8 € |
| ☐ | Fusible 5 × 20 mm **T 3,15 A** (temporisé) | 2 : 1 + 1 de rechange | distributeur, magasin d'électricité | 1 € |
| ☐ | Gaine tressée extensible Ø 6–10 mm, pour les fils secteur | 1 m | [AliExpress](https://fr.aliexpress.com/w/wholesale-braided-sleeving-pet-8mm.html) | 2 € |
| ☐ | Gaine thermorétractable assortie, et bagues ou étiquettes de repérage | 1 lot | idem | 3 € |
| ☐ | Colliers de serrage 2,5 × 100 mm | 50 | idem | 1 € |

> **Sans pince à contacts VH** : neuf câbles d'alimentation de dalle tout faits, VH 3.96
> 4 points, de 50 à 80 cm ([AliExpress](https://fr.aliexpress.com/w/wholesale-led-display-power-cable-vh3.96-4pin.html)),
> recoupés côté carte aux longueurs de [`faisceau.md`](cadre/faisceau.md). Ils remplacent les
> fils 0,75 mm², les boîtiers et les contacts. Vérifie que la section annoncée est bien de
> 18 AWG, et la polarité `+ + - -` à l'ohmmètre avant de brancher.

---

## 4. Outillage

| Là ? | Outil | Pour |
|---|---|---|
| ☐ | Pince à sertir les embouts, à empreinte carrée, 0,25–6 mm² | embouts simples et doubles |
| ☐ | Pince à sertir les cosses isolées (rouge, bleu, jaune) | cosses fourche, œillet, 6,3 mm |
| ☐ | Pince à sertir les contacts ouverts, 0,5–1 mm², type SN-28B | contacts VH ; inutile avec des câbles tout faits |
| ☐ | Clés six pans 2,5, 3 et 4 mm ; tournevis | visserie du cadre |
| ☐ | Fer à souder, flux, tresse | traversants de la carte |
| ☐ | Multimètre | mise en service : tensions, continuité de la terre |
| ☐ | Pince ampèremétrique continu (option) | courant plein blanc, à comparer aux 30 A de l'alimentation |
| ☐ | Scie à onglet pour aluminium (seulement si les profilés arrivent bruts) | coupe d'équerre des profilés |
| ☐ | Lime plate à métaux et étau aux mors protégés (ou mini-meuleuse type Dremel) | ergots d'une aile des 9 équerres |

---

## Récapitulatif

| Bloc | ~Prix |
|---|---|
| 1a · Carte : circuits imprimés et assemblage CMS | 90–150 € |
| 1b · Carte : traversants, en Phoenix d'origine | 45–60 € |
| 2a · Cadre : profilés coupés, équerres, visserie | 50–75 € |
| 2b · PETG | 20–25 € |
| 3 · Faisceau et secteur | 40–55 € |
| **Total, hors outillage** | **≈ 245–365 €** |

Avec les borniers 2EDG génériques et des câbles VH tout faits, compter 30 € de moins. Les
dalles, les Pico et l'alimentation sont déjà là.
