"""
Carte de la manette : la description électrique, source unique.

Tout le reste en découle : gen_schema.py écrit le schéma KiCad, gen_pcb.py le circuit imprimé.
Chaque composant porte sa référence, son symbole, son empreinte, sa valeur, ses références d'achat,
et la liste de ses broches avec l'équipotentielle (net) de chacune. Une broche à None est laissée
en l'air, marquée "non connectée" sur le schéma.

Le pourquoi de chaque choix est dans ../../docs/carte.md (§4) ; le brochage du Pico, au §5, fait foi
pour le firmware.

Méthode et outillage repris de la carte de l'écran (ecran/materiel/carte).
"""

PROJET = 'carte-manette'
TITRE = 'Carte de la manette : Game Boy Pocket, Pico 2 W, neuf LED'
REVISION = '0.1'
DATE = '2026-11-15'

# ------------------------------------------------------------------ empreintes
FP = dict(
    pico='Module:RaspberryPi_Pico_W_SMD_HandSolder',
    lga12='Package_LGA:LGA-12_2x2mm_P0.5mm',
    tssop14='Package_SO:TSSOP-14_4.4x5mm_P0.65mm',
    wson6='Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm',
    sot23='Package_TO_SOT_SMD:SOT-23',
    r0603='Resistor_SMD:R_0603_1608Metric',
    c0603='Capacitor_SMD:C_0603_1608Metric',
    c0805='Capacitor_SMD:C_0805_2012Metric',
    c1206='Capacitor_SMD:C_1206_3216Metric',
    l2520='Inductor_SMD:L_1008_2520Metric',
    led2020='LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm',
    jp2='Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm',
    jp3='Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm',
    tp='TestPoint:TestPoint_Pad_D1.0mm',
    # reprises de YAPoco (Natalie the Nerd, CC BY-SA 4.0), par extrait_yapoco.py
    croix='carte-manette:YAPoco_Croix',
    ab='carte-manette:YAPoco_AB',
    select='carte-manette:YAPoco_Select',
    start='carte-manette:YAPoco_Start',
    piles='carte-manette:YAPoco_Piles',
    interrupteur='carte-manette:YAPoco_Interrupteur',
    molette='carte-manette:YAPoco_Molette',
)

# Références d'achat. LCSC = code de la pièce chez JLCPCB/LCSC pour l'assemblage. Les actifs ont été
# relevés le 15/11/2026 (carte.md §4.9) ; les passifs sans code se choisissent au devis (C5), dans la
# bibliothèque "Basic" de JLCPCB quand elle a la valeur. Tout est à revérifier au moment de commander.
ACHAT = {
    'PICO':     dict(LCSC='', MPN='Raspberry Pi Pico 2 W, soudé à plat à la main (castellations)'),
    'LIS2DH12': dict(LCSC='C110926', MPN='LIS2DH12TR (ST)'),
    'TXU0304':  dict(LCSC='C4364118', MPN='TXU0304PWR (TI)'),
    'TPS61099': dict(LCSC='C2842395', MPN='TPS61099DRVR (TI)'),
    'DMG2305':  dict(LCSC='C150470', MPN='DMG2305UX-7 (Diodes)'),
    'PMV16':    dict(LCSC='C110920', MPN='PMV16XNR (Nexperia)'),
    'WS2812':   dict(LCSC='C55109522', MPN='WS2812C-2020-V6 (Worldsemi)'),
    'L2u2':     dict(LCSC='', MPN='2,2 uH, 2520, saturation 1,5 A au moins (cf. TPS61099 tableau 8-2)'),
    'R100':     dict(LCSC='', MPN='100 ohm 0603'),
    'R10k':     dict(LCSC='C25804', MPN='0603WAF1002T5E'),
    'R47k':     dict(LCSC='', MPN='47 kohm 0603'),
    'R100k':    dict(LCSC='', MPN='100 kohm 0603'),
    'R287k':    dict(LCSC='', MPN='287 kohm 1 % 0603'),
    'R330k':    dict(LCSC='', MPN='330 kohm 0603'),
    'R1M':      dict(LCSC='', MPN='1 Mohm 1 % 0603'),
    'C1n':      dict(LCSC='', MPN='1 nF 0603 X7R'),
    'C100n':    dict(LCSC='C14663', MPN='CC0603KRX7R9BB104'),
    'C10u':     dict(LCSC='', MPN='10 uF 10 V 0603 X5R'),
    'C47u':     dict(LCSC='', MPN='47 uF 6,3 V 1206 X5R'),
    'SM_INTER': dict(LCSC='', MPN='Interrupteur de rechange Game Boy Pocket (Silent Modding GBP-PWR-SWITCH)'),
    'SM_POT':   dict(LCSC='', MPN='Potentiomètre 10 kohm GBC/GBP/GBA (Silent Modding UNI-VOL-POTI)'),
    'SM_PILES': dict(LCSC='', MPN='Contacts de piles de rechange Game Boy Pocket (Silent Modding)'),
    'MEMBRANE': dict(LCSC='', MPN='Membranes RetroSix transparentes et boutons RetroSix Prestige transparents'),
}


class Comp:
    def __init__(self, ref, lib_id, value, footprint, pins, bloc, achat=None, dnp=False, **champs):
        self.ref, self.lib_id, self.value, self.footprint = ref, lib_id, value, footprint
        self.pins = {str(k): v for k, v in pins.items()}
        self.bloc = bloc
        self.dnp = dnp
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


def r(ref, valeur, a, b, bloc, achat, **k):
    return ajoute(ref, 'Device:R', valeur, FP['r0603'], {1: a, 2: b}, bloc, achat, **k)


def c(ref, valeur, a, b, bloc, achat, fp=None, **k):
    return ajoute(ref, 'Device:C', valeur, fp or FP['c0603'], {1: a, 2: b}, bloc, achat, **k)


def pmos(ref, g, s, d, bloc, note):
    return ajoute(ref, 'Transistor_FET:Q_PMOS_GSD', 'DMG2305UX', FP['sot23'],
                  {1: g, 2: s, 3: d}, bloc, 'DMG2305', Note=note)


def nmos(ref, g, s, d, bloc, note):
    return ajoute(ref, 'Transistor_FET:Q_NMOS_GSD', 'PMV16XN', FP['sot23'],
                  {1: g, 2: s, 3: d}, bloc, 'PMV16', Note=note)


# ---------------------------------------------------------------------- broches du Pico 2 W
PICO_GND = ['3', '8', '13', '18', '23', '28', '38', '33', 'D2']
PICO_GPIO = {  # GPIO -> numéro de broche
    0: 1, 1: 2, 2: 4, 3: 5, 4: 6, 5: 7, 6: 9, 7: 10, 8: 11, 9: 12, 10: 14, 11: 15, 12: 16, 13: 17,
    14: 19, 15: 20, 16: 21, 17: 22, 18: 24, 19: 25, 20: 26, 21: 27, 22: 29, 26: 31, 27: 32, 28: 34,
}

# Le brochage de carte.md §5 : il fait foi pour le firmware.
GPIO = {
    0: 'UART_TX', 1: 'UART_RX',
    2: 'BTN_DROITE', 3: 'BTN_GAUCHE', 4: 'BTN_HAUT', 5: 'BTN_BAS',
    6: 'BTN_A', 7: 'BTN_B', 8: 'BTN_SELECT', 9: 'BTN_START',
    10: 'MARCHE_LU', 11: 'MAINT_PICO', 12: 'ACC_INT_LU', 13: None, 14: None,
    15: 'GEN_APPUIS',
    16: 'SPI_MISO', 17: 'SPI_CS', 18: 'SPI_SCK', 19: 'SPI_MOSI',
    20: 'LED_EN', 21: 'POT_ALIM', 22: 'LED_DATA',
    26: 'POT_VOL', 27: 'POT_LUM', 28: None,
}

# =========================================================================== ALIMENTATION
B = 'alim'
ajoute('BT1', 'Device:Battery', '2 x AAA', FP['piles'], {1: 'VPILES_BRUT', 2: 'GND'}, B, 'SM_PILES',
       Note='Contacts de piles de Game Boy Pocket, dans les fentes de la carte')
# Protection contre l'inversion des piles : P-MOSFET en diode idéale, grille à la masse.
pmos('Q1', 'GND', 'VPILES', 'VPILES_BRUT', B, 'Inversion des piles : diode idéale')
c('C1', '10u', 'VPILES', 'GND', B, 'C10u')
# Le maintien : P-MOSFET fermé tant qu'un des trois N-MOSFET tire sa grille à la masse.
pmos('Q2', 'MAINT_G', 'VPILES', 'VMAINT', B, 'Maintien d\'alimentation')
r('R1', '100k', 'MAINT_G', 'VPILES', B, 'R100k')
nmos('Q3', 'MARCHE', 'GND', 'MAINT_G', B, 'Maintien : interrupteur sur marche')
nmos('Q4', 'ACC_INT', 'GND', 'MAINT_G', B, 'Maintien : mouvement')
nmos('Q5', 'MAINT_PICO', 'GND', 'MAINT_G', B, 'Maintien : le Pico (GP11)')
r('R2', '1M', 'MARCHE', 'GND', B, 'R1M')
r('R3', '1M', 'ACC_INT', 'GND', B, 'R1M')
r('R4', '100k', 'MAINT_PICO', 'GND', B, 'R100k')
r('R5', '47k', 'MARCHE', 'MARCHE_LU', B, 'R47k')
r('R6', '47k', 'ACC_INT', 'ACC_INT_LU', B, 'R47k')
ajoute('JP1', 'Jumper:SolderJumper_2_Open', 'maintien forcé', FP['jp2'], {1: 'MAINT_G', 2: 'GND'}, B,
       Note='À fermer si le maintien échoue : la carte reste alimentée tant que les piles sont là')
# L'interrupteur envoie la tension des piles sur MARCHE (marche) ou la masse (arrêt) : aucune
# résistance de tirage vers les piles ne touche une broche du Pico endormi.
ajoute('SW1', 'Switch:SW_SPDT', 'Interrupteur', FP['interrupteur'],
       {1: 'VPILES', 2: 'MARCHE', 3: 'GND'}, B, 'SM_INTER',
       Note='Broche 1 : position marche ; 3 : arrêt ; 2 : commun. À vérifier sur la pièce (C0.5)')
# L'USB ne recharge jamais les piles : P-MOSFET de la datasheet du Pico, grille sur VBUS.
pmos('Q6', 'VBUS', 'VSYS', 'VMAINT', B, 'USB : coupe les piles quand VBUS est là')
r('R7', '100k', 'VBUS', 'GND', B, 'R100k')
c('C2', '47u', 'VSYS', 'GND', B, 'C47u', fp=FP['c1206'])

# =========================================================================== PICO 2 W
B = 'pico'
p = {str(PICO_GPIO[g]): None for g in PICO_GPIO}
for g, net in GPIO.items():
    p[str(PICO_GPIO[g])] = net
for b in PICO_GND:
    p[b] = 'GND'
p.update({'30': 'RUN', '35': None, '36': '3V3', '37': None, '39': 'VSYS', '40': 'VBUS',
          'D1': None, 'D3': None})
# D1 (SWCLK) et D3 (SWDIO) : les pastilles de mise au point du Pico, traversantes sur la carte. Rien ne
# s'y branche : elles se soudent et se sondent par le dos (carte.md §4.8).
ajoute('U1', 'MCU_Module:RaspberryPi_Pico_W_Debug', 'Pico 2 W', FP['pico'], p, B, 'PICO',
       Note='Soudé à plat sous le cache noir, antenne au bord haut ; pastilles SWD traversantes')

# =========================================================================== ACCÉLÉROMÈTRE
B = 'acc'
ajoute('U2', 'Sensor_Motion:LIS2DE12', 'LIS2DH12TR', FP['lga12'],
       {1: 'ACC_SCK', 2: 'ACC_CS', 3: 'ACC_MISO', 4: 'ACC_MOSI', 5: 'GND', 6: 'GND', 7: 'GND', 8: 'GND',
        9: 'VPILES', 10: 'VPILES', 11: 'ACC_INT2', 12: 'ACC_INT1'}, B, 'LIS2DH12',
       Note='Alimenté par les piles, toujours ; son interruption tient le maintien')
# Laquelle des deux interruptions tient le maintien : INT1, interruption de réveil verrouillée jusqu'à
# ce que le Pico la lise (par défaut) ; ou INT2, qui porte la fonction "activité" du LIS2DH12, si elle
# tient un niveau pendant le mouvement (carte.md §4.2). Un cavalier, pour ne jamais relier deux sorties.
ajoute('JP3', 'Jumper:SolderJumper_3_Bridged12', 'INT1 | INT2', FP['jp3'],
       {1: 'ACC_INT1', 2: 'ACC_INT', 3: 'ACC_INT2'}, B,
       Note='1-2 fermé : INT1 (par défaut) ; couper, puis fermer 2-3 : INT2')
c('C3', '100n', 'VPILES', 'GND', B, 'C100n')
c('C4', '10u', 'VPILES', 'GND', B, 'C10u')
# CS reste haut pendant que le Pico dort : 330 kohm, sous le 1 Mohm que demandent les tirages internes
# de 5 Mohm de l'adaptateur.
r('R8', '330k', 'ACC_CS', 'VPILES', B, 'R330k')
# Adaptateur à sorties isolées : rien ne passe entre l'accéléromètre et le Pico éteint. OE sur VCCA :
# Pico éteint, sorties en haute impédance.
ajoute('U3', 'Logic_LevelTranslator:TXU0304PW', 'TXU0304', FP['tssop14'],
       {1: '3V3', 2: 'SPI_CS', 3: 'SPI_SCK', 4: 'SPI_MOSI', 5: 'SPI_MISO', 6: None, 7: 'GND', 8: '3V3',
        9: None, 10: 'ACC_MISO', 11: 'ACC_MOSI', 12: 'ACC_SCK', 13: 'ACC_CS', 14: 'VPILES'}, B, 'TXU0304')
c('C5', '100n', '3V3', 'GND', B, 'C100n')
c('C6', '100n', 'VPILES', 'GND', B, 'C100n')

# =========================================================================== LUMIÈRES
B = 'led'
# Élévateur de 4,48 V : VREF 1,0 V x (1 + 1 Mohm / 287 kohm). Entrée sur VSYS : il marche aussi sur l'USB.
ajoute('U4', 'Regulator_Switching:TPS61099DRV', 'TPS61099', FP['wson6'],
       {1: 'GND', 2: 'VLED_BOOST', 3: 'BOOST_FB', 4: 'LED_EN', 5: 'BOOST_SW', 6: 'VSYS', 7: 'GND'}, B,
       'TPS61099')
ajoute('L1', 'Device:L', '2,2u', FP['l2520'], {1: 'VSYS', 2: 'BOOST_SW'}, B, 'L2u2')
c('C7', '10u', 'VSYS', 'GND', B, 'C10u')
c('C8', '10u', 'VLED_BOOST', 'GND', B, 'C10u')
c('C9', '10u', 'VLED_BOOST', 'GND', B, 'C10u')
r('R9', '1M', 'VLED_BOOST', 'BOOST_FB', B, 'R1M')
r('R10', '287k', 'BOOST_FB', 'GND', B, 'R287k')
r('R11', '1M', 'LED_EN', 'GND', B, 'R1M')
# Cavalier : les LED sur l'élévateur (par défaut) ou sur le 3,3 V du Pico (carte.md §4.3).
ajoute('JP2', 'Jumper:SolderJumper_3_Bridged12', 'LED : élévateur | 3,3 V', FP['jp3'],
       {1: 'VLED_BOOST', 2: 'VLED', 3: '3V3'}, B,
       Note='1-2 fermé : élévateur (par défaut) ; couper, puis fermer 2-3 : 3,3 V du Pico')
c('C10', '10u', 'VLED', 'GND', B, 'C10u')
r('R12', '100', 'LED_DATA', 'LED_D0', B, 'R100')
LED = [('D1', "alimentation"), ('D2', 'croix, haut'), ('D3', 'croix, droite'), ('D4', 'croix, bas'),
       ('D5', 'croix, gauche'), ('D6', 'Select'), ('D7', 'Start'), ('D8', 'B'), ('D9', 'A')]
for i, (ref, role) in enumerate(LED):
    sortie = f'LED_D{i + 1}' if i < len(LED) - 1 else 'LED_FIN'
    ajoute(ref, 'LED:WS2812B-2020', 'WS2812C-2020-V6', FP['led2020'],
           {1: sortie, 2: 'GND', 3: f'LED_D{i}', 4: 'VLED'}, B, 'WS2812', Note=f'LED {i + 1} : {role}')
    c(f'C{11 + i}', '100n', 'VLED', 'GND', B, 'C100n', Note=f'découplage de {ref}, hors des membranes')

# =========================================================================== COMMANDES
B = 'commandes'
BOUTONS = [('SW2', 'Droite', 'croix'), ('SW3', 'Bas', 'croix'), ('SW4', 'Haut', 'croix'),
           ('SW5', 'Gauche', 'croix'), ('SW6', 'A', 'ab'), ('SW7', 'B', 'ab'),
           ('SW8', 'Select', 'select'), ('SW9', 'Start', 'start')]
for i, (ref, nom, fp) in enumerate(BOUTONS):
    net = 'BTN_' + nom.upper()
    ajoute(ref, 'Switch:SW_Push', nom, FP[fp], {1: 'GND', 2: net}, B, 'MEMBRANE',
           Note=f'Pastilles de {nom}, sous la membrane')
    r(f'R{13 + i}', '10k', net, '3V3', B, 'R10k', dnp=True,
      Note='Tirage externe, à poser si les pastilles de carbone sont trop résistantes (T0.5)')
# Molettes : le potentiomètre double de Silent Modding, alimenté par GP21 le temps d'une mesure.
# Broches : 1 et 2 les deux hauts, 4 le curseur de la piste de la broche 1, 3 l'autre, 5 le commun.
for ref, net, role in [('RV1', 'POT_VOL', 'volume, molette de gauche'),
                       ('RV2', 'POT_LUM', 'luminosité, molette de droite')]:
    ajoute(ref, 'carte-manette:Molette_10k_double', '10k double', FP['molette'],
           {1: 'POT_ALIM', 2: 'POT_ALIM', 3: None, 4: net, 5: 'GND'}, B, 'SM_POT', Note=role)
c('C20', '1n', 'POT_VOL', 'GND', B, 'C1n')
c('C21', '1n', 'POT_LUM', 'GND', B, 'C1n')

# =========================================================================== POINTS DE TEST
B = 'test'
for i, (net, role) in enumerate([('RUN', 'RUN'), ('UART_TX', 'TX'), ('UART_RX', 'RX'),
                                 ('GEN_APPUIS', 'GP15'), ('BTN_DROITE', 'Droite'), ('VSYS', 'VSYS'),
                                 ('VLED', 'VLED'), ('VPILES', 'piles'), ('3V3', '3V3'),
                                 ('LED_FIN', 'fin des LED'), ('GND', 'masse'), ('GND', 'masse')]):
    ajoute(f'TP{1 + i}', 'Connector:TestPoint', role, FP['tp'], {1: net}, B)

# Drapeaux d'alimentation (ERC) : ces équipotentielles sont alimentées par les piles, le Pico ou
# l'élévateur, à travers des pièces passives aux yeux de l'ERC.
for i, net in enumerate(['VPILES_BRUT', 'VPILES', 'VSYS', 'GND', 'VLED', 'BOOST_SW']):
    ajoute(f'PF{1 + i}', 'power:PWR_FLAG', 'PWR_FLAG', '', {1: net}, B)


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
