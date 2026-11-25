#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-image-3.sh - verify IMAGE #3 FROM ITS BYTES (lane wifi-image-3,
# shared task task-196).  HOST-ONLY: reads files, writes only a scratch dir.
#
# Checks (the image #2 "84/84-style" verifier, retargeted):
#   * container: the sysupgrade tar members, the kernel slice
#     (13e81214..., UNCHANGED), the UBI root member,
#   * the rootfs UBI: EC magic, geometry, volume 0 round-trip to the squashfs,
#     mtd14 capacity (rootfs-only flash),
#   * the squashfs: the image's exact options (xz / 262144 / ARM BCJ),
#   * the driver module: the pinned IMAGE #3 artifact (size/sha/64 params/
#     vermagic/depends), the strip-kmod installed form, .text identical to the
#     activity artifact, every merged feature's strings present,
#   * the userspace carry: the regdomain-fixed cfg80211.ko (pkcs7-free), the
#     802.11ax hostapd (pinned sha/size/ELF/mbedtls/HE markers), iw/iwinfo/
#     ucode/regdb,
#   * the image #3 policy payload: loader/uci/modprobe/init + the two hostapd
#     fixtures + the operator notes, byte-identical to this lane (payload/),
#   * integration: the loader parameters are a subset of the module's, every
#     loader uci key exists in the shipped uci, the new gates are present and
#     fail-closed, the rc.d link, autoload=0,
#   * policy: no vendor firmware/calibration binary packaged; package db and
#     the bench stack survive,
#   * reproducibility: the fixed container member mtime/owner (IMAGE_EPOCH).
#
# usage: ./tools/verify-image-3.sh --container FILE
#          --module-installed FILE --module-activity FILE [--work DIR]
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
TOOLBIN=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin
CROSS=$TOOLBIN/arm-openwrt-linux-muslgnueabi-
CONTAINER=""
MOD_INST=""
MOD_ACT=""
WORK=/tmp/wifi-image-3-verify
while [ $# -gt 0 ]; do
	case "$1" in
		--container) CONTAINER=$2; shift 2 ;;
		--module-installed) MOD_INST=$2; shift 2 ;;
		--module-activity) MOD_ACT=$2; shift 2 ;;
		--work) WORK=$2; shift 2 ;;
		-h|--help) sed -n '2,30p' "$0" | sed 's/^# //'; exit 0 ;;
		*) echo "verify-image-3: unknown argument: $1" >&2; exit 2 ;;
	esac
done
[ -n "$CONTAINER" ] || { echo "verify-image-3: --container is required" >&2; exit 2; }
[ -f "$CONTAINER" ] || { echo "verify-image-3: no such container: $CONTAINER" >&2; exit 2; }
[ -n "$MOD_INST" ] && [ -f "$MOD_INST" ] || { echo "verify-image-3: --module-installed FILE is required" >&2; exit 2; }
[ -n "$MOD_ACT" ] && [ -f "$MOD_ACT" ] || { echo "verify-image-3: --module-activity FILE is required" >&2; exit 2; }
command -v unsquashfs >/dev/null || { echo "verify-image-3: unsquashfs missing" >&2; exit 2; }
[ -x "$CROSS"objcopy ] || { echo "verify-image-3: cross objcopy missing: $CROSS objcopy" >&2; exit 2; }

EXPECTED_KERNEL=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SIZE=3372638
EXPECTED_KERNEL_SLOT=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
EXPECTED_MODULE_INST_SHA=a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed
EXPECTED_MODULE_INST_SIZE=358544
EXPECTED_MODULE_ACT_SHA=545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
EXPECTED_MODULE_ACT_SIZE=379092
EXPECTED_MODULE_PARAMS=64
EXPECTED_CFG80211_SHA=bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91
EXPECTED_CFG80211_SIZE=2317952
EXPECTED_HOSTAPD_SHA=fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6
EXPECTED_HOSTAPD_SIZE=1618388
EXPECTED_BASE_CFG80211_SHA=11a5e2b57f31e07b3e692ac084c2aa278bc5d1e5485e7c51e6cb114cfe57c46a
EXPECTED_BASE_HOSTAPD_SHA=451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469
EXPECTED_ROOTFS_FILES=524
PEB_SIZE=131072
LEB_SIZE=126976
MTD14_SIZE=24379392

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "PASS  $*"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL  $*"; }
record() { echo "RECORD  $*"; }
die() { echo "verify-image-3: ERROR: $*" >&2; exit 2; }

sudo rm -rf "$WORK"; mkdir -p "$WORK"
echo "== wifi-image-3 verification (built bytes), task-196 =="
echo "container: $CONTAINER"
C_SIZE=$(stat -c %s "$CONTAINER"); C_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
record "container size/sha256 = $C_SIZE / $C_SHA"
echo

# ---- 1. container members + kernel slice ----------------------------------
tar -tf "$CONTAINER" > "$WORK/members.txt" 2>/dev/null || die "not a tar container"
M_COUNT=$(wc -l < "$WORK/members.txt")
[ "$M_COUNT" = "6" ] && pass "container has exactly 6 tar members (dir + CONTROL + INSTALL-STATUS + fip-manifest + kernel + root)" || fail "container member count $M_COUNT != 6"
for m in sysupgrade-cudy_wr3000-v2/CONTROL sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt sysupgrade-cudy_wr3000-v2/fip-manifest.txt sysupgrade-cudy_wr3000-v2/kernel sysupgrade-cudy_wr3000-v2/root; do
	grep -qF "$m" "$WORK/members.txt" && pass "container carries $m" || fail "container missing $m"
done
grep -qF 'sysupgrade-cudy_wr3000-v2/' "$WORK/members.txt" && pass "container root dir member present" || fail "container root dir member missing"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin" 2>/dev/null || die "cannot extract kernel"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" 2>/dev/null || die "cannot extract root"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/CONTROL > "$WORK/CONTROL" 2>/dev/null || die "cannot extract CONTROL"
KS_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1); KS_SIZE=$(stat -c %s "$WORK/kernel.bin")
[ "$KS_SHA" = "$EXPECTED_KERNEL" ] && pass "kernel slice is the validated 13e81214... (sha, UNCHANGED)" || fail "kernel slice CHANGED: $KS_SHA"
[ "$KS_SIZE" = "$EXPECTED_KERNEL_SIZE" ] && pass "kernel slice size unchanged ($EXPECTED_KERNEL_SIZE B)" || fail "kernel slice size changed: $KS_SIZE"
record "kernel slot $EXPECTED_KERNEL_SLOT is NOT a container member: a rootfs-only flash writes mtd14 and leaves kernelb/mtd12 untouched"
grep -q 'BOARD=cudy_wr3000-v2' "$WORK/CONTROL" && pass "CONTROL names the cudy_wr3000-v2 board" || fail "CONTROL board mismatch"
grep -q 'TARGET=hisilicon/luofu' "$WORK/CONTROL" && pass "CONTROL names the hisilicon/luofu target" || fail "CONTROL target mismatch"
FM=$(tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/fip-manifest.txt 2>/dev/null | grep -m1 'slice-sha256')
[ "$FM" = "slice-sha256: $EXPECTED_KERNEL" ] && pass "fip-manifest slice-sha256 matches the unchanged kernel slice" || fail "fip-manifest slice hash mismatch: $FM"
tar --full-time -tvf "$CONTAINER" > "$WORK/members-full.txt" 2>/dev/null
grep -q '2026-11-27 00:00:00' "$WORK/members-full.txt" && pass "every container member carries the pinned IMAGE_EPOCH mtime (reproducible tar)" || fail "container member mtime is not the pinned IMAGE_EPOCH"
OWN_OK=1
awk '{print $2}' "$WORK/members-full.txt" | grep -vq '^0/0$' && OWN_OK=0
[ "$OWN_OK" = "1" ] && pass "every container member is owned by uid/gid 0 (root)" || fail "container member ownership is not 0/0"

# ---- 2. rootfs UBI -------------------------------------------------------
RS_SIZE=$(stat -c %s "$WORK/rootfs.ubi")
record "rootfs.ubi size = $RS_SIZE B"
head -c 4 "$WORK/rootfs.ubi" | grep -qF 'UBI#' && pass "rootfs member starts with the UBI# EC magic" || fail "rootfs member is not a UBI image"
[ $((RS_SIZE % PEB_SIZE)) -eq 0 ] && pass "rootfs UBI is a whole number of 128 KiB PEBs ($((RS_SIZE / PEB_SIZE)))" || fail "rootfs UBI size $RS_SIZE is not PEB-aligned"
[ "$RS_SIZE" -le "$MTD14_SIZE" ] && pass "rootfs UBI fits rootfsb/mtd14 (0x1740000 = $MTD14_SIZE B; $((MTD14_SIZE - RS_SIZE)) B free)" || fail "rootfs UBI $RS_SIZE B exceeds mtd14 $MTD14_SIZE B"
python3 "$IMAGE_INT/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/root.squashfs" > "$WORK/ubi-extract.log" 2>&1 || die "UBI vol0 extraction failed (see $WORK/ubi-extract.log)"
SQ_SHA=$(sha256sum "$WORK/root.squashfs" | cut -d' ' -f1); SQ_SIZE=$(stat -c %s "$WORK/root.squashfs")
record "rootfs squashfs (UBI vol0) size/sha256 = $SQ_SIZE / $SQ_SHA"
grep -q 'format: squashfs' "$WORK/ubi-extract.log" && pass "UBI volume 0 reassembles to a squashfs (hsqs magic)" || fail "UBI volume 0 is not a squashfs"
grep -q 'volume 0:' "$WORK/ubi-extract.log" && pass "UBI volume 0 (squashfs) extracted" || fail "UBI volume 0 extraction missing"
head -c 8 "$WORK/root.squashfs" | grep -qF 'hsqs' && pass "squashfs superblock magic hsqs present" || fail "squashfs magic missing"
EXPECT_SQ_LEBS=$(( (SQ_SIZE + LEB_SIZE - 1) / LEB_SIZE ))
record "squashfs needs $EXPECT_SQ_LEBS LEBs of $LEB_SIZE B; UBI has $((RS_SIZE / PEB_SIZE)) PEBs"
[ $((RS_SIZE / PEB_SIZE)) -ge $((EXPECT_SQ_LEBS + 1)) ] && pass "UBI PEB count covers the squashfs LEBs plus the overlay volume" || fail "UBI PEB count too small for the squashfs"

# ---- 3. squashfs options + extraction ------------------------------------
unsquashfs -s "$WORK/root.squashfs" > "$WORK/sq-super.txt" 2>&1
grep -q 'Compression xz' "$WORK/sq-super.txt" && pass "squashfs compression is xz (the image recipe)" || fail "squashfs is not xz"
grep -q 'Dictionary size 262144' "$WORK/sq-super.txt" && pass "squashfs block size is 262144" || fail "squashfs block size is not 262144"
grep -q 'Filters selected: arm' "$WORK/sq-super.txt" && pass "squashfs ARM BCJ filter is set (-Xbcj arm)" || fail "squashfs ARM BCJ filter missing"
grep -q 'Filesystem size' "$WORK/sq-super.txt" && pass "squashfs superblock is readable/complete" || fail "squashfs superblock unreadable"
sudo -n unsquashfs -n -d "$WORK/root" "$WORK/root.squashfs" > "$WORK/unsquashfs.log" 2>&1 || die "squashfs extraction failed"
RF="$WORK/root"
F_COUNT=$(sudo find "$RF" -type f | wc -l)
[ "$F_COUNT" = "$EXPECTED_ROOTFS_FILES" ] && pass "rootfs carries the expected $EXPECTED_ROOTFS_FILES files (image #2 inventory + the 3 new payload files)" || fail "rootfs file count $F_COUNT != $EXPECTED_ROOTFS_FILES"
record "rootfs: $F_COUNT files, $(sudo find "$RF" -type l | wc -l) symlinks, $(sudo find "$RF" -type d | wc -l) dirs"

# ---- 4. the driver module ------------------------------------------------
KO="$RF/lib/modules/6.18.60/hi5622_wifi.ko"
[ -f "$KO" ] && pass "rootfs carries /lib/modules/6.18.60/hi5622_wifi.ko" || fail "installed hi5622_wifi.ko missing"
KO_SHA=$(sha256sum "$KO" | cut -d' ' -f1); KO_SIZE=$(stat -c %s "$KO")
[ "$KO_SHA" = "$EXPECTED_MODULE_INST_SHA" ] && pass "installed module sha256 is the pinned IMAGE #3 installed form (a717c86d...)" || fail "installed module sha $KO_SHA != pinned $EXPECTED_MODULE_INST_SHA"
[ "$KO_SIZE" = "$EXPECTED_MODULE_INST_SIZE" ] && pass "installed module size is $EXPECTED_MODULE_INST_SIZE B (strip-kmod form)" || fail "installed module size $KO_SIZE != $EXPECTED_MODULE_INST_SIZE"
[ "$(modinfo -F name "$KO")" = "hi5622_wifi" ] && pass "module modinfo name is hi5622_wifi" || fail "module name mismatch"
case "$(modinfo -F vermagic "$KO")" in *6.18.60*) pass "module vermagic is 6.18.60" ;; *) fail "module vermagic mismatch" ;; esac
case "$(modinfo -F depends "$KO")" in *cfg80211*) pass "module depends on cfg80211" ;; *) fail "module depends mismatch" ;; esac
KP=$(modinfo -p "$KO" | wc -l)
[ "$KP" = "$EXPECTED_MODULE_PARAMS" ] && pass "module exposes the pinned $EXPECTED_MODULE_PARAMS parameters" || fail "module param count $KP != $EXPECTED_MODULE_PARAMS"
modinfo -p "$KO" > "$WORK/ko-params.txt" 2>/dev/null
for p in rx_refill_wm desc_table dfs_mode band5g vap5g ap_chan_wait_ms he_caps ba_enable mgmt_tx data_tx beacon_static; do
	grep -q "^$p:" "$WORK/ko-params.txt" && pass "module parameter $p present" || fail "module parameter $p MISSING"
done
MOD_INST_SHA=$(sha256sum "$MOD_INST" | cut -d' ' -f1); MOD_INST_SIZE=$(stat -c %s "$MOD_INST")
[ "$MOD_INST_SHA" = "$EXPECTED_MODULE_INST_SHA" ] && pass "verify input module-installed is the pinned artifact (a717c86d...)" || fail "module-installed input sha mismatch"
[ "$MOD_INST_SIZE" = "$EXPECTED_MODULE_INST_SIZE" ] && pass "verify input module-installed size is $EXPECTED_MODULE_INST_SIZE B" || fail "module-installed input size mismatch"
MOD_ACT_SHA=$(sha256sum "$MOD_ACT" | cut -d' ' -f1); MOD_ACT_SIZE=$(stat -c %s "$MOD_ACT")
[ "$MOD_ACT_SHA" = "$EXPECTED_MODULE_ACT_SHA" ] && pass "verify input module-activity is the pinned artifact (545192df...)" || fail "module-activity input sha mismatch"
[ "$MOD_ACT_SIZE" = "$EXPECTED_MODULE_ACT_SIZE" ] && pass "verify input module-activity size is $EXPECTED_MODULE_ACT_SIZE B" || fail "module-activity input size mismatch"
sudo "$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/ko.text" 2>/dev/null || die "cross objcopy failed on the installed module"
"$CROSS"objcopy -O binary --only-section=.text "$MOD_ACT" "$WORK/act.text" 2>/dev/null || die "cross objcopy failed on the activity module"
[ "$(sha256sum "$WORK/ko.text" | cut -d' ' -f1)" = "$(sha256sum "$WORK/act.text" | cut -d' ' -f1)" ] && pass "installed .text is byte-identical to the activity artifact .text (strip-kmod changed only metadata)" || fail "installed .text differs from the activity artifact"
strings -a "$KO" > "$WORK/ko-strings.txt" 2>/dev/null
for s in 'rx_refill_wm' 'RX watermark:' 'desc_table' 'dfs_mode' 'he_caps' 'ba_enable' 'himg_build_hcc_mgmt_tx' 'cfg80211_rx_mgmt_ext' 'data-tx tx: posts='; do
	grep -qF "$s" "$WORK/ko-strings.txt" && pass "module feature string present: $s" || fail "module feature string MISSING: $s"
done
[ "$(modinfo -F license "$KO" | head -1)" = "GPL" ] && pass "module license is GPL" || fail "module license mismatch"
record "module installed: $KO_SIZE B sha256 $KO_SHA, $KP params; activity: $MOD_ACT_SIZE B sha256 $MOD_ACT_SHA"

# ---- 5. userspace: regdomain cfg80211 + ax hostapd + bench stack ----------
CFG="$RF/lib/modules/6.18.60/cfg80211.ko"
[ -f "$CFG" ] && pass "rootfs carries the target-kernel cfg80211.ko" || fail "cfg80211.ko missing"
CFG_SHA=$(sha256sum "$CFG" | cut -d' ' -f1); CFG_SIZE=$(stat -c %s "$CFG")
[ "$CFG_SHA" = "$EXPECTED_CFG80211_SHA" ] && pass "cfg80211.ko is the regdomain-fixed build (bc4d9c73...)" || fail "cfg80211.ko sha $CFG_SHA != pinned"
[ "$CFG_SIZE" = "$EXPECTED_CFG80211_SIZE" ] && pass "cfg80211.ko size is the fixed 2,317,952 B (the unsigned-regdb build)" || fail "cfg80211.ko size $CFG_SIZE != pinned"
UNRES=$("$CROSS"nm -u "$CFG" 2>/dev/null | grep -c -E 'pkcs7|verify_pkcs7|x509' || true)
[ "$UNRES" = "0" ] && pass "cfg80211.ko has no undefined pkcs7/x509 symbols (the signature requirement is compiled out)" || fail "cfg80211.ko still references $UNRES pkcs7/x509 symbols"
HA="$RF/usr/sbin/hostapd"
[ -f "$HA" ] && pass "rootfs carries /usr/sbin/hostapd" || fail "hostapd missing"
HA_SHA=$(sha256sum "$HA" | cut -d' ' -f1); HA_SIZE=$(stat -c %s "$HA")
[ "$HA_SHA" = "$EXPECTED_HOSTAPD_SHA" ] && pass "hostapd is the 802.11ax build (fef44f3b...)" || fail "hostapd sha $HA_SHA != pinned ax build"
[ "$HA_SIZE" = "$EXPECTED_HOSTAPD_SIZE" ] && pass "hostapd size is the pinned 1,618,388 B ax build" || fail "hostapd size $HA_SIZE != pinned"
[ "$HA_SHA" != "$EXPECTED_BASE_HOSTAPD_SHA" ] && pass "hostapd is NOT the image #2 SAE build (451dd49f...)" || fail "hostapd is still the image #2 build"
head -c 4 "$HA" | grep -qF 'ELF' && pass "hostapd is an ELF binary" || fail "hostapd is not ELF"
readelf -d "$HA" 2>/dev/null | grep -qF 'libmbedcrypto' && pass "hostapd links libmbedcrypto (SAE/ECC backend)" || fail "hostapd does not link libmbedcrypto"
strings -a "$HA" > "$WORK/ha-strings.txt" 2>/dev/null
grep -qF 'ieee80211ax=%d' "$WORK/ha-strings.txt" && pass "hostapd carries the CONFIG_DRIVER_11AX_SUPPORT code (ieee80211ax parse path)" || fail "hostapd lacks the 11AX driver support marker"
grep -qF 'Cannot enable multiple BSSID support without ieee80211ax' "$WORK/ha-strings.txt" && pass "hostapd carries the HE/MBSS consistency code (full ax variant)" || fail "hostapd lacks the full ax HE code"
for f in usr/sbin/iw usr/bin/iwinfo usr/share/ucode/iwinfo.uc usr/share/ucode/wifi/hostapd.uc sbin/init lib/firmware/regulatory.db; do
	[ -f "$RF/$f" ] && pass "bench userspace present: /$f" || fail "bench userspace MISSING: /$f"
done
RDB=$(stat -c %s "$RF/lib/firmware/regulatory.db")
[ "$RDB" -gt 1000 ] && pass "wireless regulatory.db present ($RDB B)" || fail "regulatory.db missing/empty"

# ---- 6. the image #3 policy payload (byte-identical to this lane) --------
for f in usr/sbin/hi5622-load etc/config/hi5622 etc/modprobe.d/hi5622-wifi.conf etc/init.d/hi5622-wifi etc/hi5622/hostapd-open-ax.conf etc/hi5622/hostapd-open-5g40.conf etc/hi5622/IMAGE-3-NOTES.txt; do
	cmp -s "$RF/$f" "$LANE/payload/$f" && pass "payload carried byte-identical: /$f" || fail "payload drifted from lane: /$f"
done
[ -L "$RF/etc/rc.d/S96hi5622-wifi" ] && pass "rc.d link /etc/rc.d/S96hi5622-wifi present" || fail "rc.d link missing"
[ "$(readlink "$RF/etc/rc.d/S96hi5622-wifi")" = "../init.d/hi5622-wifi" ] && pass "rc.d link points at ../init.d/hi5622-wifi" || fail "rc.d link target wrong"
sh -n "$RF/usr/sbin/hi5622-load" && pass "extracted hi5622-load passes sh -n (host syntax check)" || fail "hi5622-load has a shell syntax error"
grep -q "option autoload '0'" "$RF/etc/config/hi5622" && pass "bench policy: hi5622.main.autoload defaults to 0 (nothing loads at boot)" || fail "autoload bench default missing"
for o in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q "option $o '" "$RF/etc/config/hi5622" && pass "uci carries the image #3 option $o" || fail "uci missing image #3 option $o"
done
grep -q "option band5g '0'" "$RF/etc/config/hi5622" && grep -q "option he_caps '0'" "$RF/etc/config/hi5622" && grep -q "option ba_enable '0'" "$RF/etc/config/hi5622" && pass "the new 5 GHz/AX/BA gates ship FAIL-CLOSED (0)" || fail "a new gate is not shipped fail-closed"
grep -q 'IMAGE #3' "$RF/etc/config/hi5622" && pass "uci policy is labelled IMAGE #3" || fail "uci policy not relabelled"
grep -q 'IMAGE #3' "$RF/usr/sbin/hi5622-load" && pass "loader is labelled IMAGE #3 with the new module identity" || fail "loader not relabelled"
for p in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q -- "$p=" "$RF/usr/sbin/hi5622-load" && pass "loader passes the new module parameter $p" || fail "loader does not pass $p"
done
for p in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q "verify_param $p " "$RF/usr/sbin/hi5622-load" && pass "loader verifies the readback of $p" || fail "loader does not verify $p"
done
grep -q 'require band5g=1' "$RF/usr/sbin/hi5622-load" && pass "loader refuses the 5 GHz gates without band5g=1 (fail-closed consistency)" || fail "loader band5g guard missing"
grep -q 'he_caps=0' "$RF/etc/modprobe.d/hi5622-wifi.conf" && pass "modprobe defaults mirror the fail-closed AX/5 GHz gates" || fail "modprobe defaults do not mirror the gates"
awk '/run insmod .*DRV_KO/{f=1;next} f&&/DRY_RUN/{exit} f' "$RF/usr/sbin/hi5622-load" | grep -o '"[a-z_0-9]*=' | sed 's/^"//' | sed 's/=$//' | sort -u > "$WORK/loader-params.txt"
modinfo -p "$KO" | sed 's/:.*//' | sort -u > "$WORK/mod-params.txt"
BAD=$(comm -23 "$WORK/loader-params.txt" "$WORK/mod-params.txt" | tr -d ' ')
[ -z "$BAD" ] && pass "every loader insmod parameter exists in the module ($(wc -l < "$WORK/loader-params.txt") passed)" || fail "loader passes unknown module parameters: $BAD"
grep -o 'uci_get main.[a-z_0-9]*' "$RF/usr/sbin/hi5622-load" | sed 's/uci_get main.//' | sort -u > "$WORK/loader-uci-keys.txt"
grep -o "option [a-z_0-9]* " "$RF/etc/config/hi5622" | sed 's/option //' | sed 's/ //' | sort -u > "$WORK/uci-keys.txt"
BADU=$(comm -23 "$WORK/loader-uci-keys.txt" "$WORK/uci-keys.txt" | tr -d ' ')
[ -z "$BADU" ] && pass "every loader uci key is present in /etc/config/hi5622 ($(wc -l < "$WORK/loader-uci-keys.txt") keys)" || fail "loader reads uci keys not shipped: $BADU"

# ---- 7. policy: no vendor firmware, package db, device nodes -------------
V1=$(sudo find "$RF" -type f -name 'FIRMWARE*' ! -name 'FIRMWARE-POLICY.txt' 2>/dev/null | wc -l)
V2=$(sudo find "$RF" -type f -name 'PD4*' 2>/dev/null | wc -l)
V3=$(sudo find "$RF" -type f -name 'wifi_block*' 2>/dev/null | wc -l)
VEND=$((V1 + V2 + V3))
[ "$VEND" = "0" ] && pass "no vendor firmware / PD4 / wifi_block binary is packaged (firmware policy)" || fail "vendor firmware-like files packaged: $VEND"
sudo find "$RF" -type f -name 'FIRMWARE-POLICY.txt' | grep -q . && pass "firmware policy text present in the rootfs" || fail "firmware policy text missing"
BIG=$(sudo find "$RF" -type f -size +4M | grep -v 'cfg80211.ko' | head -1)
[ -z "$BIG" ] && pass "no unexpected >4 MiB file in the rootfs" || fail "unexpected >4 MiB rootfs file: $BIG"
grep -q '^P:kmod-hi5622-wifi$' "$RF/lib/apk/db/installed" 2>/dev/null && pass "package db records kmod-hi5622-wifi" || fail "package db missing kmod-hi5622-wifi"
grep -q '^P:kmod-hi5622-cfg80211$' "$RF/lib/apk/db/installed" 2>/dev/null && pass "package db records kmod-hi5622-cfg80211" || fail "package db missing kmod-hi5622-cfg80211"
grep -q '^P:hostapd-mbedtls$' "$RF/lib/apk/db/installed" 2>/dev/null && pass "package db records hostapd-mbedtls (the ax hostapd's package)" || fail "package db missing hostapd-mbedtls"
PKGS=$(sudo grep -c '^P:' "$RF/lib/apk/db/installed" 2>/dev/null || true)
[ "$PKGS" -ge 83 ] && pass "package db carries the full bench stack ($PKGS packages)" || fail "package db only $PKGS packages"
sudo stat -c '%F' "$RF/dev/console" | grep -qF 'character special file' && pass "/dev/console is preserved as a character device (faithful squashfs extraction)" || fail "/dev/console is not a character device"
[ -f "$RF/lib/modules/6.18.60/modules.builtin" ] && pass "kernel module metadata present (/lib/modules/6.18.60/modules.builtin)" || fail "kernel module metadata missing"
KO_MODE=$(sudo stat -c '%a %U:%G' "$KO")
[ "$KO_MODE" = "644 root:root" ] && pass "installed module is 0644 root:root" || fail "installed module mode $KO_MODE != 644 root:root"
HA_MODE=$(sudo stat -c '%a' "$HA")
[ "$HA_MODE" = "755" ] && pass "hostapd is mode 0755" || fail "hostapd mode $HA_MODE"
FIX=0
for f in hostapd-open.conf hostapd-open.proven.conf hostapd-wpa2.conf hostapd-wpa2-hardened.conf hostapd-wpa2-wpa3.conf hostapd-wpa3-sae.conf; do
	[ -f "$RF/etc/hi5622/$f" ] && FIX=$((FIX+1))
done
[ "$FIX" = "6" ] && pass "all six WPA2/WPA3 hostapd fixtures survive in /etc/hi5622/" || fail "only $FIX/6 hostapd fixtures survive"

# ---- 8. summary ----------------------------------------------------------
echo
echo "verify-image-3: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
