# Module CAPTURE — marche à suivre détaillée

*Le « comment », étape par étape. Le « pourquoi » est dans
[`plan-firmware.md`](plan-firmware.md), qui reste la référence des décisions.*

Version 1 — 22/09/2026

**Conventions de ce document**

| Marque | Sens |
|---|---|
| 🔬 | une **mesure** : la valeur n'est pas connue, elle doit être relevée |
| 🔴 | **irréversible** : une erreur coûte une pastille, une puce, ou la console |
| ⚠️ | piège identifié, souvent payé par quelqu'un d'autre avant nous |
| ✅ | critère de sortie : tant qu'il n'est pas atteint, on ne passe pas à la suite |
| 🔑 | le point qui décide de tout le reste dans la section |

---

## Table des matières

- [Ordre de travail : le chemin critique n'est pas la soudure](#ordre)
- [A. Comprendre le signal](#a)
- [B. Phase 0 — identifier les signaux, sans rien souder](#b)
- [C. Phase 1 — la prise de signaux](#c)
- [D. Phase 2 — la capture](#d)
- [E. Phase 3 — `IDX2` de bout en bout](#e)
- [F. Phase 4 — mesurer](#f)
- [G. Phase 5 — intégrer](#g)
- [H. Ce qui peut encore invalider ce plan](#h)

---

<a id="ordre"></a>
## Ordre de travail : le chemin critique n'est pas la soudure

L'enchaînement naturel serait « j'ouvre la console, je soude, j'écris le firmware ». C'est le
mauvais ordre : il place l'étape **irréversible** avant les étapes qui pourraient encore
changer d'avis.

Le bon ordre place la soudure le plus tard possible, et sature l'attente de pièces avec du
travail qui ne demande pas la console :

```
  SEMAINE 1        SEMAINE 2              SEMAINE 3
  ─────────        ─────────              ─────────
  Phase 0 ────────────────────────────►  Phase 1 ──► Phase 2 ──► Phase 4
  (mesure,                          🔴  (soudure)   (capture)   (mesure)
   décide quoi acheter)                     ▲           ▲
        │                                   │           │
        └── commande des pièces ────────────┘           │
                                                        │
  Phase 3a + 3b ────────────────────────────────────────┘
  (IDX2 côté écran + pixelpush : AUCUNE console nécessaire)
```

Trois conséquences pratiques :

1. **La phase 0 décide ce qu'on achète** (§3.3 du plan : le tampon est-il obligatoire ?).
   La faire en premier évite de commander deux fois.
2. **Les phases 3a et 3b ne dépendent de rien.** `IDX2` côté écran et `--format idx2` dans
   `pixelpush` peuvent être écrits et éprouvés **aujourd'hui**, avec la grille et un PC. Ça
   retire une inconnue de la phase 3 et ça donne un banc de test qui ne dépend pas du sniffer.
3. **La phase 2 peut démarrer avant la phase 1** si on a enregistré une capture de la
   phase 0 assez profonde pour la rejouer (§D.11).

---

<a id="a"></a>
## A. Comprendre le signal

### A.1 Comment la Game Boy dessine son écran

Il n'y a **pas de contrôleur LCD** entre le CPU et le panneau. Le PPU sort les pixels en
direct, et le panneau les reçoit au fil de l'eau. Deux conséquences :

- **il n'y a pas de mémoire d'image à lire** : l'image ne passe qu'une fois, en temps réel.
  D'où la capture par PIO, et pas par interrogation ;
- **le timing est celui du CPU**, donc rigide et calculable.

Tout découle de l'horloge maître, **4,194304 MHz** :

```
  1 ligne  = 456 cycles  =  108,72 µs
  1 trame  = 154 lignes  =  70 224 cycles  =  16,743 ms   ⇒  59,727 Hz
             ├─ 144 lignes visibles  =  15,656 ms
             └─  10 lignes de VBlank =   1,087 ms
```

Dans une ligne visible, le PPU passe par trois modes :

| Mode | Durée | Ce qui se passe | CPG ? |
|---|---|---|---|
| 2 — balayage OAM | 80 cycles | choix des sprites de la ligne | non |
| 3 — transfert | **172 à 289 cycles** | les 160 pixels sortent | **oui** |
| 0 — HBlank | le reste (87 à 204) | rien | non |

> 🔑 **Le mode 3 est de durée variable** (sprites, fenêtre, décalage `SCX`), mais le nombre
> de pixels ne l'est pas : **toujours 160**. Le PPU sort un pixel par cycle quand sa file
> n'est pas bloquée, et les blocages allongent le mode 3.
>
> Donc : **la période minimale entre deux fronts de `CPG` est d'un cycle maître, soit 238 ns**,
> et les « creux » sont les blocages. C'est le **minimum** qui fixe le budget du PIO, pas la
> moyenne de 1,38 M impulsions/s — une moyenne ne dit rien de la contrainte.

À 150 MHz, la boucle PIO de 3 instructions dure **20 ns**. Marge : **12×**. C'est confortable,
et c'est la raison pour laquelle on ne monte pas `clk_sys`.

### A.2 Les cinq signaux, et ce qui les trahit

| Signal | Ce qu'il fait | Fréquence attendue | Sa signature |
|---|---|---|---|
| `LD0`, `LD1` | les 2 bits du pixel | — | **se taisent pendant la VBlank**, et ne bougent que pendant les salves |
| `CPG` | horloge pixel | 1,38 M/s en moyenne, **salves de 160** | la seule qui monte à plusieurs MHz |
| `CPL` | verrou de fin de ligne | **8,60 kHz** = 144 × 59,73 | **absente pendant la VBlank** — c'est ce qui la distingue de `CP` |
| `CP` / `CPV` | horloge de ligne | **9,20 kHz** = 154 × 59,73 | **présente pendant la VBlank** |
| `ST` / `S` | début de trame | **59,73 Hz** | une impulsion par trame |
| `FR` | inversion de polarité | **29,86 Hz** en niveau | **alterne** : rapport cyclique 50 %, pas une impulsion |

> ⚠️ `CPL` (8,60 kHz) et `CP` (9,20 kHz) ne diffèrent que de **6,5 %**. Un compteur de
> fréquence seul peut les confondre. **Ce qui les sépare sans ambiguïté, c'est la VBlank** :
> regarde 1,1 ms après la dernière ligne, l'une continue, l'autre s'arrête. Il faut donc une
> capture **corrélée** — d'où l'analyseur logique, et pas un multimètre en mode fréquence.

> ⚠️ `ST` et `FR` sont tous deux à l'échelle de la trame. `ST` est une **impulsion** (rapport
> cyclique très faible), `FR` est un **carré** à moitié fréquence. Les confondre donne une
> image qui n'est bonne qu'une trame sur deux — symptôme distinctif, voir §D.10.

### A.3 Ce qui reste inconnu et que la phase 0 doit trancher

| Inconnue | Pourquoi ça compte | Où c'est utilisé |
|---|---|---|
| 🔬 **VCC de la carte MGB** | décide si le tampon est obligatoire, et l'achat | §C.2 |
| 🔬 **Période minimale de `CPG`** | valide (ou non) la marge de 12× du PIO | §D.2 |
| 🔬 **Sur quel front `LD0/LD1` sont stables** | fixe le délai d'échantillonnage du PIO | §D.2 |
| 🔬 **Polarité : `00` est-il blanc ?** | décide l'ordre de la palette — **pas** du firmware | §E.6 |
| 🔬 **`CPL` est-il fiable ?** | sinon on bascule sur `CP` (GP5) et on compte 154 lignes | §D.5 |
| 🔬 **Emplacement physique des pastilles** | c'est le plan de soudure | §C.5 |

---

<a id="b"></a>
## B. Phase 0 — identifier les signaux, sans rien souder

**Durée : ½ à 1 journée. Rien d'irréversible, sauf ouvrir la console.**

### B.0 Ce qu'il faut avoir sous la main

| | |
|---|---|
| Tournevis **tri-wing Y1** (3,8 mm) | les vis de la coque MGB ne sont pas cruciformes |
| Analyseur logique ≥ 8 voies, **≥ 24 MS/s** | 24 MS/s donne ~5,7 points par période de `CPG` à 4,2 MHz — suffisant pour compter des fronts, pas pour juger un front. Validé : AZDelivery 8 CH 24 MHz (CY7C68013A / fx2lafw) |
| PulseView / `sigrok` | gratuit, et sait enregistrer un `.sr` qu'on pourra **dépouiller** (§B.3bis) puis **rejouer** (§D.11) |
| `numpy` côté PC | pour `tools/analyse_sr.py` |
| Multimètre | VCC, continuité |
| Pointe de touche fine, ou fil émaillé tacké provisoirement | sonder une pastille de ruban LCD à main levée ne marche pas |
| **Piles neuves** | et des piles usées pour le test du §B.7 |
| Une cartouche avec un écran **tout blanc** accessible | un menu, un écran-titre clair |

⚠️ **Alimente la console par ses piles, ou par une alimentation de labo à sortie flottante.**
Le risque est la **boucle de masse** : si le `−` de l'alimentation est relié à la terre, la
console se retrouve reliée à l'analyseur par deux chemins (le fil de masse, et la terre par le
PC), et la boucle invente des fronts.

La plupart des alimentations de labo ont une sortie **flottante**, avec une borne de terre
verte **séparée** des bornes `+` et `−`. **Vérifie-le** plutôt que de le supposer : ohmmètre
entre la borne `−` et la borne de terre, alimentation éteinte. Ouvert (> 1 MΩ) = flottante =
utilisable. Quelques ohms = reliée à la terre : reviens aux piles.

> Une alimentation flottante est même **préférable** aux piles pour la phase 0 : elle
> reproduit exactement le cas « piles usées » du §3.3, en réglant la tension au lieu
> d'attendre qu'elles se vident.

**Les limites de l'instrument, et ce qu'elles imposent.** Un analyseur à FX2LP n'a ni mémoire
ni trigger matériel : il **diffuse** en continu sur l'USB, à 24 Mo/s quand on lui demande
24 MS/s sur 8 voies. C'est la limite pratique de l'USB 2.0, et des échantillons peuvent être
perdus selon le contrôleur de la machine. Trois conséquences :

- **captures courtes à 24 MS/s** — 4 trames (67 ms) suffisent largement ;
- **port USB direct**, sans hub ;
- **tout ce qui n'est pas l'horloge pixel se capture à 4 MS/s**, où le débit tombe à 4 Mo/s
  et où une capture de 500 ms passe sans risque. D'où les deux captures du §B.3.

Ses seuils d'entrée sont **fixes** (V_IH = 1,4 V, V_IL = 0,8 V) et il accepte 0 à 5,25 V : sur
un bus à 3 V, et même à 2,4 V piles usées, il lit sans problème et sans risque pour lui.

### B.1 Ouvrir, sans casser

1. Retirer les piles.
2. Six vis tri-wing au dos.
3. Séparer les coques **par le bas** ; le haut est retenu par des clips.
4. ⚠️ La carte est reliée à l'écran par un **ruban souple soudé** (pas un connecteur sur la
   MGB) : ne pas tirer sur les coques.
5. Remettre les piles, vérifier que la console démarre coque ouverte.

### B.2 Les quatre règles de sondage

1. **La masse de l'analyseur se branche en premier et se débranche en dernier.** Sur une
   masse franche de la carte (blindage, plan de masse, borne − des piles). Et **le plus court
   possible** : ces analyseurs n'ont qu'un ou deux fils de masse, et à 4 MHz sur du fil
   dupont long, le signal sonne — un rebond se compte comme un front de plus.
2. **Une pointe à la fois.** Un ripage entre deux pastilles voisines met deux sorties du CPU
   en court-circuit.
3. **Ne jamais sonder avec la console éteinte puis l'allumer** avec des pointes posées : on ne
   voit pas un ripage sur un circuit éteint.
4. 🔑 **La sonde est elle-même une charge — regarde l'écran d'origine pendant que tu sondes.**
   Ces analyseurs n'ont pas de tampon d'entrée : la broche du FX2LP (~10 pF) plus le fil
   dupont (30–50 pF) atterrissent directement sur la sortie du PPU. C'est exactement la
   question que la phase 1 devait traiter avec le 74LVC244, abandonné depuis (§C.2).

   Si l'image d'origine se dégrade visiblement pendant que tu sondes `CPG`, tu viens
   d'apprendre **gratuitement** que le PPU est près de sa limite : le tampon devient non
   négociable et les fils devront être encore plus courts. Si elle ne bouge pas, c'est une
   marge de confort. Dans les deux cas, **note-le dans `signaux-mgb.md` §7** — c'est un
   préavis à coût nul sur le seul risque 🔴 du sous-projet.

### B.3 La séquence

| # | Action | Ce qu'on relève |
|---|---|---|
| 1 | Mesurer VCC au multimètre, **piles neuves** | 🔬 `VCC_neuf` |
| 2 | Mesurer VCC après 30 min de jeu, ou avec des piles usées | 🔬 `VCC_use` — c'est **lui** qui décide du tampon |
| 3 | Photographier la zone des pastilles du ruban LCD, macro, avec une règle | le plan de soudure du §C.4 |
| 4 | Sonder **une** pastille, console allumée sur un écran statique | fréquence et allure |
| 5 | Répéter jusqu'à avoir un candidat pour chacun des 6 signaux | table de §A.2 |
| 6 | **Capture LENTE** : les 6 voies, **4 MS/s, ~500 ms** → `releves/lente.sr` | `CPL`, `CP`, `ST`, `FR` |
| 7 | **Capture RAPIDE** : les 6 voies, **24 MS/s, ~4 trames (67 ms)** → `releves/rapide.sr` | `CPG`, `LD0`, `LD1`, et la matière du §D.11 |
| 8 | Dépouiller les deux (§B.3bis), puis vérifier à la main sur l'arbre §B.4 | l'attribution |
| 9 | Faire le **test blanc/noir** §B.5 — deux captures rapides de plus | la preuve |
| 10 | Relever la **période minimale de `CPG`** §B.6 | 🔬 budget PIO |
| 11 | Relever le **déphasage `LD` / `CPG`** §B.7 | 🔬 délai du PIO |
| 12 | Tout consigner dans `docs/signaux-mgb.md` | — |

> ℹ️ **Ce qui a été fait le 25/09/2026** : un point de test à la fois, d'où une capture par
> signal plutôt que `lente.sr` et `rapide.sr`. Les fichiers réels sont décrits dans
> [`releves/README.md`](releves/README.md).

> 🔑 **Pourquoi deux captures et pas une.** Elles ne répondent pas à la même question, et
> aucun taux unique ne fait les deux :
>
> - à **24 MS/s**, l'horloge pixel est visible (5,7 points par période) mais on ne peut pas
>   capturer longtemps sans risquer de perdre des échantillons ;
> - à **4 MS/s**, l'horloge pixel est repliée et illisible, mais on capture 30 trames sans
>   effort — et c'est ce qu'il faut pour mesurer proprement un signal à 59,73 Hz, qui ne donne
>   que 4 fronts sur une capture de 4 trames.
>
> ⚠️ Ne descends pas **sous 4 MS/s** pour la capture lente : les impulsions de `CPL` et `CP`
> durent de l'ordre de la microseconde, et en dessous de 3 échantillons par impulsion elles
> commencent à **disparaître**. L'outil te le dira, mais autant ne pas s'y exposer.

### B.3bis Dépouiller — `tools/analyse_sr.py`

```bash
./tools/analyse_sr.py docs/releves/lente.sr     # CPL / CP / ST / FR
./tools/analyse_sr.py docs/releves/rapide.sr    # CPG / LD0 / LD1
```

L'outil déroule l'arbre du §B.4 tout seul et **donne ses preuves**, pas seulement son verdict.
Ce qu'il fait mieux qu'un coup d'œil sur PulseView :

| Il mesure | Pourquoi c'est lui qui doit le faire |
|---|---|
| le **plus grand silence** dans la trame | c'est ce qui sépare `CPL` (1,09 ms) de `CP` (0,109 ms) ; leurs fréquences ne diffèrent que de 6,5 % et se confondent à l'œil |
| les **impulsions par salve** et les **salves par trame** | 160 et 144, c'est une preuve ; une fréquence approchante n'en est pas une |
| la **cadence par l'écart médian**, pas par un comptage sur la durée | un comptage sous-estime toujours, parce que la capture commence et finit au milieu d'une période |
| la **largeur minimale d'impulsion** | prévient que le taux choisi fait perdre des impulsions, avant qu'on accuse le câblage |
| la part des fronts de `LD` **tombant dans une salve de `CPG`** | ~100 %, c'est la signature d'une ligne de données |

Il produit en fin de rapport un tableau à recopier tel quel dans `signaux-mgb.md`.

> ⚠️ **L'outil propose, il ne décide pas.** Il annonce une confiance (`haute` / `moyenne` /
> `faible`) : tout ce qui n'est pas `haute` se vérifie à l'œil dans PulseView avant d'être
> consigné. Et une attribution `haute` sans le test blanc/noir reste une coïncidence de
> fréquence — la preuve, c'est le §B.5.

**Pour l'éprouver avant la session**, sans la console :

```bash
./tools/simuler_bus_gb.py /tmp/essai.sr && ./tools/analyse_sr.py /tmp/essai.sr
```

`simuler_bus_gb.py` fabrique une capture synthétique dont on connaît le contenu. Elle ne
valide **rien du projet** — elle valide le dépouilleur. Avoir la console ouverte n'est pas le
moment de découvrir que l'outil ne lit pas les `.sr` de ta version de PulseView.

### B.4 L'arbre de décision

```
  Le signal monte-t-il à plus de 1 MHz ?
  ├─ OUI  ──► par salves de 160, une salve par ligne visible ?
  │          ├─ OUI ──► CPG                                    ✔
  │          └─ NON ──► ce n'est pas un signal du bus LCD (horloge CPU ? CLS ?)
  └─ NON  ──► combien d'événements par seconde ?
             ├─ ~9 200 ──► présent pendant la VBlank ?
             │            ├─ OUI ──► CP / CPV                  ✔
             │            └─ NON ──► c'est 8 600, remesure : CPL
             ├─ ~8 600 ──► absent pendant la VBlank ?
             │            ├─ OUI ──► CPL                       ✔
             │            └─ NON ──► c'est CP, remesure
             ├─ ~59,7  ──► impulsion brève, ou carré ?
             │            ├─ impulsion ──► ST / S              ✔
             │            └─ carré     ──► tu mesures des fronts : c'est FR à 29,86 Hz
             ├─ ~29,9  ──► carré, alterne à chaque trame ──► FR ✔
             └─ variable, corrélé aux salves de CPG ──► LD0 ou LD1  (départager par §B.5)
```

### B.5 Le test blanc/noir — la seule vraie preuve

Une fréquence peut coïncider par hasard. Le contenu, non.

1. Analyseur sur les deux candidats `LD0`/`LD1` **et** sur `CPG`, déclenchement sur `ST`.
2. Afficher un écran **entièrement blanc** (un menu clair). Capturer.
3. Afficher un écran **entièrement noir** (écran de transition, ou éteindre le rétroéclairage
   n'est pas suffisant — il faut un contenu noir). Capturer.

**Attendu :** les deux lignes basculent **ensemble** d'un niveau constant à l'autre, pendant
les salves. Si une seule bascule, ce n'est pas `LD0`/`LD1` — c'est probablement un signal de
commande.

> 🔬 **Relever au passage la polarité** : sur DMG/MGB, `00` est attendu **blanc** et `11`
> **noir** (la valeur pilote l'opacité du cristal liquide). Peu importe le résultat : la
> palette a 4 entrées libres, on les mettra dans l'ordre qui convient. **Aucune ligne de
> firmware ne dépend de cette réponse** — c'est une des rares bonnes surprises du projet.

### B.6 La période minimale de `CPG` — le nombre qui décide du PIO

Ne pas mesurer « la fréquence de CPG » : c'est une moyenne, et elle ment.

1. Zoomer sur **une** salve de 160 impulsions.
2. Relever l'**écart le plus court** entre deux fronts montants.
3. Comparer :

| Écart minimal mesuré | Verdict |
|---|---|
| ≥ 200 ns | ✅ conforme au calcul (238 ns = 1 cycle maître). Marge PIO > 10× |
| 100 – 200 ns | 🔶 relire : l'analyseur à 24 MS/s a un pas de 42 ns, la mesure est grossière. Recapturer plus vite si possible |
| < 100 ns | ❌ incompatible avec le modèle du §A.1. **Arrêter et comprendre** avant d'écrire une ligne de firmware |

### B.7 Le déphasage `LD` / `CPG` — ce qui fixe le délai d'échantillonnage

Le PIO va échantillonner `LD0/LD1` sur un front de `CPG`. Encore faut-il savoir **lequel**,
et **combien de temps après**.

1. Zoomer sur 3 ou 4 pixels consécutifs dans une zone où l'image change vite (un damier, une
   bordure de fenêtre).
2. Répondre à : **`LD` change-t-il sur le front montant de `CPG`, ou sur le descendant ?**

| Ce que tu vois | Ce que fera le PIO |
|---|---|
| `LD` change sur le front **descendant**, stable autour du montant | `wait 1 gpio 2` puis `in` — le cas nominal |
| `LD` change sur le front **montant** | échantillonner sur le **descendant** : inverser les deux `wait` |
| `LD` change pile sur le front qu'on voulait utiliser | ajouter un délai : `wait 1 gpio 2 [7]` = 53 ns de retard à 150 MHz |

> ⚠️ Cette mesure est **grossière à 24 MS/s** (pas de 42 ns). Elle donne la bonne famille de
> réponse, pas la valeur fine. Le réglage définitif se fait en phase 2, **par l'image** : un
> échantillonnage trop tôt ou trop tard donne des colonnes de pixels mélangées avec leur
> voisine, ce qui se voit immédiatement sur un damier.

### B.8 ✅ Critère de sortie de la phase 0

- [ ] Les 5 signaux attribués, chacun par **sa fréquence ET le test blanc/noir**
- [ ] 🔬 `VCC_neuf` et `VCC_use` relevés → interface électrique tranchée (§C.2)
- [ ] 🔬 Période minimale de `CPG` relevée et ≥ 200 ns
- [ ] 🔬 Front d'échantillonnage choisi
- [ ] 🔬 Polarité de `LD0/LD1` relevée
- [ ] Les 4 `.sr` versionnés dans `docs/releves/` : `lente`, `rapide`, `blanc`, `noir`
- [ ] `docs/signaux-mgb.md` rempli : photos annotées, table pastille → signal, verdict GO/NO-GO
- [ ] 🔑 Noté : **l'écran d'origine s'est-il dégradé pendant le sondage ?** (§B.2, règle 4)

> Tant que cette liste n'est pas cochée, **ne pas sortir le fer à souder**. Souder d'après un
> brochage trouvé en ligne pour « une autre révision de carte », c'est risquer une pastille
> qui ne revient pas, pour gagner une demi-journée.

---

<a id="c"></a>
## C. Phase 1 — la prise de signaux 🔴

**Durée : 1 journée. C'est l'étape irréversible du projet.**

### C.1 Le schéma

```
   CARTE MGB                    PETITE CARTE (dans la console)      HORS CONSOLE
   ─────────                    ──────────────────────────────      ────────────

   broche P2-LD0 ──[100 Ω]──┬──────────────────────────────────┐
   broche P2-LD1 ──[100 Ω]──┤                                  │
   broche CP     ──[100 Ω]──┤                                  │  JST-SH
   broche P2-ST  ──[100 Ω]──┤        6 résistances             ├  8 points   ──► Pico
   broche P2-S   ──[100 Ω]──┤        + embase du connecteur    │  < 20 cm
   broche P2-CPL ──[100 Ω]──┤        + reprise d'effort        │
   P2-GND ───────────────┬──┴──────────────────────────────────┘
                         │           (2 masses au connecteur)
                      torsadée avec les signaux, < 10 cm
```

> 🔑 **Pas de tampon.** Décision du 25/09/2026, après vérification des datasheets — voir
> §C.2. La petite carte ne porte plus que les 6 résistances, l'embase, et la reprise
> d'effort des fils fins. Elle reste dans la console, au plus près des broches.

> ⚠️ **La longueur du câble est devenue un paramètre de conception.** Sans tampon, il pend
> directement sur les sorties du PPU : ≈ 1 pF/cm. La phase 0 a mesuré le PPU insensible à
> **40–60 pF** (les sondes de l'analyseur, sans résistance série) — donc **rester sous 20 cm**
> laisse une marge confortable. Au-delà, remettre le tampon en question.

### C.2 Pourquoi chaque composant — et pourquoi pas de tampon

| Composant | Ce qu'il fait | Ce qui se passe sans lui |
|---|---|---|
| **100 Ω en série** | limite le courant si une broche du Pico est mal configurée en sortie ; amortit les réflexions sur du fil volant | un court-circuit franc entre une sortie du PPU et une sortie du Pico. 100 Ω ramène le pire cas à 33 mA. **C'est la seule protection qui reste : ne pas les supprimer** |
| **Masse torsadée** | referme le circuit au plus court | à 4 MHz sur du fil volant, une masse lointaine crée des fronts fantômes — **constaté en phase 0** : les voies voisines de l'horloge pixel dans le faisceau ramassaient du signal |
| **Reprise d'effort** | immobilise les fils fins | un fil émaillé qui bouge arrache sa broche |

**Pourquoi le tampon 74LVC244A a été abandonné** (25/09/2026, après lecture des datasheets) :

| Argument invoqué | Verdict |
|---|---|
| « Il rattrape les niveaux marginaux » | ❌ **Faux.** RP2350 (§14.9) et SN74LVC244A (SCAS414AG §5.3) ont le **même** `V_IH` = **2,0 V** à 3,3 V. La formule `0,65 × IOVDD` ne vaut que pour IOVDD = 1,8 V |
| « Il protège la console si le Pico est hors tension » | ❌ **Faux.** `GPIO0`–`GPIO5` sont `Digital IO (FT)` : *« very little current flows into the pin whilst it is below 3.63 V and IOVDD is 0 V »*. Nos signaux plafonnent à 3,1 V |
| « Ses entrées tolèrent 5 V » | ⭕ vrai, mais ne sert qu'à un portage DMG, hors périmètre |
| « Il isole le PPU de la capacité du câble » | 🔶 **le seul qui tienne** — et la phase 0 a mesuré le PPU insensible à 40–60 pF, soit plus qu'un câble de 20 cm ne présente |

> ℹ️ **Le tampon reste une option, pas une pièce morte.** Si la vérification 2 du §C.5 montre
> l'image d'origine dégradée, ou si la scénographie impose d'éloigner le Pico, il se réinsère
> entre les résistances et le connecteur. Le plan §9 garde sa référence.

### C.4 Ordre de montage

| # | Action | Vérification avant de passer à la suite |
|---|---|---|
| 1 | Monter la petite carte **entièrement hors console** : 6 × 100 Ω et embase du connecteur | continuité au multimètre, résistance mesurée ≈ 100 Ω entre chaque entrée et sa broche du connecteur |
| 2 | 🔴 Souder les 6 fils fins sur les **broches de `P2`** | loupe : **aucun pont** entre broches voisines. Continuité vers `P2` et **isolement** vis-à-vis des voisines |
| 3 | 🔴 **Colle chaude** sur les 6 fils dès leur sortie de la carte | le fil ne bouge plus quand on tire doucement dessus |
| 4 | Souder les 6 fils côté petite carte, **< 10 cm**, torsadés avec la masse | continuité broche `P2` → broche du connecteur, à travers les 100 Ω |
| 5 | Fixer la petite carte dans la console, connecteur vers la sortie | rien ne touche le blindage |
| 6 | Confectionner le câble vers le Pico, **< 20 cm** | ⚠️ sans tampon, sa capacité pèse sur le PPU |

⚠️ **L'étape 1 se fait avant l'étape 2.** Découvrir une erreur de câblage sur la petite carte
après avoir soudé sur la MGB, c'est souder deux fois.

### C.5 Les cinq vérifications avant de brancher le Pico

| # | Vérification | Comment | Si ça échoue |
|---|---|---|---|
| 1 | **La console joue normalement**, module non alimenté | 10 minutes de jeu | le tampon charge le bus même non alimenté : vérifier que ses entrées ne sont pas alimentées par les signaux (diodes de protection) |
| 2 | **L'écran d'origine est intact** | photo avant/après, **même image, même luminosité, même angle** | retirer, chercher un pont ou une piste abîmée |
| 3 | Tension sur chaque broche du connecteur **≤ 3,2 V** | multimètre | une tension supérieure signifie qu'on n'est pas sur le signal attendu |
| 4 | Les 6 signaux, relevés **côté connecteur**, ont les signatures de la phase 0 | `./tools/sonde.sh` — l'horloge pixel doit donner **1,376 M fronts/s** | fil coupé, soudure froide, ou broche arrachée |
| 5 | Les fronts sont **nets** : pas de rebond, pas de palier | analyseur, zoom maximal | masse trop longue, ou fils non torsadés |

> 🔑 **La vérification 4 se fait avec l'outil de la phase 0**, et les signatures attendues sont
> déjà écrites dans `signaux-mgb.md` §3bis. C'est le bénéfice d'avoir tout mesuré avant : la
> recette du câblage est une comparaison, pas une découverte.

### C.6 ✅ Critère de sortie de la phase 1

- [ ] La console démarre, joue et s'éteint normalement, module **branché** puis **débranché**
- [ ] Son écran d'origine est **identique** à la photo d'avant, sur la même image
- [ ] Les 6 signaux, relevés au connecteur, ont les signatures de `signaux-mgb.md` §3bis
- [ ] Les 6 signaux relevés côté connecteur ont l'allure de ceux de la phase 0
- [ ] La console se **referme**, connecteur sorti
- [ ] Une photo du montage fini est dans `docs/`

---

<a id="d"></a>
## D. Phase 2 — la capture

**Durée : 2 à 3 jours. Pas de réseau : on prouve par l'image.**

### D.1 La chaîne, en une figure

```
                      ┌──────────── cœur 0 ────────────┐
  CPG (GP2) ─────────►│ PIO SM0   wait 0 / wait 1 / in │
  LD0 (GP0)  ────────►│           autopush 32 bits      │
  LD1 (GP1)  ────────►└──────────────┬─────────────────┘
                                     │ 1 mot = 16 pixels
                                     ▼
                        ┌──────── DMA, UN canal ────────┐
                        │  RX FIFO → framebuffer        │  1 440 mots
                        │  adresse d'écriture croissante │  = 1 trame entière
                        └───────────────────────────────┘

  P2-ST (GP3) ─► IRQ ──► compteur de lignes (144), déclenche un paquet tous les 35
  P2-S  (GP4) ─► IRQ ──► bascule les framebuffers, réarme le canal DMA

                      ┌──────────── cœur 1 ────────────┐
                      │ lwIP + CYW43  →  5 paquets UDP │
                      └────────────────────────────────┘
```

**Le CPU ne touche aucun pixel.** Il ne fait que compter des lignes et réarmer un pointeur
une fois par trame, dans une fenêtre de VBlank de 1,09 ms — soit 163 000 cycles à 150 MHz
pour faire un travail qui en demande quelques centaines.

### D.2 Le programme PIO, instruction par instruction

```
.program gb_pixels
.wrap_target
    wait 0 gpio 2        ; 1. attendre que CPG soit bas
    wait 1 gpio 2 [D]    ; 2. attendre le front montant, puis D cycles
    in   pins, 2         ; 3. échantillonner GP0 et GP1 dans l'ISR
.wrap
```

| Ligne | Ce qu'elle fait | Pourquoi elle est là |
|---|---|---|
| 1 | `wait 0` | **garantit qu'on verra le prochain front montant.** Sans elle, si `CPG` est déjà haut à l'amorçage, `wait 1` passe immédiatement et on échantillonne un pixel fantôme, décalant toute la ligne |
| 2 | `wait 1 … [D]` | se cale sur le front. `D` = 0 à 31 cycles de délai, **réglé par la mesure §B.7** puis affiné par l'image |
| 3 | `in pins, 2` | prend **2 bits contigus à partir de `IN_BASE` = GP0**. C'est pour ça que LD0 et LD1 doivent être sur GP0 et GP1 |

**Configuration de la machine d'état :**

| Réglage | Valeur | Raison |
|---|---|---|
| `in_base` | GP0 | LD0 en bit 0, LD1 en bit 1 |
| `autopush` | activé, seuil **32** | 16 pixels par mot poussé automatiquement, zéro instruction de `push` |
| `in_shiftdir` | **gauche** | voir §D.3 — c'est ce qui, combiné au `bswap` du DMA, place les pixels dans le bon ordre |
| `clkdiv` | **1** | la boucle doit tourner à pleine vitesse ; son rythme est imposé par `CPG`, pas par l'horloge PIO |
| FIFO | **join RX** (8 mots) | 8 mots = 128 pixels de tampon. Le DMA a alors ~30 µs pour réagir, il lui en faut ~0,1 |

**Budget** : la boucle fait 3 instructions, soit 20 ns à 150 MHz. Le pixel le plus rapide
arrive toutes les 238 ns 🔬. On échantillonne donc **avec 12× de marge**, et le seul réglage
fin est `D`.

### D.3 Où tombent les bits — la démonstration

On veut en mémoire, conformément au §5.5 du plan :

```
  octet 0 :  bit 7 6 | 5 4 | 3 2 | 1 0
             px  0   |  1  |  2  |  3
```

**Étape 1 — ce que fait le PIO.** Avec `in_shiftdir = gauche`, chaque `in` fait
`ISR = (ISR << 2) | pixel`. Après 16 `in` et l'autopush :

```
  bits  31 30 | 29 28 | 27 26 | 25 24 | 23 22 | ... | 1 0
  px      0   |   1   |   2   |   3   |   4   | ... |  15
```

**Étape 2 — ce que fait la mémoire.** Le RP2350 est **little-endian** : le mot est écrit
`bits 7:0` dans l'octet 0, `bits 31:24` dans l'octet 3. Sans rien faire, on obtiendrait
`octet 0 = px12..px15`. **À l'envers.**

**Étape 3 — ce que fait le DMA.** `channel_config_set_bswap(&c, true)` inverse l'ordre des
4 octets d'un mot au vol, gratuitement. L'octet 3 devient l'octet 0 :

```
  octet 0 ← bits 31:24 = px0 px1 px2 px3     ✔
  octet 1 ← bits 23:16 = px4 px5 px6 px7     ✔
  octet 2 ← bits 15:8  = px8 ... px11        ✔
  octet 3 ← bits 7:0   = px12 ... px15       ✔
```

Et à l'intérieur de l'octet 0, `px0` occupe les bits 7:6, comme voulu.

> 🔑 **`in_shiftdir = gauche` + `bswap` du DMA = exactement la disposition `IDX2` attendue,
> sans une seule instruction de CPU.**
>
> ⚠️ Cette démonstration est faite **sur table**. Elle se vérifie en dix secondes sur la
> première image capturée : si elle est fausse, les pixels apparaissent mélangés **par
> groupes de 4** — motif caractéristique, impossible à confondre avec autre chose (§D.10).

**Et l'ordre des deux bits d'un pixel ?** `in pins, 2` met GP0 (`LD0`) en bit de poids faible
et GP1 (`LD1`) en bit de poids fort. Si c'est l'inverse de la convention de la console, ou si
`00` est blanc au lieu de noir : **on permute 4 entrées de palette**. Aucun code ne change.

### D.4 Le DMA — un seul canal

> 🔑 **Depuis que l'émetteur est agnostique de l'afficheur (25/09/2026), cette section est
> devenue presque triviale.** Elle décrivait auparavant deux canaux chaînés déroulant une
> table de 144 adresses, parce qu'une ligne GB faisait 40 octets dans un canevas de 48. Avec
> un framebuffer 160×144, **les lignes sont contiguës** et tout ça disparaît.

| | Le canal unique |
|---|---|
| Lit | RX FIFO du PIO (adresse fixe) |
| Écrit | le framebuffer (adresse **incrémentée**) |
| Taille | 32 bits |
| Nombre | **1 440** transferts = 144 lignes × 10 mots = une trame entière |
| `dreq` | PIO RX non vide |
| `bswap` | **oui** (§D.3) |

À chaque VSYNC : on arrête le canal, on vide le FIFO, on bascule le framebuffer, on réarme le
canal sur l'autre. Une écriture d'adresse et un compteur, dans une fenêtre de VBlank de
**1,09 ms** — soit 163 000 cycles à 150 MHz pour un travail qui en demande quelques dizaines.

⚠️ **Ce que le DMA ne sait toujours pas** : il compte des mots, pas des lignes. Si un front
d'horloge pixel est raté, tout décale et ne se rattrape **jamais** dans la trame. D'où l'IRQ
sur `P2-ST`, qui n'est pas décorative : c'est le **contrôle d'intégrité**, et la phase 0 a
établi qu'elle donne exactement **144 impulsions par trame**.

### D.5 Les deux interruptions

**IRQ `CPL` (GP3), front montant, ~8,6 kHz**

```
  lignes++;                              /* contrôle d'intégrité */
  if (lignes == 40 || lignes == 104)     /* nœuds 0 et 1 complets */
      reveiller_coeur1(lignes);
```

Coût : un handler d'une vingtaine d'instructions toutes les 116 µs, soit **moins de 0,2 % du
cœur 0**. Et il ne touche pas le chemin des pixels : PIO et DMA continuent pendant.

**IRQ `ST` (GP4), front montant, 59,73 Hz** — c'est la seule qui fait un vrai travail, et elle
dispose de toute la VBlank (**1,09 ms**) :

```
  1. vérifier : lignes == 144 ?  sinon → compteur trames_douteuses++
  2. arrêter les canaux A et B    (abort, au cas où une ligne serait en cours)
  3. vider le FIFO RX du PIO      (sinon les restes d'une ligne ratée polluent la suivante)
  4. basculer : tampon_actif ^= 1
  5. réarmer le canal DMA sur trame[tampon_actif], 1 440 mots
  6. lignes = 0 ; trames++
  7. réveiller le cœur 1 pour émettre la trame qui vient d'être terminée
```

> ⚠️ **L'ordre 2-3-4-5 n'est pas négociable.** Vider le FIFO avant d'arrêter les canaux laisse
> le PIO le remplir à nouveau. Basculer le tampon avant d'arrêter le canal laisse celui-ci
> écrire quelques mots dans la trame qu'on est en train d'émettre — une déchirure,
> intermittente, et donc pénible à trouver.

> 🔬 **Si `ST` se révèle être `FR`** (alternance à 29,86 Hz au lieu d'une impulsion à
> 59,73 Hz), le firmware doit déclencher sur **les deux fronts** au lieu du seul front
> montant. C'est un paramètre, pas une réécriture — mais il faut l'avoir prévu.

### D.6 Le framebuffer

```c
uint8_t trame[2][160 * 144 / 4];   /* 2 × 5 760 = 11 520 octets */
```

Rien d'autre. Pas de marge à pré-remplir, pas de cadre à dessiner, pas de zone que le DMA
n'écrirait pas : **chaque octet du tampon est écrit à chaque trame**.

> Le cadre, le centrage et le choix des teintes appartiennent désormais au module ÉCRAN
> (plan §5.3). C'est ce qui rend ce sous-projet indépendant de la géométrie d'arrivée — et ce
> qui charge l'autre d'un travail qu'il n'avait pas.

### D.7 Le firmware : fichiers et responsabilités

| Fichier | Responsabilité | Ne contient pas |
|---|---|---|
| `include/config.h` | brochage, géométrie de la **source** (160×144), `D` du PIO | de la logique, et **rien sur l'afficheur** |
| `src/capture.pio` | les 3 instructions | quoi que ce soit d'autre |
| `src/capture.cpp` | PIO, DMA, les 2 IRQ, les framebuffers, les compteurs | le réseau |
| `src/net/reseau.cpp` | lwIP, CYW43, l'émission `PXL1` | la capture |
| `src/main.cpp` | démarrage, console, recette de phase | de la mécanique |
| `include/pxl1.h` | copie conforme de `../../ecran`, + `PROVENANCE.txt` | **aucune modification** |

⚠️ `pxl1.h` est une **copie**. La tentation de « juste ajouter un champ pour le sniffer » est
exactement ce qui casse la compatibilité entre deux modules. Si le protocole doit évoluer, il
évolue **d'abord** dans `../ecran`, et on recopie.

### D.8 Prouver par l'image — deux instruments, dans cet ordre

**D'abord l'ASCII, parce qu'il ne demande rien.** Sur commande (une touche sur la console
USB), vider le framebuffer, un pixel sur deux dans chaque direction, 4 caractères :

```
  80 colonnes × 72 lignes, avec " ", ".", ":", "#"
```

80 colonnes entrent dans un terminal. C'est illisible pour juger une nuance, et **parfaitement
lisible pour reconnaître un écran-titre** — donc pour répondre à la seule question de la
phase 2 : *est-ce qu'on capture la bonne chose ?*

**Ensuite le PNG, pour le détail.** `tools/gbdump.py` : demande un vidage, reçoit les
5 760 octets en hexadécimal sur l'USB, écrit un PNG 160×144 en 4 gris. Sert à juger l'ordre
des pixels, les bords, les colonnes.

> 🔑 Contrairement au module écran, où le compteur de trames annonçait 788 Hz parfaitement
> stables pendant que la dalle était noire, **ici l'instrument ne peut pas mentir** : si
> l'image est reconnaissable, la chaîne est juste. C'est un luxe, il faut s'en servir tôt.

### D.9 Les compteurs à exposer sur la console

| Compteur | Valeur attendue | Ce qu'un écart révèle |
|---|---|---|
| `trames/s` | **59,73** | 29,86 → on déclenche sur `FR` et pas sur `ST` |
| `lignes par trame` | **144** exactement | ≠ 144 → `CPL` manque des impulsions, ou en invente |
| `trames_douteuses` | **0** | le nombre de trames où `lignes != 144` |
| `mots restants dans A à la VSYNC` | **0** | ≠ 0 → des fronts de `CPG` manquent : `D` trop grand, ou front mal choisi |
| `débordements FIFO PIO` | **0** | le DMA ne suit pas — ne devrait jamais arriver avec 30 µs de marge |
| `salves de CPG par ligne` | **160** | à instrumenter seulement si la ligne ci-dessus n'est pas à 0 |

### D.10 Diagnostic : le symptôme dit la cause

| Ce que montre l'image | Cause presque certaine | Quoi faire |
|---|---|---|
| **Rien**, framebuffer vide ou figé | `CPG` n'arrive pas sur GP2, ou le PIO n'est pas démarré | test de continuité §C.5, puis relire le FIFO à la main |
| Pixels mélangés **par groupes de 4**, motif régulier | `bswap` ou `in_shiftdir` (§D.3) | inverser l'un des deux, pas les deux |
| Image **décalée horizontalement**, le décalage **grandit** ligne après ligne | un front de `CPG` raté ou compté en trop | augmenter/diminuer `D`, vérifier le front choisi (§B.7) |
| Image décalée horizontalement d'un montant **constant** | erreur d'amorçage : le `wait 0` manque, ou la ligne démarre au mauvais moment | vérifier l'ordre 2-3-4-5 du §D.5 |
| Image **cisaillée**, chaque ligne décalée de la même quantité | le nombre de mots par ligne n'est pas 10 | `transfer_count` du canal A |
| Image qui **roule verticalement** | VSYNC pas sur `ST` | table §A.2 |
| Image correcte **une trame sur deux**, l'autre noire ou brouillée | on déclenche sur `FR` (29,86 Hz) et pas sur `ST` | déclencher sur les **deux** fronts (§D.5) |
| Image correcte mais **négative** | polarité `00`/`11` | permuter les 4 entrées de palette. Rien à recompiler côté capture |
| **Colonnes** qui empruntent à leur voisine | `D` mal réglé : on échantillonne pendant la transition | balayer `D` de 0 à 8 et regarder |
| Neige, pixels aléatoires isolés | masse : fil non torsadé, découplage absent | §C.2, vérification 5 du §C.5 |
| `lignes par trame` = 154 au lieu de 144 | on compte `CP`/`CPV` et pas `CPL` | c'était l'inversion du §A.2 |

### D.11 Travailler sans la console — rejouer la phase 0

Le plan (§5.6) écarte le générateur de bus LCD **comme moyen de validation** : fabriquer un
stimulus à partir de nos propres hypothèses de timing, puis vérifier que le firmware le
comprend, ne prouve rien d'autre que notre cohérence avec nous-mêmes.

**Rejouer la capture `.sr` de la phase 0 est un autre objet** : le stimulus est le signal
**réel de la console**, enregistré. L'objection tombe.

| | |
|---|---|
| Quoi | un 2ᵉ Pico rejoue échantillon par échantillon les 6 voies du `.sr`, en PIO + DMA |
| Coût mémoire | 24 MS/s × 16,74 ms = **402 000 échantillons**, un octet chacun ⇒ **402 ko** sur les 520 ko du RP2350. Ça tient, tout juste, pour **une** trame en boucle |
| Ce que ça prouve | la **plomberie** : DMA, `bswap`, comptage des lignes, IRQ, découpage en paquets |
| Ce que ça ne prouve **pas** | la tenue analogique : fronts réels, rebonds, niveaux à piles usées, diaphonie |

C'est donc un **accélérateur de développement**, pas un critère de sortie. La phase 2 n'est
close que sur le matériel réel.

### D.12 ✅ Critère de sortie de la phase 2

- [ ] Le vidage ASCII montre un écran-titre **reconnaissable**
- [ ] Le PNG est net : pas de décalage, pas de cisaillement, pas de groupes de 4 inversés
- [ ] **59,73 trames/s** ± 0,1, mesuré sur 60 secondes
- [ ] **`lignes par trame` = 144 sur 10 000 trames consécutives**, `trames_douteuses` = 0
- [ ] `mots restants` = 0 et `débordements FIFO` = 0 sur la même durée
- [ ] `D` et le front d'échantillonnage **consignés** dans `config.h` avec la mesure qui les justifie
- [ ] L'image reste correcte après 30 minutes, et après un cycle d'extinction/rallumage de la console

---

<a id="e"></a>
## E. Phase 3 — `IDX2` de bout en bout

**Durée : 1 à 2 jours.** Prévue sur les deux modules, elle est autonome depuis le 26/09/2026 : voir §E.0.

### E.0 Le récepteur de référence est un écran virtuel, pas la matrice

**Décision du 26/09/2026.** La phase 3 ne touche plus `../ecran/`. Le récepteur est
`tools/ecran_virtuel.py`, qui reçoit le flux `PXL1` et l'affiche sur le PC.

| | |
|---|---|
| **Pourquoi** | tant qu'on met au point le sniffeur, on veut savoir si **le sniffeur** émet correctement. Avec la matrice dans la boucle, un symptôme a cinq suspects : le sniffeur, son WiFi, le réseau, le firmware de l'écran, la dalle |
| **Ce que ça retire** | une dépendance inter-dépôts du chemin critique. Le compositeur côté écran reste à écrire, mais il ne bloque plus rien |
| **Fidélité** | l'écran virtuel applique les **mêmes règles de réassemblage** que `../ecran/.../reseau.cpp` — fenêtre de resynchronisation de 8, tranches en retard écartées — et renvoie l'accusé `PXL1_TYPE_PING`. L'émetteur mesurera donc l'aller-retour sans modification, et le passage à la vraie matrice ne changera rien pour lui |

**Éprouvé le 26/09/2026** contre `tools/pxl1_envoi.py`, un émetteur de test côté PC :

| | Lien parfait | 3 % de perte injectée |
|---|---|---|
| Trames reçues | 59,39 img/s | 57,83 img/s |
| Incomplètes | **0** | 48 sur 289 — dégradation progressive, aucun blocage |
| Rejets, resyncs | 0, 0 | 0, 0 |
| Aller-retour | 0,29 / **0,89** / 1,73 ms | 0,26 / **0,83** / 1,36 ms |

> 🔑 **Deux défauts d'instrument corrigés au passage**, et tous deux auraient menti
> en phase 4 :
>
> 1. l'écran virtuel démarrait sa fenêtre de mesure à sa **construction**, donc
>    l'attente avant le premier paquet comptait comme du temps sans trames — 38 img/s
>    affichés pour 59 réels. Même piège que la cadence moyennée sur une coupure ;
> 2. l'émetteur relevait les accusés **une fois par trame, après l'envoi** : l'accusé
>    arrivait pendant la sieste et n'était lu qu'à l'itération suivante. L'aller-retour
>    ressortait à **17 ms sur boucle locale**, soit exactement une période de trame,
>    là où le vrai est de 0,9 ms. Corrigé par un fil dédié qui horodate à l'arrivée.

### E.1 Pourquoi cet ordre, et pas l'inverse

Deux choses sont neuves : un **format** que personne n'a jamais décodé, et un **émetteur**
qu'on vient d'écrire. Les brancher ensemble, c'est avoir deux suspects pour chaque symptôme.

On fait donc d'abord vivre `IDX2` entre deux pièces **déjà éprouvées** — `pixelpush` et le
firmware écran, qui tournent depuis le 18/09 — puis on remplace `pixelpush` par le sniffer.

### E.2 Côté ÉCRAN — quatre modifications, dans `../ecran/`

| # | Où | Quoi |
|---|---|---|
| 1 | `reseau.cpp`, `taille_trame()` | ajouter `IDX2` → `(DISPLAY_W * DISPLAY_H) / 4`. Aujourd'hui la fonction ne connaît que `IDX8` et retombe sur `FB_OCTETS` |
| 2 | `reseau.cpp`, filtre de `sur_paquet()` | la condition `e.format != PXL1_FMT_BGR888 && e.format != PXL1_FMT_IDX8` doit accepter `IDX2` |
| 3 | `reseau.cpp` + `reseau.hpp` | `developper_idx2(const uint8_t *indices, uint8_t *sortie)`, jumeau de `developper_idx8` : 1 octet → 4 pixels → palette → B,G,R |
| 4 | `main.cpp`, vers la ligne 249 | le `if (t.format == PXL1_FMT_IDX8)` devient un aiguillage à deux branches |
| 5 | `pxl1.h` | ajouter `PXL1_CTRL_GEOMETRIE = 2` (largeur, hauteur, format) — §E.5bis |
| 6 | `reseau.cpp`, `traiter_ctrl()` | mémoriser la géométrie annoncée par la source |
| 7 | `reseau.cpp`, `taille_trame()` | **la déduire de la SOURCE**, plus de l'affichage |
| 8 | `main.cpp` | **placer** la source 160×144 dans l'affichage : recadrage, centrage, marges |

> ⚠️ **Les modifications 5 à 8 sont nouvelles**, conséquence de l'émetteur agnostique
> (25/09/2026). Elles ne sont pas des retouches : la n° 8 est un **compositeur**, et le
> firmware du module écran n'en a jamais eu. À inscrire à son propre plan.

**Ce qui ne change pas**, et c'est l'essentiel :

- **les tampons de réception** : ils font déjà `FB_OCTETS` = `W × H × 3`, très au-delà des
  `W × H / 4` d'une trame `IDX2` ;
- **le réassemblage** : offsets, fenêtre de resynchronisation, accusés — rien à toucher ;
- **la palette** : le chemin `PXL1_CTRL_PALETTE` existe, on n'utilisera que 4 entrées sur 256 ;
- **la palette par défaut** (rampe de gris) : une trame `IDX2` sans palette reçue s'affichera
  en 4 gris très sombres (indices 0 à 3), donc **visible mais moche** — c'est le bon
  comportement, et un diagnostic gratuit : « image très sombre » = « palette pas arrivée ».

🔬 **Coût à mesurer** : `developper_idx8` coûte 0,25 ms pour 64×64. Un nœud du 3×3 fait
192×64 = 12 288 pixels, soit 3× plus, plus le dépaquetage — de l'ordre de **1 ms**. À comparer
aux 2,23 ms que coûte déjà la construction des plans de bits, dans une période de 16,7 ms.
Confortable, mais à vérifier plutôt qu'à supposer.

### E.3 Côté PC — `pixelpush --format idx2`

Dans `sources.py` / `pixelpush.py` : quantifier sur 4 niveaux de luminance, empaqueter 4
pixels par octet **dans l'ordre du §D.3**, envoyer la palette de 4 entrées en `CTRL`.

C'est 30 lignes, et ça donne un **banc de test permanent** du décodeur `IDX2`, indépendant du
sniffer : il servira encore quand la console sera refermée.

### E.4 La recette intermédiaire — à passer avant de brancher le sniffer

- [ ] `./pixelpush.py --source mire --format idx2` → la mire s'affiche en **4 niveaux**
- [ ] `--source anim --format idx2` à 60 img/s → **60 img/s** reçues, le curseur ne saccade pas
- [ ] `--perte 2` → l'image reste correcte, `trames_incompletes` reflète l'injection
- [ ] `--luminosite 12` → la luminosité change (preuve que le chemin `CTRL` marche encore)
- [ ] Couper `pixelpush` puis le relancer → la resynchronisation opère (compteur `resynchros`)
- [ ] Débit relevé : **1,47 Mbit/s par nœud** ± 5 %

> Tant que cette liste n'est pas verte, **ne pas brancher le sniffer**. Si on la saute et que
> l'image est mauvaise, on ne saura pas si c'est le décodeur ou la capture.

### E.5 Côté CAPTURE — l'émission

**Ce que le cœur 1 fait, à chaque réveil** (un réveil tous les 35 lignes, plus un à la VSYNC) :

```
  taille = min(5760 - offset, 1400)
  derniere = (offset + taille == 5760)
  entete = { magic, FRAME, node_id=0, frame_id, IDX2,
             (derniere ? DERNIERE : 0), offset }
  sendto(ip_recepteur, entete + trame[offset .. offset+taille])
  offset += taille
```

**5 paquets par trame** : 1400 × 4 + 160. **299 paquets par seconde.**

| Point d'attention | Pourquoi |
|---|---|
| `node_id` = **0**, toujours | un émetteur, un récepteur. Le champ reste dans l'en-tête pour ne pas le casser |
| `frame_id` s'incrémente **par trame GB** | c'est lui qui solde une trame côté récepteur |
| `PXL1_FLAG_DERNIERE` sur le **5ᵉ paquet** | c'est ce qui déclenche l'affichage. Trop tôt = trame tronquée |
| `offset` en octets **dans la trame source** | 0, 1400, 2800, 4200, 5600 |
| `cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM)` | ⚠️ **le levier de latence**, mesuré par le module écran : sans lui, 10 à 100 ms s'ajoutent |
| Émission depuis le **cœur 1**, jamais depuis une IRQ de capture | une IRQ qui bloque dans lwIP fait rater des fronts d'horloge pixel |

**Deux variantes, dans cet ordre :**

1. **Simple** — tout émettre à la VSYNC : 5 paquets d'affilée. À écrire en premier.
2. **Pipelinée** — un paquet dès que ses 35 lignes sont capturées. Gagne jusqu'à **11,9 ms**
   sur le haut de l'image. Le compteur de lignes sur `P2-ST` existe déjà : c'est un `if`.

| Paquet | Lignes GB | Prêt après |
|---|---|---|
| 0 | 0 – 34 | 3,8 ms |
| 1 | 35 – 69 | 7,6 ms |
| 2 | 70 – 104 | 11,4 ms |
| 3 | 105 – 139 | 15,2 ms |
| 4 | 140 – 143 | 15,7 ms |

### E.5bis Annoncer la géométrie — `PXL1_CTRL_GEOMETRIE`

Le récepteur déduisait jusqu'ici la taille d'une trame de **sa propre** géométrie. Un
émetteur agnostique doit donc lui annoncer celle de la **source** :

```
  PXL1_CTRL_GEOMETRIE = 2
      uint16 largeur   = 160
      uint16 hauteur   = 144
      uint8  format    = PXL1_FMT_IDX2
```

Émise avec la palette, **toutes les 2 secondes**. Un récepteur redémarré retrouve seul de
quoi interpréter le flux — comportement déjà en place pour la palette.

⚠️ **Cette sous-commande est à ajouter dans `pxl1.h`, donc dans `../ecran` d'abord**, puis à
recopier ici (§D.7). Le protocole évolue dans le dépôt qui le possède.

### E.6 La palette — 4 entrées, envoyées toutes les 2 secondes

```
  indice 0  ──►  le plus clair     ┐  ordre à inverser si la phase 0 dit
  indice 3  ──►  le plus sombre    ┘  que 11 est blanc
```

| Palette | Valeurs (R,G,B) | Quand |
|---|---|---|
| **DMG vert** | `9B,BC,0F` · `8B,AC,0F` · `30,62,30` · `0F,38,0F` | l'aspect Game Boy d'origine |
| **Gris MGB** | 4 gris régulièrement espacés | plus proche de l'écran de la Pocket |
| **Diagnostic** | rouge, vert, bleu, noir | ⚠️ pour la phase 2/3 : rend **chaque indice identifiable à l'œil** |

⚠️ Le paquet `CTRL` transporte **256 entrées** (768 octets), pas 4 : c'est le format existant,
on remplit les 4 premières et on laisse le reste. Le renvoi toutes les 2 s est ce qui permet à
un nœud redémarré de retrouver ses couleurs sans qu'on intervienne — comportement déjà en
place pour `IDX8`.

### E.7 Les adresses des trois nœuds

Le sniffer n'a pas de fichier de disposition : il **est** l'émetteur. Les 3 adresses vont donc
dans `config.h`, avec une réservation DHCP sur la box pour qu'elles ne bougent pas.

> 🔑 **Bonne nouvelle pour la phase 4 : l'instrumentation de latence existe déjà.** Le firmware
> écran appelle `reseau::acquitter()` dès qu'il a publié une trame, et l'accusé part vers
> **l'émetteur dont il a reçu les paquets** — donc vers le sniffer, sans aucune modification.
> Le sniffer peut mesurer l'aller-retour complet sur sa propre horloge, exactement comme
> `pixelpush` le fait aujourd'hui (`relever_accuses()`).
>
> Une découverte automatique des nœuds par `PXL1_TYPE_PING` est possible plus tard — le type
> est réservé dans le protocole. Ce n'est pas la v1.

### E.8 ✅ Critère de sortie de la phase 3

> ✅ **Atteint le 26/09/2026 sur l'écran virtuel**, récepteur de référence depuis la décision
> du §E.0 : 59,71 img/s reçues, aucune trame incomplète, 2 218 trames capturées et autant
> d'émises ([journal du firmware](../firmware/sniffer/JOURNAL.md)). Les cases ci-dessous
> supposent la grille : elles reviennent à l'intégration avec le module écran.

- [ ] La recette §E.4 est entièrement verte, **sans le sniffer**
- [ ] Un jeu tourne sur la console et **s'affiche sur la grille** — suppose que le
      module ÉCRAN sache recevoir une source 160×144 et la placer (plan §5.3)
- [ ] **59,73 img/s** reçues par les 3 nœuds
- [ ] Les 3 rangées affichent **la même trame** : aucune déchirure sur un scrolling horizontal
- [ ] Aucun scintillement sur **10 minutes** de jeu
- [ ] `trames_incompletes` < 0,1 % sur les 3 nœuds
- [ ] La palette survit au redémarrage d'un nœud, sans intervention

---

<a id="f"></a>
## F. Phase 4 — mesurer

**Durée : 1 jour.**

### F.1 La cible

**Sous une trame Game Boy — 16,74 ms** — entre la fin de capture d'une ligne et son apparition
sur la grille. Le budget prévisionnel :

| Poste | Durée | D'où il vient |
|---|---|---|
| Remplissage du rectangle du nœud | 4,3 / 11,3 / 15,7 ms | §5.3 du plan — **c'est le poste dominant** |
| Réseau + réassemblage + publication | ~8 ms | mesuré par le module écran le 18/09 |
| Rafraîchissement de la dalle | 1,3 ms | 788 Hz |

Le poste dominant n'est pas le WiFi : **c'est le temps que met la Game Boy à dessiner la partie
de l'image qui concerne un nœud donné.** C'est exactement ce que l'émission pipelinée (§E.5)
attaque, et c'est pourquoi on mesure avant de l'écrire.

### F.2 Latence photon-à-photon — la mesure qui compte vraiment

1. Placer l'écran d'origine de la console **et** la grille dans le même cadre.
2. Filmer à **240 img/s** (téléphone récent).
3. Provoquer un changement brutal et net : ouvrir un menu, une transition blanc → noir.
4. Compter les images entre le changement sur le LCD et le changement sur la grille.

Résolution : **4,17 ms par image**. Elle ne permet pas de distinguer 8 ms de 10 ms — et ce
n'est pas grave : la question est « est-ce sous une trame ? », et 4 images de retard répondent
sans ambiguïté.

> 🔑 C'est la seule mesure qui intègre **tout** : capture, réseau, affichage, persistance du
> LCD d'origine. Les mesures internes ne remplacent pas celle-là, elles l'expliquent.

### F.3 Latence interne — trois horodatages

| Signal | Sur quelle carte | Ce qu'il marque |
|---|---|---|
| **GP20** | sniffer | front montant de `ST` capturé |
| **GP21** | sniffer | dernier paquet du nœud émis |
| `PIN_MESURE_FLIP` (**GP17**) | nœud écran | trame publiée sur la dalle |

⚠️ Il faut une **masse commune** entre le sniffer et le nœud sondé pour que l'analyseur
compare deux cartes. Les deux étant alimentés en USB depuis le même PC, c'est déjà le cas — le
vérifier plutôt que le supposer.

Et, gratuitement : le **compteur d'aller-retour par les accusés** (§E.7), sur l'horloge du
sniffer, sans aucune hypothèse de synchronisation entre les cartes.

### F.4 Robustesse

| Mesure | Comment | Cible |
|---|---|---|
| Complétude | `trames_incompletes` / `trames` sur les 3 nœuds, 10 min | < 0,1 % |
| Débit réel | compteur d'octets émis / seconde | **4,40 Mbit/s** ± 5 % |
| Tenue dans la durée | 1 heure de jeu | aucune dérive, aucun blocage |
| Consommation du sniffer | multimètre en série sur son USB | pour dimensionner la powerbank |
| **Isolation de la console** | courant tiré sur les piles, module branché vs débranché | **identique** — si le sniffer tire sur la console, c'est une erreur de câblage |

> ⚠️ Comme pour le module écran : **les latences ne se comparent qu'à conditions de lien
> identiques.** Le 2,4 GHz varie au fil de la journée et domine tout le reste. Relever un
> `ping` vers un nœud avant et après chaque campagne.

### F.5 ✅ Critère de sortie de la phase 4

- [ ] Latence photon-à-photon **< 16,7 ms** (≤ 4 images à 240 img/s)
- [ ] Les 3 mesures (caméra, analyseur, accusés) **concordent** à quelques ms
- [ ] `trames_incompletes` < 0,1 % sur 10 minutes
- [ ] Débit relevé conforme à 4,40 Mbit/s
- [ ] Le courant tiré sur les piles de la console est **inchangé**
- [ ] Décision tranchée, chiffres à l'appui : **pipeliner (§E.5) ou pas**

---

<a id="g"></a>
## G. Phase 5 — intégrer

| Sujet | Le point qui compte |
|---|---|
| **Antenne du Pico** | ⚠️ Le carré d'antenne du Pico 2 W ne doit avoir **ni masse, ni métal, ni batterie** dans son voisinage immédiat. C'est le point le plus souvent négligé, et il se paie en trames perdues qu'on attribue au firmware |
| **Alimentation** | Powerbank ou USB, **jamais les piles de la console**. Masses communes, VCC jamais reliés |
| **Sortie de la console** | Option élégante : réutiliser le **port link (EXT)**, qui est déjà un connecteur 6 points avec son ouverture dans la coque. Contrepartie : on sacrifie la fonction link. À trancher, pas avant que tout le reste marche |
| **Le cadre** | ⚠️ **N'est plus du ressort de ce module.** Depuis que l'émetteur est agnostique (plan §5.3), bordure, centrage et habillage appartiennent au module ÉCRAN, qui dispose de 16 px de marge horizontale et 24 px verticale |
| **Remontage** | Vérifier que le ruban LCD d'origine n'est pas pincé, et que le module ne force sur rien avant de visser |

---

<a id="h"></a>
## H. Ce qui peut encore invalider ce plan

Un plan honnête dit où il peut se tromper. Par ordre de probabilité décroissante :

| # | Hypothèse | Si elle est fausse | Gravité |
|---|---|---|---|
| 1 | **`CPL` est fiable et vaut 144 par trame** | basculer sur `CP`/`CPV` (GP5, déjà câblé) et compter 154 lignes dont 144 visibles | 🟢 prévu |
| 2 | **`LD0/LD1` sont stables autour d'un front de `CPG`** | régler `D`, ou échantillonner sur l'autre front | 🟢 prévu |
| 3 | **`in_shiftdir` gauche + `bswap` donnent le bon ordre** | inverser l'un des deux | 🟢 se voit en une image |
| 4 | **La période minimale de `CPG` est ≥ 200 ns** | si elle est nettement plus courte, le modèle du §A.1 est faux — **s'arrêter et comprendre** avant de coder | 🟡 |
| 5 | ~~**Le PPU supporte la charge ajoutée**~~ | **levé le 25/09/2026** : trois pointes d'analyseur (40–60 pF chacune, sans résistance série) n'ont rien dégradé. La prise définitive charge 2 à 3× moins | 🟢 |
| 6 | **Le tampon tient dans la coque** | le sortir par le port link ou la trappe à piles, en gardant les fils < 10 cm côté pastilles | 🟡 |
| 7 | **`IDX2` coûte ~1 ms par nœud côté écran** | si c'est nettement plus, développer par blocs de 4 pixels avec une table de 256 entrées pré-développées | 🟢 |
| 8 | **Les 3 nœuds tiennent 4,4 Mbit/s cumulés** | déjà mesuré à 24,6 Mbit/s pour un nœud seul le 18/09 : très large | 🟢 |
| 9 | **Les pastilles du ruban LCD sont soudables à la main** | replis du plan §3.5 : DMG d'abord, ou retour à `pixelpush` | 🔴 c'est le vrai risque du projet |

> Le risque n° 9 est le seul qui puisse arrêter le sous-projet, et c'est aussi le seul qui soit
> **entièrement décidé en phase 0** — avant toute dépense et toute soudure. C'est la raison
> d'être de l'ordre de travail décrit en tête de ce document.
