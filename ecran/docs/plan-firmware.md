# Module ÉCRAN — plan de réalisation

*Sous-projet « écran » du Game Boy Pocket déstructuré.*
Version 2 — 29/09/2026 · **une tête réseau, trois nœuds d'affichage** (§2.2 bis)
Version 1 — 16/09/2026

---

## 0. Décisions figées

| Sujet | Décision | Réversible ? |
|---|---|---|
| **Cible immédiate** | Afficheur **réseau générique** — l'image vient de trames UDP, pas d'une Game Boy | — |
| **Géométrie finale** | **3 × 3 dalles** de 64×64 ⇒ **192 × 192 px** | non (dicté par les 160×144 de la GB) |
| **Découpage** | **3 chaînes de 3 dalles**, une par rangée horizontale | oui, en phase 5 |
| **Contrôleurs** | **1 tête** Pico 2 W (WiFi) + **3 nœuds d'affichage** Pico 2, un par rangée, reliés en filaire | — (révisé le 29/09/2026) |
| **Interface publique** | **une adresse, un canevas 192×192** : la source envoie son image entière, à sa taille ; l'écran la place et la répartit | non |
| **Protocole** | **PXL2** ; PXL1 + `CTRL_GEOMETRIE` accepté en compatibilité | non |
| **Langage** | **C99**, et **C++20** à partir de la phase 1 | non |
| **Couche basse** | **pico-sdk 2.x, bare-metal**, PIO + DMA, 2 cœurs | non |
| **RTOS** | **aucun** | oui, si la v2 se charge en fonctionnalités |
| **Synchronisation** | **VSYNC + RDY** pilotés par la tête, sur la nappe de liaison | — |
| **Outil émetteur** | **Python** côté PC | oui |

> 🔀 **Révision du 29/09/2026.** La v1 donnait un Pico 2 W par rangée, chacun avec son WiFi
> et son adresse : l'émetteur devait connaître la grille (3 IP, 3 rectangles, `node_id`).
> Désormais **un seul Pico 2 W reçoit**, et distribue en filaire à trois nœuds qui ne font
> qu'afficher. L'écran devient agnostique de sa source. Justification au §2.2 bis ; le reste
> de la v1 (dalles, chaînes de 3, brochage HUB75, pilote, alimentation) est conservé tel quel.

> 🔑 **Le point qui structure tout** : la Game Boy fait 160×144, la grille fait 192×192.
> **L'image rentre en 1:1.** Aucune mise à l'échelle, aucune perte de qualité — et 16 px de
> marge horizontale / 24 px verticale disponibles pour un cadre (batterie, titre, rien).
> La Game Boy devient un **client du protocole**, pas la raison d'être du firmware.

---

## 1. Objectif et périmètre

**Ce qu'on construit maintenant :** un afficheur qui reçoit des trames en UDP et les affiche.
Rien de spécifique à la Game Boy. La source est un script Python sur un PC : image, vidéo,
mire, flux temps réel.

**Ce qu'on construit plus tard :** le sniffer LCD de la Game Boy émet dans *le même protocole*,
en format indexé 2 bits. Côté écran, rien à écrire.

**Ce que l'émetteur sait de l'écran :** une adresse IP. Rien d'autre. Il envoie son image à
sa taille native ; il peut demander celle du canevas (`PING` → `PONG`, §4.2) s'il veut le
remplir. Le nombre de dalles, de rangées ou de contrôleurs ne le regarde pas.

**Hors périmètre :** le module SOURCE (sniffer), le son, les boutons de la console. Voir
`../Spec_Video_Sniffer_et_Matrice_LED.md`.

---

## 2. Architecture matérielle

### 2.1 Géométrie

```
        192 px  (3 dalles × 64)
    ┌──────┬──────┬──────┐
    │ 0,0  │ 1,0  │ 2,0  │  ← nœud 0, chaîne de 3   64 px
    ├──────┼──────┼──────┤
    │ 0,1  │ 1,1  │ 2,1  │  ← nœud 1, chaîne de 3   64 px   192 px
    ├──────┼──────┼──────┤
    │ 0,2  │ 1,2  │ 2,2  │  ← nœud 2, chaîne de 3   64 px
    └──────┴──────┴──────┘
     576 mm de côté, ~4,5 kg
```

Chaque nœud pilote **une rangée** = une chaîne de 3 dalles = **192 × 64 px logiques**.
Découpage trivial de l'image : pas de serpentin, pas de rotation des rangées paires.

### 2.2 Pourquoi 3 chaînes de 3

Une chaîne de N dalles forme un seul long registre à décalage :
`trame = 32 lignes × 8 plans × (N × 64) coups d'horloge = 16 384 × N`.

| N | Coups/trame | @ 20 MHz | @ 15 MHz | RAM (double tampon) |
|---|---|---|---|---|
| 1 | 16 384 | 1 220 Hz | 915 Hz | 32 ko |
| **3** | **49 152** | **407 Hz** | **305 Hz** | **98 ko** |
| 9 | 147 456 | 136 Hz | 102 Hz | 295 ko / 520 |

#### ✅ Mesuré sur matériel le 16/09/2026

Dalle unique 64×64, scan 1/32, `clk_sys` 266 MHz, pilote JuPfu sur le cœur 0.
Rafraîchissement annoncé par le pilote lui-même.

| Plans BCM | 10 MHz | 12 MHz | 24 MHz | 26 MHz | **28 MHz** | 29,6 MHz |
|---|---|---|---|---|---|---|
| 8 | 424 Hz | 488 Hz | 974 Hz | 1055 Hz | **1138 Hz** | — |
| 10 | — | — | — | — | **750 Hz** | 788 Hz |

Loi linéaire dans les deux cas, à diviser par la longueur de chaîne :

    8 plans  :  rafraîchissement ≈ 40,6 × horloge_pixel_MHz / N
    10 plans :  rafraîchissement ≈ 26,8 × horloge_pixel_MHz / N

**Horloge pixel maximale : ≥ 28 MHz, image nette.** La limite de la dalle n'a pas
été atteinte — c'est notre propre firmware qui plafonne, à `clk_sys / 9` = 29,6 MHz.
La dalle n'est donc pas le facteur limitant.

> ⚠️ Mesuré sur **une** dalle avec une nappe courte. Une chaîne de 3 ajoute deux
> étages de tampons et deux sauts de nappe : le plafond peut baisser. À remesurer
> en phase 5, quand la chaîne existera.

Le modèle théorique du tableau ci-dessus était **optimiste d'environ 50 %** : il ne
comptait que le temps de décalage, en ignorant les gardes et la répartition des plans
de poids fort (`balanced_light_output`). Projections corrigées à 28 MHz :

| N | 8 plans | 10 plans |
|---|---|---|
| 1 | 1138 Hz *(mesuré)* | 750 Hz *(mesuré)* |
| **3** | **≈ 379 Hz** | **≈ 250 Hz** |
| 9 | ≈ 126 Hz | ≈ 83 Hz |

**La décision du §2.2 est confirmée par la mesure, et pour une raison de plus qu'à
l'origine** : une chaîne de 9 tombe sous la cible de 150 Hz **quelle que soit la
profondeur BCM**, indépendamment de tout problème d'intégrité du signal. Une chaîne
de 3 garde une marge confortable, même en 10 plans.

| | 1 chaîne de 9 | **3 chaînes de 3** | 9 chaînes de 1 |
|---|---|---|---|
| Règle métier ≤ 4 dalles/port | ❌ | ✅ | ✅ |
| Rafraîchissement | 136 Hz | **407 Hz** | 1 220 Hz |
| RAM par contrôleur | 295 ko | **98 ko** | 32 ko |
| Fils de synchro | 0 | **2** | 8 |
| Coût contrôleurs | ~7 € | **~21 €** | ~63 € |
| Configs WiFi à gérer | 1 | **3** | 9 |

Les trois raisons d'écarter la chaîne de 9, dans l'ordre :

1. **Intégrité du signal.** Chaque dalle ré-attaque le signal vers sa sortie, donc la longueur
   de nappe n'est pas le problème (25 cm par saut). Ce qui s'accumule, c'est le **retard de
   propagation et la gigue à travers 9 étages de tampons en série** : la marge de setup/hold
   s'épuise en bout de chaîne. Règle de métier chez les installateurs : **≤ 4 dalles par port**.
   Le symptôme d'un dépassement n'est pas franc — « les deux dernières dalles scintillent
   parfois » — c'est le pire type de panne à diagnostiquer.
2. **Luminosité utile.** À N=9, les plans de bits de poids faible deviennent limités par le
   temps de décalage (28,8 µs pour 576 px) et non par leur durée d'affichage : le panneau passe
   l'essentiel du temps éteint.
3. **RAM.** 295 ko sur 520 ko avant lwIP et ses tampons.

> ℹ️ **Pas retenu mais valide** : un contrôleur unique **RP2350B** (48 GPIO) avec 3 ports
> HUB75 sur carte de dérivation. Supprime la synchro, mais impose de fabriquer une carte et
> de quitter le Pico 2 W. En mutualisant A–E, CLK, LAT et /OE (identiques pour les 3 chaînes),
> 3 ports coûtent 26 GPIO — donc *possible* sur Pico 2 W, mais sans une broche de rab.
> À réexaminer en phase 5 si la synchro logicielle déçoit.

### 2.2 bis Une seule tête réseau — révision du 29/09/2026

**Objectif :** l'écran ne présente au monde qu'**une adresse** et **un canevas de 192×192**.
Plus de fichier de disposition côté émetteur, plus de `node_id` : n'importe quelle source
envoie son image entière, à sa taille native, et c'est l'écran qui la place et la répartit.

**Un seul Pico ne peut pas tout faire :**

| Piste | Ce qui bloque |
|---|---|
| 1 chaîne de 9 sur un Pico 2 W | 126 Hz en 8 plans, 83 Hz en 10 : sous la cible de 150 Hz ; au-delà de 4 dalles par port (§2.2) |
| 3 ports HUB75 sur un Pico 2 W | 26 GPIO, soit **toutes** les broches sorties ; pilote JuPfu mono-port → réécriture ; plans de bits de 9 dalles ≈ 1 Mo pour 520 ko ; construction ≈ 9 × 2,23 ms ≈ **20 ms par image**, au-delà des 16,7 ms d'une trame à 60 Hz |

Le rendu reste donc réparti sur trois contrôleurs ; seule la **réception** se centralise. Et
elle ne coûte rien en temps d'antenne : la trame traverse l'air une fois, qu'il y ait un
récepteur ou trois (§4.5). Le facteur limitant était déjà l'air, pas le Pico (journal,
18/09/2026).

```
AVANT (v1)                WiFi ×3                         HUB75
                   ┌── node_id=0 ──► [Pico 2 W · n0] ──► ▣▣▣  rangée 0
 Émetteur ─────────┼── node_id=1 ──► [Pico 2 W · n1] ──► ▣▣▣  rangée 1
 (connaît 3 IP     └── node_id=2 ──► [Pico 2 W · n2] ──► ▣▣▣  rangée 2
  + 3 rectangles)                       └─── fil synchro GP16 (maître n0) ───┘


APRÈS (v2)     WiFi (1 IP)        liaison filaire (nappe 10 pts)     HUB75
                                ┌── L0 ──► [Pico 2 · n0] ──► ▣▣▣  rangée 0
 Source ──PXL2──► [Pico 2 W] ───┼── L1 ──► [Pico 2 · n1] ──► ▣▣▣  rangée 1
 (taille libre,    « TÊTE »     └── L2 ──► [Pico 2 · n2] ──► ▣▣▣  rangée 2
  aucune idée         │  ▲                    │   ▲
  de la grille)       │  └──── RDY ×3 ────────┘   │
                      └─────── VSYNC ─────────────┘   (l'ancien fil GP16)
```

**Rôles :**

```
TÊTE — Pico 2 W, aucune dalle              NŒUD k — Pico 2 (W), WiFi éteint
─────────────────────────────────          ──────────────────────────────────
cœur 0 : cyw43 + lwIP raw                  PIO RX ← CLK, D0, D1, CS
  réassemblage (reseau.cpp déplacé)        DMA → tampon de réception A|B
  placement : 160×144 centré, etc.         CRC vérifié (sniffer DMA, gratuit)
  découpe : rangées 64k…64k+63 → Lk        front VSYNC → display::present()
PIO TX ×3 + DMA ×3 (L0, L1, L2)            RDY = !display::occupe()
VSYNC en sortie, RDY0..2 en entrée         cœur 1 : pilote JuPfu, inchangé
potentiomètre de luminosité (GP26)
```

**Déroulé d'une image :**

```
WiFi   ▓p0▓p1▓p2▓ … ▓p26▓ (dernière)
L0     ─▓▓▓▓▓▓▓▓──────────────────     chaque paquet est relayé dès réception
L1     ─────────▓▓▓▓▓▓▓▓──────────     (≈ 0,35 ms de retard, 1 paquet à 32 Mbit/s)
L2     ─────────────────▓▓▓▓▓▓▓▓──
RDY    ──────────────────────────┐     les 3 nœuds sont prêts
VSYNC  ───────────────────────────╥──  les 3 rangées basculent ensemble
```

**Ce que l'architecture garantit :**

- **Jamais d'image partielle.** La tête est le seul point qui voit tous les paquets : c'est
  elle qui décide qu'une image est complète. Incomplète, pas de VSYNC — les trois rangées
  gardent la précédente.
- **Un nœud en retard fait sauter l'image aux trois rangées ensemble.** La construction des
  plans de bits d'une rangée 192×64 est projetée à ≈ 6,7 ms en moyenne et ≈ 17 ms au pire
  (× 3 des 2,23 / 5,8 ms mesurés en mono-dalle) : elle dépassera parfois une trame à 60 Hz.
  Grâce à RDY, c'est un saut d'image invisible, pas un déchirement entre rangées.
- **Une nappe débranchée ne bloque pas l'écran** : RDY est en pull-up côté tête.
- **L'identité d'un nœud, c'est le port de la tête où il est branché.** Plus de straps : la
  tête annonce sa rangée à chaque nœud (message `HELLO`, §4.6). Les trois nœuds ont un
  firmware identique.

**Alternatives écartées :**

| Piste | Verdict |
|---|---|
| Tête qui pilote aussi la rangée 0 (3 Pico seulement) | C'est un nœud v1 plus deux liaisons. RAM serrée (≈ 370 ko de pilote + tampons de réception + lwIP), couplage WiFi / HUB75 / 266 MHz conservé. **Repli** si l'on renonce au 4ᵉ Pico |
| UART | ~10 Mbit/s au mieux : ne tient pas le BGR888 |
| SPI matériel en esclave | Plafond `clk_peri / 12` ≈ 22 MHz sur 1 bit, juste sous le débit WiFi. **Repli** acceptable si la liaison PIO déçoit |
| Bus partagé en guirlande | Un seul jeu de fils, mais 3 charges par ligne à 16 MHz, et un RDY commun qui ne dit plus quel nœud traîne |

### 2.3 Brochage Pico 2 W ↔ HUB75

Identique pour les 3 nœuds. C'est **verbatim** celui de `pico-examples/pio/hub75` — donc du
code de référence qui tourne sans modification.

| GPIO | Signal HUB75 | Broche IDC | Rôle |
|---|---|---|---|
| GP0–GP5 | R1 G1 B1 R2 G2 B2 | 1 2 3 5 6 7 | données, 6 bits contigus (`out pins, 6`) |
| GP6–GP10 | A B C D E | 9 10 11 12 **8** | adresse ligne, 5 bits contigus |
| GP11 | CLK | 13 | horloge de décalage |
| GP12 | LAT / STB | 14 | verrou de ligne |
| GP13 | /OE | 15 | extinction, actif bas — porte la modulation BCM |
| GND | GND | **4 et 16** | relier **les deux** |

**Sérigraphie de la dalle Seengreat** (vérifiée sur photo) : `LA…LE` = A…E, et **`CE` = /OE**.
`E` est bien sur la **broche 8**, la broche 4 est une masse — le piège classique des panneaux
64×64 ne s'applique pas à ce modèle. À revérifier au multimètre malgré tout.

> 📐 **Fiche de câblage visuelle** : [`cablage-pico-hub75.html`](cablage-pico-hub75.html)
> — les 16 fils un par un, le connecteur vu de l'arrière de la dalle, et le recâblage
> depuis le brochage du wiki Seengreat. Publiée aussi sur
> https://claude.ai/artifact/NuuHJ1oeuricVr1qyfzgqP

> ⚠️ **Le brochage du wiki Seengreat n'est pas utilisable en câblage direct.** Son
> tableau 2-2 place A–E sur GP10, GP16, GP18, GP20 et GP22 : cinq broches **non
> contiguës**, alors que le programme PIO fait `out pins, 5`, qui exige cinq GPIO
> consécutifs. Ce brochage n'existe que pour le routage de la carte adaptatrice
> Seengreat. Constaté sur matériel le 16/09/2026 : câblée ainsi, la dalle n'adressait
> que les lignes 0, 1, 32 et 33.

### Couleurs de la nappe fournie

La nappe Seengreat est un ruban arc-en-ciel standard. ⚠️ **Elle se numérote à l'envers du
connecteur** : la broche 1 du HUB75 est `R1`, jamais une masse, donc le fil que l'on compte
en premier depuis le bord « masse » est en réalité sur la **broche 16**.

> **fil n° N ⟷ broche HUB75 n° (17 − N)**

| Fil | Couleur | Broche | Signal | GPIO |
|---|---|---|---|---|
| 16 | marron | 1 | R1 | GP0 |
| 15 | rouge | 2 | G1 | GP1 |
| 14 | orange | 3 | B1 | GP2 |
| 13 | **jaune** | 4 | **GND** | GND |
| 12 | vert | 5 | R2 | GP3 |
| 11 | bleu | 6 | G2 | GP4 |
| 10 | violet | 7 | B2 | GP5 |
| 9 | **gris** | 8 | **E** | GP10 |
| 8 | blanc | 9 | A | GP6 |
| 7 | noir | 10 | B | GP7 |
| 6 | marron | 11 | C | GP8 |
| 5 | rouge | 12 | D | GP9 |
| 4 | orange | 13 | CLK | GP11 |
| 3 | jaune | 14 | LAT | GP12 |
| 2 | vert | 15 | /OE | GP13 |
| 1 | **bleu** | 16 | **GND** | GND |

Dix couleurs pour seize fils : tout se répète sauf **noir, blanc, gris et violet**, groupés au
centre (fils 7 à 10) — ce sont les repères à partir desquels compter. Et le fil qui sort du
rang, `E`, est **le seul gris** de la nappe.

Contrôle de sens avant de câbler : les deux masses sont le **bleu (fil 1)** et le **jaune
(fil 13)**. ✅ **Confirmé sur matériel le 16/09/2026** — la dalle affiche correctement les
9 mires de la phase 0 avec ce câblage, ce qui ne serait pas possible si la nappe se lisait
dans l'autre sens.

**Broches restantes** : GP14–GP22, GP26–GP28.

| GPIO | Usage v2 (nœud) | Usage v1, pour mémoire |
|---|---|---|
| GP14 | **RDY**, sortie vers la tête (§2.6) | strap d'identité |
| GP15 | libre | strap d'identité |
| GP16 | **VSYNC**, entrée depuis la tête | fil de synchro maître / esclaves |
| GP17 | sortie de mesure : bascule au flip de tampon | idem |
| GP18 | sortie de mesure : bascule au 1er octet reçu de la liaison | idem, 1er octet WiFi |
| GP19, GP20 | **D0, D1** de la liaison — contigus : `in pins, 2` | libres |
| GP21 | **CLK** de la liaison | libre |
| GP22 | **CS** de la liaison | libre |
| GP26–GP28 | libres | GP26 : potentiomètre, passé sur la tête |

Le brochage de la **tête** est au §2.6 : elle ne porte aucune dalle.

> ⚠️ GP0/GP1 sont pris par R1/G1 : **la console série par défaut est morte.**
> Activer **USB CDC** (`pico_enable_stdio_usb`) dès la phase 0.

> ℹ️ Sur Pico 2 W, la CYW43439 occupe GP23–GP25 et GP29, **non sortis** sur le connecteur :
> aucun conflit. Conséquence : la LED embarquée n'est pas sur GP25, elle passe par
> `cyw43_arch_gpio_put()`.

### 2.4 Niveaux 3,3 V → 5 V

Commencer **en direct**, nappe courte. Le tampon 74HC245 5 V en entrée de dalle a un
V_IH ≈ 3,15 V : le 3,3 V passe, de justesse. Si pixels fantômes, scintillement ou colonnes
parasites → **2 × 74AHCT245** par chaîne, alimentés en 5 V, `DIR` au +5 V, `/OE` à la masse.

### 2.5 Alimentation

| | |
|---|---|
| Par dalle | 5 V, **4 A crête** (embase VH 3.96, sérigraphie `+ + - -`) |
| Par rangée (3 dalles) | **12 A** |
| Total 3×3 | **36 A ≈ 180 W crête** |

- Préférer **3 alimentations 5 V / 15 A**, une par rangée, plutôt qu'une seule 40 A : ça isole
  les pannes, ça raccourcit les câbles, et une rangée peut être testée seule.
- Chaque dalle reçoit **sa propre paire de fils** depuis un bus barre. 18 AWG minimum.
- À 12 A par rangée, la chute de tension n'est pas une abstraction : viser **5,0 V mesurés à
  l'embase de la dalle la plus éloignée**, sous charge.
- Pico alimenté par USB, **séparément**. Masses communes obligatoires.
- Ordre de branchement : **masses d'abord**, puis signaux, puis alimentations.
- La tête et les trois nœuds partagent la masse : par les nappes de liaison (4 fils de masse
  chacune), et par les alimentations.

### 2.6 Liaison tête → nœuds, et synchronisation

La v1 synchronisait trois récepteurs WiFi indépendants par un fil maître / esclaves. En v2, un
seul récepteur existe : la synchronisation devient une conséquence de la distribution. Le fil
GP16 des nœuds est conservé, mais c'est la tête qui le pilote.

**Une nappe IDC 10 points par nœud**, en étoile depuis la tête. Point à point : une seule
charge par ligne, et chaque nœud a son propre RDY.

| Broche nappe | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Signal | GND | CLK | GND | D0 | D1 | GND | CS | VSYNC | RDY | GND |
| GPIO nœud | | GP21 | | GP19 | GP20 | | GP22 | GP16 | GP14 | |

CLK est encadrée de deux masses : c'est la ligne la plus rapide, et celle dont un front
parasite coûte le plus cher.

**Brochage de la tête** (Pico 2 W, aucune dalle) :

| GPIO | Usage |
|---|---|
| GP0–GP3 | liaison **L0** : D0, D1, CLK, CS — contigus, `out pins, 2` + 2 broches de *side-set* |
| GP4–GP7 | liaison **L1**, idem |
| GP8–GP11 | liaison **L2**, idem |
| GP12 | **VSYNC**, une sortie vers les 3 nappes |
| GP13–GP15 | **RDY0–RDY2**, entrées **en pull-up** ⚠️ errata RP2350-E9 |
| GP17 | sortie de mesure : bascule à chaque image complète |
| GP18 | sortie de mesure : bascule au 1er octet WiFi d'une image |
| GP26 (ADC0) | potentiomètre de luminosité — un seul pour tout l'écran |
| GP16, GP19–GP22, GP27, GP28 | libres |

**Électrique :** résistance série de **33 Ω** au départ de chaque sortie de la tête (CLK, D0,
D1, CS, VSYNC ×3 nappes), nappes ≤ 50 cm. Les deux Pico sont en 3,3 V : aucun adaptateur.

**Débit :** 2 bits de données à ~16 MHz, soit **32 Mbit/s par liaison**, au-dessus des
24,6 Mbit/s que la tête peut recevoir par WiFi (§4.4). La nappe n'est donc jamais le goulot,
même en relayant tout le flux vers un seul nœud. La largeur (1, 2 ou 4 bits) et l'horloge
sont des paramètres du programme PIO.

**Relais au fil de l'eau :** chaque paquet WiFi est découpé et relayé dès son arrivée, sans
attendre l'image complète. Retard ajouté : le temps de passer un paquet sur la nappe,
1 400 octets à 32 Mbit/s = **0,35 ms**.

**Synchronisation :**

1. La tête relaie les paquets vers les nœuds au fil de leur arrivée.
2. Image complète et toutes les liaisons au repos : la tête attend que les **trois RDY** soient
   hauts.
3. Elle impulse **VSYNC** ; les trois nœuds publient sur le même front.
4. Si un RDY reste bas au-delà d'un délai de garde, l'image est **abandonnée pour les trois**
   (compteur côté tête). Si l'image est incomplète, pas de VSYNC : on garde la précédente.

**Cas limites :**

- **Changement de géométrie de la source** (passage de 192×192 à 160×144, par exemple) : les
  marges ne sont plus écrites par aucune image. La tête envoie un `EFFACER` aux nœuds avant la
  première image de la nouvelle géométrie.
- **Nœud redémarré** : il attend son `HELLO`. La tête le renvoie périodiquement, avec la
  palette et la luminosité.
- **Tête silencieuse** plus d'une seconde : les nœuds affichent une mire « pas de liaison ».

---

## 3. Pile logicielle

### 3.1 Le choix

**C / C++20, pico-sdk 2.x, bare-metal, deux cœurs, zéro RTOS.**

> ℹ️ **Écart assumé par rapport au C99 annoncé en v1** : le pilote JuPfu retenu en phase 1
> est du C++20 (sa configuration est un paramètre de patron `Hub75Driver<Cfg>`, évalué à la
> compilation). Le reste du firmware suit donc en C++, ce qui ne change rien aux décisions
> de fond — pas de RTOS, pas d'allocation dynamique, timing dur en PIO/DMA.

Pas « parce que le C est plus rapide » : parce que c'est la seule pile où le PIO, les DMA
chaînés et l'affinité de cœur sont des primitives de premier ordre, et parce que le code de
référence qui tourne déjà sur cette dalle est écrit dedans.

| | Tête (v2) | Nœud (v2) | Nœud v1, pour mémoire |
|---|---|---|---|
| **Cœur 0** | WiFi, réassemblage, placement, découpe, relais vers les liaisons, VSYNC | liaison : vérification, développement de palette, publication sur VSYNC | WiFi, réception UDP, décodage, écriture dans le tampon arrière |
| **Cœur 1** | libre | entretien du rendu HUB75 | idem |

Le flux des liaisons, comme celui des dalles, est en **PIO + DMA** : le CPU ne touche pas
les octets en transit.

**Outillage** : CMake + pico-sdk 2.x (`PICO_BOARD=pico2_w`), `picotool`, et — vraiment
recommandé à partir de la phase 2 — une **Debug Probe** en SWD. Sans elle, le seul moyen
d'observer est `printf`, et `printf` *change* la latence qu'on essaie de mesurer.

### 3.2 Ce qu'on écarte, et pourquoi

| Pile | Verdict |
|---|---|
| **CircuitPython** | ❌ ~50–100 ms, lib `rgbmatrix` figée, GC imprévisible. Conservé comme **outil de diagnostic** en phase 0 |
| **Arduino** (core earlephilhower + Protomatter) | ❌ Support Protomatter sur RP2350 en zone active ; on se bat contre l'abstraction dès qu'on veut du BCM custom |
| **FreeRTOS** | ⚠️ Fonctionne (port `RP2350_ARM_NTZ`, SMP, `pico_cyw43_arch_lwip_sys_freertos`), mais le timing dur est déjà en PIO/DMA : un ordonnanceur n'ajoute que de la **gigue**. Deux activités, un cœur chacune. À réévaluer si la v2 gagne UI web / OTA / menus. Piège si adopté : sous SMP, `cyw43_arch_init()` doit être appelé **depuis une tâche** |
| **Rust / Embassy** | 🔶 `embassy-rp` couvre RP235x, crate `cyw43` mature pour Pico 2 W. Mais **aucun driver HUB75 64×64 en 1/32** : `hub75-pio` est en v0.1.0 (2022), expérimental, RP2040, 64×32 en 1:16. Il faudrait porter le programme PIO soi-même. **v2 légitime**, une fois le brochage et la logique BCM validés |

---

## 4. Protocole `PXL2`, et `PXL1` en compatibilité

`PXL1` a été conçu **tuile-conscient** : chaque nœud recevait son rectangle, et l'émetteur
connaissait la grille. `PXL2` fait l'inverse : il est **agnostique de l'afficheur**. La
source envoie son image entière à une seule adresse ; le placement et la répartition sont
l'affaire de l'écran.

Les en-têtes sont définis dans `firmware/commun/` — `pxl2.h` et `pxl1.h` — qui font foi,
pour ce sous-projet comme pour le module capture.

### 4.1 En-tête `PXL2` (18 octets, UDP port 4242, charge utile ≤ 1400 o)

```
 offset  taille  champ      description
   0       4     magic      'PXL2'
   4       1     type       0=FRAME  1=CTRL  2=PING  3=PONG  4=ACK
   5       1     format     0=BGR888 1=RGB565 2=IDX2 3=IDX4 4=IDX8 5=RLE8
   6       2     frame_id   numéro d'image, LE — réassemblage et accusé
   8       2     largeur    de l'image SOURCE, en pixels
  10       2     hauteur    de l'image SOURCE
  12       4     offset     position (octets) de la tranche dans l'image source
  16       1     flags      bit0 = dernière tranche de l'image
  17       1     réservé    0
```

- **Balayage ligne par ligne**, chaque ligne commençant sur un octet : `pas = ⌈largeur ×
  bits / 8⌉`. Formats sous l'octet (`IDX2`, `IDX4`) : **pixel de gauche dans les bits de
  poids fort** — l'ordre du sniffer et de l'écran virtuel du module capture.
- **Une tranche ne coupe jamais un pixel** : en BGR888, `offset` et longueur sont multiples de
  3 ; en RGB565, de 2. Sinon, rejet.
- **L'offset passe à 32 bits** : une image 192×192 en BGR888 fait 110 592 octets, hors de
  portée des 16 bits de `PXL1`.
- **La géométrie voyage dans chaque paquet** : une tête redémarrée interprète le flux dès le
  paquet suivant, sans attendre de commande.

### 4.2 Placement, découverte, accusé

- **Placement :** une image de la taille du canevas s'affiche 1:1 ; plus petite, elle est
  **centrée** (Game Boy 160×144 : marges de 16 et 24 px) ; plus grande, elle est rejetée. La
  mise à l'échelle reste le travail de la source.
- **`PING` → `PONG` :** la source demande à l'écran ce qu'il est. Charge du `PONG` :
  `largeur (u16), hauteur (u16), formats acceptés (u16, bit n = format n), charge max (u16)`.
- **`ACK` :** renvoyé à l'émetteur avec le `frame_id` quand l'image est présentée (en phase
  5a, quand elle est complète). L'émetteur mesure ainsi l'aller-retour sur sa propre horloge.
- **`CTRL`**, premier octet de la charge : `0 = PALETTE` (+ 256 × B,G,R ; les 4 premières
  entrées servent en `IDX2`), `1 = LUMINOSITE` (+ 1 octet). Le code `2` reste réservé à la
  géométrie de `PXL1`.

### 4.3 Compatibilité `PXL1`

La tête accepte aussi le `PXL1` que le sniffer émet déjà (module capture, 25/09/2026) :

- `node_id` est **ignoré** : il n'y a plus qu'un récepteur ;
- la géométrie vient de la sous-commande **`PXL1_CTRL_GEOMETRIE`** (`largeur u16, hauteur
  u16, format u8`), que le sniffer renvoie toutes les 2 s avec sa palette. Extension ajoutée
  par le module capture, **reportée ici** comme il le demandait. Tant qu'aucune géométrie n'a
  été reçue, les tranches `PXL1` sont rejetées ;
- l'accusé est rendu en `PXL1` (type `PING`), comme le récepteur v1 : la mesure d'aller-retour
  du sniffer fonctionne sans modification ;
- l'offset reste sur 16 bits : `PXL1` plafonne à 64 ko par image, ce qui exclut le 192×192 en
  BGR888 et en RGB565. Pour ceux-là, `PXL2`.

Le sniffer n'a donc **rien à changer** pour parler à la tête. Il migrera vers `PXL2` s'il y
trouve un intérêt, pas par obligation.

#### En-tête `PXL1`, pour mémoire (12 octets)

```
 offset  taille  champ      description
   0       4     magic      'PXL1'
   4       1     type       0=FRAME  1=CTRL  2=PING
   5       1     node_id    nœud destinataire (0..N-1)
   6       2     frame_id   numéro de trame, LE — réassemblage et synchro
   8       1     format     0=RGB888 1=RGB565 2=IDX2 3=IDX4 4=IDX8 5=RLE8
   9       1     flags      bit0 = dernière tranche de la trame
  10       2     offset     position (octets) de la tranche dans la charge du nœud
```

Chaque nœud connaît **son rectangle** (`x0, y0, w, h`) par configuration ; l'émetteur n'envoie
à chaque nœud que les pixels de son rectangle, en balayage ligne par ligne. Le protocole est
donc indépendant du nombre de nœuds : 1, 3 ou 9, seul le fichier de layout de l'émetteur change.

**Paquets CTRL** : palette (256×3), luminosité, mire de test, mise en veille, élection du
maître de synchro. Séparés des pixels pour ne rien mettre d'autre dans le chemin critique.

### 4.4 Budget de débit — une seule antenne

En v2, **la tête reçoit tout**. Le plafond de l'écran entier est donc celui d'un Pico 2 W :
**24,6 Mbit/s mesurés** en phase 4. Image complète 192×192 :

| Format | Octets/image | à 60 img/s | Verdict |
|---|---|---|---|
| BGR888 | 110 592 | 53,1 Mbit/s | ❌ à 60 Hz — ✅ jusqu'à **27 img/s** |
| RGB565 | 73 728 | 35,4 Mbit/s | ❌ à 60 Hz — ✅ jusqu'à **41 img/s** |
| **IDX8** + palette | 36 864 | **17,7 Mbit/s** | ✅ à **72 %** du plafond — **le chiffre à vérifier en premier** (phase 5a) |
| IDX4 | 18 432 | 8,8 Mbit/s | ✅ |
| **IDX2**, Game Boy 160×144 | 5 760 | **2,8 Mbit/s** | ✅✅ |

Ce n'est pas une régression par rapport à la v1 : le **temps d'air** est le même, la trame ne
traversant l'air qu'une fois (§4.5). La v1 répartissait l'ingestion sur trois Pico, mais les
trois partageaient le même canal à 2,4 GHz.

> Budget v1, pour mémoire (3 nœuds recevant chacun un tiers) : BGR888 17,7 Mbit/s par nœud,
> IDX8 5,9, IDX2 1,5. Le débit soutenu par un Pico 2 W, alors estimé à 10–20 Mbit/s, a été
> mesuré à 24,6 en phase 4.

### 4.5 IDX8 — implémenté le 18/09/2026

Un octet par pixel, plus une palette de 256 entrées B,G,R transportée par un **paquet
de commande** (`PXL1_TYPE_CTRL`) : rien d'autre que des pixels ne transite par le chemin
critique. La palette est renvoyée toutes les 2 s, pour qu'un firmware redémarré la
retrouve sans intervention. Par défaut le firmware tient une rampe de gris, de sorte
qu'une trame indexée s'affiche de façon sensée même sans palette reçue.

Côté émetteur, quantification sur un **cube 6×6×6 plus 40 gris** : palette fixe, donc
la quantification se réduit à une division — 0,02 ms par trame, 3,3 % d'erreur moyenne.
Côté firmware, le développement coûte **0,25 ms** et se fait dans le tampon interne de
l'affichage, déjà alloué : aucune mémoire supplémentaire.

**Ce que l'indexé apporte vraiment**, mesuré dos à dos sur un lien dégradé
(ping passé de 3,4 à 8,3 ms), même émetteur à 60 img/s :

| | BGR888 | IDX8 |
|---|---|---|
| Débit émis | 5,95 Mbit/s | 1,98 Mbit/s |
| Paquets par trame | 9 | 3 |
| **Reçues par le firmware** | **10 img/s** | **60 img/s** |
| Assemblage | 19,3 ms | 7,8 ms |

**Diviser le débit par 3 multiplie par 6 la résistance à un lien dégradé** : une trame
de 9 paquets n'arrive entière que si les neuf passent. L'argument du §4.4 est donc
confirmé par la mesure, et renforcé — l'indexé ne sert pas qu'à tenir dans la bande
passante, il rend la chaîne robuste.

> ⚠️ Les latences ne se comparent **qu'à conditions de lien identiques**. Le lien WiFi
> varie au fil de la journée et domine tout le reste : une mesure prise avant une
> dégradation ne se compare pas à une mesure prise après.

> ℹ️ Nuance utile : le **temps d'air total** est le même quel que soit le nombre de nœuds
> (la trame ne traverse l'air qu'une fois, chaque nœud ne recevant que sa part). Ce que le
> découpage améliorait en v1, c'était le **débit à ingérer par nœud**. La phase 4 a montré
> qu'un Pico en ingère 24,6 Mbit/s : c'est l'air qui limite, pas lui — ce qui a rendu
> possible la tête unique de la v2.

### 4.6 Messages de la liaison tête → nœud

Interne à l'écran : aucune source ne le voit. Un message par salve de CS bas, mots de
32 bits (ce que le DMA et l'`autopush` du PIO manipulent naturellement) :

```
 CS↓ │ type u8 │ image u8 │ pos u16 │ n u16 │ format u8 │ lg u16 │ données…, complétées à 4 o │ CRC16 │ CS↑
```

| Type | Contenu |
|---|---|
| `PIXELS` | `n` pixels au `format` donné, à partir de la position linéaire `pos` dans la rangée (`y × 192 + x`). Quand l'image fait toute la largeur du canevas, un message couvre plusieurs lignes d'un coup |
| `PALETTE` | 256 × B,G,R |
| `LUMINOSITE` | 1 octet |
| `EFFACER` | la rangée passe au noir — changement de géométrie |
| `HELLO` | numéro de rangée, géométrie du canevas, version |
| `MIRE` | mire de diagnostic de la rangée |

- **Les pixels restent dans le format reçu** (IDX8, IDX2…) : la nappe transporte le même
  volume que l'air, et le nœud garde son `developper_idx8` (≈ 0,75 ms pour 192×64).
- **Le champ `image`** (bits de poids faible du `frame_id`) choisit le tampon de réception du
  nœud, A ou B. La tête peut ainsi relayer l'image N+1 pendant que le nœud publie l'image N.
- **Le CRC16** est calculé par le *sniffer* du DMA, des deux côtés : il ne coûte pas un cycle.
  Un message corrompu est jeté et compté ; le nœud baisse alors RDY jusqu'à la fin de l'image,
  qui est abandonnée pour les trois rangées.
- La découpe d'une tranche WiFi en messages vit dans `firmware/tete/include/decoupe.hpp`,
  en C++ portable, **testée sur PC** (`firmware/tete/test/`).

---

## 5. Les phases

### Phase 0 — « ça s'allume » · 1 dalle · ½ jour · ✅ **terminée le 16/09/2026**

**Langage / couche :** C, `pico-examples/pio/hub75`, tel quel.

Prouver le câblage et l'alimentation. **Aucune ligne de code écrite.**

1. `WIDTH 64` / `HEIGHT 64` dans `hub75.c`.
2. Ne **rien** toucher aux `#define` de broches : ils correspondent déjà au brochage §2.3.
3. Compiler pour `pico2_w`, flasher.

**Critère de sortie :** dégradé de test stable, sans colonne parasite, sans scintillement
quand on bouge la nappe.

✅ **Atteint.** Les 9 mires de `firmware/phase0-bringup` passent. Sont donc validés :
les 14 signaux, les 5 lignes d'adresse A–E (cadre complet, dégradé vertical régulier,
balayage ligne à ligne), la séparation des deux demi-écrans, la modulation BCM sur 8 plans,
l'absence de ghosting au damier 1 px, et le 3,3 V direct sans adaptateur de niveau.

**Si rien ne s'allume — ordre de diagnostic :** ① les deux GND reliés, continuité vérifiée
② 5,0 V à l'embase VH **sous charge** ③ sens de la nappe, `E` sur broche 8 ④ si le doute porte
sur le *toolchain* plutôt que le matériel : CircuitPython ≥ 9 pour Pico 2 W (⚠️ **pas** le
`.uf2` 7.1.1 de l'archive Seengreat, il est RP2040) + le `main.py` de
`docs/seengreat-rgb-matrix-p3-64x64/demo-code/extrait/` avec les broches réécrites en GP0…GP13.
Glisser-déposer, ça tourne en 10 minutes sans compilateur et ça isole matériel vs. build.

---

### Phase 1 — driver HUB75, écrit chaîne-conscient · 2 à 4 jours · ✅ **terminée le 18/09/2026**

**Langage / couche :** C99 + assembleur PIO. Bare-metal, `pico_multicore`, DMA chaînés.

Un module qui prend un framebuffer en RAM et l'affiche à haute fréquence sans consommer de CPU.
**La géométrie est paramétrée dès maintenant** — c'est ce qui rendra la phase 5 quasi gratuite.

```c
// include/config.h
#define PANEL_W       64
#define PANEL_H       64
#define CHAIN_LEN     1     // → 3 en phase 5
#define NODE_COUNT    1     // → 3 en phase 5
#define BCM_PLANES    8
#define NODE_ID_PINS  {14,15}        // straps, pull-up ⚠️ E9
```

L'API ne parle que du rectangle **du nœud** : `hub75_backbuffer()` rend un buffer
`CHAIN_LEN*64 × 64`, `hub75_flip()` bascule. Le driver ignore qu'il existe d'autres nœuds.

**Décisions techniques figées ici :**
- **BCM 8 plans**, pas de PWM.
- **Double tampon** — mais celui du pilote suffit. Y superposer le nôtre n'apportait rien
  et introduisait un scintillement à 60 Hz : `present()` basculait vers un tampon non
  rempli, donc republier la même image alternait image / noir. Constaté le 18/09/2026.
  Jamais de triple tampon : chaque tampon en plus est une trame de latence en plus.
- Cœur 1 dédié à l'entretien du rendu.
- Horloge système **200 MHz** (overclock modéré, sans surtension).
- Option anti-gigue : `pico_set_binary_type(... copy_to_ram)` supprime la gigue du cache XIP.

**Base de code :** vendoriser [JuPfu/hub75](https://github.com/JuPfu/hub75) (MIT, testé
RP2350A **et** B, BCM 8/10 bits, courbe CIE 1931, topologies serpentin et raster, flag
`HUB75_MULTICORE`) dans `firmware/vendor/` avec un `PROVENANCE.txt`, comme la doc constructeur.
Alternative : [dgrantpete/Pi-Pico-Hub75-Driver](https://github.com/dgrantpete/Pi-Pico-Hub75-Driver).

**Critère de sortie :** mire fixe (dégradé + damier 1 px) depuis un buffer statique ;
**≥ 150 Hz mesurés à l'oscilloscope sur /OE** ; une boucle `while(1)` saturant le cœur 0 ne
dégrade pas l'image ; photo à 1/250 s sans bandes.

✅ **Atteint** — `firmware/phase1-driver/`. **788 Hz, rigoureusement constants** au repos,
cœur 0 saturé et sous publication à 60 Hz (min = max sur les trois phases) : l'affichage
est autonome. Sonde sur les pads : `AFFICHE`, adresses actives 96 %. Empreinte 114 ko de
RAM sur 520. `clk_sys` retenu : **266 MHz** et non les 200 MHz envisagés — mesuré stable,
et il donne 29,6 MHz d'horloge pixel. Contrôles visuels passés le 18/09/2026 : damier 1 px
en grain fin régulier **sans traînée**, et aucun scintillement en régime nominal.

**Piège :** le damier 1 px révèle le *ghosting* (fuite de la ligne précédente). S'il apparaît,
c'est le temps d'extinction /OE avant changement d'adresse qu'il faut allonger — pas le câblage.

---

### Phase 2 — protocole et réception · 2 à 3 jours

**Langage / couche :** C, `pico_cyw43_arch_lwip_threadsafe_background` (`NO_SYS=1`),
**API raw lwIP** (`udp_recv`, callback). Pas l'API sockets : copies mémoire et réveils inutiles.

Implémenter `PXL1` §4 : réassemblage par `frame_id`, décodage de format, écriture directe dans
le tampon arrière.

**Ce qui fait la latence, par ordre d'importance :**

| # | Levier | Gain |
|---|---|---|
| 1 | `cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM)` | **10–100 ms** 🔴 |
| 2 | API raw lwIP plutôt que sockets | 1–3 ms |
| 3 | Traiter **chaque tranche à l'arrivée** au lieu d'attendre la dernière | ~8 ms |
| 4 | IP statiques, pas de DHCP ni mDNS en régime établi | variable |
| 5 | `PBUF_POOL_SIZE` / `MEM_SIZE` dimensionnés dans `lwipopts.h` | évite des pertes silencieuses |

**Critère de sortie :** RGB888 à 60 Hz sur une dalle (5,9 Mbit/s), **< 0,1 % de perte sur
10 minutes**, compteurs de perte/désordre sur USB CDC.

**Piège :** ne pas tester contre le sniffer — il n'existe pas. L'émetteur Python de la phase 3
est le banc d'essai, et il permet d'injecter pertes et désordre volontairement.

---

### Phase 3 — l'émetteur PC · 1 à 2 jours

**Langage :** **Python** (`socket` + `numpy` + `Pillow`), dans `ecran/tools/pixelpush/`.

C'est la phase qui rend le projet visible : on lance une commande, l'écran affiche une vidéo.

- bibliothèque `send_frame(np.ndarray)` : découpe par nœud, encode, expédie ;
- sources : image fixe, GIF/vidéo (OpenCV), mires, bruit de Perlin, horloge, capture d'une
  fenêtre du PC ;
- **fichier de layout** décrivant les nœuds (`node_id`, `x0`, `y0`, `w`, `h`, IP) — écrit
  maintenant pour 1 nœud, utilisé tel quel en phase 5 pour 3 ;
- mode `--inject-loss` / `--inject-reorder` pour éprouver la phase 2.

Python tient largement le débit. Si une source à très faible latence devient nécessaire, ce
module seul sera réécrit, sans toucher au firmware.

**Critère de sortie :** une vidéo fluide sur la dalle, pilotée depuis le PC.

---

### Phase 4 — mesure · 2 jours · ✅ **terminée le 18/09/2026**

**Méthode :** GP18 bascule dans le callback `udp_recv` au premier octet, GP17 bascule au flip
de tampon. Analyseur logique sur les deux : l'écart se lit directement. **Mesurer, pas supposer.**

**Les trois chiffres qui conditionnaient la phase 5, tous obtenus :**

1. ✅ **Fréquence d'horloge pixel maximale stable** — **≥ 28 MHz sur une dalle**, mesuré le
   16/09/2026 (§2.2). Reste à refaire sur une chaîne de 3 en phase 5.
2. **Rafraîchissement effectif et luminosité utile à `CHAIN_LEN=3`.**
3. ✅ **Débit UDP réellement soutenu** par un Pico 2 W — **24,6 Mbit/s**, mesuré le
   18/09/2026. Décide du format retenu pour le 3×3 : voir ci-dessus.

**Finition :** potentiomètre de luminosité GP26, reconnexion WiFi automatique, mire « pas de
signal » après 1 s de silence, réglages persistés en flash.

**Budget de latence visé** (à confirmer par la mesure) :

| Poste | Ordre de grandeur |
|---|---|
| WiFi UDP, 1 saut, power save coupé | 1–3 ms |
| Réassemblage (tranches traitées à l'arrivée) | ~1 ms |
| Décodage de format | < 0,5 ms |
| Attente du rafraîchissement BCM | 2–7 ms |
| **Total ajouté** | **5–12 ms** |

---

### Phase 5 — passage à 3×3, en trois temps

La v1 prévoyait ici « `CHAIN_LEN 3`, straps, fil de synchro : une centaine de lignes ». La
révision du 29/09/2026 (§2.2 bis) la découpe en trois étapes, **chacune mesurable seule**, dans
l'ordre du risque : d'abord le chiffre qui peut tout remettre en cause.

#### Phase 5a — la tête seule · ≈ 1 jour · 🔨 firmware écrit le 29/09/2026

**Firmware :** `firmware/tete/`. C'est le firmware réseau des phases 2–4 **sans le HUB75** :
même pile (lwIP raw, économie d'énergie coupée), même réassemblage avec fenêtre de
resynchronisation, plus :

- l'en-tête `PXL2` et la compatibilité `PXL1` + `CTRL_GEOMETRIE` (§4.1–4.3) ;
- le placement (centrage) et la **découpe par rangée** (`decoupe.hpp`, testée sur PC) — les
  messages sont comptés par rangée, pas encore émis ;
- `PING` → `PONG`, `ACK` à l'image complète.

`clk_sys` reste à **266 MHz** au départ : c'est la configuration où les 24,6 Mbit/s ont été
mesurés. Descendre à 150 MHz (et rendre à `CYW43_PIO_CLOCK_DIV_INT` sa valeur par défaut) ne
se fera qu'une fois le débit établi, pour savoir si lwIP est limité par le CPU.

**Émetteur :** `pixelpush --cible <ip>` parle `PXL2`, découvre le canevas par `PING`, et
accepte `--taille 160x144` pour simuler la Game Boy.

**Critère de sortie :** image 192×192 en **IDX8 à 60 img/s**, **< 0,1 % de perte sur
10 minutes**, octets par rangée égaux au tiers du total. Puis le flux du sniffer en `PXL1`
reçu tel quel, découpé en 144 lignes centrées.

**Si le débit ne tient pas :** IDX8 à 30 img/s, ou IDX4 à 60 — le protocole les prévoit.
C'est le seul résultat qui remettrait en cause l'architecture, d'où sa place en tête.

#### Phase 5b — une liaison, un nœud, une dalle · 2 à 3 jours

**Firmware :** `firmware/noeud/`, dérivé de `firmware/ecran/` : la réception WiFi est
remplacée par la réception PIO de la liaison ; `display.cpp` et le pilote sont repris sans
changement. Côté tête : PIO TX + DMA, VSYNC, RDY, CRC (§2.6, §4.6).

Tout se fait sur la **dalle déjà câblée** : le nœud ne pilote qu'une rangée de 64 pixels de
haut, et la tête lui envoie celle qui lui revient.

**Mesure :** GP18 sur la tête (1er octet WiFi) et GP17 sur le nœud (publication), sur le même
analyseur logique.

**Critère de sortie :** latence ≤ 9 ms (≈ 8 ms en v1 + la nappe), **zéro erreur de CRC sur
10 minutes** à 16 MHz, nappe de 50 cm.

#### Phase 5c — passage à 3×3 · 3 à 5 jours

**Firmware :** `CHAIN_LEN 3` sur les nœuds, `NB_RANGEES 3` sur la tête. Plus de straps, plus
de filtrage par `node_id` : l'identité d'un nœud est son port sur la tête.

**Le vrai travail est matériel :**

| Chantier | Détail |
|---|---|
| Alimentation | 3 × 5 V/15 A, une par rangée. Bus barres, une paire de fils **par dalle**, 18 AWG mini, fusible par rangée |
| Mécanique | 576 × 576 mm, ~4,5 kg. Cadre alu, fixations M3 sur les dalles. Emplacement de la tête au centre du dos, pour des nappes courtes |
| Liaisons | 3 nappes IDC 10 points en étoile depuis la tête (§2.6) |
| Câblage signal | 3 × (embase 2×8 mâle sur perfboard + nappes) — les dalles ont des embases **mâles** et les nappes fournies deux prises **femelles** : il faut l'embase intermédiaire |
| Réseau | **une** IP, celle de la tête |

**Mesures :** horloge pixel maximale stable sur une chaîne de 3 (§2.2, mesurée jusqu'ici sur
une dalle seule), rafraîchissement effectif, durée de construction des plans de bits d'une
rangée — celle qui décide combien d'images RDY fera sauter.

**Critère de sortie :** vidéo plein cadre 192×192, **sans déchirement entre rangées**, sans
ligne de jonction plus claire ou plus sombre.

**Porte de sortie :** si l'horloge pixel tient ≥ 25 MHz sur une chaîne de 3, une chaîne de 9
sur un contrôleur unique redevient discutable — mais en connaissance de cause (§2.2 bis :
la RAM et le temps de construction restent contre).

---

### Phase 6 — la Game Boy · plus tard

Le sniffer émet déjà vers **un seul récepteur**, sa trame native 160×144 en `IDX2`, avec la
géométrie en `PXL1_CTRL_GEOMETRIE` (module capture, 25/09/2026). La tête la centre dans
192×192. Palette DMG appliquée côté afficheur, changeable à chaud par paquet CTRL.

**Côté module écran : rien à écrire** au-delà de la phase 5a. Côté sniffer : pointer
`PXL1_CIBLE_IP` sur la tête. C'est le bénéfice d'avoir traité la Game Boy comme un client du
protocole plutôt que comme sa raison d'être.

---

## 6. Récapitulatif

| Phase | Langage | Couche | Sortie | Effort |
|---|---|---|---|---|
| 0 · Ça s'allume | C + PIO | pico-sdk | ✅ câblage validé le 16/09/2026 | ½ j |
| 1 · Driver | C + PIO asm | bare-metal, 2 cœurs, DMA | ≥ 150 Hz, 0 % CPU | 2–4 j |
| 2 · Protocole + réception | C++ | lwIP raw, cyw43 | ✅ 60 img/s, ~0,05 % de perte, 18/09/2026 | 2–3 j |
| 3 · Émetteur PC | Python | — | ✅ 7 sources + injection, 18/09/2026 | 1–2 j |
| 4 · Mesure | C++ + Python | — | ✅ ~8 ms de latence, 24,6 Mbit/s, 18/09/2026 | 2 j |
| 5a · Tête seule | C++ | lwIP raw, cyw43 | IDX8 192×192 à 60 img/s, < 0,1 % de perte | 1 j |
| 5b · Liaison, 1 nœud | C++ + PIO asm | PIO, DMA, CRC du sniffer DMA | ≤ 9 ms, 0 erreur CRC | 2–3 j |
| 5c · Passage à 3×3 | C++ + mécanique | idem | 192×192 sans déchirure | 3–5 j |
| 6 · Game Boy | — | — | 1:1, rien à écrire | — |

---

## 7. Nomenclature du 3×3

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| E1 | Seengreat RGB Matrix P3.0-64x64 | **9** | ~30 €/pièce |
| E2 | Raspberry Pi Pico 2 W | **4** | 1 tête + 3 nœuds. Les nœuds n'utilisent pas le WiFi : un Pico 2 simple suffit, mais les 3 Pico 2 W déjà en main font l'affaire — **seule la tête est à acheter** |
| E3 | Alimentation 5 V / 15 A | **3** | une par rangée |
| E4 | Embase 2×8 mâle 2,54 + perfboard | 3 | interface Pico ↔ nappe |
| E5 | Nappe IDC 16 pts | 9 | fournies avec les dalles (1 chacune) |
| E6 | Câble d'alim VH 3.96 | 9 | fournis avec les dalles |
| E7 | Nappe IDC 10 pts ≤ 50 cm + 2 embases 2×5 mâles | **3** | liaison tête → nœud (§2.6). Remplace le fil de synchro de la v1 |
| E8 | Bus barres, fil 18 AWG, fusibles | — | distribution 12 A/rangée |
| E9 | Cadre alu + visserie M3 | 1 | 576 × 576 mm |
| E10 | Potentiomètre 10 kΩ | **1** | luminosité de tout l'écran, GP26 de la tête |
| E11 | 74AHCT245 | 0 ou 6 | **seulement si** ghosting constaté |
| E12 | Raspberry Pi Debug Probe | 1 | fortement recommandé dès la phase 2 |
| E13 | Résistance 33 Ω | 15 | série, sur les sorties de liaison de la tête |

Ordre de grandeur : **410–460 €** — la v2 ajoute une dizaine d'euros (un Pico 2 W, trois
nappes) et en retire deux potentiomètres.

---

## 8. Arborescence cible

```
ecran/
├── README.md
├── docs/
│   ├── plan-firmware.md                    ← ce document
│   └── seengreat-rgb-matrix-p3-64x64/      ← doc constructeur archivée
├── firmware/
│   ├── commun/                             ← protocoles : pxl1.h, pxl2.h — font foi
│   ├── tete/                               ← v2 : WiFi, placement, découpe, liaisons
│   │   ├── include/{config.h,decoupe.hpp,reseau.hpp}
│   │   ├── src/{main.cpp,net/reseau.cpp,net/lwipopts.h}
│   │   └── test/test_decoupe.cpp           ← découpe éprouvée sur PC
│   ├── noeud/                              ← v2 (phase 5b) : liaison → HUB75
│   ├── ecran/                              ← v1 : nœud WiFi autonome, phases 1–4
│   ├── phase0-bringup/, phase1-clock-sweep/
│   └── vendor/hub75-jupfu/                 ← + PROVENANCE.txt
└── tools/
    └── pixelpush/                          ← émetteur Python : PXL2 (--cible) ou PXL1 (--layout)
```

---

## 9. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 16/09/2026 | Afficheur réseau générique d'abord, Game Boy ensuite | L'écran n'est pas lié à une console ; la GB devient un client du protocole |
| 16/09/2026 | Grille 3×3 = 192×192 | Permet le 160×144 en **1:1** — supprime toute mise à l'échelle |
| 16/09/2026 | C bare-metal, pas de FreeRTOS, pas de Rust en v1 | Timing dur déjà en PIO/DMA ; deux activités, un cœur chacune ; pas de driver HUB75 64×64 en Rust |
| 16/09/2026 | Protocole tuile-conscient et multi-format dès la v1 | Seule décision coûteuse à prendre en retard |
| 29/09/2026 | **Une tête réseau Pico 2 W + trois nœuds d'affichage en filaire** | L'écran devient agnostique de sa source : une adresse, un canevas. Un seul Pico ne peut pas tout afficher (26 GPIO, ≈ 1 Mo de plans de bits, ≈ 20 ms de construction par image) ; la réception, elle, se centralise sans coût d'antenne |
| 29/09/2026 | Protocole **PXL2** : géométrie de la source et offset 32 bits dans l'en-tête, plus de `node_id` | La source envoie son image à sa taille ; 192×192 en BGR888 dépasse les 64 ko adressables par `PXL1` |
| 29/09/2026 | `PXL1` + `CTRL_GEOMETRIE` accepté par la tête | Le sniffer l'émet déjà ; il n'a rien à changer. L'extension du module capture est reportée dans `firmware/commun/pxl1.h` |
| 29/09/2026 | Liaison PIO 2 bits ~16 MHz, nappe 10 points en étoile, VSYNC + RDY | 32 Mbit/s > 24,6 reçus par WiFi : la nappe n'est jamais le goulot. RDY transforme un nœud en retard en saut d'image commun, pas en déchirement |
| 29/09/2026 | Pixels relayés dans leur format reçu, au fil de l'eau | La nappe transporte le volume de l'air ; +0,35 ms de latence seulement |
| 29/09/2026 | Phase 5 découpée en 5a (tête) → 5b (liaison) → 5c (3×3) | Le débit IDX8 192×192 sur une seule antenne est le seul chiffre qui peut remettre l'architecture en cause : on le mesure en premier |
| 18/09/2026 | IDX8 implémenté | Sur lien dégradé, il reçoit 60 img/s là où BGR888 tombe à 10 — la robustesse, pas seulement le débit |
| 18/09/2026 | Phase 4 terminée : latence ~8 ms, débit UDP 24,6 Mbit/s | Deux mesures de latence indépendantes concordent à 0,3 ms |
| 18/09/2026 | Le facteur limitant du 3×3 est **l'air**, pas le Pico | Le Pico encaisse 24,6 Mbit/s ; c'est le total des trois nœuds sur 2,4 GHz qui ne passe pas en BGR888 |
| 18/09/2026 | Phase 3 terminée : 7 sources, injection de perte et de désordre | L'injection a trouvé deux bugs de réassemblage dans le firmware |
| 18/09/2026 | Fenêtre de resynchronisation de 8 trames à la réception | Sans elle, un émetteur redémarrant à frame_id 0 bloque la réception définitivement |
| 18/09/2026 | Phase 2 terminée : 60 img/s en BGR888, ~0,05 % de perte | Trois obstacles levés : diviseur SPI du CYW43, routes VPN, réentrance de `update_bgr` |
| 18/09/2026 | `CYW43_PIO_CLOCK_DIV_INT` à 4 | La liaison SPI du WiFi dérive de `clk_sys` : à 266 MHz elle décroche avec la valeur par défaut |
| 18/09/2026 | Publication conditionnée à `occupe()`, pas à un délai | Une construction prend 2,23 ms ; le plancher de 10 ms deviné écartait des trames et faisait chuter la cadence |
| 18/09/2026 | Un seul tampon côté firmware, celui du pilote suffit | Le double tampon superposé faisait alterner image / noir à chaque publication |
| 18/09/2026 | Phase 1 terminée : 788 Hz constants, cœur 0 libre | Le rafraîchissement ne bouge pas d'un Hz sous charge — l'affichage est bien autonome |
| 18/09/2026 | `clk_sys` à 266 MHz, pas 200 | Mesuré stable, donne 29,6 MHz d'horloge pixel |
| 18/09/2026 | Pilote sur le cœur 1 : nécessite `setBasisBrightness()` après `start()` | Sans cela les commandes de ligne restent à zéro — adresse figée à 0, panneau noir, alors que le compteur de trames tourne normalement |
| 16/09/2026 | **Horloge pixel ≥ 28 MHz, image nette** ; limite de la dalle non atteinte | Plafonné par notre firmware (clk_sys/9 = 29,6 MHz), pas par la dalle |
| 16/09/2026 | Rafraîchissement mesuré : 1138 Hz en 8 plans, 750 Hz en 10 plans, à 28 MHz | Le modèle théorique était optimiste de ~50 % ; une chaîne de 9 tombe sous 150 Hz quelle que soit la profondeur BCM |
| 16/09/2026 | Pilote JuPfu vendorisé ; le firmware passe en C++20 | Sa configuration est un paramètre de patron évalué à la compilation ; aucune conséquence sur les décisions de fond |
| 16/09/2026 | Phase 0 terminée : câblage, adresses A–E, BCM et absence de ghosting validés sur matériel | Les 9 mires de diagnostic passent |
| 16/09/2026 | Nappe numérotée à l'envers du connecteur (fil N ⟷ broche 17−N) | Le fil compté en premier est une masse, or la broche 1 d'un HUB75 est toujours R1 |
| 16/09/2026 | Brochage GP0–GP13 confirmé contre le wiki Seengreat | Le tableau 2-2 du constructeur a A–E non contigus, incompatible avec `out pins, 5` du PIO |
| 16/09/2026 | **3 chaînes de 3, 3 × Pico 2 W** | Règle ≤ 4 dalles/port, 407 Hz vs 136 Hz, 98 ko vs 295 ko de RAM, 2 fils de synchro seulement |
