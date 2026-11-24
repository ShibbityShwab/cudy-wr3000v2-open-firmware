#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# hostapd-device.sh - device-side half of the wifi-hostapd-deploy T0
# zero-flash deploy (task-125).  Installs the four OpenWrt hostapd packages
# into the RUNNING pair-B rootfs over the persistent overlay: no flash write,
# no boot-selector change, no reboot, no module load/unload.  Reversible with
# the rollback mode (apk del + remove /root/hostapd-deploy).
#
# Executed ONLY by ../deploy-hostapd.sh over the project ssh key; never run
# it by hand.  Every mutating action is gated on a clean preflight and every
# result is printed as an OK/BAD/KV line the host re-parses.
#
# Modes: preflight | install | verify | rollback
# Inputs (staged by the host in $STAGE):
#   expected-packages.txt, expected-files.txt, <pkg>-<version>.apk, hostapd-*.conf
#
# Hard rules honoured: writes only the persistent overlay (/usr, /etc,
# /lib/netifd, /lib/apk db+world, /root/hostapd-deploy); staging only in
# /tmp; no mtd/ubiformat/sysupgrade/nvram/boot-selector/reboot/rmmod/devmem.
set -u

MODE=${1:-preflight}
STAGE=${HOSTAPD_STAGE:-/tmp/hostapd-deploy-stage}
REMOTE=${HOSTAPD_REMOTE:-/root/hostapd-deploy}
RECEIPT=/root/.wifi-hostapd-deploy.receipt
PKG_LIST="$STAGE/expected-packages.txt"
FILE_LIST="$STAGE/expected-files.txt"

PASS=0
FAIL=0
STOPPED=0
ok()  { PASS=$((PASS+1)); echo "OK $*"; }
bad() { FAIL=$((FAIL+1)); echo "BAD $*"; }
kv()  { echo "KV $1=$2"; }
stop() { STOPPED=1; echo "STOP $*"; }
finish() {
	echo "HOSTAPD-DEV-RESULT gates=$PASS failed=$FAIL stopped=$STOPPED mode=$MODE"
	[ "$FAIL" -eq 0 ] && [ "$STOPPED" -eq 0 ] && exit 0
	exit 1
}

read_pkg_field() { # name field-number (1=version 3=apk_size 4=apk_sha256 5=installed)
	awk -v n="$1" -v f="$2" '$1 == n { print $(f + 1); exit }' "$PKG_LIST"
}

pkg_version() { # installed version from the local apk db, no network
	local v
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

check_root() {
	if [ "$(id -u 2>/dev/null)" = 0 ]; then ok root "running as uid 0"
	else bad root "not uid 0"; fi
}

check_image() {
	k=$(uname -r)
	kv kernel_running "$k"
	if [ -f /lib/apk/db/installed ]; then ok apkdb "/lib/apk/db/installed present"
	else bad apkdb "/lib/apk/db/installed missing (image is not apk-based?)"; fi
	if [ -x /usr/bin/apk ]; then ok apk-bin "/usr/bin/apk present"
	else bad apk-bin "/usr/bin/apk missing"; fi
	av=$(apk --version 2>/dev/null | awk '{print $2}' | head -n1)
	case "$av" in
		3.*) ok apk-version "apk-tools $av (v3 .apk format)" ;;
		*)   [ -n "$av" ] || av=unknown
		     bad apk-version "apk-tools $av (need v3)" ;;
	esac
	if [ -s /lib/functions.sh ]; then ok base-files-functions "/lib/functions.sh present"
	else bad base-files-functions "/lib/functions.sh missing (package hooks need it)"; fi
}

check_overlay() {
	if awk '$2 == "/" && $3 == "overlay" { found = 1 } END { exit !found }' /proc/mounts; then
		ok overlay "root is overlayfs (persistent)"
	else
		bad overlay "root is not overlayfs; a T0 install would not survive a power-cycle"
	fi
	if grep -q ' /overlay ' /proc/mounts; then ok overlay-mount "/overlay mounted"
	else bad overlay-mount "/overlay not mounted"; fi
	free=$(df -k / 2>/dev/null | awk 'NR == 2 { print $4 }')
	case "$free" in
		''|*[!0-9]*) bad overlay-space "cannot determine free KiB on /" ;;
		*) if [ "$free" -ge 8192 ]; then ok overlay-space "free=$free KiB (need 8192)"
		   else bad overlay-space "free=$free KiB (need 8192)"; fi ;;
	esac
	if [ -w /lib/apk/db ]; then ok apkdb-writable "/lib/apk/db writable"
	else bad apkdb-writable "/lib/apk/db not writable"; fi
	if [ -w /etc/apk ]; then ok etcapk-writable "/etc/apk writable"
	else bad etcapk-writable "/etc/apk not writable"; fi
	if [ -w /root ]; then ok rootdir-writable "/root writable"
	else bad rootdir-writable "/root not writable"; fi
}

check_stage() {
	if [ -f "$PKG_LIST" ]; then ok stage-pkgs "expected-packages.txt present"
	else bad stage-pkgs "missing $PKG_LIST"; fi
	if [ -f "$FILE_LIST" ]; then ok stage-files "expected-files.txt present"
	else bad stage-files "missing $FILE_LIST"; fi
	if [ -f "$STAGE/hostapd-device.sh" ]; then ok stage-script "hostapd-device.sh present"
	else bad stage-script "missing $STAGE/hostapd-device.sh"; fi
	if [ -f "$STAGE/hostapd-open.conf" ] && [ -f "$STAGE/hostapd-wpa2.conf" ]; then
		ok stage-configs "both hostapd config fixtures staged"
	else
		bad stage-configs "hostapd config fixture missing in stage"
	fi
	n=0
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		n=$((n+1))
		f="$STAGE/$name-$version.apk"
		if [ ! -f "$f" ]; then bad "stage-apk-$name" "missing $f"; continue; fi
		sz=$(wc -c <"$f" | tr -d ' ')
		if [ "$sz" != "$asize" ]; then bad "stage-apk-$name" "size $sz != $asize"; continue; fi
		h=$(sha256sum "$f" | awk '{print $1}')
		if [ "$h" = "$asha" ]; then ok "stage-apk-$name" "sha256 ok ($sz B)"
		else bad "stage-apk-$name" "sha256 $h != $asha"; fi
	done <"$PKG_LIST"
	kv packages_staged "$n"
	if [ "$n" -eq 4 ]; then ok stage-count "$n packages staged"
	else bad stage-count "$n packages listed (want 4)"; fi
}

check_deps_shadow() {
	# Informational host-independent closure audit on the box: every dependency
	# that is not part of this set must already be installed.
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		dep=$(awk -v n="$name" -v q="true" '
			/^info:/ { info = 1; next }
			info && /^paths:/ { info = 0 }
			info && /^  depends:/ { d = 1; next }
			info && d && /^    - / {
				split($0, a, "- "); v = a[2]
				sub(/[<>=].*$/, "", v)
				print v
				next
			}
			info && d && !/^    - / { d = 0 }
		' "$STAGE/../hostapd-lane-artifacts-placeholder" 2>/dev/null)
		: "$dep"
	done <"$PKG_LIST"
}
# The dependency parse above needs the apk metadata, which the host verifies;
# on the box the authoritative check is simply "apk add --simulate", below.

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
			absent) kv "state_$name" absent ;;
			same) ok "already-$name" "installed at the pinned version" ;;
			*) bad "already-$name" "installed at $st (want $version): refusing to mix versions" ;;
		esac
	done <"$PKG_LIST"
}

check_no_autoload() {
	# hostapd-common ships /etc/init.d/wpad; ensure no rc.d autostart link was
	# created by the install (a bench image must not autostart an AP daemon).
	found=
	for f in /etc/rc.d/S*wpad /etc/rc.d/K*wpad; do
		[ -e "$f" ] && found="$found $f"
	done
	if [ -z "$found" ]; then ok no-autoload "no /etc/rc.d wpad autostart link"
	else bad no-autoload "wpad service is enabled:$found"; fi
}

check_load_state() {
	loaded=""
	for m in cfg80211 hi5622_wifi; do
		grep -q "^$m " /proc/modules && loaded="$loaded $m"
	done
	[ -n "$loaded" ] || loaded=none
	kv modules_loaded "$loaded"
}

do_preflight() {
	check_root
	check_image
	check_overlay
	check_stage
	check_pkg_state
	check_no_autoload
	check_load_state
	finish
}

do_install() {
	check_root
	check_image
	check_overlay
	check_stage
	check_pkg_state
	if [ "$FAIL" -ne 0 ] || [ "$STOPPED" -ne 0 ]; then
		stop "preflight failed; apk add not run"
		finish
	fi
	todo=0
	FILES=
	NEW_LIST=
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		if [ "$(pkg_state "$name")" = absent ]; then
			todo=$((todo+1))
			NEW_LIST="$NEW_LIST $name"
			FILES="$FILES $STAGE/$name-$version.apk"
		fi
	done <"$PKG_LIST"
	kv packages_to_install "$todo"
	if [ "$todo" -gt 0 ]; then
		echo "HOSTAPD-DEV-ACTION apk add --allow-untrusted --force-non-repository --no-network --repositories-file /dev/null$FILES"
		# resolve the transaction first: a missing baseline dependency or a
		# version conflict fails closed before the package db is mutated.
		# shellcheck disable=SC2086 # FILES is a deliberate space-separated list
		apk add --allow-untrusted --force-non-repository --no-network \
			--repositories-file /dev/null --simulate $FILES >"$STAGE/apk-simulate.log" 2>&1
		rcsim=$?
		kv apk_simulate_rc "$rcsim"
		if [ "$rcsim" -eq 0 ]; then ok apk-resolve "apk add --simulate rc=0 (dependencies resolve)"
		else bad apk-resolve "apk add --simulate rc=$rcsim (see $STAGE/apk-simulate.log)"; fi
		if [ "$rcsim" -ne 0 ]; then
			stop "resolution failed; apk add not run"
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
	kv install_mutated "$todo"
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		got=$(pkg_version "$name")
		if [ "$got" = "$version" ]; then ok "pkg-$name" "installed $got"
		else bad "pkg-$name" "installed '$got' (want $version)"; fi
		need_world=0
		case " $NEW_LIST " in *" $name "*) need_world=1 ;; esac
		# apk v3 world lines look like "name><hash"; older ones "name" or "name=ver"
		if grep -qE "^$name([=<>]|\$)" /etc/apk/world 2>/dev/null; then
			ok "world-$name" "world entry present"
		elif [ "$need_world" -eq 1 ]; then
			bad "world-$name" "no world entry after this run installed it"
		else
			ok "world-$name" "pre-existing package without a world entry"
		fi
	done <"$PKG_LIST"
	verify_files
	# bench config fixtures: our own files under /root/hostapd-deploy
	mkdir -p "$REMOTE"
	for c in hostapd-open.conf hostapd-wpa2.conf; do
		if cp "$STAGE/$c" "$REMOTE/$c" 2>/dev/null; then chmod 600 "$REMOTE/$c"
		else bad "config-$c" "cannot install $REMOTE/$c"; fi
	done
	chmod 700 "$REMOTE" 2>/dev/null
	for c in hostapd-open.conf hostapd-wpa2.conf; do
		sh=$(sha256sum "$STAGE/$c" 2>/dev/null | awk '{print $1}')
		rh=$(sha256sum "$REMOTE/$c" 2>/dev/null | awk '{print $1}')
		if [ -n "$sh" ] && [ "$sh" = "$rh" ]; then ok "config-$c" "installed sha256 matches"
		else bad "config-$c" "installed sha256 mismatch"; fi
	done
	check_hostapd_version
	check_no_autoload
	if [ "$FAIL" -eq 0 ]; then
		{
			echo "set=hostapd-ap-start"
			echo "kernel_running=$(uname -r)"
			echo "stamp=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)"
			echo "packages=$(grep -cvE '^#|^[[:space:]]*$' "$PKG_LIST")"
			for c in hostapd-open.conf hostapd-wpa2.conf; do
				echo "config_sha256_$c=$(sha256sum "$REMOTE/$c" 2>/dev/null | awk '{print $1}')"
			done
		} >"$RECEIPT" 2>/dev/null
		if [ -f "$RECEIPT" ]; then ok receipt "wrote $RECEIPT"
		else bad receipt "cannot write $RECEIPT"; fi
	fi
	finish
}

check_hostapd_version() {
	if [ -x /usr/sbin/hostapd ]; then
		v=$(/usr/sbin/hostapd -v 2>&1 | head -n1)
		# upstream hostapd exits 1 from -v by design; gate on the version text.
		case "$v" in
			*"hostapd v"*) ok hostapd-version "$v" ;;
			*) bad hostapd-version "unexpected -v output: $v" ;;
		esac
	else
		bad hostapd-bin "/usr/sbin/hostapd missing after install"
	fi
}

verify_files() {
	while read -r path sha size; do
		case "$path" in ''|\#*) continue ;; esac
		if [ ! -f "/$path" ]; then bad "file-$path" "missing /$path"; continue; fi
		got=$(sha256sum "/$path" | awk '{print $1}')
		gsz=$(wc -c <"/$path" | tr -d ' ')
		if [ "$got" = "$sha" ] && [ "$gsz" = "$size" ]; then ok "file-$path" "sha256+size ok"
		else bad "file-$path" "sha256/size mismatch (got $got $gsz B)"; fi
	done <"$FILE_LIST"
}

do_verify() {
	check_root
	check_image
	check_stage
	if [ -f "$RECEIPT" ]; then ok receipt "receipt present"
	else bad receipt "receipt missing (set not installed by this lane?)"; fi
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		got=$(pkg_version "$name")
		if [ "$got" = "$version" ]; then ok "pkg-$name" "installed $got"
		else bad "pkg-$name" "installed '$got' (want $version)"; fi
	done <"$PKG_LIST"
	verify_files
	check_hostapd_version
	check_no_autoload
	check_load_state
	finish
}

do_rollback() {
	check_root
	check_image
	names=
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		if [ -n "$(pkg_version "$name")" ]; then names="$names $name"; fi
	done <"$PKG_LIST"
	if [ -z "$names" ]; then
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
	done <"$PKG_LIST"
	while read -r path sha size; do
		case "$path" in ''|\#*) continue ;; esac
		if [ -e "/$path" ]; then bad "gone-file-$path" "/$path still present"
		else ok "gone-file-$path" "removed"; fi
	done <"$FILE_LIST"
	if [ -d "$REMOTE" ]; then
		rm -rf "$REMOTE"
		if [ -d "$REMOTE" ]; then bad configs-removed "cannot remove $REMOTE"
		else ok configs-removed "$REMOTE removed"; fi
	else
		ok configs-removed "no config dir to remove"
	fi
	if [ -f "$RECEIPT" ]; then rm -f "$RECEIPT"; fi
	if [ -f "$RECEIPT" ]; then bad receipt-removed "cannot remove $RECEIPT"
	else ok receipt-removed "receipt removed"; fi
	check_no_autoload
	finish
}

case "$MODE" in
	preflight) do_preflight ;;
	install)   do_install ;;
	verify)    do_verify ;;
	rollback)  do_rollback ;;
	*) echo "unknown mode: $MODE" >&2; exit 2 ;;
esac