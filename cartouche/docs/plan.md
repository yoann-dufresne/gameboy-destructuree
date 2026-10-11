# Module CARTOUCHE — plan de réalisation

*Sous-projet « cartouche » de la Game Boy Pocket déstructurée.*
Version 1 — 11/10/2026 : la cartouche est lue à l'insertion, la console joue sur une copie.

**Le « pourquoi » est ici.** Le pas à pas, ses tests et ses critères de sortie :
[`etapes.md`](etapes.md).

---

## 0. Décisions

Les huit premières lignes sont tes décisions. Les autres sont des propositions : la dernière
colonne dit quel test les confirme ou les renverse, ou s'il te revient de trancher.

| Sujet | Décision ou proposition | Tranchée par |
|---|---|---|
| **Une fente à part** | la vraie cartouche se branche dans une fente séparée de la console qui joue | toi, 11/10/2026 |
| **Une copie dans la console** | à l'insertion, la cartouche est lue en entier et sa copie part dans un microcontrôleur soudé sur la console, qui tient le rôle de la cartouche : la console lit et écrit dans la copie, sans attendre le WiFi. Ce qui doit changer l'état de la vraie cartouche lui est transmis à côté, sans bloquer le jeu | toi, 11/10/2026 |
| **Qui fait foi** | la vraie cartouche. À chaque insertion, sa sauvegarde remplace toujours celle de la doublure | toi, 11/10/2026 |
| **Retrait en pleine partie** | comme l'original : la console se fige, et il faut la redémarrer pour rejouer | toi, 11/10/2026 |
| **Pas de cartouche au démarrage** | comme l'original : un rectangle noir à la place du logo Nintendo, et rien de plus | toi, 11/10/2026 |
| **La sauvegarde parquée** | une sauvegarde pas encore arrivée dans la cartouche quand on l'arrache est gardée par le lecteur. Si la même cartouche revient, telle qu'on l'avait laissée, le lecteur l'y écrit avant d'envoyer la copie. Si une autre cartouche arrive, elle est oubliée | toi, 11/10/2026 ; T4.7 |
| **Le journal ne se perd pas** | un journal que le WiFi n'a pas pu remettre au lecteur est écrit dans la flash de la doublure dès que la console s'éteint, et part au retour du WiFi | toi, 11/10/2026 ; T4.8 |
| **Le réseau** | la doublure émet le réseau de la cartouche, dont elle est le point d'accès ; le lecteur s'y connecte en direct (§8.2) | toi, 11/10/2026 ; banc R1 du [document réseau](../../reseau.md) |
| **Les deux moitiés** | le **lecteur**, qui porte la fente ; la **doublure**, le microcontrôleur de la console qui tient le rôle de la cartouche | — |
| **Ce qui remonte à la vraie cartouche** | la sauvegarde, l'horloge des jeux qui en ont une, le vibreur. Pas les changements de banque : personne ne lit la vraie cartouche pendant la partie (§3) | T4.3 |
| **Microcontrôleur** | une carte au format du Pico 2 W, mais avec un RP2350B : 41 broches au lieu de 26, 16 Mo de flash, le même WiFi (Waveshare RP2350B-Plus-W). La même des deux côtés | T2.2 |
| **La copie** | la ROM dans la flash de la doublure, la sauvegarde dans sa RAM. La flash garde plusieurs jeux : une cartouche déjà vue ne se recopie pas | T2.3, T3.3 |
| **Servir le bus** | à la manière de la Croco Cartridge : le PIO suit le bus, une cascade de DMA sert les lectures, le processeur ne fait que changer de banque | T3.2, T3.4 |
| **Niveaux** | des 74LVC8T245 entre le bus 5 V et le RP2350 3,3 V, des deux côtés : ils isolent quand un côté est hors tension | T0.3, T2.1 |
| **La fente au repos** | hors tension : on ne l'alimente que pour lire ou écrire. Un interrupteur au fond de la fente voit la cartouche | T1.2, T1.6 |
| **Côté console, au banc** | une carte au format cartouche, enfichée dans la fente de la console : rien de soudé. La soudure définitive se décide en phase 6 | toi, phase 6 |
| **Démarrage de la console** | cartouche dans la fente, la console attend que sa copie soit prête ; fente vide, elle s'allume aussitôt | T5.1 |
| **Protocole** | `CAR1` sur UDP, port 4245 ; la doublure demande les blocs qui lui manquent ; le journal des écritures est numéroté et acquitté | T4.1, T4.2 |
| **Cartouches prises en charge** | sans contrôleur de banques, MBC1, MBC2, MBC3 avec ou sans horloge, MBC5 avec ou sans vibreur : l'immense majorité des jeux Game Boy | T1.4 |

---

## 1. Objectif et périmètre

```
 LECTEUR                                    DOUBLURE                       CONSOLE PRINCIPALE
 vraie cartouche ── fente ──► Pico ── CAR1 ──► Pico ── bus cartouche ──► processeur de la console
                                   ◄─────────       (lectures et écritures,
            la copie part une fois, à l'insertion    servies sur place, à la ns)
            les sauvegardes remontent, à la seconde
```

**Ce qu'on construit :**

- **le lecteur** : une fente de cartouche, un Pico. Il voit la cartouche arriver, la lit en
  entier, ROM et sauvegarde, envoie la copie, puis applique à la cartouche ce que la partie
  change : la sauvegarde, l'horloge, le vibreur ;
- **la doublure**, dans la console : elle reçoit la copie et tient le rôle de la cartouche sur
  le bus de la console. Elle renvoie au lecteur ce que le jeu écrit.

**Ce qu'on ne construit pas :** les cartouches à capteurs ou à émetteurs, qui demandent la
vraie cartouche à chaque instant : la Game Boy Camera, l'infrarouge des HuC1 et HuC3, le
capteur d'inclinaison du MBC7, qui sont d'ailleurs presque tous des jeux Game Boy Color. Ni
les jeux Game Boy Color seulement, que la Pocket refuse de toute façon. Ni le plan du réseau,
commun à tout le projet ([document réseau](../../reseau.md)).

**Chaque moitié s'éprouve seule** :

- **le lecteur** est un lecteur de cartouches WiFi. Depuis un PC, il copie la ROM d'un jeu,
  sauvegarde une partie, la restaure ;
- **la doublure** est une cartouche programmable WiFi. Depuis un PC, on lui envoie n'importe
  quelle ROM, un jeu fait maison par exemple, et n'importe quelle Game Boy la joue.

---

## 2. Pourquoi une copie : le bus en chiffres

Le processeur de la Game Boy tourne à 4,19 MHz. Il accède à sa mémoire par cycles de **954 ns**,
un million par seconde, et la cartouche doit répondre **à l'intérieur d'un cycle** : quelques
centaines de nanosecondes entre l'adresse posée et la donnée lue. Le WiFi met quelques
millisecondes, dix mille fois plus. On ne peut pas aller chercher l'octet dans la vraie
cartouche : il faut qu'il soit déjà dans la console. C'est l'idée de la copie.

La fenêtre exacte, entre l'adresse stable et l'instant où le processeur lit la donnée, se
**mesure** en phase 0 (T0.2), puis s'éprouve en phase 3 : la doublure retarde volontairement
sa réponse jusqu'à la panne, ce qui donne la marge (T3.4).

### 2.1 Ce que la console demande à la cartouche

| Adresses | En lecture | En écriture |
|---|---|---|
| `0000`–`3FFF` | la ROM, banque 0 | les registres du contrôleur de banques (MBC) : activer la RAM, choisir une banque… |
| `4000`–`7FFF` | la ROM, la banque choisie | idem |
| `A000`–`BFFF` | la RAM de sauvegarde, ou l'horloge | la RAM de sauvegarde, ou l'horloge |

Une écriture dans la ROM ne change donc pas la ROM : c'est un ordre au contrôleur de banques.

⚠️ La broche `/CS` de la fente s'active aussi pour `C000`–`FDFF` : la RAM de travail de la
console, sur le même bus. La doublure doit alors **se taire**, sinon elle parle en même temps
que la RAM de la console. Elle décode elle-même `A000`–`BFFF` avec A13 à A15.

### 2.2 La fente, broche par broche

| Broche | Signal | Broche | Signal |
|---|---|---|---|
| 1 | VCC, 5 V | 22–29 | D0 à D7, les données |
| 2 | CLK, l'horloge du cycle | 30 | `/RST`, la remise à zéro |
| 3 | `/WR`, écriture | 31 | VIN, entrée audio de la cartouche, inutilisée |
| 4 | `/RD`, lecture | 32 | GND |
| 5 | `/CS`, mémoire externe | | |
| 6–21 | A0 à A15, l'adresse | | |

Vingt-neuf signaux utiles, VCC compris, que la doublure surveille. Un Pico 2 W n'a que 26
broches libres : d'où le RP2350B (§5.1).

---

## 3. Ce qui doit remonter à la vraie cartouche

Ta proposition dit que la doublure « transmet les commandes vers la vraie cartouche pour que
son état change avec le jeu ». En regardant ce que contient une cartouche, il n'y a que trois
choses à transmettre, et c'est une bonne nouvelle : le trafic est minuscule.

| Dans la cartouche | Change en jeu ? | À transmettre ? |
|---|---|---|
| **La ROM**, le jeu | jamais | non |
| **Les registres du MBC**, la banque choisie | sans cesse, des milliers de fois par seconde | **non** : ils s'effacent dès que la cartouche n'est plus alimentée, et personne ne lit la vraie cartouche pendant la partie |
| **La RAM de sauvegarde**, gardée par la pile de la cartouche | quand le jeu sauvegarde | **oui** : c'est la partie du joueur |
| **L'horloge** des MBC3 qui en ont une (Pokémon Or et Argent) | le jeu peut la régler | **oui**, quand le jeu la règle |
| **Le vibreur** des MBC5 qui en ont un (Pokémon Pinball) | le jeu l'allume et l'éteint | **oui**, aussitôt : c'est un geste physique |

### 3.1 La sauvegarde : un journal

La doublure marque les **pages de 256 octets** que le jeu modifie. Quand le jeu cesse d'écrire
pendant 200 ms, ou au plus tard une seconde après la première écriture, elle envoie les pages
marquées au lecteur, numérotées. Le lecteur alimente la fente, active la RAM de la cartouche,
écrit, la désactive, coupe la fente, et accuse réception. Une sauvegarde complète de 32 Ko, le
pire cas courant, tient en 128 pages : une trentaine de paquets.

Pourquoi des pages et pas chaque octet : un jeu qui sauvegarde écrit des milliers d'octets en
rafale, parfois plusieurs fois les mêmes. Seul l'état final compte, et il tient dans les pages.

Beaucoup de jeux écrivent dans leur RAM au démarrage sans rien changer (la Croco Cartridge
l'a remarqué) : la doublure compare avant de marquer, et une page réécrite à l'identique ne
part pas.

### 3.2 L'horloge

L'horloge de la cartouche tourne sur sa pile, que la cartouche soit branchée ou non. À
l'insertion, le lecteur la lit ; la doublure la fait avancer de son côté, avec sa propre
horloge. Si le jeu règle l'heure, l'écriture remonte. Les deux horloges dérivent lentement
l'une de l'autre ; à chaque insertion, la cartouche fait foi.

### 3.3 Le vibreur

Le moteur est dans la cartouche, piloté par un bit du MBC5 et alimenté par une pile AAA dans
la cartouche. Le lecteur **garde la fente alimentée** tant qu'une partie tourne sur une
cartouche à vibreur, et relaie chaque changement à la volée : quelques millisecondes de
retard, imperceptibles sur une vibration.

### 3.4 La sauvegarde parquée

Le journal met jusqu'à une seconde à arriver dans la cartouche (§3.1). Une cartouche arrachée
dans cette seconde n'a pas sa dernière sauvegarde, alors qu'une Pocket d'origine l'aurait
écrite aussitôt. Pour combler cet écart, **le lecteur parque la sauvegarde manquante** :

1. **Au retrait**, la doublure se tait (§5.6) et envoie aussitôt au lecteur ce qui restait de
   son journal. Le lecteur ne peut plus l'écrire : il le parque, dans sa flash, avec la
   signature de la cartouche partie.
2. **À l'insertion suivante**, le lecteur lit la cartouche (§4.3) et la compare à ce qu'elle
   était à son départ : même ROM, et même sauvegarde **hors des pages parquées**, celles que
   le lecteur va réécrire de toute façon.
3. **Même cartouche, inchangée** : le lecteur écrit les pages parquées, relit, puis envoie la
   copie. La partie est celle que le joueur avait sauvegardée, comme sur l'original.
4. **Autre cartouche, ou même cartouche modifiée entre-temps**, jouée sur une autre console
   par exemple : la sauvegarde parquée est oubliée. La cartouche fait foi.

Une seule sauvegarde parquée à la fois : la première cartouche insérée décide. Pour comparer,
le lecteur garde un **miroir** de la sauvegarde de la cartouche en place, lu à l'insertion et
tenu à jour à chaque écriture du journal, 128 Ko au plus. Le miroir est mis à jour page par
page, après chaque écriture réussie : une cartouche arrachée au milieu d'un envoi laisse ses
pages non confirmées dans le journal, qui sera parqué, et la comparaison reste juste.

Deux précautions :

- **la doublure arrête d'enregistrer le journal dès qu'elle se tait.** Le jeu figé exécute
  n'importe quoi et peut écrire au hasard dans la RAM de la cartouche : c'est ainsi qu'une
  Pocket d'origine abîme parfois une sauvegarde quand on arrache la cartouche. Ici, ce bruit
  n'est jamais parqué ;
- **les réglages d'horloge ne sont pas parqués.** Rejoués plus tard, ils remettraient
  l'horloge à une heure passée.

### 3.5 Le journal en lieu sûr, côté doublure

Si le WiFi est coupé, le journal reste dans la RAM de la doublure. Elle ne s'éteint jamais,
mais une coupure de courant ou un redémarrage le perdraient. Donc **dès que la console
s'éteint avec un journal non remis, la doublure l'écrit dans sa flash**, avec le numéro
d'insertion de la cartouche à qui il était destiné. À son redémarrage, elle le recharge. Au
retour du WiFi, il part au lecteur, qui l'applique si la cartouche est toujours là, ou le
traite comme une sauvegarde parquée sinon (§3.4) : mêmes vérifications, même règle.

Pourquoi attendre que la console s'éteigne : écrire la flash bloque sa lecture pendant des
millisecondes, et c'est d'elle que la doublure sert la ROM (§5.3). Console allumée, le jeu
planterait. Le journal ne quitte donc la flash qu'une fois remis et acquitté.

Reste une fenêtre : une coupure de courant de la doublure **pendant** la partie, WiFi coupé.
C'est l'équivalent d'une pile qu'on retire d'une Pocket d'origine en pleine partie.

---

## 4. Le lecteur

### 4.1 Le matériel

| Rôle | Pièce |
|---|---|
| La fente | un connecteur de cartouche 32 broches de Game Boy, en pièce de rechange |
| Le cerveau | la carte RP2350B avec WiFi (§5.1), la même que la doublure : une seule carte à apprendre |
| Les niveaux | quatre 74LVC8T245 : trois pour l'adresse et les signaux de commande, en sens fixe ; un pour les données, dont le sens suit la lecture ou l'écriture |
| L'alimentation de la fente | un interrupteur de charge 5 V, commandé par le Pico |
| La présence | un microrupteur au fond de la fente |
| Le témoin | une LED RGB |
| L'alimentation | 5 V permanents, par USB-C : comme les autres Pico de l'installation, le lecteur ne s'éteint jamais |

Les 74LVC8T245 traduisent entre 3,3 V et 5 V, et **isolent quand un côté est hors tension** :
fente coupée, aucun courant ne passe du Pico dans la cartouche. C'est ce qui permet de couper
la fente sans rien débrancher.

### 4.2 La fente hors tension au repos

Les cartouches Game Boy ne supportent pas d'être branchées ou retirées sous tension : un
contact qui frotte pendant que la cartouche est alimentée peut écrire n'importe quoi dans la
RAM de sauvegarde. C'est la façon classique de perdre une partie.

Le lecteur n'alimente donc la fente que le temps d'une opération : une à deux secondes à
l'insertion, quelques dizaines de millisecondes par envoi du journal. Le reste du temps, la
cartouche est un objet inerte qu'on peut retirer sans risque ; sa pile garde la sauvegarde et
l'horloge, comme dans un tiroir. Seule exception, le vibreur (§3.3).

Le microrupteur doit s'ouvrir **avant** que les contacts ne quittent la fente : une cartouche
qu'on commence à retirer coupe la fente avant que ses contacts ne frottent (T1.6).

### 4.3 La lecture, à l'insertion

1. Le microrupteur se ferme. Le lecteur attend 300 ms que la cartouche soit enfoncée, puis
   alimente la fente.
2. **L'en-tête**, `0100`–`014F` : le logo Nintendo, le titre, le type de cartouche, la taille
   de la ROM et de la RAM, la somme de contrôle de l'en-tête. Logo faux ou somme fausse : les
   contacts sont sales. Le lecteur recommence trois fois, puis allume sa LED en orange.
3. **La ROM entière**, banque par banque, en pilotant le MBC comme le ferait la console. La
   somme de contrôle globale de l'en-tête vérifie la lecture.
4. **La RAM de sauvegarde**, et l'horloge s'il y en a une.
5. **La sauvegarde parquée**, s'il y en a une : écrite si c'est la même cartouche, inchangée,
   oubliée sinon (§3.4).
6. La fente est coupée. La copie est prête à partir.

Un octet par microseconde environ : une seconde par Mo. Pokémon Rouge, 1 Mo de ROM et 32 Ko
de sauvegarde, se lit en une seconde (T1.3 le mesure).

Un jeu Game Boy Color seulement se copie comme un autre : c'est le jeu lui-même qui dit, à
l'écran, qu'il ne marche pas sur cette console, comme dans une Pocket d'origine.

---

## 5. La doublure

### 5.1 Pourquoi un RP2350B

Vingt-neuf signaux, plus trois broches qui commandent les 74LVC8T245 (§5.4) : le Pico 2 W, avec
ses 26 broches libres, n'y suffit pas. La puce RP2350B, la grande sœur du RP2350A du Pico 2, a 48 broches.
**La Waveshare RP2350B-Plus-W** la monte au format exact du Pico 2 W, avec le même module WiFi :
41 broches sorties (26 sur les bords, 15 en pastilles dessous), 16 Mo de flash, une dizaine
d'euros. Le SDK, le WiFi et les outils du projet restent les mêmes.

La Pimoroni Pico Plus 2 W ajoute 8 Mo de PSRAM, mais ses broches sorties sont moins
nombreuses : elle reste un plan B si la flash ne suit pas (§5.3).

### 5.2 Servir le bus

Le RP2350 est trop lent pour répondre au bus par programme, mais ses **PIO** sont faits pour
ça : de petites machines qui suivent des broches au cycle près. La méthode est éprouvée par la
**Croco Cartridge**, une cartouche à RP2040 libre ([firmware](https://github.com/shilga/rp2040-gameboy-cartridge-firmware),
GPL-3.0) :

- un PIO guette chaque cycle et lit l'adresse ;
- une cascade de DMA va chercher l'octet dans la copie et le rend au PIO, qui le pose sur le
  bus, sans que le processeur intervienne ;
- les écritures dans `0000`–`7FFF` arrivent au processeur, qui recalcule la banque : c'est son
  seul travail en jeu ;
- les écritures dans `A000`–`BFFF` vont en RAM, et marquent leur page pour le journal (§3.1).

Le RP2350 a plus de PIO et de DMA que le RP2040, et un second cœur entier pour le WiFi. On
s'inspire de la Croco ; si l'on reprend du code, il garde sa licence GPL-3.0, compatible avec
l'AGPL du dépôt.

### 5.3 Où vit la copie

| Contenu | Taille | Où |
|---|---|---|
| La ROM | 32 Ko à 2 Mo pour les jeux Game Boy, jusqu'à 8 Mo en théorie | la **flash** de la carte, 16 Mo : de quoi garder plusieurs jeux |
| La sauvegarde | 0 à 32 Ko, 128 Ko au plus | la **RAM** : le jeu y écrit à toute vitesse |
| Le journal non remis, console éteinte | 128 Ko au plus | la flash (§3.5) |

La RAM du RP2350, 520 Ko, ne peut pas tenir Pokémon Rouge et son Mo. La ROM est donc servie
depuis la flash, comme le fait la Croco, sur un RP2040 poussé à 266 MHz.

⚠️ **Le risque principal du module** : chez nous, le WiFi tourne en même temps, et son code
vit aussi dans la flash. Si le WiFi lit la flash au moment où la console demande un octet, la
réponse peut arriver trop tard, et le jeu plante. Parades, dans l'ordre :

1. le code du WiFi et du bus en RAM, la flash réservée à la ROM ;
2. les banques les plus lues gardées en RAM ;
3. **plan B** : deux puces. Un RP2350 sans WiFi sert le bus depuis sa flash, comme la Croco ;
   un Pico 2 W à côté fait le WiFi, et lui passe la copie par SPI.

T3.5 tranche : une heure de jeu sous un trafic WiFi forcé.

Un indice, depuis : la Croco Cartridge, passée au RP2350B dans sa version 2, ne sert plus la
ROM depuis la flash mais depuis une HyperRAM lue par un PIO ([étude de la Croco](etude-croco.md)).
Personne n'a encore montré la flash d'un RP2350 assez rapide pour le bus ; la première parade
ci-dessus reste à éprouver.

Écrire 1 Mo de flash prend quelques secondes. La doublure reconnaît une cartouche déjà vue à sa
signature (titre, sommes de contrôle, CRC de la ROM entière) et ne réécrit pas la flash : seule
la sauvegarde voyage.

### 5.4 Niveaux et alimentation

Le bus de la console est en 5 V (le processeur de la Pocket est alimenté en 5 V régulés), le
RP2350 en 3,3 V. Comme au lecteur, quatre 74LVC8T245 : trois pour l'adresse et la commande,
qui entrent ; un pour les données, que le PIO ne retourne vers la console que pendant une
lecture de `0000`–`7FFF` ou de `A000`–`BFFF`.

**La doublure ne s'éteint jamais, la console si.** Console éteinte, les 74LVC8T245 isolent : la
doublure ne peut pas alimenter la console par ses broches. Elle surveille VCC (broche 1) et
ne sert le bus que quand la console est allumée.

**La doublure ne tire rien de la console** : elle est alimentée par l'alimentation permanente
de l'installation, pas par la broche 1. La Croco, alimentée par la console, prévient que son
RP2040 poussé consomme plus que la console ne sait fournir.

### 5.5 Le branchement

**Au banc**, une carte au format cartouche, épaisseur 0,8 mm et contacts dorés comme la Croco,
enfichée dans la fente de la console : **rien n'est soudé**, la console reste intacte.

**À la fin**, tu veux la doublure soudée sur la console. Deux façons : garder la carte au
format cartouche dans la fente, fixée, la fente fermée ; ou dessouder le connecteur et souder
la doublure à sa place. Cela se décide en phase 6, sur les mesures de place de T0.4.

### 5.6 Se taire comme une fente vide

Pour imiter l'original, la doublure n'a qu'un geste à faire : **se taire**. Les 74LVC8T245 des
données en haute impédance, la console lit ce qu'elle lit sans cartouche, des `FF` :

- **au démarrage sans cartouche**, le logo Nintendo lu est fait de `FF` : un rectangle noir,
  et la console reste bloquée, comme l'original ;
- **au retrait en pleine partie**, le jeu lit des `FF` au lieu de son code et se fige, comme
  l'original.

La doublure reste muette **jusqu'à ce que la console s'éteigne** : elle le voit à VCC
(broche 1). Remettre la cartouche ne suffit pas, comme sur l'original ; il faut redémarrer la
console, par l'interrupteur de la manette. Au démarrage suivant, la doublure sert la copie de
la cartouche présente, s'il y en a une.

Une cartouche dans la fente doit donner le jeu, pas un rectangle noir : si la copie est en
cours quand on bascule l'interrupteur, **l'injecteur du module
[manette](../../manette/docs/plan.md), qui alimente la console, attend le feu vert de la
doublure**, quelques secondes au plus (§9). Le feu vert passe par le bus filaire de la console
([document réseau](../../reseau.md), §5). Fente vide, la console s'allume aussitôt.

---

## 6. Les situations

| Situation | Ce qui se passe |
|---|---|
| Console éteinte, on insère une cartouche | le lecteur la lit et envoie la copie ; la doublure est prête |
| Interrupteur de la manette sur marche, copie prête | la console s'allume sur le jeu |
| Interrupteur sur marche, copie en cours | la console attend la copie, puis s'allume sur le jeu |
| Interrupteur sur marche, pas de cartouche | un rectangle noir à la place du logo, et rien de plus |
| Une cartouche arrive ensuite | rien ne change à l'écran ; sa copie se prépare. Il faut redémarrer la console pour jouer |
| Cartouche Game Boy Color seulement | le jeu dit lui-même qu'il ne marche pas sur cette console |
| Le jeu sauvegarde | le journal part ; la vraie cartouche a la partie en moins d'une seconde |
| La console s'éteint juste après une sauvegarde | rien n'est perdu : la doublure reste allumée et finit d'envoyer |
| Le WiFi tombe en pleine partie | la partie continue ; le journal attend et part au retour |
| La console s'éteint, WiFi toujours coupé | la doublure écrit le journal dans sa flash ; il part au retour du WiFi (§3.5) |
| **On retire la cartouche en pleine partie** | la console se fige. Ce qui restait du journal part au lecteur, qui le parque (§3.4) |
| On remet la même cartouche, inchangée | le lecteur y écrit la sauvegarde parquée, puis la copie se prépare. Rien ne change à l'écran : il faut redémarrer la console, et la partie sauvegardée est là |
| On insère une autre cartouche, ou la même modifiée ailleurs | la sauvegarde parquée est oubliée ; la copie de la nouvelle cartouche se prépare, avec sa sauvegarde à elle. Il faut redémarrer la console |
| La cartouche a été retirée pendant que le WiFi était coupé | au retour du WiFi, l'annonce du lecteur le dit : la console se fige à ce moment-là, et le journal en attente part au lecteur, qui le parque. Si une autre cartouche est arrivée entre-temps, il est oublié |

**Ce qui reste de l'écart avec l'original** : une sauvegarde est perdue seulement si une autre
cartouche passe dans la fente avant le retour de la sienne, ou si la doublure perd son
alimentation en pleine partie pendant que le WiFi est coupé (§3.5).

---

## 7. Le protocole `CAR1` — proposition

Mêmes conventions que `PXL1`, `SON1` et `MAN1` : champs en little-endian, nombre magique
`'C','A','R','1'` en tête. UDP, port **4245**. Chaque paquet porte un **numéro d'appairage** :
le lecteur et la doublure ignorent tout paquet qui n'a pas le leur.

| Type | De → vers | Contenu |
|---|---|---|
| **Annonce** | lecteur → doublure, 4 fois par seconde, et aussitôt à chaque insertion ou retrait | cartouche présente ou non ; sa signature : titre, type, tailles de ROM et de RAM, sommes de contrôle, CRC de la ROM et de la sauvegarde, horloge lue ; numéro d'insertion |
| **Demande** | doublure → lecteur | une liste de blocs de 1 Ko, de ROM ou de sauvegarde |
| **Bloc** | lecteur → doublure | quoi, où, 1 Ko de données |
| **Écriture** | doublure → lecteur | numéro, puis des pages de sauvegarde, ou un réglage d'horloge, ou l'état du vibreur |
| **Accusé** | lecteur → doublure | le numéro de la dernière écriture appliquée, ou parquée |
| **État** | doublure → lecteur, 4 fois par seconde | copie prête ou non, console allumée ou non, écritures en attente |

**La copie, demandée par la doublure.** C'est la doublure qui sait ce qui lui manque : elle
demande les blocs par fenêtres de 32, redemande ceux qui ne sont pas venus. Un paquet perdu
coûte un aller-retour, jamais une copie fausse : la doublure vérifie le CRC de la ROM entière
avant de se dire prête.

**Le journal, numéroté et acquitté.** Le lecteur applique les écritures dans l'ordre et ignore
celles qu'il a déjà appliquées ; la doublure renvoie tant qu'elle n'a pas son accusé.

**Le numéro d'insertion** change à chaque insertion ; tiré au hasard au démarrage du lecteur,
puis incrémenté, il ne resservira pas après un redémarrage. Une annonce qui porte un autre numéro que
celui de la copie servie, ou qui dit la fente vide, fige la console (§5.6). Une écriture
destinée à une insertion précédente n'est jamais appliquée directement : le lecteur la parque
si aucune autre cartouche n'est passée depuis, l'oublie sinon (§3.4).

Le format octet par octet est proposé dans [`firmware/commun/car1.h`](../firmware/commun/car1.h),
qui fera foi.

---

## 8. Le réseau

### 8.1 Ce qui circule

| De | Vers | Quoi | Volume |
|---|---|---|---|
| lecteur | doublure | la copie, à l'insertion | 1 Mo en une seconde environ. Le module écran a mesuré 2,2 Mo/s sur une seule antenne |
| lecteur | doublure | annonces | négligeable |
| doublure | lecteur | journal, états | quelques Ko par sauvegarde |

**C'est le module le moins exigeant du projet.** Rien n'y est pressé, sauf le vibreur : le
reste supporte des secondes de retard.

### 8.2 Le réseau de la cartouche

Décision du 11/10/2026, pour tout le projet ([document réseau](../../reseau.md)) : sur la
console, un microcontrôleur par type de périphérique, chacun point d'accès de son propre
réseau. **La doublure émet le réseau de la cartouche**, `gb-cartouche` :

- **un seul saut radio** : le lecteur est client direct de la doublure ;
- **des adresses fixes** : la doublure en 192.168.7.1, le lecteur en .2 ;
- le canal proposé est le 6, partagé avec le réseau des manettes, peu chargé ;
- le réseau accepterait jusqu'à 4 lecteurs. Ce que ferait la console avec plusieurs
  cartouches se décidera plus tard ; ce plan n'en prévoit qu'un.

Ce réseau ne lève pas le risque principal du module : la doublure a une radio, et le WiFi lit
la même flash que la ROM (§5.3, T3.5). Le plan B à deux puces reste possible.

### 8.3 Au banc

N'importe quel réseau WiFi, comme pour la manette : nom et mot de passe dans `secrets.h`,
adresse de l'autre moitié et numéro d'appairage dans le réglage du firmware.

---

## 9. Les durées

| Étape | Durée | Mesurée par |
|---|---|---|
| Lire la cartouche | environ 1 s par Mo de ROM | T1.3 |
| Envoyer la copie | environ 1 s par Mo | T4.2 |
| L'écrire en flash, si la cartouche est nouvelle | quelques secondes par Mo | T3.3 |
| **Insertion → copie prête** | **2 à 3 s pour une cartouche déjà vue, une dizaine pour une nouvelle**, estimation | T4.4 |
| Une sauvegarde arrive dans la cartouche | moins d'une seconde | T4.3 |
| Un ordre au vibreur | quelques ms | T4.5 |

Le démarrage de la console, logo Nintendo compris, ajoute ses trois secondes habituelles.

---

## 10. Arborescence prévue

```
cartouche/
├── README.md
├── docs/
│   ├── plan.md                    ← ce document
│   ├── etapes.md                  ← le pas à pas, ses tests ; la fiche de la phase 0
│   ├── fenetre-du-bus.md          ← ce que disent les sources du cycle de lecture ✅
│   ├── brochage.md                ← la doublure et le lecteur sur la Waveshare ✅
│   ├── etude-croco.md             ← ce qu'on reprend de la Croco Cartridge ✅
│   └── releves/                   ← captures de l'analyseur, photos
├── firmware/
│   ├── commun/                    ← testé sur PC, sans carte ✅
│   │   ├── car1.h                 ← le protocole, qui fait foi
│   │   ├── entete.hpp, mbc.hpp    ← l'en-tête ; les contrôleurs : lus par le lecteur, imités par la doublure
│   │   ├── imitation.hpp          ← la cartouche imitée
│   │   ├── lecteur.hpp, doublure.hpp ← les deux automates de la sauvegarde
│   │   └── test/                  ← tests unitaires et simulation de bout en bout
│   ├── lecteur/                   ← la fente : présence, lecture, journal appliqué
│   └── doublure/                  ← la console : PIO et DMA du bus, copie en flash, journal
├── materiel/
│   ├── lecteur/                   ← KiCad : la carte de la fente
│   └── doublure/                  ← KiCad : la carte au format cartouche
└── tools/
    ├── cartouche_pc.py            ← depuis un PC : copier une ROM, sauvegarder et restaurer une partie, envoyer une ROM à la doublure
    ├── bus_marge.py               ← le verdict de T0.2 et T3.4 : la fenêtre du bus, la marge de la doublure
    └── console.py, flash.sh       ← copiés du module son
```

Seuls les éléments marqués ✅ sont écrits, sans matériel : le reste naîtra avec la phase qui
en a besoin.

---

## 11. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| C1 | Waveshare RP2350B-Plus-W | 3 | le lecteur, la doublure, une de rechange |
| C2 | 74LVC8T245, en TSSOP-24, et adaptateurs vers DIP | 8 + 8 | quatre par moitié |
| C3 | Connecteur de cartouche Game Boy 32 broches, de rechange | 2 | la fente du lecteur ; un pour le banc de la doublure |
| C4 | Plaquette « GB cartridge breakout » : 32 contacts au format cartouche, sortis sur broches | 2 | la doublure au banc, avant la carte maison |
| C5 | Interrupteur de charge 5 V, 1 A, avec broche d'activation | 2 | la fente du lecteur ; modèle à choisir en phase 1 |
| C6 | Microrupteur à levier, petit | 3 | la présence de la cartouche ; T1.6 choisit sa position |
| C7 | LED RGB et 3 résistances | 1 | le témoin du lecteur |
| C8 | Résistances, condensateurs de découplage 100 nF, pont diviseur pour VCC | 1 lot | |
| C9 | Une cartouche sans valeur, avec RAM et pile, MBC1 ou MBC5 | 1 | les essais d'écriture : on peut perdre sa sauvegarde |
| C10 | Cartes maison : lecteur, et doublure au format cartouche en 0,8 mm, contacts dorés (ENIG) | 5 + 5 | phase 5 |
| C11 | Plaque d'essai, fils de liaison | | |
| T1 | Analyseur logique | 1 | celui du module capture, 8 voies à 24 MHz |
| T2 | Multimètre, alimentation de laboratoire | 1 | déjà là |

Les cartouches de jeu sont les tiennes. **Avant tout essai, leurs sauvegardes sont copiées sur
le PC** (phase 1).

---

## 12. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| Perdre la sauvegarde d'une vraie cartouche | 🔴 | aucune écriture avant que la sauvegarde soit copiée et vérifiée sur le PC ; les essais d'écriture sur une cartouche sans valeur ; la fente hors tension au repos (§4.2) |
| La doublure répond trop tard quand le WiFi lit la flash : le jeu plante | 🔴 en jeu | code en RAM ; banques en RAM ; plan B à deux puces (§5.3) ; T3.5 |
| La fenêtre du bus plus étroite que prévu | 🟡 | T0.2 la mesure avant toute commande ; T3.4 mesure la marge réelle |
| Une sauvegarde perdue : cartouche retirée moins d'une seconde après | 🟢 | la sauvegarde parquée (§3.4) ; T4.7 |
| Le journal perdu par un redémarrage de la doublure, WiFi coupé | 🟢 | écrit en flash dès que la console s'éteint (§3.5) ; T4.8 |
| Une sauvegarde parquée écrite sur la mauvaise cartouche | 🔴 | ROM identique et sauvegarde identique hors des pages parquées ; une seule sauvegarde parquée, oubliée dès qu'une autre cartouche passe (§3.4) ; T4.7 |
| Une cartouche retirée pendant une écriture | 🟡 | microrupteur qui s'ouvre avant les contacts ; écritures groupées et courtes (§3.1) ; T1.6 |
| Les deux horloges dérivent | 🟢 | la cartouche fait foi à chaque insertion (§3.2) |
| Un jeu qui écrit sa RAM sans arrêt | 🟢 | le journal part au plus une fois par seconde ; T4.3 |
| Un contrôleur de banques mal imité | 🟡 | le lecteur relit la doublure comme une cartouche et compare à la copie (T2.4) ; un jeu par type de MBC (T3.6) |
| La doublure alimente la console éteinte | 🟡 | 74LVC8T245 qui isolent ; la doublure se tait sans VCC (§5.4) ; T3.1 |
| Des contacts sales donnent une copie fausse | 🟢 | logo, sommes de contrôle, CRC (§4.3) ; LED orange |
| Les quatre radios de la console se gênent | 🟡 | le banc de coexistence R1 du [document réseau](../../reseau.md), avant tout changement |

---

## 13. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 11/10/2026 | Module ouvert : une fente de cartouche séparée de la console qui joue | Le dernier module de la Game Boy Pocket déstructurée |
| 11/10/2026 | **Décision de Yoann** : la cartouche est lue en entier à l'insertion ; un microcontrôleur soudé sur la console en garde la copie et tient le rôle de la cartouche ; ce qui change l'état de la vraie cartouche lui est transmis à côté | Le bus répond en nanosecondes, le WiFi en millisecondes |
| 11/10/2026 | Seules la sauvegarde, l'horloge et le vibreur remontent à la cartouche | Le reste ne survit pas à la coupure de la cartouche, ou ne change jamais |
| 11/10/2026 | RP2350B au format Pico 2 W, des deux côtés | 30 signaux de fente ; 26 broches libres sur un Pico 2 W |
| 11/10/2026 | ROM en flash, sauvegarde en RAM, à la manière de la Croco Cartridge | 520 Ko de RAM ne tiennent pas un jeu de 1 Mo ; la Croco sert la ROM depuis sa flash |
| 11/10/2026 | La fente du lecteur hors tension au repos | Une cartouche retirée sous tension peut perdre sa sauvegarde |
| 11/10/2026 | **Décision de Yoann** : la cartouche fait foi ; à chaque insertion, sa sauvegarde remplace toujours celle de la doublure | Remplace la proposition de garder, rangées par cartouche, les sauvegardes d'une cartouche retirée |
| 11/10/2026 | **Décision de Yoann** : retirer la cartouche en pleine partie fige la console, qu'il faut redémarrer | Le comportement d'origine. Remplace la proposition de continuer la partie sur la copie |
| 11/10/2026 | **Décision de Yoann** : sans cartouche, un rectangle noir à la place du logo, rien de plus | Le comportement d'origine. Remplace la proposition d'un programme « insère une cartouche » ; la doublure n'a qu'à se taire |
| 11/10/2026 | **Décision de Yoann** : une sauvegarde pas encore écrite quand on arrache la cartouche est parquée dans le lecteur ; écrite si la même cartouche revient inchangée, oubliée si une autre arrive | Rattraper la seconde de retard du journal, sans jamais écrire sur la mauvaise cartouche |
| 11/10/2026 | **Décision de Yoann** : un journal non remis est écrit dans la flash de la doublure quand la console s'éteint | Ne pas dépendre du WiFi ni de l'alimentation de la doublure pour garder une partie. Pas avant : écrire la flash, console allumée, ferait planter le jeu |
| 11/10/2026 | **Décision de Yoann** : la doublure émet le réseau de la cartouche, le lecteur s'y connecte en direct | La topologie de tout le projet : un microcontrôleur par type de périphérique sur la console ([réseau](../../reseau.md)) |
| 11/10/2026 | Proposition : le format octet par octet de `CAR1`, dans `firmware/commun/car1.h` ; écritures numérotées par session de la doublure, avec le plus vieux numéro non accusé dans chaque paquet | Un compteur unique faisait prendre, après un redémarrage de la doublure, une écriture neuve pour un doublon ([simulation](../firmware/commun/README.md)) |
| 11/10/2026 | Proposition, **à trancher par Yoann** : la règle stricte de comparaison à la réinsertion. Les pages parquées sont comparées aussi : intactes si aucune écriture ne les a touchées, « début du contenu tenté sur la fin de l'ancien » si l'écriture a été interrompue | La règle du plan (§3.4) laisse écrire la sauvegarde parquée sur une cartouche jouée ailleurs dont le jeu n'a réécrit que les pages parquées, ou sur un autre exemplaire du même jeu quand les pages parquées couvrent toute la sauvegarde ([simulation](../firmware/commun/README.md)) |
| 11/10/2026 | Proposition : le lecteur annonce la cartouche dès que le microrupteur se ferme, avant de la lire | Sinon, une console allumée dans les 300 ms qui suivent l'insertion démarre sur un rectangle noir |
| 11/10/2026 | Proposition : la doublure ne prépare une nouvelle copie qu'une fois son journal vide ; le lecteur garde en flash son contexte de sauvegarde (CRC des pages, pages parquées et en cours d'écriture, sessions) | La copie reflète toutes les écritures d'avant ; un redémarrage du lecteur ne perd rien |
| 11/10/2026 | Proposition : la doublure sur la Waveshare dans la fenêtre basse des GPIO, A0–A15 sur 0 à 15, D0–D7 sur 24 à 31 ([brochage](brochage.md)) | Le RM2 occupe 36 à 39 : la fenêtre haute, celle de la Croco V2, n'a pas assez de broches libres |
