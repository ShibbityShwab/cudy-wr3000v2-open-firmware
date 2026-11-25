# CRX delivery gating: what makes the firmware deliver a received management frame as D2H g1 id13 - and why it never does on this bench

**Lane:** wifi-rx-crx-gating, shared task **task-164**, **host-only** (no bench contact;
the Lead fires). **Written:** 2026-11-25. **Base tree:** final association tree
`251d08f0` + wifi-sibling-gpl `0001-mgmt-tx-hcc-g0id6-carrier.patch` + wifi-rx-watermark
`0001-rx-refill-watermark.patch` (the image-2 chain; Kbuild and hi5622_dp.c are identical
to the bare base tree).

**One-line answer.** The firmware's CRX delivery path is **complete and autonomous**: it
builds the D2H group-1 id-13 message itself from the received MPDU (12-byte HCC header +
48-byte RX info + frame, total = frame_len+60, hard cap total <= 0x680), and **no host
message, mask, ring or dispatch-table entry gates it**. The host really is missing
nothing at the delivery layer. What the bench is missing is the **input**: no received
frame ever reaches the firmware's RX forwarding pipeline, so it has nothing to deliver.
The host-side step that was never implemented is therefore not an enable - it is the
**evidence that separates the three possible silences**; this lane delivers that step
(strict CRX decoder + read-only firmware-gate probe, no device writes) plus the
firmware-side gate matrix it is built on.

---

## 0. TL;DR - four questions, four answers

1. **What builds the CRX event?** The firmware. Function CPU **0xB9780** (registered in
   the firmware's own dispatch table **CA 0x10CC18 entries [2] and [5]**, one handler for
   both) contains the builder at CPU **0xBA0AA-0xBA17A**: it sets group=nibble **1**
   (mov.w sb,#1 at 0xBA0AA), id **13** (movs r3,#0xd at 0xBA150), total **frame_len+60**
   (0xBA146), copies the **48-byte RX info** (memcpy 0x30 at 0xBA022) and the MPDU
   (0xBA062), then posts it with the firmware's hcc_msg_tx primitive (CPU 0x102760, call
   at 0xBA17A). The host's dispatch table (vendor wifi.ko `.data+0x584`, 28 x 12-byte
   entries) independently maps **id 13 -> hmac_rx_process_mgmt_event**, whose
   `hmac_rx_event_pre_process` strips exactly 60 bytes and checks data+29 == total-60.
   Firmware builder and host decoder agree field-for-field.
2. **What gates it?** A fixed set of firmware-side conditions on the *received* frame
   and its per-chip context (section 4): a non-NULL per-chip RX context
   ([chip+0x1a00] at 0xB97EE), the forward flag in RX-info +6 (0xBA134), the
   first-buffer/header-length relation (0xB9A84), the frame-length windows
   (0x1c..0xfe / 0x4d..0xfe, 0xB9A9A/0xB9B30/0xB9B4A) and the total cap
   (len+60 <= 0x680, 0xBA110). **None of them is host-controlled**; every one is a
   property of the frame the firmware received plus its own state.
3. **What does our host miss?** At the delivery layer: nothing. The host already
   decodes g1 id13 (ap-mgmt lane) and the direct D2H reader already catalogues every
   group/id pair; a CRX message would be seen. The bench sees none because the MAC RX
   hardware never produced a frame for the firmware to forward: the live bank is
   prefetch-only (norm/small rptr = 0x20, completion wptr = 0, RX status bit 2 clear).
   This is the RX-direction mirror of the TXQ-publish proof, and it moves the open item
   from 'event gating' to the MAC/DMAC RX configuration (wifi-rx-vendor-diff lane).
4. **Deliverable.** `patches/0001-crx-delivery-gate.patch`: a fail-closed, **read-only**
   delivery-gate probe (`crx_gate` sysfs; MAC live bank + config mask + firmware hcc
   instance/DR record counters + the D2H catalogue) and a strict CRX wire decoder that
   refuses anything that is not byte-for-byte the firmware's form. It performs **zero
   device writes**, so it cannot be the crash trigger (section 5.4). Host-tested
   (66 checks, 0 failures) and kernel-built (0 warnings, section 7).

---

## 1. Scope, method, evidence markers

* **Inputs (local, never copied/committed):** the vendor firmware image
  `FIRMWARE.bin` (928,920 B, sha256 7fc87e20...), the vendor host module
  `hi5622v100_wifi.ko` 2.4.15/2.5.24 disassembly (read in place under
  `wifi-shim-port/raw/`, gitignored), and the committed lane reports cited below.
* **Method:** capstone Thumb disassembly of the local firmware image with the project
  convention CPU = file offset + 0x40000; whole-image literal census; xref scans; the
  vendor host `.data` dispatch tables parsed with ELF relocations (section 3.2). No
  device was contacted; no image or module byte is committed.
* **Evidence markers:** **[V]** verified here from the named local artifact (address +
  instruction); **[V\*]** device-measured by an earlier attended lane and quoted; **[I]**
  inference; **[U]** unknown.

---

## 2. The CRX wire contract, both ends

### 2.1 Firmware end: the builder at CPU 0xBA0AA-0xBA17A [V]

The builder lives inside firmware function CPU 0xB9780 (Thumb entry 0xB9781), which the
firmware registers as the handler for its own group-2 id-2 and id-5 message classes
(the firmware H2D catalog lane lists CA 0x10CC18 entries: id 2 and id 5 both ->
handler 0x000B9781). The CRX loop starts at 0xBA0A0, where the message group is
loaded as a constant:

| CPU | instruction | meaning |
| ---: | --- | --- |
| 0xBA0A0 | `movs r1,#6; ldrb r0,[r6,#0xd6d]` | save the per-chip channel for the pool call |
| 0xBA0AA | `mov.w sb,#1` | **group = 1** (used by the hdr bfi at 0xBA14A) |
| 0xBA0B0 | `ldr r8,=0x10380cf8` / `ldr sl,=0x20380cf8` | firmware log handles |
| 0xBA0B6 | loop over the received-frame list | |
| 0xBA0C0-0xBA0D4 | `bl 0x1024ea` + `bl 0x409ea` + `bl 0x40cba` | dequeue a frame, fetch its header, fetch the RX descriptor |
| 0xBA0F8 | `ldrh fp,[r0,#0x11]` | **frame_len = RX-info +0x11 (u16)** |
| 0xBA0FC | `ldrb r0,[r0,#6]` | forward flag (RX-info +6) |
| 0xBA0FE-0xBA104 | `add r2,fp,#0x30; uxth` | payload size = frame_len + 48 |
| 0xBA110-0xBA118 | total+12 <= 0x680 | **hard cap: total = frame_len+60 <= 0x680** |
| 0xBA134 | `cbnz r0,#0xba144` | flag==0 -> drop + log 0x49f |
| 0xBA144-0xBA14E | `ldrb r3,[r4]; bfi r3,sb,#0,#4; strb r3,[r4]` | **hdr[0] group nibble = 1** |
| 0xBA150-0xBA152 | `movs r3,#0xd; strb r3,[r4,#6]` | **hdr[6] id = 13** |
| 0xBA146/0xBA156 | `add.w fp,fp,#0x3c; strh r4.. [r4,#4]` | **hdr[4] total = frame_len+60** |
| 0xBA15C-0xBA172 | copies info[+2], info[+1], info[+0] | hdr[1] dst nibble, hdr[2], hdr[3] |
| 0xBA174 | `bl 0x4bdce` | copy the MPDU (frame_len from RX-info +0x11) |
| 0xBA17A | `bl 0x102760` | **post the HCC message (firmware hcc_msg_tx)** |

The 48-byte RX info the builder forwards is assembled on the stack at [sp+0x30] by the
same function (zeroed 0x30 B at 0xB98EA; populated from the hardware RX descriptor at
negatives of the descriptor pointer, 0xB98F8-0xB9A48; copied to the message payload at
0xB9A4C-0xB9A56) and patched with RSSI and a u16 from the frame context:

| RX-info offset | source [V] | host consumer [V] |
| ---: | --- | --- |
| +0x07 u8 | chip ctx +0xd72 (0xB9A7A) | channel -> frequency, hmac_rx_mgmt_send_to_host |
| +0x0d u16 | [sl+0x1e] (0xB9A72) | not consumed by the mgmt path (status/sequence) |
| +0x11 u16 | RX descriptor frame length (0xBA0F8) | cross-check total-60 (data+29) |
| +0x27 s8 | [sl+0x1d] (0xB9A68) | RSSI clamp(100+rssi) |

So the firmware is the **only** producer of this event: the host never sends anything
that triggers it, and the message id/group/length are constants in the firmware code
path. There is no firmware-side 'enable CRX' state we can find that a host message
sets: the handler is registered unconditionally at boot and runs when its internal
RX-frame message arrives.

### 2.2 Host end: the dispatch table and the decoder [V]

The vendor module's hmac D2H dispatch table is `.data + 0x584`, **28 entries of 12
bytes** `{u16 id; u16 pad; u32 handler; u32 flags}` (relocations parsed with readelf;
see `evidence/host-dispatch-table.txt`). The entry that matters:

| entry | id | handler | host behaviour |
| ---: | ---: | --- | --- |
| 8 | **0x0d (13)** | `hmac_rx_process_mgmt_event` (0x49FD0) | `hmac_rx_event_pre_process` strips 60 B, copies 48 B info to cb, checks u16 data+29 == total-60; then the AP/STA FSM |

This independently re-confirms the ap-mgmt lane's g1 id13 decode (the table layout is
now pinned: id is the *first* u16 of the entry, handler the *second* word); the earlier
ap-mgmt/sibling decoding of 'id 13 -> hmac_rx_process_mgmt_event' was correct.

Combined contract:

```
D2H HCC group 1 / id 13
  12-byte HCC header | 48-byte RX info | 802.11 MPDU (frame_len bytes)
  total = 60 + frame_len, accepted only while total <= 0x680
  new id->handler entry: group 1, id 13 (table .data+0x584 entry 8)
```

---

## 3. The delivery pipeline and where a host could interfere - nowhere

### 3.1 Firmware pipeline

```
MAC/RX hardware frame
  -> firmware RX indicator message (internal; group-2 id-2/5 dispatched to 0xB9780)
     -> the per-frame loop 0xBA0A0..0xBA17A: build group-1/id-13 message
        -> firmware hcc_msg_tx 0x102760
           -> ETE DR deposit op (CPU 0xC61E0, node.word1 = (len<<16)|0x6D2B)
              -> d2h_notify(6) -> device out[1] bit6
                 -> host: direct device group-A DR ring read (our driver) or ETE A->B
                    copy (idle in our boot) -> decode -> hi5622_hcc_deliver()
```

The host's only role in this chain is the *receive* side, which the direct D2H reader
already implements. The one thing a host could break is the *delivery channel* itself
(out[1] notification service and the DR ring space), and that channel is already
proven alive on the bench: the direct reader catalogued g4 id1/id2 (ready pair) and
the heartbeat/OAM traffic in the same boots that show no g1 id13.

### 3.2 Why no host message can be the missing enable [V negative]

* The firmware handler is registered from the firmware's own boot task (the same
  routine that later emits the ready frames) and never re-registered or gated on a
  host message [V: h2d-catalog lane's registration site file 0x833C/0x8346].
* The builder writes the group and id as **immediate constants** (0xBA0AA, 0xBA150);
  there is no host-supplied field that selects them.
* The only drop conditions are frame/context properties (section 4), not host state.
* The sibling GPL cross-check is consistent: Hi3881 separates WLAN_CRX (control/mgmt,
  host SME) from WLAN_DRX (data); the device delivers CRX events autonomously. The
  Hi3881-specific per-VAP RX-filter message has **no luofu equivalent** (sibling lane
  T2, exhaustive immediate/call-site scan) - so there is no 'enable mgmt RX' H2D
  message to port either.

---

## 4. The gate matrix: every condition that can suppress a CRX event

All rows are firmware code sites in the builder/handler; each is a property of the
received frame or of the firmware's own per-chip state. The 'actionable' column is what
a host could ever do about it. [V]

| # | CPU | condition | on failure | actionable? |
| ---: | ---: | --- | --- | --- |
| G1 | 0xB97EE | per-chip RX ctx `[chip+0x1a00] != 0` | error log, drop | **yes** (firmware init/VAP create must have run) |
| G2 | 0xB9816/0xB97A0 | chip object resolvable from hcc instance | error log | no |
| G3 | 0xB98F8-0xB9A48 | RX descriptor must carry a complete 48-B info block | error log | no (hardware) |
| G4 | 0xB9A84 | info[+2] (header/first-buffer length) <= info frame_len | drop, log 0x9a4 | no (hardware) |
| G5 | 0xB9A8E | chip ctx +0x5e == 0 -> probe/fbt report class path; non-zero -> forward class | different event class | no |
| G6 | 0xB9A9A | frame_len in 0x1c..0xfe (this branch) else 0xB9B30 0x4d..0xfe / 0xB9B6C | drop + log 0x9f5/0x9ee | no |
| G7 | 0xBA0FC/0xBA134 | RX-info forward flag (+6) != 0 | drop, log 0x49f | no (hardware) |
| G8 | 0xBA110 | frame_len+60 <= 0x680 | drop, bump chip+0xCC5 | no (hardware) |
| G9 | 0xBA048 | message allocation from the frame pool succeeds | drop | no (firmware memory pressure) |
| G10 | 0xB9C12 | for the probe/fbt class: `[chip+0x70] != 0` and FC types 0x40/0x80 | skip that class | no |

**Consequence:** the only row a host can influence is G1 (the firmware's own VAP/RX
context must exist), and our AP-start sequence already creates the VAP (beacons are
on air, and the ready/D2H traffic is flowing). Since the failure is *no frame input at
all*, no host action on G1 can help either.

---

## 5. Why the event is silent: the MAC RX input, and the crash question

### 5.1 The measured state is the exact shape of 'firmware RX input dry'

| observable | bench value | what it means |
| --- | --- | --- |
| MAC live bank status (CA 0x40050000+0) | 0x00000182 (RX bit 2 clear) | the MAC host-interface never reported RX activity |
| norm/small free-ring rptr | 0x20 / 0x20 | the MAC prefetched the 32 published buffers at init and consumed nothing since |
| completion wptr | 0x0 | no frame was ever written back to the host rings |
| config mask CA 0x40040048 | 0xfffffcc1 | the RX mask bit is unmasked (matches vendor stock) |
| D2H catalogue | ready pair + heartbeat/OAM only, **no g1 id13** | the firmware delivery channel is alive; it has nothing to deliver |

If the firmware had received even one management frame and its gates passed, the host
would see a g1 id13 frame in the same DR rings that carry the ready pair. It does not,
so the missing step is upstream of the delivery layer. The two candidate upstream
groups are (a) the MAC/DMAC RX configuration (hardware routing/recognition of received
frames - wifi-rx-vendor-diff lane) and (b) the RF/PHY receive path. Section 5.4 gives
the control experiment for the crash so the two are not confounded.

### 5.2 What this changes for the other RX lanes

* The `dp_rx_*` host-ring consumer path is not the CRX path. Management frames are not
  expected on the host RX completion ring in this architecture (sibling CRX/DRX split;
  the vendor ring-entry handler never calls the mgmt FSM). Zero completions while the
  laptop only authenticates is *expected*; the ring test needs an associated client.
* The direct D2H reader's catalogue is the correct CRX observation point; no new
  decoder is needed in principle, but the strict decoder in this patch pins the exact
  wire form so a future 'id 13 seen' cannot be a false positive.

### 5.3 What the host already does vs the vendor host

| step | vendor host | our driver | status |
| --- | --- | --- | --- |
| arm host RX free/completion rings | 12-write program at probe | dp_setup 14-write program | done |
| keep free rings fed | refill in ISR | dp_rx_drain + watermark | done (capture-only) |
| receive D2H events | group-B host DR via ETE | direct device group-A DR read | done (stronger) |
| decode g1 id13 | hmac_rx_event_pre_process | hi5622_mgmt_deliver (mgmt_rx=1) | done |
| enable CRX forwarding | nothing | nothing | **nothing exists** [V negative] |

### 5.4 The crash (B): RX-write fault surface, ranked

The Lead's finding: the firmware raises its exception path mid-session (mask
0x04000010, `excp_announce=1`), first observed after extended `dp_rx_drain` loops,
while RX captures nothing even before the crash. This lane's static analysis bounds
which host writes could possibly be involved:

1. **Our RX pointer writes are MAC-host-interface config words only** - `+0x38/+0x3c`
   (free-ring producers), `+0x40` (completion consumer), `+0x44` (W1C status, only
   behind `rx_clear_status=1`) in band 0 CA 0x40040000. The firmware image **never
   accesses** that block or CA 0x40050000: a whole-image literal census finds only two
   constants in those ranges (0x40041001 and 0x40042000) and both are used as data
   values/base pointers, never as register accesses [V]. So our writes cannot corrupt
   firmware-owned state directly.
2. **But the MAC hardware consumes those words.** A malformed published packed index
   (or a refill of a buffer still in flight) could make the MAC prefetch/DMA through
   bad buffer addresses. The drain validates every entry and read-back; the watermark
   patch bounds refills to validated completions. The highest residual risk is the
   free-ring producer publish (+0x38/+0x3c) and the +0x44 clear.
3. **Other host writes in the same session:** H2D txq_addr_sync / ctx-create messages
   (device pointers), the D2H direct reader's `+0x1C/+0x38` (only if `rd_advance=pub`
   / `rd_ab_write=1`), and the mailbox ack/re-arm (out[3]/out[4], vendor-analog).
4. **The firmware RX path itself (0xB9780) is NOT reachable without a received frame:**
   every allocation/dereference in it is downstream of the internal RX-frame message.
   If the crash is in that path, frames would have had to arrive - which would also
   have produced a g1 id13 event. They did not.

**Decisive control rung (Lead fires):** the same boot ladder (page/mask/staging ->
C2 -> AP 5-message -> dp_setup -> txq_addr_sync) with **all RX-side writes disabled**:
`rx_enable=0` (default), `rd_advance=node` or `none`, `rd_ab_write=0`, and no
`dp_rx_drain`. If the exception still appears with the AP started and beaconing but no
RX pointer write ever executed, the crash is not caused by our RX writes and the
suspects move to the H2D message payloads or the firmware's own TX/beacon path. If it
disappears, the drain/publish writes are the trigger and the RX lane must stay in
read-only mode until the MAC state mismatch is fixed.

---

## 6. The missing step this lane implements

Because the delivery layer needs no enable, the 'missing step' is the *attribution*
step: a host-side, fail-closed probe that turns the next silent boot into a decidable
reading. `patches/0001-crx-delivery-gate.patch` adds:

1. `hi5622_crx_gate.{h,c}` (new, portable + kernel halves):
   * `hcg_decode_crx()` - strict, fail-closed decoder for the firmware's CRX wire form
     (tag 0x5A5A, group 1, id 13, header total == argument, len == total-60, forward
     flag, channel band plan, RSSI physical range, 802.11 version/type = management).
   * `hcg_classify()` - pure classifier over the probe fields with named verdicts:
     `MAC-UNREADABLE`, `FW-UNREADABLE`, `FW-D2H-IDLE`, `MAC-RX-IDLE-UPSTREAM`,
     `CRX-DELIVERED`, `CRX-AND-DATA`, `INCONSISTENT`.
   * `hi5622_crx_gate_probe()` - **read-only** read of the MAC live bank + config mask
     (region 3), the firmware hcc instance CA 0x0010C0F4 and its group-A DR record
     counters (region 0), and the host direct-reader D2H catalogue (g1 id13 count and
     bytes). It refuses the non-visible CA 0x4016xxxx block by construction (the
     historic instrument bug).
2. `hi5622_dp.c`: one `crx_gate` **0444** attribute that runs the probe and prints the
   verdict + raw fields + expected values. There is no write path and no parameter:
   reading the file is always safe, and returns -EBUSY before the driver is armed.
3. `Kbuild`: adds `hi5622_crx_gate.o`.

### 6.1 Fail-closed matrix

| condition | behaviour |
| --- | --- |
| driver not bound / transport not armed / windows not programmed | `cat crx_gate` returns -EBUSY, no read |
| any CA outside the six inbound regions or mapping 0 | field marked unreadable, verdict names which half |
| any word reads 0xffffffff | MAC/FW unreadable, never interpreted as data |
| firmware DR base outside region 0 / beyond record range | FW-UNREADABLE, no dereference |
| no CRX in catalogue + healthy D2H + prefetch-only rings | `MAC-RX-IDLE-UPSTREAM` |
| CRX in catalogue | `CRX-DELIVERED` (or `CRX-AND-DATA` if completions too) |
| unexpected combination | `INCONSISTENT`, raw fields printed, no action |
| any decoder refusal | returns a negative errno-like code, frame never partially decoded |

---

## 7. Host verification

* `tools/crx_gate_selftest.c`: **66 checks, 0 failures**, strict C99
  (`-std=c99 -Wall -Wextra -Werror -pedantic`). Covers: builder contract arithmetic,
  valid decode at min/default/max frame sizes, 20 refusal classes (tag/group/id/length
  fields/flag/channel/RSSI/frame-type/version, NULL, short, oversized), all 8 verdicts
  including the measured bench shape, and the reason/name surface.
* Patch round-trip: applies with `patch -p1` to the image-2 chain tree (4 files, 544
  diff lines) and reproduces `src/` byte-for-byte (`evidence/patch-verify.log`).
* Kernel build against the project 6.18.60 tree: **RC=0, 0 warnings**, artifact
  identity in BUILD-NOTES.md.
* `crx_gate` performs no device write: no `writel`, no `readl` outside the six mapped
  inbound regions, no allocation, no lock held across a read.

---

## 8. Fire recipe and decision table (the Lead fires)

Full ladder in [FIRE-RECIPE.md](FIRE-RECIPE.md). Short form:

| rung | load | read | PASS (decides) |
| ---: | --- | --- | --- |
| R0 control | rx_enable=0, no drain, rd_advance=node | dmesg + page/mask | crash-free after >15 min: bounds section 5.4 |
| R1 gate | same load (no client traffic yet) | `cat crx_gate` | `MAC-RX-IDLE-UPSTREAM`: confirms delivery layer fine, input dry |
| R2 input | laptop sends auth frames at the AP | `cat crx_gate` + `cat rd_catalog` | `CRX-DELIVERED`: host path proven end-to-end; `MAC-RX-IDLE` persists: MAC/DMAC RX config is the bug |
| R3 data | if R2 CRX: associate | `dp_rx_arm`, `dp_rx_drain`, `dp_rx_capture` | EAPOL/ARP completions -> data path proven |

---

## 9. Open items / honest limits

* **[U] The exact meaning of the crash telemetry** (`mask CA 0x400008 = 0x04000010`,
  `vector_mask=0x10`): these are host-side driver telemetry words, not device
  registers; this lane did not decode them. The control rung R0 is the cleanest way to
  attribute the crash.
* **[U] The first hardware producer of the internal RX-frame message** (which SMAC/RX
  ISR posts group-2 id-2/5 internally): its registration is not reachable from the
  host; the builder and every gate downstream of it are proven, but the trigger ISR is
  outside this lane's static reach.
* **[U] Whether the firmware ever forwards *data* frames on this same event path**; the
  host-side design routes data via the host rings (DRX) and mgmt via CRX. Not needed
  for the current blocker.
* **[I] The probe's firmware addresses** (hcc instance CA 0x0010C0F4, DR base at
  +0x14, record fields) are [V\*] bench-read on the golden boot and [V] firmware
  constants; if a future boot shows `FW-UNREADABLE`, record the raw fields before any
  further action (the probe itself is read-only).
* **Probe scope:** it proves *where* the silence is, it does not fix the MAC RX
  configuration - that belongs to the vendor-diff / hardware lanes.

---

## 10. Reproduction

```sh
# 1) host selftest (device-free)
cd opensource/docs/incoming/wifi-rx-crx-gating
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Isrc \
   tools/crx_gate_selftest.c src/hi5622_crx_gate.c -o /tmp/crx_gate_selftest
/tmp/crx_gate_selftest        # 66 checks, 0 failures

# 2) patch dry-run against a pristine copy of the image-2 chain tree
cd <pristine-chain-tree> && patch -p1 --dry-run \
   < <repo>/opensource/docs/incoming/wifi-rx-crx-gating/patches/0001-crx-delivery-gate.patch

# 3) kernel build (see BUILD-NOTES.md section 2 for the exact env)
./tools/build.sh
```

Vendor artifacts are **not** in this lane: the firmware blob and the module
disassembly stay at their local paths; only derived addresses/constants and our own
code are committed.
