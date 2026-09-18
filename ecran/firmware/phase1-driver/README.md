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

**Un seul tampon de notre côté**, et c'est délibéré : le pilote tient déjà le sien,
basculé en fin de trame, donc les mises à jour sont sans déchirure. `present()` est
synchrone — au retour, le tampon est libre. Le contenu persiste d'une publication à
l'autre : republier sans redessiner réaffiche la même image.

> Une première version ajoutait un double tampon par-dessus. Il n'apportait rien et
> était un piège : `present()` basculait vers un tampon que l'appelant n'avait pas
> rempli, si bien qu'afficher deux fois la même image alternait **image / noir** —
> scintillement à 60 Hz. Corrigé le 18/09/2026.

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
| Absence de scintillement | visuel — `/OE` n'en voit rien, c'est du contenu | ⬜ |

Le rafraîchissement est **rigoureusement constant** entre les trois phases de la
recette : au repos, cœur 0 saturé par du calcul continu, et publication à 60 Hz. C'est
la démonstration objective que l'affichage est autonome.

> La mesure vient du compteur de trames du pilote, adossé à la fin de transfert DMA —
> pas d'un oscilloscope sur `/OE` comme l'envisageait le plan. C'est la même grandeur,
> relevée en interne.

## Le bon instrument pour un écran noir ou clignotant

Deux mesures ont servi, et l'une est trompeuse :

- **`/OE`** pilote l'activation globale de la dalle, **pas le contenu**. Une trame
  entièrement noire a exactement le même rapport cyclique qu'une trame pleine. Mesurer
  `/OE` ne dit donc rien d'un problème d'image — vérifié : 58,6 % contre 58,5 % entre
  un régime sain et un régime qui clignotait visiblement.
- **Les lignes de données R1..B2** portent le contenu : c'est là qu'il faut regarder.
- **Les lignes d'adresse A..E** distinguent « le pilote ne balaie pas » de « il balaie
  du noir ».

Et le compteur de trames du pilote ne prouve rien : il a annoncé 788 Hz parfaitement
stables pendant que l'écran était noir, puis pendant qu'il clignotait.

## Empreinte

114 ko de RAM sur 520, 47 ko de flash, pour une dalle en 10 plans.
À `CHAIN_LEN = 3` les plans de bits triplent : prévoir ~370 ko. Ça passe, sans marge
confortable — le passage à 8 plans est le repli si nécessaire.
