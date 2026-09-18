"""Sources d'images pour pixelpush.

Chaque source est un générateur qui produit des tableaux `(h, w, 3)` en uint8,
ordre R G B. Le découpage par nœud et l'encodage sont l'affaire de l'émetteur :
une source ne sait rien du protocole ni de la géométrie des dalles.
"""

from __future__ import annotations

import shutil
import subprocess
import time
from typing import Iterator

import numpy as np

Image = np.ndarray


# --- mires et motifs synthétiques -------------------------------------------

def mire(w: int, h: int) -> Iterator[Image]:
    """Mire fixe : cadre, dégradé, damier, barres de couleur."""
    img = np.zeros((h, w, 3), np.uint8)
    img[2:h // 4] = np.linspace(0, 255, w, dtype=np.uint8)[None, :, None]
    d = (np.add.outer(np.arange(h), np.arange(w)) & 1).astype(np.uint8) * 255
    img[h // 4 + 2:h // 2] = d[h // 4 + 2:h // 2, :, None]
    bandes = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    for i, c in enumerate(bandes):
        img[h // 2 + 2:3 * h // 4, i * w // 4:(i + 1) * w // 4] = c
    img[0, :] = img[-1, :] = img[:, 0] = img[:, -1] = 255
    while True:
        yield img


def animation(w: int, h: int) -> Iterator[Image]:
    """Plasma animé, avec un curseur qui fait le tour du cadre.

    Le curseur est le meilleur juge de la régularité : s'il avance d'un pas
    constant, la chaîne tient la cadence. Un œil repère une saccade bien mieux
    sur un point qui se déplace que sur un motif qui ondule.
    """
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    perimetre = 2 * (w + h) - 4
    t = 0.0
    while True:
        img = np.empty((h, w, 3), np.uint8)
        v = (np.sin(xx / 6 + t) + np.sin(yy / 7 - t * 1.3) +
             np.sin((xx + yy) / 9 + t * 0.7))
        img[:, :, 0] = ((v + 3) * 42).astype(np.uint8)
        img[:, :, 1] = ((np.sin(v * 2 + t) + 1) * 127).astype(np.uint8)
        img[:, :, 2] = ((np.cos(v + t * 0.5) + 1) * 127).astype(np.uint8)

        p = int(t * 30) % perimetre
        if p < w:
            img[0, p] = 255
        elif p < w + h - 1:
            img[p - w + 1, w - 1] = 255
        elif p < 2 * w + h - 2:
            img[h - 1, w - 1 - (p - w - h + 2)] = 255
        else:
            img[h - 1 - (p - 2 * w - h + 3), 0] = 255

        yield img
        t += 0.06


def horloge(w: int, h: int) -> Iterator[Image]:
    """Horloge à aiguilles. Source lente mais toujours vivante, utile pour
    laisser la dalle allumée longtemps et repérer une dérive ou un blocage."""
    cx, cy = (w - 1) / 2, (h - 1) / 2
    rayon = min(w, h) / 2 - 2

    def aiguille(img, angle, longueur, couleur, epaisseur=1):
        n = int(longueur * 3)
        for i in range(n):
            r = longueur * i / n
            x = int(round(cx + r * np.sin(angle)))
            y = int(round(cy - r * np.cos(angle)))
            for dx in range(-epaisseur // 2, epaisseur // 2 + 1):
                for dy in range(-epaisseur // 2, epaisseur // 2 + 1):
                    if 0 <= x + dx < w and 0 <= y + dy < h:
                        img[y + dy, x + dx] = couleur

    while True:
        img = np.zeros((h, w, 3), np.uint8)
        for k in range(12):
            a = k * np.pi / 6
            x = int(round(cx + rayon * np.sin(a)))
            y = int(round(cy - rayon * np.cos(a)))
            if 0 <= x < w and 0 <= y < h:
                img[y, x] = (80, 80, 80) if k % 3 else (180, 180, 180)

        maintenant = time.localtime()
        frac = time.time() % 1.0
        sec = maintenant.tm_sec + frac
        aiguille(img, (maintenant.tm_hour % 12 + maintenant.tm_min / 60) * np.pi / 6,
                 rayon * 0.5, (0, 200, 255), 2)
        aiguille(img, (maintenant.tm_min + sec / 60) * np.pi / 30,
                 rayon * 0.75, (255, 255, 255), 1)
        aiguille(img, sec * np.pi / 30, rayon * 0.85, (255, 60, 60), 1)
        yield img


# --- sources externes -------------------------------------------------------

def image_fixe(w: int, h: int, chemin: str) -> Iterator[Image]:
    from PIL import Image as PImage
    im = PImage.open(chemin).convert("RGB").resize((w, h), PImage.LANCZOS)
    img = np.asarray(im, np.uint8)
    while True:
        yield img


def gif(w: int, h: int, chemin: str) -> Iterator[Image]:
    """GIF animé, décodé une fois puis rejoué en boucle.

    Pillow suffit : ni ffmpeg ni OpenCV ne sont nécessaires. Les durées propres
    à chaque image du GIF sont ignorées — c'est la cadence de l'émetteur qui
    fait foi, ce qui rend la mesure de fluidité lisible.
    """
    from PIL import Image as PImage, ImageSequence
    src = PImage.open(chemin)
    images = [
        np.asarray(f.convert("RGB").resize((w, h), PImage.LANCZOS), np.uint8)
        for f in ImageSequence.Iterator(src)
    ]
    if not images:
        raise ValueError(f"{chemin} ne contient aucune image")
    while True:
        yield from images


def capture_ecran(w: int, h: int, region: tuple | None = None) -> Iterator[Image]:
    """Capture de l'écran, réduite à la dalle.

    ⚠️ Une capture plein écran coûte cher — compter 50 à 100 ms, soit une
    dizaine d'images par seconde au mieux. Passer `--region` réduit
    considérablement le coût et permet d'approcher les 60 img/s.
    """
    from PIL import Image as PImage, ImageGrab
    while True:
        im = ImageGrab.grab(bbox=region, xdisplay="")
        yield np.asarray(im.convert("RGB").resize((w, h), PImage.LANCZOS), np.uint8)


def video(w: int, h: int, chemin: str) -> Iterator[Image]:
    """Fichier vidéo, décodé par ffmpeg en flux de pixels bruts.

    Choisi plutôt qu'OpenCV pour ne pas ajouter de dépendance Python lourde.
    ffmpeg redimensionne lui-même : le travail reste hors de Python.
    """
    if shutil.which("ffmpeg") is None:
        raise RuntimeError(
            "ffmpeg est absent — nécessaire pour --source video.\n"
            "  sudo apt install ffmpeg\n"
            "Sans lui, --source gif lit les GIF animés via Pillow.")

    cmd = [
        "ffmpeg", "-loglevel", "error", "-stream_loop", "-1", "-re",
        "-i", chemin,
        "-vf", f"scale={w}:{h}:flags=lanczos",
        "-pix_fmt", "rgb24", "-f", "rawvideo", "-",
    ]
    taille = w * h * 3
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=taille * 4)
    try:
        while True:
            brut = proc.stdout.read(taille)
            if len(brut) < taille:
                break
            yield np.frombuffer(brut, np.uint8).reshape((h, w, 3))
    finally:
        proc.kill()
