#!/usr/bin/env python3
"""Console série du module écran.

`cat /dev/ttyACM0` ne fonctionne pas : le firmware n'émet que lorsque DTR est
asserté, et cat ne l'assère pas. Ce script le fait.

    ./tools/console.py [port]
"""
import sys
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"

with serial.Serial(port, 115200, timeout=1) as s:
    s.dtr = True
    s.rts = True
    print(f"— console sur {port}, Ctrl-C pour quitter —\n", file=sys.stderr)
    try:
        while True:
            data = s.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n— fin —", file=sys.stderr)
