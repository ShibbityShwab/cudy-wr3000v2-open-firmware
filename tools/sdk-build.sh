#!/bin/bash
set -euo pipefail

SDK_DIR="${1:-}"

echo "WR3000 V2.0 from-source build (vendor SDK for luofu / hi5671y)"
echo
echo "Steps this script will perform once a source tree is available:"
echo "  1. feed the SDK from the vendor GPL drop (kernel, U-Boot, build config, and the Wi-Fi driver)"
echo "  2. build the kernel and modules; compare vermagic and module list against the device dumps"
echo "  3. build a rootfs image, pack it with tar2sqfs and ubinize, then verify the UBI by hash"
echo "  4. flash slot B from slot A and run the recovery drill described in docs/FLASH-PLAN.md"
echo

if [ -z "$SDK_DIR" ] || [ ! -d "$SDK_DIR" ]; then
	echo "No SDK source tree given (or the directory does not exist)."
	echo "Nothing is built. Path A in docs/PORT-PLAN.md starts the day the sources arrive."
	exit 2
fi

echo "SDK directory given: $SDK_DIR"
echo "This script is a stub on purpose: it refuses to pretend a build happened before the vendor"
echo "sources exist. Replace this body with the real SDK build steps when they do."
exit 3
