# L'étude de la Croco Cartridge — ce qu'on en reprend pour la doublure

*Note de conception, 11/10/2026. Lecture du code ; rien n'en est recopié dans ce dépôt.*

La Croco Cartridge, de Sebastian Quilitz, est une cartouche Game Boy programmable qui fait
exactement le travail de la doublure : servir le bus de la console depuis un
microcontrôleur. Deux versions :

| | Croco V1 | Croco V2 |
|---|---|---|
| Firmware | [rp2040-gameboy-cartridge-firmware](https://github.com/shilga/rp2040-gameboy-cartridge-firmware), en C | [rp2350-gameboy-cartridge-firmware](https://github.com/shilga/rp2350-gameboy-cartridge-firmware), en Rust (Embassy) |
| Puce | RP2040, poussé à 266 MHz | RP2350B, poussé à 266 MHz |
| La ROM | dans la flash du RP2040, lue sans passer par le cache | dans une HyperRAM de 8 Mo, lue par un PIO ; chargée depuis une carte SD |
| Licence | GPL-3.0 | GPL-3.0 |

Le matériel est publié à part ([rp-gameboy-cartridge-hw](https://github.com/shilga/rp-gameboy-cartridge-hw)) :
la V2 passe le bus par des traducteurs TXS0108E et TXB0108, et porte une horloge temps réel
sur pile (MCP795xx).

## Comment la V1 sert le bus

**Le brochage** : CLK 0, `/RD` 1, A0–A15 2 à 17, D0–D7 18 à 25 ; `/RST` de la fente sur la
26 (sur la V2, par un transistor) ; l'activation des traducteurs sur la 29. Ni `/WR` ni `/CS` : une
écriture se reconnaît à `/RD` remonté, et A000–BFFF se décode par A15, A14, A13.

**Les PIO**, les 8 machines des deux blocs :
- **la machine principale** suit chaque cycle. Au front montant de CLK, elle attend une
  boucle d'environ 70 cycles (≈ 260 ns à 266 MHz), puis lit `/RD` et l'adresse. Pour une écriture, elle
  attend le front descendant de CLK, lit aussi la donnée, et pousse le tout vers le
  processeur ;
- **une machine guette la descente d'A15**, et réveille selon A14 la machine « ROM basse »
  (0000–3FFF) ou la machine « ROM haute » (4000–7FFF). Chacune pousse l'adresse dans sa file ;
- **deux machines RAM**, l'une pour lire, l'autre pour écrire, font de même pour
  A000–BFFF ; une huitième départage A14 pour elles ;
- **une machine pose la donnée** : elle prend un octet, le sort, passe les 8 broches en
  sortie, attend la fin du cycle, puis les relâche.

**Les DMA servent l'octet, sans le processeur** :
- **ROM basse**, depuis la RAM : un canal lit l'adresse dans la file du PIO et l'écrit
  comme adresse de lecture d'un deuxième canal. Un troisième y ajoute la base de la banque
  et le déclenche. Le deuxième copie l'octet dans la file de la machine qui le pose ;
- **RAM et horloge** : une petite « machine à commandes » de deux canaux déroule, à chaque
  requête, une liste de transferts : lire, écrire, lire ou écrire un registre de l'horloge ;
- **ROM haute, directement dans la flash.** Le contrôleur de la flash (le SSI du RP2040) est
  basculé en lecture d'un octet. Une chaîne de quatre canaux fait le reste : l'adresse du
  PIO, plus la base de la banque calculée par le « sniffer » du DMA, est écrite dans le SSI,
  qui lance la lecture ; l'octet reçu part dans la file du PIO. **Prix : pendant le jeu, la
  flash ne sert plus de mémoire de code.** Tout ce qui tourne doit être en RAM.

**Le processeur** : le cœur 0 lit les écritures que remonte le PIO, décode les registres du
MBC et change les bases des DMA. C'est tout. Le cœur 1 ne fait rien.

**Les sauvegardes** restent en RAM ; elles ne sont écrites en flash que sur un appui du
bouton, qui redémarre la cartouche : écrire la flash et servir la console en même temps ne
se fait pas. **L'horloge du MBC3** est émulée en logiciel. **La console reste en remise à
zéro**, par `/RST`, tant que le jeu n'est pas chargé.

## Ce que dit la V2

Sur RP2350B, l'auteur **n'a pas repris la lecture directe de la flash**. Le RP2350 remplace
le SSI du RP2040 par un autre contrôleur, le QMI : l'astuce de la V1 ne s'y transpose pas
telle quelle. La ROM est chargée dans une HyperRAM externe, et un PIO écrit pour « lire un
seul octet le plus vite possible » va l'y chercher. Le bus, lui, est servi comme en V1 :
machines PIO, cascades de DMA, processeur réduit au MBC. Le bus est placé dans la fenêtre
haute des GPIO (CLK 17, `/RD` 18, A0–A15 20 à 35, D0–D7 36 à 43).

C'est l'indice le plus net sur notre risque principal, T3.5 : servir la ROM depuis la flash
d'un RP2350 n'est démontré par personne, et le seul projet qui l'aurait pu a choisi une
autre mémoire.

## Ce qu'on reprend

- **Le partage des rôles** : les PIO suivent le bus, les DMA servent l'octet, le processeur ne
  fait que le MBC.
- **Les cascades de DMA**, telles quelles dans leur principe, pour la ROM basse, la RAM et
  l'horloge.
- **Le calage sur la descente d'A15** pour les lectures de ROM, et une machine qui pose la
  donnée puis la relâche en fin de cycle ([fenêtre du bus](fenetre-du-bus.md)).
- **`/RST` tenu bas tant que la copie n'est pas prête** : une seconde façon de donner le feu
  vert du [plan](plan.md) §5.6. Elle est câblable dans le [brochage](brochage.md).

## Ce qui change chez nous

**Le WiFi sur la même puce.** La Croco laisse un cœur libre : le WiFi le prend. Mais le code
du WiFi vit en flash, et c'est là que nous voulions la ROM.

**Où mettre la ROM**, dans l'ordre où je propose de les essayer en T3.4 et T3.5 :

1. **La flash, lue par le cache XIP**, sans la mettre en mode d'accès direct, avec tout ce
   qui tourne pendant le jeu placé en RAM : le pilote du WiFi, lwIP, l'automate. Personne
   d'autre ne lit alors la flash pendant le jeu ; le microcode du CYW43 n'y est lu qu'au
   démarrage. Un défaut de cache coûte quelques centaines de ns, dans la marge estimée.
   Copier tout le firmware en RAM serait plus simple, mais le microcode du CYW43 et la
   sauvegarde jusqu'à 128 Kio ne laissent guère de place : à chiffrer.
2. **Les banques les plus lues en RAM**, la banque 0 d'abord, comme la Croco : 16 Kio.
3. **La voie de la Croco V2** : une mémoire externe dédiée, lue par un PIO. L'empreinte PSRAM
   de la carte Waveshare ne l'offre pas : elle partage le QMI avec la flash. Il faudrait une
   HyperRAM, ou une PSRAM, sur des broches à elle.
4. **Le plan B du plan** §5.3 : deux puces.

**Le marquage des pages.** La Croco écrit la RAM par DMA sans que le processeur voie la
valeur. La doublure doit savoir quelles pages ont changé. Deux façons :
- la machine RAM remonte aussi chaque écriture au processeur, qui marque la page. C'est ce
  que suppose `doublure.hpp`, par `console_ecrit()` ;
- ou, plus économe : au moment de journaliser, comparer la RAM vivante à une ombre de ce qui
  est déjà parti, page par page. Une comparaison de 32 Kio prend quelques dizaines de µs.
  Le délai de silence de 200 ms ([plan](plan.md) §3.1) se mesure alors à un compteur
  d'écritures tenu par le DMA.

## Une première esquisse du firmware de la doublure

| Où | Quoi |
|---|---|
| PIO 0 et 1, fenêtre basse | le bus : le cycle, A15 bas, ROM basse, ROM haute, RAM, pose de la donnée avec DIR et `/OE` du traducteur en side-set ([brochage](brochage.md)) |
| PIO 2, fenêtre haute | le WiFi (RM2) |
| DMA | ROM banque 0 en RAM ; ROM haute en flash par le cache XIP ; RAM de sauvegarde et horloge en RAM |
| Cœur 0, code en RAM | le MBC : il lit les écritures, change les bases des DMA, fait avancer l'horloge |
| Cœur 1 | lwIP, l'automate de `firmware/commun/doublure.hpp` (copie, journal, silence, feu vert), le bus I²C de la console |
| Flash | le firmware ; les ROM déjà vues ; le journal, console éteinte seulement |

Le passage entre les deux cœurs se résume à trois choses :
- les écritures du jeu, que le cœur 0 remonte ;
- « se taire », que le cœur 1 commande : `/OE` du traducteur des données fermé, machines
  arrêtées ;
- la copie installée avant l'allumage.

## Licence

Les deux firmwares sont sous GPL-3.0, compatible avec l'AGPL-3.0 du dépôt. Rien n'en est
recopié ici. Si du code en était repris un jour, il garderait sa licence et ses mentions
d'auteur.
