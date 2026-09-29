# Journal de la tête (v2)

Mesures, recettes et enquêtes de mise au point de ce firmware. Pour savoir ce que fait le
firmware et comment l'utiliser, voir le [README](README.md).

## Recette de la phase 5a — 29/09/2026

Une tête seule (Pico 2 W, `clk_sys` 266 MHz), rien de branché hormis l'USB. Émetteur :
`pixelpush --cible` depuis un PC portable **lui-même en WiFi** sur la même box, donc chaque
paquet traverse l'air deux fois (PC → box → tête).

| Critère | Attendu | Mesuré | |
|---|---|---|---|
| IDX8 192 × 192 à 60 images/s | 60 images/s, ≈ 17,7 Mbit/s | **60,00 images/s, 17,69 Mbit/s**, sur 10 minutes | ✅ |
| Perte sur 10 minutes | < 0,1 % | **1 image sur 36 001, soit 0,003 %** — et ce n'est pas une perte, voir plus bas | ✅ |
| Pixels par image et par rangée, 192 × 192 | 12 288 / 12 288 / 12 288 | identique sur les 61 relevés | ✅ |
| Pixels par image et par rangée, Game Boy | 6 400 / 10 240 / 6 400 | identique, image placée en (16, 24), 144 segments par image | ✅ |
| BGR888 192 × 192 à 25 images/s | ≈ 22 Mbit/s | **22,11 Mbit/s**, sans perte sur 60 s | ✅ |
| Sniffer en PXL1, sans modification | images comptées, accusés reçus | pas encore essayé | — |

### Les chiffres de la mesure de 10 minutes (IDX8 192 × 192)

| | |
|---|---|
| Images complètes | 36 000 |
| Images abandonnées | 1 — une tranche arrivée après le début de l'image suivante (`retard. 1`) |
| Paquets reçus | 972 473 |
| Doublons écartés | 146, soit 0,015 % des paquets |
| Rejets, resynchronisations | 0 |
| Réassemblage d'une image | 9,9 ms en moyenne, 57 ms au pire (un seul épisode) |
| Aller-retour vu du PC | médiane 13,0 ms |

L'image abandonnée n'a perdu aucun paquet : une de ses tranches est arrivée **après** la
première tranche de l'image suivante. Le seul défaut de la chaîne en 10 minutes est donc un
désordre franchissant une frontière d'image.

### Ce que dit le réassemblage : le débit instantané de l'air

Le réassemblage mesure le temps que met une rafale à traverser l'air :

| Format | Octets par image | Réassemblage | Débit instantané |
|---|---|---|---|
| IDX8 192 × 192 | 36 864 | 9,9 ms | 29,8 Mbit/s |
| BGR888 192 × 192 | 110 592 | 30,2 ms | 29,3 Mbit/s |
| IDX2 160 × 144 | 5 760 | 1,4 ms | 33 Mbit/s |

La chaîne PC → box → tête débite donc **≈ 29,5 Mbit/s en rafale**, dans ce montage. Deux
conséquences :

- la latence d'une image est d'abord son temps de traversée de l'air : 10 ms pour une image
  IDX8 pleine, 1,4 ms pour une image Game Boy ;
- le plafond soutenu de la phase 4 (24,6 Mbit/s) correspond à ~ 83 % de ce débit en rafale.
  Le BGR888 à 25 images/s en utilise 75 %.

Avec une source en Ethernet, le paquet ne traverserait l'air qu'une fois. Non mesuré.

## Les doublons : le WiFi en livre

Premier essai, 60 s d'IDX8 : 2 à 3 paquets de trop par tranche de 10 s, alors que l'émetteur
n'envoie chaque tranche qu'une fois. Compte exact : 16 207 paquets = 600 images × 27 tranches
+ 5 commandes + 2 retardataires. Les retardataires étaient des **doublons**.

Le danger n'était pas le doublon lui-même, mais la règle de complétion : la tête comptait les
octets reçus. Un doublon arrivé **avant** la vraie dernière tranche aurait fait déclarer
complète une image à laquelle il manquait une tranche. En phase 5b, les nœuds auraient
affiché cette tranche manquante avec le contenu d'une image précédente.

Correction : la tête retient les offsets des tranches déjà reçues pour l'image courante, et
écarte une tranche dont l'offset est connu. Une image est complète quand **toutes ses
tranches** sont là. Mesuré ensuite : 146 doublons écartés en 10 minutes, aucune image
faussée. La v1 publie à la tranche « dernière » même quand des octets manquent, qu'elle se
contente de compter : un doublon n'y change pas ce qui s'affiche, seulement ce compteur.

## Les accusés perdus au retour

`pixelpush` n'a reçu que 98,2 % des accusés, alors que la tête a complété toutes les images
et n'a signalé **aucun échec d'émission** (compteur `envois_echoues`, ajouté pour trancher).
Les accusés se perdent donc entre la tête et le PC.

Les pertes arrivent par salves : deux fenêtres de 5 s ont perdu 40 à 50 % de leurs accusés,
avec des allers-retours à 130 ms, pendant que la tête continuait de recevoir 60 images/s. Le
chemin aller tenait, le retour vers le PC décrochait.

**Hypothèse non vérifiée :** le balayage périodique des réseaux par le PC (NetworkManager),
qui éloigne sa radio du canal quelques dizaines de millisecondes. Ses propres émissions
attendent dans son tampon, mais ce qui lui est destiné est perdu. Sans effet sur l'écran :
seule la mesure de latence est concernée, et la source finale, le sniffer, n'est pas un PC.

## `pixelpush` s'arrêtait au bout de 40 s

`BlockingIOError` sur `sendto` : en IDX8 192 × 192, une image part en rafale de 27 paquets,
et le tampon d'émission du noyau (≈ 200 ko par défaut) débordait au premier hoquet du WiFi.
Le socket étant non bloquant, l'exception arrêtait l'émetteur. Tampon porté à 1 Mo ; un envoi
refusé attend 50 ms que le tampon se vide, puis le paquet est abandonné et compté. Aucun
abandon constaté ensuite, en 10 minutes.
