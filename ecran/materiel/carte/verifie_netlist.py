import re, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import conception as K
from kicadlib import fin_parenthese
s = open(sys.argv[1], encoding='utf-8').read()
i = s.index('(nets')
nets = {}
for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)', s[i:]):
    st = i + m.start()
    e = fin_parenthese(s, st)
    blk = s[st:e+1]
    nodes = set(re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk))
    nets[m.group(1).lstrip('/')] = nodes
attendu = {}
for n, l in K.nets().items():
    attendu[n] = set(l)
# retire les PWR_FLAG (pas sur la carte)
for n in attendu: attendu[n] = {x for x in attendu[n] if not x[0].startswith('PF')}
ok = True
for n, v in attendu.items():
    if not v: continue
    if n not in nets:
        print('MANQUE dans KiCad :', n, sorted(v)); ok = False; continue
    w = {x for x in nets[n] if not x[0].startswith('PF')}
    if w != v:
        print('DIFFÉRENT', n, 'attendu-kicad', sorted(v - w), 'kicad-attendu', sorted(w - v)); ok = False
extra = [n for n in nets if n not in attendu and not n.startswith('unconnected')]
if extra: print('équipotentielles en plus dans KiCad :', extra[:20]); ok = False
print('IDENTIQUES' if ok else 'ÉCARTS', len(nets), 'nets KiCad,', len(attendu), 'attendus')
