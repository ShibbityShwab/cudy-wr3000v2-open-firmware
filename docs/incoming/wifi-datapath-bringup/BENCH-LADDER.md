# BENCH-LADDER - data-plane host bring-up (ladder D), attended only

Companion to REPORT.md. Every stage is **attended**; the standard preconditions
are unchanged: recovery door armed/READY, bench link PASS
(tools/bench-link.sh), UART capture read-only to an absolute path, image/module
hashes recorded, no vendor Wi-Fi module loaded, one clean driver, never rmmod
after release. Hard rules: never write CA 0x400392f0 (out[5]), never read the RC
misc window 0x10161000, never post mailbox id 5, never write 0xCECE during
boot, one idea per boot, no retry after a failed step.

Image baseline: the current cfg80211 bench image (kernel slice 13e81214...,
slot 1794a531...) with the PD4-patched firmware sha256 cfe24bfb... Module: this
lane's `src/hi5622_wifi.ko`, 462,952 B sha256
8fc693ad4c3c9338baa1089ecb2acd420be78eb059760bfc0fa356ade205cec9, built by
tools/build.sh against the project 6.18.60 tree (vermagic pinned by the build
log). Insert order: cfg80211.ko first (if using the cfg80211 build), then
hi5622_wifi.ko with the bench fw_path/wifi_block_path parameters.

## D0. Boot + control plane (read-only, one session)

| # | read | location | expect |
| ---: | --- | --- | --- |
| 1 | golden page | CA 0x1BF000 (BAR0 0x877000) | 0x86807FEF / 7 / 2 / 0xABCD |
| 2 | boot state | CA 0x118B60 (0x7D0B60) | 1 |
| 3 | exception mask | CA 0x400008 (0x6F8008) | 0 |
| 4 | exception staging | CA 0x40000C (0x6F800C) | 0 |
| 5 | ready pair | rd_catalog | g4 id1 + g4 id2 (payload 0x01060850) |
| 6 | H2D working lap | h2d_status | ctx_created=1, phase=ACTIVE (ctx-create already sent) |

Stop if any row differs. This lane's sequence `sequences/ladder-d-datapath.json`
wraps D1-D5 after the boot uses the H2D group; it is validated by the bench
tooling validator but has NOT been fired.

**Calibration ordering:** the vendor's six g0 id2 key-0x0101 set_* calibration
messages (proto-seq ladder C1, budget 8) are a *separate* supervised run before
this ladder. Do not combine them with D2/D3 in one module load: the per-load
H2D budget is the stop condition, and a calibration failure must stop the boot
before any MAC register write. "After calibration" here means the C1 lap is
recorded on a previous boot, not that D2 depends on its frames.

## D1. READ-ONLY MAC reachability probe - ONE access (separate session from D2)

Purpose: settle the region-3 window question recorded in wifi-datapath REPORT
open item 4 (an earlier lane bus-errored reading BAR0 0x402a004 while the
declared window covers CA 0x4004xxxx).

    echo 1 > /sys/.../dp_probe      # or: cat /sys/.../dp_probe
    read the attr output

| observation | meaning | next |
| --- | --- | --- |
| attr prints "= 0x00000100" | the window maps MAC band 0 (vendor boot value) | proceed to D2 in a later boot |
| attr prints a plausible other value (e.g. 0x0) | window answered; firmware has not programmed MAC yet | record; proceed to D2 |
| bus error / hang / reboot | window does not map the MAC block | STOP; no MAC write may be attempted; report exposure; do not add an iATU viewport |
| any page/state/mask change | something else happened | STOP, power-cycle, record |

D1 writes nothing. It is a single MMIO read; do not repeat it in the same boot
if it faults.

## D2. Data-plane setup - ONE action (separate boot from D1)

Preconditions: D1 PASS recorded, golden boot, ready pair, ctx-create sent this
load, dp_probe_ok, h2d_send_budget at least 2 (ctx_create + txq addr).

    echo 1 > /sys/.../dp_setup
    cat /sys/.../dp_status
    cat /sys/.../dp_rings

Expected: dp_status says `setup_done=1 failed=0 mac_regs=14 table_entries=8` and
lists ring device VAs; dp_rings shows `+0x38 = 0x00008000`,
`+0x3c = 0x00008000`, `+0x48 = 0xfffffcc1`, the per-TID table entries and the
device-side counters; exception mask and staging still zero; page/state golden.

Stop rules (no retry; reload for another attempt): any register read-back
mismatch (the driver already stopped), exception mask != 0, staging != 0, page
change, reboot/hang, DMA allocation failure. Do not write MAC band 1, do not
touch ETE/mailbox registers to "fix" a MAC read-back.

## D3. TXQ-table address sync - ONE frame (same boot as D2 or a later one)

Precondition: dp_setup succeeded in this module load (dp_status setup_done=1),
h2d budget not exhausted, previous announce consumed (h2d_status out[0]=0).

    echo 1 > /sys/.../dp_txq_sync      # alias: h2d_txq_addr_sync (bench tooling name)
    cat /sys/.../h2d_status

Expected: `txq_addr_sent=1`, `ok=2 timeout=0 frames_sent=2` (ctx-create + this
frame), out[0] consumed (attr shows 0), no new exception. Post pack (read-only):

| read | location | expect |
| --- | --- | --- |
| mask, staging | 0x6F8008, 0x6F800C | 0 |
| state, page | 0x7D0B60, 0x877000 | 1, 0x86807FEF |
| out[0], out[2] | 0x3F1010, 0x3F12D4 | 0, 0 |
| rd_catalog delta | catalogue-after snapshot | capture only (the 0x320 worker has no known D2H answer) |

Stop: announce not consumed within the driver's bounded window, exception
mask/staging becomes nonzero, or any signalling fault -> stop the boot, record,
power-cycle; do not resend.

## D4. Later stages (need user/VAP add - not shipped by this lane)

The rings are handed over empty for user index 0. Real traffic needs a user
(g0 id7/8) and a VAP/AP context, which is the wifi-vap-scan lane's work; then:
TX: one ARP-sized frame -> 8-byte MSDU entry on the per-TID ring -> device
consumption observed via core-g0 id0/1 events or tx_ba_info; RX: completion ring
wptr moves, read back the 0x28-byte descriptor + 802.11 MPDU from a completed
buffer. Do not improvise those payload structs.

## Decision matrix (all stages)

| signal | stop? | action |
| --- | --- | --- |
| handshake page no longer golden | YES | stop all writes; power-cycle; capture |
| exception mask CA 0x400008 != 0 | YES | stop; record staging frame; power-cycle |
| out[0]/out[2] stuck, announce not consumed | YES | lane closed for the boot; record |
| MAC register read/write bus error or reboot | YES | no retry in the same boot; report exposure |
| read-back != written value (register or MSDU table) | YES | the write did not land; lane failed by design |
| new D2H frames appear after D3 | no | capture/decode, then decide next session |
| dp_setup says failed=1 | YES | do not retry; reload only in a supervised session |

Recovery door armed for any session that reaches D2 (register writes); D1 is
read-only but still counts as a MAC access experiment (one access, one boot).
