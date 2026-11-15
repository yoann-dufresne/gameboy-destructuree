# Nœud (v2) — une rangée de l'écran

Firmware d'un **nœud d'affichage** du module écran : il reçoit sa rangée de la
[tête](../tete/README.md) par la liaison filaire et la publie sur sa chaîne de dalles au
signal VSYNC, en même temps que les autres nœuds. Pas de WiFi, pas d'identité propre : son
numéro de rangée lui vient du port de la tête où sa nappe est branchée. Son rôle dans
l'architecture est décrit dans le [README du module](../../README.md#architecture).

**État :**
- **phase 5b, éprouvé au banc le 29/09/2026** : une dalle, liaison à 16 MHz sur fils volants,
  0 erreur de CRC en 10 minutes, animation fluide ;
- **rangées de 3 dalles le 26/10/2026** : rangées du haut et du milieu, 59,7 images/s depuis la
  Game Boy, horloge pixel ramenée à 19,7 MHz sur la chaîne ; robuste aux débranchements (mode
  secours, réception qui se rattrape seule).

Détail : [`JOURNAL.md`](JOURNAL.md).

## Construire et utiliser

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Pas
de `secrets.h` ici ; la cible produit `build/noeud.uf2`. La carte est déclarée `pico2` : le
même binaire tourne sur un Pico 2 et sur un Pico 2 W, dont la puce radio reste éteinte.

Trois options CMake :

| Option | Effet |
|---|---|
| `-DCHAIN_LEN=3` | dalles de la chaîne : 1 par défaut (banc de la phase 5b), 3 pour une rangée de l'écran |
| `-DHUB75_CLKDIV=1.5` | diviseur de l'horloge pixel : 1 = 29,6 MHz, 1.25 = 23,7, 1.5 = 19,7, 2 = 14,8. Par défaut 1 pour une dalle, **1.5 dès que la chaîne en compte plusieurs** : sur 3 dalles, l'image se dégrade à 26,9 MHz (mesure du 26/10/2026) |
| `-DMIRE_COLONNES=ON` | diagnostic : la mire d'attente devient quatre bandes de 16 colonnes par dalle (rouge, vert, bleu, blanc) ; de quoi vérifier l'ordre des dalles, des colonnes et des couleurs |

Une rangée de l'écran : `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
-DCHAIN_LEN=3`. Les trois nœuds reçoivent le même binaire.

Au démarrage, la dalle affiche une mire « pas de liaison » (cadre bleu sombre et diagonale).
Elle revient si la tête se tait plus de 2,5 s. Sur `EFFACER`, la dalle affiche du **noir** :
la tête l'envoie quand sa source se tait (Game Boy éteinte) et à chaque changement de
géométrie. Le noir veut dire que la liaison est vivante mais qu'il n'y a rien à montrer ; la
mire, qu'il n'y a plus de liaison.

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

Le tableau vaut pour le nœud 0 ; les nœuds 1 et 2 ont **le même brochage côté nœud**, seules
les broches de la tête changent (L1 : GP4–GP7 et RDY sur GP14 ; L2 : GP8–GP11 et RDY sur
GP15, voir le [README de la tête](../tete/README.md#brochage)). VSYNC est commun : **GP12,
broche 16** de la tête, vers **GP16, broche 21** de chaque nœud. Deux « 16 » qui ne désignent
pas la même chose : le piège a coûté un fil délogé le 26/10/2026.

**G1 de rechange : GP15, broche 20.** Le pilote recopie en permanence G1 (le vert de la moitié
haute) sur GP15, en plus de GP1. Sur un Pico dont la sortie GP1 est morte, on déplace **un
seul fil**, celui du trou 2 de la nappe, de la broche 2 à la broche 20 ; le firmware ne change
pas. GP15 est la seule broche libre du côté des fils de la dalle : GP14 et GP16 sont RDY et
VSYNC de la liaison, GP17–GP22 la mesure et la liaison. Les autres sorties de la dalle n'ont
pas de rechange : les couleurs doivent rester sur 6 broches contiguës, et le PIO ne sait
recopier qu'une ligne à part.

Au démarrage, avant de confier les broches au pilote, le nœud commande GP0–GP13 à 1 puis à 0
et vérifie qu'elles suivent. Une sortie qui ne suit pas est signalée dans la bannière et dans
chaque rapport ; pour GP1, avec le fil à déplacer. Si plus de deux broches échouent à la fois,
le contrôle se déclare non concluant : c'est alors la dalle, hors tension, qui tire toutes ses
entrées vers 0.

Côté dalle, sur la nappe : `E` est au **trou 8**, avant `A` (trou 9). Brancher les trous 8 à 12
« dans l'ordre » sur GP6–GP10 mélange les lignes de chaque moitié de dalle, sans que le cadre
de la mire le montre ; la diagonale, elle, devient trois segments pointillés.

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
réception repart d'un début de message, en attendant que CS remonte puis redescende. De même
pour un message entamé qui reste incomplet plus de 20 ms (`MESSAGE_INACHEVE_US`) : la tête
envoie chaque message d'un trait, et une tête redémarrée en plein message laissait sinon la
réception attendre la suite d'un faux en-tête, plus d'une minute quand seuls les HELLO
circulent.

**Le mode secours.** Après 8 images validées sans un seul VSYNC (`VALIDATIONS_AVANT_SECOURS`),
fil VSYNC débranché par exemple, le nœud publie lui-même chaque image validée et tient RDY
haut pour ne pas retenir les autres rangées. Sa rangée vit, simplement plus synchrone avec les
autres. Le premier VSYNC le ramène au fonctionnement normal ; la console signale l'entrée, la
sortie, et le nombre d'images publiées sans VSYNC. Une image validée dont le tampon reçoit une
autre image (la tête l'a abandonnée et a repris le tampon) n'est plus publiable : le mode
secours ne montre jamais un tampon à moitié réécrit.

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
./pixelpush.py --cible 192.168.4.1 --sonder               # canevas 64 × 64
./pixelpush.py --cible 192.168.4.1 --format idx8 --duree 600
```

Critère de sortie ([§5 du plan](../../docs/plan-firmware.md)) : latence au plus 9 ms entre
GP18 de la tête (premier octet WiFi) et GP17 du nœud (publication), et aucune erreur de CRC
sur 10 minutes à 16 MHz, nappe de 50 cm. Résultats et enquêtes : [`JOURNAL.md`](JOURNAL.md).
