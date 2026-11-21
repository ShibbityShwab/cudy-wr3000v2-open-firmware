#!/bin/sh
# capture-runtime-state.sh - read-only LSW/PIE register + proc capture for the native
# forwarding lane (opensource/docs/incoming/lsw-forwarding).
#
# Runs ON THE BOX (stock vendor image or our 6.18 image). Issues only READ commands:
#   devmem <addr> 32
# It has no write path. Output is a directory of text files plus a sha256 manifest.
#
# Usage: sh capture-runtime-state.sh [--profile core|full] [--out DIR]
#   core (default): identity, key LSW windows first 0x100 words, IFC default entries,
#                   tagact first 64 entries (the high-value tables), PIE RX config words
#   full          : core + both IFC tab128 banks (128 entries each), L2 candidate pages
#                   (first 0x400 words of each), PA/NAPT/MCC full first 0x400 words
#
# Safety: no register outside the LSW/PIE windows is touched. The queue/flow DDR windows
# (0x80000000/0x80400000), the CRG page (0x14880000), the RC misc window (0x10161000) and
# the CA cell (0x400392f0) are never addressed.

set -u

PROFILE=core
OUT=""
while [ $# -gt 0 ]; do
	case "$1" in
	--profile) PROFILE="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
	*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

case "$PROFILE" in core|full) ;; *) echo "bad profile: $PROFILE" >&2; exit 2 ;; esac

DEV=""
for cand in /usr/bin/devmem /bin/devmem; do
	[ -x "$cand" ] && DEV="$cand" && break
done
if [ -z "$DEV" ]; then
	DEV=$(command -v devmem 2>/dev/null || true)
fi
if [ -z "$DEV" ]; then
	echo "no devmem tool on this image - capture aborted (no writes attempted)" >&2
	exit 3
fi

[ -n "$OUT" ] || OUT="/tmp/lswcap-$(date +%Y%m%dT%H%M%S)"
mkdir -p "$OUT" || exit 4

echo "capture-runtime-state: profile=$PROFILE out=$OUT devmem=$DEV"
echo "capture-runtime-state: READ-ONLY capture - no devmem write form is used"

# read one word and print "0xADDR 0xVALUE" (normalised on the host later)
rd() {
	_v=$("$DEV" "$1" 32 2>/dev/null | tr -d ' \t\r\n' | head -c 10)
	[ -n "$_v" ] || { echo "0x$1 READ_FAIL" >>"$OUT/errors.txt"; return 0; }
	case "$_v" in 0x*|0X*) ;; *) _v="0x$_v" ;; esac
	printf '0x%08x %s\n' "$1" "$_v" >>"$OUT/regs.txt"
}

# dump N words starting at physical address A
dump() {
	_a=$(($1)); _n=$(($2))
	_i=0
	while [ "$_i" -lt "$_n" ]; do
		rd $((_a + _i * 4))
		_i=$((_i + 1))
	done
}

# identity + boot context
{
	echo "== capture-runtime-state.sh =="
	date -u 2>/dev/null || true
	uname -a 2>/dev/null || true
	echo "-- /proc/cmdline --"
	cat /proc/cmdline 2>/dev/null || true
	echo "-- /proc/cpuinfo processors --"
	grep -c '^processor' /proc/cpuinfo 2>/dev/null || true
	echo "-- /sys/class/net --"
	ls /sys/class/net 2>/dev/null || true
	echo "-- lsmod (ours; stock may differ) --"
	lsmod 2>/dev/null | head -40 || true
} >"$OUT/meta.txt" 2>&1

: >"$OUT/regs.txt"
: >"$OUT/errors.txt"

# --- core register groups (physical addresses) -------------------------------------------
# LSW register windows: dp/ips/fam/pe/eqm/eps/pfe_glb/ifc/l2/napt/mcc/opp/pub
# (the queue 0x80000000 and flow 0x80400000 DDR windows are NOT register pages - not read)
for base in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 \
            0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 \
            0x12400000 0x12480000; do
	dump "$base" 0x100
done
# IFC default-classification entries 1..8 (bank0)
for idx in 1 2 3 4 5 6 7 8; do
	dump $((0x12150000 + idx * 16)) 4
done
# tag action table: pub+0x54000, first 64 entries x 16 bytes
dump 0x124d4000 $((64 * 4))
# PIE RX config/state windows of interest (patch 048/053)
dump 0x10a70000 0x40
dump 0x10a73400 0x10
dump 0x10a73500 0x40
dump 0x10a73700 0x20

if [ "$PROFILE" = full ]; then
	# both IFC tab128 banks, all 128 entries
	for idx in $(seq 0 127); do
		dump $((0x12150000 + idx * 16)) 4
		dump $((0x12160000 + idx * 16)) 4
	done
	# L2 candidate table pages: first 0x400 words of each region
	for off in 0x0 0x8000 0x10000 0x18000 0x20000 0x40000 0x60000 0x70000; do
		dump $((0x12180000 + off)) 0x400
	done
	# PA / NAPT / MCC / OPP deeper first pages
	dump 0x12081000 0x100
	dump 0x12280000 0x400
	dump 0x12380000 0x400
	dump 0x12400000 0x100
	dump 0x12442800 0x200
	dump 0x12443800 0x100
	# EPS channel map + extended words (W6/W12)
	dump 0x11280b00 0x20
	dump 0x11280b80 0x20
	dump 0x11280b20 0x20
	dump 0x11280ba0 0x20
fi

# vendor CFE/procfs views (stock image only; absent on ours)
if [ -d /proc/hsan ]; then
	{
		echo "== /proc/hsan tree =="
		ls -R /proc/hsan 2>&1 | head -200
		echo "== known reads =="
		for f in /proc/hsan/cfe/intf/dump_intf /proc/hsan/net/swa/rootif_dump \
		         /proc/hsan/net/swa/wan_port /proc/hsan/lsw/dump; do
			echo "-- $f --"
			cat "$f" 2>&1 | head -400
		done
	} >"$OUT/proc-hsan.txt" 2>&1
else
	echo "no /proc/hsan on this image (expected on our 6.18 image)" >"$OUT/proc-hsan.txt"
fi

# kernel log tail (best effort; stock telnet may allow it)
dmesg 2>/dev/null | tail -400 >"$OUT/dmesg.txt" 2>&1 || true

# manifest
( cd "$OUT" && sha256sum * 2>/dev/null >sha256.txt ) || true

echo "capture-runtime-state: done"
echo "  files: $(ls "$OUT" 2>/dev/null | tr '\n' ' ')"
echo "  words captured: $(wc -l <"$OUT/regs.txt" 2>/dev/null || echo 0), read failures: $(wc -l <"$OUT/errors.txt" 2>/dev/null || echo 0)"
echo "  next: copy '$OUT' to the host and run diff-captures.py --ours ... --stock ..."
