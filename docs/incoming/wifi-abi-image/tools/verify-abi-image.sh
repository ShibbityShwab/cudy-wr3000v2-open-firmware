#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-abi-image.sh - verify from the BUILT BYTES that the rebuilt bench
# image closes the kmod kernel-ABI mismatch (task-128):
#
#   * kernel slice sha256 + full-kernel-slot sha256 (the flash contract);
#   * the kernel package version inside the image's apk db equals
#     6.18.60~<kernel .config vermagic>-r1 recomputed from the current tree
#     (include/kernel-defaults.mk: md5 of the sorted =[ym] kernel-config lines);
#   * the kmod .apk kernel= dependency equals the same string (the exact pin
#     the T0 full-set deploy checks on the device);
#   * the packaged cfg80211.ko / hi5622_wifi.ko vermagic and depends;
#   * the bench userspace/driver payload is present (iw, iwinfo, libiwinfo,
#     wireless-regdb, cfg80211.ko, hi5622_wifi.ko);
#   * the flashable members are extracted to the artifacts dir with a
#     SHA256SUMS file (kernel-slice.bin, rootfs.ubi, root.squashfs, container).
#
# usage: ./tools/verify-abi-image.sh [--owrt DIR] [--container FILE]
#        [--artifacts DIR] [--work DIR] [--evidence FILE]
#        [--allow-kernel-change] [-h|--help]
#
# Host-only: reads files only; no device I/O, no flash command.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
INTEG=$(cd "$LANE/../wifi-image-integration" && pwd)
DEPS=$(cd "$LANE/../wifi-image-deploy" 2>/dev/null && pwd) || DEPS=
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
EVID=$LANE/build-evidence
WORK=
ART=
OWRT=$DEFAULT_OWRT
CONTAINER=
ALLOW_KERNEL_CHANGE=0

KERNEL_VERMAGIC=6f995278d21563ed4dd8f2609f3659c8
KERNEL_DEP=6.18.60~${KERNEL_VERMAGIC}-r1
EXPECTED_KERNEL_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_SLOT_SHA=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
EXPECTED_CONTAINER_SHA=df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310
EXPECTED_ROOTFS_UBI_SHA=62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746
EXPECTED_PKG_COUNT=81
KERNEL_SLOT_BYTES=8650752
UBI_EXTRACTOR=$INTEG/tools/extract-ubi-rootfs.py

PASSES=0
FAILS=0
ok()  { PASSES=$((PASSES + 1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
eq()  { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2' want '$3')"; fi; }
die() { bad "$*"; finish; }
finish() {
	printf '\n== verify-abi-image: %d passed, %d failed ==\n' "$PASSES" "$FAILS"
	if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
	printf 'RESULT: PASS - image apk-db kernel version == tree vermagic == kmod kernel= pin; kernel slice/slot/flash members match\n'
	exit 0
}

usage() { sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=$2; shift 2 ;;
		--container) CONTAINER=$2; shift 2 ;;
		--artifacts) ART=$2; shift 2 ;;
		--work) WORK=$2; shift 2 ;;
		--evidence) EVID=$2; shift 2 ;;
		--allow-kernel-change) ALLOW_KERNEL_CHANGE=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
	esac
done

[ -n "$ART" ] || { printf 'missing --artifacts\n' >&2; exit 2; }
[ -z "$WORK" ] && WORK=$(mktemp -d /tmp/abi-verify.XXXXXX)
mkdir -p "$WORK" "$ART" "$(dirname "$EVID")"
if [ -f "$UBI_EXTRACTOR" ]; then :; else printf 'missing UBI extractor: %s\n' "$UBI_EXTRACTOR" >&2; exit 2; fi
command -v unsquashfs >/dev/null 2>&1 || { printf 'unsquashfs not found\n' >&2; exit 2; }
[ -d "$OWRT" ] || { printf 'no OpenWrt tree: %s\n' "$OWRT" >&2; exit 2; }

# NOTE: the evidence tee must not capture early argument errors; start it here.
exec > >(tee "$EVID") 2>&1

printf '== kernel-ABI image verification (built bytes) ==\n'
printf 'tree      : %s\n' "$OWRT"
printf 'artifacts : %s\n' "$ART"
printf 'work      : %s\n\n' "$WORK"

# ---------------------------------------------------------------- container ---
TARGETS=$OWRT/bin/targets/hisilicon/luofu
if [ -z "$CONTAINER" ]; then
	CONTAINER=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
fi
[ -n "$CONTAINER" ] && [ -f "$CONTAINER" ] || die "no sysupgrade container (--container or $TARGETS)"
C_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
C_SIZE=$(stat -c %s "$CONTAINER")
printf 'container : %s\nbuilt     : %s\nsize/sha  : %s B / %s\n' \
	"$CONTAINER" "$(stat -c %y "$CONTAINER")" "$C_SIZE" "$C_SHA"
if [ "$C_SHA" = "$EXPECTED_CONTAINER_SHA" ]; then
	ok "container sha256 reproduces the recorded bench image df6a78c3"
else
	printf 'WARN container sha256 differs from the recorded df6a78c3 (new build; rootfs may still be identical)\n'
fi
cp -f "$CONTAINER" "$ART/bench-image-sysupgrade.bin"

tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$ART/kernel-slice.bin" 2>/dev/null || die "cannot extract the kernel member"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$ART/rootfs.ubi" 2>/dev/null || die "cannot extract the root member"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/fip-manifest.txt > "$ART/fip-manifest.txt" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/CONTROL > "$ART/CONTROL" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt > "$ART/INSTALL-STATUS.txt" 2>/dev/null || true

K_SIZE=$(stat -c %s "$ART/kernel-slice.bin")
K_SHA=$(sha256sum "$ART/kernel-slice.bin" | cut -d' ' -f1)
R_SIZE=$(stat -c %s "$ART/rootfs.ubi")
R_SHA=$(sha256sum "$ART/rootfs.ubi" | cut -d' ' -f1)
printf 'kernel    : %s B / %s\nrootfs ubi: %s B / %s\n' "$K_SIZE" "$K_SHA" "$R_SIZE" "$R_SHA"

if [ "$K_SHA" = "$EXPECTED_KERNEL_SHA" ]; then
	ok "kernel slice sha256 == hardware-validated 13e81214"
elif [ "$ALLOW_KERNEL_CHANGE" -eq 1 ]; then
	printf 'WARN kernel slice sha256 changed (allowed): %s\n' "$K_SHA"
else
	bad "kernel slice sha256 ${K_SHA:0:8} != validated 13e81214 (use --allow-kernel-change only if intended)"
fi
SLOT_SHA=$( { cat "$ART/kernel-slice.bin"; head -c $((KERNEL_SLOT_BYTES - K_SIZE)) /dev/zero | tr '\0' '\377'; } | sha256sum | cut -d' ' -f1)
eq "full kernel slot sha256 (kernelb flash contract)" "$SLOT_SHA" "$EXPECTED_SLOT_SHA"
if [ "$R_SHA" = "$EXPECTED_ROOTFS_UBI_SHA" ]; then
	ok "rootfs UBI sha256 reproduces the recorded image rootfs 62344e80"
else
	printf 'WARN rootfs UBI sha256 changed (expected if the driver/packages changed): %s\n' "$R_SHA"
fi

# --------------------------------------------------------- rootfs content ----
python3 "$UBI_EXTRACTOR" "$ART/rootfs.ubi" -o "$ART/root.squashfs" --vol-id 0 > "$WORK/ubi.log" 2>&1 || die "UBI -> squashfs extraction failed"
SQ_SIZE=$(stat -c %s "$ART/root.squashfs")
SQ_SHA=$(sha256sum "$ART/root.squashfs" | cut -d' ' -f1)
printf 'squashfs  : %s B / %s\n\n' "$SQ_SIZE" "$SQ_SHA"
rm -rf "$WORK/root"
unsquashfs -n -no-exit-code -d "$WORK/root" "$ART/root.squashfs" > /dev/null 2>&1 || die "unsquashfs failed"
ROOT=$WORK/root
[ -f "$ROOT/lib/apk/db/installed" ] || die "rootfs lacks /lib/apk/db/installed"

# tree kernel vermagic: OpenWrt = md5 of sorted =[ym] kernel-config lines
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
CONFIG_SET=$KDIR/linux-6.18.60/.config.set
VM_FILE=$KDIR/linux-6.18.60/.vermagic
TREE_VM=absent
if [ -f "$CONFIG_SET" ]; then
	TREE_VM=$(grep '=[ym]' "$CONFIG_SET" | LC_ALL=C sort | md5sum | cut -d' ' -f1)
	ok "tree kernel .config.set present (recomputed vermagic $TREE_VM)"
else
	bad "tree kernel .config.set missing: $CONFIG_SET"
fi
if [ -f "$VM_FILE" ]; then
	eq "tree .vermagic file == recomputed vermagic" "$(cat "$VM_FILE")" "$TREE_VM"
else
	bad "tree .vermagic missing: $VM_FILE"
fi
eq "tree vermagic == the kmod .apk pin (6f995278...)" "$TREE_VM" "$KERNEL_VERMAGIC"

# 1) the kernel package version inside the image's apk db
DB=$ROOT/lib/apk/db/installed
ROOT_VM=$(sed -n '/^P:kernel$/,/^V:/{s/^V:6.18.60~\([0-9a-f]*\)-r1$/\1/p}' "$DB" | head -1)
IMG_KPKG=$(sed -n '/^P:kernel$/,/^$/{s/^V: *//p}' "$DB" | head -1)
printf 'image kernel package version : %s\n' "$IMG_KPKG"
eq "image apk-db kernel version == 6.18.60~$TREE_VM-r1 (the ABI fix)" "$IMG_KPKG" "$KERNEL_DEP"

# 2) the kmods inside the image depend on that exact kernel package
for pkg in kmod-hi5622-cfg80211 kmod-hi5622-wifi; do
	dep=$(sed -n "/^P:$pkg$/,/^\$/p" "$DB" | grep -o 'kernel=6.18.60~[0-9a-f]*-r1' | head -1)
	eq "image db: $pkg depends on kernel=6.18.60~$TREE_VM-r1" "$dep" "kernel=$KERNEL_DEP"
done

# 3) the buildable kmod .apk artifacts pin the same kernel package
APK=$OWRT/staging_dir/host/bin/apk
for pkg in kmod-hi5622-cfg80211 kmod-hi5622-wifi; do
	apkfile=$(ls "$TARGETS"/packages/${pkg}-6.18.60-r1.apk 2>/dev/null | head -1)
	if [ -n "$apkfile" ] && [ -x "$APK" ]; then
		dep=$("$APK" adbdump "$apkfile" 2>/dev/null | sed -n '/^  depends:/,/^[^ ]/p' | grep -o 'kernel=6.18.60~[0-9a-f]*-r1' | head -1)
		eq "artefact $pkg.apk depends on kernel=6.18.60~$TREE_VM-r1" "$dep" "kernel=$KERNEL_DEP"
		cp -f "$apkfile" "$ART/$(basename "$apkfile")"
	else
		bad "missing kmod artefact ${pkg}-6.18.60-r1.apk or host apk tool"
	fi
done

# 4) required payload + module metadata
while IFS='|' read -r rel desc; do
	[ -z "$rel" ] && continue
	if [ -e "$ROOT/$rel" ]; then
		printf 'PRESENT  %-44s %9s B sha256 %s  # %s\n' "$rel" "$(stat -c %s "$ROOT/$rel")" "$(sha256sum "$ROOT/$rel" | cut -d' ' -f1)" "$desc"
		ok "rootfs carries $rel"
	else
		bad "rootfs missing $rel (# $desc)"
	fi
done <<'EOF'
usr/sbin/iw|iw 6.17 nl80211 CLI
usr/bin/iwinfo|iwinfo C CLI
usr/lib/libiwinfo.so.20230701|libiwinfo shared library
usr/share/libiwinfo/devices.txt|libiwinfo device data
lib/firmware/regulatory.db|wireless regulatory database
lib/modules/6.18.60/cfg80211.ko|cfg80211 from the target kernel tree
lib/modules/6.18.60/hi5622_wifi.ko|clean-room Hi5622 Wi-Fi host driver
EOF
for mod in cfg80211 hi5622_wifi; do
	ko=$ROOT/lib/modules/6.18.60/$mod.ko
	if [ -f "$ko" ]; then
		vm=$(modinfo -F vermagic "$ko" 2>/dev/null | awk '{print $1}')
		eq "$mod.ko modules vermagic 6.18.60" "$vm" "6.18.60"
	else
		bad "$mod.ko missing (module metadata check)"
	fi
done
depends=$(modinfo -F depends "$ROOT/lib/modules/6.18.60/hi5622_wifi.ko" 2>/dev/null | tr -d ' ')
eq "hi5622_wifi.ko depends on cfg80211" "$depends" "cfg80211"

# 5) package count and the userspace .apk set (for the T0 deploy)
eq "image installed package count" "$(grep -c '^P:' "$DB")" "$EXPECTED_PKG_COUNT"
for apkfile in "$OWRT"/bin/packages/arm_cortex-a9/base/iw-*.apk \
               "$OWRT"/bin/packages/arm_cortex-a9/base/iwinfo-*.apk \
               "$OWRT"/bin/packages/arm_cortex-a9/base/libiwinfo20230701-*.apk \
               "$OWRT"/bin/packages/arm_cortex-a9/base/libiwinfo-data-*.apk \
               "$OWRT"/bin/packages/arm_cortex-a9/base/wireless-regdb-*.apk; do
	[ -f "$apkfile" ] || { bad "missing userspace artefact $apkfile"; continue; }
	cp -f "$apkfile" "$ART/$(basename "$apkfile")"
done
N_APK=$(find "$ART" -maxdepth 1 -name '*.apk' | wc -l)
eq "seven deployable .apk artefacts in the artifacts dir" "$N_APK" "7"

# 6) squashfs volume consistency with the build_dir artifact
BUILT_SQ=$KDIR/root.squashfs
if [ -f "$BUILT_SQ" ]; then
	BSIZE=$(stat -c %s "$BUILT_SQ")
	if [ "$BSIZE" -le "$SQ_SIZE" ] && cmp -s -n "$BSIZE" "$BUILT_SQ" "$ART/root.squashfs"; then
		ok "squashfs volume == build_dir/root.squashfs ($BSIZE B) + ubinize padding"
	else
		bad "squashfs volume differs from build_dir/root.squashfs"
	fi
fi

# ------------------------------------------------------------- artifacts ------
(
	cd "$ART" || exit 1
	for f in *; do
		[ "$f" = SHA256SUMS ] && continue
		sha256sum "$f"
	done | LC_ALL=C sort -k2
) > "$ART/SHA256SUMS"
printf '\n-- flashable artifacts (%s) --\n' "$ART"
sed 's/^/  /' "$ART/SHA256SUMS"
finish
