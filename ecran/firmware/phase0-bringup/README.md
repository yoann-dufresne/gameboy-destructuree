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

## Critère de sortie

Les 9 mires correctes, stables, sans colonne parasite ni scintillement quand on
bouge la nappe. Alors seulement on passe à la phase 1.
