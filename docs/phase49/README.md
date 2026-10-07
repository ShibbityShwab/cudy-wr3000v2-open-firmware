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
| bracket | follow-up | `gic-view.md` ADDENDUM 21 | `build/register-dumps/exp/20261006-135935/` (the sanctioned re-run) | the take5 instrument (E5 ring / I5 post-EOI / F5 gate-fall) is verified, but the boot died at the completion marker: NO-SAMPLE, a harness/run failure, no `brk3.md` row closes |
| stuck-active | follow-up | `gic-view.md` ADDENDUM 22 | `build/register-dumps/exp/20261006-135935/` (21a's three-instant bracket) | the gate 21a named is a PRIORITY-0 SOURCE HELD ACTIVE: `E5_RPR` = `0x0` (ring) vs `I5_RPR`/`F5_RPR` = `0xFF` (post-EOI / gate-fall); the EOI retires it and the release's guard sample never ran (`M2_PSR` = `0`); the SGI bank and the group enable are REFUTED as the stopper, `RPR` is the comparator; the fast sampler that would NAME the source (`take6`) stalled at the same `[4/7]` marker - NO-SAMPLE |
| take6f capstone | follow-up | `gic-view.md` ADDENDUM 23 | `build/register-dumps/exp/20261006-152035/` | NO-SAMPLE BY CONSTRUCTION: the vendor Wi-Fi stack took both endpoints at ~13.5 s (`hardware attach failed (no endpoint bound)`, `regs=absent irq0=0 isr0=0`), so the blob was never read into the chip and no pad ran (every cell `0x0`, no sentinel `0x50aa7e49`); the ranked 66-B EOIR force is built and verified (`vtool19` CONFIRMED, 10/10 emitted) but was never spent; the harness now FAILs closed (the gate is the verdict) |

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
`build/register-dumps/exp/20261004-190222/`. A third addendum to `gic-view.md` ("the
delivery-configuration test", variant `gicmask`) then swept the delivery SETTINGS for the `0x4c` line at the
firmware's own post. **BRANCH-M (label BRANCH-M4):** the three candidate roots are all EXCLUDED - the CPU
interface is enabled (F0 GICC_CTLR = `0x1`), the line is targeted at CPU 0 (F2 ITARGETSR = `0x01010101`), and
the glue H2D mask bit is open (F5 `0x400392E8` = `0x20`), with PMR/priority/trigger all passing - so the
delivery configuration is fully ARMED and the missing piece is the EVENT ASSERTION (ctrl-rb -> GIC),
not a configuration block. F6/F7 (glue raw/post-mask = `0x0`) is the expected reading of a boot with no ring
rung, so it cannot separate "never asserts" from "asserts but is dropped". The addendum also records two
brief corrections (the `0x3d8` mask CONSTANT vs the real mask register CA `0x400392E8`; the firmware literals
behind the F addresses) and two inherited conventions flagged as residual risks (the F5 mask polarity, and
GICC_CTLR bit 0 read without an IGROUPR group check). Evidence `build/register-dumps/exp/20261004-194824/`.
A fourth addendum to `gic-view.md` ("the assertion trio", variant `gicking`) then ran that addendum's named
next branch: it rings the H2D doorbell itself and answers all three of its open questions in one boot, with
the fixture paid forward. **BRANCH-GICK:**
RING - the ring LEAVES THE HOST and latches the glue raw status (F6 bit 0 = 1, against gicmask's `0x0` on
the same pad with no ring), but the run does NOT show it reaching the device GIC: the post-ring ISPENDR2
samples (D0 = D4 = `0x00000020`, id `0x4c` bit 12 clear) were taken with the glue mask DELIBERATELY held
MASKED (F5 = `0x21`, F7 = `0x0`) and no post-unmask ISPENDR2 read exists, so the ring -> GIC step is
UNDECIDED in either direction. GROUP - the group gate DIES: G2 GICD_IGROUPR word 2 (`0x40161088`) bit 12 =
0, so id `0x4c` is Group 0, exactly the configuration F0 GICC_CTLR = `0x1` needs (EnableGrp0 SET, EnableGrp1
= 0); the residual root is the EVENT ASSERTION side, not the group. POLARITY - "1 = masked" is CONFIRMED
in-run by the F6/F7/G0 triple in the same boot (raw rises, masked status stays clear, then status rises once
the mask opens), which excludes both the inverted and the independent-raw-latch models, so the polarity no
longer rests on the inherited sibling header. Evidence `build/register-dumps/exp/20261005-053936/`.
A fifth addendum to `gic-view.md` ("the post-unmask ring test", variant `gicunmask`) then ran that addendum's
named next branch and retired its R2 confound by opening the glue mask BEFORE the ring. **BRANCH A
(ADDENDUM 5): THE RINGED EVENT REACHES THE GIC.** With the mask OPEN (`0x20`, never `0x21`) and one ring,
the distributor pending bit is set (H0 ISPENDR2 word 2 = `0x00001020`, id `0x4c` bit 12 SET) and the CPU
interface reports the same id (H1 HPPIR = `0x0000004C`), while the glue latched and passed (H3 = H4 =
`0x1`): the device-internal ctrl-rb -> GIC wire is ALIVE, phase-48's dead-link hypothesis is REFUTED, and
the earlier zeros mean the natural firmware posts never RING the doorbell (a TRIGGER problem, not a dead
wire). Evidence `build/register-dumps/exp/20261005-072450/`.
A sixth addendum to `gic-view.md` ("the trigger test", variant `trigring`) then isolated that ring -> GIC
step in one visit. **BRANCH T4-BIT12:** with the glue mask held OPEN across a SAME-VISIT pre/post pair, the
pre sample is `0x00000020` (ISPENDR2 word 2, bit 12 CLEAR) and the post sample is `0x00001020` (BIT 12 SET,
id `0x4c`) with HPPIR = `0x0000004C` and the glue raw/status rising `0x0` -> `0x1`, so the single ring store
is the only actor between them and the ring -> GIC delta now stands alone. The host-side half is NOT decided:
the port registers no IRQ handler, so the expected host witness (`hostisr.md` table row B, no `207:`/`209:`
line) is the absence of an instrument and unsupported either way; that acceptance test is the named next
branch. Evidence `build/register-dumps/exp/20261005-075136/`; two verifiers CONFIRMED. The same session wrote
two first-class plans from the phase's four spec reports: `opensource/docs/HYBRID-IMAGE-PLAN.md` (the graft,
keep/graft lists, build, A/B-safe flashing, acceptance gates, open decisions) and
`opensource/docs/TOOLING-IMPROVEMENTS.md` (the ranked 9-item harness list plus the top-3 quick wins).
A seventh addendum to `gic-view.md` ("the natural-post ring test", variant `trignat`) then ran that next
branch and it produced NO branch, honestly: the `trignat` instrument (Pad A at the firmware's own natural
post, Pad B the dispatcher-consumption read) and the endpoint-IRQ port change were both built and staged,
but no trignat cell was ever captured. The parent RUN failed on the HOST-SIDE `MODNAME` gate
(`tools/exp.sh` gates step 4 on `lsmod | grep '^wifidrv1-isr '` while the ko's internal name is `wifidrv1`),
so the capture step never ran; the ONE authorised re-run (same module bytes, renamed to satisfy the gate)
loaded and completed but its capture hook was reset by the peer, leaving `capture-cmd.txt` at 0 bytes. Every
trigger2.md section-4 input (`A_S3`, `A_S4`, `A_S5`, `A_S7`, `A_S8`, `B_D0`, `B_D2`, `B_P3`) is therefore
ABSENT, so all four branches are unsupported and the `PAD-DID-NOT-RUN` label the parsers print is a null
classification, not a measured value. **The one device-side result that survives is the virq outcome: NOT
DELIVERED** (`pci_dev->irq=0`, no `request_irq`, `irq0=0 isr0=0`, no `207:`/`209:` line), the `hostisr.md`
ROW B, and the port change did not reproduce the vendor's `pci_assign_irq() -> 207` path. Evidence
`build/register-dumps/exp/20261005-083612/` (never created), `build/register-dumps/exp/20261005-084742/`,
`build/register-dumps/diffs/20261005-083612-vrun3/`; device healthy after recovery. The same session also
started the reframe the lead directed: `opensource/docs/UPSTREAM-PORT-PLAN.md` (NEW) is the honest inventory
and staged roadmap for our own fully-current OpenWrt (our kernel + our drivers) and `HYBRID-IMAGE-PLAN.md`
now carries a `STATUS: FALLBACK` header, retained only as a labeled fallback.

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

## The trignat retry, salvaged (2026-10-05)

The seventh addendum's SALVAGE resolves it: `gic-view.md` "ADDENDUM 7 - AMENDMENT (2026-10-05)" records the
hardened retry whose local driver died at step `[4/7]` while the takeover boot stayed live and the
detached hook's output was pulled post-hoc. **BRANCH = PAD-DID-NOT-RUN**: Pad A rang (the ringed id `0x4c`
reached the device GIC) but Pad B never executed, so the consumption half stays OPEN and no numbered row
closes; the virq outcome is NEGATIVE (irq 0, ISR never executed). Evidence
`build/register-dumps/exp/20261005-090644-salvage/` (`capture-cmd.txt`, `interp.txt`, `acceptance.txt`
88/0, `cleanup.txt`, `SALVAGE-NOTE.txt`
); device recovered healthy.

## The consumption-gate retry + the virq root cause (2026-10-05)

`gic-view.md` "ADDENDUM 8 (2026-10-05)" closes the two threads the salvage named. The build-node fix is
`padb.md` option (a): the `trigcons` variant MOVES Pad B from the unreachable announce-exit site (file
`0x86F7E`) onto the `0xcece` wait's own `movw r3,#0xcece` at file `0x86F74`, so the B cells get a producer
without any handshake completing, and doubles the pagination into pre-wait and post-wait sets (a single
boot still yields one instant, so the row stays `-`/PAD-DID-NOT-RUN and no numbered row closes). The
declared ceiling is pre-consumption: row 2, with the residual gate named as the CPU-interface take. The
protocol-truth companion is option (c), a HOST write of `0x0000CECE` to BAR0+`0x3b810c`, the only fix that
can reach row 1. On the host side, the virq negative's real cause is recorded: the `-22` is the port's own
`of_irq_parse_and_map_pci` fallback (a documented dead end here), and the port threw away the 207 the core
had ALREADY assigned because vanilla 5.10 `struct pci_dev->irq` sits at 0x1ac while the vendor kernel has
it at 0x184. The fix reads the line back through `PCI_INTERRUPT_LINE` (submodule `3ac4820`,
`omo/phase22-hccaccept` only). Companion specs: `build/tmp/dt-spec/padb.md` (the gate + fix) and
`build/tmp/dt-spec/virq3.md` (the root cause).

## The corrected trigcons-2 run: ROW 2 + VIRQ 207 OWNED (2026-10-05)

The retry the earlier plan could not stage landed. `gic-view.md` "ADDENDUM 8 - AMENDMENT (2026-10-05)"
records evidence `build/register-dumps/exp/20261005-103047/` (`EXP RESULT: PASS`, acceptance 92/0): the moved
Pad-B drain-point site ran (`B_P3` = `0x50AA7E49`), the ring reached the device GIC (`A_S3` = `0x00001020`,
`A_S4` = `0x0000004C`) and was still pending at the drain point (`B_D0` = `0x8`, `B_D1` = `0x4`, `B_D2`
`0x00001020`, `B_D3` = `0x0000004C`), so the device row is ROW 2 RING-PENDING-NOT-TAKEN within the
observed window; the host row is VIRQ 207 OWNED, no ISR observed (`request_irq(207, IRQF_SHARED) rc=0`,
`207: 0 0 GIC-0 91 Level omo-drv1`) via the `PCI_INTERRUPT_LINE` fix (submodule `3ac4820`, run 37295389630,
`.ko` md5 `1f0e80ed9a02b80ab2deb337d288e781`). Two setup fixes made the run valid: staging the artifact as
`wifidrv1.ko` (the module's internal name; the old `wifidrv1-isr` filename made the wait grep the wrong name)
and a TS-gated capture recovery (the old selector grabbed the stale salvage dir). The device dispatcher's
non-consumption (bounded) and the D2H/INTA host path stay open.

## The real inta run: GLUE LATCHED, NO HOST DELIVERY (2026-10-05)

`gic-view.md` "ADDENDUM 9 - CORRECTION (2026-10-05): the real inta run (110922)" corrects the addendum's
header: the ADDENDUM 9 block above describes run `20261005-103047` (the `trigcons-2` story) and is
SUPERSEDED for the `inta` variant it claims to record. The real `inta` run is
`build/register-dumps/exp/20261005-110922/` (`EXP RESULT: PASS`, `exp_rc=0`, one `exp.sh` cycle 11:09:21Z to
11:12:13Z, acceptance 108/0 `ALL_OK`), verified by `build/register-dumps/diffs/20261005-110921-vrun6/verdict.txt`
(FINAL CONFIRMED, medium-high, two recorded deviations) and
`build/register-dumps/diffs/20261005T1115Z-vtool6/verdict.txt` (CONFIRMED). Instruments: the port knob
(submodule `b5f6aac`, "lab(wifidrv1): post-release D2H/INTA ring knob (intapost)") plus the fw blob
`build/tmp/fw-patched/inta.bin` (md5 `bcf14dbeefe45bcfce8df279c06776bd`). The four `[intapost]` steps: the
`0x8` natural-post wrote the unlock `0x0000cece` to CA `0x4000010c` (`rb=0xcece`) and the glue LATCHED (raw
`0x0` -> `0x8`, status `0x0` -> `0x18`); steps `0x1` (CA `0x400392d4`), `0x2` (CA `0x400392d4`), and `0x4`
(CA `0x40101434`) each left raw/status/isr pinned with delta `0`, 2000 ms each. **HOST ROW 2 = GLUE LATCHED,
NO HOST DELIVERY**: the glue latched but `/proc/interrupts` `207: 0 0 GIC-0 91 Level omo-drv1` stayed
count 0 and `isr0=0`, so the assertion/forward hop (glue -> endpoint INTx -> RC -> host GIC 91) is the
remaining gate. **DEVICE**: `B_D2` (ISPENDR2 w2) = `0x1020` pending persists, `B_D4` (ISACTIVER2 w2) = `0x0`
not taken, `B_D5` (CPSR) = `0x20000193` with I = 1, GICC_CTLR `0x1`, PMR `0xf0`; the unlock LANDED (Pad L ran
ONCE, `L_CNT=1`, the loop exited) but the delayed `L2_*` cells are all 0 because nothing re-visited the park.
TWO vrun6 DEVIATIONS: the host row is reached on the `0x8` natural-post provenance, not the `0x2`/`0x4`
steps the row names (their deltas are 0); and `devcpu.md` section 5 ROW 2's literal predicate is NOT
satisfied (`L2_ISP` bit 12 clear, `L2_OU0` = 0), so the named row is a meaning-based classification on the
drain instant and "parked in the `0xcece` handshake" is contradicted by this run's own `L_CNT=1`. The live
device is healthy; hard rules held (IAR never read, the gate CA `0x4000010c` is the only `0x...10c`
touched). Next threads: the forward hop (the endpoint's INTx config-space state), a post-unlock device take
sample, and a re-visit for the `L2` delayed cells.

## The INTA knob v2 + the inta2 run: the 209 witness and the post-exit take (2026-10-05)

`gic-view.md` "ADDENDUM 10 (2026-10-05): the forward-hop probe + the post-unlock take" records the run that
answered ADDENDUM 9 - CORRECTION's two named next threads in one boot. Evidence
`build/register-dumps/exp/20261005-115837/` (variant `inta2`, one `exp.sh` cycle, staged blob
`build/tmp/fw-patched/inta2.bin` md5 `545e77a5b12b4e0da8a6923eea29672c`, staged `.ko` md5
`4d56a0ae3c6f860c86b20a7c3c819605` at submodule `0398e20`, CI run 37305885971), adversarial verifier
`build/register-dumps/diffs/20261005-115836-vrun7/verdict.txt` (NOT CONFIRMED as a positive) and instrument
verifier `build/register-dumps/diffs/20261005T1200Z-vtool7/verdict.txt` (5/5 artifact checks CONFIRMED, one
functional defect found live). Specs: `build/tmp/inta-spec/{intx.md,take2.md,devcpu.md}`. The knob v2 adds
two `intapost` bits, `0x10` `dual-line` (claim the sibling and take a second counter on 209) and `0x20`
`snapshot` (the read-only config/MSI/glue comparison), with no 42nd knob. **HOST: no row.** The port read the
sibling's config `PCI_INTERRUPT_LINE` as `0xff` and skipped `request_irq(209)`, so no 209 measurement exists;
the same capture shows the kernel owns 209 (sysfs `irq=209`, lspci pin A -> IRQ 209), so the decisive witness
was never established and ROW 5's premise is false. **DEVICE: Site F ran, Site N is void.** Site F (the
`0xcece` gate's fall-through `0x86F7E`) is a coherent pre-take baseline (`F_ISP` = `0x1020` bit 12 SET,
`F_ACT` = `0x0`, `F_PSR` I = 1, `F_OU0` = `0x8`, `F_SNT` = `0x50AA7E49`); all six `N_*` cells read 0
(including the constant `N_SNT`), which the acceptance mis-read as a single-visit boot but which the pad's own
deposits contradict, so page 14 (`0x157000`) did not retain the writes (a cell-selection defect). Health after
recovery `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`, hard rules held.

## The twin/ETE probe: the run FAILED with no capture, the twin hypothesis is BLOCKED (2026-10-05)

`gic-view.md` "ADDENDUM 11 (2026-10-05): the twin/ETE probe + the corrected witnesses" records the run the
ADDENDUM 10 next threads named on the twin/ETE layer (the forward-hop layer behind the glue post-mask status,
as distinct from the host-side copy A). Evidence `build/register-dumps/exp/20261005-130020/` (variant `inta3`,
one `exp.sh` cycle via `run-inta3.sh`, `EXP RESULT: FAIL`, `exp_rc=1`, staged blob `build/tmp/fw-patched/
inta3.bin` md5 `eee1f67370b56eca42316f6eeb490c45`, staged `.ko` md5 `e20bf7e571c5a57823e76b7fa849ebf8` at
submodule `73b1230`, CI run 37312967151), adversarial verifier
`build/register-dumps/diffs/20261005-131645-vrun8/verdict.txt` (FINAL CONFIRMED, the parent's own
`CYCLE-FAILED.txt` central claim REFUTED) and instrument verifier
`build/register-dumps/diffs/20261005T1315Z-vtool8/verdict.txt` (5/5 artifact checks CONFIRMED, one accepted
spec deviation). Specs: `build/tmp/inta-spec/{twin.md,witness2.md,dtc.md}`. The knob v3 adds two `intapost`
bits, `0x40` `twin-stim` (W1 the twin mask `0x40039ae8 <= rd & 0xfffffc20`, W2 the twin doorbell
`0x40039ad4 |= 0x8`) and `0x80` `msi-test` (a read-only probe; `CONFIG_PCI_MSI=n` here), and widens v2's
`0x20` snapshot with copy B's raw/mask/status and the ETE group, with no 42nd knob. The firmware instrument
`inta3` is inta2 byte-for-byte except six Site-N cells moved onto the retention-verified page 10
(`0x150058..0x150080`), per witness2.md FLAW 2, with an `N_SNT` retention sentinel. **ALL THREE BRANCH TABLES
ARE NO-ROW**: the cycle died at step `[4/7]` (the done marker never appeared), so `capture-cmd.txt` never
landed and there is no capture to re-parse - a NEGATIVE ON DATA, not a negative on the hypothesis. The twin
hypothesis ("copy B drives `0000:00:00.0`'s pin") stays BLOCKED, not decided. **The parent's self-report is
REFUTED:** a NEW pstore panic record (`dmesg-pstore_blk-3`, 69,508 B) in the inta3-only window IS this run's
dump - `Comm: insmod`, `wifidrv1` frames, and the decisive delta vs inta2 `isr0=0` versus `isr0=6511173` plus
~350 `[isr] irq=207 ... status=0x00000018` lines (~2.3e6 IRQ/s) to t=244 s. The boot PANICKED on a 207 IRQ
storm with the glue copy-A post-mask status latched (`0x18`) the port's ISR could not clear; the record's
window begins after the knob's print points, so it cannot name the step. The one re-run rule was honored (a
device-side failure, not a named host-side cause). Health after recovery `WIPHY=2 IFACE=6 CAL_SUCC=1
OMO_OFF=0 STAGED=0 LOADER=0`, pstore 2 records (blk-2 + the new blk-3), hard rules held.

## The quiesce probe: NO-STORM, the bound armed but never tripped, page 10 finally retained (2026-10-05)

`gic-view.md` "ADDENDUM 12 (2026-10-05): the quiesce probe" records the bounded boot the ADDENDUM 11 next
threads named. Evidence `build/register-dumps/exp/20261005-135624/` (variant `quiesce`, one detached
`exp.sh` cycle via `run-quiesce.sh`, start 13:56:23Z end 13:59:05Z, `EXP RESULT: PASS`, `exp_rc=0`,
staged blob `build/tmp/fw-patched/inta3.bin` md5 `eee1f67370b56eca42316f6eeb490c45`, staged `.ko` md5
`91fba1dc512536c94bfd1e419173c046` at submodule `2507a42`, CI run 37320054878), adversarial verifier
`build/register-dumps/diffs/20261005-135623-vrun9/verdict.txt` (CONFIRMED with three recorded
corrections) and instrument verifier `build/register-dumps/diffs/20261005T1401Z-vtool9/verdict.txt` (6/6
artifact checks CONFIRMED, the mandatory bound present and structurally sound). Specs:
`build/tmp/inta-spec/{quiesce.md,bisect.md,clocks2.md}`. KNOB V4 adds four module params beside the frozen
`intapost` set (`quiesce` bitmask, `qbound` clamped `1..64`, `qwait_ms`, `bisect`) and THE HARD BOUND:
both ISRs `disable_irq_nosync()` after `qbound` entries before ANY MMIO and print `IRQ_DISABLED_BOUND`
(the ISR's own log is 1 line / 256 entries, so it cannot printk-storm). **BRANCH 1 = NO-STORM, LATCH-HELD,
NOT-QUIESCED**: the ladder and the bound were armed from the first entry (`mode=0x7 bound=64`), but the
207 line never fired (`isr0=0`, `isr_n=isr2_n=0`, `/proc/interrupts 207: 0 0`, no `[qsv] entry`), so
with no ISR entry no mechanism executed; the glue held `raw=0x1 mask=0x20 stat=0x11` the whole 5000 ms and
the supervisor ended `quiesced=0 winner=- maskrestored=0`. The bound held VACUOUSLY, and no numbered
quiesce.md row is literally satisfied (row 6's "glue clear" is only half met). **BRANCH 2 = B5 CONFIGURED,
STIMULUS UNEXECUTED**: `bisect=5` forced `effective=0x10` (the corrected 209 witness alone, twin/ETE
`0x40` OFF, 3 `[intx2]` / 0 `[intx3]` lines), but the 209 stimulus did not arm - `omo_dual_line_attach()`
fell to its `else` and the port skipped `request_irq(209)` because the sysfs virq read `255`, and vrun9
corrected the run's "armed" to "configured". The storm's culprit stays UNNAMED (it is not sufficient-cause
tested: `0x40` absent and `0x10` inert, no storm followed); bisect.md sec. 0 already settled that the
pstore storm record is an older wifidrv1 boot, not knob v3. **BRANCH 3 = the 209 witness UNAVAILABLE** (a
209 count of 0 is vacuous; the same capture shows the host owning 209, `pin A -> IRQ 209`). **BRANCH 4 =
RETAINED**: the six page-10 cells finally landed (`N_ACT=0x0 N_ISP=0x20 N_HPP=0x3ff N_OU0=0x0
N_PSR=0x20000193 N_SNT=0x50AA7E49`, with the same-page sentinels `A_P2=B_P3=0x50AA7E49`), VALIDATING
ADDENDUM 11's page-14 defect fix; the take is still UNWITNESSED. The mandatory bound held
VACUOUSLY; `IRQ_DISABLED_BOUND` never printed; the pstore set is unchanged (no new record). Health after
recovery `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0`, stock md5 `0e530b97...`, hard rules held
(no `0x400392f0`/`0x40039af0` write, no IAR `0x4016010c` or `0x10161000` read; the gate `0x4000010c`
is a different address). Next threads: make the 209 source deliver a live line, walk the bisect rows from
the control up (or exercise the bound on purpose), and re-sample the take at a later boot phase now that
page 10 is a proven home.

## The storm closure: three single-rung boots, the fixed supervisor, no rung quiesced (2026-10-05)

`gic-view.md` "ADDENDUM 14 (2026-10-05): the storm closure" records the sequence the ADDENDUM 13 next
threads named. Evidence: three chained boots, `build/register-dumps/exp/20261005-181512/` (rung 1
Q_CONSUME), `build/register-dumps/exp/20261005-182024/` (rung 2 Q_FWACK) and
`build/register-dumps/exp/20261005-182537/` (rung 3 Q_MASKCLOSE), each one detached `tools/exp.sh` cycle,
`STORMCLOSE-R1/R2/R3 RESULT: PASS` (`exp_rc=0`), staged ko `wifidrv1.ko` md5
`028f9d1281079c62d5db286f3586b23d` (97,064 B) at submodule `948f814cd6a1adadb4a8032672218445dae67a6f` (CI
run 37354023564, `omo/phase22-hccaccept` only) and the REUSED firmware `build/tmp/fw-patched/inta3.bin`
md5 `eee1f67370b56eca42316f6eeb490c45`, staged as the `.omo-pat` overlay. Adversarial verifier
`build/register-dumps/diffs/20261005T1829Z-vrun11/verdict.txt` (CONFIRMED as a record, D1-D9 recorded)
and instrument verifier `build/register-dumps/diffs/20261005T1814Z-vtool11/verdict.txt` (CONFIRMED; NO
unconditional `enable_irq` remains - the module has exactly ONE, fully gated). Specs:
`build/tmp/inta-spec/{superfix.md,ladder3.md,ep1.md,twinq.md}`. **THE FIXED SUPERVISOR**: v6 deletes the
v5 bug (the unconditional `enable_irq` at the head of every rung that panicked the box), keeps ONE
`enable_irq` behind three gates (one probe per boot, both levels clean, a bound-disabled 207 line), runs
ONE rung in PROCESS CONTEXT on the DISABLED line (the ISR runs no rung), and never re-enables after a
bound-trip. **THE LADDER, rung by rung**: CONSUME (rung 1) retired glue bit 4 (`0x18 -> 0x08`, the
phase-21 LIVEBIND signature), FWACK (rung 2) retired nothing (`0x18 -> 0x18`), MASKCLOSE (rung 3) retired
bit 3 (`0x18 -> 0x10`) and left bit 4 - the opposite survivor - so **NO RUNG QUIESCED**: twinB stat stayed
`0x08` in all three, zero `QUIESCED_BY_*` lines, and every boot left the line DISABLED (`state=BOUND`).
**THE STORM + THE BOUND**: the same W2 twin doorbell (`0x40039ad4 |= 8`) re-fired 65 IRQs on 207 in ~16 ms
in every boot, and THE HARD BOUND tripped exactly once per boot (`IRQ_DISABLED_BOUND irq=207 n=65
bound=64`), self-disabling before any MMIO with zero `callbacks suppressed`. The opt-in re-enable probe was
armed (`quiesce=0x8`) but NEVER reached, so no bound #2 was spent. **THE EP1 209 RESULT**: the v6 arm is
CODE-TRUE, RUN-FALSE - every boot prints `[intx2] no sibling INTx virq (irq=0)` and `209 arm failed
rc=-19`, because the arm is attempted at t~43.4 s before the sibling's pci_driver bind at t~66.5 s, so
`isr209=0` and the 209 rows stay VACUOUS. Health after recovery `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0
STAGED=0 LOADER=0 RECOVER=0`, `OMO_PAT=0`, stock md5 `0e530b976d5a20e87358671f1a577695`, pstore unchanged
at 3 records; hard rules held (no `0x400392f0`/`0x40039af0` write, no IAR `0x4016010c` or `0x10161000`
read; the `0x4000010c` gate is a different address). BOUNDS: this sequence IS in-capture (vrun11 re-parsed
all three boots), but the standalone ledger row is STALE for boot 3, the 209 arm is run-false, and boots
1/2 lack the processed worker files. Next threads: make the re-enable probe reachable, fix the 209 arm
timing, and pack the two captures.

## The full-reversal sprint: the 209 witness armed, the twinclose rung clears the twin (2026-10-05)

`gic-view.md` "ADDENDUM 15 (2026-10-05)" records four chained sessions. Run 1 (storm close, ADDENDUM 14)
held 3/3 boots and measured the ladder: CONSUME retires glue bit 4 (`0x18 -> 0x08`), FWACK nothing,
MASKCLOSE filters bit 3 (`0x18 -> 0x10`), and the residual `0x08` is the twin copy-B bit. Run 3
(chip-deep, `build/tmp/inta-spec/chipdeep.md`, offline) places the H2D gate at the DEVICE CPU's take (its
third precondition, reading the IAR with IRQs live, is never observed; every CPSR has I = 1), voids the old
"152 moves / 0 filled" DR line (moves were credit turnover, "filled" read word1, which the DR path never
writes) in favor of the `buf+0x0a == 0x5a5a` header test, and identifies the twin as copy B (CA
`0x40039800` = copy A + `0x800`). Run 4 (`build/register-dumps/exp/20261005-185140/`, variant `vrun12`,
verdict `diffs/20261005T1855Z-vrun12/` CONFIRMED with D1-D6, staged ko md5 `4e8088e8...`) is the payoff:
`request_irq(209, IRQF_SHARED)` returns `rc=0` (the sibling-probe claim fixed the earlier `rc=-19`), the
line caught `isr2_n=9` before its bound of 8, and the TWINCLOSE rung works - the twin mask write CA
`0x40039ae8` `0x20 -> 0x28` cleared the twin status `0x08 -> 0x00` (`twin_residual=0x0`) while glue
copy-A stayed `0x18`, so the FULL quiesce is CONSUME plus TWINCLOSE and the COMBINED RUNG is the next
experiment. Static companion: copy B has its clear (CA `0x40039af0`) but no vendor code writes it, and the
copy-A clear `0x400392f0` remains forbidden by the `out[5] <= 8` hang that originated the rule. Run 2
(upstream-compile) is green: `luofu-clk.ko` cross-builds in CI at `dff5925` (success x2, after the honest
`bed58e5` fail, block-comment fix, success cycle), the edited DTS lints at dtc 1.7.2 (dtb 4,506 B, sha256
`732104b1...b3946`, errors 0, three cosmetic warnings), and `build/tmp/inta-spec/stage2.md` carries the
ranked stage-2 driver inventory (CRG -> pinctrl -> PCIe RC -> endpoint -> glue -> wifidrv). Health: bound
held on both lines, zero panics, pstore delta none, router healthy. Reported aux gaps: `vrec10` and
`v-run2` wrote no verdict dir, `v-chip`'s landed in the wrong dir, and run 4's worker files (acceptance,
rows, MATRIX) stayed missing though `tools/finish-evidence.sh` produced KNOBSET/cleanup/interp (D1).
Next: the COMBINED QUIESCE RUNG (CONSUME + TWINCLOSE) plus the bounded re-enable probe.

## ADDENDUM 16 (2026-10-05): the combined quiesce + the real chain - the instrument is built and never spent

Knob v8 committed (`553342d` on `omo/phase22-hccaccept` only, +128/-24 on `lab/wifidrv1/wifidrv1.c`) adds
rung 7 `Q_COMBO` (TWINCLOSE then the vendor CONSUME order), the sec.3 second-bound re-enable probe, and the
real-chain preamble opt-in `Q_RC_PRE`. Both intended boots were BLOCKED on the CI artifact and never ran:
no `run-combo.log` or `run-realchain.log` exists, a tree-wide grep for the decisive strings matches only the
`build/tmp/*-selftest/` fixtures, and the newest boot on disk is still the twin-close boot `20261005-185140`.
The independent ko check scored the still-staged v7 ko 57 passed / 11 failed, with all eleven fails being the
v8-only features, so a combo boot on that ko would have been a silent `case 7` no-op. The CI run
`37365078637` landed at 19:54:36Z and the artifact verified clean (md5 `3f87f1e9fe5ed9666f27d1f784d34535`,
vermagic `5.10.201 SMP mod_unload ARMv7`, all seven v8 strings present, no store of either forbidden W1C
constant). The two branch tables are recorded so the runs can be judged the moment they exist: boot 1 against
combod.md sec.2 (`QUIESCED_BY_COMBO` / partial / probe), boot 2 against realchain2.md sec.5 (`TAKEN` /
`ABSENT-1..3` / `CONTROL`). Neither branch is disproved, only untested. The supervisor discipline was
re-derived from source: both ISRs self-disable before any MMIO, `enable_irq` exists only inside
`omo_sv_probe` under the tighter second bound with both counters reset, and a re-assert is final. The probe
was never spent, so the second bound has still never tripped on device. Router healthy (2 wiphys, 6
interfaces, `[SUCC]` both bands, zero leftovers, no new pstore). Next: fetch, verify and run the two boots on
the landed ko, then spend the second bound on a rung that leaves both levels clean. Full record in
`gic-view.md` ADDENDUM 16; verdicts `diffs/{20261005T1946Z-vtool12,20261005T1954Z-vrun13}/`.

## ADDENDUM 18 (2026-10-05): the upstream arm B - the pinctrl skeleton lands and its CI lane is green

The stage-2 second driver landed, so the pinctrl row of the stage-2 inventory (`build/tmp/inta-spec/
stage2.md` row 2, after CRG) now has code and a CI verdict: submodule commit `d4875e6` on
`omo/phase22-hccaccept` only (`lab(luofu-pinctrl): stage-2 IOMUX skeleton + the pinctrl CI lanes`, 5 files,
+421/-7). It adds `lab/luofu-pinctrl/{luofu-pinctrl.c,Makefile,README.md}`, transcribed from the vendor
`hi_kpinctrl.ko` (`hsan,luofu-peri-pinctrl`, lsmod use count 5) as 37 pins / 24 groups / 24 functions, with
`pinctrl_register` and `set_mux` + the pinconf setters as deliberate NO-OPS so the bootloader's mux state is
preserved; the skeleton maps the pinned `"mux"` (`0x14900100 0x3c`) and `"cfg"` (`0x14940000 0x100`)
windows read-only and performs no register writes at all. Full design + the vendor ELF evidence is
`build/tmp/inta-spec/pinctrl.md`.

CI, both lanes: `lab-module-build.yml` (the `omo/**` lane) becomes a `fail-fast: false` matrix over
`[luofu-clk, luofu-pinctrl]` with a per-module build step, `vermagic` line and `<module>-ko` artifact, and
the `master`-triggered `build-load-test-module.yml` gets the matching `build luofu-pinctrl module` step, its
`vermagic` line and the `luofu-pinctrl-ko` upload.

**SMOKE RESULT: CI GREEN.** Run `37371987343` (`lab-module-build`, push, head `d4875e6`, started
2026-10-05T20:48:08Z) has job `build (luofu-pinctrl)` **success** (2026-10-05T20:54:03Z -> 20:55:34Z, all
steps including `show vermagic` and the artifact upload green; the sibling `build (luofu-clk)` job is green
as well), i.e. the skeleton cross-compiles against the vanilla 5.10.201 arm headers exactly as `luofu-clk`
did at `dff5925` (ADDENDUM 15). Caveats kept: the lane has no `Module.symvers`/`vmlinux` dump so the
unresolved-symbol check is skipped, and the .ko was never loaded on the device - that task ran no device
cycle. Next: the per-group pin lists, the per-pin `drv_data {reg_off, shift, func}` behind `set_mux`, the
`"cfg"` bitfield map behind `pin_config_set`, and the reset deassert (a reset write, deliberately held
back). Pointers: plan status block `opensource/docs/UPSTREAM-PORT-PLAN.md`; spec `build/tmp/inta-spec/
pinctrl.md`.
## ADDENDUM 19 (2026-10-05): the arm-A take probe (take2) - the 0x4c priority flip lands but the take never moves

The discriminator cycle `select.md` sec.5 asked for ran as variant **`take2`** (blob md5
`eeeb252f20392f2b0cb861336feb6411`), the `gicv2` IAR-source canary layered on take1 plus the three priority
stores and the readback block. It is recorded in full as `gic-view.md` **ADDENDUM 18** ("the 0x4c
selection"); this block is the pointer.

The flip stores landed (`P_4C` byte0 `0x00`, `P_40` byte0 `0xF0`), yet the take did not move: `X_HPP` still
names `0x1D`, `X_ACT` bit12 stayed CLEAR, and `X_OU0` stayed `0x8` in the same epoch, so source `0x4c` was
never acknowledged and never consumed. At the send-site instant `F_HPP = 0x4C`, so the destination is
reachable and the pending-id ranking DID move; what the flip could not do is make any of the four ISR
entries serve it. The `0x1D` residual hardened: `P_ID` byte1 reads back `0xE0`, the authored PPI byte, and
`HPPIR` still names `0x1D` while `0x4c` sits at priority `0x00`, so priority-first does not explain it and
the group explanation is killed by `G_GRP0` bit29 = 0. One IAR return word was the invalid id `0x402`, so
this boot's per-entry ids are suspect and the flip must be re-run clean. Next: re-run the flip with only
`0x4c -> 0x00` as the mutation, then mask `0x1D` (ISENABLER0 bit29) and `0x40` (ISENABLER2 bit0) to measure
arbitration with the competitors out of the set. Evidence `build/register-dumps/exp/20261005-210057/`;
instrument verdict `build/register-dumps/diffs/20261005T2101Z-vtool14/verdict.txt` (CONFIRMED). Health: bound
held both lines, no new pstore, 2 wiphys / 6 interfaces, calibration `[SUCC]` both bands.

## ADDENDUM 20 (2026-10-05): the tie-break (take3) - ROW 5 THE INSTRUMENT: the 0x4C promotion held, the 0x1D mask never landed (an emitter bug)

The tie-break cycle `tiebreak.md` sec.3 specified ran as variant **`take3`** (blob md5
`072de986879bdbad00f339b2ad21ebd7`, `wifidrv1.ko` `3f87f1e9fe5ed9666f27d1f784d34535` reused unchanged) with
`tools/exp.sh` serial/detached, the watchdog armed first and the mandatory bound `qbound=64`/`qbound209=8`.
The cycle recovered healthy (`TAKE3 RESULT: PASS`, 21:27:10Z to 21:29:56Z). This block is the pointer; the full
record is `gic-view.md` **ADDENDUM 19** ("the tie-break"). Evidence
`build/register-dumps/exp/20261005-212711/`; instrument verdict
`build/register-dumps/diffs/20261005T2130Z-vrun16/verdict.txt` (**ROW 5**, all eight claims CONFIRMED).

The synchronized flip's two halves came apart. The `0x4C` priority promotion LANDED and HELD to the END
(`E_P4C` byte0 `0x00`, `E_EN2` bit12 SET, `GICD_CTLR`.RWP 0), and its `0x40`/`0x45` mask landed too
(`E_EN2` = `0x5000`). The competitor-disable for the banked TWD PPI `0x1D` did NOT: `E_EN0` bit29 reads SET
(`0x2000FFFF`) because the emitter `write_ca_block()` (`tools/patch_fw_scratch.py:1779`) writes `movw r0,#0`
for the mask `0x20000000` and never emits the `movt r0,#0x2000` half, so `ICENABLER0`/`ICPENDR0` got
`0x00000000`. The build's own self-check counts `write_ca_block(...)` calls and re-asserts the constant
through the SAME buggy encoder, so it passes; `vtool15`'s verdict is still an unfilled skeleton, so the take3
build was never adjudicated in writing either.

Consequently the boot does NOT test `0x4C` promoted AND `0x1D` masked; it reproduces take2's out-ranking
under the tighter mask, and the honest label is **ROW 5, the pad/site fault, with a NAMED cause**: `0x4C` stayed
pending and unaided (`E_ISP` bit12 set, `E_ACT` bit12 CLEAR, `E_OU0` = `0x8`), `E_HPP` still names `0x1D`, and
the IAR take record stayed SGI-class (`V2_ID` = `0x00000402`, ring `0,1,2,0x402`). The take2 prediction is now
measured (`E_P4C` = `0x00` AND `0x4C` pending AND the two SPI competitors masked and STILL no `0x4C` take),
which says `0x4C` cannot move from priority alone; but because the one source that out-ranks it, the banked PPI
`0x1D`, was never removed from the ENABLED set, "priority is not the comparator" STAYS BLOCKED. The new E block
(`E_P1D`..`E_SNT`, twelve page-10 cells read at `selpost`) closes ADDENDUM 18's readback gap and puts the
blocker on the one store the emitter dropped. Health: bound held both lines (`207 n=65/64`, `209 n=9/8`), no
new pstore, 2 wiphys / 6 interfaces, calibration `[SUCC]` both bands. Next: fix `write_ca_block()` to emit
`movt r0, value>>16` when `value > 0xFFFF` and re-run take3 unchanged; a rerun whose `E_EN0` bit29 reads CLEAR
is the first boot that actually tests `tiebreak.md`'s lever.

## ADDENDUM 21 (2026-10-06): the separation (take4) - the emitter fix LANDED and the 0x4C promotion HELD, yet the take still did not move; the residual is the SGI bank

ADDENDUM 20 closed on one line: emit `movt r0, value>>16` in `write_ca_block()` when `value > 0xFFFF` and re-run
take3 unchanged. That rerun is **take4**, and it's recorded in full as `gic-view.md` **ADDENDUM 20** ("the
separation"). It is the first boot to actually remove the banked PPI `0x1D` from the ENABLED set (`E_EN0` =
`0x0000FFFF`, bit29 CLEAR, where take3 read `0x2000FFFF`), and the `0x4C` promotion + the `0x40`/`0x45` mask
held to the end (`E_P4C` byte0 `0x00`, `E_EN2` = `0x5000`, `E_CTLR` RWP 0). The take still did not move: the
IAR ring stayed SGI-owned (`V2_ID` id 2, ring `0,1,2,2`) and `0x4C` stayed pending+untaken (`E_ACT` bit12
CLEAR, `E_OU0` = `0x8`, `E_ISP` bit12 SET). With the PPI gone, `0x4C` becomes the top *enabled* source
(`F_HPP` = `0x4C`), so the blocker has shifted from the PPI axis to the SGI (bank) axis. One line the `sep.md`
sec.2 table has no row for: `E_HPP` = `0x3FF` while `0x4C` is pending and enabled (later `F_HPP` = `0x4C`),
consistent either with a transient or with a group enable gate (`E_CTLR` = `0x1`, EnableGrp1 = 0), not
separated by the capture. The addendum carries the SGI note as a subsection: an SGI is not a competitor at the
SPI priority, it sits higher in the same queue, and the SGI bank is at priority byte `0x00`, so any pending IPI
out-ranks `0x4C` (priority `0x50`) - the next lever is the bank axis, not another SPI-priority write. Evidence
`build/register-dumps/exp/20261006-124348/`; blob `build/tmp/fw-patched/take4.bin` md5
`000a26af4d7b12e84d4191ae51dbad6d`, the reused v8 ko `3f87f1e9...`; adversarial verdict
`build/register-dumps/diffs/20261006T1246Z-vrun17/verdict.txt` (C1-C8 CONFIRMED); instrument verdict
`build/register-dumps/diffs/20261006T1243Z-vtool16/verdict.txt`. Health: bound held both lines
(`207 n=65/64`, `209 n=9/8`), no new pstore, 2 wiphys / 6 interfaces, calibration `[SUCC]` both bands.

## ADDENDUM 22 (2026-10-06): the bracket (take5) - the instrument is built and verified, the boot never ran

The three-instant bracket `bracket.md`, `brk3.md` and `sgi3.md` specified was built and verified as
**`take5`** (blob md5 `a5143c84a10b8e9182be70ba48a634a3`, the reused v8 ko `3f87f1e9...`), and the boot
produced NO sample. It is recorded in full as `gic-view.md` **ADDENDUM 21** ("the bracket"); this block is
the pointer.

The instrument rides take4 byte-for-byte and adds only read-only samplers: the same decisive words
(`GICC_RPR` `0x40160114`, `GICD_ISPENDR0` `0x40161200`, and `0x4C`'s group bit `GICD_IGROUPR2`
`0x40161088` at the ring and the gate-fall) deposited with the page sentinel at THREE instants of one boot
(`E5_*` the ring, `I5_*` the ISR's post-EOI via a new site at file `0x82f58`, `F5_*` the `0xcece` gate's
fall-through). It is verified: `vtool17` CONFIRMED the blob against its pin (two regenerations
byte-identical), the emitted-bytes check PASS 8/8 on take5 and still REJECTS take3, every new pad is
read-only with no forbidden CA anywhere, and the whole take5-vs-take4 delta is 175 bytes inside the three
pads, the one new site and the two retargeted tails, 0 outside. The boot is the failure: the cycle
`20261006-134010` returned at uptime 32 s and died at the completion marker (no `omo-drv1` line), so the
evidence dir `build/register-dumps/exp/20261006-134012/` was never created and the runner's capture gate
correctly selected nothing (`capture MISSING: no evidence dir at TS >= 20261006-134010`). `vrun18` labels it
**NO-SAMPLE, a HARNESS/RUN FAILURE** (`brk3.md` sec.4 row 11, extended: not even the module ran), so no
`brk3.md` row closes and take4's conclusion stands unchanged. Router healthy after recovery, no new pstore.
Evidence: none (the dir never existed); log `build/tmp/wifidrv1-art/run-take5.log`; verdicts
`build/register-dumps/diffs/20261006T1342Z-vtool17/verdict.txt` (instrument, CONFIRMED) and
`build/register-dumps/diffs/20261006T1353Z-vrun18/verdict.txt` (boot, NO-SAMPLE).

## ADDENDUM 23 (2026-10-06): the stuck-active (take6) - the gate 21a named is a PRIORITY-0 SOURCE HELD ACTIVE, retired by the EOI and never retired when the release guard never runs

ADDENDUM 21a named the gate but not the source; this block names the source's SHAPE and the two refuted
alternatives. It's recorded in full as `gic-view.md` **ADDENDUM 22** ("the stuck-active"); this block is the
pointer.

The state is read straight from ADDENDUM 21a's three-instant bracket, one boot (`exp/20261006-135935`): `E5_RPR`
(the ring) = `0x00000000`, `I5_RPR` (the ISR's post-EOI at file `0x82f58`) = `0x000000FF`, `F5_RPR` (the `0xcece`
gate's fall-through) = `0x000000FF`. So a **priority-0 source is ACTIVE** at the ring, and the ISR's own EOI
(file `0x82f52`) retires it. `HPPIR` tracks `RPR` exactly: `E_HPP`/`X_HPP`/`N_HPP` = `0x3FF` while `0x4C` is
pending+enabled+promoted, then `F_HPP` = `0x4C` the moment `RPR` idles, all in one boot. The take is not
attempted-and-lost; it is not attempted while the `0x00` epoch holds (strict `>`). Two alternatives are REFUTED:
the **SGI bank** cannot be the comparator at that instant (`E5_SGIP`/`I5_SGIP` = `0`, `F5_SGIP` = `0x20000000` only
post-EOI) and the **group enable** is not the gate (`E5_GRP2` = `F5_GRP2` = `0`, `E_CTLR` = `0x1`). The holder is a
word-0 SGI/PPI: `E_ACT` = `0` at every vein (no SPI active), and the firmware's own `set_prio` prices only SGI `0`
(file `0x8305C`) and SGI `2` (`0x83070`) at `0x00` - a candidate set, since no cell reads the word-0 bank. The
release that would end the epoch is counter-gated: `M2_PSR` = `0` means its own sample point never ran, so a
priority-0 source can be left active across the forward attempt.

The instrument that would NAME the source is `take6` (`pad_stk_fast`, read-only: `GICC_RPR` x16 + the word-0
`GICD_ISACTIVER0` `0x40161300` + `HPPIR`, sticky byte `STK_STICKY`), verified by `vtool18` CONFIRMED. Its boot
**stalled at the same `[4/7]` completion marker the take5 bracket did** (`run-take6.log`, 14:33Z, no evidence dir,
no `omo-drv1` line), so the source stays a candidate and the honest label is NO-SAMPLE, a harness/run failure.
Bounds: `0x40160114` = `GICC_RPR` is a GICv2 convention (0 image literals); the candidate set is derived from
`set_prio`, not sampled; the release story is named by disassembly, not measured. Evidence
`build/register-dumps/exp/20261006-135935/`; specs `build/tmp/inta-spec/{stk3.md,eoir.md,stuck.md}`; verdict
`build/register-dumps/diffs/20261006T1432Z-vtool18/verdict.txt`. Health: bound held, no new pstore, 2 wiphys / 6
interfaces, calibration `[SUCC]` both bands.

## ADDENDUM 24 (2026-10-06): the upstream arm B, continued - the PCIe RC design lands (spec + DT node) and the CRG forced probe reads the real part

ADDENDUM 18 landed the pinctrl skeleton, stage-2 row 2. This block is the pointer for the two arm-B
follow-ups recorded in `opensource/docs/UPSTREAM-PORT-PLAN.md` (status block, 2026-10-06).

**Stage-2 row 3 has a design.** `build/tmp/inta-spec/pcierc.md` (task `st_01a111be`) reads the vendor
`hi_pcie.ko` (disassembled with `lab/ko_disasm.py`) against the pinned DTS and fixes the DWC RC's shape: the
five-window layout (`dbi 0x10160000`, `misc 0x10161000` WRITE-ONLY, `cfg 0x50000000`, `mem 0x40000000`,
`io 0x48000000`; RC1 `+0x4000`/`+0x18000000`), the verbatim `iatu_rc` viewport table written to
`DBI+0x900+0x200*i`, the 14-step `hi_pcie_probe @0xb8c` init order, and the decision to port a from-scratch
host controller (`pci_scan_root_bus_bridge` + our `pci_ops` over the `cfg` window) rather than drop in the DWC
core. The companion DT node is already in the tree: `opensource/docs/soc/luofu-r116.dts` carries
`pcie0: pcie@10160000 { compatible = "hisilicon,luofu-pcie"; ... status = "disabled"; }` (committed at submodule
`fa11572` on `omo/phase22-hccaccept`). There is **no** `lab/luofu-pcie/`, no `luofu-pcie-ko` artifact and no
workflow matrix entry yet, so the design is the landed artifact and the skeleton is the next step.

**The CRG forced probe PASSED on the live part.** `build/tmp/inta-spec/crgprobe.md` (runner
`build/tmp/wifidrv1-art/run-crgprobe.sh`, ko md5 `b1a60c988c5dd704a500159cdc9483d2`, staged as `wifidrv1.ko`):
one serial `insmod force_probe=1` -> `rmmod`, the driver reading the live CRG `0x14880000` with no DT match and
ZERO writes, logging `[0x090] CRG_STATUS = 0x6a010008` (PLL lock mask `0x48000000` set, `rst_reason=4`) and
`[0x100] WDT_ISTATUS = 0x00000000`, then `FORCED probe PASS: 2/2 status regs read, 0 writes`; `boot_id`
unchanged, router healthy (2 wiphys / 6 interfaces / cal `[SUCC]` both bands). One non-fatal driver defect
recorded: the synthetic `luofu-crg` platform_device lacks a `.release`, so `rmmod` warns at
`drivers/base/core.c:1836` on every unload (a driver-source fix, out of scope for the receipt). Pointers: plan
status block `opensource/docs/UPSTREAM-PORT-PLAN.md`; ledger `mem-entries.md`; specs `build/tmp/inta-spec/
{pcierc.md,crgprobe.md}`.

## ADDENDUM 25 (2026-10-06): the take6f capstone - the vendor stack took the endpoint, so the ranked EOIR force was never spent (NO-SAMPLE BY CONSTRUCTION)

ADDENDUM 23 named the gate but not the source; `eoir.md` rank 1 then specified the instrument that would prove
the arc caps BY TRANSITION. That instrument is **`take6f`**, and it is recorded in full as `gic-view.md`
**ADDENDUM 23** ("the take6f capstone"); this block is the pointer.

The build is whole. `take6f.bin` (md5 `2c1ae79f892e922d0df0583f87fb1a2c`, 928 920 B, the reused v8 ko
`3f87f1e9...`) adds TWO pads to take6's retained read-only frame: the fast sampler `pad_stk_fast` (`GICC_RPR`
x16 + the ANDS sticky, the word-0 active bank `GICD_ISACTIVER0` `0x40161300` that would NAME the holder, and
`HPPIR`) and the 66-B EOIR force `pad_stk_eoir` (one 32-bit `GICC_EOIR` `0x40160110 <= 0x2`, then a post-force
`RPR`/`HPPIR` re-read into `STK_RPR1`/`STK_HPP1` - the capping proof's two halves). `vtool19` CONFIRMED it:
`--selftest` PASS with both frozen pins reproduced, `--check-emitted take6f.bin` 10/10 (take6 8/8, the force
absent from take6), two regenerations byte-identical, and the runner's gate fails CLOSED (no matching
`--eoir-id`, no launch). The boot is the strike-out: `hardware attach failed (no endpoint bound)`,
`regs=absent irq0=0 isr0=0`, the 207 line stayed the vendor's `hisi_pci_intx`, and the vendor glue took both
endpoints (`[PCIEL]request pcie intx irq 209/207 succ`) after uploading its own image at 13.16 s, so no BAR0
was ever mapped and NO pad executed - every cell reads `0x0`, every sentinel (`STK_SNT`, `E_SNT`, `X_SNT`,
`N_SNT`, `B_P3`, `B_P4`, `F_SNT`, `C_SNT`) reads `0x0`, none `0x50aa7e49`. `vrun20` labels it **NO-SAMPLE BY
CONSTRUCTION** (`layoutdiff.md` row 6, extended): the take6f hypothesis stays UNTESTED, and the `STK_ACT`
namer is still unread. The one line that moved is the HARNESS: the cycle's own gate decided FAIL
(`INSTRUMENT_GATE=NOT_HELD_NO_SAMPLE_BY_CONSTRUCTION`) while the capture hook kept the all-zero cells, so a
vendor-stack boot can no longer PASS on a loaded-but-unbound module. Evidence
`build/register-dumps/exp/20261006-152035/`; blob `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f...`; verdicts
`build/register-dumps/diffs/20261006T1519Z-vtool19/verdict.txt` (instrument, CONFIRMED) and
`build/register-dumps/diffs/20261006T1523Z-vrun20/verdict.txt` (boot, NO-SAMPLE). Health: bound armed and inert
(no endpoint, no IRQ), no new pstore, 2 wiphys / 6 interfaces, calibration `[SUCC]` both bands.

## ADDENDUM 26 (2026-10-06): the take6f re-run - the instrument RAN and NAMED the stuck id, the force HALF-LANDED, and the gate refused the wrong image slot

ADDENDUM 25 recorded `take6f` built and verified, then lost to the vendor stack (NO-SAMPLE BY CONSTRUCTION).
This block is the pointer for the SECOND take6f boot, recorded in full as `gic-view.md` **ADDENDUM 24** ("the
take6f capstone, RE-RUN").

The re-run came under the `race.md` sec.5 mitigation, and the mitigation held: `HIDE_MOVED=2`, the vendor
boot init could no longer `insmod` its Wi-Fi pair on EITHER slot, so `lsmod` shows `wifidrv1 73728 0` and NO
`hi5622v100_{plat,wifi}` - the endpoint was free and OUR module bound it (`request_irq(207, IRQF_SHARED)
rc=0`, `BAR0 base=0x40000000`, the 928 920 B image uploaded). The instrument RAN: every active pad deposited
its `0x50AA7E49` sentinel (`STK_SNT`, `E_SNT`, `F_SNT`, `B_P3`, `B_P4`, `C_SNT` all present). The 16 `GICC_RPR`
samples `STK_0..STK_15` read `0x00000000` (16/16), `STK_STICKY = 0x00000000`, and `STK_ACT`
(`GICD_ISACTIVER0` `0x40161300` word 0) = `0x00000004`. With the sticky zero, that bit NAMES the priority-0
holder: **SGI 2 (id `0x2`)** - a member of `eoir.md` sec.0's candidate set (a), and the same boot's v2 IAR ring
agrees (`V2_RING0/1/2/3 = 0,1,2,0x402`, `V2_ID = 0x00000402`). `STK_HPP = 0x3FF` at `RPR = 0x00` is the model:
`0x4C` pending (`E_ISP = 0x1021`) but not signalable while a priority-0 source runs.

The ranked EOIR force fired with the id the SAME boot had named (`0x2`), so it retired a genuinely ACTIVE
interrupt and DROPPED the running priority: `STK_RPR1 = 0xFF` after `dsb sy`, against `STK_15 = 0x00` - the
capping proof's first half, BY TRANSITION. The second half did not land inside the pad (`STK_HPP1 = 0x3FF`,
not `0x4C`), but it is present one site over in the SAME boot: Site F reads `F_HPP = 0x0000004C` with
`F_ISP = 0x1021`, `F_ACT = 0x0`. The force is recorded as HALF, not MISSED.

The result is NOT certified, because the cycle boot attached `mtd13 "rootfsa"` (the stock 2.4.15 image, from a
sibling lane's crash-reboot) instead of `mtd14 "rootfsb"`, so the runner's image gate refused it:
`INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT`, `TAKE6F RESULT: FAIL`. The cells are a GATE-REFUSED READ - the
first complete take6/take6f reading of the arc - and the take6f hypothesis moves from UNTESTED to
MEASURED-BUT-UNCERTIFIED. Restoring slot B and re-running the SAME take6f is what would certify the naming and
the force; the builder already refuses a bare or mismatched `--eoir-id`, so a wrong-id force cannot be spent by
accident. The retained take4 frame still reads as take5 left it (`E_EN0 = 0xFFFF`, `E_EN2 = 0x5000`,
`E_P4C = 0xF050F000`, `E_CTLR = 0x1`), and the mandatory bound armed and hit with no residue
(`IRQ_DISABLED_BOUND irq=207 n=65 bound=64`, `irq=209 n=9 bound=8`, `SUPERVISOR DONE quiesced=0 state=IDLE`).
Evidence `build/register-dumps/exp/20261006-154734/`; blob `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f...`;
verdicts `build/register-dumps/diffs/20261006T1557Z-vrun21/verdict.txt` (boot, GATE-REFUSED) and
`build/register-dumps/diffs/20261006T1545Z-vtool20/verdict.txt` (runner, CONFIRMED). Health: 2 wiphys / 6
interfaces, calibration `[SUCC]` both bands, stock md5 unchanged, no new pstore, boot_id moved.

## Verification - vrec19 (2026-10-06): arm A's ADDENDUM-24 record is QUOTED, not merely cited

Adversarial record-check of arm A's ADDENDUM-24 record, host-only and read-only (task `st_01a111f1`, verdict
`build/register-dumps/diffs/20261006T1605Z-vrec19/verdict.txt`). Four claims CONFIRMED: (C1) this README's
`## ADDENDUM 24 (2026-10-06): the upstream arm B, continued - the PCIe RC design lands (spec + DT node) and the
CRG forced probe reads the real part` heading (line 575) and its intra-block pointers resolve; (C2) the
`gic-view.md` ADDENDUM 24 heading `the take6f capstone, RE-RUN` (line 3989) is quoted verbatim - a DIFFERENT
record from C1; (C3) that block's `The take6f branch table, row by row` (rows 1-4: the sticky/`STK_ACT` naming,
the `0xFF` no-`RPR` row, the `STK_SNT != 0x50aa7e49` NO-SAMPLE row, and the `STK_15`/`STK_RPR1`/`STK_HPP1`
HALF-LANDED row) is reproduced byte-for-byte; (C4) the `mem-entries.md` line-395 entry binds `gic-view.md`
ADDENDUM 24 + `README.md` ADDENDUM 26 together and does NOT mislabel this arm-B block as its source. The
same-numbered collision (README 24 = arm B, gic-view 24 = take6f re-run) is kept distinct, as the sources do.
No device cycle, no commit, no push; the two CA writes and the IAR/RC reads named in the hard rules were not
made.

## ADDENDUM 27 (2026-10-07): the official take6f capstone - the SLOT GATE lands (the runner refuses a wrong-slot boot before anything is armed), the reboot lever refutes itself, and the naming plus the half-force stand as ADDENDUM 24 left them

ADDENDUM 26 recorded the second take6f boot, READ but refused on the wrong image slot. This block is the pointer
for the official capstone, recorded in full as `gic-view.md` **ADDENDUM 25** ("the official take6f capstone");
note the numbering gap, `gic-view.md` 25 is this block's source while this README already spent 25 on the
NO-SAMPLE capstone.

The SLOT GATE is built and verified. It sits in `build/tmp/wifidrv1-art/run-take6f.sh`
(md5 `41272389eb340815f0397230de1183f5`) as a block inside `EXP_PREFLIGHT_CMD`, which `tools/exp.sh` runs at
`[0/7]` before the watchdog is armed and before anything is staged. It keys on
`/sys/class/ubi/ubi0/mtd_num` plus that `mtdN`'s name in `/proc/mtd` (`13` -> `rootfsa` stock 2.4.15,
`14` -> `rootfsb` custom 2.5.24), cross-checks the boot's own `ubi0: attached mtd1[34] (name "rootfs[ab]"`
dmesg line, and carries four refuse arms. `vtool21` CONFIRMED all seven claims
(`build/register-dumps/diffs/20261007T0044Z-vtool21/verdict.txt`), including the 7-arm decision table and the
fact that a host-side refusal costs ZERO device contact (a shimmed `ssh` shows `ssh_calls=0`).

The live refusal is on the record. The runner's own preflight string, run read-only against the box, printed
`preflight booted_slot=mtd13 name=rootfsa dmesg_attach=13` and then
`PREFLIGHT_FAIL_wrong_image_slot: booted_slot=mtd13:rootfsa want=mtd14:rootfsb (the CUSTOM image)`,
`PREFLIGHT_RC=1` (`build/register-dumps/exp/20261007-004820-slotrefusal/slotgate-refusal.txt`).

The reboot lever REFUTED itself, and that is the new mechanism. The single permitted boot-id-gated reboot was
taken and moved `boot_id` (`e8e60346-...` -> `f47bbc77-...`) while the box came back on `mtd13 "rootfsa"`
again. The reason is measured: `fw_printenv` shows `bootcmd=mtd read kernel${bootflag} ${loadaddr}` with
`bootflag=a`, so u-boot loads `kernela` -> slot A on every boot while that variable says `a`, and the in-boot
`/sys/devices/platform/sysenv/boot_reg` (reads `10`) is overwritten by the sysenv driver at ~16 s on BOTH
slots, which is why `race.md`'s "0x10 -> rootfsb" map was a coincidence. Moving to slot B means writing
`bootflag=b` (the B env block into mtd3+mtd4 per `build/custom/FLASH-PLAN.md`), which is a flash/env mutation
and was raised as a decision, not taken.

The naming and the force are unchanged from ADDENDUM 24 and remain MEASURED-BUT-UNCERTIFIED: `STK_STICKY =
0x00` with `STK_ACT = 0x00000004` names **SGI 2 (id `0x2`)**, and the ranked EOIR force dropped the running
priority (`STK_RPR1 = 0xFF` against `STK_15 = 0x00`) with its second half one site over (`STK_HPP1 = 0x3FF`,
`F_HPP = 0x4C`). Both come from the GATE-REFUSED boot, so neither is certified here; a slot-B boot that
re-runs the SAME instrument and seals `--eoir-id 0x2` is what certifies them. Verdicts
`build/register-dumps/diffs/20261007T0044Z-vtool21/verdict.txt` (slot gate, CONFIRMED) and
`build/register-dumps/diffs/20261007T0050Z-vrun22/verdict.txt` (boot, GATE-REFUSED, T6-1 with the force HALF).
No device cycle ran this session, so the bound was not spent; the only device mutation was the boot-id-gated
reboot. Health: `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5
`0e530b976d5a20e87358671f1a577695` unchanged, no new pstore.

## ADDENDUM 27 (2026-10-07): the official take6f capstone, recorded and verified - the slot gate + the reboot-lever refutation + the standing MEASURED-BUT-UNCERTIFIED naming/force

ADDENDUM 26's re-run came up on the wrong image slot; ADDENDUM 25 is the official capstone that lands the SLOT GATE,
refutes the reboot lever, and leaves the SGI-2 naming and the half-force exactly where it found them. This block is
the pointer, and the phase's **vrec20** record verification (no device cycle, host-only) files the verdict.

**Verified as recorded.** `build/register-dumps/diffs/20261007T0053Z-vrec20/verdict.txt` (task `st_01a113da`) reads
ADDENDUM 25 against its three pointers and CONFIRMS the record with nothing inverting a load-bearing claim: the gate
lives in `build/tmp/wifidrv1-art/run-take6f.sh` (md5 `41272389eb340815f0397230de1183f5`) as a block inside
`EXP_PREFLIGHT_CMD` at `tools/exp.sh` `[0/7]` (before the watchdog and before staging), the live refusal is exact
(`PREFLIGHT_FAIL_wrong_image_slot: booted_slot=mtd13:rootfsa want=mtd14:rootfsb`, `PREFLIGHT_RC=1`), the one
boot-id-gated reboot moved the id (`e8e60346-...` -> `f47bbc77-...`) yet returned to `mtd13 "rootfsa"` with
`fw_printenv` naming `bootflag=a` as the lever, and NO new cell was added - Addendum 24's `STK_STICKY = 0x00` /
`STK_ACT = 0x04` (SGI 2 / id `0x2`) and the force's RPR drop (`STK_RPR1 = 0xFF` vs `STK_15 = 0x00`; second half
`STK_HPP1 = 0x3FF` / `F_HPP = 0x4C`) stand MEASURED-BUT-UNCERTIFIED. Bounds carried: only the REFUSE arm is a live
read (the PASS arm is a `vtool21` fixture), `bootflag=a` is read-not-written (mtd3/mtd4 unexercised), and the reboot
is evidence of a negative. Hard rules held: no write of CA `0x400392f0`/`0x40039af0`, no read of `0x10161000`, no
host read of the ack IAR `0x4016010c`, NO device cycle run, no reboot, nothing staged, no commit, no push. Router as
the addendum left it: `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5 unchanged, on
`mtd13` stock slot. Next: the flash/env lever (`bootflag=b`, its own gate + its own dumps) -> reboot -> confirm
`ubi0: attached mtd14` -> re-run the SAME `take6f` sealed with `--eoir-id 0x2` to certify the naming and the force.

## ADDENDUM 28 (2026-10-07): the bank/group gate - `RPR` is drop-able but not the comparator, so the residual moves to the group/bank layer; the one-boot read set (take7) decides rank 1 vs rank 2 vs rank 3, and the group-enable write stays deferred

ADDENDUM 27 recorded the official capstone: the force dropped `RPR` to `0xFF` yet `HPPIR` stayed `0x3FF`, while
the same boot's Site F read `F_HPP = 0x4C`. This block is the pointer for the arm-A answer to "which gate holds
`0x4C` now", recorded in full as `gic-view.md` **ADDENDUM 26** ("the bank/group gate"); note the numbering
slip, `gic-view.md` 26 is this block's source while this README already spent 26 on the take6f re-run.

The new negative: the comparator sits BELOW the `RPR` register. `GICC_HPPIR` is a THREE-WAY signature (`0x3FF` =
nothing pending, or the top pending's group is disabled in the CPU interface, or the top is Group 0 read
Non-secure; ARM IHI0048), so two specs rank the surviving layers: rank 1, the force's ID-MISMATCHED EOIR (bare
`0x2` written where the firmware writes the full IAR word `0x402`) left the priority-0 epoch unretired; rank 2,
an `0x4C` GROUP stamp against `EnableGrp1 = 0`; rank 3, the SGI/PPI ACTIVE BANK (`STK_ACT = 0x4` = SGI 2).
`F_HPP = 0x4C` in the SAME boot rules out any static config gate, and the image authors no `IGROUPR` write at
all, so the stamp is inherited from reset and read only in OTHER boots.

The deciding boot is take7 (design only): the capstone frame byte-for-byte plus the corrected EOIR
(`GICC_EOIR 0x40160110 <= 0x402`, emitted only if a pre-read shows `RPR = 0x00` with `ISACTIVER0` bit 2 set) and
the post-force read set R1..R14 (`TG_ACT0` bit 2, `TG_GRP2` bit 12, `TG_GRP0`, the CTLR pair, `TG_TGT`, the PMR/
ABPR pair). The branch table's load-bearing split: `STK_HPP1 = 0x3FF` with `GRP_I2` bit 12 `= 1` and `GRP_CTLR`
bit 1 `= 0` CONFIRMS the group gate; `STK_HPP1 = 0x3FF` with the group enabled and `GRP_I2` bit 12 `= 0` PROMOTES
the bank; `TG_HPP = 0x4C` with the corrected force CLOSES rank 1. The group-enable write (`GICC_CTLR -> 0x3`)
is deliberately deferred because it admits the already-pending `0x40`/`0x45`/`0x4C` into the vendor ISR at once,
and the bank quiesce is deferred last because it risks the Wi-Fi doorbell. Specs `build/tmp/inta-spec/{bankgate.md,bg2.md}`
(tasks `st_01a113ee`/`st_01a113ef`, design only); evidence base `build/register-dumps/exp/20261007-010906/`.
Hard rules held: no write of CA `0x400392f0`/`0x40039af0`, no read of `0x10161000`, no host read of the ack IAR
`0x4016010c`/AIAR `0x40160120`, no `GICD_SGIR` read, no device cycle, nothing staged, no commit, no push. Router
healthy and untouched: `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5 unchanged, on
slot B. Next: build take7, run the observation variant once, read `TG_GRP2`/`TG_ACT0`, and only then gate the
rank-2 group enable (or, if the group is enabled and `0x4C` is Group 0, the rank-3 bank quiesce).
