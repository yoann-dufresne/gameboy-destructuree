# Mesure de l'horloge pixel maximale stable

**Une fréquence par binaire.** Le diviseur d'horloge est un `constexpr`, donc le pilote
est initialisé proprement à cette cadence et rien n'est touché ensuite.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # produit 10 .uf2 : de 28 à 10 MHz
../../tools/flash.sh build/phase1_clk_28mhz.uf2
../../tools/console.py
```

Commencer par **le plus haut**. Si l'image est nette, la mesure est finie.

## D'où vient la fréquence

Le programme PIO `hub75_bitplane_stream` consomme **9 cycles par pixel** :

```
out pins, 6  [3]   side 0    -> 4 cycles
out null, 2  [3]   side 1    -> 4 cycles
jmp x--, loop      side 0    -> 1 cycle
```

D'où `horloge pixel = clk_sys / (9 × sm_clockdiv_factor)`, avec `clk_sys` fixé à
**252 MHz** au démarrage et jamais retouché.

Les gardes de verrou et d'adressage sont converties en cycles PIO à la création, à
partir du `clk_sys` et du diviseur réels : elles valent donc leurs 80 ns / 160 ns
nominaux **quelle que soit la fréquence testée**. La comparaison entre binaires est
donc honnête.

## Ce qui a été essayé et abandonné

Une première version balayait les fréquences à chaud. Deux impasses successives :

1. **Changer `clk_sys` en cours de route** — `set_sys_clock_khz()` arrête et
   reconfigure la PLL système ; les échanges d'IRQ entre `hub75_row` et
   `hub75_bitplane_stream` n'y survivent pas et l'affichage meurt définitivement.
2. **Changer le diviseur des machines PIO à chaud** — le pilote en utilise **trois**,
   dont une qui construit les plans de bits. La perturber en plein travail fait
   chuter le rafraîchissement d'un facteur 3, et les mesures ne se reproduisent pas
   d'un tour à l'autre.

D'où le choix d'un binaire par fréquence.

## La mire

| Lignes | Contenu | Ce qu'il révèle |
|---|---|---|
| 0–23 | rayures verticales de 1 px, blanches | cas le plus exigeant : les données basculent à chaque coup d'horloge |
| 24–39 | colonnes rouge / vert alternées | les 6 lignes de données basculent en opposition de phase — diaphonie |
| 40–51 | blanc plein | référence d'uniformité et de luminosité |
| 52–63 | pixels isolés tous les 8 | une bavure apparaît comme un pixel fantôme juste à droite |

**Sain** : rayures franches, contraste constant de gauche à droite.
**Défaut** : rayures qui se brouillent, grisonnent ou bavent — d'abord sur le bord
**droit**, les derniers pixels décalés dans le registre.

## Résultats mesurés

Dalle unique 64×64, scan 1/32, 8 plans BCM, `clk_sys` 252 MHz.
Le rafraîchissement est annoncé par le pilote lui-même (`frame_rate_debug`).

| Horloge pixel | Rafraîchissement |
|---|---|
| 10 MHz | 406 Hz |
| 12 MHz | 488 Hz |
| 24 MHz | 974 Hz |
| 26 MHz | 1055 Hz |
| **28 MHz** | **1138 Hz** |

Soit une loi linéaire : **≈ 40,6 Hz par MHz d'horloge pixel**, pour une dalle seule.
Pour une chaîne de N dalles, diviser par N.

Empreinte : 85 ko de RAM sur 520, 44 ko de flash.
