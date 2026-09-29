# Journal du firmware sniffer

Ce journal consigne, dans l'ordre où elles ont eu lieu, les mesures, les recettes et
les enquêtes menées sur le firmware. Pour savoir comment l'utiliser, voir le
[README](README.md).

*Terminée le 26/09/2026. Tous les critères de sortie sont atteints.*

## Le DMA à deux canaux, abandonné

> Une version antérieure du plan prévoyait deux canaux chaînés déroulant une
> table de 144 adresses, parce qu'une ligne de 40 octets devait aller dans un
> canevas de 48. Cette mécanique a disparu le 25/09/2026, quand l'émetteur est
> devenu agnostique de l'afficheur.

## Pourquoi l'image suffit comme preuve

> 🔑 **L'instrument ne peut pas mentir.** Contrairement au module écran, dont le
> compteur de trames annonçait 788 Hz parfaitement stables pendant que la dalle
> était noire, ici : si l'image est reconnaissable, la chaîne est juste.

## Recette — résultats du 26/09/2026

Console : Game Boy Pocket MGB-ECPU-01, alimentation de laboratoire à 3,2 V,
Pokémon Version Rouge. Liaison directe, sans tampon, 6 fils + masse.

```
  cadence          59.723 img/s   (attendu 59,727)  sur 181 s
  trames            10828
  lignes/trame        144        (attendu 144)
  trames douteuses      0
  mots restants         0
  debordements FIFO     0
  trames perdues        0
```

| Critère | Mesure | |
|---|---|---|
| Image reconnaissable | **écran de titre Pokémon Version Rouge, pixel exact** | ✅ |
| Cadence | **59,723 img/s**, écart **0,007 %** | ✅ |
| Lignes par trame | **144** sur **10 828 trames** | ✅ |
| Trames douteuses | **0** | ✅ |
| Mots restants | **0** — le DMA n'a jamais manqué un front | ✅ |
| Débordements FIFO | **0** | ✅ |

> 🔑 **Ce qui prouve la chaîne, c'est le petit texte.** « ©1995-1999 GAME FREAK inc. »
> est parfaitement lisible dans le PNG. Une erreur d'un seul pixel — front mal choisi,
> octet inversé, décalage de bit — l'aurait réduit en bouillie. Voir
> [`../../docs/releves/phase2-premiere-trame.png`](../../docs/releves/phase2-premiere-trame.png).

**Transitoire de démarrage.** Le premier essai montrait 8 trames douteuses et
4 débordements FIFO, tous **figés** — le DMA armé au milieu d'une trame rend les
premières incomplètes. La commande `r` a été ajoutée pour le démontrer plutôt que
le supposer : après remise à zéro, 10 828 trames sans une seule erreur.

## Deux bugs trouvés en éprouvant le firmware

**1. `GP5` jamais initialisée.** `capture::init()` appelait `gpio_init()` sur GP3 et
GP4 mais pas sur GP5, la réserve. Sans cet appel l'entrée du pad reste désactivée et
`gpio_get()` renvoie 0 quoi qu'il arrive sur le fil. Le diagnostic annonçait donc
« fil non branché » sur un câblage sain.

> ⚠️ **Un diagnostic qui ment est pire qu'un diagnostic absent** : il envoie démonter
> ce qui marche. Corrigé, `GP5` donne 1 840 transitions sur 100 ms — l'attendu étant
> 9 196 Hz × 2 × 0,1 = 1 839.

**2. Cadence mesurée sur une fenêtre d'une seconde.** Elle ne rendait que des entiers,
59 ou 60, et ne permettait pas de vérifier les 59,727 attendus. Moyennée sur toute la
durée d'observation, la résolution tombe à 0,017 img/s sur 60 s.

## Cycle d'extinction de la console — 26/09/2026

Le cas d'usage réel : dans une installation, la console et le Pico ne
s'allumeront jamais exactement en même temps.

**Protocole** : compteurs remis à zéro, ligne de base propre (657 trames,
0 erreur), console coupée ~10 s, rallumée, puis observation.

| Observation | Résultat |
|---|---|
| Pendant la coupure | compteurs **gelés**, aucun plantage. Le PIO reste bloqué sur son `wait` |
| Au rallumage | **5 trames douteuses, 5 débordements** — le transitoire attendu, le DMA étant armé au milieu d'une trame |
| **Resynchronisation** | **automatique**, sans intervention |
| Après remise à zéro | **3 566 trames, 0 erreur**, cadence **59,728** (écart 0,002 %) |
| **Image** | **correcte, non décalée** — voir `phase2-apres-cycle.png` |

> 🔑 **La cadence moyenne affichée pendant l'essai — 55,769 — n'était pas un défaut.**
> 9 537 trames à 59,727 img/s représentent 159,7 s de fonctionnement ; l'observation
> durait 171 s. La différence, **11,3 s**, est exactement la durée de la coupure. Une
> moyenne qui inclut un trou n'est pas une cadence dégradée.

> ℹ️ Détail qui confirme que la capture est **vivante** : l'écran de titre affiche
> Chenipan sur la première trame et Pikachu après le cycle. Il fait défiler ses
> sprites — ce n'est pas une trame figée en cache.

## Endurance — 30 minutes, 26/09/2026

Compteurs remis à zéro, relevé toutes les 5 secondes, journal complet conservé.

```
  cadence          59.727 img/s   (attendu 59,727)  sur 1812 s
  trames           108233
  lignes/trame        144
  trames douteuses      0
  mots restants         0
  debordements FIFO     0
  trames perdues        0
```

> 🔑 **363 relevés, et les quatre compteurs d'erreur n'ont jamais pris d'autre
> valeur que 0.** Pas « terminé à zéro » : *jamais* rien d'autre, à aucun des
> 363 instants observés. C'est l'intérêt d'avoir gardé le journal plutôt que le
> seul total — « 3 erreurs » et « 3 erreurs d'un coup à la 22ᵉ minute » ne se
> diagnostiquent pas pareil.

| Grandeur | Valeur |
|---|---|
| Trames | **108 233** en 1812 s, croissance monotone |
| Cadence finale | **59,727 img/s** — la valeur théorique à trois décimales |
| Cadence, après convergence de la moyenne | min **59,725**, max **59,738**, amplitude **0,013** |
| Image après 30 min | **correcte** — `phase2-apres-30min.png` |

## Émission réseau — phase 3, 26/09/2026

Le sniffeur émet la trame native en `PXL1`/`IDX2` vers **un** récepteur. Le
récepteur de référence est `tools/ecran_virtuel.py` sur le PC.

```
  reseau           192.168.1.84  ->  192.168.1.73:4242
  trames emises      2218
  paquets           11128        (5 par trame)
  echecs d'envoi        0
  commandes            38        (palette + geometrie)
  accuses recus      2000        (90 % des trames)
  aller-retour     min 4.56   moy 6.23   max 54.26 ms   sur 1991 mesures
```

Côté écran virtuel, sur la même fenêtre : **59,71 img/s, 0 incomplète, 0 rejet,
0 resync, 2,79 Mbit/s**. Et côté capture : **2 218 capturées = 2 218 émises =
0 perdue.**

Preuve de bout en bout : `../../docs/releves/phase3-bout-en-bout.png` — l'image
est en vert DMG, teintes transmises par `PXL1_CTRL_PALETTE`, donc le chemin des
commandes fonctionne aussi.

### ⚠️ Tout tourne sur le cœur 0 — le plan avait tort

Le plan §5.1 prévoyait « capture sur le cœur 0, réseau sur le cœur 1 ». C'est
inutile : **la capture n'a pas besoin d'un cœur.** PIO et DMA travaillent seuls,
et les deux interruptions s'exécutent de toute façon sur le cœur qui les a
armées.

Pire, c'était nuisible : `cyw43_arch_init` appelé depuis le cœur 1 n'a pas
démarré, et `multicore_launch_core1` ne rend la main que si le cœur 1 signale
son départ. Résultat : cœur 0 bloqué, **console morte, bascule 1200 bauds
impossible**, et un BOOTSEL physique pour reprendre la main.

Le module écran sert lwIP sur le cœur 0 depuis le début. Il aurait fallu
reprendre ce qui était validé plutôt que suivre une ligne du plan écrite avant
qu'on sache tout ça.

### 🔑 La porte de sortie au démarrage

```
[ une touche dans les 3 s = demarrer SANS reseau ]
```

Écrite juste après s'être fait avoir. Une touche pendant ces trois secondes
démarre sans réseau : la capture continue, la console reste vivante, et on garde
de quoi reflasher. **Un blocage réseau ne peut plus rendre le Pico
inaccessible.**

### Deux corrections d'instrument, du même genre que les précédentes

**La latence était mesurée par un filtre IIR** (`moy = (moy×7 + dt)/8`), et
`reinitialiser()` le préservait d'un régime à l'autre. Ça a donné « instantané
3,11 ms, moyenne 29,49 ms » — deux chiffres qui ne peuvent pas être vrais
ensemble. Remplacé par min / vraie moyenne / max sur un nombre d'échantillons
affiché, plus un compteur de mesures écartées : **un instrument qui jette des
mesures en silence n'est pas un instrument.**

**L'horodatage se fait maintenant AVANT le premier paquet**, pas après le
dernier : l'accusé peut revenir en 4,5 ms, donc avant qu'on ait fini de pousser
les cinq. La mesure inclut le temps d'émission, ce qui est le délai qu'on veut
réellement connaître.

### Le seul transitoire restant

À la première mesure, 641 trames perdues sur 941. Elles datent toutes de
l'**association WiFi** : la boucle principale est bloquée dans
`cyw43_arch_wifi_connect_timeout_ms` pendant ~11 s, alors que la capture, elle,
tourne (PIO, DMA, interruptions). 11 s × 59,7 = 657, ce qui colle. Après remise
à zéro : **0 perdue**.

## Phase 4 — latence décomposée et distribuée, 26/09/2026

### B · La décomposition, sur 3 920 trames

```
  attente  capture->envoi  min   0.00  moy   0.01  max   0.14 ms
  envoi    5 paquets       min   1.89  moy   1.94  max   2.54 ms
  aller-retour envoi->acc  min   4.47  moy   7.68  max 160.64 ms
```

**La boucle principale prend la trame en 0,01 ms.** Sa réactivité n'est pas un
sujet, et on peut cesser de la soupçonner.

> ⚠️ **Pousser 5 paquets coûte 1,94 ms, et j'en avais estimé 0,3.** Un facteur 6.
> C'est ~0,39 ms par `udp_sendto` — `pbuf_alloc`, la recopie de 1 400 octets, et
> la liaison SPI vers le CYW43. Ça ne bloque rien à 59,7 img/s (11,6 % de la
> période), mais ça pèse dans le budget de latence, et l'estimation ne le
> voyait pas.
>
> Piste si ça devient limitant : `PBUF_REF` au lieu de `PBUF_RAM` éviterait la
> recopie. Le double tampon garantit que la trame reste valide 16,7 ms, bien
> au-delà du besoin.

### A · L'histogramme de l'aller-retour, 3 778 mesures

```
      4 ms |                                             59   1.56 %
      5 ms |########################################   2448  64.80 %
      6 ms |############                                778  20.59 %
      7 ms |##                                          141   3.73 %
      8 ms |##                                          143   3.79 %
      9..31|  (étalé)                                   ~120   3.2 %
   >=32 ms |#                                            81   2.14 %
```

| | |
|---|---|
| **91 %** des trames | sous 9 ms |
| ~3 % | entre 9 et 32 ms |
| **2,14 %** | **au-delà de 32 ms**, max 160,64 ms |

**La queue est réelle mais mince** : environ **1,3 trame par seconde** arrive en
retard. C'est le comportement du WiFi 2,4 GHz avec ses retransmissions, et rien
côté Pico ne le corrigera.

> ℹ️ Ces trames-là ne sont pas **perdues** : le récepteur n'a compté qu'**une**
> trame incomplète sur 3 937. Elles arrivent, en retard. Sur un afficheur, ça se
> voit comme une micro-saccade occasionnelle, pas comme un trou.

### Le budget de latence qui en découle

| Poste | Moyenne | Meilleur cas | Source |
|---|---|---|---|
| Attente de fin de trame | **8,97 ms** | 1,19 ms | arithmétique : `16,74 − L × 0,109` |
| Boucle principale | 0,01 ms | 0,00 | mesuré |
| Émission des 5 paquets | 1,94 ms | 1,89 ms | mesuré |
| WiFi, aller simple | ~3,8 ms | ~2,2 ms | aller-retour / 2 |
| **Total** | **~14,8 ms** | **~5,3 ms** | |

> 🔑 **La moyenne passe tout juste sous une trame (16,74 ms), et le poste
> dominant reste l'attente de fin de trame — 61 % du total.** Ce n'est ni le
> WiFi, ni le Pico.
>
> **L'émission pipelinée** (un paquet dès ses 35 lignes capturées, §E.5
> variante 2) ramènerait ce poste de 8,97 à **1,9 ms**, soit un total moyen de
> **~7,7 ms**. Le compteur de lignes sur `P2-ST` existe déjà : c'est un `if`.

## Phase 4 C — robustesse, 26/09/2026

### ⚠️ Le trou noir : il n'y avait aucune reconnexion

Constat de **lecture de code**, pas d'essai : si l'AP disparaissait, `udp_sendto`
échouait indéfiniment et rien ne reprenait jamais. Dans une installation qui
tourne des heures, ça arrive.

Ajouté : surveillance du lien toutes les secondes, et réassociation
**asynchrone** — `cyw43_arch_wifi_connect_timeout_ms` bloque jusqu'à 30 s, et
c'est exactement ce qui a fait perdre 641 trames au démarrage.

Et surtout, **une commande `d` qui rompt volontairement l'association** : une
panne qu'on ne sait pas provoquer est une panne qu'on ne sait pas corriger.

### C1 · Coupure WiFi provoquée

```
  rupture VOLONTAIRE de l'association
  reseau PERDU (etat 0) — la capture continue
     cadence 59.754 img/s      <-- inchangée pendant toute la coupure
  reseau RETABLI : 192.168.1.84
  deconnexions 1   reconnexions 1
```

| | |
|---|---|
| Détection | **< 1 s** |
| Rétablissement | **~5 s**, même adresse IP |
| **Cadence de capture pendant la coupure** | **59,73 à 59,75 img/s — inchangée** |
| Échecs d'envoi | 35, tous pendant la coupure, figés ensuite |

> 🔑 **La capture n'a pas bronché.** PIO, DMA et interruptions ne dépendent pas
> du réseau — c'était l'hypothèse de conception, elle est maintenant vérifiée au
> lieu d'être supposée.

### C2 · Le récepteur disparaît et revient

Écran virtuel tué 8 secondes, puis relancé :

| | Avant | Après |
|---|---|---|
| Cadence | 59,80 img/s | **59,82 img/s** |
| Incomplètes, retards, resyncs, rejets | 0 | **0, 0, 0, 0** |

Le récepteur reprend **sans une seule erreur**. Côté sniffeur, l'émission n'a
jamais cessé : `echecs d'envoi` est resté à 35, car un envoi UDP vers un hôte
qui n'écoute pas ne fait pas échouer `udp_sendto`.

## Phase 4 D — l'émission pipelinée, 26/09/2026

Une tranche part **dès que ses 35 lignes sont capturées**, au lieu d'attendre la
fin de la trame. Le découpage de 1 400 octets tombe exactement sur 35 lignes de
40, donc l'interruption de ligne sait quand une tranche est complète.

> ⚠️ J'avais annoncé « c'est un `if` ». C'est faux : il faut émettre depuis le
> tampon **en cours de remplissage**, donc une file de tranches alimentée par
> les interruptions et drainée par la boucle principale. Les octets d'une
> tranche ne sont plus touchés d'ici la fin de la trame, ce qui rend la chose
> sûre — mais ce n'est pas un `if`.

### Les deux modes, dos à dos sur la même console

| | Simple | **Pipeliné** |
|---|---|---|
| Attente de fin de trame *(arithmétique)* | 8,97 ms | **1,96 ms** |
| Émission sur le chemin critique | 1,89 ms *(5 paquets)* | **0,39 ms** *(le dernier)* |
| Aller-retour, dernier paquet → accusé | 4,50 ms | **3,53 ms** |
| Aller-retour, maximum | 84,79 ms | **46,52 ms** |
| **Latence totale moyenne** | **~13,1 ms** | **~4,1 ms** |
| Cadence | 59,725 img/s | 59,742 img/s |
| Trames douteuses / perdues / tranches perdues | 0 / 0 / 0 | **0 / 0 / 0** |
| Récepteur : incomplètes | 0 sur 2 688 | 1 sur 6 275 |

**Facteur 3,2 sur la latence**, et l'image reste exacte — voir
`../../docs/releves/phase4-pipeline.png`.

> 🔑 **L'aller-retour s'améliore aussi, et je ne l'avais pas prévu** : 4,50 →
> 3,53 ms de moyenne, 84,79 → 46,52 ms au maximum. Étaler les 5 paquets sur la
> trame au lieu de les envoyer en rafale réduit la congestion instantanée, et le
> WiFi encaisse mieux. Le gain ne vient donc pas seulement de l'attente
> supprimée.

Le mode est **commutable à chaud** par la commande `P`, ce qui a permis de
mesurer les deux dans les mêmes conditions — même console, même image, même
lien WiFi, à une minute d'intervalle. Une comparaison entre deux séances
n'aurait rien valu : le 2,4 GHz varie au fil de la journée.

## Critère de sortie de la phase 2

- [x] Le vidage ASCII montre un écran **reconnaissable**
- [x] Le PNG est net : pas de décalage, pas de cisaillement, pas de groupes de 4 inversés
- [x] **59,73 img/s** ± 0,1 → **59,723**
- [x] **144 lignes/trame sur 10 000 trames** → **10 828**, `trames douteuses` = 0
- [x] `mots restants` = 0 et `débordements` = 0 sur la même durée
- [x] L'image reste correcte après un **cycle d'extinction** de la console
- [x] L'image reste correcte après **30 min** de capture continue

La table de diagnostic symptôme → cause est dans
[`../../docs/etapes-detaillees.md`](../../docs/etapes-detaillees.md) §D.10.
