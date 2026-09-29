# Tête (v2) — réception, placement, découpe

Firmware de la **tête** du module écran : un Pico 2 W sans dalle, qui reçoit les images par
WiFi, les place dans le canevas de 192 × 192 pixels et les découpe par rangée pour les trois
nœuds d'affichage. Son rôle dans l'architecture est décrit dans le
[README du module](../../README.md#architecture).

**État : phase 5a, firmware écrit, pas encore mesuré sur matériel.** Rien ne sort encore
vers les nœuds : les segments qui leur sont destinés sont comptés, pas émis. Cette étape
mesure le seul chiffre qui pourrait remettre l'architecture en cause : une image 192 × 192 en
IDX8 à 60 images/s reçue par une seule antenne ([§4.4 du plan](../../docs/plan-firmware.md)).

## Construire et utiliser

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Ici,
`secrets.h` est nécessaire et la cible produit `build/tete.uf2`. Au démarrage, la tête
annonce son adresse sur la console, et au serveur DHCP sous le nom **`ecran`**.

## Sources

| Fichier | Rôle |
|---|---|
| `include/config.h` | canevas, rangées, formats acceptés, horloge, et le brochage des liaisons vers les nœuds (utilisé à partir de la phase 5b) |
| `include/decoupe.hpp` | placement et découpe, en C++ portable, testé sur PC |
| `include/reseau.hpp`, `src/net/reseau.cpp` | WiFi, PXL2 et PXL1, réassemblage, réponse aux `PING`, accusés, reconnexion |
| `src/main.cpp` | rapport de recette toutes les 10 s |
| `test/test_decoupe.cpp` | test de la découpe sur PC |
| [`../commun/pxl1.h`](../commun/pxl1.h), [`../commun/pxl2.h`](../commun/pxl2.h) | les en-têtes des protocoles |

## Ce qui change par rapport au nœud v1

La pile réseau vient du [nœud v1](../ecran/README.md) : économie d'énergie WiFi coupée, API
brute de lwIP, fenêtre de resynchronisation de 8 images, même `lwipopts.h`, même horloge de
266 MHz et même réglage `CYW43_PIO_CLOCK_DIV_INT=4`. Les différences :

- **Deux protocoles sur le port 4242**, distingués par leur nombre magique. PXL1 reste
  accepté pour le sniffer du module capture : son `node_id` est ignoré, la taille de l'image
  est apprise par la commande `PXL1_CTRL_GEOMETRIE`, et l'accusé est rendu en PXL1. Tant
  que cette commande n'est pas arrivée (la source la renvoie toutes les 2 s), les tranches
  PXL1 sont comptées en `sans geometrie`.
- **Pas de tampon d'image.** La tête ne garde pas les pixels, elle compte des positions.
  Elle occupe 99 ko de RAM sur 520, lwIP compris : il reste la place des tampons des
  liaisons de la phase 5b.
- **Une image est complète quand tous ses octets sont arrivés, dans n'importe quel ordre.**
  La v1 publiait à l'arrivée de la tranche marquée « dernière », et une tranche retardée
  derrière elle faisait perdre l'image.
- **La première image reçue n'est plus perdue.** En v1, une image numéro 0 arrivant en
  premier passait pour une retardataire.
- **Reconnexion WiFi automatique**, reprise du module capture : l'association est relancée
  chaque seconde, sans bloquer la boucle.

## Tester la découpe sur PC

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude test/test_decoupe.cpp -o /tmp/test_decoupe
/tmp/test_decoupe
```

Le test fabrique 5 000 images aléatoires dans tous les formats et sur cinq canevas, dont des
largeurs impaires. Il les débite en tranches, les découpe, puis rejoue les segments comme le
feraient les nœuds : le canevas reconstitué doit être l'image centrée, au pixel près. Chacune
des quatre erreurs introduites volontairement dans `decoupe.hpp` pour l'éprouver l'a fait
échouer.

Résultats attendus pour les cas nominaux, en tranches de 1 400 octets :

| Source | Tranches | Segments | |
|---|---|---|---|
| 192 × 192 IDX8 | 27 | 29 | lignes jointives : on ne coupe qu'aux frontières de rangée |
| 192 × 192 BGR888 | 80 | 82 | |
| 160 × 144 IDX2, Game Boy | 5 | 144 | un segment par ligne, image placée en (16, 24) |

## Recette de la phase 5a

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

| Critère | Attendu | Mesuré |
|---|---|---|
| IDX8 192 × 192 à 60 images/s | 60 images/s, environ 17,7 Mbit/s | à mesurer |
| Perte sur 10 minutes | moins de 0,1 % | à mesurer |
| Pixels par image et par rangée, 192 × 192 | 12 288 / 12 288 / 12 288 | à mesurer |
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

**Si le débit ne tient pas**, les replis sont IDX8 à 30 images/s ou IDX4 à 60. Ce serait une
limite de la radio ou de lwIP, pas de l'architecture de distribution, mais il faut le savoir
avant de câbler les liaisons.
