# Phase 1 — mesure de l'horloge pixel maximale

Banc de mesure : à quelle fréquence peut-on envoyer les pixels à une dalle sans que l'image
se dégrade ? Le dossier contient aussi le test minimal du pilote et les sondes qui ont
expliqué pourquoi l'affichage restait noir. Aucun réseau, aucun `secrets.h`.

## Construire

Procédure générique : [README du module](../../README.md#construire-flasher-observer). La
construction produit :

| Cibles | Rôle |
|---|---|
| `phase1_clk_28mhz` … `phase1_clk_10mhz` | le banc : une fréquence par binaire, de 28 à 10 MHz par pas de 2 |
| `phase1_smoke` | l'usage minimal du pilote, strictement conforme à la référence amont |
| `probe_*` | sept sondes qui isolent chacune un écart par rapport à `phase1_smoke` ; méthode et résultats dans [`DIAGNOSTIC.md`](DIAGNOSTIC.md) |

Commencer par `phase1_clk_28mhz`. Si l'image est nette, la mesure est finie.

## Principe

**Une fréquence par binaire.** Le diviseur d'horloge est une constante de compilation : le
pilote est initialisé proprement à cette cadence et rien n'est touché ensuite. Balayer les
fréquences à chaud a été essayé et abandonné :

1. changer `clk_sys` en cours de route arrête et reconfigure la PLL système ; les échanges
   d'interruptions entre `hub75_row` et `hub75_bitplane_stream` n'y survivent pas et
   l'affichage meurt définitivement ;
2. changer le diviseur des machines PIO à chaud perturbe celle qui construit les plans de
   bits : le rafraîchissement chute d'un facteur 3 et les mesures ne se reproduisent pas.

Le programme PIO `hub75_bitplane_stream` consomme **9 cycles par pixel**, d'où
`horloge pixel = clk_sys / (9 × diviseur)`, avec `clk_sys` fixé à **266 MHz** : le plafond
est donc de 29,6 MHz. Les temps de garde du verrou et de l'adressage sont recalculés à partir
de l'horloge réelle et valent leurs 80 et 160 ns nominales à toutes les fréquences : la
comparaison entre binaires est honnête.

`clock_test.cpp` est `smoke.cpp` avec un seul écart, le diviseur d'horloge. `smoke.cpp` est
le premier programme de ce dossier qui ait affiché quelque chose : ne pas s'en écarter sans
raison.

## La mire

| Lignes | Contenu | Ce qu'il révèle |
|---|---|---|
| 0–23 | rayures verticales de 1 px, blanches | cas le plus exigeant : les données basculent à chaque coup d'horloge |
| 24–39 | colonnes rouge / vert alternées | les 6 lignes de données basculent en opposition de phase — diaphonie |
| 40–51 | blanc plein | référence d'uniformité et de luminosité |
| 52–63 | pixels isolés tous les 8 | une bavure apparaît comme un pixel fantôme juste à droite |

Image **saine** : rayures franches, contraste constant de gauche à droite. **Défaut** :
rayures qui se brouillent, grisonnent ou bavent, d'abord sur le bord droit, là où arrivent
les derniers pixels décalés dans le registre.

## Résultats

Dalle unique 64 × 64, nappe courte. Le rafraîchissement est celui qu'annonce le pilote.

| Plans BCM | 10 MHz | 12 MHz | 24 MHz | 26 MHz | **28 MHz** | 29,6 MHz |
|---|---|---|---|---|---|---|
| 8 | 424 Hz | 488 Hz | 974 Hz | 1055 Hz | **1138 Hz** | — |
| 10 | — | — | — | — | **750 Hz** | 788 Hz |

La ligne à 8 plans a été mesurée avec la première version du banc (`clk_sys` à 252 MHz) ;
celle à 10 plans avec la version actuelle. Le rafraîchissement suit une loi linéaire, à
diviser par le nombre N de dalles chaînées :

    8 plans  :  ≈ 40,6 × horloge_pixel_MHz / N
    10 plans :  ≈ 26,8 × horloge_pixel_MHz / N

**Horloge pixel maximale : au moins 28 MHz, image nette.** La limite de la dalle n'a pas été
atteinte : c'est le firmware qui plafonne à `clk_sys / 9`. À remesurer sur une chaîne de
trois dalles (phase 5c).

Empreinte : 85 ko de RAM sur 520, 44 ko de flash.
