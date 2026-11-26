# REPORT - the MAC-RX -> firmware -> D2H delivery: the shared D2H channel, the per-frame consumer, and the missing intake producer

**Lane:** `wifi-rx-mac-delivery` (task-220).  HOST-ONLY static decode of the pinned
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; **CA = file offset + 0x40000**).
Grades: **[V]** re-derived here by `tools/fw_rx_mac_delivery_re.py` (`--selftest`
95/95 PASS); **[V\*]** committed earlier lane; **[M]** bench-measured earlier; **[I]**
inference; **[U]** needs a device read / not locatable in this image.
Companions: [README.md](README.md), [TABLES.md](TABLES.md), [PROBES.md](PROBES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/](tools/), [evidence/](evidence/).

---

## 0. Verdict

1. **The D2H ring is one shared firmware->host HCC message channel, not a per-id ring.**
   The deposit path is `firmware hcc_msg_tx` **CA 0x102760** -> ETE node fill **CA 0xC192A**
   -> `d2h_notify` **CA 0xC6170** (id <= 10, pending mask at hcc object +0xB8, doorbell
   bit 0).  The observed notify ids are **6** (0xC6224), **5** (0xC6998) and **3**
   (0x462AC, the host-interface event handler's tail call).  The firmware-originated
   messages the bench does receive - ready pair g4 id1/id2, g1 id3 config replies,
   HEARTBEAT g4 id4 (builder 0xC6C0A, literal "HEARTBEAT" at 0x106328) - and the
   management-RX event **g1 id13** all deposit through this same primitive.  **[V]**
2. **Therefore "the firmware's own frames deliver" is not evidence about the MAC-RX path.**
   There is no separate MAC-RX D2H ring/route to be empty: if the RX consumer produced a
   g1 id13 message, the same host reader that logs the ready pair would log it.  The bench
   silence means the message was never produced, i.e. the break is **upstream of the
   deposit**.  **[V] decode / [V\*] bench (D2H catalogue: ready+heartbeat only, no g1 id13).**
3. **The MAC-RX intake is a firmware HCC message queue into the per-frame consumer
   0xB9780.**  The datapath handler **0xBA1C8** calls the CRX builder **0xB9780** once per
   received frame (**bl at 0xBA48A**); the builder pops received-frame nodes from a queue
   (**bl 0x1024EA at 0xBA0C0**, frees with **0x1024E6 at 0xBA0E2**), fetches the message
   header (`0x409EA`) and the RX-info block (`0x40CBA`), then walks the frame list and
   builds one D2H message per frame.  **[V] call sites/semantics, [I] role naming.**
4. **The per-frame gates on the consumer side are now pinned [V]:** the per-chip RX context
   `[chip+0x1a00]` must be non-NULL (**0xB97EE**, drop/log 0xA4E); the received-frame
   forward flag `info[+6]` must be set (**0xBA0FC/0xBA134**, drop/log 0x49F); the total
   must satisfy `frame_len+60 <= 0x680` (**0xBA110/0xBA114**, drop and bump the
   device +0xCC5 counter); `frame_len = info[+0x11]` (**0xBA0F8**).  The g1 id13 message is
   then built: group nibble 1 (**0xBA14A**), id 13 (**0xBA150**), total = frame+60
   (**0xBA146**), 48-byte RX info copy, MPDU copy, deposit (**0xBA17A**).  The wire
   contract agrees with the host decoder (cross-lane wifi-rx-crx-gating).  **[V]**
5. **The producer of the received-frame nodes - the SMAC RX-completion -> RX descriptor ->
   node path - is the missing layer.**  The image carries the SMAC ISR name table (CA
   0x12266C: `smac_rx_complete_2g/5g_isr`, `smac_rx_timeout_2g/5g_isr`, tbtt, pre_tx,
   backoff, user_ps, tx_abort, lifetime, tx_exit ... plus `smac_msg_proc_rx_ring_reset_req`
   / `..._enable_trx_req`), but **no code references the table** (32-byte name records; no
   32-bit literal points into it) - it is a build-artifact name table, not a dispatch
   table, so it cannot be mapped to handler addresses statically here.  The producer
   entry is therefore **[U]**.  Cross-lane negative: the stock H2D capture contains no
   RX-enable/TRX message, so the host is not expected to arm it.  **[V] table, [U] producer.**
6. **Where the path breaks for client frames vs firmware frames (ranked, [I]):**
   * **B1 - intake dry (best fit):** the SMAC RX completion path never posts a frame node
     on our host-only boot; the consumer 0xB9780 is never entered, no g1 id13 is built.
     Fits frames=0 / no host completion write.  The MAC still reports RX activity in its
     own status word (bench bit 10 transition), so the drop is between the MAC PPDU
     completion and the firmware node, not at the host rings.
   * **B2 - consumer gate:** the node arrives but the per-chip RX ctx `[chip+0x1a00]` is
     NULL (0xB97EE) and the frame is dropped/logged 0xA4E.  Fits a host-only boot whose VAP
     exists but whose per-chip RX context was never fully built.
   * **B3 - consumer fault:** a fault inside the producer/consumer on the first frame.
     Fits the intermittent page golden-loss in the rx_enable=1 bench runs (the same
     sessions that latched bit 10).  The fault is firmware-mediated (the host writes no
     descriptor into that path).
7. **The PPDU descriptor list is the firmware's own descriptor pool, not the host's RX
   path, and its presence is not a proof that the RX intake ran.**  The boot init
   **0xC016C** (called at 0xD3BAC from the device init **0xD1B68**, itself called at
   0x48314) sets the per-band device object **0x174BF0 + 0xE28*band** list
   (+0x66C next, +0x670 prev, +0x674 count, +0x67C name 0x1060F7) and pre-allocates
   0x1A0-byte descriptors with **0xBF128**.  The datapath reserve helper **0xB52F0**
   links a **0x198-byte** descriptor into the same list (lock +0x678, memset 0xC1E0C,
   append 0xB536C/0xB5374, count 0xB537E/0xB5388) and is called from the RX/TX datapath
   (0xB5458, 0xBAF10, 0xBBD40).  Cross-lane ppdu-init measured/derived that the boot loop
   ran on the bench boot, so the list is expected present - it does not move the F1.
   **[V] decode, [V\*] cross-lane.**

**Consequence for the F1:** the last undecoded layer is exactly "does the SMAC RX
completion post a frame node to 0xB9780 on our boot?".  This lane cannot read that from
the image (the producer entry is unresolved), so it delivers the read-only probes that
decide it on the next attended run, plus the escalation path.

---

## 1. Inputs and method

* firmware image `rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin` (2.4.15 == 2.5.24
  payload here), 928,920 B, sha256 `7fc87e20...c3ef311b`, CPU = file + 0x40000 **[V]**;
* committed lanes as prior art: `../wifi-rx-crx-gating` (CRX wire contract + host
  decoder), `../wifi-f1-from-capture` (stock H2D negative + HEARTBEAT builder),
  `../wifi-rx-f1-thread` (HCC RX thread = H2D, D2H publish), `../wifi-ppdu-init` (PPDU
  list init + corrected object/probes), `../wifi-h2d-catalog` (firmware dispatch tables),
  `../wifi-rx-format` / `-linux-parity` / `-completion-phase` / `-woc-class`;
* method: capstone 5.0.7 Thumb for exploration (the image is mostly Thumb-2 with small ARM
  islands at the exception vectors); every quoted site is then asserted from the raw
  bytes by the committed stdlib tool (near-range Thumb BL/B.W decoder, fixed word reads).
  Names/strings are asserted by content; no vendor binary byte is copied into the
  repository.

Note on scope: the task asked for the "mac_fill/ppdu_dscr_set routines".  No such symbol
exists in the vendor host modules or as a string in the firmware; the firmware-side names
found are the PPDU descriptor-list debug name (0x1060F7) and the SMAC ISR/message-proc
name table (CA 0x12266C).  The decode below uses the actual routines.

---

## 2. The D2H source selection (which producers feed the D2H ring)

### 2.1 One channel, one deposit primitive **[V]**

| stage | CA | facts |
| --- | ---: | --- |
| `hcc_msg_tx` | **0x102760** | `push {r4,r5,r6,lr}`; validates the message, copies `hdr[1]>>4` into `hdr[9]` (the destination nibble that routes the message), resolves the per-chip group record at `hcc_instance+0x448` (+0x458 record array, +0x44C map) with `0x102744`, then hands the message to the deposit path.  Its callers include every firmware-originated send (56 BL sites). |
| ETE node fill | **0xC192A** | `push {r4}`; 8-byte node `{devva, (len<<16)|0x6d2b}` into the DR ring; called from 0xC621C/0xC6254. |
| `d2h_notify` | **0xC6170** | `cmp r1,#0xa` (id bound), `r0` must be 0, hcc object from global **0x172130**; sets pending bit `id` (mask base +0xB8), publishes the mask and ORs the doorbell bit. |
| notify call sites | 0xC6224 id **6**; 0xC6998 id **5**; 0x462AC id **3** | ids 5/6 are the generic 8-byte-node deposit paths; id 3 is the host-interface event handler's tail call (the doorbell service). |
| host-side consume | [V\*] wifi-d2h-direct / phase43 | the D2H reader sees the ready pair + HEARTBEAT + config replies in every good boot. |

### 2.2 The firmware-originated producers the bench receives **[V]**

| message | builder | note |
| --- | --- | --- |
| g4 id4 HEARTBEAT | **0xC6B6A** (worker) / **0xC6C0A** (`bl 0x102760`) | copies "HEARTBEAT"+NUL from literal CA **0x106328**, hdr[0]=4, hdr[6]=4, sends.  D2H, byte-identical to the captured frames [V\*]. |
| ready pair g4 id1/id2 | boot task (~0x7D28 region, [V\*]) | same `0x102760` deposit; every good boot. |
| g1 id3 config replies | [V\*] C2 bench | the firmware's answer to host config events; same deposit. |
| **g1 id13 mgmt RX** | **0xB9780** builder at 0xBA0A0-0xBA17A | the only management-RX event; same deposit (**bl 0x102760 at 0xBA17A**) [V here]. |

**Source selection answer:** the D2H ring does not select between "firmware service" and
"MAC RX" producers - they are the same channel.  The MAC-RX producer (the SMAC completion
path that would call the CRX builder via the message queue) is a *different stage upstream*
of the deposit; it is the one that is silent.

---

## 3. The MAC-RX consumer: entry, queue intake, gates, builder

### 3.1 The call structure **[V]**

```
datapath handler 0xBA1C8
  ... per received frame:
      ldr r0,[sp,#0x28] ; bl 0xB9780        ; 0xBA48A - one CRX build per frame
CRX builder 0xB9780
  bl 0x409EA        ; 0xB979C - message header
  ldr r3,[r0,#0x14] ; chip/band object from the node
  ...
  per frame:
     bl 0x1024EA    ; 0xBA0C0 - pop a received-frame node from the queue
     bl 0x409EA     ; 0xBA0C6 - header
     bl 0x40CBA     ; 0xBA0CE - RX info/descriptor pointer (r0 = info)
     frame_len = u16[info+0x11]        ; 0xBA0F8
     forward   = u8[info+6]            ; 0xBA0FC
     total = frame_len + 0x30 + 0xC     ; 0xBA0FE/0xBA110
     if total > 0x680 -> bump dev+0xCC5, drop (log 0x497)
     if forward == 0 -> drop (log 0x49F)
     group nibble = 1                   ; 0xBA14A
     hdr[6] = 0xD                       ; 0xBA150
     hdr[4] = frame_len + 0x3C          ; 0xBA146/0xBA156
     copy 48-B info + MPDU              ; 0xBA15C..0xBA174
     bl 0x102760                        ; 0xBA17A - deposit
     bl 0x1024E6                        ; 0xBA0E2 - free the node
```

### 3.2 The per-chip RX context gate **[V]**

At **0xB97EE** the builder does `add.w r3,r6,#0x1a00; ldr.w r8,[r3]; cmp r8,#0` and on
NULL logs line 0xA4E and returns 0x8B2D.  `r6` is the chip/band object resolved from the
node header (0xB97A0-`0xB97AE`, resolver 0xA4FC8).  This is the **only demand-side state**
the consumer reads before touching the frame; a host-only boot can have the VAP but not
this per-chip RX context.  It is also the one gate the earlier crx-gating lane flagged as
host-influenceable (its G1).  **[V] instruction, [I] runtime state.**

### 3.3 The builder's gate matrix (re-derived, agrees with wifi-rx-crx-gating) **[V]**

| gate | CA | condition | on failure |
| --- | ---: | --- | --- |
| RX ctx | 0xB97EE | `[chip+0x1a00] != 0` | drop, log 0xA4E |
| queue pop | 0xBA0C0 | node available | loop ends |
| free-ring/complete info | 0xBA0CE | `0x40CBA` returns non-NULL | drop, log 0x48C |
| total cap | 0xBA110/0xBA114 | `frame_len+0x3C <= 0x680` | drop, log 0x497, dev+0xCC5++ |
| forward flag | 0xBA0FC/0xBA134 | `info[+6] != 0` | drop, log 0x49F |
| info length | 0xBA0F8 | `u16[info+0x11]` read | - |
| deposit | 0xBA17A | `0x102760` returns 0 | free node, log 0x4B3 |

None of these gates is host-controlled except the RX-ctx object's existence (G1); the
rest are properties of the received frame / the firmware's own state.

---

## 4. The PPDU descriptor pool (what the list at device+0x66C is)

* **Boot init [V]:** `0xD1B68` (device init, called at **0x48314**) loops bands and calls
  **0xC016C** at **0xD3BAC** with the per-band device object from getter **0xF4F48**
  (`0x174BF0 + 0xE28*band`, literal 0xF4F60; stride immediate 0xE28 at 0xF4F4E).
* **0xC016C [V]:** sets list next/prev self-pointers at +0x66C/+0x670, count 0 at +0x674,
  debug-name **0x1060F7** at +0x67C (literal at 0xC0484), then pre-allocates 0x1A0-byte
  descriptors with **0xBF128** and appends them (0xC043A..0xC0450).
* **Datapath reserve [V]:** `0xB52F0(dev, pool, buf)` links a 0x198-byte descriptor into
  `dev+0x66C` under the lock at +0x678 (memset 0xC1E0C, append 0xB536C-0xB5374, count
  0xB537E-0xB5388); its callers are 0xB5458, 0xBAF10 and 0xBBD40, i.e. the RX/TX
  datapath feeds the list before descriptors go to hardware.
* **Meaning [I]:** this is the firmware-owned descriptor pool that the MAC/DMAC consumes
  when receiving a PPDU; the host's 36-byte RX-buffer header +0x00 is the *device-authored*
  pointer into it (cross-lane wifi-rx-capture-plan PPDU-DESCRIPTOR-NOTES).  It is not a
  host-populated structure, and the corrected probe (name word at obj+0x67C) confirms only
  that the boot init ran - which the cross-lane data already expects.

### 4.1 Runtime chain (bench [M] datum) - the pool is present and populated

The task context / ../wifi-ppdu-init probe pass measured the corrected list on the bench:

| item | bench value | meaning |
| --- | --- | --- |
| obj+0x67C name word | 0x001060F7 | the boot init ran (ppdu-init P2) |
| obj+0x66C list next (head node) | **0x01019F80** | a descriptor node in **PKTRAM** (CA 0x01xxxxxx) |
| obj+0x670 list prev (tail node) | **0x010186A8** | the tail node |
| obj+0x674 count | **0x10** | 16 pre-allocated 0x1A0-B descriptors |

Conversion: PKTRAM phys = 0x40000000 + 0x1D8000 + (CA - 0x01000000), so the head node is
phys 0x401F1F80 and the tail phys 0x401F06A8.  The descriptors are allocated at boot by
**0xBF128** (sub-pool descriptor array at CA **0x172134**, 0x188-B records, blocks of
0x1A0 = 8-B header + 0x198 payload) and pre-filled by 0xC016C.  **Consequence:** the
descriptor pool is present, populated and device-visible on the bench boot, so the F1 is
not the list init and not "no descriptors" - the list waits for a completed PPDU; the
missing layer is the completion producer described in sec. 0/3.  **[M] bench values,
[V] allocator decode.**

### 4.2 Datapath counter words inside the device object (probe targets)

| word | site | semantics [I] |
| --- | --- | --- |
| dev+**0xCC5** (u8) | 0xBA128-0xBA12E (ldrb/adds/strb) | incremented when a frame exceeds the 0x680 CRX cap |
| dev+**0x1ADA** (u8) | 0xBAF2A-0xBAF36 (movw 0x1ada) | incremented when a frame status byte tests negative |
| dev+**0x1AD4** (u8) | 0xBAF38-0xBAF42 (movw 0x1ad4; cmp #1) | per-frame flag word compared with 1 before a field write |

These are read as part of the same descriptor handler that calls the CRX builder, so a
differential read across a client-auth burst is a direct "did the datapath touch this
frame" observation (PROBES P5-P8).  The object base is the same per-band device object the
PPDU-list reserve calls use (`sl`/`[sp,#0xc]` in 0xBA1C8; [I] base identity, [V] offsets,
self-checked by P1).

---

## 5. Candidate host states against the new "the firmware's own frames deliver" datum

| candidate | committed finding | why the new datum does not rescue it |
| --- | --- | --- |
| H2D RX-init message | no such message in the stock capture [V\*]; the two firmware names (`rx_ring_reset_req`/`enable_trx_req`) are internal names | delivering g1/g4 frames proves the H2D service loop consumes what we do send, not that a missing send would have armed the RX producer |
| RX thread | it is the H2D *message* thread (0x102552) [V\*] | a different thread from the datapath handler 0xBA1C8 |
| D2H publish | complete (notify/ETE decoded) [V\*+V here] | delivering ready/heartbeat proves the publish works - the MAC-RX event simply never reaches it |
| PPDU descriptor list | present/initialised at boot [V\*+V here] | the list is firmware-owned; presence does not mean a PPDU was received/consumed |
| EDCA | posted + consumed [V\*] | modifies a per-chip context only; no ring/TRX enable |
| RX ring/format/transform/enable | all vendor-correct [V\*] | they gate the *host buffer* side; with `rx_enable=0` the same frames were harmless (no bit10, no page loss) - the RX producer path is independent of the host ring content |
| WOC buffer class | not the fix [V\*] | same reason |
| completion phase / prepublish | bit 2 can be ticked; frames still 0 [V\*] | the host completion consumer moved; the firmware RX producer did not |

**The residual, stated exactly:** on a boot where the host rings are programmed and a
client transmits auth frames, does the firmware post a received-frame node into the queue
consumed at 0xBA0C0?  If yes, the consumer runs and the g1 id13 event or one of its drop
logs appears; if no, the SMAC RX-completion producer is the missing layer.  PROBES P1-P10
decide this without any device write.

---

## 6. Why the client frames vs the firmware's own frames differ - the concrete difference

The firmware's own delivering frames are all **firmware-internal producers** (heartbeat
worker 0xC6B6A, boot-ready task, config-reply path) that call the deposit primitive
directly.  A client frame must instead traverse:

```
MAC/DMAC PPDU completion -> SMAC RX ISR (smac_rx_complete_2g/5g_isr) -> RX descriptor
  -> internal frame node -> queue -> datapath handler 0xBA1C8 -> CRX builder 0xB9780
  -> hcc_msg_tx 0x102760 -> ETE 0xC192A -> notify 0xC6170 -> host D2H reader
```

The delivery *after* 0xBA1C8 is proven by the ready/heartbeat traffic; the layer that is
unproven and silent is the left half.  The bench's intermittent page golden-loss on the
same runs that latched bit 10 is the fault signature expected if that left half runs on
state that a host-only boot never built (B2/B3); if it never runs, the frame is simply
dropped before the firmware consumer (B1).  The probes distinguish the cases; the fire
recipe is therefore a **measurement pass**, not a blind message.

---

## 7. Limits

* The SMAC RX-completion producer entry is **[U]**: the ISR name table (CA 0x12266C) is not
  code-referenced, so it cannot be mapped to handler addresses from the image statically.
  The claim "the producer is the missing layer" is an elimination, not a location.
* The role labels on 0xBA1C8 (datapath handler) / 0xB9780 (CRX builder) are **[I]** from
  the call structure and the committed CRX wire decode; the instructions and addresses are
  **[V]**.
* The counter words (+0xCC5/+0x1AD4/+0x1ADA) are verified instructions with an inferred
  semantics; their base is the per-band device object **by the same register use**, and the
  probe self-check (P1 name word) must hold before they are interpreted.
* Device-object runtime values are **[U]** - this lane is host-only.  Nothing here
  authorises a device write; the only "fire" is the read-only probe pass.
* No firmware/vendor byte is committed: the tool reads the local image, the docs quote
  decoded addresses/opcodes/strings only.
