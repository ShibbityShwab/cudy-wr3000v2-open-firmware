# wifi-rx-vendor-diff - the definitive VENDOR-vs-OURS RX state diff (task-162)

**Lane:** wifi-rx-vendor-diff (shared task task-162).  **Host-only**: this lane
reads committed vendor dumps/disassembly and builds a module on the host; it
never touches the bench.  The Lead fires.
**Written:** 2026-11-26.  **Base tree:** FINAL association tree + corrected
mgmt-TX carrier + RX refill watermark (the chain whose artifact is
d2c608e7... - see the watermark lane; this lane builds on the same staged tree
and adds 1 052 patch lines / 6 files).

Evidence markers: **[V]** decoded from a named committed or local vendor
artifact; **[V*]** hardware-observed by an earlier attended lane and quoted from
the committed reports; **[I]** inference; **[U]** unknown.

**Deliverables:** [REPORT.md](REPORT.md) (this file),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [BUILD-NOTES.md](BUILD-NOTES.md),
[patches/0001-rx-vendor-diff.patch](patches/0001-rx-vendor-diff.patch) (6 files),
[src/](src/) (portable logic + host test),
[tools/](tools/) (host-test runner + module rebuild script), [evidence/](evidence/).

---

## 0. TL;DR - what the diff actually says

The MAC RX captures nothing even though the free-ring consumer is armed
(live bank CA 0x40050000 status=0x182, norm/small rptr=0x20, cmp_wptr=0).  This
lane built the byte-exact diff the Lead asked for: the vendor's **stock** RX
state (committed register dump opensource/dumps/reg_all.txt, taken with the
vendor driver live) against our driver's state, rebuilt from the committed
sources, plus the vendor's static RX-enable program decoded from the committed
disassembly.

**The headline negative:** every host-writable RX-enable word the vendor driver
programs is already programmed by our driver with the same value.  The vendor's
whole RX enable is the 12-write free-ring/completion-ring program
(shuangta_rx_host_init_dscr_queue 0x37888) + the full ring fill + one
interrupt-mask word (0xfffffcc1).  Our 14-write program covers all of it and
the stock dump matches field-for-field; there is **no fifth enable register, no
missing mask bit, no missing ring field, no SET_RX_FILTER message** [V]/[V*].
This confirms (and hardens with a stock-value table) the rx-static lane's
conclusion; it also means the remaining gap is **not** "a register we forgot".

**Three real deltas came out of the diff**, and this lane instruments or fixes
each one:

1. **Unobserved device-owned live-bank state.**  The vendor stock dump shows
   live-bank words **+0x14 = 0x00200020** and **+0x24..+0x30 =
   0x00800000/0x01800800/0x02801000/0x03801800** [V].  No host driver accessor
   exists for them in 2.4.15/2.5.24 (their writer is device/firmware side), and
   our driver has never read them.  If ours read zero on the next fire, the
   device-side RX/MAC state never reached the vendor shape - the highest-value
   open question, now answerable with **zero device writes** via the new
   read-only dp_rx_parity action.
2. **Host-MAC interrupt-status service is missing a vendor write.**  The vendor
   ISR always W1Cs the served status bits to config **+0x44** and forwards the
   observable bits (we see **bits 1 and 8** active; both are the vendor's
   firmware-forward class) as HCC messages to the firmware core [V].  Our
   driver never clears the word by default (rx_clear_status=0) and never
   forwards anything.  This lane adds the one safe missing write - a W1C of
   **only the RX-completion bit 2** - behind the fail-closed
   rx_status_service=1 (dp_rx_status_clear).  The forwarding messages are
   documented as a remaining gap, not implemented (payloads/risk not proven).
3. **Crash containment for the drain.**  The Lead's mid-session exception
   (mask CA 0x400008 = 0x04000010, ARM vector 4 = data abort) appeared after
   RX drain loops.  Analysis below shows the **idle** drain cannot have been the
   trigger (pending=0 -> no writes; the watermark pass is a no-op at posted
   224/480 > target 192/384).  But because the drain is the only RX pointer
   mover, this lane makes it **observe-only by default** (rx_drain_write=0,
   new param): it proves "the MAC wrote completions" by reading the completion
   ring memory and returned buffers **without publishing a single index**.  The
   old pointer-moving path is one explicit parameter away.

**Built and host-tested, NOT fired:** module 361 872 B sha256 81ab72c1...,
**59 params**, 0 warnings, strip leaves .text byte-identical, reproducible
(.text identical from a fresh patched tree).  Host tests **440/440 PASS**.  The
attended ladder (read-only first) is in [FIRE-RECIPE.md](FIRE-RECIPE.md).

---

## 1. Inputs, method, evidence pins

| artifact | used for | marker |
| --- | --- | --- |
| opensource/dumps/reg_all.txt (25 154 words) | vendor **stock live** register values, vendor driver running, band 0 | [V] |
| committed disassembly docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis / -plat.dis | every vendor function address / write order below | [V] |
| docs/phase43/channel-register-offsets.md, docs/phase43/THE-CHANNEL-MAP.md | machw array word 5 = 0x40040000 config block, word 8 = 0x40050000 live bank | [V] |
| docs/phase4/mmio-map.md | the hal_set_* family writes host RAM **shadows** for the cfg family; MMIO access goes through oal_pcie_devca_to_hostva | [V] |
| docs/incoming/wifi-rx-static/ | live-bank map, 36-B header, ring arithmetic, arm/drain implementation | [V]/[V*] |
| docs/incoming/wifi-rx-watermark/ | the current drain/refill behaviour built upon | [V] |
| docs/incoming/wifi-sibling-gpl/ (T2/T3) | SET_RX_FILTER negative on luofu; g1 id13 = mgmt RX | [V] |
| vendor 2.4.15 / 2.5.24 modules (local, never committed) | confirmed the disassembly-level decode on both shipped versions | [V*] |

Method: decode the vendor RX enable from shuangta_rx_host_init_dscr_queue
(0x37888) + shuangta_host_rx_add_buff (0x374d0) + shuangta_host_chip_irq_init
(0x3a6cc) + the ring setters; diff each write against the clean-room 14-write
program (dp_rings.c); then diff the runtime state against the vendor stock
dump word by word.  No vendor binary, firmware byte, code text or per-unit value
is committed; tracked text carries derived offsets/values and original C only.

---

## 2. The vendor RX enable - the complete static write set [V]

shuangta_rx_host_init_dscr_queue(dev) (0x37888), executed once per device/band
at hal_main_init after the firmware handshake; calls
shuangta_host_rx_add_buff (0x374d0) to fill both free rings; the final
interrupt mask comes from shuangta_host_chip_irq_init (0x3a6cc):

| # | action | register / object | value (band 0) | decode |
| ---: | --- | --- | --- | --- |
| 1 | alloc norm free ring | host DRAM 0x400 B | 256 x 4 B, type 1 | 0x37908-0x37930 |
| 2 | norm buffer length | cfg **+0x1c bits 15:0** | **0x63c** | setter 0x326c4 |
| 3 | norm ring address | cfg **+0x20** | device VA of ring | setter 0x326dc |
| 4 | norm ring depth | cfg **+0x2c bits 11:0** | **0x100** | setter 0x3263c |
| 5 | alloc small free ring | host DRAM 0x800 B | 512 x 4 B | 0x379b8-0x379e0 |
| 6 | small buffer length | cfg **+0x1c bits 31:16** | **0xb0** | setter 0x3255c |
| 7 | small ring address | cfg **+0x24** | device VA of ring | setter 0x32704 |
| 8 | small ring depth | cfg **+0x30 bits 11:0** | **0x200** | setter 0x32598 |
| 9 | alloc completion ring | host DRAM 0x3ffc B | 4095 x 4 B, type 2 | 0x37a8c/0x37ab4 |
| 10 | completion ring address | cfg **+0x28** | device VA of ring | setter 0x3262c |
| 11 | completion ring depth | cfg **+0x34 bits 11:0** | **0xfff** | setter 0x326ec |
| 12 | fill norm + small rings | free-ring memory | 256 + 512 entries, each = **devVA(buffer+4)**; skb pointer at buffer+0; **36-byte header zeroed at buffer+4** on every add | shuangta_host_rx_add_buff 0x374d0 |
| 13 | publish producers | cfg **+0x38 / +0x3c** | packed idx 0 / phase 1 = **0x8000** | hal_ring_set_sw2hw 0x31f1c |
| 14 | host MAC int mask | cfg **+0x48** | **0xfffffcc1** | shuangta_host_chip_irq_init 0x3a6cc (also unmask(3)/(4) on +0x48 after each fill - already zero bits with this mask) |

Note on the two words the vendor register dump forces into the picture:
cfg +0x4c/+0x50/+0x54 are **never read or written** by the vendor (the stock
dump keeps them 0; the ring binder stores the *live* consumer pointers from
**live bank +0x08/+0x0c** and the completion producer from **+0x10**) [V] - the
rx-static correction, re-confirmed here.

cfg_intr_mask_host = 0x1f is written by chip IRQ init into a **host RAM
shadow** (hal_set_cfg_intr_mask_host_cfg_intr_mask_host 0x328a4 writes
[r1+0x1b0], with r1 from hal_device_get = g_pst_hal_board + ...; phase4
section 2.2/5.4) - **not** an MMIO register, therefore not a candidate for the
RX capture symptom (this corrects the "[U] cfg/lmi bank" open item in the
rx-static report: the write is real but shadow-only).

### 2.1 Vendor vs our RX-init program - byte-for-byte

| word | vendor order | clean-room dp_mac_program | stock value | verdict |
| --- | --- | --- | --- | --- |
| +0x1c | norm len (bits 15:0) then small len (bits 31:16) | one write 0x00b0063c | 0x00b0063c | MATCH |
| +0x20/+0x24/+0x28 | ring addresses in that order | same three writes | 0x844c7000/844c8000/83734000 | MATCH shape (host addresses differ per boot) |
| +0x2c/+0x30/+0x34 | depths 256/512/4095 | same | 0x100/0x200/0xfff | MATCH |
| +0x38/+0x3c/+0x40 | producers after full fill; completion consumer 0 | same values | 0x8000/0x8000/0 | MATCH |
| +0x48 | 0xfffffcc1 | 0xfffffcc1 | 0xfffffcc1 | MATCH |
| +0x00/+0x04 | tx_ba depth/addr (same block) | programmed | 0x100 / 0x83fd7000 shape | MATCH |
| +0x10 | msdu-info table ACP (ready g4 id2 payload) | programmed | 0x02060850 shape | MATCH |

**Conclusion (evidence-backed negative):** there is no missing RX-enable
register write in our driver.  The check is encoded as
hi5622_rx_init_program_check() and pinned by 50+ host checks so a future edit
cannot silently drop a required word.

---

## 3. Vendor stock vs our live state - the word-by-word diff

### 3.1 Config block CA 0x40040000 (host interface, band 0)

stock = reg_all.txt (vendor driver live).  kind = who owns the word.
ours = what the committed clean-room driver writes/keeps (from the 14-write
program + runtime actions).  The new "cat dp_rx_parity" renders exactly this
table at runtime, read-only.

| +off | stock | kind | ours | parity meaning |
| --- | --- | --- | --- | --- |
| 0x00 | 0x00000100 | host fixed | 0x100 | MATCH |
| 0x04 | 0x83fd7000 | host devVA | txba devVA | RANGE_OK |
| 0x10 | 0x02060850 | host ACP devVA | ready-id2 ACP | RANGE_OK |
| 0x1c | 0x00b0063c | host fixed | 0x00b0063c | MATCH |
| 0x20 | 0x844c7000 | host devVA | norm ring devVA | RANGE_OK |
| 0x24 | 0x844c8000 | host devVA | small ring devVA | RANGE_OK |
| 0x28 | 0x83734000 | host devVA | completion ring devVA | RANGE_OK |
| 0x2c | 0x00000100 | host fixed | 0x100 | MATCH |
| 0x30 | 0x00000200 | host fixed | 0x200 | MATCH |
| 0x34 | 0x00000fff | host fixed | 0xfff | MATCH |
| 0x38 | 0x00008000 | host dynamic | 0x8000 after fill | DYNAMIC |
| 0x3c | 0x00008000 | host dynamic | 0x8000 after fill | DYNAMIC |
| 0x40 | 0x00000000 | host dynamic | 0 until a write-enabled drain | DYNAMIC |
| 0x44 | 0x00000000 | W1C dynamic | never written by default | DYNAMIC (gap - section 4.2) |
| 0x48 | 0xfffffcc1 | host fixed | 0xfffffcc1 | MATCH |
| 0x4c/0x50/0x54 | 0 | **vendor never touches** | 0 | MATCH (negative) |
| 0x58 | 0 | [U] | 0 | MATCH |
| 0x5c | 0x00000004 | [U] (no accessor) | never writes | 4 if device-owned |
| 0x6c | 0x00000004 | [U] (no accessor) | never writes | 4 if device-owned |

### 3.2 Live bank CA 0x40050000 (device telemetry + ring pointers)

| +off | stock (vendor idle) | kind | our arm read | meaning |
| --- | --- | --- | --- | --- |
| 0x00 | 0x00000400 | dynamic | **0x00000182** | vendor 0x400 is fully masked (0xfffffcc1); ours is NOT: active = **0x102** = bits 1 and 8 (section 4) |
| 0x04 | 0 | [U] device | (unread before this lane) | now printed |
| 0x08 | 0x00000020 | dynamic | **0x00000020** | norm free-ring consumer: MATCH (the MAC pre-fetched 32 entries) |
| 0x0c | 0x00000020 | dynamic | **0x00000020** | small ring consumer: MATCH |
| 0x10 | 0x00000000 | dynamic | **0x00000000** | completion producer: no frame, MATCH with vendor idle |
| 0x14 | **0x00200020** | **device-owned** | (new action) | unexplained RX/MAC meta: if ours reads 0 -> device RX state not initialized |
| 0x18/0x1c/0x20 | 0 | [U] device | new action | parity only |
| 0x24 | **0x00800000** | **device-owned** | new action | queue meta 0 |
| 0x28 | **0x01800800** | **device-owned** | new action | queue meta 1 |
| 0x2c | **0x02801000** | **device-owned** | new action | queue meta 2 |
| 0x30 | **0x03801800** | **device-owned** | new action | queue meta 3 |
| 0x34..0x40 | 0 | [U] device | new action | parity only |

The five bold words are the **only vendor-nonzero RX state with no identified
host writer**.  They are the diff's most important output: the next fire reads
them for free and settles whether the device RX path is initialized.

### 3.3 Ring geometry, entry and buffer format

| property | vendor [V] | ours | verdict |
| --- | --- | --- | --- |
| norm / small / completion depth | 256 / 512 / 4095 (band 0) | same | MATCH |
| ring entry | 4 B, device VA of **buffer+4** | same (dp_rx_buf_entry_devva) | MATCH |
| buffer DMA area | 0x63c / 0xb0 after the 4-B host word | same | MATCH |
| host word | skb pointer at buffer+0 | pool pointer bookkeeping | equivalent |
| 36-B device header | at buffer+4; len u16 @hdr+0x1a; next @hdr+0x20; payload @buffer+0x28 | implemented in hi5622_rx_live | MATCH |
| header zeroing | vendor zeroes the 36-B area on **every** publish | ours zeroes only at allocation; refill does not re-zero | small delta [I], MAC overwrites header on receive |
| band 1 ring sizes | 1024/1024, len 0x1060 | not programmed (2.4 GHz only today) | only matters for the 5 GHz ladder |

---

## 4. Host-MAC interrupt status - the service the vendor performs

CA 0x40050000 +0x00 is the host-MAC interrupt status word; mask = cfg +0x48
(0xfffffcc1).  The vendor ISR shuangta_irq_host_mac_isr (0x39a1c) walks the
set bits: **bit 2 = RX completion service** (refill free rings, drain the
completion ring, publish the consumer, W1C the served bits to cfg +0x44),
bit 3/4 = free-ring conditions, and several bits are **forwarded to the
firmware core** as HCC control messages (helpers 0x39558 with
hcc_msg_alloc(4) and 0x3920c with hcc_msg_alloc(16), group 0 id 7 header
shape).

### 4.1 What our bench actually reports

status = 0x182, mask = 0xfffffcc1 -> active = status & ~mask = 0x102
= **bits 1 and 8**.  Both of those bits are in the vendor's
**firmware-forward class**, i.e. the vendor host responds to exactly the
observable bits we have by sending messages to the firmware core.  Our driver
neither clears them nor forwards them.  This is the strongest candidate for a
**host->firmware notification** we are missing; it is *not* proven to gate RX,
and this lane does not send guessed messages (wrong HCC sub-type is a real
risk - sibling-GPL T2 rationale).

Bit 2 (RX completion) is **not** observable: no frame was handed back, which is
exactly the symptom.  The new dp_rx_parity/dp_rx_arm lines decode this
automatically.

### 4.2 The missing write we do implement

Vendor: after serving a status word, W1C the served bits to **cfg +0x44**
(shuangta_clear_host_mac_int_status 0x34bd8, isolated-bit helper at 0x39398).
Our existing rx_clear_status=1 path writes the **whole observed status word**
- that includes bits of other subsystems (TX completion etc.) and is a
plausible side-effect hazard.  New action dp_rx_status_clear (requires
rx_status_service=1 at load + rx_enable=1 + arm + the dp_setup/TXQ write
gate) writes **only bit 2**, and only when bit 2 is actually observable.
Fail-closed default: rx_status_service=0 -> refused, no write.

---

## 5. Our RX writes vs the vendor's - crash analysis

### 5.1 The write sets are the same offsets

| write | vendor does it? | ours | note |
| --- | --- | --- | --- |
| cfg +0x38/+0x3c free-ring producer publish | yes - in shuangta_host_rx_add_buff fill and in the RX ISR refill | yes - dp_setup fill and (write-enabled) drain refill | same packed-index formula (15 idx + phase bit 15) |
| cfg +0x40 completion consumer publish | yes - ISR after draining entries | only in write-enabled drain | same |
| cfg +0x44 W1C served status | **yes, on every service** | default never; rx_clear_status=1 writes the whole word; new action writes bit 2 only | gap narrowed; whole-word clear is the risky old shape |
| cfg +0x48 mask | yes (0xfffffcc1) | yes | same |
| HCC forward messages for observable status bits | **yes** (bits 1/8 class) | **no** | remaining host->firmware gap, documented not implemented |
| any novel/extra register in the RX path | - | **none** | all our RX writes are a subset of the vendor's own writes |

### 5.2 What the data abort can and cannot be

Bench exception: mask CA 0x400008 = **0x04000010**; per the project's exception
map (host-replica section 6.4: halfword vector-seen mask) bit 4 = **ARM vector
4 = data abort**.  Timeline: appearance after extended drain loops, while
cmp_wptr stayed 0 and no completion was ever produced.

- With pending = 0, the current drain performs **zero writes**: it logs "no
  pointer moved" and the watermark pass is a no-op because posted 224 (norm) /
  480 (small) are already above the 75 % targets 192 / 384 [V from the code and
  the arm state].  So the idle drain loops cannot have been the trigger.
- No RX write of ours is a write the vendor never makes (section 5.1), so a
  "write-the-vendor-never-writes" fault is not supported by the diff.
- The remaining ranked candidates: **(a)** the client's auth frame reaches the
  device RX path and the firmware takes the data abort *before* writing a host
  completion (this explains silence + crash in one), **(b)** the earlier
  stale-VAP/E2 crash class triggered by the AP-start/restart history, **(c)** a
  write-enabled drain acting on a completion that appeared and faulted later
  (not consistent with cmp_wptr=0).  (a) is the one the next fire can separate:
  the observe-only rung produces RX evidence with **zero writes**.
- Containment: rx_drain_write default **0** -> the drain cannot move any RX
  pointer during the read-only rungs; rx_status_service default **0** -> the
  status word cannot be cleared; the only RX-device writes possible in the
  shipped defaults are the ones dp_setup already did before any RX traffic.

---

## 6. What the patch implements

6 files, 1 052 diff lines, on top of the FINAL association + carrier +
watermark tree.  New portable logic hi5622_rx_diff.{c,h} (host-testable, no
device):

| piece | what it does |
| --- | --- |
| vendor stock tables | config block (21 words) and live bank (17 words) with the owner class; the five device-owned extra words; fixed value anchors |
| parity classifier | MATCH / MISMATCH / RANGE_OK / RANGE_BAD / DYNAMIC / READ_FAIL / DEVICE_OK / **DEVICE_ZERO**; summary counters |
| status decode | active bits, RX bit, firmware-forward class, compact rendering |
| read-only observation rules | completion entry must be 4-aligned, in one of the two pools, header parse accepted, no chain |
| init-program parity | asserts the 14-write program carries the six vendor fixed values and every address-bearing word (hi5622_rx_init_program_check) |

Driver integration (hi5622_dp_txrx.c / .h, hi5622_dp.c, Kbuild):

| item | behaviour |
| --- | --- |
| new param rx_drain_write (default **0**) | dp_rx_drain is OBSERVE-ONLY: reads live bank + completion-ring memory + returned buffers, validates entries/headers, logs frames, **publishes/refills nothing**.  1 = the exact old rx-static/watermark write path |
| new param rx_status_service (default **0**) | gates dp_rx_status_clear (below) |
| new RO sysfs dp_rx_parity | renders the full vendor-vs-ours diff: config block 21 words + live bank 17 words (actual vs stock vs class), a summary (mismatch / device_zero / read_fail), the status decode, and the DEVICE_ZERO interpretation note.  **No device write.** |
| new WO sysfs dp_rx_status_clear | writes W1C **bit 2 only** to cfg +0x44, only when bit 2 is observable, only behind rx_status_service=1 + rx_enable + arm + write gate; refuses otherwise |
| dp_rx_arm | additionally logs the status decode (active/forward bits) and the five device-owned live-bank words with the vendor stock values |
| dp_rx_capture | additionally prints the vendor-diff/observe telemetry line |
| observe telemetry | counters: actions, frames, bad, chains, last cmp wptr; repeat drains report "no completion pending" without writes |

The observe-only path deliberately does **not** lane-close on a bad entry/header
(nothing was consumed, so a later clean action is still possible); it counts and
reports.

---

## 7. Host verification (this lane, all PASS)

- **tools/rx_parity_selftest.c: 440 checks, 0 failures, RESULT: PASS**
  (strict C99 -Wall -Wextra -Werror -pedantic, no kernel, no device):
  vendor stock literals (0x00b0063c, 0xfffffcc1, 0x100/0x200/0xfff,
  0x00200020, 0x00800000/0x01800800/0x02801000/0x03801800), table integrity,
  every classifier branch (incl. transport-miss and DEVICE_ZERO),
  status decode for vendor idle vs our 0x182 (active=0x102, rx inactive,
  forward=0x102) and synthetic RX-pending, the observation entry rules
  (alignment/pool/slot/NULL), and the init-program parity (OK, corrupted
  fixed word, missing address word, NULL refusal).  Log:
  [evidence/host-test.txt](evidence/host-test.txt).
- **Kernel build** against the project 6.18.60 tree: **0 warnings / 0 errors**
  ([evidence/build.log](evidence/build.log)).
  - stripped artifact: **361 872 B sha256 81ab72c1...**
  - unstripped: 1 367 620 B sha256 068c61a9...
  - **59 params** (the 57 of the base + rx_drain_write + rx_status_service)
  - vermagic 6.18.60 SMP mod_unload ARMv7 p2v8; .text 152 176 B identical
    before/after strip.
- **Patch round-trip:** applying patches/0001-rx-vendor-diff.patch to a clean
  copy of the base tree reproduces the built source tree byte-for-byte (6/6
  files identical) and the rebuilt module's .text is identical to the
  recorded artifact.  The stripped sha differs only through the GNU build-id
  (absolute build path) - quote the sha of the exact baked file.

Base chain (for the build): final association tree 251d08f0 + corrected
mgmt-TX carrier ab9e18a4 + RX refill watermark 5c94d076; this lane's patch
applies last.  See [BUILD-NOTES.md](BUILD-NOTES.md).

---

## 8. Honest limits

- **No bench result is claimed.**  Everything above except the quoted bench
  states is host-derived or decoded from committed vendor artifacts.
- The five device-owned live-bank words and bits 1/8 are *decoded*, not
  explained: no host accessor exists and the firmware-side meanings are [U].
  The parity action is the instrument, not the answer.
- The firmware-forward HCC messages for status bits 1/8 are **not**
  implemented (payload/grade risk); they are the next static target if the
  read-only rung shows bits 1/8 staying asserted while RX is silent.
- The 36-byte header's sub-MSDU chain accounting is still [U]; both the old
  drain and the observe rung refuse a chain (fail-closed).
- The crash root cause is not proven; the observe-only default is containment
  and an experiment, not a fix.  The FIRE-RECIPE's R2/R3 discriminate (a) from
  (b) in section 5.2.
- Band 1 (5 GHz) RX geometry (1024/1024, 0x1060 buffers) is decoded but not
  programmed; it is 2.4 GHz-only today.

