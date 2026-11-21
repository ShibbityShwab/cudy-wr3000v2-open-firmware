#!/usr/bin/env python3
"""Regenerate wifi-datapath-slice mapping/inventory CSVs from local analysis
artifacts. Inputs (NOT committed, produced by this lane's analysis):
  <work>/wifi.nm.txt          - nm -S --size-sort of the vendor hi5622v100_wifi.ko
  <work>/exports-full.txt     - __ksymtab_strings export names (readelf -p)
  <work>/callgraph.json       - bl-target call graph from cross-objdump of the ko
  <work>/arity.json           - call-site arity/retuse evidence per symbol
  <work>/twin-scores.json     - cross-mapping vs the hi1105 reference tree
  <work>/hi1105-funcs.tsv     - reference function definition metadata
Usage: python3 gen-mapping.py <workdir> <outdir>"""
import csv, json, os, sys

W, OUT = sys.argv[1], sys.argv[2]
sizes = {}
for line in open('%s/wifi.nm.txt' % W):
    p = line.split()
    if len(p) >= 4 and p[2] in ('t', 'T') and (p[3].startswith('hdpp_') or p[3].startswith('alg_')):
        sizes[p[3]] = int(p[1], 16)
exports = set(l.strip() for l in open('%s/exports-full.txt' % W) if l.strip())
cg = json.load(open('%s/callgraph.json' % W))
arity = json.load(open('%s/arity.json' % W))
twins = {t[0]: t for t in json.load(open('%s/twin-scores.json' % W))}
ref = {}
for row in csv.DictReader(open('%s/hi1105-funcs.tsv' % W), delimiter='\t'):
    ref[row['name']] = row

def callers_of(n, cap=6):
    cs = [c for c in cg['callers'].get(n, []) if not c.startswith('$')]
    return ';'.join(cs[:cap])

def write_inv(fam, path):
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['symbol', 'size_bytes', 'exported', 'arity_sites', 'arity_mode', 'retuse_pct', 'top_callers'])
        for n in sorted(x for x in sizes if x.startswith(fam)):
            a = arity.get(n, (0, 0, 0))
            w.writerow([n, sizes[n], 'yes' if n in exports else '', a[0], a[1], a[2], callers_of(n)])

def write_map(fam, prefix, path):
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['ko_symbol', 'size_bytes', 'exported', 'twin_symbol', 'score',
                    'shared_callers', 'shared_callees', 'twin_file', 'twin_ret_kind',
                    'twin_n_params', 'twin_param_kinds'])
        rows = [(t[0], t[1], t[2], t[3], t[4]) for t in twins.values() if t[0].startswith(fam)]
        for name, size, twin_n, score, det in sorted(rows, key=lambda r: -r[3]):
            if not twin_n:
                continue
            r = ref.get(twin_n, {})
            w.writerow([name, size, 'yes' if name in exports else '', twin_n, score,
                        ';'.join(det['callers'][:6]), ';'.join(det['callees'][:6]),
                        r.get('file', ''), r.get('ret_kind', ''), r.get('n_params', ''),
                        r.get('param_kinds', '')])

write_inv('hdpp_', '%s/hdpp-inventory.csv' % OUT)
write_inv('alg_', '%s/alg-inventory.csv' % OUT)
write_map('hdpp_', 'dpe_', '%s/hdpp-to-dpe.csv' % OUT)
write_map('alg_', 'alg_', '%s/alg-to-twin.csv' % OUT)

# coverage summary
def stats(fam):
    names = [n for n in sizes if n.startswith(fam)]
    tb = sum(sizes[n] for n in names)
    exact = [n for n in names if twins[n][3] >= 1000]
    corr = [n for n in names if 20 <= twins[n][3] < 1000]
    weak = [n for n in names if 0 < twins[n][3] < 20]
    none = [n for n in names if twins[n][3] == 0]
    exp = [n for n in names if n in exports]
    return dict(total=len(names), bytes=tb, exact=len(exact),
                exact_bytes=sum(sizes[n] for n in exact), corr=len(corr),
                corr_bytes=sum(sizes[n] for n in corr), weak=len(weak),
                none=len(none), exported=len(exp),
                exported_exact=len([n for n in exp if twins[n][3] >= 1000]))
h, a = stats('hdpp_'), stats('alg_')
with open('%s/coverage.csv' % OUT, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['family', 'ko_funcs', 'ko_bytes', 'exact_name_twins', 'exact_twin_bytes',
                'call_correlated_twins', 'correlated_bytes', 'weak_signal', 'no_twin',
                'exported_funcs', 'exported_with_exact_twin'])
    for s in (h, a):
        w.writerow([s['total'] and ('hdpp_' if h is s else 'alg_'), s['total'], s['bytes'],
                    s['exact'], s['exact_bytes'], s['corr'], s['corr_bytes'], s['weak'],
                    s['none'], s['exported'], s['exported_exact']])
print('hdpp', h)
print('alg ', a)
