# REPORT - the descriptor-pool populator (C2), the ini beacon policy (C4), the band-1 start/TBTT hooks (C5)

**Lane:** wifi-fw-desc-pool (shared task **task-184**). **HOST-ONLY**: this lane
read the pinned local vendor firmware image and vendor modules in place; it
never contacted the router, never flashed, never fired. **The Lead fires.**

**Why this lane exists.** The CW-0/CW-2 fires proved the 5 GHz AP host side is
correct and stable (channel-worker ctx store word 0x01000128, VAP object
nonzero, the three RF gates 1/1/1, page golden) but the firmware's 5 GHz RF
programme state stays zero and no 5 GHz beacon appears. The fw-rf-programme
lane left the writer of the runtime descriptor pool **CA 0x1ADC98** (and its
valid bit 0x00800000) not located, and left the per-band beacon policy (C4)
and the band-1 TBTT arm (C5) undecoded. This lane closes C2, decodes the C4
host path far enough to decide reachability, and narrows C5 to a probe pack.

**Artifacts (local, never committed; read in place):**

| artifact | identity |
| --- | --- |
| firmware image FIRMWARE.bin | 928,920 B, sha256 7fc87e20...c3ef311b, CPU = file + 0x40000 |
| vendor host modules | hi5622v100_plat.ko 371,708 B, hi5622v100_wifi.ko 3,581,748 B |
| local ini | recovery-extract/lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini |

Disassembly: the OpenWrt lab objdump, Thumb-2
(`-D -b binary -m arm -M force-thumb --adjust-vma=0x40000`) for the image,
`objdump -dr` for the ELF .ko files. Grades: **[V]** re-derived by the
committed tool's selftest (97/97 PASS); **[V*]** measured by a committed lane or
the Lead; **[I]** inference; **[U]** needs a device read.

---

## 0. TL;DR (six findings)

1. **The populator is located: CA 0x1017F8.** It rebuilds **both** channel maps
   - band-0 **CA 0x117B48** (14 rows) and band-1 **CA 0x117AD4** (29 rows) -
   from the runtime descriptor pool **CA 0x1ADC98**. It first sets every row's
   slot byte (+1) to **0xFF**, then for each descriptor record i writes
   `map[row].slot = i` for every channel bit set in that record. **[V]**
2. **Pool layout (from the boot builder itself):** +0x04 = u8 count; record i
   at pool+16*i with +0x08 u32 record word (**byte0 = band type 0=2.4G /
   1=5G**, **byte2 bit7 = the 0x00800000 valid bit**), +0x0C/+0x0E, and
   +0x14 u32 channel bitmap (2.4G uses bits 0..13, 5G bits 0..28). **[V]**
3. **Three writers of the pool exist:** (a) the **boot init block** at
   CA 0x485FA..0x4861E (builds the default 2-record pool, then calls the
   populator at CA 0x48620; it sits inside a larger pre-HCC init function body
   with no direct bl caller, so the entry is [I]); (b) the **H2D key 0x00F7
   worker CA 0x45A30**
   (installs a **host-supplied descriptor blob** into the pool via a
   memcpy_s-like helper, then calls the populator at CA 0x45A76); (c) the
   **H2D key 0x00DA worker CA 0x43460** (edits a record's +0x0C/+0x0E channel
   fields from the body; it does not clear/set the valid bit). **[V]**
4. **The boot-default band-1 descriptor word is 0x00000201** (byte0=1 band 1,
   byte2=0x02) so **CA 0x101890 returns 0 and R+0x410 bit0 = 0 by design** in
   the default configuration. The 0x00800000 valid bit is **host-supplied
   data in the 0xF7 descriptor table** - no firmware code sets it on its own.
   **[V]** decode, **[I]** consequence. Our five-frame ladder does not contain
   0x00F7/0x00DA, so we run on the boot-default map. **[I]**
5. **C4 is a host-side platform path:** hi5622v100_plat.ko parses
   `beacon_2g_tx_policy / beacon_5g_tx_policy` through a generic id/key table
   (ids 0xAE/0xAF), stores them in `g_al_dts_params[174/175]`, and copies them
   into the exported `g_dev_custom_info` blob at **+0x91C (2g)** and
   **+0x7AE (5g)**; the blob is imported by hi5622v100_wifi.ko and sent to the
   device by plat's `dev_custom_send` (bal_write). **Our five H2D frames carry
   no ini-derived field and cannot synthesise it.** **[V]** path, **[I]** effect.
6. **C5: the band-1 start hooks are two global callbacks.** The VAP
   start-on-band worker CA 0x971D8 calls **G+0x370 (CA 0x174B34)** and
   **G+0x374 (CA 0x174B38)** with the VAP context, guarded by G+0 != 0, after
   the band/link reconcile. These are the first place a silent band-1 beacon
   failure can hide **and they are directly readable**. The 45-name ISR name
   blob (CA 0x12266C, incl. smac_tbtt_5g_isr) has **no code reference at all**
   in the image - it is a name-only debug blob, so the handler mapping stays
   [U] and must be replaced by the differential probe. **[V]**/ **[U]**

**Consequence for the bench state:** the CW-2 read (pool record = 0x001B5B0A,
valid bit 0) is **not** by itself evidence of a firmware bug: it is the value
of the record word the descriptor predicate reads, and a clear bit is the
expected default. The decisive question is **which table is loaded at runtime**
(boot default slot=i, or a host 0xF7 table with slot 0x23), and the read-only
probe pack answers that in one pass (T5/T8).

---

## 1. Conventions

* Firmware CPU address convention (committed lanes): **CPU = file offset +
  0x40000**; **BAR0 offset = CPU + 0x6B8000**; **bar0read phys = 0x40000000 +
  BAR0 offset = CPU + 0x46B8000**.
* **Correction to lane wifi-fw-rf-programme T7 row 20:** that table printed
  BAR0 = 0x872AD4 for CPU 0x117AD4; the committed convention gives
  BAR0 = **0x7CFAD4**, phys = **0x407CFAD4**. All other T7 rows are consistent
  (e.g. CPU 0x171458 -> 0x40829458, CPU 0x15CA08 -> 0x40814A08), which is why
  the row-20 value is a typo, not a different convention.
* Forbidden: never read the RC misc window 0x10160000..0x1016ffff; never write
  CA 0x400392f0. This lane wrote nothing anywhere.

## 2. C2 - the descriptor-pool populator [V]

### 2.1 The pool and its layout

The pool is runtime RAM at **CA 0x1ADC98** (the image ends at CA 0x122C98, so
the whole subsystem is built at runtime). The layout is proven by the boot
builder that writes it and by the consumer that reads it:

~~~
pool + 0x04   u8   descriptor count          (boot default 2)
record i at pool + 16*i:
  +0x08       u32  record word: byte0 band 0=2.4G / 1=5G,
                                byte2 bit7 = 0x00800000 valid bit
  +0x0C       u8   channel-plan field         (boot default 30)
  +0x0E       u16  channel-plan field         (boot default 300)
  +0x14       u32  channel bitmap (bit b = channel index b of that band)
~~~

Boot defaults written by the init block (CA 0x485FA..0x4861E):

| record | word | bitmap | meaning |
| ---: | ---: | ---: | --- |
| 0 | (band byte 0) | 0x00001FFF | 2.4G channels: 13 of 14 rows |
| 1 | 0x00000201 | 0x1FFFFFFF | 5G channels: all 29 rows; valid bit 0 |

The record index i is exactly the "slot" the descriptor predicate consumes:
`CA 0x101650(band, slot) = CA 0x1ADC98 + 16*slot + 8`. Therefore, in the boot
default configuration, the record for any 5G channel has slot 1 and word
0x00000201 -> byte2 = 0x02 -> **the 0x00800000 bit is clear**. The predicate
path is: channel index -> **CA 0x10BE68** field word -> slot from
**CA 0x117AD4** -> record word at **CA 0x1ADC98+16*slot+8** -> byte2 bit7 ->
latched by **CA 0x89840** into R+0x410 bit0. It writes nothing else.

### 2.2 The populator CA 0x1017F8

Literal pool CA 0x101884..0x10188C supplies band-0 table, band-1 table and the
pool base (0x1ADB80 + 0x118 = 0x1ADC98). Decode:

~~~
1) r2 = CA 0x117B48,  r0 = 0xFF: for off in 0..0x38 step 4: [r2+off+1] = 0xFF
2) r2 = CA 0x117AD4,  r0 = 0xFF: for off in 0..0x74 step 4: [r2+off+1] = 0xFF
3) r7 = count = u8[pool+4]; base = pool
4) for i = 0..count-1, rec = pool + 16*i:
     type = u8[rec + 8]
     if type == 0: for b in 0..13  if [rec+0x14] & (1<<b): [CA 0x117B48 + 4b + 1] = i
     if type == 1: for b in 0..28  if [rec+0x14] & (1<<b): [CA 0x117AD4 + 4b + 1] = i
~~~

So the populator is a pure "channel bitmap -> channel map" expansion. It runs
**after** the pool is (re)written, and is idempotent.

### 2.3 Who calls it, and when

| caller | trigger | what it installs | call site |
| --- | --- | --- | --- |
| CA 0x48520 block | firmware pre-HCC init body (unconditional at boot [I]) | the 2-record default pool | CA 0x48620 `bl 0x1017F8` |
| CA 0x45A30 | **H2D key 0x00F7** (H2D table row 92) | host descriptor blob -> pool | CA 0x45A76 `bl 0x1017F8` |
| CA 0x43460 | H2D key 0x00DA (row 22) | edits record +0x0C/+0x0E only | does not call the populator |

**The 0x00F7 worker, line by line [V]:**

~~~
r0 = pool; r1 = body; blx CA 0x1037BC            pool-state gate
if gate == 0: goto populate
idx  = u8[body+4]
len  = 8 + idx*16
CA 0xC1E44(pool, 0x148, body, len)              memcpy_s: dest=pool,
                                                destMax=0x148, src=body
if error: log + return 0x8b2d
populate: bl CA 0x1017F8
r6 = [ctx+0x4B8] (band object); if r6:
    CA 0xA13C4 / CA 0x1036AC / CA 0x1035A8 feature tests
    -> band object +0xEC0 bits 0,1,2
~~~

So **H2D 0x00F7 is the "download the band/channel descriptor table + band
feature config" message**. The host controls: the descriptor count (body[4]),
the record words (including the 0x00800000 valid bit), the bitmaps and the
+0x0C/+0x0E fields, up to the 0x148-byte pool window (max idx 20 before
memcpy_s refuses). It also needs a live band/link context (the worker's r0
object with [+0x4B8] band object).

**What host-visible state must exist for the populator to run:**

* boot path - none (it runs during firmware init);
* 0x00F7 path - a live band context (the 0x00F7 worker object), the H2D
  message, and a descriptor blob whose body[4] is <= 20. Nothing else.
* Our five-frame ladder (0x0064 VAP create, 0x0065 VAP start, 0x0067,
  0x0124 channel, 0x0127 AP config) does **not** contain 0x00F7 or 0x00DA.
  The firmware therefore runs on the **boot-default map** (slot 1 for all 5G
  channels) unless a previous vendor stack instance installed a 0xF7 table
  before the reload. **[I]**

### 2.4 The bench discrepancy (reads first)

The CW-2 read reported the channel-table slot 0x23 (35) and the pool record
word 0x001B5B0A at CA 0x1ADC98+16*0x23+8. That is **inconsistent with the
boot-default map** (slot 1, word 0x201) and byte0 = 0x0A of 0x001B5B0A is not a
valid populator band type (0/1). Two readings are possible: a host 0xF7 table
with >= 0x24 records was installed at some point, or the slot/address was
derived from a different row. The probe pack (T5/T8) settles it in one pass:
read pool count (u8 at CPU 0x1ADC9C), the current channel row
(u32 at CPU 0x117AD4+4*idx), and the record word the mapper actually returns.
**Do this before trusting either interpretation.**

## 3. C4 - the ini beacon policy path [V] path, [I] effect

The vendor ini (local copy) sets, in [HOST_WIFI_NORMAL]:

~~~
beacon_2g_tx_policy=0
beacon_5g_tx_policy=2
~~~

The keys are parsed by **hi5622v100_plat.ko**, not by the wifi module:

1. **Key table** in plat.ko .data+0x1B0..0x6B8: 162 entries {u32 id, u32
   .rodata.str1.4 offset}. **id 0xAE -> "beacon_2g_tx_policy"
   (str1.4+0x1F5C)**, **id 0xAF -> "beacon_5g_tx_policy" (+0x1F70)**. [V]
2. **Parse**: plat_main_init (CA 0x3054) -> ini_cfg_init -> hwifi_config_init
   for modules 17/18/19 -> hwifi_get_init_value(module, id) CA 0x16AC reads a
   per-module u32 array (g_al_host_init_params / g_al_dts_params, index 0..203).
   Thus **beacon_5g_tx_policy lands in g_al_dts_params[175]**. [V]
3. **Copy to the shared blob**: hwifi_get_dts_dpd_pwr_customize_param
   (CA 0x4950) reads ids 0xAD..0xB1 and stores:
   * id 0xAE (2g policy) -> **g_dev_custom_info + 0x91C** (byte) and +0x91D
     (bits 16-23);
   * ids 0xAF..0xB1 (5g policy, far/near dist power gain) -> u16 pairs with the
     low half at **g_dev_custom_info + 0x7AE/+0x7B0/+0x7B2** and the high half
     at **+0x7B4/+0x7B6/+0x7B8**; **beacon_5g_tx_policy = 2 lands at +0x7AE**
     (u16) with the high half at +0x7B4. [V]
4. **Reach**: g_dev_custom_info is a plat.ko **exported** symbol (bss 0xC88,
   size 0x9A8) and is imported by hi5622v100_wifi.ko (`U g_dev_custom_info`).
   plat registers it (wifi_dev_custom_register(0,&g_dev_custom_info,0x9A8,0))
   and the only send path is **dev_custom_send CA 0x1BE98 -> bal_write** (a
   bus/config write of a 0x60-byte record). [V]

**Verdict:** the beacon TX policy is **platform/ini-derived host state**. It is
not present in our five H2D frames; nothing the five frames carry can set
+0x7AE, and the firmware's own per-band "beacon policy word" was not located
statically ([U]); the only host->device paths are plat's bal_write config path
and the wifi driver's own config messages. C4 can therefore only be exercised
by running the vendor plat module with the shipped ini (or by adding an
equivalent host config send). It cannot be synthesised by reordering the five
frames. This re-ranks C4 below C1/C5.

## 4. C5 - the band-1 start/TBTT arm [V]/[U]

### 4.1 The decoded hook

VAP start-on-band **CA 0x971D8** (called from VAP start CA 0x46394 with the
band object + VAP ctx):

~~~
CA 0xA4CA4(ctx, band)                  resolve band/link objects
if [ctx+0x51C] != 1: skip               module state
reconcile the two VAPs (band/field compare), possibly swap
CA 0x61E14(module)
G = CA 0x1747C4                         literal at CA 0x9745C
if G+0 != 0:
    if G+0x370 != 0: G+0x370(ctx)       CA 0x174B34  MAC start hook
    if G+0x374 != 0: G+0x374(ctx)       CA 0x174B38  beacon start hook
~~~

Both callbacks are called only through the global table and only for a started
module; a NULL/zero slot silently skips the MAC/beacon start. **[V]**

Related per-band state already decoded by earlier lanes: VAP+0x58 bit2 =
band-1 VAP flag (VAP start CA 0x46394), VAP+0x13A u16 beacon interval (AP config
CA 0x84DA4, body+0x94), and CA 0x7E5CC copies VAP+0x13A into the BSS/beacon
descriptor +0x260. **[V]**

### 4.2 The dead end that shapes the probe

The image carries 45 ISR names at CA 0x12266C (incl. **smac_tbtt_5g_isr
CA 0x1229E8** and smac_tbtt_2g_isr CA 0x1229FC), but the blob has **no code
reference at all**: no literal, no pointer table, no movw/movt construction.
It is a name-only debug blob; the per-band TBTT handler addresses and the
arming condition cannot be extracted statically. C5 must be decided by **the
differential band-object/TBTT probe** (T5/T8): read G+0x370/G+0x374 and both
band objects on the working band-0 as the reference.

### 4.3 Ranked C5 sub-causes

* **C5a** - band-1 MAC/beacon start callback is 0 at the time VAP start runs
  (G+0x370/G+0x374 read 0). Signature: 5 GHz VAP enabled, band object bound,
  RF stale, no beacon. Discriminator: T5 probes 18/19.
* **C5b** - CA 0x971D8 early-returns for band 1: the VAP reconcile logic
  (band/field compare, module-state byte [ctx+0x51C]) never reaches the hook.
  Signature: VAP+0x58 bit2 = 1, G callbacks nonzero, no beacon. Discriminator:
  VAP+0x58/VAP+0x13A + band object diff.
* **C5c** - the hook runs but the beacon queue is not primed because the
  per-band descriptor/pre-TBTT state is absent (follows from a stale RF block:
  C1 territory). Discriminator: R+0x3AC etc. (already in the RF lane pack).

## 5. Ranked causes overall (this lane's re-ranking)

**C1 (primary, unchanged)** - the 0x0124 RF programme was silently skipped at
post time (all gates are silent early-returns). Signature: R == 0 or
R+0x3AC == 0 while the gate bytes now read 1. Discriminator: RF lane T7 probes
5-8/15-17, then re-post 0x0124 (R1/R2 recipe).

**C5a (new, second)** - the band-1 MAC/beacon start callback is not installed;
the beacon engine is never started even if the RF programme is fine. Read
G+0x370/G+0x374; this is the cheapest new discriminator in the whole 5 GHz
problem (2 reads, band-0 as reference).

**C4 (third)** - per-band beacon TX policy exists only in the plat ini/custom
blob, not in our frames. Cannot be synthesised by reordering the ladder; only a
host config path (vendor plat module or an equivalent send) can supply it.

**C2 (demoted: by-design observable)** - the 0x00800000 descriptor valid bit is
host-supplied in the 0xF7 blob and is 0 in the boot default. R+0x410 bit0 = 0
is therefore expected on the default map and must not be treated as the 5 GHz
TX blocker. It becomes meaningful only if the probe shows a host table
installed and a descriptor whose valid bit should be set.

## 6. Read-only probe pack

The pack (CPU / BAR0 / phys) is in **TABLES.md T5** and is printed by
`tools/fw_desc_pool_re.py --probes`. Minimum decisive set for this lane:
pool count (CPU 0x1ADC9C), current-channel row (CPU 0x117AD4+4*idx and
0x117B48+4*idx), record word (CPU 0x1ADC98+16*slot+8), G+0x370/G+0x374
(CPU 0x174B34/0x174B38), VAP+0x58/VAP+0x13A (after u32(ctx+0x190)),
R+0x3AC/R+0x410 (after the perlink->O->R chain). No writes, no side effects.

## 7. Fire recipe summary

Full rungs in **FIRE-RECIPE.md**. Order: **R0** read-only probe pack on the
running 5 GHz AP (decides map + callbacks + RF state, no fire); **R1** re-post
the 0x0124 frame once after the five-frame order (zero rebuild, tests C1);
**R2** small host patch to repeat 0x0124 after the full order and to gate on
R+0x3AC; **R3** only if R0 shows a host 0xF7 table with clear valid bits:
fix the descriptor source (host) rather than re-blazing frames; **R4** if R0
shows NULL G callbacks or a band-1 early return: report, do not patch blind
(firmware-side blocker, no host frame identified). Abort rules unchanged: page
0x86807fef / state 1 / mask 0 exception -> power-cycle, no retry; never write
CA 0x400392f0; never read 0x10160000..0x1016ffff.

## 8. Limits / honest unknowns

* The runtime pool contents (CA 0x1ADC98) are [U] until the R0 probe; the
  static decode only proves the default boot values and the populator rules.
* The 0x1037BC gate's exact predicate is [I] (it is invoked with the pool base
  and the message body and its return selects the install path; its internal
  decode is not needed for the probe).
* The firmware-side beacon-policy word (C4 landing) is [U]; only the host
  platform path is proven.
* The per-band TBTT handler addresses are [U] (name-only blob, no code refs).
* The CW-2 slot 0x23 / 0x001B5B0A reading is [V*] but inconsistent with the
  boot-default map (see 2.4); the R0 probe resolves it.
* This lane contacted no device; no vendor byte was copied into the repo.

## 9. Reproduction

| evidence | what |
| --- | --- |
| TABLES.md | every address, pool layout, C4/C5 tables, probe pack, decision tree |
| FIRE-RECIPE.md | rungs, abort rules |
| tools/fw_desc_pool_re.py | re-derives every claim from the pinned local firmware + `.ko` files; `--selftest` (97 assertions, all PASS); `--decode/--tables/--policy/--tbtt/--probes` |
| evidence/*.txt | captured output of the above (no vendor bytes) |
