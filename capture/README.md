# Module CAPTURE — Game Boy Pocket déstructurée

Sous-projet « capture » : le **sniffer du bus LCD** de la Game Boy Pocket. Il prélève les
signaux que le CPU MGB envoie à son écran, reconstitue la trame 160×144 en 2 bits par pixel,
et l'émet en UDP vers le module ÉCRAN — dans le **même protocole `PXL1`**, déjà en service.

C'est le « module SOURCE » des specs du dossier parent.

## Cible

**160 × 144 pixels, 2 bits par pixel, 59,73 img/s**, capturés sur le bus LCD par un
**Raspberry Pi Pico 2 W**, émis en `PXL1` / `IDX2` vers les 3 nœuds de la grille 192 × 192.

> 🔑 La Game Boy produit déjà des pixels sur 2 bits, et le format `IDX2` existe déjà dans
> `PXL1`. La chaîne est donc **sans conversion de couleur et sans mise à l'échelle** :
> 2 bits sortent du PPU, 2 bits traversent le WiFi, 4 entrées de palette décident de la
> teinte à l'arrivée. Tout le problème est le **timing de capture**.

## Matériel

| Rôle | Référence | Qté | Notes |
|---|---|---|---|
| Contrôleur | **Raspberry Pi Pico 2 W** | 1 | RP2350 + CYW43439 ; ⚠️ **pas tolérant 5 V** |
| Console | Game Boy Pocket MGB-001 | 1 | 🔴 elle sera ouverte et modifiée |
| Tampon | **74LVC244A** (ou 74LVC245A) + support | 1 | haute impédance, entrées tolérantes 5 V |
| Protection | Résistances 100 Ω | 6 | en série, au départ de chaque prise |
| Câblage fin | Fil émaillé 0,1–0,2 mm (Kynar) | 1 rlx | pastilles du ruban LCD |
| Liaison | Connecteur débrochable 6 pts (JST-SH) | 1 | pour pouvoir refermer la console |
| Alimentation | USB 5 V ou powerbank | 1 | **séparée de la console**, masses communes |
| Outillage | **Analyseur logique ≥ 8 voies, ≥ 24 MS/s** | 1 | **non négociable** — voir plan §6, phase 0 |

Nomenclature complète : [`docs/plan-firmware.md` §9](docs/plan-firmware.md).

## Pile logicielle

**C++20, pico-sdk 2.x, bare-metal**, PIO + DMA, deux cœurs. Pas de RTOS, pas d'Arduino.
Capture sur le cœur 0, pile réseau sur le cœur 1. Le CPU ne touche aucun pixel : le PIO
échantillonne, le DMA écrit directement dans le canevas d'émission.

## Documentation

- [`docs/plan-firmware.md`](docs/plan-firmware.md) — **les décisions** : ce qu'on capture et
  pourquoi, interface électrique, architecture firmware, ce qu'on écarte, phases 0 à 5,
  budget chiffré, nomenclature, journal des décisions.
- [`docs/signaux-mgb.md`](docs/signaux-mgb.md) — **les relevés de la phase 0** : la table
  corrigée des signaux LCD, le brochage qui fait foi, les mesures et leur verdict.
- [`docs/liste-achats.md`](docs/liste-achats.md) — **la liste d'achats de la phase 1**, avec
  la justification de chaque pièce par une mesure et les datasheets vérifiées.
- [`docs/etapes-detaillees.md`](docs/etapes-detaillees.md) — **la marche à suivre** : le même
  chemin, mais étape par étape. Timing du PPU, séquence de mesure à l'analyseur, schéma et
  ordre de montage, programme PIO instruction par instruction, chaîne DMA, table de
  diagnostic symptôme → cause, critères de sortie sous forme de cases à cocher.

## Outils

| | |
|---|---|
| [`tools/analyse_sr.py`](tools/analyse_sr.py) | dépouille une capture PulseView `.sr` et propose l'attribution des 6 signaux, **avec ses preuves**. `--comparer blanc.sr noir.sr` pour le test blanc/noir |
| [`tools/simuler_bus_gb.py`](tools/simuler_bus_gb.py) | capture `.sr` synthétique, pour éprouver le dépouilleur avant d'ouvrir la console. Ne valide rien du projet |
| [`tools/console.py`](tools/console.py) | console série du Pico (`cat /dev/ttyACM0` ne suffit pas : il faut asserter DTR) |
| [`tools/flash.sh`](tools/flash.sh) | flashe un `.uf2`, bascule en BOOTSEL par la touche 1200 bauds |

Dépendances : `numpy` pour les deux premiers, `pyserial` pour la console.

Le module d'affichage vit dans `../ecran/` — son protocole `PXL1` est la seule interface
entre les deux sous-projets. Les documents d'architecture du projet global sont dans le
dossier parent : `Spec_Gameboy_Pocket_Destructuree.md` et
`Spec_Video_Sniffer_et_Matrice_LED.md` (§4 et §5 = ce module).

## État

Démarrage du sous-projet : pour l'instant, uniquement le plan. Rien n'est encore soudé sur
la console, et aucune broche n'a été identifiée.

| Phase | État |
|---|---|
| Plan de réalisation | ✅ 22/09/2026 |
| Outillage de la phase 0 | ✅ 22/09/2026 |
| 0 · Identification des signaux à l'analyseur | ✅ 25/09/2026 — [`docs/signaux-mgb.md`](docs/signaux-mgb.md) |
| 1 · Prise de signaux et interface électrique | 🔨 suivante — tampon 74LVC244A **obligatoire** |
| 2 · Capture PIO + DMA | ⬜ |
| 3 · `IDX2` de bout en bout | ⬜ |
| 4 · Mesure de latence et robustesse | ⬜ |
| 5 · Intégration | ⬜ |
