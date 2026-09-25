# Liste d'achats — phase 1

*Établie le 25/09/2026, après la phase 0. Chaque pièce est justifiée par une mesure,
pas par un plan théorique.*

---

## ⚠️ Ce que j'ai vérifié, et ce que je n'ai pas pu vérifier

| | |
|---|---|
| ✅ **Vérifié** | les **datasheets** des composants critiques, téléchargées et lues (voir §Justifications) |
| ✅ **Vérifié** | que les liens de recherche AliExpress renvoient bien vers des annonces existantes |
| ❌ **PAS vérifié** | le **contenu des annonces** : prix, stock, vendeur, variante réellement proposée |

**Pourquoi** : les pages produit d'AliExpress sont rendues en JavaScript. Une récupération
automatique ne renvoie que le pied de page du site. Je ne peux donc pas te dire « cette
annonce-là vend bien du DIP-20 à 3,40 € ».

**Conséquence sur la forme de ce document** : la colonne principale est un **lien de
recherche**, qui est stable et montre toujours les annonces du moment. Les liens d'articles
précis, trouvés via un moteur de recherche, sont donnés en second et marqués *non vérifiés* —
à considérer comme des points de départ, pas comme des recommandations.

**Les prix indiqués sont des ordres de grandeur**, pas des relevés.

---

## 1. Le tampon — la seule pièce où l'authenticité compte

### ⚠️ Le piège : les contrefaçons

Beaucoup de circuits logiques vendus sur AliExpress sont des **`74HC244` remarqués `LVC`**.
Ce serait le pire cas ici :

| Composant | V_IH à VCC = 3,3 V | Face aux 2,2 V de la console |
|---|---|---|
| **74LVC244A** (voulu) | **2,0 V** | fonctionne, 200 mV de marge |
| 74HC244 (contrefaçon) | 0,7 × 3,3 = **2,31 V** | **ne commute jamais** |

Le symptôme serait pervers : *« ça marche sur alimentation stable, ça ne marche plus sur
piles »*. Des heures perdues à soupçonner le câblage.

### 🔬 Test de recette, avec le matériel que tu as déjà

Alimente la puce en 3,3 V, applique une tension réglable sur une entrée (alim de labo),
monte doucement, note à quelle tension la sortie bascule (multimètre) :

| Bascule vers | Verdict |
|---|---|
| **~2,0 V** | vrai LVC ✅ |
| **~2,3 V** | HC remarqué ❌ — jette |

> 💡 **Alternative recommandée pour cette pièce précise** : prends-la chez un vrai
> distributeur (TME, Reichelt, Mouser) pour 1–2 € + port. C'est le seul composant de la liste
> où ça vaut le détour, et ça t'évite le test ci-dessus.

### Les liens

| | |
|---|---|
| 🔍 **Recherche** | https://fr.aliexpress.com/w/wholesale-sn74lvc244an.html |
| 🔍 Recherche large | https://fr.aliexpress.com/w/wholesale-74lvc244.html |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/1005006959118600.html |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/1005005917361678.html |

Les deux annonces sont intitulées *« 5PCS/LOT SN74LVC244AN SN74LVC245AN SN74LVC373AN »* —
donc à variante sélectionnable. **Vérifie que tu choisis bien `SN74LVC244AN` et le boîtier
DIP-20.**

**À vérifier sur l'annonce :** boîtier **DIP-20** (`N`), pas TSSOP (`PW`) ni SOIC (`DW`) ·
marquage `LVC`, jamais `HC` ni `HCT` · **Quantité : 5** (tu en tueras un).
**~2–4 €**

### Repli si le DIP est introuvable

Le `SN74LVC245AN` (transceiver) fait le même travail : relie `DIR` à VCC et les 8 voies vont
dans le sens A→B. Même brochage d'alimentation, même V_IH.

Sinon, CMS + carte d'adaptation :

| | |
|---|---|
| 🔍 Recherche | https://fr.aliexpress.com/w/wholesale-sop20-to-dip20-adapter-board.html |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/32821305070.html — *20 pcs* |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/32821253912.html — *10 pcs* |

⚠️ Prends la **carte d'adaptation** (plaquette PCB à souder), pas le « programmer adapter
socket » à pince ZIF, qui est fait pour programmer et non pour rester dans un montage.
**~2–3 €**

---

## 2. Support DIP-20

Pour insérer la puce **après** avoir testé la carte à vide, et la remplacer sans dessouder.
C'est l'étape 3 du §C.4 : continuité sur les 20 broches, puis insertion.

| | |
|---|---|
| 🔍 Recherche | https://fr.aliexpress.com/w/wholesale-dip20-ic-socket.html |

**À vérifier :** 20 broches · type « tulipe » (contacts tournés) de préférence aux
contacts estampés. **Quantité : 10.** **~1–2 €**

---

## 3. Résistances 100 Ω × 6

Deux rôles : limiter le courant à **33 mA** si une broche du Pico se retrouve en sortie face
au PPU, et amortir les réflexions sur le fil volant (τ = 100 Ω × 25 pF = **2,5 ns**,
négligeable devant les 238 ns d'une période pixel mesurée).

| | |
|---|---|
| 🔍 **Kit assorti** *(recommandé)* | https://fr.aliexpress.com/w/wholesale-metal-film-resistor-kit-1-4w.html |
| 🔍 Juste des 100 Ω | https://fr.aliexpress.com/w/wholesale-100-ohm-resistor-1-4w.html |

**Prends le kit** : 600–1000 pièces, 30 valeurs, à peine plus cher que 20 résistances d'une
seule valeur, et tu n'en recommanderas jamais. **Vérifie qu'il contient bien 100 Ω.**
**~3–5 €**

---

## 4. Condensateur 100 nF

**Le composant le plus souvent oublié, et son absence est la plus difficile à diagnostiquer.**
Le 244 a 8 sorties qui commutent, certaines à 4 MHz ; sans réservoir local, la pointe de
courant traverse l'inductance du fil d'alimentation et devient une chute de tension visible
sur **toutes** les voies à la fois.

Se soude **à quelques millimètres** des broches 20 (VCC) et 10 (GND).

| | |
|---|---|
| 🔍 **Kit assorti** *(recommandé)* | https://fr.aliexpress.com/w/wholesale-ceramic-capacitor-assortment-kit.html |
| 🔍 Juste des 100 nF | https://fr.aliexpress.com/w/wholesale-100nf-ceramic-capacitor-50v.html |

**À vérifier :** marquage **`104`** (= 10 × 10⁴ pF = 100 nF) · 50 V minimum · **traversant**,
pas CMS. **~3–5 €**

---

## 5. Fil fin pour les 6 prises

Pas de `P2` mesuré : **~1 mm**.

> 🔑 **Je recommande le Kynar multicolore, contre ce qu'écrivaient mes propres documents.**
> Six fils identiques couleur cuivre qui partent d'une zone d'un centimètre vers un
> connecteur, c'est une erreur de câblage qui attend son heure. Le Kynar 30 AWG fait 0,25 mm
> d'âme et ~0,5 mm avec isolant : ça passe largement au pas de 1 mm, et chaque fil a sa
> couleur.

| | |
|---|---|
| 🔍 **Kynar 30 AWG** *(recommandé)* | https://fr.aliexpress.com/w/wholesale-kynar-wire-30awg.html |
| 🔍 Émaillé, plus fin | https://fr.aliexpress.com/w/wholesale-self-fluxing-enameled-wire-0.2mm.html |

**À vérifier (Kynar) :** 30 AWG · **plusieurs couleurs** (6 minimum) · âme monobrin.
**À vérifier (émaillé) :** mention **self-fluxing / UEW** — sinon le vernis ne fond pas au
fer et il faut le gratter, ce qui est infaisable proprement sur 0,2 mm.
**~4–7 €**

---

## 6. Connecteur débrochable — JST-SH 1,0 mm, **8 points**

**8 et pas 6 :** 6 signaux (`LD0`, `LD1`, `CP`, `P2-ST`, `P2-S`, `CPL` en réserve) **+ 2
masses**, une torsadée avec chaque groupe de 3.

> La diaphonie mesurée en phase 0 justifie les deux masses : les deux voies **non connectées**
> voisines de l'horloge pixel dans le faisceau ramassaient 108 et 149 fronts sur 66,7 ms,
> alors que les lointaines étaient à zéro.

**Il se place APRÈS le tampon**, là où les signaux sont des sorties 3,3 V à ±24 mA qui
supportent 30 cm de câble. Avant le tampon ils sont fragiles.

| | |
|---|---|
| 🔍 **Kit avec câbles pré-sertis** | https://fr.aliexpress.com/w/wholesale-jst-sh-1.0mm-connector-kit.html |
| 🔍 Câble 8 points seul | https://fr.aliexpress.com/w/wholesale-jst-sh-8-pin-cable.html |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/1005008151760358.html — *« kit JST SH avec câbles pré-sertis »* |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/1005006669005131.html — *« connecteurs + câbles silicone pré-sertis »* |
| 📦 Article *(non vérifié)* | https://fr.aliexpress.com/item/1005005785494996.html — *« kit 390 pcs »* |

⚠️ **Prends du PRÉ-SERTI.** Sertir du JST-SH 1,0 mm à la main sans la pince dédiée (~40 €)
est un exercice de frustration pure.

**À vérifier :** pas **1,0 mm** (série SH), pas 1,25 (GH) ni 1,5 (ZH) · **8 points** ·
câbles **pré-sertis** · **embase incluse** (sinon la commander à part).
**~3–5 €**

> 💡 Si la place le permet dans la coque, le **JST-ZH 1,5 mm** est nettement plus facile à
> manipuler. À arbitrer une fois la carte tampon dessinée.

---

## 7. Perfboard

Le support du 244, des 6 résistances et du condensateur. Il vit **dans** la console — d'où la
contrainte : il faut pouvoir en découper un morceau d'environ 20 × 15 mm.

| | |
|---|---|
| 🔍 Recherche | https://fr.aliexpress.com/w/wholesale-double-sided-prototype-pcb-board-kit.html |

**À vérifier :** assortiment de petites plaques · pas de 2,54 mm · double face de préférence.
**~3–5 €**

---

## 8. Consommables de soudure — indispensables à ce pas

Si tu ne les as pas déjà, ce n'est pas optionnel : c'est la différence entre « faisable » et
« impossible » sur du 1 mm.

| Pièce | Recherche | Pourquoi |
|---|---|---|
| **Flux en seringue** | https://fr.aliexpress.com/w/wholesale-solder-flux-paste-rosin.html | sans flux, une soudure fine ne mouille pas |
| **Étain 0,3–0,5 mm** | https://fr.aliexpress.com/w/wholesale-solder-wire-0.3mm.html | du 1 mm dépose trop de matière d'un coup |
| **Tresse à dessouder** | https://fr.aliexpress.com/w/wholesale-desoldering-wick-1.5mm.html | pour reprendre un pont entre deux broches |

**~5–8 € les trois**

---

## 9. Ce que tu as déjà

| | |
|---|---|
| Raspberry Pi Pico 2 W | projet écran |
| Analyseur logique | validé en phase 0 |
| Multimètre, alimentation de labo | phase 0 |
| **Colle chaude** | ⚠️ **obligatoire** — une goutte sur les 6 fils dès leur sortie de la carte. Un fil qui bouge arrache sa broche, et elle ne revient pas |

---

## Récapitulatif

| # | Pièce | ~Prix |
|---|---|---|
| 1 | 74LVC244A DIP-20 × 5 | 2–4 € |
| 2 | Support DIP-20 × 10 | 1–2 € |
| 3 | Kit de résistances | 3–5 € |
| 4 | Kit de condensateurs | 3–5 € |
| 5 | Fil Kynar 30 AWG multicolore | 4–7 € |
| 6 | JST-SH 1,0 mm 8 pts pré-serti + embases | 3–5 € |
| 7 | Perfboard | 3–5 € |
| 8 | Flux + étain fin + tresse | 5–8 € |
| | **Total** | **~25–40 €** |

**Commande tout en une fois** — 2 à 4 semaines de livraison depuis la Chine, et deux
commandes séparées font deux attentes.

> 🔑 **Et c'est pendant cette attente que se fait la phase 3a** : `IDX2` dans le firmware du
> module écran (4 modifications repérées dans le code) et `pixelpush --format idx2` (une
> trentaine de lignes). Aucune console nécessaire. Ça retire une inconnue de la phase 3 et
> donne un banc de test qui servira encore quand la Game Boy sera refermée.

---

## Justifications — les mesures et datasheets derrière chaque choix

### Seuils d'entrée, vérifiés sur les datasheets le 25/09/2026

| Composant | Condition | V_IH | Source |
|---|---|---|---|
| **RP2350**, IO standard | IOVDD = 1,8 V | 0,65 × IOVDD | [datasheet §14.9](https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf) |
| | IOVDD = 2,5 V | 1,7 V | idem |
| | **IOVDD = 3,3 V** | **2,0 V** | idem |
| **SN74LVC244A** | VCC = 2,3–2,7 V | 1,7 V | [SCAS414AG §5.3](https://www.ti.com/lit/ds/symlink/sn74lvc244a.pdf) |
| | **VCC = 2,7–3,6 V** | **2,0 V** | idem |
| | `V_I` (tolérance d'entrée) | **0 à 5,5 V** | idem |
| | `I_OH`/`I_OL` à VCC = 3 V | ±24 mA | idem |

> ⚠️ **Correction.** Les versions antérieures de `signaux-mgb.md` et `etapes-detaillees.md`
> appliquaient `V_IH = 0,65 × IOVDD` au RP2350 à 3,3 V, soit 2,145 V, et en concluaient à
> 55 mV de marge. **C'est faux** : cette formule ne vaut que pour IOVDD = 1,8 V. À 3,3 V le
> seuil est **2,0 V fixe**, et la marge réelle est de **200 mV**.

### Mesures de la phase 0 qui dimensionnent ces achats

| Mesure | Valeur | Ce qu'elle décide |
|---|---|---|
| `VCC` carte à 3,2 V d'entrée | **3,1 V** | marge de 1,1 V — confortable sur alimentation stable |
| `VCC` carte à 2,4 V d'entrée | **2,2 V** | marge de 200 mV — le `VCC` logique n'est **pas** régulé |
| Période min. de l'horloge pixel | **208 ns** | fixe la constante de temps acceptable des 100 Ω |
| Écran d'origine sous 3 sondes | **aucune dégradation** | le PPU encaisse 40–60 pF ; la prise définitive charge 2–3× moins |
| Diaphonie entre fils voisins | 108–149 fronts sur 66,7 ms | justifie les **2 masses** du connecteur 8 points |
| Pas du connecteur `P2` | ~1 mm | fixe le calibre du fil |

### Pourquoi le tampon, exactement

Ce n'est **pas** un argument de niveau : RP2350 et 74LVC244A ont le **même** V_IH de 2,0 V à
3,3 V. C'est :

1. **L'isolation du PPU** — ~5 pF d'entrée, et la console ne voit rien de ce qui se passe en
   aval : Pico débranché, broche mal configurée, câble capacitif. C'est le rôle qui compte
   pour une installation qui tourne sans surveillance.
2. **La tolérance 5 V** — portage éventuel sur DMG sans rien changer.
3. **La régénération du front** — 3,3 V plein à ±24 mA en sortie, insensible au câble en aval.
