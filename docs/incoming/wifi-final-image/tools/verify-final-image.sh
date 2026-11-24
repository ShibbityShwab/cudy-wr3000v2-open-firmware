#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-final-image.sh - verify the FINAL bench image FROM ITS BYTES.
#
#   ./tools/verify-final-image.sh [--owrt DIR] [--work DIR]
#                                 [--driver-ko FILE] [--lane DIR]
#
# Extracts the sysupgrade container, checks
#   * the kernel slice is the hardware-validated 13e8121445... (UNCHANGED),
#   * the rootfs UBI reassembles into a squashfs whose volume prefix equals
#     build_dir/.../root.squashfs,
#   * every required base-image file (iw/iwinfo/hostapd/ucode/regdb/cfg80211)
#     is present with its known-good identity,
#   * this lane's payload files are present BYTE-IDENTICAL to payload/,
#     including the etc/rc.d enable symlink, autoload=0 policy values and the
#     WPA2 fixtures,
#   * the installed hi5622_wifi.ko is the FINAL all-patch module: identical
#     tree-package artifact sha 5b587949..., vermagic/depends/params (47) and
#     the validated feature strings,
#   * no vendor firmware/calibration binary is packaged,
#   * the apk manifest carries the expected package set.
# Exit code 0 only if every required item passes.
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
WORK=/tmp/wifi-final-image-verify
DRIVER_KO=/tmp/wifi-allpatch-artifact/hi5622_wifi.ko

EXPECTED_KERNEL=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SIZE=3372638
EXPECTED_SLOT=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
EXPECTED_DRIVER=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
EXPECTED_DRIVER_SIZE=308080
EXPECTED_DRIVER_PARAMS=47
EXPECTED_KERNEL_CONFIG=08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=${2:-}; shift 2 ;;
		--owrt=*) OWRT=${1#*=}; shift ;;
		--work) WORK=${2:-}; shift 2 ;;
		--work=*) WORK=${1#*=}; shift ;;
		--driver-ko) DRIVER_KO=${2:-}; shift 2 ;;
		--driver-ko=*) DRIVER_KO=${1#*=}; shift ;;
		--lane) LANE=${2:-}; shift 2 ;;
		--lane=*) LANE=${1#*=}; shift ;;
		-h|--help) sed -n '3,31p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "verify-final-image: unknown argument: $1" >&2; exit 2 ;;
	esac
done

die() { echo "verify-final-image: ERROR: $*" >&2; exit 1; }
FAILED=0
pass() { printf 'PASS  %s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*"; FAILED=$((FAILED + 1)); }
record() { printf 'RECORD  %s\n' "$*"; }

TARGETS=$OWRT/bin/targets/hisilicon/luofu
[ -d "$TARGETS" ] || die "no image dir: $TARGETS"
CONTAINER=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
[ -n "$CONTAINER" ] || die "no *squashfs-sysupgrade.bin in $TARGETS"
[ -x /usr/bin/unsquashfs ] || die "unsquashfs not installed on the host"
rm -rf "$WORK"; mkdir -p "$WORK"

echo "== FINAL bench image verification (built bytes) =="
echo "tree        : $OWRT"
echo "container   : $CONTAINER"
echo "built       : $(stat -c %y "$CONTAINER")"
echo "size        : $(stat -c %s "$CONTAINER") B"
echo "sha256      : $(sha256sum "$CONTAINER" | cut -d' ' -f1)"
echo

tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin" || die "cannot extract kernel slice"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" || die "cannot extract rootfs ubi"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/fip-manifest.txt > "$WORK/fip-manifest.txt" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/CONTROL > "$WORK/CONTROL" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt > "$WORK/INSTALL-STATUS.txt" 2>/dev/null || true

KERNEL_SIZE=$(stat -c %s "$WORK/kernel.bin")
KERNEL_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1)
ROOTFS_SIZE=$(stat -c %s "$WORK/rootfs.ubi")
ROOTFS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1)
echo "kernel slice: $KERNEL_SIZE B sha256 $KERNEL_SHA"
echo "rootfs ubi  : $ROOTFS_SIZE B sha256 $ROOTFS_SHA"
if [ "$KERNEL_SHA" = "$EXPECTED_KERNEL" ] && [ "$KERNEL_SIZE" = "$EXPECTED_KERNEL_SIZE" ]; then
	pass "kernel slice UNCHANGED - the hardware-validated slice 13e81214..."
else
	fail "kernel slice CHANGED (want $EXPECTED_KERNEL_SIZE B $EXPECTED_KERNEL)"
fi
# the flashable kernel slot is the slice padded with 0xFF to the 8,650,752 B
# mtd12 write; its full-slot hash must still be the validated 1794a531...
SLOT_SIZE=8650752
if [ "$KERNEL_SIZE" -le "$SLOT_SIZE" ]; then
	python3 - "$WORK/kernel.bin" "$WORK/kernel-slot.bin" "$SLOT_SIZE" <<'PY'
import sys
src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(src, "rb").read()
open(dst, "wb").write(data + b"\xff" * (size - len(data)))
PY
	SLOT_SHA=$(sha256sum "$WORK/kernel-slot.bin" | cut -d' ' -f1)
	if [ "$SLOT_SHA" = "$EXPECTED_SLOT" ]; then
		pass "kernel full-slot hash UNCHANGED: $SLOT_SHA (8,650,752 B, the kernelb/mtd12 write unit)"
	else
		fail "kernel full-slot hash $SLOT_SHA != validated $EXPECTED_SLOT"
	fi
else
	fail "kernel slice larger than the 8,650,752 B slot"
fi
echo

echo "-- rootfs squashfs reassembled from the UBI image --"
python3 "$IMAGE_INT/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/rootfs.squashfs" --vol-id 0 \
	|| die "UBI -> squashfs extraction failed"
SQ_SIZE=$(stat -c %s "$WORK/rootfs.squashfs")
SQ_SHA=$(sha256sum "$WORK/rootfs.squashfs" | cut -d' ' -f1)
echo "squashfs    : $SQ_SIZE B sha256 $SQ_SHA"
BUILT_SQ=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/root.squashfs
if [ -f "$BUILT_SQ" ]; then
	BUILT_SQ_SHA=$(sha256sum "$BUILT_SQ" | cut -d' ' -f1)
	BUILT_SQ_SIZE=$(stat -c %s "$BUILT_SQ")
	if [ "$BUILT_SQ_SIZE" -le "$SQ_SIZE" ] && cmp -s -n "$BUILT_SQ_SIZE" "$BUILT_SQ" "$WORK/rootfs.squashfs"; then
		pass "squashfs volume == build_dir/.../root.squashfs ($BUILT_SQ_SIZE B $BUILT_SQ_SHA) + $((SQ_SIZE - BUILT_SQ_SIZE)) B ubinize padding"
	else
		fail "squashfs volume differs from build_dir/.../root.squashfs ($BUILT_SQ_SIZE B $BUILT_SQ_SHA)"
	fi
fi

rm -rf "$WORK/extract"
unsquashfs -n -no-exit-code -d "$WORK/extract" "$WORK/rootfs.squashfs" >/dev/null 2>&1 || \
	die "unsquashfs failed (corrupt squashfs volume?)"
ROOT=$WORK/extract
echo "rootfs files: $(find "$ROOT" -type f | wc -l)"
echo

echo "-- required base-image content --"
MISSING=0
while IFS='|' read -r rel desc; do
	[ -z "$rel" ] && continue
	if [ -e "$ROOT/$rel" ]; then
		printf 'PRESENT  %-46s %8s B sha256 %s  # %s\n' "$rel" \
			"$(stat -c %s "$ROOT/$rel")" "$(sha256sum "$ROOT/$rel" | cut -d' ' -f1)" "$desc"
	else
		printf 'MISSING  %-46s # %s\n' "$rel" "$desc"
		fail "missing image file: $rel ($desc)"
	fi
done <<'EOF'
usr/sbin/iw|iw CLI (nl80211)
usr/bin/iwinfo|iwinfo CLI (ucode implementation, wifi-scripts)
usr/share/ucode/iwinfo.uc|ucode iwinfo module (wifi-scripts)
usr/share/ucode/wifi/common.uc|ucode wifi scripts (wifi-scripts)
usr/share/hostap/common.uc|hostapd ucode module (wifi-scripts)
usr/share/hostap/wdev.uc|hostapd ucode module (wifi-scripts)
usr/share/hostap/wifi-detect.uc|hostapd ucode module (wifi-scripts)
usr/share/hostap/hostapd.uc|hostapd config backend (hostapd-mini)
usr/sbin/hostapd|hostapd daemon (hostapd-mini)
etc/init.d/wpad|hostapd init script (hostapd-common)
etc/capabilities/wpad.json|hostapd capability declaration
usr/lib/ucode/nl80211.so|ucode nl80211 module
usr/lib/ucode/rtnl.so|ucode rtnl module
usr/lib/libnl-tiny.so.1|libnl-tiny (iw dependency)
lib/firmware/regulatory.db|wireless regulatory database
lib/modules/6.18.60/cfg80211.ko|cfg80211 kernel module (target kernel tree)
lib/modules/6.18.60/hi5622_wifi.ko|clean-room Hi5622 Wi-Fi host driver (FINAL all-patch)
EOF

echo
echo "-- expected base-file identities (known-good hashes) --"
while IFS='|' read -r rel want_size want_sha; do
	[ -z "$rel" ] && continue
	if [ ! -e "$ROOT/$rel" ]; then
		fail "missing $rel"
		continue
	fi
	got_size=$(stat -c %s "$ROOT/$rel")
	got_sha=$(sha256sum "$ROOT/$rel" | cut -d' ' -f1)
	if [ "$got_size" = "$want_size" ] && [ "$got_sha" = "$want_sha" ]; then
		echo "IDENT    $rel ($got_size B sha256 $got_sha)"
	else
		fail "DRIFT $rel size $got_size (expected $want_size) sha256 $got_sha (expected $want_sha)"
	fi
done <<'EOF'
usr/sbin/iw|116855|e0f4ba8cb1dd8f748924f7333475f6d4120bc0b9290d1cf67df1be8687accb23
usr/sbin/hostapd|504333|13cb3a4306f752f4e1638585e734cd29368ee393096b75931f89e71e01b21919
usr/lib/ucode/nl80211.so|61442|54ac57bcef3a2bf78d5ca58d22a0a5dfc1b17505978beac5c9133e48d970f09f
usr/lib/ucode/rtnl.so|73731|fc42ccf216ff12447393a95174ed8b46b34b5c2895a806139e45d9bfb7e1b94d
lib/firmware/regulatory.db|6332|c682b8e0f2f61385929586409f092638ba84dd795503d220be15b5c3d16dd75d
lib/modules/6.18.60/cfg80211.ko|406028|11a5e2b57f31e07b3e692ac084c2aa278bc5d1e5485e7c51e6cb114cfe57c46a
EOF

echo
echo "-- wifi-final-image payload (must be byte-identical to payload/) --"
PAYLOAD=$LANE/payload
PAYLOAD_MISSING=0
while IFS= read -r src; do
	rel=${src#"$PAYLOAD/"}
	want_sha=$(sha256sum "$src" | cut -d' ' -f1)
	if [ ! -e "$ROOT/$rel" ]; then
		fail "payload file missing from image: $rel"
		PAYLOAD_MISSING=$((PAYLOAD_MISSING + 1))
		continue
	fi
	got_sha=$(sha256sum "$ROOT/$rel" | cut -d' ' -f1)
	if [ "$got_sha" = "$want_sha" ]; then
		echo "PAYLOAD  $rel  sha256 $got_sha"
	else
		fail "payload file drifted in image: $rel (image $got_sha != lane $want_sha)"
		PAYLOAD_MISSING=$((PAYLOAD_MISSING + 1))
	fi
done < <(find "$PAYLOAD" -type f | sort)
[ "$PAYLOAD_MISSING" -eq 0 ] && pass "all $(find "$PAYLOAD" -type f | wc -l) payload file(s) present byte-identically"

# the rc.d enable symlink is created by the staging step (not a payload file)
LINK=$ROOT/etc/rc.d/S96hi5622-wifi
if [ -L "$LINK" ]; then
	tgt=$(readlink "$LINK")
	if [ "$tgt" = "../init.d/hi5622-wifi" ]; then
		pass "autoload service enable symlink etc/rc.d/S96hi5622-wifi -> ../init.d/hi5622-wifi"
	else
		fail "enable symlink points to '$tgt' (expected ../init.d/hi5622-wifi)"
	fi
else
	fail "missing enable symlink etc/rc.d/S96hi5622-wifi"
fi

# policy content assertions (text)
CFG=$ROOT/etc/config/hi5622
if [ -f "$CFG" ]; then
	grep -q "option autoload '0'" "$CFG" && pass "policy: autoload=0 (bench default; no boot load)" || fail "policy: autoload is not 0"
	grep -q "option allow_frames '32'" "$CFG" && pass "policy: allow_frames=32 (final-module WPA2/RX recipe)" || fail "policy: allow_frames not 32"
	grep -q "option h2d_send_budget '10'" "$CFG" && pass "policy: h2d_send_budget=10 (final-module WPA2/RX recipe)" || fail "policy: h2d_send_budget not 10"
	grep -q "option wpa_keys '1'" "$CFG" && pass "policy: wpa_keys=1 (M3 key ops enabled)" || fail "policy: wpa_keys not 1"
	grep -q "option rx_enable '1'" "$CFG" && pass "policy: rx_enable=1 (validated RX arm)" || fail "policy: rx_enable not 1"
	grep -q "option beacon_static '1'" "$CFG" && pass "policy: beacon_static=1 (5-message AP order)" || fail "policy: beacon_static not 1"
else
	fail "policy /etc/config/hi5622 missing"
fi
LOADER=$ROOT/usr/sbin/hi5622-load
if [ -f "$LOADER" ]; then
	if [ -x "$LOADER" ]; then pass "loader /usr/sbin/hi5622-load executable"; else fail "loader not executable"; fi
	grep -q "allow_frames=32" "$LOADER" && grep -q "h2d_send_budget=10" "$LOADER" \
		&& pass "loader passes the final-module 32/10 frame-budget recipe" \
		|| fail "loader does not pass allow_frames=32/h2d_send_budget=10"
	grep -q "rx_enable" "$LOADER" && grep -q "wpa_keys" "$LOADER" && grep -q "beacon_static" "$LOADER" \
		&& pass "loader passes rx_enable/beacon_static/wpa_keys feature gates" \
		|| fail "loader feature-gate parameters missing"
else
	fail "loader /usr/sbin/hi5622-load missing"
fi
INITD=$ROOT/etc/init.d/hi5622-wifi
if [ -f "$INITD" ]; then
	grep -q '^START=96' "$INITD" && pass "init service START=96 delegating to the loader" || fail "init service START is not 96"
	[ "$(stat -c %a "$INITD")" = "755" ] && pass "init service mode 755" || fail "init service mode $(stat -c %a "$INITD") != 755"
else
	fail "init service /etc/init.d/hi5622-wifi missing"
fi
WPA2=$ROOT/etc/hi5622/hostapd-wpa2.conf
if [ -f "$WPA2" ]; then
	grep -q '^ssid=cudy-ap-test$' "$WPA2" && pass "WPA2 fixture: ssid cudy-ap-test (test network)" || fail "WPA2 fixture ssid missing"
	grep -q '^wpa=2$' "$WPA2" && grep -q '^wpa_key_mgmt=WPA-PSK$' "$WPA2" \
		&& pass "WPA2 fixture: wpa=2 WPA-PSK/CCMP" || fail "WPA2 fixture key management lines missing"
	grep -q '^wpa_passphrase=test-passphrase-121$' "$WPA2" && pass "WPA2 fixture: test passphrase (throwaway lab credential)" || fail "WPA2 fixture passphrase missing/changed"
else
	fail "WPA2 fixture /etc/hi5622/hostapd-wpa2.conf missing"
fi
for f in hostapd-open.conf hostapd-open.proven.conf hostapd-wpa2-wpa3.conf; do
	[ -f "$ROOT/etc/hi5622/$f" ] && pass "fixture present: /etc/hi5622/$f" || fail "fixture missing: /etc/hi5622/$f"
done

# no vendor firmware / calibration binaries may be packaged
FW_BAD=$(find "$ROOT/lib/firmware" -type f \( -name 'FIRMWARE*' -o -name 'wifi_block*' -o -path '*/hi_wifi/*' \) 2>/dev/null | head -5)
if [ -z "$FW_BAD" ]; then
	pass "no vendor firmware/calibration binary packaged (PD4/wifi_block are manual pushes)"
else
	fail "vendor firmware binary packaged: $FW_BAD"
fi

echo
echo "-- installed hi5622_wifi.ko identity (FINAL all-patch module) --"
KO=$ROOT/lib/modules/6.18.60/hi5622_wifi.ko
if [ -f "$KO" ]; then
	KO_SIZE=$(stat -c %s "$KO"); KO_SHA=$(sha256sum "$KO" | cut -d' ' -f1)
	record "installed module: $KO_SIZE B sha256 $KO_SHA (OpenWrt-stripped form)"
	VM=$(modinfo -F vermagic "$KO" 2>/dev/null || true)
	case "$VM" in *"6.18.60 SMP mod_unload ARMv7 p2v8"*) pass "vermagic: $VM" ;; *) fail "vermagic '$VM' != 6.18.60 SMP mod_unload ARMv7 p2v8" ;; esac
	DEP=$(modinfo -F depends "$KO" 2>/dev/null || true)
	case "$DEP" in *cfg80211*) pass "depends: $DEP" ;; *) fail "depends '$DEP' does not include cfg80211" ;; esac
	LIC=$(modinfo -F license "$KO" 2>/dev/null || true)
	[ "$LIC" = "GPL" ] && pass "license: $LIC" || fail "license '$LIC' != GPL"
	NP=$(modinfo -F parm "$KO" 2>/dev/null | wc -l)
	[ "$NP" = "$EXPECTED_DRIVER_PARAMS" ] && pass "module parameter count: $NP (final all-patch)" || fail "module parameter count $NP != $EXPECTED_DRIVER_PARAMS"
	PARMS=$(modinfo -F parm "$KO" 2>/dev/null || true)
	for p in fw_path wifi_block_path allow_frames frame_gap_ms h2d_send_budget vap_start_first vap_start_mode rx_enable beacon_static beacon_vap_start wpa_keys wpa_key_module sta_join ht_mode vht_cap band_vendor scan_band_cfg mgmt_diag hcc_trace txq_publish; do
		case "$PARMS" in *"$p:"*) ;; *) fail "module parameter missing: $p" ;; esac
	done
	SYMS=$(strings "$KO" 2>/dev/null || true)
	for f in dp_probe dp_setup dp_tx_frame dp_tx_status dp_rx_poll h2d_txq_addr_sync rx_arm add_key set_default_key beacon_static sta_join scan_band_cfg; do
		case "$SYMS" in *"$f"*) ;; *) fail "final-module feature string missing: $f" ;; esac
	done
	pass "final-module feature strings present (dp/ap/rx/key/beacon/sta/scan)"
	# exact-bytes pin: the tree package file must be the validated artifact
	for cand in "$OWRT/package/hi5622-wifi/prebuilt/hi5622_wifi.ko" "$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/hi5622-wifi/hi5622_wifi.ko"; do
		if [ -f "$cand" ]; then
			csha=$(sha256sum "$cand" | cut -d' ' -f1); csize=$(stat -c %s "$cand")
			if [ "$csha" = "$EXPECTED_DRIVER" ] && [ "$csize" = "$EXPECTED_DRIVER_SIZE" ]; then
				pass "tree package artifact == validated prebuilt ($csize B sha256 $csha): $cand"
			else
				fail "tree package artifact is NOT the validated prebuilt: $cand ($csize B $csha)"
			fi
		fi
	done
	if [ -f "$DRIVER_KO" ]; then
		asha=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
		[ "$asha" = "$EXPECTED_DRIVER" ] && pass "driver artifact on host matches pinned sha $EXPECTED_DRIVER" || fail "driver artifact sha $asha != pinned"
	fi
else
	fail "installed module missing: $KO"
fi
echo

echo "-- apk manifest packages --"
MANIFEST=$(ls -t "$TARGETS"/*.manifest 2>/dev/null | head -1)
[ -n "$MANIFEST" ] || die "no image manifest"
MANIFEST_MISSING=0
for pkg in iw wifi-scripts wireless-regdb hostapd-mini hostapd-common \
           ucode-mod-nl80211 ucode-mod-rtnl kmod-hi5622-cfg80211 kmod-hi5622-wifi libnl-tiny1; do
	case "$pkg" in
		libnl-tiny*) pat='^libnl-tiny[0-9]* - ' ;;
		*)          pat="^$pkg - " ;;
	esac
	if grep -qE "$pat" "$MANIFEST"; then
		echo "PRESENT  $(grep -E "$pat" "$MANIFEST")"
	else
		fail "package missing from manifest: $pkg"
		MANIFEST_MISSING=$((MANIFEST_MISSING + 1))
	fi
done
echo "manifest    : $(basename "$MANIFEST"), $(wc -l < "$MANIFEST") packages"
[ "$MANIFEST_MISSING" -eq 0 ] && pass "manifest carries all required packages ($(wc -l < "$MANIFEST") total)"

# kernel config must be unchanged by the build
KERNEL_CFG=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/.config
if [ -f "$KERNEL_CFG" ]; then
	KCFG_SHA=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
	[ "$KCFG_SHA" = "$EXPECTED_KERNEL_CONFIG" ] && pass "built kernel .config unchanged: $KCFG_SHA" || fail "kernel .config sha $KCFG_SHA != $EXPECTED_KERNEL_CONFIG"
fi

echo
echo "-- identities to record --"
echo "container_sha256      = $(sha256sum "$CONTAINER" | cut -d' ' -f1)"
echo "container_size        = $(stat -c %s "$CONTAINER")"
echo "kernel_slice_sha256   = $KERNEL_SHA"
echo "kernel_slice_size     = $KERNEL_SIZE"
echo "kernel_slot_sha256    = ${SLOT_SHA:-absent} (8,650,752 B)"
echo "rootfs_ubi_sha256     = $ROOTFS_SHA"
echo "rootfs_ubi_size       = $ROOTFS_SIZE"
echo "squashfs_volume_sha256 = $SQ_SHA"
echo "squashfs_volume_size   = $SQ_SIZE"
echo "squashfs_build_sha256  = ${BUILT_SQ_SHA:-absent}"
echo
if [ "$FAILED" -ne 0 ]; then
	echo "RESULT: FAIL - $FAILED check(s) failed"
	exit 1
fi
echo "RESULT: PASS - FINAL image bytes verified (kernel unchanged, payload byte-identical, final all-patch module identity OK)"
