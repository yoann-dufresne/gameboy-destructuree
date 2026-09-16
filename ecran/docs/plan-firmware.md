# Module ÉCRAN — plan de réalisation

*Sous-projet « écran » du Game Boy Pocket déstructuré.*
Version 1 — 16/09/2026

---

## 0. Décisions figées

| Sujet | Décision | Réversible ? |
|---|---|---|
| **Cible immédiate** | Afficheur **réseau générique** — l'image vient de trames UDP, pas d'une Game Boy | — |
| **Géométrie finale** | **3 × 3 dalles** de 64×64 ⇒ **192 × 192 px** | non (dicté par les 160×144 de la GB) |
| **Découpage** | **3 chaînes de 3 dalles**, une par rangée horizontale | oui, en phase 5 |
| **Contrôleurs** | **3 × Raspberry Pi Pico 2 W**, un par rangée | oui, en phase 5 |
| **Langage** | **C99** | non |
| **Couche basse** | **pico-sdk 2.x, bare-metal**, PIO + DMA, 2 cœurs | non |
| **RTOS** | **aucun** | oui, si la v2 se charge en fonctionnalités |
| **Synchronisation** | **fil matériel** entre les 3 nœuds | — |
| **Outil émetteur** | **Python** côté PC | oui |

> 🔑 **Le point qui structure tout** : la Game Boy fait 160×144, la grille fait 192×192.
> **L'image rentre en 1:1.** Aucune mise à l'échelle, aucune perte de qualité — et 16 px de
> marge horizontale / 24 px verticale disponibles pour un cadre (batterie, titre, rien).
> La Game Boy devient un **client du protocole**, pas la raison d'être du firmware.

---

## 1. Objectif et périmètre

**Ce qu'on construit maintenant :** un afficheur qui reçoit des trames en UDP et les affiche.
Rien de spécifique à la Game Boy. La source est un script Python sur un PC : image, vidéo,
mire, flux temps réel.

**Ce qu'on construit plus tard :** le sniffer LCD de la Game Boy émet dans *le même protocole*,
en format indexé 2 bits. Côté écran, rien à écrire.

**Hors périmètre :** le module SOURCE (sniffer), le son, les boutons de la console. Voir
`../Spec_Video_Sniffer_et_Matrice_LED.md`.

---

## 2. Architecture matérielle

### 2.1 Géométrie

```
        192 px  (3 dalles × 64)
    ┌──────┬──────┬──────┐
    │ 0,0  │ 1,0  │ 2,0  │  ← nœud 0, chaîne de 3   64 px
    ├──────┼──────┼──────┤
    │ 0,1  │ 1,1  │ 2,1  │  ← nœud 1, chaîne de 3   64 px   192 px
    ├──────┼──────┼──────┤
    │ 0,2  │ 1,2  │ 2,2  │  ← nœud 2, chaîne de 3   64 px
    └──────┴──────┴──────┘
     576 mm de côté, ~4,5 kg
```

Chaque nœud pilote **une rangée** = une chaîne de 3 dalles = **192 × 64 px logiques**.
Découpage trivial de l'image : pas de serpentin, pas de rotation des rangées paires.

### 2.2 Pourquoi 3 chaînes de 3

Une chaîne de N dalles forme un seul long registre à décalage :
`trame = 32 lignes × 8 plans × (N × 64) coups d'horloge = 16 384 × N`.

| N | Coups/trame | @ 20 MHz | @ 15 MHz | RAM (double tampon) |
|---|---|---|---|---|
| 1 | 16 384 | 1 220 Hz | 915 Hz | 32 ko |
| **3** | **49 152** | **407 Hz** | **305 Hz** | **98 ko** |
| 9 | 147 456 | 136 Hz | 102 Hz | 295 ko / 520 |

*(Le modèle prédit ~1220 Hz pour une dalle seule ; JuPfu/hub75 en mesure 1285 Hz en 8 bits.
Le modèle tient.)*

| | 1 chaîne de 9 | **3 chaînes de 3** | 9 chaînes de 1 |
|---|---|---|---|
| Règle métier ≤ 4 dalles/port | ❌ | ✅ | ✅ |
| Rafraîchissement | 136 Hz | **407 Hz** | 1 220 Hz |
| RAM par contrôleur | 295 ko | **98 ko** | 32 ko |
| Fils de synchro | 0 | **2** | 8 |
| Coût contrôleurs | ~7 € | **~21 €** | ~63 € |
| Configs WiFi à gérer | 1 | **3** | 9 |

Les trois raisons d'écarter la chaîne de 9, dans l'ordre :

1. **Intégrité du signal.** Chaque dalle ré-attaque le signal vers sa sortie, donc la longueur
   de nappe n'est pas le problème (25 cm par saut). Ce qui s'accumule, c'est le **retard de
   propagation et la gigue à travers 9 étages de tampons en série** : la marge de setup/hold
   s'épuise en bout de chaîne. Règle de métier chez les installateurs : **≤ 4 dalles par port**.
   Le symptôme d'un dépassement n'est pas franc — « les deux dernières dalles scintillent
   parfois » — c'est le pire type de panne à diagnostiquer.
2. **Luminosité utile.** À N=9, les plans de bits de poids faible deviennent limités par le
   temps de décalage (28,8 µs pour 576 px) et non par leur durée d'affichage : le panneau passe
   l'essentiel du temps éteint.
3. **RAM.** 295 ko sur 520 ko avant lwIP et ses tampons.

> ℹ️ **Pas retenu mais valide** : un contrôleur unique **RP2350B** (48 GPIO) avec 3 ports
> HUB75 sur carte de dérivation. Supprime la synchro, mais impose de fabriquer une carte et
> de quitter le Pico 2 W. En mutualisant A–E, CLK, LAT et /OE (identiques pour les 3 chaînes),
> 3 ports coûtent 26 GPIO — donc *possible* sur Pico 2 W, mais sans une broche de rab.
> À réexaminer en phase 5 si la synchro logicielle déçoit.

### 2.3 Brochage Pico 2 W ↔ HUB75

Identique pour les 3 nœuds. C'est **verbatim** celui de `pico-examples/pio/hub75` — donc du
code de référence qui tourne sans modification.

| GPIO | Signal HUB75 | Broche IDC | Rôle |
|---|---|---|---|
| GP0–GP5 | R1 G1 B1 R2 G2 B2 | 1 2 3 5 6 7 | données, 6 bits contigus (`out pins, 6`) |
| GP6–GP10 | A B C D E | 9 10 11 12 **8** | adresse ligne, 5 bits contigus |
| GP11 | CLK | 13 | horloge de décalage |
| GP12 | LAT / STB | 14 | verrou de ligne |
| GP13 | /OE | 15 | extinction, actif bas — porte la modulation BCM |
| GND | GND | **4 et 16** | relier **les deux** |

**Sérigraphie de la dalle Seengreat** (vérifiée sur photo) : `LA…LE` = A…E, et **`CE` = /OE**.
`E` est bien sur la **broche 8**, la broche 4 est une masse — le piège classique des panneaux
64×64 ne s'applique pas à ce modèle. À revérifier au multimètre malgré tout.

**Broches restantes** : GP14–GP22, GP26–GP28.

| GPIO | Usage |
|---|---|
| GP14, GP15 | straps d'identité de nœud (0–2), **en pull-up** ⚠️ errata RP2350-E9 |
| GP16 | fil de synchro (entrée sur les esclaves, sortie sur le maître) |
| GP17 | sortie de mesure : bascule au flip de tampon (phase 4) |
| GP18 | sortie de mesure : bascule à la réception du 1er octet (phase 4) |
| GP26 (ADC0) | potentiomètre de luminosité |
| GP19–GP22, GP27, GP28 | libres |

> ⚠️ GP0/GP1 sont pris par R1/G1 : **la console série par défaut est morte.**
> Activer **USB CDC** (`pico_enable_stdio_usb`) dès la phase 0.

> ℹ️ Sur Pico 2 W, la CYW43439 occupe GP23–GP25 et GP29, **non sortis** sur le connecteur :
> aucun conflit. Conséquence : la LED embarquée n'est pas sur GP25, elle passe par
> `cyw43_arch_gpio_put()`.

### 2.4 Niveaux 3,3 V → 5 V

Commencer **en direct**, nappe courte. Le tampon 74HC245 5 V en entrée de dalle a un
V_IH ≈ 3,15 V : le 3,3 V passe, de justesse. Si pixels fantômes, scintillement ou colonnes
parasites → **2 × 74AHCT245** par chaîne, alimentés en 5 V, `DIR` au +5 V, `/OE` à la masse.

### 2.5 Alimentation

| | |
|---|---|
| Par dalle | 5 V, **4 A crête** (embase VH 3.96, sérigraphie `+ + - -`) |
| Par rangée (3 dalles) | **12 A** |
| Total 3×3 | **36 A ≈ 180 W crête** |

- Préférer **3 alimentations 5 V / 15 A**, une par rangée, plutôt qu'une seule 40 A : ça isole
  les pannes, ça raccourcit les câbles, et une rangée peut être testée seule.
- Chaque dalle reçoit **sa propre paire de fils** depuis un bus barre. 18 AWG minimum.
- À 12 A par rangée, la chute de tension n'est pas une abstraction : viser **5,0 V mesurés à
  l'embase de la dalle la plus éloignée**, sous charge.
- Pico alimenté par USB, **séparément**. Masses communes obligatoires.
- Ordre de branchement : **masses d'abord**, puis signaux, puis alimentations.

### 2.6 Synchronisation des 3 nœuds

La gigue WiFi (5–20 ms) est largement visible comme un **déchirement entre rangées**. Un fil
la supprime définitivement.

- Nœud 0 = **maître** (strap), nœuds 1 et 2 = esclaves.
- GP16 : sortie sur le maître, entrée sur les esclaves. Guirlande + masse commune.
- Le maître impulse quand *sa* trame est prête ; tous basculent leur tampon sur le front.
- Si un esclave n'a pas sa trame complète au front : il **conserve la précédente** (pas de
  demi-trame affichée) et incrémente un compteur de retard.

---

## 3. Pile logicielle

### 3.1 Le choix

**C99, pico-sdk 2.x, bare-metal, deux cœurs, zéro RTOS.**

Pas « parce que le C est plus rapide » : parce que c'est la seule pile où le PIO, les DMA
chaînés et l'affinité de cœur sont des primitives de premier ordre, et parce que le code de
référence qui tourne déjà sur cette dalle est écrit dedans.

- **Cœur 0** : WiFi, réception UDP, décodage de format, écriture dans le tampon arrière.
- **Cœur 1** : entretien du rendu HUB75 (en pratique quasi inactif, tout est en PIO/DMA).

**Outillage** : CMake + pico-sdk 2.x (`PICO_BOARD=pico2_w`), `picotool`, et — vraiment
recommandé à partir de la phase 2 — une **Debug Probe** en SWD. Sans elle, le seul moyen
d'observer est `printf`, et `printf` *change* la latence qu'on essaie de mesurer.

### 3.2 Ce qu'on écarte, et pourquoi

| Pile | Verdict |
|---|---|
| **CircuitPython** | ❌ ~50–100 ms, lib `rgbmatrix` figée, GC imprévisible. Conservé comme **outil de diagnostic** en phase 0 |
| **Arduino** (core earlephilhower + Protomatter) | ❌ Support Protomatter sur RP2350 en zone active ; on se bat contre l'abstraction dès qu'on veut du BCM custom |
| **FreeRTOS** | ⚠️ Fonctionne (port `RP2350_ARM_NTZ`, SMP, `pico_cyw43_arch_lwip_sys_freertos`), mais le timing dur est déjà en PIO/DMA : un ordonnanceur n'ajoute que de la **gigue**. Deux activités, un cœur chacune. À réévaluer si la v2 gagne UI web / OTA / menus. Piège si adopté : sous SMP, `cyw43_arch_init()` doit être appelé **depuis une tâche** |
| **Rust / Embassy** | 🔶 `embassy-rp` couvre RP235x, crate `cyw43` mature pour Pico 2 W. Mais **aucun driver HUB75 64×64 en 1/32** : `hub75-pio` est en v0.1.0 (2022), expérimental, RP2040, 64×32 en 1:16. Il faudrait porter le programme PIO soi-même. **v2 légitime**, une fois le brochage et la logique BCM validés |

---

## 4. Protocole `PXL1`

Conçu **tuile-conscient et multi-format dès la v1** : le remanier une fois 3 nœuds déployés
serait pénible. C'est la seule décision de ce document qui coûterait cher à prendre en retard.

### 4.1 En-tête (12 octets, UDP, charge utile ≤ 1400 o pour éviter la fragmentation IP)

```
 offset  taille  champ      description
   0       4     magic      'PXL1'
   4       1     type       0=FRAME  1=CTRL  2=PING
   5       1     node_id    nœud destinataire (0..N-1)
   6       2     frame_id   numéro de trame, LE — réassemblage et synchro
   8       1     format     0=RGB888 1=RGB565 2=IDX2 3=IDX4 4=IDX8 5=RLE8
   9       1     flags      bit0 = dernière tranche de la trame
  10       2     offset     position (octets) de la tranche dans la charge du nœud
```

Chaque nœud connaît **son rectangle** (`x0, y0, w, h`) par configuration ; l'émetteur n'envoie
à chaque nœud que les pixels de son rectangle, en balayage ligne par ligne. Le protocole est
donc indépendant du nombre de nœuds : 1, 3 ou 9, seul le fichier de layout de l'émetteur change.

**Paquets CTRL** : palette (256×3), luminosité, mire de test, mise en veille, élection du
maître de synchro. Séparés des pixels pour ne rien mettre d'autre dans le chemin critique.

### 4.2 Budget de débit — ce que le format autorise

Trame complète 192×192, à 60 Hz, répartie sur 3 nœuds :

| Format | Octets/trame | Total | **Par nœud** | Verdict |
|---|---|---|---|---|
| RGB888 | 110 592 | 53 Mbit/s | 17,7 | ❌ à 60 Hz (🔶 à 30 Hz) |
| RGB565 | 73 728 | 35 Mbit/s | 11,8 | 🔶 limite |
| IDX8 + palette | 36 864 | 17,7 Mbit/s | 5,9 | ✅ |
| IDX4 | 18 432 | 8,8 Mbit/s | 2,9 | ✅ |
| **IDX2 (Game Boy natif)** | **9 216** | **4,4 Mbit/s** | **1,5** | ✅✅ |

Le débit UDP réellement soutenu par un Pico 2 W est de l'ordre de **10–20 Mbit/s** — chiffre
à **mesurer en phase 4**, c'est exactement le genre de valeur qu'il ne faut pas croire sur
parole. En mono-dalle (phases 2–3), RGB888 à 60 Hz ne pèse que 5,9 Mbit/s : aucun souci.

> ℹ️ Nuance utile : le **temps d'air total** est le même quel que soit le nombre de nœuds
> (la trame ne traverse l'air qu'une fois, chaque nœud ne recevant que sa part). Ce que le
> découpage améliore, c'est le **débit à ingérer par nœud** — et c'est lui qui est limitant.

---

## 5. Les phases

### Phase 0 — « ça s'allume » · 1 dalle · ½ jour

**Langage / couche :** C, `pico-examples/pio/hub75`, tel quel.

Prouver le câblage et l'alimentation. **Aucune ligne de code écrite.**

1. `WIDTH 64` / `HEIGHT 64` dans `hub75.c`.
2. Ne **rien** toucher aux `#define` de broches : ils correspondent déjà au brochage §2.3.
3. Compiler pour `pico2_w`, flasher.

**Critère de sortie :** dégradé de test stable, sans colonne parasite, sans scintillement
quand on bouge la nappe.

**Si rien ne s'allume — ordre de diagnostic :** ① les deux GND reliés, continuité vérifiée
② 5,0 V à l'embase VH **sous charge** ③ sens de la nappe, `E` sur broche 8 ④ si le doute porte
sur le *toolchain* plutôt que le matériel : CircuitPython ≥ 9 pour Pico 2 W (⚠️ **pas** le
`.uf2` 7.1.1 de l'archive Seengreat, il est RP2040) + le `main.py` de
`docs/seengreat-rgb-matrix-p3-64x64/demo-code/extrait/` avec les broches réécrites en GP0…GP13.
Glisser-déposer, ça tourne en 10 minutes sans compilateur et ça isole matériel vs. build.

---

### Phase 1 — driver HUB75, écrit chaîne-conscient · 2 à 4 jours

**Langage / couche :** C99 + assembleur PIO. Bare-metal, `pico_multicore`, DMA chaînés.

Un module qui prend un framebuffer en RAM et l'affiche à haute fréquence sans consommer de CPU.
**La géométrie est paramétrée dès maintenant** — c'est ce qui rendra la phase 5 quasi gratuite.

```c
// include/config.h
#define PANEL_W       64
#define PANEL_H       64
#define CHAIN_LEN     1     // → 3 en phase 5
#define NODE_COUNT    1     // → 3 en phase 5
#define BCM_PLANES    8
#define NODE_ID_PINS  {14,15}        // straps, pull-up ⚠️ E9
```

L'API ne parle que du rectangle **du nœud** : `hub75_backbuffer()` rend un buffer
`CHAIN_LEN*64 × 64`, `hub75_flip()` bascule. Le driver ignore qu'il existe d'autres nœuds.

**Décisions techniques figées ici :**
- **BCM 8 plans**, pas de PWM.
- **Double tampon strict**, bascule en fin de trame. Jamais de triple tampon : chaque tampon
  en plus est une trame de latence en plus.
- Cœur 1 dédié à l'entretien du rendu.
- Horloge système **200 MHz** (overclock modéré, sans surtension).
- Option anti-gigue : `pico_set_binary_type(... copy_to_ram)` supprime la gigue du cache XIP.

**Base de code :** vendoriser [JuPfu/hub75](https://github.com/JuPfu/hub75) (MIT, testé
RP2350A **et** B, BCM 8/10 bits, courbe CIE 1931, topologies serpentin et raster, flag
`HUB75_MULTICORE`) dans `firmware/vendor/` avec un `PROVENANCE.txt`, comme la doc constructeur.
Alternative : [dgrantpete/Pi-Pico-Hub75-Driver](https://github.com/dgrantpete/Pi-Pico-Hub75-Driver).

**Critère de sortie :** mire fixe (dégradé + damier 1 px) depuis un buffer statique ;
**≥ 150 Hz mesurés à l'oscilloscope sur /OE** ; une boucle `while(1)` saturant le cœur 0 ne
dégrade pas l'image ; photo à 1/250 s sans bandes.

**Piège :** le damier 1 px révèle le *ghosting* (fuite de la ligne précédente). S'il apparaît,
c'est le temps d'extinction /OE avant changement d'adresse qu'il faut allonger — pas le câblage.

---

### Phase 2 — protocole et réception · 2 à 3 jours

**Langage / couche :** C, `pico_cyw43_arch_lwip_threadsafe_background` (`NO_SYS=1`),
**API raw lwIP** (`udp_recv`, callback). Pas l'API sockets : copies mémoire et réveils inutiles.

Implémenter `PXL1` §4 : réassemblage par `frame_id`, décodage de format, écriture directe dans
le tampon arrière.

**Ce qui fait la latence, par ordre d'importance :**

| # | Levier | Gain |
|---|---|---|
| 1 | `cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM)` | **10–100 ms** 🔴 |
| 2 | API raw lwIP plutôt que sockets | 1–3 ms |
| 3 | Traiter **chaque tranche à l'arrivée** au lieu d'attendre la dernière | ~8 ms |
| 4 | IP statiques, pas de DHCP ni mDNS en régime établi | variable |
| 5 | `PBUF_POOL_SIZE` / `MEM_SIZE` dimensionnés dans `lwipopts.h` | évite des pertes silencieuses |

**Critère de sortie :** RGB888 à 60 Hz sur une dalle (5,9 Mbit/s), **< 0,1 % de perte sur
10 minutes**, compteurs de perte/désordre sur USB CDC.

**Piège :** ne pas tester contre le sniffer — il n'existe pas. L'émetteur Python de la phase 3
est le banc d'essai, et il permet d'injecter pertes et désordre volontairement.

---

### Phase 3 — l'émetteur PC · 1 à 2 jours

**Langage :** **Python** (`socket` + `numpy` + `Pillow`), dans `ecran/tools/pixelpush/`.

C'est la phase qui rend le projet visible : on lance une commande, l'écran affiche une vidéo.

- bibliothèque `send_frame(np.ndarray)` : découpe par nœud, encode, expédie ;
- sources : image fixe, GIF/vidéo (OpenCV), mires, bruit de Perlin, horloge, capture d'une
  fenêtre du PC ;
- **fichier de layout** décrivant les nœuds (`node_id`, `x0`, `y0`, `w`, `h`, IP) — écrit
  maintenant pour 1 nœud, utilisé tel quel en phase 5 pour 3 ;
- mode `--inject-loss` / `--inject-reorder` pour éprouver la phase 2.

Python tient largement le débit. Si une source à très faible latence devient nécessaire, ce
module seul sera réécrit, sans toucher au firmware.

**Critère de sortie :** une vidéo fluide sur la dalle, pilotée depuis le PC.

---

### Phase 4 — mesure · 2 jours

**Méthode :** GP18 bascule dans le callback `udp_recv` au premier octet, GP17 bascule au flip
de tampon. Analyseur logique sur les deux : l'écart se lit directement. **Mesurer, pas supposer.**

**Les trois chiffres qui conditionnent la phase 5 :**

1. **Fréquence d'horloge pixel maximale stable** — sur 1 dalle, puis sur une chaîne de 3.
   C'est le chiffre dont dépend tout le reste et que personne ne peut donner sur le papier.
2. **Rafraîchissement effectif et luminosité utile à `CHAIN_LEN=3`.**
3. **Débit UDP réellement soutenu** par un Pico 2 W — décide du format retenu pour le 3×3.

**Finition :** potentiomètre de luminosité GP26, reconnexion WiFi automatique, mire « pas de
signal » après 1 s de silence, réglages persistés en flash.

**Budget de latence visé** (à confirmer par la mesure) :

| Poste | Ordre de grandeur |
|---|---|
| WiFi UDP, 1 saut, power save coupé | 1–3 ms |
| Réassemblage (tranches traitées à l'arrivée) | ~1 ms |
| Décodage de format | < 0,5 ms |
| Attente du rafraîchissement BCM | 2–7 ms |
| **Total ajouté** | **5–12 ms** |

---

### Phase 5 — passage à 3×3 · 3 à 5 jours

**Firmware :** `CHAIN_LEN 3`, `NODE_COUNT 3`, lecture du `node_id` sur les straps GP14/GP15,
filtrage sur `node_id` à la réception, bascule de tampon sur le **fil de synchro** GP16 au lieu
de `flags.bit0`. Une centaine de lignes — parce que la phase 1 a été écrite chaîne-consciente.

**Le vrai travail est matériel :**

| Chantier | Détail |
|---|---|
| Alimentation | 3 × 5 V/15 A, une par rangée. Bus barres, une paire de fils **par dalle**, 18 AWG mini, fusible par rangée |
| Mécanique | 576 × 576 mm, ~4,5 kg. Cadre alu, fixations M3 sur les dalles |
| Synchro | 1 fil + masse en guirlande entre les 3 Pico |
| Câblage signal | 3 × (embase 2×8 mâle sur perfboard + nappes) — les dalles ont des embases **mâles** et les nappes fournies deux prises **femelles** : il faut l'embase intermédiaire |
| Réseau | 3 IP statiques, le fichier de layout de la phase 3 passe de 1 à 3 entrées |

**Critère de sortie :** vidéo plein cadre 192×192, **sans déchirement entre rangées**, sans
ligne de jonction plus claire ou plus sombre.

**Porte de sortie :** si la mesure de phase 4 donne ≥ 25 MHz stable sur une chaîne de 3, une
chaîne de 9 sur un contrôleur unique redevient discutable — mais en connaissance de cause.
Inversement, si la synchro à 3 nœuds déçoit, basculer vers un contrôleur unique RP2350B (§2.2).

---

### Phase 6 — la Game Boy · plus tard

Le sniffer émet dans le **même protocole**, en `IDX2`, 160×144 centré dans 192×192. Palette DMG
appliquée côté afficheur, changeable à chaud par paquet CTRL.

**Côté module écran : rien à écrire.** C'est le bénéfice d'avoir traité la Game Boy comme un
client du protocole plutôt que comme sa raison d'être.

---

## 6. Récapitulatif

| Phase | Langage | Couche | Sortie | Effort |
|---|---|---|---|---|
| 0 · Ça s'allume | C (code d'autrui) | pico-sdk | câblage validé | ½ j |
| 1 · Driver | C + PIO asm | bare-metal, 2 cœurs, DMA | ≥ 150 Hz, 0 % CPU | 2–4 j |
| 2 · Protocole + réception | C | lwIP raw, cyw43 | < 0,1 % de perte | 2–3 j |
| 3 · Émetteur PC | Python | — | **vidéo à l'écran** 🎉 | 1–2 j |
| 4 · Mesure | C | — | latence + débit chiffrés | 2 j |
| 5 · Passage à 3×3 | C + mécanique | idem | 192×192 sans déchirure | 3–5 j |
| 6 · Game Boy | — | — | 1:1, rien à écrire | — |

---

## 7. Nomenclature du 3×3

| Réf | Désignation | Qté | Note |
|---|---|---|---|
| E1 | Seengreat RGB Matrix P3.0-64x64 | **9** | ~30 €/pièce |
| E2 | Raspberry Pi Pico 2 W | **3** | un par rangée |
| E3 | Alimentation 5 V / 15 A | **3** | une par rangée |
| E4 | Embase 2×8 mâle 2,54 + perfboard | 3 | interface Pico ↔ nappe |
| E5 | Nappe IDC 16 pts | 9 | fournies avec les dalles (1 chacune) |
| E6 | Câble d'alim VH 3.96 | 9 | fournis avec les dalles |
| E7 | Fil de synchro + masse | 1 | guirlande 3 nœuds |
| E8 | Bus barres, fil 18 AWG, fusibles | — | distribution 12 A/rangée |
| E9 | Cadre alu + visserie M3 | 1 | 576 × 576 mm |
| E10 | Potentiomètre 10 kΩ | 3 | luminosité, GP26 |
| E11 | 74AHCT245 | 0 ou 6 | **seulement si** ghosting constaté |
| E12 | Raspberry Pi Debug Probe | 1 | fortement recommandé dès la phase 2 |

Ordre de grandeur : **400–450 €**.

---

## 8. Arborescence cible

```
ecran/
├── README.md
├── docs/
│   ├── plan-firmware.md                    ← ce document
│   ├── protocole-pxl1.md                   ← à écrire (phase 2)
│   └── seengreat-rgb-matrix-p3-64x64/      ← doc constructeur archivée
├── firmware/
│   ├── CMakeLists.txt
│   ├── include/config.h                    ← brochage, géométrie, straps
│   ├── src/main.c
│   ├── src/hub75/{hub75.pio,hub75.c,hub75.h}
│   ├── src/net/{pxl1.c,pxl1.h,lwipopts.h}
│   └── vendor/hub75-jupfu/                 ← + PROVENANCE.txt
└── tools/
    └── pixelpush/                          ← émetteur Python
        ├── pixelpush.py
        └── layout-3x3.toml
```

---

## 9. Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 16/09/2026 | Afficheur réseau générique d'abord, Game Boy ensuite | L'écran n'est pas lié à une console ; la GB devient un client du protocole |
| 16/09/2026 | Grille 3×3 = 192×192 | Permet le 160×144 en **1:1** — supprime toute mise à l'échelle |
| 16/09/2026 | C bare-metal, pas de FreeRTOS, pas de Rust en v1 | Timing dur déjà en PIO/DMA ; deux activités, un cœur chacune ; pas de driver HUB75 64×64 en Rust |
| 16/09/2026 | Protocole tuile-conscient et multi-format dès la v1 | Seule décision coûteuse à prendre en retard |
| 16/09/2026 | **3 chaînes de 3, 3 × Pico 2 W** | Règle ≤ 4 dalles/port, 407 Hz vs 136 Hz, 98 ko vs 295 ko de RAM, 2 fils de synchro seulement |
