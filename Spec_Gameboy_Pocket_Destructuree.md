# Game Boy Pocket déstructurée — l'intention

*Projet artistique : une Game Boy Pocket éclatée en objets indépendants qui se parlent en WiFi.*
Version 2 — 11/10/2026 : une vraie console au centre, cinq modules, un réseau par type de
périphérique. La version 1, du 23/07/2026, plaçait un émulateur dans la cartouche ; ce qui a
changé depuis est au §8, et la v1 reste dans l'historique du dépôt.

Ce document dit **ce qu'on veut et pourquoi**. Le comment est dans chaque module, qui fait foi
pour ce qui le concerne ; le réseau commun est dans [`reseau.md`](reseau.md).

---

## 0. Les décisions qui structurent tout

| Sujet | Décision |
|---|---|
| **Le cerveau** | une **vraie Game Boy Pocket**, qui exécute le vrai jeu sur la vraie cartouche. Pas d'émulateur |
| **Les pièces** | les vraies pièces de Game Boy là où on les touche et les entend : boutons, molettes, interrupteur, cartouches, haut-parleur. Un écran moderne, en dalles LED |
| **L'objectif** | **réellement jouable** : la console répond aux boutons comme une Game Boy |
| **Éclater sans trahir** | chaque morceau sort de la console par le moyen le plus fidèle possible : on espionne l'image, on prend le son, on double les boutons, on imite la cartouche. La console ne sait pas qu'elle est éclatée |
| **Chaque module est un projet** | il a son matériel, son firmware, sa documentation, et il sert à quelque chose hors de la Game Boy |
| **Qui parle à qui** | les périphériques ne parlent qu'à la console ; la console parle à chacun |
| **Le réseau** | des liaisons directes. Sur la console, un microcontrôleur par type de périphérique, chacun point d'accès de son propre réseau : jusqu'à 4 écrans, 4 haut-parleurs, 4 manettes |
| **Marche et arrêt** | l'interrupteur de la manette allume et éteint la console, qui allume et éteint le reste |

---

## 1. Le principe : couper là où le temps le permet

Une Game Boy n'est pas un ensemble de modules qui communiquent. C'est un système synchrone
autour d'un processeur à 4 MHz et d'un bus mémoire : la cartouche répond en **nanosecondes**,
l'écran est piloté au cycle près. Le WiFi met des **millisecondes**, et pas toujours les mêmes.
On ne peut donc pas éclater les puces de la console sur le réseau.

La v1 en concluait qu'il fallait un émulateur, autour duquel on distribuerait les
périphériques. La v2 garde la console entière, et la coupe **seulement aux endroits où ce qui
passe change assez lentement pour le WiFi** :

| Ce qui sort | Où on le prend | Échelle de temps de ce qui passe | Module |
|---|---|---|---|
| **L'image** | sur les fils entre le processeur et l'écran, en écoute | une image toutes les 16,7 ms | [capture](capture/) → [ecran](ecran/) |
| **Le son** | à la sortie du processeur, avant le bouton de volume, en écoute | un flux continu ; quelques dizaines de ms de tampon ne se remarquent pas | [son](son/) |
| **Les boutons** | en parallèle des boutons de la console | le jeu lit ses boutons une fois par image | [manette](manette/) |
| **La cartouche** | sur la fente de la console | la nanoseconde : **le seul endroit où le réseau est trop lent**. On y met une copie de la cartouche, et le WiFi ne porte que la copie, à l'insertion, et les sauvegardes | [cartouche](cartouche/) |

Partout, la console reste une vraie console, intacte dans son fonctionnement : ses propres
boutons marchent encore, son écran d'origine aussi.

---

## 2. Vue d'ensemble

```
                              GAME BOY POCKET PRINCIPALE
                              (exécute le vrai jeu)
   ┌──────────────────────────────────────────────────────────────────────┐
   │   bus LCD          sortie son        boutons, prise DC   fente        │
   │      │                 │                  │               │           │
   │   sniffer          source de son      injecteur        doublure      │
   │  (capture)            (son)           (manette)       (cartouche)    │
   │      └─────────────────┴──── bus I²C ─────┴───────────────┘           │
   └──────┼─────────────────┼──────────────────┼───────────────┼───────────┘
          │ réseau image    │ réseau son       │ réseau        │ réseau
          │                 │                  │ manettes      │ cartouche
          ▼                 ▼                  ▲               ▲
    1 à 4 écrans LED   1 à 4 haut-parleurs  1 à 4 manettes    le lecteur,
    192 × 192          avec HP de Game Boy  (des Game Boy     sa fente et
                                             Pocket vidées)   la vraie cartouche
```

---

## 3. Les modules

| Module | Côté console | Ailleurs | Seul, il sert à… |
|---|---|---|---|
| [capture](capture/) | le **sniffer** : un Pico 2 W soudé dans la console lit l'image sur le bus LCD | — | diffuser l'image de n'importe quelle Game Boy Pocket |
| [ecran](ecran/) | — | un **afficheur LED** de 3 × 3 dalles, 192 × 192 pixels : l'image 160 × 144 à l'échelle 1:1 | afficher toute image qu'on lui envoie |
| [son](son/) | la **source** : un Pico 2 W lit le son avant le bouton de volume | un **haut-parleur réseau**, avec le haut-parleur d'une Game Boy | jouer tout flux qu'on lui envoie ; diffuser le son d'une Pocket |
| [manette](manette/) | l'**injecteur** : des photoMOS appuient sur les boutons, un régulateur alimente la console par sa prise DC | une **seconde Game Boy Pocket** devenue manette : boutons, molettes, interrupteur, sur piles | piloter n'importe quelle Game Boy Pocket, ou un émulateur |
| [cartouche](cartouche/) | la **doublure** : elle tient le rôle de la cartouche sur le bus, depuis une copie | le **lecteur** : une fente de cartouche à part, qui lit la cartouche à l'insertion et reçoit ses sauvegardes | lire et sauvegarder ses cartouches ; jouer n'importe quelle ROM sur n'importe quelle Game Boy |

L'état de chaque module est dans le [README racine](README.md) et dans le sien.

---

## 4. Le réseau, en bref

Le détail est dans [`reseau.md`](reseau.md). L'essentiel :

- **un seul saut radio**, toujours. Deux sauts ont été mesurés : l'image gelait ;
- **un réseau par type de périphérique**, émis par son microcontrôleur dans la console. Un
  Pico en point d'accès n'accepte que 4 clients : chaque type a ses 4 places ;
- **UDP, jamais TCP** : une image en retard est une image inutile. Chaque flux gère ses
  numéros, ses accusés et ses pertes ;
- dans la console, les microcontrôleurs se parlent par un **bus filaire**, l'injecteur en
  maître.

| Protocole | Port | Flux |
|---|---|---|
| `PXL1`, `PXL2` | 4242 | l'image, vers les écrans |
| `SON1` | 4243 | le son, vers les haut-parleurs |
| `MAN1` | 4244 | les manettes, vers la console |
| `CAR1` | 4245 | la copie de la cartouche, et les sauvegardes qui lui reviennent |

---

## 5. Allumer, éteindre, alimenter

- **L'interrupteur de la manette** allume et éteint la console : l'injecteur établit ou coupe
  son alimentation. La console prévient le reste par le bus filaire, et le sniffer et la
  source de son préviennent les écrans et les haut-parleurs.
- **Les microcontrôleurs de la console, des écrans, des haut-parleurs et du lecteur ne
  s'éteignent jamais.** Seule la manette, sur piles, dort quand on la pose.
- L'installation vit donc **sur secteur**. Les dalles LED, à elles seules, demandent une
  alimentation de plusieurs ampères. Seule la manette est autonome.

---

## 6. Jouable : les retards

| Chemin | Retard | D'où vient le chiffre |
|---|---|---|
| Un pixel, de la console à l'écran | quelques ms ; 4,1 ms en moyenne jusqu'au PC | mesuré, module capture |
| Le son, de la console au haut-parleur | 30 à 60 ms | estimé, module son ; sous le seuil où l'image et le son semblent décalés |
| Du pouce à la lumière | 30 à 65 ms | estimé, module manette ; la v1 visait moins de 50 à 80 ms |
| La cartouche | aucun en jeu : la copie est dans la console | par construction |

---

## 7. La démarche

Elle est commune à tous les modules :

- **chaque phase isole un seul risque**, et ne commence pas tant que la précédente n'a pas
  rempli son critère de sortie ;
- **les critères sont écrits avant la mesure** ;
- **l'irréversible le plus tard possible** : soudures dans la console, commandes de cartes ;
- **chaque module s'éprouve seul** avant de rejoindre les autres, avec un PC dans le rôle de
  l'autre moitié ;
- **des outils qui donnent le verdict en clair**, plutôt que des courbes à interpréter.

---

## 8. Ce qui a changé depuis la v1

| Sujet | v1, 23/07/2026 | v2, 11/10/2026 |
|---|---|---|
| Le cerveau | un émulateur sur Raspberry Pi Zero 2 W, dans le module cartouche | la vraie console. L'étape vidéo du 28/08 a montré qu'on pouvait en prendre l'image ([spécification vidéo](Spec_Video_Sniffer_et_Matrice_LED.md)) |
| L'écran | un écran IPS SPI de 2 pouces | des dalles LED : 3 × 3 dalles de 64 × 64, 192 × 192 pixels |
| Les boutons | une membrane de Game Boy lue par un ESP32 | une seconde Game Boy Pocket entière, et un injecteur qui appuie sur les boutons de la console |
| Le son | le PCM de l'émulateur, vers un ESP32 | le vrai son de la console, pris avant le bouton de volume |
| La cartouche | lue par le Pi, pour l'émulateur | une fente à part ; la console joue sur une copie, les sauvegardes reviennent à la cartouche |
| Les microcontrôleurs | ESP32 et Pi Zero 2 W | Pico 2 W partout, RP2350B pour la cartouche |
| Le réseau | le Pi en point d'accès, 3 clients | un réseau par type de périphérique, émis par la console, 4 clients chacun |
| L'alimentation | des LiPo dans chaque module | le secteur, sauf la manette sur piles |
| Les modules | 4 | 5 : capture et écran séparent la prise de l'image et son affichage |

---

## 9. Pour plus tard

- **Plusieurs manettes** : qui gagne quand deux joueurs appuient en même temps.
- **Plusieurs haut-parleurs** dans une même pièce : les garder en phase.
- **Plusieurs lecteurs de cartouche** : ce que la console en ferait.
- **Une source d'image de secours** : les écrans affichent toute source qui parle `PXL`. Une
  pièce exposée des heures durant pourrait basculer sur un émulateur si la console flanche.
