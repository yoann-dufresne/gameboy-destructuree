# Recette de câblage — phase 1

*Vérification 4 du §C.5 : les 6 signaux relevés **au bout des fils soudés** ont-ils les
signatures établies en phase 0 ?*

Mesuré le 26/09/2026. Captures : `releves/phase1-lent.sr` (4 MS/s, 500 ms) et
`releves/phase1-rapide.sr` (24 MS/s, 67 ms).

> 🔑 **C'est une comparaison, pas une découverte.** Toutes les valeurs attendues ont été
> établies avant la première soudure, et sont dans [`signaux-mgb.md`](signaux-mgb.md) §3bis.
> C'est tout l'intérêt d'avoir mesuré d'abord.

---

## Le verdict, signal par signal

| Voie | Point soudé | Phase 0 (référence) | Phase 1 (après soudure) | |
|---|---|---|---|---|
| **D0** | `P2-LD0` | données, fronts dans les salves | 74 295 fronts/s · **99,4 %** dans les salves | ✅ |
| **D1** | `P2-LD1` | données | 64 710 fronts/s · **99,7 %** dans les salves | ✅ |
| **D2** | **`CP`** | 1,376 M/s · **160**/salve · 143,9 salves · **208 ns** | 1,375 M/s · **160**/salve · **143,7** salves · **208 ns** | ✅ |
| **D3** | **`P2-ST`** | 8 596/s · silence **1,196 ms** · 3,0 % | 8 595/s · silence **1,196 ms** · 3,0 % | ✅ |
| **D4** | **`P2-S`** | **59,71 Hz** · r.cycl. 0,65 % | **59,71 Hz** · r.cycl. 0,65 % | ✅ |
| **D5** | **`P2-CPL`** | 9 196/s · silence **109 µs** · 0,9 % | 9 196/s · silence **109 µs** · 0,9 % | ✅ |
| D6, D7 | *(non connectées)* | — | figées à 1, **0 front** | ✅ |

**Les six signatures sont identiques à la référence**, à la quantification près. L'affectation
des voies correspond exactement au brochage prévu : `D0`→GP0 … `D5`→GP5.

## Qualité du signal

| Contrôle | Mesure | Lecture |
|---|---|---|
| **Fronts parasites** | période minimale de l'horloge pixel = **208 ns**, inchangée | Un rebond produirait des fronts plus rapprochés que la période pixel. Il n'y en a aucun |
| **Diaphonie** | `D6` et `D7` : **0 front** sur 66,7 ms | En phase 0, les voisines de l'horloge pixel dans le faisceau de sondes ramassaient 108 et 149 fronts. **Le câblage soudé est plus propre que le faisceau de sondes** |

> ⚠️ Un analyseur à 24 MS/s ne juge pas la **forme** d'un front, seulement son existence.
> L'absence de fronts parasites est le meilleur contrôle qu'il permette.

---

## Ce que la mesure a appris sur le PPU

En cherchant pourquoi le compte de salves donnait 147,7 au lieu de 144, on est tombé sur un
comportement du PPU qui n'était pas documenté dans ce projet :

```
  555 salves de 160 impulsions     ← les lignes normales
   16 salves de  77
   16 salves de  83                ← 77 + 83 = 160
   16 trous de 3,8 µs              ← au MILIEU d'une ligne
```

**Seize lignes par trame contiennent une pause de 3,8 µs — soit 16 cycles maître — en plein
mode 3.** C'est le PPU qui bloque sa file de pixels pour charger un sprite. Seize lignes
exactement : la hauteur d'un sprite 8×16.

Les vraies fins de ligne, elles, font **≥ 56,9 µs**.

**Correction apportée à l'outil** : le seuil de découpage en salves était un multiple de la
cadence pixel (2,5 µs), donc plus court que ces pauses. Il se cale désormais sur la période
**ligne** (`T_LIGNE / 5` = 21,7 µs), ce qui sépare proprement une pause de sprite d'une fin de
ligne. Après correction : **143,7 salves par trame**, et la capture de la phase 0 donne
toujours 143,9.

> 🔑 **Aucune conséquence sur le firmware.** Le nombre d'impulsions par ligne reste 160 ;
> seule leur répartition dans le temps change. Le PIO attend un front, le DMA compte des mots :
> ni l'un ni l'autre ne s'intéresse à l'espacement. Et le contrôle d'intégrité s'appuie sur
> `P2-ST` (144/trame), pas sur le comptage des impulsions.

---

## Ce qui reste à vérifier — §C.5

La vérification 4 est **passée**. Les autres demandent tes yeux et ton multimètre :

| # | Vérification | État |
|---|---|---|
| 1 | La console joue normalement, module non alimenté | ⬜ |
| 2 | **L'écran d'origine est identique** — photo avant/après, même image, même angle | ⬜ |
| 3 | Tension sur chaque broche du connecteur **≤ 3,2 V** | ⬜ |
| 4 | Les 6 signaux ont les signatures de la phase 0 | ✅ **ci-dessus** |
| 5 | Fronts nets, sans rebond | ✅ dans la limite de l'instrument |
| — | La console **se referme** | ⬜ |

> ⚠️ La vérification 2 est celle qui ne se rattrape pas. Sans la photo d'avant, il n'y a rien
> à comparer — et c'est précisément le contrôle qui dit si la soudure a dégradé le PPU.
