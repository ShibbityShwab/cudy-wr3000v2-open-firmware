# PHASE 49 - THE FORWARD LANE: the enable half is settled, the delivery hop is the last link (2026-10-04)

Consolidation of the wifi-forward plan's five device lanes (tasks 9-13). This file adds no new measurement;
every claim below carries the lane report and the evidence path that asserts it. Each lane's full report sits
beside this one in `opensource/docs/phase49/`.

## The lane reports

| lane | plan task | report | evidence | headline |
| --- | --- | --- | --- | --- |
| read boot | 9 | `read-boot.md` | `build/register-dumps/exp/20261004-163526/` | `fn_array[0x4c]` = `0x00040295` -> `register(0x4c)` ran |
| probe boot | 10 | `probe-boot.md` | `build/register-dumps/exp/20261004-164113/` | modified blob loaded, `9/9`, readback `diffs=0`, no panic/pstore |
| scratch boot | 11 | `scratch-boot.md` | `build/register-dumps/exp/20261004-165139/` | BRANCH-1: `enable(0x4c)` EXECUTED; the enable half is settled |
| vendor-loader | 12 | `vendor-loader.md` | `build/register-dumps/exp/20261004-165759-vendorloader/` | REJECTED: loader does NOT validate; chip fails at runtime |
| viewport | 13 | `viewport.md` | `build/register-dumps/exp/20261004-171523/` | expected-negative: GIC CA `0x40160000` stays host-invisible |

## What the forward lane measured (one line per chain link)

Follow-up lane: `gic-view.md` (post-plan, 2026-10-04) flips the sensor on task 13's host-invisible GIC - the
patched firmware reads the GIC itself and deposits the words in host-readable cells. Evidence
`build/register-dumps/exp/20261004-174357/`; BRANCH-G: the device CAN read the GIC (C0 = `0x00000001`, a
pre-write ISENABLER word 2 read at CA `0x40161108`), and nothing was pending or active at either sampled
instant (C1/C3/C4 = 0, C2/C5 = `0x3FF`). That lane's own next branch, the post-SEND sampling iteration it
dispatched, then RAN: the addendum in `gic-view.md` ("the post-SEND iteration LANDED", variant `gicsend`)
samples the GIC at the firmware's own `out[1]` post. **BRANCH-S:** at that post the mechanism is alive (id
`0x45` pending in the same register) while source `0x4c`'s line does not show pending. The send-site cells
sit above an upper-address aliasing boundary, so they are quoted from the ACP alias only, and the
disagreement mechanism is unexplained and accepted as a device fact. Evidence
`build/register-dumps/exp/20261004-180736/` (round 2, sentinel-proven; round 1
`build/register-dumps/exp/20261004-180114/` kept with its `aliasing-note.txt`). The lanes below are unchanged
by either. A second addendum to `gic-view.md` ("the conditional-enable test", variant `gicpost`) then closed
the remaining pre-write gap: the enable write is the conditional `strlo.w r4,[r3,r2,lsl #2]` at file `0x8702c`,
and every earlier sample of it was PRE-write. The gate pad now deposits the APSR it tests (E3 =
`0x80000093`, Z = 0 and C = 0 -> LO held) and a post-store trampoline at file `0x87036` reads the register
AFTER the store. **BRANCH-P:** the conditional store EXECUTED and TOOK (E0 = `0x00001001`, ISENABLER2 word 2
bit 12 SET, with C0 = `0x1` the same word PRE-write in the same pass), so the silent-condition root cause is
exonerated; downstream, the line is not pending at the enable instant (E1 = `0x0`) nor at the firmware's own
post (BRANCH-S). The break sits downstream of the enable register. Evidence
`build/register-dumps/exp/20261004-190222/`.

The chain phase 47 named, with where each link now stands:

```
doorbell 0x400392d4 bit 0                [host-visible]           phase 46 + task 9 ([intrsamp] block)
  -> ctrl-rb raw status 0x400392e4 bit 0   latches 1                task 9 dmesg.txt:770-775
  -> masked status 0x400392ec bit 0        latches 1                task 9 dmesg.txt:770-775
  -> >>> device-internal wire into the GIC: SPI 0x4C <<<            STILL THE MISSING HOP
  -> GIC distributor enable 0x40161108     enable(0x4c) EXECUTED   task 11 (scratch boot, BRANCH-1)
  -> CPU interface IAR 0x4016010c          NOT host-reachable       task 13 (A = 0xffffffff)
```

Two links moved from "asserted" to "measured" this phase: the firmware's `register(0x4c)` ran (task 9), and
the enable thunk executed at runtime (task 11). One link was re-confirmed as host-invisible (task 13). The
delivery hop between the ctrl-rb's latched masked status and the GIC input remains the last link, exactly
as phase 47 stated - now with the enable and register halves eliminated as causes.

## Gate decisions (G1-G5)

| gate | decision | ledger | source |
| --- | --- | --- | --- |
| G1 | blessed the frozen knob-set on the default (bless-as-is); non-blocking ask, no objection recorded | `.omo/ulw-execute/ledger.jsonl:30` | task 5 (`tools/params/definitive-run.params`, sha256 `541060a8...`) |
| G2 | proceed to Wave 3: L1 read nonzero (`0x00040295`); user answered "Proceed to Wave 3 (default)" | `.omo/ulw-execute/ledger.jsonl:54`, `:66` | task 9 (`read-boot.md`) |
| G3 | auto-continue: `[sig] 9/9` with the modified blob + health | `.omo/ulw-execute/ledger.jsonl:60` | task 10 (`probe-boot.md`) |
| G4 | AUTHORIZED on preconditions (BRANCH-1 + `9/9` + healthy recovery); task 12 dispatched | `.omo/ulw-execute/ledger.jsonl:67`, `:73` | task 11 (`scratch-boot.md`) |
| G5 | default branch taken: A = `0xffffffff` -> the GIC block stays host-invisible; the parameters are the measurement | n/a (device result) | task 13 (`viewport.md`, `interp.txt`) |

## Branch recommendation for THE-LAST-LINK section 3

`opensource/docs/phase47/THE-LAST-LINK.md` section 3's table keys on CA `0x40161108` bit 12 and CA
`0x4016010c`:

- `0x40161108` bit 12 = 0 -> `pcie_msg_init` never ran. **Eliminated by task 11**: `enable(0x4c)` executed
  (the S1/S1+4 pair records it), so the distributor bit was written in this boot.
- IAR `0x4016010c` = `0x4c` on the line firing -> the verdict holds and the work is in the handler path.
- IAR = another id -> the id-to-line binding is wrong.
- IAR = `0x3ff` (no line) -> the ctrl-rb output never reaches the controller.

**Recommendation:** the forward-lane results compress this table to the decisive read. `register(0x4c)` ran
(task 9) and `enable(0x4c)` executed (task 11), so the distributor side is armed; task 13 shows the GIC block
cannot be read from the host, so the deciding read `0x4016010c` is device-side only. The next instrument is
therefore a device-side trace across one doorbell ring reading (a) distributor enable CA `0x40161108` bit 12
and (b) CPU-interface IAR CA `0x4016010c`, matching phase 47 section 2's item (3). The vendor-loader lane is
NOT that instrument: task 12 shows it accepts a modified blob (validation assumption falsified) but the chip
fails at firmware runtime with this patch, so the lane is dead as-is for the trampoline patch. The cheap
no-instrument indicator still leads: the masked-status latch at CA `0x400392ec` bit 0 already latches 1 on a
doorbell (tasks 9 and 11), so if a future run shows it not latching, the failure moved upstream of the whole
ctrl-rb path.

## Corrected observables carried into this phase

The phase-48 ack headline stays struck (task 4's correction blocks are in `phase48/host-side-closed.md` and
`THE-LAST-LINK.md` section 3): CA `0x400392f0` is write-to-clear, so "the ack never flips" decides nothing.
The valid observables are the raw status CA `0x400392e4` and masked status CA `0x400392ec`, and the
device-side signatures (pending id CA `0x4016010c`) as the real delivery witness. Task 9's `[intrsamp]`
block reproduces the raw/masked latch cleanly, and every lane this phase keyed on the registering cells and
the two status words, never on the ack.

## Plan-text defects the executors found (recorded, from the ledger)

- **S1/S2 spec range `[0xE7000,0xFFF00]` unsatisfiable** (zero zero-words in that range): the tool recorded
  `spec_range_zero_words:0` then extended to `0x1BFFFF`, landing S1 = `0x1035A0`, S2 = `0x103EB4`
  (`.omo/ulw-execute/ledger.jsonl:32`).
- **The probe rule was vacuous by construction**: the flipped byte at file `0xe2c97` sits outside the loaded
  image (`0xCC0CC`), so it can never execute (task 7 verification, ledger `:34`; `probe-boot.md`).
- **Host-window overlap with region 4** - the spec's P3 mitigation (spec section 6; `viewport.md`).
- **EXP_DONE_CMD default mismatch**: `wifidrv1`'s marker is `omo-drv1: init done`, not the plan's default
  (task 10 header; `read-boot.md`).
- **module-log.txt vs dmesg.txt wording**: the port logs under `omo-drv1`, and several plan references name
  `module-log.txt` while the run output lives in `dmesg.txt` (ledger `:15`).
- **`rox_pci0` is a bound PCIe driver, not an lsmod entry** (project notes).
- **Harness reboot-freshness gate** (`uptime < 180` can accept a stale boot): the round-1/round-2 artifact
  of task 11 (`diag-report.txt`; `scratch-boot.md`).
- **Capture-hook remote/local path bug**: found on first live use, fixed and reverified (ledger
  `checkbox-reset` on task 6, `:45`; the kept dir `build/register-dumps/exp/20261004-163203/`).

## Health across the phase

Every device lane ended healthy: `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0` in
`build/register-dumps/exp/20261004-163526/health.txt`, `.../20261004-164113/health.txt`,
`.../20261004-165139/health.txt`, `.../20261004-171523/health.txt`; task 12's
`.../20261004-165759-vendorloader/post-recovery-health.txt` and `.../20261004-165759-vendorloader/recovery.log`
add (synthesized from both files) `STOCKMD5=0e530b976d5a20e87358671f1a577695 PSTORE=3` and a fresh boot after
the failed vendor-loader boot.
