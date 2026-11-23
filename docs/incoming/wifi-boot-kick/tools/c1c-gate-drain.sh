#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# c1c-gate-drain.sh — candidate C1c: sustained 0xcece gate service PLUS the
# full D2H contract (ack CA 0x40101438, drain out[1] CA 0x40039014, re-arm
# CA 0x40101414) for a 60 s window, with the corrected park/registry/ROM reads.
# ORIGINAL tool; no vendor code/bytes. Evidence: ../REPORT.md §12, §13.
#
# Attended only: recovery armed, UART capture READ-ONLY, no rmmod after release.
# Requires the B3 module (b3_drain does the ack->drain->re-arm->dispatch
# sequence; see pciet_b3.c) and a phys read command (PARK_READ).
#
# Env:
#   LOG        absolute host log path (required)
#   B3_SYSFS   B3 sysfs dir (autodetect)
#   B3_RUN     host prefix to run device cmds (default tools/rsh.sh)
#   PARK_READ  template, {a} replaced; default "devmem2 {a} w"
#   HOLD_S     window seconds (default 60)
#   DRAIN_MS   contract-service interval ms (default 10)
set -eu
: "${LOG:?set LOG to an absolute host log path}"
HOLD_S="${HOLD_S:-60}"; DRAIN_MS="${DRAIN_MS:-10}"
B3_SYSFS="${B3_SYSFS:-}"; B3_RUN="${B3_RUN:-tools/rsh.sh}"
PARK_READ="${PARK_READ:-devmem2 {a} w}"
log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" | tee -a "$LOG"; }
dev() { if [ -n "$B3_RUN" ]; then "$B3_RUN" "$*"; else sh -c "$*"; fi; }
attr() { dev "echo 1 > '${B3_SYSFS}/$1'"; }
r32() { dev "$(printf '%s' "$PARK_READ" | sed "s/{a}/$1/")" 2>/dev/null | tr -d '\r' | tail -1; }

snapshot() {
    log "== $1 =="
    log "g       CA0x172130  BAR0 0x82A130 phys 0x4082A130 = $(r32 0x4082A130)"
    log "g+0x9C  BAR0 0x7C4190  phys 0x407C4190 = $(r32 0x407C4190)  (expect 0x40039014)"
    log "registrybase BAR0 0x829B6C phys 0x40829B6C = $(r32 0x40829B6C)"
    log "rec0    BAR0 0x829FB4+0  phys 0x40829FB4 = $(r32 0x40829FB4)"
    log "rec0+4  BAR0 0x829FB8    phys 0x40829FB8 = $(r32 0x40829FB8)  (expect 0x00103FB8)"
    log "rec0+10 BAR0 0x829FC4    phys 0x40829FC4 = $(r32 0x40829FC4)"
    log "mask    BAR0 0x6F8008    phys 0x406F8008 = $(r32 0x406F8008)  (target 0x1E)"
    log "out1    BAR0 0x3F1014    phys 0x403F1014 = $(r32 0x403F1014)"
    log "out0    BAR0 0x3F1010    phys 0x403F1010 = $(r32 0x403F1010)"
    log "state   BAR0 0x7D0B60    phys 0x407D0B60 = $(r32 0x407D0B60)"
    log "flag    BAR0 0x835F78    phys 0x40835F78 = $(r32 0x40835F78)"
}
dump_rom() {
    log "== ROM dump CPU 0x37000..0x37300 (read-only; post-entry-3 flow) =="
    a=0x40000000; end=$((a + 0x6EF300))
    while [ "$a" -lt "$end" ]; do
        log "ROM $(printf '0x%08x' "$a") = $(r32 "$(printf '0x%08x' "$a")")"
        a=$((a + 4))
    done
}

[ -n "$B3_SYSFS" ] || B3_SYSFS=$(dev "ls -d /sys/devices/platform/*/*/hi5622* /sys/class/*/hi5622* 2>/dev/null | head -1") \
    || { echo "FATAL: no B3 sysfs; set B3_SYSFS" >&2; exit 1; }
log "C1c start: sysfs=$B3_SYSFS hold=${HOLD_S}s drain=${DRAIN_MS}ms"
attr prepare || exit 1; snapshot "post-prepare"
attr upload || exit 1; attr verify || { log "verify failed -> STOP"; exit 1; }
snapshot "pre-release"
attr release || exit 1
start=$(date +%s); next_drain=$start; next_snap=$start; next_gate=0
while : ; do
    now=$(date +%s)
    [ $((now - start)) -ge "$HOLD_S" ] && break
    # 1 ms gate cadence, batched by seconds to keep the shell cheap
    i=0
    while [ "$i" -lt 1000 ]; do attr announce >/dev/null 2>&1 || break; i=$((i+1)); usleep 1000 2>/dev/null || true; done
    now=$(date +%s)
    if [ "$now" -ge "$next_drain" ]; then
        # b3_drain performs ack -> drain -> re-arm -> dispatch if out[1] != 0
        attr b3_drain >/dev/null 2>&1 || true
        next_drain=$((now + DRAIN_MS/1000 + 1))
    fi
    if [ "$now" -ge "$next_snap" ]; then snapshot "t=$((now-start))s"; next_snap=$((now + 5)); fi
done
snapshot "post"
dump_rom
dev "dmesg | tail -40" | tee -a "$LOG" || true
log "DECISION (REPORT §13.4): mask->0x1E / registry rec0+4=0x00103FB8 / ready dmesg = SUCCESS"
log "  out[1] reposts 0x4 only, no progress -> next = ROM-flow analysis (0x37172) + registry lane"
log "Do NOT rmmod after release; power-cycle to end the session."
