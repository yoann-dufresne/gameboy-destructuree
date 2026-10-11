# Banc de coexistence — le réseau éprouvé avant les modules

Le [plan réseau](../reseau.md) met quatre radios dans la console, chacune point d'accès
de son propre réseau. Avant de toucher au firmware d'un seul module, ce banc répond à trois
questions, avec des Pico 2 W de rechange qui imitent le vrai trafic :

| Test | La question |
|---|---|
| **R1** | Quatre radios à quelques centimètres les unes des autres tiennent-elles chacune les exigences de son module, sur trois canaux ou sur un seul ? |
| **R2** | Le sniffer, devenu point d'accès, envoie-t-il une copie de l'image à quatre écrans sans que sa capture décroche ? |
| **R3** | Le bus filaire de la console, en I²C, passe-t-il une heure sans perdre ni abîmer un message ? |

Ce n'est pas un module : rien ici ne finira dans l'installation, sauf deux formats qui
font foi, [`bus_console.h`](firmware/commun/bus_console.h), le bus de la console, que
les modules copieront, et [`banc.h`](firmware/commun/banc.h), le format des paquets du banc.

**État : compilé, jamais flashé** (11/10/2026). Ce qui est vérifié sans carte est en fin
de document.

## Ce qu'il faut

| Pour | Pièces |
|---|---|
| R1 | 8 Pico 2 W ; un hub USB alimenté, 2 A au moins : un Pico qui émet tire jusqu'à 250 mA ; 4 chargeurs USB pour les clients posés à 3 m |
| R2 | le sniffer dans la console principale ; 3 des Pico de R1, en faux écrans ; la tête de l'écran, si tu veux un quatrième écran réel |
| R3 | 4 des Pico de R1 ; 2 résistances de 4,7 kΩ ; des fils de 10 à 20 cm, la longueur qu'ils auront dans la console |
| Le PC | Python 3 et pyserial |

## Construire et flasher

```sh
cd banc-reseau/firmware
cp include/secrets.h.example include/secrets.h      # le mot de passe des quatre réseaux
cmake -S . -B build -DPICO_SDK_PATH=~/pico/pico-sdk
cmake --build build -j8
```

Treize fichiers sortent dans `build/` :

| Fichier | Rôle |
|---|---|
| `banc_ap_image.uf2`, `_son`, `_manettes`, `_cartouche` | les quatre points d'accès de R1 : ils émettent le réseau et son trafic |
| `banc_client_image.uf2`, `_son`, `_manettes`, `_cartouche` | les quatre clients de R1 : un écran, un haut-parleur, quatre manettes, le lecteur |
| `banc_faux_ecran.uf2` | un écran sans dalle, pour R2 |
| `banc_bus_maitre.uf2` | R3 : l'injecteur |
| `banc_bus_esclave_sniffer.uf2`, `_son`, `_doublure` | R3 : les trois esclaves |

**Un seul Pico branché pendant le flashage** : `flash.sh` vise le premier port.

```sh
./tools/flash.sh firmware/build/banc_ap_image.uf2
```

Une étiquette sur chaque carte, avec son rôle : huit Pico identiques se confondent vite.

## R1 — quatre radios côte à côte

**Poser.** Les quatre points d'accès côte à côte, antennes à 2 à 5 cm les unes des autres,
comme elles le seront dans la console, tous sur le hub, le hub sur le PC. Les quatre clients
à 3 m, chacun sur un chargeur : ils envoient leur bilan à leur point d'accès, qui l'imprime.

```sh
./tools/banc.py ports                          # quatre points d'accès, et qui ils sont
./tools/banc.py plan 3                         # image 11, son 1, manettes et cartouche 6
./tools/banc.py mesure --duree 600 --sortie r1-plan3.log
./tools/banc.py verdict r1-plan3.log
./tools/banc.py plan 1                         # les quatre sur le 11
./tools/banc.py mesure --duree 600 --sortie r1-plan1.log
./tools/banc.py verdict r1-plan1.log
./tools/banc.py comparer r1-plan3.log r1-plan1.log
```

Un changement de plan fait redémarrer les points d'accès ; les clients retrouvent seuls leur
réseau sur son nouveau canal. Le réglage est gardé en flash.

Ce que le banc imite, au plus fort : l'image en quatre copies, 11,2 Mbit/s ; le son en
quatre copies, 6,2 Mbit/s ; quatre manettes ; une copie de cartouche de 1 Mo toutes les
20 s, la rafale qui tombe pendant que les autres jouent. Le verdict dit, réseau par réseau,
si les exigences de son module sont tenues, et ce que les rafales de la cartouche font aux
autres.

**Si aucun plan ne tient** : écarter les antennes, les orienter, et recommencer. Baisser la
puissance d'émission demanderait un ajout au firmware.

**La manette en économie d'énergie** : `./tools/banc.py eco <port> 2`, le client manettes
relié au PC le temps du réglage, puis une mesure de plus.

## R2 — le sniffer et ses quatre écrans

1. **Le sniffer en point d'accès.** Dans `capture/firmware/sniffer/include/secrets.h`,
   ajouter `IMAGE_SSID` et `IMAGE_MOT_DE_PASSE`, le même mot de passe que celui du banc
   ([`secrets.h.example`](../capture/firmware/sniffer/include/secrets.h.example)). Puis :

   ```sh
   cd capture/firmware/sniffer
   cmake -S . -B build-ap -DPICO_SDK_PATH=~/pico/pico-sdk -DPOINT_ACCES=ON
   cmake --build build-ap -j8
   ```

   Flasher `build-ap/sniffer.uf2` ; pour revenir au sniffer d'origine, reflasher celui de
   `build/`. Option décrite dans le [README du sniffer](../capture/firmware/sniffer/README.md).
2. **Les faux écrans.** `banc_faux_ecran.uf2` sur trois Pico, puis un numéro chacun :
   `./tools/banc.py num <port> 1`, `2`, `3`, adresses .2 à .4.
3. **La tête, en quatrième écran**, si tu veux : compilée avec `-DWIFI_STATION=ON`,
   `gb-image` et son mot de passe dans son `secrets.h`. Le sniffer la trouve par son bail.
4. La console allumée sur un jeu qui bouge, le sniffer et les faux écrans reliés au PC :

   ```sh
   ./tools/banc.py mesure --duree 600 --sortie r2.log
   ./tools/banc.py verdict r2.log
   ```

Le verdict regarde surtout la capture : aucune tranche perdue faute de temps, et le temps
d'émettre une tranche et ses copies, à comparer aux 3,8 ms qui séparent deux tranches.

## R3 — le bus de la console

**Câbler.** GP6 de toutes les cartes ensemble (SDA), GP7 ensemble (SCL), les masses
ensemble ; une seule paire de 4,7 kΩ, de SDA et de SCL vers le 3V3 d'une des cartes.
`banc_bus_maitre.uf2` sur l'une, les trois esclaves sur les autres.

```sh
./tools/banc.py mesure --duree 3600 --sortie r3.log
./tools/banc.py verdict r3.log
```

Cinquante fois par seconde, le maître écrit un ordre à chaque esclave et lit son état. Chaque
esclave recalcule le contenu attendu d'après le numéro de l'ordre : un message intact au CRC
mais faux serait vu aussi.

## Lire le verdict

✅ tenu, ⚠️ à surveiller, ❌ non tenu ; puis `R1 TENU` ou `R1 NON TENU`. Les seuils sont en
tête de [`tools/banc.py`](tools/banc.py), avec ce qui les justifie : ce sont des
propositions, écrites avant la mesure. Les résultats vont dans [`reseau.md`](../reseau.md),
et le plan de canaux retenu avec eux.

## Contenu

| Chemin | Contenu |
|---|---|
| `firmware/commun/banc.h` | le format `BNC1` : en-tête, profils de trafic, seaux des retards, bilan d'un client |
| `firmware/commun/bus_console.h` | le bus de la console : adresses, ordre, état, CRC. **Fait foi** |
| `firmware/commun/` | la radio (point d'accès, client à adresse fixe), les réglages en flash, la console |
| `firmware/radio/` | point d'accès, client et faux écran |
| `firmware/bus/` | maître et esclave du bus |
| `firmware/test/` | les tests sur PC du bus et du banc |
| `tools/banc.py` | réglages, enregistrement, verdict, comparaison ; enregistrements fabriqués |
| `tools/test_banc.py` | les tests de l'outil |
| `tools/console.py`, `tools/flash.sh` | copiés du module son |

## Vérifié sans carte

```sh
cd firmware/test
g++ -std=c++20 -O2 -Wall -Wextra -I../commun -I../bus test_bus_console.cpp -o /tmp/test_bus_console && /tmp/test_bus_console
g++ -std=c++20 -O2 -Wall -Wextra -I../commun test_banc.cpp -o /tmp/test_banc && /tmp/test_banc
cd ../.. && python3 tools/test_banc.py
```

- **Le bus** : le CRC est celui du SMBus (« 123456789 » → 0xF4) ; sur les 256 numéros et
  les trois adresses, chaque bit retourné et chaque adresse croisée fait refuser le message.
- **Le banc** : tailles des formats, seaux des retards, suivi de numérotation, cadence des
  tranches, débits de 11,2 et 6,2 Mbit/s.
- **L'outil** : les seaux sont ceux de `banc.h` ; chaque champ qu'il lit est bien imprimé par
  les firmwares, sniffer compris ; sur des enregistrements fabriqués, les scénarios « bon »
  sont tenus et chaque défaut des scénarios « mauvais » est vu.
- **Les treize firmwares** se compilent sans avertissement ; le sniffer aussi, avec et sans
  `POINT_ACCES`. Sans l'option, son binaire est **identique octet pour octet** à celui
  d'avant la modification.

Rien de ce qui touche la radio n'a tourné : c'est le travail du banc lui-même.
