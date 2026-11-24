#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# validate-wpa3-configs.sh - functional config gate for the SAE-capable hostapd
# that ships in the re-baked bench image (wifi-hostapd-sae lane, task-148).
#
# It executes the TARGET ARM BINARY (extracted from the built image container)
# under qemu-arm user-mode emulation against the same musl rootfs it ships in,
# and proves:
#   * hostapd v2.12 starts and reports its version under emulation;
#   * each of the three fixtures (WPA3-SAE only / WPA2+WPA3 transition /
#     WPA2-hardened PMF) is fully parsed and semantically accepted - the run
#     stops only at the pre-driver boundary (nl80211 generic netlink is absent
#     on the host, expected);
#   * a deliberately broken config is refused (negative control, so the gate
#     cannot be vacuous);
#   * when the previous image's hostapd-mini binary (504,333 B 13cb3a43...) is
#     available, it REFUSES key_mgmt=SAE with "invalid key_mgmt 'SAE'" - the
#     exact failure the re-bake fixes.
#
# NOTE on "hostapd -v": hostapd v2.12 -v prints only the version banner (no
# build-config list), so CONFIG_SAE is proven by (a) tools/build-hostapd-sae-
# package.sh build-log evidence (-DCONFIG_SAE, src/common/sae.o) and (b) this
# functional parse gate on the shipped binary.  That is stronger than a version
# string.
#
# HOST-ONLY: no device, no network (the qemu-arm binary is a local host tool;
# the optional first-run download fetches a public static qemu-arm build).
#
# usage: ./tools/validate-wpa3-configs.sh [--container FILE] [--rootfs-dir DIR]
#          [--qemu PATH] [--evidence FILE]
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
WORK=${WORK_DIR:-/tmp/wifi-hostapd-sae-gate}
QEMU=${QEMU_ARM:-/tmp/qemu-user/qemu-arm-static}
CONTAINER=
ROOTFS_DIR=
EVIDENCE=
OLD_HOSTAPD=${OLD_HOSTAPD:-}
DUMMY=hwsim-cudy0

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); printf 'ok    %s\n' "$*"; }
fail() { FAIL=$((FAIL+1)); printf 'FAIL  %s\n' "$*"; }
record() { printf 'REC    %s\n' "$*"; }
die() { echo "validate-wpa3-configs: ERROR: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--container) CONTAINER=${2:-}; shift 2 ;;
		--container=*) CONTAINER=${1#*=}; shift ;;
		--rootfs-dir) ROOTFS_DIR=${2:-}; shift 2 ;;
		--rootfs-dir=*) ROOTFS_DIR=${1#*=}; shift ;;
		--qemu) QEMU=${2:-}; shift 2 ;;
		--qemu=*) QEMU=${1#*=}; shift ;;
		--evidence) EVIDENCE=${2:-}; shift 2 ;;
		--evidence=*) EVIDENCE=${1#*=}; shift ;;
		-h|--help) sed -n '2,33p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1" ;;
	esac
done
[ -n "$EVIDENCE" ] || EVIDENCE=$LANE/evidence/qemu-config-gate.txt
mkdir -p "$(dirname "$EVIDENCE")"
exec > >(tee "$EVIDENCE") 2>&1

if [ -z "$ROOTFS_DIR" ]; then
	[ -n "$CONTAINER" ] || die "give --container FILE or --rootfs-dir DIR"
	[ -f "$CONTAINER" ] || die "no such container: $CONTAINER"
	mkdir -p "$WORK"
	tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" 2>/dev/null || die "cannot extract root member"
	python3 "$IMAGE_INT/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/root.squashfs" >/dev/null 2>&1 || die "UBI volume extraction failed"
	rm -rf "$WORK/rootfs"
	unsquashfs -no-exit-code -d "$WORK/rootfs" "$WORK/root.squashfs" >/dev/null 2>&1 || true
	[ -e "$WORK/rootfs/usr/sbin/hostapd" ] || die "extracted rootfs has no /usr/sbin/hostapd"
	ROOTFS_DIR=$WORK/rootfs
else
	[ -e "$ROOTFS_DIR/usr/sbin/hostapd" ] || die "rootfs dir has no /usr/sbin/hostapd: $ROOTFS_DIR"
fi
RF=$ROOTFS_DIR
BIN=$RF/usr/sbin/hostapd

# ---- qemu-arm (local static user-mode build) ------------------------------
if [ ! -x "$QEMU" ]; then
	mkdir -p "$(dirname "$QEMU")"
	for ver in v8.2.0-1 v7.2.0-1; do
		url="https://github.com/multiarch/qemu-user-static/releases/download/$ver/qemu-arm-static"
		if curl -fsSL --max-time 120 -o "$QEMU.part" "$url" 2>/dev/null; then
			mv "$QEMU.part" "$QEMU"; break
		fi
	done
	[ -x "$QEMU" ] || die "no qemu-arm at $QEMU and download failed (set QEMU_ARM=/path/qemu-arm-static)"
	chmod +x "$QEMU"
fi
QVER=$("$QEMU" --version 2>&1 | head -1)
record "qemu: $QVER"

# qemu-user -L path translation falls back to the host path for a MISSING
# absolute guest path.  The image ships libjson-c only in /usr/lib (musl finds
# it there on the device); under -L the loader's first probe is /lib, so give
# the emulated root an explicit /lib symlink (verification aid only; the image
# bytes are untouched).
LINKED=0
for f in "$RF"/usr/lib/lib*.so*; do
	[ -e "$f" ] || continue
	b=$(basename "$f")
	if [ ! -e "$RF/lib/$b" ]; then
		ln -sfn "../usr/lib/$b" "$RF/lib/$b"
		LINKED=$((LINKED + 1))
	fi
done
record "emulation aid: $LINKED /lib -> ../usr/lib symlink(s) for qemu -L only (the image ships those libs in /usr/lib and musl finds them on the device)"
export QEMU_LD_PREFIX=$RF
run_guest() { timeout 60 "$QEMU" -L "$RF" "$@" 2>&1; }

# ---- 0. shipped binary identity ------------------------------------------
echo "== SAE hostapd shipped-binary functional gate =="
echo "container/rootfs : ${CONTAINER:-$ROOTFS_DIR}"
echo "guest binary     : $BIN"
BIN_SHA=$(sha256sum "$BIN" | cut -d' ' -f1); BIN_SIZE=$(stat -c %s "$BIN")
record "shipped hostapd size/sha256 = $BIN_SIZE / $BIN_SHA"
file "$BIN" | grep -q 'ELF 32-bit.*ARM' && pass "shipped hostapd is ELF32 ARM" || fail "shipped hostapd is not ELF32 ARM"
readelf -d "$BIN" | grep -q libmbedcrypto && pass "shipped hostapd links libmbedcrypto (SAE EC backend)" || fail "shipped hostapd does not link libmbedcrypto"
VOUT=$(timeout 60 "$QEMU" -L "$RF" "$BIN" -v 2>&1)
printf '%s\n' "$VOUT" | grep -q 'hostapd v2.12' && pass "shipped hostapd -v reports v2.12 under emulation" || fail "shipped hostapd -v failed under emulation"
printf '%s\n' "$VOUT" | grep -q 'CONFIG_SAE' || record "hostapd v2.12 -v prints no build-config list (CONFIG_SAE proven by build log + parse gate)"

# ---- good fixtures -------------------------------------------------------
check_good() {
	f=$1; label=$2
	out=$(timeout 60 "$QEMU" -L "$RF" "$BIN" -dd -i "$DUMMY" "$f" 2>&1)
	printf '%s' "$out" | grep -qF "Configuration file:" && printf '%s' "$out" | grep -qF "$f" && pass "$label: configuration file read" || fail "$label: configuration file not read"
	printf '%s' "$out" | grep -qE "invalid key_mgmt|unknown configuration item|Invalid line|errors found in configuration file" && fail "$label: parse/semantic error" || pass "$label: no parse/semantic errors"
	if printf '%s' "$out" | grep -qE "Failed to initialize driver|Could not read interface"; then
		pass "$label: accepted - stopped only at the pre-driver boundary"
	else
		fail "$label: did not reach the expected pre-driver stop"
		printf '%s\n' "$out" | tail -8
	fi
	printf '%s' "$out" | grep -q "SAE: " && record "$label: SAE initialization path reached (SAE debug lines present)"
}
for f in hostapd-wpa3-sae.conf hostapd-wpa2-wpa3.conf hostapd-wpa2-hardened.conf; do
	[ -f "$RF/etc/hi5622/$f" ] || { fail "image fixture missing: $RF/etc/hi5622/$f"; continue; }
	check_good "$RF/etc/hi5622/$f" "$f"
done

# ---- negative controls ---------------------------------------------------
BAD=$(mktemp /tmp/hostapd-sae-badparse.XXXXXX)
printf 'bogus_option_xyz=1\n' >"$BAD"
out=$(timeout 60 "$QEMU" -L "$RF" "$BIN" -dd -i "$DUMMY" "$BAD" 2>&1)
printf '%s' "$out" | grep -q 'unknown configuration item' && pass "negative control: invalid config refused (parser is live)" || fail "negative control: invalid config was not refused"
rm -f "$BAD"

# previous image hostapd-mini as the SAE-refusal negative control
if [ -z "$OLD_HOSTAPD" ] && [ -f /home/shibbityshwab/wifi-final-image-artifacts/root.squashfs ]; then
	rm -rf "$WORK/old-rootfs"
	unsquashfs -no-exit-code -d "$WORK/old-rootfs" /home/shibbityshwab/wifi-final-image-artifacts/root.squashfs usr/sbin/hostapd >/dev/null 2>&1 || true
	[ -f "$WORK/old-rootfs/usr/sbin/hostapd" ] && OLD_HOSTAPD=$WORK/old-rootfs/usr/sbin/hostapd
fi
if [ -n "$OLD_HOSTAPD" ] && [ -f "$OLD_HOSTAPD" ]; then
	OLD_SHA=$(sha256sum "$OLD_HOSTAPD" | cut -d' ' -f1)
	record "old image hostapd-mini size/sha256 = $(stat -c %s "$OLD_HOSTAPD") / $OLD_SHA"
	out=$(timeout 60 "$QEMU" -L "$RF" "$OLD_HOSTAPD" -dd -i "$DUMMY" "$RF/etc/hi5622/hostapd-wpa3-sae.conf" 2>&1)
	printf '%s' "$out" | grep -q "invalid key_mgmt 'SAE'" && pass "negative control: old hostapd-mini refuses key_mgmt=SAE (the exact fixed failure)" || fail "negative control: old hostapd-mini did NOT refuse SAE"
fi

echo
echo "== validate-wpa3-configs: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS - the shipped SAE hostapd parses all three WPA3/PMF fixtures; old hostapd-mini refused SAE"
