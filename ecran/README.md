# Module ÉCRAN — Game Boy Pocket déstructurée

Sous-projet « écran » : l'étage d'affichage de la Game Boy Pocket déstructurée. Il reçoit des
trames vidéo en UDP (émises par le module SOURCE — sniffer LCD ou émulateur) et les affiche
sur une matrice LED RGB 64×64.

## Matériel

| Rôle | Référence | Notes |
|---|---|---|
| Dalle | **Seengreat RGB Matrix P3.0-64x64** | HUB75E, scan 1/32, 192×192 mm, 5 V / 4 A |
| Contrôleur | **Raspberry Pi Pico 2 W** | RP2350 + CYW43439 ; le « W » est obligatoire |
| Liaison | nappe IDC 16 points | Pico ↔ *Signal Input* de la dalle |
| Alimentation dalle | 5 V dédiée, ≥ 4 A | séparée du Pico, masses communes |

## Documentation

- [`docs/seengreat-rgb-matrix-p3-64x64/`](docs/seengreat-rgb-matrix-p3-64x64/) — archive
  complète de la doc constructeur (wiki, figures, plans, codes de démo), avec une synthèse en
  français : brochage HUB75, spécifications, mise en œuvre Pico/PI/ESP32/Arduino, cascade,
  conversion d'images. Récupérée le 16/09/2026 depuis https://seengreat.com/wiki/74.

Les documents d'architecture du projet vivent dans le dossier parent :
`Spec_Gameboy_Pocket_Destructuree.md` et `Spec_Video_Sniffer_et_Matrice_LED.md`
(§6 = brochage du module ÉCRAN, §2 = choix de la mise à l'échelle 160×144 → 64×64).

## État

Démarrage du sous-projet : pour l'instant, uniquement la documentation matérielle.
Le firmware (driver HUB75 en PIO + réception UDP) reste à écrire.
