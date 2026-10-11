# La fenêtre du bus — combien de temps la doublure a pour répondre

*Note de conception, 11/10/2026. Prépare T0.2 et T3.4 ([étapes](etapes.md)) ; ne les remplace pas.*

Le [plan](plan.md) §2 pose la question : entre l'instant où la console demande un octet et
l'instant où elle le lit, combien de temps ? Cette note rassemble ce que disent les sources,
sépare ce qui y est écrit de ce que j'en déduis, et en tire une estimation chiffrée.

## Ce que dit la source

La référence de Gekkio, *Game Boy: Complete Technical Reference*, révision 192 du 16/08/2026
([gbctr.pdf](https://gekkio.fi/files/gb-docs/gbctr.pdf)), annexe C.1 « Bus timings ». Elle
porte sur les consoles d'avant la Game Boy Color, Pocket comprise. Ses chronogrammes
(figures C.8 à C.11) placent chaque front par rapport à deux horloges internes :
- CLK, à 4 MiHz : un **cycle T** de 238 ns ;
- PHI, à 1 MiHz : un **cycle machine** de 4 cycles T, 954 ns. PHI est haute pendant la
  première moitié du cycle et basse pendant la seconde.

**Une lecture de la ROM, 0000–7FFF** (figure C.9 a) :

| Front | Quand, dans le cycle machine |
|---|---|
| `/RD` descend | au tout début, et reste bas |
| A0–A14 changent | au premier front descendant de CLK, une demi-période après le début |
| A15 descend | au début du deuxième cycle T |
| la cartouche présente la donnée | dès qu'A15 est bas |
| **la console lit la donnée** | **au front montant de CLK qui ouvre le quatrième cycle T** : la ligne verticale de la figure |
| A15 remonte, l'adresse change | à la fin du cycle |

**Une lecture de A000–FDFF** (figure C.9 b) : la même chose, à ceci près que c'est `/CS` qui
descend au début du deuxième cycle T, et qu'A15 reste haut. `/CS` descend aussi pour
C000–FDFF, la RAM de travail de la console : la doublure décode elle-même A000–BFFF
([plan](plan.md) §2.1).

**Une écriture** (figure C.10) : `/RD` remonte peu après le début du cycle. `/WR` descend au
milieu du cycle, quand PHI descend, et remonte vers le milieu du quatrième cycle T. La
console tient la donnée de la descente de `/WR` à la fin du cycle.

**Une lecture par le DMA de l'OAM** (figure C.11) : l'adresse, A15 ou `/CS` sont posés dès le
début du cycle, et la donnée est attendue sur tout le cycle. La fenêtre y est plus large que
pour le processeur : ce n'est pas elle qui contraint la doublure. Mais la doublure doit la
servir aussi : des jeux copient leurs sprites depuis la ROM.

Les accès à 0000–00FF pendant que la ROM de démarrage est active ne sortent pas sur le bus
(note de la figure C.9).

## Ce que la source ne dit pas

Les chronogrammes donnent **l'ordre des fronts et leur alignement sur CLK**, pas des durées
mesurées. Ils ne disent rien des retards de sortie du processeur, de la carte ou de la
fente, ni du temps de préparation qu'il faut à la donnée avant que la console la lise. Ils
ne disent pas non plus laquelle de ces horloges sort sur la broche 2 de la fente. La Croco
Cartridge s'y cale comme sur PHI : elle attend son front montant, puis un délai, avant de
lire l'adresse. Ce serait donc PHI, à 1 MHz. **T0.2 le vérifie.**

## L'estimation, déduite

En prenant les fronts aux instants idéaux des figures, à 4,194304 MHz :

| Instant | Depuis le début du cycle |
|---|---|
| adresse A0–A14 stable | ≈ 120 à 155 ns |
| A15 ou `/CS` bas : on sait que c'est pour nous | ≈ 240 ns |
| la console lit la donnée | ≈ 715 ns |
| fin du cycle | ≈ 954 ns |

- **Depuis A15 ou `/CS` : environ 475 ns.**
- Depuis l'adresse stable, si l'on va chercher l'octet sans attendre A15 : environ 560 ns.

De ces 475 ns, il faut retirer :
- deux traversées d'un 74LVC8T245, quelques ns chacune ;
- la synchronisation des entrées du PIO, deux cycles d'horloge système, 13 ns à 150 MHz ;
- les instructions du PIO et la cascade de DMA ;
- le temps de préparation de la console, inconnu.

Il reste de l'ordre de **350 à 400 ns pour aller chercher l'octet**. Une lecture en RAM du
RP2350 en prend quelques dizaines. Une lecture en flash qui rate le cache, ou en PSRAM, en
prend quelques centaines : c'est là que le risque T3.5 se joue
([étude de la Croco](etude-croco.md)).

Le mode double vitesse de la Game Boy Color divise tout par deux. La Pocket ne l'a pas : sans
objet ici.

## Ce que T0.2 et T3.4 doivent trancher

- **T0.2**, à l'analyseur 8 voies à 24 MHz du module capture, soit ± 42 ns : vérifier l'ordre
  des fronts ci-dessus et la fréquence de la broche 2, et estimer la fenêtre à ± 42 ns près.
  Les voies proposées dans les [étapes](etapes.md) suffisent : CLK, `/RD`, `/WR`, `/CS`, A15,
  A0, D0, D7.
- **T3.4**, la marge réelle : la doublure retarde sa réponse jusqu'au plantage. C'est la seule
  mesure qui intègre le temps de préparation de la console, que rien ne documente.

## Pour le PIO de la doublure

Deux manières de se caler, toutes deux dans la Croco Cartridge :
- attendre le front montant de PHI, puis un délai fixe, avant de lire l'adresse : la
  machine principale de la Croco V1 ;
- attendre la descente d'A15 : la machine qui déclenche les lectures de ROM, dans les deux
  versions.

La seconde suit la console au lieu de supposer ses retards : c'est celle que je propose. La
donnée est posée dès qu'elle est prête et relâchée au front montant suivant de PHI, en fin de
cycle.
