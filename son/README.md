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

## Sans matériel

Le réseau et le traitement du son s'éprouvent sur un seul PC. Les outils demandent `numpy`
(et `scipy` pour la conception du filtre) :

```bash
./tools/son_envoi.py --perte 1 --retard 20 --derive 300 &   # une fausse source, malmenée
./tools/hautparleur_virtuel.py --source 127.0.0.1           # le haut-parleur de référence
```

Le haut-parleur virtuel joue le son sur le PC (`pw-play`, `aplay` ou `sounddevice`) et donne
son verdict : pertes, retards, réserve conseillée, dérive des horloges. Face à la vraie source,
il suffit de rejoindre le réseau `gb-son` et de lancer `hautparleur_virtuel.py` sans option.

La décimation et la réserve du haut-parleur, en C++ portable, ont leurs tests :

```bash
cd firmware/commun/test
g++ -std=c++20 -O2 -Wall -Wextra -I.. test_decimation.cpp -o /tmp/test_decimation && /tmp/test_decimation
g++ -std=c++20 -O2 -Wall -Wextra -I.. test_rattrapage.cpp -o /tmp/test_rattrapage && /tmp/test_rattrapage
```

Le second simule plus de quatre heures d'écoute en une vingtaine de secondes.

## Contenu

| Dossier | Contenu |
|---|---|
| [`firmware/mesure/`](firmware/mesure/README.md) | le banc de mesure de la phase 0 : brochage, commandes, conception |
| `firmware/commun/` | ce que partageront la source et le haut-parleur, éprouvé sur PC : [`son1.h`](firmware/commun/son1.h), le protocole, qui fait foi ; la décimation ; la réserve et le rattrapage de la dérive |
| `tools/` | [`mesure_son.py`](tools/mesure_son.py), le verdict du banc en clair ; [`son_envoi.py`](tools/son_envoi.py) et [`hautparleur_virtuel.py`](tools/hautparleur_virtuel.py), une fausse source et un haut-parleur de référence ; [`decimation_conception.py`](tools/decimation_conception.py), le calcul du filtre ; `console.py` et `flash.sh` |
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
| 4 · Émission réseau, récepteur sur PC | récepteur de référence et fausse source prêts, éprouvés l'un contre l'autre |
| 5 · Haut-parleur | réserve et rattrapage de la dérive écrits, éprouvés en simulation |
| 6 · Intégration | à venir |
