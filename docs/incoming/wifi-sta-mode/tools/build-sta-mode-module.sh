#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-sta-mode-module.sh - STA (client) host-SME connect module.
# HOST-ONLY: builds and verifies the module, never touches the bench router.
#
# Chain (each layer is a committed deliverable; the .ko is local evidence and
# is never committed):
#   1. FINAL ASSOCIATION tree: opensource 61f7880 chain
#      (all-patch + wpa3-sae + ap-mgmt + sta-ops + data-tx)
#      -> ../wifi-stability/tools/build-association-module.sh
#   2. ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
#      (corrected device-facing mgmt-TX carrier: HCC g0 id 6, frame inline)
#   3. ../wifi-sta-join/patches/0001-sta-join-hardening.patch
#      (108-B WAL 0x123 join body hardening + one-shot gates + g2 decoders)
#   4. patches/0001-sta-mode-connect-crx.patch (this lane: cfg80211
#      connect_done on the CRX path - auth/assoc requests on the corrected
#      carrier, auth/assoc response consumption, bounded timeout)
#
# Additions over the association tree: 2 portable sources (hi5622_sta_sme.*),
# 2 kernel-glue sources (hi5622_sta_mode.*), 2 cfg80211 provider symbols
# (cfg80211_connect_done / cfg80211_disconnected), 2 module parameters
# (sta_connect, sta_connect_timeout_ms) => 58 params total.
#
#   ./tools/build-sta-mode-module.sh
#   ASSOC_TREE=/tmp/wifi-assoc-build/integ ./tools/build-sta-mode-module.sh
set -eu
LANE_DIR=$(cd $(dirname $0)/.. && pwd)
DOCS=$(cd ${LANE_DIR}/.. && pwd)
LANE_PATCH=${LANE_DIR}/patches/0001-sta-mode-connect-crx.patch
WORK=/tmp/wifi-sta-mode-build
ART=/tmp/wifi-sta-mode-artifact
ARTWORK=${ART}.work
ASSOC_TREE=${ASSOC_TREE:-}

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE:-}" ] && KDIR="${KDIR_OVERRIDE}"
[ -n "${STAGING_OVERRIDE:-}" ] && STAGING_DIR="${STAGING_OVERRIDE}"
set -u
if [ -z "${KDIR}" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=${root}/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "${c}" ]; then KDIR=${c}; STAGING_DIR=${root}/staging_dir; break; fi
	done
fi
[ -d "${KDIR}" ] || { echo "sta-mode: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "${STAGING_DIR}" ] || { echo "sta-mode: staging dir not found" >&2; exit 2; }
CROSS=${STAGING_DIR}/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "sta-mode: cross gcc missing" >&2; exit 2; }
command -v gcc >/dev/null || { echo "sta-mode: host gcc required (selftest)" >&2; exit 2; }
[ -f "${LANE_PATCH}" ] || { echo "sta-mode: lane patch missing" >&2; exit 2; }

echo "== 1. host selftests (portable logic)"
gcc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
    -o "${WORK}.sta_mode_selftest" \
    "${LANE_DIR}/tools/sta_mode_selftest.c" "${LANE_DIR}/src/hi5622_sta_sme.c"
"${WORK}.sta_mode_selftest"

echo "== 2. stage the FINAL ASSOCIATION tree"
if [ -z "${ASSOC_TREE}" ]; then
	if [ -d /tmp/wifi-assoc-build/integ ]; then
		ASSOC_TREE=/tmp/wifi-assoc-build/integ
		echo "reusing ${ASSOC_TREE}"
	else
		echo "building the association tree (../wifi-stability/tools/build-association-module.sh)"
		"${DOCS}/wifi-stability/tools/build-association-module.sh"
		ASSOC_TREE=/tmp/wifi-assoc-build/integ
	fi
fi
[ -d "${ASSOC_TREE}" ] || { echo "sta-mode: association tree missing: ${ASSOC_TREE}" >&2; exit 2; }
rm -rf "${ART}" "${ARTWORK}" "${WORK}/tree"
mkdir -p "${ARTWORK}" "${WORK}"
cp -a "${ASSOC_TREE}" "${WORK}/tree"
( cd "${WORK}/tree" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order \
    .modules.order.cmd 2>/dev/null ) || true

echo "== 3. apply the two prerequisite patches"
( cd "${WORK}/tree" && patch -p1 --no-backup-if-mismatch \
    < "${DOCS}/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch" >/dev/null )
( cd "${WORK}/tree" && patch -p1 --no-backup-if-mismatch \
    < "${DOCS}/wifi-sta-join/patches/0001-sta-join-hardening.patch" >/dev/null )
echo "== 4. apply this lane's patch"
( cd "${WORK}/tree" && patch -p1 --no-backup-if-mismatch < "${LANE_PATCH}" >/dev/null )
if [ -n "$(find "${WORK}/tree" -name '*.rej' -o -name '*.orig' 2>/dev/null)" ]; then
	echo "sta-mode: rejects/orig files left behind - refusing" >&2; exit 1
fi

echo "== 5. build against ${KDIR}"
export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "${KDIR}" M="${WORK}/tree" \
     modules > "${WORK}/build.log" 2>&1 || {
	echo "sta-mode: BUILD FAILED" >&2; tail -40 "${WORK}/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "${WORK}/build.log"; then
	echo "sta-mode: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=${WORK}/tree/hi5622_wifi.ko
[ -f "${KO}" ] || { echo "sta-mode: no .ko" >&2; exit 1; }

echo "== 6. artifact assertions"
"${CROSS}strip" --strip-debug -o "${ARTWORK}/hi5622_wifi.ko" "${KO}"
"${CROSS}objcopy" -O binary --only-section=.text "${KO}" "${WORK}/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "${ARTWORK}/hi5622_wifi.ko" "${WORK}/.t2"
cmp -s "${WORK}/.t1" "${WORK}/.t2" || { echo "sta-mode: strip changed .text" >&2; exit 1; }
cp "${KO}" "${ARTWORK}/hi5622_wifi.ko.unstripped"
cp "${WORK}/build.log" "${ARTWORK}/build.log"

NP=$(modinfo -p "${ARTWORK}/hi5622_wifi.ko" 2>/dev/null | wc -l)
[ "${NP}" = "58" ] || { echo "sta-mode: param count ${NP} != 58" >&2; exit 1; }
for p in sta_connect sta_connect_timeout_ms sta_join allow_frames; do
	modinfo -p "${ARTWORK}/hi5622_wifi.ko" 2>/dev/null | grep -q "^${p}:" || {
		echo "sta-mode: missing param ${p}" >&2; exit 1; }
done
SV=${WORK}/tree/cfg80211-provider.symvers
[ "$(wc -l < "${SV}")" = "14" ] || { echo "sta-mode: cfg80211 symvers != 14 lines" >&2; exit 1; }
for sym in cfg80211_connect_done cfg80211_disconnected; do
	grep -q "${sym}" "${SV}" || { echo "sta-mode: ${sym} missing from symvers" >&2; exit 1; }
	"${CROSS}nm" -u "${ARTWORK}/hi5622_wifi.ko" | grep -q " ${sym}$" || {
		echo "sta-mode: ${sym} not imported" >&2; exit 1; }
done
for s in "sta mode: host-SME connect started" "assoc response OK" \
	 "CRX path" "sta_connect_timeout_ms"; do
	"${CROSS}strings" "${ARTWORK}/hi5622_wifi.ko" | grep -qF "${s}" || {
		echo "sta-mode: feature string missing: ${s}" >&2; exit 1; }
done
VERMAGIC=$("${CROSS}strings" "${KO}" | grep -m1 '^vermagic=')
echo "${VERMAGIC}" | grep -q '^vermagic=6.18.60 SMP mod_unload ARMv7 p2v8' || {
	echo "sta-mode: unexpected vermagic" >&2; exit 1; }

rm -rf "${ART}"
mv "${ARTWORK}" "${ART}"
SIZE=$(stat -c %s "${ART}/hi5622_wifi.ko")
SHA=$(sha256sum "${ART}/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "${KO}")
USHA=$(sha256sum "${KO}" | cut -d' ' -f1)
echo "== STA-MODE MODULE (host-built, NOT fired)"
echo "artifact  : ${ART}/hi5622_wifi.ko (debug-stripped)"
echo "size      : ${SIZE}"
echo "sha256    : ${SHA}"
echo "unstripped: ${USIZE} B sha256 ${USHA}"
echo "params    : ${NP} (58; adds sta_connect + sta_connect_timeout_ms)"
echo "${VERMAGIC}"
echo "chain     : association 61f7880 + sibling carrier + sta-join hardening + sta-mode"
echo "cfg80211  : $(wc -l < "${SV}") provider symbols (adds cfg80211_connect_done, cfg80211_disconnected)"
