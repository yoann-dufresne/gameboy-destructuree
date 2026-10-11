# Module CARTOUCHE — les étapes

*Ce qu'on fait, dans quel ordre, ce qu'on vérifie, et quand on passe à la suite.*
Version 1 — 11/10/2026.

Le pourquoi des choix est dans [`plan.md`](plan.md). Les règles des autres modules valent ici :

- **Chaque phase isole un seul risque.** Une phase ne commence pas tant que la précédente n'a
  pas rempli son critère de sortie.
- **Les critères sont écrits avant la mesure.** Les seuils chiffrés sont des propositions : la
  première mesure peut les corriger, pas le résultat qu'on espérait.
- **L'irréversible le plus tard possible.**

Et une règle propre à ce module : **aucune écriture dans une cartouche tant que sa sauvegarde
n'est pas copiée et vérifiée sur le PC.** Les essais d'écriture se font sur une cartouche sans
valeur.

| Marque | Sens |
|---|---|
| 🔬 | une **mesure** : la valeur n'est pas connue, elle doit être relevée |
| 🔴 | **irréversible** : une erreur coûte une sauvegarde ou une console |
| ✅ | critère de sortie |

---

## Ordre de travail

Deux moitiés, le lecteur et la doublure, qui se rejoignent en phase 4. Le lecteur vient
d'abord : il met les sauvegardes à l'abri, et il fournit les copies dont la doublure a besoin.

```
 Phase 0 ── relevés sur la console et les cartouches ; rien de soudé
    │
 Phase 1 ── le lecteur sur plaque d'essai ; les sauvegardes à l'abri
    │
 Phase 2 ── la doublure au banc, lue par le lecteur
    │
 Phase 3 ── la doublure dans la console, enfichée ; le risque principal (T3.5)
    │
 Phase 4 ── les deux moitiés par le WiFi
    │
 Phase 5 ── avec la manette : allumer, se figer, redémarrer
    │
 Phase 6 🔴 ── les cartes maison ; la doublure à sa place définitive
```

---

## Phase 0 — relever, sans rien souder

**Objectif :** la fenêtre du bus, avant de commander quoi que ce soit, et ce que fait la
console sans cartouche. Les résultats vont dans la [fiche](#fiche), en fin de document ; captures
et photos dans `docs/releves/`.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T0.1** Tes cartouches | La liste de tes cartouches : titre, et si elles gardent une partie. Choisir la cartouche sans valeur des essais d'écriture | les cartouches dont la sauvegarde compte, à copier en premier (T1.5) ; la cartouche d'essai |
| **T0.2** 🔬 La fenêtre du bus | Console principale, Tetris enfiché. L'analyseur du module capture, 8 voies, masse sur celle de la console, sur les soudures du connecteur de cartouche, côté carte : CLK, `/RD`, `/WR`, `/CS`, A15, A0, D0, D7. Enregistrer une seconde en jeu, au plus vite que l'analyseur permet | la durée d'un cycle ; quand l'adresse change par rapport à CLK ; quand `/RD`, `/WR` et `/CS` basculent ; combien de temps la donnée de la cartouche reste stable avant que l'adresse ne change. Avec l'annexe sur le bus externe de la référence de Gekkio, cela donne la fenêtre dont la doublure dispose. À 24 MHz, la précision est de ± 42 ns : c'est une première estimation, T3.4 donne la vraie marge |
| **T0.3** 🔬 Sans cartouche | Multimètre en volts continus sur la broche 1 de la fente (VCC), console allumée puis éteinte. Puis allumer la console sans cartouche ; puis retirer Tetris en pleine partie, **avec une cartouche sans valeur, pas une qui garde une partie** | VCC à 5 V allumée, à 0 éteinte. Les deux images de référence, photographiées : le rectangle noir, la partie figée. La doublure devra donner les mêmes ([plan](plan.md) §5.6) |
| **T0.4** 🔬 La place | Pied à coulisse : la cavité de la fente dans la Pocket, la profondeur d'enfoncement d'une cartouche, la place autour du connecteur, côté carte | de quoi décider, en phase 6, où vit la doublure ; la carte RP2350B mesure 51 × 21 mm |

✅ **Critère de sortie :** la fiche remplie ; la capture de T0.2 dans `docs/releves/`. **Rien
n'est commandé avant T0.2** : si la fenêtre est bien plus étroite que quelques centaines de ns,
le plan change.

---

## Phase 1 — le lecteur sur plaque d'essai

**Objectif :** lire les cartouches sans jamais les abîmer, et mettre leurs sauvegardes à l'abri.
Le lecteur parle au PC par USB, puis par le WiFi.

**Il faut :** une carte RP2350B, les 74LVC8T245, un connecteur de cartouche, l'interrupteur de
charge, le microrupteur.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T1.1** La fente coupée | Pas de cartouche. Le Pico pose des 1 sur toutes ses sorties ; fente coupée, mesurer chaque broche de la fente | 0 V partout : les 74LVC8T245 isolent |
| **T1.2** La présence | Insérer et retirer une cartouche 20 fois, vite et lentement | 20 insertions et 20 retraits vus, aucun en double |
| **T1.3** 🔬 Lire | Chaque cartouche, deux fois de suite | logo et somme de l'en-tête justes ; somme globale juste ; les deux lectures identiques ; la durée, par Mo |
| **T1.4** Les types | Pour chaque cartouche : le type lu dans l'en-tête, et le CRC de la copie comparé à la base No-Intro | chaque type pris en charge reconnu ; chaque copie conforme à la référence |
| **T1.5** Les sauvegardes à l'abri | Lire deux fois la RAM de chaque cartouche qui garde une partie, et l'horloge s'il y en a une ; ranger les copies sur le PC | deux lectures identiques. **C'est la condition de toute écriture** |
| **T1.6** 🔴 Le retrait | Sur la cartouche d'essai seulement. Analyseur sur le microrupteur et sur VCC de la fente ; retirer la cartouche lentement, 10 fois, pendant une lecture | le microrupteur s'ouvre avant que les contacts ne quittent la fente, et la fente est coupée avant ; la sauvegarde de la cartouche d'essai, relue ensuite, intacte |
| **T1.7** 🔴 Écrire | Sur la cartouche d'essai seulement. Écrire un motif dans sa RAM, couper, relire ; puis y remettre sa sauvegarde d'origine, et la jouer dans une Game Boy | le motif relu à l'identique ; la partie d'origine se charge dans la Game Boy |
| **T1.8** Depuis le PC | `cartouche_pc.py` : copier une ROM, sauvegarder et restaurer une partie, par le WiFi | les mêmes fichiers qu'en USB |

✅ **Critère de sortie :** T1.1 à T1.8 ; toutes les sauvegardes qui comptent sont sur le PC. Le
lecteur est utilisable seul.

---

## Phase 2 — la doublure au banc, lue par le lecteur

**Objectif :** une doublure qui imite les contrôleurs de banques sans faute, éprouvée sans
console. Le lecteur la lit comme une cartouche : la doublure enfichée par sa plaquette au
format cartouche dans la fente du lecteur.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T2.1** L'isolement | Doublure alimentée, côté console hors tension. Mesurer chaque broche du côté 5 V | 0 V partout |
| **T2.2** Le brochage | Câbler les 32 broches de la doublure sur la carte RP2350B | toutes accessibles, pastilles du dessous comprises. Les broches du bus tiennent dans une même fenêtre de 32 broches, la seule qu'un PIO voit sur un RP2350B |
| **T2.3** 🔬 La flash | Écrire une copie de 1 Mo dans la flash depuis le PC, la relire | CRC juste ; la durée d'écriture, par Mo |
| **T2.4** Le lecteur lit la doublure | Pour chaque copie de T1.4 : la charger dans la doublure, la faire lire par le lecteur ; écrire puis relire sa RAM | la copie relue identique à l'originale, pour chaque type de contrôleur ; la RAM relue identique |

✅ **Critère de sortie :** T2.1 à T2.4. La doublure imite les cartouches ; reste à savoir si elle
le fait assez vite.

---

## Phase 3 — la doublure dans la console, enfichée

**Objectif :** la console joue sur la doublure. Rien n'est soudé : la plaquette au format
cartouche est enfichée dans la fente de la console, et les copies viennent du PC.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T3.1** Console éteinte | Doublure allumée, enfichée, console éteinte. Multimètre sur VCC de la console | 0 V : la doublure n'alimente pas la console |
| **T3.2** Tetris | Copie de Tetris, 32 Ko sans contrôleur de banques. Allumer la console, jouer 10 minutes | le logo, puis le jeu, sans plantage |
| **T3.3** 🔬 Une cartouche déjà vue | Envoyer deux fois la même copie de 1 Mo depuis le PC | la première fois, écrite en flash, et sa durée ; la seconde, reconnue, rien n'est écrit |
| **T3.4** 🔬 La marge | La doublure retarde sa réponse, de 10 ns en 10 ns, jusqu'au plantage ; capture de l'analyseur au dernier retard qui marche | la marge, en ns. **Proposition : 100 ns au moins** |
| **T3.5** 🔬 Sous le WiFi | Une heure de Pokémon Rouge, ou du plus gros jeu disponible, pendant que le PC inonde la doublure de paquets | aucun plantage, aucune image figée. Sinon, les parades du [plan](plan.md) §5.3, dans l'ordre, jusqu'au plan B à deux puces |
| **T3.6** Un jeu par contrôleur | Un jeu de chaque type que tu possèdes. Sauvegarder en jeu, éteindre la console, la rallumer | chaque jeu joue ; chaque partie est retrouvée ; l'horloge avance, pour un jeu qui en a une |

✅ **Critère de sortie :** T3.1 à T3.6. **C'est le cœur du module :** sans T3.5, rien ne sert
d'aller plus loin.

---

## Phase 4 — les deux moitiés par le WiFi

**Objectif :** la cartouche insérée dans le lecteur se joue sur la console, et ses sauvegardes
lui reviennent.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T4.1** L'appairage | Un faux lecteur sur le PC, avec un autre numéro d'appairage, envoie des annonces | la doublure les ignore |
| **T4.2** 🔬 La copie | Insérer une cartouche de 1 Mo ; refaire avec 5 % de paquets perdus, simulés | la copie juste dans les deux cas ; sa durée |
| **T4.3** 🔴 Le journal | Sur la cartouche d'essai : jouer, sauvegarder, retirer la cartouche 2 s après, la lire dans une vraie Game Boy. Compter aussi les paquets d'écriture pendant 5 minutes de jeu sans sauvegarde | la partie est dans la cartouche ; sans sauvegarde, aucune écriture ne part : les changements de banque restent dans la doublure |
| **T4.4** 🔬 Insertion → copie prête | Une cartouche déjà vue, puis une nouvelle | les deux durées, à comparer aux 2 à 3 s et à la dizaine de secondes prévues ([plan](plan.md) §9) |
| **T4.5** Le vibreur | Si tu as une cartouche à vibreur : jouer | la cartouche vibre dans la fente, au même moment que le jeu. Sinon, le noter : non éprouvé |
| **T4.6** Les situations | Parcourir le tableau du [plan](plan.md) §6, ligne par ligne, avec tes décisions | chaque ligne se passe comme écrit |
| **T4.7** 🔴 La sauvegarde parquée | Sur la cartouche d'essai, avec le journal retenu 10 s par un réglage d'essai : sauvegarder en jeu, arracher la cartouche aussitôt. (a) La remettre ; (b) recommencer, mais insérer une autre cartouche avant de la remettre ; (c) recommencer, mais modifier sa sauvegarde depuis le PC avant de la remettre. À chaque fois, lire la cartouche d'essai dans une vraie Game Boy | (a) la dernière partie sauvegardée est là ; (b) et (c) la sauvegarde parquée est oubliée, la cartouche est telle qu'on l'a remise |
| **T4.8** 🔴 Le journal en lieu sûr | Sur la cartouche d'essai, lecteur débranché : sauvegarder en jeu, éteindre la console, débrancher puis rebrancher la doublure ; enfin rebrancher le lecteur | la doublure retrouve son journal au redémarrage et le remet au lecteur ; la cartouche, lue dans une vraie Game Boy, a la dernière partie |

✅ **Critère de sortie :** T4.1 à T4.8.

---

## Phase 5 — avec la manette : allumer, se figer, redémarrer

**Objectif :** la console se comporte comme avec une fente d'origine, et ne s'allume pas sur un
rectangle noir quand une cartouche est dans la fente.

**Il faut :** l'injecteur du module [manette](../../manette/docs/etapes.md), qui alimente la
console.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T5.1** La console attend la copie | Insérer une cartouche nouvelle et basculer aussitôt l'interrupteur de la manette | la console ne s'allume qu'une fois la copie prête, puis démarre sur le jeu |
| **T5.2** Sans cartouche | Interrupteur sur marche, fente vide ; puis insérer une cartouche ; puis redémarrer par l'interrupteur | le rectangle noir de T0.3 ; rien ne change à l'insertion ; le jeu au redémarrage |
| **T5.3** Le retrait | En pleine partie, retirer la cartouche ; en insérer une autre ; puis redémarrer par l'interrupteur | la console se fige comme en T0.3, et le reste après l'insertion ; le nouveau jeu au redémarrage, avec la sauvegarde de sa cartouche |

✅ **Critère de sortie :** T5.1 à T5.3.

---

## Phase 6 🔴 — les cartes maison, la place définitive

**Objectif :** le lecteur dans son boîtier, la doublure à sa place dans la console.

1. **La décision de la place** : la carte au format cartouche fixée dans la fente, ou la
   doublure soudée à la place du connecteur. Avec les mesures de T0.4.
2. **Les cartes maison**, dessinées sur le brochage figé en phase 2 : celle du lecteur, celle
   de la doublure.
3. **La soudure**, si elle est retenue : en dernier.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T6.1** On recommence | T2.4, T3.4, T3.5 et T4.3 avec les cartes maison | les mêmes résultats ; la marge de T3.4 n'a pas fondu |
| **T6.2** Endurance | Une séance de 2 heures, tout le système allumé, deux changements de cartouche | aucun plantage ; chaque sauvegarde arrivée dans sa cartouche |

✅ **Critère de sortie :** T6.1 et T6.2. Le module est fini.

---

<a id="fiche"></a>
## Fiche de la phase 0 — à remplir pendant la séance, pas après

Date : `__/__/____`

**Tes cartouches (T0.1)**

| Titre | Garde une partie ? | Partie qui compte ? |
|---|---|---|
| | | |
| | | |
| | | |

Cartouche d'essai : `__________`

**La console principale**

| Relevé | Valeur |
|---|---|
| Durée d'un cycle (T0.2) | |
| Adresse stable, par rapport à CLK (T0.2) | |
| `/RD`, `/WR`, `/CS` : quand ils basculent (T0.2) | |
| Durée de la donnée stable avant le changement d'adresse (T0.2) | |
| Capture (T0.2) | `docs/releves/` |
| VCC de la fente, allumée et éteinte (T0.3) | |
| Photos : sans cartouche, cartouche retirée en jeu (T0.3) | `docs/releves/` |
| Cavité de la fente : largeur, profondeur, hauteur (T0.4) | |
| Profondeur d'enfoncement d'une cartouche (T0.4) | |
| Place autour du connecteur, côté carte (T0.4) | |
