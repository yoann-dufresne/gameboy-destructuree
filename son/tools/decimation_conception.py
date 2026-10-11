#!/usr/bin/env python3
"""Conçoit le filtre de la décimation 192 kHz → 48 kHz, et le justifie.

    ./tools/decimation_conception.py

Il refait le calcul des coefficients recopiés dans
firmware/commun/decimation.hpp, et dit ce qu'ils valent. Le test
firmware/commun/test/test_decimation.cpp mesure ensuite le code lui-même, en
virgule fixe, avec des sinus.

Ce qu'il faut savoir sans être spécialiste :

- la source échantillonne à 192 kHz et garde un échantillon sur quatre. Tout
  son au-delà de 24 kHz se « replierait » alors dans ce qu'on entend, comme
  une roue de diligence qui semble tourner à l'envers au cinéma. Le filtre
  éteint ces sons avant de jeter les échantillons ;
- **la bande utile** va jusqu'à 20 kHz : elle doit passer intacte ;
- **la bande à éteindre** commence à 28 kHz : de 20 à 28 kHz, ce qui se
  replie tombe entre 20 et 24 kHz, où personne n'entend rien.

Deux étages « demi-bande » (192 → 96, puis 96 → 48 kHz) : un coefficient sur
deux y est nul, ce qui divise le calcul par quatre environ par rapport à un
seul grand filtre.

Dépendances : numpy, scipy.
"""
import numpy as np
from scipy.signal import freqz, remez

BANDE_UTILE = 20000.0
Q = 15                       # coefficients en Q15


def demi_bande(n, fs):
    """Demi-bande de n coefficients : bandes symétriques autour de fs/4, le
    coefficient central vaut 1/2, ceux à distance paire du centre sont nuls."""
    h = remez(n, [0, BANDE_UTILE, fs / 2 - BANDE_UTILE, fs / 2], [1, 0],
              fs=fs, maxiter=200)
    c = (n - 1) // 2
    cote = [int(round(h[c + k] * 2 ** Q)) for k in range(1, c + 1, 2)]
    # Gain exactement 1 pour le continu : le centre vaut 2^(Q-1), il manque
    # ce qu'il faut au plus grand coefficient.
    cote[0] += (2 ** Q - 2 ** (Q - 1) - 2 * sum(cote)) // 2
    return cote


def deplier(cote):
    c = 2 * len(cote) - 1
    h = np.zeros(2 * c + 1)
    h[c] = 2 ** (Q - 1)
    for j, v in enumerate(cote):
        h[c - (2 * j + 1)] = h[c + (2 * j + 1)] = v
    return h / 2 ** Q


def reponse(h, fs):
    w, H = freqz(h, worN=1 << 16, fs=fs)
    return w, 20 * np.log10(np.abs(H) + 1e-15)


def fr(x, d=2):
    return f"{x:.{d}f}".replace(".", ",")


def main():
    etages = (("ETAGE1", 15, 192000.0, 76000.0), ("ETAGE2", 55, 96000.0, 28000.0))
    for nom, n, fs, eteint in etages:
        cote = demi_bande(n, fs)
        w, a = reponse(deplier(cote), fs)
        utile = a[w <= BANDE_UTILE]
        rej = a[w >= eteint].max()
        print(f"{nom} : {n} coefficients à {fs / 1000:.0f} kHz, "
              f"{len(cote)} multiplications par échantillon produit")
        print(f"  bande utile 0–20 kHz : de {fr(utile.min(), 4)} à "
              f"{fr(utile.max(), 4)} dB")
        print(f"  au-delà de {eteint / 1000:.0f} kHz : {fr(rej, 1)} dB au plus")
        print(f"  static constexpr int16_t {nom}[{len(cote)}] = {{"
              + ", ".join(str(v) for v in cote) + "};")

    # Les deux étages ensemble, vus de l'entrée à 192 kHz : un sinus de
    # fréquence f ressort à la fréquence repliée ; on regarde ce qui retombe
    # dans la bande utile.
    h1, h2 = deplier(demi_bande(15, 192000.0)), deplier(demi_bande(55, 96000.0))
    f = np.linspace(0, 96000, 19201)
    _, H1 = freqz(h1, worN=f, fs=192000.0)
    _, H2 = freqz(h2, worN=np.abs(((f + 48000) % 96000) - 48000), fs=96000.0)
    total = 20 * np.log10(np.abs(H1 * H2) + 1e-15)
    sortie = np.abs(((f + 24000) % 48000) - 24000)
    dans_utile = (sortie <= BANDE_UTILE) & (f > 24000)
    multiplications = 2 * len(demi_bande(15, 192000.0)) + len(demi_bande(55, 96000.0))
    print()
    print(f"Ensemble : ce qui se replie dans la bande utile est atténué d'au "
          f"moins {fr(-total[dans_utile].max(), 1)} dB.")
    print(f"Calcul : {multiplications} multiplications par échantillon de "
          f"sortie et par voie, soit "
          f"{fr(multiplications * 48000 * 2 / 1e6, 1)} millions par seconde "
          f"pour deux voies.")


if __name__ == "__main__":
    main()
