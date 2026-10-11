# Son — le son d'une Game Boy Pocket, en WiFi

Ce module prend le son d'une Game Boy Pocket à la sortie de son processeur, avant le
bouton de volume, et le diffuse en WiFi ; un haut-parleur réseau le joue. La console
continue de fonctionner normalement, haut-parleur d'origine compris : on écoute, on ne
pilote rien.

Les deux moitiés s'éprouvent séparément. Le haut-parleur joue tout flux qu'on lui envoie,
quelle qu'en soit la source : un PC peut l'alimenter. La source, elle, se vérifie avec un
haut-parleur virtuel sur PC.

Dans la [Game Boy Pocket déstructurée](../README.md), ce module est au son ce que
[capture](../capture/README.md) et [écran](../ecran/README.md) sont à l'image.

## Principe

```
 Game Boy Pocket                          réseau « son », émis par la source
 processeur ── G, D ──► [Pico 2 W · source] ─── SON1 / UDP ───► [Pico 2 W · haut-parleur]
 (analogique)            ADC + DMA                               tampon, I2S
                                                                     │
                                                                 MAX98357A ──► HP de Game Boy
```

Le convertisseur analogique-numérique (**ADC**) du Pico lit les deux voies, et le **DMA**
range les échantillons en mémoire, sans le processeur : c'est le principe de la capture
vidéo, appliqué au son. Le flux part par blocs de 5 ms, en PCM 16 bits stéréo à 48 kHz :
200 paquets par seconde, 1,6 Mbit/s.

## Un Pico à lui

Le son a son propre Pico dans la console, à côté de celui du sniffer : un microcontrôleur par
type de périphérique, chacun émet son réseau ([le réseau du projet](../reseau.md)). Jusqu'à
quatre haut-parleurs s'y connectent en direct, et la source envoie une copie du son à chacun.

Reste une inconnue qui ne se calcule pas : **la radio s'entendra-t-elle dans l'ADC ?** Elle
est à quelques centimètres, et c'est elle qui émet le son. Une phase de mesure tranche, et
donne les remèdes ; l'analyse est au [§2 du plan](docs/plan.md).

Où souder : [`docs/prise-son-mgb.md`](docs/prise-son-mgb.md). Deux points et une masse, une
résistance de 10 kΩ sur chacun, un connecteur.

## Matériel

| Rôle | Pièce |
|---|---|
| Source | un Pico 2 W dédié, dans la console : point d'accès du réseau des haut-parleurs |
| Prise du son | 2 fils soudés sur la carte de la console, en amont du volume, 10 kΩ en série, connecteur JST-SH 3 points séparé de celui de la vidéo |
| Étage d'entrée | une poignée de passifs près du Pico : couplage, polarisation, filtre |
| Haut-parleur | un à quatre ; chacun un Pico 2 W, un module MAX98357A (convertisseur et amplificateur I2S), un haut-parleur de Game Boy |
| Mesure | un multimètre, des écouteurs, et un **banc de mesure** : un Pico 2 W de rechange avec l'étage d'entrée sur plaque d'essai. Aucun matériel audio |

Nomenclature complète : [§11 du plan](docs/plan.md).

## Construire, flasher, mesurer

Il faut le [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.x, CMake, Ninja, le
compilateur `arm-none-eabi-gcc`, et Python 3 avec `pyserial`. Pour le firmware de mesure :

```bash
cd firmware/mesure
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
../../tools/flash.sh build/mesure.uf2
```

⚠️ **Un seul Pico branché au PC pendant le flashage.** `flash.sh` vise `/dev/ttyACM0` : si
le sniffer est branché lui aussi, c'est peut-être lui qui serait reflashé. Sinon, donner le
port en second argument : `flash.sh build/mesure.uf2 /dev/ttyACM1`. La toute première
fois, il faut maintenir le bouton BOOTSEL en branchant le Pico.

Puis, depuis ce dossier :

```bash
./tools/console.py                # la console du firmware : taper « g » pour vérifier le montage
./tools/mesure_son.py niveau      # la force du son, et la résistance à monter
./tools/mesure_son.py cotes       # quelle voie est la gauche
```

`console.py` et `mesure_son.py` ne peuvent pas ouvrir la console en même temps : fermer
l'un avant de lancer l'autre. `cat /dev/ttyACM0` ne suffit pas, car le firmware n'écrit que
si le signal DTR est activé.

## Contenu

| Dossier | Contenu |
|---|---|
| [`firmware/mesure/`](firmware/mesure/README.md) | le banc de mesure de la phase 0 : brochage, commandes, conception |
| `tools/` | [`mesure_son.py`](tools/mesure_son.py), le verdict du banc en clair ; `console.py` et `flash.sh` |
| `docs/` | le plan, les tests et les points de prise, ci-dessous |

| Document | Contenu |
|---|---|
| [`docs/plan.md`](docs/plan.md) | l'analyse et les décisions : le Pico du son et la radio dans l'ADC, la chaîne, le format, le réseau, les horloges, la latence, les risques |
| [`docs/prise-son-mgb.md`](docs/prise-son-mgb.md) | où souder et quoi : le chemin du son dans la console, les points candidats, l'étage d'entrée ; la fiche de la phase 0, à remplir |
| [`docs/plan-de-tests.md`](docs/plan-de-tests.md) | les tests de chaque phase, pas à pas, et leur critère de réussite |

## État

Rien n'est encore soudé. Le module s'ouvre le 11/10/2026.

| Phase | État |
|---|---|
| 0 · Trouver où souder, et quelle résistance mettre | banc de mesure prêt (firmware compilé, script éprouvé sur des mesures simulées) ; mesures à faire |
| 1 · Soudure et étage d'entrée | à venir |
| 2 · Capture seule, sans réseau | à venir |
| 3 · La radio dans l'ADC | à venir |
| 4 · Émission réseau, récepteur sur PC | à venir |
| 5 · Haut-parleur | à venir |
| 6 · Intégration | à venir |
