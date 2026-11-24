#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# t0-device.sh - device-side half of the T0 zero-flash bench-image deploy.
# Runs on the OpenWrt box (busybox ash) as root from the staged /tmp directory.
# Installs the seven bench .apk packages into the RUNNING pair-B rootfs
# (persistent overlay), verifies the result byte-for-byte, can load the
# clean-room modules FROM /lib/modules (rootfs, not /tmp), and can roll the
# install back with apk del. It never writes flash, never touches the boot
# selector, never reboots and never reads/writes /dev/mem or the UART.
#
# modes:
#   preflight  read-only gates (image, package manager, kernel, overlay, space)
#   install    apk add + full read-back verification + receipt
#   rollback   apk del + verify the pre-install state is restored
#   load       load cfg80211 + hi5622_wifi FROM /lib/modules (rootfs, not /tmp)
#   verify     read-only post-boot verification of an installed T0 set
#
# sets (--set):
#   full       all seven packages incl. the two kmod packages (default). Needs
#              the running kernel package to match the kmod ABI pin.
#   userspace  only iw, iwinfo, libiwinfo, libiwinfo-data, wireless-regdb.
#              No kernel-ABI gate and no modules (native iw/iwinfo now); the
#              kmod ABI is closed separately (REPORT.md section 7).
#
# usage: t0-device.sh MODE STAGEDIR [--set full|userspace] [--param KEY=VALUE ...]
# output: "T0GATE <name> PASS|FAIL <detail>", "T0KV key=value", "T0-RESULT: PASS|FAIL"
set -u
LC_ALL=C
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

KVER_EXPECTED=6.18.60
KERNEL_DEP_EXPECTED=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1
CFG11_KO=/lib/modules/6.18.60/cfg80211.ko
WIFI_KO=/lib/modules/6.18.60/hi5622_wifi.ko
RECEIPT=/root/.wifi-t0-deploy.receipt
SET=full

PASS=0
FAIL=0
ok()  { PASS=$((PASS + 1)); printf 'T0GATE %s PASS %s\n' "$1" "$2"; }
bad() { FAIL=$((FAIL + 1)); printf 'T0GATE %s FAIL %s\n' "$1" "$2"; }
kv()  { printf 'T0KV %s=%s\n' "$1" "$2"; }
finish() {
	if [ "$FAIL" -eq 0 ]; then
		printf 'T0-RESULT: PASS (%d gates, 0 failed)\n' "$PASS"
		exit 0
	fi
	printf 'T0-RESULT: FAIL (%d passed, %d failed)\n' "$PASS" "$FAIL"
	exit 1
}

usage() {
	sed -n '/^# t0-device.sh MODE/,/^# output:/p' "$0" | sed 's/^# \{0,1\}//'
	sed -n '/^# modes:/,/^# output:/p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
}

[ $# -ge 2 ] || usage
MODE=$1
STAGE=$2
shift 2
MODULE_PARAMS=
while [ $# -gt 0 ]; do
	case "$1" in
		--set) SET=$2; shift ;;
		--set=*) SET=$(printf '%s' "$1" | cut -c7-) ;;
		--param) MODULE_PARAMS="$MODULE_PARAMS $2"; shift ;;
		--param=*) MODULE_PARAMS="$MODULE_PARAMS $(printf '%s' "$1" | cut -c9-)" ;;
		*) echo "unknown argument: $1" >&2; usage ;;
	esac
	shift
done
case "$SET" in
	full|userspace) ;;
	*) echo "unknown --set value: $SET (want full|userspace)" >&2; usage ;;
esac
STAGE=$(printf '%s' "$STAGE" | sed 's:/$::')

PKG_LIST=$STAGE/expected-packages.txt
FILE_LIST=$STAGE/expected-files.txt
SEL_PKGS=$STAGE/sel-packages.txt
SEL_FILES=$STAGE/sel-files.txt

# Build the selected package/file lists for the requested set. The manifests
# are always complete (7 packages / 7 payload files); userspace mode filters
# every kmod row and every /lib/modules payload. Writes only the /tmp staging
# dir (the box itself stays untouched in preflight/verify).
selection_init() {
	: >"$SEL_PKGS"
	: >"$SEL_FILES"
	if [ ! -f "$PKG_LIST" ] || [ ! -f "$FILE_LIST" ]; then return 0; fi
	if [ "$SET" = userspace ]; then
		awk '!/^#/ && NF && $1 !~ /^kmod-/' "$PKG_LIST" >"$SEL_PKGS"
		awk '!/^#/ && NF && $1 !~ /^lib\/modules\//' "$FILE_LIST" >"$SEL_FILES"
	else
		cp "$PKG_LIST" "$SEL_PKGS"
		cp "$FILE_LIST" "$SEL_FILES"
	fi
}
selection_init

# ---------------------------------------------------------------- helpers ---
read_pkg_field() { # name logical field (1=version,2=apk_size,3=apk_sha256,4=installed)
	# manifest columns are: 1=name 2=version 3=apk_size 4=apk_sha256 5=installed
	awk -v n="$1" -v f="$2" '$1 == n { print $(f + 1); exit }' "$PKG_LIST"
}
pkg_version() { # installed package version from the local apk db (no network)
	v=$(apk --repositories-file /dev/null --no-network query --fields version "$1" 2>/dev/null \
	    | sed -n 's/^Version: *//p' | head -n1)
	if [ -z "$v" ]; then
		v=$(awk -v p="$1" '
			$0 == "P:" p { found = 1; next }
			found && /^V:/ { print substr($0, 3); exit }
			$0 ~ /^P:/ { found = 0 }' /lib/apk/db/installed 2>/dev/null)
	fi
	printf '%s' "$v"
}

check_image() {
	k=$(uname -r)
	if [ "$k" = "$KVER_EXPECTED" ]; then ok image "uname -r=$k"
	else bad image "uname -r=$k (want $KVER_EXPECTED)"; fi
	if [ -f /lib/apk/db/installed ]; then ok apkdb "/lib/apk/db/installed present"
	else bad apkdb "/lib/apk/db/installed missing (image is not apk-based?)"; fi
	if [ -x /usr/bin/apk ]; then ok apk-bin "/usr/bin/apk present"
	else bad apk-bin "/usr/bin/apk missing"; fi
	av=$(apk --version 2>/dev/null | awk '{print $2}' | head -n1)
	case "$av" in
		3.*) ok apk-version "apk-tools $av (v3 .apk format)" ;;
		*)   if [ -z "$av" ]; then av=unknown; fi
		     bad apk-version "apk-tools $av (need v3)" ;;
	esac
	if [ -s /lib/functions.sh ]; then ok base-files-functions "/lib/functions.sh present"
	else bad base-files-functions "/lib/functions.sh missing (package hooks need it)"; fi
}

check_overlay() {
	if awk '$2 == "/" && $3 == "overlay" { found = 1 } END { exit !found }' /proc/mounts; then
		ok overlay "root is overlayfs (persistent overlay)"
	else
		bad overlay "root is not overlayfs; a T0 install would not survive reboot"
	fi
	if grep -q ' /overlay ' /proc/mounts; then ok overlay-mount "/overlay is mounted"
	else bad overlay-mount "/overlay is not mounted"; fi
	free=$(df -k / 2>/dev/null | awk 'NR == 2 { print $4 }')
	case "$free" in
		''|*[!0-9]*) bad overlay-space "cannot determine free KiB on /" ;;
		*) if [ "$free" -ge 4096 ]; then ok overlay-space "free=$free KiB (need 4096)"
		   else bad overlay-space "free=$free KiB (need 4096)"; fi ;;
	esac
	if [ -w /lib/apk/db ]; then ok apkdb-writable "/lib/apk/db writable"
	else bad apkdb-writable "/lib/apk/db not writable"; fi
	if [ -w /etc/apk ]; then ok etcapk-writable "/etc/apk writable"
	else bad etcapk-writable "/etc/apk not writable"; fi
}

check_kernel_package() {
	kpk=$(pkg_version kernel)
	kv kernel_running "$(uname -r)"
	kv kernel_package "$kpk"
	if [ "$SET" = userspace ]; then
		ok kernel-dep "not applicable: userspace-only set has no kmod (running kernel package recorded above)"
		return 0
	fi
	if [ "$kpk" = "$KERNEL_DEP_EXPECTED" ]; then ok kernel-dep "kernel=$kpk"
	else bad kernel-dep "kernel=$kpk (need $KERNEL_DEP_EXPECTED; kmod install fails closed)"; fi
}

check_stage() {
	if [ -f "$PKG_LIST" ]; then ok stage-pkgs "expected-packages.txt present"
	else bad stage-pkgs "missing $PKG_LIST"; fi
	if [ -f "$FILE_LIST" ]; then ok stage-files "expected-files.txt present"
	else bad stage-files "missing $FILE_LIST"; fi
	if [ -f "$STAGE/t0-device.sh" ]; then ok stage-script "t0-device.sh present"
	else bad stage-script "missing $STAGE/t0-device.sh"; fi
	kv install_set "$SET"
	if [ -s "$SEL_PKGS" ] && [ -s "$SEL_FILES" ]; then ok stage-selection "selection lists generated for the $SET set"
	else bad stage-selection "selection lists empty/missing for the $SET set"; fi
	n=0
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		n=$((n + 1))
		f=$STAGE/$name-$version.apk
		if [ ! -f "$f" ]; then bad "stage-apk-$name" "missing $f"; continue; fi
		sz=$(wc -c <"$f" | tr -d ' ')
		if [ "$sz" != "$asize" ]; then bad "stage-apk-$name" "size $sz != $asize"; continue; fi
		h=$(sha256sum "$f" | awk '{print $1}')
		if [ "$h" = "$asha" ]; then ok "stage-apk-$name" "sha256 ok ($sz B)"
		else bad "stage-apk-$name" "sha256 $h != $asha"; fi
	done <"$SEL_PKGS"
	want=7
	if [ "$SET" = userspace ]; then want=5; fi
	if [ "$n" -eq "$want" ]; then ok stage-count "$n packages staged ($SET set)"
	else bad stage-count "$n packages listed (want $want for the $SET set)"; fi
}

pkg_state() { # absent | same | different:<version>
	name=$1
	want=$(read_pkg_field "$name" 1)
	got=$(pkg_version "$name")
	if [ -z "$got" ]; then echo absent
	elif [ "$got" = "$want" ]; then echo same
	else echo "different:$got"; fi
}

check_pkg_state() {
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		st=$(pkg_state "$name")
		case "$st" in
			absent) ok "pkg-state-$name" "absent (will install)" ;;
			same)   ok "pkg-state-$name" "already installed $version (idempotent verify)" ;;
			*)      bad "pkg-state-$name" "installed $st (expected $version); refusing to clobber" ;;
		esac
	done <"$SEL_PKGS"
}

verify_files() {
	while read -r path sha size; do
		case "$path" in ''|\#*) continue ;; esac
		p=/$path
		if [ ! -f "$p" ]; then bad "file-$path" "missing $p"; continue; fi
		sz=$(wc -c <"$p" | tr -d ' ')
		h=$(sha256sum "$p" | awk '{print $1}')
		if [ "$sz" = "$size" ] && [ "$h" = "$sha" ]; then
			ok "file-$path" "$size B sha256 ok"
		else
			bad "file-$path" "size=$sz sha256=$h (want $size/$sha)"
		fi
	done <"$SEL_FILES"
}

check_version_strings() {
	iwv=$(iw --version 2>/dev/null | head -n1)
	case "$iwv" in
		*6.17*) ok iw-version "$iwv" ;;
		*) bad iw-version "iw --version gave '$iwv' (want 6.17)" ;;
	esac
	kv iw_version "$iwv"
	# iwinfo with no args prints usage when there is no wireless interface, or
	# the interface list (e.g. "wlan0 ESSID: unknown") when one exists; both
	# are healthy. Only a nonzero rc or empty output is a failure. The actual
	# output is recorded for the evidence (never pattern-matched away).
	out=$(iwinfo 2>&1)
	rc=$?
	printf '%s\n' "$out" >"$STAGE/iwinfo-probe.txt"
	if [ "$rc" -ne 0 ]; then
		bad iwinfo-exec "iwinfo rc=$rc output '$(printf '%s' "$out" | head -n1)'"
	elif [ -z "$out" ]; then
		bad iwinfo-exec "iwinfo rc=0 but produced no output"
	else
		kv iwinfo_probe "$(printf '%s' "$out" | head -n3 | tr '\n' '|')"
		ok iwinfo-exec "iwinfo rc=0 from rootfs; output recorded in staging iwinfo-probe.txt"
	fi
	if [ -x /usr/sbin/iw ]; then ok iw-path "/usr/sbin/iw executable"
	else bad iw-path "/usr/sbin/iw missing/not executable"; fi
	if [ -x /usr/bin/iwinfo ]; then ok iwinfo-path "/usr/bin/iwinfo executable"
	else bad iwinfo-path "/usr/bin/iwinfo missing"; fi
}

check_module_one() { # name path
	name=$1
	f=$2
	if [ ! -f "$f" ]; then bad "module-$name" "missing $f"; return; fi
	if grep -q "vermagic=6.18.60 SMP mod_unload ARMv7 p2v8" "$f"; then
		ok "module-$name" "$f vermagic 6.18.60 ARMv7"
	else
		bad "module-$name" "$f vermagic does not match 6.18.60 ARMv7"
	fi
}

check_modules_static() {
	if [ "$SET" = userspace ]; then
		ok modules-skipped "userspace-only set installs no modules (kmod ABI path still open)"
		return 0
	fi
	check_module_one cfg80211 "$CFG11_KO"
	check_module_one hi5622_wifi "$WIFI_KO"
	if grep -q 'depends=cfg80211' "$WIFI_KO" 2>/dev/null; then
		ok module-wifi-depends "hi5622_wifi depends=cfg80211"
	else
		bad module-wifi-depends "hi5622_wifi does not declare depends=cfg80211"
	fi
}

check_no_autoload() {
	if grep -rqsE 'hi5622|cfg80211' /etc/modules.d /etc/modules 2>/dev/null; then
		bad no-autoload "an autoload entry for hi5622/cfg80211 exists (bench policy: none)"
	else
		ok no-autoload "no hi5622/cfg80211 autoload entry (bench policy)"
	fi
}

check_loaded_state() {
	loaded=
	for m in cfg80211 hi5622_wifi; do
		if grep -q "^$m " /proc/modules; then loaded="$loaded $m"; fi
	done
	if [ -z "$loaded" ]; then loaded=none; fi
	kv modules_loaded "$loaded"
}

module_loaded() { grep -q "^$1 " /proc/modules; }

# ------------------------------------------------------------------ modes ---
do_preflight() {
	check_image
	check_overlay
	check_stage
	check_kernel_package
	check_pkg_state
	check_loaded_state
	finish
}

do_install() {
	check_image
	check_overlay
	check_stage
	check_kernel_package
	check_pkg_state
	todo=0
	FILES=
	NEW_LIST=
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		if [ "$(pkg_state "$name")" = absent ]; then
			todo=$((todo + 1))
			NEW_LIST="$NEW_LIST $name"
			FILES="$FILES $STAGE/$name-$version.apk"
		fi
	done <"$SEL_PKGS"
	kv packages_to_install "$todo"
	if [ "$todo" -gt 0 ]; then
		if [ "$FAIL" -ne 0 ]; then
			bad install "pre-install state has failures; not running apk add"
			finish
		fi
		echo "T0ACTION apk add --allow-untrusted --force-non-repository --no-network --repositories-file /dev/null$FILES"
		# resolve the transaction first (--simulate) so a missing dependency or
		# an ABI pin mismatch fails closed before anything is mutated
		# shellcheck disable=SC2086 # FILES is a deliberate space-separated list
		apk add --allow-untrusted --force-non-repository --no-network \
			--repositories-file /dev/null --simulate $FILES >"$STAGE/apk-simulate.log" 2>&1
		rcsim=$?
		kv apk_simulate_rc "$rcsim"
		if [ "$rcsim" -eq 0 ]; then ok apk-resolve "apk add --simulate rc=0 (dependencies and ABI pin resolve)"
		else bad apk-resolve "apk add --simulate rc=$rcsim (see $STAGE/apk-simulate.log)"; fi
		if [ "$rcsim" -ne 0 ]; then
			bad install "resolution failed; apk add not run"
			finish
		fi
		# shellcheck disable=SC2086 # FILES is a deliberate space-separated list
		apk add --allow-untrusted --force-non-repository --no-network \
			--repositories-file /dev/null $FILES >"$STAGE/apk-add.log" 2>&1
		rc=$?
		kv apk_add_rc "$rc"
		if [ "$rc" -eq 0 ]; then ok apk-add "apk add rc=0"
		else bad apk-add "apk add rc=$rc (see $STAGE/apk-add.log)"; fi
	else
		ok apk-add "nothing to install (all packages already present)"
	fi
	# whether this run actually changed the package database: the host uses it
	# to decide whether an automatic rollback may remove the set (an idempotent
	# verify of a pre-existing install must never delete it).
	mutated=0
	if [ "$todo" -gt 0 ]; then mutated=1; fi
	kv install_mutated "$mutated"
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		got=$(pkg_version "$name")
		if [ "$got" = "$version" ]; then ok "pkg-$name" "installed $got"
		else bad "pkg-$name" "installed '$got' (want $version)"; fi
		need_world=0
		case " $NEW_LIST " in
			*" $name "*) need_world=1 ;;
		esac
		# apk v3 world lines look like "name><hash"; older ones "name" or "name=ver"
		if grep -qE "^$name([=<>]|\$)" /etc/apk/world 2>/dev/null; then
			ok "world-$name" "world entry present"
		elif [ "$need_world" -eq 1 ]; then
			bad "world-$name" "no world entry after this run installed it (would not be tracked)"
		else
			ok "world-$name" "pre-existing package without a world entry (idempotent verify; apk del by name still removes it)"
		fi
	done <"$SEL_PKGS"
	verify_files
	check_version_strings
	check_modules_static
	check_no_autoload
	check_loaded_state
	if [ "$FAIL" -eq 0 ]; then
		pkg_n=$(grep -cvE '^#|^[[:space:]]*$' "$SEL_PKGS")
		{
			echo "image=6.18.60"
			echo "set=$SET"
			if [ "$SET" = userspace ]; then
				echo "kernel_package=not-applicable (userspace-only set)"
			else
				echo "kernel_package=$KERNEL_DEP_EXPECTED"
			fi
			echo "stamp=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)"
			echo "packages=$pkg_n"
		} >"$RECEIPT" 2>/dev/null
		if [ -f "$RECEIPT" ]; then ok receipt "wrote $RECEIPT"
		else bad receipt "cannot write $RECEIPT"; fi
	fi
	finish
}

do_rollback() {
	check_image
	names=
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		if [ -n "$(pkg_version "$name")" ]; then names="$names $name"; fi
	done <"$SEL_PKGS"
	if [ -z "$names" ]; then names=none; fi
	kv packages_present "$names"
	if [ "$names" = none ]; then
		ok apk-del "nothing to remove (set already absent)"
	else
		# shellcheck disable=SC2086 # names is a deliberate space-separated list
		apk del --no-network --repositories-file /dev/null $names >"$STAGE/apk-del.log" 2>&1
		rc=$?
		kv apk_del_rc "$rc"
		if [ "$rc" -eq 0 ]; then ok apk-del "apk del rc=0"
		else bad apk-del "apk del rc=$rc (see $STAGE/apk-del.log)"; fi
	fi
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		got=$(pkg_version "$name")
		if [ -z "$got" ]; then ok "gone-$name" "package removed"
		else bad "gone-$name" "still installed ($got)"; fi
	done <"$SEL_PKGS"
	while read -r path sha size; do
		case "$path" in ''|\#*) continue ;; esac
		if [ -e "/$path" ]; then bad "gone-file-$path" "/$path still present"
		else ok "gone-file-$path" "removed"; fi
	done <"$SEL_FILES"
	if [ -f "$RECEIPT" ]; then
		rm -f "$RECEIPT"
		if [ -f "$RECEIPT" ]; then bad receipt-removed "cannot remove $RECEIPT"
		else ok receipt-removed "receipt removed"; fi
	else
		ok receipt-removed "no receipt to remove"
	fi
	check_loaded_state
	finish
}

do_load() {
	check_image
	if [ "$SET" = userspace ]; then
		bad load-requires-full "userspace-only set installs no modules; load needs --set full (or the /tmp insmod flow)"
		finish
	fi
	check_modules_static
	if [ "$FAIL" -ne 0 ]; then finish; fi
	case "$CFG11_KO" in /tmp/*) bad load-path "cfg80211 path is under /tmp"; finish ;; esac
	case "$WIFI_KO" in /tmp/*) bad load-path "driver path is under /tmp"; finish ;; esac
	if module_loaded cfg80211; then
		ok load-cfg11 "cfg80211 already loaded"
	else
		insmod "$CFG11_KO" >"$STAGE/insmod-cfg80211.log" 2>&1
		rc=$?
		kv insmod_cfg11_rc "$rc"
		if [ "$rc" -eq 0 ]; then ok load-cfg11 "insmod $CFG11_KO rc=0"
		else bad load-cfg11 "insmod rc=$rc (see $STAGE/insmod-cfg80211.log)"; fi
	fi
	if module_loaded cfg80211; then ok cfg11-present "cfg80211 in /proc/modules"
	else bad cfg11-present "cfg80211 not in /proc/modules"; fi
	if module_loaded hi5622_wifi; then
		bad load-wifi "hi5622_wifi already loaded (bench policy: one module load per boot)"
	else
		# shellcheck disable=SC2086 # MODULE_PARAMS is a deliberate list
		insmod "$WIFI_KO" $MODULE_PARAMS >"$STAGE/insmod-hi5622.log" 2>&1
		rc=$?
		kv insmod_wifi_rc "$rc"
		if [ "$rc" -eq 0 ]; then ok load-wifi "insmod $WIFI_KO $MODULE_PARAMS rc=0"
		else bad load-wifi "insmod rc=$rc (see $STAGE/insmod-hi5622.log)"; fi
	fi
	if module_loaded hi5622_wifi; then ok wifi-present "hi5622_wifi in /proc/modules"
	else bad wifi-present "hi5622_wifi not in /proc/modules"; fi
	if [ -d /sys/module/hi5622_wifi ]; then ok wifi-sysfs "/sys/module/hi5622_wifi present"
	else bad wifi-sysfs "/sys/module/hi5622_wifi missing"; fi
	dmesg 2>/dev/null | tail -n 80 >"$STAGE/dmesg-after-load.txt"
	if grep -qE 'BUG:|WARNING:|Oops|Call trace|Kernel panic' "$STAGE/dmesg-after-load.txt"; then
		bad dmesg-clean "kernel fault signature after module load"
	else
		ok dmesg-clean "no BUG/WARNING/Oops/panic in last 80 dmesg lines"
	fi
	iw dev >"$STAGE/iw-dev-after-load.txt" 2>&1
	rc=$?
	kv iw_dev_rc "$rc"
	if [ "$rc" -eq 0 ]; then ok iw-dev "iw dev rc=0 (native iw, nl80211 reachable)"
	else bad iw-dev "iw dev rc=$rc (see $STAGE/iw-dev-after-load.txt)"; fi
	printf 'T0NOTE module load is the write-free probe only; the wiphy registers only after the attended golden boot.\n'
	printf 'T0NOTE leave the modules loaded and reboot before a normal phase-matrix session.\n'
	finish
}

do_verify() {
	check_image
	check_stage
	check_pkg_state
	verify_files
	check_version_strings
	check_modules_static
	check_no_autoload
	check_loaded_state
	if [ -f "$RECEIPT" ]; then ok receipt "receipt present"
	else bad receipt "receipt missing"; fi
	finish
}

case "$MODE" in
	preflight) do_preflight ;;
	install)   do_install ;;
	rollback)  do_rollback ;;
	load)      do_load ;;
	verify)    do_verify ;;
	*) echo "unknown mode: $MODE" >&2; usage ;;
esac
