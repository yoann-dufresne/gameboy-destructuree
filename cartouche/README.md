# Cartouche — une fente de Game Boy déportée

Ce module sépare la cartouche de la console qui la joue. On insère une vraie cartouche dans une
fente à part, le **lecteur**, loin de la console. Le lecteur la lit en entier et en envoie la
copie en WiFi à la **doublure**, un microcontrôleur branché sur la fente de la console, qui
tient le rôle de la cartouche. La console joue sur la copie ; ce que le jeu change dans la
cartouche, la sauvegarde surtout, revient à la vraie cartouche à côté, sans bloquer le jeu.

Les deux moitiés s'éprouvent séparément. Seul, le lecteur est un lecteur de cartouches WiFi :
depuis un PC, il copie un jeu, sauvegarde une partie, la restaure. Seule, la doublure est une
cartouche programmable WiFi : on lui envoie n'importe quelle ROM, et n'importe quelle Game Boy
la joue.

Dans la [Game Boy Pocket déstructurée](../README.md), la doublure est branchée sur la console
principale, celle que la [manette](../manette/README.md) allume.

## Principe

```
 LECTEUR                                       DOUBLURE                    CONSOLE PRINCIPALE
 vraie cartouche ── fente ──► [Pico] ── CAR1 ──► [Pico] ── bus cartouche ──► processeur
                                     ◄── journal ──
```

La console lit sa cartouche un million de fois par seconde et attend chaque octet moins d'une
microseconde ; le WiFi met des millisecondes. D'où la copie : à l'insertion, le lecteur lit la
ROM et la sauvegarde, une à deux secondes par Mo, et les envoie une fois. Pendant la partie, la
doublure répond au bus de la console sur place, comme la [Croco Cartridge](https://github.com/shilga/rp2040-gameboy-cartridge-firmware) :
ses PIO suivent le bus, des DMA servent les octets depuis sa flash.

Seuls trois changements remontent à la vraie cartouche : la sauvegarde, réglée en moins d'une
seconde ; l'horloge des jeux qui en ont une, quand le jeu la règle ; le vibreur, aussitôt. Les
changements de banque restent dans la doublure : personne ne lit la vraie cartouche pendant la
partie.

Au repos, la fente du lecteur est hors tension : on retire la cartouche quand on veut, sans
risquer sa sauvegarde. Pour la console, tout se passe comme avec une fente d'origine : sans
cartouche, un rectangle noir à la place du logo ; cartouche retirée en pleine partie, le jeu
se fige jusqu'au redémarrage. La cartouche fait foi : à chaque insertion, sa sauvegarde
remplace celle de la doublure. Une sauvegarde qui n'avait pas encore rejoint une cartouche
arrachée est gardée par le lecteur, et écrite si cette cartouche revient telle qu'elle était
partie.

## Matériel

| Rôle | Pièce |
|---|---|
| Lecteur | une carte RP2350B au format du Pico 2 W, avec WiFi (Waveshare RP2350B-Plus-W) ; un connecteur de cartouche de Game Boy ; 4 traducteurs de niveaux 74LVC8T245 ; un interrupteur de charge 5 V ; un microrupteur de présence |
| Doublure | la même carte RP2350B ; 4 traducteurs 74LVC8T245 ; une carte au format cartouche, enfichée dans la fente de la console |
| Mesure | un multimètre, l'analyseur logique du module capture |

Nomenclature complète : [§11 du plan](docs/plan.md).

## Contenu

| Document | Contenu |
|---|---|
| [`docs/plan.md`](docs/plan.md) | l'analyse et les décisions : le bus de la cartouche, ce qui remonte à la vraie cartouche, le lecteur, la doublure, les situations d'insertion et de retrait, le protocole `CAR1`, le réseau, les durées, les risques |
| [`docs/etapes.md`](docs/etapes.md) | les phases pas à pas, leurs tests et leurs critères de sortie ; la fiche de la phase 0, à remplir |
| [`docs/fenetre-du-bus.md`](docs/fenetre-du-bus.md) | ce que disent les sources du cycle de lecture de la cartouche, et le temps qu'il laisse à la doublure |
| [`docs/brochage.md`](docs/brochage.md) | la doublure et le lecteur sur la carte RP2350B, broche par broche |
| [`docs/etude-croco.md`](docs/etude-croco.md) | comment la Croco Cartridge sert le bus, et ce qu'on en reprend |
| [`firmware/commun/`](firmware/commun/README.md) | le code que partagent les deux moitiés : contrôleurs, protocole `CAR1`, automates de la sauvegarde ; testé sur PC |

## État

Rien n'est construit. Le module s'ouvre le 11/10/2026. Sans matériel, le cœur logique est
écrit et éprouvé sur PC : les contrôleurs de banques, le protocole, et les règles de la
sauvegarde sous les pannes ([`firmware/commun/`](firmware/commun/README.md)).

| Phase | État |
|---|---|
| 0 · Relevés sur la console et les cartouches, sans rien souder | à venir |
| 1 · Le lecteur sur plaque d'essai ; les sauvegardes à l'abri | à venir |
| 2 · La doublure au banc, lue par le lecteur | à venir |
| 3 · La doublure dans la console, enfichée | à venir |
| 4 · Les deux moitiés par le WiFi | à venir |
| 5 · Avec la manette : allumer, se figer, redémarrer | à venir |
| 6 · Les cartes maison, la place définitive | à venir |
