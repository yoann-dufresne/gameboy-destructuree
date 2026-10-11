# Module SON — plan de réalisation

*Sous-projet « son » de la Game Boy Pocket déstructurée.*
Version 2 — 11/10/2026 : le son a son propre Pico, qui émet le réseau des haut-parleurs.

**Le « pourquoi » est ici.** Où souder : [`prise-son-mgb.md`](prise-son-mgb.md). Quoi
mesurer et dans quel ordre : [`plan-de-tests.md`](plan-de-tests.md).

---

## 0. Décisions

Les deux premières lignes sont tes décisions. Les autres sont des propositions, pas encore
éprouvées : la dernière colonne dit quel test la confirme ou la renverse.

| Sujet | Proposition | Tranchée par |
|---|---|---|
| **Contrôleur** | un Pico 2 W pour le son seul, dans la console, à côté de celui du sniffer : un microcontrôleur par type de périphérique ([réseau](../../reseau.md)) | toi, 11/10/2026 |
| **Réseau** | la source émet le réseau `gb-son` ; jusqu'à 4 haut-parleurs s'y connectent en direct, un seul saut radio, une copie du son pour chacun | toi, 11/10/2026 ; canal : R1 du [réseau](../../reseau.md) |
| **Ce qu'on capture** | le signal analogique des deux voies, à l'entrée du potentiomètre de volume | T0.1 à T0.4 |
| **Conversion** | l'ADC du RP2350, deux voies à 192 kHz, décimées à 48 kHz | T2.3, T3.1 |
| **Format** | PCM 16 bits signé, stéréo, 48 kHz, blocs de 5 ms | — |
| **Protocole** | `SON1` sur UDP, port 4243, un en-tête qui se suffit à lui-même | — |
| **Destinations** | les haut-parleurs qui se signalent, jusqu'à 4 | T4.1 |
| **Restitution** | Pico 2 W, MAX98357A, haut-parleur de Game Boy | T5.1 |
| **Horloges** | le récepteur rattrape la dérive en rééchantillonnant | T5.2 |

---

## 1. Objectif et périmètre

**Ce qu'on construit :** une source qui prend le son de la console au plus près du
processeur et l'émet en UDP, et un haut-parleur réseau qui le joue. La console continue de
fonctionner normalement, haut-parleur compris : on écoute, on ne pilote rien.

**Ce qu'on ne construit pas :** les boutons, l'émulateur, et la resynthèse du son à partir
des registres (§3).

**Chaque moitié s'éprouve seule** : la source avec un haut-parleur virtuel sur PC, le
haut-parleur avec un émetteur de test sur PC. Le module écran a montré que c'est ce qui
permet de savoir, à chaque panne, laquelle des deux moitiés accuser.

---

## 2. Le Pico du son

### 2.1 Un Pico à lui — décision du 11/10/2026

La version 1 de ce plan visait un seul Pico pour l'image et le son : le sniffer avait assez de
calcul, de broches et de débit pour les deux. **Tu as tranché autrement** : un microcontrôleur
par type de périphérique sur la console, chacun point d'accès de son propre réseau
([réseau](../../reseau.md)). La source de son a donc son Pico 2 W, à côté de celui du sniffer.

Ce que ça règle : le firmware du sniffer, clos le 09/10/2026 après 108 233 images sans une
erreur, n'est pas rouvert ; le module son ne dépend plus du module capture ; et les
haut-parleurs ont un réseau à eux, avec ses 4 places, au lieu de se serrer sur celui de
l'image.

Ce que ça ne règle pas : le bruit de la radio dans l'ADC. Au contraire.

### 2.2 Le vrai inconnu : la radio dans l'ADC

Chaque émission WiFi tire une pointe de courant sur l'alimentation du Pico. Le régulateur la
rattrape, mais une ondulation reste, et l'ADC la voit : sa référence vient du même 3,3 V. Or
c'est maintenant le Pico du son qui émet : un bloc toutes les 5 ms, en autant de copies que de
haut-parleurs, jusqu'à 6,2 Mbit/s pour quatre, plus les balises de son point d'accès dix fois
par seconde. Si l'ondulation passe, elle s'entendra comme un sifflement à 200 Hz et ses
harmoniques, ou un ronronnement à 10 Hz, surtout dans les silences. Le sniffer émet aussi, à
quelques centimètres, sur son réseau à lui : son rythme, 298,6 Hz, pourrait passer par l'air,
par le câble audio, ou par une alimentation commune. Aucun calcul ne dit si cela passe ; c'est
la mesure T3.1.

Ce qui est déjà acquis : l'ADC fait 12 bits, dont 9 à 9,5 effectifs (datasheet RP2350,
§12.4 et tableau 1685). Le défaut de linéarité du RP2040 (errata E11) est corrigé. Pour la
musique d'une Game Boy, c'est suffisant ; reste le bruit apporté par le montage.

> 🔑 **Avoir son propre Pico ne lève pas cet inconnu.** Sa radio est à quelques centimètres
> de son ADC, et c'est elle qui émet le plus. Le remède à un ADC bruité est un ADC éloigné de
> la radio, pas un Pico de plus.

**Le chemin par l'alimentation commune.** Les Pico de la console seront alimentés par la même
source. Chaque émission d'un voisin tire jusqu'à 250 mA en pointe : si ce courant passe par
les mêmes fils que celui du Pico du son, la chute de tension qu'il crée arrive à son entrée.
Deux précautions, dès le premier câblage, parce qu'elles ne coûtent presque rien :

- **un câblage en étoile** : chaque Pico a ses deux fils à lui, qui partent de l'alimentation.
  Le Pico du son n'est jamais branché à la suite d'un autre, et ses fils sont courts ;
- **un filtre à l'entrée du Pico du son** : une perle de ferrite en série sur `VSYS`, puis un
  condensateur de 100 µF et un de 10 µF vers la masse. La ferrite laisse passer le courant
  continu et freine les pointes ; les condensateurs fournissent les pointes du Pico lui-même
  sans les demander aux fils.

Les remèdes, du moins cher au plus cher, chacun suivi d'une nouvelle mesure T3.1 :

1. **Forcer le régulateur du Pico en PWM** par la broche `WL_GPIO1` du CYW43439 : la
   datasheet du Pico 2 W (§3.3) le recommande pour réduire l'ondulation vue par l'ADC.
2. **Améliorer la référence** : la même section propose une référence shunt de 3,0 V
   (LM4040) sur `ADC_VREF`.
3. **Moyenner davantage** : suréchantillonner plus haut, au prix de la marge de l'ADC.
4. **Une alimentation à part pour le Pico du son**, un simple bloc USB : plus aucun courant
   des voisins ne passe par ses fils. Les masses restent reliées par la console.
5. **Un ADC externe I2S**, par exemple un module PCM1808 : la conversion se fait sur sa
   propre carte, avec sa propre alimentation analogique. Trois fils de données et une
   horloge, lus par un PIO. C'est le seul cas où il faudrait souder davantage, et ce serait
   côté Pico : rien ne change dans la console.

---

## 3. Ce qu'on capture

**Le signal analogique, pas les registres.** La spec initiale envisageait, en version
« puriste », de transmettre les écritures dans les registres du son et de resynthétiser
l'audio au récepteur. Sur une vraie console, ces registres sont dans la puce du processeur :
rien ne garantit que leurs écritures sortent sur le bus de la cartouche, et la resynthèse
demanderait un APU fidèle jusque dans ses bizarreries. Le signal analogique, lui, est le son
réel, défauts compris.

**À l'entrée du potentiomètre de volume.** Le niveau y est indépendant de la molette : la
console peut se taire sans couper le flux. On évite aussi le bruit de l'amplificateur. Le
détail, le schéma et les candidats sont dans [`prise-son-mgb.md`](prise-son-mgb.md).

---

## 4. La chaîne

```
 console                Pico source                                         réseau
 VR1 G, D ─[10 kΩ]─► étage d'entrée ─► ADC0, ADC1 ─DMA─► anneau ─► décimation ÷4 ─► blocs de 5 ms ─► SON1
                     couplage, 1,65 V   192 kHz par voie             cœur 1            48 kHz, 16 bits

 Pico haut-parleur
 SON1 ─► réserve ─► rééchantillonnage ─► I2S (PIO) ─► MAX98357A ─► haut-parleur
```

**Pourquoi suréchantillonner.** Les ondes carrées de la Game Boy ont des harmoniques bien
au-delà de 20 kHz. Échantillonné directement à 48 kHz, tout ce qui dépasse 24 kHz se
replierait dans la bande audible, et un filtre RC n'y retranche que quelques décibels.
Échantillonné à 192 kHz, le filtre analogique n'a plus qu'à éteindre ce qui dépasse
170 kHz environ, et le filtre numérique de la décimation fait le reste. La moyenne de quatre
échantillons gagne en prime jusqu'à 6 dB de bruit. 2 voies × 192 kHz = 384 kS/s, soit une
conversion toutes les 125 périodes de l'horloge de 48 MHz : la cadence tombe juste.

**Pourquoi 16 bits, stéréo, 48 kHz.** Après décimation, l'ADC livre une dizaine de bits
effectifs ; 16 bits est le format que comprennent sans conversion un PC, un fichier WAV et un
convertisseur I2S. La console produit de la stéréo, pour son casque : la garder ne coûte
rien à la source, et le haut-parleur, mono, mélange les deux voies. 48 kHz est la fréquence
native des cartes son et des convertisseurs I2S ; 32 kHz économiserait 0,5 Mbit/s dont
personne n'a besoin.

**Pourquoi des blocs de 5 ms.** Chaque milliseconde de bloc est une milliseconde de
latence, ce qui pousse à les raccourcir. Mais le module écran a mesuré, le 18/09/2026, sur
un lien dégradé, que le nombre de paquets compte plus que le débit : 5 ms donnent
200 paquets par seconde, moins que la vidéo.

---

## 5. Le réseau

### 5.1 Le réseau son

**La source émet son propre réseau**, `gb-son`, dont elle est le point d'accès. Les
haut-parleurs s'y connectent en direct : le son ne traverse l'air qu'une fois, et ne passe
plus par la tête de l'écran. La question des deux sauts, T4.1 de la version précédente, ne se
pose plus.

| | |
|---|---|
| Source | 192.168.5.1 |
| Haut-parleurs | 192.168.5.2 à .5, adresses fixes : le numéro du haut-parleur, de 1 à 4, réglé dans son firmware |
| PC de mise au point | par DHCP, de .16 à .23 ; il prend l'une des 4 places |
| Canal | 1, proposé ; le banc R1 du [réseau](../../reseau.md) le confirme |

Le reste, commun à tous les modules (canaux, adresses, bus de la console), est dans
[`reseau.md`](../../reseau.md).

### 5.2 Les destinations

**Chaque haut-parleur se signale** : un battement deux fois par seconde. La source tient une
table de 4 haut-parleurs au plus, envoie **une copie de chaque bloc à chacun**, et oublie celui
qu'elle n'entend plus depuis 2 s. Sans haut-parleur, elle n'envoie rien. Elle répond à l'adresse
et au port d'où vient le battement.

**La diffusion générale (broadcast) est écartée** : sur WiFi, elle part sans accusé ni
répétition, au débit de base, et 1,6 Mbit/s y occuperaient l'antenne en perdant des paquets.
Quatre copies coûtent 6,2 Mbit/s, chacune fiable.

**Volume, marche et arrêt** viennent de l'injecteur du module manette, par le bus filaire de
la console ([réseau](../../reseau.md) §5). La source les transmet aux haut-parleurs dans
`SON1`.

### 5.3 Le protocole `SON1` — proposition

Mêmes conventions que `PXL1` : champs en little-endian, nombre magique en tête.

| Octet | Taille | Champ |
|---|---|---|
| 0 | 4 | nombre magique `'S','O','N','1'` |
| 4 | 1 | type : 0 = bloc audio ; 1 = accusé, pour mesurer l'aller-retour comme `PXL1_TYPE_PING` ; 2 = battement, du haut-parleur vers la source ; 3 = veille, de la source vers les haut-parleurs, deux fois par seconde quand la console est éteinte : ils coupent leur amplificateur |
| 5 | 1 | format : 0 = PCM 16 bits signé |
| 6 | 1 | nombre de voies : 2 |
| 7 | 1 | volume, de 0 à 255, reçu de l'injecteur ; le haut-parleur l'applique |
| 8 | 4 | fréquence d'échantillonnage, en Hz |
| 12 | 4 | numéro du premier échantillon du bloc, roulant |
| 16 | — | échantillons entrelacés G, D : 960 octets pour 5 ms |

**L'en-tête porte tout le format**, à chaque paquet : un récepteur redémarré reprend dès le
premier paquet reçu. C'est la leçon de `PXL1`, qui a dû ajouter une commande de géométrie
renvoyée toutes les 2 secondes, quand `PXL2` la porte dans chaque en-tête. **Le numéro
d'échantillon** rend une perte visible, un trou dans la numérotation, et place chaque bloc
dans le tampon même s'il arrive dans le désordre. Il fait le tour en 24,8 heures à 48 kHz.

Le fichier [`firmware/commun/son1.h`](../firmware/commun/son1.h) fait foi.

---

## 6. Les horloges

La source échantillonne au rythme de son quartz, le haut-parleur joue au rythme du sien.
Deux quartz indépendants diffèrent de quelques dizaines de millionièmes (ppm). À 50 ppm, l'écart est de
2,4 échantillons par seconde : un tampon de 40 ms, rempli à moitié, se vide ou déborde en
moins de 7 minutes, et le son craque à chaque fois.

**Le haut-parleur rattrape la dérive.** Il mesure le remplissage de son tampon et ajuste en
continu un rapport de rééchantillonnage, par interpolation. Pour un Cortex-M33, c'est
négligeable. Jeter ou doubler un échantillon de temps en temps serait plus simple, mais
s'entendrait sur une note tenue. T5.2 mesure l'écart réel et vérifie que le tampon reste
borné sur deux heures.

**Écrit et éprouvé sur PC**, avant tout matériel :
[`firmware/commun/rattrapage.hpp`](../firmware/commun/rattrapage.hpp) et son test. Une simulation
fait vivre au haut-parleur des dérives de −500 à +500 ppm, une gigue du réseau de 0 à 20 ms,
des pertes, une coupure et un redémarrage de la source. Sur deux heures simulées à +100 ppm, la
réserve reste entre 23 et 52 ms pour une cible de 40 ; la dérive est estimée à 3 ppm près ;
aucun claquement. Les trous sont masqués : le dernier son s'éteint en 4 ms, et le retour se fait
par un fondu de 2 ms.

L'horloge de la console n'intervient pas : on échantillonne un signal analogique.

---

## 7. Latence, et accord avec l'image

| Étape | Durée |
|---|---|
| Remplissage d'un bloc | 5 ms |
| Décimation | < 0,5 ms |
| Émission et une traversée radio | quelques ms (aller-retour du sniffer en direct sur un point d'accès : 1,85 ms en moyenne) ; des pointes possibles à plusieurs dizaines |
| Réserve du haut-parleur, qui absorbe les retards du WiFi | 20 à 50 ms, dimensionnée par T4.2 sur les pointes mesurées |
| Sortie I2S | ≈ 2 ms |
| **Total** | **≈ 30 à 60 ms** |

L'image arrive sur l'écran LED en 10 à 25 ms, estimation à confirmer : la latence de la v2
de l'écran reste à mesurer. Le son suivra avec quelques dizaines de
millisecondes de retard. Un son en retard sur l'image ne se remarque qu'au-delà de 125 ms
environ (recommandation UIT-R BT.1359). T5.4 mesure le décalage réel.

Le haut-parleur d'origine, lui, sonne sans retard. S'il reste audible dans la pièce, il fera
écho : molette à zéro, puisque la prise est en amont du volume.

---

## 8. Le haut-parleur

- **Pico 2 W et module MAX98357A** : convertisseur et amplificateur en classe D, piloté en
  I2S par trois fils qu'un PIO produit. Le MAX98357A mélange lui-même les deux voies,
  (G + D) / 2, selon la tension de sa broche `SD_MODE`.
- **Le haut-parleur d'une Game Boy** : une pièce de rechange, ou celui de la console. Sa
  puissance admissible est inconnue, alors que le MAX98357A délivre plusieurs watts : gain
  du module au minimum, plafond de volume dans le firmware, réglé en T5.1.
- **Alimentation USB 5 V**, sans rien de commun avec la console.
- **Générique** : il joue tout flux `SON1`, d'où qu'il vienne. Un PC peut l'alimenter avec
  `son_envoi.py`.

---

## 9. Budget chiffré

| Grandeur | Valeur | D'où elle vient |
|---|---|---|
| Échantillonnage de l'ADC | 192 kHz par voie, 384 kS/s | 48 MHz / 125 ; 77 % des 500 kS/s |
| Sortie | 48 kHz, 2 voies, 16 bits | décimation par 4 |
| Bloc | 5 ms : 240 échantillons par voie, 960 octets | + 16 octets d'en-tête |
| Paquets | 200 par seconde | |
| Débit | 1,56 Mbit/s | 976 × 200 × 8 |
| Sur l'antenne de la source | 1,56 Mbit/s par haut-parleur, 6,2 pour quatre | une copie par haut-parleur ; l'image est sur un autre réseau |
| Décimation | 2,1 M multiplications par seconde | deux étages demi-bande, 22 multiplications par échantillon et par voie ; ce qui se replierait est atténué de 72 dB. Environ 20 % d'un cœur à 150 MHz, estimé sur le code compilé ([`decimation.hpp`](../firmware/commun/decimation.hpp)) |
| RAM, source | ≈ 25 ko | anneau du DMA de 20 ms, file de 8 blocs |

---

## 10. Arborescence prévue

```
son/
├── README.md
├── docs/
│   ├── plan.md                    ← ce document
│   ├── plan-de-tests.md
│   ├── prise-son-mgb.md           ← où souder ; la fiche de la phase 0
│   └── releves/                   ← enregistrements, spectres, photos
├── firmware/
│   ├── mesure/                    ← le banc de la phase 0 : il mesure et rend compte ✅
│   ├── commun/                    ← partagé par la source, le haut-parleur et les tests sur PC ✅
│   │   ├── son1.h                 ← le protocole, qui fait foi
│   │   ├── decimation.hpp         ← 192 → 48 kHz, en virgule fixe
│   │   ├── rattrapage.hpp         ← la réserve du haut-parleur, et la dérive des horloges
│   │   └── test/                  ← leurs tests, sur PC
│   ├── source/                    ← capture, point d'accès du réseau son, émission
│   └── hautparleur/               ← réception, rééchantillonnage, I2S
└── tools/
    ├── mesure_son.py              ← le verdict du banc : force du son, Rb, côté, avant / après ✅
    ├── console.py, flash.sh       ← dialogue avec les firmwares ✅
    ├── sondump.py                 ← l'enregistrement du Pico, en WAV (phase 2)
    ├── analyse_son.py             ← niveaux, bruit de fond, sifflements ; spectre d'un WAV
    ├── hautparleur_virtuel.py     ← récepteur SON1 de référence : pertes, retards, réserve conseillée, dérive ✅
    ├── son_envoi.py               ← émetteur SON1 de test : sinus, balayage, WAV ; pertes, gigue, dérive simulées ✅
    └── decimation_conception.py   ← le calcul et la justification du filtre de décimation ✅
```

Seuls les éléments marqués ✅ sont écrits : les autres naîtront avec la phase qui en a besoin.

---

## 11. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| A1 | Raspberry Pi Pico 2 W | 2 | la source : le Pico du banc, qui reste en place ; un par haut-parleur, jusqu'à 4 |
| A2 | Résistance 10 kΩ | 2 | Rs, dans la console |
| A3 | Condensateur 1 µF, film | 4 | couplage : 2 pour le banc, 2 pour la version définitive |
| A4 | Résistances 100 kΩ, 15 kΩ et 10 kΩ | 4, 2 et 4 | le pont de VBIAS, Rb selon T0.4, et le Rs et le Rb du banc |
| A5 | Condensateurs 820 pF et 1,5 nF, C0G | 2 de chaque | Caa selon Rb |
| A6 | Condensateur 10 µF | 2 | découplage de VBIAS, banc et version définitive |
| A7 | Connecteur JST-SH 3 points, avec embase | 1 paire | séparé du connecteur vidéo |
| A8 | Câble blindé fin à 2 conducteurs, ou fil Kynar à torsader | 30 cm | |
| A9 | Module MAX98357A | 1 | |
| A10 | Haut-parleur de Game Boy | 1 | pièce de rechange, ou celui de la console |
| A11 | Plaquette à trous | 1 | étage d'entrée, côté Pico |
| A12 | Plaque d'essai et fils de liaison | 1 | le banc de la phase 0 |
| T1 | Multimètre | 1 | déjà là |
| T2 | Écouteurs ou casque | 1 | pour écouter la console ; rien d'autre ne se branche sur sa prise casque |
| B1 | Module ADC I2S PCM1808 | 0 ou 1 | seulement si T3.1 résiste aux autres remèdes |
| B2 | Amplificateur opérationnel MCP6002 | 0 ou 1 | seulement si le signal est trop faible (T0.4) |
| B3 | Référence de tension LM4040, 3,0 V | 0 ou 1 | remède 2 du §2.2 |

---

## 12. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| La radio s'entend dans le son | 🟡 plausible | T3.1, puis les remèdes du §2.2 dans l'ordre ; l'ADC externe en dernier recours |
| Une entrée de l'ADC portée au-delà de 3,3 V | 🔴 | couplage capacitif, diviseur, et Rs qui limite le courant ([`prise-son-mgb.md`](prise-son-mgb.md) §3) |
| Le manchon du casque relié à une masse | 🔴 | on n'enregistre jamais la prise casque ; références prises aux points de prise |
| Arracher une pastille, une patte | 🔴 | colle dès la sortie des fils, connecteur débrochable, fil souple |
| Le son de la console change | 🟡 | Rs de 10 kΩ ; T0.5 avant soudure, T1.2 Pico alimenté et éteint |
| La console bourdonne d'elle-même | 🟡 | T0.4 le mesure d'abord ; on le documente, on ne le corrige pas |
| Les radios de la console se gênent : le réseau son et le réseau image, à quelques centimètres | 🟡 | le banc R1 du [réseau](../../reseau.md) ; T4.4 dos à dos |
| Plusieurs haut-parleurs dans une pièce font écho | 🟢 plus tard | [réseau](../../reseau.md) §8 |
| Dérive des horloges : un craquement toutes les quelques minutes | 🟢 certaine, et connue | rééchantillonnage piloté par le tampon (§6), T5.2 |
| Griller le haut-parleur de Game Boy | 🟡 | gain minimal, plafond logiciel, T5.1 |
| L'horloge pixel couplée dans le câble audio | 🟡 | câble et connecteur séparés, masse dédiée, filtre anti-repliement |

---

## 13. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 11/10/2026 | Module ouvert, distinct de capture et d'écran | Même découpage que l'image : une source, un récepteur, un protocole entre les deux. Chaque moitié s'éprouve seule |
| 11/10/2026 | Capture du signal analogique, pas des écritures dans les registres du son | Les registres sont internes au processeur ; le signal analogique est le son réel |
| 11/10/2026 | Prise proposée à l'entrée du potentiomètre de volume, d'après le schéma de Gekkio | En amont de la molette et de l'amplificateur, sans continu, processeur protégé derrière 1 µF et 510 Ω. À confirmer par la phase 0 |
| 11/10/2026 | Pico de banc pour les phases 0 à 2 ; la phase 3 décide du Pico définitif | Le calcul et le débit tiennent sur un Pico ; le bruit de la radio ne se calcule pas, et un second Pico ne le lèverait pas |
| 11/10/2026 | **Décision de Yoann** : `SON1` gagne un battement du haut-parleur (type 2), une veille de la source (type 3) et le volume (octet 7) | Les haut-parleurs se signalent à la source, coupent leur amplificateur console éteinte, et suivent la molette de la manette |
| 11/10/2026 | **Décision de Yoann** : câblage en étoile et filtre d'entrée pour le Pico du son, vérifiés par T3.1 ; une alimentation à part en remède | Le bruit des radios voisines peut arriver par une alimentation commune |
| 11/10/2026 | **Décision de Yoann** : un Pico pour le son seul, point d'accès de son propre réseau ; jusqu'à 4 haut-parleurs en direct | Un microcontrôleur par type de périphérique sur la console, des liaisons directes ([réseau](../../reseau.md)). Remplace la recommandation d'un seul Pico pour l'image et le son, et le relais par la tête de l'écran vers 192.168.4.2. Le bruit de la radio dans l'ADC reste à mesurer : T3.1 |
| 11/10/2026 | Suréchantillonnage à 192 kHz, sortie à 48 kHz | Les harmoniques des ondes carrées se replieraient dans la bande audible à 48 kHz direct |
| 11/10/2026 | **La phase 0 se mesure avec un Pico de banc**, pas avec une interface audio | Rien à acheter, aucune connaissance en mesure audio : le firmware `mesure` et le script `mesure_son.py` donnent le verdict en clair, et le même montage resert aux phases 1 à 3. Le banc a un gain de 0,5 : il lit jusqu'à 6,6 V crête-à-crête sans rien savoir de la console d'avance |
| 11/10/2026 | **Gauche et droite : non bloquant**, le schéma fait foi d'ici là | Le haut-parleur de Game Boy est mono ; une inversion se corrige dans le firmware. Pokémon Rouge, vérifié dans son désassemblage, n'a pas de son d'un seul côté exploitable : le script `cotes` cherche ces moments en jouant |
