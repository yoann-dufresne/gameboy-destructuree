"""
Carte de l'écran — la description électrique, source unique.

Tout le reste en découle : gen_schema.py écrit le schéma KiCad, gen_pcb.py le circuit imprimé.
Chaque composant porte sa référence, son symbole, son empreinte, sa valeur, ses références d'achat,
et la liste de ses broches avec l'équipotentielle (net) de chacune. Une broche à None est laissée
en l'air, marquée « non connectée » sur le schéma.

Brochages repris SANS CHANGEMENT du firmware (aucune ligne à modifier) :
  - tête   : firmware/tete/include/config.h  (liaisons GP0..GP11, VSYNC GP12, RDY GP13..GP15)
  - nœud   : firmware/noeud/include/config.h (HUB75 GP0..GP13, RDY GP14, G1 de rechange GP15,
             VSYNC GP16, liaison GP19..GP22, mesures GP17/GP18)
  - HUB75  : plan-firmware.md §2.3, broche 4 et 16 à la masse, E sur la broche 8.

Numérotation des dalles : celle du cadre v3, vue de dos, flèches vers le haut :
    7 8 9   rangée du haut   ← nœud 0, liaison L0
    4 5 6   rangée du milieu ← nœud 1, liaison L1
    1 2 3   rangée du bas    ← nœud 2, liaison L2
"""

PROJET = 'carte-ecran'
TITRE = "Carte de l'écran 3×3 — tête, 3 nœuds, distribution 5 V"
REVISION = '1.0'
DATE = '2026-10-29'

# Une dalle par fusible et par bornier ; le numéro est celui du cadre v3.
DALLES = [1, 2, 3, 4, 5, 6, 7, 8, 9]
RANGEES = {0: 'haut (7-8-9)', 1: 'milieu (4-5-6)', 2: 'bas (1-2-3)'}

# ------------------------------------------------------------------ empreintes et références
FP = dict(
    pico='Module:RaspberryPi_Pico_Common_THT',
    pico_noeud='carte-ecran:RaspberryPi_Pico_THT_noeud',
    tssop20='Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm',
    r0603='Resistor_SMD:R_0603_1608Metric',
    rarr='Resistor_SMD:R_Array_Convex_4x0603',
    c0603='Capacitor_SMD:C_0603_1608Metric',
    c0805='Capacitor_SMD:C_0805_2012Metric',
    cp_smd='Capacitor_SMD:CP_Elec_6.3x7.7',
    cp_tht='Capacitor_THT:CP_Radial_D10.0mm_P5.00mm',
    led0603='LED_SMD:LED_0603_1608Metric',
    sot23='Package_TO_SOT_SMD:SOT-23',
    sma='Diode_SMD:D_SMA',
    smb='Diode_SMD:D_SMB',
    fb0805='Inductor_SMD:L_0805_2012Metric',
    ptc1812='Fuse:Fuse_1812_4532Metric',
    blade='Fuse:Fuseholder_Blade_Mini_Keystone_3568',
    idc16='Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical',
    hdr1x3='Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
    hdr1x8='Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical',
    hdr2x12='Connector_PinHeader_2.54mm:PinHeader_2x12_P2.54mm_Vertical',
    mstba2='Connector_Phoenix_MSTB:PhoenixContact_MSTBA_2,5_2-G-5,08_1x02_P5.08mm_Horizontal',
    mkds4='TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-3-4-5.08_1x04_P5.08mm_Horizontal',
    sw6='Button_Switch_THT:SW_PUSH_6mm',
    trou='MountingHole:MountingHole_3.2mm_M3',
)

# Références d'achat. LCSC = code de la pièce chez JLCPCB/LCSC pour l'assemblage ; à revérifier
# au moment de la commande (le stock bouge). Les pièces traversantes sont soudées à la main.
# 29/10/2026 : LED verte et fusible réarmable en rupture chez JLCPCB (C72043 : 0 chez LCSC ;
# C139605 en manque) ; remplacés, même boîtier, par C7371905 (689 350 en stock chez LCSC)
# et C151170 (18 545).
ACHAT = {
    'R33':    dict(LCSC='C23140', MPN='0603WAF330JT5E'),
    'R1k':    dict(LCSC='C21190', MPN='0603WAF1001T5E'),
    'R1k5':   dict(LCSC='C22843', MPN='0603WAF1501T5E'),
    'R10k':   dict(LCSC='C25804', MPN='0603WAF1002T5E'),
    'RN33':   dict(LCSC='C25508', MPN='4D03WGJ0330T5E'),
    'C100n':  dict(LCSC='C14663', MPN='CC0603KRX7R9BB104'),
    'C10u':   dict(LCSC='C15850', MPN='CL21A106KAYNNNE'),
    'CP100u': dict(LCSC='C19184138', MPN='CK1V101M-CRE77 (100 µF 35 V, 6,3×7,7)'),
    'CP1000u': dict(LCSC='', MPN='1000 µF 16 V radial Ø10 × 20, pas de 5 mm (Nichicon UHE1C102MPD)'),
    'LEDV':   dict(LCSC='C7371905', MPN='XL-1608PGC-06 (XINGLIGHT, vert émeraude)'),
    'LEDR':   dict(LCSC='C2286',  MPN='KT-0603R'),
    'AHCT541': dict(LCSC='C50989', MPN='SN74AHCT541PWR'),
    '2N7002': dict(LCSC='C8545',  MPN='2N7002'),
    'SS14':   dict(LCSC='C2480',  MPN='SS14'),
    'TVS':    dict(LCSC='C83333', MPN='SMBJ5.0A (Littelfuse, unidirectionnelle)'),
    'FB':     dict(LCSC='C85840', MPN='BLM21PG221SN1D (220 Ω à 100 MHz, 2 A)'),
    'PTC':    dict(LCSC='C151170', MPN='1812L075/33DR (Littelfuse, 0,75 A maintien, 1,5 A déclenchement)'),
    'BLADE':  dict(LCSC='', MPN='Keystone 3568 (porte-fusible lame mini ATM, CI)'),
    'IDC16':  dict(LCSC='', MPN='Embase IDC 2×8 détrompée, 2,54 mm, droite (Würth 61201621621)'),
    'MSTBA2': dict(LCSC='', MPN='Phoenix MSTBA 2,5/2-G-5,08 (1757242) + fiche MSTB 2,5/2-ST-5,08 (1757019), ou 2EDG 5,08'),
    'MKDS4':  dict(LCSC='', MPN='Phoenix MKDS 3/4-5,08 (1712805), 24 A par pôle'),
    'HDR':    dict(LCSC='', MPN='Barrette mâle 2,54 mm'),
    'PICOSOCK': dict(LCSC='', MPN='2 × barrette femelle 1×20, 2,54 mm (support de Pico)'),
    'SW':     dict(LCSC='', MPN='Bouton 6 × 6 mm traversant, 5 mm de haut au plus : la fiche USB des Pico passe au-dessus'),
}


class Comp:
    def __init__(self, ref, lib_id, value, footprint, pins, bloc, achat=None, **champs):
        self.ref, self.lib_id, self.value, self.footprint = ref, lib_id, value, footprint
        self.pins = {str(k): v for k, v in pins.items()}
        self.bloc = bloc
        self.champs = dict(ACHAT.get(achat, {})) if achat else {}
        self.champs.update(champs)

    def __repr__(self):
        return f'<{self.ref} {self.lib_id} {self.value}>'


COMPOSANTS = []


def ajoute(*a, **k):
    c = Comp(*a, **k)
    assert all(x.ref != c.ref for x in COMPOSANTS), c.ref
    COMPOSANTS.append(c)
    return c


# ---------------------------------------------------------------------- broches d'un Pico
PICO_GND = ['3', '8', '13', '18', '23', '28', '33', '38']
PICO_GPIO = {  # GPIO -> numéro de broche
    0: 1, 1: 2, 2: 4, 3: 5, 4: 6, 5: 7, 6: 9, 7: 10, 8: 11, 9: 12, 10: 14, 11: 15, 12: 16, 13: 17,
    14: 19, 15: 20, 16: 21, 17: 22, 18: 24, 19: 25, 20: 26, 21: 27, 22: 29, 26: 31, 27: 32, 28: 34,
}


def pico(ref, valeur, gpio, vsys, v3, run, bloc, note='', fp=None):
    p = {str(PICO_GPIO[g]): None for g in PICO_GPIO}
    for g, net in gpio.items():
        p[str(PICO_GPIO[g])] = net
    for b in PICO_GND:
        p[b] = 'GND'
    p.update({'30': run, '35': None, '36': v3, '37': None, '39': vsys, '40': None})
    return ajoute(ref, 'MCU_Module:RaspberryPi_Pico', valeur, fp or FP['pico'], p, bloc, 'PICOSOCK',
                  Note=note or 'Monté sur 2 barrettes femelles 1×20 : le Pico reste amovible')


def r(ref, valeur, a, b, bloc, achat):
    return ajoute(ref, 'Device:R', valeur, FP['r0603'], {1: a, 2: b}, bloc, achat)


def c(ref, valeur, a, b, bloc, achat, fp=None):
    return ajoute(ref, 'Device:C', valeur, fp or (FP['c0805'] if 'u' in valeur else FP['c0603']),
                  {1: a, 2: b}, bloc, achat)


def rpack(ref, valeur, paires, bloc):
    """paires : 4 couples (côté 1..4, côté 8..5) ; None = résistance inutilisée."""
    p = {}
    for i, pr in enumerate(paires):
        a, b = pr if pr else (None, None)
        p[i + 1] = a
        p[8 - i] = b
    return ajoute(ref, 'Device:R_Pack04', valeur, FP['rarr'], p, bloc, 'RN33')


# =========================================================================== ALIMENTATION
B = 'alim'
ajoute('J1', 'Connector_Generic:Conn_01x04', 'Entrée 5 V 30 A', FP['mkds4'],
       {1: '+5V', 2: '+5V', 3: 'GND', 4: 'GND'}, B, 'MKDS4',
       Note="2 fils de 2,5 mm² par polarité, un par borne de l'alim, avec embouts")
ajoute('C1', 'Device:C_Polarized', '1000µ 16V', FP['cp_tht'], {1: '+5V', 2: 'GND'}, B, 'CP1000u')
ajoute('C2', 'Device:C_Polarized', '1000µ 16V', FP['cp_tht'], {1: '+5V', 2: 'GND'}, B, 'CP1000u')
# TVS unidirectionnelle, cathode au +5 V : si J1 est câblé à l'envers, elle court-circuite
# l'alimentation (qui se met en protection) au lieu de laisser −5 V aux dalles et aux tampons.
ajoute('D1', 'Device:D_Zener', 'SMBJ5.0A', FP['smb'], {1: '+5V', 2: 'GND'}, B, 'TVS',
       Note='TVS unidirectionnelle : cathode (bande) au +5 V')
r('R1', '1k5', '+5V', 'LED5V_A', B, 'R1k5')
ajoute('LED1', 'Device:LED', 'vert', FP['led0603'], {1: 'GND', 2: 'LED5V_A'}, B, 'LEDV', Note='présence du 5 V')
# Mesure de la tension du bus par la tête (GP27 = ADC1) : 5 V -> 2,5 V.
r('R2', '10k', '+5V', 'T_V5MES', B, 'R10k')
r('R3', '10k', 'T_V5MES', 'GND', B, 'R10k')
c('C3', '100n', 'T_V5MES', 'GND', B, 'C100n')

# 9 départs fusibles, un par dalle. Voyant rouge aux bornes du fusible : il s'allume quand le
# fusible a fondu et que la dalle est branchée (sa charge ferme le circuit).
B = 'departs'
for n in DALLES:
    ajoute(f'F{n}', 'Device:Fuse', '7,5 A lame mini', FP['blade'], {1: '+5V', 2: f'+5V_D{n}'}, B, 'BLADE',
           Note=f'Fusible ATM 7,5 A (marron), dalle {n}')
    r(f'R{10 + n}', '1k', '+5V', f'FUS{n}_A', B, 'R1k')
    ajoute(f'LED{10 + n}', 'Device:LED', 'rouge', FP['led0603'],
           {1: f'+5V_D{n}', 2: f'FUS{n}_A'}, B, 'LEDR', Note=f'voyant du fusible {n}')
    ajoute(f'J{10 + n}', 'Connector_Generic:Conn_01x02', f'Dalle {n}', FP['mstba2'],
           {1: f'+5V_D{n}', 2: 'GND'}, B, 'MSTBA2',
           Note=f'Câble de la dalle {n} : 2 rouges sur +, 2 noirs sur −, embouts doubles')

# Alimentation des Pico et des tampons, tirée du même bus : fusible réarmable, ferrite, puis une
# diode Schottky par Pico vers VSYS — elle empêche un Pico branché en USB d'alimenter le bus, et
# le bus d'alimenter l'hôte USB (le Pico a sa propre diode entre VBUS et VSYS).
B = 'logique'
ajoute('F10', 'Device:Polyfuse', '0,75 A', FP['ptc1812'], {1: '+5V', 2: '+5V_PF'}, B, 'PTC')
ajoute('FB1', 'Device:FerriteBead', '220 Ω 2 A', FP['fb0805'], {1: '+5V_PF', 2: '+5V_LOG'}, B, 'FB')
ajoute('C4', 'Device:C_Polarized', '100µ 35V', FP['cp_smd'], {1: '+5V_LOG', 2: 'GND'}, B, 'CP100u')
c('C5', '10u', '+5V_LOG', 'GND', B, 'C10u')
for i, (cible, nom) in enumerate([('VSYS_T', 'tête'), ('VSYS_N0', 'nœud 0'),
                                  ('VSYS_N1', 'nœud 1'), ('VSYS_N2', 'nœud 2')]):
    ajoute(f'D{2 + i}', 'Diode:SS14', 'SS14', FP['sma'], {1: cible, 2: '+5V_LOG'}, B, 'SS14',
           Note=f'VSYS du Pico {nom}')
    c(f'C{6 + i}', '10u', cible, 'GND', B, 'C10u')

# Drapeaux d'alimentation (ERC) : ces équipotentielles sont bien alimentées, par l'alim ou les diodes.
for i, net in enumerate(['+5V', 'GND', '+5V_LOG', 'VSYS_T', 'VSYS_N0', 'VSYS_N1', 'VSYS_N2']):
    ajoute(f'PF{1 + i}', 'power:PWR_FLAG', 'PWR_FLAG', '', {1: net}, B)

# =========================================================================== TÊTE
B = 'tete'
gp_tete = {}
for k in range(3):
    for j, sig in enumerate(['D0', 'D1', 'CLK', 'CS']):
        gp_tete[4 * k + j] = f'T_L{k}_{sig}'
gp_tete.update({12: 'VSYNC_T', 13: 'RDY_0', 14: 'RDY_1', 15: 'RDY_2', 16: 'T_GP16', 17: 'T_GP17',
                18: 'T_GP18', 19: 'T_GP19', 20: 'T_GP20', 21: 'T_GP21', 22: 'T_GP22', 26: 'T_POT',
                27: 'T_V5MES', 28: 'T_GP28'})
pico('U1', 'Pico 2 W — tête', gp_tete, 'VSYS_T', '3V3_T', 'RUN_T', B,
     note="Tête : antenne vers le bord de la carte, zone sans cuivre dessous")
# Résistances série au départ de chaque liaison (plan §2.6) : 33 Ω.
# Ordre CS, CLK, D1, D0 dans le réseau : celui des broches de la tête vues depuis le réseau tourné.
for k in range(3):
    rpack(f'RN{1 + k}', '4×33', [(f'T_L{k}_{s}', f'L{k}_{s}') for s in ['CS', 'CLK', 'D1', 'D0']], B)
for k in range(3):
    r(f'R{30 + k}', '33', 'VSYNC_T', f'VSYNC_{k}', B, 'R33')
    r(f'R{33 + k}', '10k', f'RDY_{k}', '3V3_T', B, 'R10k')
ajoute('SW1', 'Switch:SW_Push', 'RESET tête', FP['sw6'], {1: 'RUN_T', 2: 'GND'}, B, 'SW')
ajoute('J2', 'Connector_Generic:Conn_01x03', 'Potentiomètre (option)', FP['hdr1x3'],
       {1: '3V3_T', 2: 'T_POT', 3: 'GND'}, B, 'HDR', Note='Potentiomètre de luminosité 10 kΩ, GP26 (plan §2.6)')
ajoute('J3', 'Connector_Generic:Conn_01x08', 'Extension tête', FP['hdr1x8'],
       {1: '3V3_T', 2: 'T_GP16', 3: 'T_GP19', 4: 'T_GP20', 5: 'T_GP21', 6: 'T_GP22', 7: 'T_GP28', 8: 'GND'},
       B, 'HDR', Note='Broches libres de la tête (I2C0 possible sur GP20/GP21)')

# =========================================================================== NŒUDS
HUB_A = ['R1', 'G1', 'B1', 'R2', 'G2', 'B2', 'A', 'B']   # tampon A : A0..A7
HUB_B = ['C', 'D', 'E', 'CLK', 'LAT', 'OE']               # tampon B : A0..A5
HUB_GPIO = {'R1': 0, 'G1': 1, 'B1': 2, 'R2': 3, 'G2': 4, 'B2': 5, 'A': 6, 'B': 7, 'C': 8, 'D': 9,
            'E': 10, 'CLK': 11, 'LAT': 12, 'OE': 13}
HUB_IDC = {'R1': 1, 'G1': 2, 'B1': 3, 'R2': 5, 'G2': 6, 'B2': 7, 'E': 8, 'A': 9, 'B': 10,
           'C': 11, 'D': 12, 'CLK': 13, 'LAT': 14, 'OE': 15}

for k in range(3):
    B = f'noeud{k}'
    N = f'N{k}'
    gp = {HUB_GPIO[s]: f'{N}_{s}' for s in HUB_GPIO}
    gp[1] = f'{N}_GP1'                     # G1 passe par le cavalier
    gp.update({14: f'{N}_RDY', 15: f'{N}_GP15', 16: f'VSYNC_{k}', 17: f'{N}_GP17', 18: f'{N}_GP18',
               19: f'L{k}_D0', 20: f'L{k}_D1', 21: f'L{k}_CLK', 22: f'L{k}_CS'})
    pico(f'U{2 + k}', f'Pico 2 — nœud {k}', gp, f'VSYS_N{k}', f'3V3_N{k}', f'RUN_N{k}', B,
         note=f'Nœud {k}, rangée du {RANGEES[k]}', fp=FP['pico_noeud'])
    # Cavalier de G1 : GP1 (normal) ou GP15 (Pico dont la sortie GP1 est morte, 29/10/2026).
    ajoute(f'JP{1 + k}', 'Jumper:Jumper_3_Bridged12', 'G1 : GP1 | GP15', FP['hdr1x3'],
           {1: f'{N}_GP1', 2: f'{N}_G1', 3: f'{N}_GP15'}, B, 'HDR',
           Note='Cavalier sur 1-2 (GP1) ; 2-3 pour un Pico dont GP1 est mort')
    # Deux tampons 74AHCT541, alimentés en 5 V : niveaux 5 V vers la dalle, Pico protégé. Le 541 est
    # unidirectionnel, ses entrées n'ont pas de diode vers VCC : un nœud alimenté par USB, bus éteint,
    # ne réalimente pas la carte par ses broches, ce que ferait le port A d'un 245 (revue du
    # 29/10/2026). /OE1 à la masse, /OE2 commandé par le transistor de validation.
    ua, ub = f'U{5 + 2 * k}', f'U{6 + 2 * k}'
    pa = {1: 'GND', 19: f'{N}_OEB', 20: '+5V_LOG', 10: 'GND'}
    for i, s in enumerate(HUB_A):
        pa[2 + i] = f'{N}_{s}'
        pa[18 - i] = f'{N}_{s}_T'
    ajoute(ua, 'carte-ecran:74AHCT541', '74AHCT541', FP['tssop20'], pa, B, 'AHCT541')
    pb = {1: 'GND', 19: f'{N}_OEB', 20: '+5V_LOG', 10: 'GND', 8: 'GND', 9: 'GND', 12: None, 11: None}
    for i, s in enumerate(HUB_B):
        pb[2 + i] = f'{N}_{s}'
        pb[18 - i] = f'{N}_{s}_T'
    ajoute(ub, 'carte-ecran:74AHCT541', '74AHCT541', FP['tssop20'], pb, B, 'AHCT541')
    c(f'C{20 + 2 * k}', '100n', '+5V_LOG', 'GND', B, 'C100n')
    c(f'C{21 + 2 * k}', '100n', '+5V_LOG', 'GND', B, 'C100n')
    # Résistances série vers la nappe (33 Ω) : 14 signaux, 4 réseaux de 4.
    sig = HUB_A + HUB_B
    for g in range(4):
        grp = sig[4 * g:4 * g + 4]
        paires = [(f'{N}_{s}_T', f'{N}_{s}_H') for s in grp] + [None] * (4 - len(grp))
        rpack(f'RN{4 + 4 * k + g}', '4×33', paires, B)
    # Embase HUB75 vers l'entrée de la première dalle de la rangée.
    pins = {HUB_IDC[s]: f'{N}_{s}_H' for s in HUB_IDC}
    pins.update({4: 'GND', 16: 'GND'})
    ajoute(f'J{4 + k}', 'Connector_Generic:Conn_02x08_Odd_Even', f'HUB75 rangée {RANGEES[k]}', FP['idc16'],
           pins, B, 'IDC16', Note=f'Nappe vers l\'entrée de la dalle {[7, 4, 1][k]}')
    # Validation des tampons : seulement si le Pico est là et alimenté (son 3V3 commande le
    # transistor). Sans Pico, sorties en haute impédance et /OE de la dalle tiré haut : dalle noire.
    ajoute(f'Q{1 + k}', 'Transistor_FET:2N7002', '2N7002', FP['sot23'],
           {1: f'3V3_N{k}', 2: 'GND', 3: f'{N}_OEB'}, B, '2N7002')
    base = 40 + 10 * k
    r(f'R{base + 0}', '10k', f'{N}_OEB', '+5V_LOG', B, 'R10k')
    # 1 kΩ et non 100 kΩ : Pico absent, R44 et R45 ramènent du 3,3 V de la tête sur ce rail ; il
    # reste sous 0,55 V, Q bloqué, la rangée noire (revue du 29/10/2026). 3,3 mA Pico en place.
    r(f'R{base + 1}', '1k', f'3V3_N{k}', 'GND', B, 'R1k')
    r(f'R{base + 2}', '10k', f'{N}_OE_H', '+5V_LOG', B, 'R10k')      # dalle éteinte sans pilote
    r(f'R{base + 3}', '10k', f'{N}_OE', f'3V3_N{k}', B, 'R10k')      # /OE haut pendant BOOTSEL
    r(f'R{base + 4}', '10k', f'VSYNC_{k}', f'3V3_N{k}', B, 'R10k')   # VSYNC au repos sans tête
    r(f'R{base + 5}', '10k', f'L{k}_CS', f'3V3_N{k}', B, 'R10k')     # CS au repos sans tête
    r(f'R{base + 6}', '33', f'{N}_RDY', f'RDY_{k}', B, 'R33')        # départ de RDY
    ajoute(f'SW{2 + k}', 'Switch:SW_Push', f'RESET nœud {k}', FP['sw6'], {1: f'RUN_N{k}', 2: 'GND'}, B, 'SW')

# =========================================================================== MESURE
B = 'mesure'
sonde = ['VSYNC_T', 'RDY_0', 'RDY_1', 'RDY_2', 'T_GP17', 'T_GP18',
         'N0_GP17', 'N0_GP18', 'N1_GP17', 'N1_GP18', 'N2_GP17', 'N2_GP18']
p = {}
for i, s in enumerate(sonde):
    p[2 * i + 1] = s
    p[2 * i + 2] = 'GND'
ajoute('J7', 'Connector_Generic:Conn_02x12_Odd_Even', 'Sonde analyseur logique', FP['hdr2x12'], p, B, 'HDR',
       Note='Rangée impaire : signaux ; rangée paire : masse')

# Trous de fixation M3 (sans pastille : la carte repose sur un plateau imprimé).
for i in range(6):
    ajoute(f'H{1 + i}', 'Mechanical:MountingHole', 'M3', FP['trou'], {}, 'meca')


# =========================================================================== contrôles
def nets():
    d = {}
    for cp in COMPOSANTS:
        for pin, net in cp.pins.items():
            if net:
                d.setdefault(net, []).append((cp.ref, pin))
    return d


if __name__ == '__main__':
    d = nets()
    seuls = {n: v for n, v in d.items() if len(v) < 2}
    print(f'{len(COMPOSANTS)} composants, {len(d)} équipotentielles')
    for n, v in sorted(seuls.items()):
        print('  équipotentielle à une seule broche :', n, v)
