# wifi-5ghz-vap-msg - the band-1 VAP/user message decode and the fail-closed vendor-order patch

**Lane:** wifi-5ghz-vap-msg (shared task task-174). **HOST-ONLY**: this lane
disassembled local vendor artifacts and built host artifacts; it never
contacted the router, never flashed and never fired. The Lead fires.

**Why this lane exists.** The 5 GHz AP is blocked at one frame. On a fresh
chip the whole ladder works (regdomain US, boot ladder, C2 ctx_create +
txq_sync72, the 2-band wiphy), but the AP interface creation's firmware user
add wedges the chip: `announce NOT consumed (out[0]=0x8 after 51 polls)` ->
`h2d_timeout` -> the H2D lane closes -> the AP-start `vap_add` is refused ->
firmware exception mask `0x10` (data abort). The 2.4 GHz path with the same
driver code consumes the same message in 0 polls (ledger 2026-11-26 23:00).

**Source artifacts (local, never committed):**

| artifact | sha256 / size | use |
| --- | --- | --- |
| vendor host module `hi5622v100_wifi.ko` (2.5.24 build, **not stripped**, 17k symbols) | local copy under `recovery-extract/lib/hisilicon/ko/` | objdump decode, all `[V]` facts below |
| 5 GHz module under test `hi5622_wifi.ko` | 351,008 B stripped, sha256 `13366151...892d53`, 57 params | diff target; rebuilt here from `../wifi-5ghz-bringup/tools/build-5ghz-ready.sh` |
| firmware PD4 image | local, uncommitted | only cited via the committed `wifi-h2d-catalog` handler map |

Disassembler: the OpenWrt staging `arm-openwrt-linux-objdump`
(binutils 2.46.1, elf32-littlearm), `-dr` so relocations name every call.
Host module offsets below are link-time section offsets (`.text`).

---

## 1. Result in one paragraph

The vendor's CFG80211 `.add_virtual_intf` is a **stub**: it logs and returns
`-EOPNOTSUPP`; the vendor never creates a VAP/user from cfg80211, and it never
sends a g0 id7 user add at interface creation. Its VAP is created out of band
(VAP-create message with the band id at body+0x12 and chip at body+0x13), and
the **AP-start path is only** channel-config 0x0124 -> set-SSID 0x0005 -> auth
0x006D -> AP-config 0x0127 -> VAP-start 0x0065, each carrying the band byte
`*(u8 *)(dev+0x328)`. Our driver posts g0 id7 + g0 id8 from
`.add_virtual_intf` **before any firmware VAP exists**; on a single-band
(2.4 GHz) wiphy the firmware tolerates that ordering, but on the one-radio/
two-band (band5g=1) wiphy the band-1 AP's user add resolves a band/VAP context
that does not exist yet and the firmware takes a data abort. The patch
therefore does not "fix a field value" - the field decoding shows our g0 id7/id8
bodies are already the vendor bytes for band 0 - it fixes the **order**:
defer the interface-creation user add and post it after the AP's VAP-create
frame, at the point where the cfg80211 chandef names the band.

The vendor decode also corrects one inherited `[I]`: the header chip/band byte
of g0 id7/id8 is the **mac chip id** (0 - both logical bands live on chip 0,
`hmac_band_get_by_id` accepts chip 0 only), **not the band**. The band is
never carried by user_add/id8.

---

## 2. The vendor's band model (why user_add has no band field)

* `hmac_device_add_band` creates band id 0 then band id 1; `hmac_band_get_by_id`
  accepts **chip 0 only** (`wifi-5ghz/TABLES.md` T1/T4, re-checked here against
  the same module).
* `hmac_user_add` (`0x8e91c`) resolves the user's band from the **host VAP
  object**: it calls `hmac_band_get_by_vap_id(chip, vap_id)` and only then
  performs the per-VAP/per-band capacity checks.
* `hmac_config_add_user` (`0x8f0b4`) validates `hmac_dev+2 == 0`, resolves the
  mac chip by `hmac_dev[0]`, calls `hmac_user_add`, fills the user info and
  then sends the two 180-byte messages (`hmac_config_send_event(..., 178)` then
  `hmac_user_add_notify_alg`). There is no band operand anywhere in the call
  chain: the band is embedded in the VAP.

`wal_cfg80211_add_virtual_intf` (`0x103878`, 0x34 B): the whole body is an
`oam_error_log0` and `mvn r0,#10` = **-11 = -EOPNOTSUPP**. The vendor has no
add-interface wire path at all.

`wal_cfg80211_start_ap` (`0x10342c`): `r7 = *(u8 *)(dev_priv+0x328)` = the
band byte; the function calls the channel helper first (`0x102c7c`, emits
WAL/0x0124), then `wal_send_set_ssid_event(dev, band, ssid, len)` (`0x1280ac`),
then `wal_cfg80211_set_auth_mode` (`0x10137c`), then
`wal_cfg80211_fill_beacon_param(dev, settings+0x1c, body156)` (`0x10156c`),
sets `body+0x8f = 1` (start flag - the write at `sp+0x163` where the body
starts at `sp+0x10`), then writes `{u16 0x0127; u16 156; body}` through
`wal_cfg80211_start_ap_write_msg` (`0x103340`). The VAP-start 0x0065 writer
follows the same pattern. **No user add is in this path.**

---

## 3. g0 id7 `hmac_user_add` - wire layout `[V]`

Built by `hmac_user_add` (`0x8e91c`): `hcc_msg_alloc(168)` ->
`hcc_msg_tx(msg)`. 12-byte HCC header + **168-byte payload = 180 B total**
(agrees with `wifi-h2d-catalog` "#0xa8"). Fields written into the 180-byte
buffer (`r6` = header base):

| msg off | write | value |
| ---: | --- | --- |
| +0x00 | read-modify `bfc r2,#0,#4` | alloc nibble cleared (allocator owns the low nibble) |
| +0x01 | `bfi r1, hmac_dev[2], #4, #4` | **high nibble = hmac_dev+2** (0 in the validated path), low nibble preserved |
| +0x02 | `strb hmac_dev[1]` | hmac_dev+1 |
| +0x03 | `strb hmac_dev[0]` | **chip id** (0 for both logical bands) |
| +0x06 | `mov r1,#7` | message id 7 |
| +0x07 | 0 | - |
| +0x08 | 0 | - |
| +0x0c | `strh user+0xa84` | payload+0x00: u16 **user index** |
| +0x0e..13 | 6 x `strb` MAC | payload+0x02..0x07: user MAC |
| +0x9a | `strb sl` | payload+0x8e: **user type**, 2 = normal AP/VAP user (special association branch when `hmac_dev[4]==1`) |
| +0xb0 | `strb` 100 or scanned-BSS byte+6 | payload+0xa4: **RSSI**, +100 default |

Nothing else is written: **the 168-byte body has no band, channel, HT/VHT or
phy-mode field**. Our `vs_build_user_add` writes exactly uid/MAC/type/RSSI and
the header - byte-compatible.

## 4. g0 id8 `hmac_user_add_notify_alg` - wire layout `[V]`

Built by `hmac_user_add_notify_alg` (`0x8e40c`), also `hcc_msg_alloc(168)`,
180 B total:

| payload off | write | value |
| ---: | --- | --- |
| +0x00 | `strh user_idx` | u16 user index |
| +0x08 | `strh [hmac_dev+0x3e0]` | u16 alg/rate config (our `VS_ALG_OFF_ALG`) |
| +0x10 | `mac_user_get_vht_hdl(user+0xa6c, ...)` | 24-byte VHT capability block |
| +0x28 | `mac_user_get_ht_hdl(...)` | 44-byte HT capability block |
| +0x54 | `mac_user_get_he_hdl(...)` | 58-byte HE capability block |
| +0xa5 | `strb [user+0xaa6]` | u8 NSS |

Header identical to id7 except id byte `[6] = 8`; its [1]/[2]/[3] again come
from `hmac_dev[2]/[1]/[0]`. The capability blocks are the user's rate-set
handles, i.e. they are derived from the band of the VAP the user belongs to -
again not a field the host invents.

## 5. VAP-create parameters (`hmac_config_add_vap`, `0x15c0e8`)

`hmac_config_add_vap_check` (`0x15b900`) calls
`hmac_config_check_band_type` (`0x15b870`) and, on failure, logs the three
operands it validates: `params+0x11` (band **type**), `params+0x12` (band
**id**) and `params+0x13` (chip) - the same triple the firmware-side check
compares against `vap[0x11..0x13]`. `hmac_config_add_vap_init` (`0x15bac0`)
then allocates the host VAP object, writes the vap index to `params+4`, and
creates the VAP's user objects in one call
(`hmac_vap_add_multi_user`, `0x15b53c`). The wire carrier is
`hmac_config_add_vap_sync_all` (`0x15be44`).

This is the ordering fact: **the VAP (with its band) exists before its
users**. Our VAP-add builder already carries the band at `data[0x12]` and
leaves `data[0x13] = 0` (chip 0) - correct for band 1.

## 6. AP-start band-1 fields (committed decode, re-checked)

| step | carrier | band-1 field |
| --- | --- | --- |
| channel config | WAL cmd 0x0124 / g0 id1 key 0x0124, body 4 | body[0] = 1 (band), body[2] = 36 (IEEE channel) |
| set SSID | WAL 0x0005, body 36 | band passed as the call's band operand (`wal_send_set_ssid_event(dev, band, ...)`); no band byte in the body |
| auth | WAL 0x006D, body 4 | u32 nl80211 auth type; no band |
| AP config | WAL 0x0127, body 156 | band-gated HT/VHT hints at body+0x8a..0x8d and phy mode at +0x8e (5 GHz short-GI/VHT bits) |
| VAP start | WAL 0x0065, body 12 | start flag; no band |

Our builder of these five is band-driven from the cfg80211 chandef and matches
(see `../wifi-5ghz-bringup/TABLES.md` and `../wifi-5ghz/REPORT.md` 5.1/5.2).

## 7. Diff - our driver vs the vendor (band-1 AP path)

| # | message | vendor band-1 bytes | ours (5 GHz ready module) | verdict |
| ---: | --- | --- | --- | --- |
| 1 | g0 id7 user_add | chip id 0 in hdr[3], no band in body | posted at **add_interface**, hdr[3] = p->band (0 by accident under band5g=1), body same | **order defect** - vendor has no add_interface path at all |
| 2 | g0 id8 notify | chip id 0, real VHT/HT/HE blocks of the VAP's band | posted right after id7 at add_interface; capability blocks zero | same order defect; zero caps tolerated on 2.4, unverified on band 1 |
| 3 | vap_add | band id +0x12, chip +0x13=0 | data[0x12]=band, data[0x13]=0 | match |
| 4 | ap_chan | body[0]=band, body[2]=ch | band=p->band, ch resolved from chandef | match |
| 5 | ap_cfg | +0x8a..+0x8e band-gated | driven by p->band | match |
| 6 | vap_start | after ap_cfg | after ap_cfg | match |

The failing frame is therefore not a wrong byte but the wrong **sequence
position**: on a 2-band wiphy the band cannot be known at
`.add_virtual_intf` (cfg80211 supplies no chandef there; the vendor sidesteps
this by having one wiphy per band and creating the VAP out of band), so the
user add must be moved to `start_ap`, after the VAP-create that carries the
band.

## 8. The fail-closed patch (this lane)

New module parameter `vap5g` (uint, default **0** = off, one-shot meaning per
load): `0` = legacy bench-validated behaviour, `1` = vendor order for the
AP on a **multi-band wiphy**:

1. `.add_virtual_intf` (AP, wiphy has 2 bands): do **not** post g0 id7/id8;
   record `hcc_add_pending` and log the deferral.
2. `start_ap` beacon_static plan: the structural `vap_add` (band from the
   chandef) is still first; immediately **after** it succeeds the deferred
   user add + notify are posted (band is now known and the firmware VAP exists),
   then ap_chan/ap_ssid/ap_cfg/ap_vap_start proceed unchanged.
3. Everything else fails closed: `vap5g=0` = byte-identical legacy behaviour;
   `vap5g>1` = -EINVAL (never guess); single-band wiphy = legacy;
   non-AP interface = legacy; a deferred user_add that fails aborts the start
   before any AP frame (same rule as the existing provider gates).

The wire bytes of the deferred user add are the same builder bytes the legacy
path sends for band 0 (chip 0), which is exactly the vendor's encoding; the
band reaches the firmware through the preceding vap_add.

## 9. Evidence produced by this lane

| evidence | result |
| --- | --- |
| `tools/run-host-tests.sh` | host selftest of the order logic + the message bytes of the two frames (`evidence/host-tests.txt`) |
| `tools/build-vapmsg5g-ready.sh` | rebuilds the 5 GHz base from the committed lane chain, applies this patch, builds the 6.18.60 module, 0 warnings/errors, contract checks (`evidence/build-artifact.txt`): 58 params, `vap5g` present, vendor-order feature strings, debug-strip `.text` identical |
| patch applies after `build-5ghz-ready.sh` | checked by the same script from a clean `/tmp` |
| fire | **not fired by this lane**: recipe and PASS/FAIL in `FIRE-RECIPE.md` |

## 10. Limits / honest unknowns

* The vendor module is the **2.5.24** build; the firmware under test is PD4.
  The g0 id7/id8 layouts are host-sender facts and are stable across the
  project's two vendor builds (`wifi-5ghz/TABLES.md` notes same structures
  modulo layout).
* Why the 2.4 GHz firmware tolerates the legacy ordering while the two-band
  band-1 case aborts is a firmware-side inference `[I]`; the bench fire is the
  discriminator. The recipe records the exact dmesg either way; a FAIL stops
  the rung (no retry, power-cycle).
* The `[U]` leftovers are unchanged: `hmac_dev[1]` (our hdr[2] vip/vap byte)
  and `hmac_dev[2]` (hdr[1] dst) for slot>0 VAPs; the g0 id8 HE block content.
* Capability blocks: the vendor sends the VAP's own VHT/HT/HE handles; we send
  zeros. The 2.4 path is bench-validated with zeros, so the patch does not
  invent caps; if the band-1 fire still shows a notify wedge, the next rung is
  "fill id8 blocks from the band5g profile" (documented in the recipe).

## 11. Sources

* local vendor module `hi5622v100_wifi.ko` (2.5.24), link-time `.text`
  offsets in this report: `hmac_user_add 0x8e91c`,
  `hmac_user_add_notify_alg 0x8e40c`, `hmac_config_add_user 0x8f0b4`,
  `hmac_config_add_vap 0x15c0e8`, `...add_vap_check 0x15b900`,
  `...add_vap_init 0x15bac0`, `...add_vap_sync_all 0x15be44`,
  `...add_vap_multi_user 0x15b53c`, `hmac_config_check_band_type 0x15b870`,
  `wal_cfg80211_add_virtual_intf 0x103878`,
  `wal_cfg80211_start_ap 0x10342c`, `wal_cfg80211_fill_beacon_param 0x10156c`,
  `wal_cfg80211_start_ap_write_msg 0x103340`,
  `wal_send_set_ssid_event 0x1280ac`, `wal_cfg80211_set_auth_mode 0x10137c`.
* committed lanes: `../wifi-h2d-catalog/REPORT.md` (firmware g0 receive table),
  `../wifi-5ghz/{REPORT,TABLES}.md` (band model, 0x0124/0x0127),
  `../wifi-5ghz-bringup/{REPORT,FIRE-RECIPE}.md` + patches 0001/0002
  (the module under test), `../wifi-vap-start/TABLES.md` (g0 id1 carriers),
  `../wifi-stability/{REPORT,FINAL-INTEGRATION}.md` (the bench-proven
  5-message order).
