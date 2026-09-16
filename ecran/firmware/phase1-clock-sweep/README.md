# Mesure de l'horloge pixel maximale stable

Balaie la fréquence d'horloge pixel de **28 MHz à 6 MHz** en affichant une mire
conçue pour révéler une horloge trop rapide. Donne le chiffre dont dépend
l'arbitrage du 3×3 (plan §5, phase 4).

## D'où vient la fréquence

Le programme PIO `hub75_bitplane_stream` consomme **9 cycles par pixel** :

```
out pins, 6  [3]   side 0    -> 4 cycles
out null, 2  [3]   side 1    -> 4 cycles
jmp x--, loop      side 0    -> 1 cycle
```

Avec `sm_clockdiv_factor = 1.0`, l'horloge vue par la dalle vaut donc exactement
`clk_sys / 9`. On balaie en changeant `clk_sys`, ce qui met tout à l'échelle
ensemble — horloge pixel, garde de verrou, garde d'adressage.

| clk_sys | horloge pixel | | clk_sys | horloge pixel |
|---|---|---|---|---|
| 252 MHz | 28 MHz | | 126 MHz | 14 MHz |
| 234 MHz | 26 MHz | | 108 MHz | 12 MHz |
| 216 MHz | 24 MHz | | 90 MHz | 10 MHz |
| 198 MHz | 22 MHz | | 72 MHz | 8 MHz |
| 180 MHz | 20 MHz | | 54 MHz | 6 MHz |
| 162 MHz | 18 MHz | | | |
| 144 MHz | 16 MHz | | | |

## Pourquoi le balayage descend

Le pilote est créé à la fréquence **la plus haute**, donc ses gardes valent leur
valeur nominale (80 ns pour le verrou, 160 ns pour l'adressage) au point le plus
contraignant. En descendant, elles ne peuvent que devenir plus généreuses.

Un défaut observé en haut du balayage est donc bien un défaut **de fréquence**, et
pas une garde trop courte. C'est ce qui rend la mesure exploitable.

## La mire

| Lignes | Contenu | Ce qu'il révèle |
|---|---|---|
| 0–23 | rayures verticales de 1 px, blanches | cas le plus exigeant : les données basculent à chaque coup d'horloge |
| 24–39 | colonnes rouge / vert alternées | les 6 lignes de données basculent en opposition de phase — diaphonie |
| 40–51 | blanc plein | référence d'uniformité et de luminosité |
| 52–63 | pixels isolés tous les 8 | une bavure apparaît comme un pixel fantôme juste à droite |

## Lecture

Ce qu'on cherche est **spatial**, pas temporel, et apparaît d'abord sur le **bord
droit** de la dalle — les derniers pixels décalés dans le registre.

- **sain** : rayures franches, contraste constant de gauche à droite ;
- **défaut** : rayures qui se brouillent, grisonnent ou bavent à droite, pixels
  isolés suivis d'un fantôme.

> Le scintillement en bas du balayage est **normal** : le rafraîchissement est
> proportionnel à `clk_sys`. Il ne compte pas dans la mesure.

Noter le **palier le plus haut encore net**. C'est la mesure.

## Construire et flasher

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 -c "import serial,time; s=serial.Serial('/dev/ttyACM0',1200); s.dtr=False; time.sleep(.1); s.close()"
cp build/phase1_clock_sweep.uf2 /media/$USER/RP2350/
../../tools/console.py
```

## Empreinte

85 ko de RAM sur 520, 44 ko de flash — pour une dalle 64×64 en 8 plans BCM.
