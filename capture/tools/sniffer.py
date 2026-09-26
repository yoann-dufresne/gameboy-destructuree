#!/usr/bin/env python3
"""Parle au firmware du sniffer sur sa console USB.

    ./tools/sniffer.py g          # etat brut des 6 entrees
    ./tools/sniffer.py s          # compteurs
    ./tools/sniffer.py a          # vidage ASCII
    ./tools/sniffer.py gs         # plusieurs commandes à la suite
    ./tools/sniffer.py            # écoute simplement, Ctrl-C pour sortir

⚠️ `cat /dev/ttyACM0` ne fonctionne pas : le firmware n'émet que lorsque DTR
est asserté. Ce script l'assère, comme console.py.

Pour récupérer une image, c'est `gbdump.py` : il fait la même chose avec la
commande « p » et en sort un PNG.

Dépendance : pyserial.
"""
import argparse
import sys
import time

COMMANDES = {
    "g": "etat brut des 6 entrees",
    "s": "compteurs",
    "a": "vidage ASCII",
    "p": "vidage hexadecimal (prefere gbdump.py)",
    "r": "remise a zero des compteurs",
    "n": "etat du reseau",
    "h": "aide du firmware",
}


def main():
    ap = argparse.ArgumentParser(
        description="Console du firmware sniffer.",
        epilog="commandes : " + " · ".join(f"{k} = {v}"
                                           for k, v in COMMANDES.items()))
    ap.add_argument("touches", nargs="?", default="",
                    help="suite de commandes à envoyer, ex. « gs »")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--delai", type=float, default=6.0,
                    help="secondes d'écoute après l'envoi (défaut : 6)")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        sys.exit("pyserial est requis :  pip install pyserial")

    inconnues = [t for t in args.touches if t not in COMMANDES]
    if inconnues:
        sys.exit(f"commande(s) inconnue(s) : {''.join(inconnues)}\n"
                 "connues : " + " ".join(COMMANDES))

    try:
        s = serial.Serial(args.port, 115200, timeout=0.3)
    except Exception as e:
        sys.exit(f"{args.port} : {e}\n"
                 "Le Pico est-il branché et le firmware flashé ?")

    with s:
        s.dtr = True
        s.rts = True
        time.sleep(0.8)          # laisser la console s'ouvrir
        s.reset_input_buffer()

        if not args.touches:
            print(f"— écoute sur {args.port}, Ctrl-C pour quitter —\n",
                  file=sys.stderr)
            try:
                while True:
                    d = s.read(4096)
                    if d:
                        sys.stdout.write(d.decode("utf-8", "replace"))
                        sys.stdout.flush()
            except KeyboardInterrupt:
                print("\n— fin —", file=sys.stderr)
            return

        for t in args.touches:
            s.write(t.encode())
            s.flush()
            time.sleep(1.2)      # laisser le firmware répondre

        fin = time.monotonic() + args.delai
        while time.monotonic() < fin:
            d = s.read(4096)
            if d:
                sys.stdout.write(d.decode("utf-8", "replace"))
                sys.stdout.flush()


if __name__ == "__main__":
    main()
