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

## Recette — résultats du 26/09/2026

Console : Game Boy Pocket MGB-ECPU-01, alimentation de laboratoire à 3,2 V,
Pokémon Version Rouge. Liaison directe, sans tampon, 6 fils + masse.

```
  cadence          59.723 img/s   (attendu 59,727)  sur 181 s
  trames            10828
  lignes/trame        144        (attendu 144)
  trames douteuses      0
  mots restants         0
  debordements FIFO     0
  trames perdues        0
```

| Critère | Mesure | |
|---|---|---|
| Image reconnaissable | **écran de titre Pokémon Version Rouge, pixel exact** | ✅ |
| Cadence | **59,723 img/s**, écart **0,007 %** | ✅ |
| Lignes par trame | **144** sur **10 828 trames** | ✅ |
| Trames douteuses | **0** | ✅ |
| Mots restants | **0** — le DMA n'a jamais manqué un front | ✅ |
| Débordements FIFO | **0** | ✅ |

> 🔑 **Ce qui prouve la chaîne, c'est le petit texte.** « ©1995-1999 GAME FREAK inc. »
> est parfaitement lisible dans le PNG. Une erreur d'un seul pixel — front mal choisi,
> octet inversé, décalage de bit — l'aurait réduit en bouillie. Voir
> [`../../docs/releves/phase2-premiere-trame.png`](../../docs/releves/phase2-premiere-trame.png).

**Transitoire de démarrage.** Le premier essai montrait 8 trames douteuses et
4 débordements FIFO, tous **figés** — le DMA armé au milieu d'une trame rend les
premières incomplètes. La commande `r` a été ajoutée pour le démontrer plutôt que
le supposer : après remise à zéro, 10 828 trames sans une seule erreur.

## Deux bugs trouvés en éprouvant le firmware

**1. `GP5` jamais initialisée.** `capture::init()` appelait `gpio_init()` sur GP3 et
GP4 mais pas sur GP5, la réserve. Sans cet appel l'entrée du pad reste désactivée et
`gpio_get()` renvoie 0 quoi qu'il arrive sur le fil. Le diagnostic annonçait donc
« fil non branché » sur un câblage sain.

> ⚠️ **Un diagnostic qui ment est pire qu'un diagnostic absent** : il envoie démonter
> ce qui marche. Corrigé, `GP5` donne 1 840 transitions sur 100 ms — l'attendu étant
> 9 196 Hz × 2 × 0,1 = 1 839.

**2. Cadence mesurée sur une fenêtre d'une seconde.** Elle ne rendait que des entiers,
59 ou 60, et ne permettait pas de vérifier les 59,727 attendus. Moyennée sur toute la
durée d'observation, la résolution tombe à 0,017 img/s sur 60 s.

## Critère de sortie de la phase 2

- [x] Le vidage ASCII montre un écran **reconnaissable**
- [x] Le PNG est net : pas de décalage, pas de cisaillement, pas de groupes de 4 inversés
- [x] **59,73 img/s** ± 0,1 → **59,723**
- [x] **144 lignes/trame sur 10 000 trames** → **10 828**, `trames douteuses` = 0
- [x] `mots restants` = 0 et `débordements` = 0 sur la même durée
- [ ] L'image reste correcte après **30 min** et après un **cycle d'extinction** de la console

La table de diagnostic symptôme → cause est dans
[`../../docs/etapes-detaillees.md`](../../docs/etapes-detaillees.md) §D.10.
