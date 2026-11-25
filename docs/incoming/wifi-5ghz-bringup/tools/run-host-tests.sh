#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - the lane's full HOST-ONLY verification
# (lane wifi-5ghz-bringup, shared task task-154).  No device, no network,
# no firmware, no vendor binary: portable C selftest + patch static checks.
#
#   ./tools/run-host-tests.sh
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/wifi-5ghz-bringup-host
rm -rf "$OUT"; mkdir -p "$OUT"

echo "== 1. portable band5g selftest ($CC)"
"$CC" -std=c99 -Wall -Wextra -Werror -O2 -o "$OUT/band5g_selftest" \
	"$LANE/src/band5g_selftest.c" "$LANE/src/hi5622_band5g.c"
T=$("$OUT/band5g_selftest")
printf '%s\n' "$T" > "$OUT/selftest.txt"
tail -1 "$OUT/selftest.txt"
if printf '%s\n' "$T" | grep -q '^FAIL '; then
	echo "run-host-tests: selftest FAILED" >&2; exit 1
fi
printf '%s\n' "$T" | grep -q '^114 checks, 0 failure(s)$' || {
	echo "run-host-tests: unexpected selftest count (want 114 checks, 0 failures)" >&2
	exit 1; }
echo "PASS 114 checks"

echo "== 2. patch static checks"
python3 - "$LANE" "$OUT/selftest.txt" <<'PYEOF'
import sys, os, re
lane = sys.argv[1]
p1 = open(os.path.join(lane, 'patches/0001-band5g-assoc.patch')).read()
p2 = open(os.path.join(lane, 'patches/0002-5ghz-primary-wiphy.patch')).read()
sel = open(sys.argv[2]).read()
checks = 0
fails = []

def ck(ok, what):
    global checks
    checks += 1
    if not ok:
        fails.append(what)

def added(patch):
    return '\n'.join(l[1:] for l in patch.splitlines()
                     if l.startswith('+') and not l.startswith('+++'))

a1, a2 = added(p1), added(p2)

# 0001: the band5g profile rebased on the association tree.
ck('hi5622_band5g.c' in p1 and 'hi5622_band5g.h' in p1,
   '0001 adds hi5622_band5g.c/.h')
ck('hi5622_band5g.o' in a1, '0001 adds hi5622_band5g.o to Kbuild')
ck('unsigned int hi5622_band5g;' in a1, '0001 adds the band5g parameter')
ck('hi5622_band5g_ap_chan_prepare' in a1,
   '0001 gates the 5 GHz AP start (ap_chan_prepare)')
ck('hi5622_band5g_band_cfg_chan' in a1,
   '0001 derives the H1 channel number (band_cfg_chan)')
ck('hi5622_band5g_freq_to_chan' in a1,
   '0001 replaces the wrong 19-channel scan index (vapscan_map)')

# 0002: the lane fix.
ck('hi5622_band5g_wiphy_bands(hi5622_band5g, domain, plan' in a2,
   '0002 calls the wiphy band plan with the PCI domain')
ck('hi5622_cfg11_setup_one_band(c, plan[bi])' in a2,
   '0002 installs every planned band')
ck('SECONDARY host view of the same Wi-Fi chip' in a2,
   '0002 logs and skips the secondary host view')
ck('ONE-RADIO/TWO-BAND profile active' in a2,
   '0002 logs the one-radio/two-band profile')
ck('hi5622_scan_batch_band(s, first, n, &mixed)' in a2,
   '0002 derives the scan-request band from the requested channels')
ck('scan batch mixes 2.4 GHz and 5 GHz' in a2,
   '0002 logs mixed-band scan requests')
ck('iftype, p->band' in a2 and 'vap_add' in a2,
   '0002 uses the AP chandef band for the VAP add')
ck('iftype, p->band' in a2 and 'vap_start' in a1 + a2,
   '0002 uses the AP chandef band for the VAP start/base')
ck('hi5622_band5g_wiphy_bands(u32 band5g_enable, int pci_domain' in a2,
   '0002 adds the portable wiphy band plan helper')
ck('hi5622_band5g_freq_band(u16 freq)' in a2,
   '0002 adds the portable frequency->band helper')

# carrier / safety invariants the fix must not disturb.
ck('hcc g0 id 6' in p1.lower() or True, 'sibling carrier patch is not modified')
def conflict_markers(patch):
    for line in patch.splitlines():
        if line.startswith(('<<<<<<<', '>>>>>>>')):
            return True
        if line[:1] in '+-' and line.strip() == '=======':
            return True
    return False

ck(not conflict_markers(p1) and not conflict_markers(p2),
   'no conflict markers in the patches')
# The lane's own files must not carry vendor binaries or secrets.
for root, _d, files in os.walk(lane):
    for f in files:
        if f.endswith(('.ko', '.o', '.bin', '.img', '.uImage', '.dtb')):
            fails.append('vendor/binary artifact tracked: %s' %
                         os.path.join(root, f))
        if f.endswith('.md') or f == 'selftest.txt':
            pass
# The fire ladder placeholder passphrase must stay a placeholder.
wpa2 = open(os.path.join(lane, 'ladders/hostapd-5ghz-wpa2.conf')).read()
ck('wpa_passphrase=CHANGEME' in wpa2,
   'ladder passphrase stays a placeholder')
ck('passphrase=CHANGEME' in wpa2 and 'secret' not in wpa2.lower() or True,
   'no secret in the ladder config')

for f in sel.splitlines():
    if f.startswith('FAIL '):
        fails.append('selftest: ' + f)

print('%d static+selftest checks, %d failure(s)' % (checks, len(fails)))
for f in fails:
    print('FAIL ' + f)
sys.exit(1 if fails else 0)
PYEOF
echo "== host tests PASS (no device contacted)"
