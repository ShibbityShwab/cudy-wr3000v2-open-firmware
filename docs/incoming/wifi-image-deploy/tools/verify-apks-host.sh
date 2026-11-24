#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-apks-host.sh - host-only end-to-end simulation of the T0 apk
# transaction against the REAL bench-image rootfs (task-119).
#
# It reconstructs the pre-T0 package state from the built image (container
# df6a78c3 -> UBI -> squashfs), deletes the seven T0 packages to obtain the
# baseline, then performs the exact on-device apk add/del transaction with the
# OpenWrt tree's host apk tool and proves:
#   * the add restores a payload tree byte-identical to the built image;
#   * the rollback restores a payload tree byte-identical to the baseline;
#   * re-running add is idempotent;
#   * a mismatched installed kernel package fails closed (apk dependency gate).
# Nothing here contacts the router; no root privilege is needed and no package
# script is executed (--scripts=no; the device runs the stock OpenWrt hooks
# natively, see REPORT.md F4).
#
# usage: verify-apks-host.sh [--artifacts DIR] [--owrt DIR] [--work DIR]
#                            [--keep] [--evidence FILE] [--userspace-only]
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
DEFAULT_ART=/home/shibbityshwab/wifi-image-integration-artifacts
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
UBI_EXTRACTOR=$LANE/../wifi-image-integration/tools/extract-ubi-rootfs.py
CONTAINER=bench-image-sysupgrade.bin
CONTAINER_SHA=df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310
KERNEL_SLICE_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_PKGS_SHA=674f209f01e8ef5f75d19bcc85ec2cb1ea722602ca53533022afde5cd9c7563e
EXPECTED_FILES_SHA=9cdfb9072c28546fbb8f0abf0d0626ad095e8057e8c5b94c8dc6a680d6763332
KERNEL_DEP=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1

ART=$DEFAULT_ART
OWRT=$DEFAULT_OWRT
WORK=
KEEP=0
EVIDENCE=
USERS=0
while [ $# -gt 0 ]; do
	case "$1" in
		--artifacts) shift; ART=$1 ;;
		--artifacts=*) ART=$(printf '%s' "$1" | cut -c13-) ;;
		--owrt) shift; OWRT=$1 ;;
		--owrt=*) OWRT=$(printf '%s' "$1" | cut -c8-) ;;
		--work) shift; WORK=$1 ;;
		--work=*) WORK=$(printf '%s' "$1" | cut -c8-) ;;
		--keep) KEEP=1 ;;
		--userspace-only) USERS=1 ;;
		--evidence) shift; EVIDENCE=$1 ;;
		--evidence=*) EVIDENCE=$(printf '%s' "$1" | cut -c12-) ;;
		-h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done

PASSES=0
FAILS=0
ok()  { PASSES=$((PASSES + 1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
eq()  { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2' want '$3')"; fi; }
die() { bad "$*"; finish; }

finish() {
	printf '\n== verify-apks-host: %d passed, %d failed ==\n' "$PASSES" "$FAILS"
	if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
	printf 'RESULT: PASS - host-simulated T0 install/rollback is byte-identical; kernel-mismatch gate fails closed\n'
	exit 0
}

if [ -z "$WORK" ]; then WORK=$(mktemp -d /tmp/t0-verify.XXXXXX); fi
mkdir -p "$WORK" || die "cannot create work dir $WORK"
if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")"
	exec > >(tee "$EVIDENCE") 2>&1
fi
# shellcheck disable=SC2329 # invoked through the EXIT trap below
cleanup() {
	if [ "$KEEP" -eq 0 ]; then rm -rf "$WORK"; else printf 'work dir kept: %s\n' "$WORK"; fi
}
trap cleanup EXIT

printf '== T0 host apk-transaction simulation ==\n'
printf 'artifacts: %s\n' "$ART"
printf 'tree     : %s\n' "$OWRT"
printf 'work     : %s\n\n' "$WORK"

APK=$OWRT/staging_dir/host/bin/apk
if [ ! -x "$APK" ]; then die "host apk tool not found: $APK"; fi
if [ ! -x "$UBI_EXTRACTOR" ]; then die "sibling UBI extractor not found: $UBI_EXTRACTOR"; fi
if ! command -v unsquashfs >/dev/null 2>&1; then die "unsquashfs not found"; fi
if ! command -v python3 >/dev/null 2>&1; then die "python3 not found"; fi

APK_LIST=$(cat <<'TABLE'
iw 6.17-r1 52997 9b46713fb5a0e866e7b1fb919f2a30eca4bc01d9aeabf49fa50b20faedd69f08 116868
iwinfo 2026.05.26~66bdd1a0-r1 8322 5badc21509df6ba6df109697879cf28960315c8ceb19aabb258f79564ae5de23 16922
libiwinfo20230701 2026.05.26~66bdd1a0-r1 25768 d1178900f817c08c232bf4a704e5b883f02c1f67dbe93e41af9f4b8132ce3f55 57408
libiwinfo-data 2026.05.26~66bdd1a0-r1 3925 3bb216a4f2ae69a27cfe86daf97c597b92daa4f0fca68b00c0d842076d933320 18264
wireless-regdb 2026.09.03-r1 3440 afd292e0c0947aaa5497e52b2a9e9484e690a9abfef1153c36d9a06143a758a1 6360
kmod-hi5622-cfg80211 6.18.60-r1 183520 716fd384cc7ad58b7c3e4000046addce7c76b3223fe3b8d5b05600fb46ed624c 406061
kmod-hi5622-wifi 6.18.60-r1 59302 aac8f3fa0f230ac19f94cb010c6f15e443a52e35b095a7e54908b2185eaf9aef 133920
TABLE
)

# ------------------------------------------------------------- pre-checks ---
eq "expected-packages.txt sha256" "$(sha256sum "$LANE/tools/expected-packages.txt" | awk '{print $1}')" "$EXPECTED_PKGS_SHA"
eq "expected-files.txt sha256" "$(sha256sum "$LANE/tools/expected-files.txt" | awk '{print $1}')" "$EXPECTED_FILES_SHA"
while read -r name version _ asha _; do
	[ -n "$name" ] || continue
	f=$ART/$name-$version.apk
	if [ ! -f "$f" ]; then bad "missing $f"; continue; fi
	eq "$name apk sha256" "$(sha256sum "$f" | awk '{print $1}')" "$asha"
done <<TABLE
$APK_LIST
TABLE
C=$ART/$CONTAINER
if [ ! -f "$C" ]; then die "missing container $C"; fi
eq "container sha256" "$(sha256sum "$C" | awk '{print $1}')" "$CONTAINER_SHA"

# ---------------------------------------- rootfs reconstruction from bytes ---
printf '\n-- reconstructing the built-image rootfs from its bytes --\n'
tar -xOf "$C" sysupgrade-cudy_wr3000-v2/kernel >"$WORK/kernel.member" 2>/dev/null || die "cannot extract kernel member"
eq "container kernel member sha256 (validated slice)" "$(sha256sum "$WORK/kernel.member" | awk '{print $1}')" "$KERNEL_SLICE_SHA"
tar -xOf "$C" sysupgrade-cudy_wr3000-v2/root >"$WORK/root.ubi" 2>/dev/null || die "cannot extract root member"
python3 "$UBI_EXTRACTOR" "$WORK/root.ubi" -o "$WORK/root.squashfs" >/dev/null 2>&1 || die "UBI extraction failed"
rm -rf "$WORK/ref"
unsquashfs -q -d "$WORK/ref" "$WORK/root.squashfs" >/dev/null 2>&1
if [ ! -d "$WORK/ref/lib/apk/db" ]; then die "reconstructed rootfs lacks /lib/apk/db"; fi
eq "built rootfs installed package count (81 = 74 + 7)" "$(grep -c '^P:' "$WORK/ref/lib/apk/db/installed")" "81"
while read -r path sha _; do
	case "$path" in ''|\#*) continue ;; esac
	f=$WORK/ref/$path
	if [ ! -f "$f" ]; then bad "built image missing /$path"; continue; fi
	eq "/$path sha256 in built image" "$(sha256sum "$f" | awk '{print $1}')" "$sha"
done <"$LANE/tools/expected-files.txt"

# ---------------------------------------------------------------- helpers ---
APK_RUN_FLAGS="--allow-untrusted --no-network --scripts=no --logfile=no --repositories-file /dev/null"
apk_at() { # root, commit args... (--scripts is a commit option: not valid on query)
	root=$1; shift
	# shellcheck disable=SC2086 # flags are a deliberate list
	"$APK" --root "$root" $APK_RUN_FLAGS "$@"
}
apk_q() { # root, query args...
	root=$1; shift
	"$APK" --root "$root" --no-network --repositories-file /dev/null query "$@"
}
payload_manifest() { # dir -> sorted "sha  ./path" list, excluding apk/world bookkeeping
	(
		cd "$1" || exit 1
		find . -path ./lib/apk -prune -o -path ./etc/apk -prune -o -type f -print0 \
			| sort -z | xargs -0 sha256sum
	)
}
add_files() { # root
	root=$1
	set --
	while read -r name version _ asha _; do
		[ -n "$name" ] || continue
		set -- "$@" "$ART/$name-$version.apk"
	done <<TABLE
$APK_LIST
TABLE
	apk_at "$root" --force-non-repository add "$@" >"$WORK/apk-add.log" 2>&1
}
del_names() { # root
	root=$1
	apk_at "$root" del iw iwinfo libiwinfo20230701 libiwinfo-data wireless-regdb \
		kmod-hi5622-cfg80211 kmod-hi5622-wifi >"$WORK/apk-del.log" 2>&1
}
add_files_userspace() { # root - the five packages that have no kernel= dependency
	root=$1
	set --
	while read -r name version _ asha _; do
		[ -n "$name" ] || continue
		case "$name" in kmod-*) continue ;; esac
		set -- "$@" "$ART/$name-$version.apk"
	done <<TABLE
$APK_LIST
TABLE
	apk_at "$root" --force-non-repository add "$@" >"$WORK/apk-add-users.log" 2>&1
}
del_userspace() { # root
	root=$1
	apk_at "$root" del iw iwinfo libiwinfo20230701 libiwinfo-data wireless-regdb \
		>"$WORK/apk-del-users.log" 2>&1
}

# ---------------------------------------------------------------- baseline ---
printf '\n-- step 1: delete the 7 packages from the built rootfs (reconstruct the pre-T0 state) --\n'
cp -a "$WORK/ref" "$WORK/pre" || die "cannot copy rootfs to pre"
del_names "$WORK/pre"
eq "baseline installed package count (74)" "$(grep -c '^P:' "$WORK/pre/lib/apk/db/installed")" "74"
payload_manifest "$WORK/pre" >"$WORK/pre.payload"
payload_manifest "$WORK/ref" >"$WORK/ref.payload"
eq "baseline removes exactly the 7 package payloads" "$(diff "$WORK/pre.payload" "$WORK/ref.payload" | grep -c '^>')" "7"
while read -r name _; do
	[ -n "$name" ] || continue
	if [ -n "$(apk_q "$WORK/pre" --fields version "$name" 2>/dev/null | sed -n 's/^Version: *//p')" ]; then
		bad "$name unexpectedly still installed in the baseline"
	else
		ok "$name absent in the baseline"
	fi
done <<TABLE
$APK_LIST
TABLE

# ------------------------------------------------------- userspace-only path ---
if [ "$USERS" -eq 1 ]; then
	printf '\n-- userspace-only: install/verify/rollback the five non-kmod packages --\n'
	grep -vE '/(cfg80211|hi5622_wifi)[.]ko$' "$WORK/ref.payload" >"$WORK/ref-nomod.payload"
	grep -v '^lib/modules/' "$LANE/tools/expected-files.txt" >"$WORK/files-userspace.txt"
	cp -a "$WORK/pre" "$WORK/postu" || die "cannot copy rootfs to postu"
	add_files_userspace "$WORK/postu"
	rcu=$?
	eq "userspace apk add rc" "$rcu" "0"
	eq "userspace install package count (79)" "$(grep -c '^P:' "$WORK/postu/lib/apk/db/installed")" "79"
	payload_manifest "$WORK/postu" >"$WORK/postu.payload"
	eq "userspace install payload == built image minus the two module payloads" \
		"$(diff "$WORK/postu.payload" "$WORK/ref-nomod.payload" | wc -l)" "0"
	if [ -e "$WORK/postu/lib/modules/6.18.60/cfg80211.ko" ] || [ -e "$WORK/postu/lib/modules/6.18.60/hi5622_wifi.ko" ]; then
		bad "userspace-only install put modules in the rootfs"
	else
		ok "userspace-only install adds no kernel modules"
	fi
	while read -r name version _; do
		case "$name" in ''|\#*) continue ;; esac
		case "$name" in kmod-*) continue ;; esac
		got=$(apk_q "$WORK/postu" --fields version "$name" 2>/dev/null | sed -n 's/^Version: *//p')
		eq "$name installed version (userspace)" "$got" "$version"
	done <<TABLE
$APK_LIST
TABLE
	while read -r path sha _; do
		case "$path" in ''|\#*) continue ;; esac
		f=$WORK/postu/$path
		if [ -f "$f" ]; then
			eq "/$path installed sha256 (userspace)" "$(sha256sum "$f" | awk '{print $1}')" "$sha"
		else
			bad "/$path missing after userspace install"
		fi
	done <"$WORK/files-userspace.txt"
	badk=0
	while read -r name version _ asha _; do
		case "$name" in ''|\#*) continue ;; esac
		case "$name" in kmod-*) continue ;; esac
		if "$APK" adbdump "$ART/$name-$version.apk" 2>/dev/null \
			| sed -n '/^  depends:/,/^[^ ]/p' | grep -q 'kernel='; then
			bad "userspace package $name carries a kernel= dependency"
			badk=1
		fi
	done <<TABLE
$APK_LIST
TABLE
	if [ "$badk" -eq 0 ]; then ok "userspace set carries no kernel= dependency"; fi
	payload_manifest "$WORK/postu" >"$WORK/postu.before-idem"
	add_files_userspace "$WORK/postu"
	rciu=$?
	eq "second userspace apk add rc (no-op)" "$rciu" "0"
	payload_manifest "$WORK/postu" >"$WORK/postu.after-idem"
	eq "second userspace apk add left the payload unchanged" \
		"$(diff "$WORK/postu.before-idem" "$WORK/postu.after-idem" | wc -l)" "0"
	del_userspace "$WORK/postu"
	rcru=$?
	eq "userspace apk del rc" "$rcru" "0"
	eq "userspace rollback package count (74)" "$(grep -c '^P:' "$WORK/postu/lib/apk/db/installed")" "74"
	payload_manifest "$WORK/postu" >"$WORK/userspace-rollback.payload"
	eq "userspace rollback payload byte-identical to the baseline" \
		"$(diff "$WORK/userspace-rollback.payload" "$WORK/pre.payload" | wc -l)" "0"
	while read -r path _; do
		case "$path" in ''|\#*) continue ;; esac
		if [ -e "$WORK/postu/$path" ]; then bad "/$path survived the userspace rollback"; else ok "/$path removed by userspace rollback"; fi
	done <"$WORK/files-userspace.txt"
	finish
fi

# ----------------------------------------------------------- install path ---
printf '\n-- step 2: apk add the exact seven artifacts (device flags, scripts skipped on host) --\n'
cp -a "$WORK/pre" "$WORK/post" || die "cannot copy rootfs to post"
add_files "$WORK/post"
rca=$?
eq "apk add rc" "$rca" "0"
eq "post-install package count (81)" "$(grep -c '^P:' "$WORK/post/lib/apk/db/installed")" "81"
payload_manifest "$WORK/post" >"$WORK/post.payload"
eq "install payload tree byte-identical to the built image" "$(diff "$WORK/post.payload" "$WORK/ref.payload" | wc -l)" "0"
while read -r name version _; do
	[ -n "$name" ] || continue
	got=$(apk_q "$WORK/post" --fields version "$name" 2>/dev/null | sed -n 's/^Version: *//p')
	eq "$name installed version" "$got" "$version"
done <<TABLE
$APK_LIST
TABLE
while read -r path sha _; do
	case "$path" in ''|\#*) continue ;; esac
	f=$WORK/post/$path
	if [ -f "$f" ]; then
		eq "/$path installed sha256" "$(sha256sum "$f" | awk '{print $1}')" "$sha"
	else
		bad "/$path missing after install"
	fi
done <"$LANE/tools/expected-files.txt"
if grep -q "kernel=$KERNEL_DEP" "$WORK/post/lib/apk/db/installed"; then
	ok "installed db carries the kmod kernel dependency $KERNEL_DEP"
else
	bad "installed db lacks the kmod kernel dependency"
fi

# ------------------------------------------------------------- idempotency ---
printf '\n-- step 3: re-run the install (idempotency) --\n'
payload_manifest "$WORK/post" >"$WORK/post.before-idem"
add_files "$WORK/post"
rci=$?
eq "second apk add rc (no-op)" "$rci" "0"
payload_manifest "$WORK/post" >"$WORK/post.after-idem"
eq "second apk add left the payload unchanged" "$(diff "$WORK/post.before-idem" "$WORK/post.after-idem" | wc -l)" "0"

# ---------------------------------------------------------------- rollback ---
printf '\n-- step 4: apk del rollback back to the pre-T0 state --\n'
del_names "$WORK/post"
rcr=$?
eq "apk del rc" "$rcr" "0"
eq "rollback package count (74)" "$(grep -c '^P:' "$WORK/post/lib/apk/db/installed")" "74"
payload_manifest "$WORK/post" >"$WORK/rollback.payload"
eq "rollback payload tree byte-identical to the baseline" "$(diff "$WORK/rollback.payload" "$WORK/pre.payload" | wc -l)" "0"
while read -r path _; do
	case "$path" in ''|\#*) continue ;; esac
	if [ -e "$WORK/post/$path" ]; then bad "/$path survived the rollback"; else ok "/$path removed by rollback"; fi
done <"$LANE/tools/expected-files.txt"

# ------------------------------------------------------ fail-closed kernel ---
printf '\n-- step 5: negative test - mismatched installed kernel must fail closed --\n'
cp -a "$WORK/ref" "$WORK/neg" || die "cannot copy rootfs to neg"
sed -i "s/^V:6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1\$/V:6.18.60~deadbeefdeadbeefdeadbeefdeadbeef-r1/" "$WORK/neg/lib/apk/db/installed"
if ! grep -q '^V:6.18.60~deadbeef' "$WORK/neg/lib/apk/db/installed"; then die "negative-test db edit failed"; fi
# shellcheck disable=SC2086 # flags are a deliberate list
"$APK" --root "$WORK/neg" $APK_RUN_FLAGS --force-non-repository add \
	"$ART/kmod-hi5622-wifi-6.18.60-r1.apk" >"$WORK/apk-neg.log" 2>&1
rcneg=$?
if [ "$rcneg" -ne 0 ]; then
	ok "mismatched kernel package rejects the kmod install (apk rc=$rcneg)"
else
	bad "mismatched kernel package did NOT reject the kmod install (rc=0)"
fi
if grep -q 'kernel=6.18.60' "$WORK/apk-neg.log"; then
	ok "negative-test log names the unsatisfied kernel= constraint"
else
	bad "negative-test log does not name the kernel constraint"
fi
# control: a matching add on the rolled-back baseline must still pass
# shellcheck disable=SC2086 # flags are a deliberate list
"$APK" --root "$WORK/post" $APK_RUN_FLAGS --force-non-repository add \
	"$ART/iw-6.17-r1.apk" >"$WORK/apk-control.log" 2>&1
eq "control: add on the rolled-back baseline succeeds" "$?" "0"

finish
