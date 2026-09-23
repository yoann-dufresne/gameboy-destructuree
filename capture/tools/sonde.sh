#!/usr/bin/env bash
# Capture courte + dépouillement immédiat — pour le sondage interactif de la
# phase 0. On pose la pointe, on lance, on lit le verdict, on passe à la
# pastille suivante.
#
#   ./tools/sonde.sh              # LENT   : 4 MS/s, 500 ms  → CPL, CP, ST, FR
#   ./tools/sonde.sh rapide       # RAPIDE : 24 MS/s, 67 ms  → CPG, LD0, LD1
#   ./tools/sonde.sh lent out.sr  # garde la capture sous ce nom
#
# Les voies laissées en l'air ressortent « figé 1 » : c'est normal, le boîtier
# a des résistances de tirage vers le haut. Si une voie EN L'AIR se met à
# montrer des fronts, c'est une boucle de masse — voir §B.0.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-lent}"
case "$MODE" in
  rapide) TAUX=24m ; N=1600000 ;;   # 66,7 ms ≈ 4 trames
  lent)   TAUX=4m  ; N=2000000 ;;   # 500 ms  ≈ 30 trames
  *) echo "usage: sonde.sh [lent|rapide] [sortie.sr]" >&2 ; exit 1 ;;
esac

SORTIE="${2:-}"
if [ -z "$SORTIE" ]; then
  SORTIE="$(mktemp -u /tmp/sonde-XXXX).sr"
  EPHEMERE=1
fi

echo "— capture $MODE : $TAUX, $N échantillons —"
sigrok-cli --driver fx2lafw --config samplerate=$TAUX --samples "$N" \
           -O srzip -o "$SORTIE" 2>&1 | grep -vE '^$' || true

[ -s "$SORTIE" ] || { echo "capture vide — l'analyseur a-t-il été débranché ?" >&2; exit 1; }
./tools/analyse_sr.py "$SORTIE"
[ -n "${EPHEMERE:-}" ] && rm -f "$SORTIE"
exit 0
