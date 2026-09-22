# Relevés de la phase 0

Les captures PulseView (`.sr`) prises sur la carte MGB. **Elles sont versionnées** :
ce sont les preuves sur lesquelles reposent toutes les décisions du sous-projet, et
la matière première du rejeu de la phase 2 (`../etapes-detaillees.md` §D.11).

| Fichier attendu | Taux | Durée | Sert à |
|---|---|---|---|
| `lente.sr` | 4 MS/s | ~500 ms | `CPL`, `CP`, `ST`, `FR` |
| `rapide.sr` | 24 MS/s | ~67 ms | `CPG`, `LD0`, `LD1`, et le rejeu §D.11 |
| `blanc.sr` | 24 MS/s | ~67 ms | test blanc/noir, écran blanc |
| `noir.sr` | 24 MS/s | ~67 ms | test blanc/noir, écran noir |
| `pastilles-*.jpg` | — | — | photos macro de la zone de soudure |

Un `.sr` est un zip : `unzip -l rapide.sr` montre `metadata` et les blocs `logic-1-*`.
