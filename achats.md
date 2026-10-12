# Liste d'achats — pendant le voyage

*Établie le 11/10/2026, pour commander pendant ton absence : les délais de livraison courent
pendant que le matériel est hors de portée.* Elle rassemble les nomenclatures des modules
pour leurs prochaines phases, et le banc de coexistence du [réseau](reseau.md). Les
nomenclatures complètes restent dans les plans : celle-ci ne dit que **quoi commander
maintenant, combien, où**.

Coche la colonne **Là ?** avant de commander : je ne sais pas tout ce que tu as déjà.

---

## Ce que valent les liens et les prix

Comme pour la [liste du module capture](capture/docs/liste-achats.md) :

- les liens AliExpress sont des **liens de recherche**, stables, pas des annonces vérifiées.
  Vérifie la variante, le vendeur et le stock ;
- **les prix sont des ordres de grandeur** ;
- les composants dont la contrefaçon ou la variante comptent (photoMOS, traducteurs de
  niveaux, MOSFET, références de tension) se prennent chez un distributeur : **Mouser**,
  **DigiKey** ou **LCSC**. Livraison en quelques jours, gratuite au-delà d'un seuil vers 50 € :
  une seule commande pour tout ce qui est marqué *distributeur*.

**Deux commandes, pas plus** : une chez un distributeur, une sur AliExpress (deux à quatre
semaines). Plus les cartes du §4 chez un fabricant de circuits imprimés, si tu assouplis la
règle de la cartouche.

---

## 0. Une décision avant de commander

Le plan cartouche dit **« rien n'est commandé avant T0.2 »** : la mesure de la fenêtre du bus,
qui dit si la doublure est faisable. Le §4 coûte environ 90 €. La Croco Cartridge sert déjà le
bus d'une Game Boy avec un RP2040 : le risque que T0.2 ferme la porte est faible, et attendre
ajoute deux à trois semaines au module. **À toi de choisir** : commander le §4 maintenant, ou
après T0.2.

---

## 1. Le banc de coexistence — [réseau](reseau.md), §7

Huit Pico 2 W : quatre points d'accès, quatre clients. **Ils ne sont pas perdus après le
banc** : ils deviennent la source et le haut-parleur du son, l'injecteur et la manette de
banc, les faux écrans de R2.

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | Raspberry Pi Pico 2 W | 8, moins ceux que tu as en rechange | revendeur officiel (Kubii, Mouser) : pas de copie | 7 € pièce |
| ☐ | Câble micro-USB **de données** | 8 | [AliExpress](https://fr.aliexpress.com/w/wholesale-micro-usb-data-cable-short.html) | 1 € pièce |
| ☐ | Hub USB alimenté, 7 à 10 ports, **2 A au moins** : un Pico qui émet tire jusqu'à 250 mA | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-powered-usb-hub-10-port.html), ou un magasin | 15–25 € |
| ☐ | Chargeur USB 5 V, pour les quatre clients posés à 3 m du hub. Ceux des téléphones font l'affaire | 4 | déjà là, sans doute | — |
| ☐ | Fils Dupont femelle-femelle | 1 lot | [AliExpress](https://fr.aliexpress.com/w/wholesale-dupont-wire-female-female.html) | 2 € |

---

## 2. Son — phases 1 à 5 ([plan](son/docs/plan.md), §11)

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | Module MAX98357A | 2 | [AliExpress](https://fr.aliexpress.com/w/wholesale-max98357a-module.html) | 2 € pièce |
| ☐ | Haut-parleur de Game Boy Pocket, de rechange | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-game-boy-pocket-speaker.html) | 3 € |
| ☐ | Condensateur film 1 µF | 4 | distributeur | 0,5 € pièce |
| ☐ | Condensateurs C0G 820 pF et 1,5 nF | 2 de chaque | distributeur | 0,2 € pièce |
| ☐ | Condensateurs 10 µF céramique et **100 µF** électrolytique | 4 et 2 | distributeur | quelques € |
| ☐ | **Perle de ferrite**, pour le filtre d'entrée du Pico du son (décision du 11/10) | 3 | distributeur, ex. série BLM18 en 0603, ou traversante | 0,2 € pièce |
| ☐ | Connecteur JST-SH 3 points, avec embase | 1 paire | celui du module capture si le kit en contient | — |
| ☐ | Câble blindé fin à 2 conducteurs | 1 m | [AliExpress](https://fr.aliexpress.com/w/wholesale-2-core-shielded-wire-thin.html) | 2 € |
| ☐ | En réserve : référence LM4040 3,0 V, en boîtier TO-92 | 2 | distributeur | 1 € pièce |
| ☐ | En réserve : module ADC PCM1808 | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-pcm1808-module.html) | 3 € |

Les deux pièces en réserve ne servent que si T3.1 entend la radio : à quelques euros, elles
évitent trois semaines d'attente à ce moment-là. La résistance Rb dépend de T0.4 : le kit de
résistances du module capture la contient.

---

## 3. Manette — phases 1 à 3 ([plan](manette/docs/plan.md), §11)

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | PhotoMOS **AQY221N2S** (Panasonic) | 10 | **distributeur** : la faible capacité est tout l'intérêt, pas de copie | 2–3 € pièce |
| ☐ | Adaptateurs SOP-4 vers DIP | 10 | [AliExpress](https://fr.aliexpress.com/w/wholesale-sop4-to-dip-adapter.html) | 2 € le lot |
| ☐ | Diode 1N4148 | 10 | distributeur ou kit | — |
| ☐ | P-MOSFET DMG2305UX | 4 | distributeur | 0,3 € pièce |
| ☐ | N-MOSFET à seuil bas : DMG1012T (VGS(th) ≤ 1 V, boîtier SOT-523), ou tout SOT-23 dont la fiche garantit VGS(th) max ≤ 1 V | 4 | distributeur | 0,3 € pièce |
| ☐ | Adaptateurs SOT-23 (et SOT-523 si DMG1012T) vers DIP | 6 | [AliExpress](https://fr.aliexpress.com/w/wholesale-sot23-to-dip-adapter.html) | 2 € le lot |
| ☐ | Diode Schottky BAT54 | 4 | distributeur | — |
| ☐ | **ADXL362** sur carte : SparkFun SEN-11446, alimentée en direct de 1,6 à 3,5 V, donc sans régulateur. Vérifier sur son schéma qu'elle n'a pas de LED | 1 | distributeur, ou [SparkFun](https://www.sparkfun.com/sparkfun-triple-axis-accelerometer-breakout-adxl362.html) | 15–20 € |
| ☐ | Adaptateur de niveaux 4 voies | 2 | [AliExpress](https://fr.aliexpress.com/w/wholesale-4-channel-logic-level-converter.html) | 1 € pièce |
| ☐ | Boîtier 2 × AAA avec interrupteur | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-2-aaa-battery-holder-switch.html) | 1 € |
| ☐ | Piles AAA NiMH et chargeur | 4 | magasin | 10–15 € |
| ☐ | Boutons poussoirs 6 mm, potentiomètres 10 kΩ linéaires | 8 et 2 | [AliExpress](https://fr.aliexpress.com/w/wholesale-tactile-switch-6x6-kit.html) | 2 € |
| ☐ | Fiche DC 2,35 × 0,75 mm, centre positif, avec câble | 1 | [AliExpress](https://fr.aliexpress.com/w/wholesale-dc-2.35x0.75-cable.html) | 2 € |
| ☐ | Connecteur JST-SH 6 points, avec embase | 1 paire | celui du module capture si le kit en contient | — |

---

## 4. Cartouche — phases 1 à 3, **si tu assouplis la règle du §0** ([plan](cartouche/docs/plan.md), §11)

| Là ? | Pièce | Qté | Où | ~Prix |
|---|---|---|---|---|
| ☐ | **Waveshare RP2350B-Plus-W** | 3 | [Waveshare](https://www.waveshare.com/rp2350b-plus-w.htm), ou sa boutique AliExpress | 11 € pièce |
| ☐ | **74LVC8T245**, en TSSOP-24 (SN74LVC8T245PWR) | 10 | **distributeur** | 1 € pièce |
| ☐ | Adaptateurs TSSOP-24 vers DIP | 10 | [AliExpress](https://fr.aliexpress.com/w/wholesale-tssop24-to-dip-adapter.html) | 3 € le lot |
| ☐ | Connecteur de fente de cartouche Game Boy DMG, 32 broches, de rechange | 2 | [AliExpress](https://fr.aliexpress.com/w/wholesale-game-boy-dmg-cartridge-slot.html) | 3–5 € pièce |
| ☐ | **Carte GB-BRK-SLOT-A** de Gekkio : la fente du lecteur au banc. Fichiers de fabrication libres (CC BY 4.0) dans [gb-hardware](https://github.com/Gekkio/gb-hardware/tree/main/GB-BRK-SLOT-A) ; 1,2 mm, finition quelconque | 5 | JLCPCB ou PCBWay, avec les fichiers `gerber/` | 5–10 € + port |
| ☐ | **Carte GB-BRK-CART** de Gekkio : la plaquette au format cartouche, enfichée dans la console. [gb-hardware](https://github.com/Gekkio/gb-hardware/tree/main/GB-BRK-CART) ; **1,0 mm, finition ENIG obligatoire** (pas de HASL : les contacts s'useraient) | 5 | JLCPCB ou PCBWay | 20–30 € + port |
| ☐ | Barrette 2 × 16 au pas de 2,54 mm, CMS, pour la GB-BRK-CART | 2 | distributeur (Molex 15-91-0800, celle de sa nomenclature) | 3 € pièce |
| ☐ | Interrupteur de charge 5 V, 2 A, à broche d'activation : TPS22918 (SOT-23-6), et un adaptateur SOT-23-6 vers DIP | 2 | distributeur | 1 € pièce |
| ☐ | Microrupteur à levier, petit | 3 | [AliExpress](https://fr.aliexpress.com/w/wholesale-micro-limit-switch-lever-small.html) | 1 € le lot |
| ☐ | **Une cartouche d'essai sans valeur**, avec pile et sauvegarde, MBC1 : Super Mario Land 2 ou Wario Land, d'occasion. Pile en état : sinon, T1.7 ne relira rien | 1 | occasion | 10–20 € |

Les cartes de Gekkio évitent de dessiner deux cartes de banc : elles sortent les 32 signaux
de la fente sur des broches, l'une côté console, l'autre côté cartouche.

---

## 5. Ce que tu as sans doute déjà

Kit de résistances, fil Kynar, flux, étain et tresse, kit JST-SH (module capture) ; analyseur
logique, multimètre, alimentation de laboratoire ; plaque d'essai et fils de liaison.

---

## 6. Ce qui attend un relevé : ne pas commander maintenant

| Pièce | Attend | Pourquoi |
|---|---|---|
| Régulateur 3,0 V commandé de l'injecteur | T0.8 de la manette | son courant se dimensionne sur celui de la console |
| LED RGB de la manette | T0.1 de la manette | son diamètre est celui de la fenêtre de la coque |
| Carte maison de la manette | phases 0 et 1 | dessinée sur les scans et le brochage |
| Cartes maison du lecteur et de la doublure | phase 2 de la cartouche | dessinées sur le brochage figé |
| Pièces de rechange de la manette : interrupteur, molettes, contacts de piles | T0.1 à T0.3 | la référence exacte dépend de la révision de la carte |

---

## Récapitulatif

| Bloc | ~Prix |
|---|---|
| 1 · Banc de coexistence, 8 Pico 2 W compris | 75–90 € |
| 2 · Son | 20–30 € |
| 3 · Manette | 60–80 € |
| 4 · Cartouche, si la règle est assouplie | 90–120 € |
