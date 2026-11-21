#!/usr/bin/env python3
"""decode-stock-dump.py - regenerate the stock CFE dump findings (read-only).

Reads build/evidence-bench-gates/cfe-stock/ (carveout-p0.bin, p2-carveout.bin,
win*.bin) and writes cfe-stock-analysis.md next to it. No device access.
"""
import argparse, collections, os, struct

def load(d, name):
    p = os.path.join(d, name)
    return open(p, 'rb').read() if os.path.exists(p) else b''

def u32(b, o):
    return struct.unpack_from('<I', b, o)[0] if 0 <= o <= len(b) - 4 else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', required=True)
    a = ap.parse_args(); d = a.dir
    p0 = load(d, 'carveout-p0.bin'); p2 = load(d, 'p2-carveout.bin')
    pub = load(d, 'win15-pub.bin'); mcc = load(d, 'win13-mcc.bin')
    ifc = load(d, 'win10-ifc.bin'); ifc2 = load(d, 'p2-win10-ifc.bin')
    napt = load(d, 'win12-napt.bin'); napt2 = load(d, 'p2-win12-napt.bin')
    l2 = load(d, 'win11-l2.bin'); glb = load(d, 'win08-pfeglb.bin'); pie = load(d, 'win16-pie.bin')
    out = ['# stock CFE dump analysis (regenerated)', '']
    out.append('inputs: carveout-p0 %s, p2 %s' % (p0[:0] or '', ''))
    # tagact
    out.append('## tagact (pub+0x54000)')
    for i in range(64):
        row = pub[0x54000 + i*16:0x54000 + (i+1)*16]
        if any(row):
            w0, w1 = u32(row, 0), u32(row, 4)
            out.append('tagact[%d] w0=%#010x w1=%#010x valid=%d selector=%d' %
                       (i, w0, w1, (w1 >> 15) & 1, (w1 >> 12) & 7))
    # tables that stock leaves empty
    empty = []
    for name, off, ids in (('l3intf-key', 0x5b000, 64), ('l3intf-data', 0x5c000, 64),
                           ('nhp', 0x59000, 128), ('car', 0x55000, 64),
                           ('car_profile', 0x53000, 32)):
        n = sum(1 for i in range(ids) if any(pub[off+i*16:off+(i+1)*16]))
        if n == 0:
            empty.append(name)
        else:
            for i in range(ids):
                row = pub[off+i*16:off+(i+1)*16]
                if any(row):
                    out.append('%s[%d] %s' % (name, i, row.hex()))
    if not any(mcc[0x44000:0x44000+2048]):
        empty.append('mcc-userll')
    out.append('empty stock tables: ' + ', '.join(empty))
    # ifc
    out.append('## ifc (tab128 + engine)')
    for i in range(128):
        row = ifc[0x50000 + i*16:0x50000 + (i+1)*16]
        if any(row):
            out.append('ifc_tab[%d] %s' % (i, row.hex()))
    out.append('ifc engine last descriptor: ' + ' '.join('%#x' % (u32(ifc, o) or 0)
               for o in (0x138, 0x13c, 0x140, 0x144, 0x148)))
    out.append('ifc counters: ' + ' '.join('%#x' % (u32(ifc, o) or 0) for o in range(0x1000, 0x1040, 4)))
    out.append('ifc counters p2: ' + ' '.join('%#x' % (u32(ifc2, o) or 0) for o in range(0x1000, 0x1040, 4)))
    # napt + flow geometry
    out.append('## napt')
    out.append('control: ' + ' '.join('%#x=%#x' % (o, u32(napt, o) or 0)
               for o in (0x50, 0x54, 0x60, 0x64, 0x68, 0x70, 0x130, 0x134, 0x138, 0x13c)))
    out.append('counters: ' + ' '.join('%#x=%d' % (o, u32(napt, o) or 0)
               for o in range(0x1000, 0x109c, 4) if u32(napt, o)))
    out.append('counters p2: ' + ' '.join('%#x=%d' % (o, u32(napt2, o) or 0)
               for o in range(0x1000, 0x109c, 4) if u32(napt2, o)))
    out.append('flow_cnt nonzero: %d' % sum(1 for i in range(255) if any(napt[0x40000+i*16:0x40100+i*16])))
    poison, marker = 0x66cc66cc, 0x017be000
    for name, s, e in (('v4 buckets', 0x400000, 0x408000), ('v4 arena', 0x408000, 0x448000),
                       ('v6 buckets', 0x448000, 0x450000), ('v6 arena', 0x450000, 0x500000)):
        c = collections.Counter(struct.unpack_from('<I', p0, o)[0] for o in range(s, e, 4))
        ch = sum(1 for i in range(s, e) if p0[i] != p2[i]) if p2 and len(p2) >= e else -1
        out.append('flow %s: top=%s changed-bytes p0->p2=%d' %
                   (name, [(hex(k), v) for k, v in c.most_common(3)], ch))
    # queue window
    if p0 and p2:
        q0, q2 = p0[:0x400000], p2[:0x400000]
        changed = sum(1 for i in range(0, min(len(q0), len(q2))) if q0[i] != q2[i])
        pages = sum(1 for p in range(0, min(len(q0), len(q2)), 4096)
                    if q0[p:p+4096] != q2[p:p+4096])
        out.append('## queue window')
        out.append('p0->p2 changed bytes %d of %d, changed pages %d/1024' %
                   (changed, len(q0), pages))
        out.append('queue top words: ' + str([(hex(k), v) for k, v in collections.Counter(
            struct.unpack_from('<I', q0, o)[0] for o in range(0, len(q0), 4)).most_common(8)]))
    out.append('## l2/glb/pie')
    out.append('l2+0x100: ' + ' '.join('%#x=%#x' % (o, u32(l2, o) or 0)
               for o in (0x104, 0x108, 0x140, 0x164, 0x180, 0x184, 0x188)))
    out.append('glb attr0(0x2c)=%#x' % (u32(glb, 0x2c) or 0))
    out.append('pie nonzero pages: ' + str([hex(p) for p in range(0, len(pie), 0x1000) if any(pie[p:p+0x1000])]))
    open(os.path.join(d, 'cfe-stock-analysis.md'), 'w').write('\n'.join(out) + '\n')
    print('wrote', os.path.join(d, 'cfe-stock-analysis.md'))

if __name__ == '__main__':
    main()
