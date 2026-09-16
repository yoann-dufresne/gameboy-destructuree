# Phase 0 — validation du câblage

Fait défiler 9 mires de diagnostic sur une dalle HUB75 64×64. Chaque mire isole un
groupe de signaux : celle qui échoue désigne le fil fautif. Le nom de la mire, ce
qu'elle valide et le symptôme attendu en cas de défaut sont écrits sur la console USB.

## Pourquoi pas l'exemple `pico-examples` tel quel

Il affiche une image *mountains* de **128×64**. Passer `WIDTH` à 64 ne la recadre pas :
le pas de ligne reste celui de l'image, l'affichage est illisible. Et une photo de
montagne ne dit pas *quel* fil est mal branché. Le programme PIO, lui, est repris
sans modification (voir `PROVENANCE.txt`).

## Construire et flasher

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# BOOTSEL enfoncé à la mise sous tension → volume RP2350 monté
cp build/phase0_bringup.uf2 /media/$USER/RP2350/
```

## Console

```bash
../../tools/console.py          # /dev/ttyACM0 par défaut
```

⚠️ `cat /dev/ttyACM0` ne donne rien : le firmware n'émet que lorsque **DTR** est
asserté, et `cat` ne l'assère pas.

## Les mires

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

## Les deux programmes de diagnostic

Si une mire échoue, deux cibles supplémentaires cernent le fautif.

### `phase0_diag_adresse`

Fige l'adresse de ligne sur une valeur connue, écran entièrement blanc. La ligne qui
s'allume révèle quels bits d'adresse arrivent réellement à la dalle.

| Adresse | Bit testé | Lignes attendues |
|---|---|---|
| 0 | aucun (référence) | 0 et 32 |
| 1 | A (GP6) | 1 et 33 |
| 2 | B (GP7) | 2 et 34 |
| 4 | C (GP8) | 4 et 36 |
| 8 | D (GP9) | 8 et 40 |
| 16 | E (GP10) | 16 et 48 |
| 31 | tous | 31 et 63 |

Un bit mort laisse l'affichage sur les lignes 0 et 32.

> ⚠️ L'adresse étant figée, la ligne reste allumée en permanence au lieu de 1/32 du
> temps. La largeur d'impulsion /OE est divisée par ~32 pour que le courant moyen dans
> ces LED reste celui du régime normal.

### `phase0_walk_gpio`

Met une seule broche du Pico à 3,3 V à la fois et annonce la broche du connecteur HUB75
qui doit suivre. À vérifier au multimètre **côté dalle** : c'est toute la liaison
(soudure, embase, nappe) qui est testée. Le panneau est vidé au démarrage pour qu'aucune
LED ne s'allume pendant le test.

Fil coupé → 0 V. Fils inversés → 3,3 V sur la mauvaise broche. Court-circuit → 3,3 V
sur deux broches.

## Basculer en BOOTSEL sans débrancher

```bash
python3 -c "import serial,time; s=serial.Serial('/dev/ttyACM0',1200); s.dtr=False; time.sleep(.1); s.close()"
```

## Critère de sortie

Les 9 mires correctes, stables, sans colonne parasite ni scintillement quand on
bouge la nappe. Alors seulement on passe à la phase 1.

### ✅ Atteint le 16/09/2026

Les 9 mires passent sur une dalle Seengreat RGB Matrix P3.0-64×64 pilotée par un
Pico 2 W en 3,3 V direct, sans adaptateur de niveau.

Deux enseignements consignés dans le plan :

1. Le brochage du **tableau 2-2 du wiki Seengreat** (carte adaptatrice V3.8) place
   A–E sur GP10/16/18/20/22, cinq broches non contiguës — incompatible avec le
   `out pins, 5` du PIO. Câblée ainsi, la dalle n'adressait que les lignes 0, 1,
   32 et 33, seul le bit A variant.
2. La **nappe fournie se numérote à l'envers du connecteur** : fil n° N ⟷ broche
   n° (17 − N). Le fil gris est `E`, seul signal hors séquence et seule couleur
   grise du ruban.

Fiche de câblage : [`../../docs/cablage-pico-hub75.html`](../../docs/cablage-pico-hub75.html)
