# Module MANETTE : la carte

*Sous-projet « manette » de la Game Boy Pocket déstructurée.*
Version 2, 15/11/2026 : le schéma est fait (C1) et la géométrie posée (C2) ; le LIS2DH12 remplace
l'ADXL362 ; les molettes ne sont alimentées que le temps d'une mesure.
Version 1, 15/11/2026 : le plan de conception de la carte qui remplace celle d'une Game Boy
Pocket, avec neuf LED RGB derrière des boutons transparents.

Le pourquoi du module est dans [`plan.md`](plan.md), son pas à pas dans
[`etapes.md`](etapes.md). Ce document décide pour la carte, et l'emporte sur `plan.md` là où ils
diffèrent : la place du Pico, les lumières, les molettes, le brochage, la nomenclature.

**Le principe.** Une vraie coque de Game Boy Pocket, qui paraît normale, sauf qu'elle n'a pas
d'écran. Dedans, une seule carte, dessinée pour elle : elle lit la croix, les quatre boutons,
les deux molettes et l'interrupteur, et les envoie par un Pico 2 W. Deux piles AAA
l'alimentent, comme la console d'origine. Neuf LED RGB éclairent les boutons, transparents, et
la fenêtre de la LED d'alimentation.

---

## 0. Décisions

La dernière colonne dit qui a tranché : toi, à une date, ou l'étape qui confirme ou renverse une
proposition (§7).

| Sujet | Décision ou proposition | Tranchée par |
|---|---|---|
| **Une carte dessinée** | elle remplace la carte d'origine. Modifier la carte d'origine est écarté : son processeur reste sur les lignes des boutons, et des LED derrière les boutons ne s'y câblent pas proprement | toi, 14/11/2026 |
| **Point de départ** | les fichiers KiCad de [YAPoco](https://github.com/nataliethenerd/YAPoco), de Natalie the Nerd : contour, vis, pastilles et emplacements des pièces, pour une coque de Pocket. La carte de la manette passe donc sous **CC BY-SA 4.0**, avec attribution ; le reste du dépôt reste sous AGPL | toi, 15/11/2026 |
| **Quatre manettes** | à terme : on compte cinq jeux de pièces, dont un de rechange | toi, 15/11/2026 |
| **Derrière la vitre** | un cache noir à la place de l'écran ; le Pico 2 W dessous | toi, 15/11/2026 |
| **Aucune ouverture ajoutée** | pas de port à l'extérieur. Le firmware se met à jour par le WiFi ; l'USB du Pico ne sert qu'en secours, la carte sortie de la coque. La fente de cartouche et les trous du port link, de la prise casque et de la prise DC restent vides | toi, 15/11/2026 |
| **Neuf LED RGB** | quatre sous la croix, une par direction ; une sous A, B, Start et Select ; la LED d'alimentation. Croix, boutons et membranes transparents | toi, 15/11/2026 |
| **Qui décide des couleurs** | le Pico de la manette : seul, et sur ordre reçu par le WiFi | toi, 15/11/2026 |
| **Pièces de rechange** | l'interrupteur de rechange de Silent Modding ; son potentiomètre 10 kΩ pour les deux molettes | toi, 14/11/2026 ; le même potentiomètre pour les deux molettes : proposition, C0.4 |
| **Piles** | deux AAA, comme la console d'origine | toi, 14/11/2026 |
| **Fabrication** | le dessin d'abord, puis le comparatif des devis de JLCPCB et de PCBWay, port et frais d'importation compris | toi, 15/11/2026 |
| **Outil** | KiCad 10 | toi, 15/11/2026 |
| Méthode | celle de la carte de l'écran ([`ecran/materiel`](../../ecran/materiel/README.md), §9) : une description électrique en Python, source unique, d'où des scripts tirent le schéma et le circuit ; on ne retouche pas les fichiers KiCad à la main | proposition |
| LED adressables | une chaîne de LED « WS2812 », sur une seule broche du Pico. Un élévateur de tension les alimente, que le Pico coupe ; un cavalier permet de les alimenter plutôt par le 3,3 V du Pico, si les LED choisies s'en contentent | proposition ; C1, C8 |
| Emplacement des LED | dans le creux des dômes des membranes, ou montées par le dos de la carte et éclairant par un trou | proposition ; C0.6, C0.7 |
| Composants CMS sur une seule face | la face avant, sous le cache et sous les membranes : un assemblage sur une face coûte moins cher, et le dos reste nu là où on le voit par la fente de cartouche | proposition ; C0.2, C4 |
| La carte | deux couches, comme YAPoco ; l'épaisseur de la carte d'origine ; finition or (ENIG) pour les pastilles des boutons ; vernis noir | proposition ; C0.2 |
| Accéléromètre | le LIS2DH12, qui réveille la carte par une interruption verrouillée ; le Pico compte la minute d'immobilité (§4.2) | proposition, 15/11/2026 ; C8 |
| Reste du plan du 11/10 | Pico 2 W soudé à plat et alimenté par les piles ; sommeil hors tension, réveil par l'accéléromètre ; une broche par bouton ; couleurs de la LED d'alimentation ; `MAN1` | décisions et propositions du 11/10, inchangées |

---

## 1. Ce que porte la carte

```
 piles 2 x AAA     protection            maintien
 1,8 à 3,2 V ----- contre l'inversion --+-- P-MOSFET --+-- P-MOSFET de l'USB -- VSYS : Pico 2 W --> 3,3 V
                                        |      ^       |
                                        |      |       +-- élévateur ~4,5 V, coupé par le Pico
                                        |      |                 |
                                        |      |                 +-- 9 LED RGB adressables, en chaîne
                                        |      |
                                        |   tenu par : l'interrupteur sur marche,
                                        |              l'accéléromètre qui sent un mouvement,
                                        |              le Pico lui-même
                                        |
                                        +-- accéléromètre, toujours alimenté ; SPI vers le Pico
                                            par un adaptateur qui l'isole quand le Pico est éteint

 croix, A, B, Select, Start --> 8 broches        molettes --> ADC0, ADC1
 interrupteur --> 1 broche, et le maintien       LED <-- 1 broche (PIO)
```

Rien d'autre : ni processeur de Game Boy, ni écran, ni son, ni connecteur de cartouche.

---

## 2. La géométrie

### 2.1 Ce que donne YAPoco

[YAPoco](https://github.com/nataliethenerd/YAPoco) (« Yet Another Pocket Color ») met une Game
Boy Color dans une coque de Pocket. Sa carte est dessinée sous KiCad, en deux couches, pour les
pièces d'une Game Boy Color, et publiée sous CC BY-SA 4.0. Son auteur demande une épaisseur de
1 mm, et conseille une coque FunnyPlaying, avec laquelle aucune retouche n'est nécessaire.

Relevé dans `YAPoco.kicad_pcb` le 15/11/2026, vue de face, origine au coin haut-gauche du
contour, y vers le bas :

| Élément | Dans YAPoco | Pour la manette |
|---|---|---|
| Contour | 69,5 × 114,4 mm ; encoche du haut-parleur en bas à droite | repris |
| Trous de vis et de centrage | | repris |
| Croix | quatre pastilles autour de (12 ; 77) | reprises ; une LED près de chacune |
| A, B | vers (62 ; 73) et (48 ; 77) | reprises ; une LED chacun |
| Select, Start | vers (29 ; 94) et (40,5 ; 94) | reprises ; une LED chacun |
| Contacts de piles | deux fentes, + vers (45,5 ; 71) et - vers (54 ; 90), dans la zone de A et B | reprises ; vérifiées sur les contacts de rechange (C0.5) |
| Interrupteur | en haut à gauche, au dos ; une pièce de Game Boy Color | empreinte vérifiée sur l'interrupteur de rechange de Pocket (C0.5) |
| Molette de gauche | au dos, tétons centrés vers y = 39,6 : le potentiomètre de volume de la Game Boy Color, 10 kΩ double, à 5 pattes | même place : c'est la pièce que Silent Modding vend pour les deux consoles, à vérifier (C0.4) |
| Molette de droite | tétons centrés vers y = 25,4 : un interrupteur à molette, pour le menu d'un écran IPS | le même potentiomètre, retourné, ses tétons à la même distance du bord que ceux de gauche (C0.4) |
| LED d'alimentation | 3 mm, traversante, en haut à gauche, vers (5 ; 15) | une LED RGB adressable au même endroit (§4.4) |
| Port link (bord gauche), prises DC et casque (bord bas), connecteurs de cartouche et d'écran | présents | absents, décision du 15/11 |
| Épaisseur | 1,6 mm dans le fichier, 1 mm dans son mode d'emploi | celle de ta carte d'origine (C0.2) ; `plan.md` supposait 1,2 mm |

YAPoco met le volume à gauche. Quelle molette est le volume, quelle autre le contraste, sur une
Pocket d'origine : C0.4 le relève sur ta console. Le contraste devient la luminosité de l'écran
([`plan.md`](plan.md) §0).

### 2.2 Ce qui se vérifie sur une vraie Pocket

YAPoco a été dessinée pour des pièces de Game Boy Color, et pour une coque FunnyPlaying. Avant
de la reprendre, C0.3 la superpose à ta carte d'origine, et C3 l'essaie en carton dans ta coque,
avec les membranes, l'interrupteur et les molettes. **Pas de commande avant.**

---

## 3. Le placement

### 3.1 Le Pico sous la vitre

- **Face avant, à la place de l'écran, sous le cache noir.** Le Pico 2 W mesure 51 × 21 mm ;
  l'écran laisse une place d'environ la largeur de la carte sur la moitié de sa hauteur.
- **L'antenne au bord haut de la carte.** L'empreinte `RaspberryPi_Pico_W_SMD` de la
  bibliothèque de KiCad, faite aussi pour le Pico 2 W, interdit le cuivre sous l'antenne, sur
  14,2 × 8 mm, et sur 10 mm au-delà du bout du module. Le Pico debout, antenne en haut :
  au-delà du bord, il n'y a que le plastique de la coque, et les piles sont à l'autre bout de la
  console. Couché, l'antenne serait contre le trou vide du port link, ou contre une molette.
- **La hauteur** entre la carte et la vitre, écran retiré, doit loger le Pico et le cache :
  C0.2 la mesure, C0.8 mesure le Pico. Si elle manque, le Pico passe au dos, derrière les
  boutons.
- **Le secours.** La prise micro-USB et le bouton BOOTSEL du Pico restent accessibles carte
  sortie de la coque. Au quotidien, rien n'y passe : le firmware se met à jour par le WiFi
  (§6).
- **Le cache** : une feuille noire mate, découpée sur le gabarit, derrière l'ouverture de
  l'écran dans la coque avant. Il cache le Pico et ce qui l'entoure.

### 3.2 Les LED sous les membranes

- **La pastille de contact sous chaque bouton est en carbone noir**, même sur une membrane
  transparente : la lumière doit passer à côté. Donc ou bien une LED très basse, posée dans le
  creux du dôme, à côté des pastilles de cuivre ; ou bien une LED montée par le dos de la
  carte, qui éclaire par un trou.
- **Ailleurs, la membrane repose à plat sur la carte** : rien ne doit dépasser sous elle hors
  des dômes, sans quoi les appuis se perdent.
- **C0.6 mesure les membranes, C0.7 essaie la lumière** avec les pièces transparentes, avant de
  placer quoi que ce soit. La carte LED de Natalie the Nerd pour la Pocket, d'une seule couleur,
  conseille des membranes blanches avec des boutons transparents, pour une lueur égale : C0.7
  compare, si les blanches sont disponibles.
- **Les LED montées par le dos** imposeraient un assemblage sur les deux faces, plus cher, et
  des composants au dos de la carte : C0.7 les départage aussi sur ce point.

### 3.3 Le reste

- **Face avant, autour du Pico, sous le cache** : le maintien, l'accéléromètre et son
  adaptateur, l'élévateur des LED, les condensateurs. Tout ce qui est CMS, si la place suffit.
- **Face avant, en bas** : les pastilles des boutons et leurs LED, sous les membranes, et rien
  d'autre.
- **Au dos** : l'interrupteur, les deux molettes et les contacts de piles, traversants, soudés à
  la main, et les points de test (§4.8). La zone qu'on voit par la fente de cartouche reste nue,
  en vernis noir.
- **L'élévateur** loin de l'antenne, avec une boucle de courant courte : c'est la seule source de
  bruit de la carte.

---

## 4. L'électronique

### 4.1 Alimentation et maintien

Le principe de `plan.md` §4.2 est gardé : **dormir, c'est être hors tension**.

- **Protection contre l'inversion des piles** : un P-MOSFET monté en diode idéale. Les piles
  AAA entrent dans le compartiment dans les deux sens.
- **Le maintien** : un P-MOSFET entre les piles et le Pico, fermé tant que l'un des trois tient
  sa grille à la masse, chacun par un N-MOSFET à seuil bas, conducteur dès 1,8 V :
  l'interrupteur sur marche, l'interruption de l'accéléromètre, et le Pico, par GP11.
- **L'interrupteur envoie la tension des piles**, et non la masse. Ainsi aucune résistance de
  tirage vers les piles ne touche une broche du Pico endormi, ce qui ferait fuir du courant par
  ses protections. Le Pico lit l'interrupteur sur GP10, à travers une résistance.
- **L'USB** : le P-MOSFET de la datasheet du Pico 2 W, grille sur VBUS, coupe les piles quand
  l'USB est branché : l'USB ne les recharge jamais.
- **La tension des piles** : VSYS / 3 sur ADC3, dans le Pico (`plan.md` §5.3).
- **Des cavaliers à souder** contournent le maintien et coupent l'élévateur : si l'un des deux
  échoue sur les premières cartes, la carte vit quand même.

### 4.2 L'accéléromètre

- **Le LIS2DH12, retenu le 15/11/2026** à la place de l'ADXL362 de `plan.md` §4.2. L'ADXL362
  coûte 13 $ pièce chez JLCPCB, qui en a peu en stock ; le LIS2DH12 coûte moins d'un dollar. Il
  consomme 2 à 3 µA au lieu de 0,27 : à ce courant, deux piles de 800 mAh tiendraient encore des
  dizaines d'années. Il est alimenté directement par les piles.
- **Le réveil : une interruption verrouillée.** Au premier mouvement, INT1 monte, et reste haute
  jusqu'à ce que le Pico la lise (`LIR_INT1`). Elle ferme le maintien ; le Pico démarre, le tient
  par GP11, et lit l'interruption.
- **Éveillé, c'est le Pico qui compte la minute d'immobilité**, relancée à chaque interruption. Au
  bout, il relâche GP11. Si une interruption est arrivée entre-temps, la carte reste sous
  tension : le Pico le voit, la lit, et repart pour une minute. S'il plante, le chien de garde le
  redémarre, GP11 retombe, et la carte s'éteint.
- **La fonction « activité » du LIS2DH12** (`I2_ACT`, sur INT2) passe en veille après un temps
  d'immobilité réglable, et revient au mouvement. Sa fiche ne dit pas si INT2 tient un niveau
  pendant tout le mouvement. Si oui, l'accéléromètre compterait la minute lui-même, comme
  l'ADXL362.
- **Le cavalier JP3 choisit la broche qui tient le maintien** : INT1 par défaut, INT2 en coupant le
  cavalier puis en le refermant de l'autre côté. Les deux broches ne sont jamais reliées. C8
  essaie INT2 ; le firmware suit.
- **Son SPI passe par un adaptateur qui isole**, le TXU0304. Pico hors tension, les sorties de
  l'adaptateur sont en haute impédance : rien ne passe entre l'accéléromètre et le Pico. Sa broche
  OE est sur le 3,3 V du Pico.
- **CS tiré vers la tension des piles, par 330 kΩ** : l'accéléromètre reste désélectionné pendant
  que le Pico dort. Les tirages internes de 5 MΩ de l'adaptateur demandent 1 MΩ au plus.
- **Son interruption** commande un N-MOSFET du maintien. Le Pico la lit sur GP12, à travers 47 kΩ.

### 4.3 Les LED

- **Neuf LED adressables en chaîne**, sur une seule broche du Pico, pilotées par un PIO, comme
  l'exemple `ws2812` du pico-sdk. Dans l'ordre : la LED d'alimentation, la croix (haut, droite,
  bas, gauche), Select, Start, B, A.
- **Un élévateur de tension** : une LED bleue demande près de 3 V, les piles en donnent de 1,8 à
  3,2. Il prend le courant après le maintien, sort environ 4,5 V, et le Pico le met en marche par
  GP20. Une résistance le tient arrêté tant que le Pico ne dit rien.
- **Coupé, il ne laisse rien passer** : la plupart des LED adressables consomment de 0,3 à
  0,6 mA chacune, même éteintes ; l'élévateur coupé doit les séparer des piles (§4.9).
- **Ou le 3,3 V du Pico** : les LED « -V6 » de Worldsemi consomment au plus 1 µA éteintes, et
  sont données à partir de 3,3 V. Si C0.6 leur fait une place, un cavalier branche la chaîne sur
  le 3,3 V du Pico, qui s'éteint avec lui, en restant sous le courant que sa fiche autorise : les
  cartes suivantes se passent alors de l'élévateur. Les premières cartes ont les deux, et C8
  compare les couleurs, le bleu surtout, à 3,3 V et à 4,5 V.
- **Le niveau de la ligne de données** : une LED adressable lit un 1 au-dessus d'une fraction de
  sa tension d'alimentation, de 0,55 à 0,78 selon le modèle. Sous 4,5 V, les 3,3 V du Pico
  suffisent jusqu'à 0,65, soit 2,9 V : on écarte les LED plus exigeantes, et il n'y a pas
  d'adaptateur de niveau.
- **L'éclat est plafonné par le firmware**, et l'élévateur dimensionné pour ce plafond. Ce que
  coûtent les LED, et donc l'autonomie, se mesure en C8 (`plan.md` §9, objectif 6 heures).

### 4.4 La LED d'alimentation

La première de la chaîne, dans la fenêtre de la LED d'origine, en haut à gauche. La fenêtre a
le diamètre d'une LED de 3 mm, et LCSC n'a pas de LED adressable ronde de 3 mm : les plus
petites font 5 mm. Ce sera donc une LED CMS de la chaîne, sous un guide de lumière de 3 mm, une
tige d'acrylique coupée à la hauteur de la fenêtre. Ses couleurs ne changent pas : rouge fixe
appairée, bleu pulsé quand la console ne répond pas, éteinte sur arrêt (`plan.md` §5.6).

### 4.5 Les boutons

Les pastilles de YAPoco : une moitié à la masse, l'autre sur une broche du Pico, tirée vers le
haut. Une broche par bouton, tous les appuis simultanés lus (`plan.md` §5.3). Une empreinte de
résistance de tirage externe par bouton, à poser si T0.5 trouve des pastilles de carbone trop
résistantes.

### 4.6 Les molettes

Le potentiomètre de rechange de Silent Modding : 10 kΩ, double, à 5 pattes d'après l'empreinte de
YAPoco. Les broches 1 et 2 sont les hauts des deux pistes, 4 et 3 leurs curseurs, 5 le commun.

- **Les deux hauts sont reliés** : quelle que soit la piste du curseur 4, il donne la position.
- **Le curseur 4 va sur ADC0 ou ADC1**, avec 1 nF vers la masse.
- **Le Pico alimente les molettes par GP21, le temps d'une mesure.** Sur le 3,3 V, deux
  potentiomètres doubles tireraient 1,3 mA en permanence, soit quelques pour cent de
  l'autonomie. Alimentés quelques dizaines de microsecondes par lecture, ils ne coûtent plus
  rien : le 1 nF se charge en quelques microsecondes.
- **`ADC_VREF` reste libre** : elle sert aussi à mesurer la tension des piles, et une charge la
  fausserait.
- **La mesure reste proportionnelle à la position** : GP21 sort à peu près la tension
  d'`ADC_VREF`, et le firmware cale les butées (T1.3).

La fiche du vendeur dit qu'en molette de contraste, il n'atteint pas le blanc. C'est une limite du
circuit de contraste de la console, pas la nôtre, puisqu'on lit la position du curseur.

### 4.7 L'interrupteur

L'interrupteur de rechange de Silent Modding, livré avec une petite carte « pour un
positionnement facile ». Son empreinte se relève sur la pièce et sur une Pocket (C0.5).

### 4.8 Les points de test

Au dos, accessibles en ouvrant la seule coque arrière, la carte restant en place :

- **SWD** : les trois pastilles de mise au point du Pico 2 W (SWCLK, masse, SWDIO) sont traversantes
  sur la carte. On les soude par le dos, et la soudure monte jusqu'au Pico. Une sonde s'y pose pour
  reflasher, même si une mise à jour par le WiFi a échoué ;
- **TP1 à TP12** : RUN ; la liaison série (GP0, GP1) ; GP15 et la ligne de Droite, pour refaire
  T3.4 et T5.4 ; VSYS ; l'alimentation des LED ; les piles ; le 3,3 V ; la fin de la chaîne des
  LED ; deux masses.

### 4.9 Les pièces à choisir

Relevées le 15/11/2026 sur les fiches des fabricants et le stock de JLCPCB ; choisies en C1.
Prix JLCPCB à l'unité, en dollars. **Toutes sont des pièces « Extended » chez JLCPCB** : chacune
ajoute des frais fixes à l'assemblage, que le comparatif compte (§9).

| Rôle | Candidat (référence LCSC) | Ce qui compte | Prix |
|---|---|---|---|
| LED des boutons, dans le dôme | **WS2812C-2020-V6** (C55109522) | 2,2 × 2,0 × 0,84 mm ; 5 mA par couleur ; au plus 1 µA éteinte ; donnée de 3,3 à 5,3 V ; seuil de données 0,55 VDD | 0,10 |
| | SK6805-EC14 (C2909055), si le dôme est étroit | 1,4 × 1,4 × 0,4 mm ; 5 mA ; 0,3 mA éteinte ; de 3,7 à 5,5 V ; seuil 0,65 VDD | 0,11 |
| | XL-1010RGBC-WS2812B (C5349953), la plus petite | 1,0 × 1,0 × 0,8 mm ; 5 mA ; 0,35 mA éteinte ; seuil 2,8 V | 0,08 |
| LED des boutons, par le dos | **SK6812MINI-E** (C5149201) | montage inversé, 3,2 × 2,8 × 1,78 mm, découpe de 3,8 × 3,4 mm ; 12 mA ; 0,6 mA éteinte ; de 3,7 à 5,5 V ; seuil 0,65 VDD | 0,08 |
| LED d'alimentation | une LED de la chaîne, sous un guide de lumière | pas de LED adressable ronde de 3 mm chez LCSC (§4.4) | |
| Élévateur | **TPS61099** (C2842395) | de 0,7 à 5,5 V en entrée ; sortie séparée de l'entrée à l'arrêt ; 0,5 µA arrêté ; environ 0,2 A sous 4,5 V avec 2,0 V en entrée ; WSON-6 de 2 × 2 mm | 1,31 |
| | TPS61070 (C9841), en repli | de 0,9 à 5,5 V en entrée ; sortie séparée ; 19 µA en marche ; SOT-23-6 | 0,69 |
| Adaptateur de l'accéléromètre | **TXU0304** (C4364118 en TSSOP-14, C2959768 en UQFN-12) | 3 voies vers l'accéléromètre, 1 vers le Pico ; sorties en haute impédance si l'une des alimentations manque ; 1 µA côté piles, Pico éteint ; tirages internes de 5 MΩ vers la masse, donc un tirage de CS de 1 MΩ au plus | 0,64 |
| Accéléromètre | **LIS2DH12TR** (C110926), retenu | 2 à 3 µA ; LGA-12 de 2 × 2 mm, brochage du LIS2DE12 de KiCad (§4.2) | 0,89 |
| | ADXL362BCCZ (C59641), écarté | 0,27 µA en veille de mouvement ; LGA-16 ; peu de stock | 13,18 |
| P-MOSFET : inversion, maintien, USB | **DMG2305UX-7** (C150470) | 200 mΩ au plus sous -1,8 V ; SOT-23 | 0,50 |
| N-MOSFET du maintien | **PMV16XNR** (C110920) | seuil de 0,4 à 0,9 V ; 33 mΩ au plus sous 1,8 V ; SOT-23 | 0,14 |
| | DMG1012T-7 (C20512) | seuil de 0,5 à 1,0 V ; SOT-523 | 0,09 |

Écartés : la SK6805-EC15, dont le seuil de données, 0,78 VDD, dépasse les 3,3 V du Pico sous
4,5 V ; le TPS61023, qui ne démarre qu'au-dessus de 1,7 à 1,8 V, la tension de piles vides ; le
TPS613222A, qui n'a pas de coupure ; les adaptateurs SN74AVC4T774 et SN74AXC4T774, qui tirent 8 à
10 µA des piles quand le Pico dort.

---

## 5. Le brochage

Celui de la phase 1 du 11/10, sauf la LED d'alimentation, qui rejoint la chaîne, et les molettes,
qu'alimente GP21 (§4.6). Il fait foi pour la plaque d'essai comme pour la carte, et `conception.py`
le reprend tel quel.

| Rôle | GPIO | Broche du Pico |
|---|---|---|
| Droite, Gauche, Haut, Bas | GP2, GP3, GP4, GP5 | 4, 5, 6, 7 |
| A, B, Select, Start | GP6, GP7, GP8, GP9 | 9, 10, 11, 12 |
| Interrupteur, lecture | GP10 | 14 |
| Maintien d'alimentation | GP11 | 15 |
| Interruption de l'accéléromètre, lecture | GP12 | 16 |
| Générateur d'appuis pour T3.4 et T5.4 | GP15 | 20 |
| Accéléromètre, SPI : MISO, CS, SCK, MOSI | GP16, GP17, GP18, GP19 | 21, 22, 24, 25 |
| Élévateur des LED, marche | GP20 | 26 |
| LED, données | GP22 | 29 |
| Volume, curseur | GP26 · ADC0 | 31 |
| Contraste (luminosité), curseur | GP27 · ADC1 | 32 |
| Molettes, alimentation le temps d'une mesure | GP21 | 27 |
| Liaison série de mise au point : TX, RX | GP0, GP1 | 1, 2 |
| Piles, après le maintien et le P-MOSFET de l'USB | VSYS, GND | 39, 38 |
| Libres | GP13, GP14, GP28 | 17, 19, 34 |

---

## 6. Ce que la carte demande au firmware

Le firmware n'est pas l'objet de ce document ; la carte lui laisse ceci à écrire :

- **Les LED** : le PIO, l'éclat plafonné, l'élévateur coupé quand tout est éteint. Interrupteur
  sur arrêt : tout est éteint, comme la LED d'alimentation aujourd'hui.
- **Les couleurs** : des effets décidés par le Pico (un bouton qui s'allume à l'appui, une
  couleur par bouton, l'état de la manette) et des ordres reçus par le WiFi. Proposition : un
  nouveau type de message `MAN1`, de la console vers la manette, qui porte la couleur de chaque
  LED ou le numéro d'un effet. [`man1.h`](../firmware/commun/man1.h) fera foi ; au banc,
  `manette_ecoute.py` l'enverra.
- **Les mises à jour par le WiFi** : le RP2350 sait garder deux images du firmware et revenir à
  l'ancienne si la nouvelle ne démarre pas. Une mise à jour ratée se rattrape par les points SWD
  (§4.8).
- **L'autonomie** : si C8 la trouve trop courte, le firmware baisse les LED après quelques
  secondes sans appui. Seuil à décider sur la mesure.

---

## 7. Les étapes

| Étape | Qui | Ce qu'on fait | Critère de sortie |
|---|---|---|---|
| **C0** · Relevés et premiers achats | toi | les relevés du §7.1 ; un jeu de chaque pièce du §8 : membranes et boutons transparents, interrupteur, deux potentiomètres, contacts de piles | la fiche du §10 remplie ; scans et photos dans `docs/releves/` |
| **C1** · Schéma | moi | la description électrique, puis le schéma KiCad qu'on en tire ; les pièces du §4.9 choisies, avec leurs références LCSC | contrôle électrique (ERC) sans erreur ; nomenclature complète |
| **C2** · Géométrie et empreintes | moi | contour, trous et pastilles repris de YAPoco par script, corrigés par C0 ; empreintes de l'interrupteur, des molettes, des contacts et des LED | superposition à ta carte d'origine, sur un tirage à l'échelle 1 |
| **C3** · Gabarit | toi | le gabarit [`gabarit-echelle-1.pdf`](../materiel/carte-manette/gabarit/gabarit-echelle-1.pdf), imprimé à 100 %, collé sur un carton, découpé, essayé dans la coque avec les membranes, l'interrupteur et les molettes | trous en face des vis, pastilles sous les boutons, molettes et interrupteur à leur fenêtre, place du Pico et du cache, coque fermée |
| **C4** · Placement, routage, contrôles | moi | placement, routage, plans de masse, sérigraphie ; un rendu 3D | contrôle de règles (DRC) sans erreur ; rien sous les membranes hors des dômes ; rien de visible par la fente de cartouche ; antenne dégagée |
| **C5** · Devis | moi, puis toi | les fichiers de fabrication pour JLCPCB et PCBWay ; le comparatif du §9 | ton choix du fabricant et du nombre de cartes assemblées |
| **C6** · Commande | toi | | |
| **C7** · Assemblage | toi | le Pico, l'interrupteur, les molettes, les contacts de piles ; première mise sous tension sur l'alimentation de laboratoire, à 3,0 V, courant limité à 100 mA, relevé ensuite pour la radio et les LED ; les piles après | VSYS, 3,3 V, maintien, puis chaque LED |
| **C8** · Essais | toi et moi | T1.1 à T1.6 sur la carte, puis T5.1 à T5.5 dans la coque ([`etapes.md`](etapes.md), phase 5) | les critères de ces essais |

C1 et C2 commencent sans attendre C0 ; le placement des LED (C4) attend C0.6 et C0.7, et la
commande (C6) attend C3.

**État au 15/11/2026 : C1 et C2 sont faites.**
- Le schéma passe le contrôle électrique de KiCad sans erreur ni avertissement, et sa netlist est
  identique à la description électrique.
- La géométrie de YAPoco est reprise à la pastille près, vérifiée par le script.
- Le gabarit de C3 est prêt.
- Le placement attend C0 et C3. Les fichiers sont décrits au §13.

### 7.1 Les relevés de C0

Sur une Pocket de référence, **une Pocket qui a la fenêtre de la LED d'alimentation**, à gauche
de l'écran : c'est celle qu'auront les quatre manettes. Elle sert aussi à T0.1 à T0.5 de
[`etapes.md`](etapes.md), qui en sont la plupart.

| Relevé | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **C0.1** Identité | la révision sérigraphiée sur la carte (`MGB-...CPU-...`) ; les deux faces photographiées et scannées à 600 ppp, une règle posée à côté (T0.1) | les scans à l'échelle |
| **C0.2** 🔬 Épaisseurs et hauteurs | au pied à coulisse : l'épaisseur de la carte ; la hauteur libre entre la carte et la vitre, écran retiré, aux quatre coins de la fenêtre de l'écran ; entre le dos de la carte et le compartiment des piles | si le Pico et son cache tiennent sous la vitre ; si des composants tiennent au dos |
| **C0.3** Superposition | le tirage [`releves/yapoco-echelle-1.pdf`](releves/yapoco-echelle-1.pdf) (YAPoco, Natalie the Nerd, CC BY-SA 4.0) : page 1 la face avant, page 2 le dos vu de dos. Imprimé à 100 %, sans ajustement à la page : le contour doit mesurer 69,5 × 114,4 mm. Posé sur la carte d'origine, à contre-jour | les écarts sur les trous, les pastilles, l'interrupteur, les molettes, les contacts, la LED ; au-delà de 0,3 mm, les noter |
| **C0.4** Les molettes | quelle molette porte le volume, laquelle le contraste ; le potentiomètre Silent Modding posé sur chaque empreinte d'origine, au dos : pattes, dépassement de la molette par sa fenêtre | si la même pièce sert aux deux molettes |
| **C0.5** L'interrupteur et les contacts | l'interrupteur Silent Modding, sa petite carte comprise, et les contacts de piles, comparés à ceux d'une Pocket : pattes, entraxes, hauteur ; la course du curseur | leurs empreintes |
| **C0.6** 🔬 Les membranes | sur un jeu de membranes ordinaire : scan des deux faces à 600 ppp ; au pied à coulisse, le diamètre de chaque dôme à sa base, celui de la pastille de carbone, la hauteur du creux sous le dôme | la place libre pour une LED dans chaque dôme |
| **C0.7** 🔬 La lumière | avec les pièces transparentes : une LED, n'importe laquelle, placée à côté de la pastille de carbone, puis sous une carte percée ; la membrane et le bouton par-dessus, à la lumière du jour et dans le noir | où et comment poser les LED ; membrane transparente ou blanche |
| **C0.8** Le Pico | l'épaisseur d'un Pico 2 W, prise micro-USB comprise | ce qui doit tenir sous la vitre |

---

## 8. Achats

Pour quatre manettes et une de rechange. **Pour C0, un jeu de chaque pièce suffit** : membranes
et boutons, un interrupteur, deux potentiomètres, un jeu de contacts. Le reste attend C3.

| Pièce | Qté | Où | ~Prix | Note |
|---|---|---|---|---|
| [Membranes RetroSix pour Game Boy Pocket, transparentes](https://www.silentmodding.com/fr/membranes-silicone-retrosix-game-boy-pocket-transparentes.html) | 5 jeux | Silent Modding | 3,99 € le jeu | croix, A et B, Start et Select ; Start et Select sont en silicone, moulés avec leur membrane |
| [Boutons RetroSix Prestige pour Game Boy Pocket, transparents](https://www.silentmodding.com/fr/boutons-retrosix-prestige-game-boy-pocket-transparents.html) | 5 jeux | Silent Modding | 4,90 € le jeu | croix, A, B, curseur de l'interrupteur ; « translucides, aspect trouble » : ils diffusent la lumière |
| [Interrupteur d'alimentation de rechange pour Game Boy Pocket](https://www.silentmodding.com/fr/interrupteur-alimentation-rechange-game-boy-pocket.html) | 5 | Silent Modding | 2,99 € | un seul en stock le 14/11/2026 ; sinon, ceux de Pocket donneuses |
| [Potentiomètre de volume pour GBC, GBP et GBA](https://www.silentmodding.com/fr/potentiometre-volume-game-boy-color-game-boy-pocket-game-boy-advance.html) | 10 | Silent Modding | 1,99 € | deux par manette |
| [Contacts de piles de rechange pour Game Boy Pocket](https://www.silentmodding.com/fr/contacts-piles-rechange-game-boy-pocket.html) | 5 jeux | Silent Modding | 3,99 € le jeu | « 1 × compartiment à piles, 2 × carte mère » |
| Feuille noire mate, 0,3 à 0,5 mm | 1 | papeterie, loisirs créatifs | | le cache, découpé sur le gabarit |
| Pico 2 W | 5 | | | soudés à demeure |
| Cartes, composants CMS posés | 5 | JLCPCB ou PCBWay | le comparatif du §9 | |

Silent Modding demande une commande de 9,90 € au moins ; la livraison est gratuite à partir de
150 €.

---

## 9. Le comparatif des devis

Fait en C5, sur les fichiers définitifs, pour JLCPCB et pour PCBWay :

- **la même commande des deux côtés** : 5 cartes, deux couches, l'épaisseur de C0.2, finition or
  (ENIG), vernis noir ; les CMS posés sur une face, sur 2 cartes puis sur 5, pour voir ce que
  coûte une première série d'essai ;
- **le prix rendu chez toi**, pas le prix affiché : les pièces, la pose, le port, la TVA, et les
  frais de dédouanement. Ta commande précédente chez JLCPCB a coûté 25 € de frais DHL en plus de
  la TVA : le comparatif cherche, chez l'un et l'autre, un envoi dont la TVA est payée à la
  commande, sans frais du transporteur à l'arrivée. Ce que chacun permet vers la France se
  vérifie sur son site au moment du devis ;
- **les pièces « Extended »** : chez JLCPCB, toutes les pièces actives de la carte en sont
  (§4.9) ; leurs frais fixes pèsent lourd sur une petite série, et le comparatif les compte ;
- **les délais** de fabrication et de livraison.

---

## 10. Fiche des relevés de C0 : à remplir pendant la séance, pas après

Date : `__/__/____` ; Pocket de référence : `______`

| Relevé | Valeur |
|---|---|
| Révision de la carte (C0.1) | |
| Épaisseur de la carte (C0.2) | |
| Hauteur libre sous la vitre, aux quatre coins de la fenêtre (C0.2) | |
| Hauteur libre entre le dos de la carte et le compartiment des piles (C0.2) | |
| Écarts entre YAPoco et la carte d'origine (C0.3) | |
| Côté du volume, côté du contraste (C0.4) | |
| Le potentiomètre Silent Modding sur chaque empreinte (C0.4) | |
| Interrupteur Silent Modding : pattes, entraxes, hauteur (C0.5) | |
| Contacts de piles : pattes, entraxes (C0.5) | |
| Dômes : diamètre à la base ; pastille de carbone : diamètre ; creux : hauteur, pour la croix, A et B, Start et Select (C0.6) | |
| Où la lumière passe le mieux ; membrane transparente ou blanche (C0.7) | |
| Épaisseur du Pico 2 W, prise micro-USB comprise (C0.8) | |

---

## 11. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| La géométrie de YAPoco tombe à côté dans une coque d'origine | 🟡 | C0.3, puis le gabarit de C3 ; pas de commande avant |
| La lumière ne traverse pas : pastille de carbone, membrane, bouton | 🟡 | C0.7 essaie avant de placer les LED |
| Une LED soulève la membrane : appuis perdus ou fantômes | 🟡 | LED dans le creux des dômes, ou par le dos ; T5.1, T5.5 |
| Le Pico ne tient pas sous la vitre | 🟡 | C0.2 et C0.8 ; repli : au dos, derrière les boutons |
| L'élévateur brouille la radio | 🟡 | loin de l'antenne, boucle courte, plan de masse ; T5.2 refait LED allumées |
| Les LED vident les piles | 🟡 | élévateur coupé au repos, éclat plafonné ; C8 mesure ; les LED baissent sans appui si besoin |
| Du courant fuit en sommeil vers le Pico éteint | 🟡 | adaptateur isolant pour l'accéléromètre, interrupteur qui envoie la tension des piles ; T1.6 au µA |
| Le maintien ou l'élévateur échoue sur les premières cartes | 🟡 | cavaliers à souder pour les contourner (§4.1) |
| Une mise à jour par le WiFi échoue | 🟡 | deux images du firmware ; points SWD au dos (§4.8) |
| Pas cinq interrupteurs de rechange | 🟢 | ceux des Pocket donneuses |
| Les frais d'importation | 🟢 | le comparatif compte le prix rendu chez toi (§9) |
| L'ADXL362 : 13 $ pièce, peu de stock chez JLCPCB | 🟢 | le LIS2DH12, si C1 confirme son mode « activité » (§4.2) |
| Le bleu terne si la chaîne vit sur le 3,3 V du Pico | 🟢 | l'élévateur, gardé sur les premières cartes, et un cavalier (§4.3) |

---

## 12. Journal

| Date | Décision | Raison |
|---|---|---|
| 14/11/2026 | **Décision de Yoann** : une carte dessinée, dans une vraie coque de Pocket qui paraît normale sans écran ; deux piles AAA ; le Pico 2 W lit les boutons et les molettes et les envoie | |
| 14/11/2026 | Carte dessinée plutôt que carte d'origine modifiée | Le processeur reste sur les lignes des boutons ; les LED ne se câblent pas proprement sur la carte d'origine ; une carte dessinée se refait pour chaque manette |
| 15/11/2026 | **Décision de Yoann** : partir de YAPoco ; la carte passe sous CC BY-SA 4.0 | Contour, vis et pastilles déjà placés pour une coque de Pocket |
| 15/11/2026 | **Décision de Yoann** : neuf LED RGB, quatre sous la croix ; le Pico décide des couleurs, seul et sur ordre reçu par le WiFi | |
| 15/11/2026 | **Décision de Yoann** : cache noir derrière la vitre, le Pico dessous ; aucune ouverture ajoutée ; mises à jour par le WiFi | La console garde son aspect |
| 15/11/2026 | **Décision de Yoann** : quatre manettes ; le dessin d'abord, puis le comparatif JLCPCB et PCBWay, port et frais d'importation compris | 25 € de frais DHL sur une commande précédente |
| 15/11/2026 | Pico debout sous la vitre, antenne au bord haut | Loin des piles ; ni trou vide ni molette contre l'antenne |
| 15/11/2026 | L'interrupteur envoie la tension des piles ; adaptateur isolant pour l'accéléromètre | Rien ne fuit vers le Pico endormi |
| 15/11/2026 | Pièces relevées chez LCSC et JLCPCB (§4.9) : LED « -V6 » de Worldsemi, TPS61099, TXU0304, DMG2305UX, PMV16XNR | Les LED « -V6 » consomment au plus 1 µA éteintes et acceptent 3,3 V ; aucune LED adressable ronde de 3 mm ; toutes les pièces actives sont « Extended » chez JLCPCB |
| 15/11/2026 | Le LIS2DH12 remplace l'ADXL362 ; une interruption verrouillée réveille la carte, le Pico compte la minute ; JP3 permet d'essayer la fonction « activité » sur INT2 | 13 $ contre moins d'un dollar ; la fiche ne dit pas si INT2 tient un niveau |
| 15/11/2026 | Les molettes alimentées par GP21 le temps d'une mesure, avec 1 nF sur chaque curseur | 1,3 mA économisés en jeu ; `ADC_VREF` libre pour mesurer les piles |
| 15/11/2026 | Les pastilles SWD du Pico, traversantes, se soudent et se sondent par le dos | Reflasher sans sortir la carte de la coque avant |
| 15/11/2026 | C1 et C2 faites : schéma sans erreur à l'ERC, géométrie de YAPoco reprise à la pastille près, gabarit de C3 | |

---

## 13. Les fichiers

La carte se régénère par script, comme celle de l'écran : [`../materiel/carte-manette/`](../materiel/carte-manette/README.md).

| Fichier | Rôle |
|---|---|
| `conception.py` | la description électrique, source unique : composants, empreintes, équipotentielles, références d'achat |
| `gen_schema.py`, `gen_pcb.py` | le schéma et le circuit, tirés de la description |
| `construire.sh` | tout, du schéma au gabarit, en une commande |
| `yapoco/` | ce que la carte reprend de YAPoco, et sa provenance |
| `gabarit/gabarit-echelle-1.pdf` | le gabarit de C3 : la face avant, puis le dos vu de dos |
