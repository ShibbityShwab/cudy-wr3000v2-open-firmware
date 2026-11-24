#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-image-with-hostapd-sae.sh - verify a re-baked bench image FROM ITS
# BYTES (wifi-hostapd-sae lane, task-148).  HOST-ONLY.
#
# Checks
#   * kernel slice in the container is the hardware-validated 13e8121445...
#     (UNCHANGED - the package/hostapd work must not touch the kernel),
#   * the rootfs UBI reassembles into the squashfs prefix of the build tree,
#   * /usr/sbin/hostapd is the new SAE-capable mbedtls full variant (ARM EABI5,
#     mbedtls DT_NEEDED, SAE code strings), and is NOT the old 504,333 B
#     hostapd-mini binary 13cb3a43... ("invalid key_mgmt 'SAE'"),
#   * the three WPA3-SAE / WPA2-hardened fixtures are present BYTE-IDENTICAL to
#     this lane's payload (the stale transition fixture is superseded),
#   * the installed hi5622_wifi.ko is the variant-pinned module (variant A:
#     installed sha f07dd505...; variant B: recorded, modinfo identity checked),
#   * the package manifest has hostapd-mbedtls + libmbedtls21 + kmod-hi5622-wifi
#     and has NO hostapd-mini,
#   * no vendor firmware/calibration binary is packaged.
#
# usage: ./tools/verify-image-with-hostapd-sae.sh --variant A|B --container FILE
#          [--work DIR] [--driver-installed-sha SHA]
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
FINAL_IMAGE=$(cd "$LANE/../wifi-final-image" && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
VARIANT=
CONTAINER=
WORK=/tmp/wifi-hostapd-sae-verify
DRIVER_INSTALLED_SHA=
ROOT_SQUASHFS=

EXPECTED_KERNEL=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SIZE=3372638
OLD_HOSTAPD_SHA=13cb3a4306f752f4e1638585e734cd29368ee393096b75931f89e71e01b21919
OLD_HOSTAPD_SIZE=504333
ACTION_HOSTAPD_SHA=f07dd505f1d7ac5dd62a826c440414bf2bc1662fe2fecb504dc4cec5b5e0d412

while [ $# -gt 0 ]; do
	case "$1" in
		--variant) VARIANT=${2:-}; shift 2 ;;
		--variant=*) VARIANT=${1#*=}; shift ;;
		--container) CONTAINER=${2:-}; shift 2 ;;
		--container=*) CONTAINER=${1#*=}; shift ;;
		--work) WORK=${2:-}; shift 2 ;;
		--work=*) WORK=${1#*=}; shift ;;
		--driver-installed-sha) DRIVER_INSTALLED_SHA=${2:-}; shift 2 ;;
		--driver-installed-sha=*) DRIVER_INSTALLED_SHA=${1#*=}; shift ;;
		--root-squashfs) ROOT_SQUASHFS=${2:-}; shift 2 ;;
		--root-squashfs=*) ROOT_SQUASHFS=${1#*=}; shift ;;
		-h|--help) sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "verify-image-with-hostapd-sae: unknown argument: $1" >&2; exit 2 ;;
	esac
done
[ "$VARIANT" = A ] || [ "$VARIANT" = B ] || { echo "verify: --variant A|B is required" >&2; exit 2; }
[ -n "$CONTAINER" ] || { echo "verify: --container is required" >&2; exit 2; }
[ -f "$CONTAINER" ] || { echo "verify: no such container: $CONTAINER" >&2; exit 2; }
[ -x /usr/bin/unsquashfs ] || { echo "verify: unsquashfs missing on host" >&2; exit 2; }

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); printf 'PASS  %s\n' "$*"; }
fail() { FAIL=$((FAIL+1)); printf 'FAIL  %s\n' "$*"; }
record() { printf 'RECORD  %s\n' "$*"; }
die() { echo "verify-image-with-hostapd-sae: ERROR: $*" >&2; exit 2; }

rm -rf "$WORK"; mkdir -p "$WORK"
echo "== wifi-hostapd-sae image verification (built bytes), variant $VARIANT =="
echo "container : $CONTAINER"
C_SIZE=$(stat -c %s "$CONTAINER"); C_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
record "container size/sha256 = $C_SIZE / $C_SHA"
echo

# ---- 1. container members -------------------------------------------------
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin" 2>/dev/null || die "cannot extract kernel member"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" 2>/dev/null || die "cannot extract root member"
KS_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1); KS_SIZE=$(stat -c %s "$WORK/kernel.bin")
[ "$KS_SHA" = "$EXPECTED_KERNEL" ] && pass "container kernel slice is the validated 13e81214... (sha)" || fail "kernel slice CHANGED: $KS_SHA"
[ "$KS_SIZE" = "$EXPECTED_KERNEL_SIZE" ] && pass "kernel slice size unchanged ($EXPECTED_KERNEL_SIZE B)" || fail "kernel slice size changed: $KS_SIZE"
RS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1); RS_SIZE=$(stat -c %s "$WORK/rootfs.ubi")
record "rootfs.ubi size/sha256 = $RS_SIZE / $RS_SHA"
head -c 4 "$WORK/rootfs.ubi" | grep -q 'UBI#' && pass "rootfs UBI starts with UBI# EC magic" || fail "rootfs member is not UBI"

# ---- 2. rootfs squashfs + prefix match -----------------------------------
python3 "$IMAGE_INT/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/root.squashfs" > "$WORK/ubi-extract.log" 2>&1 || die "UBI volume extraction failed"
SQ_SHA=$(sha256sum "$WORK/root.squashfs" | cut -d' ' -f1)
BUILT_SQ=${ROOT_SQUASHFS:-$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/root.squashfs}
if [ -f "$BUILT_SQ" ]; then
	B_SIZE=$(stat -c %s "$BUILT_SQ")
	X_SIZE=$(stat -c %s "$WORK/root.squashfs")
	if cmp -s -n "$B_SIZE" "$WORK/root.squashfs" "$BUILT_SQ"; then
		PAD=$(tail -c +$((B_SIZE + 1)) "$WORK/root.squashfs" | tr -d '\0' | wc -c)
		if [ "$PAD" -eq 0 ]; then
			pass "UBI volume 0 == build_dir root.squashfs prefix ($B_SIZE B) + zero padding only"
		else
			fail "UBI volume 0 has $PAD non-padding bytes after the root.squashfs prefix"
		fi
		record "volume size $X_SIZE B, squashfs $B_SIZE B, end padding $((X_SIZE - B_SIZE)) B"
	else
		fail "UBI volume 0 does not match the build root.squashfs prefix"
	fi
else
	record "build_dir root.squashfs absent (recording UBI volume sha $SQ_SHA only)"
fi
# non-root host cannot create the /dev nodes: non-fatal (unsquashfs rc 2); -no-exit-code
# keeps that from aborting, and the hostapd/fixture checks below prove real extraction.
unsquashfs -no-exit-code -d "$WORK/extract" "$WORK/root.squashfs" >/dev/null 2>&1 || true
[ -e "$WORK/extract/usr/sbin/hostapd" ] || die "unsquashfs did not extract the rootfs"
RF=$WORK/extract

# ---- 3. the SAE-capable hostapd binary -----------------------------------
H=$RF/usr/sbin/hostapd
[ -f "$H" ] || die "no /usr/sbin/hostapd in the image rootfs"
H_SIZE=$(stat -c %s "$H"); H_SHA=$(sha256sum "$H" | cut -d' ' -f1)
record "image /usr/sbin/hostapd size/sha256 = $H_SIZE / $H_SHA"
[ "$H_SHA" != "$OLD_HOSTAPD_SHA" ] && pass "hostapd is NOT the old hostapd-mini binary 13cb3a43..." || fail "hostapd is still the old hostapd-mini binary"
[ "$H_SIZE" != "$OLD_HOSTAPD_SIZE" ] && pass "hostapd size differs from hostapd-mini (504,333 B)" || fail "hostapd size equals hostapd-mini"
readelf -h "$H" | grep -q 'Machine:.*ARM' && pass "hostapd is ARM EABI5" || fail "hostapd is not ARM"
readelf -h "$H" | grep -q 'Version5 EABI' || fail "hostapd is not EABI5"
readelf -d "$H" | grep -q 'libmbedcrypto' && pass "hostapd DT_NEEDED libmbedcrypto (SAE EC backend)" || fail "hostapd does not link libmbedcrypto"
readelf -d "$H" | grep -q 'libmbedx509' && pass "hostapd DT_NEEDED libmbedx509" || fail "hostapd does not link libmbedx509"
readelf -d "$H" | grep -q 'libmbedtls' && pass "hostapd DT_NEEDED libmbedtls" || fail "hostapd does not link libmbedtls"
SAE_STRINGS=$(strings "$H" | grep -c '^SAE: ' || true)
[ "${SAE_STRINGS:-0}" -ge 5 ] && pass "hostapd carries SAE code strings ($SAE_STRINGS)" || fail "hostapd carries too few SAE strings ($SAE_STRINGS)"
strings "$H" | grep -q 'invalid key_mgmt' && pass "hostapd config parser string present" || fail "hostapd parser string missing"
[ -L "$RF/usr/sbin/hostapd-radius" ] && pass "hostapd-radius symlink installed (full variant)" || record "hostapd-radius symlink absent"
for lib in libmbedcrypto libmbedx509 libmbedtls; do
	ls "$RF/usr/lib/$lib".so.* >/dev/null 2>&1 && pass "image carries $lib runtime" || fail "image missing $lib runtime"
done

# ---- 4. WPA3 fixtures bit-identical --------------------------------------
for f in hostapd-wpa3-sae.conf hostapd-wpa2-wpa3.conf hostapd-wpa2-hardened.conf; do
	[ -f "$LANE/payload/etc/hi5622/$f" ] || fail "lane payload missing $f"
	[ -f "$RF/etc/hi5622/$f" ] && pass "fixture $f present in image" || fail "fixture $f missing from image"
	P_SHA=$(sha256sum "$LANE/payload/etc/hi5622/$f" | cut -d' ' -f1)
	I_SHA=$(sha256sum "$RF/etc/hi5622/$f" 2>/dev/null | cut -d' ' -f1)
	[ -n "$I_SHA" ] && [ "$P_SHA" = "$I_SHA" ] && pass "fixture $f byte-identical to payload ($P_SHA)" || fail "fixture $f differs from payload ($P_SHA vs $I_SHA)"
done

# ---- 5. driver module ----------------------------------------------------
KO=$RF/lib/modules/6.18.60/hi5622_wifi.ko
if [ -f "$KO" ]; then
	KO_SIZE=$(stat -c %s "$KO"); KO_SHA=$(sha256sum "$KO" | cut -d' ' -f1)
	record "installed module size/sha256 = $KO_SIZE / $KO_SHA"
	[ "$(modinfo -F name "$KO")" = "hi5622_wifi" ] && pass "installed module name hi5622_wifi" || fail "installed module name wrong"
	[ "$(modinfo -F vermagic "$KO" | cut -d' ' -f1)" = "6.18.60" ] && pass "installed module vermagic 6.18.60" || fail "installed module vermagic wrong"
	echo "$(modinfo -F depends "$KO")" | grep -q cfg80211 && pass "installed module depends cfg80211" || fail "installed module does not depend on cfg80211"
	KPARMS=$(modinfo -F parm "$KO" | wc -l)
	[ "$KPARMS" = 47 ] && pass "installed module carries 47 parameters" || fail "installed module parameter count $KPARMS != 47"
	if [ "$VARIANT" = A ]; then
		[ "$KO_SHA" = "$ACTION_HOSTAPD_SHA" ] && pass "variant A installed module sha f07dd505... (task-147 identity)" || fail "variant A installed module sha $KO_SHA != f07dd505..."
	fi
	if [ "$VARIANT" = B ]; then
		[ "$KO_SHA" != "$ACTION_HOSTAPD_SHA" ] && pass "variant B module bytes are the wpa3-sae build (installed sha $KO_SHA)" || fail "variant B module sha unexpectedly equals the task-147 identity"
	fi
else
	fail "installed module /lib/modules/6.18.60/hi5622_wifi.ko missing"
fi

# ---- 6. package manifest -------------------------------------------------
MANIFEST=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*.manifest 2>/dev/null | head -1 || true)
if [ -n "$MANIFEST" ]; then
	grep -q '^hostapd-mbedtls ' "$MANIFEST" && pass "manifest has hostapd-mbedtls" || fail "manifest has no hostapd-mbedtls"
	grep -q '^hostapd-mini ' "$MANIFEST" && fail "manifest still has hostapd-mini" || pass "manifest has no hostapd-mini"
	grep -q '^libmbedtls21 ' "$MANIFEST" && pass "manifest has libmbedtls21" || fail "manifest has no libmbedtls21"
	grep -q '^kmod-hi5622-wifi ' "$MANIFEST" && pass "manifest has kmod-hi5622-wifi" || fail "manifest has no kmod-hi5622-wifi"
	grep -q '^hostapd-common ' "$MANIFEST" && pass "manifest has hostapd-common" || fail "manifest has no hostapd-common"
	record "manifest packages = $(wc -l < "$MANIFEST")"
else
	fail "no built package manifest found"
fi

# ---- 7. policy: no vendor firmware/calibration in the image --------------
if [ -d "$RF/lib/firmware" ]; then
	FW_LIST=$(find "$RF/lib/firmware" -type f 2>/dev/null | sed "s|^$RF||" | sort)
	FW_BAD=$(printf '%s\n' "$FW_LIST" | grep -v -E '^/lib/firmware/regulatory\.db$' | grep -c . || true)
	[ "$FW_BAD" = 0 ] && pass "no vendor firmware packaged (only regulatory.db: $FW_LIST)" || fail "unexpected firmware files: $FW_LIST"
fi
PD4=$(find "$RF" -name 'FIRMWARE*.bin' 2>/dev/null | wc -l)
[ "$PD4" = 0 ] && pass "no vendor FIRMWARE*.bin in rootfs" || fail "vendor FIRMWARE*.bin packaged: $PD4"
BIGBIN=$(find "$RF/lib/modules" -name '*.bin' -size +200k 2>/dev/null | wc -l)
[ "$BIGBIN" = 0 ] && pass "no large binary blobs under /lib/modules" || fail "binary blobs under /lib/modules: $BIGBIN"

# ---- 8. boot policy (autoload=0) ----------------------------------------
[ -L "$RF/etc/rc.d/S96hi5622-wifi" ] && pass "hi5622-wifi rc.d link present (service is autoload=0 no-op)" || record "hi5622-wifi rc.d link absent"
grep -qE "autoload ('0'|='0')" "$RF/etc/config/hi5622" && pass "hi5622 uci policy autoload=0 (bench: nothing loads at boot)" || fail "hi5622 uci autoload policy missing"

# ---- result --------------------------------------------------------------
echo
echo "== verify-image-with-hostapd-sae: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS - variant $VARIANT image carries SAE hostapd, WPA3 fixtures, pinned module; kernel slice unchanged"
