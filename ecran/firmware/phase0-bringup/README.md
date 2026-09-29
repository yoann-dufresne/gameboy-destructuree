# Phase 0 — validation du câblage

Trois petits programmes pour vérifier qu'un Pico 2 W est correctement câblé à une dalle HUB75
64 × 64, avant d'écrire le moindre pilote. Aucun réseau, aucun `secrets.h`.

## Construire

Procédure générique : [README du module](../../README.md#construire-flasher-observer). La
construction produit trois cibles : `phase0_bringup.uf2`, `phase0_diag_adresse.uf2` et
`phase0_walk_gpio.uf2`. Le programme PIO `hub75.pio` est celui de `pico-examples`, repris sans
modification ([`PROVENANCE.txt`](PROVENANCE.txt)). L'exemple d'origine n'a pas été utilisé tel
quel : il affiche une image de 128 × 64 qu'on ne peut pas simplement recadrer, et une photo ne
dit pas quel fil est mal branché.

## `phase0_bringup` — les mires

Le programme fait défiler 9 mires. Chacune isole un groupe de signaux : celle qui échoue
désigne le fil fautif. La console décrit chaque mire, ce qu'elle valide et le symptôme
attendu en cas de défaut.

| # | Mire | Valide | Symptôme d'un défaut |
|---|---|---|---|
| 1 | Cadre + coins | géométrie 64×64, orientation, toutes les adresses | coin manquant = ligne d'adresse morte |
| 2 | Rouge plein | R1 (GP0) et R2 (GP3) | demi-écran noir = la ligne R de ce groupe |
| 3 | Vert plein | G1 (GP1) et G2 (GP4) | idem |
| 4 | Bleu plein | B1 (GP2) et B2 (GP5) | idem |
| 5 | Moitiés rouge/bleu | séparation des deux demi-écrans | mélange = permutation entre groupes |
| 6 | Dégradé horizontal | profondeur BCM (8 plans) | marches franches = /OE (GP13) ou LAT (GP12) |
| 7 | Dégradé vertical | ordre des adresses A–E (GP6–GP10) | bandes désordonnées = deux fils permutés |
| 8 | Damier 1 px | ghosting, intégrité de CLK | traînées horizontales = ghosting |
| 9 | Balayage ligne | chaque adresse, une par une | ligne qui saute = fil d'adresse en l'air |

**Critère de sortie :** les 9 mires correctes et stables, sans colonne parasite ni
scintillement quand on bouge la nappe. Atteint le 16/09/2026 ; les deux pièges rencontrés en
chemin (le brochage du wiki Seengreat, inutilisable en câblage direct, et la nappe numérotée
à l'envers) sont décrits au [§2.3 du plan](../../docs/plan-firmware.md).

## `phase0_diag_adresse` — quels bits d'adresse arrivent

Si une mire d'adresse échoue, ce programme fige l'adresse de ligne sur une valeur connue,
écran entièrement blanc. La ligne qui s'allume révèle les bits d'adresse qui arrivent
réellement à la dalle.

| Adresse | Bit testé | Lignes attendues |
|---|---|---|
| 0 | aucun (référence) | 0 et 32 |
| 1 | A (GP6) | 1 et 33 |
| 2 | B (GP7) | 2 et 34 |
| 4 | C (GP8) | 4 et 36 |
| 8 | D (GP9) | 8 et 40 |
| 16 | E (GP10) | 16 et 48 |
| 31 | tous | 31 et 63 |

Un bit mort laisse l'affichage sur les lignes 0 et 32. L'adresse étant figée, la ligne reste
allumée en permanence au lieu de 1/32 du temps : la durée d'allumage est divisée par 32 environ
pour que le courant moyen dans ces LED reste celui du régime normal.

## `phase0_walk_gpio` — continuité fil par fil

Le programme met une seule broche du Pico à 3,3 V à la fois et annonce la broche du connecteur
HUB75 qui doit suivre. On la vérifie au multimètre **côté dalle**, ce qui teste toute la
liaison : soudure, embase, nappe. Le panneau est vidé au démarrage pour qu'aucune LED ne
s'allume pendant le test.

Fil coupé : 0 V. Fils inversés : 3,3 V sur la mauvaise broche. Court-circuit : 3,3 V sur deux
broches.
