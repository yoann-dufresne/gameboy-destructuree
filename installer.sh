#!/usr/bin/env bash
# Installe ce qu'il faut pour construire, flasher et observer les modules du
# projet, sur Ubuntu ou Debian, en natif comme sous WSL2. Se relance sans
# danger : ce qui est déjà en place est sauté.
#
#   ./installer.sh
#   SDK_VERSION=2.3.1 PICO_DIR=~/pico ./installer.sh   # les valeurs par défaut
#
# Installe : arm-none-eabi-gcc, CMake, Ninja, g++ (tests sur PC), le pico-sdk
# et ses sous-modules (cyw43, lwIP, tinyusb), picotool, les outils Python
# (pyserial, numpy, scipy, Pillow, tkinter, sounddevice), ffmpeg (pixelpush),
# sigrok-cli et le firmware fx2lafw de l'analyseur logique.
set -euo pipefail

SDK_VERSION="${SDK_VERSION:-2.3.1}"
PICO_DIR="${PICO_DIR:-$HOME/pico}"
SDK="$PICO_DIR/pico-sdk"
DEPOT="$(cd "$(dirname "$0")" && pwd)"
WSL=""
grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null && WSL=1

etape() { printf '\n\033[1m— %s —\033[0m\n' "$*"; }

command -v apt-get >/dev/null || { echo "apt-get introuvable : script prévu pour Ubuntu / Debian" >&2; exit 1; }

# ─────────────────────────────────────────────── paquets
etape "paquets"
sudo apt-get update
sudo apt-get install -y \
    git build-essential cmake ninja-build pkg-config libusb-1.0-0-dev \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    python3 python3-serial python3-numpy python3-scipy python3-pil python3-tk \
    ffmpeg sigrok-cli sigrok-firmware-fx2lafw
# Facultatifs : PulseView pour regarder les .sr (WSLg l'affiche sous WSL2),
# sounddevice pour son/tools/hautparleur_virtuel.py seulement.
sudo apt-get install -y pulseview \
    || echo "pulseview indisponible — sigrok-cli suffit aux scripts"
sudo apt-get install -y python3-sounddevice 2>/dev/null \
    || { sudo apt-get install -y libportaudio2 python3-pip \
         && pip install --user --break-system-packages sounddevice; } \
    || echo "sounddevice non installé — seul hautparleur_virtuel.py en a besoin"

# ─────────────────────────────────────────────── pico-sdk
etape "pico-sdk $SDK_VERSION dans $SDK"
mkdir -p "$PICO_DIR"
if [ ! -d "$SDK/.git" ]; then
    git clone -b "$SDK_VERSION" --depth 1 https://github.com/raspberrypi/pico-sdk.git "$SDK"
else
    ACTUELLE="$(git -C "$SDK" describe --tags 2>/dev/null || echo inconnue)"
    [ "$ACTUELLE" = "$SDK_VERSION" ] \
        || echo "⚠️  $SDK est en version $ACTUELLE, pas $SDK_VERSION — laissé tel quel"
fi
# cyw43 et lwIP pour le WiFi, tinyusb pour la console USB, mbedtls pour picotool.
git -C "$SDK" submodule update --init

LIGNE="export PICO_SDK_PATH=$SDK"
if ! grep -qE '^\s*export PICO_SDK_PATH=' ~/.bashrc; then
    echo "$LIGNE" >> ~/.bashrc
    echo "PICO_SDK_PATH ajouté à ~/.bashrc"
fi
export PICO_SDK_PATH="$SDK"

# ─────────────────────────────────────────────── picotool
etape "picotool"
if command -v picotool >/dev/null; then
    echo "déjà installé : $(picotool version 2>/dev/null | head -1)"
else
    # Le SDK veut un picotool de même version majeure.mineure que lui ; à
    # défaut, la plus récente.
    TAGS="$(git ls-remote --tags --refs https://github.com/raspberrypi/picotool.git \
            | sed 's#.*refs/tags/##' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V)"
    TAG="$(grep -E "^${SDK_VERSION%.*}\." <<<"$TAGS" | tail -1 || true)"
    [ -n "$TAG" ] || TAG="$(tail -1 <<<"$TAGS")"
    echo "version retenue : $TAG"
    rm -rf "$PICO_DIR/picotool"
    git clone -b "$TAG" --depth 1 https://github.com/raspberrypi/picotool.git "$PICO_DIR/picotool"
    cmake -S "$PICO_DIR/picotool" -B "$PICO_DIR/picotool/build" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$SDK"
    cmake --build "$PICO_DIR/picotool/build"
    sudo cmake --install "$PICO_DIR/picotool/build"
fi
# Règles udev : picotool sans sudo. Celles de l'analyseur viennent avec sigrok.
if compgen -G "$PICO_DIR/picotool/udev/*.rules" >/dev/null; then
    sudo install -m 644 "$PICO_DIR"/picotool/udev/*.rules /etc/udev/rules.d/
fi
sudo udevadm control --reload-rules 2>/dev/null && sudo udevadm trigger 2>/dev/null || true

# ─────────────────────────────────────────────── droits
etape "droits"
# dialout : les consoles /dev/ttyACM* ; plugdev : l'USB brut (picotool, analyseur).
sudo usermod -aG dialout,plugdev "$USER"
# Les bits d'exécution se perdent d'une machine à l'autre : on les remet aux
# scripts qui ont un shebang.
while IFS= read -r -d '' f; do
    [ "$(head -c2 "$f")" = "#!" ] && chmod +x "$f"
done < <(find "$DEPOT" -path "$DEPOT/.git" -prune -o \
              -path '*/tools/*' -not -path '*/docs/*' \( -name '*.sh' -o -name '*.py' \) -print0)
chmod +x "$DEPOT/installer.sh"

# ─────────────────────────────────────────────── bilan
etape "bilan"
for c in arm-none-eabi-gcc cmake ninja g++ picotool sigrok-cli ffmpeg python3; do
    printf '%-18s %s\n' "$c" "$(command -v "$c" || echo ABSENT)"
done
python3 -c 'import serial, numpy, scipy, PIL, tkinter; print("python : modules ok")'

echo
echo "Ferme et rouvre le terminal : PICO_SDK_PATH et les groupes ne valent qu'ensuite."

if [ -n "$WSL" ]; then
    cat <<'EOF'

── WSL2 : l'USB passe par usbipd-win, côté Windows ──────────────────────────
Une fois, dans un PowerShell administrateur :

    winget install --exact dorssel.usbipd-win
    usbipd bind --hardware-id 2e8a:0009     # Pico, firmware du SDK (console USB)
    usbipd bind --hardware-id 2e8a:000f     # Pico 2 en BOOTSEL
    usbipd bind --hardware-id 04b4:8613     # analyseur, avant téléversement du fx2lafw
    usbipd bind --hardware-id 1d50:608c     # analyseur, fx2lafw chargé

À chaque séance (PowerShell normal, à laisser ouvert) :

    usbipd list                                     # repérer le BUSID du Pico
    usbipd attach --wsl --auto-attach --busid <BUSID>

--auto-attach est indispensable : le Pico (comme l'analyseur) se déconnecte
et revient sous un autre identifiant quand il passe en BOOTSEL, et doit être
ré-attaché aussitôt. Une fenêtre par appareil.

Sans usbipd, tools/flash.sh sait aussi copier le .uf2 sur le lecteur RP2350
vu par Windows (BOOTSEL maintenu au branchement) ; la console et sigrok, eux,
exigent l'attachement.
EOF
fi
