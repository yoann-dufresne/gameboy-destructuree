# Module MANETTE — plan de réalisation

*Sous-projet « manette » de la Game Boy Pocket déstructurée.*
Version 3 — 11/10/2026 : la manette dort posée, se réveille au mouvement et rejoint la console
avant qu'on bascule l'interrupteur.

**Le « pourquoi » est ici.** Le pas à pas, ses tests et ses critères de sortie :
[`etapes.md`](etapes.md).

---

## 0. Décisions

Les sept premières lignes sont tes décisions. Les autres sont des propositions : la dernière
colonne dit quel test les confirme ou les renverse.

| Sujet | Décision ou proposition | Tranchée par |
|---|---|---|
| **Qui parle à qui** | la manette ne parle qu'à la console ; la console parle à l'écran et au son | toi, 11/10/2026 |
| **Le réseau** | l'injecteur émet le réseau des manettes, dont il est le point d'accès : jusqu'à 4 manettes, en liaison directe. Il prévient le reste de la console par un bus filaire, dont il est le maître ([réseau](../../reseau.md)) | toi, 11/10/2026 ; banc R1 |
| **Marche et arrêt** | l'interrupteur de la manette allume et éteint la console ; la console allume et éteint l'écran et le son. Les Pico de la console, de l'écran et du son ne s'éteignent jamais ; seul celui de la manette s'éteint | toi, 11/10/2026 |
| **Molettes** | le volume règle le son, le contraste devient la luminosité de l'écran, par l'intermédiaire de la console | toi, 11/10/2026 |
| **Rien sans marche** | interrupteur sur arrêt, la manette ne transmet rien : ni boutons, ni molettes. Seul l'ordre « arrêt » part, au moment où on bascule | toi, 11/10/2026 |
| **LED** | une LED RGB à l'emplacement de la LED d'origine. Interrupteur sur arrêt : éteinte. Sur marche et appairée à la console, c'est-à-dire quand la console répond : rouge fixe, comme sur la console d'origine. Sur marche sans réponse de la console : bleu, pulsé lentement, le temps de chercher le réseau et la console | toi, 11/10/2026 |
| **Sommeil et réveil** | posée, la manette dort et ne consomme presque rien. Un mouvement la réveille : elle rejoint la console avant qu'on bascule l'interrupteur, qui démarre alors la console aussitôt. Elle se rendort interrupteur sur arrêt, immobile depuis 1 minute | toi, 11/10/2026 |
| **La manette** | une seconde Game Boy Pocket. On garde la coque, la croix, les boutons et leurs membranes, les deux molettes, l'interrupteur et les piles. Sa carte est remplacée par une carte maison : plus de processeur, plus d'écran allumé | phases 0 et 5 |
| **Contrôleur** | un Pico 2 W, comme les autres modules, soudé à plat sur la carte maison | T0.4 (la place), T5.2 (l'antenne) |
| **Alimentation de la manette** | les deux piles AAA, sans convertisseur. **Dormir, c'est être hors tension** : un accéléromètre alimenté en permanence rallume le Pico, qui se coupe lui-même pour se rendormir | T1.6, T1.7, T2.6 |
| **Lecture** | une broche par bouton, pas de matrice : tous les appuis simultanés sont lus | T1.1, T1.2 |
| **Protocole** | `MAN1` sur UDP, port 4244 ; chaque paquet porte l'état complet ; la console accuse réception | T2.2, T2.3 |
| **Boutons de la console** | 8 photoMOS de faible capacité, chacun avec sa diode, en parallèle des boutons ; 6 fils soudés | T0.7, T4.2 |
| **Alimentation de la console** | par sa prise DC, en 3,0 V, que l'injecteur établit et coupe ; l'interrupteur de la console reste sur marche | T0.8, T4.7 |
| **Manette perdue** | les boutons sont relâchés après 200 ms ; un message par-dessus l'image du jeu dit qu'aucune manette n'est connectée ; le sniffer l'ajoute ; la console s'éteint après 15 minutes d'absence | toi, 11/10/2026 |

---

## 1. Objectif et périmètre

```
 MANETTE                              CONSOLE PRINCIPALE                       (hors de ce module)
 boutons, molettes,      MAN1         injecteur ── 8 photoMOS ──► boutons
 interrupteur, piles  ─────────►      Pico      ── 3,0 V ───────► prise DC       ──► écran : image, luminosité,
                      ◄─────────                ── molettes, marche/arrêt ──►         marche/arrêt
                       accusés                     au reste de la console     ──► son : son, volume, marche/arrêt
```

**Ce qu'on construit :**

- **la manette** : une Game Boy Pocket qui ne joue plus. Elle lit ses boutons, ses molettes, son
  interrupteur et ses piles, et envoie son état à la console ;
- **l'injecteur**, côté console : il reçoit cet état, appuie sur les boutons de la console à la
  place du joueur, l'allume et l'éteint, et transmet molettes et marche/arrêt au reste de la
  console.

**Ce qu'on ne construit pas :** l'image, le son, la cartouche, et la façon dont la console
prévient l'écran et le son : elle passe par le bus filaire de la console, décrit dans le
[document réseau](../../reseau.md). La console principale reste une vraie console qui exécute le
vrai jeu, et ses propres boutons continuent de marcher.

**Chaque moitié s'éprouve seule** : la manette avec un récepteur sur PC, l'injecteur avec une
fausse manette sur PC et l'analyseur logique. Hors de la Game Boy, la manette pilote tout ce
qui lit `MAN1`, un émulateur sur PC par exemple ; l'injecteur fait jouer n'importe quelle Game
Boy Pocket.

---

## 2. Comment la console lit ses boutons

Le processeur ne lit pas huit boutons, mais **quatre fils d'entrée, P10 à P13**, qu'il tient à 1
par des résistances internes. Pour lire la croix, il met **P14** à 0 : un bouton de la croix
enfoncé relie son entrée à P14, qui la tire à 0. Pour lire les boutons, il remet P14 à 1 et met
**P15** à 0. Le jeu fait ce tour une fois par image, en quelques microsecondes.

| Entrée | P14 à 0 : la croix | P15 à 0 : les boutons |
|---|---|---|
| P10 | Droite | A |
| P11 | Gauche | B |
| P12 | Haut | Select |
| P13 | Bas | Start |

**Sur la MGB, chaque bouton passe par une diode**, cathode côté P14 ou P15 : les réseaux DA1
pour la croix, DA2 pour les boutons, d'après le schéma de Gekkio
([`MGB-xCPU`](https://github.com/Gekkio/gb-schematics/tree/main/MGB-xCPU), feuille `joypad`).
Sans elles, Droite et A enfoncés ensemble relieraient P14 à P15, deux sorties du processeur
qui se contredisent.

**P14 et P15 ont des points de test**, TP45 et TP46. P10 à P13 n'en ont pas : la phase 0 les
trouve au multimètre, sur les pastilles des boutons.

---

## 3. Appuyer sur les boutons : un interrupteur commandé par la lumière

### 3.1 Imiter un bouton, pas un processeur

Un bouton enfoncé, c'est un contact entre une entrée et une ligne de sélection, à travers une
diode. Il ne sait rien du moment où le jeu lit : c'est la console qui vient voir. L'injecteur
doit faire exactement cela.

Un **photoMOS** est un interrupteur électronique que commande une LED enfermée dans le même
boîtier. Le Pico allume la LED ; de l'autre côté, deux bornes se ferment, sans lien électrique
avec le Pico.

```
 Pico injecteur              photoMOS                        console principale
                      ┌────────────────────┐
 GP2 ──[R]──► LED ────┤ ≋≋►  interrupteur  ├── P10 ───────►|──── P14        = « Droite »
 GND ─────────────────└────────────────────┘                diode
```

Huit photoMOS, un par bouton : entre P10 et P14 pour Droite, entre P10 et P15 pour A, et ainsi
de suite. **Chacun a sa diode**, dans le même sens que celle du bouton qu'il double, pour la
même raison qu'au §2.

Ce que ça apporte :

- **aucun réglage de temps** : la console lit le photoMOS comme un bouton, quand elle veut ;
- **aucune question de niveaux** : que les lignes soient en 3 V ou en 5 V, le Pico n'en voit
  aucune ;
- **injecteur éteint ou débranché : interrupteurs ouverts**, la console est comme avant ;
- **les boutons de la console principale continuent de marcher**, en parallèle ;
- **six fils seulement** dans la console : P10 à P13, P14, P15.

### 3.2 ⚠️ Le piège : la capacité

Ouvert, un photoMOS n'est pas un vide parfait : il se comporte comme un minuscule condensateur
entre ses bornes. Chaque fois que P14 descend, il entraîne un peu P10 avec lui. Les photoMOS
courants, optimisés pour conduire fort, en ont des dizaines à des centaines de picofarads :
P10 serait tiré vers 0 pendant quelques microsecondes, juste au moment où le jeu lit. Ce
seraient des **appuis fantômes**.

**Le modèle proposé est l'AQY221N2S de Panasonic**, de la famille « C×R » faite pour cela :
1 pF typique, 1,5 pF au plus, ouvert ; 9,5 Ω fermé, contre quelques centaines d'ohms pour la
pastille de carbone d'un bouton ; 40 V ([fiche Panasonic](https://industry.panasonic.com/global/en/products/control/relay/photomos/number/aqy221n2s)).
À 1 pF, l'à-coup reste une fraction de la capacité propre de la ligne. T0.7 mesure combien de
temps le jeu laisse entre la sélection et la lecture ; T4.2 cherche les fantômes à
l'analyseur, injecteur branché.

### 3.3 Ce qu'on a écarté

| Idée | Pourquoi non |
|---|---|
| Tirer P10 à la masse | ce serait Droite **et** A à la fois, et un conflit avec le processeur |
| Imiter la matrice au PIO : lire P14 et P15, répondre sur P10 à P13 | faisable, et rapide. Mais il faut adapter les niveaux, et la console dépend alors du firmware. **Plan B** si les photoMOS échouent en T4.2 |
| Un commutateur analogique (74HC4066) | il doit être alimenté par la console, et commandé à son niveau logique |

---

## 4. Allumer et éteindre

### 4.1 La séquence

```
 posée, la manette dort : le Pico est hors tension, seul l'accéléromètre veille

 on prend la manette en main
   → l'accéléromètre rallume le Pico, qui rejoint le réseau, sans rien envoyer     1 à 3 s

 interrupteur sur marche
   → « marche » part aussitôt → l'injecteur établit le 3,0 V → la console démarre
   → la console prévient l'écran et le son                     (hors de ce module)
   → la LED s'allume en rouge ; en bleu pulsé tant que la console n'a pas répondu (§5.6)

 interrupteur sur arrêt
   → « arrêt », jusqu'à l'accusé → l'injecteur relâche tout et coupe le 3,0 V → la console s'éteint
   → la manette reste éveillée et connectée tant qu'on la bouge, mais n'envoie plus rien

 interrupteur sur arrêt, et manette immobile depuis 1 minute
   → le Pico coupe sa propre alimentation : la manette se rendort
```

**Le temps de connexion est pris pendant le geste** : prendre la manette, la tourner,
chercher l'interrupteur. Quand l'interrupteur bascule, la manette est déjà connectée, et la
console démarre aussi vite qu'une console d'origine. Si l'interrupteur bascule sans
mouvement préalable, il réveille la manette lui aussi, et la console démarre après la
connexion. T2.7 mesure les deux cas.

**Interrupteur sur marche, la manette ne dort jamais**, même immobile : une partie en pause
ne doit pas s'éteindre.

### 4.2 Côté manette : dormir hors tension

**Pourquoi ne pas endormir simplement le microcontrôleur ?** Le RP2350 a des modes de sommeil
profond, mais sur la carte Pico 2 W, le régulateur, la mémoire flash et la puce radio restent
branchés sur les piles. La consommation de la carte entière dans ce mode n'est pas publiée ;
T1.7 la mesure, pour comparer. Or c'est elle qui compte. Deux piles AAA NiMH contiennent environ
800 mAh :

| Courant en sommeil | Les piles tiennent |
|---|---|
| 5 µA | des années : leur propre décharge l'emporte |
| 0,5 mA | deux mois |
| 2 mA | deux semaines |

**Le Pico hors tension ne consomme rien, de façon garantie.** On garde donc le maintien
d'alimentation : un P-MOSFET entre les piles et le Pico, fermé tant que l'un des trois le tient.

```
 piles + ──●──────────────── P-MOSFET ─────────── VSYS, par le P-MOSFET de l'USB (§5.3)
           │                    │ grille
        [100 kΩ]                ●──── tirée à la masse par l'un des trois :
           └────────────────────┘
     • l'interrupteur, sur marche, par une diode Schottky ; GP10 lit sa position
     • l'accéléromètre, tant qu'il sent du mouvement, par un N-MOSFET à seuil bas
     • le Pico lui-même, « maintien », par un N-MOSFET sur GP11, 100 kΩ vers la masse
```

**L'accéléromètre proposé est l'ADXL362 d'Analog Devices**, fait pour cet usage : environ
270 nA en mode de réveil, alimenté directement par les piles (1,6 à 3,5 V). Son « commutateur
de mouvement » tient sa broche d'interruption active dès qu'il sent un mouvement, et la
relâche après une durée d'immobilité qu'on règle : **la minute d'immobilité est comptée par
l'accéléromètre, pas par le Pico.** Le Pico le règle une fois, à la mise en place des piles ;
l'accéléromètre garde ses réglages tant qu'elles sont là. Repli : un LIS2DH12, de quelques µA,
plus courant en modules. T1.6 règle le seuil de mouvement.

- **Interrupteur sur arrêt, accéléromètre au repos** : le Pico envoie « arrêt » si ce n'est déjà
  fait, attend l'accusé, puis relâche GP11. Le P-MOSFET s'ouvre : il ne reste que
  l'accéléromètre.
- **Si le firmware plante** : le chien de garde redémarre le Pico, GP11 retombe. Et une console
  injoignable ne retient pas le Pico : 2 s sans accusé, il relâche quand même.
- ⚠️ **Deux tensions** : l'accéléromètre vit à la tension des piles, de 1,8 à 3,2 V, le Pico en
  3,3 V. Leur liaison SPI passe par un adaptateur de niveaux, et le N-MOSFET de l'interruption
  doit conduire dès 1,8 V sur sa grille : un seuil de 1 V au plus.

### 4.3 Côté console : la prise DC

D'après le schéma de Gekkio (feuille `power`), la prise DC de la MGB (3 V, fiche 2,35 ×
0,75 mm, centre positif) remplace les piles, **en amont de l'interrupteur** de la console.

**L'interrupteur de la console reste donc sur marche, et l'injecteur établit ou coupe 3,0 V
sur la prise**, par un régulateur dont la broche d'activation est tenue par une GPIO. Rien à
souder pour l'alimentation, et plus de piles à changer : c'est déjà l'hypothèse
« alimentation stable » du module capture.

- **Pico en redémarrage ou débranché : console éteinte.** La broche d'activation retombe ; la
  console ne peut être allumée que si le Pico qui la commande l'est.
- **Une seconde d'arrêt au moins** entre une extinction et un rallumage, quoi que demande la
  manette : la console doit avoir le temps de se vider.
- **Écarté : un photoMOS en parallèle de l'interrupteur de la console.** En position arrêt,
  l'interrupteur relie l'alimentation de la carte à une résistance de décharge de 75 Ω, R4 : le
  photoMOS y débiterait 40 mA, et R4 chaufferait.

### 4.4 Une manette perdue

Piles vides, manette hors de portée, plantage : l'injecteur ne reçoit plus rien, alors que
l'interrupteur de la manette est resté sur marche.

| Depuis | Ce qui se passe |
|---|---|
| 200 ms | l'injecteur relâche tous les boutons |
| 2 s | **un message s'affiche par-dessus l'image du jeu** : aucune manette n'est connectée, et le temps restant avant l'extinction. Les 2 s évitent qu'il clignote à chaque trou du réseau |
| 15 min | **la console s'éteint**, et prévient l'écran et le son, comme sur un ordre « arrêt » |

Avant l'extinction, rien n'est perdu : on change les piles, la manette se réappaire, le message
disparaît et la partie continue. Une console d'origine aux piles vides, elle, aurait perdu la
partie.

**Le message n'est pas dessiné par ce module.** L'injecteur signale seulement « manette
absente depuis tant de secondes » au reste de la console. **C'est le sniffer qui l'incruste**
dans l'image avant de l'envoyer, puisqu'il la tient : un changement dans le module capture,
pas dans celui-ci.
---

## 5. La manette

### 5.1 Ce qu'on garde de la console distante

| Pièce | Devenir |
|---|---|
| Coque, vitre, croix, boutons, membranes | gardés tels quels |
| Molette de volume, VR1 | gardée ; devient un capteur. Une seule des deux pistes sert |
| Molette de contraste, VR2 | gardée ; devient un capteur, la « luminosité » |
| Interrupteur SW1 | gardé ; marche et arrêt de la console, et réveil de la manette (§4.2) |
| Contacts de piles | gardés |
| LED d'alimentation | remplacée par une LED RGB, au même endroit (§5.6) |
| Écran | éteint. Collé en place pour l'aspect, ou retiré pour mettre la LED derrière la vitre : choix de la phase 5 |
| Processeur, mémoire, amplificateur, convertisseur, prise casque, port link, connecteur de cartouche, prise DC | partent avec la carte d'origine |

Sur la MGB-ECPU-01, Gekkio relève VR1 = 2 × 10 kΩ et VR2 = 30 kΩ, et note que les premières
cartes n'avaient pas de LED d'alimentation. La console distante peut être d'une autre
révision : la phase 0 relève les siennes.

### 5.2 Pourquoi une carte maison

La carte d'origine porte les pastilles des boutons, mais aussi le processeur. Éteint, il reste
branché sur les lignes des boutons, et ses protections internes les tireraient vers la masse.
Garder la carte demanderait de le dessouder, un boîtier QFP, à l'air chaud, ou de couper des
pistes.

Une carte maison, au contour de l'originale, porte les pastilles, les molettes, l'interrupteur,
les contacts de piles et le Pico, et rien d'autre. **Si l'interrupteur, les molettes et les
contacts sont achetés en pièces de rechange, la carte d'origine reste intacte** : la console
distante redevient une Game Boy en échangeant sa carte.

*Raccourci écarté par défaut* : dépeupler la carte d'origine, processeur et convertisseur
compris, et câbler le Pico sur la matrice, qu'il balaie comme le faisait le processeur. Plus
rapide, mais irréversible pour la console distante, et fragile : des fils volants dans la
coque.

### 5.3 L'électronique

```
 piles 2 × AAA ── maintien (§4.2) ── P-MOSFET de l'USB ──┬── VSYS ── régulateur 3,3 V du Pico 2 W
 de 3,2 à 1,8 V                                          │
                                  USB, mise au point ────┘  (par la diode du Pico)

 8 pastilles ─────────────► GP2 à GP9, tirage interne vers le haut, appui = masse
 interrupteur ────────────► GP10 ; maintien ◄── GP11
 accéléromètre ◄── SPI ───► GP16 à GP19, par un adaptateur de niveaux ; son interruption ──► maintien
 VR1, VR2 : curseurs ─────► ADC0, ADC1 ; extrémités sur ADC_VREF et AGND
 VSYS / 3 ────────────────► ADC3, sur la carte du Pico
 LED RGB ─────────────────► GP20, GP21, GP22, en PWM
```

- **Pas de convertisseur à ajouter.** Le régulateur du Pico 2 W, un RT6154, monte ou descend
  selon le besoin : il fait 3,3 V de 1,8 à 5,5 V (datasheet du Pico 2 W, §2.3 et §3.4). Deux
  piles neuves font 3,2 V ; deux piles vides, 1,8 V.
- **Les piles et l'USB ensemble** : un second P-MOSFET avant VSYS, selon le montage du §3.5
  de la datasheet (un DMG2305UX). Sans lui, l'USB de mise au point pousserait du courant dans
  les piles ; une alcaline qu'on recharge peut couler.
- **Une broche par bouton, appui = masse, tirage interne vers le haut.** Pas de matrice, pas de
  diodes. Les tirages vers le bas du RP2350 sont à éviter (errata E9).
- **Les molettes se lisent en rapport, pas en volts** : extrémités sur `ADC_VREF` et `AGND`, le
  curseur donne la position quelle que soit la tension.
- **La tension des piles** : VSYS / 3 arrive sur ADC3, mais la broche est partagée avec la
  radio. Elle se lit entre deux échanges (datasheet §3.8). La manette l'envoie : l'autonomie se
  mesure depuis le PC.
- **Reflasher sans ouvrir la coque** : `flash.sh` passe par l'USB. En secours, Select + Start
  + A + B maintenus à l'allumage font redémarrer le Pico en mode programmation.

### 5.4 Le firmware, en bref

- **Lecture à 1 kHz, anti-rebond qui ne retarde rien** : le premier front part aussitôt, les
  rebonds des 5 ms suivantes sont ignorés.
- **Molettes** : moyennées, ramenées à 256 crans, avec une hystérésis d'un cran pour que la
  valeur ne tremble pas.
- **Émission** : à chaque changement, aussitôt, puis deux répétitions à 4 ms d'écart. Sans
  changement, un battement toutes les 50 ms.
- **Trois états** : *prête*, interrupteur sur arrêt : connectée, la radio dans son mode le
  plus économe, **rien n'est transmis**, ni boutons ni molettes ; *en marche* : la cadence
  ci-dessus ; *fin*, à la bascule sur arrêt : l'ordre « arrêt » répété jusqu'à l'accusé, puis
  le silence, puis le maintien relâché quand l'accéléromètre relâche (§4.1, §4.2).
- **Sobriété** : le processeur dort entre deux lectures ; le mode d'économie de la radio est
  choisi par la mesure (T2.4, T3.4).

### 5.5 La place, et l'antenne

Le Pico 2 W mesure 51 × 21 mm. **Son antenne doit être au bord de la carte, au-dessus d'une
découpe sans cuivre de 14 × 9 mm, loin du métal** (datasheet §2.2.1). Dans une Game Boy, le
métal, ce sont les piles et leurs contacts.

Deux places candidates : **l'emplacement du connecteur de cartouche**, au dos du haut de la
carte, dont la manette n'a plus l'usage, loin des piles ; ou **derrière la vitre**, à la place
de l'écran. T0.4 les mesure ; le gabarit de la phase 5 tranche avant toute commande.
L'accès au port USB se décide en même temps.

---

### 5.6 La LED

Une LED RGB, à l'emplacement et au diamètre de la LED d'origine, derrière la même fenêtre de la
coque. Elle ne dépend que de l'interrupteur et de l'appairage avec la console :

| Interrupteur | Console | LED |
|---|---|---|
| arrêt | — | éteinte, que la manette dorme ou soit éveillée |
| marche | appairée : ses accusés arrivent | **rouge fixe**, comme la LED d'origine |
| marche | pas d'accusé depuis 500 ms : réseau absent, console injoignable ou pas encore jointe | **bleu, pulsé lentement** : la manette cherche le réseau ou la console |

Trois broches du Pico en PWM, une résistance par couleur ; le rouge et le bleu se règlent pour
que leur éclat ressemble à celui de la LED d'origine. Alimentée par le 3,3 V du Pico, elle ne
faiblit pas avec les piles. Le rouge allumé pendant toute une partie coûte quelques
milliampères : peu, à côté de la radio.

Les premières cartes MGB n'avaient pas de LED d'alimentation (note de Gekkio) : si la coque de
la console distante n'a pas de fenêtre, T0.1 le dit, et il faudra en percer une ou choisir une
autre place.

## 6. Le protocole `MAN1` — proposition

Mêmes conventions que `PXL1` et `SON1` : champs en little-endian, nombre magique en tête.
UDP, port **4244**.

| Octet | Taille | Champ |
|---|---|---|
| 0 | 4 | nombre magique `'M','A','N','1'` |
| 4 | 1 | type : 0 = état, de la manette vers la console ; 1 = accusé, de la console vers la manette |
| 5 | 1 | boutons, 1 = enfoncé. Bits 0 à 3 : Droite, Gauche, Haut, Bas ; bits 4 à 7 : A, B, Select, Start. L'ordre du registre P1 de la console |
| 6 | 1 | volume, 0 à 255 |
| 7 | 1 | contraste, 0 à 255 |
| 8 | 1 | état : interrupteur de la manette sur marche (1) ou arrêt (0). Dans un accusé : console allumée (1) ou éteinte (0) |
| 9 | 1 | réservé, 0 |
| 10 | 2 | tension des piles, en mV |
| 12 | 4 | session, tirée au hasard au démarrage de la manette |
| 16 | 4 | numéro du paquet ; dans un accusé, celui du paquet acquitté |
| 20 | 4 | temps depuis le démarrage de la manette, en ms |

24 octets ; en marche, au repos, 20 paquets par seconde dans chaque sens. **Interrupteur sur
arrêt, rien ne part**, sauf l'ordre « arrêt » au moment de la bascule. Le débit est
négligeable.

**Chaque paquet porte l'état complet.** Un paquet perdu ne coûte rien : le suivant dit tout.
Le vrai danger n'est pas un appui manqué, c'est **un relâchement manqué : un bouton coincé**.
D'où les répétitions, le battement de 50 ms, et la règle de sécurité de l'injecteur : sans
nouvelles depuis 200 ms, il relâche tout. Ce seuil doit dépasser les trous normaux du réseau ;
T2.3 les mesure.

**L'accusé** répond à chaque paquet. Il fait savoir à la manette que la console a entendu
l'arrêt.

**La session** fait reconnaître une manette redémarrée, dont la numérotation repart de 0.
**Le temps depuis le démarrage** fait voir un redémarrage involontaire, par exemple des piles
qui s'effondrent pendant une émission. Si c'est l'interrupteur qui a réveillé la manette, il
donne, dans le premier paquet, le temps de connexion.

Le fichier `man1.h` fera foi, dans `firmware/commun/`.

---

## 7. Le réseau

### 7.1 Ce qui circule

| De | Vers | Quoi |
|---|---|---|
| manette | console | `MAN1` : boutons, molettes, interrupteur, piles |
| console | manette | accusés : l'état de la console |
| injecteur | sniffer, source de son, doublure | par le bus filaire de la console : marche, arrêt, luminosité, volume, le message « aucune manette » ; le sniffer et la source les transmettent à l'écran et au son |

### 7.2 Le réseau des manettes

Décision du 11/10/2026, pour tout le projet ([document réseau](../../reseau.md)) : sur la
console, un microcontrôleur par type de périphérique, chacun point d'accès de son propre
réseau. **L'injecteur émet le réseau des manettes**, `gb-manettes` :

- **un seul saut radio** : la manette est cliente directe de l'injecteur ;
- **jusqu'à 4 manettes**, la limite d'un Pico 2 W en point d'accès. Qui gagne quand deux
  joueurs appuient en même temps se décidera plus tard ;
- **des adresses fixes**, qui raccourcissent le réveil (§4.1) : l'injecteur en 192.168.6.1, les
  manettes de .2 à .5 selon leur numéro, réglé dans leur firmware ;
- le canal proposé est le 6, partagé avec le réseau de la cartouche, peu chargé. Le banc de
  coexistence R1 du document réseau confirme le plan de canaux.

### 7.3 Au banc

N'importe quel réseau WiFi fait l'affaire : celui de l'écran, une box, le partage de connexion
d'un téléphone. Le nom et le mot de passe vont dans `secrets.h`, l'adresse de la destination
dans le réglage du firmware : au banc, celle du PC, puis celle de l'injecteur.

---

## 8. Latence

| Étape | Durée |
|---|---|
| Lecture du bouton | ≤ 1 ms |
| Émission et radio | quelques ms ; des pointes à quelques dizaines (aller-retour du sniffer : 3,5 ms en moyenne, 46,5 ms au pire) |
| photoMOS | ≈ 0,03 ms |
| Le jeu lit ses boutons | une fois par image : de 0 à 16,7 ms |
| Le jeu réagit, l'image arrive sur l'écran LED | une image, puis 10 à 25 ms, estimation du module écran |
| **Bouton → lumière** | **≈ 30 à 65 ms** |

La spec initiale visait moins de 50 à 80 ms pour rester jouable. **La part propre à ce module
est la deuxième ligne**, quelques millisecondes : le reste existe déjà sur une console d'origine,
ou appartient aux modules capture et écran. T3.4 la mesure seule ; T6.2 mesure le tout.

---

## 9. Autonomie

Endormie, la manette ne consomme que son accéléromètre, moins d'un µA (§4.2). Prête, elle reste
connectée sans rien émettre : la radio dort entre deux balises du point d'accès. En marche,
deux piles AAA rechargeables (NiMH)
contiennent environ 2 Wh : à 0,1 W, une vingtaine d'heures ; à 0,3 W, moins de 7. La
consommation d'un Pico 2 W radio allumée dépend surtout du mode d'économie de la radio, et
n'est pas publiée : **T2.4 la mesure**, T2.5 vide un jeu de piles pour de vrai.

Les leviers, dans l'ordre : le mode d'économie de la radio, s'il ne coûte pas en latence (T3.4) ;
un battement plus espacé ; le sommeil du processeur entre deux lectures. Une manette souvent
manipulée sans jouer coûte une minute de radio à chaque fois.

**Objectif proposé : 6 heures**, une longue séance de jeu.

---

## 10. Arborescence prévue

```
manette/
├── README.md
├── docs/
│   ├── plan.md                    ← ce document
│   ├── etapes.md                  ← le pas à pas, ses tests ; la fiche de la phase 0
│   └── releves/                   ← scans, photos, captures de l'analyseur
├── firmware/
│   ├── commun/man1.h              ← le protocole, qui fait foi
│   ├── manette/                   ← la console distante : lecture, émission, sommeil et réveil
│   └── injecteur/                 ← la console principale : réception, photoMOS, alimentation
├── materiel/
│   ├── carte-manette/             ← KiCad : la carte qui remplace l'originale
│   └── injecteur/                 ← plaquette des photoMOS et du régulateur 3,0 V
└── tools/
    ├── manette_ecoute.py          ← l'état reçu, en direct ; accusés, pertes, trous, piles, autonomie
    ├── manette_envoi.py           ← une fausse manette : tour des boutons, appuis au hasard, marche, arrêt, silence
    ├── latence.py                 ← le verdict de l'analyseur : bouton → photoMOS
    └── console.py, flash.sh       ← copiés du module son
```

Rien n'est encore écrit : chaque élément naîtra avec la phase qui en a besoin.

---

## 11. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| A1 | Game Boy Pocket, la console distante | 1 | |
| A2 | Raspberry Pi Pico 2 W | 2 | la manette ; l'injecteur de banc |
| A3 | PhotoMOS AQY221N2S (SOP-4), ou équivalent à moins de 2 pF | 10 | 8, plus 2 de rechange |
| A4 | Adaptateurs SOP-4 et SOT-23 vers DIP | 8 + 4 | pour la plaque d'essai |
| A5 | Diode de signal 1N4148 | 8 | une par photoMOS |
| A6 | Résistances des LED des photoMOS | 8 | valeur selon la datasheet du photoMOS |
| A7 | P-MOSFET DMG2305UX | 4 | le maintien et l'USB : banc et carte maison |
| A8 | N-MOSFET à seuil bas (≤ 1 V), diode Schottky BAT54, résistances de 100 kΩ | 4, 2, 4 | le maintien : deux N-MOSFET par montage, l'un pour le Pico, l'autre pour l'accéléromètre |
| A9 | Boîtier 2 × AAA avec interrupteur | 1 | le banc |
| A10 | Piles AAA NiMH et chargeur | 4 | |
| A11 | 8 boutons poussoirs, 2 potentiomètres 10 kΩ linéaires | 1 lot | le banc |
| A12 | Régulateur 3,0 V à broche d'activation, 500 mA au moins | 1 | l'alimentation de la console ; modèle à choisir d'après T0.8 |
| A13 | Fiche DC 2,35 × 0,75 mm, centre positif, avec son câble | 1 | vers la prise DC de la console |
| A14 | Connecteur JST-SH 6 points, avec embase | 1 paire | les six fils des boutons de la console |
| A15 | Fil Kynar, colle | | |
| A16 | Interrupteur, molettes de volume et de contraste, contacts de piles de Game Boy Pocket | 1 jeu | en pièces de rechange ; sinon, ceux de la carte d'origine |
| A17 | Carte maison | 5 | épaisseur 1,2 mm comme l'originale, finition or (ENIG) pour les pastilles des boutons ; 5 est le minimum courant des fabricants |
| A18 | Plaque d'essai, fils de liaison, plaquette à trous | | |
| A19 | Accéléromètre ADXL362 : un module pour le banc, la puce seule pour la carte maison | 1 + 1 | réveil ; module sans régulateur ni LED, qui consommeraient en sommeil |
| A20 | Adaptateur de niveaux 4 voies | 2 | SPI entre l'accéléromètre, à la tension des piles, et le Pico |
| A21 | LED RGB, au diamètre de la LED d'origine, et 3 résistances | 2 | banc et carte maison ; diamètre relevé en T0.1 |
| T1 | Multimètre | 1 | déjà là |
| T2 | Analyseur logique | 1 | celui du module capture |
| T3 | Alimentation de laboratoire | 1 | celle du module capture |
| T4 | Pied à coulisse ; scanner à plat, ou photo bien à plat avec une règle | | phase 0 |
| T5 | Téléphone qui filme au ralenti, 240 images/s | 1 | T4.7, T6.2 |

---

## 12. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| Un bouton reste coincé : relâchement perdu, manette éteinte | 🔴 en jeu | état complet à chaque paquet, répétitions, battement, relâche générale après 200 ms (§6) ; T3.3 |
| La console s'éteint sans que le joueur l'ait voulu | 🔴 en jeu | seuls l'ordre « arrêt » et une longue absence éteignent ; le message prévient et décompte avant (§4.4) ; T3.5 |
| La manette ne s'endort plus et vide ses piles | 🟡 | chien de garde ; relâche du maintien sans attendre une console injoignable (§4.2) ; T1.6 |
| Des réveils pour rien : vibrations, table heurtée | 🟢 | seuil de mouvement réglé en T1.6 ; chaque réveil coûte une minute de radio |
| Le sommeil consomme plus que prévu | 🟡 | Pico hors tension plutôt qu'endormi ; module d'accéléromètre sans régulateur ; T1.6 et T5.3 au µA près |
| La console démarre mal après un arrêt trop court | 🟡 | une seconde d'arrêt au moins (§4.3) ; T3.5, T4.7 |
| Des appuis fantômes sur la console principale | 🟡 | photoMOS à 1 pF ; T0.7, puis T4.2 dos à dos ; le PIO en plan B (§3.3) |
| Arracher une pastille de la console principale | 🔴 | TP45 et TP46 ; colle dès la sortie des fils ; connecteur débrochable |
| L'USB recharge les piles de la manette | 🔴 | P-MOSFET de l'USB (§5.3) |
| Console éteinte, Pico de la console allumés : du courant entre dans la console par ses fils de signal | 🟡 | à vérifier pour la vidéo et le son, qui ne sont pas de ce module ; les photoMOS, eux, n'en font pas passer |
| Réveil trop lent : la console attend la connexion | 🟡 | connexion pendant le geste (§4.1) ; adresse fixe, pas de DHCP ; T2.7 |
| Le Pico ne loge pas dans la coque | 🟡 | T0.4, puis un gabarit en papier avant toute commande |
| L'antenne étouffée par la coque et les piles | 🟡 | antenne au bord, découpe sans cuivre, loin des piles ; T5.2 |
| Les piles s'effondrent pendant une émission | 🟡 | NiMH ; réserve sur VSYS ; T2.5 compte les redémarrages |
| Autonomie trop courte | 🟡 | les leviers du §9 |
| Pastilles de la carte maison mal placées | 🟡 | scan à l'échelle en phase 0, gabarit, essai des membranes (T5.1) |
| Piles montées à l'envers | 🟡 | protection contre l'inversion sur la carte maison |

---

## 13. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 11/10/2026 | Module ouvert : une seconde Game Boy Pocket devient la manette de la console principale | Garder le toucher d'origine, boutons, molettes et interrupteur, sans fil entre le joueur et la console |
| 11/10/2026 | Appuis par photoMOS en parallèle des boutons, chacun avec sa diode | Imite un bouton exactement, sans réglage de temps ni question de niveaux ; la console reste intacte injecteur éteint |
| 11/10/2026 | PhotoMOS de faible capacité, AQY221N2S | Les modèles courants, à fort courant, risquent des appuis fantômes |
| 11/10/2026 | Carte maison plutôt que carte d'origine dépeuplée | Le processeur éteint gênerait la lecture ; la carte d'origine reste intacte si les pièces sont achetées en rechange |
| 11/10/2026 | Pico 2 W alimenté directement par les piles | Son régulateur accepte de 1,8 à 5,5 V : rien à ajouter. Même famille que les autres modules |
| 11/10/2026 | **Décision de Yoann** : la manette ne parle qu'à la console ; la console parle à l'écran et au son. Topologie physique non décidée, une cartouche distante est à venir | Remplace la v1 du plan, où la manette rejoignait le réseau de l'écran et lui envoyait la luminosité |
| 11/10/2026 | **Décision de Yoann** : l'interrupteur de la manette allume et éteint la console, qui allume et éteint l'écran et le son. Les Pico ne s'éteignent jamais, sauf celui de la manette | |
| 11/10/2026 | Maintien d'alimentation sur la manette | Prévenir la console avant de s'éteindre, et ne rien consommer éteinte |
| 11/10/2026 | **Décision de Yoann** : la manette dort posée, un mouvement la réveille et la connecte avant l'interrupteur ; elle se rendort interrupteur sur arrêt et immobile depuis 1 minute | La console démarre dès l'interrupteur, sans attendre la connexion ; presque rien consommé entre deux parties |
| 11/10/2026 | **Décision de Yoann** : interrupteur sur arrêt, rien n'est transmis, sauf l'ordre « arrêt » | Un appui sur une manette posée ne doit rien faire ; et la radio économise |
| 11/10/2026 | **Décision de Yoann** : une LED RGB à la place de la LED d'origine ; éteinte sur arrêt, rouge appairée à la console, bleu pulsé sinon | Garder le rouge de la console d'origine, et voir quand les boutons n'arrivent pas à la console. Le rouge suit les accusés de la console, pas le seul WiFi |
| 11/10/2026 | Dormir hors tension : le maintien coupe le Pico, un ADXL362 le rallume | La consommation de la carte Pico 2 W en sommeil n'est pas publiée ; hors tension, elle est nulle. L'accéléromètre compte lui-même la minute d'immobilité |
| 11/10/2026 | Console alimentée par sa prise DC, en 3,0 V commutés ; son interrupteur reste sur marche | La prise est en amont de l'interrupteur : rien à souder. Un photoMOS en parallèle de l'interrupteur ferait chauffer R4 |
| 11/10/2026 | Une manette perdue relâche les boutons, sans éteindre aussitôt la console | On change les piles sans perdre la partie |
| 11/10/2026 | **Décision de Yoann** : l'injecteur émet le réseau des manettes, jusqu'à 4 manettes en liaison directe ; il prévient le reste de la console par un bus filaire | La topologie de tout le projet : un microcontrôleur par type de périphérique sur la console ([réseau](../../reseau.md)) |
| 11/10/2026 | **Décision de Yoann** : après un temps d'absence de la manette, la console s'éteint ; un message par-dessus l'image du jeu dit qu'aucune manette n'est connectée | Ne pas laisser tourner une console abandonnée, et dire au joueur pourquoi ses boutons ne répondent plus. 15 minutes ; le sniffer incruste le message |
