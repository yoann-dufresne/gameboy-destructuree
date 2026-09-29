# Game Boy Pocket déstructurée

Un projet artistique : éclater une Game Boy Pocket en objets indépendants qui se parlent
par WiFi.

Chaque module est un projet à part entière : il a son matériel, son firmware, sa
documentation, et il sert à quelque chose en dehors de la Game Boy.

## Modules

| Module | Ce qu'il fait |
|---|---|
| [`capture/`](capture/) | Espionne le bus LCD d'une vraie Game Boy Pocket et diffuse son image en UDP, sans perturber la console. |
| [`ecran/`](ecran/) | Afficheur réseau sur matrices LED, 192×192 pixels visés. Il affiche toute image qu'on lui envoie en UDP, quelle qu'en soit la source. |

Les modules ne partagent qu'un protocole réseau, `PXL1` sur UDP. Chacun s'éprouve seul :
`capture` avec un écran virtuel sur PC, `ecran` avec un émetteur de test.

La spécification initiale décrit aussi des modules boutons, son et cartouche. Aucun
n'est commencé.

## Documents de conception

| Document | Contenu |
|---|---|
| [`Spec_Gameboy_Pocket_Destructuree.md`](Spec_Gameboy_Pocket_Destructuree.md) | La spécification initiale : quatre modules WiFi autour d'un émulateur. |
| [`Spec_Video_Sniffer_et_Matrice_LED.md`](Spec_Video_Sniffer_et_Matrice_LED.md) | La chaîne vidéo : l'image prise sur une vraie console plutôt que sur un émulateur. |

Les deux spécifications sont les documents de départ. Là où elles divergent de la
documentation d'un module, c'est le module qui fait foi : ses mesures les ont corrigées.

## Licence

Sauf mention contraire, ce dépôt est distribué sous licence
[GNU Affero General Public License v3.0 ou ultérieure](LICENSE) (AGPL-3.0-or-later).

Le contenu tiers garde sa licence d'origine :

- `ecran/firmware/vendor/hub75-jupfu/` : pilote HUB75 de JuPfu, licence MIT.
- Les fichiers `pico_sdk_import.cmake`, `lwipopts.h` et `ecran/firmware/phase0-bringup/hub75.pio`,
  repris ou dérivés du pico-sdk et de pico-examples de Raspberry Pi : licence BSD-3-Clause.
- `ecran/docs/seengreat-rgb-matrix-p3-64x64/` : documentation et codes de démonstration du
  fabricant Seengreat, archivés pour référence ; ils restent la propriété de leurs auteurs.
