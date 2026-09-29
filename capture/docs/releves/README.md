# Relevés

Les captures de l'analyseur logique et les images produites par le firmware. Elles
sont versionnées parce qu'elles prouvent les décisions du module, et qu'elles
permettent de retravailler sans ouvrir la console
([`../etapes-detaillees.md`](../etapes-detaillees.md) §D.11).

Les `.sr` s'ouvrent dans PulseView, ou se dépouillent avec
[`../../tools/analyse_sr.py`](../../tools/analyse_sr.py). Un `.sr` est une archive zip :
`unzip -l cp.sr` montre ses métadonnées et ses blocs d'échantillons.

## Photos de la carte

| Fichier | Contenu |
|---|---|
| `carte-mgb.jpg` | la carte mère `MGB-ECPU-01`, vue large |
| `carte-mgb-dos.jpg` | la même, côté dos, avec ses points de test sérigraphiés |
| `ruban-lcd.jpg` | gros plan du connecteur `P2` du ruban de l'écran |

## Identification des signaux

Un point de test sondé à la fois, à 4 MS/s pour les cadences et à 24 MS/s (`-rapide`)
pour les détails fins. Analyse : [`../signaux-mgb.md`](../signaux-mgb.md) §3bis.

| Fichier | Taux | Signal |
|---|---|---|
| `cp.sr`, `cp-rapide.sr` | 4 et 24 MS/s | `CP`, l'horloge pixel |
| `p2-st.sr` | 4 MS/s | `P2-ST`, le verrou de ligne |
| `p2-s.sr` | 4 MS/s | `P2-S`, le départ d'image |
| `p2-cpl.sr` | 4 MS/s | `P2-CPL`, l'horloge de ligne |
| `p2-fr.sr` | 4 MS/s | `P2-FR`, l'inversion de ligne |
| `cpg.sr`, `cpg-rapide.sr` | 4 et 24 MS/s | `CPG`, fonction indéterminée, non utilisé |
| `donnees.sr` | 24 MS/s | `LD0`, `LD1` et `CP` ensemble : à quel instant échantillonner |
| `blanc.sr`, `noir.sr` | 24 MS/s | écran tout blanc, puis tout noir : confirme les lignes de données et la valeur de pixel la plus claire |

## Vérification après soudure

Les 6 signaux relevés au bout des fils soudés. Analyse :
[`../recette-cablage.md`](../recette-cablage.md).

| Fichier | Taux |
|---|---|
| `phase1-lent.sr` | 4 MS/s, 500 ms |
| `phase1-rapide.sr` | 24 MS/s, 67 ms |

## Images obtenues

Images de 640×576 pixels, soit 160×144 agrandi 4 fois. Contexte de chacune :
[journal du firmware](../../firmware/sniffer/JOURNAL.md).

| Fichier | Contenu |
|---|---|
| `phase2-premiere-trame.png` | la première image capturée, en gris |
| `phase2-premiere-trame-dmg.png` | la même, avec les 4 verts de la Game Boy d'origine |
| `phase2-apres-cycle.png` | après extinction et rallumage de la console |
| `phase2-apres-30min.png` | après 30 minutes de capture continue |
| `phase3-bout-en-bout.png` | reçue en WiFi par l'écran virtuel |
| `phase4-pipeline.png` | reçue en émission pipelinée |
