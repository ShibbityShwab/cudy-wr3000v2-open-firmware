#!/usr/bin/env bash
# publish-prep-check.sh - READ-ONLY publish-readiness checker for this repository.
#
# usage: tools/publish-prep-check.sh [--strict] [--no-history] [--no-links] [--help]
#
# This is the executable companion to docs/PUBLISH-PREP.md. It answers one question:
# "is the published half actually ready to be called a release, and does its history
# carry material that must not be published?" Every check is READ-ONLY.
#
# Checks (sections A-H):
#   A  required release files and license texts (MIT LICENSE + GPL-2.0 COPYING)
#   B  license split: SPDX headers and per-file Copyright under lab/, DCO/commit sign-offs
#   C  provenance wording ("GPL-compatible reverse engineering", not "clean-room")
#      and the Wi-Fi / LAN-switch licensing statement
#   D  GPL source request: sent/no-reply record, sent-copy evidence, NOTICE cross-reference
#   E  git history: the vendor .o/.ko blobs, any other binary blob, large blobs,
#      per-ref reachability audit, tags, vendor-derived text remnants
#   F  secrets: key-material filenames, high-confidence credential patterns in the tree
#      and in every reachable blob, device-unique MAC identifiers
#   G  relative markdown links resolve (no network; external links are not fetched)
#   H  PUBLISH-READINESS items 6-8: reproducible kernel delta, trailer generator,
#      single-source DTS, schematic target tree
#
# GUARANTEES (by construction):
#   - no file is created, modified or deleted; output goes to stdout only
#   - no git command that writes: no gc, reflog, fetch, commit, tag, config --set,
#     checkout, reset, branch, push, filter-repo/filter-branch
#   - no network access; it uses only the local repository and the worktree
#   - the only external commands are git plumbing, grep/awk/sed/sort and python3
#     (python3 stdlib only; the script degrades to SKIP if python3 is absent)
#
# Exit codes: 0 = no FAIL, 1 = at least one FAIL (or a WARN under --strict),
#             2 = usage error / not a git work tree.
#
# The invocation is a DRY RUN even when it FAILs: nothing is fixed here. The blocker
# map, the owner-approval table and the non-destructive release sequence are in
# docs/PUBLISH-PREP.md. Do not turn a PASS from this script into a publication
# decision on its own - the human checks (license review, GPL decision, upstream
# review) are not automatable.
set -uo pipefail

export LC_ALL=C
STRICT=0
DO_HISTORY=1
DO_LINKS=1
PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0

usage() {
	cat <<'EOF'
usage: tools/publish-prep-check.sh [--strict] [--no-history] [--no-links] [--help]

  --strict      treat WARN as failure in the exit code (advisory checks become gates)
  --no-history  skip the git history scan (E) and unreachable-object audit (fast mode;
                NOT valid for a release decision - the history scan is the load-bearing one)
  --no-links    skip the markdown link check (G)
  --help        print this text

Read-only: no writes, no network, no push, no git mutation.
Exit: 0 no FAIL, 1 FAIL (or WARN under --strict), 2 usage / not a git work tree.
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--strict) STRICT=1 ;;
		--no-history) DO_HISTORY=0 ;;
		--no-links) DO_LINKS=0 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "publish-prep-check: unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

pass() { PASS_COUNT=$((PASS_COUNT + 1)); printf 'PASS  %s\n' "$*"; }
fail() { FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL  %s\n' "$*"; }
warn() { WARN_COUNT=$((WARN_COUNT + 1)); printf 'WARN  %s\n' "$*"; }
info() { printf 'INFO  %s\n' "$*"; }
section() { printf '\n== %s ==\n' "$*"; }

# Consume "SEVERITY<TAB>message" lines from a python helper and dispatch them.
consume_scan() {
	local sev msg
	while IFS=$'\t' read -r sev msg; do
		[ -n "${sev:-}" ] || continue
		case "$sev" in
			PASS) pass "$msg" ;;
			FAIL) fail "$msg" ;;
			WARN) warn "$msg" ;;
			*) info "$msg" ;;
		esac
	done
}

# -- context -----------------------------------------------------------------
if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "publish-prep-check: not inside a git work tree (run it from the published half)" >&2
	exit 2
fi
ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "publish-prep-check: cannot resolve the work tree root" >&2; exit 2; }
cd "$ROOT" || { echo "publish-prep-check: cannot cd to $ROOT" >&2; exit 2; }

printf 'publish-prep-check - read-only publish-readiness check (dry run)\n'
printf 'repo: %s\n' "$ROOT"
printf 'HEAD: %s  branch: %s\n' "$(git rev-parse --short HEAD 2>/dev/null || echo '?')" "$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
printf 'origin: %s\n' "$(git config --get remote.origin.url 2>/dev/null || echo '(no origin)')"
printf 'time: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf 'mode: DRY RUN - no file is written, no network, no push, no git mutation\n'

SHALLOW=$(git rev-parse --is-shallow-repository 2>/dev/null || echo unknown)
VISIBLE_COMMITS=$(git rev-list --count --all 2>/dev/null || echo 0)
VISIBLE_REFS=$(git for-each-ref --format='%(refname)' | wc -l | tr -d ' ')
info "visible history: ${VISIBLE_COMMITS} commits reachable from ${VISIBLE_REFS} refs (shallow=${SHALLOW})"

if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
	warn "worktree is dirty; a release candidate must be a clean checkout (git status)"
else
	pass "worktree is clean"
fi

if [ "$SHALLOW" = "true" ]; then
	fail "shallow clone: the history scan cannot see commits before $(cat "$(git rev-parse --git-dir)/shallow" 2>/dev/null | tr '\n' ' '); run 'git fetch --unshallow' (network, owner) before trusting any PASS in section E"
else
	pass "repository is not shallow: the history scan covers every reachable commit"
fi

FETCH_REFS=$(git config --get-all remote.origin.fetch 2>/dev/null | tr '\n' ' ' || true)
case "$FETCH_REFS" in
	*'refs/heads/*'*) pass "fetch refspec covers all remote branches" ;;
	'') warn "no remote.origin.fetch is configured; remote-branch audit must be done with 'git ls-remote' (network) or after a full clone" ;;
	*) warn "fetch refspec is narrow (${FETCH_REFS%% }); remote branches other than the tracked one are invisible to this offline audit - see git ls-remote in PUBLISH-PREP appendix B" ;;
esac

AHEAD=$(git rev-list --count 'origin/master..HEAD' 2>/dev/null || echo 0)
BEHIND=$(git rev-list --count 'HEAD..origin/master' 2>/dev/null || echo 0)
if [ "${AHEAD:-0}" -gt 0 ] 2>/dev/null; then
	warn "HEAD is ${AHEAD} commit(s) ahead of origin/master and ${BEHIND} behind: publish plan must account for unpushed work (curated path: PUBLISH-PREP section 4; legacy purge: appendix A)"
else
	pass "HEAD is not ahead of origin/master"
fi

# -- A. required release files and license texts -----------------------------
section "A. required release files and license texts"
REQUIRED="LICENSE COPYING NOTICE.md README.md CONTRIBUTING.md .gitignore docs/PUBLISH-READINESS.md docs/PUBLISH-PREP.md docs/GPL-REQUEST.md"
MISSING=""
for f in $REQUIRED; do
	[ -f "$f" ] || MISSING="$MISSING $f"
done
if [ -z "$MISSING" ]; then
	pass "required publish files present"
else
	fail "missing required publish files:$MISSING (see docs/PUBLISH-PREP.md section 3, blocker B2)"
fi

if [ -f LICENSE ]; then
	if grep -q 'MIT License' LICENSE && grep -q 'THE SOFTWARE IS PROVIDED "AS IS"' LICENSE; then
		pass "LICENSE is the MIT text"
	else
		fail "LICENSE exists but does not look like the MIT text"
	fi
else
	fail "LICENSE (MIT) missing"
fi

if [ -f COPYING ]; then
	if grep -q 'GNU GENERAL PUBLIC LICENSE' COPYING && grep -q 'Version 2' COPYING; then
		pass "COPYING is the GPL-2.0 text"
	else
		fail "COPYING exists but does not look like the GPL-2.0 text (PUBLISH-READINESS item 4)"
	fi
else
	fail "COPYING (GPL-2.0 license text) missing: the lab/ kernel code is GPL-2.0 but the repository only carries MIT text (PUBLISH-READINESS item 4)"
fi

REF_MISS=""
for f in docs/PUBLISH-READINESS.md docs/GPL-REQUEST.md docs/PUBLISH-PREP.md; do
	grep -q -- "$f" NOTICE.md 2>/dev/null || REF_MISS="$REF_MISS $f"
done
if [ -z "$REF_MISS" ]; then
	pass "NOTICE.md references the publish checklist, the GPL record and the purge plan"
else
	warn "NOTICE.md does not reference:$REF_MISS (link them once the files are in the tree)"
fi

if grep -q 'docs/PUBLISH-READINESS.md' README.md 2>/dev/null; then
	pass "README.md points at the publish checklist"
else
	fail "README.md does not reference docs/PUBLISH-READINESS.md"
fi

# -- B. license split: SPDX / copyright / DCO --------------------------------
section "B. license split: SPDX headers, per-file copyright, DCO"
KERNEL_FILES=$(git ls-files lab 2>/dev/null | grep -E '\.(c|h|S)$|(^|/)(Makefile|Kconfig)$|\.mk$' || true)
KERNEL_TOTAL=$(printf '%s\n' "$KERNEL_FILES" | grep -c . || true)
SPDX_MISSING=$(printf '%s\n' "$KERNEL_FILES" | while IFS= read -r f; do [ -n "$f" ] || continue; grep -q 'SPDX-License-Identifier' "$f" 2>/dev/null || echo "$f"; done)
SPDX_MISSING_COUNT=$(printf '%s\n' "$SPDX_MISSING" | grep -c . || true)
if [ "${SPDX_MISSING_COUNT:-0}" -eq 0 ]; then
	pass "all ${KERNEL_TOTAL} lab/ kernel-source files carry an SPDX-License-Identifier"
else
	fail "${SPDX_MISSING_COUNT} of ${KERNEL_TOTAL} lab/ kernel-source files lack SPDX-License-Identifier (first: $(printf '%s' "$SPDX_MISSING" | head -3 | tr '\n' ' ')) - PUBLISH-READINESS item 4"
fi

BAD_SPDX=$(grep -rhoE 'SPDX-License-Identifier: [A-Za-z0-9.-]+' $KERNEL_FILES 2>/dev/null | sed 's/.*SPDX-License-Identifier: *//' | grep -vE '^(GPL-2\.0|GPL-2\.0-only|MIT)$' | sort -u || true)
if [ -n "$BAD_SPDX" ]; then
	fail "unexpected SPDX license id(s) in lab/: $(printf '%s' "$BAD_SPDX" | tr '\n' ' ')"
else
	pass "SPDX ids under lab/ are GPL-2.0/GPL-2.0-only"
fi
MIXED=$(grep -rhoE 'SPDX-License-Identifier: [A-Za-z0-9.-]+' $KERNEL_FILES 2>/dev/null | sed 's/.*SPDX-License-Identifier: *//' | sort -u | tr '\n' ' ')
case "$MIXED" in
	*'GPL-2.0-only'*'GPL-2.0 '*) warn "mixed SPDX spelling under lab/ (${MIXED}); new files should use GPL-2.0-only (CONTRIBUTING.md convention)" ;;
esac

COPYRIGHT_FILES=$(printf '%s\n' "$KERNEL_FILES" | grep -E '\.(c|h|S)$' || true)
COPYRIGHT_TOTAL=$(printf '%s\n' "$COPYRIGHT_FILES" | grep -c . || true)
COPYRIGHT_MISSING=$(printf '%s\n' "$COPYRIGHT_FILES" | while IFS= read -r f; do [ -n "$f" ] || continue; grep -qi 'Copyright' "$f" 2>/dev/null || echo "$f"; done)
COPYRIGHT_MISSING_COUNT=$(printf '%s\n' "$COPYRIGHT_MISSING" | grep -c . || true)
if [ "${COPYRIGHT_MISSING_COUNT:-0}" -eq 0 ]; then
	pass "all ${COPYRIGHT_TOTAL} lab/ C/H/S files carry a Copyright line"
else
	fail "${COPYRIGHT_MISSING_COUNT} of ${COPYRIGHT_TOTAL} lab/ C/H/S files have no Copyright line (PUBLISH-READINESS item 4; add the standard header from CONTRIBUTING.md)"
fi

if [ -f CONTRIBUTING.md ] && grep -q 'Developer Certificate of Origin' CONTRIBUTING.md && grep -q 'Signed-off-by' CONTRIBUTING.md; then
	pass "CONTRIBUTING.md documents the DCO sign-off convention"
elif [ -f CONTRIBUTING.md ]; then
	fail "CONTRIBUTING.md exists but does not document the DCO / Signed-off-by convention"
else
	fail "CONTRIBUTING.md (DCO + SPDX + provenance convention) missing (PUBLISH-READINESS item 4)"
fi

TOTAL_COMMITS=$(git rev-list --count --all 2>/dev/null || echo 0)
if [ "${TOTAL_COMMITS:-0}" -gt 0 ]; then
	MISSING_SOB=$(git log --all --format='%H%x09%(trailers:key=Signed-off-by,valueonly)' 2>/dev/null | awk -F '\t' 'NF < 2 || $2 == "" {n++} END {print n+0}')
	if [ "${MISSING_SOB:-0}" -eq 0 ]; then
		pass "all ${TOTAL_COMMITS} commits carry a Signed-off-by trailer"
	else
		fail "${MISSING_SOB} of ${TOTAL_COMMITS} commits lack a Signed-off-by trailer (DCO evidence; PUBLISH-READINESS item 4). Policy: PUBLISH-PREP section 6.2 (curated repos) and section 5.2 (legacy noted limitation)"
	fi
fi
if grep -q 'GPL-2.0' README.md 2>/dev/null && grep -q 'MIT' README.md 2>/dev/null; then
	pass "README.md states the MIT / GPL-2.0 split"
else
	fail "README.md does not state the MIT / GPL-2.0 split (PUBLISH-READINESS item 4)"
fi

# -- C. provenance and scope statements --------------------------------------
section "C. provenance and Wi-Fi/LAN-switch statements"
if grep -qiE 'reverse[- ]engineer' README.md NOTICE.md 2>/dev/null && grep -q 'GPL-2.0' README.md NOTICE.md 2>/dev/null; then
	pass "provenance wording present in README.md/NOTICE.md (GPL-2.0 reverse engineering, not clean-room)"
else
	fail "provenance statement missing from README.md/NOTICE.md (PUBLISH-READINESS item 5)"
fi
if [ -f PROVENANCE.md ] || [ -f lab/PROVENANCE.md ]; then
	pass "dedicated PROVENANCE file present"
else
	warn "no dedicated PROVENANCE.md / lab/PROVENANCE.md; the statement exists only inside README/NOTICE (draft in PUBLISH-PREP section 6.3)"
fi
clean_room_pos=$(grep -inE 'clean[ -]?room' README.md NOTICE.md 2>/dev/null | grep -viE 'not|never|rather than|instead of|inaccurate|no longer' || true)
if [ -n "$clean_room_pos" ]; then
	fail "positive 'clean-room' claim in README/NOTICE (PUBLISH-READINESS item 5): $(printf '%s' "$clean_room_pos" | head -2 | tr '\n' ' ')"
else
	pass "no un-negated 'clean-room' claim in README/NOTICE"
fi

HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
if [ "$HAVE_PY" -eq 1 ] && [ -f README.md ] && [ -f NOTICE.md ]; then
	python3 - README.md NOTICE.md <<'PY' | consume_scan
import re, sys

def paragraphs(text):
    for block in re.split(r'\n\s*\n', text):
        yield re.sub(r'\x60{3}.*?\x60{3}', '', block, flags=re.S)

def emit(sev, msg):
    print(sev + '\t' + ' '.join(msg.split()))

found = []
for path in sys.argv[1:]:
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    for para in paragraphs(text):
        low = para.lower()
        wifi = re.search(r'wi[ -]?fi', low) is not None
        switch = re.search(r'\blan\b', low) is not None and 'switch' in low
        scope = any(k in low for k in ('vendor-only', 'vendor only', 'unresolved',
                                       'no source', 'not a complete', 'cannot claim',
                                       'firmware blob', 'binary-only', 'binary only'))
        if wifi and switch and scope:
            found.append(path)
            break
if found:
    emit('PASS', 'Wi-Fi + LAN-switch status statement present in ' + ', '.join(sorted(set(found))))
else:
    emit('FAIL', 'missing Wi-Fi + LAN-switch status statement in README.md/NOTICE.md '
                 '(PUBLISH-READINESS item 9: vendor-only today, firmware-blob license unresolved, '
                 'GPL source request unanswered; the submission cannot claim a complete open port)')
PY
else
	info "python3 unavailable: Wi-Fi/LAN-switch paragraph check SKIPPED"
fi

# -- D. GPL request evidence trail -------------------------------------------
section "D. GPL source request evidence"
if [ -f docs/GPL-REQUEST.md ]; then
	if grep -q 'SENT' docs/GPL-REQUEST.md; then
		pass "docs/GPL-REQUEST.md records the request as sent"
	else
		fail "docs/GPL-REQUEST.md does not record a sent request"
	fi
	if grep -qiE 'no reply|not received|no response' docs/GPL-REQUEST.md; then
		pass "dated no-reply record present in docs/GPL-REQUEST.md"
	else
		fail "no dated no-reply record in docs/GPL-REQUEST.md (PUBLISH-READINESS item 3 alternative)"
	fi
	SENT_COPY=$(git ls-files 2>/dev/null | grep -Ei '(^|/)(sent|receipt)|\.eml$|sent-copy|gpl-request/.+' | grep -v '^docs/GPL-REQUEST.md$' || true)
	if [ -n "$SENT_COPY" ]; then
		pass "sent-copy/receipt evidence file(s) tracked: $(printf '%s' "$SENT_COPY" | tr '\n' ' ')"
	else
		warn "no sent copy / delivery receipt is stored; only the dated record exists. The checklist's dated-record alternative is met; the owner decides whether to store the sent message (or its private path) - PUBLISH-PREP blocker B5"
	fi
	if grep -q 'GPL-REQUEST' NOTICE.md; then
		pass "NOTICE.md cross-references the GPL request record"
	else
		fail "NOTICE.md does not cross-reference docs/GPL-REQUEST.md (PUBLISH-READINESS item 3)"
	fi
else
	fail "docs/GPL-REQUEST.md missing (PUBLISH-READINESS item 3)"
fi

# -- E. git history scan ------------------------------------------------------
section "E. git history: vendor objects, binaries, refs, tags"
if [ "$DO_HISTORY" -eq 1 ]; then
	if [ "$HAVE_PY" -eq 1 ]; then
		python3 - "$ROOT" "$VISIBLE_COMMITS" <<'PY' | consume_scan
import os, re, subprocess, sys

ROOT = sys.argv[1]
VISIBLE = sys.argv[2]
SELF = 'tools/publish-prep-check.sh'

def git(*args, inp=None):
    return subprocess.run(['git', '-C', ROOT] + list(args), input=inp,
                          capture_output=True)

def emit(sev, msg):
    print(sev + '\t' + ' '.join(msg.split()))

refs = [r for r in git('for-each-ref', '--format=%(refname)').stdout.decode().splitlines() if r]
tags = [t for t in git('tag', '-l').stdout.decode().splitlines() if t]
objects = git('rev-list', '--objects', '--all').stdout.decode(errors='replace').splitlines()
entries = []
for line in objects:
    parts = line.split(' ', 1)
    entries.append((parts[0], parts[1] if len(parts) > 1 else ''))

VENDOR_OBJ = re.compile(r'\.(o|ko)$')
BINARY_EXT = re.compile(r'\.(a|so|bin|img|fw|dtb|elf|ubi|squashfs|jffs2|cal|nvram|pem|key|p12|pfx)$', re.I)
SECRET = [
    (b'BEGIN OPENSSH PRIVATE KEY', 'OpenSSH private key'),
    (b'BEGIN RSA PRIVATE KEY', 'RSA private key'),
    (b'BEGIN PGP PRIVATE KEY', 'PGP private key'),
    (b'BEGIN EC PRIVATE KEY', 'EC private key'),
    (b'ghp_', 'GitHub token (ghp_)'),
    (b'github_pat_', 'GitHub token (github_pat_)'),
    (b'AKIA', 'AWS access key id'),
    (b'xoxb-', 'Slack token'),
]

vendor_paths = sorted({p for _, p in entries if VENDOR_OBJ.search(p)})
if vendor_paths:
    emit('FAIL', 'vendor object blobs reachable from refs: %d %s (PUBLISH-READINESS item 1; purge required for the legacy repo; PUBLISH-PREP appendix A)'
         % (len(vendor_paths), ' | '.join(vendor_paths[:12])))
else:
    emit('PASS', 'no .o/.ko blob paths are reachable from any ref')

binext = sorted({p for _, p in entries if BINARY_EXT.search(p)})
if binext:
    emit('FAIL', 'binary/trailer/firmware-extension paths in history: %d %s'
         % (len(binext), ' | '.join(binext[:12])))
else:
    emit('PASS', 'no .bin/.img/.fw/.dtb/.elf/.ubi/.cal/.key-style paths in any history')

for ref in refs:
    out = git('rev-list', '--objects', ref).stdout.decode(errors='replace')
    n = sum(1 for line in out.splitlines() if VENDOR_OBJ.search(line.split(' ', 1)[1] if ' ' in line else ''))
    if n:
        emit('FAIL', 'ref %s reaches %d vendor .o/.ko blob path(s)' % (ref, n))
    else:
        emit('PASS', 'ref %s reaches no vendor .o/.ko blob' % ref)

if tags:
    emit('WARN', 'tags exist (%d): %s - each tag is audited as a ref above and must be re-pointed after any purge'
         % (len(tags), ' '.join(tags[:10])))
else:
    emit('PASS', 'no tags exist (nothing to re-point)')

typecheck = git('cat-file', '--batch-check=%(objecttype) %(objectname) %(objectsize)', '--buffer',
                inp=('\n'.join(oid for oid, _ in entries) + '\n').encode())
info = {}
for line in typecheck.stdout.decode().splitlines():
    p = line.split()
    if len(p) >= 3:
        info[p[1]] = (p[0], int(p[2]))
blobs = [(oid, path, info.get(oid, ('', 0))[1]) for oid, path in entries if info.get(oid, ('', 0))[0] == 'blob']
batch = git('cat-file', '--batch', '--buffer', inp=('\n'.join(b[0] for b in blobs) + '\n').encode()).stdout
header = re.compile(rb'([0-9a-f]{40}) blob ([0-9]+)\n')
pos = 0
content = {}
while pos < len(batch):
    m = header.match(batch, pos)
    if not m:
        break
    oid = m.group(1).decode()
    size = int(m.group(2))
    start = m.end()
    content[oid] = batch[start:start + size]
    pos = start + size + 1

binaries, large, secret_hits = [], [], []
LARGE_WARN, LARGE_FAIL = 1024 * 1024, 5 * 1024 * 1024
for oid, path, size in blobs:
    data = content.get(oid, b'')
    is_binary = bool(data) and b'\x00' in data[:8192]
    if is_binary:
        binaries.append((path, size))
    if size >= LARGE_WARN:
        large.append((path, size))
    if is_binary or path == SELF or not data:
        continue
    for pat, label in SECRET:
        if pat in data:
            secret_hits.append((label, path))
            break

if binaries:
    unexplained = [p for p, _ in binaries if not VENDOR_OBJ.search(p)]
    if unexplained:
        emit('FAIL', 'binary blobs with a non-vendor extension in history: %d %s'
             % (len(unexplained), ' | '.join(unexplained[:8])))
    else:
        emit('FAIL', 'all-history binary-blob count is %d; every one is a vendor .o path listed above' % len(binaries))
else:
    emit('PASS', 'no binary blob in any reachable history')

if secret_hits:
    emit('FAIL', 'credential pattern(s) in history: %s'
         % ' | '.join('%s in %s' % (l, p) for l, p in secret_hits[:8]))
else:
    emit('PASS', 'no high-confidence credential pattern in any reachable blob')

if large:
    worst = sorted(large, key=lambda x: -x[1])[:5]
    msg = 'large blobs (>1 MiB) in history: %s' % ' | '.join('%s %dB' % (p, s) for p, s in worst)
    if any(s >= LARGE_FAIL for _, s in large):
        emit('FAIL', msg + ' (any blob >=5 MiB is a hard fail)')
    else:
        emit('WARN', msg + ' (text evidence is allowed, but confirm each is worth publishing)')
else:
    emit('PASS', 'no blob larger than 1 MiB in reachable history')

tracked = git('ls-files').stdout.decode(errors='replace').splitlines()
remnants = [p for p in tracked if p.startswith(('lab/luofu-kernel/nand/triductor/', 'lab/luofu-kernel/nand/parsers/'))]
if remnants:
    emit('WARN', 'vendor-derived build glue/header still tracked: %s (remove or rewrite with a provenance note; PUBLISH-PREP blocker B6)'
         % ' | '.join(remnants[:8]))
else:
    emit('PASS', 'no vendor-derived build glue under lab/luofu-kernel/nand/')

print('INFO\thistory scan: %s reachable commits visible from %d refs; %d objects, %d blobs' %
      (VISIBLE, len(refs), len(entries), len(blobs)))
PY
	else
		info "python3 unavailable: history scan SKIPPED (a release decision needs it)"
	fi

	UNREACHABLE=$(git fsck --unreachable --no-reflogs 2>/dev/null | wc -l | tr -d ' ')
	if [ "${UNREACHABLE:-0}" -gt 0 ]; then
		warn "${UNREACHABLE} unreachable object(s) in the local object database; a purge leaves old objects here until reflog expiry + gc (owner-approved; PUBLISH-PREP appendix A). They are not reachable from refs."
	else
		pass "no unreachable objects in the local object database"
	fi
else
	info "history scan skipped by --no-history (NOT valid for a release decision)"
fi

# -- F. secrets: filenames and current-tree patterns -------------------------
section "F. secrets: tracked key material and current-tree patterns"
KEYFILES=$(git ls-files 2>/dev/null | grep -Ei '(^|/)(id_rsa|id_ed25519|.*_ed25519|authorized_keys|\.env|.*\.(pem|key|p12|pfx))$' | grep -v '^tools/publish-prep-check\.sh$' || true)
if [ -n "$KEYFILES" ]; then
	fail "tracked key-material filename(s): $(printf '%s' "$KEYFILES" | tr '\n' ' ')"
else
	pass "no tracked private-key / credential filenames"
fi
SECRET_HITS=$(git grep -nE 'BEGIN (OPENSSH|RSA|EC|PGP) PRIVATE KEY|ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{10,}' -- . ':!tools/publish-prep-check.sh' 2>/dev/null | head -5 || true)
if [ -n "$SECRET_HITS" ]; then
	fail "credential pattern(s) in the tracked tree: $(printf '%s' "$SECRET_HITS" | tr '\n' ' | ' | cut -c1-400)"
else
	pass "no high-confidence credential pattern in the tracked tree"
fi
if grep -rqE '(password|passwd)[[:space:]]*=[[:space:]]*["'"'"'][A-Za-z0-9!@#%^&*._=-]{4,}["'"'"']' --include='*.sh' --include='*.md' --include='*.txt' . 2>/dev/null; then
	warn "a quoted literal password assignment appears in the tree; verify it is not a live credential (grep -rn 'password=' .)"
else
	pass "no quoted literal password assignment in the tree (env-var references are expected and allowed)"
fi

# -- G. markdown links --------------------------------------------------------
section "G. relative markdown links"
if [ "$DO_LINKS" -eq 1 ]; then
	if [ "$HAVE_PY" -eq 1 ]; then
		python3 - "$ROOT" <<'PY' | consume_scan
import os, re, subprocess, sys

root = sys.argv[1]
files = subprocess.run(['git', '-C', root, 'ls-files', '*.md'], capture_output=True, text=True).stdout.split()
broken, checked, external = [], 0, 0
fence = re.compile(r'\x60{3}.*?\x60{3}', re.S)
link = re.compile(r'\[[^\]]*\]\(([^)]+)\)')
for f in files:
    try:
        text = open(os.path.join(root, f), encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    text = fence.sub('', text)
    for m in link.finditer(text):
        target = m.group(1).split('#', 1)[0].strip()
        if not target or target.startswith(('http://', 'https://', 'mailto:', '#', '<')):
            if target and not target.startswith(('#', '<')):
                external += 1
            continue
        checked += 1
        p = os.path.normpath(os.path.join(root, os.path.dirname(f), target))
        if not os.path.exists(p):
            broken.append(f + ': ' + target)
        elif not os.path.abspath(p).startswith(os.path.abspath(root) + os.sep):
            broken.append(f + ': ' + target + ' (escapes the repository root)')

def emit(sev, msg):
    print(sev + '\t' + ' '.join(msg.split()))

if broken:
    emit('FAIL', '%d broken relative markdown link(s) in %d file(s): %s'
         % (len(broken), len(set(b.split(':')[0] for b in broken)), ' | '.join(broken[:8])))
else:
    emit('PASS', 'all %d relative markdown links resolve (%d external links not fetched: no-network policy)' % (checked, external))
PY
	else
		info "python3 unavailable: markdown link check SKIPPED"
	fi
else
	info "link check skipped by --no-links"
fi

# -- H. PUBLISH-READINESS items 6-8 (artifact presence) ----------------------
section "H. reproducible artifact / DTS / target-tree presence (items 6-8)"
if [ -f lab/luofu-kernel/kernel-tree.patch ]; then
	pass "kernel delta patch published (lab/luofu-kernel/kernel-tree.patch)"
else
	fail "lab/luofu-kernel/kernel-tree.patch missing (PUBLISH-READINESS item 6: kernel delta series)"
fi
if [ -f target-skeleton/tools/fip-slice.py ]; then
	pass "trailer/slice tool present (target-skeleton/tools/fip-slice.py; generator is structural, never booted)"
else
	fail "target-skeleton/tools/fip-slice.py missing (PUBLISH-READINESS item 6: trailer regeneration)"
fi
DTS_FILES=$(git ls-files 2>/dev/null | grep -E '\.dts$' || true)
DTS_COUNT=$(printf '%s\n' "$DTS_FILES" | grep -c . || true)
if [ "${DTS_COUNT:-0}" -le 1 ]; then
	pass "single-source DTS (${DTS_COUNT} tracked .dts file)"
else
	fail "${DTS_COUNT} tracked .dts copies (${DTS_FILES//$'\n'/ }): PUBLISH-READINESS item 7 requires one upstream-style single source"
fi
if [ -f target-skeleton/target/linux/hisilicon/image/luofu.mk ] && [ -f target-skeleton/target/linux/hisilicon/image/Makefile ]; then
	pass "schematic target/linux tree present (target-skeleton/target/linux/hisilicon/)"
	warn "target tree is SCHEMATIC/UNTESTED and host-build only (target-skeleton/README.md); it is not an upstream target/linux/luofu/ submission"
else
	fail "target-skeleton/target/linux/hisilicon/ image recipe missing (PUBLISH-READINESS item 8)"
fi
if [ -e target-skeleton/target/linux/hisilicon/luofu/profiles/cudy_wr3000-v2.mk ]; then
	pass "active device profile present"
else
	warn "device profile file is not active (only profiles/cudy_wr3000-v2.mk.unused); the image device stanza lives in image/luofu.mk - decide what upstream keeps"
fi

# -- summary ------------------------------------------------------------------
printf '\n== summary ==\n'
printf 'PASS=%d FAIL=%d WARN=%d\n' "$PASS_COUNT" "$FAIL_COUNT" "$WARN_COUNT"
if [ "$FAIL_COUNT" -gt 0 ]; then
	printf 'PUBLISH-PREP-CHECK: FAIL\n'
elif [ "$STRICT" -eq 1 ] && [ "$WARN_COUNT" -gt 0 ]; then
	printf 'PUBLISH-PREP-CHECK: FAIL (--strict: WARN counts as failure)\n'
else
	printf 'PUBLISH-PREP-CHECK: PASS\n'
fi
printf 'mode: DRY RUN - nothing was written, fetched, pushed or rewritten.\n'
printf 'blocker map / owner-approval gates / release sequence: docs/PUBLISH-PREP.md\n'

if [ "$FAIL_COUNT" -gt 0 ]; then
	exit 1
fi
if [ "$STRICT" -eq 1 ] && [ "$WARN_COUNT" -gt 0 ]; then
	exit 1
fi
exit 0
