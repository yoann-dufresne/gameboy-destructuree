# Seengreat RGB Matrix P3.0-64x64 — documentation constructeur (archive locale)

Archive hors-ligne de toute la documentation du panneau LED utilisé par le module **ÉCRAN**
du projet « Game Boy Pocket déstructurée ».

| | |
|---|---|
| **Produit** | RGB Matrix P3.0-64x64 (dalle LED full-color, pas de 3 mm) |
| **Fabricant** | Seengreat |
| **Page produit** | https://seengreat.com/product/192/matrix-panel-3mm |
| **Wiki constructeur** | https://seengreat.com/wiki/74 |
| **Archivé le** | 16/09/2026 |

> ⚠️ Cette dalle est un **écran nu** : elle n'embarque aucun contrôleur. Il faut une carte
> qui génère elle-même tout le timing HUB75 (ici : un Pico 2 du module ÉCRAN).

---

## 1. Spécifications

| Caractéristique | Valeur |
|---|---|
| Pixels | 64 × 64 = 4096 LEDs |
| Pas (pitch) | 3 mm |
| Forme du pixel | 1R1G1B |
| Angle de vision | ≥ 160° |
| Connecteurs | 2 × HUB75 (un *entrée signal*, un *sortie signal* pour cascader) |
| Type de contrôle | synchrone |
| Multiplexage | **1/32 scan** (⇒ ligne d'adresse E utilisée, 5 bits A..E) |
| Alimentation | **5 V / 4 A** |
| Connecteur d'alim | embase VH 3.96 mm (4 points) |
| Dimensions | 192 × 192 × 15 mm |

Contenu de la boîte : 1 dalle + 1 câble d'alimentation VH3.96 + 1 nappe grise 16 points.

### Notes pour notre projet

- 4 A à 5 V dans le pire cas (blanc plein écran). Le rendu Game Boy (4 niveaux de vert,
  luminosité réduite) tirera bien moins, mais l'alim doit être dimensionnée pour le pic.
- **Ne jamais alimenter la dalle par le 5 V du Pico** : alim séparée, masses communes.
- 1/32 scan ⇒ pilote HUB75 « classique » à 5 lignes d'adresse.
- Le module ÉCRAN assemble 3 × 3 dalles, soit 192 × 192 pixels : l'image 160×144 de la
  Game Boy s'y affiche à l'échelle 1:1, sans conversion (cf. [`../plan-firmware.md`](../plan-firmware.md) §0).

---

## 2. Connecteur HUB75

La dalle a deux embases 16 points : *Signal Input* et *Signal Output*. On câble la carte de
contrôle sur **Signal Input** ; *Signal Output* ne sert qu'au chaînage vers une autre dalle.

Brochage physique, vu du dos de la dalle (cf. `images/wiki74/03-fig-2-1-connecteurs-hub75.jpg`).
Attention : **la sérigraphie utilise d'autres noms** que les tableaux du wiki — les lignes
d'adresse sont notées `LA LB LC LD LE` (= A B C D E) et l'*output enable* est noté `CE` (= OE).

```
              IN  (Signal input)
   R1  ● ●  G1          R1  ● ●  G1
   B1  ● ●  N           B1  ● ●  GND
   R2  ● ●  G2          R2  ● ●  G2
   B2  ● ●  LE          B2  ● ●  E
   LA  ● ●  LB    ⇔      A  ● ●  B
   LC  ● ●  LD           C  ● ●  D
  CLK  ● ●  LAT        CLK  ● ●  LAT
   CE  ● ●  GND         OE  ● ●  GND

  sérigraphie dalle     noms usuels HUB75
```

Alimentation : embase VH 3.96 mm 4 points sérigraphiée `+ + - -` → deux broches **+5 V**,
deux broches **GND**.

| Signal | Rôle |
|---|---|
| R1 / G1 / B1 | données couleur, **demi-écran haut** (lignes 0-31) |
| R2 / G2 / B2 | données couleur, **demi-écran bas** (lignes 32-63) |
| A B C D E | sélection de ligne (5 bits ⇒ 32 adresses, 1/32 scan) |
| CLK | horloge de décalage des données |
| LAT | verrou (latch) de fin de ligne |
| OE | output enable, **actif bas** (sert aussi au PWM/BCM de luminosité) |
| GND | masse (2 broches) |

---

## 3. Brochages par plateforme

### 3.1 Raspberry Pi Pico / Pico 2 — carte adaptatrice **V3.9 ou plus récente** ⭐

C'est le mapping **contigu** (GP2…GP15), donc le plus adapté à un driver PIO écrit maison :
les 6 bits RGB sont sur GP2-GP7, les 5 bits d'adresse sur GP8-GP12, le contrôle sur GP13-GP15.

| Signal | Pico | Signal | Pico |
|---|---|---|---|
| R1 | GP2 | G1 | GP3 |
| B1 | GP4 | GND | GND |
| R2 | GP5 | G2 | GP6 |
| B2 | GP7 | E | GP12 |
| A | GP8 | B | GP9 |
| C | GP10 | D | GP11 |
| CLK | GP13 | LAT | GP14 |
| OE | GP15 | GND | GND |

*(Table 2-3 du wiki.)*

> ℹ️ **Ces brochages ne sont contraignants que si on utilise la carte adaptatrice Seengreat.**
> La dalle, elle, ne connaît que le HUB75 : en nappe directe Pico ↔ dalle, on choisit
> librement ses GPIO. Le module ÉCRAN du projet a son propre mapping
> (**GP0–GP5** RGB, **GP6–GP10** adresse, **GP11** CLK, **GP12** LAT, **GP13** /OE),
> qui fait foi dans [`../plan-firmware.md`](../plan-firmware.md) §2.3, avec sa fiche de
> câblage [`../cablage-pico-hub75.html`](../cablage-pico-hub75.html) — même principe de
> contiguïté pour le PIO, mais décalé d'un cran vers le bas.

### 3.2 Raspberry Pi Pico / Pico 2 — carte adaptatrice **V3.8 et antérieure**

Mapping utilisé par le code de démo CircuitPython archivé ici
(`demo-code/extrait/Pico_RGB_Matrix_LED_64x64_/…/main.py`).

| Signal | Pico | Signal | Pico |
|---|---|---|---|
| R1 | GP2 | G1 | GP3 |
| B1 | GP4 | GND | GND |
| R2 | GP5 | G2 | GP8 |
| B2 | GP9 | E | GP22 |
| A | GP10 | B | GP16 |
| C | GP18 | D | GP20 |
| CLK | GP11 | LAT | GP12 |
| OE | GP13 | GND | GND |

*(Table 2-2 du wiki.)*

### 3.3 Raspberry Pi (GPIO natif, bibliothèque hzeller)

| Signal | BCM | Signal | BCM |
|---|---|---|---|
| R1 | 11 (SCLK) | G1 | 27 |
| B1 | 7 (CE1) | GND | GND |
| R2 | 8 (CE0) | G2 | 9 (MISO) |
| B2 | 10 (MOSI) | E | 15 (RXD) |
| A | 22 | B | 23 |
| C | 24 | D | 25 |
| CLK | 17 | LAT | 4 |
| OE | 18 | GND | GND |

*(Table 2-1 du wiki.)*

### 3.4 Arduino Mega

| Signal | Mega | Signal | Mega |
|---|---|---|---|
| R1 | D24 | G1 | D25 |
| B1 | D26 | GND | GND |
| R2 | D27 | G2 | D28 |
| B2 | D29 | E | A4 |
| A | A0 | B | A1 |
| C | A2 | D | A3 |
| CLK | D11 | LAT | D10 |
| OE | D9 | GND | GND |

*(Table 2-4 du wiki.)*

### 3.5 ESP32

| Signal | ESP32 | Signal | ESP32 |
|---|---|---|---|
| R1 | IO25 | G1 | IO26 |
| B1 | IO27 | GND | GND |
| R2 | IO14 | G2 | IO12 |
| B2 | IO13 | E | IO32 |
| A | IO23 | B | IO22 |
| C | IO5 | D | IO17 |
| CLK | IO16 | LAT | IO4 |
| OE | IO15 | GND | GND |

*(Table 2-5 du wiki.)*

---

## 4. Mise en œuvre selon la plateforme

### Raspberry Pi
Bibliothèque de référence : https://github.com/hzeller/rpi-rgb-led-matrix

```bash
sudo git clone https://github.com/hzeller/rpi-rgb-led-matrix
cd rpi-rgb-led-matrix
sudo make
cd examples-api-use
sudo ./demo -D 9 --led-no-hardware-pulse --led-rows=64 --led-cols=64
```

Précautions imposées par le constructeur :
1. Couper l'audio embarqué : `dtparam=audio=off` dans `/boot/config.txt` — le circuit audio et
   le timing du RGB-Matrix ne peuvent pas cohabiter.
2. Ne rien faire tourner d'autre sur les GPIO en parallèle.
3. Désactiver le 1-Wire : `raspi-config` → *Interface Options* → *1-Wire*.
4. Ajouter `isolcpus=3` à la fin de `/boot/cmdline.txt` (séparé par un espace).

### Raspberry Pi Pico / Pico 2
Deux démos fournies, toutes deux en **CircuitPython** (lib `rgbmatrix` d'Adafruit) :
- **bit-bang** : le CPU pilote les GPIO à la main, reproduit le timing HUB75 en logiciel.
  Simple à lire, bon pour comprendre le protocole, mais coûteux en CPU.
- **PIO** : délègue la génération du timing aux machines à états PIO du RP2040. C'est la voie
  à suivre pour un affichage fluide, et c'est ce qu'on veut pour le module ÉCRAN.

Mise en route : câbler, ouvrir Thonny, copier tout le contenu du dossier de démo sur le Pico,
ouvrir `main.py`, *Run*.

### Arduino Mega
Ouvrir `demo-code/extrait/Arduino_Mega_RGB_Matrix_64x64/Arduino_Mega_RGB_Matrix_64x64.ino`,
*Verify* puis *Upload*. La démo affiche du texte et des images en boucle.

### ESP32
IDE utilisé par le constructeur : arduino-ide 2.3.2 (Windows 64 bits).
1. Dézipper `ESP32_Packages` (Google Drive, lien plus bas) et copier le dossier `esp32`
   dans `…/Arduino15/packages`.
2. Copier `ESP32/libraries/*` dans `Documents/Arduino/libraries`.
3. Le dossier `ESP32` contient 4 exemples : `SimpleTestShapes`, `PatternPlasma`,
   `BouncingSquares`, `AurroraDemo`.
4. Choisir la carte et le port, *Verify*, *Upload*.

---

## 5. Cascader plusieurs dalles

Avec deux dalles A et B :
- MCU → **Signal Input** de A (nappe grise 16 points) ;
- **Signal Output** de A → **Signal Input** de B (seconde nappe) ;
- les deux dalles sont alimentées **chacune** en 5 V, simultanément.

Schéma : `images/wiki74/09-fig-2-5-cascade-multi-ecrans.png`.
Pour 4 dalles sur ESP32, voir `demo-code/4_screens_on_ESP32.pdf`.

---

## 6. Afficher une image

- **Pico** : la démo lit directement un BMP. Préparer un BMP RGB888 (24 bits). Pour du
  128×64 (2 dalles), passer `unit_width = 128` (ligne 13 de `main.py`) et remplacer le nom de
  fichier ligne 132 (`self.image = 'wales_128x64.bmp'`). Tester avec `RGB.test(1)`.
- **ESP32 / Arduino** : pas de lecture de fichier — il faut convertir l'image en tableau C avec
  le logiciel **Image2Lcd**, puis coller le tableau à la fin de `bit_bmp.h`.
  Tutoriel constructeur archivé : `source/wiki-159-image-conversion-tutorial.html`
  (original : https://seengreat.com/wiki/159) ; le logiciel lui-même est dans
  `outils/Image2Lcd.rar`. Pour du 128×64 : régler *Maximum Width* = 128 et
  *Maximum Height* = 64 → fichier `.c` contenant un tableau de 16384 octets.
  Dans `showbitmap.ino`, la ligne 11 vaut `#define PANEL_CHAIN 2`.

---

## 7. Contenu de cette archive

```
docs/seengreat-rgb-matrix-p3-64x64/
├── README.md                       ← ce fichier (synthèse FR de la doc constructeur)
├── source/                         ← pages web d'origine (images ré-écrites en local)
│   ├── wiki-74-rgb-matrix-p3-0-64x64.html      (wiki principal)
│   ├── wiki-159-image-conversion-tutorial.html (conversion d'image Image2Lcd)
│   └── product-192-matrix-panel-3mm.html       (fiche produit)
├── images/
│   ├── wiki74/    ← figures 2-1 à 2-8, brochages, photos produit
│   ├── wiki159/   ← captures Image2Lcd
│   └── produit/   ← visuels fiche produit (dont cotes et brochage)
├── demo-code/
│   ├── Pico_RGB_Matrix_LED_64x64_(adaptateur_V3.8_et_anterieur).zip
│   ├── Pico-RGB_Matrix_LED_64x64-V3.9_(adaptateur_V3.9+).rar
│   ├── Pico-RGB_Matrix_LED_128x64_(cascade_2_ecrans).rar
│   ├── Arduino_Mega_RGB_Matrix_64x64.zip
│   ├── ESP32_demo_codes_128x64_et_128x128.rar   (48 Mo)
│   ├── 4_screens_on_ESP32.pdf                   (cascade 4 dalles sur ESP32)
│   └── extrait/                                 ← contenu décompressé des .zip
│       ├── Pico_RGB_Matrix_LED_64x64_/{PIO,bit-bang}/
│       └── Arduino_Mega_RGB_Matrix_64x64/
├── github-seengreat-RGB-Matrix-P3.0-64x64/      ← copie du dépôt GitHub (variante RBG)
├── outils/
│   └── Image2Lcd.rar                            (conversion image → tableau C)
├── mecanique/
│   └── P3QD-64X64-21A-PRO.dwg                   (plan mécanique AutoCAD)
└── cartes-reception/                            ← cartes de réception Colorlight (non utilisées ici)
    ├── Colorlight_5A-75B_6124-138.rcvbp
    └── Colorlight_5A-75E_rcvbp.rar
```

Les `.rar` sont archivés tels quels, sans version décompressée. Pour les ouvrir :
`sudo apt install unar`, puis `unar <fichier>.rar`.

### Ressources restées en ligne

| Ressource | Lien | Pourquoi pas archivée |
|---|---|---|
| `ESP32_Packages` | https://drive.google.com/file/d/1i9FywdbV2cuVglfrSeyZXViWyhtUd8vm/view | Google Drive, pas de lien direct |
| Bibliothèque Raspberry Pi | https://github.com/hzeller/rpi-rgb-led-matrix | dépôt volumineux et actif, à cloner au besoin |
| Support technique Seengreat | https://seengreat.com/site/support | page de contact |

### Note du constructeur

Si la dalle reçue affiche en **RBG** au lieu de RGB, utiliser le code du dépôt
https://github.com/seengreat/RGB-Matrix-P3.0-64x64 — copie locale dans
`github-seengreat-RGB-Matrix-P3.0-64x64/` (commit `85c759a`, 10/01/2024).
