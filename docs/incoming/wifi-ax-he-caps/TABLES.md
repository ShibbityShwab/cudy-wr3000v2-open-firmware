# wifi-ax-he-caps - evidence tables (T1-T10)

All host-module addresses are **link-time section offsets** of the pinned local
vendor module `hi5622v100_wifi.ko` (unstripped; **3,581,748 B**, sha256
`22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2` - local
fixture only, never committed).  Module version per the project fixture is the
2.5.24 OpenWrt vendor build.  Reproduce any row with the OpenWrt objdump:

    OBJ=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump
    $OBJ -dr --section=.text --start-address=<addr> --stop-address=<addr+size> \
        /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
    $OBJ -s -j .rodata --start-address=<off> --stop-address=<off+len> <ko>   # values, not committed
    $OBJ -h <ko>                                                            # section layout

No vendor object byte, firmware byte, calibration value or secret is
reproduced here - only derived field maps, addresses and the capability
**values** that our clean-room driver is going to advertise.

Markers: **[V]** = decoded from the vendor host module, reproducible with the
command above; **[I]** = inferred from surrounding code (state the residual
doubt); **[U]** = unknown / not decoded; **[H]** = hostapd source (the shipped
open-source hostapd 2026.08.07 tree) or the 6.18 cfg80211 tree in this repo.

## T1. Where the vendor advertises wiphy HE capabilities [V]

`wiphy_new_nm` / `wiphy_register` are the only cfg80211 registration calls in
the module; `wal_cfg80211_init` (0x10686c, size 0x490) is the registration
function.

| item | evidence |
| --- | --- |
| wiphy_new_nm(&ops, priv=4, NULL) | call @0x1069a8 (ops pointer `.data+0xeb0`) |
| 2.4 GHz band object | `str rX,[wiphy,#0xcc]` ← `.data+0xdf8` (`g_st_supported_band_2ghz_info`, 92 B) when `hmac_device_check_2g_enable()==1` (@0x106ab0-0x106abc) |
| 5 GHz band object | `str rX,[wiphy,#0xd0]` ← `.data+0xe54` (92 B, the 5 GHz twin) when `hmac_device_check_5g_enable()==1` (@0x106ac8-0x106ad8) |
| wiphy->max_scan_ssids = 12 | `strh #12,[wiphy,#32]` @0x106a08 |
| wiphy->perm_addr = 6 B | `memcpy_s(wiphy,6,sp+30,...)` @0x1069fc |
| wiphy_apply_custom_regulatory + wiphy_register | @0x1068f4 / @0x106900 |

The band object size 92 B and every later offset match the **5.10**
`struct ieee80211_supported_band` in `build/linux-kbuild/linux-5.10.201`
(T10 layout): `channels` +0x00, `bitrates` +0x04, `band` +0x08,
`n_channels` +0x0c, `n_bitrates` +0x10, `ht_cap` +0x14 (22 B),
`vht_cap` +0x24 (16 B), `s1g_cap` +0x34 (16 B), `edmg_cap` +0x44 (8 B),
`n_iftype_data` +0x54 (u16), `iftype_data` +0x58 (pointer).

## T2. The vendor's HE iftype_data [V]

Both band objects carry **one** iftype-data entry (the `n_iftype_data` word at
object+0x54 is 1); the pointer at +0x58 is an ABS32 relocation into `.rodata`:

| band | band object | n_iftype_data | iftype_data target |
| --- | --- | ---: | --- |
| 5 GHz | `.data+0xe54` | 1 | `.rodata+0x9448` |
| 2.4 GHz | `.data+0xdf8` | 1 | `.rodata+0x9484` |

Entry stride is 0x3c = 60 B = `sizeof(struct ieee80211_sband_iftype_data)` in
5.10 (`u16 types_mask` + `struct ieee80211_sta_he_cap` (56 B) +
`struct ieee80211_he_6ghz_capa` (2 B), align 2).

## T3. The vendor's advertised HE capabilities - decoded values [V]

Per-entry layout (5.10): `+0x00` types_mask u16; `+0x02` has_he bool;
`+0x03..0x08` HE MAC cap (6 B); `+0x09..0x13` HE PHY cap (11 B);
`+0x14` HE-MCS/NSS set (12 B: rx/tx 80, rx/tx 160, rx/tx 80+80);
`+0x20..0x38` PPE thresholds (25 B); `+0x3a` HE 6 GHz capa (u16).

| field | 2.4 GHz entry | 5 GHz entry | note |
| --- | --- | --- | --- |
| types_mask | BIT(3) = **AP only** | BIT(3) = **AP only** | station iftype data absent |
| has_he | 1 | 1 | |
| HE MAC cap | `01 00 00 00 00 00` | `01 00 00 00 00 00` | byte0 bit0 = HTC HE |
| HE PHY cap | `02 00 ..00` (11 B) | `0c 00 ..00` (11 B) | 0x02 = 40 MHz in 2.4 GHz [H: HE_PHYCAP_CHANNEL_WIDTH_SET_40MHZ_IN_2G]; 0x0c = 40/80 + 160 MHz in 5 GHz |
| HE-MCS/NSS 80 MHz | rx 0xfffa, tx 0xfffa | rx 0xfffa, tx 0xfffa | 2 SS, MCS 0-9 |
| HE-MCS/NSS 160 MHz | rx 0xffff, tx 0xffff | rx 0xfffa, tx 0xfffa | 2.4 GHz does not advertise 160 MHz |
| HE-MCS/NSS 80+80 | 0xffff / 0xffff | 0xffff / 0xffff | not supported |
| PPE thresholds | all zero | all zero | PPE-present bit clear -> IE carries no PPE field |
| HE 6 GHz capa | 0 | 0 | chip has no 6 GHz band (this is correct, not a gap) |

The three `wiphy_register` fields + this entry are the entire cfg80211 HE
surface of the vendor driver: there is no wiphy-level iftype data and no
6 GHz sband.  This is exactly the "vendor wiphy HE cap advertisement" that our
patch has to reproduce (**AX-1**).

## T4. Vendor host-side HE capability source and IE builders [V]

| function | offset | size | role |
| --- | ---: | ---: | --- |
| `mac_vap_init_mib_11ax` | 0x2d7ac | 0x798 | initialises the per-VAP 11ax MIB HE-cap bytes (the source for the host-built HE Capabilities IE) |
| `mac_set_he_channel_width` | 0x2cef8 | 0x8c | sets the MIB PHY-cap channel-width subfield (2.4 GHz: 20/40 -> 1/2; 5 GHz: 20/40/80/160 -> 2/6) |
| `mac_fill_he_cap_ie` | 0x245d4 | 0xdf0 | builds the HE Capabilities element body from the VAP MIB (mac caps + phy caps + mcs map; the sparse caps of T3) |
| `mac_set_he_capabilities_ie` | 0x253c8 | 0x1c0 | gate + wrapper: only when the VAP MIB is HE-initialised; tail-calls `mac_fill_he_cap_ie` |
| `mac_set_he_operation_params` | 0x25588 | 0x7c | builds the 3-byte HE Operation Parameters from the MIB (default PE duration, TWT, ER-SU, BSS color) |
| `mac_set_he_operation_params` -> `mac_set_bss_color_info` | 0x25604 | 0x80 | BSS Color Information byte |
| `mac_set_basic_he_mcs_nss_set` | 0x25684 | 0x48 | Basic HE-MCS/NSS set = `ff ff 00` (all rates basic) |
| `mac_set_he_opern_ie` | 0x26218 | - | assembles the HE Operation element |
| `hmac_mgmt_encap_asoc_rsp_ap` | 0x9ef10 | - | AP: adds HE Cap + HE Operation to the (re)association response (calls @0x9f64c / @0x9f664) |
| `hmac_mgmt_encap_asoc_req_sta` | 0xa0fa0 | - | STA: adds HE Cap to the association request (@0xa1338) |

MIB HE-cap byte map (inside the object at `[vap+0x18c]`; MIB+0x198 is the
HE MAC byte0): **HE MAC cap = MIB+0x198..0x19d (6 B), HE PHY cap =
MIB+0x1a0..0x1aa (11 B), MIB+0x1ac.. = PPE/threshold mask,
MIB+0x1c8 = "11ax MIB initialised" flag** [V: `mac_vap_init_mib_11ax`
stores and `mac_fill_he_cap_ie`/`hmac_proc_he_cap_ie` read the same
offsets].  The values set are the T3 values (band byte 0 -> PHY[0]=0x02;
band 1 -> PHY[0]=0x0c).

**Important negative result:** neither `mac_set_he_capabilities_ie` nor
`mac_fill_he_cap_ie` is reachable from the beacon path.  Beacons are
firmware-generated and hostapd's beacon IEs are only mined for the 156-byte
AP-config hints (../wifi-beacon-static T6; ../wifi-ap-start hi5622_ap_frames.h).
So the vendor never sends a HE template to the firmware: HE on air is a
firmware effect of the AP-config `wireless mode = 10 (HE)` byte, which our
driver derives from the *presence of an extension IE (0xFF)* in hostapd's
beacon tail (../wifi-ap-start TABLES T7).

## T5. The user_add g0 id8 capability blocks (what AX-2 must fill) [V]

`hmac_user_add_notify_alg` (0x8e40c, size 0x1c4) builds the 180-byte g0 id8
message.  Body starts 12 bytes into the HCC header; the three capability
blocks are copied **verbatim from the per-user hdl objects**:

| payload | wire offset | size | source (mac_user object offsets) | getter |
| --- | ---: | ---: | --- | --- |
| VHT capabilities | +0x10 | 24 B | user+0x7c | `mac_user_get_vht_hdl` @0x297f0 |
| HT capabilities | +0x28 | 44 B | user+0x50 | `mac_user_get_ht_hdl` @0x29c98 |
| HE capabilities | +0x54 | 58 B | user+0x94 | `mac_user_get_he_hdl` @0x29f28 |

All three getters are `memcpy_s(out, <size>, user+<off>, <size>)`; the hdl
region is contiguous: HT 0x50(+44)=0x7c VHT (+24)=0x94 HE (+58)=0xce [V].
The id8 header fields are the 5ghz-vap-msg T3 set (dst/chip nibbles, id byte
0x08); our driver currently sends this message with the whole capability
region zero.

## T6. The HE hdl (58 B @user+0x94, id8 payload+0x54) layout [V]

`hmac_proc_he_cap_ie` (0x43100) parses a peer HE Capabilities element and
updates the hdl through `mac_ie_parse_he_cap` (0x217f4) + `mac_user_set_he_hdl`
(0x29f90); `mac_ie_proc_he_opern_ie` (0x222b0) caches the peer HE Operation
element into the same hdl.

| hdl offset | size | content | evidence |
| ---: | ---: | --- | --- |
| +0x00 | 2 | prelude, **preserved** across a caps update | caller copies the parsed 37 B block to hdl+2, not hdl+0 (@0x431e8) |
| +0x02 | 6 | HE MAC cap (from the peer's HE Cap IE) | `mac_ie_parse_he_cap` writes out+0..5 = IE he_cap_elem.mac_cap_info |
| +0x08 | 11 | HE PHY cap | out+6..16 |
| +0x13 | 12 | HE-MCS/NSS set (80/160/80+80 rx+tx) | out+17..28; the parser copies 4 B (80 MHz only) or 8 B (adds 160 MHz) by the PHY cap width bits, 12 B capacity |
| +0x1f | 8 | PPE threshold subset | out+29..36 (masked by the local PPE mask; only 8 of the 25 PPE bytes are carried in the hdl) |
| +0x27 | 9 | HE Operation element cache (params 3, BSS color info 1, basic MCS/NSS 2, tail 3) | `mac_ie_proc_he_opern_ie` memcpy_s(hdl+39, 9, parsed9) |
| +0x30 | 10 | residual [U] - initialised but not touched by either path above; likely TWT/sounding/OM state | not decoded |

The HE Capabilities element layout the parser consumes is the standard one:
EID 0xFF, len, ext-id 0x23, MAC 6, PHY 11, MCS 12 (variable by PHY width
bits), optional PPE.  MCS/PPE sizes are the same rule hostapd uses (T8).

## T7. HT 44 B and VHT 24 B hdl blocks - block map [V]/[I]

| hdl | offset | size | source / parse | notes |
| --- | ---: | ---: | --- | --- |
| HT | user+0x50 | 44 | `hmac_proc_ht_cap_ie` @0x42bfc (HT Cap IE 45) | +0x52 (u16) HT cap word; +0x55 ampdu params; +0x57..0x60 RX MCS mask (10 B); +0x61 TX MCS highest; parse also forwards TX-beamforming words @0x42d40; remaining bytes are our-rate bookkeeping [I] |
| VHT | user+0x7c | 24 | `hmac_proc_vht_cap_ie` @0x42efc + `hmac_parse_vht_cap_field` @0x42db0 (VHT Cap IE 191) | +0x04 32-bit VHT cap word assembled from IE cap fields; +0x0c/+0x0e RX MCS map/highest; +0x10/+0x12 TX MCS map/highest; +0x14 VHT-capable flag (set 1 when the IE is accepted); +0x00/+0x02 a computed RX-highest word keyed by the map type [V] |

Both blocks are copied verbatim into id8 by T5.  A future AX-2 patch fills
them from the 6.18 `ieee80211_sta_ht_cap`/`ieee80211_sta_vht_cap` advertised
on the wiphy (the 6.18 cfg80211 is the same open format as the 5.10 one these
parsers target) - not in this lane.

## T8. The hostapd gate this patch unlocks [H]

Shipped hostapd is OpenWrt `hostapd-mini`/SAE build of hostapd 2026.08.07
(ARM APKs, `hostapd` 1.2 MB SAE build / 593 KB mini build); both binaries
contain the `CONFIG_IEEE80211AX` code (strings `ieee80211ax=%d`,
`disable_11ax`, "Cannot enable multiple BSSID..."):
OpenWrt's Makefile sets `HOSTAPD_IEEE80211AX=y` from `CONFIG_DRIVER_11AX_SUPPORT=y`
in the target config, so the .config comment is not the shipped state.

| hostapd behaviour | code |
| --- | --- |
| per-band HE caps come from cfg80211 iftype data | `src/drivers/driver_nl80211_capa.c` `phy_info_iftype_copy`: `NL80211_BAND_IFTYPE_ATTR_IFTYPES` flag for AP -> `he_capab->he_supported = 1`, then HE_CAP_MAC/PHY/MCS_SET/PPE attrs |
| AP with `ieee80211ax=1` and no driver HE caps aborts | `src/ap/hw_features.c` `hostapd_check_ht_capab`: "Driver does not support HE" -> -1 |
| 2.4 GHz HT40 + AX requires PHY[0] 40MHZ_IN_2G | `src/ap/hw_features.c` @455 (our 0x02 satisfies it) |
| HE elements in beacon/probe-response | `src/ap/beacon.c` @949/@2500 -> `hostapd_eid_he_capab` + `hostapd_eid_he_operation` (HE Capabilities + HE Operation + MU EDCA + SR + 6 GHz band cap when 6 GHz) |
| HE Capabilities IE size | `src/ap/ieee802_11_he.c`: `ie_size = 17 + mcs_nss_size + ppet_size`; `mcs_nss_size = 4` + 4 if 160 MHz bit + 4 if 80+80 bit; PPE size 0 when the PPE present bit is clear |
| HE config security constraints | `src/ap/ap_config.c` @1434/@1522: WEP disables HE; WPA without CCMP/GCMP disables HE (WPA2-CCMP is fine) |

**Consequence for our driver:** advertising the T3 caps for the **AP** iftype
is sufficient and necessary for hostapd's gate; hostapd then emits the HE
Capabilities IE into the beacon head/tail it sends to our `start_ap`.  Our
existing AP-config builder maps "extension IE present in the tail" to
`body[0x8e] = 10 (HE wireless mode)` (../wifi-ap-start TABLES).  Whether the
firmware's own beacon then carries the IE is exactly what the fire evaluates.

## T9. Patch contract (the fail-closed module parameter) [H]

| input | `he_caps=0` (default) | `he_caps=1` |
| --- | --- | --- |
| wiphy iftype_data | none set (byte-identical to the current module) | AP-only entry per installed band |
| 2.4 GHz entry | - | T3 values: PHY0 0x02, MCS 80 0xfffa, 160/80+80 0xffff |
| 5 GHz entry | - | T3 values: PHY0 0x0c, MCS 80/160 0xfffa, 80+80 0xffff |
| other bands | - | never advertised (no 6 GHz band exists) |
| hostapd `ieee80211ax=1` | "Driver does not support HE", AP fails closed | HTTP gate passes, HE IEs in the start_ap beacon tail |
| `he_caps>1` | - | module init fails (-EINVAL); no wiphy, no AP (never guessed) |
| data contract | - | the static cfg80211 entries are memcmp-checked against the portable decoded blob at band setup; mismatch refuses the band |

## T10. The exact bytes the fire expects [H]

Hostapd builds the beacon HE Capabilities IE from these caps; with defaults
(no SU/MU beamformer config, no PPE) the element on the air must be:

    2.4 GHz (24 B):  ff 16 23  01 00 00 00 00 00  02 00 00 00 00 00 00 00 00 00 00 00  fa ff fa ff
    5 GHz   (28 B):  ff 1a 23  01 00 00 00 00 00  0c 00 00 00 00 00 00 00 00 00 00 00  fa ff fa ff fa ff fa ff

Layout: EID 0xFF, length (1 + 17 + mcs_size), ext-id 0x23, MAC 6, PHY 11,
HE-MCS/NSS set (4 B for 2.4 GHz because no 160/80+80 bit; 8 B for 5 GHz
because the 160 MHz bit is set).  The HE Operation IE (ext-id 0x24) must be
present too (BSS color from hostapd's default `he_bss_color=1`, i.e. the
16-byte operation subfield's first byte low 6 bits = 1); the recipe checks
presence + the capabilities element bytes, not the operation params.

## T11. Host-test matrix and artifact contract (this lane)

| check | expectation | evidence |
| --- | --- | --- |
| policy: `he_caps=0` | no IE built, no iftype data, "off" | `src/he_caps_selftest.c` |
| policy: `he_caps=1` + band 0 | T3 2.4 GHz blob + 24 B IE | idem |
| policy: `he_caps=1` + band 1 | T3 5 GHz blob + 28 B IE | idem |
| policy: `he_caps=2` | -EINVAL, no blob, no IE | idem |
| policy: band outside 0..1 | -EINVAL | idem |
| data-contract | static cfg80211 entries == portable blob (mac/phy/mcs/ppe) | the driver checks memcmp at band setup; the host checker `tools/check-data-contract.py` re-parses both sources |
| build | 0 warnings / 0 errors against linux-6.18.60, undefsym clean | build script |
| artifact | 59 params (58 vap5g base + `he_caps`), `he_caps` default 0, 4 HE feature strings, 12 cfg80211 symvers, .text identical after `strip --strip-debug` | build script + `evidence/build-artifact.txt` |
