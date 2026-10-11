# Le brochage — la doublure et le lecteur sur la Waveshare RP2350B-Plus-W

*Note de conception, 11/10/2026. Proposition à confirmer en T2.2 ([étapes](etapes.md)).*

La fente compte 29 signaux utiles, VCC compris ([plan](plan.md) §2.2). Cette note les place
sur la carte retenue au [plan](plan.md) §5.1, d'après son schéma, et dit ce qui reste à
vérifier.

## La carte

Source : le schéma de Waveshare,
[RP2350B-Plus-W.pdf](https://files.waveshare.com/wiki/RP2350B-Plus-W/RP2350B-Plus-W.pdf).

| GPIO | Où | Libre ? |
|---|---|---|
| 0 à 22 | les deux rangées de broches, comme sur un Pico 2 W | oui |
| 23 | — | non : la LED rouge de la carte |
| 24 à 35 | pastilles sous la carte | oui |
| 36 à 39 | — | non : le module WiFi RM2 (WL_ON, WL_D, WL_CS, WL_CLK) |
| 40, 41, 42 | les broches où le Pico 2 W a GP26, GP27, GP28 | oui ; ADC 0 à 2 |
| 43, 44, 45 | pastilles sous la carte | oui ; ADC 3 à 5 |
| 46 | — | non : la mesure de VSYS |
| 47 | — | non : la sélection de l'empreinte PSRAM, vide |

**41 broches libres**, dont 15 en pastilles dessous. La documentation de Zephyr situe la
sélection de la PSRAM sur la broche 0 : le schéma dit 47, et c'est lui qui fait foi.

## Les contraintes du PIO

- **Une fenêtre de 32 broches.** Sur un RP2350B, chacun des trois blocs PIO voit soit les
  GPIO 0 à 31, soit les GPIO 16 à 47, à choisir. Tout ce qu'une machine du bus lit ou
  commande doit tenir dans la fenêtre de son bloc.
- **Des broches qui se suivent.** `in pins` lit des broches consécutives ; `out pins` et le
  side-set écrivent des broches consécutives. Les 8 données doivent donc se suivre, de même
  que les deux commandes du traducteur des données si le PIO les pilote en side-set.
- **Le WiFi a son bloc.** Le pilote du CYW43 parle au RM2 par un PIO : ce bloc doit voir les
  GPIO 36 à 39, donc la fenêtre 16 à 47. Il reste deux blocs, soit 8 machines, pour le bus : la
  Croco V1 les utilise toutes.

La Croco V2 place tout le bus dans la fenêtre haute (CLK 17, `/RD` 18, A0–A15 20 à 35, D0–D7
36 à 43). Sur cette carte, c'est impossible : le RM2 occupe 36 à 39, et la fenêtre haute ne
compte que 25 broches libres pour 26 signaux. **Le bus va dans la fenêtre basse, 0 à 31.**

## La doublure

| GPIO | Où | Signal | Par |
|---|---|---|---|
| 0 à 15 | broches | A0 à A15 (fente 6 à 21) | deux 74LVC8T245, sens fixe : fente → Pico |
| 16 | broche | PHI, l'horloge du cycle (fente 2) | troisième 74LVC8T245, même sens |
| 17 | broche | `/RD` (fente 4) | idem |
| 18 | broche | `/WR` (fente 3) | idem |
| 19 | broche | `/CS` (fente 5) | idem |
| 20 | broche | DIR du traducteur des données | side-set du PIO |
| 21 | broche | `/OE` du traducteur des données | side-set du PIO, à la suite |
| 22 | broche | libre | |
| 24 à 31 | pastilles | D0 à D7 (fente 22 à 29) | quatrième 74LVC8T245, sens commandé |
| 32 | pastille | `/OE` des trois traducteurs d'entrée | le processeur : ouverts quand VCC est là |
| 33 | pastille | `/RST` (fente 30), facultatif | un transistor N qui tire la ligne à 0 |
| 34, 35 | pastilles | libres | |
| 40, 41 | broches | SDA, SCL de l'I²C0 : le bus de la console ([réseau](../../reseau.md) §5) | |
| 42 | broche | VCC de la fente (broche 1) | pont diviseur, ADC2 : présence et tension |
| 43 à 45 | pastilles | libres | |

Les blocs PIO 0 et 1 en fenêtre basse servent le bus ; le bloc 2, en fenêtre haute, le WiFi.
Une seule instruction `in pins 32` depuis la broche 0 capture d'un coup l'adresse, les
commandes et les données. Le processeur garde les bits utiles.

**Libres : 22, 34, 35, 43, 44, 45.** `/WR` et `/CS` sont commodes, pas indispensables : la
Croco s'en passe. Elle distingue une écriture à `/RD` remonté, et décode A000–BFFF par A13
à A15. Sans eux, et sans `/RST`, il reste 9 broches libres. C'est exactement ce que
demanderait l'injecteur du module manette, si l'on voulait le loger sur la même carte : 8
photoMOS et l'alimentation de la console.

`/RST` : la Croco tient la console en remise à zéro par cette broche tant que son jeu n'est
pas chargé. Ce serait une autre façon de donner le feu vert du [plan](plan.md) §5.6, sans
passer par l'injecteur. Je la garde câblable ; rien ne l'utilise aujourd'hui.

## Le lecteur

Même carte, même logique de placement. La lecture n'a pas d'échéance à la nanoseconde : le
processeur pilote les broches lui-même, sans PIO.

| GPIO | Où | Signal | Par |
|---|---|---|---|
| 0 à 15 | broches | A0 à A15 | deux 74LVC8T245, sens fixe : Pico → fente |
| 16 | broche | `/RD` | troisième 74LVC8T245, même sens |
| 17 | broche | `/WR` | idem |
| 18 | broche | `/CS` | idem |
| 19 | broche | `/RST`, tenu haut | idem |
| 20 | broche | PHI vers la cartouche, facultatif | idem |
| 21 | broche | `/OE` des trois traducteurs de sortie | |
| 22 | broche | DIR du traducteur des données | |
| 24 à 31 | pastilles | D0 à D7 | quatrième 74LVC8T245, sens commandé |
| 32 | pastille | `/OE` du traducteur des données | |
| 33 | pastille | l'interrupteur de charge 5 V de la fente | |
| 34 | pastille | le microrupteur de présence | entrée, tirage interne |
| 35 | pastille | la LED RGB (WS2812) | |
| 40 à 45 | | libres | |

Les traducteurs prennent leur 5 V **après** l'interrupteur de charge : fente coupée, ils
isolent d'eux-mêmes, sans rien laisser passer vers la cartouche ([plan](plan.md) §4.1).

PHI : d'après l'émulateur SameBoy, seule la caméra s'en sert ; aucun contrôleur du périmètre
n'en a besoin. La broche reste câblée au cas où un MBC refuserait, à voir en T1.4.

## Ce qui reste à vérifier en T2.2

1. **Les pastilles du dessous** : 8 données et 4 commandes. Au banc, ce sont des fils soudés
   sous la carte ; sur la carte maison, le module se soude à plat, pastilles comprises. À
   essayer dès la phase 1, sur une carte de rechange.
2. **Un en-tête de carte pour le SDK.** Le pico-sdk 2.3.1 n'en a pas pour cette carte. On
   l'écrira d'après `pimoroni_pico_plus2_w_rp2350.h`, une autre carte RP2350B avec un RM2 :
   RM2 sur 36 à 39, LED sur 23, 16 Mo de flash, pas de PSRAM.
3. **Le bloc PIO du WiFi.** Le pilote du CYW43 prend un bloc libre : les deux blocs du bus
   doivent être réservés avant `cyw43_arch_init()`, ou le bloc du WiFi imposé.
4. **Les retards des traducteurs**, à mesurer à l'analyseur pendant T3.4 : ils mangent la
   marge de la [fenêtre du bus](fenetre-du-bus.md).
