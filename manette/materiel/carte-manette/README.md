# La carte de la manette : les fichiers

Le plan, les décisions et les étapes sont dans [`../../docs/carte.md`](../../docs/carte.md). Ici, la
carte elle-même, régénérée par script, comme [celle de l'écran](../../../ecran/materiel/README.md) :
on ne retouche pas les fichiers KiCad à la main. On modifie `conception.py` ou `gen_pcb.py`, puis on
relance `construire.sh`. Les fichiers restent ouvrables dans KiCad 10 pour les regarder.

**État au 15/11/2026.**
- **Schéma complet (C1)** : le contrôle électrique ne trouve aucune erreur ni aucun avertissement,
  et la netlist de KiCad est identique à `conception.py`.
- **Géométrie posée (C2)** : le contour, les trous et les 11 pièces reprises de YAPoco sont à leur
  place exacte. Le Pico et la LED d'alimentation sont posés.
- **Les autres pièces attendent leur placement (C4)**, rangées hors de la feuille. Il dépend des
  relevés des membranes (C0) et de l'essai en carton (C3).

| Fichier | Rôle |
|---|---|
| `conception.py` | la description électrique : composants, empreintes, équipotentielles, références d'achat. Source unique |
| `gen_schema.py` | écrit `carte-manette.kicad_sch`, sa bibliothèque de symboles et les tables du projet |
| `gen_pcb.py` | écrit `carte-manette.kicad_pcb` : contour, trous, pièces placées. Vérifie chaque pastille reprise de YAPoco |
| `extrait_yapoco.py` | a tiré de YAPoco le contour, les empreintes et leur place. N'a pas à être relancé |
| `yapoco/` | ce qui vient de YAPoco, et sa provenance |
| `carte-manette.pretty/` | les empreintes propres à la carte |
| `kicadlib.py`, `verifie_netlist.py`, `bilan_drc.py` | outils repris de la carte de l'écran, adaptés à KiCad 10 |
| `construire.sh` | tout, du schéma au gabarit |
| `gabarit/gabarit-echelle-1.pdf` | le gabarit de l'essai en carton (C3) : la face avant, puis le dos vu de dos |

```bash
cd manette/materiel/carte-manette
./construire.sh
```

Prérequis : KiCad 10 (`kicad-cli` et le module Python `pcbnew`), Python 3, `pdfunite`. Les tables
du projet ne listent que les bibliothèques de la carte ; celles de KiCad viennent de ses tables
globales, les tables par défaut proposées au premier lancement.

**Imprimer le gabarit à 100 %, sans ajustement à la page** : le contour doit mesurer 69,5 × 114,4 mm
sur le papier.

## Licence

La carte reprend la géométrie de [YAPoco](https://github.com/nataliethenerd/YAPoco), de Natalie the
Nerd, publiée sous [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). Ce dossier est
publié sous la même licence ; le détail de ce qui est repris est dans
[`yapoco/PROVENANCE.txt`](yapoco/PROVENANCE.txt).
