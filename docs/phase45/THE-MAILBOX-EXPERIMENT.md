# THE MAILBOX EXPERIMENT - the next live run's specification (phase 45, 2026-10-04)

Merges the three verified phase-45 lane reports into one runnable specification for the next live
experiment against the luofu (`hi5622v100`) mailbox block. Every section is attributed to the lane
that grounds it. Read this file alone to run the experiment; follow the links for the evidence.

Lane reports (the three inputs):

| lane | file | task | what it contributes here | lane status |
| --- | --- | --- | --- | --- |
| L1 - hi1105 PCIe sibling | [`hi1105-mailbox-irq.md`](hi1105-mailbox-irq.md) | `st_01a1073e` | the register, its bit, the polarity, the arm call-chain | PASS |
| L2 - hi3881 SDIO sibling | [`hi3881-mailbox-irq.md`](hi3881-mailbox-irq.md) | `st_01a1073f` | the arm order and the "source-class enable, not per-id" principle | PASS |
| L3 - luofu reconciliation | [`reconcile-luofu.md`](reconcile-luofu.md) | `st_01a10740` | the address mapping onto the luofu, the value, the observable ladder | FAIL - one wrong label (`plat.ko` "file = `.text` + `0x38`"); all luofu facts used here survive |

Verification of the three reports: [`VERIFICATION.md`](VERIFICATION.md) (task `st_01a10757`). Its
verdict is reproduced in handoff form in section 5 below.

Scope note: this document is a **plan**. No register in it has been written; the run it describes
has not been executed. Values marked "expected" are predictions with the source named.

---

## 1. Top candidate register + value, and its grounding

**Write `CA 0x400392e8` (BAR0 `0x3f12e8`) to the value `0x20`.**

| field | value |
| --- | --- |
| CA address | `0x400392e8` |
| host BAR0 view | `0x3f12e8` (inside the port's existing `OMO_IO_WIN`; no new mapping) |
| the port's name for it | "glue chn_res" / `OMO_CHN_RES` (`opensource/lab/wifidrv1/wifidrv1.c:170`) |
| family name | `PCIE_CTRL_RB_HOST_INTR_MASK` (ctrl-rb offset `0x2E8`) |
| value to write (top form) | **`0x20`** - bit 0 = 0 (H2D doorbell unmasked), bit 3 = 0 (D2H unmasked), bit 5 = 1 |
| polarity | `1 = masked, 0 = enabled` |
| alternative minimal form | `v & ~0x9u` (clear exactly bits 0 and 3; from the observed reset `0x3ff` this yields `0x3f6`) |
| the port's current write | `v & 0xfffffc20` = `0x20` from the reset `0x3ff` - **already lands on the top value** |

### 1.1 Grounding

- **Register identity and bit polarity - L1.** `PCIE_CTRL_RB_HOST_INTR_MASK_OFF = 0x2E8`, bit 0
  `host2device_tx_intr_mask`, in `platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` L383-L404 (pinned
  commit `297b33b07b56adb3fa53baf618fdbc16ad9fd447`); the polarity is stated in code as
  `/* mask:1 for mask, 0 for unmask */` at `pcie_chip_mp17c.c` L261; the unmask RMW is
  `oal_frw_msg_int_unmask_mp17c` (L466-L483), setting `host2device_tx_intr_mask = 0` and
  `device2host_rx_intr_mask = 0`. L1's own flagged caveat: the older eDMA header disagrees on the
  doorbell *bit numbering* but agrees on the offsets `0x2D4`/`0x2E8`; the mp17c header is the one
  the hi1105 code writes, so bit 0 stands.
- **The luofu address - L3.** The luofu message block is the same ctrl-rb at base `0x40039000`
  (L3 §3), so the enable sits at `0x40039000 + 0x2e8 = 0x400392e8`. L3 re-derived the vendor's own
  RMW on it: `pcie_ete_chn_res` (`plat.ko` symbol `0x07490`) does `ldr r8,[r3,#0x2e8]` /
  `and r8,r8,sb` with `sb = 0xfffffc20` (`movw sb,#0xfc20` `0x074a4`, `movt sb,#0xffff` `0x074a8`) /
  `str r8,[r2,#0x2e8]` (`0x07520`). The doorbell this gates is the *same* `0x400392d4` the port
  already rings, whose observed values `1` and `8` are exactly bit 0 and bit 3 of the sibling's
  `0x2D4`.
- **Why `0x20` and not just `v & ~0x9`: L3 §1/§6.1.** `0x20` is the vendor's own live value
  (`reg_all.txt`: `400392e8 = 20`), and it is exactly what `0x3ff & 0xfffffc20` produces - so the
  top form is the *natural* value for the register, and its bit 5 set matches the vendor's resting
  state (`pcie_msg_irq_mask` on the sibling revisions L1 describes).
- **Principle - L2.** The enable is a **source-class** bit, not a per-message-id enable; the
  doorbell and the enable are different registers (hi3881: `WRITE_MSG` `0x24` vs `INT_ENABLE`
  `0x09`, `oal_sdio.h:39-55`). That is the shape `0x400392e8` (enable) vs `0x400392d4` (doorbell)
  already has.

### 1.2 The key prediction, and why the experiment is still worth running

L1 and L3 both predict, independently, that **the port's existing write already satisfies this
register**: `0x3ff & 0xfffffc20 = 0x20`, the vendor's live value, with bit 0 already unmasked. If
that is true, `0x400392e8` is *named and cleared*, not *missing*, and the gate is downstream of the
ctrl-rb (delivery to line `0x4C`) rather than at the mask. **The experiment exists to falsify or
confirm that prediction with one read**, not because the write is expected to change anything.

Readback expectation: `0x400392e8` reads `0x20` in a normal-op vendor boot and should still read
`0x20` after the port's own write. Nothing in the firmware image writes `+0x2e8` (L3 §6.1: no
little-endian literal for it anywhere in `FIRMWARE.bin`, no `+0x2e8` field in the dispatcher ctx,
the boot helpers touch only `+0x41c`/`+0x424`/`+0x428`). L3 §8 labels the per-bit wiring on the
luofu an *inference* (offset and bits established; the bit-0 gate inferred from the doorbell/ack bit
values) - treat it as the strongest available hypothesis, not a proven fact.

### 1.3 Second candidate (labelled guess) - L3 §6.2

`CA 0x40039ae8` (BAR0 `0x3f1ae8`), the **twin PCIe1 ctrl-rb's** host intr mask, value
`old & ~0x9u` (from its vendor reset `0x3ff` this is `0x3f6`). Two conflicting facts bound it: the
vendor boots fine while leaving the twin at `0x3ff` (MASKED), which under the sibling polarity means
it does not block the vendor - consistent with the record's verified claim that both endpoint
windows decode the same doorbell register. It is offered only because it is the one family enable
register in the host-visible window **the port has never written**. If the window alias holds the
write is a no-op; if any part of the port's TLP path is instead decoded through the twin's file,
clearing bit 0 is exactly the sibling's arm. Run it only after candidate 1 is excluded, and only in
the minimal `& ~0x9` form so the twin's mac/phy masks are not disturbed.

---

## 2. Write order relative to the existing takeover cycle

The ordering is L1's call-chain placement (`oal_firmware_msg_download_pre` unmasks **when the
message service starts**, `pcie_firmware_msg.c` L486-L515 -> L442-L447 -> the chip callback), applied
to the port by L3 §6.1. It is a placement rule: the enable must be held in force at doorbell time,
not merely written at probe.

Existing port cycle (unchanged - `tools/exp.sh`, vendor modules hidden after the watchdog is armed):

1. arm the device watchdog, hide the vendor modules, load the port driver
2. the port's pre-release write, inside `omo_ete_program`, already sets `0x400392e8 = 0x20`
3. **release the firmware** (the release write) and let the firmware's own boot init run
4. the firmware's mailbox init helpers run: `0x86f3e`/`0x86f44` on base `0x40039000`,
   `0x86f4a`/`0x86f50` on base `0x40039800` (L3 §3 fact 4)
5. ring the first doorbell (`0x400392d4` bit 0)

**Insertion: between step 4 and step 5** - after the firmware's boot init has finished and
immediately before the first doorbell. This matches L1's "unmask when the message service starts"
and it is the only placement that can observe a mask the firmware re-armed during boot.

The inserted block, in order:

1. `v = omo_rd(omo_msg, OMO_CHN_RES)` - read `0x400392e8`; expect `0x20` (prediction, §1.2).
2. `omo_wr(omo_msg, OMO_CHN_RES, v & ~0x9u, "H2D/D2H doorbell intr unmask (bits 0,3)")` - the
   sibling-faithful minimal unmask.
3. `omo_wr(omo_msg, 0x2e8, 0x20, "host intr mask = vendor live 0x20")` - optional readback pin of
   the top form from §1.
4. read back and log `0x400392e8`, and also read the two instruments the port has never touched:
   `0x400392e4` (raw status) and `0x400392ec` (post-mask status), to establish their pre-doorbell
   baseline.
5. only then ring the doorbell.

**Never** write the adjacent `CA 0x400392f0` (the intr-clear - the record's forbidden ack
register). L3 §6.1: the sibling arm-order's "clear stale status" step is the **device's own** write
(firmware `0x818b8`), never the host's. The clear is on the device side of the contract.

**L2's arm order** - `clear stale status -> write enable -> open state -> open line`
(`oal_sdio_host.c` L1099-L1124) - is the structural form of this insertion; on the luofu the
"clear stale status" step is the firmware's, the "write enable" step is the block above, and "open
state"/"open line" are the port's existing release + `omo_ete_program` path. L2's own note: hi3881 is
SDIO with a function-register file, luofu is PCIe with a memory-mapped window - this is a
*resemblance*, which is exactly why the luofu follows L1's PCIe layout and only borrows L2's order.

---

## 3. The observable that decides success

The deciding observable is a **ladder**, in order of how much each step proves (L3 §6.3). The
experiment succeeds at the highest rung it reaches.

| rung | observe | means | grounding |
| --- | --- | --- | --- |
| **R0 - the falsifier** | `0x400392e8` read *before* the doorbell | `0x20` confirms the prediction (mask already armed; gate is downstream). Anything else is itself the finding: something between the port's write and the doorbell re-armed the mask | L3 §6.1/§6.3; L1 §5 |
| **R1 - the separator** | raw status `0x400392e4` (BAR0 `0x3f12e4`) bit 0 latches 1 after a doorbell write, and stays until the firmware's clear | the doorbell event reached the ctrl-rb interrupt stage. **Never measured before.** This splits the phase31-35 gate in half without a new window | L3 §6.3 obs. 1; offset from L1 §2 |
| **R2 - polarity on the live part** | post-mask status `0x400392ec` bit 0 = 1 for the same event, with mask bit 0 = 0 | confirms the `1 = masked, 0 = enabled` reading live | L3 §6.3 obs. 2; polarity from L1 §2 |
| **R3 - the canonical proof** | the dispatcher signature: ack `0x400392f0` pulses 1 (firmware `0x818b8`), out[0] `0x40039010` clears (`0x818be`), doorbell `0x400392d4` re-arms with 8 (`0x818c4`), then D2H announce bits land in out[1] `0x40039014` | **success**: the mailbox is consumed and the device answers. Downstream, DR deposits appear (`omo_dr_watch` events > 0) | L3 §6.3 obs. 3, `phase32/THE-GATE-MAP.md` §1 |

**Decision rule.** R3 = the experiment worked; record it and move to the next gate. R1 reached but
R3 not = the event is in the mailbox and the missing gate is between the ctrl-rb and the dispatcher.
**R1 stays 0 after a doorbell that reads back as consumed** = the event never reaches the ctrl-rb
interrupt stage at all, and **no mask value can open it** - the gate is the TLP/decode path, not the
enable. That negative result is the experiment's most valuable outcome: it retires the entire
enable-register hypothesis and redirects the search to decode, which is what L1's and L3's shared
prediction (§1.2) already expects.

---

## 4. Safety constraints that apply

Each constraint is attributed; the last block is repo-wide (AGENTS.md anti-patterns) and applies
whether or not the experiment runs.

**From L1 (`hi1105-mailbox-irq.md`):**

- L1 is **static web research only** - it names the register and value; it must not be read as
  license to touch the device. Its §4 caveat fixes the *bit numbering* (mp17c wins over the older
  eDMA header); do not let the eDMA values (`1<<4/1<<5/1<<6`) leak into the write.
- The arm is an **RMW**, never a blind store: `oal_frw_msg_int_unmask_mp17c` reads, clears two bits,
  writes back. Preserve the other status/mask bits the vendor holds (that is what `& 0xfffffc20` and
  `& ~0x9` both do).

**From L2 (`hi3881-mailbox-irq.md`):**

- **Source-class enable, not per-message-id.** Do not invent a per-id enable and do not re-arm per
  message: the enable is written once and left. Adding per-id enable writes would be touching
  registers that have no such function.
- The SDIO candidate (`0x40039008/0x40039009`) is **resolved against** for the luofu (L3 §5) - do
  not spend a run on it.

**From L3 (`reconcile-luofu.md`):**

- **Never write `CA 0x400392f0`** (BAR0 `0x3f12f0`). It is the intr-clear; its bit 0 is the
  firmware's own `host2device_tx_intr_clr` (`0x818b8`). Host ownership of that write is what the
  sibling arm order forbids.
- The twin `/` window write (candidate 2) is a **guess, minimal form only** - `& ~0x9`, so the twin's
  mac/phy masks (which the vendor deliberately leaves set) are not disturbed. Run it only after
  candidate 1 is excluded.
- L3 carries a **FAIL verdict** (`VERIFICATION.md` §3): its `plat.ko` mapping label "file = `.text`
  + `0x38`" is wrong - the quoted instructions sit at the bare symbol offset, not `+0x38`. Every
  luofu address this document uses is grounded in L3's **§7 verification ledger** and in
  `reg_all.txt`/dmesg values, all of which VERIFICATION.md re-derived as correct. **When
  disassembling `plat.ko` for this experiment, use the repo's own reader**
  (`opensource/lab/ko_disasm.py`, `sec.data()[val:val+size]`, no bias, ARM mode) - **not** the `+0x38`
  rule, which points at unrelated instructions.
- The firmware offsets in L3 are printed as **file** offsets; ignore its stated `+0x40000` runtime
  rule for reading them (VERIFICATION.md §3.3: an internal inconsistency, the printed file offsets
  are correct).

**Repo-wide (applies to the run; AGENTS.md):**

- **Never write CA `0x400392f0`** (repeated here because it is also a record anti-pattern).
- **Never read the RC misc window `0x10161000`** - a read-only `devmem` there PANICKED the box.
  Every address in this document is inside the host-visible BAR0 window; measure through the
  endpoints' BAR0/BAR2 only.
- **Never `rmmod` the vendor modules.** The cycle hides them; it does not remove them.
- The device cycle runs **DETACHED**, with the **watchdog armed before the vendor modules are
  hidden** (`tools/exp.sh` does this; never run it foreground - a dead agent strands the router in
  takeover config with Wi-Fi down).
- **Snapshot calibration first** (`tools/wifi-cal-snapshot.sh`) and check `MANIFEST.sha256` before
  any restore; restore is dry-run by default.
- Restore with `tools/restore-now.sh`: unhide the vendor mods, remove loaders, then reboot. Leave
  the router healthy - 2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands, no `.omo-off`
  leftovers.
- Evidence lands in `build/register-dumps/exp/<UTC-timestamp>/`, never in the repo root.

---

## 5. Lane provenance and verification, in handoff form

VERIFICATION.md (`st_01a10757`) verdicts, and what each means for this specification:

- **L1 `hi1105-mailbox-irq.md` - PASS.** All quoted lines re-fetched from the pinned commit and
  string-matched; the arm register, its bit and the polarity independently re-derived. **Used for
  §1's register and value and §2's placement rule.**
- **L2 `hi3881-mailbox-irq.md` - PASS.** All four load-bearing files re-fetched; every quoted line
  and line number reproduced. **Used for §2's arm order and §1's source-class principle.**
- **L3 `reconcile-luofu.md` - FAIL on one label.** The `plat.ko` "file = `.text` + `0x38`" mapping is
  wrong (the quoted instructions are at the bare symbol offset). Everything else re-derived is
  correct: all 22 firmware instructions, all six firmware literals, both helper tables, the literal
  scan, the `reg_all.txt` and dmesg values, and the substantive `plat.ko` claims. One imprecision
  noted: "the 20261002 runs read `0x00000000`" is true only for the five early runs. **Used for §1's
  luofu address and value, §2's insertion point, §3's observable ladder, §4's forbidden register -
  all of which lie in the parts VERIFICATION re-derived as correct.** The defective label is called
  out in §4 and is not used anywhere in this plan.

Reproduction (from VERIFICATION.md §4), to be run before trusting any address in this file:

```bash
cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2
./pyenv/Scripts/python.exe opensource/build/verify_p45.py   # exit 0; 45 PASS / 7 FAIL
./pyenv/Scripts/python.exe opensource/lab/ko_disasm.py build/tmp/hi5622v100_plat.ko pcie_ete_chn_res pcie_ete_intr_init
```

The 7 FAILs are the deliberate negative check - exactly the `plat.ko` rows disassembled under the
report's own wrong `+0x38` label.
