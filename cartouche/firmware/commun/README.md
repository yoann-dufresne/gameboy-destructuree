# Le cœur logique de la cartouche, éprouvé sur PC

Ce dossier contient ce que le lecteur et la doublure partagent, et ce qui se vérifie sans
carte : l'en-tête des cartouches, les contrôleurs de banques, le protocole `CAR1`, et les
deux automates qui tiennent les règles de la sauvegarde ([plan](../../docs/plan.md) §3, §6,
§7). C'est du C++20 portable, sans pico-sdk : les firmwares l'incluront tel quel, en lui
branchant la fente, le réseau et la flash.

Rien de ce dossier n'a encore tourné sur une carte.

## Les fichiers

| Fichier | Contenu |
|---|---|
| `entete.hpp` | l'en-tête : logo, sommes de contrôle, type, tailles ; le CRC-32 ; la signature qui reconnaît une cartouche |
| `mbc.hpp` | les contrôleurs : sans MBC, MBC1 et MBC1M, MBC2, MBC3 avec horloge et MBC30, MBC5 avec vibreur. Dans un sens, ce que la doublure imite ; dans l'autre, les écritures qui amènent une vraie cartouche dans l'état voulu par le lecteur |
| `imitation.hpp` | la cartouche imitée par la doublure : lectures, écritures, pages marquées pour le journal |
| `car1.h` | le protocole `CAR1`, octet par octet : une proposition |
| `paquet.hpp` | emballer et déballer un paquet |
| `lecteur.hpp` | l'automate du lecteur : lecture, copie, journal appliqué, sauvegarde parquée, mémoire en flash |
| `doublure.hpp` | l'automate de la doublure : copie, service de la console, journal, silence, feu vert |
| `test/test_mbc.cpp` | les contrôleurs et l'en-tête, cas par cas, et l'aller-retour lecteur → cartouche pour chaque banque |
| `test/test_car1.cpp` | les paquets abîmés ou étrangers |
| `test/simulation.cpp` | la sauvegarde de bout en bout, sous les pannes |

## Tester sur PC

```sh
cd test
g++ -std=c++20 -O2 -Wall -Wextra -I.. test_mbc.cpp -o /tmp/test_mbc && /tmp/test_mbc
g++ -std=c++20 -O2 -Wall -Wextra -I.. test_car1.cpp -o /tmp/test_car1 && /tmp/test_car1
g++ -std=c++20 -O2 -Wall -Wextra -I.. simulation.cpp -o /tmp/simulation && /tmp/simulation
```

`/tmp/simulation --tirages 10000` multiplie les tirages aléatoires ; `--verbeux` raconte
chaque scénario écrit ; `--graine N` rejoue un tirage avec son récit, `--plan` à la règle du
plan.

**La simulation** fait vivre des cartouches (ROM, RAM sur pile, contrôleur), la fente, le
lecteur et la doublure tels qu'ils tourneront, un réseau qui perd, double et retarde, une
console qu'on allume et éteint, et un jeu qui sauvegarde. Le temps avance d'une milliseconde
à la fois. Les gestes :
- insérer, retirer, arracher au milieu d'une opération du lecteur ;
- allumer, éteindre, redémarrer ;
- couper le WiFi ;
- jouer une cartouche ailleurs ;
- redémarrer le lecteur, priver la doublure de courant.

Elle vérifie :
- **R1, la cartouche fait foi** : une copie prête porte la sauvegarde de la cartouche ;
- **R2, jamais d'écriture sur la mauvaise cartouche** : une page n'est écrite que dans la
  cartouche d'où vient sa copie, et pas si celle-ci a été jouée ailleurs depuis ;
- **R3, rien de perdu** hors des cas que le plan admet ;
- **R4, rien de bloqué** : le journal finit par se vider.

**Résultats au 11/10/2026** : `test_mbc`, 1 364 vérifications justes ; `test_car1`, 11 ;
la simulation, 14 scénarios écrits et 10 000 tirages aléatoires par règle (chiffres
ci-dessous).

## Ce que la simulation a montré

### Un trou dans la règle du plan — à trancher par Yoann

Le plan §3.4 écrit la sauvegarde parquée si la cartouche revient avec « même ROM, même
sauvegarde **hors des pages parquées** ». Scénario qui la met en défaut :
1. le jeu sauvegarde ;
2. on arrache la cartouche dans la seconde : la sauvegarde est parquée ;
3. on joue la cartouche sur une autre console, et ce jeu réécrit son emplacement de
   sauvegarde, c'est-à-dire justement les pages parquées ;
4. on la remet. Hors des pages parquées, rien n'a changé : la règle accepte, et la sauvegarde
   parquée **écrase la partie jouée ailleurs**.

Cas extrême : quand les pages parquées couvrent toute la sauvegarde, la règle ne compare plus
rien. C'est le cas d'un MBC2, dont les 512 demi-octets tiennent en deux pages, ou d'un jeu
qui réécrit toute sa sauvegarde. Elle accepte alors n'importe quel exemplaire du même jeu.

Sur 10 000 tirages aléatoires, **la règle du plan échoue dans 88** : des pages écrites dans
une cartouche jouée ailleurs, ou dans un autre exemplaire du même jeu, qui y perd sa propre
partie. **La règle stricte, dans aucun.** Les 14 scénarios écrits passent aux deux règles :
celui du trou, à la règle du plan, réussit en le montrant.

**La règle stricte proposée** (`Regle::Stricte` dans `lecteur.hpp`) compare aussi les pages
parquées :
- une page parquée qu'aucune écriture n'a touchée doit être restée telle que le lecteur la
  savait ;
- une page dont l'écriture a été interrompue doit porter **un début du contenu tenté sur la
  fin de l'ancien contenu**. Le lecteur écrit une page du premier octet au dernier et
  reprend toujours du début : c'est la seule trace qu'une interruption peut laisser.

Il lui reste une faille : une modification faite ailleurs qui épouserait exactement cette
forme, par exemple un seul octet changé en tête d'une page interrompue, et changé justement
vers la valeur tentée. Une vraie sauvegarde change des centaines d'octets et sa somme de
contrôle : sans portée pratique.

### Corrigé dans le code, sans toucher aux règles

- **Le lecteur annonce la cartouche dès que le microrupteur se ferme**, avant les 300 ms
  d'attente et la lecture. Sinon, une console allumée dans cet intervalle démarrait sur un
  rectangle noir, cartouche pourtant dans la fente.
- **Les écritures sont numérotées par session de la doublure**, avec le plus vieux numéro
  non accusé dans chaque paquet (`base`). Avec un compteur unique, un redémarrage de la
  doublure, ou un lecteur qui ne connaît pas encore la session, faisait prendre une écriture
  neuve pour un doublon.
- **La doublure ne prépare une nouvelle copie qu'une fois son journal vide.** Le lecteur a
  ainsi appliqué, parqué ou oublié toutes les écritures d'avant quand il donne la sauvegarde
  de la cartouche suivante : la copie la reflète.
- **Le lecteur garde son contexte en flash** : la signature de la cartouche, le CRC de
  chaque page, les pages parquées et celles en cours d'écriture, la dernière écriture
  traitée de chaque session. C'est ce qui permet d'encaisser un redémarrage du lecteur, même
  au milieu d'une écriture.
- **Une écriture qui échoue, cartouche toujours là** (un contact qui lâche), fait relire la
  cartouche au lieu de bloquer le lecteur.

### Les limites connues

- **Un redémarrage du lecteur fige la console**, même cartouche en place : la nouvelle
  lecture porte un nouveau numéro d'insertion (plan §7). Rien n'est perdu. Pour l'éviter,
  l'annonce pourrait dire « cette insertion continue la précédente » ; mais alors un
  retrait-réinsertion rapide dont l'annonce « fente vide » se serait perdue passerait
  inaperçu. À trancher.
- **Deux exemplaires du même jeu, aux sauvegardes identiques, sont indiscernables** : la
  sauvegarde parquée de l'un va dans l'autre. Sans dommage, puisque l'autre portait
  exactement la même partie.
- **Le lecteur garde la copie de la ROM en mémoire**, dans l'automate. Les 520 Kio du RP2350
  ne tiennent pas une ROM de 1 Mio. Le firmware devra la ranger en flash, au prix de quelques
  secondes, ou relire les blocs dans la cartouche à la demande, la fente restant alimentée le
  temps de la copie. À choisir en phase 1.
- **MBC1M** : la doublure l'imite (reconnu au second logo de la banque 10), mais le lecteur
  lit en câblage MBC1 ordinaire et ferait une copie fausse. Hors périmètre tant qu'aucune de
  ces compilations n'est à éprouver.
- **L'horloge du MBC3** : les valeurs hors plage suivent le modèle des émulateurs, pas une
  mesure sur cartouche.
- **Les conteneurs** de la bibliothèque standard (`vector`, `deque`) allouent sur le tas. Sur
  le Pico, cela se tolère à l'initialisation ; à revoir si cela gêne.
