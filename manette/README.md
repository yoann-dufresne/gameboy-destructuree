# Manette — une Game Boy Pocket devenue manette WiFi

Ce module transforme une seconde Game Boy Pocket en manette sans fil. Elle ne joue plus :
plus de processeur, plus d'écran allumé. Elle garde ce qu'on touche : la croix, les boutons, les
deux molettes et l'interrupteur. Ses deux piles AAA alimentent un Pico 2 W, qui envoie son état
en WiFi à une vraie console, celle qui joue. Là, un injecteur appuie sur ses boutons à la place
du joueur, et l'allume ou l'éteint quand on bascule l'interrupteur de la manette.

Les deux moitiés s'éprouvent séparément. Un PC lit la manette ; un PC commande l'injecteur, qui
fait jouer n'importe quelle Game Boy Pocket.

Dans la [Game Boy Pocket déstructurée](../README.md), la console principale est celle que
[capture](../capture/README.md) espionne et dont [son](../son/README.md) prend le son : la
manette en devient les mains.

## Principe

```
 Game Boy Pocket distante                                       Game Boy Pocket principale
 boutons, molettes ──► [Pico 2 W · manette] ── MAN1 / UDP ──► [Pico 2 W · injecteur] ── 8 photoMOS ──► ses boutons
 2 piles AAA ── interrupteur                ◄── accusés ─────                        ── 3,0 V ──────► sa prise DC
```

L'injecteur ne parle pas au processeur de la console : il double ses boutons. Chaque bouton
reçoit, en parallèle, un **photoMOS** : un interrupteur commandé par une LED, sans aucun lien
électrique entre la commande et le contact. La console lit l'injecteur comme elle lit ses
boutons, quand elle veut ; éteint, il n'appuie sur rien. Six fils sont soudés dans la console.

Chaque paquet porte l'état complet de la manette. Sans nouvelles pendant 200 ms, l'injecteur
relâche tout : un bouton ne reste jamais coincé. La console reste allumée pour autant : un
message par-dessus l'image du jeu dit qu'aucune manette n'est connectée, et la partie reprend
si elle revient. Sans retour, la console s'éteint au bout de 15 minutes. Interrupteur sur
arrêt, la manette ne transmet rien.

Posée, la manette dort : seul un accéléromètre veille, et elle ne consomme presque rien. Prise
en main, elle se réveille et rejoint la console avant qu'on bascule l'interrupteur : la console
démarre alors aussitôt. Interrupteur sur arrêt et immobile depuis une minute, elle se rendort.
Sa LED, une LED RGB à la place de l'originale, est rouge quand elle est en marche et appairée à
la console, bleue et pulsée tant que la console ne répond pas, éteinte sur arrêt.

La manette ne parle qu'à la console. C'est la console qui transmet la marche, l'arrêt et les
molettes à l'écran et au son ; la topologie du réseau n'est pas encore décidée.

## Matériel

| Rôle | Pièce |
|---|---|
| Manette | une Game Boy Pocket, sa carte remplacée par une carte maison qui porte un Pico 2 W ; les molettes, l'interrupteur et les contacts de piles d'origine ou de rechange |
| Injecteur | un Pico 2 W ; 8 photoMOS de faible capacité (AQY221N2S) et 8 diodes ; un connecteur JST-SH 6 points dans la console principale ; un régulateur 3,0 V commandé, vers la prise DC de la console |
| Mesure | un multimètre, l'analyseur logique du module capture |

Nomenclature complète : [§11 du plan](docs/plan.md).

## Contenu

| Document | Contenu |
|---|---|
| [`docs/plan.md`](docs/plan.md) | l'analyse et les décisions : la lecture des boutons par la console, l'injection, la marche et l'arrêt, la carte maison, le protocole `MAN1`, le réseau, la latence, l'autonomie, les risques |
| [`docs/etapes.md`](docs/etapes.md) | les phases pas à pas, leurs tests et leurs critères de sortie ; la fiche de la phase 0, à remplir |

## État

Rien n'est construit. Le module s'ouvre le 11/10/2026.

| Phase | État |
|---|---|
| 0 · Relevés sur les deux consoles, sans rien souder | à venir |
| 1 · La manette sur plaque d'essai | à venir |
| 2 · La manette parle au PC | à venir |
| 3 · L'injecteur sur une fausse console | à venir |
| 4 · L'injecteur dans la console principale | à venir |
| 5 · La carte maison | à venir |
| 6 · Intégration | à venir |
