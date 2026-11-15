#!/usr/bin/env bash
# Fichiers de fabrication de la carte, dans fab/ :
#   carte-ecran-gerber.zip   Gerber + perçages, à déposer tel quel chez le fabricant
#   bom_jlcpcb.csv, cpl_jlcpcb.csv   nomenclature et placement des CMS pour l'assemblage JLCPCB
#   nomenclature.csv / .md   tout ce qu'il faut acheter pour la carte
#   orientation_cms.png      repère des pièces polarisées, à contrôler dans l'aperçu JLCPCB
#   schema.pdf, plan_montage.pdf, cuivre.pdf
# et des rendus dans ../docs/ si les modèles 3D de KiCad sont installés.
set -euo pipefail
cd "$(dirname "$0")"
KICAD_CLI=${KICAD_CLI:-kicad-cli}
PCB=carte-ecran.kicad_pcb
rm -rf fab/gerber && mkdir -p fab/gerber build
"$KICAD_CLI" pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask --no-protel-ext -o fab/gerber/ "$PCB"
"$KICAD_CLI" pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 \
    -o fab/gerber/ "$PCB"
python3 -c "import zipfile, glob, os; z = zipfile.ZipFile('fab/carte-ecran-gerber.zip', 'w', zipfile.ZIP_DEFLATED); [z.write(f, os.path.basename(f)) for f in sorted(glob.glob('fab/gerber/*'))]; z.close()"
"$KICAD_CLI" pcb export pos --format csv --units mm --side front -o build/positions.csv "$PCB"
python3 fab.py build/positions.csv
python3 orientation.py
"$KICAD_CLI" sch export pdf -o fab/schema.pdf carte-ecran.kicad_sch
"$KICAD_CLI" pcb export pdf --layers F.Fab,F.SilkS,Edge.Cuts --mode-single -o fab/plan_montage.pdf "$PCB"
rm -rf fab/cuivre.pdf
"$KICAD_CLI" pcb export pdf --layers F.Cu,F.SilkS,Edge.Cuts --mode-single -o fab/cuivre_dessus.pdf "$PCB"
"$KICAD_CLI" pcb export pdf --layers B.Cu,B.SilkS,Edge.Cuts --mode-single --mirror -o fab/cuivre_dessous.pdf "$PCB"
echo "fabrication : $(ls fab | tr '\n' ' ')"
