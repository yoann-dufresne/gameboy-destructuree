# Capture — sniffer vidéo de Game Boy Pocket

Ce module lit l'image d'une Game Boy Pocket directement sur les fils qui relient son
processeur à son écran, et la diffuse en WiFi. Un Raspberry Pi Pico 2 W, soudé à
l'intérieur de la console, reconstitue chaque image de 160×144 pixels, 59,73 fois par
seconde, et l'envoie en UDP à l'adresse de votre choix. La console continue de
fonctionner normalement, écran d'origine compris : le Pico écoute, il ne pilote rien.

Pour voir le résultat, un récepteur de référence tourne sur n'importe quel PC :
[`tools/ecran_virtuel.py`](tools/ecran_virtuel.py) affiche le flux dans une fenêtre et
mesure sa régularité.

Dans la [Game Boy Pocket déstructurée](../README.md), ce module est la source d'image
du [module écran](../ecran/README.md).

## Principe

La Game Boy envoie ses pixels à l'écran deux bits à la fois, au rythme d'une horloge
pixel : quatre niveaux de gris. Le Pico lit ces deux fils à chaque coup d'horloge grâce
à son **PIO** (*programmable I/O* : de petits automates qui lisent et écrivent des
broches sans le processeur). Le **DMA** (*direct memory access*) range les échantillons
en mémoire, lui aussi sans le processeur.

L'image est émise telle quelle, au format **`IDX2`** : 2 bits par pixel, 4 pixels par
octet, et une palette de 4 couleurs envoyée à part, qui décide des teintes à
l'affichage. Aucune conversion ni mise à l'échelle : une image fait 5 760 octets et part
en 5 paquets UDP. Le transport suit le protocole **`PXL1`**, défini par le module
écran ([`pxl1.h`](../ecran/firmware/commun/pxl1.h)) ; ce module en tient une copie.

Un pixel met en moyenne 4,1 ms pour aller de la console au PC.

## Matériel

| Rôle | Pièce |
|---|---|
| Contrôleur | Raspberry Pi Pico 2 W |
| Console | Game Boy Pocket (MGB-001) ; elle sera ouverte et modifiée |
| Prise de signaux | 6 fils fins soudés sur des points de test de la carte mère, une résistance de 100 Ω en série sur chacun |
| Liaison | connecteur JST-SH 1,0 mm, 8 points (6 signaux, 2 masses), pour pouvoir débrancher et refermer la console |
| Alimentation | USB, séparée de celle de la console ; masses communes |
| Outillage | analyseur logique ≥ 8 voies, ≥ 24 MS/s, pour vérifier les signaux avant et après soudure ; matériel de soudure fine |

Les points de soudure sont dans [`docs/signaux-mgb.md`](docs/signaux-mgb.md), la liste
d'achats détaillée dans [`docs/liste-achats.md`](docs/liste-achats.md).

## Construire, flasher, dialoguer

Il faut le [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.x, CMake, Ninja et le
compilateur `arm-none-eabi-gcc`.

```bash
cd firmware/sniffer
cp include/secrets.h.example include/secrets.h   # puis y écrire le nom et le mot de passe du WiFi
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/sniffer.uf2
../../tools/console.py
```

`secrets.h` n'est jamais versionné. Avant de construire, réglez l'adresse du récepteur
dans `include/config.h` : voir le [README du firmware](firmware/sniffer/README.md).

`flash.sh` fait passer le Pico en mode programmation par la console USB. La toute
première fois, il faut maintenir le bouton BOOTSEL en branchant le Pico.

`console.py` ouvre la console du firmware ; `cat /dev/ttyACM0` ne suffit pas, car le
firmware n'écrit que si le signal DTR est activé.

Puis, sur le PC dont l'adresse a été configurée, depuis ce dossier :

```bash
./tools/ecran_virtuel.py
```

## Contenu du dossier

| Dossier | Contenu |
|---|---|
| [`firmware/sniffer/`](firmware/sniffer/README.md) | le firmware du Pico : configuration, commandes de la console, conception |
| `tools/` | les outils côté PC, ci-dessous |
| `docs/` | le plan et les relevés, ci-dessous |

### Documentation

| Document | Contenu |
|---|---|
| [`docs/plan-firmware.md`](docs/plan-firmware.md) | le plan de réalisation : décisions et leur justification, architecture, phases, budget, journal des décisions |
| [`docs/etapes-detaillees.md`](docs/etapes-detaillees.md) | la marche à suivre, étape par étape : mesures, montage, programme PIO, diagnostic |
| [`docs/signaux-mgb.md`](docs/signaux-mgb.md) | l'identification des signaux de l'écran, mesurée sur la carte ; le brochage qui fait foi |
| [`docs/recette-cablage.md`](docs/recette-cablage.md) | la vérification des signaux après soudure |
| [`docs/liste-achats.md`](docs/liste-achats.md) | la liste d'achats, chaque pièce justifiée |
| [`docs/releves/`](docs/releves/README.md) | les captures de l'analyseur logique et les images obtenues |

### Outils

| Outil | Rôle |
|---|---|
| [`tools/ecran_virtuel.py`](tools/ecran_virtuel.py) | reçoit le flux `PXL1` et l'affiche sur le PC ; mesure cadence, pertes et débit |
| [`tools/gbdump.py`](tools/gbdump.py) | demande une image au firmware par la console USB et l'enregistre en PNG |
| [`tools/balaye_premier_pixel.py`](tools/balaye_premier_pixel.py) | balaye le délai de lecture du premier pixel de chaque ligne et compare la colonne 0 à une référence : retrouve sa fenêtre sans analyseur |
| [`tools/sniffer.py`](tools/sniffer.py) | envoie des commandes au firmware depuis un script |
| [`tools/console.py`](tools/console.py) | console série du Pico |
| [`tools/flash.sh`](tools/flash.sh) | flashe un `.uf2` |
| [`tools/pxl1_envoi.py`](tools/pxl1_envoi.py) | émetteur `PXL1` de test, avec perte de paquets simulée : éprouve l'écran virtuel sans le Pico |
| [`tools/sonde.sh`](tools/sonde.sh) | capture courte à l'analyseur logique et dépouillement immédiat, pour sonder la carte point par point |
| [`tools/analyse_sr.py`](tools/analyse_sr.py) | dépouille une capture PulseView `.sr` et identifie les signaux, preuves à l'appui |
| [`tools/debut_ligne.py`](tools/debut_ligne.py) | chronomètre le début des lignes et des images face au sniffer : donnée du premier pixel contre son front, relance du PIO après `S` |
| [`tools/mesure_debut_ligne.sh`](tools/mesure_debut_ligne.sh) | capture à l'analyseur, 24 MS/s, puis `debut_ligne.py` — l'enquête du 08/10/2026 |
| [`tools/simuler_bus_gb.py`](tools/simuler_bus_gb.py) | fabrique une capture `.sr` synthétique, pour éprouver `analyse_sr.py` sans console |

Dépendances Python : `numpy`, `Pillow`, `pyserial` et `tkinter`. `sonde.sh` demande
`sigrok-cli`.

## État

| Phase | État |
|---|---|
| 0 · Identification des signaux à l'analyseur logique | ✅ 25/09/2026 |
| 1 · Soudure des prises de signaux | ✅ 26/09/2026 |
| 2 · Capture | ✅ 26/09/2026 — 108 233 images en 30 minutes, sans une erreur |
| 3 · Émission réseau | ✅ 26/09/2026 — le jeu s'affiche sur le PC |
| 4 · Latence et robustesse | ✅ 26/09/2026 — émission pipelinée, reconnexion WiFi automatique |
| 5 · Intégration : boîtier, antenne, alimentation définitive | à venir |

Le détail des mesures est dans le [journal du firmware](firmware/sniffer/JOURNAL.md).
