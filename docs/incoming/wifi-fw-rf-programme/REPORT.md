# REPORT - the firmware 5 GHz RF programme and the band-1 beacon path

**Lane:** wifi-fw-rf-programme (shared task **task-182**). **HOST-ONLY**: this
lane read the pinned local vendor firmware image in place; it never contacted
the router, never flashed, never fired. **The Lead fires.**

**Why this lane exists.** The CW-0/CW-2 fires took the 5 GHz AP to AP-ENABLED
with the full vendor order, the page golden, and **all three 5 GHz RF gates
read live as satisfied** (module[4]=0x02, module+0x440=0x01,
bandobj+0xEC8=0x01). Per the fw-chan-worker decision tree the remaining gap
is inside the firmware's band-1-only RF programme (CA 0x40684 -> CA 0x101890)
or the band-1 beacon engine. This lane decodes both.

**Artifacts (local, never committed; read in place):**

| artifact | identity |
| --- | --- |
| firmware image FIRMWARE.bin | 928,920 B, sha256 7fc87e20...c3ef311b, CPU (image-linear) = file + 0x40000, loaded to device CA 0x01240000 (BAR0+0x6f8000) |
| bench copy pd4-rebuilt.bin | 928,920 B (same decode space; not needed by this lane's claims) |
| vendor host module hi5622v100_wifi.ko | 3,581,748 B, 2.4.15 build |
| vendor hi5622v100_plat.ko + /lib/firmware/hi_wifi/cfg_*.ini | the ini parser and the per-band config (section 6.4) |

Disassembly: the OpenWrt lab objdump, Thumb-2
(-D -b binary -m arm -M force-thumb --adjust-vma=0x40000). Evidence grades:
**[V]** re-derived in this lane by the committed tool's selftest; **[V*]**
bench/pack-measured by a committed lane or the Lead; **[I]** inference; **[U]**
undecided without a device read.

---

## 0. TL;DR (the six findings)

1. **The prior name is misleading: CA 0x101890 is NOT the RF programmer.** It
   is a **band-1-only predicate/lookup**: it reads a per-channel descriptor
   index out of a static table (CA 0x10BE68), maps it through the channel table
   to a runtime descriptor record (CA 0x1ADC98, 16-byte records), and returns
   **1 iff one descriptor byte has bit 7 set**. It writes no register, no
   object, no log. **[V] and: in the shipped image the descriptor-slot bytes of
   BOTH channel tables are 0xFF for all 43 channels, so the predicate is
   statically 0 until/unless the firmware populates those tables at runtime**
   (the record pool CA 0x1ADC98 is outside the image, so the subsystem is
   runtime-built; the populating writer is [U]).
2. **The actual 5 GHz RF register-block programme is CA 0xA2FA2**, called by
   CA 0x40684 right after CA 0x101890. It builds a ~30-word **RF/PHY register
   image** in an object at [[perlink+0xE20+0x1A00]+0x2D8] (offsets
   +0x3A8..+0x418) - the band-1 RF/baseband/AGC/TX-chain parameters. [V]
3. **It has a channel-range split**: channel-table index >= 8 (ch 100..196)
   runs the inline image in CA 0xA2FA2 itself; index 0..7 (**ch 36..64**) calls
   a **different image builder CA 0x89924**. The two images differ in five
   words (+0x3AC/+0x3B4/+0x3B8/+0x3C0/+0x3CC) and in the branch flag
   perlink+0xE20+0xF87 (0 for ch36-64, 1 for ch100+). Both are fingerprints
   the host can read back. [V]
4. **CA 0x89840 latches CA 0x101890's predicate into bit 0 of the same RF
   block word +0x410** (read-modify-write, no log). That bit is the only
   firmware state that depends on the descriptor table; it is a first-class
   read-only probe. [V]
5. **The three helpers the worker lane listed (CA 0x97008, CA 0xFDCE4,
   CA 0xF4FD8) are band-agnostic bookkeeping** - none writes an RF register.
   The 5 GHz RF difference is the CA 0xA2FA2/CA 0x89924 block only. [V]
6. **The beacon engine**: the firmware starts a VAP per band from the
   band object (CA 0x46394 -> CA 0x971D8, which dispatches through a global
   callback table at G+0x370/+0x374), and CA 0x84DA4 (key 0x0127) stores the
   beacon interval into the VAP object at **+0x13A** (u16). The beacon
   parameter copy CA 0x7E5CC reads VAP+0x138/+0x13A into the BSS/beacon
   descriptor. The image also carries a 45-entry ISR **name blob** with
   separate smac_tbtt_5g_isr / smac_tbtt_2g_isr (CA 0x1229E8 / 0x1229FC):
   the TBTT/beacon engine is genuinely per band. [V] names, [U] mapping.
   Separately, the local **cfg_hi5622v100_hisi.ini sets
   beacon_2g_tx_policy=0 but beacon_5g_tx_policy=2**, and that key is parsed
   by **hi5622v100_plat.ko** - a per-band asymmetric setting the five-frame
   ladder cannot synthesise. [V] file/module, [I] effect.

**The decisive consequence:** the three gate bytes being 1 **now** does not
prove the RF programme ran **when the 0x0124 frame was posted**. Every gate
is a silent early-return (no log, worker still returns 0), and nothing
re-runs the programme until another 0x0124 arrives. The read-only probe pack
in TABLES T7/T8 separates "never ran" from "ran, wrong variant" from "ran,
TX-enable bit 0" from "ran fully, beacon engine silent" - with **ten BAR0
reads plus two pointer steps**, no rebuild.

---

## 1. The corrected call graph (what "the 5 GHz RF programme" is)

~~~
0x0124 worker CA 0x841B8 (stores ctx, see fw-chan-worker)
  ... stores ctx+0x10..+0x13 ...
  CA 0xFDCE4(ctx)                            band-class field, no RF      [V]
  CA 0xF4FD8(ctx+0x4AC)                      channel bookkeeping, no RF   [V]
  CA 0x40684(ctx, perlink+0xE20, perlink)    the 5 GHz-only branch        [V]
    if (ctx[4] != 2) return;                        AP create type
    if (CA 0x944DE(ctx) != 1) return;               the three gate bytes
    r6 = CA 0x101890(ctx+0x11, ctx+0x12, ctx+0x10)  PREDICATE -> 0/1
    CA 0xA2FA2(perlink+0xE20, ctx)                  RF REGISTER IMAGE
    CA 0x89840(perlink, r6)                         latch the predicate bit
    cleanup of three per-link flags (CA 0xC52B4)    no RF
  if (perlink+0x1C8) CA 0x97008(ctx)         general channel apply, no RF  [V]
~~~

CA 0xA2FA2 decides the image by ctx+0x13 (the channel-table index stored by
the worker, 0..28):

~~~
  if ([perlink+0xE20+0xF04] != 3) -> log, return      RF mode byte
  if (idx - 8 > 20) -> CA 0x89924(obj), [perlink+0xE20+0xF87] = 0   ch36..64
  else              -> inline image,    [perlink+0xE20+0xF87] = 1   ch100..196
~~~

Both branches write the same object r4 = [[perlink+0xE20+0x1A00]+0x2D8].
CA 0x101890 is evaluated **before** the image is written and its result is
latched **after**, so the two are independent observables.

## 2. CA 0x40684, line by line [V]

~~~
r3 = ctx[4]; if (r3 != 2) return;                 VAP create type
if (CA 0x944DE(ctx) != 1) return;                 gate: band==1, +0x440==1, bandobj+0xEC8==1
r0 = ctx+0x11 (band); r1 = ctx+0x12 (field); r2 = ctx+0x10 (channel)
r6 = CA 0x101890(band, field, channel);
CA 0xA2FA2(perlink+0xE20, ctx);
CA 0x89840(perlink, r6);                          [perlink+0x2D8]+0x410 bit0 = r6
then three conditional flag cleanups on [perlink+0xE20] (+0x10A8, +0xF40, +0x1028)
~~~

Returns nothing; every skip is silent. **This is where a "gates were 0 at post
time" failure becomes invisible forever** (candidate C1).

## 3. CA 0x101890 - the band-1 predicate, decoded exactly [V]

Signature: u32 f(u32 band, u32 field, u32 channel).

1. if (band != 1) return 0; - band 0 never enters this function.
2. CA 0x10172C(band, channel, &idx) - the channel validator the worker also
   uses (band-1 table CA 0x117AD4: 36 40 44 48 52 56 60 64 100 ... 196,
   29 entries). Miss -> 0.
3. field selects a **window** of the 4-byte-entry table at **CA 0x10BE68**
   (a 15-column x 29-row matrix laid out as a word array; each entry is
   {u8 a, u8 b, u8 c, u8 descriptor_slot} and the top byte is the slot).
4. The window is walked and each word's top byte t is looked up through
   **CA 0x101650(band=1, t)**: slot = (band ? CA 0x117AD4 : CA 0x117B48)[t].byte1;
   **0xFF means "no record" -> the mapper returns NULL and the entry is
   skipped**; otherwise record = **CA 0x1ADC98 + slot*16 + 8**.
5. Returns **1** as soon as byte[record+2] & 0x80; otherwise 0.

**Static fact that re-ranks the candidates:** in the shipped image every row of
both channel tables has byte1 = 0xFF (both tables are exactly
{channel, 0xFF, 0x00, 0x00}), so the mapper returns NULL for every channel and
CA 0x101890 returns 0 **without any runtime table population**. The record pool
CA 0x1ADC98 lies outside the image (image ends CA 0x122CD8), so the whole
descriptor subsystem is runtime-built; if it is populated at runtime the
predicate can become 1, and if not then bit 0 of R+0x410 is structurally 0 and
is **not** a TX gate (else no unit would ever transmit 5 GHz). The read-only
probe therefore reads the channel-table row first (probe 20) and treats the
predicate bit as meaningful only when the slot is not 0xFF. [V] static table,
[U] runtime population.

The field windows (base word offset relative to CA 0x10BE68, count):

| field | base | n | descriptor slots checked |
| ---: | ---: | ---: | --- |
| 0 (our AP) | +0x150 | 1 | idx |
| 1 | +0x150 | 2 | idx, idx+1 |
| 2 | +0x14C | 2 | idx-1, idx |
| 3 | +0x148 | 4 | idx-2 .. idx+1 |
| 4 | +0x148 | 4 | idx-2 .. idx+1 |
| 5 | +0x14C | 4 | idx-1 .. idx+2 |
| 6 | - | - | [U] (switch branch not decoded) |
| 7 | +0x150 | 8 | idx .. idx+7 |
| 8..14 | +0x140 / +0x148 / +0x14C / +0x138 / +0x13C / +0x144 / +0x134 | 8 | 8 slots around idx |

**Our bench case is field 0** (the 0x0124 body byte 1 is the vendor chip id,
0, for both bands), so the predicate is a **single descriptor lookup**:

~~~
word   = u32(CA 0x10BFB8 + 4*idx)          top byte t (= idx, verified 0..28)
row    = u32(CA 0x117AD4 + 4*t)            byte1 = slot; 0xFF = no record
rec    = CA 0x1ADC98 + slot*16 + 8         only if slot != 0xFF
result = (u32(rec) >> 23) & 1              bit 7 of rec+2
~~~

The returned bit is the only state the RF programme gets from the descriptor
table and it lands in [perlink+0x2D8]+0x410 bit 0 via CA 0x89840, a
three-instruction leaf that writes exactly that one bit and nothing else. The
mapper is also consumed by other functions (e.g. CA 0xA14xx builds a
29-channel table by calling CA 0x101650 per channel index), so the subsystem
is live in the design even though the shipped image's slots are 0xFF.

## 4. CA 0xA2FA2 / CA 0x89924 - the RF register image [V]

Object chain (all read-only from the host once perlink is known):

~~~
perlink = *(ctx+0x4B0)                    module context -> per-link
P       = perlink + 0xE20                 RF/band context passed to CA 0xA2FA2
O       = *(P + 0x1A00)                   chip/bank object
R       = *(O + 0x2D8)                    the RF/PHY register block
~~~

Pre-conditions: *(P+0xF04) u8 == 3 (RF mode; else CA 0xA2FA2 logs and
returns), and R != 0. CA 0xA2FA2/RF writes are plain str for whole words and
bfi for bitfields; the resulting image, **per channel range**:

| word | ch 36..64 (CA 0x89924) | ch 100..196 (inline) |
| ---: | ---: | ---: |
| +0x3A8 | 0x01800060 | 0x01800060 |
| +0x3AC | **0x03E6138A** | **0x056B0597** |
| +0x3B0 | bits4-7=3, bits8-11=3, bits0-3=4/5 * | bits4-7=mode(=3), bits8-11=mode, bits0-3=4/5 * |
| +0x3B4 | **0x026F138A** | **0x0F9B0FA5** |
| +0x3B8 | **0x00E800E8** | **0x009400E8** |
| +0x3BC | 0x00C601F6 | 0x00C601F6 |
| +0x3C0 | **0x00C601F6** | **0x014A014F** |
| +0x3C4 | 0x1FFF1FFF | 0x1FFF1FFF |
| +0x3C8 | 0x1C29614B | 0x1C29614B |
| +0x3CC | bits0-7=0xBE, bits24-31=**0x16** | bits0-7=0xBE, bits24-31=**0x1D** |
| +0x3D0 | 0x005200B2 | 0x005200B2 |
| +0x3D4 | 0x164021FF | 0x164021FF |
| +0x3D8 | 0xC14580B0 | 0xC08E141A ** |
| +0x3DC | 0x80000614 | 0x80000614 |
| +0x3E0 | 0x03FF1AA8 | 0x03FF1AA8 |
| +0x3E4 | 0x00000052 | 0x00000052 |
| +0x3EC | 0x003C0050 | 0x003C0050 |
| +0x3F0 | 0x1001F405 | 0x1001F405 |
| +0x3F4 | 0x05B57409 | 0x05B57409 |
| +0x3F8 | 0 | 0 |
| +0x3FC | 0x18200003 | 0x18200003 |
| +0x400 | 0x00507D0B | 0x00507D0B |
| +0x404 | 0x00F80064 | 0x00F80064 |
| +0x408 | 0x0300000F | 0x0300000F |
| +0x40C | 0x03790000 | 0x03790000 |
| +0x410 | **bit0 = CA 0x101890 predicate** | same |
| +0x414 | bits0-1 = 1 | bits0-1 = 1 |
| +0x418 | 0x003C0513 | 0x003C0513 |
| perlink+0xE20+0xF87 | **0** | **1** |

\* the low nibble is 4 when the chip-type query CA 0x40D60 returns 3 **and**
[O+0x12E]==0, else 5. CA 0x40D60 reads a peripheral register at
[0x40000280+0x28] and maps 2 -> 1, {5,0x34,0x85} -> 2, else 3.
\** if the chip-type query returns != 1 and [O+0x12E]==1, bits22-27 of +0x3D8
are set to 1 first (bfi), so the readback may be 0xC0CE141A.

**What can make it no-op or write the wrong band:**
- [P+0xF04] != 3 -> CA 0xA2FA2 logs and returns; R keeps its previous
  content (P+0xF04 is set by the band/PHY init, not by the 0x0124 frame).
- the CA 0x40684 gates are false **at post time** -> neither CA 0x101890 nor
  CA 0xA2FA2 runs at all; the worker still returns 0 and the ctx word stays
  correct (candidate C1).
- R or O NULL -> CA 0xA2FA2 logs (message id 2999) and returns; CA 0x89840's
  bit still lands, so a *programmed-looking* bit with an unprogrammed block
  is the signature of this failure.
- the channel-range split is by design, not a bug: ch 36..64 legitimately use
  the CA 0x89924 image, so a ch40 fire must be compared with the **left**
  column.

**Why a wrong-band write is unlikely [I]:** the chain is reached from the
*band-1* per-link (on the G6a readback ctx+0x4B0 == band-1 publish word), and
CA 0x101890/CA 0x944DE are band-1-gated; a wrong-band outcome would require
ctx+0x4B0 to point at the band-0 bank - the probe T7.2 checks that directly.

## 5. The three helpers - band-agnostic [V]

| helper | decoded behaviour | RF writes |
| --- | --- | --- |
| CA 0x97008 | general channel apply: link context (CA 0xA4CA4), CA 0x5FA6C/0x5FB18/0xB819C, then reconciles the modules sharing the band object (CA 0x961E0); requires ctx[4] in {1,2} | none |
| CA 0xFDCE4 | band-class code from (band, field): band0 field1-2 -> 1; band1 field1-6 -> 2, field7-14 -> 6, else 0; writes VAP object +0x1A0 **bits1-7** | none |
| CA 0xF4FD8 | module+0x4AC object: type 1/2 -> CA 0xF4F48 lookup then CA 0xD98FC/0xD98C8/0xD9894 channel/width bookkeeping; other types log | none |

None of them can explain a band asymmetry; they are listed only to close the
worker's post-store map.

## 6. The 5 GHz beacon engine

### 6.1 VAP start CA 0x46394 (H2D key 0x0065) [V]

~~~
body[2] -> ctx+0x11 (band) ; body[3] -> ctx+0x12 (field) ; u32 body[4] -> ctx+0x444
CA 0xD4048(ctx, body[1]) ; CA 0x94518(ctx)          mode/rate helpers
if (ctx[4]==2) { CA 0xBD114([ctx+0x190]) ; ctx+0x0E = 1      VAP ENABLED
                 CA 0xD41AC(ctx) }
if (ctx[4] in {1,2}) {
    VAP[+0x58] bit2 = (ctx+0x11 != 0)               the BAND-1 VAP flag
    if (ctx+0x0F == 10) CA 0xFDCE4(ctx)
    CA 0x5E95C(ctx) ; CA 0x971D8(band object ctx+0x4B8, ctx)
}
CA 0x971D8 = "start this VAP on the band object": it resolves the band/link
objects (CA 0xA4CA4), reconciles the modules bound to the band, then calls two
**global callbacks** G+0x370 / G+0x374 with the module pointer
(sites CA 0x97360/0x97366). Those callbacks are the MAC/beacon start hooks [I].
~~~

### 6.2 AP config CA 0x84DA4 (H2D key 0x0127) [V]

Writes into the **VAP object** [ctx+0x190]: +0x64 flags (bits 0,1,3,4),
+0x34C bit3, +0x360 bits2/3, **+0x13A = u16 body+0x94 (the beacon interval)**,
+0x138 bits 0/1/2, then CA 0x94728(ctx+0xACC) and CA 0xFDDF8(ctx, body+0x8B).
This is the per-VAP beacon configuration; it is **band-agnostic** (it never
reads ctx+0x11).

### 6.3 What consumes the beacon config [V] + [U]

* CA 0x7E5CC copies the VAP beacon fields into a BSS/beacon descriptor:
  +0x138 -> desc+0x25E, **+0x13A -> desc+0x260**, +0xF8 -> desc+0x262, plus
  EDCA/HT fields from another object. The beacon engine's interval source is
  VAP+0x13A. [V]
* The image carries a 45-entry **ISR name blob** at CA 0x12266C with separate
  **smac_tbtt_5g_isr (CA 0x1229E8)** and **smac_tbtt_2g_isr (CA 0x1229FC)**,
  plus the per-band pairs for rx/tx complete, backoff, pre-tx, user-ps,
  lifetime, and smac_msg_proc_beacon_tx_resume_req / _suspend_req
  (CA 0x122904/0x122928). The TBTT/beacon transmit engine is therefore a
  **separate per-band engine**; the handler addresses are [U] (the names have
  no code xref in the image - they are a name-only debug blob).
* The ISR names prove the shape of the engine: a band-1 beacon needs the
  band-1 TBTT interrupt armed and the band-1 beacon queue primed. Nothing in
  the five H2D frames we post is band-1-specific beyond the 0x0124 body, so
  the arm must happen inside firmware from band-object / RF-block state.

### 6.4 The per-band beacon TX policy lives in the vendor configuration [V]

The local /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini [HOST_WIFI_NORMAL]
section contains:

~~~
beacon_2g_tx_policy=0
beacon_5g_tx_policy=2
~~~

and the key beacon_5g_tx_policy is present in **hi5622v100_plat.ko** (not in
the wifi module, and not in our frames): the plat module parses the ini and
the value reaches the firmware through the plat/wifi config path. This is the
only **per-band asymmetric** beacon setting found anywhere in the host-side
material, and it is not something the five-frame ladder can synthesise. [V]
file+module, [I] effect on band-1 beaconing.

## 7. Ranked candidate causes

Facts (a) all gate bytes are 1 when read, (b) the 5 GHz AP reaches AP-ENABLED
and the page stays golden, (c) the 2.4 GHz path beacons - are satisfied by all
of the following. Ordered by probability x cheapness of the discriminating
read.

**C1 - the RF programme was silently skipped at the time the 0x0124 frame
ran.** Every gate is a silent early-return and nothing re-runs the programme
unless another 0x0124 arrives; the bench reads the gate bytes only *after*
the whole AP order (the writers are the band/config-apply path CA 0x455D0 /
CA 0x7100C and the band-object path CA 0xB6C44, which our ladder reaches later
in the order). Signature: **R is zero/unprogrammed** (or stale) while the
three gate bytes read 1. Discriminator: T7 probes 5-8. Fix: re-post 0x0124
after the full order (rung R1/R2).

**C2 - the RF programme ran, but the descriptor predicate bit read 0 (only
meaningful if the runtime channel tables were populated).** Then [R+0x410]
bit0 = 0 even though the image is written. The shipped image makes the
predicate structurally 0 (channel-table slot bytes all 0xFF), so this is a
*conditional* candidate: if probe 20 reads slot != 0xFF at runtime, the
descriptor subsystem is live and bit0 matters; if slot == 0xFF, bit0 = 0 is by
design and MUST NOT be treated as the blocker. Discriminator: T7 probes 18-21.

**C3 - the RF programme ran the wrong channel-range variant / a stale
variant.** ch 36..64 must show the CA 0x89924 image (left column) and
perlink+0xE20+0xF87 == 0; ch 100+ the other column and 1. A ch40 fire that
reads the ch100+ image, or an all-zero block, means the last 0x0124 did not
reprogram the block (stale) or the mode byte [P+0xF04] was not 3.
Discriminator: T7 probes 5, 8, 10.

**C4 - the band-1 beacon engine is armed but the 5 GHz beacon TX policy is
not applied.** beacon_5g_tx_policy=2 is a per-band policy parsed by
hi5622v100_plat.ko; if the plat/ini path is absent or the ini differs from the
box's shipped one, band 1 gets a different beacon policy than the band that
works. Discriminator: verify the plat module's ini load (dmesg /
/sys/module/hi5622v100_plat), then re-fire with the shipped ini guaranteed in
place; firmware-side policy storage is [U].

**C5 - the band-1 TBTT/beacon queue is never armed.** If the image and the
predicate bit are both correct (C1..C3 excluded) and no beacon reaches the
air, the gap is the per-band TBTT engine: VAP start reaches the beacon start
only through the global callbacks G+0x370/+0x374, and the band-1 TBTT ISR is a
distinct handler. Discriminator: the **differential band-object dump** (T8) -
read both band objects and diff the same offsets; the working band is the
reference for the silent one. Plus VAP+0x13A (beacon interval; expected 100 TU
for the vendor default) and VAP+0x58 bit2.

## 8. Read-only probe pack and decision tree

All at bar0read <phys>, phys = 0x40000000 + CPU + 0x6B8000 (BAR0 base
0x40000000 per the committed bringup decode; **never** read the RC misc window
0x10160000..0x1016ffff - the helper refuses it). CPU values are the
image-linear convention of the committed lanes; if the boot's ctx0 differs,
resolve the two pointer steps first (T7.1). Full addresses, expected values
and the decision tree: **TABLES T7-T9** and **FIRE-RECIPE.md**.

Minimum decisive set (11 reads, 2 pointer steps): ctx0 -> perlink P ->
P+0x2820 -> O -> O+0x2D8 -> R; then R+0x3AC, R+0x3B4, R+0x3CC, R+0x410,
P+0xF87, the channel-table row for the current channel (its byte1 = slot) and,
if the slot is not 0xFF, the pool record word.

| readback | verdict |
| --- | --- |
| R == 0 or O == 0 | band-1 RF object chain absent -> C1 (programme never ran) |
| R+0x3AC == 0 | the image was never written -> **C1** |
| R+0x3AC == 0x03E6138A and R+0x3CC top byte == 0x16 | the ch36-64 image was written -> C2/C4/C5 |
| R+0x3AC == 0x056B0597 while the AP is on ch36-64 | stale/wrong variant -> **C3** |
| ch36-64 image, probe 20 slot == 0xFF | the descriptor subsystem is inactive in this config: bit0=0 is by design, ignore it -> **C4/C5** |
| probe 20 slot != 0xFF, R+0x410 bit0 == 0 | a live descriptor table but the predicate failed -> **C2** |
| ch36-64 image, predicate consistent | the RF programme completed -> gap is the beacon engine (**C4/C5**) |
| full image + bit0=1 + no beacon | go to T8 differential band-object dump; check VAP+0x13A != 0 |

## 9. Fire recipe (summary; full rung in FIRE-RECIPE.md)

* **R0 (read-only, no fire)**: the T7 probe pack on the running CW-0 AP. Ends
  the "did the programme run" question with no risk.
* **R1 (zero rebuild)**: re-fire the same ch40 rung **without reloading the
  vendor module** (or, if the ladder must reload it, re-post the 0x0124 frame
  once more after the five-frame order). If beacons appear, C1 is confirmed
  and the host fix is "post 0x0124 last / repeat it".
* **R2 (small host patch)**: after the five-frame order, re-send the 0x0124
  channel frame (same body); optionally gate the AP start on the T7 readback
  (R+0x3AC != 0). Fire ch40 first, then ch100 for the other variant.
* **R3 (only if R1 shows R == 0 and no gate explanation)**: check the plat
  module ini (beacon_5g_tx_policy) and the mode byte [P+0xF04]; if the mode
  byte is not 3 the band-1 PHY init never ran (a different blocker - report
  back, do not patch blind).
* **Abort rules**: page 0x86807fef / state 1 / mask 0 exception -> power-cycle,
  no retry. Never write 0x400392f0, never read 0x10160000..0x1016ffff.

## 10. Limits / honest unknowns

* The **contents** of the runtime descriptor pool CA 0x1ADC98 are [U] until
  T7 probe 9 is fired; the pool initialiser was not located (its address
  literals appear in five literal pools: CA 0x04358C, 0x045B40, 0x048694,
  0x0F2934, 0x0F4218 - a targeted follow-up can decode the init from there).
* The semantic of +0x410 bit 0 is [I] ("TX chain enable"); the decode proves it
  is a latched predicate bit and that no log/error is produced when it is 0.
* The exact **beacon/TBTT handler addresses** are [U]: the 45 ISR names have no
  code xref in the image, so the per-band engine split is [V] but its arming
  condition is not decoded.
* CA 0x101890 field 6's window is [U] (switch branch not decoded); fields 0-5
  and 7-14 are as tabled.
* The gate bytes' writers (CA 0x455D0 / CA 0x7100C / CA 0xB6C44) are located
  but their **triggering H2D keys are not decoded**; the "post-time gate was
  0" hypothesis (C1) is therefore [I] until the R1 fire.
* This lane fired nothing and contacted no device; no vendor byte was copied
  into the repository.

## 11. Evidence and reproduction

| evidence | what |
| --- | --- |
| TABLES.md | every address, table, image and probe value |
| FIRE-RECIPE.md | the rungs, gates, abort rules |
| tools/fw_rf_programme_re.py | re-derives the decode from the pinned local image + objdump; --selftest asserts the windows this report relies on; --tables/--readback print the tables and the probe pack; no vendor byte is stored in the repo |

## 12. Sources

* pinned local FIRMWARE.bin (identity above) read in place;
* committed lanes: ../wifi-fw-chan-worker/ (worker, CA list, the three gates),
  ../wifi-5ghz-beacon/ (five-frame order, BAR0 conventions),
  ../wifi-boot-gate/ (band objects, publish words), ../wifi-firmware-re/
  (load map, image layout, ISR-name blob), ../wifi-vendor-userspace/
  (ini ownership, plat module), ../wifi-beacon-static/ (worker body map);
* local vendor config /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini and the
  vendor hi5622v100_plat.ko (read in place, not committed).
