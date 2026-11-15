# Recette de la carte — les Pico comme instruments de mesure

Deux firmwares de contrôle pour la [carte de l'écran](../../materiel/README.md#3-la-carte--carte),
à flasher sur les Pico avant de les enficher. Chaque Pico éprouve son support, puis émet et
mesure des signatures qui vérifient chaque piste de la carte. La marche à suivre, étape par
étape, est dans [`materiel/montage-carte.md`](../../materiel/montage-carte.md).

| Cible | Pour | Support |
|---|---|---|
| `recette_tete.uf2` | le Pico 2 W de la tête | U1 |
| `recette_noeud.uf2` | les trois Pico 2 des nœuds, le même pour les trois | U2, U3, U4 |

Construits pour la carte `pico2`, sans radio : `recette_tete` ne touche jamais GP23 à GP25 ni
GP29, les broches de la puce WiFi du Pico 2 W. Chacun vérifie au démarrage qu'il est dans le
bon support ; sinon il ne pilote rien et le dit.

## Construire

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Pas
de `secrets.h`.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/recette_noeud.uf2
../../tools/console.py
```

## Ce que fait chaque Pico

**Au démarrage, le contrôle des broches**, en 8 passes à intervalles aléatoires. Chaque broche
du support est commandée à 0 puis à 1, à 2 puis 12 mA, relâchée vers le haut puis vers le
bas. Il en sort, pour chaque broche :

- *reliée* à une autre : elle la suit quand on la commande, un pont de soudure ;
- *ne monte pas*, *ne descend pas* : court-circuit, ou sortie du Pico abîmée (celle de GP1,
  le 29/10/2026) ;
- *tirée vers le haut* ou *vers le bas* par une résistance de la carte : c'est ainsi que la
  tête voit R33 à R35 et, pour chaque support de nœud vide, R45 et R41 ; un nœud voit R43
  et R45 ;
- *pilotée* : un autre Pico la fait déjà basculer, elle n'est pas essayée.

Un défaut n'est retenu que s'il revient à trois passes sur quatre : deux Pico qui démarrent
ensemble et font leur contrôle en même temps ne se trompent pas l'un l'autre. Une broche en
défaut n'est jamais pilotée ensuite. Pour refaire le contrôle, appuyer sur le RESET du Pico.

**Ensuite, en permanence, les signatures.** Chaque sortie bat à 10 kHz avec son propre
rapport cyclique, son « code ». Un multimètre en continu en lit la moyenne ; un Pico qui reçoit
la ligne mesure le même code. Un fil coupé ne porte plus de code, deux fils croisés échangent
le leur, un court-circuit les mêle.

| Émetteur | Ligne | Code | Au multimètre |
|---|---|---|---|
| tête | L*k* D0, D1, CLK, CS | 10 + 20 *k* + 5 *s* % : L0 10-25, L1 30-45, L2 50-65 | 0,33 à 2,15 V |
| tête | VSYNC | 80 % | ≈ 2,6 V |
| tête | J3, broche *n* (2 à 7) | | ≈ 0,4 V × *n* |
| tête, nœuds | GP17, GP18 vers J7 | 30 %, 60 % | ≈ 1,0 V, ≈ 2,0 V |
| nœud | nappe HUB75, broche *n* | 6 *n* %, après les 74AHCT541 en 5 V | 0,3 V × *n* sous 5,0 V |
| nœud | G1 de rechange, GP15 | 96 % | 4,8 V sur la broche 2 de la nappe, cavalier en 2-3 |
| nœud | RDY | 20 / 45 / 70 % : liaison 0 / 1 / 2 complète ; 30 / 55 / 80 % : un signal faux ; 90 % : aucune liaison reconnue | |

Le nœud reconnaît sa liaison aux codes qui lui arrivent, donc son support, et le rend à la
tête par son RDY, relu et mis à jour toutes les 0,5 s : la console de la tête résume ainsi
toute la carte. La tête jette sa première mesure, prise pendant que les nœuds démarrent.

**La console**, toutes les 2 s. Tête : contrôle des broches, occupation des supports de nœud,
tension du bus mesurée par R2/R3 sur GP27, tension de J2.2, état de chaque liaison d'après le
RDY de son nœud. Nœud : contrôle des broches, VSYS mesuré sur GP29, les cinq lignes venues de
la tête avec leur code et leur verdict, le support qui s'en déduit.

**La LED d'un nœud** (Pico 2 ; pas sur la tête, dont la LED passe par la radio) : fixe, tout
va bien ; clignotement lent, rien d'anormal mais pas de tête ; clignotement rapide, un défaut
à lire sur la console.

## Sources

| Fichier | Rôle |
|---|---|
| `src/carte.h` | les codes, et la patte du Pico de chaque GPIO |
| `src/banc.h`, `src/banc.c` | le contrôle des broches ; le motif de signatures, cadencé au µs sur le cœur 1, qui mesure aussi les entrées |
| `src/tete.c` | la tête : liaisons, J3, J7, bus, RDY des nœuds |
| `src/noeud.c` | un nœud : nappe HUB75, J7, lecture de la liaison, RDY, LED |

## Tester sur PC

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -Itest/faux -Isrc \
    test/test_controle.c src/banc.c -o /tmp/test_controle && /tmp/test_controle
```

Le contrôle des broches est rejoué contre un modèle électrique de la carte : chaque broche
est un nœud résistif (sortie du Pico, tirages internes, résistances de la carte). Dix cas :
nœud seul, tête seule, pont de soudure, patte à la masse, R43 absente, sortie qui ne descend
plus, liaison pilotée par la tête, tête avec ses trois nœuds en marche, pont vers une broche
tirée, voisin qui fait son contrôle en même temps. Le faux pico-sdk de `test/faux/` ne sert
qu'à compiler `banc.c` tel quel. Le motif et la lecture des codes ne s'éprouvent que sur la
carte.
