# Chaîne vidéo — Sniffer LCD MGB → matrice LED 64×64

*Sous-projet du « Game Boy Pocket déstructurée ». Module SOURCE (capture) + module ÉCRAN (matrice LED), reliés en WiFi.*
Version 1 — 28/08/2026

---

## 0. Ce que change cette étape par rapport à la Spec v1

Dans la Spec v1, l'image vient d'un **émulateur** hébergé dans le module cartouche. Ici, on
prend le chemin « puriste » : l'image vient d'une **vraie Game Boy Pocket dont on espionne
les signaux LCD**. La console tourne normalement, on se branche en écoute passive.

Conséquence architecturale importante — et bonne nouvelle :

> **Le module écran ne sait pas d'où vient l'image.**
> Il reçoit des paquets UDP `FRAME` au format défini en Spec v1 §5.3. Que l'émetteur soit
> l'émulateur (Pi Zero 2 W) ou le sniffer (Pico 2 W), le firmware d'affichage est le **même**.

Le sniffer devient donc une **source interchangeable**, pas un 5ᵉ module. Tu peux développer
les deux et basculer de l'un à l'autre.

| | Spec v1 (émulateur) | Cette étape (sniffer) |
|---|---|---|
| Origine de l'image | Émulateur headless sur Pi | PPU de la vraie console |
| Console physique | Pas nécessaire | Indispensable, et modifiée (soudures fines) |
| Latence ajoutée | ~0 (l'émulateur choisit) | ≤ 16,7 ms (on subit la cadence du PPU) |
| Boutons / son | Passent par le réseau | **Restent sur la console** (elle est complète) |
| Difficulté | Logicielle (émulateur) | Matérielle (soudure 🔴 + rétro-ingénierie signaux) |

---

## 1. Vue d'ensemble

```
  ┌─────────────────────────────────────┐
  │  GAME BOY POCKET (MGB-001)          │
  │  fonctionne normalement,            │
  │  son LCD d'origine reste branché    │
  │                                     │
  │   CPU MGB ──► signaux LCD ──► LCD   │
  │                   │                 │
  └───────────────────┼─────────────────┘
                      │  prise en dérivation (5 fils + GND)
                      │  LD0 LD1 CLK HSYNC VSYNC
                      ▼
        ┌──────────────────────────────┐
        │  MODULE SOURCE               │
        │  Pico 2 W  « sniffer »       │
        │  PIO + DMA → framebuffer     │
        │  160×144 × 2 bpp             │
        └──────────────┬───────────────┘
                       │  WiFi 2,4 GHz — UDP, 1 saut
                       │  paquets FRAME (Spec v1 §5.3)
                       ▼
        ┌──────────────────────────────┐
        │  MODULE ÉCRAN                │
        │  Pico 2 W  « display »       │
        │  réduction d'échelle + BCM   │
        │  HUB75 : 14 GPIO             │
        └──────────────┬───────────────┘
                       │  nappe IDC 16 pts
                       ▼
        ┌──────────────────────────────┐
        │  Seengreat RGB-Matrix-P3.0   │
        │  64 × 64 · 192 × 192 mm      │
        │  scan 1/32 · alim 5 V dédiée │
        └──────────────────────────────┘
```

Les deux Pico sont **galvaniquement isolés** (lien WiFi) : pas de boucle de masse entre la
console et l'alimentation 5 A de la matrice. C'est un vrai avantage de sécurité ici.

---

## 2. ⚠️ Le point à trancher en premier : 160×144 → 64×64

C'est la contrainte structurante du projet, autant la regarder en face tout de suite.

- Source : **160 × 144** (rapport 10:9)
- Panneau : **64 × 64** (rapport 1:1)

Réduire 160 → 64 c'est un facteur **2,5×**. Une police de jeu Game Boy fait 8×8 px : elle
tombe à 3,2 px. **Le texte devient illisible.** L'action reste lisible (sprites, décors,
mouvement), le texte non.

### Options de mise à l'échelle

| Option | Rendu sur le panneau | Verdict |
|---|---|---|
| **Letterbox 64×58** (÷2,5 exact en X, ÷2,48 en Y) | Image entière, 3 lignes noires en haut et en bas | ✅ Défaut recommandé — géométrie correcte |
| **Étirement 64×64** | Image entière mais déformée (+11 % en vertical) | ❌ Ça se voit |
| **Recadrage 1:1** | 64×64 pixels bruts pris au centre : on ne voit que 40 % de l'écran | Intéressant en installation (effet « loupe »), inutilisable en jeu |
| **Chaînage 2 panneaux → 128×64** | Réduction ÷1,25 seulement en X mais toujours ÷2,25 en Y | Bancal |
| **Chaînage 3×3 → 192×192** | **160×144 en 1:1**, bordure de 16 px à gauche/droite et 24 px en haut/bas | 🏆 Le vrai « pixel perfect », mais ×9 le budget et l'alim |

**Recommandation** : commence avec **un** panneau en letterbox 64×58. C'est déjà une belle
pièce (une Game Boy « impressionniste »), et le firmware d'affichage est écrit pour être
paramétrable en largeur/hauteur. Si tu veux le pixel-perfect plus tard, passe à 3×3 : le code
de mise à l'échelle devient une simple copie, et seule l'alimentation change vraiment.

### Bénéfice caché de la réduction

Le filtrage « boîte » 2,5×2,5 fait la **moyenne** de ~6 pixels source à 4 niveaux. Le résultat
n'a plus 4 niveaux mais une vingtaine — donc un **anti-aliasing gratuit** et un dégradé bien
plus riche que l'original. Sur une matrice RGB 8 bits par canal, c'est très joli. Ne fais
surtout pas du « plus proche voisin ».

---

## 3. Nomenclature (BOM)

### 3.1 Module écran

| Réf | Composant | Qté | Notes |
|---|---|---|---|
| E1 | **Seengreat RGB-Matrix-P3.0-64x64** | 1 | HUB75E, scan 1/32, 192×192 mm, 5 V |
| E2 | **Raspberry Pi Pico 2 W** | 1 | RP2350 + CYW43439. **Le « W » est obligatoire** |
| E3 | Alim 5 V / **5 A** (25 W) à découpage | 1 | Voir §7 pour le dimensionnement |
| E4 | Condensateur électrolytique 1000 µF / 10 V | 1 | Au plus près de l'entrée d'alim du panneau |
| E5 | Condensateur céramique 100 nF | 2 | Découplage |
| E6 | Nappe IDC 16 points (2×8, pas 2,54) | 1 | Souvent fournie avec le panneau — **≤ 20 cm** |
| E7 | Connecteur IDC 2×8 mâle sur PCB proto | 1 | Pour souder le Pico proprement |
| E8 | Câble d'alim panneau (cosses/ferrules) | 1 | Souvent fourni. Fil **18–20 AWG** |
| E9 | *(optionnel)* 74AHCT245 + support DIL20 | 2 | Adaptation de niveau 3,3 → 5 V, voir §6.3 |
| E10 | *(optionnel)* Potentiomètre 10 kΩ | 1 | Réglage de luminosité sur GP26 (ADC0) |
| E11 | Plaque à trous / PCB proto 5×7 cm | 1 | Support Pico + connecteur + condensateurs |

### 3.2 Module source (sniffer)

| Réf | Composant | Qté | Notes |
|---|---|---|---|
| S1 | **Raspberry Pi Pico 2 W** | 1 | Idem |
| S2 | Game Boy Pocket MGB-001 fonctionnelle | 1 | Elle sera ouverte et modifiée 🔴 |
| S3 | **74LVC244A** (ou 74LVC245A) + support | 1 | Tampon haute impédance, entrées tolérantes 5 V |
| S4 | Résistances 100 Ω (série) | 6 | Sur chaque signal prélevé, côté console |
| S5 | Résistances 10 kΩ | 2 | Pull-down externes si besoin (voir §6.4) |
| S6 | Fil émaillé 0,1–0,2 mm (type Kynar / wire-wrap) | 1 rlx | Pour les soudures fines sur la carte MGB |
| S7 | Alim USB 5 V ou powerbank | 1 | **Séparée de la console** — voir §7.2 |
| S8 | Colle chaude ou UV | — | Reprise d'effort obligatoire sur les fils fins |

### 3.3 Outillage indispensable

| Outil | Pourquoi | Alternative |
|---|---|---|
| **Analyseur logique ≥ 8 voies, ≥ 24 MS/s** | Identifier et valider les signaux LCD. **Non négociable** | Un 2ᵉ Pico en analyseur (PIO + `sigrok`) |
| Oscilloscope | Mesurer VCC, voir la qualité des fronts | Multimètre pour VCC seulement |
| Fer à souder pointe fine + flux + tresse | Soudures 🔴 sur la carte MGB | — |
| Multimètre | Continuité, tension, courant du panneau | — |
| Loupe / microscope USB | Vérifier les soudures fines | — |

---

## 4. Côté Game Boy Pocket — la prise de signaux

### 4.1 Les signaux du bus LCD

Le CPU MGB pilote directement le LCD (il n'y a pas de contrôleur intermédiaire). Noms usuels
sur les schémas DMG/MGB :

| Signal | Rôle | Nous ? |
|---|---|---|
| **LD0, LD1** | Les 2 bits de couleur du pixel (4 niveaux) | ✅ Indispensable |
| **CPG** | Horloge pixel — 160 impulsions par ligne visible | ✅ Indispensable |
| **CPL** | Verrou de fin de ligne (transfert du registre à décalage vers le panneau) | ✅ Utilisé comme HSYNC |
| **CP / CPV** | Horloge de ligne (1 impulsion par ligne, y compris VBlank) | ⭕ Alternative HSYNC |
| **ST / S** | Impulsion de début de trame côté registre vertical | ✅ Utilisé comme VSYNC |
| **FR** | Inversion de polarité du LCD, alterne à chaque trame | ⭕ VSYNC de secours (÷2) |
| **CLS** | Horloge du générateur de tension de contraste | ❌ Inutile |

Il faut donc **5 signaux + une masse**.

### 4.2 🔬 À MESURER — identification des broches

Les noms et positions varient selon le modèle et la révision de carte. **N'assume aucun
brochage** : identifie-les à l'analyseur logique, par leur fréquence. Ces valeurs-là sont
sûres, elles découlent du timing de la Game Boy (4,194304 MHz, 154 lignes de 456 cycles) :

| Ce que tu mesures | C'est… |
|---|---|
| **59,73 Hz** | VSYNC (`ST`/`S`) — ou `FR` s'il alterne, donc à **29,86 Hz** en niveau |
| **9,20 kHz** (154 × 59,73) | Horloge de ligne `CP`/`CPV` |
| **8,60 kHz** (144 × 59,73) | Verrou `CPL` — présent uniquement sur les lignes visibles |
| **≈ 1,38 M impulsions/s en moyenne**, en salves de **160 par ligne** | Horloge pixel `CPG`. Fréquence instantanée entre ~1,5 et 4,2 MHz — mesure-la, elle fixe le budget du PIO |
| 2 lignes qui portent des données pendant les salves et **se taisent en VBlank** | `LD0` et `LD1` |

**Test de validation** : lance un jeu, mets l'écran tout blanc (menu) puis tout noir. `LD0` et
`LD1` doivent basculer ensemble d'un extrême à l'autre. C'est ta preuve d'identification.

> Note polarité : sur DMG/MGB, `00` correspond au **blanc** et `11` au **noir** (la valeur
> pilote l'opacité du cristal liquide). Vérifie-le avec le test ci-dessus et prévois un
> inverseur logiciel — c'est un booléen dans le firmware, pas un souci matériel.

### 4.3 🔬 À MESURER — la tension logique

**Mesure VCC sur la carte MGB avant de brancher quoi que ce soit.** Le RP2350 **n'est pas
tolérant 5 V** : une entrée à 5 V détruit le Pico.

| VCC mesuré | Interface à utiliser |
|---|---|
| **≈ 2,4 – 3,3 V** (cas attendu sur MGB, 2×AAA) | 74LVC244 alimenté en 3,3 V, ou liaison directe + 100 Ω en série. Attention : à piles usées (2,4 V) les niveaux deviennent marginaux → **le tampon est fortement conseillé** |
| **≈ 5 V** (cas DMG, ou révision inattendue) | **74LVC244 obligatoire**, alimenté en **3,3 V** : ses entrées sont tolérantes 5 V et ses sorties sortent du 3,3 V propre |

Le 74LVC244A couvre les deux cas — c'est pour ça qu'il est au BOM par défaut. Ne prends
**pas** de TXS0108 / TXB0108 : ces convertisseurs sont conçus pour de l'open-drain lent et se
comportent mal sur des signaux push-pull de plusieurs MHz.

### 4.4 Où souder

Par ordre de préférence :

1. **Les pastilles du ruban LCD sur la carte mère** — pas le plus fin, accessible, et le LCD
   d'origine reste connecté (la console continue de fonctionner normalement).
2. **Les points du mod « bivert »** — le mod bivert
   consiste précisément à intercaler un inverseur sur `LD0` et `LD1`. Les tutos bivert pour
   MGB documentent donc déjà l'emplacement exact de deux de tes cinq signaux. Excellent point
   de départ.
3. **Les broches du CPU MGB** — dernier recours, pas très fin, réservé si les pastilles sont
   inaccessibles.

**Règles de câblage (importantes)** :

- Le PPU pilote un panneau LCD avec une capacité d'attaque **faible**. Toute charge parasite
  peut dégrader l'image d'origine. → **100 Ω en série** au départ de chaque prise, tampon en
  haute impédance (74LVC244, ~5 pF d'entrée), et **fils courts : < 10 cm** jusqu'au tampon.
- **Une masse par paquet de signaux**, torsadée avec eux. Ne compte pas sur une masse unique
  loin des signaux : à 4 MHz sur du fil volant, ça sonne.
- **Reprise d'effort obligatoire** : une goutte de colle chaude sur les 6 fils dès qu'ils
  sortent de la carte. Un fil émaillé qui bouge arrache sa pastille.
- Prévois un **connecteur débrochable** (JST-SH ou barrette 6 pts) entre la console et le
  module sniffer : tu voudras la refermer, la déplacer, la débrancher.

### 4.5 Chemin de repli si la soudure fine bloque

Si les soudures MGB sont trop risquées, deux replis existent, à documenter mais hors périmètre v1 :

- **Sniffer sur une DMG** (Game Boy d'origine) : broches plus larges, signaux 5 V, beaucoup
  mieux documentées. Tu valides toute la chaîne, puis tu portes sur MGB.
- **Retour à l'émulateur** (Spec v1) : le module écran fonctionne à l'identique, seule la
  source change. C'est exactement pour ça qu'on garde le protocole commun.

---

## 5. Module SOURCE — brochage du Pico 2 W « sniffer »

| GPIO | Signal | Sens | Note |
|---|---|---|---|
| **GP0** | LD0 | entrée | Base du groupe `in pins` du PIO |
| **GP1** | LD1 | entrée | Contigu à GP0 : `in pins, 2` en une instruction |
| **GP2** | CPG (horloge pixel) | entrée | `wait 1 gpio 2` dans le PIO |
| **GP3** | CPL (HSYNC) | entrée | IRQ GPIO ou 2ᵉ machine d'état |
| **GP4** | ST / FR (VSYNC) | entrée | IRQ GPIO |
| GP5 | *(réserve)* CP/CPV | entrée | Utile si CPL se révèle capricieux |
| GP16 / GP17 | UART0 TX / RX de debug | sortie/entrée | ⚠️ Voir la note ci-dessous |
| GND (broches 3, 8, 13, 18, 23, 28, 33, 38) | Masse commune | — | Relie **au moins deux** broches GND à la masse console |

> ⚠️ **Piège** : l'UART de debug par défaut du SDK est sur **GP0/GP1**, occupés ici par LD0/LD1.
> Déplace la console série sur GP16/GP17 (`PICO_DEFAULT_UART_TX_PIN=16`, `..._RX_PIN=17`) ou
> passe en USB CDC. Sinon les traces `printf` écrasent tes entrées.

### 5.1 Architecture firmware

- **PIO SM0** — boucle de 3 instructions : `wait 0 gpio 2` / `wait 1 gpio 2` / `in pins, 2`.
  Autopush tous les 32 bits = 16 pixels. À 150 MHz de sysclk, la limite théorique est ~50 M
  pixels/s pour un signal à ~4 MHz max : **marge de plus de 10×**.
- **DMA** — le FIFO du PIO alimente le framebuffer sans intervention du CPU.
- **Framebuffer** — 160 × 144 × 2 bits = **5 760 octets**. Double tampon = 11,5 ko sur les
  520 ko de SRAM du RP2350. Aucune tension mémoire.
- **IRQ VSYNC** — bascule les tampons, remet le DMA à zéro, déclenche l'envoi.
- **Cœur 1** — dédié à la pile réseau (lwIP + CYW43). Séparer capture (cœur 0) et réseau
  (cœur 1) évite que le WiFi ne fasse rater un front d'horloge.

### 5.2 Envoi par tranches (réduction de latence)

Ne pas attendre la fin de la trame pour émettre : envoyer **dès qu'une tranche de 24 lignes
est capturée**.

```
1 ligne = 160 px × 2 bits = 40 octets
1 tranche = 24 lignes = 960 octets de données
+ en-tête 8 octets = 968 octets par datagramme  (< MTU 1500, pas de fragmentation IP)
144 lignes / 24 = 6 tranches par trame
```

En-tête proposé (compatible Spec v1 §5.3) :

| Offset | Taille | Champ |
|---|---|---|
| 0 | 2 | Magic `0x4742` (« GB ») |
| 2 | 2 | `seq` — numéro de trame (16 bits, roulant) |
| 4 | 1 | `slice` — index de tranche 0…5 |
| 5 | 1 | `lines` — nombre de lignes utiles (24, ou moins sur la dernière) |
| 6 | 1 | `flags` — bit 0 = dernière tranche de la trame |
| 7 | 1 | réservé (alignement) |
| 8 | 960 | données 2 bpp, ligne par ligne |

Le récepteur affiche dès qu'il a la dernière tranche **ou** au bout d'un délai de garde
(~5 ms). Une tranche perdue ⇒ ces 24 lignes gardent leur contenu précédent. Dégradation
discrète, pas de blocage.

### 5.3 Débit

`5 760 o × 59,73 img/s = 344 ko/s ≈ 2,75 Mbit/s` brut, conforme à ton estimation Spec v1 §5.3.
Un Pico 2 W en UDP encaisse largement ça sur un lien dédié. Deux leviers si la marge manque :

1. **RLE par ligne** — les images GB ont de grandes zones plates. Gain typique 3 à 6×.
2. **Réduire à la source** — envoyer directement du 64×58 en 4 bpp = 1 856 o/trame, soit
   **0,9 Mbit/s**. Mais on perd la propriété « le module écran est agnostique » : à garder
   comme repli, pas comme défaut.

---

## 6. Module ÉCRAN — brochage du Pico 2 W ↔ matrice HUB75

### 6.1 Le connecteur HUB75E (2×8, pas 2,54 mm)

```
        ┌──────────────────┐
   R1 ──┤ 1             2  ├── G1
   B1 ──┤ 3             4  ├── GND
   R2 ──┤ 5             6  ├── G2
   B2 ──┤ 7             8  ├── E        ← spécifique 64×64 (scan 1/32)
    A ──┤ 9            10  ├── B
    C ──┤11            12  ├── D
  CLK ──┤13            14  ├── LAT/STB
  /OE ──┤15            16  ├── GND
        └──────────────────┘
     (détrompeur en haut — vérifie sur ton panneau)
```

`R1/G1/B1` pilotent la **moitié haute** (lignes 0–31), `R2/G2/B2` la **moitié basse**
(lignes 32–63). Les deux moitiés sont écrites **simultanément**, c'est le principe du scan 1/32.

### 6.2 Table de câblage complète

| GPIO Pico 2 W | Broche physique | Signal HUB75 | Broche IDC | Rôle |
|---|---|---|---|---|
| **GP0** | 1 | R1 | 1 | 🔴 Rouge, moitié haute |
| **GP1** | 2 | G1 | 2 | 🟢 Vert, moitié haute |
| **GP2** | 4 | B1 | 3 | 🔵 Bleu, moitié haute |
| **GP3** | 5 | R2 | 5 | 🔴 Rouge, moitié basse |
| **GP4** | 6 | G2 | 6 | 🟢 Vert, moitié basse |
| **GP5** | 7 | B2 | 7 | 🔵 Bleu, moitié basse |
| **GP6** | 9 | A | 9 | Adresse ligne, bit 0 |
| **GP7** | 10 | B | 10 | Adresse ligne, bit 1 |
| **GP8** | 11 | C | 11 | Adresse ligne, bit 2 |
| **GP9** | 12 | D | 12 | Adresse ligne, bit 3 |
| **GP10** | 14 | E | 8 | Adresse ligne, bit 4 — **64×64 seulement** |
| **GP11** | 15 | CLK | 13 | Horloge de décalage |
| **GP12** | 16 | LAT / STB | 14 | Verrou de ligne |
| **GP13** | 17 | /OE | 15 | Extinction (actif bas) — porte la modulation |
| **GND** | 3, 8, 13, 18, 23, 28, 33, 38 | GND | **4 et 16** | Relie **les deux** broches de masse |

**Pourquoi ce brochage précis** : GP0–GP5 forment un groupe contigu de 6 bits (une seule
instruction PIO `out pins, 6`), GP6–GP10 un groupe contigu de 5 bits pour l'adresse de ligne.
C'est le mapping de l'exemple `hub75` de **pico-playground/pico-extras**, qui tourne tel quel
sur RP2350 — tu as donc une base de code de référence qui fonctionne.

> ⚠️ **Piège n°1** : `E` est sur la broche **8** sur la plupart des panneaux 64×64, mais
> certains fabricants la mettent sur la broche **4** (à la place d'une masse). 🔬 **Vérifie
> au multimètre** : sur ton panneau, la broche 4 et la broche 16 doivent être à la même
> masse ; si la broche 4 n'est pas à la masse, c'est là qu'est `E`.

> ⚠️ **Piège n°2** : l'UART de debug par défaut est sur **GP0/GP1** — occupés par R1/G1.
> Même remède que pour le sniffer : UART0 sur GP16/GP17, ou USB CDC.

### 6.3 Faut-il un adaptateur de niveau 3,3 V → 5 V ?

Le Pico sort du 3,3 V, le panneau attend du logique 5 V.

- Les panneaux HUB75 modernes ont en entrée un tampon **74HC245 alimenté en 5 V**, dont le
  V_IH vaut 0,7 × 5 = 3,15 V. Un 3,3 V passe donc — **de justesse**.
- En pratique, **ça marche directement dans la majorité des cas** sur nappe courte. Commence
  comme ça.
- Si tu observes des **pixels fantômes, du scintillement, des colonnes parasites** ou que
  l'image se dégrade quand la nappe s'allonge : intercale **2 × 74AHCT245** (V_IH = 2,0 V,
  très confortable) alimentés en 5 V, `DIR` au +5 V, `/OE` à la masse. 14 signaux ⇒ 2 boîtiers.
- N'utilise pas de 74HCT**04**/**14** improvisé : il faut de la vitesse (≥ 20 MHz) et de la
  cohérence de propagation entre les 14 lignes.

### 6.4 Broches libres du module écran

Après le HUB75, il reste **GP14–GP22 et GP26–GP28**.

| GPIO | Usage suggéré |
|---|---|
| GP26 (ADC0) | Potentiomètre de luminosité |
| GP16 / GP17 | UART de debug |
| GP14, GP15 | Boutons (changement de palette, mode d'affichage) |
| GP18–GP22 | Extension : chaînage d'un 2ᵉ panneau, LED d'état réseau |

> ℹ️ **Errata RP2350-E9** : sur RP2350, une broche en entrée avec **pull-down interne** peut
> se figer vers 2,1 V quand la source passe en haute impédance. Ça ne concerne pas nos
> signaux (tous activement pilotés), mais si tu ajoutes des boutons : câble-les en
> **pull-up** (bouton vers la masse), ou mets un **pull-down externe de 8,2 kΩ maximum**.

> ℹ️ **Pico 2 W** : la puce CYW43439 occupe GP23, GP24, GP25 et GP29 — mais **aucune n'est
> sortie sur le connecteur 40 broches**, donc zéro conflit avec ce plan. Conséquence à
> connaître : la **LED embarquée n'est pas sur GP25**, elle s'allume via `cyw43_arch_gpio_put()`.

### 6.5 Architecture firmware d'affichage

- **Réception** — socket UDP, réassemblage des 6 tranches par `seq`, double tampon.
- **Mise à l'échelle** — filtre boîte 2,5 × 2,48 depuis 160×144 vers 64×58, puis application
  de la palette (voir §6.6). Coût : ~3 700 pixels de sortie, négligeable pour un RP2350.
- **Rendu HUB75** — modulation binaire (BCM) sur 7 ou 8 plans de bits, PIO + DMA en chaîne.
  Budget : 32 paires de lignes × 255 unités de temps ; à 0,5 µs l'unité, ça donne
  **~245 Hz de rafraîchissement** — suffisant pour être stable à l'œil **et** en photo/vidéo,
  ce qui compte pour une pièce exposée. Vise **≥ 150 Hz**.
- **Mémoire** — plans de bits : 32 lignes × 64 px × 8 plans × 1 octet = **16 ko**. Confortable.
- **Cœurs** — cœur 0 : réseau + mise à l'échelle. Cœur 1 : entretien du rafraîchissement HUB75.

### 6.6 Palettes

On reçoit 2 bits par pixel, on rend du RGB 24 bits : la palette est un choix **libre et
changeable à chaud**. Quelques candidates, cohérentes avec ta pièce :

| Nom | 0 (clair) | 1 | 2 | 3 (foncé) |
|---|---|---|---|---|
| DMG « vert d'origine » | `#9BBC0F` | `#8BAC0F` | `#306230` | `#0F380F` |
| Pocket (STN gris-olive, fidèle au MGB) | `#E3E6C9` | `#C3C4A5` | `#8E8B61` | `#6C6C4E` |
| Contraste maxi (lisible de loin) | `#FFFFFF` | `#A8A8A8` | `#505050` | `#000000` |

⚠️ Une matrice LED est **émissive**, l'écran d'origine est **réflectif** : la palette « Pocket »
fidèle rendra un blanc terne et sale sur LED. Pour l'exposition, prévois de **remonter le
niveau 0** et d'**écraser un peu le noir**. Fais-en un réglage, pas une constante.

⚠️ Ne mets **pas** la luminosité au maximum : à P3, un panneau à 100 % éblouit à moins de
2 m et fait exploser la consommation. Prévois 20–40 % en usage courant.

---

## 7. Alimentation

### 7.1 Module écran — le budget courant

Le panneau a **4 096 LED RGB**, mais en scan 1/32 seules **2 lignes (128 pixels)** sont
allumées à un instant donné.

| Cas | Courant 5 V |
|---|---|
| Annonce constructeur (max théorique, blanc plein, 100 %) | ~3,5 – 4 A (≈ 20 W) |
| Blanc plein, luminosité 30 % | ~1,2 A |
| Palette Game Boy, image typique, 30 % | **0,4 – 0,9 A** |
| Pico 2 W avec WiFi actif | 60 – 120 mA (pointes ~250 mA à l'émission) |

**Dimensionnement retenu : 5 V / 5 A.** Tu ne l'utiliseras jamais, mais une alim à découpage
qui travaille à 20 % de sa charge chauffe peu, ne siffle pas et ne s'effondre pas sur les
pointes du WiFi. C'est 5 € de différence.

### 7.2 Schéma de distribution

```
   Secteur
      │
      ▼
  ┌─────────────┐
  │ Alim 5 V/5 A│
  └──┬───────┬──┘
     │       │
     │       └──────────► Pico 2 W : broche 39 (VSYS)
     │                    (⚠️ VSYS, pas 3V3 — voir ci-dessous)
     │                    et broche 38 (GND)
     ▼
  ┌──────────────────────────────┐
  │ Bornier d'alim du PANNEAU    │  ← fil 18–20 AWG, court
  │ +5 V  /  GND                 │  ← 1000 µF + 100 nF ICI, pas ailleurs
  └──────────────────────────────┘
```

**Règles à ne pas contourner** :

1. **Étoile de masse à l'alimentation.** Le panneau ET le Pico rejoignent la borne GND de
   l'alim par leurs propres fils. Ne fais **jamais** transiter le courant du panneau par la
   masse fine de la nappe IDC.
2. **N'alimente jamais le panneau depuis le Pico.** Le régulateur du Pico fournit ~300 mA, le
   panneau en veut jusqu'à 4 A.
3. **Alimente le Pico par VSYS (broche 39), pas par 3V3_OUT (broche 36).** VSYS accepte
   1,8 – 5,5 V et une diode Schottky embarquée gère la coexistence avec l'USB : tu peux
   brancher l'USB pour flasher sans rien débrancher. Injecter du 5 V sur 3V3_OUT détruit le
   Pico.
4. **1000 µF au plus près du bornier du panneau.** Les appels de courant HUB75 sont brutaux
   (commutation ligne par ligne à quelques centaines de Hz) ; sans réservoir local, tu auras
   du scintillement et des redémarrages.
5. **Ne débranche/rebranche jamais la nappe IDC sous tension.** Il n'y a aucune protection :
   si la masse se connecte après les signaux, le courant repasse par les entrées logiques.

### 7.3 Module source — alimentation

**Alimente le sniffer séparément de la console** : USB 5 V ou powerbank sur VSYS.

Raison : le WiFi tire des pointes de ~250 mA. La Game Boy Pocket vit sur **2 piles AAA (3 V)**
via un convertisseur dimensionné pour ~120 mA. Tirer le Pico dessus fera chuter le rail, ce
qui dégradera… les signaux LCD que tu essaies de capturer. Effet de bord vicieux.

**En revanche, la masse doit être commune** entre la console et le sniffer — sinon les
niveaux logiques n'ont pas de référence. Une seule liaison de masse, dans le faisceau des
signaux.

Si tu as installé le mod batterie USB-C, tu as un
rail 3,3 V bien plus stable : les niveaux logiques deviennent propres et constants. **C'est
un vrai argument pour faire ce mod en premier.**

---

## 8. Liaison WiFi

### 8.1 Topologie

Pour ce sous-projet, en autonomie (avant intégration à la Spec v1) :

- **Module écran = point d'accès (SoftAP)**, IP fixe `192.168.4.1`, canal fixe (1, 6 ou 11 —
  choisis le moins encombré avec un scanner WiFi).
- **Module source = client**, envoie ses datagrammes UDP vers `192.168.4.1:5000`.
- **Un seul saut, aucun routeur, aucun autre trafic.** C'est ce qui rend la latence tenable.

Pour l'intégration à la Spec v1 : les deux Pico deviennent clients du **SoftAP du Pi Zero 2 W**,
et l'adresse de destination devient celle du module écran sur ce réseau. Aucun changement de
protocole.

### 8.2 Choix de transport

**UDP, jamais TCP** — cohérent avec Spec v1 §5.2. Une trame en retard est une trame inutile :
on la jette, on n'attend pas de retransmission. Le numéro de trame (`seq`) sert à détecter les
paquets périmés et à les ignorer.

Radio : le CYW43439 est **2,4 GHz uniquement** (pas de 5 GHz). C'est la bande la plus
encombrée : le choix du canal compte vraiment en lieu public.

### 8.3 🔬 Placement de l'antenne — point souvent négligé

L'antenne du Pico 2 W est une piste imprimée en bout de carte, avec une **zone d'exclusion**
qui doit rester dégagée de tout métal et de tout plan de masse.

Or on va monter ce Pico sur un panneau LED de 192 × 192 mm à **cadre et dos métalliques**, à
côté d'une alim à découpage. C'est un environnement hostile.

**Règles de montage** :
- L'extrémité antenne du Pico **dépasse du bord du panneau d'au moins 10 mm**, dans le vide.
- **Aucun fil ne passe au-dessus ou en-dessous** de la zone d'antenne.
- Éloigne le Pico de l'alim à découpage (≥ 5 cm) et ne fais pas cheminer la nappe IDC le long
  de l'antenne.
- Si le débit s'écroule une fois le tout assemblé alors qu'il était bon sur l'établi : c'est
  presque toujours ça. Teste en écartant physiquement le Pico avant de suspecter le firmware.

### 8.4 Budget de latence

| Étape | Latence |
|---|---|
| Capture d'une tranche de 24 lignes | 2,8 ms |
| Émission UDP (pile lwIP + radio) | 1 – 3 ms |
| Traversée WiFi, 1 saut, réseau dédié | 3 – 15 ms (pointes possibles à 50 ms) |
| Réception + mise à l'échelle | < 1 ms |
| Apparition sur le panneau (rafraîchissement 245 Hz) | ≤ 4 ms |
| **Total typique** | **≈ 12 – 25 ms** |

C'est **meilleur** que le budget Spec v1 §6, parce qu'on envoie par tranches sans attendre la
fin de trame. Sur ce chemin-là il n'y a pas de boucle de jeu : les boutons et le son restent
sur la console physique, donc cette latence est purement visuelle. La console dans tes mains
répond, elle, instantanément — c'est son écran déporté qui est en retard de ~1 trame.

---

## 9. Plan de montée en puissance

Chaque étape est démontrable seule et **isole un seul risque**. Ne saute pas d'étape : si tu
branches tout d'un coup et que rien ne s'allume, tu auras cinq causes possibles.

### Étape 1 — Le panneau seul 🟢
Pico 2 W + matrice + alim 5 V. Mire de test générée localement (bandes RGB, damier, dégradé).
**Objectif** : maîtriser HUB75, BCM, luminosité, rafraîchissement.
**Critère de réussite** : dégradé 8 bits propre, pas de scintillement filmé au téléphone.

> 🔬 **Si le panneau reste noir ou très sombre avec un code HUB75 standard** : beaucoup de
> panneaux 64×64 utilisent des drivers **FM6126A / FM6124 / ICN2038S** qui exigent une
> **séquence d'initialisation par registres** avant de répondre. C'est la panne n°1 sur ces
> modules. Identifie la puce sérigraphiée au dos du panneau et cherche sa séquence d'init.

### Étape 2 — Le lien WiFi, sans Game Boy 🟢
Deuxième Pico 2 W en SoftAP client, qui envoie une **animation 160×144 synthétique** (damier
défilant, compteur de trames).
**Objectif** : protocole, fragmentation en tranches, réassemblage, gestion des pertes.
**Critère** : 60 img/s stables pendant 10 minutes, taux de perte < 1 %, latence mesurée.

### Étape 3 — La mise à l'échelle 🟢
Toujours sans console : valide le filtre boîte 160×144 → 64×58, le letterbox, les palettes,
le réglage de luminosité.
**Critère** : une image de test Game Boy réelle (capture d'émulateur) rendue proprement.

### Étape 4 — Identification des signaux 🟡
Analyseur logique sur la carte MGB ouverte, **sans rien souder de définitif** (pointes de
touche ou fils provisoires). Applique la procédure §4.2.
**Livrable** : un tableau signal → pastille, photographié et annoté. **Écris-le dans ce
document** avant de continuer.

### Étape 5 — Capture réelle 🔴
Soudures définitives + 74LVC244 + PIO de capture. Dump d'une trame en mémoire, envoi par UART
vers le PC, reconstruction en PNG.
**Critère** : le PNG correspond exactement à ce qu'affiche l'écran d'origine.

### Étape 6 — La chaîne complète 🔴
Sniffer → WiFi → matrice, en direct, manette en main.
**Critère** : jouer à Tetris en regardant la matrice.

### Étape 7 — Intégration & habillage
Basculer les deux Pico sur le SoftAP du Pi (Spec v1), monter les modules, gérer les
alimentations, faire cohabiter la source « sniffer » et la source « émulateur ».

---

## 10. Risques et parades

| Risque | Probabilité | Parade |
|---|---|---|
| Le panneau reste noir (driver FM6126A) | Élevée | Identifier la puce, appliquer la séquence d'init. Étape 1 avant tout le reste |
| Broche `E` sur pin 4 au lieu de pin 8 | Moyenne | Test de continuité à la masse sur les pins 4 et 16 |
| Signaux LCD mal identifiés | Moyenne | Analyseur logique + validation par la table de fréquences §4.2 |
| Pastille MGB arrachée | Moyenne | Colle chaude systématique, connecteur débrochable, fil fin souple |
| RP2350 grillé par du 5 V | Faible mais fatale | **Mesurer VCC avant de brancher.** 74LVC244 par défaut |
| Image d'origine dégradée par la prise | Faible | 100 Ω série, tampon haute impédance, fils < 10 cm |
| WiFi qui s'écroule une fois assemblé | Moyenne | Zone d'antenne dégagée (§8.3), canal fixe, alim éloignée |
| Scintillement / redémarrages du panneau | Moyenne | 1000 µF au bornier, étoile de masse, alim surdimensionnée |
| Texte illisible en 64×64 | **Certaine** | Assumé (§2) — ou chaînage 3×3 en v2 |
| Débit UDP insuffisant | Faible | RLE par ligne, puis réduction à la source en repli |

---

## 11. Ce qu'il reste à décider

1. **Un panneau ou neuf ?** Le 3×3 (192×192) donne du 160×144 pixel-perfect avec bordure.
   Ça change le budget, l'alim (~15 A), la mécanique et le poids — mais pas le firmware, qui
   est paramétrable dès le départ si on le prévoit maintenant. **À trancher avant l'étape 3.**
2. **Sniffer ou émulateur, à terme ?** Les deux peuvent coexister avec un sélecteur de source.
   Le sniffer est plus « vrai » ; l'émulateur est plus fiable en exposition. Une pièce d'art
   qui tourne 6 h par jour a intérêt à avoir les deux.
3. **Mod batterie USB-C sur la console d'abord ?** Ça stabilise le rail 3,3 V et rend les
   niveaux logiques propres et constants. Recommandé avant l'étape 4.
4. **Autonomie visée** — question déjà ouverte en Spec v1 : le panneau LED sur batterie est un
   tout autre problème (0,5 – 1 A en continu). Sur secteur, c'est un non-sujet.

---

## 12. Références utiles

- **pico-playground / pico-extras**, exemple `hub75` — pilote HUB75 en PIO+DMA pour RP2040,
  compatible RP2350. Base de code de référence pour l'étape 1.
- **Raspberry Pi Pico 2 W datasheet** — §« Powering Pico », brochage, zone d'exclusion antenne.
- **RP2350 datasheet** — chapitre PIO ; et la liste d'errata pour E9 (pull-downs).
- **Tutoriels du mod bivert MGB** — localisation de `LD0`/`LD1` sur la carte Game Boy Pocket.
- **Pan Docs (gbdev)** — timing du PPU : 456 cycles/ligne, 154 lignes, 59,727 img/s.
- **Spec_Gameboy_Pocket_Destructuree.md** §5.3 — format des messages, à garder synchronisé.

---

*Les blocs marqués 🔬 sont des mesures à faire sur ton matériel : ils dépendent de la révision
de ta console et du lot de ton panneau, et aucune documentation ne peut les remplacer.
Reporte les résultats dans ce fichier au fur et à mesure — c'est lui qui devient la vérité.*
