# FIRE-RECIPE - H2D stall discriminator (the out[0]=0x8 pattern)

**Lane:** fw-h2d-stall (task-181). **The Lead fires**; this lane made no bench contact. **One
hypothesis per boot, one load per boot, never retry in place.** If a gate fails, STOP, capture,
power-cycle.

Inherited non-negotiables (BENCH-FACTS / LESSONS / RUNBOOK, unchanged): recovery door armed (RESET
through the counter + TFTP ready) before any fire; UART capture READ-ONLY to an absolute path; bench
link PASS; no human at the bench = no fire; never read RC misc `0x10161000`; never write CA
`0x400392f0`; no UART transmit; one module load per boot; a stall or exception means
**capture the read-only frame, power-cycle, do not send a second message on that boot** (the lane is
closed and the page may be golden:0).

Why this recipe exists: the 2026-11-26/27 fires showed `out[0]=0x00000008` after 44-51 polls on
the 5GHz `user_add` and on the RC short alg_cfg frame, then a firmware exception. The decode
(`REPORT.md` §1) says out[0]=0x8 is the **unconsumed SR-announce bit**, and the dispatcher
clears it **before** calling handlers - so the stalled frame was never dispatched and the cause is
the **previous** dispatch (or the PD4 poller being dead). The recipe below turns that into one
read-only observation: **the firmware exception record names the fault PC.**

---

## 0. Read-only observable pack (collect on every arm, before any power-cycle)

| # | what | where / how | decode |
| --- | --- | --- | --- |
| O1 | page / state / mask | `cat $D/boot $D/status` (existing gate attrs) | golden = page `0x86807FEF` (word0), state 1, mask 0 |
| O2 | H2D counters | `cat $D/h2d_status` + `dmesg | grep -E 'H2D|announce|lane_closed'` | ok/timeout/refused, last out[0], poll count |
| O3 | **exception record** | CPU `0x4000C..0x4004F` = **BAR0 `0x6F800C`**, 17 u32: r0-r12, sp, lr, PC, SPSR | PC = the faulting instruction; SPSR mode 0x17 = ABT. **This is the decisive read.** |
| O4 | mask word | CPU `0x40008` = BAR0 `0x6F8008` (or the boot attr's mask field) | bit 0x10 = DABT, bit 0x02 = UND; a persistent non-zero mask means the exception path keeps re-entering |
| O5 | PD4 poller integrity | CPU `0x108240` = **BAR0 `0x7C0240`**, 16 B | must still hold the poll stub (`b.w`-reachable code + the literal pool); a wipe means a runtime writer, not our fault model |
| O6 | firmware identity | sha256 of `/lib/firmware/hi_wifi/FIRMWARE.PD4.bin` | `cfe24bfb…037b1` (PD4); anything else invalidates the frame decode |

BAR0 = firmware CPU + `0x6b8000` (window 5). Use the same **read-only** BAR0 read path the
calibration ladder's P0 used (`../wifi-proto-seq/recipes/cal-alg-ladder.json` P0 names
`exception staging = BAR0 0x6F800C`). Read-only only; these are reads, never writes, and never
through a write-capable `/dev/mem` helper.

**PC decode table** (firmware file = PC - 0x40000; from the committed record and this lane):

| PC (CPU) | what it means | consequence |
| --- | --- | --- |
| `0x81040` (or `0x8103C`) | the PD4-proven NULL `ctx+0x190` worker fault: `ldr r3,[ctx+0x190]` = 0, `strb r1,[r3,#0x4c]` | **H1/H2 confirmed**: the preceding context/TXQ path faulted; the stalled frame was never processed |
| `0x96068` / its worker | the g0 id2 alg-event handler (descriptor scan + per-key worker) | **H3**: the alg blob/Cfg-id path faulted; read the blob/detail |
| `0x9832C` / its callees | the g0 id7 user_add handler (context/user lookup + object writes) | **H2/H3**: the VAP/user context was missing/zeroed |
| `0xC18AC`, `0x294`, `0x108240` region | the dispatcher / thunk / PD4 poller itself | a service-path fault (unexpected; record and treat as a new finding) |
| staging all-zero, mask 0 | no exception: the firmware is alive but the announce was not consumed within the window | **H4** (busy/slow) or the poller is wedged -> R-D5 |

---

## R-D0 - preconditions (read-only, one boot)

    uname -r                                          # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko  # the lane artifact (96c54159... for rate_ctl)
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko -F parm | grep -E 'rate_ctl|h2d_send_budget'
    sha256sum /lib/firmware/hi_wifi/FIRMWARE.PD4.bin  # cfe24bfb...037b1
    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                             # page 0x86807FEF / state 1 / mask 0
    # O3/O4/O5 read-only pack (expect: staging all zero, mask 0, poll stub present)

STOP if page/state/mask are not golden, the module or firmware sha differ, or the bench state is the
post-fire exception state (the 2026-11-27 07:30 handoff left the chip in the exception state: a
**power cycle first**). Recovery armed before any arm below.

## R-D1 - cause vs victim: proven C2, then ONE candidate frame

Purpose: does the 39-B `rate_mode auto` frame stall on its own, or only after the C2 sequence?

    insmod .../hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin rate_ctl=1 rate_ctl_fb=4 \
           rate_ctl_arbitrary=0 h2d_send_budget=8
    echo 1 > $D/prepare ; echo 1 > $D/upload ; echo 1 > $D/verify ; echo 1 > $D/seed
    echo 1 > $D/hcc_arm ; echo 1 > $D/hcc_loop ; echo 1 > $D/release      # 0x5A5A ONLY
    cat $D/h2d_status                                 # phase ACTIVE
    # --- the CALIBRATION-PROVEN C2 (do not vary this arm) ---
    echo 1 > $D/h2d_ctx_create                        # type 1, the proven create
    # read-only readback: ctx4+0x190 != 0 (BAR0 0x7D45E8 per cal-alg-ladder P1)
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                                 # ok=2 timeout=0; O1 mask 0
    # --- the ONE candidate frame ---
    echo auto > $D/rc_rate_mode                       # 39-B g0 id2 key 0x0101

Interpretation:

| observation | verdict |
| --- | --- |
| consumed in 0 polls + one g1 id3 syn 0x0101 rsp + mask 0 | the rate frame is **clean**; the 07:00 stall was the C2/context path (H1/H2) -> run R-D2 to confirm |
| announce `out[0]=0x8` stalled -> **STOP** (do not retry) -> O1..O5 | read the exception PC: 0x81040 = context fault; g0-id2 handler/worker = H3; staging 0/mask 0 = H4 -> R-D5 |
| consumed but no rsp / mask != 0 / page lost | the frame's worker faulted after the ack -> O3 names the PC (H3) |

## R-D2 - reproduce the 07:00 signature and take the frame

Purpose: confirm whether the **ctx_create type 3** written in the old rate-ctl recipe (vs the
proven type 1) is the fault trigger. One boot, one difference.

    # same boot ladder as R-D1, then:
    echo 3 > $D/h2d_ctx_create        # the OLD recipe's value (its comment says 1)
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status ; cat $D/boot   # if txq_sync72 announce stalls or mask != 0 -> capture O3..O5 NOW
    echo auto > $D/rc_rate_mode       # only if mask is still 0 and the phase is ACTIVE

*Expected if the hypothesis holds:* txq_sync72 (or the rate frame) stalls and the exception PC is
`0x8103C/0x81040` with mask `0x10` -> the type-3 create never initialised the field-B
context; the rate frame is exonerated.
*Refutation:* R-D2 is clean and identical to R-D1 -> the old stall was a one-off/race; treat the
rate frame as still-unproven and go to R-D3.

## R-D3 - content vs environment: clean calibration control, then the short frame

One boot, two messages (budget permits 8): the **proven** 298-B calibration frame (g0 id2 key
0x0101, cfg 3507, field B 4, 270-byte body) after the proven C2 -> expect consumed + a g1 id3
syn-0x0101 answer (`../wifi-proto-seq/recipes/cal-alg-ladder.json` P3). Then the 39-B
`rate_mode auto` frame.

| calibration | rate frame | verdict |
| --- | --- | --- |
| clean | consumed + rsp | both paths clean; close the stall as H1/H2 (context/C2) |
| clean | stalled | **H3**: the short/11-B alg blob really is content-specific -> capture O3 and the blob/len; do not guess a payload fix in place |
| stalled | - | the environment (C2/ladder) is still wrong -> H1/H2, back to R-D1 |

## R-D4 - the 5GHz order control (already natural experiment, make it explicit)

One boot: full vendor AP order with the **VAP add first** and the deferred user_add posted only
after a non-zero VAP-object readback (the 00:30 fire's proven order) -> the user_add must be
consumed in 0 polls. Compare with the 23:00 failure (user_add at interface creation, before the
VAP): the difference is the precondition (H2), not the wire (`../wifi-5ghz-vap-msg/`; the
vendor never posts g0 id7/id8 at interface creation).

## R-D5 - dead vs busy (only if R-D1/R-D2 stall with mask 0 and an empty exception staging)

    # after the stalled announce, do NOT post another config frame.
    # extend the bounded window (module param if exposed; else the rate/cal module's poll window)
    # and poll out[0] (CA 0x40039010) read-only for ~2 s (10 ms samples):
    #   -> clears late        = H4 (busy/slow), page golden, no exception -> do not power-cycle
    #   -> never clears + mask 0 + empty staging = poller wedged/dead (H1 variant; capture O5)
    # then ONE payload-free g0 id0 stub frame is allowed on the same boot ONLY if the injector
    # arm is available and the budget/gates permit (handler 0x41695 logs and returns, no payload,
    # no context dereference). Consumed -> the poller is alive (H4); not consumed -> poller dead.

---

## Decision summary (what the Lead needs after the fire)

1. **Exception PC + mask** from O3/O4 - names H1/H2 (0x81040), H3 (message handler/worker), or no
   fault (H4).
2. **Was the stalled frame ever dispatched?** - it cannot have been (dispatcher clears out[0]
   before dispatch); the fire log's out[0] value is only a consumption timeout.
3. **R-D1/R-D2 result** - whether the 07:00 rate-frame stall reproduces under the proven C2; this
   alone decides whether the rate frame stays in the backlog (it probably does).
4. **O5** - the PD4 poller is intact (otherwise the fault model changes).

## What NOT to do

* Do not re-send the stalled frame (lane closed; page may be golden:0).
* Do not write CA `0x400392f0` (firmware-owned ack), do not read CA `0x10161000`, no UART
  transmit, no raw write helper.
* Do not change the proven C2 (ctx_create type 1 + ctx190 readback) in a fire that is about
  something else.
* Do not "fix" the rate frame payload in place; R-D3 decides whether it is content-specific.
