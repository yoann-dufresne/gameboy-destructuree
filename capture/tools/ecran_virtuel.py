#!/usr/bin/env python3
"""Écran virtuel — reçoit un flux `PXL1` et l'affiche sur le PC.

    ./tools/ecran_virtuel.py                  # écoute, affiche, mesure
    ./tools/ecran_virtuel.py --echelle 4
    ./tools/ecran_virtuel.py --sans-affichage # mesure seule, sans fenêtre

Il tient le rôle du module ÉCRAN, et il sépare les problèmes : tant qu'on met
au point le sniffeur, on veut savoir si **le sniffeur** émet correctement, sans
que la matrice LED, son firmware et son WiFi s'ajoutent à la liste des
suspects.

Il imite fidèlement le récepteur réel :

  * mêmes règles de réassemblage que `../ecran/.../reseau.cpp` — fenêtre de
    resynchronisation de 8 trames, tranches en retard écartées ;
  * il renvoie l'accusé `PXL1_TYPE_PING`, donc l'émetteur peut mesurer
    l'aller-retour sans aucune modification (phase 4) ;
  * il accepte `IDX2`, `IDX8` et `BGR888`.

> ⚠️ **Cadence REÇUE et cadence AFFICHÉE sont deux choses différentes.** tkinter
> ne tient pas 60 images/s en 640×576 ; l'affichage est donc plafonné, et ça
> n'enlève RIEN aux trames reçues, qui sont toutes comptées. Les deux chiffres
> sont affichés séparément pour qu'on ne prenne jamais la lenteur de la fenêtre
> pour de la perte de paquets.

Dépendances : numpy, Pillow, tkinter (tous déjà présents).
"""
import argparse
import socket
import struct
import sys
import threading
import time

import numpy as np

# ─────────────────────────────────────────────────────── protocole PXL1
PORT = 4242
ENTETE = 12
MAGIC = b"PXL1"

TYPE_FRAME, TYPE_CTRL, TYPE_PING = 0, 1, 2
FMT_BGR888, FMT_RGB565, FMT_IDX2, FMT_IDX4, FMT_IDX8, FMT_RLE8 = range(6)
FLAG_DERNIERE = 0x01

CTRL_PALETTE, CTRL_LUMINOSITE, CTRL_GEOMETRIE = 0, 1, 2

NOM_FORMAT = {FMT_BGR888: "BGR888", FMT_IDX2: "IDX2", FMT_IDX8: "IDX8"}
BITS = {FMT_BGR888: 24, FMT_IDX2: 2, FMT_IDX8: 8}

# Au-delà de cette ancienneté, ce n'est plus du désordre : c'est un émetteur
# qui a redémarré. Même valeur que le firmware du module écran, où son absence
# bloquait la réception définitivement (constaté le 18/09/2026).
RESYNC = -8

# 🔬 Polarité mesurée en phase 0 : « 00 » = blanc.
PALETTES = {
    "gris": [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)],
    "dmg": [(0x9B, 0xBC, 0x0F), (0x8B, 0xAC, 0x0F),
            (0x30, 0x62, 0x30), (0x0F, 0x38, 0x0F)],
    "diag": [(255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0)],
}


class Recepteur:
    """Écoute, réassemble, décode. Ne sait rien de l'affichage."""

    def __init__(self, port, node_id, largeur, hauteur, format_, palette):
        self.node_id = node_id
        self.largeur, self.hauteur, self.format = largeur, hauteur, format_
        self.palette = np.array(PALETTES[palette], dtype=np.uint8)
        self.geometrie_annoncee = False

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", port))
        self.sock.settimeout(0.5)

        self.verrou = threading.Lock()
        self.trame_prete = None       # ndarray (h, w) d'indices, ou RGB
        self.id_prete = 0
        self.t_prete = 0.0

        self._tampon = bytearray(self._taille())
        self._frame_courante = 0
        self._en_cours = False
        self._octets = 0

        self.c = dict(paquets=0, rejets=0, trames=0, incompletes=0,
                      retardataires=0, resynchros=0, ctrl=0, octets=0)
        # ⚠️ La fenêtre de mesure démarre au PREMIER PAQUET, pas à la
        # construction : sinon le temps d'attente avant que la source
        # n'émette est compté comme du temps sans trames, et la cadence
        # affichée est fausse tant que la moyenne n'a pas convergé.
        self.t0 = None
        self.stop = False

    def _taille(self):
        return self.largeur * self.hauteur * BITS.get(self.format, 2) // 8

    def _redimensionner(self, largeur, hauteur, format_):
        if (largeur, hauteur, format_) == (self.largeur, self.hauteur, self.format):
            return
        self.largeur, self.hauteur, self.format = largeur, hauteur, format_
        self._tampon = bytearray(self._taille())
        self._en_cours = False
        print(f"  géométrie annoncée par la source : {largeur}x{hauteur} "
              f"{NOM_FORMAT.get(format_, format_)}", file=sys.stderr)

    # ─────────────────────────────────────────────────────────── boucle

    def boucle(self):
        while not self.stop:
            try:
                data, source = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            self._paquet(data, source)

    def _paquet(self, data, source):
        if self.t0 is None:
            self.t0 = time.monotonic()
        self.c["paquets"] += 1
        self.c["octets"] += len(data)
        if len(data) < ENTETE or data[:4] != MAGIC:
            self.c["rejets"] += 1
            return
        type_, node, fid, fmt, flags, offset = struct.unpack_from("<BBHBBH", data, 4)
        if node != self.node_id:
            self.c["rejets"] += 1
            return
        charge = data[ENTETE:]

        if type_ == TYPE_CTRL:
            self._ctrl(charge)
            return
        if type_ == TYPE_PING:
            return
        if type_ != TYPE_FRAME:
            self.c["rejets"] += 1
            return

        if fmt != self.format:
            self._redimensionner(self.largeur, self.hauteur, fmt)
        if offset + len(charge) > len(self._tampon):
            self.c["rejets"] += 1
            return

        # Âge de la tranche, en entier signé 16 bits — gère le bouclage.
        age = ((fid - self._frame_courante + 0x8000) & 0xFFFF) - 0x8000

        if age <= RESYNC:
            self.c["resynchros"] += 1
            self._nouvelle(fid)
        elif age > 0:
            if self._en_cours and self._octets < len(self._tampon):
                self.c["incompletes"] += 1
            self._nouvelle(fid)
        elif age < 0:
            self.c["retardataires"] += 1
            return
        elif not self._en_cours:
            self.c["retardataires"] += 1
            return

        self._tampon[offset:offset + len(charge)] = charge
        self._octets += len(charge)

        if flags & FLAG_DERNIERE:
            if self._octets < len(self._tampon):
                self.c["incompletes"] += 1
            self.c["trames"] += 1
            self._en_cours = False
            self._publier(fid)
            self._acquitter(fid, source)

    def _nouvelle(self, fid):
        self._frame_courante = fid
        self._en_cours = True
        self._octets = 0

    def _publier(self, fid):
        buf = np.frombuffer(bytes(self._tampon), dtype=np.uint8)
        if self.format == FMT_IDX2:
            # 4 pixels par octet, pixel de gauche dans les bits de poids fort.
            idx = np.stack([(buf >> 6) & 3, (buf >> 4) & 3,
                            (buf >> 2) & 3, buf & 3], axis=1).ravel()
            img = self.palette[idx].reshape(self.hauteur, self.largeur, 3)
        elif self.format == FMT_IDX8:
            img = np.repeat(buf, 3).reshape(self.hauteur, self.largeur, 3)
        else:  # BGR888
            img = buf.reshape(self.hauteur, self.largeur, 3)[:, :, ::-1]
        with self.verrou:
            self.trame_prete = img
            self.id_prete = fid
            self.t_prete = time.monotonic()

    def _acquitter(self, fid, source):
        """Renvoie l'accusé, comme le fait `reseau::acquitter()` du module
        écran. C'est ce qui donnera à l'émetteur sa mesure d'aller-retour en
        phase 4, sans qu'il ait à supposer une horloge commune."""
        e = MAGIC + struct.pack("<BBHBBH", TYPE_PING, self.node_id, fid, 0, 0, 0)
        try:
            self.sock.sendto(e, source)
        except OSError:
            pass

    def _ctrl(self, charge):
        if not charge:
            return
        self.c["ctrl"] += 1
        cmd = charge[0]
        if cmd == CTRL_PALETTE and len(charge) >= 1 + 12:
            # 256 entrées B,G,R ; seules les 4 premières servent en IDX2.
            bgr = np.frombuffer(charge[1:1 + 12], dtype=np.uint8).reshape(4, 3)
            self.palette = bgr[:, ::-1].copy()
        elif cmd == CTRL_GEOMETRIE and len(charge) >= 6:
            largeur, hauteur, fmt = struct.unpack_from("<HHB", charge, 1)
            self.geometrie_annoncee = True
            self._redimensionner(largeur, hauteur, fmt)

    # ─────────────────────────────────────────────────────────── mesures

    def resume(self):
        if self.t0 is None:
            return "en attente du premier paquet..."
        d = max(time.monotonic() - self.t0, 1e-9)
        c = self.c
        return (f"reçues {c['trames'] / d:6.2f} img/s   "
                f"{c['trames']:6d} trames   "
                f"{c['octets'] * 8 / d / 1e6:5.2f} Mbit/s   "
                f"incompletes {c['incompletes']}   "
                f"retard {c['retardataires']}   resync {c['resynchros']}   "
                f"rejets {c['rejets']}   ctrl {c['ctrl']}")

    def remise_a_zero(self):
        for k in self.c:
            self.c[k] = 0
        self.t0 = None


def affichage(rec, echelle, fps_max):
    import tkinter as tk
    from PIL import Image, ImageTk

    racine = tk.Tk()
    racine.title("Écran virtuel — PXL1")
    vue = tk.Label(racine, bg="black")
    vue.pack()
    etat = tk.Label(racine, font=("monospace", 9), anchor="w", justify="left")
    etat.pack(fill="x")

    compte_affiche = [0]
    t_affiche = [time.monotonic()]
    dernier_id = [-1]

    def sauver(_=None):
        with rec.verrou:
            img = rec.trame_prete
            fid = rec.id_prete
        if img is not None:
            nom = f"ecran-virtuel-{fid}.png"
            Image.fromarray(img).save(nom)
            print(f"  écrit : {nom}", file=sys.stderr)

    def raz(_=None):
        rec.remise_a_zero()
        compte_affiche[0] = 0
        t_affiche[0] = time.monotonic()

    racine.bind("<KeyPress-s>", sauver)
    racine.bind("<KeyPress-r>", raz)
    racine.bind("<KeyPress-q>", lambda _: racine.destroy())

    def boucle():
        with rec.verrou:
            img = rec.trame_prete
            fid = rec.id_prete
        if img is not None and fid != dernier_id[0]:
            dernier_id[0] = fid
            pil = Image.fromarray(img)
            if echelle > 1:
                pil = pil.resize((pil.width * echelle, pil.height * echelle),
                                 Image.NEAREST)
            photo = ImageTk.PhotoImage(pil)
            vue.configure(image=photo)
            vue.image = photo
            compte_affiche[0] += 1

        d = max(time.monotonic() - t_affiche[0], 1e-9)
        etat.configure(
            text=rec.resume() + f"\naffichées {compte_affiche[0] / d:6.2f} img/s"
                                f"   (plafonnées à {fps_max} — sans rapport avec"
                                f" les trames reçues)\n"
                                f"s = enregistrer un PNG    r = remise à zéro"
                                f"    q = quitter")
        racine.after(max(1, int(1000 / fps_max)), boucle)

    boucle()
    racine.mainloop()


def main():
    ap = argparse.ArgumentParser(
        description="Écran virtuel : reçoit un flux PXL1 et l'affiche.")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--noeud", type=int, default=0,
                    help="node_id accepté (défaut : 0)")
    ap.add_argument("--largeur", type=int, default=160)
    ap.add_argument("--hauteur", type=int, default=144)
    ap.add_argument("--palette", choices=sorted(PALETTES), default="gris")
    ap.add_argument("--echelle", type=int, default=4)
    ap.add_argument("--fps-affichage", type=int, default=30,
                    help="plafond d'affichage ; n'affecte PAS la réception")
    ap.add_argument("--sans-affichage", action="store_true",
                    help="mesure seule, sans fenêtre")
    ap.add_argument("--enregistrer", metavar="FICHIER.png",
                    help="en mode sans-affichage : enregistre une trame reçue "
                         "puis continue. Preuve visuelle de la chaîne complète")
    args = ap.parse_args()

    rec = Recepteur(args.port, args.noeud, args.largeur, args.hauteur,
                    FMT_IDX2, args.palette)
    print(f"  écoute sur udp/{args.port}, nœud {args.noeud}, "
          f"{args.largeur}x{args.hauteur} IDX2 par défaut", file=sys.stderr)
    print("  (la géométrie réelle sera adoptée si la source l'annonce "
          "en CTRL)", file=sys.stderr)

    fil = threading.Thread(target=rec.boucle, daemon=True)
    fil.start()

    try:
        if args.sans_affichage:
            enregistre = args.enregistrer is None
            while True:
                time.sleep(1.0)
                print("  " + rec.resume(), file=sys.stderr)
                if not enregistre:
                    with rec.verrou:
                        img, fid = rec.trame_prete, rec.id_prete
                    if img is not None:
                        from PIL import Image
                        Image.fromarray(img).resize(
                            (img.shape[1] * 4, img.shape[0] * 4),
                            Image.NEAREST).save(args.enregistrer)
                        print(f"  écrit : {args.enregistrer} (trame {fid})",
                              file=sys.stderr)
                        enregistre = True
        else:
            affichage(rec, args.echelle, args.fps_affichage)
    except KeyboardInterrupt:
        pass
    finally:
        rec.stop = True
        print("\n  " + rec.resume(), file=sys.stderr)


if __name__ == "__main__":
    main()
