# Tête — phase 5a : réception, placement, découpe

Firmware de la **tête** du module ÉCRAN (plan §2.2 bis). Un Pico 2 W sans dalle : il
reçoit les images par WiFi, les place dans le canevas 192×192, et les découpe par
rangée pour les trois nœuds d'affichage.

En phase 5a, **rien ne sort encore** : les segments destinés aux nœuds sont comptés, pas
émis. C'est le banc du seul chiffre qui peut remettre l'architecture en cause : une
image 192×192 en IDX8 à 60 img/s sur une seule antenne (plan §4.4).

```bash
cp include/secrets.h.example include/secrets.h && $EDITOR include/secrets.h
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/tete.uf2
../../tools/console.py
```

La tête annonce son adresse sur la console, et au DHCP sous le nom **`ecran`**.

## Découpage

| | |
|---|---|
| `include/config.h` | canevas, rangées, formats acceptés, brochage des liaisons (5b), horloge |
| `include/decoupe.hpp` | placement et découpe — C++ portable, **testé sur PC** |
| `include/reseau.hpp`, `src/net/reseau.cpp` | WiFi, PXL2 + PXL1, réassemblage, `PONG`, accusés, reconnexion |
| `src/main.cpp` | recette de la phase 5a : rapport toutes les 10 s |
| `../commun/pxl1.h`, `../commun/pxl2.h` | les protocoles — font foi pour tout le projet |

## Ce qui vient de la v1, et ce qui change

Repris de `../ecran/src/net/reseau.cpp` : économie d'énergie WiFi coupée, API raw de
lwIP, fenêtre de resynchronisation de 8 images, `lwipopts.h` tel quel, 266 MHz et
`CYW43_PIO_CLOCK_DIV_INT=4`.

Ce qui change :

- **Deux protocoles sur le port 4242**, distingués par le magic. `PXL1` est accepté pour
  le sniffer : `node_id` ignoré, géométrie apprise par `PXL1_CTRL_GEOMETRIE`, accusé
  rendu en `PXL1`. Tant que la géométrie n'est pas arrivée (elle est renvoyée toutes les
  2 s), les tranches `PXL1` sont comptées en `sans geometrie`.
- **Pas de tampon d'image.** La tête ne garde pas les pixels ; elle compte des
  positions. 99 ko de RAM sur 520, lwIP compris : la place pour les anneaux des
  liaisons en 5b est là.
- **Une image est complète quand tous ses octets sont arrivés, dans n'importe quel
  ordre.** La v1 publiait à la tranche marquée « dernière » : une tranche retardée
  derrière elle faisait perdre l'image. Ici elle la complète. Conséquence pour
  `pixelpush --desordre` : les tranches retardées ne sont plus des pertes.
- **La première image reçue n'est plus perdue.** En v1, une image numéro 0 arrivant en
  premier passait pour une retardataire.
- **Reconnexion WiFi automatique**, reprise du module capture : l'association est
  relancée chaque seconde, sans bloquer la boucle.

## Tester la découpe sur PC

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude test/test_decoupe.cpp -o /tmp/test_decoupe
/tmp/test_decoupe
```

Le test fabrique des images aléatoires dans tous les formats, les débite en tranches,
les découpe, puis rejoue les segments comme le feraient les nœuds. Le canevas
reconstitué doit être l'image, centrée, au pixel près. 5 000 images sur cinq canevas,
dont des largeurs impaires pour le bourrage de fin de ligne. Il a été vérifié par
mutation : chacune des quatre erreurs introduites volontairement dans `decoupe.hpp` le
fait échouer.

Résultats attendus pour les cas nominaux, en tranches de 1 400 octets :

| Source | Tranches | Segments | |
|---|---|---|---|
| 192×192 IDX8 | 27 | 29 | lignes jointives : on ne coupe qu'aux frontières de rangée |
| 192×192 BGR888 | 80 | 82 | |
| 160×144 IDX2, Game Boy | 5 | 144 | un segment par ligne, placée en (16, 24) |

## Recette de la phase 5a

```bash
cd ../../tools/pixelpush
./pixelpush.py --cible <ip> --sonder                          # l'écran se décrit
./pixelpush.py --cible <ip> --format idx8 --duree 600         # LE critère
./pixelpush.py --cible <ip> --taille 160x144 --format idx2    # la Game Boy, simulée
./pixelpush.py --cible <ip> --format bgr888 --fps 25          # le plafond BGR888
```

Et le sniffer lui-même, sans le modifier : pointer `PXL1_CIBLE_IP` sur la tête.

| Critère | Attendu | Mesuré |
|---|---|---|
| IDX8 192×192 à 60 img/s | 60 img/s, ≈ 17,7 Mbit/s | à mesurer |
| Perte sur 10 minutes | < 0,1 % | à mesurer |
| Pixels par image et par rangée, 192×192 | 12 288 / 12 288 / 12 288 | à mesurer |
| Pixels par image et par rangée, Game Boy | 6 400 / 10 240 / 6 400 | à mesurer |
| Sniffer en PXL1, sans modification | images comptées, accusés reçus | à mesurer |

Le rapport de la console, toutes les 10 s :

```
  600 images (60,00/s)  17,68 Mbit/s  perte 0,00 %   paquets 16230 (PXL1 0)
    rejets 0  hors canevas 0  format refuse 0  sans geometrie 0  retard. 0  resync 0  ctrl 5  ping 0
    assemblage (us)   moy  ....  min  ....  max  ....
    pixels par image et par rangee : 12288/ 12288/ 12288   segments par image 29,00
    source : 192x192 IDX8 en PXL2
```

**Si le débit ne tient pas** : IDX8 à 30 img/s, ou IDX4 à 60. Ce serait une limite de
l'air ou de lwIP, pas de l'architecture de distribution — mais c'est à savoir avant de
câbler les liaisons.
