# Prise du son sur la carte MGB-ECPU-01

*Où souder, quoi souder, et la fiche à remplir pendant la phase 0.*
Version 1 — 11/10/2026

> ⚠️ **Rien de ce qui suit n'est encore mesuré.** La source est le schéma rétro-conçu de
> Gekkio, [`MGB-xCPU`](https://github.com/Gekkio/gb-schematics/tree/main/MGB-xCPU)
> (révision E, 18/07/2023), page 2 « Analog audio ». Ses composants ont été relevés sur une
> carte MGB-ECPU-01, la révision de notre console. C'est une base bien plus sûre que la spec
> du module capture, fausse sur 5 signaux sur 6. La règle ne change pas pour autant : on
> identifie par la mesure, puis on soude. Les tests cités sont ceux du
> [plan de tests](plan-de-tests.md).

---

## 1. Le chemin du son dans la console

```
 CPU MGB              couplage   série      VR1 : volume, 2 × 10 kΩ        U3 « AMP-MGB »
 broche 28 SO2 (G) ──[C19 1 µF]─[RA4 510 Ω]── broche 5 ┐  curseur 2 ──► LIN  8   HPLOUT 12 ──[15 Ω]──► casque, G
 broche 27 SO1 (D) ──[C18 1 µF]─[RA4 510 Ω]── broche 4 ┘  curseur 3 ──► RIN  7   HPROUT 15 ──[15 Ω]──► casque, D
                                               broche 1 = masse                     SPKOUT  3 ──► HP ──► A-GND
                                                                                                          │
                                                          manchon du casque ──► A-GND ──[C31 100 µF]── masse
```

Ce qu'il faut en retenir :

- **Le processeur sort deux voies**, gauche (`SO2`) et droite (`SO1`). Il est alimenté par
  `VDD`, le 5 V régulé du convertisseur de la console, et non par le 3 V des piles : ses
  sorties son vivent dans le domaine 5 V.
- **La molette de volume est un potentiomètre double, placé après le couplage.** En amont
  de lui, broches 4 et 5, le niveau ne dépend pas de la molette.
- **L'amplificateur a deux sorties casque et une seule sortie haut-parleur** : le
  haut-parleur de la console est mono.
- ⚠️ **Le manchon de la prise casque n'est pas la masse.** Il rejoint `A-GND`, qui ne
  touche la masse qu'à travers C31. D'après le schéma, il est sans doute au niveau continu
  des sorties de l'amplificateur. Le relier à une masse, celle d'un PC, d'une interface
  audio ou du Pico, ferait débiter du continu à l'amplificateur dans le haut-parleur. **On
  ne branche rien sur la prise casque**, à part un casque pour écouter.

---

## 2. Les points candidats

| Rang | Point | Nœud du schéma | Continu attendu | Pour | Contre |
|---|---|---|---|---|---|
| **1** | **broches 5 (G) et 4 (D) du potentiomètre VR1** | entrée du volume, après C18/C19 et RA4 | ≈ 0 V | en amont du volume ; déjà débarrassé du continu ; le processeur reste protégé derrière 1 µF et 510 Ω ; des pattes de potentiomètre plutôt que des pastilles fines. C'est sans doute le point du mod « pro sound » de la Pocket : un [fil de chipmusic.org](https://chipmusic.org/forums/topic/4651/gameboy-pocket-prosound-question/) parle des « deuxième et troisième broches près du volume », sans plus de précision | aucun connu |
| 2 | pastilles `SO2` (G) et `SO1` (D) | broches 28 et 27 du CPU, avant C18/C19 | inconnu, probablement autour de 2,5 V | le signal le plus brut ; le schéma y place des points de test, comme ceux de la vidéo | du continu ; la sortie du processeur n'est plus protégée du câble que par notre 10 kΩ |
| ✗ | curseurs de VR1, sorties casque, haut-parleur | après le volume, ou après l'amplificateur | — | — | le niveau suit la molette ; le bruit de l'amplificateur s'ajoute ; le manchon n'est pas la masse |

**Masse :** broche 1 de VR1, la masse locale du volume, au plus près des deux signaux.

> 🔑 **Le choix du point 1 a une conséquence heureuse pour l'installation** : molette à
> zéro, la console se tait et le flux continue. Le haut-parleur d'origine ne fera donc pas
> écho au haut-parleur réseau.

> ℹ️ **Le numéro de broche de VR1 est celui de l'empreinte du schéma**, pas une
> sérigraphie. T0.1 trouve les bonnes pattes au multimètre : la broche 1 est à la masse,
> les broches 4 et 5 font 10 kΩ avec elle, quelle que soit la molette. Savoir laquelle des
> deux est la gauche n'est pas nécessaire pour souder (T0.6, facultatif).

---

## 3. L'étage d'entrée

```
 dans la console                   câble ≤ 20 cm          côté Pico, au plus près des broches 31 à 35

 VR1-5 (G) ──[Rs 10 kΩ]────────────── G ──────┤├──●─────────────────── GP26 · ADC0 (broche 31)
                                             1 µF  │          │
                                                  [Rb]      [Caa]
                                                   │          │
                                                 VBIAS      AGND (broche 33)

 VR1-4 (D) ──[Rs 10 kΩ]────────────── D ────── même montage ──────────── GP27 · ADC1 (broche 32)
 VR1-1     ────────────────────────── masse ───────────────────────────── AGND (broche 33)

 VBIAS : ADC_VREF (broche 35) ──[100 kΩ]──●──[100 kΩ]── AGND, et 10 µF de VBIAS à AGND
```

| Pièce | Valeur | Rôle |
|---|---|---|
| **Rs** | 10 kΩ, **dans la console**, au ras du point soudé | protège la console d'un court-circuit dans le câble, et limite le courant dans les protections de l'ADC. Le rôle des 100 Ω de la vidéo. 22 kΩ si T1.2 montre que la console s'en ressent |
| **C** | 1 µF, film de préférence | bloque le continu, celui de la console comme le nôtre |
| **Rb** | selon T0.4, ci-dessous | ramène le signal sur VBIAS, et forme avec Rs un diviseur de gain Rb / (Rs + Rb) |
| **Caa** | selon Rb, ci-dessous, C0G | filtre anti-repliement vers 20 kHz, et réservoir de charge de l'ADC |
| **VBIAS** | ≈ 1,65 V | le milieu de la plage de l'ADC, pris sur sa propre référence : leurs bruits se compensent en partie |

**Le banc de mesure de la phase 0 est ce même montage, avec Rb = 10 kΩ et Caa = 1,5 nF**,
sur plaque d'essai, Rs compris, au départ de trois fils provisoires. Son gain de 0,5 lui
permet de lire sans saturer jusqu'à 6,6 V crête-à-crête au point de prise : de quoi mesurer
la console sans rien savoir d'elle d'avance.

Le signal doit arriver à l'ADC sous **2,3 V crête-à-crête**, soit 3 dB de marge sur la
plage de 3,3 V. Rb se choisit donc d'après la crête-à-crête maximale mesurée en T0.4 ; le
script `mesure_son.py niveau` fait la lecture de ce tableau à ta place :

| Crête-à-crête au point de prise | Rb | Gain | Caa (fréquence de coupure) |
|---|---|---|---|
| jusqu'à 2,5 V | 100 kΩ | 0,91 | 820 pF (21 kHz) |
| jusqu'à 3,8 V | 15 kΩ | 0,60 | 1,5 nF (18 kHz) |
| jusqu'à 4,6 V | 10 kΩ | 0,50 | 1,5 nF (21 kHz) |
| moins de 0,5 V | voir §5 | | |

> ⚠️ **Les entrées de l'ADC ne tolèrent pas le 5 V.** GPIO0 à GPIO25 du RP2350 sont
> `Digital IO (FT)` et supportent 5,5 V, Pico alimenté ; GPIO26 à GPIO28 sont
> `Digital IO / Analogue` et ne doivent jamais dépasser `IOVDD`, 3,3 V (datasheet RP2350,
> tableau des broches et §12.4). Le continu de la console ne les atteint pas grâce à C, et
> l'amplitude reste dans la plage grâce au diviseur. Rs limite le courant si l'une ou
> l'autre précaution venait à manquer.

Côté firmware, les broches se configurent par `adc_gpio_init()`, qui coupe leur étage
d'entrée numérique : sans cela, une broche maintenue à mi-tension fuit et dérive (errata
RP2350-E9).

---

## 4. Règles de câblage

- **Un connecteur à part**, JST-SH 3 points (G, masse, D). Celui de la vidéo est plein, et
  le son doit rester à l'écart de l'horloge pixel `CP`, qui bat à 4 MHz.
- **Une masse qui voyage avec les signaux** : trois fils torsadés, ou un câble blindé fin à
  deux conducteurs. Elle arrive sur `AGND` (broche 33), la masse analogique du Pico, pas sur
  une masse numérique.
- **Les deux Rs sur la plaquette déjà logée dans la console**, au plus court du point soudé.
- **Colle dès la sortie des fils**, comme pour la vidéo : un fil fin qui bouge arrache sa
  pastille.

---

## 5. Si la mesure surprend

| Constat | Conduite |
|---|---|
| Moins de 0,5 V crête-à-crête au point 1 | Mesurer au point 2, avant le diviseur 510 Ω / 10 kΩ. Sinon, un étage de gain : amplificateur opérationnel rail à rail alimenté en 3,3 V (MCP6002) |
| Plus de 4,6 V crête-à-crête | Rb plus faible, Caa recalculé : fréquence de coupure = 1 / (2π · (Rs ∥ Rb) · Caa) |
| Un bourdonnement dès la phase 0 | C'est la console. On le mesure (T0.4) et on le documente : le Pico n'a pas à le corriger |
| Un bruit qui n'apparaît qu'avec le Pico | [Plan de tests](plan-de-tests.md), T3.2 |

---

## 6. Fiche de la phase 0 — à remplir pendant la séance, pas après

Date : `__/__/____` · Alimentation de la console : `________`

**Les bonnes pattes (T0.1)** — console éteinte ; photos dans `releves/`

| Broche | Patte repérée (photo) | Ohms jusqu'à la borne − | Bouge avec la molette ? |
|---|---|---|---|
| 1, masse | | ≈ 0 attendu | non |
| 4 | | ≈ 10 kΩ attendu | non |
| 5 | | ≈ 10 kΩ attendu | non |
| curseurs (2 pattes) | | variable | oui |

**La tension fixe (T0.2)** — console allumée

| Broche | Molette au minimum | Molette au maximum |
|---|---|---|
| 4 | | |
| 5 | | |

**Le banc branché (T0.3)** — commande `g`

| | Voie A (patte 5) | Voie B (patte 4) |
|---|---|---|
| console éteinte : polarisation | | |
| console allumée : le son passe ? | | |

**La force du son (T0.4)** — recopier le verdict du script ; fichier
`releves/phase0-niveau.json`

| | Voie A | Voie B |
|---|---|---|
| son le plus fort, au point de prise | | |
| bruit de fond | | |
| passages joués | | |

**La console ne remarque rien (T0.5)** — à l'écoute : ☐ même volume ☐ aucun bourdonnement nouveau

**Gauche ou droite (T0.6, facultatif)** — la voie A est : ☐ la gauche ☐ la droite ☐ pas cherché

**Décision**

| | |
|---|---|
| Rb (donné par le script) | |
| Caa | |
| Rs | 10 kΩ |
| Verdict | ☐ GO phase 1 |
