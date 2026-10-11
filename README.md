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
| [`son/`](son/) | Prend le son d'une Game Boy Pocket avant son bouton de volume et le diffuse en UDP ; un haut-parleur réseau le joue. Phase 0 prête : banc de mesure, plan, tests et points de prise. |
| [`manette/`](manette/) | Une seconde Game Boy Pocket devenue manette WiFi : ses boutons, ses molettes et son interrupteur partent en UDP vers la console principale, où un injecteur appuie sur les boutons et l'allume ou l'éteint. Plan écrit, rien de construit. |
| [`cartouche/`](cartouche/) | La cartouche dans une fente à part : un lecteur la lit à l'insertion et en envoie la copie en WiFi à une doublure branchée sur la console principale, qui tient le rôle de la cartouche. Les sauvegardes reviennent à la vraie cartouche. Plan écrit, rien de construit. |

Les modules ne partagent que des protocoles réseau sur UDP : `PXL1` pour l'image ; `SON1`
pour le son, `MAN1` pour la manette et `CAR1` pour la cartouche, proposés. Chacun s'éprouve
seul : `capture` avec un écran virtuel sur PC, `ecran` avec un émetteur de test, `son` avec un
haut-parleur virtuel et un émetteur de test, `manette` avec un récepteur et une fausse manette
sur PC, `cartouche` avec un PC qui lit le lecteur ou envoie une ROM à la doublure.

Sur la console, chaque module a son microcontrôleur, qui émet son propre réseau WiFi : jusqu'à
quatre écrans, haut-parleurs ou manettes s'y connectent en direct. Le plan d'ensemble est dans
[`reseau.md`](reseau.md).

## Documents de conception

| Document | Contenu |
|---|---|
| [`reseau.md`](reseau.md) | Le réseau : un réseau par type de périphérique, émis par la console ; canaux, adresses, bus de la console, banc de coexistence. |
| [`achats.md`](achats.md) | La liste d'achats du 11/10/2026, tous modules confondus, pour leurs prochaines phases et le banc de coexistence. |
| [`Spec_Gameboy_Pocket_Destructuree.md`](Spec_Gameboy_Pocket_Destructuree.md) | L'intention : une vraie console au centre, cinq modules, et ce qui a changé depuis la v1, qui plaçait un émulateur dans la cartouche. |
| [`Spec_Video_Sniffer_et_Matrice_LED.md`](Spec_Video_Sniffer_et_Matrice_LED.md) | Le départ de la chaîne vidéo : l'image prise sur une vraie console plutôt que sur un émulateur. |

La spécification principale dit l'intention d'aujourd'hui ; la spécification vidéo est le
document de départ de la chaîne image. Là où elles divergent de la documentation d'un module,
c'est le module qui fait foi : ses mesures les ont corrigées.

## Licence

Sauf mention contraire, ce dépôt est distribué sous licence
[GNU Affero General Public License v3.0 ou ultérieure](LICENSE) (AGPL-3.0-or-later).

Le contenu tiers garde sa licence d'origine :

- `ecran/firmware/vendor/hub75-jupfu/` : pilote HUB75 de JuPfu, licence MIT.
- Les fichiers `pico_sdk_import.cmake`, `lwipopts.h` et `ecran/firmware/phase0-bringup/hub75.pio`,
  repris ou dérivés du pico-sdk et de pico-examples de Raspberry Pi : licence BSD-3-Clause.
- `ecran/docs/seengreat-rgb-matrix-p3-64x64/` : documentation et codes de démonstration du
  fabricant Seengreat, archivés pour référence ; ils restent la propriété de leurs auteurs.
