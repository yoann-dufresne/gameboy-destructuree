# Module ÉCRAN

Un **afficheur réseau générique** : une grille de dalles LED RGB qui affiche les images
qu'on lui envoie par WiFi. N'importe quelle source peut l'alimenter : un PC qui diffuse une
vidéo, une horloge, une mire, ou une console de jeu.

Dans la [Game Boy Pocket déstructurée](../README.md), c'est l'écran de la console, alimenté
par le module [capture](../capture/README.md).

## Ce qu'il fait

L'écran final assemble **3 × 3 dalles de 64 × 64 pixels, soit 192 × 192 pixels**. Vu de
l'extérieur, il se résume à une adresse IP et à un canevas : la source envoie son image
entière, à sa taille native, et l'écran la place (centrée si elle est plus petite) puis la
répartit sur les dalles. L'image 160 × 144 de la Game Boy s'y affiche à l'échelle 1:1, avec
une marge libre de 16 pixels sur les côtés et de 24 en haut et en bas.

## Architecture

```
                 WiFi (1 IP)          nappes 10 pts                  HUB75
                                ┌── L0 ──► [Pico 2 · nœud 0] ──► ▣▣▣  rangée 0
 Source ──PXL2──► [Pico 2 W] ───┼── L1 ──► [Pico 2 · nœud 1] ──► ▣▣▣  rangée 1
                   « tête »     └── L2 ──► [Pico 2 · nœud 2] ──► ▣▣▣  rangée 2
                                   données + VSYNC ►  ◄ RDY, sur chaque nappe
```

- La **tête**, un Pico 2 W sans dalle, reçoit les images par WiFi, les place dans le canevas
  et envoie à chaque nœud la rangée qui lui revient, par une liaison filaire.
- Chaque **nœud**, un Pico 2, pilote une chaîne de trois dalles. Il signale par `RDY` qu'il
  est prêt, et les trois nœuds basculent ensemble sur le signal `VSYNC` de la tête : les
  rangées changent d'image au même instant, sans déchirure.

Un seul Pico ne suffit pas à piloter les neuf dalles (mémoire, broches, temps de calcul) :
la justification chiffrée est au [§2.2 bis du plan](docs/plan-firmware.md). Cette
architecture, dite v2, date du 29/09/2026. La v1, validée sur une dalle lors des phases 0 à
4, reposait sur des nœuds qui recevaient chacun leur part de l'image par WiFi.

### Vocabulaire

| Terme | Sens |
|---|---|
| **HUB75** | l'interface parallèle standard des dalles LED : 6 lignes de couleur, 5 d'adresse de ligne, une horloge, un verrou et une validation de sortie |
| **PIO** | les petites machines à états du microcontrôleur RP2350, qui produisent les signaux HUB75 sans occuper le processeur |
| **DMA** | l'accès direct à la mémoire : les pixels vont de la RAM au PIO sans passer par le processeur |
| **BCM** | *Binary Code Modulation*, la façon dont la dalle produit des niveaux de luminosité en allumant chaque LED plus ou moins longtemps, bit par bit |
| **PXL2**, **PXL1** | les deux protocoles UDP du projet pour transporter des images ; PXL2 est celui de la v2, PXL1 reste accepté car le module capture l'émet. Spécification au [§4 du plan](docs/plan-firmware.md), en-têtes dans [`firmware/commun/`](firmware/commun/) |
| **BGR888**, **IDX8**, **IDX2** | formats de pixel : 3 octets par pixel ; 1 octet par pixel et une palette de 256 couleurs ; 2 bits par pixel et 4 couleurs, le format natif de la Game Boy |

## Matériel

| Rôle | Référence | Qté | Notes |
|---|---|---|---|
| Dalle | **Seengreat RGB Matrix P3.0-64x64** | 9 | HUB75E, balayage 1/32, 192 × 192 mm, 5 V / 4 A |
| Tête | **Raspberry Pi Pico 2 W** | 1 | le WiFi est indispensable ici |
| Nœuds | Raspberry Pi Pico 2 (W) | 3 | le WiFi n'y sert pas : un Pico 2 simple suffit |
| Alimentation | 5 V / 15 A | 3 | une par rangée, soit 36 A et environ 180 W au total |
| Signal des dalles | nappe IDC 16 pts + embase 2×8 mâle | 3 | du nœud à l'entrée de la première dalle de sa chaîne |
| Liaison | nappe IDC 10 pts | 3 | de la tête à chaque nœud : données, `VSYNC`, `RDY`, 4 masses |

Nomenclature complète et chiffrée : [§7 du plan](docs/plan-firmware.md).

## Construire, flasher, observer

Les firmwares sont écrits en C++20 (C pour la phase 0) sur le **pico-sdk 2.x**, sans système
d'exploitation. Il faut le pico-sdk et sa chaîne de compilation (`arm-none-eabi-gcc`, CMake,
Ninja), ainsi que Python 3 avec `pyserial` pour les outils. Les alternatives écartées
(FreeRTOS, Rust, CircuitPython) sont discutées au [§3 du plan](docs/plan-firmware.md).

La procédure est la même pour tous les dossiers de `firmware/` :

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cd firmware/<dossier>
cp include/secrets.h.example include/secrets.h   # firmwares WiFi seulement,
$EDITOR include/secrets.h                        # puis y mettre le nom et le mot de passe du réseau
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/<cible>.uf2
../../tools/console.py
```

- `secrets.h` est ignoré par git : les identifiants WiFi ne sont jamais versionnés.
- [`tools/flash.sh`](tools/flash.sh) bascule le Pico en mode de flashage (BOOTSEL) sans le
  débrancher, attend que son volume USB apparaisse et y copie le `.uf2`. Si le Pico ne répond
  plus, maintenir le bouton BOOTSEL enfoncé en le branchant.
- [`tools/console.py`](tools/console.py) affiche la console USB du Pico. `cat /dev/ttyACM0`
  ne suffit pas : le firmware n'écrit que si la ligne DTR est activée, ce que `cat` ne fait
  pas. La console du Pico passe par l'USB parce que GP0 et GP1, les broches de l'UART par
  défaut, sont prises par les signaux de la dalle ou de la liaison.

## Contenu

| Dossier | Rôle |
|---|---|
| [`firmware/phase0-bringup/`](firmware/phase0-bringup/README.md) | mires de validation du câblage d'une dalle |
| [`firmware/phase1-clock-sweep/`](firmware/phase1-clock-sweep/README.md) | banc de mesure de l'horloge pixel, et diagnostic de l'écran noir |
| [`firmware/ecran/`](firmware/ecran/README.md) | v1 : nœud WiFi autonome qui pilote une dalle, en PXL1 |
| [`firmware/tete/`](firmware/tete/README.md) | v2 : la tête — réception PXL2 et PXL1, placement, découpe par rangée |
| [`firmware/commun/`](firmware/commun/) | `pxl1.h` et `pxl2.h`, les en-têtes des protocoles, qui font foi pour tout le projet |
| [`firmware/vendor/hub75-jupfu/`](firmware/vendor/hub75-jupfu/PROVENANCE.txt) | pilote HUB75 de JuPfu (licence MIT), repris avec une modification signalée |
| [`tools/pixelpush/`](tools/pixelpush/README.md) | émetteur pour PC : mires, images, GIF, vidéo, capture d'écran |
| `tools/` | `flash.sh` et `console.py`, décrits ci-dessus |

## Documentation

- [`docs/plan-firmware.md`](docs/plan-firmware.md) : le plan de réalisation. Décisions
  d'architecture et leur justification chiffrée, brochage, protocoles, phases, nomenclature,
  journal des décisions.
- [`docs/cablage-pico-hub75.html`](docs/cablage-pico-hub75.html) : la fiche de câblage d'un
  Pico à une dalle, fil par fil, avec les couleurs de la nappe fournie.
- [`docs/seengreat-rgb-matrix-p3-64x64/`](docs/seengreat-rgb-matrix-p3-64x64/README.md) :
  l'archive hors ligne de la documentation constructeur des dalles, avec une synthèse en
  français.

## État

| Phase | État |
|---|---|
| 0 · Une dalle s'allume, câblage validé | ✅ 16/09/2026 |
| 1 · Pilote d'affichage | ✅ 18/09/2026 — rafraîchissement de 788 Hz, cœur 0 libre |
| 2 · Protocole et réception | ✅ 18/09/2026 — 60 images/s en BGR888 |
| 3 · Émetteur PC | ✅ 18/09/2026 — 7 sources, injection de défauts |
| 4 · Mesure | ✅ 18/09/2026 — latence d'environ 8 ms, 24,6 Mbit/s reçus par un Pico |
| Révision v2 : une tête et trois nœuds | ✅ 29/09/2026 — [§2.2 bis du plan](docs/plan-firmware.md) |
| 5a · La tête seule | ✅ 29/09/2026 — IDX8 192 × 192 à 60 images/s sur une antenne, 0,003 % de perte |
| 5b · Une liaison, un nœud | à venir |
| 5c · Passage à 3 × 3 | à venir |
