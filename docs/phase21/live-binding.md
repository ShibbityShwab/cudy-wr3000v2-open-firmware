# live-binding: the vendor's device-side ETE/glue binding, captured live (phase 21, 2026-10-02)

Task `st_01a0fb83`. Direct sequel to `docs/phase21/sr-pump.md`, which fixed the SR producer
phase bit (`SR+0x18` `0x400 -> 0x000`, proven live) and named the blocker: the device-side HCC
accept gate / ETE glue binding (the status `[[ctx+4]]+0x2ec` unresolved and reading 0
everywhere reachable) plus the absent interrupt line. All knowledge of it was static
disassembly. This phase captures the vendor's own binding live: kprobes/kretprobes on the
vendor's functions on a **normal vendor boot**, plus a read-only read-back of the live state
through the endpoint's BAR windows; then it implements the two missing device-side writes in
`lab/bind` and tests them in a takeover boot.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, a
kprobe-captured value, or a value the device printed) or **[inferred]**.

---

## Headline

**The unresolved status word is device CA `0x400392ec`, in the message/glue block
(CA `0x40039000`), not in the ETE ring block (`0x4003a000`).** The vendor's `pcie_intr_handle`
@`0x82e4` reads `*(*[ctx+4] + 0x2ec)`; a kprobe on the live vendor stack shows
`*[ctx+4] = 0xc9ab9000`, and the boot capture shows that VA is the mapped base of device CA
`0x40039000` (the block that carries `out[0]` CA `0x40039010`). So the status is
CA **`0x400392ec`**, and the earlier takeover read the wrong block's `+0x2ec`
(CA `0x4003a2ec`, always 0) and probed a third candidate (CA `0x400002ec`, `0x2292` but not
`[ctx+4]+0x2ec`). **[proven]**

**Two device-side binding writes our takeover never performed, both captured live from the
vendor's own init path and now applied in `lab/bind`:**

| # | write | vendor code | live vendor value | takeover pre-state | after bind |
|---|-------|-------------|-------------------|--------------------|------------|
| 1 | ETE interrupt block CA `0x40039508` `&= 0xffe0f8f8` | `pcie_ete_intr_init` @`0x7528` | `0x3f201818` | `0x3f3f1f1f` | `0x3f201818` (match) |
| 2 | glue channel-resource CA `0x400392e8` `&= 0xfffffc20` | `pcie_ete_chn_res` @`0x7490` | `0x00000020` | `0x000003ff` | `0x00000020` (match) |

The earlier takeover (`srpump`) did write a `& 0xfffffc20` mask, but to the **ETE ring block**
offsets (`0x4003a6e8` …), missing the real register at CA `0x400392e8`. **[proven]**

**The chip still does not take the frame.** With both binding writes applied and matching the
vendor's live values, and the real glue status now observable and non-zero
(`0x18 -> 0x08 -> 0x09`), the device still never clears `out[0]` (CA `0x40039010`), never posts
the id-1 reply, and `irq_taken = 0`. The named next blocker is now concrete: the message
interrupt is delivered on the **sibling endpoint** `0001:00:00.0` (irq **209**, 5719-15739
interrupts on a normal vendor boot) while the takeover claims `0000:00:00.0` (irq **207**),
which carries **0** interrupts even on a fully working vendor boot. **[proven]**

---

## Part A — the vendor's device-side binding, captured live on a normal vendor boot

### A.0 Method

Two read-only measurements on the normal vendor stack (no takeover, no `rmmod`, no device
register writes, RC misc window `0x10161000` never touched):

1. **Boot binding capture.** A one-shot init aid (`/etc/init.d/omo-livebind`, `START=08`)
   busy-polls for `/sys/module/hi5622v100_plat`, then writes the whole kprobe set to
   `kprobe_events` in **one** write so it lands inside the ~300 ms window between module load
   and `pcie_main_init`. This was necessary: with 26 probes appended one-per-`echo` the arming
   took ~3 s and missed the window (first two attempts: 0 binding hits), while the single write
   armed in time (255 hits). The aid is removed afterwards.
2. **Live state read-back.** `devmem` through endpoint 0's BAR0 region-3 viewport
   (`host 0x403b8000 -> device CA 0x40000000`, so `host = 0x403b8000 + (CA - 0x40000000)`),
   read-only.
3. **Runtime glue read.** A kprobe at `pcie_intr_handle+0x20` (after the base pointer is
   loaded) fetches `base=%r3` and `status=+0x2ec(%r3)`, exercised by ordinary Wi-Fi traffic on
   the live stack.

Probe set (26 events, all validated): entry probes on `pcie_main_init`, `pcie_ete_init`,
`pcie_comm_init`, `pcie_msg_init`, `oal_pcie_set_inbound_by_viewport`, `bal_irq_enable`,
`pcie_ete_rings_init`, `pcie_ete_init_src_ring`, `pcie_ete_init_dst_ring`,
`pcie_ete_intr_init`, `pcie_ete_chn_res`, `pcie_ete_dr_init`, `pcie_ete_sr_init`; store-point
probes at `pcie_ete_intr_init+0x90`, `pcie_ete_chn_res+0x90`, `pcie_ete_sr_reg_init+0x68/0x88/0x94/0xac`,
`pcie_ete_dr_reg_init+0x4c/0x6c/0x78`; entry+return probes on `oal_pcie_inbound_ca_to_va` and
`pcie_hostca_to_devva`. The store offsets were fixed from the disassembly of those functions
(each is the `str` that commits a register). **[proven]**

### A.1 The ordered boot binding capture

`build/register-dumps/livebind/011_bootprobe_trace.txt` (kernel timestamps, `chip 0`):

```
16.145635  pcie_main_init                       chip=0x0
16.229664  pcie_ete_init                        r0=0x0 r1=0xbf942218   (chip ctx)
16.229670  pcie_ete_intr_init                   r0=0x0 r1=0xc4fe9f00   (ETE ctx)
16.229680  pcie_ete_intr_init+0x90  CA 0x40039508 <= 0x3f201818   <-- binding write #1
16.229683  pcie_ete_rings_init                  r0=0x0 r1=0xc4fe9f00
16.229690  pcie_ete_init_src_ring               r0=0xc4fe9f00
16.229722  pcie_ete_sr_init  x3 (ch0..2)        base=0x865dc000/0x865db000/0x865da000, depth-1=0x1f, wptr=0, ctrl=0
16.229831  pcie_ete_init_dst_ring               r0=0xc4fe9f00
16.229848  pcie_ete_dr_init  x4 (ch3..6)        base=0x865d9000/…/0x865d6000, depth-1=0x1f, wptr=0
16.229942  pcie_ete_chn_res                     r0=0x0
16.229947  pcie_ete_chn_res+0x90   CA 0x400392e8 <= 0x00000020    <-- binding write #2
16.229951  pcie_comm_init                       r0=0xbf942218
16.230080  pcie_msg_init                        r0=0xbf942218
16.594016  bal_irq_enable                       r0=0x0
```

The SR base values use `pcie_hostca_to_devva` (host CA -> device VA), and they match the live
read of the ring block exactly (A.3), so the capture is the working vendor state. **[proven]**

Note: the three SR channels were all written with `ctrl (+0x08) = 0`, not 1; the earlier takeover
wrote 1. Minor and not pursued (the live vendor reads 0 there). **[proven]**

### A.2 The VA ↔ device CA mapping (this is what places `[ctx+4]+0x2ec`)

The captured store targets and the live addresses agree on a single offset
`VA - CA = 0x89a80000` for this boot:

| VA (kernel) | device CA | what |
|---|---|---|
| `0xc9ab9000` | `0x40039000` | message/glue block base — also the runtime `*[ctx+4]` |
| `0xc9ab9508` | `0x40039508` | ETE interrupt block (`pcie_ete_intr_init` target) |
| `0xc9aba400` | `0x4003a400` | ETE SR0 block (`+0x10` base -> CA `0x4003a410`) |
| `0xc9aba590` | `0x4003a590` | ETE DR3 block (`+0x30` base -> CA `0x4003a5c0`) |

The vendor's own service code independently pins `*[ctx+4]` to the glue block: a kprobe at
`pcie_intr_handle+0x20` on the live stack gives `base=0xc9ab9000 status=0x8/0x10`, i.e. the
status word is CA `0x400392ec` (block base `0x40039000`). **[proven]**

### A.3 Live state on the working vendor boot vs the takeover

Read-only through endpoint 0 BAR0 (`build/register-dumps/livebind/021_live_state_vendor.txt`):

| device CA | working vendor boot | prior takeover (srpump) | note |
|---|---|---|---|
| `0x400392e8` (glue chn_res) | `0x00000020` | wrote mask at `0x4003a6e8` instead | **binding write #2** |
| `0x400392ec` (glue status `[ctx+4]+0x2ec`) | `0` idle; **`0x8`/`0x10` dynamic** | read `0x4003a2ec` = 0 always | the real status word |
| `0x40039508` (ETE intr block) | `0x3f201818` | never written | **binding write #1** |
| `0x4003a410/460/4b0` (SR base) | `0x865dc000/865db000/865da000` | same addresses programmed | rings were right |
| `0x4003a418` (SR0 wptr) | `0x00000408` (advanced) | phase-fixed, stopped at 0 | pump works, device stalls |
| `0x400002ec` (SSU block `+0x2ec`) | `0x00002292` | probed read-only | a third candidate, not `[ctx+4]` |
| mailbox `out[0..5]` | all `0` when idle | module re-rings them | — |

The "difference set" is therefore: the takeover was missing the ETE interrupt-block write and the
glue channel-resource write, and was reading the wrong block for the glue status. Everything
else (rings, doors, mailbox CAs) already matched. **[proven]**

### A.4 Runtime glue status (vendor service loop)

`build/register-dumps/livebind/040_runtime_glue_status.txt`, kprobe at
`pcie_intr_handle+0x20` on the live stack (irq 209):

```
base=0xc9ab9000 status=0x10   (x55)
base=0xc9ab9000 status=0x8    (x12)
base=0xc9ab9000 status=0x0    (x12)
```

Bits 3 (`0x8`) and 4 (`0x10`) are inside the vendor's dispatch mask `0x3d8`. This is the read
our static analysis could not place: it is the *message/glue* block, at `+0x2ec`. **[proven]**

---

## Part B — the missing writes, the module, and the takeover test

### B.1 Minimal ordered list of missing device-side writes

In vendor order:

1. **`pcie_ete_intr_init` (before the rings):** map CA `0x40039508`; `*p &= 0xffe0f8f8`.
   Captured live `0x3f3f1f1f -> 0x3f201818`.
2. *(rings are already correct in the takeover.)*
3. **`pcie_ete_chn_res` (after the rings):** CA `0x400392e8`; `*p &= 0xfffffc20`.
   Captured live `0x000003ff -> 0x00000020`.

Both are the vendor's own instructions and were reproduced with the same read-modify-write, so
they are `[proven]` writes, not guessed literals. No unproven-but-quoted write was needed. The
glue status at CA `0x400392ec` is read-only in the vendor (`pcie_intr_handle` only reads it; the
device clears it when the host acks/re-arms at `out[3]`/`out[4]`), so `bind` keeps it read-only.
CA `0x400392f0` (`out[5]`, the enable=1 hang family) is still never written. **[proven]**

### B.2 `lab/bind`

`lab/bind/bind.c` is `lab/srpump/srpump.c` with:

* the glue status read moved from `ETE_BAR0_OFF + 0x2ec` (CA `0x4003a2ec`) to
  `GLUE_BAR0_OFF + GLUE_STAT` (CA `0x400392ec`), and made read-only (no W1C — the W1C mask was
  writing an unused ETE offset and at the wrong block);
* the `& 0xfffffc20` RMW moved from the per-SR/DR ETE offsets to the single real register
  `GLUE_BAR0_OFF + 0x2e8` (CA `0x400392e8`);
* the new ETE interrupt-block RMW at `ETE_INTR_OFF` (CA `0x40039508`) applied before the rings,
  with pre/post logging.

Everything else (claim/decode/load/release, six inbound + one outbound iATU viewports, the
firmware, DR buffer posts, the SR pump, the service thread, `enable=0`) is carried from
`srpump` unchanged. `lab/bind/omo-bind` is the loader; `lab/bind/recover-bind.sh` the recovery.

Built by the module CI (`build-load-test-module.yml`, new `bind` step + `bind-ko` artifact):
run **36980065967**, commit `183f29e`, `bind.ko` md5
**`aae5c3a0b1a96d45592e367dcaf3253f`**, 54412 bytes, `vermagic=5.10.201`. **[proven]**

### B.3 The takeover boot

`build/register-dumps/livebind/061_testboot_full.txt` (811 `omo-bind` lines),
`062_decisive.txt`:

```
---- pcie_ete_intr_init CA 0x40039508 pre=0x3f3f1f1f mask=0xffe0f8f8 ----
  ETE intr 0x40039508    [0x000] <= 0x3f201818 readback=0x3f201818 match=YES
---- pcie_ete_chn_res glue CA 0x400392e8 (mask 0xfffffc20) pre=0x000003ff ----
  glue chn_res 0x400392e8 [0x2e8] <= 0x00000020 readback=0x00000020 match=YES
...
[glue svc +1210ms] CA 0x400392ec (BAR0+0x3f12ec) 0x00000018 -> 0x00000008 mask0x3d8=0x00000008
[glue svc +2650ms] CA 0x400392ec (BAR0+0x3f12ec) 0x00000008 -> 0x00000009 mask0x3d8=0x00000008
...
H2D MASK CLEARED BY DEVICE count = 0
out[1] values seen = {0x40 (id 6), 0x04 (id 2)}   -- never bit 0 (id 1)
SR ch0 commit SR+0x18 <= 0x00000000 (phase-fixed); rptr stays 0x400; DR rptr stays 0x10
done (release=1 rings=1 sr_posted=1 acpoff=0 svc=1 iters=170 glue_clears=0 pollms=100
      polldur=20000 irq=207 irq_taken=0 irq_handled=0 msgs=3 services=4 sendflag=1
      dr_events=4 sr_events=1 pumped=64)
```

Both writes landed and read back **exactly the vendor's live working values**. The real glue
status word is now observable and non-zero during the run (`0x18`, then `0x8 -> 0x9`), which the
earlier takeover could never see. But the chip's behaviour is unchanged: `out[0]` is only ever
written by the module's own doorbells and **never cleared by the device**, there is no id-1
reply, and `irq_taken = 0`. **[proven]**

---

## Part C — difference set, did the chip take the frame, next blocker

**Difference set (working vendor boot vs takeover):** the takeover was missing (1) the ETE
interrupt block write CA `0x40039508`, (2) the glue channel-resource write CA `0x400392e8`, and
(3) the correct glue status block (`0x40039000 + 0x2ec`, not `0x4003a000 + 0x2ec`). All three are
now reproduced; the remaining state (rings, doors, mailbox CAs, release `0x5a5a`) was already
identical.

**Did the chip take the frame?** No. `out[0]` was never cleared by the device, no id-1 reply, no
payload byte changed, and the host interrupt never fired. The binding writes are proven applied
and match the working system, so the accept gate is not the glue register contents alone.

**Named next blocker — the interrupt is not on our endpoint.** On the recovered normal vendor
boot, `/proc/interrupts` shows:

```
207: 0      GIC-0  91 Level  hisi_pci_intx     <- 0000:00:00.0 (what the takeover claims)
209: 5719   GIC-0  95 Level  hisi_pci_intx     <- 0001:00:00.0 (vendor's live message ISR)
```

and the runtime `pcie_intr_handle` kprobe fires only from irq 209. So the vendor's message
completion interrupt is delivered through the **sibling endpoint** (`0001:00:00.0`, domain 1,
irq 209); endpoint 0 (irq 207) carries **zero** interrupts even on a fully working vendor boot.
The takeover (`domain=0`) requests irq 207 and will always see `irq_taken=0`. The next step is
to take over **domain 1** (or determine the endpoint-to-INTx routing the RC uses) so that the
device's glue completions actually reach `pcie_intr_handle`/`pcie_msg_handle`; the `enable=1`
`out[5]` family remains forbidden (it hangs the chip). Note the glue status bits now visible on
the takeover endpoint (`0x8`/`0x9`) are the same bit class the vendor services on irq 209.
**[proven]**

---

## Artifacts

```
build/register-dumps/livebind/probes.txt                  the 26 kprobe definitions
build/register-dumps/livebind/omo-livebind                the S08 boot arming aid (removed after use)
build/register-dumps/livebind/005_arm_log.txt             arming log (armed in time on attempt 3)
build/register-dumps/livebind/010_bootprobe_trace.txt     first (missed) boot trace
build/register-dumps/livebind/011_bootprobe_trace.txt     full capture, 10906 lines (the good one)
build/register-dumps/livebind/020_decisive_boot_binding.txt  ordered binding sequence
build/register-dumps/livebind/021_live_state_vendor.txt   live state, working vendor boot
build/register-dumps/livebind/030_cleanup_rearm.txt       probe aid removal + watchdog re-arm
build/register-dumps/livebind/040_runtime_glue_status.txt pcie_intr_handle base/status
build/register-dumps/livebind/050_staging.txt             bind staging
build/register-dumps/livebind/060_first_contact.txt       takeover boot first contact + re-arm
build/register-dumps/livebind/061_testboot_full.txt       full takeover dmesg (1362 lines)
build/register-dumps/livebind/062_decisive.txt            takeover decisive lines
build/register-dumps/livebind/070_recovery_run.txt        recovery run
build/register-dumps/livebind/080_final_health.txt        recovered health
build/register-dumps/livebind/081_leftovers.txt           leftovers check
build/register-dumps/livebind/082_irq_routing.txt         /proc/interrupts + driver binding
build/tmp/phase21/bind-ko/bind.ko                         built module (md5 aae5c3a0…)
```

Boots this session: three normal vendor boots for the probe capture (attempt 1 missed on a bad
symbol pattern; attempt 2 missed because arming took ~3 s; attempt 3 captured), one takeover
boot (`bind`), one recovery boot. No panic; pstore records unchanged (`blk-0` 10:41, `blk-1/2`
14:37).

## Writes per takeover boot

Six inbound iATU viewports + one outbound viewport + `PCI_COMMAND=7` + the 928,920-byte firmware
(read back) + the SR/DR program registers + **the new ETE interrupt-block RMW CA `0x40039508`**
+ **the new glue channel-resource RMW CA `0x400392e8`** + the `0x5a5a` release + the host
`out[1]`/`out[3]`/`out[4]` service words + `out[0]`/`out[2]` for the id-3/id-5 doorbells. The
glue status CA `0x400392ec` is read-only; the RC misc window `0x10161000` was never touched;
CA `0x400392f0` (`out[5]`) was never written (`enable=0`).

## Recovery and health

The device-side watchdog was armed before staging and re-armed in the takeover boot as soon as
the box answered (`060_first_contact.txt`, pid 6803). Recovery was cancelled with
`/tmp/omo-bind.done` and run by hand (`070_recovery_run.txt`); the recovered boot verifies:

```
2 wiphys: phy0 phy1 ; radios vap0 (2g) / vap8 (5g) AP up
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
calibration: get_2g_power_param -> [SUCC]17161605 ; get_5g_power_param -> [SUCC]00000000
br-lan 192.168.10.1/24 UP ; HTTP OK
leftovers: no *.omo-off, no bind.ko, no init.d/omo-bind, no S99omo-bind, no omo-livebind,
           no /root/recover-bind.sh, watchdog script removed, /tmp clean
pstore: no new record
```
