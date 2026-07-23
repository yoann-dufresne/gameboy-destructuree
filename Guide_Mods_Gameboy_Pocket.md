# Guide complet des mods — Game Boy Pocket (MGB-001)

Petit rappel sur la console : la Game Boy Pocket est sortie en 1996. Écran monochrome réflectif (STN, 4 niveaux de gris, 160×144, ~2,6"), **pas de rétroéclairage d'origine**, alimentée par **2 piles AAA (3 V)**. Elle joue les jeux Game Boy classiques (DMG) en noir et blanc — attention, **elle ne joue pas les jeux Game Boy Color exclusifs**, même après un mod couleur (les palettes ajoutées sont de la « fausse couleur » appliquée à l'image monochrome).

**Légende difficulté :** 🟢 sans soudure / débutant · 🟡 soudure simple · 🔴 soudure fine / avancé

---

## 1. Écran — le mod le plus impactant

### Kit IPS FunnyPlaying « Q5 » 🔴 *(la référence moderne)*
C'est LE mod star. On remplace complètement l'écran d'origine par une dalle IPS rétroéclairée, laminée, avec des noirs profonds et une luminosité réglable. Fonctions du kit : **36 palettes de couleurs** (appui sur capteur tactile), mode **RetroPixel** (effet pixels d'origine, appui long 5 s), réglage de luminosité, consommation 0,3–0,8 W.

Points à connaître : la dalle Q5 est **plus grande que la fenêtre d'origine** (on voit plus d'image), une **lentille adaptée est fournie**. Il faut **retirer les brackets plastique** qui tiennent l'écran d'origine, souvent **découper un peu la coque** et **souder** l'alimentation + le capteur tactile. Beaucoup optent pour une **coque « IPS-ready »** pré-découpée pour éviter le travail au cutter.

### Bivert + rétroéclairage 🔴 *(la méthode « à l'ancienne »)*
Avant les kits IPS, on gardait l'écran d'origine et on ajoutait : un **panneau rétroéclairé** derrière la dalle (après retrait du film réfléchissant), un **nouveau film polarisant**, et surtout une **puce bivert** (hex inverter type 74HC04) qui inverse le signal pour que l'image reste correcte avec le backlight et gagne en contraste. Kits vendus complets (backlight + fils + puce bivert + polariseur). Bon marché mais résultat inférieur à l'IPS et soudure délicate — aujourd'hui on préfère quasi toujours l'IPS.

### Lentille en verre 🟢
Remplacer le plastique d'origine (rayé/terne) par une **lentille en verre trempé** : meilleure clarté, résistance aux rayures, look premium. Adhésive, pose en 2 minutes. Compatible écran d'origine ET IPS (petit liseré autour, un peu plus marqué avec la dalle d'origine). Dispo en plein de teintes.

---

## 2. Alimentation

### Mod batterie USB-C Li-ion 🟡 *(gros confort au quotidien)*
On abandonne les piles AAA au profit d'une **batterie Li-ion rechargeable en USB-C**. Le design de référence est celui de **Giltesa** : circuit à base de **TP4056** (gestion de charge) + **DW01A** (protection décharge). LED de charge (rouge) / pleine (verte), souvent une LED de jeu (blanche). Versions ~**700 mAh, ~6 h** d'autonomie. Certaines cartes s'installent quasi **sans fil** ; il faut **découper la coque** pour le port USB-C. Vendu en kit carte seule ou pré-assemblé avec batterie + cache arrière.

### Piles rechargeables (option 🟢)
Solution zéro-mod : des **AAA NiMH** rechargeables. Simple, mais 2×1,2 V = 2,4 V (contre 3 V) : l'indicateur de batterie faible peut s'allumer plus tôt. Le mod USB-C reste la solution propre.

---

## 3. Audio

L'ampli et le haut-parleur d'origine sont faibles et saturent dans les aigus. Plusieurs mods 🟡 :

- **Ampli « wire-free » (Hand Held Legend)** : dédié GBP, faible conso, **2–3× le volume**, compatible IPS, coupure auto quand on branche un casque.
- **GBAmp3** : ampli **Classe D « Hi-Fi »**, plus efficace que l'ampli interne, compatible Pocket/Color/DMG.
- **CleanAmp v1.2 (RetroSix)** : remplace ampli + HP d'origine par un ampli Classe D moderne, très fort et propre.

À coupler avec un **haut-parleur de remplacement** de meilleure qualité (l'OEM sature quand on pousse le volume).

### Pour les musiciens chiptune : « Pro Sound / Clean Audio » 🔴
Sortie audio propre prise **avant le potentiomètre de volume**, envoyée vers le port link ou un jack — signal non bruité pour l'enregistrement. Mod de niche mais classique dans la scène LSDj.

---

## 4. Boutons & réactivité 🟢 *(rapide, pas cher, gros gain de feel)*

- **Pads silicone conducteurs neufs** : corrige les boutons collants/qui ne répondent plus. (Avant d'acheter : un coup de coton-tige + alcool isopropylique sur les pads suffit parfois.)
- **Boutons A/B et Start/Select de remplacement** : dispo en **20+ coloris** pour personnaliser.
- Pads compatibles coques MGB d'origine et aftermarket.

---

## 5. Coque & esthétique 🟢

- **Coques de remplacement** : transparentes, colorées, ou modèles premium (ex. « Prestige » RetroSix). Livrées en général avec vis, boutons standard, sticker, cache-pile et lentille plastique (membranes silicone souvent non incluses).
- **Coques « IPS-ready »** : pré-découpées pour la dalle IPS et/ou l'USB-C → t'évite le passage au cutter.
- Personnalisation : **stickers custom**, cache-pile et lentille de couleur, boutons assortis.

---

## 6. Entretien / restauration

### Recap (remplacement des condensateurs) 🔴
Les condensateurs électrolytiques vieillissent. Symptômes : **ne s'allume plus, écran terne, son dégradé/absent**. Le kit GBP = **4 condensateurs** : 33 µF/25 V (C29), 330 µF/6 V (C30), 100 µF/6 V (C31), 100 µF/6 V (C32). Soudure interne requise (pads pas évidents à chauffer). Privilégier des marques sérieuses (Panasonic, Nichicon, Rubycon, UCC).

### Petits entretiens 🟢
Nettoyage des **contacts de piles** (remplacement des lames corrodées si besoin), nettoyage des pads et de la lentille.

---

## 7. Extras (pas des mods hardware, mais complètent l'expérience)

- **Cartouche flash** (EverDrive-GB, EZ-Flash Junior) : charge tes ROMs/homebrew depuis une carte SD. Rappel : la GBP reste monochrome.
- **Câble link** : jeu à deux, ou détourné pour la sortie audio (voir Pro Sound).

---

## Tableau récap

| Mod | Difficulté | Soudure | Bénéfice principal | Prix indicatif* |
|---|---|---|---|---|
| Kit IPS FunnyPlaying Q5 | 🔴 | Oui | Écran couleur rétroéclairé, énorme saut visuel | ~35–55 € |
| Bivert + backlight | 🔴 | Oui | Écran éclairé (méthode ancienne) | ~15–25 € |
| Lentille verre trempé | 🟢 | Non | Clarté + protection rayures | ~5–12 € |
| Batterie USB-C Li-ion | 🟡 | Un peu | Rechargeable, fini les piles | ~15–30 € |
| Ampli + HP | 🟡 | Oui | Son 2–3× plus fort et propre | ~10–20 € |
| Pads silicone / boutons | 🟢 | Non | Boutons réactifs + perso | ~5–15 € |
| Coque de remplacement | 🟢 | Non | Look neuf / perso | ~10–20 € |
| Recap condensateurs | 🔴 | Oui | Répare alim/écran/son | ~5–10 € |

*Prix indicatifs 2026, variables selon boutique et pays.

## Parcours conseillés
- **Débutant, sans fer à souder :** lentille verre + pads/boutons neufs + coque → console remise à neuf et personnalisée.
- **Le combo « ultime » :** kit IPS Q5 + batterie USB-C + ampli/HP + coque IPS-ready → la meilleure GBP possible.
- **Restauration d'une console morte :** commencer par le **recap**, puis nettoyage contacts, avant tout mod cosmétique.

## Boutiques citées
Hand Held Legend, FunnyPlaying, Retro Modding, Retro Game Repair Shop, RetroSix, Console5, SilentModding, Tindie / shop Giltesa.
