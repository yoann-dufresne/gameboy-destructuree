#!/usr/bin/env python3
"""Résumé du contrôle des règles (DRC) de KiCad, au format JSON : erreurs par type, liaisons
manquantes, écarts avec le schéma. Code de sortie non nul s'il reste une erreur électrique."""
import collections
import json
import sys

d = json.load(open(sys.argv[1]))
v = d.get('violations', [])
cosmetique = {'silk_overlap', 'silk_over_copper', 'silk_edge_clearance', 'text_height', 'text_thickness'}
c = collections.Counter(x['type'] for x in v)
nc = d.get('unconnected_items', [])
par = d.get('schematic_parity', [])
print('DRC :', dict(c) or 'aucune violation')
print('liaisons manquantes :', len(nc), '- écarts schéma/carte :', len(par))
for x in [x for x in v if x['type'] not in cosmetique][:30]:
    print(' ', x['type'], '|', ' || '.join(i['description'][:60] for i in x['items']))
for x in nc[:30]:
    print('  manque |', ' || '.join(i['description'][:60] for i in x['items']))
for x in par[:30]:
    print('  parité |', x.get('description', ''), ' || '.join(i['description'][:60] for i in x.get('items', [])))
graves = sum(n for t, n in c.items() if t not in cosmetique) + len(nc) + len(par)
sys.exit(1 if graves else 0)
