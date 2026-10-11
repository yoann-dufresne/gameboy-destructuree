# Firmware de mesure — le banc de la phase 0

Firmware d'un Pico 2 W qui lit les deux fils du son et dit ce qu'il voit, en volts. C'est
l'instrument de la phase 0, et des comparaisons avant / après soudure de la phase 1. La
radio reste éteinte : on mesure le son de la console, sans le bruit du WiFi.

Construction et flashage : [README du module](../../README.md#construire-flasher-mesurer).
Le montage à câbler : [`prise-son-mgb.md`](../../docs/prise-son-mgb.md) §3, avec les
valeurs du banc. Ce qu'il faut mesurer avec : [plan de tests](../../docs/plan-de-tests.md),
phase 0.

## Brochage

| Broche du Pico | Rôle |
|---|---|
| GP26 (broche 31) | voie A : le fil de la broche 5 de VR1, la gauche d'après le schéma |
| GP27 (broche 32) | voie B : le fil de la broche 4 de VR1 |
| AGND (broche 33) | masse du montage, reliée à la masse de la console |
| ADC_VREF (broche 35) | haut du pont de polarisation |

⚠️ GP26 et GP27 ne supportent pas plus de 3,3 V. C'est le montage qui les protège : ne
jamais y relier un fil de la console en direct.

## Commandes de la console

| Touche | Effet |
|---|---|
| `g` | les deux entrées, verdict sur une seconde : le montage est-il bon, le son passe-t-il ? |
| `s` | relevé depuis la remise à zéro : moyenne, son le plus fort, moment le plus calme, compteurs |
| `r` | remise à zéro |
| `f` | flux de mesures pour [`tools/mesure_son.py`](../../tools/mesure_son.py), marche / arrêt |
| `h` | aide |

`g` est le premier réflexe après avoir câblé :

```
--- les deux entrees, sur 1 s ---
  voie A (GP26)  moyenne 1.65 V  polarisation correcte
                 le son passe : 0.81 V crete-a-crete au Pico, environ 1.62 V au point de prise
```

La conversion « au point de prise » suppose le gain du banc, 0,5 (`GAIN_MONTAGE` dans
`include/config.h`). Le script, lui, prend Rs et Rb en option.

## Le flux

Une ligne par fenêtre de 50 ms, en pas de l'ADC (0 à 4095) :

```
F <n> <minA> <maxA> <moyA> <effA> <satA> <minB> <maxB> <moyB> <effB> <satB>
```

`eff` est la valeur efficace du son seul, sans la moyenne ; `sat`, le nombre d'échantillons
au bout de la plage. `n` se suit : un trou est une ligne que le PC n'a pas lue à temps,
car le firmware ne l'attend jamais plus de 10 ms.

## Lire les compteurs

| Compteur | Attendu | Un écart signifie |
|---|---|---|
| `debordements FIFO` | 0 | le DMA n'a pas suivi l'ADC : un échantillon perdu, et la capture relancée |
| `relances` | 0 | idem |
| `retards` | 0 | la boucle a relu l'anneau trop tard : la mesure en cours a été jetée |
| `erreurs ADC` | 0 | des conversions signalées fautives par l'ADC |

## Conception

**Le processeur ne touche aucun échantillon en temps réel.** L'ADC convertit en continu,
A et B en alternance, 384 000 fois par seconde. Le DMA les range sans fin dans un anneau
de 32 ko, le plus grand que permette son bouclage d'adresse : 42,7 ms de son.

**La boucle principale relit l'anneau** et en tire, toutes les 50 ms, le minimum, le
maximum, la moyenne et la valeur efficace de chaque voie. Elle ne garde aucun échantillon,
seulement ces chiffres.

**Un échantillon perdu échangerait les voies.** Un indice pair de l'anneau est A, un impair
est B. Si la FIFO de l'ADC déborde, l'alternance se décale : plutôt que de deviner, la
capture repart de zéro, de A.

**L'USB n'attend jamais plus de 10 ms** (`CMakeLists.txt`). Au-delà de 42 ms, l'anneau
serait réécrit avant d'être relu. Une ligne perdue vaut mieux qu'une mesure fausse.

| Fichier | Rôle |
|---|---|
| `include/config.h` | brochage, cadence, anneau, gain du montage |
| `src/main.cpp` | ADC et DMA, mesures, console |
