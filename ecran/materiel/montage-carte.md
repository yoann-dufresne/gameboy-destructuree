# Monter la carte de l'écran, et l'éprouver à chaque étape

*Rédigé le 05/11/2026, à la réception des cartes de JLCPCB.* Cinq circuits imprimés, dont deux
portent déjà leurs 93 CMS. Il reste les traversants à souder à la main ([nomenclature](carte/fab/nomenclature.md)).
Ce document dit dans quel ordre les souder, et comment savoir à chaque étape que tout va bien
avant de passer à la suivante. La mise en service avec l'alimentation 30 A et les dalles reste
au [§7 du dossier de montage](README.md#7-mise-en-service) : elle commence là où celui-ci
s'arrête.

**Le principe.** Un bloc à la fois, éprouvé avant le suivant. Le premier exemplaire d'un bloc
répété — support de nœud, embase HUB75 — entièrement validé avant de souder les deux autres :
une erreur de sens ou de méthode ne se commet qu'une fois. Les Pico servent d'instruments
dès qu'ils sont enfichés : le firmware [`recette-carte`](../firmware/recette-carte/README.md)
contrôle leur support, puis fait porter à chaque piste une signature que l'autre bout
reconnaît, ou qu'un multimètre lit.

| Étape | On soude | On vérifie |
|---|---|---|
| [0](#étape-0--avant-le-fer--inspection-et-ohmmètre) | rien | les CMS de JLCPCB : sens, ponts, courts-circuits |
| [1](#étape-1--lentrée-j1-et-la-première-mise-sous-tension) | J1 | le 5 V et les rails des Pico, à l'alimentation de labo |
| [2](#étape-2--la-tête--u1-sw1-j7-j3-j2) | U1, SW1, J7, J3, J2 | la tête et son support ; les pistes vers les trois nœuds, supports encore vides |
| [3](#étape-3--le-nœud-0--u2-sw2-jp1-j4) | U2, SW2, JP1, J4 | le nœud 0 : liaison depuis la tête, tampons, embase HUB75 |
| [4](#étape-4--les-nœuds-1-et-2) | U3, U4, SW3, SW4, JP2, JP3, J5, J6 | les deux autres nœuds, de la même façon |
| [5](#étape-5--la-distribution--f1f9-j11j19-c1-c2) | F1–F9, J11–J19, C1, C2 | les neuf départs, leurs voyants, les condensateurs |
| [6](#étape-6--les-vrais-firmwares-puis-les-dalles) | — | les firmwares de l'écran, une première dalle, puis le §7 |

C1 et C2, les plus hauts, viennent en dernier : la carte reste plate et facile à retourner,
et l'alimentation de labo n'en a pas besoin.

---

## Ce qu'il faut

| | |
|---|---|
| Fer, flux, tresse, une loupe | les 74AHCT541 sont au pas de 0,65 mm : c'est à la loupe qu'on voit un pont |
| Multimètre | tension continue, ohmmètre, test de diode |
| **Alimentation de laboratoire** | réglée à **5,00 V**, limitée en courant. Tout ce document se fait avec elle. L'alimentation 30 A n'arrive qu'au §7 : un court-circuit sur une carte neuve, derrière 30 A, brûle une piste avant qu'on ait coupé |
| Deux fils avec embouts | de l'alimentation de labo à J1 |
| Une résistance de 1 kΩ, ¼ W | serrée dans une fiche MSTB : la « charge d'essai » des départs (étape 5) |
| Les 4 Pico, barrettes mâles soudées | la tête (Pico 2 W) et les trois nœuds (Pico 2) ; 3 cavaliers |
| PC, câble micro-USB | WSL avec le pico-sdk ([`installer.sh`](../../installer.sh)) |

**La seconde carte assemblée reste de côté.** Si une mesure surprend, la même mesure sur elle
dit si c'est la carte ou le montage.

## Le firmware de recette

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cd ecran/firmware/recette-carte
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`recette_tete.uf2` va sur le Pico 2 W, `recette_noeud.uf2` sur chacun des trois Pico 2. On les
flashe **hors de la carte**, par l'USB : `../../tools/flash.sh build/recette_noeud.uf2`, ou
BOOTSEL et glisser-déposer sur le lecteur `RP2350`. Chacun vérifie au démarrage qu'il est dans
le bon support ; sinon il ne pilote rien et le dit sur sa console.

**La console** : `../../tools/console.py /dev/ttyACM0`. Sous WSL, garder le même port USB du PC
d'un Pico à l'autre : `usbipd` le rattache seul (`--auto-attach`). Côté Windows, PuTTY sur le
port COM fait aussi l'affaire. Un Pico alimenté par le bus garde son USB branchable : aucun des
deux n'alimente l'autre. On peut donc lire chaque Pico à son tour avec un seul câble.

**Quatre règles, de l'étape 2 à la fin :**

- un Pico ne s'enfiche ou ne se retire que **bus éteint** ;
- **sens du Pico** : nœuds, USB vers la droite ; tête, USB vers le haut — comme la flèche de
  la sérigraphie. Retourné, son VSYS tomberait sur un GPIO ;
- dès que deux Pico sont sur la carte, **le bus reste allumé** pendant qu'on en lit un par
  l'USB : un Pico sur son USB seul alimenterait les autres par les liaisons ;
- l'alimentation de labo seule sur J1, jamais avec une autre.

## Repérer les broches d'un support de Pico

Les supports suivent le brochage du Pico. Repères utiles : **39 = VSYS** (le 5 V qui alimente
le Pico), **38 = masse**, **36 = 3V3**, **30 = RUN** (le bouton RESET), 40 = VBUS, non reliée.

```
Support de nœud (U2 à U4), vu de dessus, USB vers la droite :

    20 19 18 …  5  4  3  2  1    ← rangée du haut ; 1 au bout, côté USB
    21 22 23 … 36 37 38 39 40    ← rangée du bas ; 39 = VSYS, 38 = masse, 36 = 3V3

Support de la tête (U1), vu de dessus, USB vers le haut :

     1 40          ← 1 en haut à gauche ; 40 en haut à droite
     2 39          ← 39 = VSYS
     3 38          ← masse
     4 37
     5 36          ← 3V3
     …  …
    20 21
```

---

## Étape 0 — Avant le fer : inspection et ohmmètre

**À l'œil, carte telle que reçue :**

- chaque CMS polarisé dans le sens de [`carte/fab/orientation_cms.png`](carte/fab/orientation_cms.png) :
  le repère physique (bande, point, patte seule) tombe sur le rouge. Bande de D1 à droite ;
  bande de la SS14 D2 à gauche, de D3, D4, D5 à droite ; point des six 74AHCT541 en bas à
  droite, contre le ◁ de la sérigraphie ; + de C4 ; patte seule des 2N7002 ;
- **les six 74AHCT541 à la loupe** : aucun pont entre deux pattes, toutes mouillées ;
- rien de manquant ni de dressé sur une patte (« tombstone »), surtout parmi les 0603.

**À l'ohmmètre**, sur les trous de J1 : **+5 V = trous 1 et 2** (le carré, à gauche), **masse
= trous 3 et 4**.

| Mesure | Attendu | Sinon |
|---|---|---|
| Ω, rouge sur +5 V, noir sur la masse | monte, puis se stabilise vers **20 kΩ** (R2 + R3) | moins de 100 Ω : court-circuit, à trouver avant tout |
| Diode, rouge sur la masse, noir sur +5 V | **0,5 à 0,7 V** : D1 et les tampons en direct | 0 V : court-circuit |
| Diode, rouge sur +5 V, noir sur la **broche 39** de chacun des quatre supports | **0,15 à 0,35 V** : F10, FB1 et la SS14 de ce support | OL : SS14 tournée ou absente (D2 tête, D3 nœud 0, D4 nœud 1, D5 nœud 2) ; OL partout : F10 ou FB1 |
| Ω, **broche 36** des supports U2, U3, U4 → masse | **≈ 0,9 à 1 kΩ** : R41, R51, R61 | OL : la résistance du support |
| Ω, broche 36 de U1 → masse | OL | |

---

## Étape 1 — L'entrée J1, et la première mise sous tension

**Souder J1**, entrées des fils vers le bord de la carte, comme dessiné sur la sérigraphie.
Rien d'autre.

**Mettre sous tension** : alimentation de labo à 5,00 V, **limitée à 100 mA**, sortie coupée ;
+ sur une borne de gauche de J1, − sur une borne de droite (« J1 : + + − − »). Allumer.

| Point | Attendu | Sinon |
|---|---|---|
| Courant | **quelques mA** : LED1 en prend ≈ 1,5 | l'alimentation passe en limitation : couper. Une pièce tournée chauffe (74AHCT541, D1, C4) : la chercher du doigt, à 100 mA c'est sans danger |
| LED1, verte | allumée | LED1 ou R1 |
| Broche 39 de chaque support (VSYS) | **4,8 à 5,0 V** | 0 V : la SS14 de ce support |
| Broche 36 de chaque support (3V3) | 0 V | |
| U1, broche 32 (GP27, mesure du bus) | **2,5 V** | R2 ou R3 |
| J4, J5, J6, **trou 15** (/OE vers la dalle) | **5,0 V** : R42, R52, R62 tiennent la dalle éteinte sans Pico | |
| J4, J5, J6, autres trous | ≈ 0 V : tampons en haute impédance | |

Une minute plus tard, au doigt : rien de tiède (D1, F10, FB1, les 74AHCT541).

---

## Étape 2 — La tête : U1, SW1, J7, J3, J2

**Souder**, du plus bas au plus haut :

1. **SW1**, le RESET de la tête ;
2. **le support U1** : deux barrettes femelles 1 × 20, enfichées sur le Pico 2 W qui sert de
   gabarit. Deux pattes en diagonale de chaque barrette, Pico en place, pour l'alignement ;
   retirer le Pico, souder le reste. Carte calée à plat pendant ce temps ;
3. **J7** (2 × 12), **J3** (1 × 8), **J2** (1 × 3), barrettes mâles, côté court dans la carte.

Nettoyer le flux à l'alcool isopropylique.

**Avant d'enficher**, bus éteint : broche 30 de U1 → masse, **OL** ; bouton SW1 enfoncé,
**0 Ω**. Bus allumé : broche 39, 4,8 à 5,0 V.

**Enficher la tête** : `recette_tete.uf2` flashé, bus éteint, Pico 2 W en U1, USB vers le haut.
Limite de l'alimentation à **300 mA**. Allumer, sans USB : le courant monte de 20 à 30 mA, et
J3.1 lit 3,3 V. Le Pico tourne sur le bus : VSYS et la masse du support sont soudés. Brancher
l'USB, lire la console :

```
── Recette de la carte · TÊTE (U1) · 6 s ──
Broches, 8 passes au démarrage :
  ✔ aucun court-circuit, aucune broche en défaut
  Supports des nœuds, d'après le CS de leur liaison : U2 vide, U3 vide, U4 vide
Bus +5 V, par R2/R3 sur GP27 : 5,00 V  ✔
J2.2 (GP26, potentiomètre) : …      (en l'air : une valeur quelconque)
Nœuds, d'après le code de leur RDY :
  L0 → U2   RDY0 100 %   aucun nœud ne répond : support vide, ou nœud sans le firmware de recette
  …
```

Ce que ce compte rendu prouve déjà : pas de pont sur les 40 soudures du support ; R33 à R35
en place (sinon « RDY*k* n'est pas tiré vers le 3,3 V ») ; pour chaque support de nœud, R45
et R41 et les pistes qui y mènent (« U2 vide » : le CS est tiré vers la masse à travers elles ;
« en l'air » sinon) ; R2, R3 et l'entrée analogique de la mesure du bus.

**Au multimètre**, la tête en marche :

| Où | Attendu |
|---|---|
| J7.1, VSYNC | ≈ 2,5 à 2,6 V |
| J7.3, J7.5, J7.7, RDY0 à RDY2 | 3,3 V : tirés haut, pas de nœud |
| J7.9, J7.11, GP17 et GP18 de la tête | ≈ 1,0 V et ≈ 2,0 V |
| J7.13 à J7.23 (impaires), J7 paires | ≈ 0 V pour l'instant ; 0 V, masses |
| J3.1 · J3.2 à J3.7 · J3.8 | 3,3 V · **≈ 0,4 V × numéro** : 0,79 1,19 1,58 2,01 2,41 2,81 V · 0 V |
| J2.1 · J2.3 | 3,3 V · 0 V |
| Cavalier sur J2.1–J2.2, puis J2.2–J2.3 | la console lit J2.2 à ≈ 3,3 V, puis 0 V |

**Les pistes vers les nœuds, supports encore vides.** Sur les trous des supports U2 à U4,
pointe noire à la masse (broche 38) :

| Broche du support | Signal | U2 (L0) | U3 (L1) | U4 (L2) |
|---|---|---|---|---|
| 25 | D0 | 0,33 V | 0,99 V | 1,65 V |
| 26 | D1 | 0,50 V | 1,16 V | 1,82 V |
| 27 | CLK | 0,66 V | 1,32 V | 1,98 V |
| 29 | CS | 0,83 V | 1,49 V | 2,15 V |
| 21 | VSYNC | ≈ 2,5 V | ≈ 2,5 V | ≈ 2,5 V |

Quinze mesures, et toutes les liaisons tête → nœuds sont vérifiées, réseaux RN1 à RN3 et R30
à R32 compris, avant de souder un seul support de nœud. Une valeur à 0 V : piste ou résistance
coupée ; la valeur d'une autre case : deux lignes croisées.

**RESET** : SW1 enfoncé, la console décroche, et le compteur de secondes repart de 2.

---

## Étape 3 — Le nœud 0 : U2, SW2, JP1, J4

Le premier nœud, validé en entier avant de souder les deux autres.

**Souder** :

1. **SW2** ;
2. **le support U2**, avec un Pico 2 en gabarit, comme à l'étape 2 ;
3. **JP1**, barrette 1 × 3 ;
4. **J4**, l'embase HUB75 : **l'encoche du détrompeur du côté du bord gauche de la carte**,
   comme dessinée sur la sérigraphie ; le ▷ marque la broche 1. Tournée de 180°, la nappe
   mélangerait tous les signaux.

**Avant d'enficher** : broche 30 de U2 → masse, OL, 0 Ω bouton enfoncé ; bus allumé, broche
39 à 4,8–5,0 V.

**Enficher le nœud 0** : `recette_noeud.uf2` flashé, bus éteint, Pico 2 en U2, USB vers la
droite ; **cavalier JP1 sur 1-2**, côté « GP1 ». Allumer, sans USB : la LED du Pico s'allume
pendant le contrôle des broches — il tourne sur le bus —, puis :

| LED du nœud | Sens |
|---|---|
| **fixe** | tout va bien : la liaison complète lui arrive |
| clignotement lent | rien d'anormal, mais pas de tête |
| clignotement rapide | un défaut : lire sa console |

**Console de la tête** : `L0 → U2   RDY0  20 %   ✔ nœud 0 : les 5 signaux de sa liaison lui
arrivent`. **Console du nœud** (déplacer l'USB, bus allumé) :

```
── Recette de la carte · NŒUD · 8 s ──
Broches, 8 passes au démarrage :
  ✔ aucun court-circuit, aucune broche en défaut
  · non essayées, un autre Pico les pilotait : GP16 GP19 GP20 GP21 GP22
Alimentation : VSYS 4,7… V, USB branché
Nappe HUB75 : broche n ≈ 0,3 V × n sous 5,0 V, 4 et 16 à 0 V ; cavalier JP en 2-3 : broche 2 ≈ 4,8 V
Sonde J7 : .17 ≈ 1,0 V, .18 ≈ 2,0 V
Liaison depuis la tête :
  GP19 D0     10 %   ✔ L0 D0
  GP20 D1     15 %   ✔ L0 D1
  GP21 CLK    20 %   ✔ L0 CLK
  GP22 CS     25 %   ✔ L0 CS
  GP16 VSYNC  80 %   ✔ VSYNC
⇒ nœud 0, support U2, rangée du haut : liaison complète. RDY renvoie 20 %.
```

Si la tête pilotait déjà les lignes de la liaison quand le nœud a démarré, elles ne passent
pas par le contrôle des broches (la ligne « non essayées ») : c'est leur code qui les éprouve,
et un pont entre deux d'entre elles se voit en « codes mêlés ».

**Au multimètre, la nappe HUB75 débranchée** : chaque broche de J4 porte **0,3 V × son
numéro** (bus à 5,0 V ; à 5,1 V, tout est 2 % plus haut). Pointe noire sur la broche 4 ou 16,
la masse ; pointe rouge, fine, sur chaque broche, dans l'embase.

| Broche | Signal | Attendu | GPIO (patte) | Tampon | Réseau 33 Ω |
|---|---|---|---|---|---|
| 1 | R1 | 0,30 V | GP0 (1) | A | 1er |
| 2 | G1 | 0,60 V | GP1 (2), par JP | A | 1er |
| 3 | B1 | 0,90 V | GP2 (4) | A | 1er |
| 4 | masse | 0 V | | | |
| 5 | R2 | 1,50 V | GP3 (5) | A | 1er |
| 6 | G2 | 1,80 V | GP4 (6) | A | 2e |
| 7 | B2 | 2,10 V | GP5 (7) | A | 2e |
| 8 | E | 2,40 V | GP10 (14) | B | 3e |
| 9 | A | 2,70 V | GP6 (9) | A | 2e |
| 10 | B | 3,00 V | GP7 (10) | A | 2e |
| 11 | C | 3,30 V | GP8 (11) | B | 3e |
| 12 | D | 3,60 V | GP9 (12) | B | 3e |
| 13 | CLK | 3,90 V | GP11 (15) | B | 3e |
| 14 | LAT | 4,20 V | GP12 (16) | B | 4e |
| 15 | /OE | 4,50 V | GP13 (17) | B | 4e |
| 16 | masse | 0 V | | | |

Tampons et réseaux de chaque nœud : nœud 0, tampon A = U5, B = U6, réseaux RN4 à RN7 ; nœud 1,
U7, U8, RN8 à RN11 ; nœud 2, U9, U10, RN12 à RN15.

- **Cavalier JP1 sur 2-3** : la broche 2 passe à **≈ 4,8 V** (GP15, le G1 de rechange). Le
  remettre sur 1-2. Sans cavalier, l'entrée du tampon est en l'air et la broche 2 lit n'importe
  quoi : il en faut toujours un.
- **J7** : J7.3 (RDY0) ≈ 0,66 V ; J7.13 (N0.17) ≈ 1,0 V ; J7.15 (N0.18) ≈ 2,0 V.
- **RESET** : SW2 enfoncé, la LED s'éteint ; elle redevient fixe quelques secondes après. La
  console de la tête montre, le temps d'un compte rendu, un code de passage sur RDY0.

Le nœud 0 est validé de bout en bout : support, liaison, transistor de validation Q1, deux
tampons, quatre réseaux, embase.

---

## Étape 4 — Les nœuds 1 et 2

Comme l'étape 3, nœud par nœud : **U3, SW3, JP2, J5**, puis **U4, SW4, JP3, J6**. Bus éteint
pour enficher, cavalier sur 1-2.

| | Nœud 1 (U3) | Nœud 2 (U4) |
|---|---|---|
| Console de la tête | `L1 → U3   RDY1  45 %   ✔ nœud 1` | `L2 → U4   RDY2  70 %   ✔ nœud 2` |
| Console du nœud | codes 30, 35, 40, 45 % et VSYNC 80 % | codes 50, 55, 60, 65 % et VSYNC 80 % |
| Embase | J5 : même tableau que J4 | J6 : même tableau |
| J7 | J7.5 (RDY1) ≈ 1,5 V ; J7.17, J7.19 ≈ 1,0 et 2,0 V | J7.7 (RDY2) ≈ 2,3 V ; J7.21, J7.23 ≈ 1,0 et 2,0 V |

Les trois LED de nœud fixes, et les trois lignes de la tête en ✔ : toute la logique de la
carte est éprouvée. Avec les quatre Pico, le courant est de l'ordre de 0,1 à 0,15 A.

---

## Étape 5 — La distribution : F1–F9, J11–J19, C1, C2

**Souder** les neuf porte-fusibles, puis les neuf embases de départ, ouverture vers le bord
droit de la carte (sur chaque départ, « − » en haut, « + ▷ » en bas), puis **C1 et C2 dans le
bon sens** : la patte + (la longue) dans la **pastille carrée, à droite**, à côté du « + » ;
la bande − du condensateur du côté de la **moitié blanche** de la sérigraphie.

**Mettre sous tension**, limite à 500 mA : un bref appel de courant (2 × 1000 µF à charger),
puis le courant revient à sa valeur de l'étape 4. S'il continue de monter, ou si C1 ou C2
tiédit : couper, il est à l'envers.

**Chaque départ D1 à D9**, l'un après l'autre :

1. fusible retiré, la fiche MSTB avec sa résistance de 1 kΩ dans l'embase : **le voyant rouge
   s'allume**, faiblement ;
2. fusible 7,5 A en place : **le voyant s'éteint**, et les vis de la fiche lisent **5,0 V**,
   + sur la borne du bas ;
3. retirer la fiche.

Le voyant rouge d'un départ veut dire : fusible fondu, dalle branchée. Celui qui reste éteint
à l'essai 1 : la LED, sa résistance (R11 à R19) ou l'embase.

---

## Étape 6 — Les vrais firmwares, puis les dalles

**Flasher en place**, bus allumé et USB branché, comme décrit dans leurs README :
[`noeud.uf2`](../firmware/noeud/README.md) avec `-DCHAIN_LEN=3` sur les trois nœuds,
[`tete.uf2`](../firmware/tete/README.md) sur la tête. Garder les `recette_*.uf2` sous la main :
en cas de panne, reflasher un Pico en place et lire sa console localise un défaut sans rien
démonter.

**Une première dalle, sur l'alimentation de labo** (si elle donne au moins 1 A) : bus éteint,
tête retirée, câble d'alimentation de la dalle 7 sur D7, nappe de J4 à l'entrée de la dalle 7,
fusible F7 en place. Limite à 1 A, allumer : sans tête, le nœud 0 affiche sa mire « pas de
liaison », un cadre bleu sombre et une diagonale. Il pilote une chaîne de trois dalles : la
dalle 7 seule n'en montre qu'un morceau, les dalles 7, 8 et 9 chaînées la montrent entière. La
mire tire peu ; si l'alimentation passe en limitation, rester à une dalle. Avec la tête en
place, les dalles restent noires tant qu'aucune source n'émet : c'est la commande `EFFACER`,
normale.

**Puis la mise en service complète** avec l'alimentation 30 A, réglée à 5,15 V, et les neuf
dalles : [§7 du dossier de montage](README.md#7-mise-en-service).

---

## Annexe — Dépannage

| Symptôme | Cause probable | Où regarder |
|---|---|---|
| Courant fort dès l'étape 1, l'alimentation en limitation | pièce tournée ou pont | 74AHCT541 tourné (VCC et masse inversés), D1 tournée, pont sous un TSSOP ; la pièce fautive chauffe |
| Pas de VSYS sur un support | SS14 de ce support | D2 tête, D3 nœud 0, D4 nœud 1, D5 nœud 2 |
| Un Pico démarre sur son USB, pas sur le bus seul | VSYS ou masse du support | soudures des pattes 38 et 39 ; la SS14 du support |
| Un Pico ne démarre pas du tout, même sur l'USB | RUN tenu à la masse | patte 30 : bouton RESET, pont de soudure |
| Un Pico redémarre en boucle | pont entre RUN et une voisine | patte 30 avec la 29 (GP22), ou avec la 31 (GP26) sur un nœud : le contrôle des broches remet RUN à 0 |
| Console : « GPa et GPb reliées » | pont de soudure | entre les pattes indiquées, ou sous le tampon |
| Console du nœud : « pas de signal » sur une ligne | coupure | la console nomme la patte de la tête, le réseau (RN1 à RN3, ou R30 à R32) et la patte du support |
| Console du nœud : « reçoit L*k* … » | lignes croisées | erreur de routage : comparer avec le tableau de l'étape 2 |
| Console de la tête : « ne reconnaît pas sa liaison » (RDY 90 %) | le nœud ne voit aucun code | lire la console du nœud ; refaire les mesures de l'étape 2 sur son support |
| Embase : toutes les broches à 0 V, la 15 comprise | tampons sans alimentation | bus éteint, Pico sur son USB seul : les tampons ne vivent que du bus |
| Embase : broche 15 à 5 V, toutes les autres à 0 V | tampons coupés | Q1, Q2 ou Q3, que le 3V3 du Pico commande : le transistor, ou la patte 36 du support |
| Embase : seules les broches 1, 2, 3, 5, 6, 7, 9, 10 mortes | tampon A | U5, U7 ou U9 : alimentation, soudures |
| Embase : seules les broches 8, 11 à 15 mortes | tampon B | U6, U8 ou U10 |
| Embase : une broche à 0 V | sortie coupée | son réseau 33 Ω, sa soudure sur l'embase |
| Embase : deux broches à la même valeur intermédiaire | pont entre deux sorties | entre ces deux pattes du tampon, du réseau ou de l'embase |
| Voyant rouge allumé, dalle branchée | fusible fondu | changer le fusible avant de rallumer ([§8](README.md#8-limites-et-points-à-vérifier)) |
