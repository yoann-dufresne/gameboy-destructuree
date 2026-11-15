#!/usr/bin/env bash
# Regénère la carte depuis conception.py : schéma, contrôle électrique, netlist et sa vérification,
# circuit imprimé (la géométrie, étape C2 de manette/docs/carte.md), gabarit à l'échelle 1.
# Prérequis : KiCad 10 (kicad-cli et le module Python pcbnew), Python 3, pdfunite (poppler-utils).
set -euo pipefail
cd "$(dirname "$0")"
KICAD_CLI=${KICAD_CLI:-kicad-cli}
PCB=carte-manette.kicad_pcb
mkdir -p build gabarit
python3 gen_schema.py
"$KICAD_CLI" sch erc --severity-all --exit-code-violations -o build/erc.rpt carte-manette.kicad_sch
"$KICAD_CLI" sch export netlist --format kicadsexpr -o build/carte-manette.net carte-manette.kicad_sch
python3 verifie_netlist.py build/carte-manette.net
python3 gen_pcb.py
# Gabarit à l'échelle 1, pour l'essai en carton (C3) : la face avant, puis le dos vu de dos.
"$KICAD_CLI" pcb export pdf --mode-single -l Edge.Cuts,F.Mask,F.Silkscreen,F.Fab --black-and-white \
    --drill-shape-opt 2 --scale 1 -o build/gabarit-face.pdf "$PCB"
"$KICAD_CLI" pcb export pdf --mode-single -l Edge.Cuts,B.Mask,B.Silkscreen,B.Fab --black-and-white \
    --drill-shape-opt 2 --scale 1 --mirror -o build/gabarit-dos.pdf "$PCB"
pdfunite build/gabarit-face.pdf build/gabarit-dos.pdf gabarit/gabarit-echelle-1.pdf
# Le contrôle des règles (DRC) viendra avec le placement (C4) : tant que des pièces attendent hors de
# la carte, il ne dirait rien d'utile.
echo "carte à jour ; gabarit : gabarit/gabarit-echelle-1.pdf"
