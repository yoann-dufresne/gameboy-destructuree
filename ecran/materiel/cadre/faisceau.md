# Faisceau d'alimentation — fiche de coupe

Généré par `gen_cadre.py` à partir du cheminement calculé (tracés sur `../docs/cadre_plan.png`). Longueur de coupe = tracé + remontée à la fiche (50 mm) + descente sur l'embase de la dalle et mou (80 mm) + 40 mm par profilé enjambé, + 10 %, arrondie aux 50 mm supérieurs. Aucun câble ne passe sur un connecteur de dalle ni sur sa marge d'accès de 8 mm.

## Les 9 câbles de dalle

Constitution : **4 conducteurs souples de 0,75 mm² (18 AWG)**, 2 rouges et 2 noirs. Côté dalle, boîtier JST **VHR-4N** et 4 contacts **SVH-21T-P1.1** (embase VH 3.96 de la dalle, sérigraphiée `+ + - -`) — ou le câble d'origine de la dalle s'il est assez long, cosses coupées. Côté carte, fiche **MSTB 2,5/2-ST-5,08** : les 2 rouges dans un **embout double 2 × 0,75 mm²** sur la borne 1 (+), les 2 noirs dans un autre sur la borne 2 (−). Repérer chaque câble à ses deux bouts (gaine thermo ou bague numérotée).

| Câble | Départ sur la carte | Arrivée | Tracé | Profilés enjambés | **Coupe** | Chute de tension à 4 A |
|---|---|---|---|---|---|---|
| D1 | J11, fusible F1 | dalle 1 | 266 mm | 0 | **450 mm** | 41 mV |
| D2 | J12, fusible F2 | dalle 2 | 62 mm | 0 | **250 mm** | 23 mV |
| D3 | J13, fusible F3 | dalle 3 | 234 mm | 0 | **450 mm** | 41 mV |
| D4 | J14, fusible F4 | dalle 4 | 266 mm | 0 | **450 mm** | 41 mV |
| D5 | J15, fusible F5 | dalle 5 | 86 mm | 0 | **250 mm** | 23 mV |
| D6 | J16, fusible F6 | dalle 6 | 282 mm | 0 | **500 mm** | 46 mV |
| D7 | J17, fusible F7 | dalle 7 | 442 mm | 1 | **700 mm** | 64 mV |
| D8 | J18, fusible F8 | dalle 8 | 262 mm | 1 | **500 mm** | 46 mV |
| D9 | J19, fusible F9 | dalle 9 | 438 mm | 1 | **700 mm** | 64 mV |

Total : 4,25 m de câble 4 conducteurs, soit 17,0 m de fil 0,75 mm² (8,5 m de rouge et autant de noir) ; compter 10 % de plus pour les essais de sertissage.

Chute de tension : aller et retour, 2 × 0,75 mm² par pôle. S'y ajoutent ~40 mV dans le fusible et ~30 mV dans le bus de la carte : la dalle la plus éloignée voit au pire ~0,15 V de moins que l'alimentation. Régler celle-ci à **5,15 V** à vide (≤ 5,25 V).

## Les 3 nappes HUB75 de la carte

Nappe IDC 16 points, de l'embase de la carte à l'**entrée** de la première dalle de la rangée ; les 6 nappes entre dalles, sortie → entrée voisine, restent celles fournies avec les dalles. Longueur utile minimale (tracé + 60 mm de remontée de l'embase + 40 mm de descente, + 10 %) :

| Nappe | De | Vers | Tracé | Longueur minimale |
|---|---|---|---|---|
| J4 | carte, nœud 0 | entrée de la dalle 7 | 280 mm | **500 mm** |
| J5 | carte, nœud 1 | entrée de la dalle 4 | 124 mm | **250 mm** |
| J6 | carte, nœud 2 | entrée de la dalle 1 | 112 mm | **250 mm** |

Une nappe fournie plus courte se remplace par une nappe IDC 16 points du commerce de la longueur standard au-dessus (30 ou 50 cm). Détrompeur vers le repère ▷ de l'embase de la carte, broche 1 = R1.

## Câble 5 V : alimentation → carte (J1)

**4 conducteurs souples de 2,5 mm² (H07V-K)**, 2 rouges et 2 noirs, coupés à **250 mm** (tracé 123 mm). Chute de tension à 36 A : 62 mV.

- Côté alimentation : **une cosse fourche isolée par fil** (bleue, 1,5–2,5 mm², M3,5 ou M4 selon les vis du bornier), chaque fil sur **sa propre borne** : 2 rouges sur deux bornes +V, 2 noirs sur deux bornes −V. Plus aucune cosse empilée.
- Côté carte : **embouts 2,5 mm²** dans le bornier J1 (MKDS 3/4-5,08) : bornes 1 et 2 = +5 V, 3 et 4 = masse (sérigraphie de la carte).
- Torsader légèrement rouge et noir ensemble ; un collier sur l'ancre du profilé du bas.

## Secteur : embase IEC → alimentation

Embase **IEC C14 3-en-1** (fiche, interrupteur, porte-fusible 5 × 20) avec **fusible T 3,15 A** (temporisé). Conducteurs **H05V-K 1 mm²** : marron (L), bleu (N), vert-jaune (PE), coupés à **400 mm** (tracé 275 mm), passés ensemble dans une gaine tressée ou une gaine PVC (double isolation le long du cadre métallique), colliers tous les 10 cm.

- Côté embase : **cosses femelles 6,3 mm entièrement isolées** (L et N sur les cosses de l'interrupteur, côté charge, pas sur la fiche). Sur la languette de terre, une cosse **« piggyback »** pour en repartir vers le cadre.
- Côté alimentation : cosses fourche isolées (rouges, 0,5–1,5 mm²) sur L, N et ⏚.
- **Liaison du cadre à la terre** : du piggyback de l'embase, un fil vert-jaune 1,5 mm² terminé par une cosse à œillet M5, vissée **seule** dans la rainure arrière du profilé du bas (**vis M5 × 6**, écrou marteau, **rondelle à dents** DIN 6798 A sous la cosse pour percer l'anodisation). Pas plus longue : sous la tête, cosse et rondelle ne font que 1,4 mm, et une M5 × 8 ou × 10 buterait au fond de la rainure avant de serrer la cosse.
- Contrôle à l'ohmmètre, prise débranchée : **< 0,5 Ω** entre la broche de terre de l'embase et chacun des 5 profilés. Si un profilé n'y est pas, rondelle à dents sous une vis de ses équerres.
- Le secteur reste à **25 mm au moins** du câble 5 V sur tout son tracé (bornes L, N, ⏚ en bas du bornier, sorties continues en haut). Bornier de l'alimentation **sous son capot** ; une alimentation sans capot n'a pas sa place au dos d'un écran qu'on touche.

## À acheter pour le faisceau

| Pièce | Qté |
|---|---|
| Fil souple 0,75 mm² rouge / noir | 9,3 m de chaque |
| Boîtier JST VHR-4N + contacts SVH-21T-P1.1 (si câbles neufs) | 9 + 36 (+ 10 % de contacts) |
| Fiche Phoenix MSTB 2,5/2-ST-5,08 (1757019) | 9 (déjà comptées avec la carte) |
| Embout double 2 × 0,75 mm² (gris, ex. Phoenix AI-TWIN 2 × 0,75-10 GY 3200807) | 18 (+ 5) |
| Fil H07V-K 2,5 mm² rouge / noir | 0,6 m de chaque |
| Cosse fourche isolée bleue 1,5–2,5 mm² M3,5/M4 | 4 (+ 2) |
| Embout 2,5 mm² | 4 (+ 2) |
| Fil H05V-K 1 mm² marron, bleu, vert-jaune | 0,5 m de chaque |
| Fil H07V-K 1,5 mm² vert-jaune (liaison du cadre) | 0,3 m |
| Cosse femelle 6,3 mm entièrement isolée + 1 piggyback | 3 + 1 |
| Cosse fourche isolée rouge 0,5–1,5 mm² M3,5 | 3 (+ 2) |
| Cosse à œillet M5 1,5 mm² + rondelle à dents M5 DIN 6798 A | 1 + 1 |
| Embase IEC C14 3-en-1 avec interrupteur et fusible (découpe 47,5 × 27,5) | 1 |
| Fusible 5 × 20 T 3,15 A | 2 (1 de rechange) |
| Gaine tressée Ø 6–10 mm, gaine thermo, colliers 2,5 × 100 | ~1 m, ~1 m, 50 |
| Pince à sertir embouts (carrée) et pince pour contacts VH (0,5–1 mm²) | outillage |
