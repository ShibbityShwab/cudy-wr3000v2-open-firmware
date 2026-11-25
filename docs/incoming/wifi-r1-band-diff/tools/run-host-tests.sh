#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-r1-band-diff (task-183) host test.
#
# HOST-ONLY: stages the pinned beacon-lane base source, applies
# patches/0001-vap-start-band.patch to a scratch copy, and runs the model
# against both the patched source and the unpatched base (negative control).
# No device contact, no vendor bytes.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
set +u
if [ -z "$BASE_TREE" ]; then BASE_TREE=/tmp/beacon5g-build; fi
set -u
WORK=/tmp/r1-band-hosttest
P=bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828
[ -d "$BASE_TREE" ] || { echo "r1-band-diff: base tree missing: $BASE_TREE" >&2; exit 2; }
got=$(sha256sum "$BASE_TREE/hi5622_vapscan_provider.c" | cut -d' ' -f1)
[ "$got" = "$P" ] || {
	echo "r1-band-diff: base provider drifted ($got != $P)" >&2; exit 1; }
rm -rf "$WORK"
mkdir -p "$WORK" "$LANE/evidence"
cp "$BASE_TREE/hi5622_vapscan_provider.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-vap-start-band.patch" >/dev/null )
echo "== staged: base provider + patches/0001-vap-start-band.patch"
python3 "$LANE/tools/r1_band_model.py" --source "$WORK/hi5622_vapscan_provider.c" --base-source "$BASE_TREE/hi5622_vapscan_provider.c" | tee "$LANE/evidence/host-test.txt"
