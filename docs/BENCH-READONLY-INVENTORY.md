# BENCH-READONLY-INVENTORY - attended read-only register/table inventory (LSW, PIE, CRG, SYSCTRL)

**Status:** tool + mock tests built and tested host-only on 2026-11-18. The tool has **never
been executed against the router**, and this document makes no hardware-measured claim. The
first attended run is the next action; everything below is the procedure for that run.

**Scope:** one attended, human-supervised, READ-ONLY bench session on the stock vendor system
that captures the register/table baseline the LSW, PIE and clock work needs. The tool is
parent:tools/bench-inventory.sh, its mock suite is parent:tools/tests/bench-inventory.test.sh,
and the machine-readable evidence lands under parent:build/ (gitignored).

**Related specs (this directory):** [LSW-SPEC.md](LSW-SPEC.md) (windows, port/queue/qid
model, tables, D0/D1b), [PIE-DATAPATH.md](PIE-DATAPATH.md) (ring/channel register model, P3
inventory), [MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) (CRG status/control words and SYSCTRL
cells), [SELECTOR-0x21.md](SELECTOR-0x21.md) (boot selector cell). Parent documents:
parent:BENCH-FACTS.md (physical invariants), parent:RUNBOOK.md (preconditions, recovery),
parent:LESSONS.md (durable rules), parent:STATUS.md (live state).

**Evidence markers** are the project convention (REMAINING-DRIVERS.md section 0.1): **[V]**
verified artifact or recorded measurement, **[I]** inferred/planned, **[U]** unknown, **[X]**
blocked externally. This document is mostly [I] by construction: the tool exists [V], its
tests pass [V], but the inventory itself is [U] until the attended run.

---

## 1. Why a read-only inventory, and why now

Bench fires are **stopped** and there is no bootable 6.18 target, but the three open bench
gates do not need either:

- **PIE stage P3** (PIE-DATAPATH.md section 11.2) is explicitly a read-only inventory of the
  PIE window taken on the vendor system: the draft driver (patch 015) refuses to probe because
  this inventory does not exist. P3 is the gate before any ring/DMA/NAPI work.
- **LSW D0/D1b** (LSW-SPEC.md sections 11 and 12.3) needs the W1 baseline: what the vendor
  probe left in dp/eps/eqm/opp before a clean-room write set can even be written on paper.
- **CRG rates/parents** (MISC-SOC-BLOCKS.md section 5.5) needs one readback of the PLL status
  and the two PLL control-word pairs; patch 019 models the equation but explicitly says the
  on-board rates are never measured. The same run can capture the clk tree summary if debugfs
  is available.
- **SYSCTRL/SYSENV** (MISC-SOC-BLOCKS.md section 6.3) cells 0xc00/0xc08/0xc10 are named in
  the specs with one writer each, but only the selector has been read live in this project;
  a side-by-side sysfs/devmem read closes the upper-24-bit question of the selector [U].

Reading is safe on the stock vendor system because the vendor drivers for all four blocks are
loaded and running; we are sampling state that already exists, not touching the blocks. The
hard constraint is that a READ is still a device action: it needs the recovery door armed, the
console captured, and a human present (BENCH-FACTS.md sections 3-6, LESSONS.md rules 15/17/19).
A read of a wrong MMIO window can hang the box; the RC misc window 0x10161000 is proof of that
(HAZARDS.md section 9). The allowlist exists because of that one measured hazard.

---
## 2. Safety contract (read this before running anything)

1. **READ-ONLY by construction.** The tool has no write path. Every device command is
   generated from one static read-only template, every address comes from a static table,
   and the generated script is re-checked for forbidden tokens before it is sent. Addresses
   are 32-bit aligned reads only. There is no devmem write, no mtd write, no sysfs store, no
   rmmod/insmod/modprobe, no reboot, no mount, no redirection on the device.
2. **Never the RC misc window.** 0x10160000-0x1016ffff (which includes 0x10161000 and
   0x10165000) is refused by an explicit denylist *and* by the window list. A read there
   panicked the box. The CA address 0x400392f0 is likewise out of scope.
3. **Known-writer rule.** Every allowlisted cell has a named writer in the specs: the vendor
   LSW/PIE drivers, the vendor CRG/watchdog/clock code, the loader + hi_ksysenv (selector),
   the vendor SMP code (0xc00) and the vendor machine reset code (0xc10). "No driver writes
   it" is not enough to read a cell (LESSONS.md rule 5, HAZARDS.md section 8).
4. **The gates are the run.** Attended, bench link PASS, recovery door READY, read-only UART
   capture running (or explicit --no-uart), healthy stock vendor system. Local gates run
   **before any SSH contact**: if one fails, the router is never touched. The stock-health
   gate runs before any register read: if it fails, nothing is read.
5. **Recovery is physical.** The human must be able to power-cycle the router and hold RESET
   through the counter, with the TFTP recovery server armed (parent:tools/recovery-card.sh).
   An agent cannot recover the box alone.
6. **UART is READ-ONLY.** Capture with parent:tools/uart-capture.sh only; never type to the
   tty, never open it read-write, no screen/minicom/picocom (BENCH-FACTS.md section 3).
7. **One boot, one snapshot.** The tool reads boot_id before and after the reads and marks the
   inventory INVALID if the box rebooted mid-run. Do not combine runs from different boots.
8. **Mismatches are findings.** Non-zero expected-value checks are data for the spec owners,
   not reasons to change the tool or the box during the session. Do not "fix" a value.

---
## 3. Deliverables and evidence layout

| artifact | location | notes |
| --- | --- | --- |
| inventory tool | parent:tools/bench-inventory.sh | the only device-facing artifact; ~1,700 lines including the 600-entry allowlist |
| mock test suite | parent:tools/tests/bench-inventory.test.sh | 145 assertions, runs against stubs and fixtures; no device |
| procedure (this file) | this file | the attended runbook |
| evidence | parent:build/bench-inventory/<UTC>-<label>/ | gitignored; hashed in SHA256SUMS |

The evidence directory contains:

- inventory.json - one entry per read with group, block, address, spec section, field,
  expected token, all read passes, value, stability, status, expected-value check and detail;
  plus gates, health pre/post maps, health checks, context, crosschecks and summary.
- inventory.tsv - the same table for spreadsheets/diffing.
- plan.tsv - the exact allowlist selection for this run (group, block, address, spec, field,
  expected, note), the audit surface for "what was read".
- inventory.log - the human transcript: gates, health, every batch result, per-entry values,
  the final result.
- commands.txt - the exact remote scripts sent (health, context, every batch). No key path.
- batches/batch-NNNN.cmd/.out/.err - the raw device output per SSH call.
- context/ + .context.idx - device-tree binary properties, /proc/mtd, /proc/cmdline, selector
  sysfs shadow, UBI rootfs slot and clk_summary, each with its own SHA-256.
- health-pre.raw / health-post.raw - the raw KEY=VALUE health probes.
- SHA256SUMS - hashes of every file in the directory (the first thing to verify later).

Archive the directory after the run. parent:build/ is not versioned, so a run only becomes
durable evidence when the owner copies it, adds the parent:EVIDENCE-INDEX.md row with a
regeneration command, and appends a dated parent:mem-entries.md entry. This document does not
edit those shared files.

---
## 4. Preconditions and gates

The tool refuses (exit 2) unless every gate below passes. Gate names are stable and appear in
the console, the log and inventory.json.

| # | gate | check | fix |
| --- | --- | --- | --- |
| 1 | attended | --attended given | a human must be at the bench for the whole run; recovery is physical |
| 2 | transport | parent:tools/rsh.sh executable and .omo-secrets/router_ed25519 present | restore the project key; never paste key material into a tracked file |
| 3 | no-active-fire | no fire driver process running | wait/stop it; never run two device actions at once |
| 4 | bench-link | parent:tools/bench-link.sh exits 0 | parent:tools/bench-link.sh --apply |
| 5 | recovery-door | parent:tools/recovery-card.sh exits 0 and prints READY | parent:tools/recovery-card.sh --arm |
| 6 | uart-capture | a read-only capture is running (pidfile or timeout-N-cat process) | parent:tools/uart-capture.sh start --label inv; or --no-uart (recorded WARN) |
| 7 | stale-writers | no stale fire-log writer (WARN only) | parent:tools/bench-teardown.sh --apply |
| 8 | stock-health | see below; only after gates 1-7 pass | fix the box state first; do not read registers |

The **stock-health** gate runs over SSH (one probe, no register reads) and fails closed on:

- kernel release not exactly 5.10.201, or uname -v missing the stock build stamp
  "Sat Nov 25 18:18:57 2023";
- OpenWrt release not 22.03.6;
- fewer than 2 wiphys, or calibration [SUCC] missing on either band;
- fewer than 30 hi_ modules loaded;
- any .omo-off leftover (hidden vendor module -> not stock);
- mtd11/kernela, mtd12/kernelb, mtd13/rootfsa, mtd14/rootfsb missing from /proc/mtd;
- bootflag not a/b, or the SYSCTRL selector low byte (sysfs boot_reg, printed as hex without
  the 0x prefix) not the recorded pair (a->0x10, b->0x11). A short hold or a previous handover
  can leave 0x00/0x21/0x11; fix the boot state and re-run, do not inventory around it;
- running rootfs mtd_num not the slot implied by bootflag (a->mtd13, b->mtd14);
- no usable read path: neither devmem nor (/dev/mem readable with dd and od);
- uptime not numeric.

The kernel build stamp and OpenWrt release are constants at the top of the tool; if a future
recovery image legitimately changes them, update them with the new image's [V] evidence, not
ad hoc.

If the recovery door is not READY, **stop**: do not SSH at all. The tool enforces this order
itself, but the operator must also know that a refused run has not touched the router.

---
## 5. Attended procedure (exact order)

### 5.1 Preflight (host + physical, no register reads)

1. Read parent:BENCH-FACTS.md, parent:STATUS.md (Next action), parent:LESSONS.md.
2. parent:tools/session-brief.sh - one screen of git/doc/bench state.
3. parent:tools/bench-link.sh - must print PASS. If not: parent:tools/bench-link.sh --apply,
   then PASS again (the adapter must be NetworkManager-unmanaged with both addresses).
4. parent:tools/recovery-card.sh --arm - the physical card and the TFTP server; confirm
   "recovery door is READY", recovery.bin md5 421114c8ea999c925b7aa79cc548af81, and a human
   who can power-cycle the router and hold RESET through the counter.
5. parent:tools/uart-capture.sh start --label inv-YYYYMMDD - read-only console capture,
   absolute log path under parent:build/uart-logs/. Leave it running for the whole session.
6. Review the plan on the host, without the device:
       tools/bench-inventory.sh --print-plan > inv-plan.txt
       awk -F'\t' 'NF>=7 && $3 ~ /^0x/ {n++} END {print n}' inv-plan.txt   # 600 reads for all groups
   Optionally spot-check decisions:
       tools/bench-inventory.sh --explain-address 0x10161000   # DENY, RC misc window
       tools/bench-inventory.sh --explain-address 0x14880090   # ALLOW, CRG PLL status
7. Run the gate/health-only mode first (this is the only device contact in this step):
       tools/bench-inventory.sh --attended --check-only --label inv-preflight
   Expect exit 0 and RESULT: CHECK-ONLY-PASS with eight PASS gates (or a WARN for
   --no-uart). This proves the gates and the stock-health probe before
   any register read. If it refuses, fix the named gate and repeat; the box was not read.

### 5.2 The inventory run

    tools/bench-inventory.sh --attended --label inv-YYYYMMDD

Defaults: all four groups, batch size 16 addresses per SSH call (38 calls for 600 reads),
repeat 2 read passes per address (stability evidence), 20 s per-call timeout, 10 s connect
timeout, stop after 2 consecutive failed batches. The run is read-only; it takes an estimated
1-3 minutes [I], dominated by the on-device busybox devmem calls (about 1,200 reads across the
two passes). It is safe to leave the console capture running.

Optional, same run: --batch 32 halves the SSH calls; --repeat 1 is a faster single-pass
inventory (no stability evidence); --groups lsw,pie,crg,sysctrl selects a subset (the console
prints the exact planned count); --no-context skips the device-tree/proc capture;
--ssh-timeout 60 for a slow link.

### 5.3 After the run

1. Read the console result. COMPLETE (exit 0) means all gates passed, all allowlisted reads
   returned, and the post-run health check proved the same boot. PARTIAL (exit 3) means one
   or more batches failed; INVALID (exit 3) means the box rebooted or degraded mid-run;
   REFUSED (exit 2) means a gate stopped the tool (local failure -> no device contact).
2. Verify the hashes:
       ( cd build/bench-inventory/<UTC>-<label> && sha256sum -c SHA256SUMS )
3. Stop the capture (parent:tools/uart-capture.sh stop --label inv-YYYYMMDD) and run
   parent:tools/bench-teardown.sh --apply. Leave TFTP armed only if a recovery is genuinely
   still intended (parent:STATUS.md decides).
4. Archive the evidence directory, add the parent:EVIDENCE-INDEX.md row and a dated ledger
   entry with the result, the SHA-256 of inventory.json, and any expected-value mismatches.
5. Feed the results to the owning lanes: P3 into patch 015, CRG decode into patch 019, LSW
   baseline into the D1b write-set derivation, SYSCTRL cells into the SMP/selector lanes.

### 5.4 If something goes wrong

- The tool refuses: nothing was read. Follow the named gate.
- The run reports PARTIAL/INVALID: stop. Do not re-run the same session to "get a clean
  number". Review the failed batches (batches/*.err) and the post-health checks first. A
  reboot mid-run invalidates the whole evidence set.
- The console goes silent or the box stops answering: the capture is the evidence; follow the
  recovery card exactly (power off, hold RESET, power on still holding, TFTP from
  192.168.1.88, then verify stock health with session-brief.sh). Never type to the UART and
  never retry the inventory before the stock-health gate passes again.
- If a value is implausible, record it as a finding. The tool does not write, so there is
  nothing to undo; the risk is entirely in a read to a bad window, which the allowlist and
  window/denylist checks are designed to prevent.

---
## 6. Command reference

    tools/bench-inventory.sh --attended [options]   # the only mode that touches the device
    tools/bench-inventory.sh --check-only --attended # gates + stock health only
    tools/bench-inventory.sh --print-plan           # host-only: the full allowlist
    tools/bench-inventory.sh --explain-address 0xADDR  # host-only: ALLOW (0) / DENY (1)
    tools/bench-inventory.sh --help

    --attended              human at the bench (mandatory for device contact)
    --label NAME            evidence label [A-Za-z0-9_-]
    --out DIR               evidence directory (default build/bench-inventory/<UTC>-<label>)
    --groups LIST           lsw,pie,crg,sysctrl (default all)
    --batch N               addresses per SSH call, 1..32 (default 16)
    --repeat N              read passes per address, 1..3 (default 2)
    --ssh-timeout SECONDS   per-call timeout, 5..120 (default 20)
    --connect-timeout SEC   rsh.sh connect timeout, 1..60 (default 10)
    --max-failed-batches N  stop after N consecutive failed batches, 1..20 (default 2)
    --no-uart               allow without a console capture (recorded WARN)
    --no-context            skip the device-tree/proc capture
    --check-only            gates + stock health, no register reads

Exit codes: 0 COMPLETE / CHECK-ONLY-PASS / print-plan / ALLOW; 1 usage error; 2 REFUSED
(precondition failure or unwritable evidence directory); 3 PARTIAL or INVALID.

Host-only modes never contact the device and need no gates.

---
## 7. What is read (the allowlist)

The full, exact list is parent:tools/bench-inventory.sh and can be dumped with --print-plan or
found in plan.tsv of any run. 600 32-bit reads in four groups:

| group | reads | windows | spec |
| --- | --- | --- | --- |
| lsw | 476 | dp 0x11000000, ips 0x11080000, fam 0x11100000, pe 0x11180000, eqm 0x11200000, eps 0x11280000, prbs 0x11380000, pfe_glb 0x12000000, pa 0x12080000, ifc 0x12100000, l2 0x12180000, napt 0x12280000, mcc 0x12380000, opp 0x12400000, pub 0x12480000 | LSW-SPEC.md sections 5-7 |
| pie | 108 | 0x10a70000-0x10a7ffff (64 KiB) | PIE-DATAPATH.md sections 4-5, 11.2 P3 |
| crg | 13 | 0x14880000-0x14880fff (4 KiB) | MISC-SOC-BLOCKS.md sections 5.2/5.3, Appendix C |
| sysctrl | 3 | 0x10100000-0x10100fff (4 KiB) | MISC-SOC-BLOCKS.md section 6.3, SELECTOR-0x21.md |

Key address sets, by purpose:

- **LSW W1 baseline** (LSW-SPEC.md section 12.3): dp+0x1c, dp+0x64, dp+0x68, eps+0x1008,
  eqm+0x00/0x18/0x38/0x40/0x48/0x5c/0xa4/0x504/0x600c, eqm+0x2004..0x203c counters,
  eps+0x2084..0x20b4 counters, fam+0x504, ips+0x00/0x08/0x10/0x1120/0x1344.
- **LSW tables** (LSW-SPEC.md section 7.3): the qid table A at opp+0x42800 (128 words, 16
  ports x 8 slots) and table B at opp+0x43800 (256 words, 8 ports x 8 slots x 4 words), plus
  the EPS channel-map words eps+0xB00/+0xB80 (8 channels each) and eps+0x50000 (tab128 base).
- **LSW PFE counters** (LSW-SPEC.md section 5.3): ifc+0x00 and, for l2/napt/mcc/opp/pa/pe/
  pfe_glb/prbs/pub, offsets 0x00, 0x1004, 0x1008.
- **PIE P3** (PIE-DATAPATH.md section 11.2): 0x1000, 0x2200, 0x3200/0x3204/0x3208/0x3404/
  0x3408/0x3700; the per-channel words 0x180/0x200/0x300/0x380/0x400/0x3210/0x3290/0x3310
  for channels 0-7; TX/RX ring registers 0x2000/0x3000 blocks (base, config, wptr, rptr) and
  the RX ring-size register 0x3504+ch*0x10 for channels 0-3.
- **CRG** (MISC-SOC-BLOCKS.md section 5): gate words 0x14/0x20, reset words 0x2c/0x30/0x34,
  resume word 0x38, PLL status 0x90, watchdog ISTATUS 0x100, mux 0x138, PLL1 words
  0x198/0x19c, PLL2 words 0x1e0/0x1e4.
- **SYSCTRL** (MISC-SOC-BLOCKS.md section 6.3): 0xc00 (SMP release word), 0xc08 (boot
  selector), 0xc10 (reset record).

The **context capture** (read-only, fixed paths, no globs) records the device tree *after* the
U-Boot overlay for lsw_dp/lsw_pfe/pie/sysctrl/sysenv/rstinfo (reg, reg-names, hwver,
rx_ring/tx_ring, interrupts, dma-coherent, smp-offset, bootreg-offset, record_addr), plus
/proc/mtd, /proc/cmdline, the selector sysfs shadow, the UBI rootfs mtd_num, and
/sys/kernel/debug/clk/clk_summary when debugfs is mounted. The device-tree reg readback is
what settles the queue/flow DDR base question of LSW-SPEC.md section 5.4 without any risk.

**Never read, hard exclusions (also printed by --print-plan):**

    0x10160000-0x1016ffff   RC misc window, includes 0x10161000 (a read panicked the box)
    0x400392f0              CA write-forbidden address; CA space is out of scope
    anything outside the four windows above, any unaligned access, any write of any kind

---
## 8. Reading the evidence

inventory.json is the primary artifact. Entry fields:

- i, group, block, addr, spec, field - identity and the spec section/field the read maps to.
- expected - the expected token(s) from the static allowlist: unknown, dynamic, nonzero,
  eq:0xVALUE, mask:0xMASK, clear:0xMASK, or pair (selector low byte must equal the recorded
  bootflag pair); several checks can be combined with semicolons.
- reads - the value per pass (null when that pass has no read); repeat defaults to 2.
- value - the first read value, or null when nothing was read.
- stable - true if all passes returned the same value, false if they differ, null when not
  enough passes returned. Dynamic registers (ring pointers, counters, IRQ status) are expected
  to be false; a static register that is false is itself a finding.
- status - ok, partial, error, not-attempted.
- check - pass/fail/unknown against the expected token; check_detail carries the arithmetic.
- summary - counts; batches_total/run/failed; checks pass/fail/unknown.
- crosschecks - selector sysfs shadow versus the devmem read of 0xc08 (low byte).
- context - per-file path, kind, status, bytes, SHA-256 and, for device-tree properties,
  words_be32 (the property bytes grouped as big-endian 32-bit words; a DT reg property is a
  list of (address, size) pairs in that order).
- health_pre/health_post - the raw KEY=VALUE probe maps; health_checks_pre/post - the
  individual checks with PASS/FAIL/WARN and detail.

A quick mismatch list on the host (no device):

    python3 -c 'import json; d=json.load(open("build/bench-inventory/<run>/inventory.json")); [print(e["addr"], e["group"], e["expected"], e["value"], e["check"]) for e in d["entries"] if e["check"]=="fail"]'

Remember LESSONS.md rule 2: this is one snapshot on one boot. "No change" needs n>=5 with the
same method; this tool is a baseline, not a rate test. Rule 3 (compare like with like) applies
when comparing these values with older captures from a different image or slot.

---
## 9. Interpreting each group against the specs

### 9.1 CRG (0x14880000 page)

Expected-reference values below are recorded measurements from earlier sessions on the lab
image (parent ledger, 2026-10-07) and are [V] as measurements, not as values you must see on
today's stock boot. A difference is a finding, not a tool error.

| address | field | expected / reference | how to read it |
| --- | --- | --- | --- |
| 0x14880014 | gate word 0x14 (apb group: sfc, gpio0/1, i2c0, led_pwm) | ref 0x01360ef1 on a fresh boot; 0x01364ef1 after the led_pwm gate bit 0x0e was set by an old write test | one bit per gate, 1 = enabled; this page is a write-1-set latch class, so a CLEAR does not take - do not infer polarity for writes from here |
| 0x14880020 | gate word 0x20 (gemac, mdio, lsw, pie group) | ref 0x02f8300b | as above; the PIE gate (0x19) and LSW gates (0x00/0x01/0x03/0x04) live here |
| 0x1488002c, 0x30, 0x34 | reset words | 0x34 ref 0x000ff309 with both PCIe RCs up | active-low class: 1 = out of reset; 0x30 carries LSW DP/PFE and PIE reset bits, 0x34 the PCIe/efuse/core bits |
| 0x14880038 | CRG resume/soft-reset word | unknown [U] | SMP lane owns it; read-only baseline for the CPU1 resume bits 0x16/0x17/0x18/0x19 |
| 0x14880090 | PLL status | ref 0x6a010008 and 0x6a090008; expected mask 0x48000000 (bit30 CPU PLL lock, bit27 LSW PLL lock) | if the mask check fails, the PLLs are not locked as expected: stop and report, do not interpret the PLL words |
| 0x14880100 | watchdog ISTATUS | expected 0x00000000 | read-only status; 0 is the healthy value |
| 0x14880138 | clock mux | unknown [U] (bit1 efuse source, bit3 CPU source) | read-only; never write bit3 (CPU clock source) |
| 0x14880198 / 0x19c | PLL1 control word pair (CPU PLL) | unknown [U] raw | decode with the procedure below |
| 0x148801e0 / 0x1e4 | PLL2 control word pair (LSW/other PLL) | unknown [U] raw | same decode; the vendor node names its output lsw-clk |

**PLL decode (MISC-SOC-BLOCKS.md section 5.3, read-only).** With w0 = 0x14880198 and
w1 = 0x1488019c (and w0 = 0x148801e0 / w1 = 0x148801e4 for PLL2):

- integer field: int = (w0 >> 12) & 0xfff; fraction: frac = w1 & 0xffffff.
- feedback divider: fbdiv = (w0 >> 24) & 0x3f.
- word0 byte0: bit3 selects the integer-only path (when set, ignore frac); bits 4..6 and
  8..10 are divider-select fields. The div-factor property is absent on this board (0).
- rate = 20 MHz * (int + frac / 2^24) / fbdiv, then each non-zero divider-select value is
  applied as an integer division.
- Cross-check: the CPU PLL must land on one of the OPP rates 800/1000/1200 MHz; the second PLL
  feeds the LSW clock tree (166/333 MHz domains). A match confirms the decoded equation in
  MISC-SOC-BLOCKS.md and patch 019 (which currently has no measured rate). No match means the
  decode or the divider-select mapping needs revision - record the raw words and stop.
- Parent confirmation: if /sys/kernel/debug/clk/clk_summary was captured, it names the live
  gate parents and rates; compare with the gate-parent table in MISC-SOC-BLOCKS.md section
  5.2 (apb -> sfc/gpio/i2c0/led_pwm/pcie; gemacN -> 5 GEMAC + MDIO; lsw_dp/lsw_pfe -> LSW;
  ahb -> PIE; osc -> PLLs). If debugfs is not mounted the context entry is missing; that is
  expected and the PLL words still stand on their own.

### 9.2 PIE (0x10a70000 window, 64 KiB)

| address set | field | expected after the vendor boot | interpretation |
| --- | --- | --- | --- |
| 0x1000 | global control | bit0 clear, bits6-8 set (clear:0x1;mask:0x1c0) | matches the vendor reg_init write model; a different bit0 means RX not enabled |
| 0x2000+ch*0x10 +0x00 | TX ring DMA base ch0-3 | non-zero | must be a 32-byte aligned DMA address; zero means the channel was never armed |
| +0x04 | TX channel config / ring-size shift N | 0xc [I] | confirms the DT tx_ring first word 0xc (4096 descriptors) reading of PIE-DATAPATH.md section 5.1 |
| +0x08 | TX write pointer / doorbell | dynamic | volatile; a value moving between passes is normal |
| +0x0c | TX read pointer (low 16) | dynamic | completion pointer |
| 0x3000+ch*0x10 +0x00 | RX ring DMA base ch0-3 | non-zero | as TX |
| +0x04 | RX channel config / ring-size shift N | 0xa [I] | confirms the DT rx_ring first word 0xa (1024 descriptors) |
| +0x08 | RX write pointer (15:0) and sf pointer (31:16) | dynamic | volatile |
| +0x0c | RX read pointer (low 16) | dynamic | volatile |
| 0x3504+ch*0x10 | RX ring-size config | 0x100 [I] | ring size >> 7: 32 KiB RX ring -> 0x100 |
| 0x3200 | RX enable/mode | bit0 set [I] | global RX enabled after init |
| 0x3204 / 0x3208 | reg_init values | 0 / 0x7d [V write values] | match the vendor write model; other values are a finding |
| 0x3404 | 2-bit per-channel field | unknown [U] | rx_init_chan programs cfg[16]&3 per channel; meaning unknown, record raw |
| 0x3408 | large-ring bitmap | expected 0 [I] | board rings are 32 KiB with 2048 B buffers, under the 9600 B trigger |
| 0x3700 | GRO ring size minus 1 | unknown [U] | only written when a gro_ring exists; the board DT has none |
| 0x180/0x200/0x380 +ch*4 | per-channel words | 1 for wired ch0-3 [V write model] | ch4-7 belong to the WoC/Wi-Fi paths; their values are not asserted |
| 0x300/0x400 +ch*4 | RX IRQ status / mask | status dynamic; mask 0 for ch0-3 [I] (unmasked after init) | the mask is 1 = masked; a wired channel left masked is a finding |
| 0x3210/0x3290/0x3310 +ch*4 | reg_init per-channel words | 3 / 2 / 8 for ch0-3 [V write model] | meaning [U]; record raw |
| 0x2200 | DFX counter control | unknown [U] | diagnostic path only |

**P3 acceptance** (PIE-DATAPATH.md section 11.2): every recovered offset reads a plausible,
stable value. Plausible means: the static/reg_init words match the model where the model has
a known write value, DMA bases are non-zero and aligned, and no value contradicts the window
layout. Stable means repeat=2 shows equal passes for static addresses; the dynamic addresses
(ring pointers, IRQ status, counters) are expected to move. Record the inventory in the wave
evidence directory and compare against the model before enabling any ring in patch 015.

### 9.3 LSW (0x11xxxxxx/0x12xxxxxx windows)

The window map and module-id table are LSW-SPEC.md sections 5.2/5.3. What the run adds is the
first **baseline** of the vendor-initialized state:

- dp+0x1c, dp+0x64, dp+0x68: probe-time values (memory number and cell-size selector), the
  W1 baseline of LSW-SPEC.md section 12.3.
- eps+0x1008 and eqm+0x48 (+0x5c): queue/DFX and the EQM indirect command/status register.
  eqm+0x48 bits 31 (busy) and 5 (error) are documented [V]; an idle engine should show both
  clear, but do not use the value to infer the command word layout (still [U]).
- qid table A at opp+0x42800 (128 words): table shape per port/slot is in the field names;
  table B at opp+0x43800 (256 words) is 16-byte entries, 8 slots x 8 ports for the 40..47
  class. The vendor writes entries of the form (0x100 enable bit) | (qid low byte) [I]; the
  baseline lets the D1b write set be derived from observed state instead of invented values.
- eps+0xB00 / +0xB80 channel-map words: PIE channel to LSW queue-port binding (section 7.4);
  the exact bit packing is [U], so record raw and compare across runs, do not decode.
- DFX counter blocks (eps 0x2084..0x20b4, eqm 0x2004..0x203c, PFE blocks): counters; expect
  small non-zero values on an active vendor boot and movement between passes.
- pm/pe/fam/ips/pub/ifc words: probe/config/counter state, meaning [U] per LSW-SPEC.md 5.3.

Also captured by context: the live lsw_dp/lsw_pfe device-tree reg after the U-Boot overlay.
The lsw_dp reg property starts with queue and flow base/size pairs; reading 0x87b00000 +
0x400000 / 0x87f00000 + 0x100000 means the overlay did not rebase them, while
0x80000000-class values mean it did. That is the direct answer to the queue/flow DDR base
question of LSW-SPEC.md section 5.4.

### 9.4 SYSCTRL (0x10100000 page)

| address | field | expected | interpretation |
| --- | --- | --- | --- |
| 0x10100c08 | boot selector cell | low byte 0x10 when bootflag=a, 0x11 when bootflag=b (the recorded running pair); 0x00 = no pair recorded yet; 0x21 = the project manual handover value | the health gate requires the pair to match before any read, so a fail here means the cell changed mid-run (finding) or the upper 24 bits are non-zero (the [U] question). Cross-check against the sysfs shadow: inventory.json crosschecks.boot_reg_crosscheck |
| 0x10100c00 | SMP release/pen word | unknown [U] | first live read for the SMP lane; read-only baseline, do not interpret as a write plan |
| 0x10100c10 | reset record (rstinfo record_addr) | unknown [U] layout | do not interpret; recorded for the reset-record lane |

The selector sysfs shadow is also captured in context (/sys/devices/platform/sysenv/boot_reg),
so the two readings can be compared outside the tool as well (SELECTOR-0x21.md section 8,
experiment 3).

---
## 10. What one run closes, and what it does not

**Closes (or closes partly) when the run is COMPLETE and reviewed:**

| gate / unknown | what the run supplies | evidence after run |
| --- | --- | --- |
| PIE stage P3 register inventory | all registers named in PIE-DATAPATH.md 11.2, with stability and expected-value diff | inventory.json entries group pie; feeds patch 015 |
| PIE section 10 rows "ring pair-word semantics" and "per-channel words" | live programmed 0x2004/0x3004/0x3504 and per-channel words | changed [I] -> measured; update PIE-DATAPATH.md with the run's artifact path |
| PIE "bootloader/firmware pre-initializes PIE?" | only the post-vendor-boot state; pre-init state still needs a different method | partial |
| LSW D0 read-only plan | execution of LSW-SPEC.md section 12.2 items 3-4 (DT/reg window readback) | context entries |
| LSW D1b W1 baseline | vendor-initialized dp/eps/eqm/opp + qid tables before any clean-room write | inventory.json entries group lsw |
| LSW queue/flow DDR base (section 5.4) | live lsw_dp reg after the U-Boot overlay | context lsw_dp/reg words_be32 |
| CRG rates/parents (MISC-SOC-BLOCKS.md 5.5) | PLL status + both PLL control pairs + clk_summary when available | crg entries + context clk summary; feeds patch 019 |
| SYSCTRL selector upper 24 bits (SELECTOR-0x21 R3) | full cell value beside the sysfs shadow | sysctrl entry 0xc08 + crosschecks |
| SYSCTRL SMP word and reset record | first live read-only baseline | sysctrl entries 0xc00/0xc10 |

**Does not close (do not claim otherwise):**

- The EQM indirect-table command-word bit layout (LSW-SPEC.md 7.5) - still [U]; the read gives
  idle status only.
- Per-port VLAN/PVID location (8.4), the L2 hardware table format (8.3), the IFC classifier
  row encoding (8.2) - still [U].
- The D1b **write set** itself: this tool reads the vendor state, it does not derive or test
  writes. W2-W6 of LSW-SPEC.md section 12.3 remain a separate written plan with a gate.
- Post-reset / pre-driver values: the vendor system is already initialized. Anything about
  the bootloader-only state needs another method.
- /proc/hsan DFX dumps (LSW-SPEC.md 12.2 items 1 and 6): they are the cheapest remaining
  read-only items (live port names, queue counts, interface mapping, hi_kcfe_pub bypass flag)
  but this tool deliberately does not walk /proc; they are a short, separate, bounded read
  listed as follow-up. Add them to the D1b evidence before deriving the write set.
- Wi-Fi/WoC per-virtual-port behavior, vendor userspace ABI, and anything requiring the 6.18
  target.

---
## 11. Stop conditions and recovery fallback

The tool itself never writes, and it stops by itself on gate failures and read failures. The
operator's stop rules are:

1. Any gate FAIL -> stop; the run refused (local gates: no contact; stock-health: no register
   read). Fix the box/host state, do not bypass a gate.
2. PARTIAL or INVALID result -> stop and review. Do not "try again quickly" for a clean
   number. A boot_id change means the evidence set is invalid; a health degradation means the
   box needs attention before anything else.
3. Console silence, SSH loss, or a hung box during the run -> the read hit an unsafe state.
   Follow the physical recovery card: capture console (read-only), power off, hold RESET,
   power on still holding, let the counter reach 2 s, serve recovery.bin from 192.168.1.88
   over TFTP, then verify stock health with session-brief.sh. Do not type to UART, do not
   retry blindly, and do not re-arm anything else until the stock-health gate passes.
4. Any "implausible" value -> record it as a finding and continue only if the run is otherwise
   healthy. The read did not change the box; there is nothing to undo.

Recovery door discipline: the door must already be READY before the run starts; if it is not,
do not start. After the run, parent:STATUS.md decides whether TFTP stays armed.

---
## 12. Mock tests: what they prove and what they do not

    tools/tests/bench-inventory.test.sh

The suite builds a throw-away sandbox with stubbed tools/rsh.sh, tools/bench-link.sh and
tools/recovery-card.sh, fixture health/context/register data, and a fake live UART pidfile. It
never opens a network socket to the router. 145 assertions cover:

- the allowlist: --print-plan lists all 600 reads and no RC misc row; --explain-address allows
  every group's representative addresses and denies 0x10160000-0x1016ffff, 0x400392f0,
  unaligned, out-of-window and non-table addresses;
- fail-closed refusals: no --attended, bench link fail, recovery fail, no UART capture (and
  --no-uart path), missing transport, unreachable box, non-stock kernel, selector pair
  mismatch, no read path - each refuses with the named gate and no register read;
- check-only mode: eight PASS gates and zero register reads;
- full run: 600 parsed entries, one per plan row, COMPLETE, JSON valid, TSV rows, SHA256SUMS
  verifies, stability on repeat, batch splitting, selector pair check, CRG mask check, PIE
  expected values, and spec sections present in entries;
- safety scan of every emitted command: no forbidden tokens (mtd write, fw_setenv, rmmod,
  insmod, modprobe, reboot, flash_erase, ...), no output redirection beyond 2>/dev/null, no
  key path in the transcript, and every address in a batch script is in the run plan; the only
  literal address outside batch blocks is the health probe read of 0x14880090;
- failure handling: per-register read error, SSH timeout (exit 3), truncated output,
  unexpected address in remote output, stop after consecutive failed batches, and a changed
  boot_id producing INVALID with exit 3;
- argument validation (unknown group, bad batch size, bad label).

What the tests do **not** prove: the real stock register values, OpenWrt busybox devmem
behavior, the real SSH transport, the stock-health signatures on today's box, or that the
allowlist is safe on real silicon. Those are exactly what the attended run validates; the
tests only prove the tool's own logic and its inability to write.

---
## 13. Limitations and honesty

- The tool has never been run against the router. Every expected-value token is a placeholder
  derived from the specs, not a measurement; a failed check is not proof that the spec is
  wrong or that the box is faulty.
- One snapshot per boot. The register state is the vendor system's live state; it can only be
  compared like-with-like (same image, same slot, same boot phase). Uptime is captured so a
  later reviewer knows how late in the boot the sample was taken.
- Reads are passive. The tool cannot prove what a register does; it records what it reads, and
  the [U]/[I] labels in the specs stay until the owning lane interprets the run.
- Device access uses busybox devmem (preferred) or a /dev/mem+dd fallback. On a kernel with
  /dev/mem restrictions the devmem probe fails and the capability gate refuses the run.
- The device-tree readback depends on /proc/device-tree or /sys/firmware/devicetree/base being
  available; the context capture is marked unavailable rather than failing the register
  inventory.
- clk_summary requires debugfs to be mounted; if not, the CRG PLL words are still captured.
- The tool prints no credentials and logs no key path; the target address (root@192.168.10.1)
  is documented project infrastructure, not a secret.
- No claim here is a publication-ready measurement: the attended run, its hashes and its
  review are the artifacts that matter.

---
## 14. Host-only reproduction commands

    # static review of the allowlist (no device)
    tools/bench-inventory.sh --print-plan
    tools/bench-inventory.sh --explain-address 0x10161000
    tools/bench-inventory.sh --explain-address 0x14880090
    tools/bench-inventory.sh --help

    # mock test suite (no device, no network)
    tools/tests/bench-inventory.test.sh

    # lint
    shellcheck -S warning tools/bench-inventory.sh tools/tests/bench-inventory.test.sh

---
## 15. Follow-ups (not part of this deliverable)

1. First attended run per section 5; archive the evidence; add the parent:EVIDENCE-INDEX.md row
   and a dated parent:mem-entries.md entry.
2. Feed the PIE P3 results into patch 015 (and update PIE-DATAPATH.md section 10 statuses).
3. Feed the CRG PLL decode into patch 019 (confirm or correct the decoded equation).
4. Take the bounded /proc/hsan DFX read (LSW-SPEC.md section 12.2 items 1/6) on the same
   attended session if time allows; it is read-only and complements the register baseline for
   the D1b write set.
5. Only after the baseline is reviewed: write the D1b write-set table (W2), then the attended
   write campaign - a separate written plan, never part of this tool.

---
## 16. Changelog

- 2026-11-18: initial version. Tool, mock tests (145 assertions) and this procedure written
  host-only; nothing executed against the hardware. Bench fires remain stopped; the bench
  remains stock and idle.
