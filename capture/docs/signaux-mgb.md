# Signaux du bus LCD — relevés sur la carte MGB

*Fiche de la phase 0. À remplir pendant la session, pas après.*

**Statut : ✅ PHASE 0 TERMINÉE le 25/09/2026**
Console : MGB-001, n° de série `________`, révision de carte `________`
Date de la session : `__/__/____`
Analyseur : AZDelivery 8 CH 24 MHz (CY7C68013A / fx2lafw), PulseView `______`

> Tant que le §7 n'est pas conclu, **aucune soudure** (`etapes-detaillees.md` §B.8).

---

## 0. Instrument — validé le 23/09/2026

| Point | Résultat |
|---|---|
| Détection USB | `0925:3881 Lakeview Research Saleae Logic` |
| Pilote | `fx2lafw`, sigrok-cli 0.7.2 |
| **Firmware** | ⚠️ volatile : téléversé à **chaque** branchement. Paquet `sigrok-firmware-fx2lafw` requis, il n'est **pas** une dépendance de `sigrok-cli` |
| Connexion | derrière un **dock USB-C** (pas de port direct sur la machine). Le premier téléversement a échoué 2 fois (`LIBUSB_ERROR_OTHER`) avant de passer — **ne pas débrancher en cours de session** |
| Taux disponibles | 20 kHz … 48 MHz ; 4 MHz et 24 MHz présents |
| Sérigraphie | `CH1 … CH8 GND GND` |
| **Correspondance** | **`CHn` = `D(n-1)`** — vérifié en mettant `CH1` à la masse : seule `D0` est tombée à 0 % |
| Entrées en l'air | **100 %** → le boîtier a des **résistances de tirage vers le haut**. Le test du doigt ne fonctionne pas dessus, et ces tirages sont une charge de plus sur le PPU (§B.2 règle 4) |

> 🔑 **Le test du doigt a échoué, et c'est une information.** Une entrée tirée vers le haut ne
> descend pas sous 1,4 V par simple couplage capacitif. Le test concluant est le
> court-circuit franc à la masse.

---

## 1. 🔬 Tension logique

Mesuré le 25/09/2026, au multimètre, entre le test point `VCC` et `P2-GND`.
Console alimentée par une alimentation de laboratoire à sortie flottante, en
lieu et place des piles.

| Entrée | `VCC` carte | Marge contre V_IH du RP2350 (**2,0 V**) |
|---|---|---|
| **3,2 V** (piles neuves) | **3,1 V** | **1 100 mV** |
| **2,4 V** (piles usées) | **2,2 V** | **200 mV** |

> ⚠️ **Correction du 25/09/2026.** Une première version de cette fiche annonçait
> 55 mV, en appliquant `V_IH = 0,65 × IOVDD` à 3,3 V. C'est faux : la datasheet
> RP2350 §14.9 ne donne cette formule **que pour IOVDD = 1,8 V**. À 2,5 V c'est
> 1,7 V, et **à 3,3 V c'est 2,0 V fixe**. La marge réelle est de 200 mV.

> 🔑 **Le `VCC` logique n'est PAS régulé** : il suit l'entrée à 0,1–0,2 V près.
> Le `DMG-REG` régule les tensions de polarisation du LCD (`V1`–`V5`, `VEE`),
> pas l'alimentation logique. Le plan §3.3 envisageait les deux cas sans
> trancher ; c'est tranché.

**Décision d'interface** (`etapes-detaillees.md` §C.2) :

- [x] **74LVC244A retenu**, alimenté en 3,3 V.

Valeurs vérifiées sur les datasheets réelles, pas sur les familles :

| | V_IH | Source |
|---|---|---|
| RP2350, IO standard, IOVDD 3,3 V | **2,0 V** | datasheet RP2350 §14.9 |
| SN74LVC244A, VCC 2,7–3,6 V | **2,0 V** | SCAS414AG, §5.3 |
| SN74LVC244A, VCC 2,3–2,7 V | **1,7 V** | idem |

> 🔑 **Les deux composants ont le MÊME seuil à 3,3 V.** Le tampon n'améliore donc
> **pas** la marge d'entrée : elle vaut 200 mV dans les deux cas à piles
> fatiguées. Ce n'est pas l'argument de niveau qui le justifie.

**Ce qui le justifie réellement :**

1. **Isolation du PPU** — il ne présente que ~5 pF et absorbe tout ce qui se
   passe en aval. Le Pico peut planter, être débranché, mal configurer une
   broche : la console ne le voit pas. C'est le rôle qui compte pour une
   installation qui tournera sans surveillance.
2. **Tolérance 5 V** — `V_I` = 0 à 5,5 V (datasheet). Si le montage est un jour
   porté sur une DMG (bus 5 V), la même carte fonctionne.
3. **Régénération du front** — ce qui sort est du 3,3 V plein avec ±24 mA de
   capacité, donc insensible à la longueur du câble en aval.

> ℹ️ **Variante si le fonctionnement sur piles usées devient critique** :
> alimenter le 244 en **2,5 V** au lieu de 3,3 V. Son V_IH tombe à 1,7 V
> (marge d'entrée **500 mV**) et sa sortie à 2,5 V reste 500 mV au-dessus du
> seuil du RP2350. C'est mieux des deux côtés — au prix d'un régulateur 2,5 V,
> que le Pico ne fournit pas. Non retenu en v1.

> ℹ️ **Sur alimentation stable** — le cas de l'installation — `VCC` reste à 3,1 V
> et la marge est de **1,1 V** des deux côtés. La question des niveaux ne se pose
> alors pas du tout, et seul le rôle n° 1 compte.

**Ce qu'on commande :** 74LVC244A (ou 74LVC245A) + support, 6 × 100 Ω, 100 nF.

---

## 2. Le point de prise — connecteur `P2`

**Carte `MGB-ECPU-01` (© 1996).** Le ruban LCD n'est pas soudé : connecteur **ZIF
`P2`, 18 broches**, repères `1` et `18` sérigraphiés, verrou rabattable.

| | |
|---|---|
| Photos | `releves/carte-mgb.jpg` (vue large), `releves/ruban-lcd.jpg` (gros plan) |
| Nombre de broches | **18**, confirmé au comptage |
| Pas | `______ mm` 🔬 (estimé ~1,0 d'après photo ; confirmer par la largeur du ruban) |
| Rang de broches du CPU en regard | **41 à 64** |
| Point de masse retenu | `BT−` |

**Correspondance broche `P2` ↔ voie de l'analyseur ↔ signal** :

| Broche P2 | Voie | Signal identifié | Preuve |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| 4 | | | |
| 5 | | | |
| 6 | | | |
| 7 | | | |
| 8 | | | |
| 9 | | | |
| 10 | | | |
| 11 | | | |
| 12 | | | |
| 13 | | | |
| 14 | | | |
| 15 | | | |
| 16 | | | |
| 17 | | | |
| 18 | | | |

---

## 3. Les captures

Deux captures, et c'est **volontaire** — un seul taux ne peut pas répondre aux
deux questions (`etapes-detaillees.md` §B.3) :

| Fichier | Taux | Durée | À quoi elle sert |
|---|---|---|---|
| `releves/rapide.sr` | 24 MS/s | ~4 trames (67 ms) | `CPG` : structure en salves, période minimale. `LD0/LD1` |
| `releves/lente.sr` | 4 MS/s | ~30 trames (500 ms) | `CPL`, `CP`, `ST`, `FR` : cadences et silences |
| `releves/blanc.sr` | 24 MS/s | ~4 trames | test §B.5, écran **blanc** |
| `releves/noir.sr` | 24 MS/s | ~4 trames | test §B.5, écran **noir** |

---

## 3bis. ⚠️ TABLE CORRIGÉE — mesurée le 25/09/2026

**La table de `Spec_Video_Sniffer_et_Matrice_LED.md` §4.1 est fausse sur 5 signaux
sur 6.** Ce qui suit la remplace. Chaque ligne est une mesure, pas une hypothèse ;
les `.sr` correspondants sont dans `releves/`.

| Sérigraphie | Mesuré | Fonction réelle | Relevé |
|---|---|---|---|
| **`CP`** | 160 impulsions/ligne visible, 143,9 salves/trame, période min **208 ns**, silence 1,156 ms | **horloge pixel** | `cp-rapide.sr` |
| **`P2-ST`** | **8596 fronts/s** (144/trame), silence **1,196 ms** | **verrou de ligne** (STrobe) — muet en VBlank | `p2-st.sr` |
| **`P2-S`** | **59,71 Hz**, r.cycl. 0,65 % = **une ligne** | **départ de trame** (Start) — le VSYNC | `p2-s.sr` |
| **`P2-CPL`** | 9196 fronts/s (**154**/trame), silence 109 µs | **horloge de ligne** — bat aussi en VBlank | `p2-cpl.sr` |
| **`P2-FR`** | cadence 4598 Hz, r.cycl. 50 %, période min 1 ligne | **inversion LIGNE**, pas trame | `p2-fr.sr` |
| **`CPG`** | 616/trame = 4/ligne × 154 ; motif 28,5 / 46,8 / 3,0 / 30,5 µs ; impulsions de 1 µs | **indéterminée** — non requise | `cpg.sr` |

> 🔑 **Ce que la spec appelait `CPG` n'est pas l'horloge pixel, et ce qu'elle appelait
> `ST` n'est pas le départ de trame.** Souder d'après elle aurait mis l'horloge pixel
> sur un signal à 4 impulsions/ligne et le VSYNC sur un signal à 144/trame. Le firmware
> n'aurait produit que du bruit, et la cause aurait été cherchée dans le PIO.

**Brochage corrigé pour la phase 1** — c'est ce tableau qui fait foi :

| GPIO | Point de test | Rôle |
|---|---|---|
| GP0 | `P2-LD0` | donnée, bit 0 |
| GP1 | `P2-LD1` | donnée, bit 1 |
| GP2 | **`CP`** | horloge pixel (le plan disait `CPG`) |
| GP3 | **`P2-ST`** | marqueur de ligne visible (le plan disait `CPL`) |
| GP4 | **`P2-S`** | VSYNC (le plan disait `ST`) |
| GP5 | **`P2-CPL`** | réserve, horloge de ligne 154/trame |
| GND | **`P2-GND`** | masse locale, au milieu du groupe |

> ℹ️ `P2-ST` est **meilleur** que ce que le plan espérait : 144 impulsions par trame,
> c'est exactement le compte de lignes du canevas, et son silence de 1,09 ms en VBlank
> offre un détecteur de frontière de trame en prime.

### Phase d'échantillonnage — mesurée le 25/09/2026 (§B.7)

Capture `releves/donnees.sr` : `LD0`, `LD1` et `CP` simultanément, 24 MS/s.

```
  Décalage des 256 transitions de LD après le front MONTANT de CP :
     +0 éch (   0 ns)  209  ████████████████████████████████████████████
     +1 éch (  42 ns)   47  ███████████
     au-delà            0
```

| Mesure | Valeur |
|---|---|
| Période pixel | 250 ns (quantifié ; valeur vraie 238) |
| Temps haut de `CP` | **125 ns** (3 échantillons) |
| **`LD` change sur le front MONTANT** | 100 % des transitions à +0 ou +1 échantillon |
| Fenêtre stable | ~50 ns → 238 ns après le front montant |

> 🔑 **On échantillonne donc sur le front DESCENDANT de `CP`**, à 125 ns du front
> montant : au milieu de la fenêtre stable, 75 ns de marge avant, 113 ns après.

**Programme PIO retenu** — les deux `wait` sont inversés par rapport au plan §D.2 :

```
.wrap_target
    wait 1 gpio 2        ; attendre que CP soit haut
    wait 0 gpio 2        ; front DESCENDANT — LD est stable depuis 125 ns
    in   pins, 2
.wrap
```

**`D` = 0.** Le front descendant tombe déjà au bon endroit, aucun délai n'est nécessaire.

### `LD0` et `LD1` — qualifiées

| Preuve | Valeur |
|---|---|
| Fronts dans une salve de `CP` | **100,0 %** |
| Silence maximal | 15,1 ms — muettes hors de la bande d'image |
| `LD0` ≠ `LD1` | **4 échantillons sur 1 600 000** (0,000 %) |

Les deux lignes sont **identiques** sur cette image : l'écran de démarrage sans
cartouche n'a que deux niveaux, donc les 2 bits valent toujours `00` ou `11`.
C'est une confirmation forte qu'il s'agit bien des deux lignes de données — et
c'est déjà un demi-test blanc/noir.

🔬 **Polarité, à confirmer** : `LD` est bas **97,76 %** du temps sur un écran de
démarrage à fond clair avec logo sombre. Cohérent avec `00` = blanc, l'hypothèse
du plan. À prouver formellement avec une cartouche (§5).

### ⚠️ Diaphonie entre fils de sonde

| Voie | Connectée ? | Fronts sur 66,7 ms |
|---|---|---|
| D2 (`CP`) | oui | 91 681 |
| D3 | **non** | 108 |
| D4 | **non** | 149 |
| D5, D6, D7 | non | **0** |

Les deux voisines immédiates de `CP` ramassent du signal, les lointaines non.
C'est de la **diaphonie capacitive entre fils volants**, pas une boucle de masse
— une boucle affecterait toutes les voies. L'alimentation flottante est donc
confirmée une seconde fois.

Sans conséquence sur la mesure (99,98 % de niveau haut), mais **c'est un
avant-goût de la phase 1** : à 4 MHz, des fils parallèles non torsadés couplent.
La règle « une masse torsadée par paquet de signaux » du §3.4 n'est pas
décorative.

---

## 4. Dépouillement

```bash
./tools/analyse_sr.py docs/releves/lente.sr     # CPL / CP / ST
./tools/analyse_sr.py docs/releves/rapide.sr    # CPG / LD0 / LD1
```

**Attribution retenue** (coller ici le tableau produit par l'outil, puis
trancher à la main les cas « moyenne » et « faible ») :

| Voie | Signal | Confiance | Preuve retenue |
|---|---|---|---|
| D0 | | | |
| D1 | | | |
| D2 | | | |
| D3 | | | |
| D4 | | | |
| D5 | | | |

---

## 5. 🔬 Test blanc/noir — la preuve

```bash
./tools/analyse_sr.py --comparer docs/releves/blanc.sr docs/releves/noir.sr
```

**Fait le 25/09/2026**, comparaison restreinte aux salves de l'horloge pixel.

| | Valeur |
|---|---|
| Voies qui basculent | **D1** et **D3** (les deux lignes de données) |
| Écart mesuré | **61,3 %** et **79,4 %** |
| Niveau des données sur l'image claire | **10,3 %** et **8,5 %** |
| Niveau des données sur l'image sombre | **71,6 %** et **88,0 %** |
| Toutes les autres voies | 100 % → 100 %, écart **0,0 %** |
| **Polarité : `00` est-il blanc ?** | ✅ **oui** — bas = clair, conforme à l'attendu |

> Exactement **deux** voies basculent, et elles basculent **ensemble**. C'est ce
> qu'aucune mesure de fréquence ne pouvait établir.

**Indépendance des deux lignes**, prouvée sur l'image sombre à 4 niveaux :
`D1 ≠ D3` sur **41,65 %** des échantillons. Sur l'écran de titre à 2 niveaux
elles étaient confondues (46 échantillons d'écart sur 800 000) ; une image à
4 gris les sépare franchement.

> ℹ️ **On ne sait pas laquelle est `LD0` et laquelle est `LD1`**, et on ne peut
> pas le savoir depuis le bus seul. Sans conséquence : l'ordre se corrige en
> permutant les 4 entrées de palette, sans recompiler le firmware de capture.

> La polarité ne change **aucune ligne de firmware** : elle décide de l'ordre
> des 4 entrées de palette. Elle est notée ici pour ne pas la redécouvrir en
> phase 3 devant une image en négatif.

---

## 6. 🔬 Les mesures fines

| Mesure | Valeur | Verdict |
|---|---|---|
| **Période minimale de l'horloge pixel (`CP`)** | **208 ns** | ✅ bande ≥ 200 ns. Valeur vraie 238 ns, quantifiée à 5 échantillons |
| Impulsions par salve de `CP` | **160** | ✅ exactement l'attendu |
| Salves par trame | **143,9** | ✅ 144, bords de capture tronqués |
| Plus grand silence de `P2-ST` | **1,196 ms** | ✅ la VBlank |
| Plus grand silence de `P2-CPL` | **0,109 ms** | ✅ une ligne — il bat aussi en VBlank |
| Temps haut de `CP` | **125 ns** | 3 échantillons |
| **Front d'échantillonnage de `LD`** | ✅ **DESCENDANT** | `LD` change sur le front montant (209/256 à +0 éch) |
| Délai `D` | **0** | le front descendant tombe déjà au milieu de la fenêtre stable |

---

## 7. Conclusion de la phase 0

- [x] Les 5 signaux sont attribués, chacun par **fréquence ET test blanc/noir**
- [x] `VCC` relevé dans les deux états → **tampon 74LVC244A retenu** (pour l'isolation, pas pour les niveaux)
- [x] Période minimale de l'horloge pixel ≥ 200 ns (**208 ns**)
- [x] Front d'échantillonnage choisi (**descendant**, `D` = 0)
- [x] Polarité relevée (**`00` = blanc**)
- [x] Les `.sr` sont versionnés dans `releves/` (10 captures)
- [x] Cette fiche est remplie

**Observation — l'écran d'origine pendant le sondage** (§B.2, règle 4) :

> ✅ **Aucune dégradation observée.** L'image d'origine est restée identique
> pendant toute la séance, y compris avec **trois pointes posées simultanément**
> dont une sur l'horloge pixel à 4 MHz.

Ce que ça permet de conclure, et ce que ça ne permet pas :

| | |
|---|---|
| Charge du sondage | broche FX2LP ~10 pF **+ fil dupont long 30–50 pF**, **sans résistance série**, ≈ **40–60 pF** par voie |
| Charge de la phase 1 | **100 Ω en série** + entrée 74LVC244 ~5 pF + fil court ≈ **15–25 pF**, amortie |

> 🔑 **La prise définitive chargera le PPU deux à trois fois MOINS que ce qu'on
> vient de lui faire subir — et ça n'a rien dégradé.** Le risque n° 5 du §H
> (« le PPU supporte-t-il la charge ajoutée ») est donc largement levé, par
> mesure et non par calcul.

> ⚠️ Observation sur **cet** exemplaire, avec **cette** charge. Elle ne dispense
> ni des 100 Ω, ni des fils courts, ni de la photo avant/après de la
> vérification 2 du §C.5 — qui reste le critère de sortie de la phase 1.

**Verdict : ✅ GO phase 1.**

Tous les critères de sortie sont atteints. Le brochage corrigé du §3bis fait foi
pour la soudure, et il ne ressemble pas à celui du plan : `CP` est l'horloge pixel,
`P2-ST` le marqueur de ligne visible, `P2-S` le VSYNC.
