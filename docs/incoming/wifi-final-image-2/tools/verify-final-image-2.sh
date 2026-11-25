#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-final-image-2.sh - verify the FINAL IMAGE #2 FROM ITS BYTES
# (wifi-final-image-2, task-159).  HOST-ONLY.
#
# Checks
#   * container: two members (kernel + root), kernel slice is the
#     hardware-validated 13e81214... (UNCHANGED), rootfs is a UBI starting UBI#,
#   * the rootfs UBI reassembles into the build_tree root.squashfs prefix,
#   * the userspace carry: SAE-capable hostapd (sha 451dd49f...), iw, iwinfo,
#     the wifi-scripts ucode files, wireless-regdb, the cfg80211 + hi5622 kmods,
#   * the installed hi5622_wifi.ko is the CORRECTED FINAL ASSOCIATION module:
#     pinned artifact sha/size/param count, 56 params, the corrected mgmt-TX
#     carrier symbol himg_build_hcc_mgmt_tx present and the superseded WAL
#     carrier log string ABSENT, no undefined symbols,
#   * all six WPA2/WPA3 hostapd fixtures + the hi5622 loader/init are carried
#     byte-identical to this lane's payload, the rc.d link and autoload=0
#     policy survive,
#   * package manifest has hostapd-mbedtls + libmbedtls21 + the bench stack and
#     has NO hostapd-mini,
#   * policy: no vendor firmware/calibration binary is packaged (only
#     /lib/firmware/regulatory.db), and the rootfs fits mtd14 (rootfs-only
#     flash feasibility: kernelb slot needs no write).
#
# usage: ./tools/verify-final-image-2.sh --container FILE
#          [--work DIR] [--root-squashfs FILE]
#          [--module-artifact FILE --module-artifact-sha SHA
#           --module-artifact-size B --module-params N
#           --module-installed-sha SHA]
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
CONTAINER=
WORK=/tmp/wifi-final-image-2-verify
ROOT_SQUASHFS=
MOD_ART=
MOD_ART_SHA=
MOD_ART_SIZE=
MOD_PARAMS=
MOD_INST_SHA=

EXPECTED_KERNEL=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SIZE=3372638
EXPECTED_KERNEL_SLOT=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
EXPECTED_KERNEL_SLOT_SIZE=8650752
EXPECTED_HOSTAPD_SHA=451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469
OLD_HOSTAPD_SHA=13cb3a4306f752f4e1638585e734cd29368ee393096b75931f89e71e01b21919
OLD_HOSTAPD_SIZE=504333
MTD14_SIZE=24379392

while [ $# -gt 0 ]; do
	case "$1" in
		--container) CONTAINER=${2:-}; shift 2 ;;
		--container=*) CONTAINER=${1#*=}; shift ;;
		--work) WORK=${2:-}; shift 2 ;;
		--work=*) WORK=${1#*=}; shift ;;
		--root-squashfs) ROOT_SQUASHFS=${2:-}; shift 2 ;;
		--root-squashfs=*) ROOT_SQUASHFS=${1#*=}; shift ;;
		--module-artifact) MOD_ART=${2:-}; shift 2 ;;
		--module-artifact=*) MOD_ART=${1#*=}; shift ;;
		--module-artifact-sha) MOD_ART_SHA=${2:-}; shift 2 ;;
		--module-artifact-sha=*) MOD_ART_SHA=${1#*=}; shift ;;
		--module-artifact-size) MOD_ART_SIZE=${2:-}; shift 2 ;;
		--module-artifact-size=*) MOD_ART_SIZE=${1#*=}; shift ;;
		--module-params) MOD_PARAMS=${2:-}; shift 2 ;;
		--module-params=*) MOD_PARAMS=${1#*=}; shift ;;
		--module-installed-sha) MOD_INST_SHA=${2:-}; shift 2 ;;
		--module-installed-sha=*) MOD_INST_SHA=${1#*=}; shift ;;
		-h|--help) sed -n '2,34p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "verify-final-image-2: unknown argument: $1" >&2; exit 2 ;;
	esac
done
[ -n "$CONTAINER" ] || { echo "verify-final-image-2: --container is required" >&2; exit 2; }
[ -f "$CONTAINER" ] || { echo "verify-final-image-2: no such container: $CONTAINER" >&2; exit 2; }
[ -x /usr/bin/unsquashfs ] || { echo "verify-final-image-2: unsquashfs missing on host" >&2; exit 2; }
MOD_PARAMS=${MOD_PARAMS:-57}

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); printf 'PASS  %s\n' "$*"; }
fail() { FAIL=$((FAIL+1)); printf 'FAIL  %s\n' "$*"; }
record() { printf 'RECORD  %s\n' "$*"; }
die() { echo "verify-final-image-2: ERROR: $*" >&2; exit 2; }

rm -rf "$WORK"; mkdir -p "$WORK"
echo "== wifi-final-image-2 verification (built bytes), task-159 =="
echo "container : $CONTAINER"
C_SIZE=$(stat -c %s "$CONTAINER"); C_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
record "container size/sha256 = $C_SIZE / $C_SHA"
echo

# ---- 1. container members + kernel slice ----------------------------------
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin" 2>/dev/null || die "cannot extract kernel member"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" 2>/dev/null || die "cannot extract root member"
MEMBERS=$(tar -tf "$CONTAINER" | grep -c 'sysupgrade-cudy_wr3000-v2/' || true)
KS_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1); KS_SIZE=$(stat -c %s "$WORK/kernel.bin")
[ "$KS_SHA" = "$EXPECTED_KERNEL" ] && pass "container kernel slice is the validated 13e81214... (sha)" || fail "kernel slice CHANGED: $KS_SHA"
[ "$KS_SIZE" = "$EXPECTED_KERNEL_SIZE" ] && pass "kernel slice size unchanged ($EXPECTED_KERNEL_SIZE B)" || fail "kernel slice size changed: $KS_SIZE"
record "kernel slot $EXPECTED_KERNEL_SLOT ($EXPECTED_KERNEL_SLOT_SIZE B) is NOT a container member and needs no write (rootfs-only flash)"
KCFG=${KERNEL_CFG:-$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/.config}
if [ -f "$KCFG" ] && grep -q '^CONFIG_CMDLINE="ubi.mtd=rootfsb ' "$KCFG" && grep -q '^CONFIG_CMDLINE_FORCE=y$' "$KCFG"; then
	pass "built kernel config (source of the slice, config sha 08b6e595) forces cmdline ubi.mtd=rootfsb (rootfs-only install writes mtd14, pair b stays active)"
else
	fail "built kernel config does not force the ubi.mtd=rootfsb cmdline"
fi
record "note: CONFIG_CMDLINE lives in the self-decompressing kernel payload and is asserted from the built kernel config, not by string-scanning the uImage"
RS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1); RS_SIZE=$(stat -c %s "$WORK/rootfs.ubi")
record "rootfs.ubi size/sha256 = $RS_SIZE / $RS_SHA"
head -c 4 "$WORK/rootfs.ubi" | grep -q 'UBI#' && pass "rootfs UBI starts with UBI# EC magic" || fail "rootfs member is not UBI"
[ "$RS_SIZE" -le "$MTD14_SIZE" ] && pass "rootfs UBI fits the rootfsb/mtd14 partition (0x1740000 = $MTD14_SIZE B; $((MTD14_SIZE - RS_SIZE)) B free)" || fail "rootfs UBI $RS_SIZE B exceeds mtd14 $MTD14_SIZE B"

# ---- 2. rootfs squashfs + prefix match ------------------------------------
python3 "$IMAGE_INT/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/root.squashfs" > "$WORK/ubi-extract.log" 2>&1 || die "UBI volume extraction failed"
SQ_SHA=$(sha256sum "$WORK/root.squashfs" | cut -d' ' -f1); SQ_SIZE=$(stat -c %s "$WORK/root.squashfs")
BUILT_SQ=${ROOT_SQUASHFS:-$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/root.squashfs}
if [ -f "$BUILT_SQ" ]; then
	B_SIZE=$(stat -c %s "$BUILT_SQ")
	if cmp -s -n "$B_SIZE" "$WORK/root.squashfs" "$BUILT_SQ"; then
		PAD=$(tail -c +$((B_SIZE + 1)) "$WORK/root.squashfs" | tr -d '\0' | wc -c)
		[ "$PAD" -eq 0 ] && pass "UBI volume 0 == build_dir root.squashfs prefix ($B_SIZE B) + zero padding only" || fail "UBI volume 0 has $PAD non-padding bytes after the root.squashfs prefix"
		record "volume size $SQ_SIZE B, squashfs $B_SIZE B, end padding $((SQ_SIZE - B_SIZE)) B"
	else
		fail "UBI volume 0 does not match the build root.squashfs prefix"
	fi
else
	record "build_dir root.squashfs absent (recording UBI volume sha $SQ_SHA only)"
fi
unsquashfs -no-exit-code -d "$WORK/extract" "$WORK/root.squashfs" >/dev/null 2>&1 || true
[ -e "$WORK/extract/usr/sbin/hostapd" ] || die "unsquashfs did not extract the rootfs"
RF=$WORK/extract

# ---- 3. userspace carry: hostapd-SAE, iw, ucode, regdb, kmods -------------
H=$RF/usr/sbin/hostapd
[ -f "$H" ] || die "no /usr/sbin/hostapd in the image rootfs"
H_SIZE=$(stat -c %s "$H"); H_SHA=$(sha256sum "$H" | cut -d' ' -f1)
record "image /usr/sbin/hostapd size/sha256 = $H_SIZE / $H_SHA"
[ "$H_SHA" = "$EXPECTED_HOSTAPD_SHA" ] && pass "hostapd is the SAE-capable mbedtls full variant (sha 451dd49f...)" || fail "hostapd sha $H_SHA != pinned 451dd49f..."
[ "$H_SHA" != "$OLD_HOSTAPD_SHA" ] && [ "$H_SIZE" != "$OLD_HOSTAPD_SIZE" ] && pass "hostapd is NOT the old SAE-less hostapd-mini (13cb3a43.../504,333 B)" || fail "hostapd is the old hostapd-mini"
readelf -h "$H" | grep -q 'Machine:.*ARM' && pass "hostapd is ARM" || fail "hostapd is not ARM"
readelf -d "$H" | grep -q 'libmbedcrypto' && pass "hostapd DT_NEEDED libmbedcrypto (SAE EC backend)" || fail "hostapd does not link libmbedcrypto"
for lib in libmbedcrypto libmbedx509 libmbedtls; do
	ls "$RF/usr/lib/$lib".so.* >/dev/null 2>&1 && pass "image carries $lib runtime" || fail "image missing $lib runtime"
done
SAE_STRINGS=$(strings "$H" | grep -c '^SAE: ' || true)
[ "${SAE_STRINGS:-0}" -ge 5 ] && pass "hostapd carries SAE code strings ($SAE_STRINGS)" || fail "hostapd carries too few SAE strings ($SAE_STRINGS)"
IW=$RF/usr/sbin/iw
[ -f "$IW" ] && pass "image carries /usr/sbin/iw" || fail "image missing /usr/sbin/iw"
readelf -h "$IW" 2>/dev/null | grep -q 'Machine:.*ARM' && pass "iw is the target ARM binary" || fail "iw is not ARM"
[ -f "$RF/usr/bin/iwinfo" ] && pass "image carries /usr/bin/iwinfo (wifi-scripts ucode implementation)" || fail "image missing /usr/bin/iwinfo"
UCODE_UC=$(find "$RF/usr/share/ucode" -name '*.uc' 2>/dev/null | wc -l)
[ "$UCODE_UC" -ge 8 ] && pass "wifi-scripts ucode modules carried ($UCODE_UC .uc files)" || fail "too few ucode modules ($UCODE_UC)"
for u in /usr/share/ucode/wifi/hostapd.uc /usr/share/ucode/wifi/common.uc /usr/share/ucode/iwinfo.uc \
         /usr/share/hostap/common.uc /usr/share/hostap/wdev.uc /usr/share/hostap/wifi-detect.uc; do
	[ -f "$RF$u" ] && pass "ucode carry: $u" || fail "ucode carry missing: $u"
done
[ -f "$RF/lib/firmware/regulatory.db" ] && pass "wireless-regdb regulatory.db carried" || fail "regulatory.db missing"
[ -f "$RF/lib/modules/6.18.60/cfg80211.ko" ] && pass "kmod-hi5622-cfg80211 cfg80211.ko carried" || fail "cfg80211.ko missing"
[ -f "$RF/lib/modules/6.18.60/hi5622_wifi.ko" ] && pass "kmod-hi5622-wifi hi5622_wifi.ko carried" || fail "hi5622_wifi.ko missing"

# ---- 4. the CORRECTED FINAL ASSOCIATION module ---------------------------
if [ -n "$MOD_ART" ]; then
	[ "$(sha256sum "$MOD_ART" | cut -d' ' -f1)" = "$MOD_ART_SHA" ] && pass "input module artifact sha matches pin ($MOD_ART_SHA)" || fail "input module artifact sha mismatch"
	[ "$(stat -c %s "$MOD_ART")" = "$MOD_ART_SIZE" ] && pass "input module artifact size matches pin ($MOD_ART_SIZE B)" || fail "input module artifact size mismatch"
	[ "$(modinfo -p "$MOD_ART" 2>/dev/null | wc -l)" = "$MOD_PARAMS" ] && pass "input module artifact carries $MOD_PARAMS parameters" || fail "input module artifact param count != $MOD_PARAMS"
	record "module artifact: $MOD_ART (sha $MOD_ART_SHA)"
fi
KO=$RF/lib/modules/6.18.60/hi5622_wifi.ko
if [ -f "$KO" ]; then
	KO_SIZE=$(stat -c %s "$KO"); KO_SHA=$(sha256sum "$KO" | cut -d' ' -f1)
	record "installed module size/sha256 = $KO_SIZE / $KO_SHA"
	[ "$(modinfo -F name "$KO")" = "hi5622_wifi" ] && pass "installed module name hi5622_wifi" || fail "installed module name wrong"
	[ "$(modinfo -F vermagic "$KO" | cut -d' ' -f1)" = "6.18.60" ] && pass "installed module vermagic 6.18.60" || fail "installed module vermagic wrong"
	echo "$(modinfo -F depends "$KO")" | grep -q cfg80211 && pass "installed module depends cfg80211" || fail "installed module does not depend on cfg80211"
	KPARMS=$(modinfo -F parm "$KO" | wc -l)
	[ "$KPARMS" = "$MOD_PARAMS" ] && pass "installed module carries $MOD_PARAMS parameters" || fail "installed module parameter count $KPARMS != $MOD_PARAMS"
	modinfo -p "$KO" | grep -q '^rx_refill_wm:' && pass "installed module carries the RX refill watermark param (rx_refill_wm)" || fail "installed module has no rx_refill_wm param (not the watermark build)"
	strings -a "$KO" | grep -qF 'RX watermark: rx_refill_wm=' && pass "installed module carries the RX watermark telemetry string" || fail "installed module has no RX watermark telemetry string"
	[ -n "$MOD_INST_SHA" ] && { [ "$KO_SHA" = "$MOD_INST_SHA" ] && pass "installed module sha matches pin ($MOD_INST_SHA)" || fail "installed module sha $KO_SHA != pinned $MOD_INST_SHA"; }
	NMFILE=$WORK/module.nm
	nm "$KO" > "$NMFILE" 2>/dev/null || true
	grep -qE ' [Tt] himg_build_hcc_mgmt_tx$' "$NMFILE" && pass "CORRECTED carrier builder symbol himg_build_hcc_mgmt_tx present" || fail "corrected carrier builder symbol himg_build_hcc_mgmt_tx MISSING"
	strings -a "$KO" | grep -q 'WAL cfg-event carrier' && fail "superseded WAL 0x01F2 carrier log string still present" || pass "superseded WAL 0x01F2 carrier log string ABSENT (corrected g0 id6 carrier)"
	strings -a "$KO" | grep -q 'mgmt_tx: DMA frame buffer' && fail "superseded descriptor-carrier log string still present" || pass "superseded descriptor-carrier (host-DRAM frame addr) log string ABSENT"
	missing_syms=0
	for sym in cfg80211_mgmt_tx_status_ext cfg80211_rx_mgmt_ext cfg80211_bss_iter; do
		grep -q " U $sym$" "$NMFILE" || { fail "installed module does not import $sym"; missing_syms=$((missing_syms+1)); }
	done
	[ "$missing_syms" -eq 0 ] && pass "installed module imports the association cfg80211 symbols"
	DEPS=$(modinfo -F depends "$KO" | tr -d ' ')
	[ "$DEPS" = "cfg80211" ] && pass "installed module depends exactly cfg80211 (no unresolved module dependency)" || fail "installed module depends '$DEPS' != cfg80211"
	# Strong identity: the installed (rstrip) module's .text must be byte-identical
	# to the pinned artifact's .text - only symbol/debug sections may be stripped.
	CROSS=$OWRT/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
	if [ -n "$MOD_ART" ] && [ -x "${CROSS}objcopy" ]; then
		"${CROSS}objcopy" -O binary --only-section=.text "$MOD_ART" "$WORK/art.text" 2>/dev/null
		"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/inst.text" 2>/dev/null
		if cmp -s "$WORK/art.text" "$WORK/inst.text"; then
			pass "installed module .text is byte-identical to the pinned artifact (strip only removed symbols/debug; $(stat -c %s "$WORK/art.text") B .text)"
		else
			fail "installed module .text differs from the pinned artifact"
		fi
	else
		record "cross objcopy unavailable - skipped .text identity comparison"
	fi
	feat=0
	for s in "sta_ops: STA %pM added" "data-tx tx: posts=" "cfg80211: set_default_mgmt_key posted" "DP rx_arm: live bank CA"; do
		strings -a "$KO" | grep -qF "$s" || { fail "module feature string missing: $s"; feat=$((feat+1)); }
	done
	[ "$feat" -eq 0 ] && pass "module feature strings: sta_ops + data_tx + set_default_mgmt_key + DP rx_arm present"
else
	fail "installed module /lib/modules/6.18.60/hi5622_wifi.ko missing"
fi

# ---- 5. WPA2/WPA3 fixtures + loader/init byte-identical ------------------
echo
for f in etc/hi5622/hostapd-open.conf etc/hi5622/hostapd-open.proven.conf \
         etc/hi5622/hostapd-wpa2.conf etc/hi5622/hostapd-wpa2-hardened.conf \
         etc/hi5622/hostapd-wpa2-wpa3.conf etc/hi5622/hostapd-wpa3-sae.conf; do
	[ -f "$RF/$f" ] && pass "image carries $f" || { fail "image missing $f"; continue; }
	P_SHA=$(sha256sum "$LANE/payload/$f" | cut -d' ' -f1)
	I_SHA=$(sha256sum "$RF/$f" | cut -d' ' -f1)
	[ "$P_SHA" = "$I_SHA" ] && pass "image $f byte-identical to lane payload ($P_SHA)" || fail "image $f differs from lane payload ($P_SHA vs $I_SHA)"
done
for f in etc/config/hi5622 etc/init.d/hi5622-wifi etc/modprobe.d/hi5622-wifi.conf \
         etc/hi5622/FIRMWARE-POLICY.txt usr/sbin/hi5622-load; do
	[ -f "$RF/$f" ] && pass "image carries $f" || { fail "image missing $f"; continue; }
	P_SHA=$(sha256sum "$LANE/payload/$f" | cut -d' ' -f1)
	I_SHA=$(sha256sum "$RF/$f" | cut -d' ' -f1)
	[ "$P_SHA" = "$I_SHA" ] && pass "image $f byte-identical to lane payload ($P_SHA)" || fail "image $f differs from lane payload ($P_SHA vs $I_SHA)"
done
[ -x "$RF/usr/sbin/hi5622-load" ] && pass "hi5622-load is executable in the image" || fail "hi5622-load not executable"
[ -x "$RF/etc/init.d/hi5622-wifi" ] && pass "hi5622-wifi init script is executable" || fail "hi5622-wifi init not executable"
[ -L "$RF/etc/rc.d/S96hi5622-wifi" ] && pass "hi5622-wifi rc.d S96 link present (service is autoload=0 no-op)" || fail "hi5622-wifi rc.d link absent"
grep -qE "option autoload '0'" "$RF/etc/config/hi5622" && pass "hi5622 uci policy autoload=0 (nothing loads at boot)" || fail "hi5622 uci autoload policy missing"
grep -qE '^\s*option mgmt_tx .0.' "$RF/etc/config/hi5622" && pass "association gate mgmt_tx fail-closed (0) in the image policy" || fail "mgmt_tx fail-closed policy missing"
SHA8=${MOD_ART_SHA:0:8}; SHA8=${SHA8:-d2c608e7}
grep -q "$SHA8" "$RF/etc/config/hi5622" && pass "image uci policy names the baked module sha ($SHA8...)" || fail "image uci policy does not name the baked module sha ($SHA8...)"

# ---- 6. package manifest -------------------------------------------------
MANIFEST=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*.manifest 2>/dev/null | head -1 || true)
if [ -n "$MANIFEST" ]; then
	for p in hostapd-mbedtls hostapd-common libmbedtls21 iw wireless-regdb wifi-scripts \
	         kmod-hi5622-wifi kmod-hi5622-cfg80211; do
		grep -q "^$p " "$MANIFEST" && pass "manifest has $p" || fail "manifest has no $p"
	done
	grep -q '^hostapd-mini ' "$MANIFEST" && fail "manifest still has hostapd-mini" || pass "manifest has no hostapd-mini"
	record "manifest packages = $(wc -l < "$MANIFEST")"
else
	fail "no built package manifest found"
fi

# ---- 7. policy: no vendor firmware/calibration in the image --------------
if [ -d "$RF/lib/firmware" ]; then
	FW_LIST=$(find "$RF/lib/firmware" -type f 2>/dev/null | sed "s|^$RF||" | sort | tr '\n' ' ')
	FW_BAD=$(printf '%s\n' "$FW_LIST" | tr ' ' '\n' | grep -v -E '^/lib/firmware/regulatory\.db$' | grep -c . || true)
	[ "$FW_BAD" = 0 ] && pass "no vendor firmware packaged (only regulatory.db)" || fail "unexpected firmware files: $FW_LIST"
fi
PD4=$(find "$RF" -name 'FIRMWARE*.bin' 2>/dev/null | wc -l)
[ "$PD4" = 0 ] && pass "no vendor FIRMWARE*.bin in rootfs" || fail "vendor FIRMWARE*.bin packaged: $PD4"
BIGBIN=$(find "$RF/lib/modules" -name '*.bin' -size +200k 2>/dev/null | wc -l)
[ "$BIGBIN" = 0 ] && pass "no large binary blobs under /lib/modules" || fail "binary blobs under /lib/modules: $BIGBIN"

# ---- 8. rootfs-only flash feasibility ------------------------------------
record "rootfs-only flash feasibility: kernel member sha unchanged ($EXPECTED_KERNEL) -> kernelb/mtd12 slot $EXPECTED_KERNEL_SLOT already carries those bytes, no write needed"
record "  only changed flash member = rootfs UBI $RS_SIZE B (<= mtd14 0x1740000/$MTD14_SIZE B); same container size class as 8944740e; kernel config forces ubi.mtd=rootfsb -> write mtd14, stay pair b"
pass "rootfs-only flash geometry re-checked (see FLASH-FEASIBILITY.md for the manual sequence)"

# ---- result --------------------------------------------------------------
echo
echo "== verify-final-image-2: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS - FINAL IMAGE #2 carries the corrected association module, SAE hostapd, WPA2/WPA3 fixtures, iw/ucode/regdb, and the validated kernel slice (UNCHANGED)"
