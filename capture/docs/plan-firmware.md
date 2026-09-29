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
| **Interface électrique** | **100 Ω en série** sur chaque prise, **liaison directe** au Pico. Pas de tampon | oui — le tampon reste une option si l'image se dégrade |
| **Protocole de sortie** | **`PXL1`**, réutilisé tel quel — le module ÉCRAN ne change pas d'interface | non |
| **Format** | **`IDX2`**, le format natif de la Game Boy, déjà réservé dans le protocole | non |
| **Mise à l'échelle** | **aucune** — la trame native sort telle quelle | non |
| **Périmètre de l'émetteur** | **la trame Game Boy brute, rien d'autre** : 160×144 en `IDX2`. Aucune connaissance de l'afficheur | non |
| **Récepteur de référence** | un **écran virtuel sur PC**. Le sous-projet se valide de bout en bout **sans la matrice LED** | — |
| **Langage** | **C++20**, pico-sdk 2.x bare-metal, PIO + DMA, **un seul cœur** (§12, 26/09/2026) | non |
| **`clk_sys`** | **150 MHz**, la valeur par défaut — pas les 266 MHz du module écran | oui |
| **Alimentation** | **séparée de la console**, masses communes | non |

> 🔑 **Le point qui structure tout** : la Game Boy produit déjà ses pixels sur **2 bits**, et
> `PXL1_FMT_IDX2` existe déjà dans le protocole depuis sa v1. La chaîne complète est donc
> **sans conversion de couleur et sans mise à l'échelle** : 2 bits sortent du PPU, 2 bits
> traversent le WiFi, 4 entrées de palette décident de la teinte à l'arrivée. Tout le
> problème de ce sous-projet est le **timing de capture**, pas le traitement d'image.

> ⚠️ **Côté module ÉCRAN** : le nœud v1 ne décode que `BGR888` et `IDX8`. C'est la tête v2,
> firmware écrit le 29/09/2026 et pas encore mesuré, qui accepte ce flux `PXL1`/`IDX2`.
> D'ici là, le récepteur de référence est l'écran virtuel sur PC (§12, 26/09/2026).

---

## 1. Objectif et périmètre

**Ce qu'on construit :** un module qui se branche sur le bus LCD d'une Game Boy Pocket,
reconstitue chaque trame 160×144 en 2 bits par pixel, et l'émet en UDP `PXL1` vers un
récepteur : l'écran virtuel sur PC, ou la tête du module ÉCRAN. La console continue de fonctionner normalement, son écran d'origine
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
| Débit UDP encaissé par un Pico 2 W | **24,6 Mbit/s** | nos 2,75 Mbit/s (§7) sont hors sujet |
| Placement et répartition | assurés par la tête v2 depuis le 29/09/2026 | le sniffer n'a rien à savoir de l'afficheur — voir §5.3 |
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

> ⚠️ **Les §3.1 et §3.2 sont les hypothèses d'avant la mesure**, reprises de la spec. La
> phase 0 en a contredit la plupart : l'horloge pixel est `CP` et non `CPG`, le verrou de
> ligne est `P2-ST`, le départ de trame `P2-S`. La table qui fait foi est au §3bis de
> [`signaux-mgb.md`](signaux-mgb.md).

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

Le 74LVC244A couvre les deux cas — c'est pour ça qu'il était au BOM par défaut. **Ne prends pas
de TXS0108 / TXB0108** : ces convertisseurs sont conçus pour de l'open-drain lent et se
comportent mal sur du push-pull à plusieurs MHz.

> ✅ **Tranché en phase 0** ([`signaux-mgb.md`](signaux-mgb.md) §1) : `VCC` n'est pas régulé,
> il suit l'entrée — 3,1 V sur piles neuves, 2,2 V sur piles usées. Le tampon a ensuite été
> abandonné (§12, 25/09/2026) : **liaison directe, 100 Ω en série**. Il reste une option si le
> câble doit dépasser 20 cm, ou pour un portage sur DMG, en 5 V
> ([`liste-achats.md`](liste-achats.md) §1).

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
  dégrade l'image d'origine. → **100 Ω en série** au départ de chaque prise, et un câble
  **sous 20 cm** jusqu'au Pico : sans tampon, il pend directement sur le PPU, à ≈ 1 pF/cm
  (§12, 25/09/2026).
- **Une masse par paquet de signaux**, torsadée avec eux. Une masse unique et lointaine, à
  4 MHz sur du fil volant, sonne.
- **Reprise d'effort obligatoire** : une goutte de colle chaude sur les six fils dès leur
  sortie de la carte. Un fil émaillé qui bouge arrache sa pastille — et la pastille ne
  revient pas.
- **Connecteur débrochable** (JST-SH 8 points, §9) entre la console et le module : on
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
| **GP2** | `CP` (horloge pixel) | entrée | `wait 1 pin 2` / `wait 0 pin 2` dans le PIO |
| **GP3** | `P2-ST` (verrou de ligne, 144 par trame) | entrée | IRQ GPIO : compte les lignes |
| **GP4** | `P2-S` (départ de trame, VSYNC) | entrée | IRQ GPIO |
| GP5 | *(réserve)* `P2-CPL` (horloge de ligne, 154 par trame) | entrée | si `P2-ST` se révèle capricieux |
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
 CP  ──► PIO SM0 : wait 1 / wait 0 / in pins,2   (autopush 32 bits = 16 pixels)
                        │
                        ▼
                    DMA ──► framebuffer 160×144 IDX2, lignes contiguës
                        │
 P2-ST ► IRQ GPIO ──► compte les lignes (144), déclenche un paquet tous les 35
 P2-S  ► IRQ GPIO ──► bascule les tampons, réarme le DMA

 cœur 0 : lwIP + CYW43 ──► 5 paquets UDP PXL1/IDX2 par trame
```

- **PIO SM0** — boucle de 3 instructions : `wait 1 pin 2` / `wait 0 pin 2` / `in pins, 2`,
  soit un échantillon sur le front **descendant** de `CP`, au milieu de la fenêtre stable
  des données (mesuré en phase 0).
  À 150 MHz, une itération dure 20 ns ; l'horloge pixel culmine à ~4,2 MHz, soit 238 ns par
  pixel : **plus de 10× de marge.** Autopush à 32 bits = 16 pixels par mot, 10 mots par ligne.
- **DMA** — le FIFO du PIO alimente la mémoire sans le CPU. Le CPU **ne touche aucun pixel**.
- **IRQ VSYNC** — bascule les tampons, réarme le DMA, réveille l'émission.
- **Réseau** — lwIP et CYW43, servis en arrière-plan sur le **cœur 0**. La capture n'occupe
  aucun cœur : PIO et DMA travaillent seuls, et une coupure WiFi ne change pas sa cadence
  (mesuré en phase 4). Le découpage en deux cœurs prévu ici a été abandonné (§12, 26/09/2026).

### 5.2 Le framebuffer : la trame native, et rien de plus

Une ligne GB fait 160 px × 2 bits = **40 octets**. Le framebuffer en fait 144 :

```
  160 × 144 × 2 bits  =  5 760 octets
  double tampon       =  11,5 ko  sur les 520 ko du RP2350
```

> 🔑 **Les lignes sont contiguës.** C'est ce qui fait disparaître toute la mécanique
> d'adressage : il n'y a ni décalage horizontal à ménager, ni pas de ligne différent du
> contenu, ni table d'adresses à dérouler. Le PIO produit 10 mots par ligne, le DMA les écrit
> les uns après les autres, et les frontières de ligne sont **implicites**.

**Conséquence sur la chaîne DMA** : là où un canevas plus large aurait exigé deux canaux
chaînés déroulant une table de 144 adresses (pour sauter de 40 à 48 octets à chaque ligne),
il suffit ici d'**un seul canal**, de **1 440 mots**, réarmé à chaque VSYNC. Le canal de
contrôle et la table n'existent pas.

⚠️ Ce que le DMA ne sait toujours pas : il compte des mots, pas des lignes. Un front d'horloge
pixel raté décale tout et ne se rattrape jamais dans la trame. D'où le compteur d'intégrité
sur `P2-ST` — **144 impulsions par trame**, mesuré en phase 0.

### 5.3 Ce que l'émetteur ne fait pas

Le sniffer **ignore tout de l'afficheur**. Il ne connaît ni sa géométrie, ni le nombre de
nœuds, ni le découpage. Il émet une trame Game Boy, vers un récepteur, point.

| Responsabilité | Où elle vit |
|---|---|
| Capturer 160×144 en `IDX2` | **ici**, module CAPTURE |
| Émettre la trame en UDP | **ici** |
| Recadrer, centrer, dessiner un cadre | **module ÉCRAN** |
| Répartir sur N nœuds | **module ÉCRAN** |
| Choisir les 4 teintes | **module ÉCRAN** (palette) |

> ⚠️ **La complexité n'a pas disparu, elle a changé de côté.** Le module ÉCRAN doit désormais
> savoir recevoir une source **plus petite que son affichage**, la placer, et la répartir.
> Aujourd'hui son firmware déduit la taille d'une trame de sa **propre** géométrie
> (`taille_trame()` renvoie `DISPLAY_W × DISPLAY_H`) : il faut donc qu'il apprenne la
> géométrie de la **source**. Voir §5.4.
>
> C'est un travail réel, à inscrire au plan du module écran. Le noter ici évite qu'il tombe
> entre les deux sous-projets.
>
> ✅ Fait le 29/09/2026 : la tête v2 du module écran apprend la géométrie de la source par
> `PXL1_CTRL_GEOMETRIE` (§5.4), la place et la répartit elle-même.

### 5.4 Annoncer la géométrie de la source

`PXL1` n'a pas de champ de dimensions : le récepteur a toujours déduit la taille d'une trame
de son propre affichage. Avec un émetteur agnostique, ça ne tient plus.

**Extension retenue : une sous-commande `CTRL`.**

```
  PXL1_CTRL_GEOMETRIE = 2
      + uint16 largeur      (160)
      + uint16 hauteur      (144)
      + uint8  format       (PXL1_FMT_IDX2)
```

Émise avec la palette, **toutes les 2 secondes**, comme elle. Un récepteur redémarré
retrouve seul de quoi interpréter le flux, sans intervention — c'est le comportement déjà
en place pour la palette.

Trois raisons de passer par `CTRL` plutôt que d'élargir l'en-tête :

1. l'en-tête de 12 octets est figé et déjà déployé ; l'élargir casserait le module écran ;
2. la géométrie change au plus une fois par démarrage : elle n'a rien à faire dans le chemin
   critique des pixels ;
3. `CTRL` est précisément la voie prévue pour ce qui n'est pas du pixel (plan `ecran` §4.1).

### 5.4bis Débit et découpage en paquets

| | Valeur |
|---|---|
| Trame | **5 760 octets** |
| Débit | **2,75 Mbit/s** (5 760 × 59,727 × 8) |
| Paquets par trame | **5** — 1400 × 4 + 160 |
| Paquets par seconde | **299** |
| Lignes par paquet de 1400 o | **35** (1400 / 40) |

**L'émission reste pipelinée**, et plus simplement qu'avant : un paquet part dès que ses
35 lignes sont capturées.

| Paquet | Lignes GB | Prêt après |
|---|---|---|
| 0 | 0 – 34 | **3,8 ms** |
| 1 | 35 – 69 | **7,6 ms** |
| 2 | 70 – 104 | **11,4 ms** |
| 3 | 105 – 139 | **15,2 ms** |
| 4 | 140 – 143 | **15,7 ms** |

Comparé au découpage par nœud de la version précédente, c'est **le même principe avec une
frontière plus naturelle** : celle du paquet, qui ne dépend d'aucune hypothèse sur
l'afficheur.

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
qu'un récepteur redémarré la retrouve seul. C'est ce qui rend les teintes réglables sans
recompiler : vert DMG, gris, bivert, ou n'importe quoi d'autre.

### 5.6 Ce qu'on écarte, et pourquoi

| Option | Verdict |
|---|---|
| **Capture par interruption CPU sur l'horloge pixel** | ❌ 4 MHz d'interruptions, ~240 ns par pixel pour entrer et sortir d'un handler. Impossible, et c'est précisément ce à quoi sert le PIO |
| **Lire la VRAM par le bus cartouche** | ❌ il faudrait reconstruire le PPU : fenêtre, sprites, priorités, registres. C'est écrire un émulateur pour ne pas souder cinq fils |
| **Réduire ou mettre à l'échelle à la source** | ❌ l'émetteur sort la trame native. Toute transformation appartient à l'afficheur, qui seul connaît sa géométrie |
| **RLE ou `IDX4`** | ❌ `IDX2` est déjà natif et plus compact. Compresser un format natif de 2 bits, c'est du travail pour rien |
| **Générateur de bus LCD sur un 2ᵉ Pico, pour développer sans ouvrir la console** | 🔶 tentant, écarté en v1 : on validerait le firmware contre **nos propres hypothèses de timing**, pas contre la console. À garder comme repli si la soudure bloque |
| **Faire transiter par un PC** | ❌ un saut de plus, une machine à allumer, et la latence mesurée du lien direct est déjà de ~8 ms |
| **FreeRTOS** | ❌ même raison que sur le module écran : le timing dur est déjà en PIO/DMA, et tout le reste tient sur un cœur |

---

## 6. Les phases

Les phases 0 à 4 sont terminées. Leurs résultats sont dans [`signaux-mgb.md`](signaux-mgb.md),
[`recette-cablage.md`](recette-cablage.md) et le [journal du firmware](../firmware/sniffer/JOURNAL.md).
Le texte ci-dessous est le plan, corrigé là où la réalisation l'a contredit.

### Phase 0 — identification des signaux · ½ à 1 jour · 🔬 **aucune soudure définitive**

Analyseur logique sur la carte MGB ouverte, alimentée par ses piles. On cherche les cinq
fréquences du §3.2, on mesure VCC, et on relève la fréquence instantanée de l'horloge pixel.

**Critère de sortie :**
- les 5 signaux identifiés, chacun par **sa fréquence ET le test blanc/noir** ;
- VCC mesuré, donc interface électrique tranchée (§3.3) ;
- fréquence instantanée max de l'horloge pixel relevée — elle valide (ou non) la marge du PIO ;
- polarité de `LD0/LD1` relevée : `00` est-il blanc ?
- un relevé sauvegardé, annoté, versionné dans `docs/`.

> C'est la phase la plus importante du sous-projet et la seule qui soit purement de la mesure.
> Tout ce qui suit repose sur elle. L'analyseur logique est **non négociable** : un
> oscilloscope 2 voies ne permet pas de corréler cinq signaux.

### Phase 1 — prise de signaux et interface électrique · 1 jour · 🔴 **irréversible**

Soudure des six fils, 100 Ω en série sur perfboard, connecteur débrochable,
reprise d'effort à la colle. Alimentation du sniffer **séparée**, masses communes.

**Critère de sortie :**
- la console démarre et joue normalement, module **branché** puis **débranché** ;
- **son écran d'origine est intact** — pas de traînée, pas de contraste modifié. Comparer sur
  la même image, avant/après, en photo ;
- les signaux relevés **côté Pico**, au bout du câble, sont propres : fronts nets, pas de
  rebond, niveaux 3,3 V ;
- on peut refermer la console.

### Phase 2 — capture · 2 à 3 jours

PIO + DMA + IRQ VSYNC. Pas encore de réseau : on prouve **par l'image** qu'on a capturé la
bonne chose.

- `tools/gbdump.py` récupère une trame par la console USB et l'écrit en PNG ;
- compteurs : lignes par trame (doit valoir 144), pixels par ligne (160), trames par seconde.

**Critère de sortie :**
- le PNG est **reconnaissable** : l'écran-titre d'un jeu, lisible, sans décalage ;
- **59,73 trames/s** ± 0,1 ;
- **0 ligne manquante et 0 ligne en trop sur 10 000 trames** — c'est ce compteur qui dira si
  `P2-ST` suffit ou s'il faut basculer sur `P2-CPL` (GP5) ;
- l'ordre des pixels dans l'octet est tranché (§5.5) et consigné.

> Un décalage horizontal progressif = un front de `CP` raté. Un décalage vertical = `P2-ST` ou
> `P2-S`. Une image en groupes de 4 pixels désordonnés = l'ordre des bits. Les trois symptômes
> sont distincts : l'image est un bon instrument de diagnostic, contrairement au compteur de
> trames du module écran, qui ne prouvait rien.

### Phase 3 — émission réseau · 1 à 2 jours · **autonome depuis le 26/09/2026**

Émission de la trame en 5 paquets `PXL1`/`IDX2` (§5.4bis), plus la palette et la géométrie
en `CTRL` toutes les 2 s, vers **l'écran virtuel** sur PC (§12, 26/09/2026). Recevoir `IDX2`
côté module ÉCRAN ne fait plus partie de cette phase : c'est le rôle de la tête v2.

**Critère de sortie :** le jeu tourne sur la console et **s'affiche sur l'écran virtuel**, à
59,73 img/s, sans trame perdue ([`etapes-detaillees.md`](etapes-detaillees.md) §E.8).

### Phase 4 — mesure · 1 jour

- **Latence photon-à-photon** : filmer l'écran d'origine et la grille dans le même cadre, à
  240 img/s, sur un changement brutal (menu qui s'ouvre). Compter les images.
- **Latence interne** : GP20 (VSYNC capturée) et GP21 (trame émise) à l'analyseur, contre
  `PIN_MESURE_FLIP` du module écran. Trois horodatages, une chaîne complète.
- **Taux de trames complètes** reçues par le récepteur, sur 10 minutes.
- **Consommation** du sniffer, et vérification que la console ne débite rien pour lui.

**Cible :** sous **une trame GB (16,7 ms)** entre la fin de capture d'une ligne et son
affichage. Le budget : 3,8 à 15,7 ms de remplissage selon le paquet (§5.4bis), ~8 ms de réseau mesurés,
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
| **Trame émise** | **5 760 octets** | 160 × 144 × 2 bits — la trame native |
| **Débit** | **2,75 Mbit/s** | 5 760 × 59,73 × 8 |
| **Paquets par trame** | **5** | 1400 × 4 + 160 |
| Paquets par seconde | **299** | 5 × 59,73 |
| Lignes par paquet | **35** | 1400 / 40 |
| **RAM** | **11,5 ko** | 2 framebuffers |
| Transferts DMA par trame | **1**, de 1 440 mots | lignes contiguës |

---

## 8. Récapitulatif

| Phase | Nature | Sortie | Effort | Risque |
|---|---|---|---|---|
| 0 · Identification | mesure | 5 signaux identifiés, VCC tranché | ½–1 j | 🟢 |
| 1 · Prise de signaux | matériel | console intacte, signaux propres | 1 j | 🔴 |
| 2 · Capture | C++ + PIO | PNG reconnaissable, 59,73 img/s | 2–3 j | 🟡 |
| 3 · `IDX2` bout en bout | C++ + Python | le jeu s'affiche sur l'écran virtuel | 1–2 j | 🟡 |
| 4 · Mesure | analyseur + caméra | latence < 16,7 ms | 1 j | 🟢 |
| 5 · Intégration | mécanique | — | — | 🟢 |

---

## 9. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| S1 | Raspberry Pi Pico 2 W | 1 | ⚠️ pas tolérant 5 V |
| S2 | Game Boy Pocket MGB-001 fonctionnelle | 1 | 🔴 sera ouverte et modifiée |
| S3 | ~~74LVC244A (ou 74LVC245A) + support~~ | 0 | tampon haute impédance, abandonné le 25/09/2026 (§12) |
| S4 | Résistance 100 Ω | 6 | en série, côté console — une par signal prélevé |
| S5 | Résistance 10 kΩ | 2 | pull-down, si besoin constaté |
| S6 | Fil émaillé 0,1–0,2 mm (Kynar / wire-wrap) | 1 rlx | soudures fines |
| S7 | Connecteur JST-SH **8 pts** + embase | 1 paire | liaison débrochable. **6 signaux + 2 masses** : LD0, LD1, CP, P2-ST, P2-S, P2-CPL (réserve). Une masse par paquet de 3, torsadée avec eux (§3.4) |
| S8 | Perfboard + barrettes | 1 | porte les 6 résistances, à loger DANS la console |
| S11 | ~~Condensateur 100 nF~~ | 0 | servait au découplage du tampon, abandonné avec lui |
| S9 | Alim USB 5 V ou powerbank | 1 | **séparée de la console** |
| S10 | Colle chaude ou UV | — | reprise d'effort, obligatoire |
| T1 | **Analyseur logique ≥ 8 voies, ≥ 24 MS/s** | 1 | **non négociable**. Repli : 2ᵉ Pico + `sigrok` |
| T2 | Multimètre | 1 | VCC, continuité |
| T3 | Fer pointe fine + flux + tresse | 1 | soudures 🔴 |
| T4 | Loupe ou microscope USB | 1 | vérifier les soudures fines |
| T5 | Oscilloscope | 0 ou 1 | qualité des fronts ; l'analyseur suffit le plus souvent |

Ordre de grandeur, hors console et hors outillage : **40–60 €**.

---

## 10. Arborescence

```
capture/
├── README.md
├── docs/
│   ├── plan-firmware.md                ← ce document
│   ├── etapes-detaillees.md            ← la marche à suivre, étape par étape
│   ├── signaux-mgb.md                  ← relevés de la phase 0 : le brochage qui fait foi
│   ├── recette-cablage.md              ← recette de la phase 1
│   ├── liste-achats.md
│   └── releves/                        ← captures de l'analyseur logique, images obtenues
├── firmware/
│   └── sniffer/
│       ├── CMakeLists.txt
│       ├── README.md, JOURNAL.md
│       ├── PROVENANCE.txt              ← d'où viennent les fichiers copiés
│       ├── include/config.h            ← brochage, géométrie SOURCE (160×144), cible réseau
│       ├── include/pxl1.h              ← copie de ../ecran/firmware/commun/pxl1.h
│       ├── include/secrets.h.example
│       ├── src/main.cpp
│       ├── src/capture.{pio,hpp,cpp}   ← PIO + DMA + IRQ
│       └── src/net/{reseau.hpp,reseau.cpp,lwipopts.h}
└── tools/
    ├── console.py, flash.sh, sniffer.py   ← dialogue avec le firmware
    ├── gbdump.py                          ← trame 160×144 → PNG (phase 2)
    ├── ecran_virtuel.py                   ← récepteur de référence sur PC (phase 3)
    ├── pxl1_envoi.py                      ← émetteur de test, pour l'écran virtuel
    └── analyse_sr.py, sonde.sh, simuler_bus_gb.py   ← outillage de la phase 0
```

> `pxl1.h` est **dupliqué**, pas partagé : chaque module se construit seul, sans dépendre de
> l'arborescence de l'autre. La duplication est assumée, avec un `PROVENANCE.txt` qui dit
> d'où vient le fichier et à quelle date. La définition de référence du protocole reste
> celle du module écran.

---

## 11. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| Arracher une pastille de la carte MGB | 🔴 élevée | Colle chaude dès la sortie des fils, connecteur débrochable, fil émaillé souple |
| Dégrader l'image d'origine par la charge ajoutée | 🟡 | 100 Ω en série, câble sous 20 cm. Photo avant/après |
| Détruire le Pico par une entrée 5 V | 🔴 | **Mesurer VCC avant de brancher** (§3.3) : 3,1 V au plus sur MGB. Un portage sur DMG, en 5 V, exigerait le tampon |
| Identifier le mauvais signal | 🟡 | Fréquence **et** test blanc/noir. Le mod bivert confirme LD0/LD1 indépendamment |
| `P2-ST` capricieux (absent, dédoublé) | 🟡 | GP5 en réserve sur `P2-CPL` ; compteur de lignes par trame en phase 2 |
| Ordre des bits inversé dans l'octet `IDX2` | 🟢 | Se voit immédiatement à l'image ; `channel_config_set_bswap()` corrige sans coût |
| Le WiFi fait rater un front d'horloge | 🟡 | La capture ne dépend d'aucun cœur : PIO et DMA. Vérifié en phase 4 : cadence inchangée pendant une coupure WiFi |
| Antenne du Pico contre une masse | 🟡 | Placement vérifié en phase 5, avant fermeture définitive |
| Alimenter le sniffer depuis les piles de la console | 🔴 | Alimentation séparée. Masses communes, **jamais les VCC** |

---

## 12. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 22/09/2026 | Sous-projet ouvert, distinct du module écran | Deux dépôts, une seule interface : le protocole `PXL1`. Le module écran est terminé jusqu'à sa phase 4 et n'a pas à bouger pour nous |
| 22/09/2026 | Format `IDX2`, pas `IDX8` | C'est le format natif du PPU : zéro conversion à la source, 4,4 Mbit/s au lieu de 17,6, et 9 paquets par trame au lieu de 27. La mesure du 18/09 sur lien dégradé dit que le nombre de paquets compte plus que le débit |
| ~~22/09/2026~~ | ~~Le sniffer émet le canevas **192×192 complet**~~ — **annulé le 25/09/2026** | Le firmware du module écran ne connaît pas de sous-rectangle. 1,65 Mbit/s d'économie ne justifient pas de toucher au protocole sur un lien qui encaisse 24,6 Mbit/s |
| ~~22/09/2026~~ | ~~Pas de framebuffer intermédiaire : le DMA écrit dans le canevas~~ — **sans objet depuis le 25/09/2026**, le framebuffer EST la trame | `x0 = 16` pixels = **4 octets pile** en `IDX2`. La recopie 160×144 → 192×192 n'existe pas |
| 22/09/2026 | `clk_sys` laissé à 150 MHz | Marge PIO déjà supérieure à 10× ; et monter `clk_sys` obligerait à rediviser l'horloge SPI du CYW43, ce qui a coûté une journée au module écran |
| 22/09/2026 | stdio sur USB, UART par défaut désactivé | GP0/GP1 portent LD0/LD1. Même piège que sur le module écran, où ils portaient R1/G1 |
| 22/09/2026 | LD0, LD1, CPG sur GP0, GP1, GP2 — contigus | `in pins, 2` exige un groupe contigu. Le module écran a payé cette leçon en phase 0 avec les adresses A–E non contiguës |
| 22/09/2026 | Phase 3 découpée : `IDX2` côté écran d'abord, avec `pixelpush` | Déboguer un nouveau format **et** une nouvelle capture en même temps, c'est une phase qui n'avance pas |
| 22/09/2026 | Phase 0 = mesure pure, aucune soudure | Les cinq signaux ne sont pas documentés de façon fiable pour cette révision de carte. Souder d'après un schéma trouvé en ligne, c'est risquer une pastille pour rien |
| 22/09/2026 | Générateur de bus LCD sur 2ᵉ Pico écarté en v1 | On validerait le firmware contre nos propres hypothèses de timing. Gardé comme repli si la soudure bloque |
| 22/09/2026 | `pxl1.h` dupliqué depuis `../ecran`, avec `PROVENANCE.txt` | Deux dépôts git indépendants ; un lien symbolique entre eux serait pire que la copie |
| 23/09/2026 | Carte identifiée : `MGB-ECPU-01`, ruban LCD sur connecteur ZIF `P2` 18 broches | Observation photographique, pas une supposition. Change le point de prise de signaux |
| 23/09/2026 | **Soudure directe sur les broches de `P2`**, pas d'interposeur FFC | L'interposeur supprimerait le risque 🔴, mais le projet est artistique et le geste de soudure fait partie de la démarche. Décision de Yoann |
| 25/09/2026 | Seuils vérifiés sur datasheets : RP2350 et 74LVC244A ont le **même** `V_IH` = 2,0 V à 3,3 V | La formule `0,65 × IOVDD` ne vaut que pour IOVDD = 1,8 V. Le tampon n'améliorait donc pas la marge de niveau, contrairement à ce qui était écrit |
| 25/09/2026 | **Tampon 74LVC244A abandonné**, liaison directe + 100 Ω | Les trois arguments sont tombés à la vérification : niveaux identiques ; GPIO0–5 sont `Digital IO (FT)`, donc protégés même Pico hors tension ; tolérance 5 V inutile sans portage DMG. Reste la charge capacitive, et la phase 0 a mesuré le PPU insensible à 40–60 pF |
| 25/09/2026 | **L'émetteur devient agnostique de l'afficheur** : il émet la trame GB native 160×144 en `IDX2`, et rien d'autre | Un émetteur, un récepteur. Recadrage, placement et répartition remontent au module ÉCRAN. Gain mesurable : 5 760 o au lieu de 9 216 (−37,5 %), 5 paquets au lieu de 9, 11,5 ko au lieu de 18,4 — et **la chaîne DMA passe de deux canaux avec table de 144 adresses à un seul canal**, les lignes étant contiguës |
| 25/09/2026 | Sous-commande `PXL1_CTRL_GEOMETRIE` ajoutée au protocole | Le récepteur déduisait la taille d'une trame de sa propre géométrie. Avec un émetteur agnostique il doit apprendre celle de la source. Par `CTRL` et pas par l'en-tête : celui-ci est figé et déployé |
| 26/09/2026 | **Tout tourne sur le cœur 0**, le découpage en deux cœurs du §5.1 est abandonné | La capture n'a pas besoin de cœur : PIO et DMA travaillent seuls, et les interruptions s'exécutent sur le cœur qui les a armées. Pire, `cyw43_arch_init` depuis le cœur 1 bloquait `multicore_launch_core1`, tuait la console et imposait un BOOTSEL physique. Le module écran sert lwIP sur le cœur 0 depuis le début |
| 26/09/2026 | Porte de sortie au démarrage : une touche en 3 s démarre sans réseau | Un blocage réseau avait rendu le Pico inaccessible — plus de console, plus de bascule 1200 bauds. La capture seule suffit à garder de quoi reflasher |
| 26/09/2026 | Latence mesurée en min / moyenne / max sur N échantillons, plus les mesures écartées | Un filtre IIR conservé à travers une remise à zéro mélangeait deux régimes : « instantané 3,11 ms, moyenne 29,49 » |
| 26/09/2026 | **Un écran virtuel sur PC devient le récepteur de référence** | Sépare les problèmes : tant qu'on mettra au point le sniffeur, on veut savoir si **le sniffeur** émet correctement, sans que la matrice, son firmware et son WiFi s'ajoutent à la liste des suspects. Retire aussi une dépendance inter-dépôts du chemin critique |
| 26/09/2026 | L'écran virtuel imite le récepteur réel, accusés compris | Mêmes règles de réassemblage, et il renvoie `PXL1_TYPE_PING` : l'émetteur mesure donc l'aller-retour sans modification, et le passage à la vraie matrice ne changera rien pour lui |
| 25/09/2026 | **La longueur du câble devient un paramètre de conception** | Sans tampon, le câble pend directement sur le PPU. ≈ 1 pF/cm : rester **sous 20 cm** |
