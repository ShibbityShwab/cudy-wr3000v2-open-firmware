#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# c2-park.sh — candidate C2 (L1SS/pinmux parity) + park-marker readout.
# ORIGINAL tool by the wifi-boot-kick lane; no vendor code/bytes.
# Evidence: ../REPORT.md §9 (C1 negative, park model) and §10 (C2).
#
# Attended session only: recovery armed, UART capture READ-ONLY, no rmmod after
# release (power-cycle to end). Requires the B3 module plus one addition:
#   b3_l1ss_prep  (WO) -> CA 0x40000554 |= 0x220; CA 0x400005BC = 6
# and a way to read physical addresses (PARK_READ, default "devmem2 <addr> w").
#
# Env:
#   LOG        absolute host log path (required)
#   B3_SYSFS   B3 sysfs dir (autodetect if empty)
#   B3_RUN     host prefix to run device shell cmds, e.g. tools/rsh.sh
#   PARK_READ  command template to read a 32-bit phys addr; {a} is replaced.
#              default: "devmem2 {a} w"   (e.g. PARK_READ="physdump {a}")
#   C1_HOLD_S  gate-service window (default 60)
set -eu
: "${LOG:?set LOG to an absolute host log path}"
C1_HOLD_S="${C1_HOLD_S:-60}"
B3_SYSFS="${B3_SYSFS:-}"
B3_RUN="${B3_RUN:-tools/rsh.sh}"
PARK_READ="${PARK_READ:-devmem2 {a} w}"

log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" | tee -a "$LOG"; }
dev() { if [ -n "$B3_RUN" ]; then "$B3_RUN" "$*"; else sh -c "$*"; fi; }
attr() { dev "echo 1 > '${B3_SYSFS}/$1'"; }

# BAR0/phys tables from REPORT.md §9.2 (region-3: BAR0=0x3B8000+(CA-0x40000000),
# phys=0x40000000+BAR0; CPU R: BAR0=0x6B8000+R)
read32() {  # $1 = phys address
    cmd=$(printf '%s' "$PARK_READ" | sed "s/{a}/$1/")
    dev "$cmd" 2>/dev/null | tr -d '\r' | tail -1
}

park_snapshot() {  # $1 = label
    log "== park: $1 =="
    log "mask   CPU0x40008      phys 0x406F8008 = $(read32 0x406F8008)"
    log "flag   CPU0x17DF78     phys 0x40835F78 = $(read32 0x40835F78)"
    log "doorbell CA0x40101434  phys 0x404B9434 = $(read32 0x404B9434)"
    log "0x40000558             phys 0x403B8558 = $(read32 0x403B8558)"
    log "trimA  CA0x4003941C    phys 0x403F141C = $(read32 0x403F141C)"
    log "trimA  CA0x40039424    phys 0x403F1424 = $(read32 0x403F1424)"
    log "trimA  CA0x40039428    phys 0x403F1428 = $(read32 0x403F1428)"
    log "out1   CA0x40039014    phys 0x403F1014 = $(read32 0x403F1014)"
    log "g      CPU0x172130     phys 0x4082A130 = $(read32 0x4082A130)  <- 0x82B130 is a known-bad offset"
    log "state  CPU0x118B60     phys 0x407D0B60 = $(read32 0x407D0B60)"
}

[ -n "$B3_SYSFS" ] || B3_SYSFS=$(dev "ls -d /sys/devices/platform/*/*/hi5622* /sys/class/*/hi5622* 2>/dev/null | head -1") \
    || { echo "FATAL: cannot autodetect B3 sysfs; set B3_SYSFS" >&2; exit 1; }
log "C2+park session start sysfs=$B3_SYSFS hold=${C1_HOLD_S}s read='$PARK_READ'"

attr prepare || { log "prepare failed"; exit 1; }
park_snapshot "post-prepare (pre-C2)"
# --- C2: L1SS/pinmux parity (vendor plat.ko 0x1b468-0x1b490 / 0x1b4a4-0x1b4c0) ---
attr b3_l1ss_prep || { log "C2 write failed"; exit 1; }
park_snapshot "post-C2"
attr upload || exit 1
attr verify || { log "verify failed -> STOP, do not release"; exit 1; }
park_snapshot "pre-release"
attr release || exit 1
ipass=1
while [ "$ipass" -le "$C1_HOLD_S" ]; do        # 1 s per pass, ~1 ms gate cadence
    i=0
    while [ "$i" -lt 1000 ]; do
        attr announce >/dev/null 2>&1 || { log "announce write failed"; break; }
        i=$((i + 1))
        if [ "$i" -lt 1000 ] && command -v usleep >/dev/null 2>&1; then usleep 1000 || true; fi
    done
    park_snapshot "t=${ipass}s"
    ipass=$((ipass + 1))
done
dev "dmesg | tail -40" | tee -a "$LOG" || true
log "DECISION (REPORT §10.3):"
log "  flag 0xF + doorbell 1 + mask/g moved -> success or downstream (P3): next lane = dispatcher/registry"
log "  flag 0x7 + doorbell 0 with C2       -> P1 confirmed: next candidate C3 tail-IO gate writes"
log "  no rmmod after release; power-cycle to end the session"
