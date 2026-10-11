#!/usr/bin/env python3
"""Éprouve tools/banc.py sans carte.

    python3 tools/test_banc.py

Ce qu'on vérifie :
  - les seaux de l'histogramme sont ceux de firmware/commun/banc.h ;
  - chaque champ que l'outil lit existe bien dans les lignes qu'impriment les
    firmwares, y compris celui du sniffer : un champ renommé d'un côté seulement
    ferait un verdict faux sans bruit ;
  - les enregistrements fabriqués donnent le verdict attendu : les scénarios
    « bon » sont tenus, chaque défaut des scénarios « mauvais » est vu ;
  - les compteurs cumulés survivent à une remise à zéro.
"""
import io
import os
import re
import contextlib
import random
import sys
import unittest

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
import banc  # noqa: E402

FIRMWARE = os.path.join(ICI, "..", "firmware")
SNIFFER = os.path.join(ICI, "..", "..", "capture", "firmware", "sniffer", "src", "net", "reseau.cpp")


def source(*chemin):
    with open(os.path.join(*chemin), encoding="utf-8") as f:
        return f.read()


def champs(texte):
    """Les noms de champs écrits par les printf d'un fichier : « cle=% » et « cle:% »."""
    return set(re.findall(r"(\w+)[=:]%", texte)) | set(re.findall(r'"(rtt)"', texte))


class Bornes(unittest.TestCase):
    def test_identiques_a_banc_h(self):
        h = source(FIRMWARE, "commun", "banc.h")
        bloc = re.search(r"BANC_BORNES_US\[[^\]]*\]\s*=\s*\{([^}]*)\}", h).group(1)
        bornes = [int(x) for x in re.findall(r"\d+", bloc)]
        self.assertEqual(bornes, banc.BORNES_US)


class Champs(unittest.TestCase):
    LUS = {
        ("radio", "point_acces.cpp"): {"canal", "sta", "emis", "echecs", "recus", "perdus", "acc",
                                       "retards", "copies", "abandons", "redemandes", "faux",
                                       "duree_ms", "rafale", "rtt", "num", "ct", "eco",
                                       "completes", "incompletes", "trous50", "envoyes",
                                       "gigue_us"},
        ("radio", "client.cpp"): {"num", "ct", "eco", "recus", "perdus", "completes",
                                  "incompletes", "trous50", "envoyes", "acc", "gigue_us", "rtt"},
        ("radio", "faux_ecran.cpp"): {"num", "completes", "incompletes", "trous50"},
        ("bus", "maitre.cpp"): {"cycles", "ok", "ecr", "lec", "crc", "seq", "refus", "feux"},
        ("bus", "esclave.cpp"): {"recus", "refus", "incoherents"},
    }

    def test_firmwares_du_banc(self):
        for chemin, lus in self.LUS.items():
            with self.subTest(fichier=chemin[1]):
                manquants = lus - champs(source(FIRMWARE, *chemin))
                self.assertFalse(manquants, f"{chemin[1]} n'imprime pas {sorted(manquants)}")

    def test_sniffer(self):
        lus = {"tranches_perdues", "perdues", "douteuses", "echecs", "emission_max_us",
               "ecrans", "refuses", "rtt_n", "rtt_moy_us", "rtt_max_us", "ip"}
        texte = source(SNIFFER)
        manquants = (lus - {"ip"}) - champs(texte)
        self.assertFalse(manquants, f"le sniffer n'imprime pas {sorted(manquants)}")
        self.assertIn("ip=%s", texte)


class Verdicts(unittest.TestCase):
    def enregistrement(self, scenario, duree=600):
        fonction, mauvais = banc.SCENARIOS[scenario]
        lignes = []
        for brut in fonction(duree, mauvais, random.Random(3)):
            t, port, texte = brut.split(" ", 2)
            lignes.append((float(t), port, banc.lire_ligne(texte)))
        return banc.Enregistrement(lignes)

    def resultats(self, scenario):
        with contextlib.redirect_stdout(io.StringIO()):
            return dict(banc.analyser(self.enregistrement(scenario)))

    @staticmethod
    def echecs(resultats):
        return [texte for v in resultats for s, texte in v.constats if s == "❌"]

    def test_bons_scenarios_tenus(self):
        for sc, test in (("r1-bon", "R1"), ("r2-bon", "R2"), ("r3-bon", "R3")):
            with self.subTest(scenario=sc):
                (nom, res), = self.resultats(sc).items()
                self.assertTrue(nom.startswith(test))
                self.assertEqual(self.echecs(res), [])

    def test_r1_mauvais(self):
        (res,) = self.resultats("r1-mauvais").values()
        echecs = " | ".join(self.echecs(res))
        for attendu in ("images perdues", "gels", "blocs perdus", "copies de 1 Mo"):
            self.assertIn(attendu, echecs)
        manettes = res[2]
        self.assertFalse(manettes.tenu, "l'aller-retour lent des manettes n'est pas vu")

    def test_r2_mauvais(self):
        (res,) = self.resultats("r2-mauvais").values()
        echecs = " | ".join(self.echecs(res))
        for attendu in ("tranches perdues", "émettre une tranche", "images perdues"):
            self.assertIn(attendu, echecs)

    def test_r3_mauvais(self):
        (res,) = self.resultats("r3-mauvais").values()
        echecs = self.echecs(res)
        self.assertEqual(len(echecs), 1)
        self.assertIn("son", echecs[0])


class Calculs(unittest.TestCase):
    def test_cumul_apres_remise_a_zero(self):
        serie = [(0, {"n": 0}), (1, {"n": 10}), (2, {"n": 25}), (3, {"n": 4}), (4, {"n": 9})]
        self.assertEqual(banc.cumul(serie, "n"), 25 + 9)

    def test_centile(self):
        h = [0] * 25
        h[3] = 90   # ≤ 2 ms
        h[9] = 10   # ≤ 10 ms
        self.assertEqual(banc.centile(h, 0.5), 2.0)
        self.assertEqual(banc.centile(h, 0.99), 10.0)
        self.assertIsNone(banc.centile([0] * 25, 0.5))

    def test_lecture_d_une_ligne(self):
        d = banc.lire_ligne("bruit BNC role=bus_maitre t=5 cycles=50 son=ok:49,ecr:1,lec:0 "
                            "rtt=3:2,24:1")
        self.assertEqual(d["son"], {"ok": 49, "ecr": 1, "lec": 0})
        self.assertEqual(d["rtt"][3], 2)
        self.assertEqual(d["rtt"][24], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
