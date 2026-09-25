# Signaux du bus LCD — relevés sur la carte MGB

*Fiche de la phase 0. À remplir pendant la session, pas après.*

**Statut : 🔨 en cours — instrument validé le 23/09/2026**
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

| Mesure | Valeur | Conditions |
|---|---|---|
| `VCC` piles neuves | `_____ V` | à la mise sous tension, console allumée |
| `VCC` piles usées | `_____ V` | après `___` min de jeu, ou piles usagées |
| Où mesuré | `____________` | point de mesure sur la carte |

**Décision d'interface** (`etapes-detaillees.md` §C.2) :

- [ ] `VCC_usé ≥ 3,0 V` → tampon **recommandé** (protège la console de l'aval)
- [ ] `VCC_usé < 3,0 V` → tampon **obligatoire** (marge contre V_IH du RP2350)
- [ ] `VCC ≈ 5 V` → tampon **obligatoire**, alimenté en 3,3 V

**Ce qu'on commande :** `________________________________`

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

| | Valeur |
|---|---|
| Voies qui basculent | `______` et `______` |
| Écart mesuré | `____ %` et `____ %` |
| Niveau des données sur l'image **blanche** | `____ %` |
| **Polarité : `00` est-il blanc ?** | ⬜ oui (attendu) ⬜ non |

> La polarité ne change **aucune ligne de firmware** : elle décide de l'ordre
> des 4 entrées de palette. Elle est notée ici pour ne pas la redécouvrir en
> phase 3 devant une image en négatif.

---

## 6. 🔬 Les mesures fines

| Mesure | Valeur | Verdict |
|---|---|---|
| **Période minimale de `CPG`** | `_____ ns` | ⬜ ≥ 200 ns ✅ ⬜ 100–200 🔶 ⬜ < 100 ❌ arrêter |
| Impulsions par salve de `CPG` | `_____` | attendu 160 |
| Salves par trame | `_____` | attendu 144 |
| Plus grand silence de `CPL` | `_____ ms` | attendu ≈ 1,09 (la VBlank) |
| Plus grand silence de `CP` | `_____ ms` | attendu ≈ 0,109 (une ligne) |
| Largeur minimale des impulsions `CPL` | `_____ ns` | fixe le taux minimal de la capture lente |
| **Front d'échantillonnage de `LD`** | ⬜ montant ⬜ descendant | `LD` change sur l'autre front |
| Délai `D` estimé | `___` cycles PIO | à affiner par l'image en phase 2 |

---

## 7. Conclusion de la phase 0

- [ ] Les 5 signaux sont attribués, chacun par **fréquence ET test blanc/noir**
- [ ] `VCC` relevé dans les deux états → interface tranchée
- [ ] Période minimale de `CPG` ≥ 200 ns
- [ ] Front d'échantillonnage choisi
- [ ] Polarité relevée
- [ ] Les 4 `.sr` sont versionnés dans `releves/`
- [ ] Cette fiche est remplie

**Observation libre — l'écran d'origine pendant le sondage** (§B.2, règle 4) :

> `____________________________________________________________________`
>
> S'il s'est dégradé pendant qu'on sondait `CPG`, c'est que le PPU est près de
> sa limite : le tampon de la phase 1 devient non négociable, et il faut
> raccourcir encore les fils.

**Verdict : ⬜ GO phase 1  ⬜ NO-GO, raison : `___________________________`**
