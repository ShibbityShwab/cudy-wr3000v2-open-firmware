#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# c1-gate-hold.sh — candidate C1: serve the Hi5622V100 firmware 0xcece gate
# after release, sustained, until the vector mask reaches 0x1E / g becomes
# non-zero / a ready message arrives.
#
# ORIGINAL tool written by the wifi-boot-kick lane. No vendor code/bytes.
# Evidence: ../REPORT.md §0, §2.4, §5. See also wifi-fw-ready-gate REPORT §14.
#
# RUN THIS ONLY WITH A HUMAN AT THE BENCH, RECOVERY DOOR ARMED, UART CAPTURE
# READ-ONLY. Do NOT rmmod after release: power-cycle to end the session.
#
# Device paths (B3 instrument):
#   B3_SYSFS   sysfs directory of the B3 module attrs (prepare/upload/verify/
#              release/announce/b3_p4_state/... )   default: autodetect
#   B3_RUN     host command prefix used to run a device shell command, e.g.
#              "tools/rsh.sh" (repo default). Leave empty to run commands
#              locally on the device (when invoked on the router itself).
#   LOG        absolute host log path (required)
#
# Usage:
#   LOG=/abs/path/c1-$(date -u +%%Y%%m%%dT%%H%%M%%SZ).log ./c1-gate-hold.sh
set -eu

: "${LOG:?set LOG to an absolute host log path}"
C1_HOLD_S="${C1_HOLD_S:-45}"      # total hold window (s); firmware boots ~1.8-2 s
C1_POLL_MS="${C1_POLL_MS:-1}"     # gate write cadence (ms)
C1_READ_EVERY="${C1_READ_EVERY:-500}"  # iterations between observable reads
B3_SYSFS="${B3_SYSFS:-}"
B3_RUN="${B3_RUN:-tools/rsh.sh}"
PREP_ONLY="${PREP_ONLY:-0}"       # 1 = stop after verify (dry run, no release)

die() { echo "FATAL: $*" >&2; exit 1; }
log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" | tee -a "$LOG"; }

# run a device shell command (local if B3_RUN empty)
dev() {
    if [ -n "$B3_RUN" ]; then "$B3_RUN" "$*"; else sh -c "$*"; fi
}

# one-shot: write 1 to a sysfs attribute of the B3 device
attr() {
    dev "echo 1 > '${B3_SYSFS}/$1'"
}

# read a BAR0 offset through the B3 physdump convention (region-5 window:
# phys = 0x40000000 + (0x6b8000 + CPU/W_off)); the B3 module's b3_p4_state
# already prints the canonical set, so we use it as the primary readout.
snapshot() {
    label="$1"
    log "== snapshot: $label =="
    dev "cat '${B3_SYSFS}/b3_p4_state' 2>/dev/null || true" | tee -a "$LOG"
    dev "cat '${B3_SYSFS}/b3_p3_hcc'    2>/dev/null || true" | tee -a "$LOG"
    dev "cat '${B3_SYSFS}/status'       2>/dev/null || true" | tee -a "$LOG"
}

if [ -z "$B3_SYSFS" ]; then
    B3_SYSFS=$(dev "ls -d /sys/devices/platform/*/*/hi5622* /sys/class/*/hi5622* 2>/dev/null | head -1") \
        || die "cannot autodetect B3 sysfs dir; set B3_SYSFS"
fi
[ -d "$B3_SYSFS" ] || [ -n "$B3_RUN" ] || die "B3 sysfs $B3_SYSFS not found"
log "C1 gate-hold start: sysfs=$B3_SYSFS hold=${C1_HOLD_S}s poll=${C1_POLL_MS}ms"
log "reminder: attended session, recovery armed, UART read-only, no rmmod after release"

# ---- preconditions -------------------------------------------------------
snapshot "pre"
attr prepare || die "prepare failed"
snapshot "post-prepare"
log "gate: require 6/6 iatu readback=OK in the log above"
attr upload || die "upload failed"
attr verify || die "verify failed"
log "gate: require diffs=0 in the log above; anything else -> STOP"
if [ "$PREP_ONLY" = "1" ]; then log "PREP_ONLY=1 -> stop before release"; exit 0; fi

# ---- release + sustained gate service ------------------------------------
attr release || die "release failed"
log "released: CA 0x40000108 <- 0x5a5a; now serving CA 0x4000010C <- 0xcece"

# loop: 1 write per iteration, observable snapshot every C1_READ_EVERY iters
iters=$(( C1_HOLD_S * 1000 / C1_POLL_MS ))
i=0
while [ "$i" -lt "$iters" ]; do
    attr announce >/dev/null 2>&1 || { log "announce write $i failed"; break; }
    i=$((i + 1))
    if [ $((i % C1_READ_EVERY)) -eq 0 ]; then
        out=$(dev "cat '${B3_SYSFS}/b3_p1_events' 2>/dev/null || true")
        log "i=$i/$iters events: $out"
        case "$out" in
            *g4_id1_seen=1*|*g4_id2_seen=1*)
                log "SUCCESS: HCC group-4 ready id seen -> stopping loop early"
                break ;;
        esac
        # optional: check mask change if module exposes it
        st=$(dev "cat '${B3_SYSFS}/b3_p4_state' 2>/dev/null || true")
        case "$st" in *mask*=0x0000001e*|*mask*=0x1e*) log "SUCCESS: vector mask 0x1E"; break ;; esac
    fi
    # portable sub-ms-ish sleep
    if [ "$C1_POLL_MS" -ge 1 ]; then
        usleep $((C1_POLL_MS * 1000)) 2>/dev/null || sleep 0.001 2>/dev/null || true
    fi
done
log "gate-hold loop finished after $i writes"

# ---- post snapshot / cleanup ---------------------------------------------
snapshot "post"
log "INTERPRETATION:"
log "  mask 0x1E / g != 0 / 'Device plat ready'  -> C1 SUCCESS: snapshot + power-cycle"
log "  mask still 0x08, gate reads 0xcECE, 60 s   -> C1 negative: power-cycle, next=C2"
log "  mask moved but g==0                        -> record exit point; power-cycle"
log "Do NOT rmmod after release. Power-cycle the board to end the session."
