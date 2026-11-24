# Context-create H2D message: the g0 id1 / key 0x64 config event that makes the firmware call CA 0xFE6BC

**Lane:** `wifi-ctx-create` (shared task **task-101**), 2026-11-24. **Host-only static RE.**
The pinned vendor firmware blob and the vendor .ko modules stay local evidence only; nothing
vendor-derived is committed here (addresses, decoded tables, constants and a *synthesized* frame
only). Bench evidence is cited from the Lead's PD3/PD4/txq_sync fires (attended sessions).

Address conventions (project-wide, unchanged): image **CPU = file + 0x40000**; host **BAR0 of
firmware RAM = CPU + 0x6B8000** (region 5, size 0x218000); device CA window
0x40000000..0x4011FFFF only. Evidence markers: **[V]** verified here with
`tools/ctx_create_re.py` (40/40 assertions) or `tools/ctx_create_frame.py` (26/26);
**[V*]** device-measured by earlier fires; **[I]** inference; **[U]** undecidable without a
device read.

---

## 0. TL;DR

1. **The context-create H2D message is a group-0 / id-1 config event** (the
   `hdpp_config_send_event` shape already byte-modelled in `../wifi-h2d-frames`) **carrying
   `u16 key = 0x0064` at payload+0x8**. The g0-id1 handler (CA 0x43D58) looks the key up in its
   **static key table A at CA 0x10C23C** (217 x `{u16 key; u16 pad; u32 worker}`) and calls
   **worker CA 0x449E4**, the only host-reachable caller of the **context-create helper
   CA 0xFE6BC** (`str.w r0,[ctx,#0x190]` at CA 0xFE7E4). [V]
2. **The worker chooses the context from its data block** (payload+0xC):
   `data[0]` = module type (**1/2/3** only reach the +0x190 allocation), `data[1]` =
   parent/vap module index (lookup gate, `ctx[1]` must read 0), `data[4]` = **the module index
   that gets created** (`ctx[0]=data[4]`, `<= 0x12`). It calls
   `0xFE6BC(module[data[4]], G=0x170E28, data[4], data)`. [V]
3. **For the victim TXQ config sync (hdr[3]=0x04, key 0x00d8) the created context must be
   module 4**: `data[4] = 4` (and `hdr[3] = 4`). The create frame is the 48-byte config event
   built by `tools/ctx_create_frame.py`; the payload is **synthesized from the firmware parser,
   not a vendor capture**. [V]/[I]
4. **Boot does NOT create the TXQ context.** The boot task's own call at CA 0x48862 creates
   module **18** (`0x12`) from a **zeroed** data block -> type 0 -> helper returns without
   allocating +0x190. So on a fresh boot `ctx4+0x190 == 0` and the captured TXQ sync **must**
   fault at CA 0x81040 (exactly the PD4 fire: last-frame evidence CA 0x81039/0x43DF5,
   mask 0x12). The only writer of `ctx+0x190` in the whole image is CA 0xFE7E4 (plus its clear
   at CA 0x44ADE). [V]
5. **Verification read (host-readable, read-only):** module-table entry `G+(0x18C+4)*4` =
   CA **0x171468** = BAR0 **0x829468** (expect `0x0011C458`); the derived context body is
   CA **0x11C458** = BAR0 **0x7D4458**; the decisive field `ctx4+0x190` is CA **0x11C5E8** =
   BAR0 **0x7D45E8** (expect 0 before create, non-zero after; the allocated buffer then carries
   `+0x10=0x64, +0x14=3, +0x238=0x92a, +0x52=1`). [V]
6. **Single next bench action:** on a golden PD4 boot, post **one** synthesized g0/id1 key-0x64
   create frame (48-byte node length, mailbox id-3 announce), take the read-only pack, then the
   existing TXQ-sync 72-byte frame and confirm **no new exception** (mask CA 0x40008 stays 0
   and the PD4 staging frame at BAR0 0x6F800C is unchanged). Details in section 9.

---

## 1. Inputs, method, tools

Local evidence (gitignored, read in place):
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256
`7fc87e20...311b`; vendor `hi5622v100_wifi.ko` from the rootfs teardown (md5 `4737fcb2...`).
Prior committed lanes are cross-referenced, not re-derived:
`../wifi-irq-unmask/REPORT-PD4.md` (field-B context precondition, worker fault model),
`../wifi-h2d-catalog/REPORT.md` (HCC registration tables / registrar / mailbox layers),
`../wifi-h2d-send/REPORT-G0ID1-HANDLER.md` (g0 id1 handler parse: hdr[3], key, subkey, data),
`../wifi-h2d-frames/REPORT.md` + `../../phase38/announce-body-bytes.md` (exact config-event wire
shape and the live 72-byte first frame), `../wifi-boot-gate/REPORT.md` (boot-task chain,
module-table init, the boot create at CA 0x48862).

Tools in this directory (both read the pinned blob in place; no device, no vendor byte copied):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-ctx-create/tools
$PY $T/ctx_create_re.py --selftest        # 40/40 PASS (addresses, tables, handler/worker/helper bytes)
$PY $T/ctx_create_re.py --trace           # the full handler -> key table -> worker -> helper chain
$PY $T/ctx_create_re.py --tables          # key table A/B (key -> worker)
$PY $T/ctx_create_re.py --readback        # the bench read-only pack with BAR0 addresses
$PY $T/ctx_create_frame.py --selftest     # 26/26 PASS
$PY $T/ctx_create_frame.py --frame --pad72
$PY $T/ctx_create_frame.py --recipe       # committed as recipes/ctx-create-g0id1-key64.json
```

The verification used the project address convention: the image is one flat Thumb-2 blob;
branch/caller scans decode every even file offset, and table bases are decoded from the literal
pools the handler itself loads (CA 0x43E18 -> 0x10C23C, CA 0x43E1C -> 0x103BF8).

---

## 2. The H2D receive chain, and where 0xFE6BC sits

```
host: SR node {buf VA, (total<<16)|0x6D2B}  + mailbox id 3 announce (out[0]|=1<<3, out[2]|=1)
  -> firmware pcie_msg thread  -> mailbox dispatcher CA 0xC18AC (wrapper CA 0x40294)
     (ids 1/3/5/6; id 3 runs the HCC message worker)
  -> HCC message layer: hdr[0]&0xf = group, u16 hdr[6] = id
     group-0 receive table CA 0x10CCFC (40 x 12 B, registered by CA 0x102866 from the boot
     chain at CA 0x48346; group-2 table CA 0x10CC18 at CA 0x4833C)
  -> (group 0, id 1) handler CA 0x43D58
       hdr = hcc_msg_get_hdr_validate(msg)            CA 0x409EA
       ctx = lookup(0, hdr[3])                        CA 0xFE578
       key = u16 hdr+0x14 ; subkey = u8 hdr+0x16 ; data = hdr+0x18
       search table A CA 0x10C23C (217 entries) then table B CA 0x103BF8 (88 entries)
       worker = entry.worker ; blx worker(ctx, subkey, data)
  -> key 0x0064 -> worker CA 0x449E4 -> context-create helper CA 0xFE6BC -> ctx+0x190
  -> key 0x00D8 -> worker CA 0x81038 -> ldr [ctx+0x190] ; strb payload+0xC[0] -> +0x4c
```

Note on the framing in the task/Lead notes: **CA 0x17D430 is the system-object event handler
table** used by the device event dispatcher CA 0xC2EFC (event controller layer, see
`../wifi-event-router`); it is **not** the H2D HCC group/id table. The H2D tables are
CA 0x10CCFC (group 0) and CA 0x10CC18 (group 2). The mailbox dispatcher CA 0xC18AC and wrapper
CA 0x40294 sit *below* the HCC message layer; they never parse group/id. [V]

---

## 3. Key table A - the corrected entry layout, and the two relevant keys

The g0-id1 handler loads two static tables (literals at CA 0x43E18 / CA 0x43E1C) and searches
for `key = u16 hdr+0x14`. Each entry is **8 bytes**:

```
u16 key ; u16 pad/parent ; u32 worker (Thumb address, low bit set)
table A: CA 0x10C23C, 217 entries (0x6C8 B)
table B: CA 0x103BF8,  88 entries (0x2C0 B)
```

The relevant entries (`ctx_create_re.py --tables`):

| table | entry | CA | key | worker | role |
| --- | ---: | ---: | ---: | ---: | --- |
| A | 1 | 0x10C244 | **0x0064** | **CA 0x449E5 (0x449E4)** | **context create / user add** |
| A | 41 | 0x10C384 | **0x00D8** | CA 0x81039 (0x81038) | TXQ config sync (the captured frame's cmd) |
| A | 164 | 0x10C75C | 0x0320 | CA 0x81445 | TXQ-table-address sync in the *teardown* .ko build |
| A | 0 | 0x10C23C | 0x0004 | CA 0x811CD | writes `[ctx+0x190]+0x52` (needs a created ctx) |

Correction to a detail in the earlier PD4 note: the table base is **0x10C23C**, so the word at
0x10C240 that looked like "fn 0x811CD" is the *worker* of entry 0 (key 0x0004); the earlier
"caller/registration of 0x449E4 in a hcc+0xB0 table" reading is the same table A entry 1.
There is no separate ops-table registration: **table A entry 1 *is* the registration.**

The two keys matter in this order:

1. key 0x0064 -> worker CA 0x449E4 -> **creates** the per-module context (allocates +0x190);
2. key 0x00D8 -> worker CA 0x81038 -> **uses** `[ctx+0x190]` (the captured TXQ frame).

Table A entry 0 (key 0x0004) and the remaining config keys similarly assume a created context,
which is why the vendor host's config stream must start with the create.

`ctx_create_re.py --tables` also confirms **key 0x64 is in table A only** (not table B), and
the whole-image caller scan finds **exactly two callers of CA 0xFE6BC**: CA 0x48862 (boot task)
and CA 0x44AAC (this worker). [V]

---

## 4. The create worker CA 0x449E4 (key 0x0064) - what the data block must contain

Signature as called by the handler: `worker(ctx_from_hdr3, subkey, data)` where
`data = hdr+0x18` = payload+0xC. The worker ignores the handler ctx and re-derives all indices
from the data block. Decoded flow (CA 0x449E4..0x44AEA, listing via
`ctx_create_re.py --trace`):

```
449E4 push {r4..fp,lr}
449EC r1 = data[1] ; r0 = lookup(0, r1)      ; parent/vap module; 0 -> log+return 0x64
44A08 r1 = ctx2[1] ; r0 = 0x9438C(ctx2[1])   ; must read 0 -> returns G singleton CA 0x169E90
44A24 r5 = data[4] ; 0xBC726(data[4])        ; refcount bump, returns 0 for id <= 0x12
44A4C ctx3 = lookup(0, data[4])              ; the module that will be CREATED; 0 -> log+return
44A60 ctx3[0x187] == 0x2B ? (log only)
44A80 data[0] == 2 ? store data[8]&1 to a global
44A94 memset(ctx3+0x490, 0, 0x6B0)           ; exactly the 2nd half of the 0xB40 block
44A98 r5 = [CA 0x169E90]                     ; = CA 0x170E28 (G) after the boot per-record body
44AAC 0xFE6BC(ctx3, G, data[4], data)        ; <-- the create helper
44AB0 if ret != 0: log + clear ctx3+0x190
44AE2 0x943CC(ctx3[2], data[4])              ; set the in-use/refcount field
```

Data-block requirements (payload+0xC; the frame declares `len` bytes but only these offsets are
consumed):

| offset | meaning | default instance |
| ---: | --- | --- |
| `data[0]` | module **type**; the create helper allocates +0x190 only for **1/2/3** | `3` (minimal init; 1/2 add the rate/retry defaults at CA 0xFE804) |
| `data[1]` | parent/vap module index for the gate lookup (0..18; that module's byte 1 must be 0) | `4` (fresh zeroed module) |
| `data[4]` | **user id = module index created** (`<= 0x12`; also stored as `ctx[0]`) | `4` (the TXQ sync's field B) |
| `data[8]` | flag copied to a global when `data[0]==2` | `0` |
| `data[0x11], data[0x12]` | flags read by the create helper | `0` |
| `data[2..3], data[5..7], ...` | not read | 0 |

Minimum data length = 0x13 (the helper's highest read is `data[0x12]`); the builder uses
`len = 0x14` (20), matching the token/key/len/pad frame geometry of the captured config event.
No MAC address is consumed by this path: the create helper only copies `G[1],G[2]` ->
`ctx[1],ctx[2]`, `G[4..7]` -> `ctx[8]` and stores `data[4]` at `ctx[0]`; MAC/key material
belongs to the later per-context config keys. [V]

---

## 5. The create helper CA 0xFE6BC - the only `ctx+0x190` allocator

```
FE6BC 0xFE6BC(ctx=r0, G/desc=r1, id=r2, data=r3)  returns 0 ok, 0x64 rejected, 0x67 alloc fail
  guard: r0/r1/r3 nonzero else log line 0x2FD + return 0x64
  ctx[2]=r1[2] ; ctx[1]=r1[1] ; ctx[3]=0 ; ctx[8]=r1[4] ; ctx[0]=r2
  ctx[4]=data[0] ; ctx[5]=data[0x11] ; ctx[6]=0 ; ctx[7]=data[0x12]
  ... defaults at +0xD0/+0xDA/+0x480/+0x481/+0x18C..+0x18E, list init at +0xFC..+0x17C,
      ctx+0x400 list init ...
  type = ctx[4] = data[0]:
      type 1/2  -> CA 0xFE804 defaults (ctx+0x3E2=0xFFF, +0xF8=0xFFF, +0xD1=0x1E, flags)
      type 3    -> straight to the alloc path
      type 0    -> CA 0xFE7C4 then the no-alloc path
      type >6 / 4 / 5 / 6 -> no-alloc path
  alloc path: 0xBF128(0x96, 0x327, 1, 0x3A8) -> allocate the per-context buffer
              CA 0xFE7E4: str.w r0,[ctx,#0x190]      <== the ONLY +0x190 writer
  success: buffer init writes +0x10=0x64, +0x14=3, +0x238=0x92A, +0x23C=0x92A, +0x52=1,
           +0x49..+0x4C=1, +0x46=0x80, +0x58 bits, ... and returns 0
  type 0/4/5/6 -> CA 0xFF940: ctx[0x187]=0x2B, return 0, **no +0x190**
```

Two callers only (whole-image Thumb BL scan):

| call site | caller | id used | data block | +0x190? |
| ---: | --- | ---: | --- | --- |
| CA 0x48862 | firmware boot task per-record body | `0x12` (module 18) | `sp+0x54`, memset 0x18 (CA 0x487EA) -> data[0]=0 | **no** (type 0) |
| CA 0x44AAC | H2D g0 id1 key 0x64 worker | `data[4]` | host payload+0xC | **yes** for data[0] in 1/2/3 |

Consequence: **on every boot the firmware creates module 18 (`CA 0x1261D8`) with type 0 and no
+0x190; no boot path creates any other module.** So the vendor's first post-ready TXQ sync
(fb=4) finds `module4+0x190 == 0` unless a key-0x64 create ran first. [V]

The boot path is also why the PD4 model is exact: the boot's `0x49204` chain fills all 19
module-table pointers (so lookup(0,4) is non-NULL and the handler does **not** take its clean
zero arm) and zeroes the bodies (so +0x190 is NULL) - the worker then stores through NULL. [V]

---

## 6. Answer to part (2): is the TXQ sync's context id 4 created, and what must the payload contain?

* **Field B (hdr[3]) = 4 is a module-table index**: handler `ctx = lookup(0,4)` =
  `*(G+0x630+4*4) + u16(G+0x6DC)` = `CA 0x11C458` (when u16(G+0x6DC)=0). It is **not** created
  by boot (boot creates index 0x12 = 18) and it is **not** created by the TXQ sync frame itself
  (key 0xd8's worker only stores one byte through +0x190).
* **It is created by the g0 id1 key-0x64 message with `data[4] = 4`.** The same message should
  use `hdr[3] = 4` (the handler's lookup must be the same module) and `data[1] = 4` (or any
  0..18 whose byte 1 is 0) for the parent gate.
* **Payload requirement summary**: type `data[0] in {1,2,3}` (use 3 for the minimal instance;
  1/2 are the vendor's extra-default types), `data[1]` valid module, `data[4]=4`,
  `data[0x11]=data[0x12]=0`, length >= 0x13 (builder uses 0x14). No MAC, no user-name/key
  material is consumed on this path.
* **Uncertainty [I]:** the key-0x64 *vendor provenance* is not provable from the artefacts here:
  the rootfs teardown `wifi.ko` (md5 `4737fcb2...`) has only three `hdpp_config_send_event`
  call sites (keys 0x78, 0x78 and 0x320) and the live capture's cmd 0x00d8 has **no producer**
  in that build either (a known phase38 caveat; the live box was observed with an overlay-patched
  `wifi.ko` per phase16). The shipped build therefore numbers its config keys differently. The
  *firmware* side is what matters for our host: on this FIRMWARE.bin, key 0x64 is the only route
  to CA 0xFE6BC, and the frame geometry is the vendor-proven config-event shape.

---

## 7. The frame (synthesized) - byte-exact spec

`tools/ctx_create_frame.py --frame --pad72` (default instance: type 3, vap 4, user 4, field B 4,
key 0x0064, len 0x14, total 0x30):

```
header : 00 01 00 04  30 00 01 00  00 00 5a 5a    group 0, alloc=1, dst=0,
                                                   fa=0, fb=4, total=0x30, id=1,
                                                   fc=0, dst=0, tag=0x5a5a
payload: 00 00 00 00 01 00 00 00                    token[8] (builder sets it; inherited in vendor frames)
         64 00                                      u16 key   = 0x0064
         14 00                                      u16 len   = 0x0014
         03 04 00 00 04 00 00 00                    data[0]=type 3, data[1]=vap 4, data[4]=user 4
         00 00 00 00 00 00 00 00 00 00 00 00        data[8]=0, ..., data[0x11]=data[0x12]=0
         ff ff ff ff                                inherited pad (not written by any builder)
48-byte frame (node length = header total = 0x30) / 72-byte buffer with 24 x ff (capture style)
```

Frame hex (48 B):
`000100043000010000005a5a0000000001000000640014000304000004000000000000000000000000000000ffffffff`

Two interchangeable node-length variants: the header-consistent **48-byte** node (the
`wifi-h2d-frames` working rule) and the capture-style **72-byte** buffer (24 x 0xFF tail,
exactly like the live vendor first frame). Both carry the same 0x30 declared total.

Emission rule for this lane (same safety frame as `wifi-h2d-frames`): SR node
`{word0 = buffer device VA, word1 = (total<<16)|0x6D2B}`, then mailbox id-3 announce
(`out[0] |= 1<<3`, `out[2] |= 1`); never post when `out[0] != 0`; never write CA 0x400392F0
(out[5]) or mailbox id 5. The create frame is safe to send even if a precondition is missing:
every failure arm logs and returns (0x64 / 0x67); the only dereference on this path is
`ldr r5,[CA 0x169E90]` which is a fixed RAM word, and the create target is always a
boot-populated module pointer. [V]

---

## 8. Why the canned TXQ sync is invalid without the create (fault model)

On our host-only boot the g0-id1 TXQ sync with `hdr[3]=0x04`, key 0x00d8 resolves
`ctx = module 4 = CA 0x11C458` (non-NULL: boot filled the table, body zeroed), then the
key-0xd8 worker CA 0x81038 executes

```
8103A  ldrb r1,[r2]                 ; data[0] = payload+0xC[0]
8103C  ldr.w r3,[r0,#0x190]        ; r0 = ctx4 ; r3 = 0 (no create ran)
81040  strb.w r1,[r3,#0x4c]        ; store to address 0x4C -> precise data abort
```

PD4's first-fault frame read confirmed the worker/handler evidence (CA 0x81039 / CA 0x43DF5,
mask 0x12). Payload tweaks cannot fix it: **every** module index 0..18 is boot-populated with a
zeroed body, and +0x190 has no boot writer - only CA 0xFE7E4 (create) and CA 0x44ADE (create
failure clear). So the message order must be **create (key 0x64, data[4]=4) -> sync (key 0xd8)**.
[V]/[V*]

Vendor-boot nuance [I]: on a normal vendor boot the driver's first post-ready H2D *is* this sync
(`../wifi-vendor-userspace` chain: `hmac_main_init -> hmac_board_init -> hdpp_chip_sync_add ->
hmac_txq_table_init -> hmac_txq_table_addr_sync`). Our static reading says that frame cannot
reach the +0x190 store before a create (or it lands before the boot's table fill and is dropped
on the handler's clean zero arm). The vendor's periodic id-1 stream (phase25/phase38 saw recurring
72-byte id-1 frames) is the natural place for the create to precede the effective sync; this is
consistent with a working vendor but not provable from the teardown modules. That uncertainty is
precisely why the bench ladder below verifies the create state first. [I]

---

## 9. Bench ladder (attended; recovery armed; one frame per idea)

Preconditions: standard golden-boot procedure that reaches the ready pair with the PD4 patched
image (sha256 `cfe24bfb...037b1`); `tools/bench-link.sh` PASS; recovery card armed; no `rmmod`;
never write CA 0x400392F0, never mailbox id 5. All reads are host read-only reads of the region-5
BAR0 window (BAR0 = CPU + 0x6B8000); no device-RAM write is needed.

### Step 1 - pre-create read pack (read-only, one shot)

| # | read | CPU CA | BAR0 | expect | decides |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | G singleton | 0x169E90 | 0x821E90 | 0x00170E28 | boot per-record body ran (worker precondition) |
| 2 | module-table entry 4 | 0x171468 | 0x829468 | 0x0011C458 | boot `0x49204` chain ran (lookup non-NULL) |
| 3 | u16 common offset | 0x171504 | 0x829504 | 0x0000 | lookup additive term |
| 4 | **ctx4+0x190** | **0x11C5E8** | **0x7D45E8** | **0x00000000** | the precondition is absent (as PD4 predicted) |
| 5 | ctx4 body | 0x11C458 | 0x7D4458 | 0x00000000 | body zeroed |
| 6 | exception mask | 0x400008 | 0x400008 | 0x00000000 | clean start |
| 7 | PD4 cave | 0x108240 | 0x7C0240 | stub bytes | cave integrity |

### Step 2 - post ONE create frame

Use the existing validated sender (same transport as the txq_sync72 fire) with the 48-byte frame
from `tools/ctx_create_frame.py --frame` (or the 72-byte pad variant). Node length = header total
(0x30) for the header-consistent rule; announce mailbox id 3. Then wait/observe that the device
consumed the node (SR consumer advances) and `out[0]` clears - same contract as the prior
txq_sync fire.

### Step 3 - post-create read pack (read-only)

| # | read | BAR0 | expect | meaning |
| ---: | --- | ---: | --- | --- |
| 1 | **ctx4+0x190 = V** | **0x7D45E8** | **non-zero** | the create allocated the per-context buffer |
| 2 | V+0x10 | V+0x6B8010 | 0x00000064 | create-buffer signature (CA 0xFE8FE) |
| 3 | V+0x14 | V+0x6B8014 | 0x00000003 | create-buffer signature (CA 0xFE8DA) |
| 4 | V+0x238 | V+0x6B8238 | 0x0000092A | create-buffer signature (CA 0xFE8E4) |
| 5 | V+0x52 | V+0x6B8052 | 0x01 | create helper CA 0xFE8F6 |
| 6 | ctx4[0] | 0x7D4458 | byte = 4 | ctx[0] = data[4] (id) |
| 7 | ctx4[4] | 0x7D445C | byte = 3 | ctx[4] = data[0] (type) |
| 8 | in-use refcount | 0x8294F4 | 0x01 | G+0x6C8+4 bumped by CA 0xBC726 |
| 9 | exception mask | 0x400008 | 0x00000000 | no exception during create |
| 10 | PD4 staging frame | 0x6F800C | unchanged/zero | no new exception frame |

Note on V: it is the allocator's return value. Firmware-RAM pointers are CPU addresses
(`BAR0 = V + 0x6B8000`); if V reads in the device-CA window (0x40xxxxxx) instead, use the
device-IO BAR0 convention from `BENCH-FACTS.md` for the buffer reads and report it [U].

### Step 4 - post the existing TXQ sync (72-byte txq_sync72 frame), then read-only

Pass criteria: **consumed with no new exception** - `out[0]` clears, mask CA 0x400008 stays 0,
the PD4 first-frame area at BAR0 0x6F800C is unchanged, and the cave at BAR0 0x7C0240 still
holds the stub. (The worker writes payload+0xC[0] = 0x01 into V+0x4C, where the create already
left 0x01, so that byte is not a discriminator - the absence of a fault is the observable.)
Negative control (already measured by PD4, do not repeat): sync without create -> data abort at
CA 0x81040, last-frame evidence CA 0x81039/0x43DF5, mask 0x12.

### Step 5 - fallback matrix

* create consumed but `ctx4+0x190` still 0 -> the worker failed at a gate; read
  `0x821E90` (must be 0x00170E28), `0x8294F4` (refcount must bump to 0x01), and check the log
  path (type 0/4/5/6 no-alloc returns are silent on the wire).
* `ctx4+0x190` non-zero but signatures 2-5 mismatch -> the allocated buffer is not the create
  buffer: report; do not proceed to step 4.
* create itself causes an exception -> stop; the only possible faults would be a wrong field B
  (module index out of range) or data[1] parent with byte 1 != 0; both are config errors.

---

## 10. Evidence table

| # | claim | method | grade |
| ---: | --- | --- | --- |
| E1 | g0-id1 handler CA 0x43D58: `ctx=lookup(0,hdr[3])`, `key=u16 hdr+0x14`, `data=hdr+0x18` | disasm + byte assert | [V] |
| E2 | key tables A CA 0x10C23C (217 x 8 B {u16 key;u16 pad;u32 worker}) and B CA 0x103BF8 (88) | handler literal decode + table dump | [V] |
| E3 | key 0x0064 -> table A entry 1 -> worker CA 0x449E4 | table decode + byte assert | [V] |
| E4 | key 0x00D8 -> table A entry 41 -> worker CA 0x81038 (the captured TXQ sync) | table decode | [V] |
| E5 | CA 0xFE6BC has exactly two callers: CA 0x48862 (boot) and CA 0x44AAC (worker) | whole-image Thumb BL scan | [V] |
| E6 | the helper allocates and stores `ctx+0x190` only for data[0] in {1,2,3}; type 0/4/5/6 return without allocating | disasm of CA 0xFE6BC..0xFF948 | [V] |
| E7 | the boot create (CA 0x48862) uses id 0x12 and a zeroed 0x18-byte data block -> no +0x190 on boot | disasm CA 0x487EA/0x48862 + helper | [V] |
| E8 | worker data layout: data[0]=type, data[1]=parent module, data[4]=created module id; no MAC consumed | disasm CA 0x449E4..0x44AEA + helper | [V] |
| E9 | module 4 = CA 0x11C458; table entry `G+(0x18C+4)*4` = CA 0x171468; `ctx4+0x190` = CA 0x11C5E8; BAR0 0x7D45E8 | address arithmetic from PD4 facts | [V] |
| E10 | the create frame geometry matches the vendor config-event shape (key@+8, len@+0xA, data@+0xC, total 0x1C+len) | `../wifi-h2d-frames` + phase38, cross-checked against this firmware's handler | [V] |
| E11 | payload instance (type 3, vap 4, user 4) is a valid instance of the firmware parser; it is synthesized, not captured | parser-derived constraints + builder selftest | [I high] |
| E12 | vendor key-0x64 provenance unproven (teardown .ko sends 0x78/0x320; live cmd 0xd8 has no producer there; overlay build differs) | rootfs .ko relocation/symbol analysis + phase38 caveat | [I] |
| E13 | ordering create -> sync is required on this firmware: the sync's worker dereferences `ctx+0x190` which only the create sets | E5/E6/E9 + PD4 bench | [V]/[V*] |

---

## 11. Open items

1. **O1 - vendor create key in the shipped build**: cannot be pinned without the live overlay
   `hi5622v100_wifi.ko` (phase16 md5 `e21629d2...`). If that module is ever recovered, the
   key numeric that maps to the firmware's 0x64 route can be confirmed; the frame builder only
   needs the firmware-side key.
2. **O2 - type choice 3 vs 1/2**: 3 is the minimal instance; if later per-context config keys
   need the rate/retry defaults of CA 0xFE804, re-send the create with type 1 or 2 (both
   accepted).
3. **O3 - alloc pointer convention**: whether CA 0xBF128 returns a CPU or device-CA address is
   settled by the post-create read (step 3 row 1); the ladder carries both read conventions.
4. **O4 - first-fault frame on the PD4 fire**: the Lead's frame read is the confirmation of the
   fault model; the create/read ladder here is independent of it and can run in either order.
5. **O5 - module-table entry 0 (key 0x0004)** and the other config keys of table A assume a
   created context; their vendor payloads remain catalogue-only (`../wifi-h2d-frames`).

---

*Lane wifi-ctx-create, 2026-11-24, shared task task-101. Host-only static reversal and frame
specification; all bench actions are read-only except the normal H2D frame posts. The vendor
firmware blob and vendor modules remain local evidence only; this directory keeps addresses,
decoded tables, constants, assertions and a synthesized frame, never vendor bytes.*
