# Module CAPTURE — plan de réalisation

*Sous-projet « capture » du Game Boy Pocket déstructuré.*
Version 1 — 22/09/2026

**Le « pourquoi » est ici. Le « comment », étape par étape, est dans
[`etapes-detaillees.md`](etapes-detaillees.md).**

---

## 0. Décisions figées

| Sujet | Décision | Réversible ? |
|---|---|---|
| **Ce qu'on capture** | Le **bus LCD** de la MGB — 5 signaux prélevés, l'écran d'origine reste branché | non |
| **Contrôleur** | **1 × Raspberry Pi Pico 2 W** | non |
| **Interface électrique** | **74LVC244A** alimenté en 3,3 V + 100 Ω en série sur chaque prise | oui, mais à ne pas risquer |
| **Protocole de sortie** | **`PXL1`**, réutilisé tel quel — le module ÉCRAN ne change pas d'interface | non |
| **Format** | **`IDX2`**, le format natif de la Game Boy, déjà réservé dans le protocole | non |
| **Mise à l'échelle** | **aucune** — 160×144 posés en 1:1 dans les 192×192 de la grille | non |
| **Composition du cadre** | **sur le sniffer** : il émet un canevas 192×192 complet | oui |
| **Langage** | **C++20**, pico-sdk 2.x bare-metal, PIO + DMA, 2 cœurs | non |
| **`clk_sys`** | **150 MHz**, la valeur par défaut — pas les 266 MHz du module écran | oui |
| **Alimentation** | **séparée de la console**, masses communes | non |

> 🔑 **Le point qui structure tout** : la Game Boy produit déjà ses pixels sur **2 bits**, et
> `PXL1_FMT_IDX2` existe déjà dans le protocole depuis sa v1. La chaîne complète est donc
> **sans conversion de couleur et sans mise à l'échelle** : 2 bits sortent du PPU, 2 bits
> traversent le WiFi, 4 entrées de palette décident de la teinte à l'arrivée. Tout le
> problème de ce sous-projet est le **timing de capture**, pas le traitement d'image.

> ⚠️ **Ce qui n'est pas encore vrai** : `IDX2` est déclaré dans `pxl1.h` mais le firmware du
> module ÉCRAN ne décode aujourd'hui que `BGR888` et `IDX8`. C'est la phase 3, et elle porte
> pour partie sur l'autre dépôt.

---

## 1. Objectif et périmètre

**Ce qu'on construit :** un module qui se branche sur le bus LCD d'une Game Boy Pocket,
reconstitue chaque trame 160×144 en 2 bits par pixel, et l'émet en UDP `PXL1` vers les trois
nœuds du module ÉCRAN. La console continue de fonctionner normalement, son écran d'origine
compris : **on écoute, on ne pilote rien**.

**Ce qu'on ne construit pas :** le son, les boutons, l'habillage mécanique de l'ensemble, et
tout ce qui concerne l'affichage — le module ÉCRAN est terminé jusqu'à sa phase 4 et n'attend
de nous que des paquets conformes.

**Ce qui reste possible sans console :** l'émetteur `pixelpush` du module écran continue de
fonctionner. Si la soudure bloque, la chaîne d'affichage ne devient pas inutile — c'est
exactement pourquoi le protocole a été fait commun (cf. `../ecran/docs/plan-firmware.md` §1).

---

## 2. Ce que le module ÉCRAN nous donne déjà

Ce sous-projet ne part pas de zéro. Acquis, mesurés, publiés dans `../ecran/` :

| Acquis | Valeur | Conséquence pour nous |
|---|---|---|
| Protocole `PXL1` | en service depuis le 18/09/2026 | rien à concevoir : on écrit un émetteur, pas un protocole |
| Latence bout en bout | **~8 ms** | il nous reste ~8 ms de budget pour tenir sous une trame |
| Débit UDP encaissé par un Pico 2 W | **24,6 Mbit/s** | nos 1,5 Mbit/s par nœud sont hors sujet |
| Découpage en nœuds | 3 rangées de 192×64 | la trame GB est à cheval sur les trois — voir §5.3 |
| Rafraîchissement de la dalle | 788 Hz | ajoute 1,3 ms au pire, négligeable |
| Palette | transportée par paquet `CTRL`, hors du flux de pixels | les 4 teintes GB sont un réglage, pas une compilation |

Et trois pièges déjà payés, qu'on n'a pas à repayer :

1. **La console série.** `cat /dev/ttyACM0` ne donne rien : le firmware n'émet que si DTR est
   asserté. `tools/console.py` le fait.
2. **`CYW43_PIO_CLOCK_DIV_INT`.** La liaison SPI du WiFi dérive de `clk_sys`. Le module écran
   a dû la rediviser parce qu'il tourne à 266 MHz. **Nous restons à 150 MHz**, donc la valeur
   par défaut convient — et c'est une raison de plus de ne pas monter `clk_sys` sans motif.
3. **`pico_enable_stdio_uart`.** Sur le module écran, GP0/GP1 sont pris par R1/G1 ; chez nous
   ils sont pris par LD0/LD1. Même conclusion : **stdio sur USB, pas sur l'UART par défaut.**

---

## 3. Le signal à capturer

### 3.1 Les cinq signaux

Le CPU MGB pilote directement le panneau LCD — il n'y a pas de contrôleur intermédiaire, donc
pas de mémoire à lire : **l'image ne passe sur ce bus qu'une fois, en direct.** C'est ce qui
rend la capture temps réel obligatoire, et c'est aussi ce qui la rend simple.

| Signal | Rôle | Nous ? |
|---|---|---|
| **LD0, LD1** | les 2 bits de couleur du pixel (4 niveaux) | ✅ indispensable |
| **CPG** | horloge pixel — 160 impulsions par ligne visible | ✅ indispensable |
| **CPL** | verrou de fin de ligne | ✅ notre HSYNC |
| **ST / S** | impulsion de début de trame | ✅ notre VSYNC |
| CP / CPV | horloge de ligne, VBlank comprise | ⭕ HSYNC de secours |
| FR | inversion de polarité, alterne à chaque trame | ⭕ VSYNC de secours (÷2) |
| CLS | horloge du générateur de contraste | ❌ inutile |

**Cinq signaux plus une masse.** Une sixième prise est prévue au BOM pour `CP/CPV` si `CPL`
se révèle capricieux.

### 3.2 🔬 À MESURER — identifier les broches par leur fréquence

**N'assume aucun brochage.** Les noms et positions varient selon la révision de carte. Les
fréquences, elles, découlent du timing de la console (4,194304 MHz, 154 lignes de 456 cycles)
et ne mentent pas :

| Ce que tu mesures | C'est… |
|---|---|
| **59,73 Hz** | VSYNC `ST`/`S` — ou `FR` s'il alterne, donc **29,86 Hz** en niveau |
| **9,20 kHz** (154 × 59,73) | horloge de ligne `CP`/`CPV` |
| **8,60 kHz** (144 × 59,73) | verrou `CPL` — **absent pendant la VBlank**, c'est sa signature |
| **≈ 1,38 M impulsions/s en moyenne, en salves de 160** | horloge pixel `CPG`. Fréquence instantanée entre ~1,5 et 4,2 MHz — **mesure-la**, elle fixe le budget du PIO |
| 2 lignes qui portent des données pendant les salves et **se taisent en VBlank** | `LD0` et `LD1` |

**Test de validation** : lance un jeu, mets l'écran tout blanc (un menu), puis tout noir.
`LD0` et `LD1` doivent basculer ensemble d'un extrême à l'autre. C'est la preuve
d'identification — pas la fréquence seule.

> Note polarité : sur DMG/MGB, `00` = **blanc** et `11` = **noir** (la valeur pilote
> l'opacité du cristal liquide). Vérifie-le avec le test ci-dessus. L'inversion est un
> booléen dans le firmware — ou, mieux, un simple ordre des 4 entrées de palette.

### 3.3 🔬 À MESURER — la tension logique

**Mesure VCC sur la carte MGB avant de brancher quoi que ce soit.** Le RP2350 **n'est pas
tolérant 5 V** : une entrée à 5 V détruit le Pico.

| VCC mesuré | Interface |
|---|---|
| **≈ 2,4 – 3,3 V** (cas attendu sur MGB, 2×AAA) | 74LVC244 alimenté en 3,3 V. À piles usées (2,4 V) les niveaux deviennent marginaux : **le tampon est fortement conseillé**, pas optionnel |
| **≈ 5 V** (cas DMG, ou révision inattendue) | **74LVC244 obligatoire**, alimenté en **3,3 V** : entrées tolérantes 5 V, sorties 3,3 V propres |

Le 74LVC244A couvre les deux cas — c'est pour ça qu'il est au BOM par défaut. **Ne prends pas
de TXS0108 / TXB0108** : ces convertisseurs sont conçus pour de l'open-drain lent et se
comportent mal sur du push-pull à plusieurs MHz.

### 3.4 Où souder, et comment

**Carte constatée le 23/09/2026 : `MGB-ECPU-01` (© 1996).** Le ruban LCD n'y est **pas
soudé** : il arrive sur un **connecteur ZIF `P2` de 18 broches**, verrouillable, repères `1`
et `18` sérigraphiés. Pas d'estimation, c'est une observation photographique.

Par ordre de préférence :

1. **Les broches du connecteur `P2`** — c'est le point retenu. Leurs soudures sont exposées
   sur le bord du connecteur côté CPU, et l'écran d'origine reste branché : la console
   continue de fonctionner, ce qui est la condition pour déboguer.
2. **Les points du mod « bivert »** — le bivert (cf. `../Guide_Mods_Gameboy_Pocket.md` §1)
   consiste précisément à intercaler un inverseur sur `LD0` et `LD1`. Les tutos bivert pour
   MGB documentent donc déjà l'emplacement exact de **deux de nos cinq signaux**. Excellent
   point de départ, et une vérification croisée gratuite de l'identification du §3.2.
3. **Les broches du CPU MGB** — dernier recours. Le rang qui fait face à `P2` est celui des
   broches **41 à 64**, au pas de 0,65 mm : bien plus fin que `P2`.

> ℹ️ Le connecteur rend techniquement possible un **interposeur FFC 18 points** entre le ruban
> et `P2`, qui supprimerait toute soudure sur la console. **Écarté le 23/09/2026** : le projet
> est artistique et le geste de soudure fait partie de la démarche. La piste reste notée ici
> au cas où une autre console devrait être préservée intacte.

**Règles de câblage — ce sont elles qui décident si l'image d'origine survit :**

- Le PPU attaque un panneau LCD avec une capacité d'attaque **faible**. Toute charge parasite
  dégrade l'image d'origine. → **100 Ω en série** au départ de chaque prise, tampon en haute
  impédance (74LVC244, ~5 pF d'entrée), **fils < 10 cm** jusqu'au tampon.
- **Une masse par paquet de signaux**, torsadée avec eux. Une masse unique et lointaine, à
  4 MHz sur du fil volant, sonne.
- **Reprise d'effort obligatoire** : une goutte de colle chaude sur les six fils dès leur
  sortie de la carte. Un fil émaillé qui bouge arrache sa pastille — et la pastille ne
  revient pas.
- **Connecteur débrochable** (JST-SH ou barrette 6 pts) entre la console et le module : on
  voudra refermer la console, la déplacer, la débrancher.

### 3.5 Chemin de repli si la soudure fine bloque

- **Sniffer sur une DMG** (Game Boy d'origine) : broches plus larges, signaux 5 V, bien mieux
  documentées. On valide toute la chaîne, puis on porte sur MGB.
- **Retour à `pixelpush`** : le module écran fonctionne à l'identique, seule la source change.

---

## 4. Brochage du Pico 2 W

| GPIO | Signal | Sens | Note |
|---|---|---|---|
| **GP0** | LD0 | entrée | base du groupe `in pins` du PIO |
| **GP1** | LD1 | entrée | contigu à GP0 : `in pins, 2` en **une** instruction |
| **GP2** | CPG (horloge pixel) | entrée | `wait 1 gpio 2` dans le PIO |
| **GP3** | CPL (HSYNC) | entrée | 2ᵉ machine d'état, ou IRQ GPIO |
| **GP4** | ST / FR (VSYNC) | entrée | IRQ GPIO |
| GP5 | *(réserve)* CP/CPV | entrée | si `CPL` se révèle capricieux |
| GP16 / GP17 | UART0 TX / RX de debug | sortie/entrée | ⚠️ voir ci-dessous |
| GP20 | mesure — bascule à chaque VSYNC capturée | sortie | analyseur logique, phase 4 |
| GP21 | mesure — bascule à chaque trame émise | sortie | analyseur logique, phase 4 |
| GND (3, 8, 13, 18, 23, 28, 33, 38) | masse commune | — | relier **au moins deux** broches à la masse console |

> ⚠️ **Le piège** : l'UART de debug par défaut du SDK est sur **GP0/GP1**, occupés ici par
> LD0/LD1. Les `printf` écraseraient les entrées. On passe en **USB CDC**
> (`pico_enable_stdio_usb(1)` / `pico_enable_stdio_uart(0)`), comme le module écran l'a fait
> pour la raison symétrique. GP16/GP17 restent la porte de sortie si l'USB gêne.

**GP0, GP1, GP2 contigus n'est pas un confort, c'est la condition du PIO** : `in pins, 2`
échantillonne un groupe contigu commençant à `IN_BASE`. Si LD0 et LD1 atterrissent sur des
broches non contiguës, la boucle passe de 3 à 5 instructions et perd sa lisibilité. C'est
exactement l'erreur que le module écran a payée en phase 0 avec les adresses A–E.

---

## 5. Architecture firmware

### 5.1 La chaîne

```
 CPG ──► PIO SM0 : wait 0 / wait 1 / in pins,2   (autopush 32 bits = 16 pixels)
                        │
                        ▼
                    DMA ──► canevas 192×192 IDX2, directement à la bonne ligne
                        │
 CPL ──► PIO SM1 ───────┘   (ré-arme le DMA ligne par ligne)
 ST  ──► IRQ GPIO ──► bascule les canevas, réveille le cœur 1

 cœur 1 : lwIP + CYW43 ──► 3 × 3 paquets UDP PXL1/IDX2
```

- **PIO SM0** — boucle de 3 instructions : `wait 0 gpio 2` / `wait 1 gpio 2` / `in pins, 2`.
  À 150 MHz, une itération dure 20 ns ; l'horloge pixel culmine à ~4,2 MHz, soit 238 ns par
  pixel : **plus de 10× de marge.** Autopush à 32 bits = 16 pixels par mot, 10 mots par ligne.
- **DMA** — le FIFO du PIO alimente la mémoire sans le CPU. Le CPU **ne touche aucun pixel**.
- **IRQ VSYNC** — bascule les canevas, remet le DMA à zéro, réveille l'émission.
- **Cœur 1** — pile réseau (lwIP + CYW43). Séparer capture (cœur 0) et réseau (cœur 1) évite
  que le WiFi ne fasse rater un front d'horloge. C'est le même découpage que le module écran,
  pour la même raison.

### 5.2 Pas de tampon intermédiaire : le DMA écrit dans le canevas

Une ligne GB fait 160 px × 2 bits = **40 octets**. Le canevas fait 192 px × 2 bits =
**48 octets par ligne**. L'image est centrée : `x0 = (192−160)/2 = 16` pixels, soit
**exactement 4 octets** en `IDX2`.

> 🔑 L'offset horizontal tombe sur une frontière d'octet. Le DMA peut donc écrire les
> 40 octets d'une ligne **directement dans le canevas**, à 4 octets du bord. Il n'y a pas de
> framebuffer 160×144 à recopier ensuite vers un canevas 192×192 : **cette recopie n'existe
> pas.** Que `(192−160)/2` soit un multiple de 4 est une coïncidence heureuse — mais elle
> découle du choix 192×192 fait par le module écran, qui était déjà motivé par le 1:1.

Empreinte mémoire :

| | |
|---|---|
| Canevas 192×192 en `IDX2` | 9 216 octets |
| Double tampon | **18,4 ko** sur les 520 ko du RP2350 |
| Reste (pile réseau, pile lwIP, tampons UDP) | quelques dizaines de ko |

Aucune tension mémoire — au contraire du module écran, dont les plans de bits pèsent 370 ko
en 3×3.

### 5.3 Où va quoi : le découpage en nœuds

L'image GB est **centrée** dans la grille : `x0 = 16`, `y0 = (192−144)/2 = 24`. Elle occupe
donc les lignes 24 à 167 du canevas, et se trouve **à cheval sur les trois nœuds** :

| Nœud | Lignes du canevas | Lignes GB | Prêt à émettre après… |
|---|---|---|---|
| 0 | 0 – 63 | 0 – 39 (40 lignes) + 24 lignes de marge haute | ligne 40 ⇒ **4,3 ms** |
| 1 | 64 – 127 | 40 – 103 (64 lignes) | ligne 104 ⇒ **11,3 ms** |
| 2 | 128 – 191 | 104 – 143 (40 lignes) + 24 lignes de marge basse | ligne 144 ⇒ **15,7 ms** |

(une ligne GB dure 456 / 4 194 304 = **108,7 µs**.)

**L'émission est donc naturellement pipelinée** : on n'attend pas la fin de la trame pour
émettre, on envoie la part d'un nœud dès que ses lignes sont capturées. C'est le « envoi par
tranches » de la spec §5.2, mais découpé selon une frontière qui a déjà un sens — celle des
nœuds — plutôt qu'en tranches de 24 lignes arbitraires.

### 5.4 Émettre le cadre, ou ne pas l'émettre

Le sniffer capture 160×144 ; il émet 192×192. Les 13 824 pixels de marge (37,5 % du canevas)
sont émis à chaque trame alors qu'ils ne changent jamais.

| Option | Débit total | Paquets/trame | Verdict |
|---|---|---|---|
| **Canevas complet 192×192, `IDX2`** | **4,40 Mbit/s** (1,47 par nœud) | **9** | ✅ retenu |
| Sous-rectangle 160×144 seul | 2,75 Mbit/s | 9 aussi, ou bien ~40 si on suit les lignes | ❌ |
| Canevas complet, `IDX8` | 17,6 Mbit/s (5,9 par nœud) | 27 | ❌ sans intérêt ici |

Le sous-rectangle demanderait au firmware du module ÉCRAN de connaître un rectangle plus
petit que sa dalle — ce qu'il ne sait pas faire aujourd'hui (`charge_noeud()` renvoie la
taille de l'affichage entier). Pour **1,65 Mbit/s** d'économie sur un lien qui en encaisse
24,6, on ne touche pas au protocole. **Décision : le sniffer compose le canevas complet.**

Bénéfice secondaire : la marge devient un vrai cadre, librement dessinable côté sniffer
(entrée de palette dédiée, bordure, plus tard un indicateur de batterie), sans rien changer
au protocole ni au module écran.

### 5.5 Le format `IDX2`, octet par octet

4 pixels par octet, **pixel de gauche dans les bits de poids fort** :

```
  bit  7 6 | 5 4 | 3 2 | 1 0
       px0 | px1 | px2 | px3
```

⚠️ **Le piège prévisible** : le PIO remplit son ISR à coups de 2 bits ; l'ordre des pixels
dans le mot de 32 bits dépend du sens de décalage (`in_shiftdir`), et l'ordre des octets en
mémoire dépend du little-endian du RP2350. Les deux peuvent se combiner en un ordre inversé.
Deux parades, aucune ne coûte de CPU :

1. choisir `in_shiftdir` pour que l'ordre tombe juste ;
2. si ça ne suffit pas, **le DMA du RP2350 sait inverser les octets d'un mot en vol** —
   `channel_config_set_bswap()`. Gratuit.

À trancher sur matériel en phase 2 : c'est le genre de détail qu'on ne devine pas
correctement du premier coup, mais qui se voit immédiatement à l'image (pixels par groupes
de 4 dans le désordre).

**La palette** est envoyée par paquet `PXL1_TYPE_CTRL` / `PXL1_CTRL_PALETTE`, comme pour
`IDX8` : 256 entrées B,G,R dont seules les 4 premières servent. Renvoyée toutes les 2 s, pour
qu'un nœud redémarré la retrouve seul. C'est ce qui rend les teintes réglables sans
recompiler : vert DMG, gris, bivert, ou n'importe quoi d'autre.

### 5.6 Ce qu'on écarte, et pourquoi

| Option | Verdict |
|---|---|
| **Capture par interruption CPU sur CPG** | ❌ 4 MHz d'interruptions, ~240 ns par pixel pour entrer et sortir d'un handler. Impossible, et c'est précisément ce à quoi sert le PIO |
| **Lire la VRAM par le bus cartouche** | ❌ il faudrait reconstruire le PPU : fenêtre, sprites, priorités, registres. C'est écrire un émulateur pour ne pas souder cinq fils |
| **Réduire à la source (64×58, 4 bpp)** | ❌ le 192×192 accueille le 160×144 en 1:1 ; la réduction a disparu avec la grille 3×3 |
| **RLE ou `IDX4`** | ❌ `IDX2` est déjà natif et plus compact. Compresser un format natif de 2 bits, c'est du travail pour rien |
| **Générateur de bus LCD sur un 2ᵉ Pico, pour développer sans ouvrir la console** | 🔶 tentant, écarté en v1 : on validerait le firmware contre **nos propres hypothèses de timing**, pas contre la console. À garder comme repli si la soudure bloque |
| **Faire transiter par un PC** | ❌ un saut de plus, une machine à allumer, et la latence mesurée du lien direct est déjà de ~8 ms |
| **FreeRTOS** | ❌ même raison que sur le module écran : le timing dur est déjà en PIO/DMA, et il y a deux activités pour deux cœurs |

---

## 6. Les phases

### Phase 0 — identification des signaux · ½ à 1 jour · 🔬 **aucune soudure définitive**

Analyseur logique sur la carte MGB ouverte, alimentée par ses piles. On cherche les cinq
fréquences du §3.2, on mesure VCC, et on relève la fréquence instantanée de `CPG`.

**Critère de sortie :**
- les 5 signaux identifiés, chacun par **sa fréquence ET le test blanc/noir** ;
- VCC mesuré, donc interface électrique tranchée (§3.3) ;
- fréquence instantanée max de `CPG` relevée — elle valide (ou non) la marge du PIO ;
- polarité de `LD0/LD1` relevée : `00` est-il blanc ?
- un relevé sauvegardé, annoté, versionné dans `docs/`.

> C'est la phase la plus importante du sous-projet et la seule qui soit purement de la mesure.
> Tout ce qui suit repose sur elle. L'analyseur logique est **non négociable** : un
> oscilloscope 2 voies ne permet pas de corréler cinq signaux.

### Phase 1 — prise de signaux et interface électrique · 1 jour · 🔴 **irréversible**

Soudure des six fils, 100 Ω en série, tampon 74LVC244 sur perfboard, connecteur débrochable,
reprise d'effort à la colle. Alimentation du sniffer **séparée**, masses communes.

**Critère de sortie :**
- la console démarre et joue normalement, module **branché** puis **débranché** ;
- **son écran d'origine est intact** — pas de traînée, pas de contraste modifié. Comparer sur
  la même image, avant/après, en photo ;
- les cinq signaux relevés **côté Pico** (après le tampon) sont propres : fronts nets, pas de
  rebond, niveaux 3,3 V ;
- on peut refermer la console.

### Phase 2 — capture · 2 à 3 jours

PIO + DMA + IRQ VSYNC. Pas encore de réseau : on prouve **par l'image** qu'on a capturé la
bonne chose.

- `tools/gbdump.py` récupère un canevas par la console USB et l'écrit en PNG ;
- compteurs : lignes par trame (doit valoir 144), pixels par ligne (160), trames par seconde.

**Critère de sortie :**
- le PNG est **reconnaissable** : l'écran-titre d'un jeu, lisible, sans décalage ;
- **59,73 trames/s** ± 0,1 ;
- **0 ligne manquante et 0 ligne en trop sur 10 000 trames** — c'est ce compteur qui dira si
  `CPL` suffit ou s'il faut basculer sur `CP/CPV` (GP5) ;
- l'ordre des pixels dans l'octet est tranché (§5.5) et consigné.

> Un décalage horizontal progressif = un front de `CPG` raté. Un décalage vertical = `CPL` ou
> `ST`. Une image en groupes de 4 pixels désordonnés = l'ordre des bits. Les trois symptômes
> sont distincts : l'image est un bon instrument de diagnostic, contrairement au compteur de
> trames du module écran, qui ne prouvait rien.

### Phase 3 — `IDX2` de bout en bout · 1 à 2 jours · **touche aussi `../ecran/`**

Deux moitiés, dans cet ordre :

1. **Côté ÉCRAN** : décoder `PXL1_FMT_IDX2` dans `reseau.cpp`. Le chemin `IDX8` existe déjà
   (indexé + palette) ; il s'agit d'un dépaquetage 4 pixels par octet en amont, et d'ajuster
   `charge_noeud()`. Éprouvé **sans la console**, avec `pixelpush --format idx2` — à ajouter
   à l'émetteur PC, ce qui donne au passage un banc de test indépendant du sniffer.
2. **Côté CAPTURE** : émission des 3 × 3 paquets par nœud, pipelinée selon §5.3, plus la
   palette en `CTRL` toutes les 2 s.

**Critère de sortie :** le jeu tourne sur la console et **s'affiche sur la grille**, à
59,73 img/s, sans déchirure ni scintillement, pendant 10 minutes d'affilée.

> Découper la phase ainsi permet de déboguer `IDX2` avec un émetteur PC maîtrisé avant d'y
> ajouter l'inconnue de la capture. Deux inconnues à la fois, c'est une phase qui n'avance
> pas.

### Phase 4 — mesure · 1 jour

- **Latence photon-à-photon** : filmer l'écran d'origine et la grille dans le même cadre, à
  240 img/s, sur un changement brutal (menu qui s'ouvre). Compter les images.
- **Latence interne** : GP20 (VSYNC capturée) et GP21 (trame émise) à l'analyseur, contre
  `PIN_MESURE_FLIP` du module écran. Trois horodatages, une chaîne complète.
- **Taux de trames complètes** reçues par les trois nœuds, sur 10 minutes.
- **Consommation** du sniffer, et vérification que la console ne débite rien pour lui.

**Cible :** sous **une trame GB (16,7 ms)** entre la fin de capture d'une ligne et son
affichage. Le budget : 4,3 à 15,7 ms de remplissage selon le nœud, ~8 ms de réseau mesurés,
1,3 ms de rafraîchissement.

> ⚠️ Comme pour le module écran : **les latences ne se comparent qu'à conditions de lien
> identiques.** Le WiFi 2,4 GHz varie au fil de la journée et domine tout le reste.

### Phase 5 — intégration · plus tard

Boîtier, placement de l'antenne (⚠️ **jamais contre une masse ou un blindage** — c'est le
point le plus souvent négligé), alimentation définitive, cadre dessiné dans la marge.

---

## 7. Budget chiffré

| Grandeur | Valeur | D'où elle vient |
|---|---|---|
| Fréquence trame | **59,727 Hz** | 4 194 304 / (154 × 456) |
| Durée d'une ligne | **108,7 µs** | 456 / 4 194 304 |
| Partie visible d'une trame | **15,65 ms** | 144 lignes |
| VBlank | **1,09 ms** | 10 lignes |
| Horloge pixel, moyenne | **1,38 M impulsions/s** | 160 × 144 × 59,73 |
| Horloge pixel, crête | **~4,2 MHz** 🔬 | à mesurer en phase 0 |
| Budget PIO par pixel | **238 ns**, soit ~12 itérations de boucle | 3 instructions à 150 MHz |
| Trame GB brute | **5 760 octets** | 160 × 144 × 2 bits |
| Canevas émis | **9 216 octets** | 192 × 192 × 2 bits |
| Par nœud | **3 072 octets**, **3 paquets** | 192 × 64 × 2 bits, MTU 1400 |
| Débit total | **4,40 Mbit/s** | 9 216 × 59,73 × 8 |
| Débit par nœud | **1,47 Mbit/s** | à comparer aux 24,6 Mbit/s mesurés |
| Paquets par seconde | **538** | 9 × 59,73 |
| RAM | **18,4 ko** | 2 canevas |

---

## 8. Récapitulatif

| Phase | Nature | Sortie | Effort | Risque |
|---|---|---|---|---|
| 0 · Identification | mesure | 5 signaux identifiés, VCC tranché | ½–1 j | 🟢 |
| 1 · Prise de signaux | matériel | console intacte, signaux propres | 1 j | 🔴 |
| 2 · Capture | C++ + PIO | PNG reconnaissable, 59,73 img/s | 2–3 j | 🟡 |
| 3 · `IDX2` bout en bout | C++ + Python | le jeu s'affiche sur la grille | 1–2 j | 🟡 |
| 4 · Mesure | analyseur + caméra | latence < 16,7 ms | 1 j | 🟢 |
| 5 · Intégration | mécanique | — | — | 🟢 |

---

## 9. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| S1 | Raspberry Pi Pico 2 W | 1 | ⚠️ pas tolérant 5 V |
| S2 | Game Boy Pocket MGB-001 fonctionnelle | 1 | 🔴 sera ouverte et modifiée |
| S3 | 74LVC244A (ou 74LVC245A) + support | 1 | tampon haute impédance |
| S4 | Résistance 100 Ω | 6 | en série, côté console |
| S5 | Résistance 10 kΩ | 2 | pull-down, si besoin constaté |
| S6 | Fil émaillé 0,1–0,2 mm (Kynar / wire-wrap) | 1 rlx | soudures fines |
| S7 | Connecteur JST-SH 6 pts + embase | 1 paire | liaison débrochable |
| S8 | Perfboard + barrettes | 1 | carte du tampon |
| S9 | Alim USB 5 V ou powerbank | 1 | **séparée de la console** |
| S10 | Colle chaude ou UV | — | reprise d'effort, obligatoire |
| T1 | **Analyseur logique ≥ 8 voies, ≥ 24 MS/s** | 1 | **non négociable**. Repli : 2ᵉ Pico + `sigrok` |
| T2 | Multimètre | 1 | VCC, continuité |
| T3 | Fer pointe fine + flux + tresse | 1 | soudures 🔴 |
| T4 | Loupe ou microscope USB | 1 | vérifier les soudures fines |
| T5 | Oscilloscope | 0 ou 1 | qualité des fronts ; l'analyseur suffit le plus souvent |

Ordre de grandeur, hors console et hors outillage : **40–60 €**.

---

## 10. Arborescence cible

```
capture/
├── README.md
├── docs/
│   ├── plan-firmware.md                ← ce document
│   ├── signaux-mgb.md                  ← à écrire (phase 0) : relevés, photos, brochage réel
│   └── releves/                         ← captures de l'analyseur logique
├── firmware/
│   └── sniffer/
│       ├── CMakeLists.txt
│       ├── include/config.h            ← brochage, géométrie, placement dans le canevas
│       ├── include/pxl1.h              ← copie conforme de ../ecran (+ PROVENANCE.txt)
│       ├── src/main.cpp
│       ├── src/capture.cpp             ← PIO + DMA + IRQ VSYNC
│       └── src/net/{reseau.cpp,lwipopts.h}
└── tools/
    ├── console.py                      ← console série (DTR)
    ├── flash.sh                        ← flash UF2
    ├── gbdump.py                       ← canevas → PNG (phase 2)
    └── pxl1recv.py                     ← nœud factice sur PC, affiche ce qu'on émet (phase 3)
```

> `pxl1.h` est **dupliqué**, pas partagé : les deux sous-projets sont deux dépôts git
> indépendants. La duplication est assumée, avec un `PROVENANCE.txt` qui dit d'où vient le
> fichier et à quelle date. Un lien symbolique entre deux dépôts est pire.

---

## 11. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| Arracher une pastille de la carte MGB | 🔴 élevée | Colle chaude dès la sortie des fils, connecteur débrochable, fil émaillé souple |
| Dégrader l'image d'origine par la charge ajoutée | 🟡 | 100 Ω en série, tampon haute impédance, fils < 10 cm. Photo avant/après |
| Détruire le Pico par une entrée 5 V | 🔴 | **Mesurer VCC avant de brancher** (§3.3). Le 74LVC244 alimenté en 3,3 V couvre les deux cas |
| Identifier le mauvais signal | 🟡 | Fréquence **et** test blanc/noir. Le mod bivert confirme LD0/LD1 indépendamment |
| `CPL` capricieux (absent, dédoublé) | 🟡 | GP5 en réserve pour `CP/CPV` ; compteur de lignes par trame en phase 2 |
| Ordre des bits inversé dans l'octet `IDX2` | 🟢 | Se voit immédiatement à l'image ; `channel_config_set_bswap()` corrige sans coût |
| Le WiFi fait rater un front d'horloge | 🟡 | Capture sur le cœur 0, réseau sur le cœur 1 — le découpage qui a fonctionné sur le module écran |
| Antenne du Pico contre une masse | 🟡 | Placement vérifié en phase 5, avant fermeture définitive |
| Alimenter le sniffer depuis les piles de la console | 🔴 | Alimentation séparée. Masses communes, **jamais les VCC** |

---

## 12. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 22/09/2026 | Sous-projet ouvert, distinct du module écran | Deux dépôts, une seule interface : le protocole `PXL1`. Le module écran est terminé jusqu'à sa phase 4 et n'a pas à bouger pour nous |
| 22/09/2026 | Format `IDX2`, pas `IDX8` | C'est le format natif du PPU : zéro conversion à la source, 4,4 Mbit/s au lieu de 17,6, et 9 paquets par trame au lieu de 27. La mesure du 18/09 sur lien dégradé dit que le nombre de paquets compte plus que le débit |
| 22/09/2026 | Le sniffer émet le canevas **192×192 complet**, marges comprises | Le firmware du module écran ne connaît pas de sous-rectangle. 1,65 Mbit/s d'économie ne justifient pas de toucher au protocole sur un lien qui encaisse 24,6 Mbit/s |
| 22/09/2026 | Pas de framebuffer intermédiaire : le DMA écrit dans le canevas | `x0 = 16` pixels = **4 octets pile** en `IDX2`. La recopie 160×144 → 192×192 n'existe pas |
| 22/09/2026 | `clk_sys` laissé à 150 MHz | Marge PIO déjà supérieure à 10× ; et monter `clk_sys` obligerait à rediviser l'horloge SPI du CYW43, ce qui a coûté une journée au module écran |
| 22/09/2026 | stdio sur USB, UART par défaut désactivé | GP0/GP1 portent LD0/LD1. Même piège que sur le module écran, où ils portaient R1/G1 |
| 22/09/2026 | LD0, LD1, CPG sur GP0, GP1, GP2 — contigus | `in pins, 2` exige un groupe contigu. Le module écran a payé cette leçon en phase 0 avec les adresses A–E non contiguës |
| 22/09/2026 | Phase 3 découpée : `IDX2` côté écran d'abord, avec `pixelpush` | Déboguer un nouveau format **et** une nouvelle capture en même temps, c'est une phase qui n'avance pas |
| 22/09/2026 | Phase 0 = mesure pure, aucune soudure | Les cinq signaux ne sont pas documentés de façon fiable pour cette révision de carte. Souder d'après un schéma trouvé en ligne, c'est risquer une pastille pour rien |
| 22/09/2026 | Générateur de bus LCD sur 2ᵉ Pico écarté en v1 | On validerait le firmware contre nos propres hypothèses de timing. Gardé comme repli si la soudure bloque |
| 22/09/2026 | `pxl1.h` dupliqué depuis `../ecran`, avec `PROVENANCE.txt` | Deux dépôts git indépendants ; un lien symbolique entre eux serait pire que la copie |
| 23/09/2026 | Carte identifiée : `MGB-ECPU-01`, ruban LCD sur connecteur ZIF `P2` 18 broches | Observation photographique, pas une supposition. Change le point de prise de signaux |
| 23/09/2026 | **Soudure directe sur les broches de `P2`**, pas d'interposeur FFC | L'interposeur supprimerait le risque 🔴, mais le projet est artistique et le geste de soudure fait partie de la démarche. Décision de Yoann |
