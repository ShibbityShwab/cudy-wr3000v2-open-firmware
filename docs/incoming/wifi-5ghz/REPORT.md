# REPORT - vendor 5 GHz bring-up reversal + band5g implementation

**Lane:** `wifi-5ghz` (shared task **task-145**), host-only clean-room.
**Written:** 2026-11-24. **Status:** static RE complete; patch + host tests +
compile-verify delivered; **bench ladder designed, not fired**.
**Sources (local, never committed):** vendor module 2.5.24
`/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` (3,564,728 B,
md5 `4737fcb2...`), vendor module 2.4.15
`/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko`,
firmware `/tmp/hi5622-patched/FIRMWARE.PD4.bin` (928,920 B, sha256
`cfe24bfb...`), project 6.18.60 kernel tree.  No device contact.
Re-derive every claim with [tools/band5g_re.py](tools/band5g_re.py); copies in
[evidence/](evidence/).

**Evidence markers:** [V] re-verified by this lane from the named local
artifact; [V*] device-measured by an earlier attended lane (cited); [I]
inference; [U] unknown.  All host-module addresses are link-time `.text` /
`.data` / `.rodata` / `.bss` offsets of the 2.5.24 build (the 2.4.15
build carries the same structures modulo layout); all firmware addresses are
CPU addresses (CA = file offset + 0x40000).

---

## 1. Headline

1. **There is no separate "5 GHz bring-up" message.**  The vendor driver is a
   per-band cfg80211 full-MAC: one wiphy per logical band, and the 5 GHz band
   is chosen (a) when the wiphy is registered with the 5 GHz band template,
   (b) by the **band byte** the existing AP-start channel config carries, and
   (c) implicitly by the **IEEE channel number** every channel-carrying
   message uses.  The firmware owns the 5 GHz PHY/beacon engine
   (`smac_tbtt_5g_isr` in the image) and selects it from those values.  [V]
2. **Both the host module and the firmware carry the same two static band
   tables** - 2.4 GHz 14 channels (1..14) and 5 GHz **29 channels** (36..64,
   100..144, 149..165, 4.9 GHz 184..196).  Host:
   `g_ast_channel_list_2G/5G` + `g_ast_freq_map_2g/5g` [V].  Firmware:
   CA 0x117B48 / 0x117AD4 [V].  The customer readout "29 channels incl. ch144
   and the 4.9 GHz block" is therefore not a userspace/INI artifact.
3. **Channel NUMBER <-> band-local INDEX** is a first-class firmware/host rule:
   `mac_get_channel_num_from_idx` (0..13 / 0..28, list position) and
   `mac_is_channel_num_valid` [V]; the firmware validator CA 0x10172C does
   the same against the same two tables [V].  The scan request channel entry
   (`entry+0x03`) carries that index - our old 19-channel 5 GHz list was
   wrong after ch140 and could not express ch144/149..165/4.9 GHz at all.
4. **The H1 band config (g0 id10) carries the channel NUMBER** and the
   firmware resolves it through its chip channel table
   (CA 0xD6A1E -> CA 0x94728 -> {count, stride, base} + valid bitmap).  The
   inherited note "5 GHz channel-id table [U]" is resolved: there is no such
   table; the old clean-room auto path sent the raw MHz value (5180) which the
   resolver cannot find.
5. **Domain -> band is not a module decision.**  The vendor creates two logical
   bands on chip 0 (band ids 0 and 1) and picks the band at VAP-creation time
   from the user-space command name (`g2` = band type 0, `g5` = band type 1)
   -> band id -> wiphy.  The PCI endpoints are two RF chips behind one host
   MAC, not two cfg80211 instances.  Our driver's "domain 0 = 2.4 GHz / domain
   1 = 5 GHz" split stays **[I]** and the ladder identifies the 5 GHz wiphy with
   `iw phy` before starting the AP.
6. **The 5 GHz wiphy profile is the missing enable.**  Vendor 5 GHz template:
   HT cap **0x0062** (20/40 + SGI20/40), VHT cap **0x00400064** (HTC_VHT |
   160 MHz | SGI80 | SGI160), VHT MCS map **0xfffa** (2 SS MCS0-9), max power
   23 dBm.  This lane exposes it behind one `band5g` parameter, keeps the
   default 0 = bench-validated behaviour, and makes 5 GHz AP start fail-closed
   otherwise.

---

## 2. Vendor dual-band model

### 2.1 Two logical bands on chip 0 (one host MAC)

`hmac_device_add_band` (`.text+0x93c2c`) is the only band creator: it calls
`hmac_band_get_by_id(chip, 0)` -> `hmac_band_init` -> `hmac_band_sync_add`
and then the identical sequence for band id 1 [V].  `hmac_band_get_by_id`
(`.text+0x13f6f4`) accepts **chip 0 only** (any other chip is logged and
rejected) and returns `g_ast_band + band_id*0x2330` (`g_ast_band` at
`.bss+0x49288`, exactly two 0x2330-byte objects) [V].  `hmac_band_init`
(`.text+0x13f880`) initialises the band object: `mac_band_init`,
`hmac_ap_channel_list_init`, `hmac_dfs_init` and the band **type byte at
band+0x10d1** [V].

### 2.2 Band type 0 = 2.4 GHz, 1 = 5 GHz

The band type byte is read by `hmac_band_id_to_band_type`
(`.text+0x1407b4`) [V].  `wal_cfg80211_init` (`.text+0x106a74`) runs one
wiphy per board band: `wiphy_new_nm` -> `hmac_band_id_to_band_type` ->
type 0: `hmac_device_check_2g_enable` then `wiphy->bands[0]` = the 2.4 GHz
template; type 1: `hmac_device_check_5g_enable` then `wiphy->bands[1]` =
the 5 GHz template; then `wiphy_apply_custom_regulatory` + `wiphy_register`
[V].  The board band list is `local board` config data (band count at
board+6, ids at board+7+i) [V]; the module itself does **not** derive the band
from the PCI domain.

The user-space path confirms the operator-selected band: the vendor's private
VAP-create command parser (`wal_hipriv_add_vap`, `.text+0x147b7c`) matches
the command argument strings `g2` (0x6732) and `g5` (0x6735), sets the band
type 0/1, calls `hmac_band_type_to_band_id` (`.text+0x14067c`, scans the two
band objects' type bytes) and `hmac_band_get_wiphy` [V].  The WAL add-VAP
body then carries band id (body+0x12) and chip id (body+0x13); the firmware
AP-side check `hmac_config_check_band_type` (`.text+0x15c0e4`) compares
`vap[0x11]` (band type) with the band object's type and `vap[0x12]`
(band id) / `vap[0x13]` (chip id) [V].

### 2.3 The two RF chips

`hmac_board_init` (`.text+0x94da4`) loops over
`oal_bus_get_chip_num()` PCI chips and initialises a per-chip device
(`get_chip_type` -> `hmac_band_module_init` -> `hmac_device_init` ->
`mac_chip_init`) [V].  The cfg80211 band objects, however, exist only on
chip 0 (`hmac_band_get_by_id` rejects chip != 0) [V]; the second PCI endpoint
is the second RF front-end, selected by the band/chip fields of the
firmware-bound configuration, not a second cfg80211 instance.  Which physical
endpoint serves which band is firmware/INI state and stays **[U]** here.
Bench-observed stock `WIPHY=2` [V*] is consistent with the two logical bands
on one host MAC.

**Consequence for our driver.**  We allocate one wiphy per PCI endpoint; two
endpoints give the same observable two wiphys and one band each, but the
domain->band association is **[I]**.  `band_force` selects explicitly, and
the ladder's first rung prints each wiphy's bands so the 5 GHz one is
identifiable before any AP start.

---

## 3. The band tables (host and firmware, identical data)

| table | 2.4 GHz | 5 GHz |
| --- | --- | --- |
| host channel list | `g_ast_channel_list_2G` `.data+0x3b8`, 14 x 4 B `{u8 chan; u8 0xff; u16 0}` | `g_ast_channel_list_5G` `.data+0x3f0`, 29 x 4 B, same shape |
| host frequency map | `g_ast_freq_map_2g` `.rodata+0x918`, 14 x 4 B `{u16 freq; u8 chan; u8 0}` | `g_ast_freq_map_5g` `.rodata+0x8a4`, 29 x 4 B |
| wiphy band template | `.data+0xdf8` (92 B) | `.data+0xe54` (92 B) |
| wiphy channels | `.data+0x16e4` (14 x 56 B) | `.data+0x108c` (29 x 56 B) |
| firmware channel table | CA 0x117B48, 14 x 4 B | CA 0x117AD4, 29 x 4 B |

Channels [V]:

* 2.4 GHz: 1..14 (2412..2472 + 2484).
* 5 GHz: 36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132 136
  140 **144** 149 153 157 161 165 **184 188 192 196**.
  Frequencies: 5000 + 5*ch for the 5 GHz channels, **4000 + 5*ch** for the
  4.9 GHz block (5720 = ch144, 4920..4980 = ch184..196).
* The 4-byte channel-list entries carry the enable byte 0xff at +1 on both
  bands; `mac_is_channel_num_valid` treats 0xff as "not enabled" (a runtime
  regulatory/INI path fills it) [V].

The 29-channel list is the same order as the static wiphy channel array
(`.data+0x108c`) and as the firmware table, i.e. the **band-local index is
the table position**: ch36 -> 0, ch140 -> 18, ch144 -> **19**, ch149 -> **20**,
ch165 -> 24, ch184 -> 25, ch196 -> 28 [V].

---

## 4. Channel-number <-> index semantics (why the old scan map was wrong)

Host:

* `mac_get_channel_num_from_idx(band, idx, *chan)` (`.text+0x1f42c`):
  band 0 requires idx <= 13, band 1 requires idx <= 28; returns list[idx].chan
  [V].
* `mac_is_channel_num_valid(band, chan)` (`.text+0x1f684`): scans the 14/29
  entry list, then checks the entry's enable byte (0xff = not enabled) [V].

Firmware:

* Channel validator CA 0x10172C(band, channel, *idx): band 0 count 14 / table
  CA 0x117B48; band 1 count **29** / table CA 0x117AD4; linear search, returns
  0 and the table position or 0x3e8 [V].
* The channel-config worker CA 0x841B8 calls it before storing the module
  channel context [V].

Our previous `hi5622_vapscan_map.c` used a hand-written 19-channel 5 GHz
frequency list and its position as `idx`.  That is correct only for ch36..140
and cannot represent ch144/149..165/4.9 GHz (scan of those channels failed with
-EINVAL).  The patch uses the lane's 14/29 table (same data) for both
`num` and `idx`.

---

## 5. The 5 GHz AP-start path (no new message)

### 5.1 Where the band comes from

`wal_cfg80211_start_ap` (`.text+0x103634`): the band byte is
`*(u8*)dev_priv` (`dev+0x328`) - the per-band wiphy private area; it then
calls the **channel helper first** (`.text+0x102e84`, direct BL), then
set-SSID, auth, beacon-param fill and the AP-config + VAP-start writer [V].
The channel helper validates the channel
(`mac_is_channel_num_valid`), resolves width
(`wal_cfg80211_convert_width_to_value` /
`..._convert_value_to_vht_width`), runs the DFS check
(`wal_cfg80211_set_chan_dfs_check`) and sends a cfg event through
`wal_send_cfg_event` carrying command **0x0124** [V].

The 0x0124 body is 4 bytes (already decoded by the beacon-static lane [V*],
re-verified here): `[0] band (0 = 2.4 GHz, 1 = 5 GHz), [1] vendor mac_vap+2
[I], [2] channel number (hw_value), [3] unused`.  The firmware worker
CA 0x841B8 stores `body[2] -> module ctx+0x10 (channel), body[0] -> ctx+0x11
(band), body[1] -> ctx+0x12, valid flag -> ctx+0x13` and then programs the
PHY/band channel (CA 0xFDCE4 family) [V].  So on a 5 GHz wiphy our existing
`beacon_static` channel-config builder already emits band = 1 and the IEEE
channel - what was missing was the wiphy profile and the fail-closed gate, not
a new message.

### 5.2 AP config (0x0127) band gates

`wal_cfg80211_fill_beacon_param` (`.text+0x101774`) reads the same per-band
byte and copies only band-gated HT/VHT hint bits into the 156-byte AP-config
body: body[0x8a] HT short-GI-20, body[0x8b] HT short-GI-40 (5 GHz only),
body[0x8c/0x8d] VHT SGI80/SGI160 (5 GHz only) [V].  Our inherited builder
already sets those from `p->band`, and `hi5622_scan_fill_ap_start` derives
`p->band` from `chandef.chan->band` - so the 5 GHz AP config body is driven
by the same chandef.  No host beacon template exists: firmware SMAC owns
beacon TX (beacon-static REPORT T6; firmware own `smac_tbtt_2g_isr` /
`smac_tbtt_5g_isr`) [V].

### 5.3 The VAP add band field

The vendor VAP-create WAL body carries the band id at body+0x12 (decoded above
from `wal_hipriv_add_vap`/`fill_param`) [V]; our g0 id1 key 0x0064
VAP-add builder already maps `data[0x12] = band` from the endpoint band, and
the channel/H1 frames carry the band-local index.  On the 5 GHz endpoint
`band==1` flows into VAP add, user add (hdr chip id), channel config and AP
config consistently.

---

## 6. H1 band config (g0 id10) on 5 GHz - the resolver chain

The inherited note in `hi5622_band_cfg.h` called the payload+0x02 u16 a
"channel id looked up by the firmware via CA 0xD6A1E".  Disassembly resolves
the semantics:

1. H1 handler CA 0x5FF64 reads `payload+0x0e` (i.e. header+0x0c+2) as u16 and
   calls CA 0xD6A1E [V].
2. CA 0xD6A1E is a thunk: `mov r1, r0; movs r0, #0; b.w 0x94728` [V].
3. CA 0x94728 gets the chip resource (CA 0x943AC, chip 0 only), then:
   * CA 0xC1FBA: valid-channel bitmap test
     (`count > idx`, bit `idx` set in `[+8]`),
   * CA 0xC1FF4: element lookup `base[+0x14] + stride[+4] * idx` on a
     `{u16 count; u32 stride; ...; u32 base}` descriptor [V].
4. The returned per-channel object is used as the context base: the handler
   bounds the slot index to <= 7 and writes into
   `base + 0xEC + slot*0x20` (`+0x103` mode, `+0x104` field10,
   `+0x106` start, `+0x15` compare) [V].

Because payload+0x0E is used as a **bit index / table index**, and because the
H1 subtype-1 payload has no separate band field, the value is the **global
IEEE channel number** (2.4 GHz 1..14, 5 GHz 36..196): it both selects the band
and the per-channel object.  The firmware's 14/29 tables are the validation/
index lists for that number space (CA 0x10172C).  Therefore:

* Sending 5180 (the old auto path in `hi5622_scan_post_band_cfg`) cannot
  resolve; sending 36 does.
* No 5 GHz-specific channel-id table needs decoding - the [U] is closed by the
  channel-number rule.

The patch makes the H1 auto path map the scan frequency to the vendor IEEE
channel number for both bands and keeps 5 GHz H1 config fail-closed behind
`band5g`.

---

## 7. The 5 GHz wiphy profile (vendor static capability words)

| word | 2.4 GHz | 5 GHz |
| --- | --- | --- |
| HT cap | 0x0062 | **0x0062** (SUP_WIDTH_20_40 | SGI20 | SGI40) |
| HT ampdu | factor 0, density 0 | same |
| VHT supported | 1 | 1 |
| VHT cap | 0x00400000 (HTC_VHT) | **0x00400064** (HTC_VHT | 160 MHz | SGI80 | SGI160) |
| VHT MCS map | 0xfffa (2 SS MCS0-9) | 0xfffa |
| max power in template | 30 | 23 (live 23/24 after regdomain [V*]) |

The static HT MCS `rx_mask` is all zero in the template [V]; a shipping
wiphy cannot use that, so (as the sta-static lane concluded) the vendor fills
it at runtime.  This lane does **not** copy a zero MCS mask: the patched
5 GHz profile keeps the driver's 0xff mask and adopts only the capability
words/tables.

---

## 8. Implementation delivered

[patches/0001-band5g.patch](patches/0001-band5g.patch) applies with
`patch -p1` on the FINAL ALL-PATCH tree (the module validated on the bench:
308,080 B stripped `5b587949...`) and adds:

| file | change |
| --- | --- |
| `hi5622_band5g.h/.c` (new) | portable 14/29 vendor tables + freq<->channel<->index helpers; AP channel gate; H1 band-config channel derivation; 5 GHz cap words. Pure C99, host-tested. |
| `hi5622_cfg80211.c` | `band5g` module parameter (default 0); when 1 and the endpoint band is 5 GHz: 29-channel vendor table, HT 0x0062, VHT 0x00400064 + 0xfffa MCS map in the registered wiphy; profile log; `cfg80211_status` shows `band5g`; final log reports the active channel count. Explicit `band_vendor`/`ht_mode`/`vht_cap` still take precedence when set. |
| `hi5622_scan.c` | start_ap: band-aware channel gate before any frame (5 GHz requires `band5g=1` and a vendor-table channel; 2.4 GHz unchanged). H1 band config: derive the IEEE channel number from the scan frequency for both bands (5 GHz 36..196, 4.9 GHz block included) instead of forwarding the raw MHz value; fail closed when the frequency is not in the vendor table; 5 GHz H1 gated by `band5g`. |
| `hi5622_vapscan_map.c` | scan request channel entries use the vendor 14/29 tables: `num` = IEEE channel number, `idx` = vendor table position (0..13 / 0..28). Fixes ch144/149..165/4.9 GHz (old code returned -EINVAL) and the index mismatch after ch140. |
| `Kbuild` | `hi5622_band5g.o` |

**Parameter contract (all defaults change nothing on the bench):**

| parameter | default | meaning |
| --- | ---: | --- |
| `band5g` | **0** | 0 = bench-validated behaviour; 5 GHz AP start and 5 GHz H1 band config are fail-closed (-EOPNOTSUPP). 1 = register the 5 GHz endpoint wiphy with the vendor 29-channel/HT 0x0062/VHT 0x00400064 profile and allow the 5 GHz AP-start/H1 rungs. |

Fail-closed chain for a 5 GHz AP start with `band5g=0`: cfg80211
`.start_ap` -> `hi5622_band5g_ap_chan_prepare()` -> -EOPNOTSUPP, no VAP
base, no channel config, no AP frame, no firmware write (the inherited
`allow_frames` gate remains the final fence).  With `band5g=1` the
channel is additionally required to be in the vendor 29-entry table.

---

## 9. Host verification performed

* **Host selftest: 98 checks, 0 failures** - tables/order/frequencies,
  freq<->channel<->index roundtrip for all 43 channels, negative cases
  (invalid band/frequency/channel, ch68 not in table), the `band5g` AP gate
  (2.4 GHz unchanged, 5 GHz -EOPNOTSUPP while 0, ch36/144/184 accepted while 1,
  channel/frequency mismatch refused), H1 channel derivation
  (2412->1, 2484->14, 5180->36, 5745->149, 4920->184, raw-MHz not forwarded),
  capability words.  Evidence: [evidence/host-selftest.txt](evidence/host-selftest.txt).
* **Static-RE probes: 73 checks, 0 failures** - 28 on 2.5.24, 28 on 2.4.15,
  17 on the PD4 firmware (band tables, wiphy templates, dual-band model,
  g2/g5 selection, channel index/validity rules, AP-start helper chain, H1
  handler + resolver chain, AP channel worker, 5 GHz TBTT ISR).  Evidence:
  [evidence/band5g-re-2.5.24.txt](evidence/band5g-re-2.5.24.txt),
  [evidence/band5g-re-2.4.15.txt](evidence/band5g-re-2.4.15.txt),
  [evidence/band5g-re-firmware.txt](evidence/band5g-re-firmware.txt).
* **Patch apply: clean** `patch -p1 --dry-run` on the regenerated final
  all-patch integration tree.  Evidence:
  [evidence/patch-apply.txt](evidence/patch-apply.txt).
* **Compile-verify** (host-only, 6.18.60 tree, OpenWrt GCC 14.4.0):
  **0 warnings/errors**, cfg80211 contract 10 symbols + `cfg80211_bss_iter`,
  **48 params** (47 chain + `band5g`), vermagic
  `6.18.60 SMP mod_unload ARMv7 p2v8`, `strip` leaves `.text`
  byte-identical.  Artifact (local evidence only):
  stripped **312,432 B sha256 `2bee4391...`**, unstripped 1,150,092 B
  sha256 `2b8ff159...`.  Evidence:
  [evidence/build-verify.txt](evidence/build-verify.txt),
  [evidence/artifact-identity.txt](evidence/artifact-identity.txt).

**What this does NOT prove:** no 5 GHz frame has been on air from this patch.
The ladder in [TEST-LADDER.md](TEST-LADDER.md) is designed, not fired.

---

## 10. Limits / open items

* **Domain -> band mapping stays [I].**  The vendor chooses the band by
  user-space command (g2/g5), not by PCI domain; the module only knows the
  board's band list.  Our driver keeps `band_force` and `band5g`, and the
  first ladder rung (G1) identifies the 5 GHz wiphy from its advertised bands.
* **On-air proof is the ladder's job** (G3/G4): AP-ENABLED + channel config
  evidence only says the firmware accepted the configuration.
* **VHT 160 MHz is advertised, not programmed.**  Channel-width programming
  (AP-config width fields / `hmac_config_set_bw` family) is out of scope
  here; the ladder's first 5 GHz AP is 20 MHz.
* **DFS/CAC** channels (52..144) are in the vendor table and the wiphy; the
  kernel regdb and hostapd CAC rules still apply.  The first AP rung uses
  non-DFS ch36.
* **HE (11ax)** capability is not in these static tables (runtime/INI in the
  vendor) - not covered.
* **Which RF chip serves which band is [U]** (firmware/INI state); the module
  exposes no static mapping.  The two logical bands are both on chip 0 [V].
* **Vendor AP management stays host-software** (`wal_cfg80211_mgmt_tx`
  family); client assoc/auth on 5 GHz inherits the same mgmt-tx + RX blockers
  documented by wifi-beacon-static/wifi-ap-mgmt - independent of band.

---

## 11. Evidence marker summary

| claim group | marker | re-derived by |
| --- | --- | --- |
| host band tables/freq maps/templates | [V] | band5g_re.py H-probes 2.5.24 + 2.4.15 |
| dual-band model, band type 0/1, g2/g5 | [V] | band5g_re.py H5-H9 |
| channel index/validity semantics | [V] | band5g_re.py H10-H12 |
| AP-start band source + 0x0124 helper | [V] | band5g_re.py H12 |
| firmware band tables/validator/H1 resolver | [V] | band5g_re.py F1-F5 |
| firmware 5 GHz beacon engine | [V] | band5g_re.py F6 (name) + beacon-static T6 |
| observable two wiphys / 5 GHz 160 MHz live | [V*] | wifi-sta-static REPORT sec 4 (bench) |
| domain 0 = 2.4 GHz / domain 1 = 5 GHz | [I] | not provable from the module |
| which RF chip serves which band | [U] | firmware/INI state |
