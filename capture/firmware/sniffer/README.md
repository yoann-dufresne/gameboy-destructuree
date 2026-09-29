# Firmware sniffer

Firmware du Raspberry Pi Pico 2 W du module capture. Il échantillonne le bus LCD de
la Game Boy Pocket, reconstitue chaque image de 160×144 pixels en `IDX2`, et l'émet
en `PXL1` vers un récepteur UDP.

Présentation du module, matériel, construction et flashage : [README du module](../../README.md).
Mesures, recettes et incidents : [JOURNAL.md](JOURNAL.md).

## Configuration

| Fichier | Contenu |
|---|---|
| `include/secrets.h` | nom et mot de passe du réseau WiFi, code pays. À créer depuis `secrets.h.example` ; ignoré par git |
| `include/config.h` | **`PXL1_CIBLE_IP`**, l'adresse du récepteur. Aussi : brochage, palette, mode d'émission par défaut. Chaque valeur mesurée y est justifiée par sa mesure |

Le récepteur écoute sur le port UDP 4242. Par défaut, c'est le
[module écran](../../../ecran/README.md) : il émet son propre réseau WiFi, que le sniffer
rejoint en direct, sans box — `secrets.h` reprend son nom et son mot de passe, et la cible
est sa tête, en 192.168.4.1. Pour travailler sans l'écran, le récepteur peut être un PC qui
fait tourner [`tools/ecran_virtuel.py`](../../tools/ecran_virtuel.py) : `secrets.h` vise
alors le réseau du PC, et `PXL1_CIBLE_IP` son adresse.

## Brochage

| Broche du Pico | Point de test de la console | Signal |
|---|---|---|
| GP0 | `P2-LD0` | donnée, bit 0 |
| GP1 | `P2-LD1` | donnée, bit 1 |
| GP2 | `CP` | horloge pixel |
| GP3 | `P2-ST` | verrou de ligne, 144 par image |
| GP4 | `P2-S` | départ d'image (VSYNC) |
| GP5 | `P2-CPL` | horloge de ligne, 154 par image — en réserve |
| GP20, GP21 | — | sorties de mesure pour l'analyseur : bascule à chaque image capturée, à chaque image émise |

GP0 à GP2 doivent rester contigus : le PIO les lit comme un groupe. L'identification
des signaux, mesurée sur la carte, est dans
[`docs/signaux-mgb.md`](../../docs/signaux-mgb.md) §3bis.

## Au démarrage

La console passe par l'USB : l'UART par défaut du SDK est sur GP0 et GP1, occupés par
les données.

Le firmware attend d'abord **3 secondes** : une touche pressée pendant ce délai démarre
**sans réseau**. La capture et la console fonctionnent alors normalement. C'est la porte
de sortie si le WiFi bloque le démarrage : on garde de quoi reflasher sans avoir à
appuyer sur BOOTSEL.

Ensuite, le Pico s'associe au WiFi et commence à émettre. S'il perd le réseau, il tente
de se réassocier chaque seconde, sans interrompre la capture. Compteurs de capture et
état du réseau s'affichent toutes les 5 secondes.

## Commandes de la console

| Touche | Effet |
|---|---|
| `a` | image en ASCII, 80×72 caractères : aucun outil nécessaire |
| `p` | image en hexadécimal, lue par `tools/gbdump.py` |
| `s` | compteurs de capture |
| `n` | état du réseau et compteurs d'émission |
| `l` | histogramme de l'aller-retour réseau |
| `g` | état brut des 6 entrées : pour vérifier qu'un fil est bien branché |
| `r` | remise à zéro des compteurs |
| `d` | rupture volontaire de l'association WiFi, pour éprouver la reconnexion |
| `P` | bascule entre émission simple et émission pipelinée, et remet les compteurs à zéro |
| `h` | aide |

[`tools/sniffer.py`](../../tools/sniffer.py) envoie ces commandes depuis un script.

## Lire les compteurs

| Compteur | Attendu | Un écart signifie |
|---|---|---|
| `cadence` | 59,73 img/s | 29,86 : la capture se déclenche sur un signal à mi-fréquence |
| `lignes/trame` | 144 | `P2-ST` manque des impulsions, ou en invente |
| `trames douteuses` | 0 | idem, cumulé sur toutes les images |
| `mots restants` | 0 | des fronts d'horloge pixel ont été manqués : mauvais front ou délai d'échantillonnage |
| `debordements FIFO` | 0 | le DMA ne suit pas le PIO |
| `trames perdues` | — | des images capturées n'ont pas été lues par la boucle principale |
| `tranches perdues` | 0 | en mode pipeliné, la file des paquets à émettre a débordé |

Côté réseau (`n`) : images et paquets émis, échecs d'envoi, commandes émises (palette
et géométrie), état du lien, déconnexions et reconnexions, accusés reçus du récepteur,
et aller-retour minimal, moyen et maximal.

Au démarrage, ou quand la console est rallumée, quelques images douteuses sont
normales : la capture a repris au milieu d'une image. `r` remet les compteurs à zéro ;
une erreur qui apparaît ensuite est réelle. La table symptôme → cause complète est dans
[`docs/etapes-detaillees.md`](../../docs/etapes-detaillees.md) §D.10.

## Obtenir une image

```bash
../../tools/gbdump.py --echelle 4       # en gris, 640×576
../../tools/gbdump.py --palette dmg     # les 4 verts de la Game Boy d'origine
../../tools/gbdump.py --palette diag    # une couleur franche par valeur de pixel
```

La palette `diag` rend visible au premier coup d'œil une erreur d'ordre des bits.

## Sources

| Fichier | Rôle |
|---|---|
| `include/config.h` | tout ce qui vient d'une mesure, avec la mesure qui le justifie |
| `include/pxl1.h` | le protocole, copié du module écran (voir `PROVENANCE.txt`) |
| `src/capture.pio` | le programme PIO : trois instructions |
| `src/capture.hpp`, `src/capture.cpp` | PIO, DMA, interruptions, double tampon, file des tranches |
| `src/net/reseau.hpp`, `src/net/reseau.cpp` | WiFi, émission `PXL1`, accusés, reconnexion, mesure de latence |
| `src/net/lwipopts.h` | configuration de la pile réseau lwIP |
| `src/main.cpp` | démarrage, boucle principale, console |

## Conception

**Le processeur ne touche aucun pixel.** Le PIO échantillonne, le DMA écrit en mémoire.
Le processeur compte les lignes et réarme le DMA une fois par image, pendant les 1,09 ms
où l'écran ne reçoit rien (VBlank).

**Échantillonner sur le front descendant.** Le programme PIO attend un front descendant
de l'horloge pixel, puis lit `LD0` et `LD1` :

```
wait 1 pin 2          ; garantit qu'on verra le prochain front descendant
wait 0 pin 2 [0]      ; front descendant
in   pins, 2          ; LD0 et LD1
```

Les données changent sur le front montant ; le front descendant tombe au milieu de
leur fenêtre de stabilité, avec 75 ns de marge avant et 113 ns après.

**Un seul transfert DMA par image.** Les 144 lignes de 40 octets sont contiguës en
mémoire : 1 440 mots de 32 bits, un transfert. Le DMA compte des mots, pas des lignes :
un front d'horloge manqué décalerait tout le reste de l'image. D'où le contrôle
d'intégrité : `P2-ST` doit battre 144 fois par image.

**L'ordre de la séquence VSYNC compte.** À chaque départ d'image, dans cet ordre :

1. relever le compte de lignes ;
2. arrêter le DMA, relever ce qu'il n'a pas écrit ;
3. vider la FIFO et redémarrer le PIO ;
4. basculer le tampon ;
5. réarmer et relancer.

Vider la FIFO avant d'arrêter le PIO le laisserait la remplir à nouveau. Basculer le
tampon avant d'arrêter le DMA le laisserait écrire dans l'image qu'on publie : une
déchirure intermittente, difficile à trouver.

**Émission par tranches.** Une image de 5 760 octets part en 5 paquets UDP de
1 400 octets au plus, soit 35 lignes chacun. En mode pipeliné (par défaut), une tranche
part dès que ses 35 lignes sont capturées, sans attendre la fin de l'image. La palette
et la géométrie sont renvoyées toutes les 2 secondes, pour qu'un récepteur redémarré
retrouve seul de quoi interpréter le flux.

**Tout tourne sur le cœur 0.** PIO et DMA travaillent sans processeur, et la pile
réseau est servie en arrière-plan. Le second cœur reste libre.

Les décisions et leur justification sont dans le
[plan de réalisation](../../docs/plan-firmware.md).
