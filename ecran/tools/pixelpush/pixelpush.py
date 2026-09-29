#!/usr/bin/env python3
"""Émetteur PXL2 / PXL1 — pousse des images vers le module ÉCRAN en UDP.

PXL2 (--cible) : l'écran n'est qu'une adresse. On lui demande sa taille, on lui
envoie l'image entière ; il la place et la répartit lui-même (plan §4).

    ./pixelpush.py --cible 192.168.1.50 --source anim --format idx8
    ./pixelpush.py --cible 192.168.1.50 --taille 160x144 --format idx2
    ./pixelpush.py --cible 192.168.1.50 --sonder

PXL1 (--layout, par défaut) : le protocole v1, pour les nœuds WiFi autonomes du
firmware `ecran/`. Le fichier de disposition décrit les nœuds et leur rectangle.

    ./pixelpush.py --source anim
    ./pixelpush.py --source horloge --fps 10
    ./pixelpush.py --source gif --fichier boucle.gif
    ./pixelpush.py --source ecran --region 100,100,512,512
    ./pixelpush.py --source video --fichier film.mp4        (nécessite ffmpeg)

Éprouver le réassemblage du firmware :

    ./pixelpush.py --source anim --perte 2        # 2 % de paquets non émis
    ./pixelpush.py --source anim --desordre 5     # 5 % de paquets retardés
"""

from __future__ import annotations

import argparse
import random
import select
import socket
import struct
import sys
import time
import tomllib
from pathlib import Path

import numpy as np

import sources

# --- protocoles (miroir de firmware/commun/pxl1.h et pxl2.h) -----------------

MAGIC = b"PXL1"
ENTETE = 12
MAGIC2 = b"PXL2"
ENTETE2 = 18
CHARGE_MAX = 1400

TYPE_FRAME = 0
TYPE_CTRL = 1
TYPE_PING = 2   # PXL1 : l'accusé de l'écran. PXL2 : la question de la source
TYPE_PONG = 3   # PXL2 seulement
TYPE_ACK = 4    # PXL2 seulement

FMT_BGR888 = 0
FMT_IDX2 = 2
FMT_IDX8 = 4
FORMATS = {"bgr888": FMT_BGR888, "idx2": FMT_IDX2, "idx8": FMT_IDX8}
NOMS_FORMAT = {0: "BGR888", 1: "RGB565", 2: "IDX2", 3: "IDX4", 4: "IDX8", 5: "RLE8"}

FLAG_DERNIERE = 0x01

CTRL_PALETTE = 0
CTRL_LUMINOSITE = 1

# Tranches multiples de 3 octets : jamais un pixel coupé en deux.
CHARGE_UTILE = (CHARGE_MAX // 3) * 3


def entete(node_id: int, frame_id: int, offset: int, derniere: bool,
           format_: int = FMT_BGR888, type_: int = TYPE_FRAME) -> bytes:
    return MAGIC + struct.pack(
        "<BBHBBH", type_, node_id, frame_id & 0xFFFF,
        format_, FLAG_DERNIERE if derniere else 0, offset)


def entete2(type_: int, format_: int, frame_id: int, largeur: int, hauteur: int,
            offset: int = 0, derniere: bool = False) -> bytes:
    """En-tête PXL2 : la géométrie de la source voyage dans chaque paquet."""
    return MAGIC2 + struct.pack(
        "<BBHHHIBB", type_, format_, frame_id & 0xFFFF, largeur, hauteur,
        offset, FLAG_DERNIERE if derniere else 0, 0)


def sonder(ip: str, port: int, delai: float = 0.5) -> dict | None:
    """PING → PONG : demande à l'écran ce qu'il est. None s'il ne répond pas."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(delai)
        s.sendto(entete2(TYPE_PING, 0, 0, 0, 0), (ip, port))
        fin = time.monotonic() + delai
        while time.monotonic() < fin:
            try:
                data, _ = s.recvfrom(256)
            except (socket.timeout, OSError):
                return None
            if len(data) >= ENTETE2 + 8 and data[:4] == MAGIC2 and data[4] == TYPE_PONG:
                w, h, formats, charge = struct.unpack_from("<HHHH", data, ENTETE2)
                return {"largeur": w, "hauteur": h, "charge_max": charge,
                        "formats": [f for f in range(16) if formats >> f & 1]}
    return None


def palette_gris4() -> np.ndarray:
    """IDX2 : quatre gris, du noir au blanc ; le reste de la palette est noir."""
    pal = np.zeros((256, 3), np.uint8)
    for i in range(4):
        pal[i] = (i * 85,) * 3
    return pal


def emballer_idx2(rgb: np.ndarray) -> np.ndarray:
    """RGB → 4 niveaux de luminance, quatre pixels par octet, pixel de gauche
    en poids fort — l'ordre du sniffer. Chaque ligne commence sur un octet."""
    lum = (rgb[:, :, 0].astype(np.uint16) * 77 + rgb[:, :, 1].astype(np.uint16) * 150
           + rgb[:, :, 2].astype(np.uint16) * 29) >> 8
    idx = (lum >> 6).astype(np.uint8)
    h, w = idx.shape
    if w % 4:
        idx = np.pad(idx, ((0, 0), (0, 4 - w % 4)))
    q = idx.reshape(h, -1, 4)
    return (q[..., 0] << 6) | (q[..., 1] << 4) | (q[..., 2] << 2) | q[..., 3]


def palette_cube() -> np.ndarray:
    """Cube 6×6×6 (216 couleurs) complété par 40 gris.

    Palette fixe : elle n'est envoyée qu'une fois, et la quantification se
    réduit à une division — quelques centaines de microsecondes par trame en
    numpy. Une palette adaptative rend mieux sur du contenu à couleurs limitées,
    mais doit être recalculée et renvoyée dès que le contenu change.
    """
    pal = np.zeros((256, 3), np.uint8)
    niveaux = np.array([0, 51, 102, 153, 204, 255], np.uint8)
    i = 0
    for r in range(6):
        for g in range(6):
            for b in range(6):
                pal[i] = (niveaux[r], niveaux[g], niveaux[b])
                i += 1
    for k in range(40):
        v = round(k * 255 / 39)
        pal[216 + k] = (v, v, v)
    return pal


def quantifier_cube(rgb: np.ndarray) -> np.ndarray:
    """RGB → indices du cube 6×6×6. Pure arithmétique, donc rapide."""
    n = np.clip((rgb.astype(np.uint16) + 25) // 51, 0, 5).astype(np.uint16)
    return (n[:, :, 0] * 36 + n[:, :, 1] * 6 + n[:, :, 2]).astype(np.uint8)


class Noeud:
    def __init__(self, d: dict):
        self.id = int(d["id"])
        self.ip = str(d["ip"])
        self.port = int(d.get("port", 4242))
        self.x0, self.y0 = int(d["x0"]), int(d["y0"])
        self.w, self.h = int(d["w"]), int(d["h"])

    def __repr__(self) -> str:
        return (f"nœud {self.id} → {self.ip}:{self.port} "
                f"rect ({self.x0},{self.y0}) {self.w}×{self.h}")


class Emetteur:
    def __init__(self, noeuds: list[Noeud], largeur: int, hauteur: int,
                 protocole: int, perte: float = 0.0, desordre: float = 0.0,
                 format_: int = FMT_BGR888):
        self.largeur = largeur
        self.hauteur = hauteur
        self.noeuds = noeuds
        self.protocole = protocole
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        # Une image 192×192 part en rafale de 27 à 80 paquets : le tampon
        # d'émission par défaut du noyau déborde dès que le WiFi hoquette.
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
        self.frame_id = 0
        # Mesure de latence : le firmware renvoie un accusé au moment où la
        # trame est affichée. L'aller-retour est donc chronométré sur la seule
        # horloge de cette machine — aucune synchronisation à supposer.
        self.envois: dict[int, float] = {}
        self.latences: list[float] = []
        self.accuses_recus = 0
        self.accuses_orphelins = 0
        self.octets = 0
        self.paquets = 0
        # Injection de défauts, pour éprouver le réassemblage du firmware.
        self.perte = perte / 100.0
        self.desordre = desordre / 100.0
        self.perdus = 0
        self.retardes = 0
        self.bloques = 0  # paquets abandonnés : tampon d'émission plein
        self.format = format_
        self.palette = palette_gris4() if format_ == FMT_IDX2 else palette_cube()
        self.palette_envoyee = 0.0

    @classmethod
    def depuis_layout(cls, chemin: Path, **kw) -> "Emetteur":
        """PXL1 : l'émetteur connaît la grille et découpe lui-même."""
        with open(chemin, "rb") as f:
            conf = tomllib.load(f)
        return cls([Noeud(n) for n in conf["noeud"]], int(conf["image"]["largeur"]),
                   int(conf["image"]["hauteur"]), 1, **kw)

    @classmethod
    def vers_tete(cls, ip: str, port: int, largeur: int, hauteur: int,
                  **kw) -> "Emetteur":
        """PXL2 : un seul destinataire, qui reçoit l'image entière."""
        tete = Noeud({"id": 0, "ip": ip, "port": port, "x0": 0, "y0": 0,
                      "w": largeur, "h": hauteur})
        return cls([tete], largeur, hauteur, 2, **kw)

    def entete(self, n: Noeud, offset: int, derniere: bool,
               type_: int = TYPE_FRAME) -> bytes:
        if self.protocole == 2:
            return entete2(type_, self.format, self.frame_id, self.largeur,
                           self.hauteur, offset, derniere)
        return entete(n.id, self.frame_id, offset, derniere, self.format, type_)

    def envoyer(self, image_rgb: np.ndarray) -> None:
        """image_rgb : (hauteur, largeur, 3) uint8, ordre R G B."""
        if image_rgb.shape != (self.hauteur, self.largeur, 3):
            raise ValueError(f"image {image_rgb.shape}, attendu "
                             f"({self.hauteur}, {self.largeur}, 3)")

        if self.format == FMT_IDX8:
            # Un octet par pixel : le débit est divisé par trois. La palette,
            # elle, voyage dans un paquet de commande, hors du flux de pixels.
            self.maj_palette()
            plan = quantifier_cube(image_rgb)
        elif self.format == FMT_IDX2:
            # Quatre pixels par octet : on emballe l'image entière, ce que
            # seul PXL2 sait transporter (un seul rectangle, toute l'image).
            self.maj_palette()
            plan = emballer_idx2(image_rgb)
        else:
            # Le protocole transporte du BGR : c'est l'ordre qu'attend la dalle,
            # ce qui permet au firmware d'écrire la charge utile en place.
            plan = image_rgb[:, :, ::-1]

        differes: list[tuple[bytes, tuple[str, int]]] = []

        for n in self.noeuds:
            if self.format == FMT_IDX2:
                tuile = plan  # déjà emballée ; PXL2 n'a qu'un destinataire
            else:
                tuile = plan[n.y0:n.y0 + n.h, n.x0:n.x0 + n.w]
            charge = np.ascontiguousarray(tuile).tobytes()
            dest = (n.ip, n.port)

            offset, total = 0, len(charge)
            while offset < total:
                bout = min(self.charge_utile(), total - offset)
                derniere = (offset + bout) >= total
                paquet = (self.entete(n, offset, derniere) +
                          charge[offset:offset + bout])
                offset += bout

                if self.perte and random.random() < self.perte:
                    self.perdus += 1
                    continue
                if self.desordre and random.random() < self.desordre:
                    # Retardé après la dernière tranche : le firmware doit le
                    # traiter comme du désordre, pas comme une nouvelle trame.
                    differes.append((paquet, dest))
                    self.retardes += 1
                    continue

                self.expedier(paquet, dest)

        for paquet, dest in differes:
            self.expedier(paquet, dest)

        self.envois[self.frame_id] = time.monotonic()
        if len(self.envois) > 512:  # borne mémoire : on oublie les vieux
            for k in sorted(self.envois)[:256]:
                del self.envois[k]

        self.frame_id = (self.frame_id + 1) & 0xFFFF

    def expedier(self, paquet: bytes, dest: tuple[str, int]) -> None:
        """Envoie sans jamais lever d'exception. Si le tampon d'émission du
        noyau est plein, on attend qu'il se vide, 50 ms au plus, puis on
        abandonne le paquet : compté, pas fatal. Constaté le 29/09/2026 en
        IDX8 192×192 — `BlockingIOError` au bout de 40 s."""
        for _ in range(2):
            try:
                self.sock.sendto(paquet, dest)
            except BlockingIOError:
                select.select([], [self.sock], [], 0.05)
                continue
            self.octets += len(paquet)
            self.paquets += 1
            return
        self.bloques += 1

    def charge_utile(self) -> int:
        """Taille utile d'une tranche. Multiple de 3 en BGR888 pour ne jamais
        couper un pixel en deux ; sans contrainte en indexé, un pixel = un octet."""
        return CHARGE_UTILE if self.format == FMT_BGR888 else CHARGE_MAX

    def maj_palette(self, force: bool = False) -> None:
        """Renvoie la palette périodiquement : un firmware qui redémarre la
        retrouve sans intervention."""
        maintenant = time.monotonic()
        if not force and maintenant - self.palette_envoyee < 2.0:
            return
        self.palette_envoyee = maintenant
        # La dalle attend du B,G,R.
        charge = bytes([CTRL_PALETTE]) + self.palette[:, ::-1].tobytes()
        for n in self.noeuds:
            paquet = self.entete(n, 0, True, TYPE_CTRL) + charge
            self.expedier(paquet, (n.ip, n.port))

    def regler_luminosite(self, basis: int) -> None:
        charge = bytes([CTRL_LUMINOSITE, max(1, min(255, basis))])
        for n in self.noeuds:
            paquet = self.entete(n, 0, True, TYPE_CTRL) + charge
            self.expedier(paquet, (n.ip, n.port))

    def relever_accuses(self) -> None:
        """Vide la file des accusés arrivés, sans bloquer."""
        while True:
            try:
                data, _ = self.sock.recvfrom(64)
            except BlockingIOError:
                return
            except OSError:
                return
            # PXL1 : accusé de type PING. PXL2 : type ACK. Dans les deux,
            # le type est à l'octet 4 et le frame_id aux octets 6-7.
            if len(data) < ENTETE or data[:4] not in (MAGIC, MAGIC2):
                continue
            type_ = data[4]
            (fid,) = struct.unpack_from("<H", data, 6)
            if type_ != (TYPE_ACK if data[:4] == MAGIC2 else TYPE_PING):
                continue
            self.accuses_recus += 1
            t0 = self.envois.pop(fid, None)
            if t0 is not None:
                self.latences.append(time.monotonic() - t0)
            else:
                self.accuses_orphelins += 1


def choisir_source(nom: str, w: int, h: int, args):
    if nom == "mire":
        return sources.mire(w, h)
    if nom == "anim":
        return sources.animation(w, h)
    if nom == "horloge":
        return sources.horloge(w, h)
    if nom == "ecran":
        region = None
        if args.region:
            x, y, rw, rh = (int(v) for v in args.region.split(","))
            region = (x, y, x + rw, y + rh)
        return sources.capture_ecran(w, h, region)
    if not args.fichier:
        sys.exit(f"--source {nom} exige --fichier")
    if nom == "image":
        return sources.image_fixe(w, h, args.fichier)
    if nom == "gif":
        return sources.gif(w, h, args.fichier)
    if nom == "video":
        return sources.video(w, h, args.fichier)
    sys.exit(f"source inconnue : {nom}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Émetteur PXL2 / PXL1 pour le module ÉCRAN",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--cible", help="PXL2 : adresse de la tête, ip[:port]")
    ap.add_argument("--taille", help="PXL2 : image LxH (défaut : le canevas)")
    ap.add_argument("--sonder", action="store_true",
                    help="PXL2 : afficher ce que l'écran dit de lui et quitter")
    ap.add_argument("--layout", type=Path,
                    help="PXL1 : disposition des nœuds (défaut layout-1x1.toml)")
    ap.add_argument("--source", default="anim",
                    choices=["anim", "mire", "horloge", "image", "gif",
                             "ecran", "video"])
    ap.add_argument("--fichier", help="source, pour image / gif / video")
    ap.add_argument("--region", help="capture d'écran : x,y,largeur,hauteur")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--duree", type=float, default=0.0, help="s, 0 = sans fin")
    ap.add_argument("--perte", type=float, default=0.0,
                    help="%% de paquets volontairement non émis")
    ap.add_argument("--desordre", type=float, default=0.0,
                    help="%% de paquets volontairement retardés")
    ap.add_argument("--format", default="bgr888", choices=sorted(FORMATS),
                    help="bgr888 = 3 octets/pixel ; idx8 = 1 octet + palette ; "
                         "idx2 = 4 gris, 4 pixels par octet (PXL2 seulement)")
    ap.add_argument("--luminosite", type=int,
                    help="luminosité de base de la dalle, 1 à 255")
    args = ap.parse_args()

    fmt = FORMATS[args.format]
    kw = {"perte": args.perte, "desordre": args.desordre, "format_": fmt}

    if args.cible:
        if args.layout:
            sys.exit("--cible (PXL2) et --layout (PXL1) s'excluent")
        ip, _, port = args.cible.partition(":")
        port = int(port) if port else 4242
        ecran = sonder(ip, port)
        if ecran:
            print(f"écran {ip}:{port} — canevas {ecran['largeur']}×{ecran['hauteur']}, "
                  f"formats {' '.join(NOMS_FORMAT.get(f, str(f)) for f in ecran['formats'])}")
            if fmt not in ecran["formats"]:
                print(f"  ⚠ l'écran n'annonce pas {args.format} : il refusera les images")
        else:
            print(f"écran {ip}:{port} — pas de réponse au PING (on émet quand même)")
        if args.sonder:
            return 0 if ecran else 1
        if args.taille:
            w, h = (int(v) for v in args.taille.lower().split("x"))
        elif ecran:
            w, h = ecran["largeur"], ecran["hauteur"]
        else:
            w, h = 192, 192
        em = Emetteur.vers_tete(ip, port, w, h, **kw)
        print(f"image {w}×{h} en PXL2 — l'écran la place lui-même")
    else:
        if fmt == FMT_IDX2:
            sys.exit("idx2 exige PXL2 (--cible) : le firmware v1 ne le lit pas")
        layout = args.layout or Path("layout-1x1.toml")
        if not layout.exists():
            sys.exit(f"disposition introuvable : {layout}")
        em = Emetteur.depuis_layout(layout, **kw)
        print(f"image {em.largeur}×{em.hauteur}, {len(em.noeuds)} nœud(s), PXL1")
        for n in em.noeuds:
            print(f"  {n}")
    octets_trame = {FMT_BGR888: em.largeur * 3, FMT_IDX8: em.largeur,
                    FMT_IDX2: (em.largeur + 3) // 4}[em.format] * em.hauteur
    print(f"  format {args.format} — {octets_trame} octets par trame, "
          f"{octets_trame * 8 * args.fps / 1e6:.2f} Mbit/s à {args.fps:g} img/s")
    if args.luminosite:
        em.regler_luminosite(args.luminosite)
        print(f"  luminosité de base réglée à {args.luminosite}")
    if em.format in (FMT_IDX8, FMT_IDX2):
        em.maj_palette(force=True)
        print("  palette envoyée ("
              + ("cube 6×6×6 + 40 gris" if em.format == FMT_IDX8 else "4 gris") + ")")
    if args.perte or args.desordre:
        print(f"  ⚠ injection : {args.perte:g} % de perte, "
              f"{args.desordre:g} % de désordre")

    try:
        gen = choisir_source(args.source, em.largeur, em.hauteur, args)
    except RuntimeError as e:
        sys.exit(str(e))

    periode = 1.0 / args.fps if args.fps > 0 else 0.0
    debut = prochain = dernier_point = dernier_envoi = time.monotonic()
    trames = 0
    pire_ecart = 0.0
    decrochages = 0

    print(f"\némission « {args.source} » à {args.fps:g} img/s — Ctrl-C pour arrêter\n")
    try:
        while True:
            em.envoyer(next(gen))
            trames += 1

            maintenant = time.monotonic()
            # Décrochage de l'émetteur lui-même : si l'envoi se bloque, le
            # problème n'est ni le firmware ni l'air, mais cette machine.
            ecart = maintenant - dernier_envoi
            dernier_envoi = maintenant
            pire_ecart = max(pire_ecart, ecart)
            if ecart > 3 * periode and trames > 2:
                decrochages += 1
                print(f"    décrochage émetteur : {ecart * 1000:.0f} ms "
                      f"à t+{maintenant - debut:.1f} s")

            if maintenant - dernier_point >= 5.0:
                dt = maintenant - dernier_point
                ligne = (f"  {trames} trames  {trames / dt:.1f} img/s  "
                         f"{em.octets * 8 / dt / 1e6:.2f} Mbit/s  "
                         f"{em.paquets} paquets  "
                         f"pire écart {pire_ecart * 1000:.0f} ms  "
                         f"{decrochages} décrochages")
                if em.perdus or em.retardes:
                    ligne += f"  [injecté : {em.perdus} perdus, {em.retardes} retardés]"
                if em.bloques:
                    ligne += f"  ⚠ {em.bloques} non émis (tampon d'émission plein)"
                print(ligne)

                if em.latences:
                    lat = sorted(em.latences)
                    n = len(lat)
                    print(f"    aller-retour jusqu'à l'affichage : "
                          f"moy {sum(lat) / n * 1000:.1f} ms  "
                          f"min {lat[0] * 1000:.1f}  "
                          f"médiane {lat[n // 2] * 1000:.1f}  "
                          f"p95 {lat[int(n * 0.95)] * 1000:.1f}  "
                          f"max {lat[-1] * 1000:.1f}   "
                          f"({n} apparies / {em.accuses_recus} recus, "
                          f"{em.accuses_orphelins} orphelins, "
                          f"{len(em.envois)} en attente)")
                    em.latences.clear()
                    em.accuses_recus = em.accuses_orphelins = 0
                em.octets = em.paquets = em.perdus = em.retardes = em.bloques = 0
                trames = 0
                pire_ecart = 0.0
                decrochages = 0
                dernier_point = maintenant

            if args.duree and maintenant - debut >= args.duree:
                break

            prochain += periode
            # On attend la prochaine échéance en RELEVANT LES ACCUSÉS au fil de
            # leur arrivée. Les relever une seule fois par tour quantifierait la
            # mesure à la période de trame : la médiane vaudrait exactement
            # 16,7 ms quelle que soit la latence réelle. Constaté le 18/09/2026.
            while True:
                reste = prochain - time.monotonic()
                if reste <= 0:
                    break
                pret, _, _ = select.select([em.sock], [], [], reste)
                if pret:
                    em.relever_accuses()
            em.relever_accuses()
            if prochain < time.monotonic() - periode:
                # La source ne suit pas la cadence : on n'accumule pas de dette.
                prochain = time.monotonic()
    except KeyboardInterrupt:
        print("\narrêt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
