# Phase 2 — capture du bus LCD

Firmware du module CAPTURE. Échantillonne le bus LCD de la Game Boy et
reconstitue la trame 160×144 en 2 bits par pixel. **Pas de réseau** : cette
phase prouve par l'image.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/sniffer.uf2
```

## Découpage

| | |
|---|---|
| `include/config.h` | **tout ce qui vient d'une mesure**, avec la mesure qui le justifie |
| `src/capture.pio` | les 3 instructions, et pourquoi elles sont dans cet ordre |
| `src/capture.hpp` | la façade : `init()`, `trame_prete()`, `stats()` |
| `src/capture.cpp` | PIO, DMA, les 2 interruptions, le double tampon |
| `src/main.cpp` | recette de phase 2 : vidages et compteurs |

**Le CPU ne touche aucun pixel.** Le PIO échantillonne, le DMA écrit, le
processeur compte des lignes et réarme un pointeur une fois par trame — dans
une fenêtre de VBlank de 1,09 ms, soit 163 000 cycles pour un travail qui en
demande quelques dizaines.

## Les trois instructions, et la mesure derrière chacune

```
wait 1 pin 2          ; garantit qu'on verra le prochain front descendant
wait 0 pin 2 [0]      ; front DESCENDANT
in   pins, 2          ; LD0 et LD1
```

🔬 **Phase 0** : `LD` change sur le front **montant** de l'horloge pixel —
209 des 256 transitions à +0 échantillon, 47 à +1, aucune au-delà. Le front
montant est donc exactement l'instant de la transition.

Le temps haut de l'horloge étant de **125 ns** et sa période de **238 ns**, le
front **descendant** tombe au milieu de la fenêtre stable : 75 ns de marge
avant, 113 ns après. D'où l'inversion des deux `wait`, et d'où le délai nul.

## Un seul canal DMA

Les lignes du framebuffer sont **contiguës** (40 octets, sans trou), donc les
frontières de ligne sont implicites : **1 440 mots, un transfert, une trame.**

> Une version antérieure du plan prévoyait deux canaux chaînés déroulant une
> table de 144 adresses, parce qu'une ligne de 40 octets devait aller dans un
> canevas de 48. Cette mécanique a disparu le 25/09/2026, quand l'émetteur est
> devenu agnostique de l'afficheur.

⚠️ Le DMA compte des **mots**, pas des lignes. Un front d'horloge pixel raté
décale tout et ne se rattrape jamais dans la trame. D'où le compteur
d'intégrité sur `P2-ST` : **144 impulsions par trame**, mesuré en phase 0.

## L'ordre de la séquence VSYNC n'est pas négociable

1. relever le compte de lignes (intégrité)
2. **arrêter le DMA**, relever ce qu'il n'a pas écrit
3. **puis** vider le FIFO et redémarrer le PIO
4. **puis** basculer le tampon
5. réarmer, relancer

Vider le FIFO avant d'arrêter le PIO le laisserait le remplir à nouveau.
Basculer le tampon avant d'arrêter le DMA le laisserait écrire dans la trame
qu'on publie — une déchirure intermittente, donc pénible à trouver.

## Console USB

⚠️ GP0/GP1 portent `LD0`/`LD1` : l'UART par défaut du SDK est inutilisable.
Même piège que sur le module écran, où ils portaient R1/G1.

```bash
../../tools/console.py
```

| Touche | Effet |
|---|---|
| `a` | vidage **ASCII** 80×72 — ne demande aucun outil |
| `p` | vidage **hexadécimal**, consommé par `tools/gbdump.py` |
| `s` | compteurs |
| `h` | aide |

Les compteurs s'affichent seuls toutes les 5 s.

## Les compteurs, et ce qu'un écart révèle

| Compteur | Attendu | Un écart signifie |
|---|---|---|
| `cadence` | **59,73 img/s** | 29,86 ⇒ on déclenche sur un signal à moitié fréquence |
| `lignes/trame` | **144** | `P2-ST` manque des impulsions, ou en invente |
| `trames douteuses` | **0** | idem, cumulé |
| `mots restants` | **0** | des fronts d'horloge pixel manquent : délai mal réglé, ou front mal choisi |
| `débordements FIFO` | **0** | le DMA ne suit pas — ne devrait jamais arriver avec 30 µs de marge |
| `trames perdues` | — | la boucle principale n'a pas lu assez vite ; sans conséquence en phase 2 |

## Faire une image

```bash
../../tools/gbdump.py --echelle 4                  # gris
../../tools/gbdump.py --palette dmg                # les 4 verts d'origine
../../tools/gbdump.py --palette diag               # une couleur franche par indice
```

> 🔑 **L'instrument ne peut pas mentir.** Contrairement au module écran, dont le
> compteur de trames annonçait 788 Hz parfaitement stables pendant que la dalle
> était noire, ici : si l'image est reconnaissable, la chaîne est juste.

La palette `diag` rend une erreur d'ordre de bits immédiatement visible — chaque
indice a sa couleur.

## Critère de sortie de la phase 2

- [ ] Le vidage ASCII montre un écran **reconnaissable**
- [ ] Le PNG est net : pas de décalage, pas de cisaillement, pas de groupes de 4 inversés
- [ ] **59,73 img/s** ± 0,1 sur 60 secondes
- [ ] **144 lignes/trame sur 10 000 trames**, `trames douteuses` = 0
- [ ] `mots restants` = 0 et `débordements` = 0 sur la même durée
- [ ] L'image reste correcte après 30 min et après un cycle d'extinction de la console

La table de diagnostic symptôme → cause est dans
[`../../docs/etapes-detaillees.md`](../../docs/etapes-detaillees.md) §D.10.
