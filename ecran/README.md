# Module ÉCRAN — Game Boy Pocket déstructurée

Sous-projet « écran » : un **afficheur réseau générique** bâti sur des matrices LED RGB.
Il reçoit des images en UDP et les affiche. La Game Boy n'en est qu'un **client parmi
d'autres** — voir [`docs/plan-firmware.md`](docs/plan-firmware.md).

## Cible

**Grille 3 × 3 dalles de 64×64 ⇒ 192 × 192 pixels.** Vu de l'extérieur : **une adresse IP et
un canevas**. La source envoie son image entière, à sa taille native ; l'écran la place et la
répartit.

```
                 WiFi (1 IP)          nappes 10 pts                  HUB75
                                ┌── L0 ──► [Pico 2 · nœud 0] ──► ▣▣▣  rangée 0
 Source ──PXL2──► [Pico 2 W] ───┼── L1 ──► [Pico 2 · nœud 1] ──► ▣▣▣  rangée 1
                   « tête »     └── L2 ──► [Pico 2 · nœud 2] ──► ▣▣▣  rangée 2
                                   données + VSYNC ►  ◄ RDY, sur chaque nappe
```

Une **tête** Pico 2 W reçoit, place et découpe ; trois **nœuds** pilotent chacun une chaîne de
3 dalles et basculent ensemble sur VSYNC. Pourquoi pas un seul Pico : plan §2.2 bis.

> 🔑 192×192 accueille les **160×144** de la Game Boy en **1:1** — aucune mise à l'échelle,
> et 16 px de marge horizontale / 24 px verticale libres pour un cadre.

## Matériel

| Rôle | Référence | Qté | Notes |
|---|---|---|---|
| Dalle | **Seengreat RGB Matrix P3.0-64x64** | 9 | HUB75E, scan 1/32, 192×192 mm, 5 V / 4 A |
| Tête | **Raspberry Pi Pico 2 W** | 1 | RP2350 + CYW43439 ; le « W » est obligatoire ici |
| Nœuds | Raspberry Pi Pico 2 (W) | 3 | WiFi inutilisé ; les 3 Pico 2 W de la v1 servent |
| Alimentation | 5 V / 15 A | 3 | une par rangée — 36 A ≈ 180 W au total |
| Signal dalles | nappe IDC 16 pts + embase 2×8 mâle | 3 chaînes | nœud ↔ *Signal Input* de la 1ʳᵉ dalle |
| Liaison | nappe IDC 10 pts | 3 | tête → nœud : données, VSYNC, RDY, 4 masses |

Nomenclature complète : [`docs/plan-firmware.md` §7](docs/plan-firmware.md).

## Pile logicielle

**C++20, pico-sdk 2.x, bare-metal**, PIO + DMA, deux cœurs. Pas de RTOS, pas d'Arduino.
Émetteur de test en Python côté PC. Les alternatives (FreeRTOS, Rust/Embassy, CircuitPython)
sont documentées avec leur verdict au §3.2 du plan.

## Documentation

- [`docs/plan-firmware.md`](docs/plan-firmware.md) — **le plan de réalisation** : décisions
  d'architecture et leur justification chiffrée, brochage, protocole `PXL1`, phases 0 à 6,
  nomenclature, journal des décisions.
- [`docs/seengreat-rgb-matrix-p3-64x64/`](docs/seengreat-rgb-matrix-p3-64x64/) — archive
  hors-ligne complète de la doc constructeur (wiki, figures, plans mécaniques, codes de démo),
  avec une synthèse en français. Récupérée le 16/09/2026 depuis https://seengreat.com/wiki/74.

Les documents d'architecture du projet global vivent dans le dossier parent :
`Spec_Gameboy_Pocket_Destructuree.md` et `Spec_Video_Sniffer_et_Matrice_LED.md`.

## État

| Phase | État |
|---|---|
| Documentation matérielle archivée | ✅ |
| Plan de réalisation | ✅ |
| 0 · Bring-up d'une dalle | ✅ 16/09/2026 |
| 1 · Driver HUB75 | ✅ 18/09/2026 — 788 Hz, cœur 0 libre |
| 2 · Protocole + réception | ✅ 18/09/2026 — 60 img/s en BGR888 |
| 3 · Émetteur PC | ✅ 18/09/2026 — 7 sources, injection de défauts |
| 4 · Mesure | ✅ 18/09/2026 — ~8 ms, 24,6 Mbit/s |
| Révision v2 : tête + 3 nœuds | ✅ 29/09/2026 — plan §2.2 bis |
| 5a · Tête seule (`PXL2`, découpe) | 🔨 firmware écrit le 29/09/2026, à mesurer sur matériel |
| 5b · Liaison, 1 nœud | à venir |
| 5c · Passage à 3×3 | à venir |
