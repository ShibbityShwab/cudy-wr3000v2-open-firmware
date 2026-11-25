# REPORT - the H2D 0x00F7 descriptor table: vendor builder, blob, gate, and the host-armed valid bit

**Lane:** wifi-fw-desc-table (shared task **task-187**), 2026-11-25. HOST-ONLY:
static decode of the pinned local artifacts; no device contact, no write to any
artifact, no fire. **The Lead fires.** Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_desc_table_re.py](tools/fw_desc_table_re.py).

Grades: **[V]** re-derived here by the committed tool; **[V*]** decoded by a
committed sibling lane and cited; **[I]** inference; **[U]** needs a device read.

## 0. TL;DR

1. **The vendor emitter is `hmac_config_set_country` (wifi.ko .text+0x63B6C)**,
   an exported hmac-layer function. At **.text+0x63C3C** it calls
   `hmac_config_send_event(ctx, key=0x00F7, len=8+blob[4]*16, data=blob)` - the
   length formula matches the firmware worker's memcpy formula exactly. It is
   the only static 0x00F7 g0-id1 emitter in the shipped module set. **[V]**
2. **Frame id/layout**: HCC **group 0, message id 1** (the firmware g0-id1
   config-event handler CA 0x43D59). 12-byte header, 8-byte preamble, **u16 key
   at hdr+20**, **u16 len at hdr+22**, **blob at hdr+24**; declared total =
   len+24 (our clean driver's family also appends the 4-byte inherited tail,
   so wire = len+28). Key 0x00F7 is firmware table-A **row 92** -> worker
   **CA 0x45A31**. **[V]**
3. **Blob = 8-byte header + N*16-byte records**; header `{cc[2], 0, chip_id,
   N, 0,0,0}`; record `{u32 word (band byte0, bw code byte1, flags byte2
   bit7=valid), u8 (+4), u16 (+6), u32 channel bitmap (+12)}`; N <= 20;
   no CRC and no self-describing type - validation is the gate + memcpy_s
   bound + the handler's key lookup. **[V]**
4. **The gate CA 0x1037BC is an ARM `strcmp(pool, body)` dedupe**, not a
   permission gate. It returns 0 ("already the same table") when the pool's
   leading country-code string equals the blob's, and the install is then
   skipped; non-zero installs. Boot default pool string = **"99"**, so the
   fix frame must use a different cc (e.g. **"US"**) or it is silently ignored.
   The host twin `mac_regdomain_set_country` (.text+0x206E0) performs the
   identical strcmp-then-memcpy on the host copy of the same 0x148-byte table,
   which cross-checks the semantics. **[V]**
5. **The 0x00800000 bit is never written by shipped code**: the vendor WAL
   table filler (.text+0x121E10) writes record byte2 bits 0/1/2 only; the host
   twin channel-list init (.text+0x205E4) never touches byte2. It is read by
   the band-1 predicate CA 0x101890 and is therefore pure host data - the C1
   arming decision. **[V] read / [I] effect**
6. **Decoys**: the WAL layer sends group-2 cmd 0xF7 twice (`wal_regdomain_update`
   CA 0x1226F0 with a host pointer payload, and `wal_ioctl_get_reg_domain`
   with a 2-byte GET body); neither carries the descriptor blob inline at
   hdr+24, so neither can be the message the firmware pool worker consumes. **[V]**

## 1. Inputs and method

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...c3ef311b`; **CPU = file + 0x40000** (project convention) |
| vendor modules | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` (3,581,748 B, not stripped, 16,751 defined nm rows) and `hi5622v100_plat.ko` |
| disassembler | lab toolchain `arm-none-linux-gnueabihf-objdump -d -r` for the ET_REL modules (.text offsets are section-relative); `objdump -D -b binary -m arm [-M force-thumb]` for the image (CA convention) |
| symbols | `nm -n --defined-only`; all call/goto relocations via `objdump -r` |
| artifacts | `build/wifi-fw-desc-table/` (gitignored): `full.dis`, `syms.txt`, `relocs.txt`, `plat.dis` |

Method: (1) locate every 0x00F7 immediate and every call site of the two
config-event constructors in wifi.ko/plat.ko; (2) backward-scan the argument
registers of each call site; (3) disassemble the emitter, the constructor, the
blob filler and the host twin; (4) disassemble the firmware g0-id1 handler, the
table-A row, the 0x00F7 worker, the pool gate and the populator at the CA
addresses the sibling lanes committed; (5) cross-check the wire layout against
the project's *working* g0-id1 frames (0x0064/0x0124/0x0127 ladder).

## 2. The emitters - all 0x00F7 sites in the shipped module set [V]

`grep -rn '#247'` over the full wifi.ko disassembly and the whole plat.ko
disassembly; only these are message keys:

| site | function | what it sends | matches the firmware worker? |
| --- | --- | --- | --- |
| wifi.ko .text+0x63C3C | **`hmac_config_set_country`** (.text+0x63B6C) | g0-id1 key 0xF7, len = 8+blob[4]*16, **blob inline at hdr+24** | **yes - the only one** |
| wifi.ko .text+0x12290C | `wal_regdomain_update` (.text+0x1226F0) | **g2 WAL cfg** cmd 0xF7, body = `{u16 0x00F7, u16 0x0004, u32 host_ptr}` (8 B) | no (blob not inline) |
| wifi.ko .text+0x108454 | `wal_ioctl_get_reg_domain` | g2 WAL cfg cmd 0xF7, 2-byte GET body | no (GET) |
| plat.ko | - | no `#247` immediate at all | - |

`hmac_config_set_country` is exported through `__ksymtab` (.data+0xE64), and is
reached inside the module through the WAL ops table at **.data+0x8900** via the
thunk **`wal_config_set_country` .text+0x12AE0C** (a `b hmac_config_set_country`).
No shipped .ko imports the ksym (swept all 256 modules of the rootfs), so the
in-tree WAL table entry is the reachability path.

### 2.1 `hmac_config_set_country` .text+0x63B6C, line by line

Signature `(vap r0, ?, country_cfg *r2)`; `r6 = [r2]` is the blob pointer
(`r2`'s first word) and `r2` itself is passed on to
`mac_regdomain_set_country` (the host-side twin). Flow:

~~~
63b6c  r6 = [r2]                                  ; blob = *cfg
63b94  r8 = [r0+2]                                ; module id for logs
63ba8  mac_res_get_mac_chip(0)                    ; chip object
63be8  r5 = [chip + 0x3B8]                        ; per-module hmac ctx (must be live)
63bf4  mac_regdomain_get_country()                ; current cc string
63bf8  if blob[0..1] == current cc: goto done     ; same-country fast path
63c08  [r5+0xD36] = 0
63c14  mac_regdomain_set_country(r2)              ; host copy of the 0x148 table +
                                                  ;   strcmp + memcpy + channel maps
63c18  if [r5+0xD36] == 0:                        ; first-time path: also reinit DFS list
63c24  len = 8 + (u8)blob[4]*16                   ; *** the firmware's exact formula ***
63c30  hmac_config_send_event(ctx=r4, key=0xF7, len, data=blob)   ; *** 0x63C3C ***
63c40  if error: log + kfree(blob) + return
63c4c  kfree(blob) ; hmac_multiap_init_opr_class_per_band()
~~~

So the *same* 0x148-layout table is installed host-side (mac emulation) and
shipped to the device in the same call - that is why the wire length formula and
the firmware memcpy formula are identical. **[V]**

### 2.2 `hmac_config_send_event` .text+0x5717C (+ shared allocator .text+0x57090)

Signature `(ctx r0, u16 key r1, u16 len r2, data r3)`; body verified by the
wifi-rx-init-msg lane [V*] and re-read here:

~~~
571d8  alloc = hcc_msg_alloc(len + 16)            ; 12-byte header + prologue
571e4  data_ptr = hdr + 12
571f8  strh key -> hdr[20]                        ; payload + 8
571fc  strh len -> hdr[22]                        ; payload + 10
57200  memcpy_s(hdr+24, len, data, len)           ; payload + 12
57210  hcc_msg_tx(msg)
~~~

Header (shared builder, .text+0x57090): `hdr[6..7] = 1` (message id 1), group
nibble from the descriptor, dst nibble from the descriptor, tag 0x5A5A at
hdr+10. This is exactly the family our clean driver already sends
(h2d_build_g0_cfg_event: token[8] + u16 key + u16 len + data + 4-byte tail);
the project's working 0x0064/0x0124/0x0127 ladder confirms the wire shape **[V*]**.

## 3. The blob - the 0x148-byte pool image minus the pad [V]

The firmware worker copies `8 + body[4]*16` bytes from the message body into the
runtime pool CA 0x1ADC98 (destMax 0x148 = 328 = 8+20*16). Therefore the blob is
exactly the pool image for a table of N records:

~~~
blob + 0x00  u8[2]  country code chars cc[0], cc[1]        (string; boot pool = "99")
blob + 0x02  u8     0                                      (NUL terminates the cc string)
blob + 0x03  u8     chip / regdomain id                    (boot pool = 0)
blob + 0x04  u8     N = record count                       (the worker reads this)
blob + 0x05  u8[3]  0
record i at blob + 8 + 16*i:
  +0x00  u32  word:  byte0 = band 0=2.4G / 1=5G / 2=other-5G-range,
                      byte1 = bandwidth code (2=20 MHz, 4=40 MHz, 5=80 MHz, 6=other),
                      byte2 = flags; **bit7 = 0x80 = the host-armed valid bit**,
                      byte3 = 0
  +0x04  u8   power/plan field   (vendor: block[+36]/100; boot default 30)
  +0x05  u8   0
  +0x06  u16  power/plan field   (vendor: block[+36]/10;  boot default 300)
  +0x08  u32  0
  +0x0C  u32  channel bitmap; bit b = channel-map row index b of that band
              (band 0: 14 rows ch1..13; band 1: 29 rows ch36..196)
~~~

There is **no CRC, no checksum, no version and no self-describing length** in the
blob. Validation is only: (a) the gate strcmp (§5), (b) memcpy_s destMax 0x148
(N <= 20), (c) the firmware handler's table-A key lookup. **[V]**

### 3.1 The vendor's blob filler .text+0x121E10 [V]

`wal_regdomain_update` (.text+0x1226F0) allocates `8 + 16*regdb_entry[8]` bytes,
zeroes it, and calls the filler with (regdb entry, blob):

~~~
121e24  blob[0] = sb[entry+12] ; blob[1] = sb[entry+13] ; blob[2] = 0
121e38  search a 194-entry {char *name; u8 id} table by the cc string -> id
121e94  blob[3] = id
121e9c  blob[4] = (u8)entry[8]                     ; record count
        per 96-byte channel-plan block at entry + 96*i:
          freq_kHz = block[+20], freq_end_kHz = block[+24], bw_kHz = block[+28]
          band = 1 (5G) if 2.4 GHz <= freq < 2.500999 GHz or in the 4.9/5.9 GHz
                 windows; 0 (2.4G) in the 2.4G window; 2 otherwise
          blob[8+16i+0] = band
          blob[8+16i+1] = bw/1000 -> 2 (20), 4 (40), 5 (80), 6 (other)
          pb = block[+36]: blob[8+16i+4] = pb/100, blob[8+16i+6] = pb/10
          blob[8+16i+12] = bitmap of g_ast_freq_map_{2g,5g} rows from
                 start+10 MHz to end-10 MHz in 5 MHz steps
          (+0x0A byte2 bits 0/1/2 = next block's flags & 0x10/0x08/0x04)
~~~

Consequences: (a) the record fields above are the vendor semantics; (b) the
filler **never writes byte2 bit7**; (c) the record's +4/+6 fields are
power-plan numbers (3000/100 = 30, 3000/10 = 300 - the same boot defaults).

### 3.2 The boot-default pool (firmware CA 0x485BA..0x48620) [V]

`memcpy_s(pool, 3, CA 0x1043A2, 3)` copies the literal **"99\0"** into
pool[0..2]; then count=2, rec0 {word 0, +4 30, +6 300, bitmap 0x1FFF},
rec1 {word **0x00000201** (band 1, bw 20 MHz, byte2 0), +4 30, +6 300,
bitmap 0x1FFFFFFF}; then `bl CA 0x1017F8` (the populator). The boot 5G word
has the valid bit **clear by design**, which is exactly the C1 symptom. The
block's entry is [I] (it sits inside a larger pre-HCC init body with no direct
`bl` caller - the sibling lane's finding, unchanged here).

## 4. The firmware side: key table, handler, worker [V]

| element | address | fact |
| --- | --- | --- |
| g0-id1 handler | CA 0x43D59 | `[r4+20]` = key, looked up in two 8-byte-entry tables; found entry -> `r3=[entry+4]` = worker; calls `worker(r0=?, r1=[r4+22], r2=r4+24)` -> **body = msg+24** |
| table A | CA 0x10C23C, 217 entries | the first table searched (bound 0x6C8); row 92 = key **0x00F7** -> worker **0x045A31** |
| table B | 88 entries (bound 0x2C0) | second table searched; 0xF7 is in table A |
| 0x00F7 worker | CA 0x45A30 | the pool install worker (below) |
| pool | CA 0x1ADC98 | runtime descriptor pool |
| populator | CA 0x1017F8 | clears both maps to 0xFF then expands the pool bitmaps |
| band-1 predicate | CA 0x101890 | returns 1 iff the resolved record's byte2 bit 0x80 is set |
| latch | CA 0x89840 | `R+0x410 bit0 = predicate` |

Worker flow, literal pool at CA 0x45B40 = **0x001ADC98** (the pool):

~~~
45a30  r4 = msg ctx ; r7 = body ; ldr r0, =pool
45a3c  blx CA 0x1037BC          ; gate(pool, body)  *** ARM strcmp, see §5 ***
45a40  if gate == 0: goto populate          ; identical table -> keep the pool
45a44  count = body[4]
45a4a  len = 8 + count*16
45a52  memcpy_s(pool, 0x148, body, len)     ; ARM helper CA 0xC1E44
45a58  if memcpy_s != 0: log, return 0x8B2D
45a76  bl CA 0x1017F8          ; populate the two channel maps from the pool
45a7a  band = [ctx+0x4B8] ; if band == 0: log, return 0x64
45a98  band-feature tests (CA 0xA13C4 / 0x1036AC / 0x1035A8) -> band+0xEC0 bits 0,1,2
~~~

**Refusal/return codes**: `0` = applied (or deduped then re-expanded);
**0x8B2D** = pool memcpy failed (destMax 0x148 exceeded: count > 20);
**0x64** = no live band object at ctx+0x4B8 (table installed but the band
feature bits are not programmed). The gate itself cannot "refuse": `0` means
*identical, skip the copy* and the populator still runs on the old pool. **[V]**

## 5. The gate CA 0x1037BC = ARM `strcmp(pool, body)` [V]

The worker calls it with `blx` (an even address = ARM state) and
`r0 = pool, r1 = body`. Decoded from the image:

~~~
1037bc  eor ip, r0, r1 ; tst ip, #3 ; bne 0x103850   ; align fast path
1037c8  ands ip, r0, #3 ; bic r0/r1, #3 ; ldr r2, [r0], #4 ; ...
10380c  word-at-a-time compare with the classic 0x01010101 zero-byte test
103828  tail: byte compare, return (signed) difference
103850  unaligned/byte path: ldrb r2,[r0],#1 ; ldrb r3,[r1],#1 ; cmp; return diff
~~~

This is the standard ARM `strcmp`; it stops at the first NUL. The pool's first
bytes are the country-code string (boot "99", NUL at pool+2), and the blob's
first bytes are its cc string (NUL at blob+2), so the gate is in practice
**"is this the same country's table?"**:

* `pool_cc != blob_cc` -> non-zero -> **memcpy install** -> populator -> table live;
* `pool_cc == blob_cc` -> 0 -> **install skipped**, old pool re-expanded.

Cross-check: the host twin `mac_regdomain_set_country` (.text+0x206E0) does
`strcmp(host_copy, blob)` then `memcpy_s(host_copy, 0x148, blob, 8+N*16)` and
calls `mac_init_channel_list` (.text+0x205E4, the host twin of the populator:
clear both maps to 0xFF, expand). Same bound, same dedupe, same layout. **[V]**

**Fire-recipe consequence (important)**: the fix frame must carry a country code
different from what is in the pool. Boot default = **"99"**, so **use "US"**
(any two letters except "99" work; "99" is silently ignored).

## 6. When the vendor sends it - trigger and order [V] path, [I] order

Static trigger chain (all wifi.ko):

~~~
module init: hwifi_config_init_ini (.text+0x122FE0)
  -> hwifi_config_init_ini_proc (.text+0x122F08)
  -> hwifi_config_init_ini_country (.text+0x1558)
       cc = hwifi_get_country_code()            ; ini / plat value
       -> wal_hipriv_setcountry (.text+0x122D0C) ; the "setcountry" hipriv command
            -> wal_regdomain_update_for_dfs (.text+0x122B14)
            -> wal_regdomain_update (.text+0x1226F0)  ; builds the 8+16N blob (fill §3.1)
iwpriv/cfg80211 country set: same handler; the hmac twin is reached through the
WAL ops table (.data+0x8900) -> wal_config_set_country (.text+0x12AE0C) ->
hmac_config_set_country (.text+0x63B6C) -> g0-id1 key 0xF7 (the wire form the
firmware actually consumes).
~~~

So the 0x00F7 table belongs to **regulatory/country initialisation**, i.e. the
start-up configuration phase - once, before the AP/VAP start ladder. The exact
position relative to 0x0124 in the *vendor's* init is **[I]** (the sender is not
inside the AP-start call graph; the call sites are init/regulatory). For our
host the ordering that satisfies both the firmware prerequisites and the RF
programme is:

**0x0064 (create) -> 0x0065 (start) -> 0x00F7 (this table) -> 0x0124 (channel
post) ...**. Rationale: the g0-id1 handler needs a live module object; the
worker's band-object test (ctx+0x4B8) passes only after the VAP/band context
exists; the RF programme (CA 0x40684 -> predicate CA 0x101890) evaluates the
descriptor map at 0x0124 post time, so the table must already be installed.
If the ladder has already posted 0x0124, re-post it after 0x00F7 (the populator
is idempotent). [I]

## 7. The two decoys - what not to copy [V]

1. **`wal_regdomain_update` CA 0x1226F0** builds the *same* 8+16N blob (via the
   filler of §3.1) but sends it as a **group-2 WAL cfg event**
   (`wal_send_cfg_event` CA 0x126C98) with an 8-byte body
   `{u16 0x00F7, u16 0x0004, u32 host_pointer}` - the table is *not* inline.
   The firmware pool worker reads the blob at a fixed body offset and cannot
   follow a host pointer, and the group-2 receive table has no id-0 handler for
   this shape (sibling h2d-catalog finding). **Not our model.**
2. **`wal_ioctl_get_reg_domain`** sends g2 cmd 0xF7 with a 2-byte body: a GET.
   It reads the current regdomain back; it installs nothing.

The g0-id1 hmac path (§2.1) is the only emitter whose `len = 8+body[4]*16`
matches the firmware worker's `memcpy_s(pool, 0x148, body, 8+body[4]*16)`.
That identity is the decisive join. **[V]**

## 8. Why the host sets the valid bit [V read, I policy]

The record word byte2 bit7 (0x00800000) is the **descriptor-valid / armed**
marker consumed by the band-1 predicate CA 0x101890 (sibling wifi-fw-rf-programme
lane [V*]: for field 0 it resolves the current channel's map row -> slot ->
record, and returns 1 as soon as `record+2 & 0x80`; otherwise 0). No shipped
module code writes the bit:

* the vendor blob filler writes byte2 bits 0/1/2 only (§3.1);
* the host twin `mac_init_channel_list` writes only the channel-map slot bytes
  (§5).

The boot-default 5G record is `0x00000201` -> byte2 = 0x00 -> predicate 0. The
bit therefore exists only if the host puts it in its 0x00F7 table - it is a
host **policy** bit ("this 5 GHz record is a live, armed descriptor"), not a
firmware-computed gate. Setting it is how the host arms the 5 GHz channel plan;
that is the C1 fix, and the fire is the discriminator (the predicate result is
latched read-only into R+0x410 bit0, so the RF/beacon observation is separate).

Note the sibling rf-programme lane's call-graph finding that CA 0xA2FA2 (the RF
image writer) is invoked *before* the predicate result is latched: whether the
ch36-64 image (CA 0x89924) or the beacon additionally depends on the armed bit
is **[U]** on the bench - the read-only probe pack of §FIRE-RECIPE isolates the
two observables (R+0x410 bit0 vs R+0x3AC / beacon). If the RF image appears
while bit0 stays 0, C1 is not the predicate gate and the Lead must re-rank.

## 9. Limits / honest unknowns

* The object that builds `hmac_config_set_country`'s blob in the *vendor host
  stack* is not statically visible: the function is exported but unimported by
  the 256 shipped .ko files, and its in-tree reachability is the WAL ops table.
  The blob layout itself is [V] (firmware worker + host twin + filler agree).
* Whether the vendor's own 0xF7 table carries byte2 bit7 is **[U]**: no static
  path sets it; the hmac-path producer is the missing link. The bit's *consumer*
  is [V].
* The gate's exact string semantics are [V] as ARM strcmp, but the first-byte
  comparison only (country code) is the practical effect; if the pool's cc is
  empty (boot block never ran) any non-empty cc installs.
* Field 0 of the predicate (our AP's field) is decoded; field 6's window stays
  [U] in the sibling lane.
* The runtime pool contents (CA 0x1ADC98) remain [U] until the R1 probe; this
  lane contacts no device.

## 10. Reproduction

| evidence | what |
| --- | --- |
| TABLES.md | all module/firmware addresses, frame table, blob table, boot pool, probes |
| FIRE-RECIPE.md | byte-exact frame, host notes, verification rungs, abort rules |
| tools/fw_desc_table_re.py | re-derives every claim from the pinned local artifacts; `--selftest` (see evidence/), `--frame` (emits the exact 68-byte frame), `--tables` |
| evidence/selftest.txt | captured selftest output (no vendor bytes) |
