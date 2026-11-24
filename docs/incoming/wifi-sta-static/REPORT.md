# STA connect / 5 GHz second endpoint / HT capability - static reversal

**Lane:** `wifi-sta-static` (shared task **task-141**, host-only clean-room).
**Written:** 2026-11-24. **Status:** static reversal complete; patch + host tests
+ compile-verify delivered; **bench ladder designed, not fired** (the Lead fires).
**Sources:** local vendor modules only, never committed:
`/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` (2.5.24, md5
`4737fcb2…`) and `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/
hi5622v100_wifi.ko` (2.4.15, md5 `1745ef77…`); the project 6.18.60 tree
`…/linux-6.18.60`; cfg80211 struct definitions from the project's 5.10.201 and
6.18.60 trees. **No device contact, no firmware/binary/calibration byte in any
tracked file.** Re-derive every claim with `tools/sta_re.py` (evidence copies in
`evidence/sta-re-2.5.24.txt` / `evidence/sta-re-2.4.15.txt`).

**Evidence markers:** [V] re-verified by this lane from the named local artifact
(disassembly/data only, addresses are link-time `.text`/`.data` offsets);
[V*] device-measured by an earlier attended lane (cited doc); [I] inference;
[U] unknown.

---

## 1. Headline

1. **The STA connect path is NOT the d2h-direct HCC family - it is a WAL
   cfg-event.** Both `cfg80211_ops.connect` and `.disconnect` are thin
   wrappers over `wal_cfg80211_start_req()`, which posts **HCC group 2, HCC id 0,
   WAL cfg-event id 1** with a `{u16 opcode, u16 body_len, body}` data block
   [V]. Opcodes: **connect = 0x0123, body 108 B**; **disconnect = 0x00d0, body
   16 B** (see TABLES T2/T3).
2. **The driver already carries (and the bench has proven) exactly this WAL
   envelope** - the AP-start lane's 4 frames use the same carrier and are
   consumed by the firmware [V*]. This lane only adds the two STA opcodes to the
   allowlist; no new transport work is needed.
3. **The firmware does the auth/assoc**; the vendor driver implements no
   cfg80211 `.auth`/`.assoc` ops (they are NULL in its 39-op table [V], the
   driver is a connect-style SME). The host's STA sequence is: user/VAP exists
   (add_virtual_intf, g0 id7/8) -> WAL connect opcode 0x123 -> firmware
   auth/assoc over the air -> D2H management/assoc event -> host
   `cfg80211_connect_done`. **The completion event decode is the missing piece
   in our driver (RX/management lane), so our `.connect` posts the join but
   never fakes `cfg80211_connect_done`.**
4. **The 5 GHz "second endpoint" is one wiphy per BAND, not one wiphy per PCI
   endpoint.** `wal_cfg80211_init` loops over the board's band list
   (`hmac_board_get_instance` -> band count at board+6, band ids at board+7+i)
   and calls `wiphy_new_nm()` once **per band**; `hmac_band_get_by_id()` only
   accepts chip 0 and returns `g_ast_band[band_id]` (stride 0x2330) [V]. The two
   PCI endpoints are two RF chips behind one host MAC ("cross" board), so stock
   shows `WIPHY=2` [V*]. Our driver's per-endpoint/one-band model produces the
   same observable count and band split; which endpoint serves which band stays
   **[I]** (the board config comes from the vendor INI, not from the module).
5. **Vendor band tables decoded exactly**: 2.4 GHz **14 channels** (ch1-14,
   max_power 30), 5 GHz **29 channels** (36-64, 100-144, 149-165, and the
   4.9 GHz public-safety block 184-196 at 4920-4980 MHz) [V]. HT capability word
   **0x0062** (SUP_WIDTH_20_40 | SGI_20 | SGI_40) on both bands; VHT is
   advertised on both bands with 2 SS MCS0-9 (`0xfffa`) and cap
   **0x00400000 (2.4 GHz)** / **0x00400064 (5 GHz: HTC_VHT | 160 MHz | SGI80 |
   SGI160)** - the latter matches the bench's live "5 GHz 160 MHz on ch44"
   readout [V*].
6. **The static HT template's MCS rx_mask is all zero** [V]; a shipping wiphy
   cannot work with that, so the vendor must fill it from the INI at runtime -
   an [U] we must not copy. Our patch therefore keeps our own MCS mask and only
   adopts the cap word.
7. **1 correction to the inherited driver**: the upstream WAL allowlist already
   has opcode **0x00d0 / 16 B named `KICK_STA`**; this lane shows it is the
   cfg80211 **disconnect** request (same opcode, same body: bssid + reason +
   constant 1 at +15). No wire change, naming/documentation only (REPORT 7).

---

## 2. Artifacts, tools, method

| item | value |
| --- | --- |
| primary vendor module | 2.5.24 `hi5622v100_wifi.ko` 3,564,728 B md5 `4737fcb2…` |
| cross-check module | 2.4.15 `hi5622v100_wifi.ko` md5 `1745ef77…` (same opcodes/offsets modulo layout) |
| re-derivation tool | `tools/sta_re.py` (pyelftools + capstone; prints only derived facts) |
| annotation aid | inherited `wifi-host-replica/tools/annotate.py` (objdump reloc annotator) |
| kernel trees | 5.10.201 struct layouts + 6.18.60 compile target |

Method: symbol-table lookup on the (unstripped, ET_REL) vendor module, ARM/Thumb
disassembly of the named functions, relocation resolution for call/literal
targets, and direct read of the `.data` band tables. Everything below is
reproducible with `sta_re.py <vendor .ko>` plus the address table in TABLES.md.

---

## 3. The STA connect path (decoded)

### 3.1 cfg80211.op connect / disconnect

```
wal_cfg80211_connect            .text+0x100590   (size 0x2b4)  2.5.24
  validates wiphy/netdev/params, copies cfg80211_connect_params fields into a
  108-byte stack body (memset 108, then field stores), optionally parses the
  RSN/WPA IE (mac_find_ie(0x30/0xdd), mac_ie_get_rsn_cipher), then:
    wal_force_scan_complete(dev, 1)              ; abort an in-flight scan
    wal_cfg80211_start_connect(dev, body)
       -> wal_cfg80211_start_req(dev, body, 108, 0x123, wait=1)
          -> wal_send_cfg_event(dev, 1, 108+4, &{opcode,len,body})
             -> WAL cfg-event on HCC group 2 / HCC id 0 / WAL id 1

wal_cfg80211_disconnect         .text+0x100844   (size 0x1b8)  2.5.24
  looks up mac_vap = netdev+0x808, user id = u16 at mac_vap+0x3e2,
  mac_res_get_mac_user(chip, user), builds a 16-byte body
  {bssid[6] (from the mac user object +0x12), u16 reason at +6, [15]=1},
  then wal_cfg80211_start_disconnect(dev, body)
       -> wal_cfg80211_start_req(dev, body, 16, 0x0d0, wait=1)
```

Both `wal_cfg80211_start_req` (.text+0x127ec4) and `wal_send_cfg_event`
(.text+0x1270d8) were read directly: start_req copies the body into a 588-byte
buffer behind a `{u16 opcode, u16 len}` header, `uxth(len+4)` becomes the WAL
`data_len`, and `wal_send_cfg_event` allocates the WAL message, writes the WAL
id/len/sequence and appends the data - exactly the envelope our driver's
AP-start WAL builder already emits and the bench consumes [V*].

### 3.2 WAL cfg-event envelope (wire)

```
12-byte HCC header: group 2, alloc 1, dst nibble, total, HCC id 0, tag 0x5a5a
+0x0c u32 response cookie (0)
+0x10 u32 WAL message id (1 = WAL_MSG_CFG)
+0x14 u16 pad
+0x16 u16 data_len = 4 + body_len
+0x18 u32 WAL sequence (vendor: global atomic counter g_wal_config_seq_num, incremented per cfg event)
+0x1c u16 opcode
+0x1e u16 body_len
+0x20 body[]
```

The inherited `h2d_check_wal_cfg()` validator already enforces this shape; this
lane extends its command allowlist with `{0x0123, 108}` [patch h2d_frames.c].

### 3.3 Connect body (108 B) - field map [V]

`wal_cfg80211_connect` stores the following (all other bytes zero from the
memset):

| off | width | value / source (`cfg80211_connect_params` 5.10 offsets) |
| ---: | ---: | --- |
| +0x00 | u8 | 0 (reserved) |
| +0x01 | u8 | `ssid_len` (params+0x14) |
| +0x02 | u8 | `mfp` (params+0x28) |
| +0x03 | u8 | 0 |
| +0x04 | u32 | IE buffer address (params+0x1c) |
| +0x08 | u32 | SSID buffer address (params+0x10) |
| +0x0c | u32 | BSSID buffer address (params+0x08) |
| +0x10 | u8 | `privacy` (params+0x24) |
| +0x11 | u8 | `auth_type` (params+0x18 low byte) |
| +0x12 | u8 | `key_len` (params+0x78) |
| +0x13 | u8 | `key_idx` (params+0x79) |
| +0x14 | u32 | key buffer address (params+0x74) |
| +0x18..+0x67 | - | 0 |
| +0x68 | u32 | `ie_len` (params+0x20) |

Notes:
* The four "address" fields are **host-DRAM addresses**. The vendor's
  host-VA==device-address rule holds on its build (the RC inbound window maps
  host DRAM at the same CA base, see e.g. the ETE node `devva` field); on our
  kernel the equivalent is a `dma_alloc_coherent` address, which is exactly
  what the patch does. The firmware's dereference contract for these four
  buffers is still **[U]** - one reason the post path is module-parameter
  gated and the ladder is one single controlled fire.
* There is **no channel/frequency field** in the connect body [V]; the firmware
  joins with the channel state the MAC already has (prior scan / VAP band
  config). Our patch logs and does not invent one.
* `ht_bw` is **not** in the connect body either: the STA bandwidth lives in the
  per-user MAC config (the vendor's `hmac_config_set_bw` / user-add path) and
  in the join-request message below.

### 3.4 The firmware join-request builder (g0 id 0x0f)

Separately from the WAL cfg event, the vendor MAC posts the legacy **direct
group-0 message id 15** when the join starts:

`hmac_sta_sync_join_req_params` (.text+0x52fac, size 0x254), decoded [V]:

```
hdr = *(msg_obj + 0x118)            ; 12-byte HCC header
alloc = 0x40                        ; payload 64 B
hdr[0] group = 0 (bfi r3,#0,4 from the input argument)
hdr[6] id = 0x0f
hdr[2]/hdr[3] = user object +0xfa9 / +0xfa8   ; vap id / chip id
hdr[1] high nibble = user object +0xfaa low nibble (band)
payload +0x00..+0x05  BSSID         (event params +0x36)
payload +0x06..+0x07  u16           (event params +0x2c)
payload +0x08..+0x0b  u32           (user object +0xfb8)
payload +0x0c..+0x0f  0
payload +0x10..+0x13  1             (str ip=1)
payload +0x14..+0x34  33 B via memcpy_s from event params +0x0b
                                    (SSID/join parameters block)
payload +0x34         0
payload +0x36         bandwidth byte (0x1/0x2 picked from the chip res
                      +0x34c bit via the user object, see 5.3)
then hmac_sta_update_join_multi_bssid_info(user+0xfa8, payload, params+0x84)
```

Companions (all [V], same family): `hmac_sta_wait_join` (.text+0x538b8, g0/rt
id 0x10, 16 B), `hmac_sta_wait_join_rx_beacon` (.text+0x514d4, g0 id 0x10,
16 B), and on the reverse side `hmac_handle_asoc_rsp_sta` /
`hmac_report_connect_failed_result` (g2 id 3, 28 B). Our patch does **not**
send this legacy message: the WAL cfg-event path above is the complete
`.connect` request; the g0 id 0x0f join is issued by the vendor MAC as a
follow-up and its reference fields are pre-staged in firmware memory, not
expressible host-side yet [U].

### 3.5 Completion / auth / assoc / EAPOL

* The vendor `struct cfg80211_ops` has **no .auth/.assoc/.deauth** slots [V];
  the firmware authenticates and associates, so the host never builds
  auth/assoc frames. hostapd AP-side auth/assoc is a different path (already
  covered by the AP lane).
* The result path is D2H: the MAC event table contains
  `hmac_rx_process_mgmt_event`, `hmac_mgmt_send_disasoc_deauth_event`,
  `hmac_mgmt_deauth_msg_process`, `hmac_del_user_msg_process` etc.
  (wifi-driver-re sec 3.6) and the vendor's D2H handler for the assoc response
  calls the cfg80211 SME completion. **Our driver has no D2H management decode
  yet**, so no `cfg80211_connect_done` is (or should be) faked - the patch
  documents this and userspace sees a clean timeout.
* **EAPOL** is ordinary 802.3 data on the STA netdev, i.e. it depends on the MAC
  RX path (currently 0 captures; owned by the RX lane). WPA2 STA is therefore
  triply blocked: no RX data, no key path (wpa lane), no completion event.
* `set_power_mgmt` (.text+0x1050ac) exists in the vendor driver but is not
  needed for connect; left NULL (cfg80211 returns -EOPNOTSUPP).

---

## 4. The 5 GHz second endpoint

### 4.1 Vendor band/radio model [V]

`wal_cfg80211_init` (.text+0x106a74, size 0x490) is a per-band loop:

```
hmac_board_get_instance(&board)        ; band count = *(u8*)(board+6)
oal_bus_get_chip_num()                 ; must be nonzero (two PCI chips)
for (i = 0; i < board->band_num; i++) {
    band_id = board->band_ids[i]       ; *(u8*)(board+7+i)
    wiphy = wiphy_new_nm(&wal_cfg80211_ops, sizeof_priv=4, NULL)
    band   = hmac_band_get_by_id(0, band_id)   ; chip 0 only, g_ast_band[band_id]
    *(band + 0x2324) = wiphy                   ; back-pointer
    wiphy->perm_addr[5] = band_id + 1          ; vendor's band marking
    wiphy->mgmt_stypes = .rodata+0x9448
    wiphy->iface_combinations = .data+0x1070, n = 1
    wiphy->interface_modes = 12                ; STATION|MONITOR (AP via combo)
    wiphy->flags regs, cipher_suites = .rodata+0x947c (10 suites)
    if (hmac_band_id_to_band_type(0, band_id) == 2g and check_2g_enable)
        wiphy->bands[0] = .data+0xdf8
    if (5g and check_5g_enable)
        wiphy->bands[1] = .data+0xe54
    wiphy_apply_custom_regulatory(wiphy, g_st_default_regdom)
    wiphy_register(wiphy)
}
```

`hmac_device_check_band_enable(chip=0, band_type)` (.text+0x9562c) checks
whether a band object with that type exists; `hmac_band_get_by_id()`
(.text+0x13f6f4) rejects any chip != 0 and indexes `g_ast_band` (2 objects,
**stride 0x2330**). `hmac_band_id_to_band_type` reads the type byte at
`band+0x10d1`. This is a **single host-MAC (chip 0) with two logical bands**;
the second PCI endpoint is a second RF chip reached through the SoC's cross
arrangement, not a second cfg80211 instance. Bench-observed stock `WIPHY=2`
[V*] agrees.

Consequence for our driver: our one-wiphy-per-PCI-endpoint model (domain 0 ->
2.4 GHz, domain 1 -> 5 GHz) yields the same two wiphys and band split, but the
domain->band mapping is still [I]. A host-only discriminator is not provable
from the module (the board config comes from
`/etc/wireless/hi5622v100/hi5622v100.ini` via the vendor userspace); the
ladder therefore includes a `band_force` sanity rung and an endpoint-probe read
of the two wiphys' advertised channel lists.

### 4.2 Band tables (decoded) [V]

| band | channels | rates | HT cap | VHT cap | VHT MCS |
| --- | ---: | ---: | --- | --- | --- |
| 2.4 GHz | 14 (ch1-14, 2412-2484) | 12 (10..540) | 0x0062 | 0x00400000 | 0xfffa (2 SS MCS0-9) |
| 5 GHz | 29 (see TABLES T5) | 8 (60..540) | 0x0062 | 0x00400064 | 0xfffa |

Channel struct specifics [V]: `band` field 0/1, `hw_value` = IEEE channel
number, static `flags` = 0 (the vendor's custom regdomain disables DFS/NO_IR
channels at register time; our driver leaves that to the kernel regdb).
`max_power` bytes are 30 (2.4) / 30 (5G) in the template; the live values are
23/24 dBm after the regulatory pass [V*].

The static supported-band structs live at `.data+0xdf8` (2.4) and
`.data+0xe54` (5G), size 92 B each; channel arrays at `.data+0x16e4`
(14 x 56 B) and `.data+0x108c` (29 x 56 B); rate arrays at `.data+0x19f4`
(12 x 12 B) and `.data+0x1a24` (8 x 12 B - the 5G list shares the OFDM tail of
the 2.4G array with a different base pointer).

### 4.3 Does 5 GHz scan/connect need the g0 id 10 band config?

The H1 band-config message is **band-agnostic**: a single handler (CA
0x5FF64) configures the firmware's per-slot scan/channel context and the channel
id is looked up in the firmware's own channel table. The per-band differences
are (a) the channel numbers/ids in the payload, (b) the HCC header band nibble
and (c) which scan request channel entries are used - our scan request already
carries a `band` byte per channel entry `{u8 num; u8 band; u8 0; u8 idx}` [V].
So the 5 GHz wiphy does **not** need a different band-config message; it needs
5 GHz channel ids in the same message. The firmware channel-id table for 5 GHz
is [U] (the same [U] the 2.4 GHz H1 experiment already lives with: the builder
takes a raw `chan_id` module parameter). Recommendation for the bench ladder:
run the existing H1 single-request scan rung on the 5 GHz wiphy first with the
default `scan_band_cfg=0` (the request path is band-clean), then optionally with
`scan_band_cfg=1` using 5 GHz channel ids.

---

## 5. HT / VHT configuration

### 5.1 What the vendor advertises [V]

* HT: `cap = 0x0062` = `IEEE80211_HT_CAP_SUP_WIDTH_20_40` (bit1) |
  `SHORT_GI_20` (bit5) | `SHORT_GI_40` (bit6); `ht_supported=1`;
  `ampdu_factor=0`, `ampdu_density=0`; **`mcs.rx_mask` all zero in the static
  template** ([U]: almost certainly runtime-filled from the INI - a wiphy with
  an empty MCS mask cannot run HT, yet the stock unit does).
* VHT: advertised on both bands, 2 spatial streams MCS0-9; 2.4 GHz cap has no
  width bits (0x00400000 = HTC_VHT only); 5 GHz cap adds 160 MHz + SGI80/160
  (0x00400064), matching the live "5 GHz ch44 at 160 MHz, 2.4 GHz ch6 at 40 MHz"
  readout [V*].
* HE (11ax): not present in these static tables; the live unit runs
  `hwmodes='11bgnax'` [V*], so HE caps come from a runtime/INI path too [U].

### 5.2 What our patch can safely do

* **Default (no change):** baseline HT20 word (SGI20 | MAX_AMSDU) on both bands;
  no VHT; bench-validated behaviour preserved.
* **`ht_mode=1|2`:** adopt the vendor HT cap word (0x20 / 0x62). Mode 2 is the
  first time our wiphy advertises **HT40**, which hostapd's
  `ht_capab=[HT40+]` path checks. The MCS mask is **not** copied (stays ours,
  0xff) - documented limitation.
* **`vht_cap=1`:** adopt the vendor VHT word per band (2.4: 0x00400000, 5G:
  0x00400064) + 2 SS MCS0-9.
* **`band_vendor=1`:** swap the channel tables for the vendor's 14/29-channel
  lists (2.4 ch14, 5G ch144 + 4.9 GHz block). The regulatory core still applies
  the kernel regdb (world: NO_IR), so this only widens the table.

### 5.3 What it does not give you

Advertising HT40/VHT is **capability only**. Actual 40/80/160 MHz use needs:
(a) the per-band channel-width config on the firmware side
(`hmac_config_set_bw` / `hmac_config_set_mode` / the AP-start AP-config width
field), and (b) for STA the join-request bandwidth byte
(`hmac_sta_sync_join_req_params` payload+0x36, [V]) or the equivalent
per-user bandwidth config. Neither is wired here; see the ladder rungs S5/S6.
Also the AP path's 4-frame config may need its HT/VHT IEs re-checked by
hostapd - the ladder verifies the advertisement, not on-air 40 MHz.

---

## 6. Implementation delivered

`patches/0001-sta-static.patch` applies on the combined
AP+DP+scan+stability chain (the bench-validated module source) and adds:

| file | change |
| --- | --- |
| `hi5622_sta_logic.h/.c` (new) | vendor band tables, HT/VHT words, connect bounds, auth mapping (portable C99, host-tested) |
| `hi5622_sta_frames.h/.c` (new) | WAL connect (opcode 0x0123, 108 B body) and disconnect (0x00d0, 16 B body) builders + structural self-check (portable) |
| `hi5622_sta.h/.c` (new) | cfg80211 .connect/.disconnect kernel path, `sta_join` gate (0/1/2), coherent join buffer, frame posting through the inherited fail-closed `hi5622_frame_post()` |
| `hi5622_cfg80211_ops.c` | the inherited fail-closed connect/disconnect stubs now route to the STA path (default still -EOPNOTSUPP) |
| `hi5622_cfg80211.c/.h` | `ht_mode`, `vht_cap`, `band_vendor`, `ht_mode` validation, vendor channel tables, STA buffer teardown |
| `h2d_frames.h/.c` | WAL allowlist entry `{0x0123, 108}` |

Fail-closed semantics:

```
sta_join=0 (default) : .connect/.disconnect -> -EOPNOTSUPP, no frame built,
                       no DMA buffer, no device write
sta_join=1           : frame built+validated and logged, nothing posted
sta_join=2           : frame posted through hi5622_frame_post() (which also
                       enforces allow_frames>0, gate set, budget, gap, and the
                       h2d validator); completion is NOT faked
```

Compile-verify (this lane, host-only): chain base builds clean
(990,664 B unstripped), patched module builds with **zero warnings/errors**,
stripped artifact **278,044 B sha256 `333c72a5…`**, `depends=cfg80211`,
vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; host selftests PASS.
Evidence: `evidence/build-verify.txt`, `evidence/selftest.txt`,
`evidence/artifact-identity.txt`.

---

## 7. Corrections / additions to the existing record

1. **WAL opcode 0x00d0 is cfg80211 disconnect, not "KICK STA".** The inherited
   `h2d_frames.h` comment names it `H2D_WAL_CMD_KICK_STA`; the vendor path
   proves it is `wal_cfg80211_disconnect` -> `wal_cfg80211_start_disconnect` ->
   opcode 0x00d0 with a `{bssid[6], reason u16, [15]=1}` body [V]. No wire
   change; this lane documents it and reuses the same allowlist row.
2. **The vendor is a connect-SME driver, not auth/assoc.** `.auth`/`.assoc`
   are NULL in its ops table [V]; the firmware auths/associates. Earlier notes
   that hint at a host-side auth/assoc state machine describe the *firmware*
   handler names (hmac_sta_wait_auth etc.), not host cfg80211 ops.
3. **One wiphy per band, not per endpoint** (sec 4.1). The prior
   "one radio per PCI endpoint" working assumption in
   `hi5622_cfg80211.c` gives the same observable result but a different
   architecture; the static code is unambiguous.
4. **VUT/HT words corrected**: 5 GHz VHT cap is 0x00400064 (160 MHz + SGI), not
   0x00400000; 2.4 GHz is 0x00400000; both bands advertise VHT with 2 SS.
5. **`wal_cfg80211_init` wiphy field notes**: the value 0x1070 seen at
   `[wiphy+20]` in earlier notes is the *iface_combinations pointer offset*,
   not `interface_modes`; the real mode mask is 12 (STATION|MONITOR) and AP
   comes from the single iface combination.

---

## 8. Open items ([U] / blocked on other lanes)

* Connect completion: D2H management/assoc event decode (RX/management lane)
  -> `cfg80211_connect_done`.
* The four host-buffer addresses in the connect body: firmware dereference
  contract unverified; the ladder's first post is the experiment.
* 5 GHz firmware channel-id table (for the H1 band config on 5 GHz).
* Legacy g0 id 0x0f join-request reference fields (pre-staged firmware memory).
* STA data path/EAPOL = MAC RX path (rx-static lane).
* WPA2 key install = wpa-static lane.
