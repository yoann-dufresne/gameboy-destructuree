# Module MANETTE — les étapes

*Ce qu'on fait, dans quel ordre, ce qu'on vérifie, et quand on passe à la suite.*
Version 2 — 11/10/2026 : marche et arrêt depuis la manette ; réseau quelconque au banc.

Le pourquoi des choix est dans [`plan.md`](plan.md). Trois règles, héritées des modules
capture et son :

- **Chaque phase isole un seul risque.** Une phase ne commence pas tant que la précédente n'a
  pas rempli son critère de sortie.
- **Les critères sont écrits avant la mesure.** Les seuils chiffrés sont des propositions : la
  première mesure peut les corriger, pas le résultat qu'on espérait.
- **L'irréversible le plus tard possible.**

| Marque | Sens |
|---|---|
| 🔬 | une **mesure** : la valeur n'est pas connue, elle doit être relevée |
| 🔴 | **irréversible** : une erreur coûte une pastille ou une console |
| ✅ | critère de sortie |

---

## Ordre de travail

Deux moitiés indépendantes, la manette et l'injecteur, et une attente : la fabrication de la
carte maison, une à trois semaines. Le dessin de la carte commence dès que la phase 1 a fixé le
brochage, et la fabrication court pendant les phases 2 à 4.

```
 Phase 0 ── relevés sur les deux consoles ; rien de soudé
    │
 Phase 1 ── la manette sur plaque d'essai ─────────────┐
    │                                                  │ brochage figé
 Phase 2 ── la manette parle au PC                     ▼
    │                                    Phase 5a-c ── dessin, gabarit, fabrication
 Phase 3 ── l'injecteur sur une fausse console         ·  (une à trois semaines d'attente)
    │                                                  ·
 Phase 4 🔴 ── l'injecteur dans la console principale  ·
    │                                                  ·
 Phase 5d-e 🔴 ── la carte dans la coque ◄·············┘
    │
 Phase 6 ── intégration
```

---

## Phase 0 — relever, sans rien souder

**Objectif :** tout ce que les phases suivantes supposent, mesuré sur les deux consoles. Les
résultats vont dans la [fiche](#fiche), en fin de document ; scans, photos et captures dans
`docs/releves/`.

### Sur la console distante, ouverte et sans piles

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T0.1** 🔬 Identité | Photographier les deux faces de la carte. Les scanner à plat, à 600 ppp, une règle posée à côté. Regarder si la coque a une fenêtre pour la LED d'alimentation ; mesurer son diamètre | la révision de la carte, sérigraphiée (`MGB-…CPU-…`) ; deux scans à l'échelle, qui serviront à dessiner la carte maison ; la fenêtre de la LED et son diamètre, ou son absence |
| **T0.2** 🔬 Les molettes | Multimètre en ohms. Entre chaque paire de pattes de VR1 (volume), puis de VR2 (contraste) : molette en butée basse, au milieu, en butée haute | la résistance totale, quelle patte est le curseur, et si la valeur au milieu est à peu près la moitié du total. Attendu d'après Gekkio : 2 × 10 kΩ et 30 kΩ |
| **T0.3** 🔬 L'interrupteur | Multimètre en continuité, entre la borne + des piles et chaque patte de SW1, dans chaque position | quelle patte reçoit les piles, laquelle les rend en position marche |
| **T0.4** 🔬 La place | Pied à coulisse : épaisseur de la carte ; hauteur libre entre la carte et la coque arrière, à l'emplacement du connecteur de cartouche ; place derrière la vitre, écran retiré ; épaisseur de ton Pico 2 W, composants compris | un rectangle de 51 × 21 mm, avec son antenne au bord et loin des piles, tient-il quelque part ? ([plan](plan.md) §5.5) |
| **T0.5** 🔬 Les membranes | Les pointes du multimètre sur la pastille de carbone d'une membrane, à 3 mm l'une de l'autre | quelques centaines d'ohms à quelques kΩ. Au-delà de 10 kΩ, le noter : la carte maison prévoira des tirages externes plus forts |

### Sur la console principale

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T0.6** 🔬 Les six points | Console **éteinte**, multimètre en continuité. Chaque pastille de bouton a deux moitiés. La moitié commune à Droite et à A est P10 ; à Gauche et B, P11 ; à Haut et Select, P12 ; à Bas et Start, P13 ([plan](plan.md) §2). Chercher pour chacune un point soudable relié : une via, un bord de pastille, de préférence au dos. Vérifier que TP45 est P14 et TP46, P15. Puis, multimètre en mode diode : pointe rouge sur l'autre moitié de la pastille Droite, noire sur TP45 | six points soudables, photographiés. En mode diode, environ 0,6 V dans ce sens, rien dans l'autre : c'est la diode de DA1, et elle dit dans quel sens monter les nôtres |
| **T0.7** 🔬 La lecture des boutons | Console **allumée**, un jeu dans un menu. Multimètre en volts continus sur TP45 : quelle tension au repos ? Puis l'analyseur logique, masse sur celle de la console, sur P10 à P13, P14 et P15. Enregistrer quelques secondes sans toucher, puis en appuyant sur Droite | la tension des lignes : 5 V ou 3 V. Combien de fois par seconde P14 et P15 descendent, et combien de microsecondes ils restent à 0 : c'est le temps que le jeu laisse avant de lire. Droite enfoncé : P10 à 0 pendant que P14 est à 0. Garder la capture : elle sert de référence à T4.2 |
| **T0.8** 🔬 L'alimentation par la prise DC | Piles retirées, interrupteur de la console sur marche. L'alimentation de laboratoire réglée à 3,0 V, limitée à 0,5 A, branchée sur la prise DC par une fiche 2,35 × 0,75 mm, centre positif. Allumer l'alimentation ; noter le courant au démarrage et en jeu. Recommencer à 2,7 V | la console démarre par la prise, à 3,0 V comme à 2,7 V ; son courant le plus fort, qui dimensionne le régulateur de l'injecteur ([plan](plan.md) §4.3) |

✅ **Critère de sortie :** la fiche remplie ; les deux scans à l'échelle ; les six points photographiés.
**Pas de soudure avant.**

---

## Phase 1 — la manette sur plaque d'essai

**Objectif :** lire proprement les boutons et les molettes, vivre sur deux piles, dormir et se
réveiller. Sans réseau.

**Le montage :** un Pico 2 W, 8 boutons poussoirs entre leur broche et la masse, 2
potentiomètres, le boîtier 2 × AAA, le maintien d'alimentation, transistors sur adaptateurs
SOT-23, et un module ADXL362 alimenté par les piles, relié au Pico par un adaptateur de
niveaux ([plan](plan.md) §4.2 et §5.3). L'interrupteur du boîtier reste sur marche : c'est un
interrupteur à glissière sur la plaque d'essai qui joue celui de la manette. Brochage
proposé, que la carte maison reprendra :

| Rôle | GPIO | Broche du Pico |
|---|---|---|
| Droite, Gauche, Haut, Bas | GP2, GP3, GP4, GP5 | 4, 5, 6, 7 |
| A, B, Select, Start | GP6, GP7, GP8, GP9 | 9, 10, 11, 12 |
| Volume, curseur | GP26 · ADC0 | 31 |
| Contraste, curseur | GP27 · ADC1 | 32 |
| Extrémités des potentiomètres | `ADC_VREF`, `AGND` | 35, 33 |
| Interrupteur, lecture | GP10 | 14 |
| LED RGB : rouge, vert, bleu | GP20, GP21, GP22 | 26, 27, 29 |
| Maintien d'alimentation | GP11 | 15 |
| Accéléromètre, SPI : MISO, CS, SCK, MOSI | GP16, GP17, GP18, GP19 | 21, 22, 24, 25 |
| Générateur d'appuis pour T3.4 | GP15 | 20 |
| Piles, après l'interrupteur et la diode | VSYS, GND | 39, 38 |

**Le firmware :** `firmware/manette`, première version. Il affiche l'état sur la console USB,
compte les appuis, tient le maintien, règle l'accéléromètre et se rendort.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T1.1** Chaque bouton | Appuyer sur chaque bouton, un à un | le bon nom, et aucun autre |
| **T1.2** Tous ensemble | Les huit à la fois, puis Droite et Gauche ensemble | les huit ; les deux. La croix d'origine empêche Droite + Gauche, pas l'électronique |
| **T1.3** 🔬 Les molettes | D'une butée à l'autre, lentement ; puis immobiles une minute | de 0 à 255 sans trou ; immobiles, la valeur ne bouge pas |
| **T1.4** Les rebonds | 100 appuis francs sur un bouton, puis 100 appuis mous, à peine enfoncés | le compteur dit 100, puis 100 |
| **T1.5** Sur piles seules | USB débranché, interrupteur sur marche | la LED pulse en bleu : le Pico vit sur les piles et cherche un réseau, qu'il n'a pas encore à ce stade |
| **T1.6** 🔬 Le sommeil et le réveil | USB débranché, le multimètre en série sur les piles, calibre µA. Manette posée, interrupteur sur arrêt : attendre. Puis la soulever ; la reposer ; puis interrupteur sur marche, attendre 3 minutes immobile ; puis arrêt. Enfin, sur marche, faire planter le firmware par sa commande de test, et passer sur arrêt. Taper la table à côté | endormie : quelques µA au plus. Soulevée : le courant passe à quelques dizaines de mA, et la LED reste éteinte ; reposée, il retombe au µA une minute plus tard. Sur marche : la LED pulse en bleu et le Pico reste éveillé, même immobile. Firmware planté : la manette se rendort quand même, par le chien de garde. Un coup sur la table ne la réveille pas : sinon, relever le seuil de mouvement |
| **T1.7** 🔬 Pour comparer : le sommeil du Pico lui-même, *facultatif* | Le maintien forcé fermé, le firmware endort le RP2350 dans le mode le plus économe que permet le SDK. Le multimètre au µA | la consommation de la carte Pico 2 W endormie. Si elle est de l'ordre du µA, le maintien et ses transistors deviennent superflus sur la carte maison ([plan](plan.md) §4.2) |

✅ **Critère de sortie :** T1.1 à T1.6 ; T1.7 est facultatif. **Le brochage est figé** : le dessin de la carte peut
commencer (phase 5a).

---

## Phase 2 — la manette parle au PC

**Objectif :** `MAN1` de bout en bout, la marche et l'arrêt, et les chiffres de la radio :
pertes, trous, consommation.

**Il faut :** un réseau WiFi quelconque, celui de l'écran, une box ou le partage de connexion
d'un téléphone ([plan](plan.md) §7.3), le PC sur ce réseau, et son adresse comme destination
dans le firmware. `manette_ecoute.py` tient le rôle de la console : il reçoit, accuse
réception, affiche l'état en direct et donne un verdict en clair.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T2.1** Le premier paquet | La manette sur piles, le PC à 1 m. `manette_ecoute.py` | les paquets arrivent ; la LED passe du bleu pulsé au rouge fixe dès le premier accusé. Arrêter l'outil, le réseau restant là : elle repasse au bleu pulsé en une demi-seconde. Puis couper le réseau : bleu pulsé aussi. Au retour de l'un et de l'autre : rouge. Si la manette et le PC sont deux clients d'un même point d'accès, c'est aussi la réponse à T4.1 du module son |
| **T2.2** L'état en direct | Chaque bouton, chaque molette | l'outil suit sans retard visible ; chaque relâchement arrive |
| **T2.3** 🔬 Pertes et trous | 10 minutes à 1 m, puis 10 minutes à 5 m, la manette en main | la proportion de paquets perdus, et le plus long trou sans paquet. Le seuil de sécurité de l'injecteur, 200 ms, doit dépasser largement le plus long trou ; sinon, il se règle ici |
| **T2.4** 🔬 Consommation | Multimètre en série sur les piles, **calibre 10 A** : sur le calibre mA, la résistance du multimètre ferait chuter la tension pendant les pointes de la radio, et le Pico redémarrerait. Lire le courant moyen dans chaque mode d'économie de la radio, du plus gourmand au plus sobre | un tableau mode → courant ; l'outil en tire l'autonomie estimée. Le mode le plus sobre est retenu si T3.4 ne le voit pas dans la latence. L'aller-retour ne tranche pas : en mode économe, c'est la réponse vers la manette qui attend, pas l'appui |
| **T2.5** 🔬 Vider un jeu de piles | Piles NiMH chargées. `manette_ecoute.py --journal`, et laisser tourner jusqu'au bout, une nuit | l'autonomie réelle, la tension au dernier paquet, et le nombre de redémarrages. Un redémarrage avant la fin : les piles s'effondrent pendant les émissions |
| **T2.6** L'arrêt, et rien sans marche | Interrupteur sur marche, puis arrêt, l'outil en marche. Puis, toujours sur arrêt, appuyer sur tous les boutons et tourner les molettes. Recommencer l'arrêt l'outil arrêté | l'outil affiche « arrêt » et l'acquitte, **puis ne reçoit plus rien**, appuis et molettes compris. Outil arrêté : la manette cesse de répéter l'arrêt au bout de 2 s |
| **T2.7** 🔬 Le réveil | 10 fois : manette endormie, la prendre en main, et basculer l'interrupteur au bout de 5 s. Puis 10 fois : manette endormie, basculer l'interrupteur sans l'avoir bougée, posée sur la table | avec un geste : le paquet « marche » arrive dès la bascule, la manette était déjà connectée. Sans geste : l'outil lit le temps de connexion dans le premier paquet, ≤ 3 s proposé. Trop long : adresse fixe pour la manette, plutôt que le DHCP |

✅ **Critère de sortie :** T2.1, T2.2, T2.6 et T2.7 réussis ; moins de 1 % de pertes ; aucun trou au-delà de la
moitié du seuil de sécurité ; autonomie d'au moins 6 heures, estimée en T2.4 puis vécue en
T2.5. T2.5 peut tourner pendant la phase 3.

---

## Phase 3 — l'injecteur sur une fausse console

**Objectif :** l'injecteur complet, sans toucher la console principale.

**Le montage :** un second Pico 2 W, 8 photoMOS sur adaptateurs, chacun avec sa diode et la
résistance de sa LED ([plan](plan.md) §3.1). Les LED sur GP2 à GP9, dans l'ordre de la phase 1 ;
la broche d'activation du régulateur 3,0 V sur GP10, avec une LED et sa résistance en guise
de console à alimenter.
Côté interrupteurs, **une fausse console** : P10 à P13 tirés à 3,3 V par 10 kΩ ; P14 et P15 sur
deux cavaliers, l'un à la masse, l'autre à 3,3 V, qu'on échange pour lire l'autre groupe. Sur
la plaque d'essai, la fausse console prend le 3,3 V du Pico : l'isolation se vérifie à part, en
T3.2.

**Le firmware :** `firmware/injecteur`. Il reçoit `MAN1`, accuse réception, pose les 8 broches,
relâche tout après 200 ms sans nouvelles, allume et éteint la console, et fait son rapport sur
la console USB toutes les 10 s : paquets reçus, perdus, relâches de sécurité. La manette prend
son adresse comme destination.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T3.1** Chaque bouton à sa place | `manette_envoi.py tour` : la fausse manette appuie sur chaque bouton tour à tour. L'analyseur sur P10 à P13. Puis échanger les cavaliers | P14 à la masse, seuls Droite, Gauche, Haut et Bas font descendre leur ligne ; P15 à la masse, seuls A, B, Select et Start |
| **T3.2** L'isolation | Pico débranché. Multimètre en ohms entre la masse du Pico et chaque borne côté console. Puis entre chaque paire de bornes d'un même photoMOS | « circuit ouvert » partout : rien ne relie le Pico à la console, et un injecteur éteint n'appuie sur rien |
| **T3.3** La sécurité | `manette_envoi.py maintenir A` : A enfoncé, puis le silence. Recommencer avec la vraie manette : maintenir A, et l'éteindre | A relâché moins de 250 ms après le dernier paquet, mesuré à l'analyseur |
| **T3.4** 🔬 Latence de la manette à l'injecteur | La vraie manette sur piles, en mode banc : GP15 appuie sur Droite à intervalles aléatoires, par un fil de GP15 à GP2 à travers 1 kΩ. L'analyseur : voie 0 sur GP2 de la manette, voie 1 sur P10 de la fausse console. 10 minutes. `latence.py capture.sr` | médiane ≤ 10 ms, 99 % des appuis ≤ 30 ms, le pire noté ; autant d'appuis et de relâchements arrivés que d'envoyés. Recommencer dans le mode d'économie choisi en T2.4 |

| **T3.5** Marche et arrêt | Avec la vraie manette : marche, arrêt, 10 fois. Puis marche, et retirer une pile de la manette. Puis marche, arrêt et marche en moins d'une demi-seconde | la LED « console » suit l'interrupteur. Pile retirée : les boutons sont relâchés, la LED « console » **reste allumée** ; au bout de 2 s, l'injecteur signale l'absence sur sa console USB ; au bout de la durée d'absence, réglée à 30 s pour le test, la LED « console » s'éteint. Remettre la pile avant : tout reprend, sans extinction. Aller-retour rapide : la LED « console » reste éteinte une seconde au moins |

✅ **Critère de sortie :** T3.1 à T3.5.

---

## Phase 4 🔴 — l'injecteur dans la console principale

**Objectif :** jouer à la console principale depuis la manette.

**La soudure :** six fils Kynar sur les points de T0.6, collés dès leur sortie, vers un connecteur
JST-SH 6 points. La plaquette des photoMOS reste **hors de la console**, au bout du câble :
la console ne reçoit que des fils et un connecteur, comme pour la vidéo et le son.
L'alimentation, elle, ne demande aucune soudure : la fiche DC dans la prise, l'interrupteur de
la console sur marche, les piles retirées.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T4.1** La console ne remarque rien | Jouer, câble débranché ; puis câble branché, injecteur éteint | le jeu et les boutons d'origine marchent comme avant |
| **T4.2** 🔬 Pas d'appui fantôme | L'analyseur comme en T0.7, injecteur branché, allumé, au repos. Comparer à la capture de T0.7, dos à dos | aucune descente nouvelle sur P10 à P13. Sinon : le plan B, [plan](plan.md) §3.3 |
| **T4.3** Chaque bouton dans un jeu | `manette_envoi.py tour` dans un menu ; puis A + B + Select + Start depuis la vraie manette | chaque bouton agit ; la combinaison redémarre le jeu, ce qui éprouve les huit à la fois |
| **T4.4** Les deux ensemble | A maintenu sur la console principale, Droite sur la manette | les deux comptent |
| **T4.5** 🔬 Latence jusqu'à la console | Comme T3.4, voie 1 sur P10 de la vraie console : on mesure jusqu'à la première lecture du jeu | la latence de T3.4, plus 0 à 16,7 ms d'attente de la lecture |
| **T4.6** Endurance | 30 minutes de jeu à la manette | aucun bouton coincé ni manqué ; le rapport de l'injecteur ne compte aucune relâche de sécurité |
| **T4.7** 🔬 Allumer la vraie console | 20 fois : prendre la manette endormie, basculer sur marche, attendre le logo, puis arrêt. Filmer quelques allumages au ralenti, l'interrupteur et l'écran de la console dans le cadre | la console démarre à chaque fois et s'éteint à chaque arrêt ; de l'interrupteur au logo, le délai d'une console d'origine, à quelques dizaines de ms près |

✅ **Critère de sortie :** T4.1 à T4.7.

---

## Phase 5 — la carte maison

**Objectif :** la manette dans sa coque, sur sa propre carte.

**5a — Dessin, dans KiCad**, d'après les scans et les mesures de la phase 0 : contour, trous de
vis, pastilles des boutons au bon endroit ; molettes, interrupteur et contacts de piles sur leurs
empreintes ; le Pico 2 W soudé à plat, antenne au bord au-dessus d'une découpe sans cuivre de
14 × 9 mm ; le maintien d'alimentation et l'accéléromètre, sauf si T1.7 les a rendus
superflus ; P-MOSFET de l'USB et protection contre l'inversion des piles ; la LED RGB à l'emplacement de l'originale ; accès au port USB ;
points de test sur une ligne de bouton et sur GP15, pour refaire T3.4. Épaisseur 1,2 mm, finition
or (ENIG). Des cartes au format de la Game Boy Pocket ont déjà été publiées, pour y loger un
Raspberry Pi par exemple : les chercher avant de tout relever soi-même.

**5b — Gabarit.** Imprimer la carte à l'échelle 1, la coller sur un carton, découper. Dans la
coque, avec les membranes : les trous tombent en face des vis, les pastilles en face des
boutons, le Pico a sa place, la coque ferme. **Pas de commande avant.**

**5c — Fabrication.**

**5d 🔴 — Assemblage.** Le Pico soudé à plat ; l'interrupteur, les molettes et les contacts de
piles, neufs ou dessoudés de la carte d'origine. Dessoudés, la console distante ne redevient
plus une Game Boy sans eux.

**5e — Essais dans la coque :** T1.1 à T1.4 repris sur la carte, puis :

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T5.1** Les membranes | 100 appuis sur chaque bouton, francs, mous, en biais | 100 lus à chaque fois |
| **T5.2** 🔬 L'antenne | La force du signal que la manette rapporte, coque ouverte puis fermée, piles en place ; puis T2.3 repris coque fermée | quelques dB perdus au plus ; les pertes et les trous de T2.3 tiennent leur critère |
| **T5.3** 🔬 Autonomie et sommeil | T2.5 repris dans la coque ; puis le courant de la manette endormie, au µA, piles en place | au moins 6 heures ; endormie, quelques µA au plus |
| **T5.4** 🔬 Latence | T3.4 repris, sur les points de test | les chiffres de T3.4 |

✅ **Critère de sortie :** T5.1 à T5.4.

---

## Phase 6 — intégration

**Objectif :** la manette dans l'installation.

**Il faut :** le banc de coexistence du [document réseau](../../reseau.md) passé (R1 à R3).

1. **L'injecteur à sa place définitive**, point d'accès du réseau des manettes ; la manette
   réglée sur ce réseau et son adresse fixe ([plan](plan.md) §7.2).
2. **La console prévient l'écran et le son** : l'injecteur envoie marche, arrêt, luminosité,
   volume et le message « aucune manette » sur le bus filaire de la console ; le sniffer
   incruste le message. Ce sont des changements dans ces modules et dans l'injecteur, pas dans
   la manette, qui a tout envoyé à la console dès la phase 2.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T6.1** Endurance | Une séance de 2 heures, tout le système allumé | aucun bouton coincé ni manqué ; aucune relâche de sécurité |
| **T6.2** 🔬 Du pouce à la lumière | Filmer au ralenti, 240 images/s, le pouce sur la manette et l'écran LED dans le même cadre. Compter les images entre l'appui et le changement à l'écran, sur 20 appuis | la latence ressentie, à comparer aux 30 à 65 ms prévues ([plan](plan.md) §8) |
| **T6.3** La chaîne complète | Interrupteur de la manette sur marche, puis arrêt, 5 fois. Tourner chaque molette | la console, l'écran et le son s'allument et s'éteignent ensemble ; la luminosité et le volume suivent les molettes |
| **T6.4** La manette absente | En jeu, retirer une pile de la manette ; la remettre après 30 s. Puis la retirer et attendre la durée d'absence | le message « aucune manette » apparaît par-dessus le jeu au bout de 2 s, avec son décompte, et disparaît quand la manette revient ; la partie continue. Sans retour : la console, l'écran et le son s'éteignent |

✅ **Critère de sortie :** T6.1 à T6.4. Le module est fini.

---

<a id="fiche"></a>
## Fiche de la phase 0 — à remplir pendant la séance, pas après

Date : `__/__/____`

**Console distante**

| Relevé | Valeur |
|---|---|
| Révision de la carte (T0.1) | |
| VR1, volume : résistance totale, patte du curseur, valeur au milieu (T0.2) | |
| VR2, contraste : résistance totale, patte du curseur, valeur au milieu (T0.2) | |
| SW1 : patte des piles, patte en marche (T0.3) | |
| Fenêtre de la LED dans la coque, diamètre (T0.1) | |
| Épaisseur de la carte (T0.4) | |
| Hauteur libre au connecteur de cartouche (T0.4) | |
| Place derrière la vitre (T0.4) | |
| Épaisseur du Pico 2 W, composants compris (T0.4) | |
| Résistance d'une pastille de carbone (T0.5) | |

**Console principale**

| Ligne | Point soudable choisi (photo) | Vérifié en continuité |
|---|---|---|
| P10 | | |
| P11 | | |
| P12 | | |
| P13 | | |
| P14 | TP45 ? | |
| P15 | TP46 ? | |

| Relevé | Valeur |
|---|---|
| Sens de la diode de Droite (T0.6) | |
| Tension de P14 au repos (T0.7) | |
| Lectures par seconde (T0.7) | |
| Durée de P14 à 0, en µs (T0.7) | |
| Capture de référence (T0.7) | `docs/releves/` |
| Démarre par la prise DC à 3,0 V ? à 2,7 V ? (T0.8) | |
| Courant au démarrage, en jeu (T0.8) | |
