# Module SON — plan de réalisation

*Sous-projet « son » de la Game Boy Pocket déstructurée.*
Version 1 — 11/10/2026

**Le « pourquoi » est ici.** Où souder : [`prise-son-mgb.md`](prise-son-mgb.md). Quoi
mesurer et dans quel ordre : [`plan-de-tests.md`](plan-de-tests.md).

---

## 0. Décisions proposées

Aucune n'est encore éprouvée. La dernière colonne dit quel test la confirme ou la renverse.

| Sujet | Proposition | Tranchée par |
|---|---|---|
| **Ce qu'on capture** | le signal analogique des deux voies, à l'entrée du potentiomètre de volume | T0.1 à T0.4 |
| **Conversion** | l'ADC du RP2350, deux voies à 192 kHz, décimées à 48 kHz | T2.3, T3.1 |
| **Contrôleur** | le Pico du sniffer si la phase 3 le permet ; un Pico 2 W de banc d'ici là | T3.1, T3.3 |
| **Format** | PCM 16 bits signé, stéréo, 48 kHz, blocs de 5 ms | — |
| **Protocole** | `SON1` sur UDP, port 4243, un en-tête qui se suffit à lui-même | — |
| **Destination** | une adresse fixe, celle du haut-parleur | T4.1 |
| **Restitution** | Pico 2 W, MAX98357A, haut-parleur de Game Boy | T5.1 |
| **Horloges** | le récepteur rattrape la dérive en rééchantillonnant | T5.2 |

---

## 1. Objectif et périmètre

**Ce qu'on construit :** une source qui prend le son de la console au plus près du
processeur et l'émet en UDP, et un haut-parleur réseau qui le joue. La console continue de
fonctionner normalement, haut-parleur compris : on écoute, on ne pilote rien.

**Ce qu'on ne construit pas :** les boutons, l'émulateur, et la resynthèse du son à partir
des registres (§3).

**Chaque moitié s'éprouve seule** : la source avec un haut-parleur virtuel sur PC, le
haut-parleur avec un émetteur de test sur PC. Le module écran a montré que c'est ce qui
permet de savoir, à chaque panne, laquelle des deux moitiés accuser.

---

## 2. Un seul Pico, ou deux ?

### 2.1 Ce que le sniffer a de libre

| Ressource | Prise par la vidéo | Demandée par le son | |
|---|---|---|---|
| Broches | GP0–GP5, GP20, GP21 | GP26 et GP27 (ADC0, ADC1), AGND, ADC_VREF | libres ✅ |
| ADC | rien | 384 kS/s sur 500 | ✅ |
| DMA | un canal, plus ceux du pilote WiFi | un canal | 16 sur le RP2350 ✅ |
| Processeur | le cœur 0 : lwIP et l'émission ; **le cœur 1 est libre** | la décimation, ≈ 3 M multiplications/s ; 200 paquets/s | ✅ |
| RAM | 11,5 ko d'images | ≈ 25 ko | sur 520 ✅ |
| Débit | 2,79 Mbit/s, 299 paquets/s | 1,56 Mbit/s, 200 paquets/s | un Pico 2 W en a reçu 24,6 (module écran, phase 4) ✅ |

**Pour le calcul et le débit, un Pico suffit, avec une marge large.** Comme pour l'image,
l'échantillonnage ne demande pas de processeur : l'ADC convertit en continu à son propre
rythme, le DMA range.

La décimation irait sur le cœur 1. Ce n'est pas une question de charge, elle tiendrait sur
le cœur 0 : c'est qu'elle ne pourra alors pas retarder les interruptions de la capture
vidéo, dont la séquence VSYNC se joue à quelques microsecondes près. Le cœur 1 ne touchera
ni au WiFi ni à lwIP : c'est `cyw43_arch_init` lancé depuis le cœur 1 qui avait bloqué le
sniffer ([journal](../../capture/firmware/sniffer/JOURNAL.md), 26/09/2026).

### 2.2 « Un second canal WiFi » : il n'y a qu'une radio

Le CYW43439 du Pico 2 W a une radio, qui écoute un canal et rejoint un réseau. Le son sera
**un second flux UDP sur la même association** : un autre port, une autre destination. Pas
un second canal radio, et il n'en a pas besoin : son débit est la moitié de celui de
l'image.

### 2.3 Le vrai inconnu : la radio dans l'ADC

Chaque émission WiFi tire une pointe de courant sur l'alimentation du Pico. Le régulateur la
rattrape, mais une ondulation reste, et l'ADC la voit : sa référence vient du même 3,3 V. La
vidéo émet 5 paquets par image : si l'ondulation passe, elle s'entendra comme un sifflement grave à
298,6 Hz et ses harmoniques, surtout dans les silences. Aucun calcul ne dit si elle passe ;
c'est la mesure T3.1.

Ce qui est déjà acquis : l'ADC fait 12 bits, dont 9 à 9,5 effectifs (datasheet RP2350,
§12.4 et tableau 1685). Le défaut de linéarité du RP2040 (errata E11) est corrigé. Pour la
musique d'une Game Boy, c'est suffisant ; reste le bruit apporté par le montage.

> 🔑 **Un second Pico 2 W ne lèverait pas cet inconnu.** Il aurait sa propre radio, à
> quelques centimètres de son propre ADC, et émettrait ses 200 paquets de son par seconde.
> Le remède à un ADC bruité est un ADC éloigné de la radio, pas un second Pico.

Les remèdes, du moins cher au plus cher, chacun suivi d'une nouvelle mesure T3.1 :

1. **Forcer le régulateur du Pico en PWM** par la broche `WL_GPIO1` du CYW43439 : la
   datasheet du Pico 2 W (§3.3) le recommande pour réduire l'ondulation vue par l'ADC.
2. **Améliorer la référence** : la même section propose une référence shunt de 3,0 V
   (LM4040) sur `ADC_VREF`.
3. **Moyenner davantage** : suréchantillonner plus haut, au prix de la marge de l'ADC.
4. **Un ADC externe I2S**, par exemple un module PCM1808 : la conversion se fait sur sa
   propre carte, avec sa propre alimentation analogique. Trois fils de données et une
   horloge, lus par un PIO. C'est le seul cas où il faudrait souder davantage, et ce serait
   côté Pico : rien ne change dans la console.

### 2.4 Ce qui plaide encore pour un second Pico

- **Ne pas rouvrir le firmware du sniffer**, déclaré clos le 09/10/2026 après 108 233
  images sans une erreur. Y ajouter le son, c'est risquer une régression ; T3.3 la
  chercherait, dos à dos.
- **L'indépendance des modules** : si le son vit dans le sniffer, le module capture
  embarque du code du module son, copié avec un `PROVENANCE.txt` comme l'est `pxl1.h`.

Et contre :

- une seconde alimentation, une seconde antenne contre la console, une seconde boucle de
  masse avec le PC pendant la mise au point ;
- **le réseau de l'écran n'accepte que 4 clients** (datasheet du Pico 2 W : « SoftAP, up
  to 4 clients »). Sniffer, source son, haut-parleur et un PC de mise au point le
  rempliraient.

### 2.5 Recommandation

**Viser un seul Pico, et laisser la phase 3 trancher.** Les phases 0 à 2 se font sur un
Pico 2 W de banc, avec le même câble et le même étage d'entrée que la version définitive.
T3.1 se passe sans toucher au sniffer : le firmware de banc imite le trafic de la vidéo.
Rouvrir le sniffer n'est utile que si T3.1 passe, et reste ta décision.

**La soudure dans la console est la même dans les deux cas.** Seul change le Pico au bout du
câble audio.

---

## 3. Ce qu'on capture

**Le signal analogique, pas les registres.** La spec initiale envisageait, en version
« puriste », de transmettre les écritures dans les registres du son et de resynthétiser
l'audio au récepteur. Sur une vraie console, ces registres sont dans la puce du processeur :
rien ne garantit que leurs écritures sortent sur le bus de la cartouche, et la resynthèse
demanderait un APU fidèle jusque dans ses bizarreries. Le signal analogique, lui, est le son
réel, défauts compris.

**À l'entrée du potentiomètre de volume.** Le niveau y est indépendant de la molette : la
console peut se taire sans couper le flux. On évite aussi le bruit de l'amplificateur. Le
détail, le schéma et les candidats sont dans [`prise-son-mgb.md`](prise-son-mgb.md).

---

## 4. La chaîne

```
 console                Pico source                                         réseau
 VR1 G, D ─[10 kΩ]─► étage d'entrée ─► ADC0, ADC1 ─DMA─► anneau ─► décimation ÷4 ─► blocs de 5 ms ─► SON1
                     couplage, 1,65 V   192 kHz par voie             cœur 1            48 kHz, 16 bits

 Pico haut-parleur
 SON1 ─► réserve ─► rééchantillonnage ─► I2S (PIO) ─► MAX98357A ─► haut-parleur
```

**Pourquoi suréchantillonner.** Les ondes carrées de la Game Boy ont des harmoniques bien
au-delà de 20 kHz. Échantillonné directement à 48 kHz, tout ce qui dépasse 24 kHz se
replierait dans la bande audible, et un filtre RC n'y retranche que quelques décibels.
Échantillonné à 192 kHz, le filtre analogique n'a plus qu'à éteindre ce qui dépasse
170 kHz environ, et le filtre numérique de la décimation fait le reste. La moyenne de quatre
échantillons gagne en prime jusqu'à 6 dB de bruit. 2 voies × 192 kHz = 384 kS/s, soit une
conversion toutes les 125 périodes de l'horloge de 48 MHz : la cadence tombe juste.

**Pourquoi 16 bits, stéréo, 48 kHz.** Après décimation, l'ADC livre une dizaine de bits
effectifs ; 16 bits est le format que comprennent sans conversion un PC, un fichier WAV et un
convertisseur I2S. La console produit de la stéréo, pour son casque : la garder ne coûte
rien à la source, et le haut-parleur, mono, mélange les deux voies. 48 kHz est la fréquence
native des cartes son et des convertisseurs I2S ; 32 kHz économiserait 0,5 Mbit/s dont
personne n'a besoin.

**Pourquoi des blocs de 5 ms.** Chaque milliseconde de bloc est une milliseconde de
latence, ce qui pousse à les raccourcir. Mais le module écran a mesuré, le 18/09/2026, sur
un lien dégradé, que le nombre de paquets compte plus que le débit : 5 ms donnent
200 paquets par seconde, moins que la vidéo.

---

## 5. Le réseau

### 5.1 Topologie

La tête du module écran émet le réseau, sur le canal 11, en 192.168.4.1, et distribue par
DHCP les adresses 192.168.4.16 à .23. Le sniffer en est client ; le haut-parleur le sera
aussi. Le son ira donc d'un client à un autre, **relayé par la tête** : il passe deux fois
sur l'antenne. Avec la vidéo, cela fait environ 6 Mbit/s, très en dessous de ce que le lien
a montré.

**Ce relais ne devrait pas passer par le firmware de la tête.** Le pilote `cyw43-driver`
laisse le paramètre `ap_isolate` à sa valeur par défaut, qui, pour cette famille de puces
Broadcom, autorise le relais entre clients dans la radio elle-même (d'après le pilote
Linux `brcmfmac`). La pile lwIP de la tête, qui garde parfois son verrou 140 ms, ne verrait
donc pas passer le son. Ce n'est confirmé par aucun document propre au CYW43439 : c'est le
premier test de la phase 4, T4.1.

### 5.2 La destination

Comme l'image vise la tête en 192.168.4.1, le son vise une adresse fixe : le haut-parleur se
donne **192.168.4.2**, hors de la plage du DHCP. Le module écran n'a rien à changer.

Plusieurs haut-parleurs, plus tard : chacun s'annoncerait à la source, qui enverrait une
copie à chacun. **La diffusion générale (broadcast) est écartée** : sur WiFi, elle part
sans accusé ni répétition, au débit de base, et 1,6 Mbit/s y occuperaient l'antenne en
perdant des paquets.

### 5.3 Le protocole `SON1` — proposition

Mêmes conventions que `PXL1` : champs en little-endian, nombre magique en tête.

| Octet | Taille | Champ |
|---|---|---|
| 0 | 4 | nombre magique `'S','O','N','1'` |
| 4 | 1 | type : 0 = bloc audio, 1 = accusé, pour mesurer l'aller-retour comme `PXL1_TYPE_PING` |
| 5 | 1 | format : 0 = PCM 16 bits signé |
| 6 | 1 | nombre de voies : 2 |
| 7 | 1 | réservé |
| 8 | 4 | fréquence d'échantillonnage, en Hz |
| 12 | 4 | numéro du premier échantillon du bloc, roulant |
| 16 | — | échantillons entrelacés G, D : 960 octets pour 5 ms |

**L'en-tête porte tout le format**, à chaque paquet : un récepteur redémarré reprend dès le
premier paquet reçu. C'est la leçon de `PXL1`, qui a dû ajouter une commande de géométrie
renvoyée toutes les 2 secondes, quand `PXL2` la porte dans chaque en-tête. **Le numéro
d'échantillon** rend une perte visible, un trou dans la numérotation, et place chaque bloc
dans le tampon même s'il arrive dans le désordre. Il fait le tour en 24,8 heures à 48 kHz.

Le fichier `son1.h` fera foi, dans `firmware/commun/`.

---

## 6. Les horloges

La source échantillonne au rythme de son quartz, le haut-parleur joue au rythme du sien.
Deux quartz indépendants diffèrent de quelques dizaines de millionièmes (ppm). À 50 ppm, l'écart est de
2,4 échantillons par seconde : un tampon de 40 ms, rempli à moitié, se vide ou déborde en
moins de 7 minutes, et le son craque à chaque fois.

**Le haut-parleur rattrape la dérive.** Il mesure le remplissage de son tampon et ajuste en
continu un rapport de rééchantillonnage, par interpolation. Pour un Cortex-M33, c'est
négligeable. Jeter ou doubler un échantillon de temps en temps serait plus simple, mais
s'entendrait sur une note tenue. T5.2 mesure l'écart réel et vérifie que le tampon reste
borné sur deux heures.

L'horloge de la console n'intervient pas : on échantillonne un signal analogique.

---

## 7. Latence, et accord avec l'image

| Étape | Durée |
|---|---|
| Remplissage d'un bloc | 5 ms |
| Décimation | < 0,5 ms |
| Émission et deux traversées radio | quelques ms ; des pointes à plusieurs dizaines (aller-retour du sniffer : 3,5 ms en moyenne, 46,5 ms au pire) |
| Réserve du haut-parleur, qui absorbe les retards du WiFi | 20 à 50 ms, dimensionnée par T4.2 sur les pointes mesurées |
| Sortie I2S | ≈ 2 ms |
| **Total** | **≈ 30 à 60 ms** |

L'image arrive sur l'écran LED en 10 à 25 ms, estimation à confirmer : la latence de la v2
de l'écran reste à mesurer. Le son suivra avec quelques dizaines de
millisecondes de retard. Un son en retard sur l'image ne se remarque qu'au-delà de 125 ms
environ (recommandation UIT-R BT.1359). T5.4 mesure le décalage réel.

Le haut-parleur d'origine, lui, sonne sans retard. S'il reste audible dans la pièce, il fera
écho : molette à zéro, puisque la prise est en amont du volume.

---

## 8. Le haut-parleur

- **Pico 2 W et module MAX98357A** : convertisseur et amplificateur en classe D, piloté en
  I2S par trois fils qu'un PIO produit. Le MAX98357A mélange lui-même les deux voies,
  (G + D) / 2, selon la tension de sa broche `SD_MODE`.
- **Le haut-parleur d'une Game Boy** : une pièce de rechange, ou celui de la console. Sa
  puissance admissible est inconnue, alors que le MAX98357A délivre plusieurs watts : gain
  du module au minimum, plafond de volume dans le firmware, réglé en T5.1.
- **Alimentation USB 5 V**, sans rien de commun avec la console.
- **Générique** : il joue tout flux `SON1`, d'où qu'il vienne. Un PC peut l'alimenter avec
  `son_envoi.py`.

---

## 9. Budget chiffré

| Grandeur | Valeur | D'où elle vient |
|---|---|---|
| Échantillonnage de l'ADC | 192 kHz par voie, 384 kS/s | 48 MHz / 125 ; 77 % des 500 kS/s |
| Sortie | 48 kHz, 2 voies, 16 bits | décimation par 4 |
| Bloc | 5 ms : 240 échantillons par voie, 960 octets | + 16 octets d'en-tête |
| Paquets | 200 par seconde | |
| Débit | 1,56 Mbit/s | 976 × 200 × 8 |
| Sur l'antenne | ≈ 3,1 Mbit/s de son, 2,8 d'image | le son passe deux fois, relayé par la tête |
| Décimation | ≈ 3 M multiplications par seconde | 48 000 × 2 voies × 32 coefficients ; quelques % d'un cœur à 150 MHz |
| RAM, source | ≈ 25 ko | anneau du DMA de 20 ms, file de 8 blocs |

---

## 10. Arborescence prévue

```
son/
├── README.md
├── docs/
│   ├── plan.md                    ← ce document
│   ├── plan-de-tests.md
│   ├── prise-son-mgb.md           ← où souder ; la fiche de la phase 0
│   └── releves/                   ← enregistrements, spectres, photos
├── firmware/
│   ├── mesure/                    ← le banc de la phase 0 : il mesure et rend compte ✅
│   ├── commun/son1.h              ← le protocole, qui fait foi
│   ├── source/                    ← capture et émission ; banc, puis peut-être fusionné au sniffer
│   └── hautparleur/               ← réception, rééchantillonnage, I2S
└── tools/
    ├── mesure_son.py              ← le verdict du banc : force du son, Rb, côté, avant / après ✅
    ├── console.py, flash.sh       ← dialogue avec les firmwares ✅
    ├── sondump.py                 ← l'enregistrement du Pico, en WAV (phase 2)
    ├── analyse_son.py             ← niveaux, bruit de fond, sifflements ; spectre d'un WAV
    ├── hautparleur_virtuel.py     ← récepteur SON1 de référence, sur PC (phase 4)
    └── son_envoi.py               ← émetteur SON1 de test : sinus, WAV, pertes simulées
```

Seuls les éléments marqués ✅ sont écrits : les autres naîtront avec la phase qui en a besoin.

---

## 11. Nomenclature

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| A1 | Raspberry Pi Pico 2 W | 2 | un pour le banc, puis peut-être la source définitive ; un pour le haut-parleur |
| A2 | Résistance 10 kΩ | 2 | Rs, dans la console |
| A3 | Condensateur 1 µF, film | 4 | couplage : 2 pour le banc, 2 pour la version définitive |
| A4 | Résistances 100 kΩ, 15 kΩ et 10 kΩ | 4, 2 et 4 | le pont de VBIAS, Rb selon T0.4, et le Rs et le Rb du banc |
| A5 | Condensateurs 820 pF et 1,5 nF, C0G | 2 de chaque | Caa selon Rb |
| A6 | Condensateur 10 µF | 2 | découplage de VBIAS, banc et version définitive |
| A7 | Connecteur JST-SH 3 points, avec embase | 1 paire | séparé du connecteur vidéo |
| A8 | Câble blindé fin à 2 conducteurs, ou fil Kynar à torsader | 30 cm | |
| A9 | Module MAX98357A | 1 | |
| A10 | Haut-parleur de Game Boy | 1 | pièce de rechange, ou celui de la console |
| A11 | Plaquette à trous | 1 | étage d'entrée, côté Pico |
| A12 | Plaque d'essai et fils de liaison | 1 | le banc de la phase 0 |
| T1 | Multimètre | 1 | déjà là |
| T2 | Écouteurs ou casque | 1 | pour écouter la console ; rien d'autre ne se branche sur sa prise casque |
| B1 | Module ADC I2S PCM1808 | 0 ou 1 | seulement si T3.1 résiste aux autres remèdes |
| B2 | Amplificateur opérationnel MCP6002 | 0 ou 1 | seulement si le signal est trop faible (T0.4) |
| B3 | Référence de tension LM4040, 3,0 V | 0 ou 1 | remède 2 du §2.3 |

---

## 12. Risques et parades

| Risque | Gravité | Parade |
|---|---|---|
| La radio s'entend dans le son | 🟡 plausible | T3.1, puis les remèdes du §2.3 dans l'ordre ; l'ADC externe en dernier recours |
| Une entrée de l'ADC portée au-delà de 3,3 V | 🔴 | couplage capacitif, diviseur, et Rs qui limite le courant ([`prise-son-mgb.md`](prise-son-mgb.md) §3) |
| Le manchon du casque relié à une masse | 🔴 | on n'enregistre jamais la prise casque ; références prises aux points de prise |
| Arracher une pastille, une patte | 🔴 | colle dès la sortie des fils, connecteur débrochable, fil souple |
| Le son de la console change | 🟡 | Rs de 10 kΩ ; T0.5 avant soudure, T1.2 Pico alimenté et éteint |
| La console bourdonne d'elle-même | 🟡 | T0.4 le mesure d'abord ; on le documente, on ne le corrige pas |
| La vidéo se dégrade quand le son s'ajoute | 🟡 | T3.3 dos à dos ; repli sur un second Pico |
| Les clients du réseau de l'écran ne se voient pas | 🟡 | T4.1 en tête de la phase 4 |
| Le réseau de l'écran est plein | 🟢 | 4 clients au plus : un seul Pico côté console en libère une place |
| Dérive des horloges : un craquement toutes les quelques minutes | 🟢 certaine, et connue | rééchantillonnage piloté par le tampon (§6), T5.2 |
| Griller le haut-parleur de Game Boy | 🟡 | gain minimal, plafond logiciel, T5.1 |
| L'horloge pixel couplée dans le câble audio | 🟡 | câble et connecteur séparés, masse dédiée, filtre anti-repliement |

---

## 13. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 11/10/2026 | Module ouvert, distinct de capture et d'écran | Même découpage que l'image : une source, un récepteur, un protocole entre les deux. Chaque moitié s'éprouve seule |
| 11/10/2026 | Capture du signal analogique, pas des écritures dans les registres du son | Les registres sont internes au processeur ; le signal analogique est le son réel |
| 11/10/2026 | Prise proposée à l'entrée du potentiomètre de volume, d'après le schéma de Gekkio | En amont de la molette et de l'amplificateur, sans continu, processeur protégé derrière 1 µF et 510 Ω. À confirmer par la phase 0 |
| 11/10/2026 | Pico de banc pour les phases 0 à 2 ; la phase 3 décide du Pico définitif | Le calcul et le débit tiennent sur un Pico ; le bruit de la radio ne se calcule pas, et un second Pico ne le lèverait pas |
| 11/10/2026 | Suréchantillonnage à 192 kHz, sortie à 48 kHz | Les harmoniques des ondes carrées se replieraient dans la bande audible à 48 kHz direct |
| 11/10/2026 | **La phase 0 se mesure avec un Pico de banc**, pas avec une interface audio | Rien à acheter, aucune connaissance en mesure audio : le firmware `mesure` et le script `mesure_son.py` donnent le verdict en clair, et le même montage resert aux phases 1 à 3. Le banc a un gain de 0,5 : il lit jusqu'à 6,6 V crête-à-crête sans rien savoir de la console d'avance |
| 11/10/2026 | **Gauche et droite : non bloquant**, le schéma fait foi d'ici là | Le haut-parleur de Game Boy est mono ; une inversion se corrige dans le firmware. Pokémon Rouge, vérifié dans son désassemblage, n'a pas de son d'un seul côté exploitable : le script `cotes` cherche ces moments en jouant |
