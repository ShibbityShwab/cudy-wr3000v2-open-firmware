# REPORT - the 5 GHz beacon TX path: what the firmware needs between "AP enabled" and a beacon on air

**Lane:** wifi-fw-beacon-tx (shared task **task-192**), 2026-11-27.
**HOST-ONLY**: static decode of the pinned local artifacts; no device contact,
no write, no fire. **The Lead fires.** Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_beacon_tx_re.py](tools/fw_beacon_tx_re.py), evidence/.

Grades: **[V]** re-derived here by the committed tool from the pinned artifacts;
**[V\*]** bench-measured by a committed lane or the Lead; **[I]** inference;
**[U]** needs a device read. Firmware CPU address convention: CA = file offset
+ 0x40000 of the pinned FIRMWARE.bin (928,920 B, sha256 7fc87e20...311b). Host
read paths: firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO
phys = 0x40000000 + 0x3B8000 + (CA - 0x40000000).

## 0. TL;DR

The 2.4 GHz and 5 GHz AP beacons are produced by the **same** firmware path,
but that path is **per module context and per band**, and the band is chosen by
the **VAP-add message (key 0x0064) body[0x12]**, not by the AP config or the
VAP start. The beacon transmitter itself is the per-link state created by the
**VAP start (key 0x0065)** and armed through the band-start CA 0x971D8, which
calls the runtime hooks **G+0x370 (MAC start)** and **G+0x374 (beacon start)**.
The per-band MAC blocks (band0 0x40110000 / band1 0x40114000) are separate and
have band-specific init tables. The committable facts are in TABLES; the exact
TBTT ISR entry and the TBTT timer register are **[U]** (probes in FIRE-RECIPE).

Ranked causes for "5 GHz RF programmed, AP enabled, no beacon":

| # | cause | test | grade |
| --- | --- | --- | --- |
| C1 | the 5 GHz VAP's 0x0065 VAP start never ran on a ctx bound to band 1 (or ran with the wrong field B, like the F1 0x01F7 fire), so band1's per-link has **no beacon state** (+0x6C6 = 0) | read each ctx's +0x4B8 and per-link +0x6B8/+0x6C0/+0x6C6 | [U] |
| C2 | band1's MAC block was never started/armed (0x4AF0C path for perlink+0x11D = 1) or the per-band init table was not applied (0x548/0x54C != 0x6101) | read device CA 0x40114500/0x51C/0x520/0x548/0x54C/0x5BC and 0x40101220 | [U] |
| C3 | the G hooks are zero/stale, so the VAP start's band-start call did nothing on band1 | read G, G+0x370, G+0x374 | [U] |
| C4 | the AP config's only band-ish hints (HT/VHT IE bits) are set for the wrong band; they land in VAP+0x34C.b3 / VAP+0x360.b2/b3 | read VAP+0x34C / VAP+0x360 | [U] |
| C5 | the per-band TBTT timer is not armed even though the beacon state exists; C5 [U] of the desc-pool lane remains open | sample the per-band block registers one beacon interval apart | [U] |

The cheapest discriminator is **C1** (one read of band1 per-link +0x6C6). If it
is 0, the fix is a single ordered re-post (VAP add band=1 -> AP config -> VAP
start) with the same field B, not a firmware patch. If it is 1, C2/C5 become
the target and the probe pack says which.

## 1. Method and inputs

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`; CPU = file + 0x40000 |
| disassembler | the OpenWrt lab ARM objdump (override `--objdump` / `$BW_OBJDUMP`) |
| dispatch | g0-id1 handler CA 0x43D58: key = u16 hdr+0x14, len = u8 hdr+0x16, body = hdr+0x18, ctx = CA 0xFE578(0, hdr[3]), worker from table A CA 0x10C23C (217 rows) else table B CA 0x103BF8 (88 rows) |
| committed anchors | ../wifi-fw-rf-descriptor (P/O/R chain, 0x01F7), ../wifi-fw-dfs-worker (field-B context model), ../wifi-fw-desc-pool (C5 band-1 hooks, pool), ../wifi-vap-start (key 0x0064 body layout + band binding), ../wifi-ap-start (AP config body T3), ../wifi-beacon-static and ../wifi-5ghz-beacon (host ladder) |

All [V] claims below are asserted by `tools/fw_beacon_tx_re.py --selftest`
(105 checks, exit 0) from the raw instructions/tables; the tool prints only
derived values.

## 2. The beacon TX chain, end to end

```
host g0-id1 messages (hdr[3] = field B = module ctx index):
  0x0064 VAP add  -> ctx[body[4]] bound to band body[0x12]   [V]
  0x0124 channel  -> ctx+0x4B0 per-link; CA 0x40684 -> RF image when
                     u8(P+0xF04)==3                            [V]
  0x01F7 RF mode  -> worker CA 0xB5B9C sets u8(P+0xF04) and the R image  [V]
  0x0127 AP config-> VAP fields only (no band, no enable)      [V]
  0x0065 VAP start-> per-link beacon state + band start CA 0x971D8  [V]
firmware:
  per-link +0x6B8 beacon buffer (280 B), +0x6BC aux object,
  +0x6C0 descriptor (92 B), +0x6C4 = 16, +0x6C6 = 1           [V]
  band start CA 0x971D8 -> G+0x370 MAC start, G+0x374 beacon start  [V/U]
  per-band MAC block: band0 0x40110000 / band1 0x40114000     [V]
  beacon descriptor refresh CA 0x7E5CC: VAP+0x138 -> desc+0x25E,
  VAP+0x13A -> desc+0x260                                     [V]
  per-band TBTT interrupts (smac_tbtt_5g_isr / smac_tbtt_2g_isr)  [names V, entry U]
```

## 3. Field B and the band binding (the per-band selector our ladder must keep consistent)

The g0-id1 dispatcher resolves `ctx = CA 0xFE578(0, hdr[3])` before it looks
up the key. CA 0xFE578 bounds `hdr[3] <= 0x12` (19 contexts) and returns
`u32(0x171458 + 4*fieldB) + u16(0x172504)`; boot init fills the 19 slots with
`0x119758 + k*0xB40` (ctx0 = 0x119758 = the F2 bench context) [V, also
committed in ../wifi-fw-dfs-worker section 3].

The g0-id1 **0x0064 VAP-add worker CA 0x449E4** then does, on the created
module context from **body[4]**:

```
44B6C  ldrb r1,[body+0x12]      ; band selector
44B6E  ldrb r0,[ctx+2]
44B70  bl CA 0xA4FC8            ; band object = 0x15CA08 + 0x1B68*band
44B76  add.w r1,r0,#0x1A00 ; ldr r3,[r1]  ; published per-link must be nonzero
44BBC  str.w r3,[ctx+0x4B0]     ; per-link
44BC2  str.w r0,[ctx+0x4B8]     ; band object
44BC6  str.w r3,[ctx+0x4B4]     ; bank context
```

band0 = 0x15CA08, band1 = 0x15E570 (== the F2 bench P at CA 0x15E570). The
F2 bench readback `u32(ctx0+0x4B8) = 0x15E570` therefore means **ctx0 was
bound to band 1 (5 GHz)** - which is why the field-B fix (hdr[3] 4 -> 0) made
the 0x01F7 worker write the band-1 mode/RF image [V*, ../wifi-fw-dfs-worker].

Consequence: a VAP's whole lifecycle (VAP add, AP config, VAP start, channel,
RF mode) must use **the same field B** and the band binding is whatever the
0x0064 body[0x12] said. If the 5 GHz VAP's 0x0065 was posted with a different
field B than its 0x0064, it ran on a context that is not bound to band 1 - and
then it configured (or created beacon state on) the wrong band. This is C1 and
the first probe.

## 4. AP config (key 0x0127): no band field, no beacon enable

Worker CA 0x84DA4 [V]. Body map (host side: ../wifi-ap-start TABLES T3):

| body off | width | firmware destination | note |
| ---: | ---: | --- | --- |
| +0x00 | u32 | VAP+0x10 | beacon interval |
| +0x04 | u32 | VAP+0x14 | DTIM |
| +0x08 | u8 | branches the RSN/vendor paths | |
| +0x09..+0x48 | 64 B | RSN IE copy (id 0x30 checked) | |
| +0x49..+0x88 | 64 B | vendor IE copy (id 0xDD checked) | |
| +0x89 | u8 | ctx+0x18D.b0 | hidden_ssid |
| +0x8A | u8 | VAP+0x34C.b3 | HT cap bit5 |
| +0x8B | u8 | CA 0xFDDF8 | HT cap bit6 |
| +0x8C | u8 | VAP+0x360.b2 | VHT cap bit5 (5 GHz hint) |
| +0x8D | u8 | VAP+0x360.b3 | VHT cap bit6 |
| +0x8E | u8 | CA 0xD4048 | wireless mode |
| +0x93 | u8 | no reader | start flag (1 start_ap / 0 change_beacon) |
| +0x94 | u16 | VAP+0x13A | MDID (IE 0x36) |
| +0x96..+0x98 | u8 | VAP+0x138 bits 0..2 | MD present / capability bits |

There is **no band selector and no beacon-enable bit** in 0x0127 [V]. The only
band-correlated fields are the HT/VHT capability hints (+0x8A..+0x8D) which
the worker turns into VAP flag bits; a 5 GHz AP config built with 2.4 GHz-style
HT/VHT caps would silently stage wrong flags (C4).

## 5. VAP start (key 0x0065): the per-link beacon state is created here

Worker CA 0x46394 [V]. After resolving the VAP/band context:

```
46 3D4  bandobj = *(ctx+0x4B8)
46 450  perlink = *(ctx+0x4B0)
(VAP mode == 2 = AP path)
46 4F2  if (u8(perlink+0x6C6) != 1)   ; first start of this VAP on this band
          alloc 0x118 (280 B) -> perlink+0x6B8   (beacon buffer)
          alloc 0x5C  (92 B)  -> perlink+0x6C0   (beacon descriptor)
          perlink+0x6C4 = 16 ; perlink+0x6C6 = 1
          build a 0x310-byte frame from ctx+0x4AC and publish
          perlink+0x6BC = result of CA 0xEE394
46 4A6  CA 0x5E95C(ctx)
46 4B0  CA 0x971D8(bandobj, ctx)      ; band start
```

So **perlink+0x6C6 = 1 is the firmware's per-VAP "beacon state exists" flag**
and perlink+0x6B8 / +0x6BC / +0x6C0 are its buffers/objects. The existence,
sizes and creation/free sites are [V]; the role names ([I]) follow from the
AP-only VAP-start path, the per-VAP lifetime (freed only by the teardown
CA 0x87024) and the 0x310-byte object built from ctx+0x4AC in the same block. A second VAP start
on the same band skips the allocation (the guard), and the stop path
(CA 0x87024) frees the three pointers and clears the state. The AP config
(0x0127) only stages VAP fields - the beacon objects appear at VAP start.

## 6. Band start CA 0x971D8 and the G hooks

CA 0x971D8 (called from the VAP start and from the stop path) [V]:

```
9735C  if (*G != 0):
97360    r3 = *(G+0x370)            ; MAC start hook
97366    call hook(ctx)
9736A    r3 = *(G+0x374)            ; beacon start hook
97370    call hook(ctx)
```

G = CA 0x1747C4, a runtime callback table (RAM, filled by boot code; not in
the image). The **hook values are [U]** and are the cheapest way to prove that
the MAC/beacon engines were armed: both must be nonzero and both are called
with the module ctx (which carries the band binding). If G+0x374 is zero, no
beacon TX engine is started on any band; if it is nonzero and the band-1
beacon is silent, the arming happens but the band-1 MAC/TBTT is not ready
(C2/C5).

## 7. The beacon TX descriptor refresh CA 0x7E5CC

CA 0x7E5CC (called from 0x72184) [V] fills the per-link TX descriptor used for
beacons: it reads the VAP object (r5) and writes

```
7E634  VAP+0x138 (u16) -> desc+0x25E     ; MD present/caps
7E63C  VAP+0x13A (u16) -> desc+0x260     ; MDID
```

plus the rate/power fields at desc+0x254..+0x284 (from the link/rate struct).
This is the last [V] firmware step between the VAP state and the beacon
descriptor; after it the descriptor is handled by the per-band TX engine
(per-band MAC block) and the TBTT timer, both [U].

## 8. The per-band MAC/TBTT hardware

The MAC has two register blocks, **band0 = device CA 0x40110000** and
**band1 = device CA 0x40114000** (delta 0x4000) [V]. Each is initialised from
a 23-entry `{u16 offset, u16 value}` table:

| table | CA | entries |
| --- | ---: | --- |
| band0 MAC init | 0x105714 | 23 pairs |
| band1 MAC init | 0x105770 | 23 pairs |

applied by CA 0x9F2C6/0xA04C2 with base `0x40000000/0x40004000 + 0x110000`
(i.e. 0x40110000 / 0x40114000) [V]. Band1 differs in exactly 10 pairs; the key
ones: 0x548 = 0x6000/0x6001/**0x6101** vs band0 0x0000/0x0001/**0x0101**,
0x54C the same pair, 0x588/0x594 = 0x35/0x24 vs 0x33/0x22. Bit **0x6000** is
the band-1 marker the firmware itself writes, and 0x401145BC gets the band
index (0 in the band1 path, band value in the band0 path).

CA 0x4AF0C resets/starts the band selected by **u8(perlink+0x11D)** (0 -> the
0x401105xx block, 1 -> the 0x401145xx block): write 0x55 then 0xFF to base+0x500,
read base+0x51C/0x520, set **0x4010C220** bits 1|8|0x20|0x1000 (SMAC interrupt
enable/mask), write **0x40101220** = 1 (band 1) or 2 (band 0), and clear
per-link/RX bits [V]. The **enable-TRX worker CA 0xE8520** additionally uses
band-1-only registers **0x4010E150/0x4010E154** (band0: 0x4010820C/0x40108214)
through 0x4010C200/0x4010C220 and the SoC INTC 0x40002264/68/6C [V].

These are the registers that must be read after the re-post: they tell whether
the band-1 MAC engine was started at all (C2) and, sampled twice, which
counter is the TBTT timer (C5). The exact bit semantics of 0x548/0x54C/0x550/
0x554/0x55C/0x560/0x564/0x568 remain [U] - TABLES T6 lists the values the
firmware writes, FIRE-RECIPE R0/R3 lists the reads.

## 9. TBTT ISRs: names only, arming conditions [U]

The image carries the **names** `smac_tbtt_5g_isr` (CA 0x1229E8) and
`smac_tbtt_2g_isr` (CA 0x1229FC) in the ISR-name blob CA 0x12266C..0x122BA0
(45 names: `*_5g_isr`/`*_2g_isr` pairs including rx/tx complete, common
timer, tbtt, rx_timeout, pre_tx, backoff, ...) [V]. The blob has **no code
pointer and no code reference**: the LE32 pointer scan over the whole image
finds no pointer into the blob [V]. The TBTT handler registration therefore
happens through a runtime structure (like G), and the entry addresses stay
**[U]** offline. The per-band **common timer** ISR name next to tbtt confirms
the TBTT bucket runs off the per-band "common timer" interrupt - which is the
per-band MAC block above, not a global timer [I].

The band-1 TBTT arm conditions from the desc-pool lane's C5 are unchanged: the
VAP start's band-start call (**G+0x374**) plus the per-band MAC start
(0x4AF0C / 0xE8520) are the only firmware-visible arming steps; no H2D key or
WAL command in tables A/B is a "TBTT arm" (TABLES T8/T9) [V negative].

## 10. What makes 2.4 GHz work and 5 GHz silent (ranked)

1. **C1 - beacon state missing on band1.** The 2.4 GHz AP obviously ran
   0x0064 body[0x12]=0 -> ctx bound to band0 -> 0x0127 + 0x0065 -> per-link
   band0 +0x6C6 = 1 and G hooks called. For 5 GHz, if the 5 GHz VAP start was
   posted with a different field B (the F1 fire's mistake class) or before the
   band binding, band1's per-link keeps +0x6C6 = 0 and no TX object exists.
   **Probe: band1 published per-link +0x6C6 / +0x6B8 / +0x6C0.**
2. **C2 - band1 MAC not started.** Even with the beacon state, the band-1 MAC
   block must be started (0x4AF0C for perlink+0x11D = 1, table 0x105770
   applied, 0x40101220 = 1). The F2 fire only proves the RF image write; it
   did not drive the AP/VAP-start path on band1. **Probe: 0x401145xx block,
   0x4010C220/0x40101220.**
3. **C3 - hooks not reached/zero.** G+0x370/+0x374 are RAM values; if the
   band-start CA 0x971D8 ran before boot completed them (or on a ctx that was
   not band-bound), nothing armed. **Probe: G, G+0x370, G+0x374.**
4. **C4 - AP config band hints wrong** (VAP+0x34C/+0x360 flags from a 2.4 GHz
   style HT/VHT IE set). Cosmetic to the engine start but a real per-band
   state difference our driver controls.
5. **C5 - TBTT timer/bucket not armed** after everything else is present; the
   timer register identification is the R3 read-only double sample.

The discriminator order is C1 -> C3 -> C2 -> C5/C4: C1 is one read, and it
splits "our ladder never created the TX state" (re-post fix, no patch) from
"the firmware state exists but the engine is not armed" (hardware/arming).

## 11. Negative results and limits

* No "beacon enable" H2D message exists on the g0-id1 path in table A/B:
  0x0067 is the VAP stop/teardown worker (it calls 0x5E95C + 0x971D8 and
  releases the VAP) [V]; 0x0066 is another VAP command [U]. The beacon
  transmit is not switched by a separate message our ladder "misses" on this
  path; it is switched by the VAP start (band bound) and the band-start hooks.
* The 19 module contexts are static RAM bodies; a fresh context is all zeros
  and only becomes meaningful after the 0x0064 create binds it. Sending a
  per-band message with a field B that was never created/bound silently acts
  on a zeroed context (the F1 failure mode) rather than failing loudly.
* The TBTT ISR entry and the TBTT timer register address are [U]; this lane
  deliberately does not guess a register number. FIRE-RECIPE R0/R3 contains
  the exact reads that identify them without writing anything.
* The rf-programme lane's R image (R = device 0x400B2000, R+0x3A8, R+0x3AC)
  is a **PHY/RF** register image; it is necessary but not sufficient for a
  beacon. Nothing in this lane contradicts it.

## 12. Reproduction

```
./tools/fw_beacon_tx_re.py --selftest   # 105/105 PASS, exit 0
./tools/fw_beacon_tx_re.py --decode
./tools/fw_beacon_tx_re.py --tables
./tools/fw_beacon_tx_re.py --probes
./tools/fw_beacon_tx_re.py --frames
```

Evidence snapshots: evidence/selftest.txt, decode.txt, tables.txt, probes.txt,
frames.txt (regenerated by the four commands above; the pinned artifacts are
local and uncommitted, `--fw`/`--objdump` override the paths).