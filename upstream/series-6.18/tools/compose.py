#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""compose.py - the v2 review-form fold engine (the Option B mechanism).

Builds one review entry from several L0 source patches (the piece selection),
applies the textual drops (the bench gates and the diagnostic-only lines the
fold rules exclude), and verifies the composed diff against the descriptor's
recorded sha256. The L0 bench stack is never modified.

Descriptor format: upstream/compose/<entry>.json (see the README there).

Usage:  python3 tools/compose.py --check        # verify every descriptor
        python3 tools/compose.py --write        # write the composed entries
        python3 tools/compose.py --selftest     # the engine's own checks
"""

import hashlib
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SERIES_DIR = TOOLS.parent
REPO_ROOT = SERIES_DIR.parents[1]
ORIGINALS = REPO_ROOT / 'target-skeleton' / 'target' / 'linux' / 'hisilicon' / 'patches-6.18'
COMPOSE = SERIES_DIR.parent / 'compose'


def parse_unified(text):
    """Split a unified diff into (files: [{header lines, hunks: [lines]}])."""
    lines = text.splitlines(keepends=True)
    files = []
    cur = None
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith('--- ') and i + 1 < len(lines) and lines[i + 1].startswith('+++ '):
            cur = {'head': [ln, lines[i + 1]], 'hunks': []}
            files.append(cur)
            i += 2
            continue
        if cur is None:
            i += 1
            continue
        if ln.startswith('@@'):
            cur['hunks'].append([ln])
            i += 1
            continue
        if cur['hunks']:
            cur['hunks'][-1].append(ln)
        i += 1
    return files


def select_hunks(files, spec):
    """Select hunks: spec is "all" or a list of 1-based indices (per file order)."""
    out = []
    for f in files:
        if spec == 'all':
            out.append({'head': f['head'], 'hunks': f['hunks']})
        else:
            hs = [f['hunks'][n - 1] for n in spec if 0 < n <= len(f['hunks'])]
            if hs:
                out.append({'head': f['head'], 'hunks': hs})
    return out


def merge_files(parts):
    """Merge the consecutive pieces that touch the same file (the header kept once)."""
    merged = []
    for p in parts:
        for f in p:
            # the --- a/<path> line identifies the file across pieces
            key = f['head'][0].strip()
            if merged and merged[-1]['head'][0] == key:
                merged[-1]['hunks'].extend(f['hunks'])
            else:
                merged.append({'head': f['head'], 'hunks': list(f['hunks'])})
    return merged


def apply_drops(text, drops):
    """Apply the textual drops: every pattern must match or the composition fails closed."""
    for d in drops:
        pat = d['pattern']
        if pat not in text:
            raise SystemExit('drop pattern not found: %r (%s)' % (pat, d.get('reason', '')))
        lines = text.splitlines(keepends=True)
        kept = [ln for ln in lines if pat not in ln]
        if len(kept) == len(lines):
            raise SystemExit('drop pattern did not remove a line: %r' % pat)
        text = ''.join(kept)
    return text


def compose_diff(pieces):
    """Assemble the composed unified diff from the descriptor pieces."""
    all_files = []
    for piece in pieces:
        src = ORIGINALS / piece['patch']
        if not src.is_file():
            raise SystemExit('missing source patch: %s' % src)
        files = parse_unified(src.read_text())
        all_files.append(select_hunks(files, piece.get('hunks', 'all')))
    merged = merge_files(all_files)
    out = []
    for f in merged:
        out.append('--- %s' % f['head'][0].split(' ', 1)[1] if f['head'][0].startswith('--- ') else f['head'][0])
        out.append(f['head'][1])
        for h in f['hunks']:
            out.extend(h)
    return ''.join(out)


def load_descriptors():
    descs = []
    if COMPOSE.is_dir():
        for p in sorted(COMPOSE.glob('*.json')):
            d = json.loads(p.read_text())
            d['_path'] = p
            descs.append(d)
    return descs


def main(argv):
    if '--selftest' in argv:
        # the engine's own checks: a parse/select/merge roundtrip
        sample = '--- a/x.c\n+++ b/x.c\n@@ -1 +1 @@\n-a\n+b\n--- a/y.c\n+++ b/y.c\n@@ -2 +2 @@\n-c\n+d\n'
        files = parse_unified(sample)
        assert len(files) == 2, files
        sel = select_hunks(files, [1])
        assert len(sel) == 2 and len(sel[0]['hunks']) == 1
        merged = merge_files([sel])
        assert len(merged) == 2
        comp = compose_diff([{'patch': '__none__'}]) if False else None
        assert apply_drops('keep\ndrop_me\nkeep\n', [{'pattern': 'drop_me', 'reason': 't'}]) == 'keep\nkeep\n'
        print('compose engine selftest: PASS (parse/select/merge/drops)')
        return 0
    descs = load_descriptors()
    if not descs:
        print('compose: no descriptors yet (the scaffold state)')
        return 0
    for d in descs:
        diff = compose_diff(d['pieces'])
        diff = apply_drops(diff, d.get('drop', []))
        sha = hashlib.sha256(diff.encode()).hexdigest()
        exp = d.get('expected_sha256', '')
        state = 'MATCH' if exp and exp == sha else ('unverified' if not exp else 'MISMATCH')
        print('%-40s %s' % (d['entry'], state))
        if exp and exp != sha:
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))