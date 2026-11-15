#!/usr/bin/env bash
# Regénère toute la carte depuis conception.py : schéma, contrôle électrique, netlist, circuit
# imprimé routé, contrôle des règles, puis les fichiers de fabrication (fabrication.sh).
# Prérequis : KiCad 9 (kicad-cli, et le module Python pcbnew), Python 3 avec numpy et shapely.
set -euo pipefail
cd "$(dirname "$0")"
KICAD_CLI=${KICAD_CLI:-kicad-cli}
mkdir -p build
python3 gen_schema.py
"$KICAD_CLI" sch erc --severity-all --exit-code-violations -o build/erc.rpt carte-ecran.kicad_sch
"$KICAD_CLI" sch export netlist --format kicadsexpr -o build/carte-ecran.net carte-ecran.kicad_sch
python3 verifie_netlist.py build/carte-ecran.net
python3 gen_pcb.py | tee build/route.log
"$KICAD_CLI" pcb drc --severity-all --schematic-parity --format json -o build/drc.json carte-ecran.kicad_pcb || true
python3 bilan_drc.py build/drc.json
