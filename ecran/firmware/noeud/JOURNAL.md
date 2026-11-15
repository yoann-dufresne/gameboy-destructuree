# Journal du nœud (v2)

Mesures, recettes et enquêtes de mise au point de ce firmware. Pour savoir ce que fait le
firmware et comment l'utiliser, voir le [README](README.md).

## Banc de la phase 5b — 29/09/2026

Le montage des phases 0 à 4, inchangé côté dalle : un Pico 2 W câblé en HUB75 sur une dalle
64 × 64. Il reçoit le firmware du nœud. Une tête en mode banc (`-DBANC_UNE_DALLE=ON`, canevas
64 × 64) lui est reliée par des **fils volants** — D0, D1, CLK, CS, VSYNC, RDY et trois
masses —, sans les résistances série prévues. Liaison à 16 MHz, soit 32 Mbit/s. Source :
`pixelpush` depuis le PC, en IDX8, via la box.

**Premier message :** `HELLO : rangee 0, canevas de 64 px`, reçu sans erreur au premier
démarrage.

**40 s, dalle éteinte, puis 3 minutes d'animation, dalle allumée :** 600 images reçues par la
tête et 600 publiées par le nœud par tranche de 10 s ; 0 erreur de CRC, 0 resynchronisation,
0 refus de validation, 0 VSYNC sans image. À l'œil : le point qui fait le tour du cadre
avance d'un pas régulier, l'animation est fluide. Aller-retour médian vu du PC : 5,4 ms.

**Critère de la liaison, 10 minutes :**

| | |
|---|---|
| Images publiées par le nœud | 34 573 |
| Erreurs de CRC | **0** |
| Resynchronisations, refus, VSYNC vides | 0, 0, 0 |
| Abandons de synchronisation côté tête | 0 |

**Aucune erreur sur la liaison en 10 minutes, à 16 MHz, sur des fils volants sans
résistances série.** Le critère de la phase 5b sur la liaison est atteint. Reste à relever la
latence à l'analyseur logique (GP18 de la tête, GP17 du nœud).

**Mais des gels visibles** pendant ces 10 minutes, qui ne venaient ni du nœud ni de la
liaison : la radio, entre le PC et la tête, via la box. Enquête et décision dans le
[journal de la tête](../tete/JOURNAL.md) : l'écran émet désormais son propre réseau WiFi.

## Une rangée de 3 dalles — 25 et 26/10/2026

Premier nœud sur une chaîne de trois dalles : la rangée du milieu (port L1 de la tête), puis
la rangée du haut (L0). Firmware construit avec `-DCHAIN_LEN=3`. Les dalles de ces rangées
sont une autre série que celle du banc : nappe **grise** à liseré rouge (fil 1), connecteur
`J1-IN` repéré par un triangle ▽ sur la broche 1, et l'ergot de la prise du côté de la
colonne impaire.

### L'horloge pixel : nette jusqu'à 23,7 MHz sur la chaîne

Une dalle seule restait nette à 29,6 MHz (16/09/2026). Sur trois dalles, mire à bandes
(option `MIRE_COLONNES`) :

| Horloge pixel (`HUB75_CLKDIV`) | Résultat |
|---|---|
| 29,6 MHz (1) | première dalle juste, deuxième en blocs de couleur, troisième noire |
| 26,9 MHz (1,1) | lignes blanches parasites sur les 2ᵉ et 3ᵉ dalles |
| 23,7 MHz (1,25) | net |
| 19,7 MHz (1,5) | net |
| 9,9 MHz (3) | net |

Retenu : **19,7 MHz**, soit ≈ 175 Hz de rafraîchissement en 10 plans (26,8 × 19,7 / 3) : au-dessus
des 150 Hz visés, et 27 % sous le premier palier qui casse. C'est désormais le défaut du
`CMakeLists.txt` dès que `CHAIN_LEN` dépasse 1. Le plafond est bien celui que le plan
craignait (§2.2) : chaque dalle retransmet le signal et ajoute un étage de retard.

### Trois pannes de câblage, et comment elles se sont montrées

- **Les cinq fils d'adresse branchés dans l'ordre des trous** (trou 8 sur GP6, trous 9 à 12 sur
  GP7 à GP10). Sur la dalle, `E` est au trou 8, **avant** `A` ; sur le Pico, GP10 vient après
  GP6–GP9. Symptôme : cadre juste, mais diagonale en trois segments pointillés deux fois trop
  pentus — chaque moitié de dalle affichait ses lignes paires puis impaires. Le cadre ne le
  montre pas : ses deux lignes, 0 et 63, tombent juste malgré la permutation.
- **Un Pico à la broche GP1 hors service.** Moitié haute de la rangée verte en continu. Au
  multimètre, avec un firmware qui fixe trois niveaux (`phase0_niveaux_rgb1`) : GP1 commandé
  à 0 V reste à 2,6 V, même débranché de la nappe. Pico écarté ; il ne peut servir ni de nœud
  (G1) ni de tête (D1 de L0). Le 29/10, patte ressoudée puis autotest (`phase0_test_broches`) :
  GP1 commandé à 0, même à 12 mA, se relit à 1 **dans la puce**, à chaque tour (5 664 sur
  5 664) ; les 25 autres broches sont saines, aucun pont de soudure. Le défaut est dans le
  RP2350, pas dans la soudure : aucune réparation possible. Pas de sniffer non plus (LD1).
  Mesure plus fine ensuite : GP1 part de 0, monte à 1 quand on le commande, puis **ne
  redescend plus**, quel que soit le tirage. Les 2,6 V du 26/10 étaient une broche que plus
  rien ne tirait vers le bas, tenue haute par la fuite de l'errata RP2350-E9. Ce Pico redevient
  un nœud grâce à la broche de rechange de G1 (plus bas).
- **Le fil VSYNC du nœud 1 délogé** en ajoutant celui du nœud 0 sur la même broche de la tête :
  messages et validations normaux, 0 VSYNC reçu, 0 image publiée, et la mire d'attente qui
  restait affichée. Piège de numérotation : VSYNC est sur **GP12 = broche 16** de la tête, et
  sur **GP16 = broche 21** du nœud.

Reste un défaut matériel isolé : une LED éteinte au coin d'une dalle (sa couleur bleue au
moins), à confirmer sur une image blanche.

### Bout en bout

Sniffer → tête → nœuds 0 et 1, Game Boy allumée : 59,7 images/s publiées par chaque rangée,
0 erreur de liaison, 0 abandon. Rangée 1 : 10 240 pixels par image (64 lignes × 160) ;
rangée 0 : 6 400 (les 40 lignes du bas, sous la marge de 24).

### Robustesse aux débranchements — 26/10/2026

Exigence posée ce jour : **l'écran doit reprendre seul, quel que soit l'ordre des
débranchements et rebranchements**. Deux blocages trouvés côté nœud :

- **Un fil VSYNC qui saute figeait la rangée.** Mode secours : après 8 images validées sans
  un seul VSYNC (`VALIDATIONS_AVANT_SECOURS`), le nœud publie lui-même chaque image validée et
  tient RDY haut pour ne pas retenir les autres rangées ; le premier VSYNC l'en fait sortir.
  Pour qu'il ne publie jamais un tampon à moitié réécrit, `rangee.hpp` invalide une image
  validée dès que son tampon reçoit une autre image (compteur `perimees`) : c'est le cas
  d'une image que la tête a abandonnée avant de reprendre son tampon. Test sur PC ajouté ;
  il échoue sans la correction.
- **Une tête redémarrée en plein message pouvait bloquer la réception** plus d'une minute : un
  faux en-tête vraisemblable faisait attendre jusqu'à 1 400 octets, quand seuls les HELLO
  circulent. Un message entamé et toujours incomplet après 20 ms (`MESSAGE_INACHEVE_US`)
  provoque maintenant une resynchronisation (compteur `inacheves`).

Éprouvé sur le montage, Game Boy allumée :

| Test | Résultat |
|---|---|
| Fil VSYNC du nœud 1 retiré 25 s, puis remis | mode secours en 0,1 s, rangée à 59,5 images/s ; sortie au retour du fil |
| USB du nœud 0, puis du nœud 1, débranché puis rebranché, fils en place | l'autre rangée continue ; reprise seule au rebranchement (côté tête : voir son journal) |
| Tête reflashée en marche | nœuds sur leur mire au bout de 2,5 s, puis reprise à 59,7 images/s, 0 erreur |

En manipulant le fil VSYNC, 2 187 erreurs de liaison en quelques secondes : les fils de
données voisins bougeaient. Chacune a été rattrapée seule, mais les fils volants ne sont pas
une solution d'installation : la nappe IDC 10 points du plan (§2.6) s'impose.

## G1 de rechange — 29/10/2026

Pour qu'un Pico dont GP1 est mort serve quand même de nœud, G1 sort aussi sur **GP15
(broche 20)**, en permanence : on déplace le seul fil du trou 2, sans changer de firmware.

Déplacer G1 seul n'est pas prévu par le pilote. `out pins, 6` écrit les couleurs sur 6
broches contiguës, et GP6–GP13 appartiennent à l'autre machine du même bloc PIO. Décaler tout
le groupe aurait coûté une dizaine de fils. La copie voyage donc dans les 2 bits de bourrage
de chaque octet de plan : le programme qui fabrique les plans y recopie G haut, et le
programme de flux la pose sur GP15 par `set pins`, après un saut calculé (`out pc`). Le
rythme reste de 9 cycles par pixel, et G1 est posé 3 cycles avant le front d'horloge. Détail
dans `vendor/hub75-jupfu/src/hub75_rechange.pio` et `PROVENANCE.txt`.

Contrôle des sorties au démarrage : chaque sortie est commandée à 1 puis à 0, tirée vers le
haut. L'ordre compte : un premier contrôle « 0 puis 1 », avec le tirage par défaut, déclarait
GP1 sain, puisqu'il part de 0. Une dalle hors tension fait échouer toutes les broches à la
fois, ses entrées non alimentées tirant vers 0 : au-delà de deux, le contrôle se déclare non
concluant au lieu de condamner le Pico.

Sur le Pico abîmé, sans dalle : contrôle « ⚠ sorties HS : GP1 », avec le fil à déplacer.

Sur dalle, le 29/10 : le Pico abîmé pilote la rangée du bas, fil du trou 2 sur GP15. Flux de
la Game Boy à 59,7 images/s, aucune erreur ; vert et blanc justes dans la moitié haute des
dalles, vus par Yoann. La recopie de G1 est validée.
