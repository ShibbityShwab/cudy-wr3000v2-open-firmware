# both-eps: claiming both functions makes the ETE engine fetch AND the interrupt fire, but the chip still will not accept the H2D message (phase 21b, 2026-10-02)

Task `st_01a0fbda`. Direct sequel to `docs/phase21/sr-trigger.md`, which closed on the split:
endpoint 0 `0000:00:00.0` (irq 207) fetched the SR descriptors but its INTx never fired; the sibling
`0001:00:00.0` (irq 209) took the interrupt but its SR engine never fetched (`SR+0x1c` frozen at
`0x10`, `docs/phase21/sr-trigger.md` Part C). The two functions alias one on-chip register space but
sit behind two root complexes, and the descriptor fetch is a device DMA read through one of them.

This phase (A) determines from `hi5622v100_plat.ko` and a read-only live vendor capture how the
vendor's single driver gets both halves; (B) implements `lab/bothep/bothep.c` (claim BOTH functions,
decode BOTH RCs, program the outbound window on both, issue the ring programming through each BAR in
sequence) and boots it twice; and (C) names the configuration that achieves both and the next
blocker.

Every claim is **[proven]** (a constant/relocation/control-flow in an instruction stream, a live
read, or a value the device printed) or **[inferred]**.

---

## Headline

**Part A — the vendor decodes both RCs and drives the ETE registers through the first-probed
function (ep0).**

* A read-only live read of BOTH endpoints' BAR2 iATU on a normal vendor boot shows the vendor
  programs **both** root complexes: the same outbound window (`devva 0x80000000..0xffffffff -> host
  0x80000000`) and the same six inbound viewports, shifted by the `0x18000000` host offset. The
  second RC is not left at reset. **[proven]**
  (`build/register-dumps/bothep/001_live_vendor_bars2.txt`)
* The vendor's ETE register programming (SR ring base/depth/wptr/ctrl, the DR program, the ETE
  interrupt block `0x40039508`) resolves its VAs through **one** viewport table —
  `chip->dev[0]->[4]`, the **first-probed** function's table. `pcie_ete_init` @`0x7820`,
  `pcie_ete_intr_init` @`0x7528` and `pcie_ete_rings_init` @`0x7680` all load `[chip_ctx]` then
  `[chip->dev[0]]` then `[+4]` and call `oal_pcie_inbound_ca_to_va(table, CA)`; `pci_dev_res_init`
  @`0x821c` fills `chip->dev[0..probe_cnt-1]` in **probe order**, so index 0 is the function the
  vendor probes first = `0000:00:00.0` (`[PCIEL]probe cnt 1`). The ring/descriptor programming is
  therefore issued through **ep0's BAR0 region-3 viewport**, not the sibling's. **[proven]**
* The message ISR is bound to **irq 209 = ep1**; ep0's own line 207 takes **0** interrupts on a
  working vendor boot. **[proven]** (live `/proc/interrupts`, phase-21 live-binding)
* The device's SR descriptor fetch DMA traverses **ep0's RC**: phase 20f (ep0 only) fetched; bothep
  boot 1 (ep1 primary) did not; bothep boot 2 (ep0 primary) did. **[proven]**

So the vendor's single driver needs **both** RCs decoded (it programs both) but issues the ETE
register/descriptor programming through the first-probed function, and takes the completion
interrupt on the sibling. That is exactly the combination our single-endpoint takeovers were
missing.

**Part B — two boots of `lab/bothep`.**

* **Boot 1 (ep1 primary):** both functions claimed, both RCs decoded, but the ETE rings/descriptors
  and the `0x5a5a` release were issued through **ep1's** BAR. The device rewrote `SR+0x18` to `0x04`
  and `SR+0x1c` stayed `0x10` for the whole 25 s run — **no fetch**, even after the in-boot sequence
  re-programmed the same quoted values through ep0's BAR post-release. `irq 209` taken 23245,
  `irq 207` 0. **[proven]**
* **Boot 2 (ep0 primary):** identical module and args except the endpoint roles are swapped. The
  engine **fetched**: `[post0 +530ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400`; the SR ch0
  block read `+18=0x00000400 +1c=0x00000400`, and the pump's next lap was consumed too
  (`wptr 0x00000000 / rptr 0x00000400`). `irq 209` taken **52507**, `irq 207` 0. **[proven]**

**Part C — both halves at once, but the chip does not answer.**

The winning configuration is boot 2's: claim both functions, program both RCs' iATU (six inbound +
one outbound), issue the **ETE ring/descriptor programming and the release through ep0's BAR**
(domain 0), and bind the ISR to **irq 209** (ep1). With it the ETE engine reads every descriptor
**and** the completion interrupt fires, and the device's own D2H handshake runs (`out[1]` id 6
`0x40` then id 2 `0x04`). But the chip still does **not** clear `out[0]` (the H2D mask, CA
`0x40039010`) and sends no id-1 "Device plat ready!" reply, so the **H2D HCC message is still not
accepted by the firmware**. Named next blocker: the device-side H2D/HCC message accept — the
synchronous arm `out[5]` (CA `0x400392f0 <= 8`, `pcie_msg_send_irq` @`0x174a8`) and/or the firmware
message dispatcher (file `0x818a8`) — now that both the fetch and the interrupt are proven live.
**[proven observation, inferred cause]**

---

## Part A — how the vendor's single driver gets both halves

### A.0 Sources

| file | md5 | use |
| --- | --- | --- |
| `build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | host-side probe/ETE/interrupt code |
| `build/register-dumps/bothep/001_live_vendor_bars2.txt` | — | live read of BOTH endpoints' BAR2 iATU (normal vendor boot) |
| `build/register-dumps/bothep/000_live_vendor_interrupts.txt` | — | live `/proc/interrupts` + module state |
| `build/register-dumps/bothep/002_live_vendor_dmesg.txt` | — | live vendor probe/region/irq dmesg |

Disassembly is the repo's `lab/ko_disasm.py` (capstone ARM). The live reads were done with `devmem`
through each endpoint's BAR2 on a normal vendor boot, read-only; the RC misc window `0x10161000`
was not touched.

### A.1 The vendor programs BOTH root complexes' windows [proven]

`build/register-dumps/bothep/001_live_vendor_bars2.txt` (a normal vendor boot, before any staging):

```
ep0 0000:00:00.0 BAR2 @ 0x41800000        ep1 0001:00:00.0 BAR2 @ 0x59800000
outbound [0x004]=0x80000000               outbound [0x004]=0x80000000
         [0x008]=0x80000000                        [0x008]=0x80000000
         [0x010]=0xffffffff                        [0x010]=0xffffffff
         [0x014]=0x80000000                        [0x014]=0x80000000
inbound vp0..vp5 base_lo:                 inbound vp0..vp5 base_lo:
  0x40000000, 0x401c0000, 0x401d8000,       0x58000000, 0x581c0000, 0x581d8000,
  0x403b8000, 0x404d8000, 0x406b8000        0x583b8000, 0x584d8000, 0x586b8000
```

Both RCs carry the identical outbound identity window (`devva 0x80000000..0xffffffff -> host
0x80000000`) and the same six inbound viewports, exactly `0x18000000` apart. So the answer to "does
the vendor ever program the second RC's windows at all" is **yes, both** — the vendor's per-function
`pci_dev_res_init` -> `oal_pci_lres_init` -> `oal_pcie_host_init` path sets up each function's own
RC (the vendor requests irq 209 *and* 207, `002_live_vendor_dmesg.txt`). **[proven]**

### A.2 The ETE register programming is issued through the first-probed function (ep0) [proven]

`pci_dev_res_init` @`0x821c` walks `chip->dev[0..probe_cnt-1]` **in probe order** and initialises
each:

```
0x008234: ldr  r3, [r6]              ; chip->[0] = the device-pointer array
0x008238: ldr  r3, [r3, r5, lsl #2]  ; chip->dev[r5], r5 = 0,1,... (probe order)
0x008244: ldr  ip, [r3, #0x18]       ; that device's lres
0x008258: bl   oal_pci_lres_init     ; per-function RC init (irq, host init)
```

The ETE layer resolves the ring/interrupt register CAs through the **first** entry of that same
array. `pcie_ete_init` @`0x7820`:

```
0x007860: str  r7, [r4, #0x84]   ; ete_ctx+0x84 = chip_ctx (arg r1)
0x007868: str  r5, [r4, #0x80]   ; ete_ctx+0x80 = chip arg (0)
0x00786c: bl   get_pcie_ete_res  ; r7 = ete resource table
```

and the register-init helpers all do the same two indirections before `oal_pcie_inbound_ca_to_va`:

* `pcie_ete_intr_init` @`0x7528`: `ldr ip,[r1,#0x84]` (chip ctx); `ldr r3,[ip]` (device array);
  `ldr r3,[r3]` (**device[0]**); `ldr r0,[r3,#4]` (its viewport table); `ldr r1,[r1,#4]` (the ETE CA
  from the ete resource); `bl oal_pcie_inbound_ca_to_va`.
* `pcie_ete_rings_init` @`0x7680`: `ldr r7,[r1,#0x84]`; `ldr r3,[r7]`; `ldr r3,[r3]`;
  `ldr r0,[r3,#4]`; `bl oal_pcie_inbound_ca_to_va`.
* `pcie_ete_init_src_ring` @`0x7068`: `ldr r3,[r5,#0x84]`; `ldr r3,[r3]`; `ldr r3,[r3]`;
  `ldr r3,[r3,#4]`; `ldr r3,[r3,#0x10]` (the mapped ETE base) `+0x70`.

`[chip->dev[0]]` is the **first-probed** function — `[PCIEL]chip 0, bus 0, probe cnt 1,
phy_devid:1` = `0000:00:00.0`. So the vendor's single `pcie_ete_init` writes the SR/DR ring
registers and the ETE interrupt block through **ep0's BAR0 region-3 viewport**, regardless of which
function's INTx later delivers the completion. This is also why the device's fetch works on ep0 and
not on ep1 in our takeovers. **[proven]** (`004_disasm_devres.txt`)

### A.3 Which IRQ the ISR is bound to: irq 209 (ep1) [proven]

Live `/proc/interrupts` on the vendor boot (`000_live_vendor_interrupts.txt`):

```
207:          0          0     GIC-0  91 Level     hisi_pci_intx
209:      12734          0     GIC-0  95 Level     hisi_pci_intx
```

and the vendor dmesg binds them `request pcie intx irq 209 succ` (ep1, `bus_id_hostview[1]`) then
`irq 207` (ep0). `pcie_intr_handle` @`0x82e4` runs from the irq-209 stack and reads the glue status
CA `0x400392ec` (phase 21 live-binding). ep0's line is requested but never delivered on a working
boot. **[proven]**

### A.4 Which endpoint's outbound window decodes the descriptor fetch [proven by experiment]

The outbound (device->host) window is what lets the device DMA to host buffers. Both RCs' windows
are programmed in the vendor; our takeovers decoded only one at a time and each failed one half:

| takeover | RC window(s) decoded | ETE programming via | engine fetch? | interrupt? |
| --- | --- | --- | --- | --- |
| phase 20f txpath | ep0 only | ep0 BAR0 | **YES** (`SR+0x1c 0x10->0x400`) | no (`irq_taken=0`) |
| phase 21 sr2/srt | ep1 only | ep1 BAR0 | no (`SR+0x1c=0x10`) | **YES** (irq 209, thousands) |
| bothep boot 1 | both | ep1 BAR0 | no | YES |
| bothep boot 2 | both | **ep0 BAR0** | **YES** | **YES** |

The engine's DMA read of the descriptor ring reaches host RAM only when the ring was programmed
through ep0's viewport (the first-probed function whose RC carries the ETE engine). Decoding both
RCs is necessary for the interrupt side and harmless for the fetch; the decisive variable is the
**function whose BAR0 issues the ETE register/descriptor programming and the release**. **[proven]**

### A.5 Proven vs inferred

| claim | status |
| --- | --- |
| the vendor programs BOTH RCs' inbound + outbound iATU | **proven** (live BAR2 read) |
| the vendor's ETE register programming resolves through `chip->dev[0]` = first-probed = ep0 | **proven** (`0x7820`/`0x7528`/`0x7680` + `pci_dev_res_init` probe order) |
| the completion ISR is bound to irq 209 (ep1); irq 207 takes 0 | **proven** (live `/proc/interrupts`, irq-209 stack) |
| the device's descriptor fetch DMA traverses ep0's RC | **proven** (20f + bothep boot 1 vs boot 2 A/B) |
| the ETE ring base is a device VA (`pcie_hostca_to_devva`) that must go through the outbound window | **proven** (phase-21 `sr-sibling.md` A.4) |
| the device engine binds to the first-enabled/first-probed function, not the interrupt function | **inferred** from the boot-1/boot-2 A/B (only the domain roles changed) |

---

## Part B — `lab/bothep`, the two boots, and the BAR sequence test

### B.1 The module

`lab/bothep/bothep.c` is `lab/srt/srt.c` (sibling bring-up + full field dump + ETE re-assert + SR
pump + service thread) changed to claim **two** functions:

* `omo_dev` = the **primary** endpoint (`domain`, default 1) and `omo_dev2` = the **secondary**
  endpoint (`domain2`, default 0). Both get `pci_enable_device` + `pci_request_mem_regions`, BAR0 +
  BAR2 `pci_iomap`.
* `omo_program_regions()`/`omo_program_outbound()` now take the iATU pointer and host base, so the
  six inbound viewports and the one outbound window are programmed on **both** RCs.
* `omo_request_irq_line()` requests the primary line (`irq`/`hostirq`) and `omo_request_irq2()`
  requests the secondary line (`irq2`/`hostirq2`); the handler counts and labels each line.
* `omo_dump_dual()` reads `SR+0x18/+0x1c`, `out[0]/out[1]` and the glue status through **both**
  BAR0 region-3 viewports and logs them side by side.
* `omo_seq_program()` (module param `seq=1`) is the in-boot BAR sequence test: it re-asserts the
  quoted SR program and commits a fresh lap through the **primary** BAR first, polls `SR+0x1c` for
  2 s through both viewports, and only if the engine still has not fetched does it repeat the same
  quoted writes through the **secondary** BAR.

`enable=0` is retained (CA `0x400392f0`, the hang family, is never written); `intr=0`, `srctrl=0`
select the vendor's live values with read-only diagnostics. The only unproven-but-quoted writes
remain `PCI_INTERRUPT_LINE` (209 on ep1, 207 on ep0).

Built by CI (`build-load-test-module.yml`, new `bothep` step + `bothep-ko` artifact), run
**`36988044949`**, branch `omo/phase21-bothep`, commit `f883e7d`:
`bothep.ko` **67912 bytes**, md5 **`aef02c20d8b51c90ca51b83d39514ff3`**,
`vermagic=5.10.201`.

**Label caveat [proven]:** the `who` strings in `bothep.ko`'s iATU/dual/seq logs are hardcoded to
the *boot-1* role names (`primary/ep1`, `secondary/ep0`). In boot 2 the roles are swapped by the
loader (`domain=0`); the `BAR0 base=0x...` printed in each line is authoritative, and the actual
role is the `domain`/`domain2` module parameter. The source and the tested binary are identical.

### B.2 Device-side self-recovery (armed before staging, re-armed inside each boot)

`/root/recover-bothep.sh` (md5 `26ddcd6ec3fcd5f2f8d8d8d760bda64f`) restores both `.omo-off`
modules, removes the loader, its `rc.d` symlink, the staged module, the `/tmp` copies and itself,
then reboots. It was armed in the vendor boot with
`start-stop-daemon -S -b -m -p /tmp/omo-bothep.timer.pid -x /bin/sh -- /root/recover-bothep.sh --watch`
(pid `15277`, `ALIVE`) **before anything was staged**, and the loader re-arms the same watchdog
**inside** each takeover boot before `insmod` (pid `7365` boot 2). Cancelled with
`touch /tmp/omo-bothep.done` followed by `/root/recover-bothep.sh` (`080_final_health.txt`). The
recovery plan was recorded before rebooting (`000_recovery_plan.txt`). **[proven]**

### B.3 Staging

`bothep.ko` md5 `aef02c20d8b51c90ca51b83d39514ff3` staged in `/lib/modules/5.10.201/`,
`/etc/init.d/omo-bothep` + `S99omo-bothep`, `/root/recover-bothep.sh`, both vendor modules hidden
as `.omo-off` (364660 B / 3564728 B); loader and recovery `sh -n` OK. Both boots re-created the
one-shot `S99omo-bothep` symlink (the loader removes it on start).

### B.4 Boot 1 — ep1 primary (the sibling's BAR drives the rings): still no fetch

Loader args: `domain=1 domain2=0 irq=209 hostirq=209 irq2=207 hostirq2=207 ...`. Decisive lines
(`010_boot1_full_dmesg.txt`, `011_boot1_decisive.txt`):

```
omo-bothep: endpoint 0001:00:00.0 id 59e7:0005
omo-bothep: BAR0 base=0x58000000 (config space), BAR2=0x59800000 (iatu_bar1)
omo-bothep: second endpoint 0000:00:00.0 id 59e7:0005
omo-bothep: ep0 BAR0 base=0x40000000, BAR2=0x41800000 (iatu)
omo-bothep: ep1 programmed 6 viewports
omo-bothep: ep0 programmed 6 viewports
omo-bothep: ep0 outbound AFTER: [0x004]=0x80000000 [0x008]=0x80000000 [0x010]=0xffffffff [0x014]=0x80000000
omo-bothep: request_irq(209, IRQF_SHARED, "omo-bothep") rc=0 - IRQ path live (primary ep1)
omo-bothep: ep0 request_irq(207, IRQF_SHARED, "omo-bothep-ep0") rc=0
omo-bothep: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-bothep: [post0] blk 0x400 ... +10=83e13000 +14=1f +18=00000004 +1c=00000010 ...
omo-bothep: [post0] blk 0x450 ... +18=00000002 +1c=00000010 ...
omo-bothep: [post0] blk 0x4a0 ... +18=00000002 +1c=00000010 ...
...
omo-bothep: [seq post0 step1 +3860ms] SR ch0 wptr/rptr: primary(ep1)=0x00000410/0x00000010 secondary(ep0)=0x00000410/0x00000010
omo-bothep: [seq post0] STEP 2 - ring program + commit via SECONDARY BAR0 (0x40000000)
omo-bothep: [seq post0 step2 +6500ms] SR ch0 wptr/rptr: primary(ep1)=0x00000410/0x00000010 secondary(ep0)=0x00000410/0x00000010
omo-bothep: done (... irq=209 irq_taken=23245 irq_handled=2 msgs=3 services=3 ... pumped=192)
omo-bothep: done ep0 (irq=207 irq_taken=0 irq_handled=0) iatu_program=1 outwin=1 seq=1
```

Both RCs were decoded, both lines requested, and the interrupt fired — but the device rewrote
`SR+0x18` to `0x04` (its firmware's own init, exactly as `sr-sibling.md`/`sr-trigger.md` saw) and
`SR+0x1c` never moved from `0x10`. Re-programming the same quoted values through **ep0's** BAR
*after* release ("STEP 2") did **not** re-arm the fetch. `pumped=192` (the seq test's two laps plus
the service pump) and no `SR engine read`. **[proven]**

### B.5 Boot 2 — ep0 primary (ep0's BAR drives the rings): the engine fetches

Loader args changed only in the endpoint roles: `domain=0 domain2=1 irq=207 hostirq=207 irq2=209
hostirq2=209 ...`. Decisive lines (`020_boot2_full_dmesg.txt`, `021_boot2_decisive.txt`):

```
omo-bothep: endpoint 0000:00:00.0 id 59e7:0005
omo-bothep: BAR0 base=0x40000000 (config space), BAR2=0x41800000 (iatu_bar1)
omo-bothep: second endpoint 0001:00:00.0 id 59e7:0005
omo-bothep: ep0 BAR0 base=0x58000000, BAR2=0x59800000 (iatu)          [label names are stale; roles swapped]
omo-bothep: ep1 programmed 6 viewports                               [= ep0 primary's iATU, base 0x40000000]
omo-bothep: ep0 programmed 6 viewports                               [= ep1 secondary's iATU, base 0x58000000]
omo-bothep: request_irq(207, IRQF_SHARED, "omo-bothep") rc=0 - IRQ path live (primary ep1)
omo-bothep: ep0 request_irq(209, IRQF_SHARED, "omo-bothep-ep0") rc=0
omo-bothep: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-bothep: [post0 +530ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
omo-bothep: [post0] blk 0x400 +00=00000001 ... +10=82da6000 +14=1f +18=00000400 +1c=00000400 ...
omo-bothep: [post0] blk 0x450 ... +18=00000400 +1c=00000010 ...
omo-bothep: [post0] blk 0x4a0 ... +18=00000400 +1c=00000010 ...
omo-bothep: [pump seq1 primary/ep1 BAR0 +1040ms] SR ch0 refilled 32 node(s) (wptr=0x00000400 rptr=0x00000400 outstanding=0) commit SR+0x18 <= 0x00000000 readback=0x00000000
omo-bothep: [seq post0 step1 +1280ms] SR ch0 wptr/rptr: primary(ep1)=0x00000000/0x00000400 secondary(ep0)=0x00000000/0x00000400  -- DEVICE FETCHED
omo-bothep: [seq post0] STEP 2 skipped - the device fetched after the PRIMARY write
omo-bothep: done (... irq=207 irq_taken=0 irq_handled=0 msgs=3 services=4 ... pumped=96)
omo-bothep: done ep0 (irq=209 irq_taken=52507 irq_handled=2) iatu_program=1 outwin=1 seq=1
```

The engine consumed the pre-release lap (SR ch0 `rptr 0x10 -> 0x400`) **before any seq step**, and
consumed the pump's next lap as well (`wptr 0x00 / rptr 0x400`). The interrupt fired on **irq 209**
(52507); ep0's own line 207 took 0. The D2H handshake ran (`out[1]` id 6 `0x40` at +290 ms, id 2
`0x04`), and the full field dump matched the vendor except the dynamic indices. **[proven]**

`omo_seq_program` therefore never needed STEP 2: with both RCs decoded **and** the ring programmed
through ep0's BAR, the fetch is armed by the pre-release commit. The single-endpoint failures were
not a post-release timing problem — they were the wrong function on the register path.

### B.6 Was the fetch ep0's BAR, or the mere claiming of both? [proven, A/B]

Boot 1 and boot 2 use the **same `bothep.ko`**; the only difference is the loader's `domain`/
`domain2` (and hence which BAR0 carries the ETE ring/descriptor programming and the release). Boot 1
(ep1 BAR) failed, boot 2 (ep0 BAR) succeeded. In boot 1, ep0's RC *was* decoded and ep0's BAR was
re-programmed post-release by the seq test — and it still did not fetch. So the decisive variable is
the function that issues the ETE register/descriptor programming (and the release), not the number
of RCs claimed, and not a post-release re-arm. This matches A.2's static finding. **[proven]**

---

## Part C — which configuration works, did the chip answer, next blocker

**Which configuration made the engine fetch AND the interrupt fire:** claim **both** functions
(`0000:00:00.0` first + `0001:00:00.0`), program **both** RCs' iATU (six inbound viewports + one
outbound window each), issue the ETE ring/descriptor programming, the SR/DR posts and the `0x5a5a`
release through **ep0's BAR0/BAR2** (domain 0), and request/bind the ISR to **irq 209** (ep1). Boot 2
is the proof: `SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400` **and** `irq 209 irq_taken=52507`, in
the same run. **[proven]**

**Did the chip consume and answer?** It consumed the descriptors (the ETE engine read every posted
node, twice) and it ran the device->host handshake (`out[1]` id 6 then id 2), but it did **not**
answer the H2D message:

* `out[0]` (H2D mask, CA `0x40039010`) is written only by our own id-3/id-5 doorbells and is
  **never cleared by the device** — no `H2D MASK CLEARED BY DEVICE` line in either boot;
* `out[1]` never carries the id-1 bit; the one `ID-1 READY!` line in boot 1 is our own posted frame
  decoded before the release, not a device reply;
* no `0x5a5a` payload lands in the DR buffers, and the DR device index stays at its idle baseline.

**Named next blocker: the device-side H2D/HCC message accept, downstream of the descriptor fetch.**
The ETE engine now proves the descriptor ring is correct and reachable (it reads the nodes) and the
completion interrupt is live, so the remaining gate is the firmware's HCC message service that would
take `out[0]` down and emit the id-1 reply. The concrete candidates, unchanged from phase 21 but now
isolated with both halves live:

1. the synchronous transmit arm `out[5]` (CA `0x400392f0 <= 8`, `pcie_msg_send_irq` @`0x174a8`) —
   the asynchronous `pcie_msg_send` used here never writes it, and `enable=1` (which does) was
   proven to hang the chip (`fw-accept.md`); a safer graded arm (e.g. `out[5]` only after the DR
   path is quiesced) is the next experiment;
2. the firmware's H2D dispatcher at file `0x818a8` (`str r1(=0), [r2]` with `r2 = ctx+4 = out[0]`,
   `docs/phase20/fw-accept.md` A.2) — present but never entered;
3. whether the host SR node's HCC `id`/group must match a firmware-registered channel: the posted
   frame is the vendor's byte-exact id-1 frame, but the firmware may require the per-channel
   context (`pcie_ete_chn_res`/`pcie_msg_init`) to be the one it built on this function.

`enable=1` (`out[5]`) was **not** touched in either boot; CA `0x400392f0` and the RC misc window
`0x10161000` were never written.

---

## Recovery and health

Recovery cancelled with `touch /tmp/omo-bothep.done` and run by hand (`080_final_health.txt`); the
recovered vendor boot verifies:

```
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
irq 209 hisi_pci_intx live ; irq 207 = 0
2 wiphys, 6 interfaces (vap0 Cudy-1C73 2g / vap8 Cudy-1C73-5G 5g + guest vaps)
br-lan 192.168.10.1/24 UP
leftovers: no *.omo-off, no bothep.ko, no init.d/omo-bothep, no S99omo-bothep,
           no /root/recover-bothep.sh, no /tmp/bothep.ko
pstore: no new record (blk-0 10:41, blk-1/2 14:37 unchanged)
```

Pre-existing (not from this task): the dangling `/etc/rc.d/S99omo-rtmsg` symlink and the
`/root/recover-*.sh` scripts left by earlier phase lanes are still present; they predate this phase
and were not touched.

Boots this session: two `bothep` takeover boots (both real module boots, both captured) plus one
recovery boot. No panic, no chip hang.

## Writes per takeover boot

Both boots: `pci_enable_device` + `pci_request_mem_regions` on **both** functions; six inbound
viewports + one outbound window on **each** RC; `PCI_COMMAND=7`; the 928,920-byte firmware (read
back); the SR/DR program registers; the glue `+0x2e8` RMW; the pre-release ETE-intr RMW CA
`0x40039508 &= 0xffe0f8f8`; the post-release SR re-assert (base/depth/wptr/ctrl per channel); the
`0x5a5a` release; the host message service words; the SR producer commits; and the two
unproven-but-quoted `PCI_INTERRUPT_LINE` bytes (209 on ep1, 207 on ep0). Boot 1 additionally ran the
seq test's second (secondary-BAR) re-program. Neither boot wrote CA `0x400392f0` (`enable=0`); the
RC misc window `0x10161000` was never touched.

## Artifacts

```
lab/bothep/bothep.c                       the module (srt + second endpoint + dual view + seq test)
lab/bothep/Makefile, omo-bothep           build + one-shot loader (domain=0 primary, irq 209)
lab/bothep/stage-bothep.sh, recover-bothep.sh
build/tmp/phase21/bothep-ko/bothep.ko     the module (md5 aef02c20d8b51c90ca51b83d39514ff3)
build/register-dumps/bothep/000_live_vendor_interrupts.txt  live /proc/interrupts + module state
build/register-dumps/bothep/001_live_vendor_bars2.txt       BOTH endpoints' BAR2 iATU (Part A)
build/register-dumps/bothep/002_live_vendor_dmesg.txt       live vendor probe/region/irq dmesg
build/register-dumps/bothep/003_disasm_pcie_main_init.txt   pcie_ete_init call site
build/register-dumps/bothep/004_disasm_devres.txt           pci_dev_res_init / lres / set_inbound
build/register-dumps/bothep/000_recovery_plan.txt           recovery recorded before booting
build/register-dumps/bothep/010_boot1_full_dmesg.txt, 011_boot1_decisive.txt
build/register-dumps/bothep/020_boot2_full_dmesg.txt, 021_boot2_decisive.txt
build/register-dumps/bothep/080_final_health.txt
```
