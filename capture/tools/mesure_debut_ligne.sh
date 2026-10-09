#!/usr/bin/env bash
# Mesure le début des lignes et des images, et le sniffer face à eux —
# l'enquête du 08/10/2026 sur le premier pixel instable et l'image décalée
# (journal du firmware).
#
#   ./tools/mesure_debut_ligne.sh                    # 1 s → docs/releves/debut-ligne.sr
#   ./tools/mesure_debut_ligne.sh 2 essai.sr         # 2 s, sous ce nom
#
# Câblage de l'analyseur : celui de la phase 1 (D0 = LD0, D1 = LD1, D2 = CP,
# D3 = ST, D4 = S, D5 = CPL), plus D6 sur GP20 du sniffer — broche 26 du Pico,
# qui bascule à la fin de sur_vsync(). Masse commune.
#
# Scène : la colonne 0 de l'écran doit changer d'une ligne à l'autre, sinon la
# donnée du premier pixel ne bouge jamais et ne peut pas être chronométrée.
set -euo pipefail
cd "$(dirname "$0")/.."

DUREE="${1:-1}"
SORTIE="${2:-docs/releves/debut-ligne.sr}"
N=$(( DUREE * 24000000 ))

echo "— capture : 24 MS/s, ${DUREE} s ($N échantillons) → $SORTIE —"
sigrok-cli --driver fx2lafw --config samplerate=24m --samples "$N" \
           -O srzip -o "$SORTIE" 2>&1 | grep -vE '^$' || true

[ -s "$SORTIE" ] || { echo "capture vide — l'analyseur a-t-il été débranché ?" >&2; exit 1; }
./tools/debut_ligne.py "$SORTIE" --vsync 6
