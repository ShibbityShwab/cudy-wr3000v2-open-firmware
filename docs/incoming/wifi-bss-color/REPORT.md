# REPORT - g0 id36 hmac_scan_config_bss_color: builder, caller, firmware handler, integration

**Lane:** wifi-bss-color (task-233), host-only. **Basis:** the stock H2D capture decoded by
task-232 (commit e977557; capture `/tmp/h2d-stock.log`, sha256 in
`../wifi-stock-h2d-census/evidence/LOG-SHA256.txt`, local/gitignored), plus the local
vendor host-module disassembly (`wifi.dis.txt`, vendor module 2.5.24 line) and the local
firmware disassembly (`build/fw-h2d-stall/stock-thumb.dis`, the 928,920-byte firmware
image). No vendor byte, object or firmware image is committed here; only derived addresses,
structure and original text.

## 1. The builder @0x99C10 - complete decode [V]

`hmac_scan_config_bss_color` (nm: `00099c10 t hmac_scan_config_bss_color`), ARM, 0x254 B
(0x99C10..0x99E63 incl. literal pool). Signature (from the code):

    int hmac_scan_config_bss_color(void *band_scan_obj /*r0*/, void *result_list /*r1*/)

### 1.1 Geometry - it is a direct group-0 id message, not a config key

* `99C50`: arg0 -> r6 (scan object); `99C58`: `mov r0,#8; bl hcc_msg_alloc` ->
  **payload allocation = 8 B**, so the total wire frame is 12 + 8 = **20 B**.
* `99C68`: `ldr r3,[r4,#280]` (0x118) = the message buffer pointer (same `msg->buf`
  offset the clean-room catalogue uses); NULL -> error path.
* `99C90`: `mov r2,#36 (0x24); strb r2,[r3,#6]` -> **header+6 = id 0x24 = 36 (group 0)**.
  There is **no g0-id1 config-key envelope and no subkey**: the builder writes an id into
  the direct-message id field and an 8-byte payload behind the 12-byte HCC header.
* Header fields it writes: `hdr[0]` low nibble = 0 (group 0, `bfi r2,r8,#0,#4` with r8=0),
  `hdr[6]=0x24`, `hdr[7]=0`, `hdr[8]=0`, and the three sequence/addressing bytes below.
  Length (`hdr[4..5]`), alloc nibble (`hdr[1]` low nibble = 1) and tag 0x5A5A are set by
  `hcc_msg_alloc` / `hcc_msg_tx` (our contract: H2D_LEN_OFF 0x04, H2D_ID_OFF 0x06,
  H2D_TAG_OFF 0x0A).

### 1.2 The three addressing bytes (hdr[1] high nibble, hdr[2], hdr[3])

`99C94..99CBC` copy three consecutive bytes of the scan object:

| wire offset | source | captured value | decode |
| --- | --- | --- | --- |
| hdr[1] bits 7:4 | `scan_obj[+0xFAA]` (`ldrb r1,[r6,#4010]`; `bfi r2,r1,#4,#4`) | 0x0 | upper nibble of the bid/dst byte; 0 in the capture [V] |
| hdr[2] | `scan_obj[+0xFA9]` (`ldrb r2,[r6,#4009]`) | 0x00 | FA byte; 0 in all four new-family captures [V] |
| hdr[3] | `scan_obj[+0xFA8]` (`ldrb r2,[r6,#4008]`) | 0x07 | **destination chip id** [V] |

The `+0xFA8` byte is the per-band destination-chip byte: it equals the capture record's chip
column for the sibling families (g0 id32 FB=0x04 / chip 4; g0 id22 FB=0x0A / chip 10; g0
id36 FB=0x07 / chip 7) [V correlation]. The handler below also consumes hdr[3] as the chip
id, which closes the loop [V]. Our builder takes FA/FB/bid-hi as explicit parameters so the
byte-exact KAT can reproduce both the captured vector and our own posting.

### 1.3 Payload = 64-bit BSS-color bitmap

`99CBC..99D48`:

* `99CBC` `bl _raw_spin_lock` on the result-list object; the list is iterated from
  `[list+4]` (empty when next == head).
* Loop body (`99CDC..99D0C`): `ldrb ip,[node,#133]` (0x85) and a branchless 64-bit OR of
  `1 << ip` into a two-word accumulator on the stack (low word at +0x10, high at +0x14).
  So **bit i of a 64-bit bitmap is entry[i].color**, i.e. the payload is the union of the
  BSS colors of the list entries. ip >= 64 would silently fold (ARM register-shift
  semantics); the producer field is 6 bits, so 0..63 is the intended domain.
* `99D10..99D24`: the inlined ARM ticket-spinlock unlock idiom (dmb/ldrh+1/strh/dsb/sev).
* `99D28..99D38`: `memcpy_s(payload /*hdr+12*/, 8, bitmap, 8)` -> the 8-byte payload is
  the bitmap **little-endian**.
* `99D44..99D48`: `hcc_msg_tx(msg)`.
* Return: 0 on success; **100 (0x64)** on every failure path (`99DFC`, `99E24`, `99E50`
  set r5=100) - the vendor HAF status convention, not errno.

### 1.4 The entry colour field is proven: HE Operation IE BSS color [V]

The builder reads `node[+0x85]`. What writes it:

* `hmac_scan_proc_scan_result_handle + 0xB34` (0x9D870) is the only scanner that writes it:
  `9D848` `memset_s(sp+58, 9, 0)`; `9D854` `bl mac_ie_parse_bss_he_oper`; `9D860`
  `ldrb r3,[sp,#61]` (the parsed 9-byte HE Operation record's 4th byte = the BSS colour
  information byte); `9D86C` `ubfx r3,r3,#0,#6` (low 6 bits); `9D870`
  `strb r3,[r4,#133]` - store into the scan-result entry the builder later walks.
* The parsed record layout is the committed one (../wifi-ax-he-caps/TABLES.md T?:
  "HE Operation element cache (params 3, BSS color info 1, basic MCS/NSS 2, tail 3)").
  The 6-bit field is the 802.11ax BSS Color; bit 7 (partial) is masked off.

**Conclusion:** the vendor message is the scan path's **"BSS colors observed in this
scan" report**. It is not an AP BSS-color setter: the AP's own colour does not travel in
this builder (that is the firmware beacon path; ../wifi-ax-advanced/TABLES.md T9).

## 2. The caller @0x9A00C - scan completion [V]

`hmac_scan_proc_scan_comp_event` (0x99EAC), the D2H scan-complete event processor:

* `99EB8` msg buffer = `[msg+0x118]`; `99EC0` chip = `msg[+1] >> 4`;
  `99ED8` band = `msg[+3]`; per-band object = `g_st_mac_res[band + 0x15A]`.
* The scan object's status byte `[+0xFAC]` selects the completion handling
  (1 vs 2; other values are logged/ignored).
* Exactly the type-2 branch reaches `9A004: mov r1,r8; mov r0,r7; bl 99c10`, where
  **r7 = the per-band scan object (arg0)** and **r8 = mac_dev+0x88 = the scan-result list
  head (arg1)** - the same list `hmac_scan_print_scan_record_info` just walked (r8 is set
  at `99FC0: add r8,r4,#136`).
* After the call the handler advances the scan state and calls
  `hmac_restart_wait_scan_band` - i.e. the bitmap is posted **once per completed scan**.

Meaning of `[+0xFAC]==2` is [I] (a scan type/state enum); its role as the builder gate is
[V].

## 3. The firmware handler - file 0x755E1 / CA 0xB55E1 (Thumb) [V]

From the committed firmware receive-table decode (../wifi-h2d-catalog/REPORT.md App. A):
`g0 table CA 0x10CCFC, idx 30: id 0x24 -> handler file 0x755E1`. Disassembly of the image
at that file offset (address = file + 0x40000 => CA 0xB55E1, Thumb entry 0xB55E0):

* `B55E0` saves args; `B55E4` NULL msg -> log + status **100** error path.
* `B55FA` obtains the message body (helper 0x409EA); `B5600` reads `body[+3]` = the
  destination **chip id** and resolves the per-chip context (`bl 0xBC524(chip)`).
* `B5618` takes `ctx[+0x4B8]` (a per-chip object), indexes a byte table by
  `obj[+0xD70]` and uses it as a **one-shot dedupe flag** (`cmp #1; strb #1`): a repeated
  message for the same chip/state returns success without re-running the action.
* `B563C` loads firmware global+0x3EC (a registered callback pointer) and, when non-NULL,
  `B5646` `ldrd r2,r3,[body,#12]` = **payload[0..7] as two words**, then `B5656`
  `blx r6` calls the callback with (chip ctx, payload word0, payload word1, 0, 0).
* Return: 0 = consumed; 100 on the error paths.

**Safety:** the generic handler only loads the payload words and passes them by value to the
chip callback; there is no payload-derived pointer dereference (the catalogue's
`payload_offsets.py` heuristic listed "+0x0[W], +0x4ac[W]" - the +0x4AC is a reloaded r0,
per that tool's own caveat). The masked (bitmap) 8-byte payload is therefore safe to post,
which is what the fire rung does. What the chip callback does with the colour bitmap is
firmware-internal [U] and is observable only differentially on air.

## 4. Relation to our stack: what to build

* **What we implement:** the frame itself (portable builder + validator + bitmap helpers),
  byte-exact against the captured vector, and a kernel posting path.
* **Knob / posting point (this lane's integration choice):** cfg80211 hands a cfg80211-only
  driver the AP's HE BSS colour as `NL80211_ATTR_HE_BSS_COLOR`
  (kernel 6.18 `net/wireless/nl80211.c: nl80211_parse_he_bss_color`, colour range 1..63,
  disabled/partial flags) -> `settings->beacon.he_bss_color` +
  `settings->beacon.he_bss_color_valid`; hostapd 2026.08 sends it whenever
  `params->freq->he_enabled` (driver_nl80211.c:6203, support check is just attr <= kernel
  max attr). Our new fail-closed module knob `bss_color` (default 0) posts the frame once
  from `hi5622_scan_op_start_ap`, after the AP config is applied, with bitmap = 1<<color.
* **Honesty about the trigger:** the vendor's trigger is scan completion, not AP start; the
  captured bitmap is scan-observed colours. Posting the AP colour at start_ap is therefore
  a *differential probe* of whether firmware consumes the same 64-bit colour bitmap for its
  own BSS colour state (the on-air HE Operation IE is firmware-built: color 25 on image #3;
  see ../wifi-ax-he-caps and ../wifi-ax-advanced T9). The fire recipe grades the on-air
  differential, not an assumed write.
### 4.1 What this lane built and proved host-side

* `patches/0001-bss-color.patch` against the image #3 module tree: new
  `hi5622_bss_color.{c,h}` (portable 20-byte builder/validator + bitmap/colour policy +
  `bss_color` module param 0644 default 0 + `hi5622_bss_color_post_ap()`), one call in
  `hi5622_scan_op_start_ap` after the AP config, and the Kbuild line.
* Kernel build on the image #3 base: **0 warnings**, stripped artifact 382,404 B sha256
  `d87fbdcb...dcb`, 65 params, vermagic 6.18.60, .text identical after strip
  (evidence/build-artifact.txt).
* Host tests: 29/29 PASS, including the captured stock frame byte-exact (K1) and the
  hostapd `he_bss_color=25` vector (K2) - evidence/host-tests.txt.
* Fire rung: FIRE-RECIPE.md (runtime knob enable -> hostapd fixture with he_bss_color=25 ->
  dmesg post line + frame_post counter + on-air HE Operation differential).

* **Not done here (and why):** hooking our scan-completion path to post the observed-colour
  bitmap. Our scan path is a separate provider/offload lane; wiring it without a bench
  differential would claim an unverified mapping. The builder accepts any bitmap, so that
  wiring is a two-line follow-up once the frame is bench-accepted.

## 5. Limits

* The AP-start posting point and knob are our own design (fail-closed), not vendor
  behaviour [D].
* `[+0xFAC]` enum meaning, the chip callback body, and the on-air effect of the bitmap are
  [I]/[U] - settled only by the FIRE-RECIPE differential.
* Colour 0 is legal in the bitmap (vendor producer masks 6 bits) but the cfg80211 attribute
  range is 1..63; our AP-colour path therefore cannot post bit 0 (a scan-observed bitmap
  could).
