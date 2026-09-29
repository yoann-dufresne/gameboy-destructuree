# Nœud WiFi autonome (v1)

Firmware d'un nœud de la v1 du module écran : un Pico 2 W qui pilote une dalle 64 × 64 et
reçoit lui-même ses images par WiFi, en PXL1. C'est le firmware des phases 1 à 4. En v2, il
servira de base au firmware des nœuds (phase 5b), où la réception WiFi sera remplacée par la
liaison filaire venant de la tête.

## Ce qu'il fait

1. Il affiche une mire de recette et vérifie seul que le pilote affiche vraiment, en sondant
   les broches de données et d'adresse : au repos, puis avec le cœur 0 saturé de calcul.
2. Il se connecte au WiFi et annonce sur la console son adresse IP et le format attendu.
   Sans réseau, la mire reste affichée.
3. Il affiche chaque image PXL1 dès qu'elle est complète, en BGR888 ou en IDX8, et renvoie un
   accusé à l'émetteur. Les commandes de palette et de luminosité sont appliquées au vol.
4. Toutes les 10 s, il écrit un rapport : images reçues, pertes, et latence interne
   décomposée (assemblage, attente, rendu).

## Construire et utiliser

Procédure générique : [README du module](../../README.md#construire-flasher-observer). Ici,
`secrets.h` est nécessaire et la cible produit `build/ecran.uf2`.

Pour lui envoyer des images, [`pixelpush`](../../tools/pixelpush/README.md) **sans**
`--cible` : il parle alors PXL1 et lit l'adresse du nœud dans `layout-1x1.toml`.

## Sources

| Fichier | Rôle |
|---|---|
| `include/config.h` | tout ce qui dépend du matériel : brochage, géométrie, rendu, horloge. Aucune constante matérielle ailleurs |
| `include/display.hpp`, `src/display.cpp` | la façade d'affichage — `init()`, `backbuffer()`, `present()`, `occupe()`, `node_id()` — au-dessus du pilote vendorisé |
| `include/reseau.hpp`, `src/net/reseau.cpp` | WiFi, réception PXL1, réassemblage, palette, accusés |
| `src/net/lwipopts.h` | dimensionnement de la pile réseau lwIP |
| `src/main.cpp` | recette de démarrage, puis boucle de réception et rapports |
| [`../commun/pxl1.h`](../commun/pxl1.h) | l'en-tête du protocole PXL1 |

Points de configuration à connaître, dans `include/config.h` :

- **`CLK_SYS_KHZ`** vaut 266 MHz, ce qui donne une horloge pixel de 29,6 MHz. Il est lié à
  `CYW43_PIO_CLOCK_DIV_INT` dans `CMakeLists.txt` : la liaison avec la puce WiFi en dérive, et
  si elle part trop vite la puce décroche (`[CYW43] STALL: timeout`). Changer l'un impose de
  recalculer l'autre.
- **`PIN_NODE_ID_0`, `PIN_NODE_ID_1`** (GP14, GP15) : deux cavaliers donnent le numéro du
  nœud. À câbler en pull-up, cavalier vers la masse, à cause de l'errata RP2350-E9.
- **`CHAIN_LEN`, `NODE_COUNT`** valent 1 : une dalle, un nœud.

## Notes de conception

**Répartition des cœurs.** Le cœur 0 sert le réseau et remplit le tampon. Le cœur 1 possède
le pilote : création, interruptions, conversion en plans de bits. Le flux vers la dalle
passe par PIO et DMA, sans le processeur.

**Un seul tampon de notre côté.** Le pilote tient déjà le sien, basculé en fin de trame :
l'affichage est donc sans déchirure. `present()` est synchrone ; au retour, le tampon est
libre, et son contenu persiste d'une publication à l'autre.

**Trois pièges du pilote amont**, consignés aussi dans le code :

1. **`setBasisBrightness()` est obligatoire après `start()` sur le cœur 1.** Sans cet appel,
   la dalle reste noire alors que le compteur de trames du pilote tourne normalement.
   L'enquête est dans [`../phase1-clock-sweep/DIAGNOSTIC.md`](../phase1-clock-sweep/DIAGNOSTIC.md).
2. **`chain_cols` compte les dalles côte à côte**, contrairement à ce qu'affirme le README
   amont : le code calcule `DISPLAY_WIDTH = matrix_panel_width * chain_cols`. Sans effet
   avec une dalle, déterminant avec trois.
3. **`update_bgr()` n'a aucun garde-fou de réentrance.** La construction des plans de bits
   qu'il amorce se poursuit par interruption ; rappelé avant la fin, il repart de zéro sans
   tout réinitialiser et l'image se brouille. Le pilote vendorisé a donc reçu un accesseur
   `occupe()` (voir son [`PROVENANCE.txt`](../vendor/hub75-jupfu/PROVENANCE.txt)) : la boucle
   ne consomme une image que si le pilote a fini, et sinon une image plus récente la
   remplace.

Recette, mesures et enquêtes de mise au point : [`JOURNAL.md`](JOURNAL.md).
