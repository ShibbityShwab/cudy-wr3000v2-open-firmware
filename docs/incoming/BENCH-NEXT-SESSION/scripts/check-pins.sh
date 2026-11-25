#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# check-pins.sh - host-only artifact pin checker for the BENCH-NEXT-SESSION runbook.
#
# Lane: bench-runbook (task-173). HOST-ONLY: this script reads local files and prints;
# it never contacts the router, UART, TFTP or the bench link.
#
# It verifies that every local (never-committed) artifact the next attended session needs
# is present with the pinned size/sha256, and prints the exact rebuild recipe for anything
# missing. A module that has not been built yet is reported MISSING (rebuild hint) or
# PENDING (its sharing lane has not delivered the final pin) - neither is a crash.
#
# Usage:
#   ./scripts/check-pins.sh              # report only; exit 0 unless a present file is WRONG
#   ./scripts/check-pins.sh --strict     # exit 1 while any pin is MISSING or PENDING
#
# Pins are the ones in README.md section 11; the per-lane deliverable stays authoritative.
set -eu

LANE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)

STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1

OK=0; MISSING=0; WRONG=0; PENDING=0

say() { printf '%s\n' "$*"; }
hr()  { say "-------------------------------------------------------------------------------"; }

# check <name> <path> <size-bytes> <sha256> <rebuild-hint>
check() {
	name=$1; path=$2; size=$3; sha=$4; hint=$5
	if [ ! -f "$path" ]; then
		printf '%-30s MISSING   %s\n' "$name" "$path"
		printf '%-30s           rebuild: %s\n' "" "$hint"
		MISSING=$((MISSING + 1))
		return 0
	fi
	asize=$(wc -c < "$path" | tr -d ' ')
	asha=$(sha256sum "$path" | cut -d' ' -f1)
	if [ "$asize" = "$size" ] && [ "$asha" = "$sha" ]; then
		printf '%-30s OK        %s  (%s B)\n' "$name" "$sha" "$asize"
		OK=$((OK + 1))
	else
		printf '%-30s WRONG     %s\n' "$name" "$path"
		printf '%-30s           size %s B (want %s B)\n' "" "$asize" "$size"
		printf '%-30s           sha  %s\n' "" "$asha"
		printf '%-30s           want %s\n' "" "$sha"
		WRONG=$((WRONG + 1))
	fi
}

# pending <name> <hint>
pending() {
	printf '%-30s PENDING   %s\n' "$1" "$2"
	PENDING=$((PENDING + 1))
}

# exists <name> <path> [kind]
exists() {
	if [ -e "$2" ]; then
		printf '%-30s OK        %s\n' "$1" "$2"
		OK=$((OK + 1))
	else
		printf '%-30s MISSING   %s\n' "$1" "$2"
		MISSING=$((MISSING + 1))
	fi
}

say "BENCH-NEXT-SESSION pin check (host-only; no device contact)"
say "repo root : $ROOT"
hr
say "1. bench tooling + lane deliverables (text, committed)"
exists "bench-link"            "$ROOT/tools/bench-link.sh"
exists "recovery-card"         "$ROOT/tools/recovery-card.sh"
exists "uart-capture"          "$ROOT/tools/uart-capture.sh"
exists "bench-teardown"        "$ROOT/tools/bench-teardown.sh"
exists "stock-provision"       "$ROOT/tools/box-provision.sh"
exists "stock-telnet-install"  "$ROOT/tools/stock-telnet-install.sh"
exists "rx-init deliverable"   "$DOCS/wifi-rx-init-msg/FIRE-RECIPE.md"
exists "image-2 flash doc"     "$DOCS/wifi-final-image-2/FLASH-FEASIBILITY.md"
exists "5GHz fire recipe"      "$DOCS/wifi-5ghz-bringup/FIRE-RECIPE.md"
exists "STA test ladder"       "$DOCS/wifi-sta-mode/TEST-LADDER.md"
exists "assoc RUNBOOK"         "$DOCS/wifi-assoc-e2e/RUNBOOK.md"
exists "assoc client harness"  "$DOCS/wifi-assoc-e2e/scripts/assoc_e2e.py"
exists "assoc router script"   "$DOCS/wifi-assoc-e2e/scripts/router/assoc-router-fire.sh"
hr
say "2. local artifacts: final image #2 (P2)"
IMG2=/home/shibbityshwab/wifi-final-image-2-artifacts
check "image-2 container" "$IMG2/final-image-2-sysupgrade.bin" 7188799 \
	8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8 \
	"re-bake: $DOCS/wifi-final-image-2/tools/bake-final-image-2.sh"
check "image-2 rootfs UBI" "$IMG2/rootfs.ubi" 3801088 \
	31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d \
	"tar -xOf <container> sysupgrade-cudy_wr3000-v2/root > rootfs.ubi"
check "image-2 kernel slice" "$IMG2/kernel-slice.bin" 3372638 \
	13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f \
	"image bake (kernel unchanged)"
hr
say "3. local artifacts: firmware + PHY block (P0/P1/P3/P4/P5)"
PY="$ROOT/.provision-venv/bin/python"
check "PD4 firmware" "/tmp/hi5622-pd4/FIRMWARE.PD4.bin" 928920 \
	cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1 \
	"$PY $DOCS/wifi-irq-unmask/tools/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4"
check "Wi-Fi PHY block" "/tmp/blocks/wifi_block.bin" 2472 \
	0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32 \
	"$PY $DOCS/wifi-driver-clean/tools/mkblocks.py --wifi-capture <local 618-word capture> --outdir /tmp/blocks"
hr
say "4. session modules (local builds; one per phase)"
check "5GHz module (P3)" "/tmp/wifi-5ghz-ready-artifact/hi5622_wifi.ko" 351008 \
	1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53 \
	"cd $DOCS/wifi-5ghz-bringup && ./tools/build-5ghz-ready.sh"
check "STA-mode module (P4)" "/tmp/wifi-sta-mode-artifact/hi5622_wifi.ko" 364572 \
	8c0adf93995fd5014b73107de7fed77c825e3eee88fec8cc7a1c284f501c2059 \
	"cd $DOCS/wifi-sta-mode && ./tools/build-sta-mode-module.sh"
check "association artifact (P5 alt)" "/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko" 347388 \
	d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6 \
	"cd $DOCS/wifi-rx-watermark && REBUILD_ASSOC=1 ./tools/build-final-module.sh"
pending "RX-init module (P1)" "PIN PENDING after the Lead builds it from the fired association tree + $DOCS/wifi-rx-init-msg/patches/0001-rx-init-msg.patch (FIRE-RECIPE sec. 0 port note); module sha/size go into README sec. 4.1"
hr
say "5. on-box checks after the P0 push (run these manually; this script never contacts the box)"
say "   tools/rsh.sh 'sha256sum /lib/firmware/hi_wifi/FIRMWARE.PD4.bin /lib/firmware/hi_wifi/wifi_block.bin'"
say "   tools/rsh.sh 'sha256sum /root/hi5622_<phase>.ko; modinfo /root/hi5622_<phase>.ko | grep -c ^parm:'"
hr
printf 'pins: %s OK, %s MISSING, %s WRONG, %s PENDING\n' "$OK" "$MISSING" "$WRONG" "$PENDING"

if [ "$WRONG" -gt 0 ]; then
	say "RESULT: FAIL - a present artifact does not match its pin; do not fire it."
	exit 1
fi
if [ "$STRICT" -eq 1 ] && { [ "$MISSING" -gt 0 ] || [ "$PENDING" -gt 0 ]; }; then
	say "RESULT: PENDING - missing/pending pins exist (strict mode)."
	exit 1
fi
if [ "$MISSING" -gt 0 ] || [ "$PENDING" -gt 0 ]; then
	say "RESULT: PRESENT-PARTIAL - present artifacts are correct; missing/pending pins must be prepared before the session (see hints above)."
	exit 0
fi
say "RESULT: PASS - every pinned artifact is present and correct."
exit 0
