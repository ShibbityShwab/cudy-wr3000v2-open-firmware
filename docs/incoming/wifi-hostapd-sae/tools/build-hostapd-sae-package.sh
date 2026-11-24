#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-hostapd-sae-package.sh - build an SAE-capable hostapd package for the
# Cudy WR3000 v2.0 bench image (wifi-hostapd-sae lane, task-148).  HOST-ONLY.
#
# What it does
#   1. selects hostapd-mbedtls (OpenWrt "full" hostapd variant; its package
#      Makefile adds CONFIG_SAE=y + CONFIG_TLS=mbedtls for the mbedtls full
#      variant) in the shared OpenWrt release-line tree, next to the baseline
#      libmbedtls21 selection that the bench image already carries;
#   2. runs "make defconfig", checks the symbols resolved, and builds the
#      package with the tree's own arm_cortex-a9 musl toolchain;
#   3. proves the built ARM binary is an SAE build: ELF identity, DT_NEEDED
#      mbedtls closure, compile line -DCONFIG_SAE, SAE code strings, and the
#      built variant's kconfig (the SAE flag is a command-line make option, so
#      it is proven from the build log, not from hostapd/.config);
#   4. records the .apk + binary identities and keeps a local copy in
#      $ART_DIR (never committed: binaries stay local, hashes go to git);
#   5. restores the shared tree (.config + built kernel .config byte-identical).
#
# The tree is the project's shared release line; one world build at a time is
# enforced by /tmp/cudy-openwrt-world-build.lock (same lock as the image bake).
#
# usage: ./tools/build-hostapd-sae-package.sh [--owrt DIR] [--jobs N]
#   env: WORK_DIR, ART_DIR
set -euo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
WORK=${WORK_DIR:-/tmp/wifi-hostapd-sae-build}
ART=${ART_DIR:-/home/shibbityshwab/wifi-hostapd-sae-artifacts}
JOBS=${JOBS:-$(nproc)}
EXPECTED_TREE_HEAD=27663a0
EXPECTED_BASE_CONFIG_SHA=9052d5e36fd30e6c59276910f2b286d5a109a5dffd03f3a8e1386aece0ed075d
LOCKFILE=/tmp/cudy-openwrt-world-build.lock
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
KERNEL_CFG=$KDIR/linux-6.18.60/.config
HOSTAPD_PKG=$OWRT/package/network/services/hostapd
CONFIG_DELTA=$LANE/config-hostapd-mbedtls.config

die() { echo "build-hostapd-sae-package: ERROR: $*" >&2; exit 1; }
log() { echo "build-hostapd-sae-package: $*"; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=${2:-}; shift 2 ;;
		--owrt=*) OWRT=${1#*=}; shift ;;
		--jobs) JOBS=${2:-}; JOBS=${JOBS#-j}; shift 2 ;;
		--jobs=*) JOBS=${1#*=}; JOBS=${JOBS#-j}; shift ;;
		-h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done
[ -n "${JOBS:-}" ] || JOBS=$(nproc)

mkdir -p "$WORK" "$ART"

# ---------------------------------------------------------------------------
# preflight
# ---------------------------------------------------------------------------
[ -f "$OWRT/rules.mk" ] && [ -f "$OWRT/.config" ] || die "not an OpenWrt tree: $OWRT"
[ -d "$HOSTAPD_PKG" ] || die "hostapd package missing: $HOSTAPD_PKG"
[ -f "$OWRT/dl/hostapd-2026.08.07~831364bf.tar.zst" ] || \
	die "hostapd source tarball not prefetched under $OWRT/dl"
TREE_HEAD=$(git -C "$OWRT" rev-parse --short HEAD 2>/dev/null || echo unknown)
[ "$TREE_HEAD" = "$EXPECTED_TREE_HEAD" ] || die "tree HEAD $TREE_HEAD != pinned $EXPECTED_TREE_HEAD"
[ -f "$KERNEL_CFG" ] || die "built kernel .config missing: $KERNEL_CFG"
[ -e "$OWRT/files" ] && die "$OWRT/files already exists; another lane is staging an overlay"
BASE_CFG_SHA=$(sha256sum "$OWRT/.config" | cut -d' ' -f1)
if [ "$BASE_CFG_SHA" != "$EXPECTED_BASE_CONFIG_SHA" ]; then
	log "WARNING: tree .config sha $BASE_CFG_SHA differs from the recorded baseline $EXPECTED_BASE_CONFIG_SHA"
	log "         (still restoring it byte-identically after this build)"
fi
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq 'build-hostapd-sae-package'; then
	die "a make command line references $OWRT; another lane is building - retry later"
fi
exec 9>"$LOCKFILE"
flock -n 9 || die "another world build holds $LOCKFILE - retry later"
log "lock acquired: $LOCKFILE"

CLEANED=0
cleanup() {
	rc=$?
	trap - EXIT
	if [ "$CLEANED" -eq 0 ]; then
		CLEANED=1
		set +e
		log "restoring shared-tree state"
		[ -f "$WORK/.config.pre-build" ] && cp -p "$WORK/.config.pre-build" "$OWRT/.config"
		[ -f "$WORK/kernel-config.pre-build" ] && cp -p "$WORK/kernel-config.pre-build" "$KERNEL_CFG"
		if [ -f "$WORK/.config.pre-build" ] && cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then
			log "restored: .config byte-identical"
		else
			echo "WARN: .config restore differs" >&2
		fi
		if [ -f "$WORK/kernel-config.pre-build" ] && cmp -s "$WORK/kernel-config.pre-build" "$KERNEL_CFG"; then
			log "restored: kernel .config byte-identical"
		else
			echo "WARN: kernel .config restore differs" >&2
		fi
		set -e
	fi
	exit $rc
}
trap cleanup EXIT

cp -p "$OWRT/.config" "$WORK/.config.pre-build"
cp -p "$KERNEL_CFG" "$WORK/kernel-config.pre-build"
KCFG_SHA_PRE=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)

# ---------------------------------------------------------------------------
# select the SAE-capable hostapd variant
# ---------------------------------------------------------------------------
log "config: select hostapd-mbedtls (SAE-capable full hostapd, mbedtls backend)"
cat "$CONFIG_DELTA" >> "$OWRT/.config"
( cd "$OWRT" && make defconfig ) 2>&1 | tee "$WORK/defconfig.log"
cp "$OWRT/.config" "$WORK/.config.resolved"

for sym in CONFIG_PACKAGE_hostapd-mbedtls CONFIG_PACKAGE_libmbedtls \
           CONFIG_PACKAGE_hostapd-common CONFIG_WPA_MBO_SUPPORT; do
	grep -q "^$sym=y$" "$OWRT/.config" || die "config symbol did not resolve: $sym"
done
grep -q '^# CONFIG_PACKAGE_hostapd-mini is not set$' "$OWRT/.config" || \
	die "hostapd-mini is still selected - the image delta must replace it, not add a second variant"
# Carry the kernel symbol the release-line config needs; the package build does
# not touch the kernel, but a later world build must not regenerate the kernel
# config from the defconfig-normalized .config (see wifi-image-integration
# kernel-symbols.config).  Restored at exit like everything else.
cat "$LANE/../wifi-image-integration/kernel-symbols.config" >> "$OWRT/.config"

# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
log "build: mbedtls dependency (staged by the release image, rebuilt if needed)"
( cd "$OWRT" && make package/libs/mbedtls/compile V=s -j"$JOBS" ) 2>&1 | tee "$WORK/mbedtls-build.log"
log "build: hostapd variant full-mbedtls (purged for a fresh compile + evidence)"
rm -rf "$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/hostapd-full-mbedtls"
( cd "$OWRT" && make package/network/services/hostapd/compile V=s -j"$JOBS" ) 2>&1 | tee "$WORK/hostapd-build.log"

BD=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/hostapd-full-mbedtls
SRC=$(ls -d "$BD"/hostapd-2026.08.07* 2>/dev/null | head -1 || true)
[ -n "$SRC" ] || die "hostapd-mbedtls build dir missing under $BD"
BIN=$SRC/hostapd/hostapd
[ -f "$BIN" ] || die "hostapd binary not produced: $BIN"

# ---------------------------------------------------------------------------
# prove it is an SAE build (target ARM artifact)
# ---------------------------------------------------------------------------
BIN_SIZE=$(stat -c %s "$BIN")
BIN_SHA=$(sha256sum "$BIN" | cut -d' ' -f1)
readelf -h "$BIN" | grep -q 'Machine:.*ARM' || die "built binary is not ARM: $BIN"
readelf -h "$BIN" | grep -q 'Version5 EABI' || die "built binary is not EABI5: $BIN"
SAE_CFLAGS=$(grep -c -- '-DCONFIG_SAE' "$WORK/hostapd-build.log" || true)
[ "$SAE_CFLAGS" -ge 1 ] || die "build log has no -DCONFIG_SAE compile flag"
grep -q -- 'sae\.o' "$WORK/hostapd-build.log" || die "build log has no src/common/sae.o compile"
grep -q 'CONFIG_TLS=mbedtls' "$WORK/hostapd-build.log" || die "build log does not show the mbedtls TLS backend"
SAE_STRINGS=$(strings "$BIN" | grep -c '^SAE: ' || true)
[ "${SAE_STRINGS:-0}" -ge 5 ] || die "built binary carries too few SAE code strings ($SAE_STRINGS)"
readelf -d "$BIN" | grep -q 'libmbedcrypto' || die "binary does not link libmbedcrypto"
readelf -d "$BIN" | grep -q 'libmbedx509' || die "binary does not link libmbedx509"
readelf -d "$BIN" | grep -q 'libmbedtls' || die "binary does not link libmbedtls"

# installed/built package payload (the .pkgdir is what the image install uses)
PKGDIR=$SRC/.pkgdir/hostapd-mbedtls
[ -d "$PKGDIR/usr/sbin" ] || die "hostapd-mbedtls .pkgdir missing /usr/sbin: $PKGDIR"
INST_BIN=$PKGDIR/usr/sbin/hostapd
[ -f "$INST_BIN" ] || die "installed hostapd payload missing: $INST_BIN"
INST_SIZE=$(stat -c %s "$INST_BIN")
INST_SHA=$(sha256sum "$INST_BIN" | cut -d' ' -f1)

APK=$(ls -t "$OWRT"/bin/packages/arm_cortex-a9/base/hostapd-mbedtls-*.apk 2>/dev/null | head -1 || true)
[ -n "$APK" ] || die "hostapd-mbedtls .apk not produced"
APK_SIZE=$(stat -c %s "$APK")
APK_SHA=$(sha256sum "$APK" | cut -d' ' -f1)
COMMON_APK=$(ls -t "$OWRT"/bin/packages/arm_cortex-a9/base/hostapd-common-*.apk 2>/dev/null | head -1 || true)
LIBMBED_APK=$(ls -t "$OWRT"/bin/packages/arm_cortex-a9/base/libmbedtls21-*.apk 2>/dev/null | head -1 || true)
HOSTAPD_VERSION=$(basename "$APK" .apk | sed -E 's/.*hostapd-mbedtls-//')

# ---------------------------------------------------------------------------
# keep local copies (never committed) + record evidence
# ---------------------------------------------------------------------------
mkdir -p "$ART"
cp -f "$BIN" "$ART/hostapd-mbedtls-$HOSTAPD_VERSION.arm"
cp -f "$APK" "$ART/"
[ -n "$LIBMBED_APK" ] && cp -f "$LIBMBED_APK" "$ART/"
[ -n "$COMMON_APK" ] && cp -f "$COMMON_APK" "$ART/"
{
	sha256sum "$ART/hostapd-mbedtls-$HOSTAPD_VERSION.arm"
	sha256sum "$ART/hostapd-mbedtls-$HOSTAPD_VERSION.apk"
	[ -n "$LIBMBED_APK" ] && sha256sum "$ART"/libmbedtls21-*.apk
	[ -n "$COMMON_APK" ] && sha256sum "$ART"/hostapd-common-*.apk
} > "$ART/SHA256SUMS" 2>/dev/null || true

{
	echo "wifi-hostapd-sae package build record (host-only; no device)"
	echo "generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "openwrt tree      : $OWRT"
	echo "tree HEAD         : $TREE_HEAD"
	echo "package           : hostapd-mbedtls $HOSTAPD_VERSION (OpenWrt 'full' hostapd variant, mbedtls TLS)"
	echo "openwrt defconfig base .config sha256 (pre-build) : $BASE_CFG_SHA"
	echo "built kernel .config sha256 pre/post             : $KCFG_SHA_PRE / KCFGPOSTPLACEHOLDER"
	echo
	echo "== SAE build proof (target ARM artifact) =="
	echo "compile line -DCONFIG_SAE occurrences in log : $SAE_CFLAGS"
	echo "SAE code strings in binary                   : $SAE_STRINGS"
	echo "DT_NEEDED:"
	readelf -d "$BIN" | grep -o 'Shared library: \[[^]]*\]' | sed 's/^/    /'
	echo
	echo "== artifacts (local, not committed) =="
	echo "binary (build tree)  : $BIN"
	echo "binary size/sha256   : $BIN_SIZE / $BIN_SHA"
	echo "installed payload    : $INST_BIN"
	echo "installed size/sha256: $INST_SIZE / $INST_SHA"
	echo "apk                  : $APK"
	echo "apk size/sha256      : $APK_SIZE / $APK_SHA"
	echo "hostapd-common apk   : ${COMMON_APK:-absent}"
	echo "libmbedtls apk       : ${LIBMBED_APK:-absent}"
	echo "local artifact dir   : $ART"
} > "$LANE/evidence/package-build.txt"
{
	echo "== hostapd package compile - selected lines =="
	grep -E -- '-(DCONFIG_SAE|DCONFIG_TLS)|sae\.o|mbedtls|LD  hostapd|CC  hostapd|Package hostapd|^make.*Error' "$WORK/hostapd-build.log" | tail -80 || true
} > "$LANE/evidence/package-build-log.txt" 2>/dev/null || true

# final kernel-config check before the recorded numbers are printed
KCFG_SHA_POST=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
sed -i "s/KCFGPOSTPLACEHOLDER/$KCFG_SHA_POST/" "$LANE/evidence/package-build.txt"

log "binary  : $BIN_SIZE B sha256 $BIN_SHA"
log "installed: $INST_SIZE B sha256 $INST_SHA"
log "apk     : $APK_SIZE B sha256 $APK_SHA"
log "evidence: $LANE/evidence/package-build.txt"
echo "RESULT: PASS - hostapd-mbedtls $HOSTAPD_VERSION built with CONFIG_SAE for arm_cortex-a9"
