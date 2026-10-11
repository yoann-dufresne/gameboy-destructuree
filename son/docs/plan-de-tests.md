# Module SON — plan de tests

*Ce qu'il faut mesurer, dans quel ordre, et à quoi on reconnaît la réussite.*
Version 2 — 11/10/2026 : la phase 0 se mesure avec un Pico, sans matériel audio.

Le pourquoi des choix est dans [`plan.md`](plan.md) ; où souder, et la fiche à remplir pendant
la phase 0, dans [`prise-son-mgb.md`](prise-son-mgb.md).

Deux règles, héritées du module capture :

- **Chaque phase isole un seul risque.** Une phase ne commence pas tant que la précédente n'a
  pas rempli son critère de sortie.
- **Les critères sont écrits avant la mesure.** Les seuils chiffrés ci-dessous sont des
  propositions : la première mesure peut les corriger, mais pas le résultat qu'on espérait.

Trois mots reviennent souvent :

| Mot | Sens |
|---|---|
| **crête-à-crête** | l'écart, en volts, entre le point le plus haut et le point le plus bas du signal : la « force » du son à cet endroit |
| **bruit de fond** | ce qui reste sur le fil quand la console ne joue rien : souffle, bourdonnement |
| **saturer** | dépasser ce que le Pico peut lire : le haut du son est coupé, et la mesure est fausse |

---

## Instruments

| Instrument | Sert à |
|---|---|
| Multimètre | trouver les bonnes pattes, lire une tension fixe |
| **Le banc de mesure** | un Pico 2 W de rechange, le montage d'entrée sur plaque d'essai ([`prise-son-mgb.md`](prise-son-mgb.md) §3), le firmware [`firmware/mesure`](../firmware/mesure/README.md) et le script [`tools/mesure_son.py`](../tools/mesure_son.py). **Il mesure à ta place et donne le verdict en clair** : la force du son, la résistance à monter, le côté |
| Écouteurs ou casque | branchés sur la console, pour écouter seulement |

L'analyseur logique du module capture ne sert pas ici : il ne voit que des 0 et des 1.

> ⚠️ **Sur la prise casque de la console, on ne branche qu'un casque.** Son contact de masse
> n'est pas la masse de la console ([`prise-son-mgb.md`](prise-son-mgb.md) §1) : le relier
> au PC ou au Pico ferait passer du courant dans le haut-parleur.

**Toute comparaison se fait dos à dos** : même console, même scène, même réglage, à quelques
minutes d'intervalle. Un écart entre deux séances de jours différents ne prouve rien.

**Le son de test gratuit : celui du démarrage.** Il est identique à chaque allumage.

---

## Phase 0 — trouver où souder, et quelle résistance mettre

Trois questions, dans l'ordre : **où passe le son ? y a-t-il une tension fixe dessus ? quelle
force a-t-il ?** Le multimètre répond aux deux premières, le banc de mesure à la troisième.
Rien n'est soudé définitivement : seulement trois fils provisoires, retirés ensuite.

| Test | Ce que tu fais | Ce que tu dois trouver |
|---|---|---|
| **T0.1** Les bonnes pattes | Console **éteinte**, multimètre en ohms. Une pointe sur la borne − des piles, l'autre sur chaque patte de la molette de volume, en attendant que l'affichage se stabilise. Puis tourne la molette et regarde ce qui change. Photos | une patte à environ **0 Ω** : la masse (broche 1) ; deux pattes à environ **10 kΩ qui ne bougent pas** avec la molette : **nos deux points** (broches 4 et 5) ; deux pattes dont la valeur change avec la molette : pas celles-là |
| **T0.2** La tension fixe | Console **allumée**, multimètre en volts continus, sur les deux pattes trouvées. Molette au minimum, puis au maximum | **environ 0 V**, qui ne bouge pas avec la molette. Plus de 3 V : s'arrêter et le noter, le schéma aurait tort ; le montage le supporte quand même |
| **T0.3** Le banc branché | Câbler le banc sur plaque d'essai ([`prise-son-mgb.md`](prise-son-mgb.md) §3, valeurs du banc). Trois fils provisoires vers la console : patte 5 → voie A, patte 4 → voie B, patte 1 → masse. Flasher `firmware/mesure`, ouvrir la console, taper `g` : console éteinte, puis console allumée pendant le son de démarrage | console éteinte : « polarisation correcte » sur les deux voies. Console allumée : « le son passe » sur les deux |
| **T0.4** La force du son | `./tools/mesure_son.py niveau --duree 120 --sortie docs/releves/phase0-niveau.json`, puis allumer la console et jouer les passages les plus forts qu'on connaît : musique de l'écran titre, combats, explosions | le script donne le son le plus fort en volts, et **la valeur de Rb à monter**. C'est le chiffre qui manquait |
| **T0.5** La console ne remarque rien | Casque sur la console. Écouter le son de démarrage banc branché, puis débranché | même volume, aucun bourdonnement nouveau. Le calcul prévoit que le banc baisse le son de la console de l'ordre de 0,2 dB : inaudible |
| **T0.6** Gauche ou droite, *facultatif* | `./tools/mesure_son.py cotes`, voir ci-dessous | le script dit quelle voie est la gauche |

**Critère de sortie :** les deux pattes repérées et photographiées, leur tension fixe, et le
verdict de T0.4, le tout reporté dans la fiche de
[`prise-son-mgb.md`](prise-son-mgb.md) §6. **Pas de soudure définitive avant.**

### Gauche ou droite : pourquoi ce n'est pas bloquant, et comment le trouver

**On n'en a pas besoin pour souder.** Le haut-parleur de Game Boy est mono : le module
mélange les deux voies, et leur ordre n'y change rien. Et si un jour on écoute en stéréo, une
inversion se corrige dans le firmware en échangeant deux constantes, sans ressouder : c'est le
même raisonnement que pour `LD0` et `LD1` dans le module capture. D'ici là, on suit le
schéma : **patte 5 = gauche**.

**Pour le vérifier :**

1. Des écouteurs sur la console, pour écouter seulement ; le banc branché comme en T0.3.
2. `./tools/mesure_son.py cotes`, et jouer.
3. Quand un son ne passe que sur une voie, le script bipe et l'annonce. Appuie alors sur `g`
   si tu l'as entendu dans l'oreille gauche, `d` si c'était la droite. Tu peux aussi appuyer
   de toi-même dès que tu entends un son d'un seul côté.
4. Après trois réponses concordantes, le script conclut et dit s'il faut changer quelque
   chose.

**Où trouver un son d'un seul côté ?** Peu de jeux en font, et ce n'est écrit nulle part.
Pokémon Rouge, vérifié dans son désassemblage public
([pret/pokered](https://github.com/pret/pokered)), ne convient pas. Une seule note de tout le
jeu y est séparée : la première de la musique du repaire de la Team Rocket, qu'on entend
aussi à la Centrale, à la Caverne Azurée et dans la salle d'Aldo. Et à ce moment-là, chaque
côté joue une note différente : aucun n'est silencieux. Pour les autres cartouches, deux
façons de chercher : jouer avec le script allumé, qui repère ces moments tout seul ; ou
vérifier chaque jeu dans son désassemblage, quand il en existe un.

---

## Phase 1 — soudure et étage d'entrée

| Test | Ce que tu fais | Critère |
|---|---|---|
| **T1.1** Référence « avant » | **Avant de souder**, banc sur les fils provisoires : `mesure_son.py niveau --duree 15 --sortie docs/releves/avant.json`, puis allumer la console : le son de démarrage seul. Écouter la même séquence au casque | fichier rangé dans `docs/releves/` |
| **T1.2** « Après » | Fils définitifs soudés, Rs dans la console, connecteur branché : même mesure (`--sortie docs/releves/apres.json`, et `--rb` si Rb a changé), puis `mesure_son.py comparer docs/releves/avant.json docs/releves/apres.json`. Écouter au casque, Pico alimenté, puis Pico débranché de l'USB | le script annonce moins de 0,5 dB d'écart ; au casque, rien de changé **dans les deux états**. Pico éteint, ses entrées ne sont plus polarisées : c'est le cas qui charge le plus la console |
| **T1.3** L'étage d'entrée | Commande `g`, console éteinte puis allumée | « polarisation correcte » dans les deux cas : la tension fixe de la console ne traverse pas |
| **T1.4** La vidéo n'a rien vu | Commande `s` du sniffer, 5 minutes | compteurs d'erreur à 0, comme avant la soudure |

**Critère de sortie :** la console sonne comme avant, et l'étage d'entrée présente au Pico un
signal centré dans sa plage.

---

## Phase 2 — capture seule, sans réseau

Firmware autonome, radio éteinte : l'ADC lit les deux voies, le DMA les range, le processeur
réduit la cadence à 48 kHz, et la console USB livre l'enregistrement au PC (outil
`sondump.py`, fichier WAV qu'on écoute sur le PC).

| Test | Ce que tu fais | Critère |
|---|---|---|
| **T2.1** Reconnaissable | Enregistrer le son de démarrage | identique à l'écoute à ce qu'on entend au casque ; ni trou ni craquement |
| **T2.2** Marge | Les passages les plus forts de T0.4 | le son le plus fort reste sous 70 % de la plage du Pico : jamais saturé |
| **T2.3** Bruit de fond, radio éteinte | Une scène silencieuse, 30 s | pas plus de 1,4 fois (3 dB) le bruit de fond relevé en T0.4 |
| **T2.4** Continuité | 30 minutes | aucun débordement ; nombre d'échantillons = cadence × durée |

**Critère de sortie :** le Pico enregistre le son de la console, reconnaissable, sans trou ni
saturation.

---

## Phase 3 — la cohabitation : un seul Pico suffit-il ?

**C'est la phase qui décide** ([`plan.md`](plan.md) §2). Elle se fait en deux temps, pour ne
rouvrir le firmware du sniffer que si le premier passe.

| Test | Ce que tu fais | Critère |
|---|---|---|
| **T3.1** La radio s'entend-elle ? | Firmware de la phase 2, sur le Pico qui restera en place. Quatre enregistrements dos à dos sur une scène silencieuse : radio éteinte · WiFi connecté, sans trafic · trafic qui imite l'image, 299 paquets/s de 1 400 octets · image et son imités, ≈ 500 paquets/s | rien de nouveau au casque, volume au maximum ; à la mesure, un bruit de fond au plus 2 fois (6 dB) celui de T2.3. Si un bruit apparaît, sa hauteur dit d'où il vient : 298,6 Hz, le rythme des paquets de l'image ; 59,7 Hz, celui des images ; 200 Hz, celui des paquets de son ; 10 Hz, les balises du WiFi |
| **T3.2** Les remèdes, si T3.1 échoue | Un à la fois, T3.1 refait après chacun : régulateur du Pico forcé en mode PWM · polarisation mieux filtrée · moyenne sur plus d'échantillons · ADC externe I2S ([`plan.md`](plan.md) §2.3) | le premier qui fait passer T3.1 est retenu, et noté au journal |
| **T3.3** La vidéo n'a rien vu | Firmware fusionné. 30 min son actif, puis 30 min son coupé, dos à dos | compteurs d'erreur vidéo à 0 dans les deux cas ; cadence 59,727 img/s ; aller-retour min/moy/max équivalents |
| **T3.4** Le son n'a rien perdu | Même séance | 0 bloc perdu, 0 débordement |

**Décision :** un seul Pico si T3.1 (éventuellement après remède) et T3.3 passent. Si T3.1
résiste à tout, c'est l'ADC externe ; un second Pico ne servirait que si T3.3 échouait.

---

## Phase 4 — émission réseau, récepteur sur PC

Récepteur de référence : `hautparleur_virtuel.py`, qui joue le flux et horodate chaque paquet.

| Test | Procédure | Critère |
|---|---|---|
| **T4.1** Un client en joint-il un autre ? | Sur le réseau de l'écran : deux PC, ou un PC et un Pico, se pinguent puis s'échangent un flux UDP | oui : le haut-parleur pourra être un simple client du réseau. Non : à trancher avant d'aller plus loin ([`plan.md`](plan.md) §5) |
| **T4.2** Pertes et retards | 30 min, vidéo et son | pertes < 0,1 % ; retards d'arrivée relevés. **La réserve du haut-parleur se règle sur les plus longs retards mesurés**, le pire d'un paquet sur mille, pas sur une intuition |
| **T4.3** Sans coupure | 30 min avec la réserve retenue | aucune coupure audible ; les blocs manquants comptés, et masqués |
| **T4.4** L'écran n'a rien vu | Rapport de la tête, son actif puis coupé, dos à dos | pertes et gels inchangés |
| **T4.5** Reprises | Récepteur redémarré ; source redémarrée ; coupure WiFi provoquée (commande `d`) | le son revient seul, en moins de 2 s |

---

## Phase 5 — le haut-parleur

| Test | Procédure | Critère |
|---|---|---|
| **T5.1** Seul, depuis le PC | `son_envoi.py` : sinus à 1 kHz, balayage de 20 Hz à 20 kHz, puis un WAV | son propre, sans claquement. Le volume maximal est réglé pour que le haut-parleur de Game Boy ne distorde pas |
| **T5.2** Dérive des horloges | 2 heures de flux continu | la réserve du haut-parleur ne se vide ni ne déborde ; 0 coupure ; écart entre les deux horloges relevé |
| **T5.3** De bout en bout | Console → haut-parleur | à l'écoute, identique au casque de la console |
| **T5.4** Son et image | Au démarrage, filmer à ≥ 120 img/s l'écran LED et le haut-parleur : l'arrêt du logo contre le « ding ». Même film sur la console seule, pour la référence | retard du son sur l'image relevé. Repère : un son en retard devient perceptible vers 125 ms (UIT-R BT.1359) |
| **T5.5** Endurance | 6 heures, une journée d'exposition | aucun décrochage durable ; compteurs relevés |

---

## Phase 6 — intégration

Boîtier du haut-parleur, alimentation définitive, câble audio rangé dans la console. Recette :
T1.2, T3.3 et T5.3 refaits sur l'ensemble fermé.
