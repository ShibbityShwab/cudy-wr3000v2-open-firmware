#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Device-free self-test for the wifi-openwrt-pkg deliverable.
#
#   ./tools/selftest.sh
#
# Needs: POSIX sh, sha256sum, python3, a host C compiler (for the syntax
# check).  Does NOT need the OpenWrt tree and NEVER touches the device.
#
# It validates: source-snapshot integrity against src/SHA256SUMS, the package
# Makefile contract, init/config/status script syntax, package-tree hygiene
# (no vendor blob / object / firmware byte), the assembled driver state
# (direct-read D2H object present, CECE policy still refused before boot), the
# seed helper compile, and the two lane self-test models (HCC frames/rings and
# the D2H direct reader).

set -u

LANE=$(cd "$(dirname "$0")/.." && pwd)
PKG="$LANE/package/hi5622-pciet-hcc"
pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS %s\n' "$*"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$*"; }

echo "== wifi-openwrt-pkg self-test: $LANE"

echo
echo "== 1. source snapshot integrity (src/SHA256SUMS)"
if [ -f "$PKG/src/SHA256SUMS" ] &&
   ( cd "$PKG/src" && sha256sum -c --status SHA256SUMS ); then
	ok "src/SHA256SUMS: all driver-snapshot files match"
else
	bad "src/SHA256SUMS: snapshot mismatch or manifest missing"
fi

echo
echo "== 2. package Makefile contract"
M="$PKG/Makefile"
for needle in \
	'include $(INCLUDE_DIR)/kernel.mk' \
	'define KernelPackage/hi5622-pciet-hcc' \
	'FILES:=$(PKG_BUILD_DIR)/hi5622_pciet_hcc.ko' \
	'include $(INCLUDE_DIR)/package.mk' \
	'$(eval $(call KernelPackage,hi5622-pciet-hcc))'
do
	if grep -Fq "$needle" "$M"; then
		ok "Makefile contains: $needle"
	else
		bad "Makefile missing: $needle"
	fi
done
if grep -q '^PKG_SOURCE' "$M"; then
	bad "Makefile declares PKG_SOURCE (local snapshot must not download)"
else
	ok "Makefile is download-free (local src/ snapshot)"
fi
if grep -Eq '^[[:space:]]*AUTOLOAD[[:space:]]*:?=' "$M"; then
	bad "Makefile sets AUTOLOAD (module must not auto-bind)"
else
	ok "Makefile has no AUTOLOAD (load stays explicit/attended)"
fi
grep -Fq 'hi5622_pciet_hcc' "$PKG/src/Kbuild" && ok "Kbuild names hi5622_pciet_hcc" ||
	bad "Kbuild does not name hi5622_pciet_hcc"
if grep -Fq 'pciet_d2h_direct.o' "$PKG/src/Kbuild"; then
	ok "Kbuild includes the direct-read D2H object (assembled lane state)"
else
	bad "Kbuild is missing pciet_d2h_direct.o (wrong source snapshot)"
fi

echo
echo "== 2b. lane tooling scripts: syntax"
if sh -n "$LANE/tools/install-into-tree.sh"; then
	ok "tools/install-into-tree.sh: sh -n clean"
else
	bad "tools/install-into-tree.sh: shell syntax error"
fi
if bash -n "$LANE/tools/build-package.sh"; then
	ok "tools/build-package.sh: bash -n clean"
else
	bad "tools/build-package.sh: shell syntax error"
fi

echo
echo "== 3. instance scripts: shell syntax + policy defaults"
if sh -n "$PKG/files/hi5622-pciet.init"; then
	ok "files/hi5622-pciet.init: sh -n clean"
else
	bad "files/hi5622-pciet.init: shell syntax error"
fi
if sh -n "$PKG/files/hi5622-status"; then
	ok "files/hi5622-status: sh -n clean"
else
	bad "files/hi5622-status: shell syntax error"
fi
grep -q "option 'enabled' '0'" "$PKG/files/hi5622-pciet.config" &&
	ok "config default enabled=0" || bad "config does not default enabled=0"
grep -q "refusing to unload" "$PKG/files/hi5622-pciet.init" &&
	ok "init refuses to unload the module (never rmmod after release)" ||
	bad "init does not refuse rmmod"
if grep -Eq '0x5a5a|0x5A5A|0xcece|0xCECE|writel|devmem' "$PKG/files/hi5622-pciet.init"; then
	bad "init script mentions release/CECE/device writes (must be write-free)"
else
	ok "init script is write-free (no release/CECE/devmem)"
fi

echo
echo "== 4. package-tree hygiene (no vendor blob / object / firmware byte)"
hits=$(find "$PKG" -type f ! -path '*/src/SHA256SUMS' \
	\( -name '*.bin' -o -name '*.ko' -o -name '*.o' -o -name '*.fw' \
	   -o -name '*.img' -o -name '*.itb' -o -name '*.dtb' -o -name '*.dtbo' \))
if [ -n "$hits" ]; then
	bad "binary/blob candidates in package tree:"; printf '%s\n' "$hits"
else
	ok "no firmware blob / object / image file in the package tree"
fi
if find "$PKG" -type f -size +200k | grep -q .; then
	bad "unexpected >200 KiB file in the package tree"
else
	ok "all package-tree files are <200 KiB (text only)"
fi

echo
echo "== 5. seed helper (hi5622-seed.c) compile syntax"
CC=""
for c in cc gcc clang; do
	if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
done
if [ -n "$CC" ]; then
	if "$CC" -std=gnu99 -Wall -Wextra -Werror -fsyntax-only "$PKG/tools/hi5622-seed.c"; then
		ok "$CC -std=gnu99 -Wall -Wextra -Werror -fsyntax-only hi5622-seed.c"
	else
		bad "hi5622-seed.c does not compile cleanly"
	fi
else
	bad "no host C compiler for the seed-helper syntax check"
fi

echo
echo "== 6. lane models: HCC frame/ring + D2H direct reader"
if command -v python3 >/dev/null 2>&1; then
	if python3 "$PKG/tools/frame_selftest.py" >/dev/null 2>&1; then
		ok "frame_selftest.py (HCC frame/ring model)"
	else
		bad "frame_selftest.py failed"
	fi
	if out=$(python3 "$PKG/tools/d2h_selftest.py" 2>&1); then
		printf '%s\n' "$out" | tail -1
		ok "d2h_selftest.py (direct-read D2H model)"
	else
		printf '%s\n' "$out" | tail -5
		bad "d2h_selftest.py failed"
	fi
else
	bad "python3 missing; lane models not run"
fi

echo
if [ "$fail" -eq 0 ]; then
	echo "wifi-openwrt-pkg selftest: $pass checks PASS"
	exit 0
fi
echo "wifi-openwrt-pkg selftest: $pass PASS, $fail FAIL"
exit 1
