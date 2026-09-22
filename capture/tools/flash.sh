#!/usr/bin/env bash
# Flashe un .uf2 sur le Pico : bascule en BOOTSEL par la touche 1200 bauds,
# attend le montage du volume, copie.
#
#   ./tools/flash.sh firmware/sniffer/build/sniffer.uf2
set -euo pipefail
UF2="${1:?usage: flash.sh <fichier.uf2>}"
PORT="${2:-/dev/ttyACM0}"
VOL="/media/$USER/RP2350"

[ -f "$UF2" ] || { echo "introuvable : $UF2" >&2; exit 1; }

if [ -e "$PORT" ]; then
  python3 -c "
import serial, time
try:
    s = serial.Serial('$PORT', 1200); s.dtr = False; time.sleep(.1); s.close()
except Exception: pass
"
fi

for _ in $(seq 1 30); do
  [ -f "$VOL/INFO_UF2.TXT" ] && break
  sleep 0.5
done
[ -f "$VOL/INFO_UF2.TXT" ] || { echo "BOOTSEL non monté — maintiens BOOTSEL et rebranche" >&2; exit 1; }

cp "$UF2" "$VOL/" && sync
echo "flashé : $(basename "$UF2")"
