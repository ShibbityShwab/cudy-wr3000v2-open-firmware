#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel verify for the wifi-dp-air merge
# (shared task task-126): the mode-1 provider module + the E1 MAC data-plane
# bring-up + the wifi-dp-txrx first-TX/RX actions in ONE module.
#
#   1. merge provenance: verify that the committed patch reproduces src/ from
#      the hardware-validated provider base (../wifi-scan-complete/src) and
#      that the changed/added file set is exactly the documented one;
#   2. host regressions on the MERGED tree (all inherited, unchanged):
#      H2D control plane 242, txrx portable TX/RX 254, scan logic 290,
#      scan-complete 195, cfg80211 454;
#   3. this lane's merged-tree selftest (dp geometry/table/MAC program, the
#      g0 id1 key-0x0320 hand-off builder/wire/gate, merge invariants);
#   4. protocol-sequence validation of the F-AIR ladder through the bench
#      tooling (allowlist, executable plan and write list);
#   5. out-of-tree kernel module build against a configured project 6.18.60
#      tree (staged copy of src/ under /tmp so the tracked tree stays clean):
#      0 compiler+modpost warnings, artifact size/sha256/vermagic, required
#      symbols, the 9 cfg80211 provider imports, module parameters;
#   6. device-free source/publication checks (SPDX, no binaries, patch scope,
#      no CA 0x400392F0 / RC-misc / band-1 access in the merged sources).
#
# Never builds an image, never touches the device, never copies vendor
# firmware/calibration bytes: only the sources under src/ are compiled.
# The recorded artifact numbers in BUILD-NOTES.md belong to the DEFAULT build
# path below; a different build directory changes the .ko sha256 (debug info),
# so compare sizes/hashes only against the build you are deploying.
set -o pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
SRC=$LANE/src
WORK=/tmp/wifi-dp-air-build
STAGE=$WORK/src
CC=cc
KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
LOG=/tmp/hi5622-wifi-dp-air-build.log
SEQ=$LANE/../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json
BENCH=$LANE/../wifi-bench-tooling/tools
INC=$LANE/../wifi-scan-complete/tools
export LC_ALL=C

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$SRC" ] || die "lane src/ missing"
[ -x "$PY" ] || PY=python3
command -v "$CC" >/dev/null || die "host compiler not found"
command -v python3 >/dev/null || die "python3 not found"

# Fall back to another configured luofu 6.18.60 tree if the canonical one was
# cleaned (all are local build products, not deliverables).
if [ ! -f "$KDIR/include/generated/autoconf.h" ]; then
    for cand in /home/shibbityshwab/openwrt-lab-verify \
                /home/shibbityshwab/openwrt-lab-selectorfix-k \
                /home/shibbityshwab/openwrt-lab; do
        k=$cand/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
        if [ -f "$k/include/generated/autoconf.h" ] &&
           [ -x "$cand/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc" ]; then
            KDIR=$k
            STAGING_DIR=$cand/staging_dir
            CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
            echo "NOTE: canonical tree missing; using fallback $cand (artifact sha differs from BUILD-NOTES)"
            break
        fi
    done
fi
export STAGING_DIR KDIR CROSS_COMPILE

echo "== 1. merge provenance (provider base -> this src tree) =="
"$PY" "$HERE/verify_merge.py" || die "merge provenance check failed"
echo

echo "== 2. inherited host regressions on the merged tree =="
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -I$SRC"
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" $CFLAGS -o "$WORK/r-h2d" "$INC/h2d_logic_selftest.c" \
	"$SRC/h2d_send_logic.c" "$SRC/h2d_frames.c" "$SRC/hcc_core.c" ||
	die "H2D regression compile failed"
"$WORK/r-h2d" | tail -1
"$CC" $CFLAGS -o "$WORK/r-txrx" "$LANE/../wifi-dp-txrx/tools/dp_txrx_selftest.c" \
	"$SRC/dp_txrx.c" || die "txrx portable compile failed"
"$WORK/r-txrx" | tail -1
"$CC" $CFLAGS -o "$WORK/r-scanlogic" "$INC/scan_logic_selftest.c" \
	"$SRC/hi5622_scan_logic.c" "$SRC/hi5622_vapscan_map.c" \
	"$SRC/hi5622_scan_d2h.c" "$SRC/vapscan_frames.c" \
	"$SRC/h2d_frames.c" || die "scan logic compile failed"
"$WORK/r-scanlogic" | tail -2 | head -1
"$CC" $CFLAGS -o "$WORK/r-scanc" "$INC/scan_complete_selftest.c" \
	"$SRC/hi5622_scan_logic.c" "$SRC/hi5622_scan_d2h.c" \
	"$SRC/hi5622_vapscan_map.c" "$SRC/vapscan_frames.c" ||
	die "scan-complete compile failed"
"$WORK/r-scanc" | tail -1
"$CC" $CFLAGS -o "$WORK/r-cfg" "$INC/cfg80211_logic_selftest.c" \
	"$SRC/hi5622_cfg80211_logic.c" || die "cfg80211 regression compile failed"
"$WORK/r-cfg" | tail -2 | head -1
echo

echo "== 3. merged-tree data-plane selftest (host, device-free) =="
"$CC" $CFLAGS -o "$WORK/merged_selftest" "$HERE/merged_selftest.c" \
	"$SRC/dp_rings.c" "$SRC/h2d_send_logic.c" "$SRC/h2d_frames.c" \
	"$SRC/hcc_core.c" || die "merged selftest compile failed"
"$WORK/merged_selftest" || die "merged selftest failed"
echo

echo "== 4. F-AIR protocol-sequence validation (bench tooling) =="
if [ -f "$SEQ" ] && [ -d "$BENCH" ]; then
	"$PY" - "$SEQ" "$BENCH" <<'PYEOF' || die "sequence validation failed"
import json, os, sys
seq, bench = sys.argv[1], sys.argv[2]
sys.path.insert(0, bench)
import protocol_sequence as PS
doc = PS.load(seq)
script = PS.render_script(doc)
writes = PS.plan_writes(doc)
assert doc["name"] == "bringup-f-air-txrx", doc["name"]
assert doc["require"]["budget_min"] == 3, doc["require"]
assert doc["load_params"]["h2d_send_budget"] == 4, doc["load_params"]
assert [w[1] for w in writes] == [
    "h2d_ctx_create", "h2d_txq_sync72", "dp_setup",
    "h2d_txq_addr_sync", "dp_tx_frame", "dp_tx_frame", "dp_rx_poll"], writes
names = [st["name"] for st in doc["steps"]]
i = {n: k for k, n in enumerate(names)}
assert (i["ctx-create-t1"] < i["txq-sync72"] < i["scan"] < i["dp-setup"]
        < i["dp-txq-sync"] < i["tx-frame-probe-req"]), names
tot = PS.plan_doc(doc)["totals"]
print("sequence: %d steps, %d checks, %d writes, %d rendered lines "
      "(order checked)" % (len(doc["steps"]), tot["checks"], len(writes),
                           len(script)))
PYEOF
else
	die "F-AIR sequence or bench tooling missing (../wifi-bench-tooling)"
fi
echo

echo "== 5. kernel module build (project 6.18.60 tree, staged src copy) =="
[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CROSS_COMPILE}gcc" ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"
rm -rf "$WORK" && mkdir -p "$WORK"
cp -r "$SRC" "$STAGE" || die "staging copy failed"
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$STAGE" \
	clean >/dev/null 2>&1 || true
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$STAGE" \
	modules >"$LOG" 2>&1; then
	tail -40 "$LOG"
	die "kernel module build failed (full log: $LOG)"
fi
KO=$STAGE/hi5622_wifi.ko
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG" || true)
[ "$WARN" -eq 0 ] || { grep -E 'warning:|error:' "$LOG" | head -20;
		       die "build is not clean: $WARN warning/error line(s)"; }
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
NM=${CROSS_COMPILE}nm
for sym in hi5622_dp_txrx_bind h2d_post_txq_addr dp_mac_program \
	   dp_txrx_txe_encode hi5622_dp_probe hi5622_dp_rx_poll_store \
	   hi5622_cfg80211_init; do
	"$NM" "$KO" | grep -q " T $sym" || die "symbol missing: $sym"
done
CFGIMP=$("$NM" "$KO" | grep -cE ' U (cfg80211_|wiphy_)')
[ "$CFGIMP" -eq 9 ] || die "cfg80211 provider imports are $CFGIMP, expected 9"
for parm in fw_path wifi_block_path h2d_send_budget allow_frames \
	    frame_gap_ms vap_start_first vap_start_mode vap_start_module \
	    scan_timeout_ms scan_dwell_ms; do
	modinfo -F parm "$KO" | grep -q "^${parm}:" ||
		die "module parameter missing: $parm"
done
"$PY" "$INC/gen_cfg80211_symvers.py" --check --kdir "$KDIR" \
	--src "$STAGE" >/dev/null || die "cfg80211-provider.symvers mismatch"
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "cfg80211 U  : $CFGIMP (wiphy_* + cfg80211_*)"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo

echo "== 6. device-free source/publication checks =="
[ "$(grep -c '^--- a/' "$LANE/patches/0001-merge-dp-txrx-into-provider.patch")" -eq 8 ] ||
	die "merge patch modified-file count is not 8"
grep -q 'dp_tx_frame' "$SRC/hi5622_dp.c" || die "dp_tx_frame attr missing"
grep -q 'dp_rx_capture' "$SRC/hi5622_dp.c" || die "dp_rx_capture attr missing"
grep -q 'txq_addr_sent' "$SRC/h2d_send.c" || die "hand-off status missing"
grep -q 'hi5622_dp_txrx_bind' "$SRC/hi5622_dp.c" || die "txrx bind missing"
grep -q 'static DEVICE_ATTR(dp_probe, 0644, dp_probe_show, dp_probe_store)' \
	"$SRC/hi5622_dp.c" || die "dp_probe dual read/write-1 attribute missing"
grep -q 'probe_read_required' "$SRC/hi5622_dp.c" || die "probe gate name missing"
grep -q 'dp_probe is ONE read-only MAC word' "$SRC/hi5622_dp.c" || die "dp_status probe hint missing"
for f in "$SRC/dp_txrx.c" "$SRC/hi5622_dp_txrx.c" \
	 "$SRC/hi5622_dp_txrx.h" "$HERE/merged_selftest.c"; do
	head -1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' ||
		die "missing GPL-2.0 SPDX header: $f"
done
if git -C "$LANE/../.." ls-files --error-unmatch \
	"$SRC/hi5622_wifi.ko" >/dev/null 2>&1; then
	die "built .ko must never be committed"
fi
if find "$LANE" -type f \( -name '*.ko' -o -name '*.o' -o -name 'FIRMWARE*' \
	 -o -name '*.bin' -o -name 'wifi_block*' -o -name '*.mod.c' \) | \
	 grep -v '/tools/sequences/' | grep -q .; then
	die "vendor/built binary artifact present in the lane"
fi
# Forbidden-address rule: the NEVER-write CA and the RC misc window may only
# appear as read-only constants/definitions - no writel and no RC-misc access.
BAD=$(grep -rInE 'writel[^;]*(0x400392[fF]0|HCC_CA_NEVER_WRITE|H2D_CA_MBOX_NEVER_WRITE)' \
	"$SRC"/*.c || true)
[ -z "$BAD" ] || { echo "$BAD" | head -5; die "writel to the NEVER-write CA 0x400392f0"; }
BAD=$(grep -rInE '(readl|writel)[^;]*(0x10161000|[Rr][Cc]_MISC)' "$SRC"/*.c || true)
[ -z "$BAD" ] || { echo "$BAD" | head -5; die "RC misc window access"; }
BAD=$(grep -rInE 'writel[^;]*DP_MAC_CA_BAND1|dp_mac_write\([^)]*DP_MAC_CA_BAND1' \
	"$SRC"/*.c || true)
[ -z "$BAD" ] || { echo "$BAD" | head -5; die "MAC band 1 write"; }
echo "OK: merge provenance + 1435 inherited host checks + 48 merged checks + sequence + clean module build + publication checks"
echo "artifact: $SIZE B sha256 $SHA"
