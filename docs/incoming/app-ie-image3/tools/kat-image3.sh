#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/kat-image3.sh - lane app-ie-rebase (task-235) known-answer test for
# the delivered rebased module on the IMAGE #3 module lineage.
#
# Read-only KAT: verifies the delivered artefact (/tmp/app-ie-image3/
# hi5622_wifi.ko) against the committed pins, the source/patch pins, the
# IMAGE #3 base-tree manifest, the integration witnesses inside the module,
# and re-runs the host KAT.  No device, no firmware, no vendor binary.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
ART=${ART:-/tmp/app-ie-image3}
TREE_SRC=${TREE_SRC:-/tmp/wifi-image-3-module}
KO=$ART/hi5622_wifi.ko
TEXT=$ART/.text.bin
fails=0
checks=0
ok() { checks=$((checks+1)); echo "ok   $*"; }
bad() { checks=$((checks+1)); fails=$((fails+1)); echo "FAIL $*" >&2; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

[ -f "$KO" ] || { echo "KAT FAIL: $KO absent (run tools/build-app-ie-image3.sh)" >&2; exit 1; }
[ -f "$TEXT" ] && ok "the .text extract is present" || bad "the .text extract is absent (rebuild with the build script)"

# 1. artefact pins (the fire recipe H0 checks)
[ "$(sha "$KO")" = 2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2 ] && ok "installed sha256 = 2a2fff35" || bad "installed sha256 differs"
[ "$(stat -c %s "$KO")" = 364516 ] && ok "installed size = 364516 B" || bad "installed size differs (want 364516)"
[ "$(modinfo "$KO" | grep -c '^parm:')" = 66 ] && ok "parameter count = 66" || bad "parameter count differs (want 66)"
modinfo -p "$KO" | grep -q '^app_ie: ' && ok "app_ie parameter present" || bad "app_ie parameter missing"
modinfo -p "$KO" | grep -q '^app_ie_type: ' && ok "app_ie_type parameter present" || bad "app_ie_type parameter missing"
[ "$(sha "$TEXT")" = c1ad137974b6866bbc7cdc1971288ade203bf9dfcca6421b73343ef3d50b9417 ] && ok ".text sha256 = c1ad1379" || bad ".text sha256 differs"

# 2. integration witnesses: the rebased step's dmesg format strings
for s in "app_ie: collected %zu B of vendor IE(s) (0xDD) from the cfg80211 AP settings" \
         "start_ap beacon_static: 'app_ie' posted (%zu B, g0 id 0x20" \
         "app_ie post: mctx=0x%08x vap=0x%08x slot(class %u)=vap+0x%03x"; do
	strings "$KO" | grep -qF "$s" && ok "witness string: $(echo "$s" | cut -c1-52)..." || bad "missing integration witness: $s"
done
strings "$KO" | grep -qF "the g0 0x20 step runs after the image #3 'ap_vap_start' step" && ok "image-#3 placement witness string" ||
	bad "missing the image-#3 placement witness string"

# 3. source / patch pins (the rebased integration is the committed one)
[ "$(sha "$LANE/src/hi5622_app_ie.c")" = 19c9cbf7d822a05eac919ac4bf660058b38b86d67b4a912feac0470be5a306ba ] && ok "portable .c = task-234 pin" || bad "portable .c drifted from task-234"
[ "$(sha "$LANE/src/hi5622_app_ie.h")" = e0f3b39f9b15d410ed70a62a7e7977433d3454158457a7b9c7c4e0902929432c ] && ok "portable .h = task-234 pin" || bad "portable .h drifted from task-234"
[ "$(sha "$LANE/patches/0001-app-ie-image3.patch")" = 09523f988b7a0256565278017a4e82687d7dd6363abbccc5722de595550adb7d ] && ok "rebase patch sha256 = 09523f98" || bad "rebase patch sha differs"

# 4. the IMAGE #3 base tree (optional: scratch /tmp tree may be absent)
if [ -d "$TREE_SRC" ]; then
	( cd "$TREE_SRC" && sha256sum -c "$LANE/evidence/base-tree.sha256" >/dev/null 2>&1 ) &&
		ok "IMAGE #3 base-tree manifest (97 files)" || bad "IMAGE #3 base-tree manifest does not verify"
else
	echo "note: base tree $TREE_SRC absent - base manifest check skipped" >&2
fi

# 5. host KAT (the task-234 suite, 55 checks)
out=$("$LANE/tools/run-host-tests.sh" 2>&1) || bad "host KAT failed to run"
echo "$out" | grep -q 'app_ie selftest: 55 checks, 0 failures' && ok "host KAT: 55 checks, 0 failures" || bad "host KAT result differs (want 55 checks, 0 failures)"
echo "$out" | grep -q 'stock-rebuilt (23 B): 00 01 00 04 17 00 20 00 00 00 5a 5a' && ok "host KAT stock-frame vector byte-exact" ||
	bad "host KAT stock-frame vector differs"

echo "KAT: $((checks-fails))/$checks PASS"
[ "$fails" = 0 ] || { echo "KAT: FAIL" >&2; exit 1; }
echo "KAT: PASS"
