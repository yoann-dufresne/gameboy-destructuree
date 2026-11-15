#!/usr/bin/env bash
# Flashe un .uf2 sur le Pico : bascule en BOOTSEL par la touche 1200 bauds,
# attend le montage du volume, copie.
#
# Sous WSL2, le volume ne se monte pas côté Linux. Si le Pico est attaché à
# WSL (usbipd attach --auto-attach), picotool le bascule et le flashe ; sinon,
# le .uf2 est copié sur le lecteur RP2350 vu par Windows.
#
#   ./tools/flash.sh firmware/phase1-clock-sweep/build/phase1_clk_28mhz.uf2
set -euo pipefail
UF2="${1:?usage: flash.sh <fichier.uf2> [port]}"
PORT="${2:-/dev/ttyACM0}"
# Selon la distribution, le volume est monté sous /media ou sous /run/media.
VOLUMES=("/media/$USER/RP2350" "/run/media/$USER/RP2350")

[ -f "$UF2" ] || { echo "introuvable : $UF2" >&2; exit 1; }

flashe() { echo "flashé : $(basename "$UF2")"; exit 0; }

# ─────────────────────────────────────────────── WSL2
if grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null; then
  # Un appareil Raspberry Pi (2e8a) est-il attaché à WSL ?
  if grep -qsx 2e8a /sys/bus/usb/devices/*/idVendor; then
    command -v picotool >/dev/null \
      || { echo "picotool absent — lance installer.sh" >&2; exit 1; }
    # -f redémarre le Pico en BOOTSEL par l'USB. Il se déconnecte alors, et
    # usbipd met une ou deux secondes à le ré-attacher : d'où les reprises.
    picotool load -x -f "$UF2" 2>/dev/null && flashe
    for _ in $(seq 1 20); do
      sleep 0.5
      picotool load -x "$UF2" 2>/dev/null && flashe
    done
    echo "picotool ne joint pas le Pico en BOOTSEL — usbipd attach lancé avec --auto-attach ?" >&2
    exit 1
  fi

  # Pas d'attachement : on passe par le lecteur que Windows a monté.
  command -v powershell.exe >/dev/null \
    || { echo "Pico non attaché à WSL et powershell.exe introuvable" >&2; exit 1; }
  echo "Pico non attaché à WSL — maintiens BOOTSEL et branche-le côté Windows…"
  LECTEUR=""
  # Chaque appel à PowerShell prend déjà ~2 s : 15 essais font ~30 s.
  for _ in $(seq 1 15); do
    LECTEUR="$(powershell.exe -NoProfile -Command \
      "(Get-Volume -FileSystemLabel RP2350 -ErrorAction SilentlyContinue).DriveLetter" \
      2>/dev/null | tr -d '\r\n')"
    [ -n "$LECTEUR" ] && break
  done
  [ -n "$LECTEUR" ] || { echo "aucun lecteur RP2350 vu par Windows" >&2; exit 1; }
  powershell.exe -NoProfile -Command \
    "Copy-Item -LiteralPath '$(wslpath -w "$(realpath "$UF2")")' -Destination '${LECTEUR}:\\'"
  flashe
fi

# ─────────────────────────────────────────────── Linux natif
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
flashe
