# Nœud (v2) — une rangée de l'écran

Firmware d'un **nœud d'affichage** du module écran : il reçoit sa rangée de la
[tête](../tete/README.md) par la liaison filaire et la publie sur sa chaîne de dalles au
signal VSYNC, en même temps que les autres nœuds. Pas de WiFi, pas d'identité propre : son
numéro de rangée lui vient du port de la tête où sa nappe est branchée. Son rôle dans
l'architecture est décrit dans le [README du module](../../README.md#architecture).

**État : phase 5b, firmware écrit, pas encore éprouvé sur matériel.**

## Construire et utiliser

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Pas
de `secrets.h` ici ; la cible produit `build/noeud.uf2`. La carte est déclarée `pico2` : le
même binaire tourne sur un Pico 2 et sur un Pico 2 W, dont la puce radio reste éteinte.

Au démarrage, la dalle affiche une mire « pas de liaison » (cadre bleu sombre et diagonale).
Elle revient si la tête se tait plus de 2,5 s.

## Brochage

La dalle garde le câblage HUB75 du nœud v1, GP0 à GP13. La liaison vers la tête :

| Signal | GPIO du nœud | Broche du Pico | GPIO de la tête (liaison L0) |
|---|---|---|---|
| D0 | GP19 | 25 | GP0 |
| D1 | GP20 | 26 | GP1 |
| CLK | GP21 | 27 | GP2 |
| CS | GP22 | 29 | GP3 |
| VSYNC (entrée, active basse) | GP16 | 21 | GP12 |
| RDY (sortie) | GP14 | 19 | GP13 |
| masse | GND | 18, 23, 28 | GND |

GP14 était un strap d'identité en v1 : s'il est relié à la masse, retirer le strap. GP17 bascule
à chaque publication et GP18 à chaque `VALIDER` reçu, pour l'analyseur logique.

## Sources

| Fichier | Rôle |
|---|---|
| `include/config.h` | brochage, géométrie de la chaîne (`CHAIN_LEN`), rendu |
| `include/rangee.hpp` | reconstruction de la rangée à partir des messages, validation — C++ portable, testé sur PC |
| `include/reception.hpp`, `src/reception.cpp`, `src/lien_rx.pio` | réception de la liaison : PIO, DMA sans fin, anneau de 32 ko |
| `src/main.cpp` | la boucle : VSYNC, RDY, messages, mire d'attente, rapport toutes les 10 s |
| [`../commun/liaison.h`](../commun/liaison.h) | le format des messages de la liaison, et leur CRC |
| [`../commun/affichage/`](../commun/affichage/) | la façade d'affichage, partagée avec le nœud v1 |

## Fonctionnement

**Deux tampons de réception.** La tête écrit une image dans l'un pendant que l'autre attend son
VSYNC ; c'est elle qui choisit le tampon, et elle ne touche jamais à celui qu'elle a validé
tant que le VSYNC n'est pas passé.

**La validation.** Quand l'image est complète, la tête envoie `VALIDER` avec le nombre de
pixels qu'elle a envoyés à ce nœud. Le nœud compare avec ce qu'il a reçu : un message perdu en
route se voit ici, même sans erreur de CRC. Validation acceptée et pilote libre : RDY monte.

**Le VSYNC.** Sur son front descendant, une interruption abaisse aussitôt RDY, puis la boucle
publie le tampon validé. La boucle traite le VSYNC avant tout nouveau message : juste après
l'impulsion, la tête peut reprendre le tampon qu'elle vient de faire publier.

**Les erreurs.** Un en-tête impossible ou un CRC faux signale une perte d'alignement : la
réception repart d'un début de message, en attendant que CS remonte puis redescende.

**Les positions** sont exprimées dans la largeur du canevas annoncée par `HELLO`. Le nœud ne
garde que ce qui tombe sur sa dalle : au banc, une dalle de 64 px sous un canevas de 192 montre
le tiers gauche de l'image.

## Tester sur PC

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude -I../commun -I../tete/include \
    test/test_rangee.cpp -o /tmp/test_rangee
/tmp/test_rangee
```

Le test rejoue la chaîne complète sans carte ni nappe. Côté tête, la découpe réelle et
l'encodage des messages ; les mots passent par un anneau qui boucle, lu par le même lecteur
que le nœud ; côté nœud, la vraie reconstruction. À chaque `VALIDER`, le tampon validé doit
être la rangée attendue, au pixel près, sur trois bancs : l'écran 3 × 3, une dalle seule, et
trois nœuds de 64 px sous un canevas de 192. Un bit abîmé en route doit être vu par le CRC et
faire refuser l'image. Quatre erreurs introduites volontairement dans `rangee.hpp` le font
échouer.

## Recette de la phase 5b

Au banc, une tête en mode une dalle (`-DBANC_UNE_DALLE=ON`), une liaison vers ce nœud, la dalle
déjà câblée :

```bash
cd ../../tools/pixelpush
./pixelpush.py --cible <ip-de-la-tete> --sonder               # canevas 64 × 64
./pixelpush.py --cible <ip-de-la-tete> --format idx8 --duree 600
```

Critère de sortie ([§5 du plan](../../docs/plan-firmware.md)) : latence au plus 9 ms entre
GP18 de la tête (premier octet WiFi) et GP17 du nœud (publication), et aucune erreur de CRC
sur 10 minutes à 16 MHz, nappe de 50 cm. Les résultats iront dans un `JOURNAL.md` voisin.
