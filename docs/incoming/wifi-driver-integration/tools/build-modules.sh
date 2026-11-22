#!/usr/bin/env bash
# tools/build-modules.sh - reproducible build of the driver-port deliverable
#
# Builds THREE things against the project's built 6.18.60 luofu kernel tree:
#   1. transport/  : hi5622_pciet.ko = transport skeleton + the marked
#                    integration delta (transport-delta/0001-*.patch) +
#                    interface/pciet_iface.c
#   2. driver/     : hi5622_hdpp_drv.ko linked against the transport exports
#                    (KBUILD_EXTRA_SYMBOLS) - the real split integration
#   3. mock/       : hi5622_hdpp_mock.ko = driver sources + a simulated
#                    transport, self-contained, no device required
#
# NEVER runs an OpenWrt image build (no make world); module builds only.
# All build products stay OUTSIDE the repository (default workdir under /home).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DELIV="$REPO/docs/incoming/wifi-driver-integration"
WORK="${WIFI_DRIVER_PORT_WORK:-$HOME/wifi-driver-port-work}"
KDIR="${KDIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}"
STAGING_DIR="${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir}"
CROSS_COMPILE="${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}"

[ -d "$KDIR" ] || { echo "KDIR not found: $KDIR" >&2; exit 1; }
[ -x "${CROSS_COMPILE}gcc" ] || { echo "toolchain not found: ${CROSS_COMPILE}gcc" >&2; exit 1; }

export STAGING_DIR CROSS_COMPILE
MAKE=(make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE")

echo "== workdir: $WORK"
rm -rf "$WORK/transport" "$WORK/driver" "$WORK/mock" "$WORK/interface"
mkdir -p "$WORK/transport" "$WORK/driver" "$WORK/mock" "$WORK/interface" "$WORK/logs"

# Shared interface header (the transport patch includes it as a local header;
# the real transport source stays untouched in the repo).
cp "$DELIV/interface/hi5622_transport_iface.h" "$WORK/interface/"
cp "$DELIV/interface/hi5622_transport_iface.h" "$WORK/transport/"
cp "$DELIV/interface/pciet_iface.c" "$WORK/transport/"

echo "== 1/3 transport module (+ integration delta)"
cp "$DELIV/../wifi-transport-port/build"/{hi5622_pciet.h,pciet_main.c,pciet_ete.c,Kbuild} "$WORK/transport/"
( cd "$WORK/transport" && git apply --verbose "$DELIV/transport-delta/0001-pciet-integration-iface.patch" ) \
	|| { echo "patch failed - check the transport baseline sha256 in transport-delta/README.md" >&2; exit 1; }
"${MAKE[@]}" -C "$KDIR" M="$WORK/transport" modules 2>&1 | tee "$WORK/logs/transport-build.log"

echo "== 2/3 driver module (split, against transport exports)"
cp "$DELIV/driver"/*.c "$DELIV/driver"/*.h "$DELIV/driver/Kbuild" "$WORK/driver/"
cp "$DELIV/interface/hi5622_transport_iface.h" "$WORK/interface/"
"${MAKE[@]}" -C "$KDIR" M="$WORK/driver" \
	KBUILD_EXTRA_SYMBOLS="$WORK/transport/Module.symvers" \
	modules 2>&1 | tee "$WORK/logs/driver-build.log"

echo "== 3/3 mock module (self-contained: driver + simulated transport)"
cp "$DELIV/driver"/*.c "$DELIV/driver"/*.h "$DELIV/mock"/*.c "$DELIV/mock/Kbuild" "$WORK/mock/"
cp "$DELIV/interface/hi5622_transport_iface.h" "$WORK/mock/"
"${MAKE[@]}" -C "$KDIR" M="$WORK/mock" modules 2>&1 | tee "$WORK/logs/mock-build.log"

echo "== artifacts / verification"
for ko in "$WORK/transport/hi5622_pciet.ko" "$WORK/driver/hi5622_hdpp_drv.ko" "$WORK/mock/hi5622_hdpp_mock.ko"; do
	echo "  $(sha256sum "$ko" | cut -d' ' -f1)  $ko"
	"${CROSS_COMPILE}modinfo" -F vermagic "$ko" 2>/dev/null | sed 's/^/    vermagic: /' || true
done

echo "== transport exports (driver interface)"
"${CROSS_COMPILE}nm" -g --defined-only "$WORK/transport/hi5622_pciet.ko" 2>/dev/null \
	| awk '$3 ~ /^hi5622_transport_/ {print "  " $3}' || true

echo "== driver -> transport unresolved symbols (must be exactly 2)"
"${CROSS_COMPILE}nm" -u "$WORK/driver/hi5622_hdpp_drv.ko" 2>/dev/null \
	| awk '$2 ~ /^hi5622/ {print "  " $2}' || true

echo "== mock is self-contained (no hi5622_transport_* undefined)"
if "${CROSS_COMPILE}nm" -u "$WORK/mock/hi5622_hdpp_mock.ko" 2>/dev/null | grep -q hi5622_transport_; then
	echo "  FAIL: mock still depends on the transport module" >&2
	exit 1
fi
echo "  OK"

# Known tree artifact, not a source warning: CONFIG_MODULE_STRIPPED=y makes
# MODULE_INFO_STRIP drop the description tag at modpost time (the tag is in the
# source of every module).  Everything else is a real warning.
if grep -i 'warning:' "$WORK"/logs/*.log | grep -v 'missing MODULE_DESCRIPTION' >/tmp/hi5622-warn.$; then
	echo "== WARNING: build logs contain real warnings" >&2
	cat /tmp/hi5622-warn.$ >&2
	rm -f /tmp/hi5622-warn.$
	exit 1
fi
rm -f /tmp/hi5622-warn.$
echo "== only the known CONFIG_MODULE_STRIPPED MODULE_DESCRIPTION note is present"
echo "== OK"
