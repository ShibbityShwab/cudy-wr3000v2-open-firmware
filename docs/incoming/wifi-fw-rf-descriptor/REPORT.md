# REPORT - what sets the per-link RF pointer (P), publishes O, and arms the mode gate

**Lane:** wifi-fw-rf-descriptor (shared task **task-189**), 2026-11-25.
**HOST-ONLY**: static decode of the pinned local artifacts; no device contact,
no write to any artifact, no fire. **The Lead fires.**

Companions: [README.md](README.md), [TABLES.md](TABLES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/fw_rf_descriptor_re.py](tools/fw_rf_descriptor_re.py).
Grades: **[V]** re-derived here by the committed tool + pinned artifacts;
**[V***] bench-measured by a committed lane or the Lead; **[I]** inference;
**[U]** needs a device read.

## 0. TL;DR - the six findings

1. **The committed chain is wrong at the first hop.** CA 0x40684 does not
   receive `perlink+0xE20`; it receives **`P = *(perlink+0xE20)`**, a pointer
   dereference. The worker loads it at CA 0x83CCC/0x84228 (`ldr.w
   r1,[r7,#0xE20]`) and CA 0x40684 loads it again at CA 0x406BA. The committed
   probes CPU 0x178238 (`perlink+0x2820`) and 0x17773C are therefore not in the
   RF chain at all; their "0" readbacks prove nothing about the programme. **[V]**
2. **The corrected chain** (all five-hop read-only):
   `perlink = *(ctx+0x4B0)` -> `P = *(perlink+0xE20)` ->
   `O = *(P+0x1A00)` -> `R = *(O+0x2D8)`. P is the **band object**
   (loaded from `ctx+0x4B8` by the mode worker); O is its **published
   per-link**; R is the RF register block. **[V]** identity, **[I]** for "P is
   always the band object".
3. **What publishes O**: the pre-HCC boot task's per-record body writes
   `u32(bandobj+0x1A00) = CA 0xB1EE4(bank)` at CA 0x48A68..0x48A6C, once per
   band record. The bench measured this word == the per-link (0x00175A18)
   [V*] -> **O is already armed on our boot; O is not the blocker**. **[V]**
4. **What stays 0 is the RF mode byte `P+0xF04`, i.e. `bandobj+0xF04`.**
   It has **exactly one writer in the image**: CA 0xB5D1C
   (`strb.w r5,[r4,#0xF04]`) inside CA 0xB5B9C. That function is the g0-id1
   config-event **worker for H2D key 0x01F7** (table A entry 93 at CA 0x10C524,
   {u16 key; u16 pad; u32 worker|thumb}). It reads the mode from the message
   body (`body[0]`, 1..5 via a `tbb` at CA 0xB5D72), writes the mode byte and
   also programs the whole RF register image `R+0x3A8..R+0x418`. **[V]**
5. **The stock host trigger for key 0x01F7 is a WAL userspace command, not the
   AP-start ladder.** The only shipped emitter is
   `hmac_config_set_country_for_dfs` (wifi.ko .text+0x5762C ->
   `hmac_config_send_event(key 0x01F7, len, data)`, payload forwarded
   verbatim); it is reached through the WAL ops slot `.data+0x890C`
   (`wal_config_set_country_for_dfs`, thunk .text+0x12AE10) from the WAL
   command named `set_country_for_dfs`. **Our clean driver's five-frame ladder
   never sends it** -> `P+0xF04` stays 0 -> CA 0xA2FA2 returns silently
   (mode != 3 -> plain return; only a NULL P/O/ctx logs id 2999). This is the
   host-visible trigger candidate. **[V]** emitter/worker, **[I]** that it is
   the remaining bench blocker.
6. **P and R themselves have no writer in the image.** An exhaustive scan
   (Thumb-2 T3/T4 stores, ARM LDR/STR immediate, add/movw immediates, LE32
   literals) finds **zero** stores to `perlink+0xE20` (only 50 loads) and
   **no** store of the R pointer `perlink+0x2D8` (the only store in that field
   family is R+0x2D8 at CA 0xF39E8). Those two pointers are therefore
   established outside the pinned image code (boot prelude/ROM/host patch).
   If the corrected probe reads P == 0 or R == 0, no H2D re-post can fix it. **[V]**
   negative, **[U]** provenance.

**Consequence for the bench:** the decisive next read is `u32(0x176838)` (P)
plus `u8(0x15F474)` (mode) and `u32(0x175CF0)` (R). If P != 0 and mode == 0,
the fix is to add the 0x01F7 mode event to our ladder (FIRE-RECIPE F1), not to
re-post 0x0124.

## 1. Inputs and method

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`; CPU = file + 0x40000 |
| host module | `.../lib/hisilicon/ko/hi5622v100_wifi.ko` (3,581,748 B, read in place) |
| disassembler | lab `arm-openwrt-linux-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x40000` (firmware) + `-d`/ELF parse (module) |
| tables | g0-id1 key table A CA 0x10C23C (8-byte entries `{u16 key; u16 pad; u32 worker|thumb}`) |
| artifacts | `build/wifi-fw-rf-descriptor/` (gitignored; local only) |
| cross-check | `../wifi-datapath/tables/firmware-config-keys.md` entry 93 already lists key 0x01F7 -> worker 0x0B5B9D, independently derived here [V] |

Method: (1) re-disassemble the pinned image and locate every access to the
chain offsets 0xE20/0x1A00/0x2D8 and to the RF mode byte 0xF04; (2) run an
encoding-level census over the whole image for stores/loads of those
displacements in Thumb-2 T3/T4 and ARM encoding, plus immediate/literal forms;
(3) map each writer to its g0-id1 key by decoding table A; (4) decode the
worker's body handling and required firmware state; (5) find the host-side
emitters in the vendor module (ELF .text + MOVW encodings); (6) rebuild the
read-only probe pack from the corrected chain; (7) assert every fact in
`fw_rf_descriptor_re.py --selftest` (62 checks, all PASS).

## 2. The corrected object chain [V]

~~~
perlink = u32(ctx+0x4B0)                       ; bench 0x00175A18
P       = u32(perlink+0xE20)                   ; CA 0x84228 ldr.w r1,[r7,#0xE20]
O       = u32(P+0x1A00)                        ; CA 0xA2FA8 add.w r3,r0,#0x1A00 ; 0xA2FAC ldr r6,[r3]
R       = u32(O+0x2D8) = u32(perlink+0x2D8)    ; CA 0x89840/0x89928 ldr.w r2,[r0,#0x2D8]
~~~
CA 0x40684 is called as `CA 0x40684(ctx, P, perlink)` (worker CA 0x841B8 at
CA 0x84226..0x8422E; second caller CA 0x83AEA). CA 0x40684 loads the same P
again at CA 0x406BA for its cleanup flags (+0x10A8/+0xF40/+0x1028) and passes
it to CA 0xA2FA2. CA 0xA2FA2's own checks, in order [V]:

| CA | instruction | meaning |
| ---: | --- | --- |
| 0xA2FA6 | `cbz r0` | P == 0 -> log id **2999** and return |
| 0xA2FA8 | `add.w r3,r0,#0x1A00` | O address |
| 0xA2FAC | `ldr r6,[r3]` | O |
| 0xA2FAE | `cbz r6` | O == 0 -> log id 2999 and return |
| 0xA2FB0 | `cbnz r1` | ctx == 0 -> log 2999 |
| 0xA2FC0 | `ldrb.w r2,[r0,#0xF04]` | RF mode byte |
| 0xA2FC4 | `cmp r2,#3` ; `bne 0xA30FC` | mode != 3 -> **silent** return |
| 0xA2FD4 | `ldr.w r4,[r6,#0x2D8]` | R (NOT null-checked) |

So the observed "programme never runs" splits three ways: `P==0` or `O==0`
(logs 2999), or `mode != 3` (silent). The committed probes cannot tell them
apart because they read the wrong addresses. The bench's published per-link
(often called "O") is non-zero [V*], which makes **mode == 0** the strongest
candidate for the observed skip.

## 3. Who publishes O (bandobj+0x1A00) [V]

The pre-HCC boot task's per-record body (the same body that initialises the
band object; entry region 0x4845x-0x48Fxx) does, per band record:

~~~
0x48A60  ldr  r3,[sp,#0x2c]
0x48A64  strb.w r3,[r4,#0xD6E]          ; band/rf index byte
0x48A68  add.w r3,r4,#0x1A00            ; r4 = band object (CA 0xA4FC8 result)
0x48A6C  str.w sl,[r3]                  ; bandobj+0x1A00 = published per-link
0x48A70  ldrb r3,[sl,#1]
0x48A78  cmp  r3,#2
0x48A7E  strb.w r3,[r4,#0xD70]          ; min(perlink[+1],2) = band attach flag
~~~

`sl` is the CA 0xB1EE4(bank) per-bank context (0x174BF0 / 0x175A18); the
band object is `CA 0xA4FC8(band)` = `0x15CA08 + 0x1B68*band` (accessor
literal 0x0015CA08 + `movw r1,#7016 (0x1B68)`). Bench read: `u32(0x15FF70)
== 0x00175A18 == ctx+0x4B0` [V*], i.e. **O is already published on our boot.**

## 4. Who arms the mode gate P+0xF04 - the H2D 0x01F7 worker [V]

### 4.1 Table A row 93

Table A at CA 0x10C23C stores 8-byte entries `{u16 key; u16 pad; u32
worker|thumb}`:

| entry | CA | key | worker CA | role |
| ---: | ---: | ---: | ---: | --- |
| 92 | 0x10C51C | 0x00F7 | 0x45A30 | descriptor table (fw-desc-table lane) |
| **93** | **0x10C524** | **0x01F7** | **0xB5B9C** | **RF mode + register image** |
| 94 | 0x10C52C | 0x07E7 | 0xB58FC | CAC state -> MAC regs 0x4010Exxx |
| 95 | 0x10C534 | 0x0136 | 0xB5F3C | [U] |
| 96 | 0x10C53C | 0x07E8 | 0x429B0 | [U] |

Every fact above is asserted by the tool. (The earlier commit history called
this worker "key 0x07E7"; the entry layout is key-first, so it is 0x01F7. The
pre-existing `wifi-datapath/tables/firmware-config-keys.md` row 93 already
agrees.)

### 4.2 CA 0xB5B9C, line by line [V]

~~~
0xB5B9E  ldr.w r4,[r0,#0x4B8]      ; r4 = band object
0xB5BA6  add.w r6,r4,#0x1A00
0xB5BAA  ldr   r0,[r6]             ; published per-link; 0 -> error log 1442
0xB5BC8  ldrb.w r1,[r4,#0xD70]     ; band attach flag; must be 1 (log 1448)
0xB5BE2  ldrb  r5,[r2,#0]          ; r5 = body[0] = mode
0xB5BE8  ldr.w r3,[r0,#0x2D8]      ; R = *(perlink+0x2D8)
0xB5BF0  str.w r2,[r3,#0x3A8]      ; the RF register image (same layout as CA 0xA2FA2)
...      (+0x3A8 .. +0x418)
0xB5D6A  subs r3,r5,#1 ; cmp r3,#4 ; bhi 0xB5EA4   ; mode must be 1..5
0xB5D72  tbb  [pc,r3]              ; per-mode image variant
0xB5D1C  strb.w r5,[r4,#0xF04]     ; *** the only write to the mode byte ***
~~~

So a single 1-byte H2D config event (key 0x01F7, `body[0]` = 1..5) sets both
the mode byte and the RF register image for that mode. The 5 GHz RF programme
CA 0xA2FA2 requires **mode == 3**.

### 4.3 The host-side emitter [V]

`hmac_config_set_country_for_dfs` (wifi.ko .text+0x5762C):

~~~
5762c  push {r4,r5,lr} ; r3=r2 (data) ; r2=r1 (len) ; r5=r0 (ctx)
5763c  movw r1,#0x01F7
57644  bl   hmac_config_send_event        ; send_event(ctx, key, len, data)
~~~

It is exported (ksymtab) and reached from the WAL ops slot `.data+0x890C`
(`wal_config_set_country_for_dfs` -> thunk .text+0x12AE10 -> CA 0x5762C). The
string `set_country_for_dfs` names the WAL command; the vendor userspace WAL
client is what invokes it on the stock box (the exact 1-byte `mode` value it
passes is [U] until the stock H2D trace is compared). grep of the module finds
exactly two `MOVW r1,#0x01F7` encodings (the function above and one site at
.text+0xC3254) -> the emitter set is tiny. The sibling key 0x07E7
(`hmac_config_cac_sync_state` / `hmac_dfs_cac_start/_stop/_timeout_fn`) only
syncs CAC bits into MAC registers 0x4010E0xx; it does **not** touch the mode
byte.

### 4.4 Why our ladder misses it [I]

Our five-frame ladder (0x0064 create, 0x0065 VAP start, 0x0127 AP config,
0x0124 channel, 0xF7 desc table) contains no 0x01F7 event; the clean host has
no vendor WAL userspace. Therefore `P+0xF04` stays at its boot value (0) and
CA 0xA2FA2 takes the silent `mode != 3` return, which matches the observed
"gates 1/1/1, predicate armed, programme silent" state. The stock ordering is
[I]: WAL set_country_for_dfs (mode 3) before/around the channel-start 0x0124.

## 5. The RF register block R [V]

R is dereferenced (not allocated) by the firmware:
`R+0x3A8..+0x418` is the ~30-word RF/PHY/baseband/AGC/TX-chain image. It is
written by **two** workers with the same layout: CA 0xA2FA2 (0x0124 channel
path, ch36-64 via CA 0x89924 / ch100+ inline; requires mode==3) and CA 0xB5B9C
(0x01F7 mode path, one image per mode 1..5). The latch CA 0x89840 writes
`R+0x410 bit0` from the CA 0x101890 predicate; CA 0x89924/0xA2FD4 read R
without a NULL check, so a NULL P (log 2999) is the "programme not running"
signature while R==0 would fault on the first store.

**R itself is never written by image code** (see 6) - it is a pointer field
attached to the per-link before either worker can run.

## 6. Negative writer census: P and R are external [V]

Full-image encoding scan (asserted in `--selftest`):

| access | count | form |
| --- | ---: | --- |
| load `[perlink+0xE20]` | 50 | Thumb-2 T4 LDR imm12 0xE20 (all the RF/user sites) |
| store to `+0xE20` | **0** | no Thumb-2 word/byte/half store, no ARM LDR/STR imm 0xE20 |
| `add/movw/str` immediate 0xE20 | **0** | objdump text search over the whole image |
| LE32 literal 0x00000E20 / 0x00002820 | **0** | raw scan |
| store to `+0x2D8` field family | 1 | CA 0xF39E8 stores into **R+0x2D8** (a field of R), not the pointer itself |

Consequences: the per-link->band back-pointer (P) and the per-link->RF block
pointer (R) cannot be produced by any instruction in FIRMWARE.bin. They are
created by the boot prelude/ROM or a host DMA patch before the image code runs.
This is why the corrected probe (P and R) is the only way to decide between
"init missing on our boot" and "mode gate missing".

## 7. Band-0 vs band-1 differential [V]/[I]

The publish/attach path is **identical per band**: the boot-task record body
loops over the 2 records, writes the band ids at `bandobj+0xD6C` (CA 0x4854A),
publishes the per-link at `bandobj+0x1A00` (CA 0x48A6C) and the attach flag at
`bandobj+0xD70` (CA 0x48A7E), and the two band objects are zeroed together
(CA 0x4852E memsets 0x36D0 B at 0x15CA08 = 2 x 0x1B68). The 5 GHz-only part is
the CA 0x40684 branch itself: it requires `ctx[4]==2` and CA 0x944DE == 1
(band==1, ctx+0x440==1, bandobj+0xEC8==1) before CA 0x101890/CA 0xA2FA2.
The 2.4 GHz band never enters CA 0x40684, so it does not need the mode gate;
that is why a working 2.4 GHz AP does not prove the 5 GHz RF programme ran.
No band-specific writer of O/P/R was found - the asymmetry is the band-1-only
branch plus the mode gate. **[I]** for the "2.4 GHz works without the gate"
consequence.

## 8. Corrected probe pack (read-only)

The full pack with CPU/BAR0/phys is in [TABLES.md](TABLES.md) section T5 and
printed by `--probes`. The minimal decisive set (11 reads):

| # | read | CPU | expected |
| ---: | --- | ---: | --- |
| 1 | `u32(ctx0+0x4B0)` | 0x119C08 (ctx0 0x119758) | 0x00175A18 |
| 2 | `u32(ctx0+0x4B8)` | 0x119C10 | 0x0015E570 |
| 3 | **`u32(perlink+0xE20)` = P** | **0x176838** | **0x0015E570 (band object); 0 = chain broken** |
| 4 | `u32(P+0x1A00)` = O | 0x15FF70 | 0x00175A18 (published per-link) |
| 5 | **`u8(P+0xF04)` = mode** | **0x15F474** | **3 after the 0x01F7 mode event; 0 = never armed** |
| 6 | `u8(P+0xF87)` branch flag | 0x15F4F7 | 0 on ch36-64, 1 on ch100+ |
| 7 | `u8(P+0xD70)` attach flag | 0x15F3E0 | 1 (CA 0xB5B9C requires it) |
| 8 | `u32(perlink+0x2D8)` = R | 0x175CF0 | nonzero block pointer |
| 9 | `u32(R+0x3AC)` | two-step | 0x03E6138A (ch36-64) / 0x056B0597 (ch100+) |
| 10 | `u32(R+0x3A8)` | two-step | 0x01800060 |
| 11 | `u32(R+0x410)` | two-step | bit0 = predicate (read probe 20 first) |

Verdicts: P==0 -> the per-link/band bind never happened on this boot (no image
writer; report, do not patch blind). P!=0, mode==0 -> the 0x01F7 arm is
missing -> F1. P!=0, mode==3 -> the programme did run; re-read R+0x3AC to
confirm the ch36-64 image and move to the beacon/engine candidates (C4/C5 of
the rf-programme lane).

## 9. Fire recipe (summary; full rung in FIRE-RECIPE.md)

* **F0 (read-only, no fire)**: the corrected chain probe on the running
  CW-0/CW-2 AP. Ends the P/O/mode/R ambiguity with no risk.
* **F1 (small host patch, no rebuild of the firmware)**: after the 0xF7
  descriptor-table arm, send a g0-id1 config event **key 0x01F7, len 1,
  body {3}** (the stock `set_country_for_dfs` shape), then re-post the 0x0124
  channel frame and read `P+0xF04` / `R+0x3AC`. If R shows the ch36-64 image
  and beacons start, the blocker is closed.
* **F1b (if mode 3 is rejected or the image is wrong)**: repeat with modes
  1,2,4,5 and compare the R image; keep the readback, do not blind-loop.
* **Abort**: any page/exception anomaly -> power-cycle, no retry. Never write
  0x400392f0; never read the RC misc window 0x10160000..0x1016ffff. Do not
  write any firmware RAM value by hand (the probe pack is read-only).

## 10. Limits / honest unknowns [U]

* **Who writes P and R** (per-link+0xE20 / +0x2D8): not in FIRMWARE.bin by the
  exhaustive scan; likely boot prelude/ROM or a host DMA patch. The probe
  decides whether they are live on our boot.
* **P == band object** is inferred from field-offset consistency (the mode
  worker writes `ctx+0x4B8` object +`0xF04`; CA 0xA2FA2 reads
  `P+0xF04`; `P+0x1A00` is the band publish word; `P+0xD6C/D70` are band
  fields). The probe (P vs bandobj) confirms it in one read.
* **The exact mode byte the stock userspace sends** for `set_country_for_dfs`
  is not recoverable from the artifacts (the payload is forwarded verbatim from
  the WAL caller). F1 pins it to 3 (required by CA 0xA2FA2); F1b enumerates if
  needed.
* The order of the 0x01F7 event vs the 0x0124 channel event on stock is [I]
  (send 0x01F7 first - the mode byte must exist before CA 0xA2FA2 runs).
* The 0x0136/0x07E8 workers are decoded only as table entries.

## 11. Reproduction

```
cd opensource/docs/incoming/wifi-fw-rf-descriptor
./tools/fw_rf_descriptor_re.py --selftest   # 62 checks, exit 0
./tools/fw_rf_descriptor_re.py --decode     # chain + writers + host emitters
./tools/fw_rf_descriptor_re.py --probes     # corrected probe pack
./tools/fw_rf_descriptor_re.py --frame      # the H2D 0x01F7 frame
```
Artifacts used in place (firmware sha256 checked, no vendor byte committed):
FIRMWARE.bin `7fc87e20...311b`; hi5622v100_wifi.ko (local; its ELF .text
offset/size is reported by `--decode`).

## 12. Sources

* pinned local FIRMWARE.bin and the vendor hi5622v100_wifi.ko read in place;
* committed lanes: ../wifi-fw-rf-programme/ (the programme/predicate/R chain and
  the probe pack this lane corrects), ../wifi-fw-desc-table/ (the 0xF7 install
  and worker-key method), ../wifi-fw-desc-pool/ (pool boot block, band objects),
  ../wifi-boot-gate/ (per-bank contexts CA 0xB1EE4, module+0x4B0/0x4B8 bind),
  ../wifi-datapath/tables/firmware-config-keys.md (table A key 0x01F7 cross-check).
