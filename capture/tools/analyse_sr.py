#!/usr/bin/env python3
"""Dépouille une capture PulseView (.sr) du bus LCD d'une Game Boy.

Déroule automatiquement l'arbre de décision de `docs/etapes-detaillees.md` §B.4
et propose une attribution pour chaque voie, **avec les preuves qui l'appuient**.

    ./tools/analyse_sr.py docs/releves/capture.sr
    ./tools/analyse_sr.py --comparer docs/releves/blanc.sr docs/releves/noir.sr

Ce que l'outil sait faire, et qu'un coup d'œil sur PulseView fait mal :

  * séparer `CPL` (8,60 kHz) de `CP`/`CPV` (9,20 kHz), qui ne diffèrent que de
    6,5 % — il les sépare par **le plus grand silence dans la trame** : 1,09 ms
    pour CPL, qui se tait en VBlank, contre 0,109 ms pour CP, qui continue ;
  * compter les impulsions **par salve** de l'horloge pixel : 160 par salve et
    144 salves par trame, c'est une preuve, pas une coïncidence de fréquence ;
  * relever la **période minimale** entre deux fronts, seul nombre qui décide du
    budget du PIO (§B.6) — et dire si le pas d'échantillonnage permet d'y
    répondre ;
  * vérifier que les voies candidates `LD0`/`LD1` ne bougent **que** pendant les
    salves de l'horloge pixel.

Dépendance : numpy.
"""
import argparse
import configparser
import sys
import zipfile
from dataclasses import dataclass, field

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("numpy est requis :  pip install numpy")

# ─────────────────────────────────────────── le timing de la console, §A.1
F_MAITRE = 4_194_304          # Hz
CYCLES_LIGNE = 456
LIGNES_TRAME = 154
LIGNES_VISIBLES = 144
PX_PAR_LIGNE = 160

T_LIGNE = CYCLES_LIGNE / F_MAITRE                              # 108,72 µs
T_TRAME = CYCLES_LIGNE * LIGNES_TRAME / F_MAITRE               # 16,743 ms
F_TRAME = 1.0 / T_TRAME                                        # 59,727 Hz
T_VBLANK = (LIGNES_TRAME - LIGNES_VISIBLES) * T_LIGNE          # 1,087 ms
T_PIXEL_MIN = 1.0 / F_MAITRE                                   # 238,4 ns

TAUX_CPL = LIGNES_VISIBLES * F_TRAME                           # 8 601 Hz
TAUX_CP = LIGNES_TRAME * F_TRAME                               # 9 203 Hz


# ══════════════════════════════════════════════════ lecture du fichier .sr

def _taux_vers_hz(texte):
    """« 24 MHz », « 500 kHz », « 24000000 » → float en Hz."""
    t = texte.strip()
    for suffixe, facteur in (("GHz", 1e9), ("MHz", 1e6),
                             ("kHz", 1e3), ("KHz", 1e3), ("Hz", 1.0)):
        if t.endswith(suffixe):
            return float(t[: -len(suffixe)].strip()) * facteur
    return float(t)


@dataclass
class Capture:
    echantillons: "np.ndarray"   # (n, unitsize) uint8
    taux: float                  # Hz
    noms: list                   # nom de chaque voie
    n_voies: int
    chemin: str

    @property
    def n(self):
        return self.echantillons.shape[0]

    @property
    def duree(self):
        return self.n / self.taux

    def bits(self, i):
        """Voie i, en tableau de 0/1."""
        return (self.echantillons[:, i // 8] >> (i % 8)) & 1


def lire_sr(chemin):
    """Lit un fichier de session sigrok v2 (.sr)."""
    with zipfile.ZipFile(chemin) as z:
        noms_fichiers = z.namelist()
        if "metadata" not in noms_fichiers:
            sys.exit(f"{chemin} : pas de « metadata » — est-ce bien un .sr ?")

        meta = configparser.ConfigParser()
        meta.read_string(z.read("metadata").decode("utf-8", "replace"))

        sections = [s for s in meta.sections() if s.startswith("device")]
        if not sections:
            sys.exit(f"{chemin} : aucune section [device] dans metadata")
        d = meta[sections[0]]

        base = d.get("capturefile", "logic-1")
        unitsize = int(d.get("unitsize", "1"))
        n_voies = int(d.get("total probes", "8"))
        taux = _taux_vers_hz(d.get("samplerate", "0"))
        if taux <= 0:
            sys.exit(f"{chemin} : fréquence d'échantillonnage absente ou nulle")

        noms = [f"D{i}" for i in range(n_voies)]
        for cle, valeur in d.items():
            if cle.startswith("probe") and cle[5:].isdigit():
                i = int(cle[5:]) - 1
                if 0 <= i < n_voies:
                    noms[i] = valeur

        # Les morceaux sont numérotés : logic-1-1, logic-1-2, … logic-1-10.
        # Un tri lexical mettrait -10 avant -2.
        morceaux = [f for f in noms_fichiers if f.startswith(base + "-")]
        morceaux.sort(key=lambda f: int(f.rsplit("-", 1)[1]))
        if not morceaux:
            sys.exit(f"{chemin} : aucun bloc de données « {base}-* »")
        brut = b"".join(z.read(f) for f in morceaux)

    ech = np.frombuffer(brut, dtype=np.uint8)
    reste = ech.size % unitsize
    if reste:
        ech = ech[: ech.size - reste]
    ech = ech.reshape(-1, unitsize)

    return Capture(ech, taux, noms, n_voies, chemin)


# ══════════════════════════════════════════════════ mesures sur une voie

@dataclass
class Mesures:
    idx: int
    nom: str
    n_fronts: int = 0
    taux_fronts: float = 0.0     # fronts montants par seconde, sur la durée
    f_periodique: float = 0.0    # 1 / écart médian entre fronts
    rapport_cyclique: float = 0.0
    periode_min: float = 0.0     # s, entre deux fronts montants
    silence_max: float = 0.0     # s, le plus grand écart entre deux fronts
    largeur_min: float = 0.0     # s, la plus courte impulsion haute
    largeur_min_ech: float = 0.0 # la même, en échantillons
    montants: "np.ndarray" = field(default=None, repr=False)
    salves: "np.ndarray" = field(default=None, repr=False)   # impulsions/salve
    debuts_salves: "np.ndarray" = field(default=None, repr=False)
    fins_salves: "np.ndarray" = field(default=None, repr=False)
    constante: bool = False
    niveau: int = 0              # si constante


def mesurer(cap, i):
    bits = cap.bits(i)
    m = Mesures(idx=i, nom=cap.noms[i])
    m.rapport_cyclique = float(bits.mean())

    diff = np.diff(bits.astype(np.int8))
    montants = np.flatnonzero(diff == 1) + 1
    descendants = np.flatnonzero(diff == -1) + 1
    m.montants = montants
    m.n_fronts = int(montants.size)

    # « Constante » se juge sur le signal, pas sur le nombre de fronts : une
    # impulsion par trame ne donne que 2 fronts sur 2 trames de capture, et
    # ce n'est pas pour autant une voie morte. Bug trouvé en éprouvant
    # l'outil sur une capture synthétique, le 22/09/2026.
    if bits.size == 0 or bits.min() == bits.max():
        m.constante = True
        m.niveau = int(bits[0]) if bits.size else 0
        return m

    # Largeur de la plus courte impulsion : sous ~3 échantillons, le taux
    # d'échantillonnage est trop lent et des impulsions sont PERDUES. Ça se
    # voit ensuite comme un silence anormal, et on accuse le câblage à tort.
    if montants.size and descendants.size:
        j = np.searchsorted(descendants, montants, side="left")
        dans = j < descendants.size
        if dans.any():
            larg = descendants[j[dans]] - montants[dans]
            m.largeur_min_ech = float(larg.min())
            m.largeur_min = m.largeur_min_ech / cap.taux

    m.taux_fronts = m.n_fronts / cap.duree
    if m.n_fronts < 2:
        return m                        # un seul front : rien de plus à dire

    ecarts = np.diff(montants)                      # en échantillons
    m.periode_min = float(ecarts.min()) / cap.taux
    m.silence_max = float(ecarts.max()) / cap.taux
    # L'écart médian ne souffre pas de la troncature aux bords de la capture,
    # contrairement à n_fronts / duree qui sous-estime systématiquement.
    mediane_s = float(np.median(ecarts)) / cap.taux
    m.f_periodique = 1.0 / mediane_s if mediane_s > 0 else 0.0

    # Structure en salves : un « trou » sépare deux lignes. Le seuil ne peut
    # PAS être un simple multiple de la cadence : le PPU se bloque en plein
    # mode 3 pour charger un sprite, ce qui crée des trous de ~3,8 µs au
    # milieu d'une ligne. Mesuré le 26/09/2026 : 16 lignes par trame coupées
    # en 77 + 83 impulsions, alors que les vraies fins de ligne font ≥ 56,9 µs.
    # On se cale donc sur la période LIGNE, pas sur la période pixel.
    mediane = float(np.median(ecarts))
    seuil_ligne = T_LIGNE / 5.0 * cap.taux        # 21,7 µs
    trous = np.flatnonzero(ecarts > max(10.0 * mediane, seuil_ligne, 3.0))
    if trous.size >= 3:
        # Les salves de bord sont tronquées par la fenêtre de capture : on ne
        # garde que celles qui sont entièrement dans la capture.
        m.salves = np.diff(trous)
        m.debuts_salves = montants[trous[:-1] + 1]
        m.fins_salves = montants[trous[1:]]
    return m


# ══════════════════════════════════════════════════ classification (§B.4)

@dataclass
class Verdict:
    signal: str
    confiance: str        # « haute », « moyenne », « faible »
    preuves: list
    alertes: list = field(default_factory=list)


def _proche(valeur, cible, tolerance):
    return abs(valeur - cible) <= tolerance * cible


def classer(m, cap, cpg=None):
    """Attribue un signal à une voie. `cpg` = les mesures de l'horloge pixel,
    si elle est déjà identifiée (sert à qualifier LD0/LD1)."""
    p, a = [], []

    if m.constante:
        return Verdict("— (constante)", "haute",
                       [f"niveau figé à {m.niveau} sur toute la capture"],
                       ["voie non connectée, ou signal absent"])

    if m.n_fronts < 2:
        return Verdict("indéterminé", "faible",
                       [f"{m.n_fronts} front montant dans toute la capture"],
                       ["capture beaucoup trop courte pour ce signal — "
                        "voir la capture LENTE du §B.3"])

    # On classe sur la fréquence PÉRIODIQUE (1 / écart médian), pas sur le
    # comptage rapporté à la durée : ce dernier sous-estime systématiquement,
    # parce que la capture commence et finit au milieu d'une période.
    f = m.f_periodique
    peu = m.n_fronts < 8
    if peu:
        a.append(f"seulement {m.n_fronts} fronts dans la capture : la "
                 "fréquence est indicative, rallonge la capture lente")

    # ── horloge pixel : la seule à monter à plusieurs MHz
    if m.taux_fronts > 200_000:
        if m.salves is not None and m.salves.size:
            imp = float(np.median(m.salves))
            salves_par_trame = (m.salves.size + 1) / cap.duree / F_TRAME
            p.append(f"{imp:.0f} impulsions par salve (attendu {PX_PAR_LIGNE})")
            p.append(f"{salves_par_trame:.1f} salves par trame "
                     f"(attendu {LIGNES_VISIBLES} ; les salves des bords de "
                     "capture sont tronquées)")
            bon_imp = _proche(imp, PX_PAR_LIGNE, 0.05)
            bon_salves = _proche(salves_par_trame, LIGNES_VISIBLES, 0.08)
            if bon_imp and bon_salves:
                conf = "haute"
            elif bon_imp or bon_salves:
                conf = "moyenne"
                a.append("une des deux structures ne tombe pas juste — "
                         "masse trop longue, ou fronts comptés deux fois ?")
            else:
                conf = "faible"
                a.append("structure en salves présente mais hors cible")
        else:
            conf = "faible"
            p.append(f"{m.taux_fronts / 1e6:.2f} M fronts/s")
            a.append("aucune structure en salves détectée : recapture plus "
                     "longtemps, ou la masse fait rebondir les fronts")
        p.append(f"période minimale {m.periode_min * 1e9:.0f} ns "
                 f"(attendu {T_PIXEL_MIN * 1e9:.0f})")
        if m.periode_min < 0.5 * T_PIXEL_MIN:
            a.append("période minimale très inférieure au cycle maître : "
                     "fronts comptés deux fois (rebond), ou le modèle §A.1 "
                     "est faux — À COMPRENDRE avant d'écrire du firmware")
        return Verdict("HORLOGE PIXEL  [carte MGB : « CP »]", conf, p, a)

    # ── verrou de ligne / horloge de ligne. Les deux battent à la ligne
    #    (9 203 Hz) ; ce qui les sépare, c'est le silence de la VBlank.
    if 6_000 < f < 12_000:
        p.append(f"{f:.0f} Hz de cadence (une impulsion par ligne)")
        p.append(f"{m.taux_fronts:.0f} fronts/s en moyenne "
                 f"(CPL : {TAUX_CPL:.0f} · CP : {TAUX_CP:.0f})")
        p.append(f"plus grand silence {m.silence_max * 1e3:.2f} ms")
        if m.silence_max > 5 * T_LIGNE:
            p.append(f"→ se tait {m.silence_max / T_LIGNE:.0f} lignes d'affilée : "
                     "c'est la VBlank")
            conf = "faible" if peu else (
                "haute" if m.silence_max < 25 * T_LIGNE else "moyenne")
            if m.silence_max >= 25 * T_LIGNE:
                a.append(f"silence bien plus long que la VBlank attendue "
                         f"({T_VBLANK * 1e3:.2f} ms) : impulsions manquantes ?")
            return Verdict("VERROU DE LIGNE, 144/trame  [carte MGB : « P2-ST »]", conf, p, a)
        p.append(f"→ ne se tait jamais 5 lignes d'affilée : "
                 "il compte aussi la VBlank")
        conf = "faible" if peu else "haute"
        if m.silence_max > 1.5 * T_LIGNE:
            conf = "moyenne"
            a.append(f"un silence de {m.silence_max / T_LIGNE:.1f} lignes alors "
                     "qu'on en attend 1 : des impulsions manquent")
        return Verdict("HORLOGE DE LIGNE, 154/trame  [carte MGB : « P2-CPL »]", conf, p, a)

    # ── échelle de la trame : ST (impulsion) ou FR (carré à moitié fréquence)
    if 45 < f < 75:
        p.append(f"{f:.2f} Hz (attendu {F_TRAME:.2f})")
        p.append(f"rapport cyclique {m.rapport_cyclique * 100:.2f} %")
        if m.rapport_cyclique < 0.25:
            p.append("→ impulsion brève à la cadence trame")
            return Verdict("DÉPART DE TRAME / VSYNC  [carte MGB : « P2-S »]", "moyenne" if peu else "haute", p, a)
        a.append("cadence de trame mais rapport cyclique élevé : "
                 "ce n'est pas l'impulsion attendue, à regarder à l'œil")
        return Verdict("cadence trame, rapport cyclique inattendu ?", "faible", p, a)

    if 22 < f < 40:
        p.append(f"{f:.2f} Hz (moitié de la cadence trame, {F_TRAME / 2:.2f})")
        p.append(f"rapport cyclique {m.rapport_cyclique * 100:.1f} %")
        if 0.35 < m.rapport_cyclique < 0.65:
            p.append("→ carré qui alterne à chaque trame")
            a.append("utilisable comme VSYNC de secours, mais il faut alors "
                     "déclencher sur LES DEUX fronts (§D.5)")
            return Verdict("INVERSION  [carte MGB : « P2-FR », par LIGNE]",
                           "moyenne" if peu else "haute", p, a)
        return Verdict("FR ?", "faible", p, a)

    # ── données : rythme dicté par l'image, donc aucune fréquence propre
    p.append(f"{m.taux_fronts:.0f} fronts/s — pas de cadence propre")
    if cpg is not None and cpg.debuts_salves is not None and m.montants.size:
        dans = _fraction_dans_salves(m.montants, cpg.debuts_salves,
                                     cpg.fins_salves)
        p.append(f"{dans * 100:.1f} % des fronts tombent dans une salve de CPG")
        if dans > 0.95:
            p.append(f"plus grand silence {m.silence_max * 1e3:.2f} ms "
                     f"(VBlank = {T_VBLANK * 1e3:.2f} ms)")
            conf = "haute" if m.n_fronts > 200 else "moyenne"
            if m.n_fronts <= 200:
                a.append("peu de fronts : l'image capturée est trop uniforme. "
                         "Recapture sur une image contrastée (damier, texte)")
            return Verdict("DONNÉES  [carte MGB : « P2-LD0 » ou « P2-LD1 »]", conf, p, a)
        a.append("les fronts débordent des salves de l'horloge pixel : "
                 "ce n'est probablement pas une ligne de données")
    else:
        a.append("horloge pixel non identifiée : impossible de qualifier "
                 "cette voie comme ligne de données")
    return Verdict("inconnu", "faible", p, a)


def _fraction_dans_salves(montants, debuts, fins):
    """Part des fronts de `montants` qui tombent dans [debut, fin] d'une salve."""
    if debuts is None or debuts.size == 0:
        return 0.0
    # Pour chaque front, la salve dont le début le précède.
    j = np.searchsorted(debuts, montants, side="right") - 1
    valide = j >= 0
    if not valide.any():
        return 0.0
    dans = np.zeros(montants.size, dtype=bool)
    dans[valide] = montants[valide] <= fins[j[valide]]
    return float(dans.mean())


# ══════════════════════════════════════════════════ rapport

def _ms(x):
    return f"{x * 1e3:.3f} ms"


def rapport(cap):
    print(f"\n═══ {cap.chemin} ═══")
    print(f"  {cap.n_voies} voies · {cap.taux / 1e6:.3f} MS/s · "
          f"pas de {1e9 / cap.taux:.1f} ns")
    print(f"  {cap.n} échantillons · {_ms(cap.duree)} · "
          f"{cap.duree / T_TRAME:.2f} trames Game Boy")

    if cap.duree < 2 * T_TRAME:
        print(f"\n  ⚠️  capture trop courte : {_ms(cap.duree)} pour "
              f"{_ms(2 * T_TRAME)} recommandées (2 trames).")
        print("     Les structures par trame ne seront pas fiables.")

    # Résolution : peut-on répondre à la question du §B.6 ?
    pts_par_pixel = cap.taux * T_PIXEL_MIN
    print(f"\n  Résolution : {pts_par_pixel:.1f} échantillons par période "
          f"pixel minimale ({T_PIXEL_MIN * 1e9:.0f} ns)")
    if pts_par_pixel < 3:
        print("     ❌ insuffisant pour voir l'horloge pixel. Recapture plus vite.")
    elif pts_par_pixel < 5:
        print("     🔶 juste — on compte les fronts, on ne juge pas leur forme.")
    else:
        print("     ✅ suffisant pour compter les fronts (§B.6).")

    mesures = [mesurer(cap, i) for i in range(cap.n_voies)]

    courtes = [m for m in mesures
               if not m.constante and 0 < m.largeur_min_ech < 3]
    if courtes:
        noms = ", ".join(m.nom for m in courtes)
        print(f"\n  ⚠️  Impulsions trop courtes pour ce taux sur : {noms}")
        for m in courtes:
            print(f"       {m.nom} : la plus courte fait "
                  f"{m.largeur_min_ech:.1f} échantillon(s) "
                  f"({m.largeur_min * 1e9:.0f} ns)")
        print("     En dessous de 3 échantillons, des impulsions sont PERDUES,")
        print("     ce qui se lit ensuite comme un silence anormal et fait")
        print("     accuser le câblage à tort. Recapture plus vite.")

    # L'horloge pixel sert de référence pour qualifier LD0/LD1 : on la cherche
    # d'abord, puis on classe tout le monde.
    cpg = None
    for m in mesures:
        if not m.constante and m.taux_fronts > 200_000:
            if cpg is None or m.taux_fronts > cpg.taux_fronts:
                cpg = m

    print("\n── Mesures ─────────────────────────────────────────────────────")
    print(f"  {'voie':<6}{'fronts/s':>12}{'cadence':>12}{'période min':>14}"
          f"{'silence max':>14}{'r.cycl.':>10}")
    for m in mesures:
        if m.constante:
            print(f"  {m.nom:<6}{'—':>12}{'—':>12}{'—':>14}{'—':>14}"
                  f"{'figé ' + str(m.niveau):>10}")
            continue
        taux = (f"{m.taux_fronts / 1e6:.3f} M" if m.taux_fronts > 1e5
                else f"{m.taux_fronts:.1f}")
        cad = ("—" if m.f_periodique <= 0 else
               f"{m.f_periodique / 1e6:.2f} M" if m.f_periodique > 1e5
               else f"{m.f_periodique:.1f}")
        print(f"  {m.nom:<6}{taux:>12}{cad:>12}"
              f"{m.periode_min * 1e9:>11.0f} ns"
              f"{m.silence_max * 1e6:>11.0f} µs"
              f"{m.rapport_cyclique * 100:>9.1f} %")

    print("\n── Attribution proposée ────────────────────────────────────────")
    verdicts = {}
    for m in mesures:
        v = classer(m, cap, cpg if m is not cpg else None)
        verdicts[m.idx] = v
        marque = {"haute": "✔", "moyenne": "~", "faible": "?"}[v.confiance]
        print(f"\n  {m.nom} → {v.signal}   [{marque} confiance {v.confiance}]")
        for preuve in v.preuves:
            print(f"      · {preuve}")
        for alerte in v.alertes:
            print(f"      ⚠️  {alerte}")

    _bilan(mesures, verdicts, cap)
    return mesures, verdicts


def _bilan(mesures, verdicts, cap):
    print("\n── Bilan ───────────────────────────────────────────────────────")
    trouves = {}
    for m in mesures:
        v = verdicts[m.idx]
        cle = v.signal.split(" ")[0]
        if cle == "HORLOGE":
            cle = "HORLOGE" if "PIXEL" in v.signal else "LIGNE154"
        trouves.setdefault(cle, []).append(m.nom)

    attendus = [("HORLOGE", "horloge pixel"), ("VERROU", "verrou de ligne 144/trame"),
                ("DÉPART", "VSYNC"), ("DONNÉES", "lignes de données")]
    manquants = []
    for cle, role in attendus:
        if cle == "DONNÉES":
            n = len(trouves.get("DONNÉES", []))
            if n < 2:
                manquants.append(f"{role} : {n} voie(s) sur 2")
        elif cle not in trouves:
            manquants.append(f"{cle} ({role})")

    if manquants:
        print("  ❌ Signaux non identifiés :")
        for x in manquants:
            print(f"       {x}")
        print("\n     Pistes : la voie n'est pas connectée · la masse est trop "
              "longue ·\n     la capture est trop courte · l'image affichée est "
              "trop uniforme\n     pour faire bouger LD0/LD1.")
    else:
        print("  ✅ Les 5 signaux nécessaires sont identifiés.")
        print("     Il reste à faire le TEST BLANC/NOIR (§B.5) — la fréquence")
        print("     seule n'est pas une preuve :")
        print("         ./tools/analyse_sr.py --comparer blanc.sr noir.sr")

    if "DONNÉES" in trouves and len(trouves["DONNÉES"]) == 2:
        print(f"\n  ℹ️  {trouves['DONNÉES'][0]} et {trouves['DONNÉES'][1]} sont les deux "
              "lignes de données, mais\n     l'outil ne sait pas laquelle est "
              "LD0 et laquelle est LD1. C'est sans\n     conséquence : l'ordre "
              "se corrige en permutant 4 entrées de palette.")

    print("\n  À recopier dans docs/signaux-mgb.md :")
    print("  | Voie | Signal | Confiance |")
    print("  |---|---|---|")
    for m in mesures:
        v = verdicts[m.idx]
        print(f"  | {m.nom} | {v.signal} | {v.confiance} |")
    print()


# ══════════════════════════════════════════════════ test blanc / noir (§B.5)

def comparer(chemin_a, chemin_b):
    a, b = lire_sr(chemin_a), lire_sr(chemin_b)
    if a.n_voies != b.n_voies:
        sys.exit("les deux captures n'ont pas le même nombre de voies")

    print(f"\n═══ test blanc/noir ═══")
    print(f"  A : {chemin_a}  ({_ms(a.duree)})")
    print(f"  B : {chemin_b}  ({_ms(b.duree)})")

    # On ne compare les niveaux QUE pendant les salves de l'horloge pixel :
    # hors salve, les lignes de données ne veulent rien dire.
    fenetres = {}
    for cap, cle in ((a, "A"), (b, "B")):
        cpg = None
        for i in range(cap.n_voies):
            m = mesurer(cap, i)
            if not m.constante and m.taux_fronts > 200_000:
                if cpg is None or m.taux_fronts > cpg.taux_fronts:
                    cpg = m
        fenetres[cle] = (cap, cpg)

    # Sans horloge pixel dans l'une des captures, on ne peut pas restreindre aux
    # salves. Le dire explicitement : une première version annonçait « aucune
    # voie ne bascule », ce qui envoie chercher le problème du mauvais côté.
    manquantes = [cle for cle, (_, cpg) in fenetres.items() if cpg is None]
    degrade = bool(manquantes)
    if degrade:
        print(f"\n  ⚠️  Horloge pixel absente de la capture "
              f"{' et '.join(manquantes)}.")
        print("     Impossible de restreindre la comparaison aux salves : la sonde")
        print("     de l'horloge pixel a-t-elle décroché ?")
        print("     → repli sur les niveaux BRUTS, VBlank et HBlank comprises.")
        print("       Moins rigoureux, mais un écart franc reste concluant.")

    titre = ("niveaux BRUTS (repli)" if degrade
             else "niveau moyen pendant les salves de l'horloge pixel")
    print(f"\n  {titre} :")
    print(f"  {'voie':<6}{'A':>10}{'B':>10}{'écart':>10}   verdict")
    candidats = []
    for i in range(a.n_voies):
        if degrade:
            na, nb = float(a.bits(i).mean()), float(b.bits(i).mean())
        else:
            na = _niveau_en_salve(*fenetres["A"], i)
            nb = _niveau_en_salve(*fenetres["B"], i)
        if na is None or nb is None:
            print(f"  {a.noms[i]:<6}{'—':>10}{'—':>10}{'—':>10}   "
                  "pas de salve de référence")
            continue
        ecart = abs(na - nb)
        verdict = ""
        if ecart > 0.5:
            verdict = "✔ BASCULE — ligne de données"
            candidats.append(a.noms[i])
        elif ecart > 0.15:
            verdict = "~ bouge un peu — à regarder à l'œil"
        print(f"  {a.noms[i]:<6}{na * 100:>9.1f}%{nb * 100:>9.1f}%"
              f"{ecart * 100:>9.1f}%   {verdict}")

    print()
    if len(candidats) == 2:
        print(f"  ✅ {candidats[0]} et {candidats[1]} basculent ENSEMBLE d'un "
              "extrême à l'autre.")
        print("     C'est la preuve d'identification de LD0 et LD1 (§B.5).")
    elif len(candidats) == 1:
        print(f"  ❌ Une seule voie bascule ({candidats[0]}).")
        print("     Les deux lignes de données doivent bouger ensemble. "
              "L'autre est\n     probablement un signal de commande — "
              "reprends l'arbre §B.4.")
    elif not candidats:
        print("  ❌ Aucune voie ne bascule.")
        print("     Les deux images sont-elles vraiment l'une blanche et "
              "l'autre noire ?\n     Un rétroéclairage éteint ne suffit pas : "
              "il faut un CONTENU noir.")
    else:
        print(f"  ⚠️  {len(candidats)} voies basculent : "
              f"{', '.join(candidats)}.")
        print("     Plus de deux lignes de données, c'est un signal de "
              "commande qui suit\n     l'image, ou de la diaphonie. "
              "Vérifie la masse.")

    # La polarité, qui décide de l'ordre de la palette — pas du firmware.
    if len(candidats) == 2:
        if degrade:
            moy_a = float(np.mean([a.bits(a.noms.index(c)).mean()
                                   for c in candidats]))
        else:
            moy_a = np.mean([_niveau_en_salve(*fenetres["A"], a.noms.index(c))
                             for c in candidats])
        print(f"\n  Polarité : dans A, les lignes de données sont à "
              f"{moy_a * 100:.0f} % de niveau haut.")
        print("     Si A est l'image BLANCHE et que ce chiffre est bas, alors "
              "00 = blanc,\n     ce qui est le cas attendu sur DMG/MGB. "
              "Dans tous les cas : aucune ligne\n     de firmware n'en dépend, "
              "seul l'ordre des 4 entrées de palette.")
    print()


def _niveau_en_salve(cap, cpg, i):
    """Niveau moyen de la voie i, restreint aux salves de l'horloge pixel."""
    if cpg is None or cpg.debuts_salves is None or cpg.debuts_salves.size == 0:
        return None
    bits = cap.bits(i)
    total = somme = 0
    # Quelques salves suffisent, et ça évite de parcourir toute la capture.
    for d, f in zip(cpg.debuts_salves[:200], cpg.fins_salves[:200]):
        somme += int(bits[d:f].sum())
        total += int(f - d)
    return somme / total if total else None


# ══════════════════════════════════════════════════════════════════ main

def main():
    ap = argparse.ArgumentParser(
        description="Dépouille une capture .sr du bus LCD d'une Game Boy.")
    ap.add_argument("capture", nargs="?", help="fichier .sr à analyser")
    ap.add_argument("--comparer", nargs=2, metavar=("A.sr", "B.sr"),
                    help="test blanc/noir : compare deux captures (§B.5)")
    args = ap.parse_args()

    if args.comparer:
        comparer(*args.comparer)
    elif args.capture:
        rapport(lire_sr(args.capture))
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
