# TEST-LADDER - wifi-rx-linux-parity attended rungs (task-166)

**The Lead fires. This lane does not touch the router.** Every rung is attended,
recovery-armed (project rules), and stops on the first anomaly. The module
artifact must be built by the integration lane from the final association tree
with this lane's patch rebased on top (BUILD-NOTES.md).

## Preconditions (every rung)

1. tools/bench-link.sh PASS and the recovery door READY (project rules).
2. Fresh golden boot (page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0) -
   the Lead's standard readout.
3. Module loaded with `rx_enable=1` (plus any rung-specific parameters below);
   log the exact modinfo -p parameter set with the rung.
4. AP-first order is mandatory for RX: **AP start (5-message) -> `dp_setup` ->
   `dp_txq_sync` -> `dp_rx_arm`**. An AP start after `dp_setup` leaves the live
   status at 0x02 instead of 0x182 (earlier lane finding); use the AP-first
   order so the rung is comparable.
5. Sample before and after every action (crash watch): page/golden, mask
   (+8 / CA 0x400008 value), the exception-announce and vector-mask counters,
   and dmesg -c for the RX lines of this load. Stop immediately on any
   exception/page loss: power-cycle, no retry in the same boot.

## R0 - defaults are inert (fail-closed proof)

Parameters: `rx_enable=1 rx_clear_status=0 rx_refill_wm=75` and **all new
parameters at their defaults** (`rx_entry_wait=32`, `rx_ring_kick=0`,
`rx_ctl_enable=0`, `rx_legacy_bank=0`).

Steps: AP-first -> setup -> txq_sync -> arm -> `cat dp_rx_capture` -> one
`echo 1 > dp_rx_drain`.

PASS: arm line as before (status 0x182, mask 0xfffffcc1, norm/small rptr 0x20);
drain line shows the commit counters (expected 0) and stall class IDLE or
PREFETCH_ONLY; **no new register write** appears (no kick/ctl/legacy lines).
This proves the new code is inert unless opted in.

## R1 - read-only parity measurement (H-enable / H-filter data)

Parameters: R0 set (all experiments still off).

Steps: on the AP-first state, `cat dp_rx_parity > /tmp/dp_rx_parity.txt` and
copy it off the box, then on the host:

    opensource/docs/incoming/wifi-rx-linux-parity/tools/compare_stock.py \
        --probe /tmp/dp_rx_parity.txt \
        --dump opensource/dumps/reg_all.txt --all

Record: every `*` word (vendor non-zero, never written by our program) and
every `!` word (programmed word differing from stock). Focus:
* config block +0x08/+0x0c/+0x14/+0x18 (the four H-enable candidates);
* config block +0x5c/+0x6c (known vendor-live 4s - is our device also 4 even
  without `mac_tail_writes`? if yes, the writer is the device/firmware, not the
  host, which changes the interpretation of the existing experiment);
* the eight other machw blocks: any word where stock is non-zero and our boot
  differs is a new candidate.

PASS: capture succeeds and the diff table is recorded in the bench note. There
is no pass/fail on the values themselves - this rung is the measurement that
decides H-enable. Do **not** start writing candidate bits in the same boot;
R4 is per-boot, one candidate bit at a time.

## R2 - crash watch on the fixed drain path

Parameters: R0 set. Steps: AP-first -> setup -> txq_sync -> arm -> **20-50
bounded drains with a client scan/auth attempt between them** (a few hundred
total if time allows), sampling the crash-watch words after each group.

PASS: page golden, mask 0, staging 0, no exception-announce change, no dmesg
exception line, module alive; `cat dp_rx_capture` shows the commit/spurious
counters and the drain line shows no REFUSED.
FAIL: any exception/page loss -> stop, power-cycle, record the last actions;
that would mean a divergence fix did **not** cover the crash class (compare the
last drain line and the live status).

## R3 - second producer publish ("kick") experiment

Parameters: R0 + `rx_ring_kick=1`. Steps: AP-first -> setup -> txq_sync ->
arm -> `echo 1 > dp_rx_kick` -> `cat dp_rx_parity` / `dp_rx_capture` -> client
scan/auth on air -> `echo 1 > dp_rx_drain`.

PASS: kick read-back correct (log line shows norm +0x38 / small +0x3c values);
live bank recorded before/after.
OUTCOME A: live bank unchanged -> the kick is not the enable (clean negative).
OUTCOME B: free-ring rptr and/or completion wptr starts moving -> log it as a
positive; then run R5.
Stop on any exception.

## R4 - candidate RX-control word experiment (one bit per boot)

Parameters: R0 + `rx_ctl_enable=1 rx_ctl_off=<0x08|0x0c|0x14|0x18>
rx_ctl_set=<single bit> rx_ctl_clr=0`.

Steps: AP-first -> setup -> txq_sync -> arm -> `cat dp_rx_parity` (baseline)
-> `echo 1 > dp_rx_ctl` (one shot) -> client scan/auth on air -> re-read
`dp_rx_parity` / `dp_rx_capture` -> `echo 1 > dp_rx_drain`.

PASS/negative: the ctl log line records old/new/read-back; live bank unchanged
= clean negative for that word/bit. POSITIVE: completion wptr / free-ring rptr
moves on air (or a D2H g1 id13 event appears) -> that word is (part of) the
missing enable; stop the ladder and report.
**Rules:** exactly one word and one bit per boot; power-cycle between
candidates; no retry after a read-back mismatch or a REFUSED line; stop the
whole ladder on any exception/page loss. Candidate order (recommended, cheapest
first): 0x18, 0x14, 0x08, 0x0c - arbitrary (all [U]) but fixed so runs are
comparable.

## R5 - on-air data/mgmt proof

Parameters: whatever the best R2/R3/R4 state was (or R0 if R2 stayed clean).
Steps: AP-first -> setup -> txq_sync -> arm -> client auth/association attempt
(open or WPA2 per the association ladder) -> drain a few times -> capture
`dp_rx_capture` and dmesg.

PASS (any one): live free-ring rptr advances past 0x20; completion wptr becomes
non-zero; the management D2H CRX g1 id13 event appears; a completion entry
validates into a capture. Record the stall class: PROGRESS is the healthy
class, PREFETCH_ONLY means the MAC consumes free entries but writes no
completion, IDLE means no activity at all (re-check RF/client).
FAIL: none of the above after N drains with a known-transmitting client ->
report as H-filter/H-enable still open with the R1 diff table.

## R6 - read-side control (only if a read effect is suspected)

Parameters: R0 + `rx_legacy_bank=1`. Steps: repeat R0's poll and observe
whether the old +0x4c/+0x50/+0x54 reads correlate with a crash.

PASS: no difference vs R2 -> the legacy reads are not the crash trigger
(unlikely, but this closes T4); any correlation -> keep the default 0 forever.

## Crash-watch data to record with every rung

| item | where |
| --- | --- |
| load parameter set | modinfo -p + the echo/cat sequence |
| page/golden/mask/staging | the Lead's standard boot readout |
| exception-announce / vector-mask / d2h word counts | the Lead's standard event readout |
| arm line | "DP rx_arm: ..." |
| each drain line | "DP rx_drain: ... stall=... commit{...}" |
| parity probe | cat dp_rx_parity (saved to a file) |
| last 100 dmesg lines | before power-cycle |
