#!/usr/bin/env python3
"""analyze-cfe-dump.py - host-side decoder for the stock CFE/switch read-only dump.

Input: a directory produced by the stock collect-stock-dump.sh, i.e. one or more
phase subdirectories (p0, p1, p2, p2b, p3, p4, p5, ...) each containing:
  carveout.bin       0x80000000..0x80500000 (queue 4 MiB + flow 1 MiB)
  mmio-pub.bin       pub window dump (0x12480000) if full/key
  mmio-mcc.bin, mmio-ifc.bin, mmio-napt.bin, mmio-l2.bin, mmio-eps.bin, ...
  proc*.txt, ip-s-link.txt, time.txt, etc.
The script decodes the readable MMIO tables (tagact, l3intf, nhp, car, car_profile,
mcc userll, ifc tab128, ifc/flow/l3/nhp counters), scans the DDR carveout for the
known MACs/IPs and for nonzero pages/pointers, and diffs phases to separate static
tables from runtime counters.

It is read-only: it only reads the dump files and writes the report/JSON next to
them. No device access.
"""

import argparse
import json
import os
import re
import struct
import sys

WINDOW = 0x80000
PUB_BASE = 0x12480000

def rd32(b, off):
    if off + 4 > len(b):
        return None
    return struct.unpack_from('<I', b, off)[0]

def rd16(b, off):
    if off + 2 > len(b):
        return None
    return struct.unpack_from('<H', b, off)[0]

def file_bytes(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return b''

WINDOW_BASES = {
    0x11000000: 'dp', 0x11080000: 'ips', 0x11100000: 'fam', 0x11180000: 'pe',
    0x11200000: 'eqm', 0x11280000: 'eps', 0x11380000: 'prbs',
    0x12000000: 'pfe_glb', 0x12080000: 'pa', 0x12100000: 'ifc',
    0x12180000: 'l2', 0x12280000: 'napt', 0x12380000: 'mcc',
    0x12400000: 'opp', 0x12480000: 'pub', 0x10a70000: 'pie',
}

def load_flat(windows_dir, carveout_path):
    mm = {}
    for fn in sorted(os.listdir(windows_dir)):
        m = re.match(r'win\d+-0x([0-9a-fA-F]+)\.bin$', fn)
        if not m:
            continue
        base = int(m.group(1), 16)
        name = WINDOW_BASES.get(base)
        if not name:
            continue
        mm[name] = file_bytes(os.path.join(windows_dir, fn))
    carve = file_bytes(carveout_path) if carveout_path else b''
    return mm, carve

def summary_nonzero_pages(buf, base, lines, limit=16):
    if not buf:
        lines.append('%#x window: missing' % base)
        return
    pages = [p for p in range(0, len(buf), 4096) if any(buf[p:p + 4096])]
    lines.append('%#x window: %d bytes, %d nonzero 4K pages (first: %s)' %
                 (base, len(buf), len(pages),
                  ' '.join('%#x' % (base + p) for p in pages[:limit])))

def poison_runs(buf, word, min_run=16):
    pat = struct.pack('<I', word)
    runs = []
    i = buf.find(pat)
    while i >= 0:
        j = i
        while j + 4 <= len(buf) and buf[j:j + 4] == pat:
            j += 4
        if (j - i) // 4 >= min_run:
            runs.append((i, (j - i) // 4))
        i = buf.find(pat, j)
    return runs

def carveout_report(buf, lines):
    if not buf:
        lines.append('carveout: missing')
        return
    lines.append('carveout: %d bytes (0x80000000..0x%08x)' % (len(buf), 0x80000000 + len(buf)))
    poison = poison_runs(buf, 0x66cc66cc)
    lines.append('0x66cc66cc poison runs (>=16 words): %d' % len(poison))
    for off, words in poison[:10]:
        lines.append('  poison at phys %#x..%#x (%d words)' % (0x80000000 + off, 0x80000000 + off + words * 4, words))
    # regions: queue 0x0..0x400000, flow table bases at 0x400000/0x408000/0x448000/0x450000
    for roff in (0x400000, 0x408000, 0x448000, 0x450000):
        chunk = buf[roff:roff + 4096]
        first = chunk[:64].hex()
        lines.append('flow region %#x..%#x first 64B: %s' % (roff, roff + 4096, first))
    # non-poison, non-zero words in the flow window (possible table headers)
    flow = buf[0x400000:0x500000]
    hits = []
    for off in range(0, len(flow) - 4, 4):
        w = struct.unpack_from('<I', flow, off)[0]
        if w not in (0, 0x66cc66cc):
            hits.append((0x400000 + off, w))
            if len(hits) >= 60:
                break
    lines.append('flow window non-poison/non-zero words: %d shown' % len(hits))
    for off, w in hits[:40]:
        lines.append('  phys %#x = %#010x' % (0x80000000 + off, w))

def mmio(phase, name):
    p = os.path.join(phase, 'mmio-' + name + '.bin')
    if not os.path.exists(p) and name == 'pub':
        return b''
    return file_bytes(p)

def decode_tagact(pub, lines):
    if len(pub) < 0x54000 + 64 * 16:
        return
    n = 0
    for idx in range(64):
        off = 0x54000 + idx * 16
        w0 = rd32(pub, off); w1 = rd32(pub, off + 4)
        w2 = rd32(pub, off + 8); w3 = rd32(pub, off + 12)
        if not (w0 or w1 or w2 or w3):
            continue
        n += 1
        byte5 = (w1 >> 8) & 0xff if w1 is not None else 0
        selector = (byte5 >> 4) & 7
        valid = bool(byte5 & 0x80)
        add_tag = bool(w0 & 0x40000)
        lines.append('tagact[%d] (pub+0x54000+%d*16): w0=%#010x w1=%#010x w2=%#010x w3=%#010x '
                     'valid=%d selector=%d add_tag=%d svlan=%d' %
                     (idx, idx, w0, w1, w2, w3, valid, selector, add_tag, w0 & 0xfff))
    lines.append('tagact: %d non-zero entries' % n)

def decode_l3intf(pub, lines):
    if len(pub) < 0x5c000 + 64 * 16:
        return
    n = 0
    for idx in range(64):
        koff = 0x5b000 + idx * 16
        doff = 0x5c000 + idx * 16
        key = pub[koff:koff + 16]
        data = pub[doff:doff + 16]
        if not any(key) and not any(data):
            continue
        n += 1
        d = data
        sip = struct.unpack_from('<I', d, 0)[0] if len(d) >= 4 else 0
        mac = d[4:10].hex(':') if len(d) >= 10 else ''
        session = struct.unpack_from('<H', d, 10)[0] if len(d) >= 12 else 0
        word12 = struct.unpack_from('<I', d, 12)[0] if len(d) >= 16 else 0
        mtu = word12 & 0x3fff
        tagact = (word12 >> 14) & 0x3f
        b14 = d[14] if len(d) > 14 else 0
        b15 = d[15] if len(d) > 15 else 0
        smac_if = bool(b14 & 0x10)
        sip_if = bool(b14 & 0x20)
        ppp = (b14 >> 6) & 3
        ttl = bool(b15 & 1)
        statis = (b15 >> 1) & 0x3f
        lines.append('l3intf[%d]: key=%s data=%s | sip=%s mac=%s session=%d mtu=%d tagact_id=%d '
                     'smac_if=%d sip_if=%d ppp=%d ttl_dec=%d statis=%d' %
                     (idx, key.hex(), data.hex(), '.'.join(str((sip >> s) & 0xff) for s in (0, 8, 16, 24)),
                      mac, session, mtu, tagact, smac_if, sip_if, ppp, ttl, statis))
    lines.append('l3intf: %d non-zero entries' % n)

def decode_simple_tab(pub, name, off, ids, lines):
    if len(pub) < off + ids * 16:
        return
    n = 0
    for idx in range(ids):
        chunk = pub[off + idx * 16:off + (idx + 1) * 16]
        if not any(chunk):
            continue
        n += 1
        lines.append('%s[%d] (+%#x): %s' % (name, idx, off + idx * 16, chunk.hex()))
    lines.append('%s: %d non-zero entries' % (name, n))

def decode_userll(mcc, lines):
    if len(mcc) < 0x44000 + 512 * 4:
        return
    n = 0
    for idx in range(512):
        w = rd32(mcc, 0x44000 + idx * 4)
        if not w:
            continue
        n += 1
        port = w & 0x1ff
        vport = (w >> 10) & 0xf
        bit14 = (w >> 14) & 1
        ext = (w >> 16) & 0x3f
        if n <= 32:
            lines.append('userll[%d] (mcc+0x44000+%d*4): %#010x port~%d vport~%d bit14=%d ext~%d' %
                         (idx, idx, w, port, vport, bit14, ext))
    lines.append('userll: %d non-zero words' % n)

def decode_ifc(ifc, lines):
    if len(ifc) < 0x50000 + 128 * 16:
        return
    n = 0
    for idx in range(128):
        chunk = ifc[0x50000 + idx * 16:0x50000 + (idx + 1) * 16]
        if not any(chunk):
            continue
        n += 1
        if n <= 32:
            lines.append('ifc_tab[%d]: %s' % (idx, chunk.hex()))
    lines.append('ifc tab128: %d non-zero entries' % n)
    # ifc counters at +0x60000 (16B each)
    n2 = 0
    for idx in range(63):
        chunk = ifc[0x60000 + idx * 16:0x60000 + (idx + 1) * 16]
        if any(chunk):
            n2 += 1
    lines.append('ifc counters: %d non-zero entries' % n2)

def decode_flow_cnt(napt, lines):
    if len(napt) < 0x40000 + 255 * 16:
        return
    n = 0
    for idx in range(255):
        chunk = napt[0x40000 + idx * 16:0x40000 + (idx + 1) * 16]
        if not any(chunk):
            continue
        n += 1
        if n <= 32:
            lines.append('flow_cnt[%d]: %s' % (idx, chunk.hex()))
    lines.append('flow counters: %d non-zero entries' % n)
    lines.append('napt control: 0x50=%#010x 0x54=%#010x 0x60=%#010x 0x64=%#010x 0x68=%#010x '
                 '0x70=%#010x 0x130=%#010x 0x134=%#010x 0x138=%#010x 0x13c=%#010x' %
                 tuple(rd32(napt, o) or 0 for o in (0x50, 0x54, 0x60, 0x64, 0x68, 0x70, 0x130, 0x134, 0x138, 0x13c)))

def scan_carveout(buf, host_mac, box_mac, host_ip, box_ip, lines, max_hits=40):
    if not buf:
        lines.append('carveout.bin missing')
        return {}
    lines.append('carveout: %d bytes' % len(buf))
    # nonzero 4 KiB pages
    pages = []
    for p in range(0, len(buf), 4096):
        chunk = buf[p:p + 4096]
        if any(chunk):
            pages.append(p)
    lines.append('nonzero 4K pages: %d (first 24: %s)' %
                 (len(pages), ' '.join('%#x' % (0x80000000 + p) for p in pages[:24])))
    # MAC scans in both byte orders
    if host_mac:
        m = bytes.fromhex(host_mac.replace(':', ''))
        for order, label in ((m, 'host-mac'), (m[::-1], 'host-mac-rev')):
            off = buf.find(order)
            while off >= 0 and max_hits > 0:
                lines.append('%s found at carveout+%#x (phys 0x%08x)' % (label, off, 0x80000000 + off))
                max_hits -= 1
                off = buf.find(order, off + 1)
    if box_mac:
        m = bytes.fromhex(box_mac.replace(':', ''))
        for order, label in ((m, 'box-mac'), (m[::-1], 'box-mac-rev')):
            off = buf.find(order)
            while off >= 0 and max_hits > 0:
                lines.append('%s found at carveout+%#x (phys 0x%08x)' % (label, off, 0x80000000 + off))
                max_hits -= 1
                off = buf.find(order, off + 1)
    # IPv4 addresses in both endian orders
    for ip in (host_ip, box_ip):
        if not ip:
            continue
        parts = [int(x) for x in ip.split('.')]
        le = bytes(parts)
        be = bytes(reversed(parts))
        for pat, label in ((le, ip + ' (LE)'), (be, ip + ' (BE)')):
            off = buf.find(pat)
            if off >= 0:
                lines.append('ip %s found at carveout+%#x (phys 0x%08x)' % (label, off, 0x80000000 + off))
    # words that look like carveout pointers
    ptr_hits = []
    for off in range(0, len(buf) - 4, 4):
        w = struct.unpack_from('<I', buf, off)[0]
        if 0x80000000 <= w < 0x80500000:
            ptr_hits.append((off, w))
            if len(ptr_hits) >= 40:
                break
    lines.append('carveout-pointer-like words: %d (first: %s)' %
                 (len(ptr_hits), ' '.join('+%#x->%#x' % h for h in ptr_hits[:12])))
    # changed vs p0 handled elsewhere
    return {'nonzero_pages': len(pages)}

def diff_files(a, b, label, lines, limit=12):
    if not a or not b or len(a) != len(b):
        return
    runs = []
    start = None
    for i in range(len(a)):
        if a[i] != b[i]:
            if start is None:
                start = i
        else:
            if start is not None:
                runs.append((start, i - start)); start = None
    if start is not None:
        runs.append((start, len(a) - start))
    lines.append('%s: %d changed bytes in %d runs (first %d runs: %s)' %
                 (label, sum(r[1] for r in runs), len(runs), min(limit, len(runs)),
                  ', '.join('+%#x(%d)' % r for r in runs[:limit])))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', required=True)
    ap.add_argument('--host-mac', default='')
    ap.add_argument('--box-mac', default='')
    ap.add_argument('--host-ip', default='192.168.10.44')
    ap.add_argument('--box-ip', default='192.168.10.1')
    ap.add_argument('--windows-dir', default='', help='flat our-side layout: windows/ directory')
    ap.add_argument('--carveout', default='', help='flat our-side layout: carveout file')
    args = ap.parse_args()
    root = args.dir
    flat_mm = None
    flat_carve = b''
    if args.windows_dir:
        flat_mm, flat_carve = load_flat(args.windows_dir, args.carveout)
        phases = ['ours']
    else:
        phases = sorted([d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))])
    if not phases:
        sys.exit('no phase subdirectories under ' + root)
    out = []
    out.append('# CFE stock dump analysis')
    out.append('')
    out.append('root: ' + os.path.abspath(root))
    out.append('phases: ' + ', '.join(phases))
    out.append('')
    summary = {}
    cache = {}
    for ph in phases:
        pdir = os.path.join(root, ph)
        lines = []
        lines.append('## phase ' + ph)
        lines.append('')
        if flat_mm is not None:
            pub = flat_mm.get('pub', b''); mcc = flat_mm.get('mcc', b''); ifc = flat_mm.get('ifc', b'')
            napt = flat_mm.get('napt', b''); carve = flat_carve
            for name in ('dp','ips','fam','pe','eqm','eps','prbs','pfe_glb','pa','l2','opp','pie'):
                summary_nonzero_pages(flat_mm.get(name, b''), [k for k, v in WINDOW_BASES.items() if v == name][0], lines)
        else:
            pub = mmio(pdir, 'pub'); mcc = mmio(pdir, 'mcc'); ifc = mmio(pdir, 'ifc')
            napt = mmio(pdir, 'napt'); carve = file_bytes(os.path.join(pdir, 'carveout.bin'))
        cache[ph] = {'pub': pub, 'mcc': mcc, 'ifc': ifc, 'napt': napt, 'carve': carve}
        decode_tagact(pub, lines)
        decode_l3intf(pub, lines)
        decode_simple_tab(pub, 'nhp', 0x59000, 128, lines)
        decode_simple_tab(pub, 'car', 0x55000, 64, lines)
        decode_simple_tab(pub, 'car_profile', 0x53000, 32, lines)
        decode_userll(mcc, lines)
        decode_ifc(ifc, lines)
        decode_flow_cnt(napt, lines)
        summary[ph] = scan_carveout(carve, args.host_mac, args.box_mac, args.host_ip, args.box_ip, lines)
        carveout_report(carve, lines)
        out.extend(lines)
        out.append('')
    # phase diffs
    out.append('## phase diffs')
    out.append('')
    for ph in phases[1:]:
        for key, label in (('carve', 'carveout'), ('pub', 'mmio-pub'), ('mcc', 'mmio-mcc'),
                           ('ifc', 'mmio-ifc'), ('napt', 'mmio-napt')):
            diff_files(cache[phases[0]][key], cache[ph][key], '%s vs %s %s' % (phases[0], ph, label), out)
    out.append('')
    report = '\n'.join(out) + '\n'
    rp = os.path.join(root, 'cfe-analysis.md')
    with open(rp, 'w') as f:
        f.write(report)
    with open(os.path.join(root, 'cfe-analysis.json'), 'w') as f:
        json.dump({'phases': phases, 'summary': summary}, f, indent=1)
    print('wrote', rp)
    print('bytes:', len(report))

if __name__ == '__main__':
    main()
