# Phase 1 — pilote d'affichage

Firmware du module ÉCRAN. Expose une façade minimale — *un tampon arrière, on le
remplit, on le publie* — au-dessus du pilote HUB75 vendorisé.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/ecran.uf2
```

## Découpage

| | |
|---|---|
| `include/config.h` | **tout** ce qui change entre la phase 1 et la phase 5 : brochage, géométrie, rendu. Aucune constante matérielle ailleurs |
| `include/display.hpp` | la façade : `init()`, `backbuffer()`, `present()`, `node_id()` |
| `src/display.cpp` | enveloppe le pilote vendorisé, possède le cœur 1 |
| `src/main.cpp` | recette de phase 1 |

**Cœur 0** remplit le tampon arrière puis publie. **Cœur 1** possède le pilote :
création, interruptions, conversion en plans de bits. Le flux vers la dalle est en
PIO + DMA — le CPU n'y participe pas.

**Double tampon strict**, jamais trois : chaque tampon supplémentaire est une trame
de latence de plus. `present()` attend l'acquittement du cœur 1 plutôt que d'écraser,
ce qui garantit qu'on n'écrit jamais dans un tampon en cours de lecture.

## Deux pièges du pilote amont, consignés dans le code

1. **`setBasisBrightness()` est obligatoire après `start()` sur le cœur 1.** Sans cet
   appel, les commandes de ligne restent à zéro : adresse figée à 0, `lit_cycles` à 0,
   panneau noir — **alors que le compteur de trames tourne normalement**. Mesuré, voir
   [`../phase1-clock-sweep/DIAGNOSTIC.md`](../phase1-clock-sweep/DIAGNOSTIC.md).
2. **`chain_rows` / `chain_cols` : le README amont contredit son propre code.** Le code
   fait `DISPLAY_WIDTH = matrix_panel_width * chain_cols`, donc c'est `chain_cols` qui
   compte les dalles côte à côte. Sans effet à `CHAIN_LEN = 1`, déterminant en phase 5.

## Recette — résultats du 18/09/2026

Une dalle 64×64, `clk_sys` 266 MHz (horloge pixel 29,6 MHz), 10 plans BCM,
canaux CIE séparés, luminosité de base 6.

| Critère du plan §5 | Mesure | |
|---|---|---|
| Mire fixe affichée | sonde : `AFFICHE`, adresses actives 96 % | ✅ |
| Rafraîchissement ≥ 150 Hz | **788 Hz** | ✅ 5× la cible |
| Cœur 0 saturé ne dégrade pas | **788 Hz, min = max**, au repos comme sous charge | ✅ |
| Damier 1 px sans ghosting | visuel — ne se mesure pas depuis le firmware | ⬜ |

Le rafraîchissement est **rigoureusement constant** entre les trois phases de la
recette : au repos, cœur 0 saturé par du calcul continu, et publication à 60 Hz. C'est
la démonstration objective que l'affichage est autonome.

> La mesure vient du compteur de trames du pilote, adossé à la fin de transfert DMA —
> pas d'un oscilloscope sur `/OE` comme l'envisageait le plan. C'est la même grandeur,
> relevée en interne.

## Empreinte

114 ko de RAM sur 520, 47 ko de flash, pour une dalle en 10 plans.
À `CHAIN_LEN = 3` les plans de bits triplent : prévoir ~370 ko. Ça passe, sans marge
confortable — le passage à 8 plans est le repli si nécessaire.
