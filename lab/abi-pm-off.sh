#!/bin/sh
# abi-pm-off.sh - make a prepared vanilla linux-5.10.201 tree present
# CONFIG_PM=n to external modules, so their struct layouts match a vendor
# kernel that was built without PM.
#
# The module build only reads the kernel *headers*, so the generated
# include/generated/autoconf.h is the only thing that has to agree. We first
# try the honest route (scripts/config + olddefconfig); multi_v7_defconfig
# selects PM via ARCH_ROCKCHIP and ARCH_TEGRA, so that normally does not
# stick, and we then comment the two symbols out of autoconf.h directly.
#
# Usage: lab/abi-pm-off.sh <kernel-tree> [cross-compile-prefix]
# Must run after the tree has been configured and modules_prepare'd, and
# before building the external module.

set -e

TREE="${1:?usage: abi-pm-off.sh <kernel-tree> [cross-prefix]}"
CROSS="${2:-arm-linux-gnueabihf-}"
cd "$TREE"

echo "== CONFIG_PM state before (autoconf.h) =="
grep -E '^#define CONFIG_PM( |_SLEEP )' include/generated/autoconf.h || echo "(no active CONFIG_PM defines)"

echo "== try scripts/config --disable PM + make olddefconfig =="
scripts/config --disable PM
scripts/config --disable PM_SLEEP
make ARCH=arm CROSS_COMPILE="$CROSS" olddefconfig >/dev/null

if grep -qE '^CONFIG_PM=y' .config; then
	echo "CONFIG_PM stayed =y in .config (ARCH_ROCKCHIP / ARCH_TEGRA 'select PM')"
	echo "== forcing it off in include/generated/autoconf.h instead =="
	sed -i 's|^#define CONFIG_PM 1$|/* #undef CONFIG_PM */|' include/generated/autoconf.h
	sed -i 's|^#define CONFIG_PM_SLEEP 1$|/* #undef CONFIG_PM_SLEEP */|' include/generated/autoconf.h
	# keep the header newer than .config so make does not regenerate it
	touch include/generated/autoconf.h
else
	echo "CONFIG_PM disabled by kconfig; nothing to post-process"
fi

echo "== CONFIG_PM state after (autoconf.h) =="
grep -nE 'CONFIG_PM' include/generated/autoconf.h | grep -E 'CONFIG_PM( |_SLEEP )' || true

if grep -qE '^#define CONFIG_PM( |_SLEEP 1)' include/generated/autoconf.h; then
	echo "ERROR: CONFIG_PM still active in autoconf.h" >&2
	exit 1
fi

echo "== ok: headers now present CONFIG_PM=n / CONFIG_PM_SLEEP=n to modules =="
