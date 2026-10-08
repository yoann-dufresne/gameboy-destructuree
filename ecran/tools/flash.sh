#!/usr/bin/env bash
# Flashe un .uf2 sur le Pico : bascule en BOOTSEL par la touche 1200 bauds,
# attend le montage du volume, copie.
#
#   ./tools/flash.sh firmware/phase1-clock-sweep/build/phase1_clk_28mhz.uf2
set -euo pipefail
UF2="${1:?usage: flash.sh <fichier.uf2> [port]}"
PORT="${2:-/dev/ttyACM0}"
# Selon la distribution, le volume est monté sous /media ou sous /run/media.
VOLUMES=("/media/$USER/RP2350" "/run/media/$USER/RP2350")

[ -f "$UF2" ] || { echo "introuvable : $UF2" >&2; exit 1; }

if [ -e "$PORT" ]; then
  python3 -c "
import serial, time
try:
    s = serial.Serial('$PORT', 1200); s.dtr = False; time.sleep(.1); s.close()
except Exception: pass
"
fi

VOL=""
for _ in $(seq 1 30); do
  for v in "${VOLUMES[@]}"; do
    [ -f "$v/INFO_UF2.TXT" ] && { VOL="$v"; break 2; }
  done
  sleep 0.5
done
[ -n "$VOL" ] || { echo "BOOTSEL non monté — maintiens BOOTSEL et rebranche" >&2; exit 1; }

cp "$UF2" "$VOL/" && sync
echo "flashé : $(basename "$UF2")"
