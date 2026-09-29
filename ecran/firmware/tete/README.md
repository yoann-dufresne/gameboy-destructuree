# Tête (v2) — réception, découpe, relais, synchronisation

Firmware de la **tête** du module écran : un Pico 2 W sans dalle, qui reçoit les images par
WiFi, les place dans le canevas de 192 × 192 pixels, les découpe par rangée et relaie chaque
morceau à son [nœud d'affichage](../noeud/README.md) par une liaison filaire. Elle tient la
synchronisation : les nœuds publient ensemble, sur son signal VSYNC. Son rôle dans
l'architecture est décrit dans le [README du module](../../README.md#architecture).

**État :**
- **phase 5a atteinte le 29/09/2026** : une seule antenne reçoit une image 192 × 192 en IDX8 à
  60 images/s, 0,003 % de perte sur 10 minutes ([§4.4 du plan](../../docs/plan-firmware.md)) ;
- **phase 5b en cours** : liaisons et synchronisation écrites, éprouvées sans nœud (60 images
  reçues, 60 publiées par seconde) ; reste l'essai avec un nœud et sa dalle.

## Construire et utiliser

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Ici,
`secrets.h` est nécessaire et la cible produit `build/tete.uf2`. Au démarrage, la tête
annonce son adresse sur la console, et au serveur DHCP sous le nom **`ecran`**.

Deux options CMake, pour le banc :

| Option | Effet |
|---|---|
| `-DBANC_UNE_DALLE=ON` | canevas réduit à une dalle (64 × 64) et une seule liaison : l'image entière est visible sur le banc de la phase 5b |
| `-DLIEN_HORLOGE_KHZ=1000` | horloge de la liaison, 16 000 kHz par défaut. La baisser pour une première mise en route. Une liaison transporte 2 bits par coup d'horloge et doit suivre le débit de sa rangée : 6 Mbit/s en IDX8 192 × 192 à 60 images/s, 2 Mbit/s pour une image 64 × 64 en IDX8 au banc |

Sans nœud branché, les entrées RDY sont tirées haut : la tête tourne seule et publie chaque
image, comme en phase 5a.

## Sources

| Fichier | Rôle |
|---|---|
| `include/config.h` | canevas, rangées, formats acceptés, brochage et horloge des liaisons, garde RDY |
| `include/decoupe.hpp` | placement et découpe, en C++ portable, testé sur PC |
| `include/reseau.hpp`, `src/net/reseau.cpp` | WiFi, PXL2 et PXL1, réassemblage, relais des segments, tampons des nœuds, accusés, reconnexion |
| `include/lien.hpp`, `src/lien.cpp`, `src/lien.pio` | les liaisons : un anneau, un DMA et une machine PIO par nœud ; VSYNC, RDY |
| `src/main.cpp` | cœur 1 : la synchronisation ; cœur 0 : l'entretien et le rapport toutes les 10 s |
| `test/test_decoupe.cpp` | test de la découpe sur PC |
| [`../commun/pxl1.h`](../commun/pxl1.h), [`../commun/pxl2.h`](../commun/pxl2.h), [`../commun/liaison.h`](../commun/liaison.h) | les protocoles : les sources, et la liaison vers les nœuds |

## Brochage

| GPIO | Broche du Pico | Rôle |
|---|---|---|
| GP0–GP3 | 1, 2, 4, 5 | liaison L0 : D0, D1, CLK, CS |
| GP4–GP7 | 6, 7, 9, 10 | liaison L1 |
| GP8–GP11 | 11, 12, 14, 15 | liaison L2 |
| GP12 | 16 | VSYNC vers les trois nœuds, actif bas |
| GP13–GP15 | 17, 19, 20 | RDY0 à RDY2, entrées en pull-up |
| GP17, GP18 | 22, 24 | mesure : image complète, premier octet d'une image |

Une résistance de 33 Ω en série sur chaque sortie de liaison et sur VSYNC (plan §2.6).

## Ce qui change par rapport au nœud v1

La pile réseau vient du [nœud v1](../ecran/README.md) : économie d'énergie WiFi coupée, API
brute de lwIP, fenêtre de resynchronisation de 8 images, même `lwipopts.h`, même horloge de
266 MHz et même réglage `CYW43_PIO_CLOCK_DIV_INT=4`. Les différences :

- **Deux protocoles sur le port 4242**, distingués par leur nombre magique. PXL1 reste
  accepté pour le sniffer du module capture : son `node_id` est ignoré, la taille de l'image
  est apprise par la commande `PXL1_CTRL_GEOMETRIE`, et l'accusé est rendu en PXL1. Tant
  que cette commande n'est pas arrivée (la source la renvoie toutes les 2 s), les tranches
  PXL1 sont comptées en `sans geometrie`.
- **Pas de tampon d'image.** La tête ne garde pas les pixels : chaque segment part vers son
  nœud dès que sa tranche arrive. 200 ko de RAM sur 520, dont 96 ko d'anneaux de liaison.
- **Une image est complète quand toutes ses tranches sont arrivées, dans n'importe quel
  ordre**, et les doublons que livre le WiFi sont écartés, reconnus à leur offset.
- **La première image reçue n'est plus perdue.** En v1, une image numéro 0 arrivant en
  premier passait pour une retardataire.
- **Reconnexion WiFi automatique**, reprise du module capture.

## La synchronisation, sur le cœur 1

Une image complète est validée auprès de chaque nœud (message `VALIDER`, avec le nombre de
pixels qu'il doit avoir reçus). Quand ce message a quitté la tête et que tous les RDY sont
hauts, la tête impulse VSYNC : les nœuds publient ensemble. Un nœud qui n'a pas levé RDY au
bout de 40 ms fait abandonner l'image à tous.

Cette boucle a son propre cœur, et ne prend jamais le verrou de lwIP : l'état des tampons et
l'ajout d'un message dans un anneau ont chacun un verrou matériel, tenu quelques
microsecondes. Seul l'accusé à l'émetteur passe par lwIP ; il part du cœur 0. La raison est
mesurée, voir [`JOURNAL.md`](JOURNAL.md) : sous charge, la réception garde le verrou de lwIP
jusqu'à 140 ms.

## Tester la découpe sur PC

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude test/test_decoupe.cpp -o /tmp/test_decoupe
/tmp/test_decoupe
```

Le test fabrique 5 000 images aléatoires dans tous les formats et sur cinq canevas, dont des
largeurs impaires. Il les débite en tranches, les découpe, puis rejoue les segments comme le
feraient les nœuds : le canevas reconstitué doit être l'image centrée, au pixel près. Chacune
des quatre erreurs introduites volontairement dans `decoupe.hpp` pour l'éprouver l'a fait
échouer. La chaîne complète, jusqu'aux tampons des nœuds, est éprouvée par le
[test du nœud](../noeud/README.md#tester-sur-pc).

## Recettes

Les images viennent de [`pixelpush`](../../tools/pixelpush/README.md), avec `--cible` :

```bash
cd ../../tools/pixelpush
./pixelpush.py --cible <ip> --sonder                          # l'écran se décrit
./pixelpush.py --cible <ip> --format idx8 --duree 600         # le critère principal
./pixelpush.py --cible <ip> --taille 160x144 --format idx2    # la Game Boy, simulée
./pixelpush.py --cible <ip> --format bgr888 --fps 25          # le plafond en BGR888
```

Puis le sniffer lui-même, sans modification : il suffit de pointer sa cible (`PXL1_CIBLE_IP`)
sur l'adresse de la tête.

La console rend compte toutes les 10 s : images reçues et publiées, débit, perte, doublons,
synchronisation (abandons, images supplantées, attente des RDY), débit de chaque liaison et
état de son RDY, pixels par image et par rangée. Résultats et enquêtes :
[`JOURNAL.md`](JOURNAL.md).
