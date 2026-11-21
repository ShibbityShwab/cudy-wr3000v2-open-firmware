#!/bin/sh
# apply-napt-init.sh - M1a follow-up prototype: replay the decoded vendor NAPT/flow
# engine initialization (hi_pfe_napt_init) with the stock-captured final values.
#
# ON-BOX script, attended only. Reads/writes an explicit 13-address allow-list:
#   pfe_glb+0x2c, napt+0x50/0x54/0x60/0x64/0x68/0x70/0x130/0x134/0x138/0x13c
# Nothing else is touched. No DDR is written by the CPU (the engine clears and owns its
# tables in the reserved switch flow window 0x80400000/1 MiB, which is outside Linux
# memory - the DT memory node starts at 0x80500000).
#
#   print plan: sh apply-napt-init.sh
#   apply:      LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-napt-init.sh --apply
#   revert:     LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-napt-init.sh --revert
#
# The script refuses unless the mirror workaround is active in the running command line
# (management must stay reachable), unless the flow engine looks unconfigured, snapshots
# every target, verifies every readable write, polls both clear commands with a bound,
# and restores everything on the first mismatch.

set -u
DEV=""
for c in /usr/bin/devmem /bin/devmem; do [ -x "$c" ] && DEV="$c" && break; done
[ -n "$DEV" ] || DEV=$(command -v devmem 2>/dev/null || true)
[ -n "$DEV" ] || { echo "no devmem tool" >&2; exit 3; }

RB="$0.rollback.$(date +%Y%m%dT%H%M%S)"

GLB=/tmp/../pc; GLB=""
GLB_ADDR=0x1200002c
NAPT=0
N50=0x12280050; N54=0x12280054; N60=0x12280060; N64=0x12280064
N68=0x12280068; N70=0x12280070
N130=0x12280130; N134=0x12280134; N138=0x12280138; N13C=0x1228013c

rd() { "$DEV" "$1" 32 2>/dev/null | tr -d ' \t\r\n'; }
wr() { "$DEV" "$1" 32 "$2"; }
dec() { v=$(rd "$1"); [ -n "$v" ] || { echo "READ-FAIL $1" >&2; exit 9; }; v=$((v)); echo "$v"; }

MODE=""
for arg in "$@"; do
  case "$arg" in
    --apply) MODE=apply ;;
    --revert) MODE=revert ;;
    -h|--help) MODE=help ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done
case "$MODE" in
help|"") [ -n "$MODE" ] || MODE=print ;;
esac

print_plan() {
  echo "=== NAPT init replay (no writes performed) ==="
  echo "  1 0x1200002c <- 0x10000034   (pfe_glb up_port high16 0x1000)"
  echo "  2 0x12280070 <- 0x00110138   (flow ctrl/mode)"
  echo "  3 0x12280064 <- 0x00000001   (engine enable)"
  echo "  4 0x12280054 <- 0x00000100   (flow_age_en)"
  echo "  5 0x12280130 <- 0x80400000   (flow table base A, switch carveout)"
  echo "  6 0x12280138 <- 0x80408000   (flow table base B)"
  echo "  7 0x12280134 <- 0x80448000   (flow table base C)"
  echo "  8 0x1228013c <- 0x80450000   (flow table base D)"
  echo "  9 0x12280068 <- 0x00020303   (hash/aging config)"
  echo " 10 0x12280060 <- 0x000a0001   (v6 table clear; poll bit0 clear)"
  echo " 11 0x12280060 <- 0x00080001   (v4 table clear; poll bit0 clear)"
  echo " 12 0x12280050 <- 0x00f00b69   (hash params)"
  echo "gates: LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 ; management mirror must be active"
}
if [ "$MODE" = help ]; then print_plan; exit 0; fi
if [ "$MODE" = print ]; then print_plan; exit 0; fi

# ---- gates -------------------------------------------------------------
[ "${LSW_WRITE_ATTENDED:-0}" = 1 ] && [ "${LSW_RECOVERY_ARMED:-0}" = 1 ] || {
  echo "refusing: LSW_WRITE_ATTENDED=1 and LSW_RECOVERY_ARMED=1 are required" >&2; exit 5; }
if [ "$MODE" = revert ]; then
  [ -f "$RB" ] || RB=$(ls -t "$0".rollback.* 2>/dev/null | head -n 1)
  [ -f "$RB" ] || { echo "no rollback file for $0" >&2; exit 7; }
  echo "reverting from $RB"
  while read -r a v; do
    [ -n "$a" ] || continue
    wr "$a" "$v" >/dev/null || { echo "revert write failed at $a" >&2; exit 8; }
    got=$(dec "$a"); [ "$got" = "$(($v))" ] || echo "revert readback differs at $a (got $got)" >&2
  done <"$RB"
  echo "revert done"
  exit 0
fi

# ---- apply-only preflight (fail closed) -------------------------------
if [ "${LSW_TEST_SKIP_CMDLINE:-0}" != 1 ]; then
  grep -q 'luofu_switch.cpu_mirror=1' /proc/cmdline 2>/dev/null || {
    echo "refusing: cpu_mirror=1 is not in the running command line; the management path would not be guaranteed" >&2; exit 6; }
  grep -q 'luofu_switch.mirror_dst=4' /proc/cmdline 2>/dev/null || {
    echo "refusing: mirror_dst=4 is not in the running command line" >&2; exit 6; }
fi
if [ "${LSW_TEST_SKIP_PREFLIGHT:-0}" != 1 ]; then
  for a in 0x12280130 0x12280134 0x12280138 0x1228013c; do
    v=$(rd "$a"); v=$((v))
    [ "$v" -eq 0 ] || { echo "refusing: $a = $v (flow table bases already configured; unexpected on our baseline)" >&2; exit 10; }
  done
  v=$(rd 0x12280064); v=$((v))
  [ "$v" -eq 0 ] || { echo "refusing: napt+0x64 = $v (flow engine already enabled)" >&2; exit 10; }
fi

# ---- snapshot ----------------------------------------------------------
: >"$RB"
echo "snapshotting rollback words into $RB"
for a in "$GLB_ADDR" "$N70" "$N64" "$N54" "$N130" "$N134" "$N138" "$N13C" "$N68" "$N60" "$N50"; do
  v=$(rd "$a"); [ -n "$v" ] || { echo "cannot read $a; aborting" >&2; exit 9; }
  echo "$a $v" >>"$RB"
done

APPLIED=""
restore_all() {
  for a in $APPLIED; do
    r=$(awk -v a="$a" '$1==a{print $2}' "$RB" | head -n 1)
    [ -n "$r" ] && wr "$a" "$r" >/dev/null 2>&1
  done
}
fail() {
  echo "FAILED: $1" >&2
  echo "restoring the words written so far" >&2
  restore_all
  exit 11
}

write_verify() {
  a="$1"; v="$2"
  echo "write $a <- $v"
  wr "$a" "$v" >/dev/null || fail "write failed at $a"
  APPLIED="$a $APPLIED"
  got=$(rd "$a"); got=$((got)); want=$((v))
  [ "$got" = "$want" ] || fail "readback mismatch at $a: wrote $want, read $got"
  echo "  readback ok"
}

command_poll() {
  a="$1"; v="$2"; limit="$3"; i=0
  echo "command $a <- $v (poll bit0 clear)"
  wr "$a" "$v" >/dev/null || fail "command write failed at $a"
  while [ "$i" -lt "$limit" ]; do
    cur=$(rd "$a"); cur=$((cur))
    [ $((cur & 1)) -eq 0 ] && { echo "  command completed (value $cur)"; return 0; }
    i=$((i + 1))
  done
  fail "command at $a did not complete within $limit polls"
}

# ---- the vendor NAPT init sequence (stock final values) ----------------
write_verify "$GLB_ADDR" 0x10000034
write_verify "$N70" 0x00110138
write_verify "$N64" 0x00000001
write_verify "$N54" 0x00000100
write_verify "$N130" 0x80400000
write_verify "$N138" 0x80408000
write_verify "$N134" 0x80448000
write_verify "$N13C" 0x80450000
write_verify "$N68" 0x00020303
command_poll "$N60" 0x000a0001 2000
command_poll "$N60" 0x00080001 2000
write_verify "$N50" 0x00f00b69

echo "APPLIED NAPT/flow init replay; rollback file $RB"
echo "now: snapshot counters, ping 500 from the host, snapshot again; if no effect run:"
echo "  LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh $0 --revert"
exit 0
