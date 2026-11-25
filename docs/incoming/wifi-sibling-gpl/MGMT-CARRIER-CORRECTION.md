# MGMT-CARRIER CORRECTION (task-151 follow-up): the device mgmt-TX message is a raw HCC g0 id 6 control message, not WAL 0x01F2

**Lane:** wifi-sibling-gpl (shared task task-151), **host-only**.
**Written:** 2026-11-25.  **Supersedes** the carrier assumption of the
wifi-ap-mgmt lane (task-143) and its M2 ladder rung - see
[../wifi-ap-mgmt/LADDER-MGMT.md](../wifi-ap-mgmt/LADDER-MGMT.md) correction note.

**Item pointers from the Lead:** T1 = decode the luofu mgmt-TX path and deliver a
corrected builder + patch; T2 = decode the luofu SET_RX_FILTER message; T3 =
assess the g1 id13 mgmt-RX decode; T4 = EAPOL ring refill watermark.  T1 is
solved below; T2 is an **evidence-backed negative**; T3 is confirmed; T4 is a
design note.

Evidence markers: **[V]** decoded from a public/committed source; **[V*]** decoded
by this lane from the local vendor module 2.5.24 (relocatable disassembly, never
committed); **[I]** inference; **[U]** unknown.  Vendor-module function addresses
below are file-relative ARM addresses in
`docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis` / `hi5622v100_plat.dis`
(local-only artifacts; no bytes reproduced in this repo).

---

## T1 - the mgmt-TX wire format (SOLVED)

### T1.1 The vendor call chain [V*]

```
wal_cfg80211_mgmt_tx (cfg80211 op)
  -> WLAN_CFGID_CFG80211_MGMT_TX request  (host-internal; no device message)
  -> hmac_wpas_mgmt_tx            0x4050c  (vendor 2.5.24 wifi.ko)
       alloc skb; copy the 802.11 frame; fill the tx control block
       -> helper                     0x3fe68  (local static, "tx event -> HCC msg")
            hcc_msg_alloc(48 + frame_len)          (plat module 0x11d9c)
            fill 12-byte HCC header in place
            copy 33 bytes of tx control to payload+0 .. +32
            copy the frame to payload+48 (= wire offset 60)
            hcc_msg_tx(msg)                        (plat module 0x11bf8)
              hdr[9] = hdr[1] >> 4  (chip/dst)
              hcc_queue_get_by_msg()  -> hcc_queue_add_msg()
```

The companion senders prove the same message class: `hmac_mgmt_reset_psm`
(0x4039c) sends the same 12-byte header with message id 14 and a 2-byte
payload; `hdpp_send_deauth_msg` (0x5b80) and `hmac_send_mgmt_to_host`
(0x424dc) use the same `hcc_msg_alloc`/`hcc_msg_tx` pair.  This is the
generation's WLAN_CTX-style event channel; it is **not** a WAL cfg-event TLV.

### T1.2 The 12-byte HCC header written by `hcc_msg_alloc` + the helper [V*]

| offset | width | value / source | evidence |
| --- | --- | --- | --- |
| +0x00 | u8 | group nibble 0 (high nibble preserved; helper clears low nibble) | helper `bfi`; queue lookup uses `hdr[0]&0xf` |
| +0x01 | u8 | low nibble `0x1` (alloc/valid); high nibble = VAP/chip byte <<4 | `hcc_msg_alloc` `bfi`; helper `bfi hdr[1], [vap+2], #4` |
| +0x02 | u8 | `[vap+1]` (our builders call it `hdr2`/FA) | helper load/store |
| +0x03 | u8 | `[vap+0]` = VAP id (our builders call it FB) | helper load/store |
| +0x04 | u16 | total length = payload + 12 | `hcc_msg_alloc` `strh r5,[ip,#4]` |
| +0x06 | u16 | **message id 6 (MGMT)**; byte +7 = 0 | helper `mov r3,#6; strb [ip,#6]` + `strb [ip,#7]`=0 |
| +0x08 | u8 | 0 (flags) | helper |
| +0x09 | u8 | dst = hdr[1] >> 4 (chip/dst) | `hcc_msg_tx` `strb [ip,#9]` |
| +0x0A | u16 | `0x5a5a` transport tag | established H2D contract (h2d_frames.h) |

This matches the project's already-decoded H2D contract field-for-field
(`H2D_GROUP_OFF 0x00, H2D_BID_OFF 0x01, H2D_FA_OFF 0x02, H2D_FB_OFF 0x03,
H2D_LEN_OFF 0x04, H2D_ID_OFF 0x06 u16, H2D_FC_OFF 0x08, H2D_DST_OFF 0x09,
H2D_TAG_OFF 0x0A`) and the established clean-room g0 header builders
(`hsop_hdr()`, `h2d_build_g0_cfg_event()`) - independent confirmation from
three directions.

### T1.3 The payload: 48-byte device TX control + frame [V*]

The helper allocates `hcc_msg_alloc(48 + frame_len)`, copies 33 bytes of the
host tx control block (`skb+24`) to payload+0..+32, and copies the 802.11
frame to payload+48 (wire offset 60).  The vendor-filled control fields
(offsets relative to the 12-byte HCC header):

| ctl offset | width | value | evidence |
| --- | --- | --- | --- |
| +0x00 | u16 | `frame_len - 24` (MPDU length) | helper: `ldrh [skb+24]`; wpas: `strh (frame_len-24), [skb+24]`; `strb 24, [skb+26]` |
| +0x02 | u8 | `24` (802.11 header length assumed) | `strb r3=24, [skb+26]` |
| +0x06 | u8 | `0x03` (vendor flags; need-response / retry style bits) | wpas `bic #3; orr #3` on `[skb+30]` |
| +0x08 | u8 | `(cookie & 0xf) << 4` (tx cookie / mgmt-frame id) | wpas `bfi [skb+32], [request+4], #4, #4` |
| +0x09 | u16 | station user/assoc id when known; **0x0fff when no user** | wpas: user assoc id at `user+0xa84`; no-user path `0xff,0x0f` |
| +0x0b.. | | zero | 48-byte block zero-initialised |

The remaining tail of the 48-byte block (offsets 0x0b..0x2f) is not written by
the vendor for this message and is left zero; the receiver is the already-known
**firmware group 0 id 6 handler** from the committed H2D catalogue
(`h2d_catalogue.inc`: `{ 0, 0x06, 0x000601fdu, H2D_RISK_PTR_DEREF }`).

### T1.4 Completion path [V*]

The device reports the TX result as a DMAC->HMAC config-syn event.  The vendor
handler table (relocatable .data, 8-byte `{u32 id; u32 handler}` records) pairs
**event id 0x01F3 (499) -> `hmac_mgmt_tx_event_status` (0x427e4)**:

* the event payload is `{u32 frame_id; u8 status; u8 pad; u16 ???}` (the
  handler logs `[payload+0]` u32, `[payload+4]` u8, `[payload+6]` u16);
* the handler then re-posts a **host-internal HCC group 2 / id 13** message with
  an 8-byte payload `{u32 frame_id at +12; u8 status at +16}`
  (`hmac_mgmt_tx_event_status` itself calls `hcc_msg_alloc(8)` and sets
  `hdr[0]=2`, `hdr[6]=13`) - this is the `wal_cfg80211_mgmt_tx_status`
  event the ap-mgmt lane decoded.  **Correction:** that g2 id13 message is
  host-internal WAL plumbing, not a device->host frame; the device->host event
  is the 0x01F3 config-syn event.

### T1.5 Corrected implementation in this lane

* Portable builder: [src/hi5622_mgmt_logic.h](src/hi5622_mgmt_logic.h) /
  [src/hi5622_mgmt_logic.c](src/hi5622_mgmt_logic.c) -
  `himg_build_hcc_mgmt_tx()` (g0 id6, 48-byte control + inline frame,
  frame length > 25, <= 588 as the vendor host bound; cookie nibble and
  user-id handling as decoded).
* Driver patch: [patches/0001-mgmt-tx-hcc-g0id6-carrier.patch](patches/0001-mgmt-tx-hcc-g0id6-carrier.patch)
  (5 files: `hi5622_mgmt_logic.{h,c}`, `hi5622_frames_api.h`,
  `hi5622_vapscan_provider.c`, `hi5622_mgmt.c`; 366 diff lines).
  It applies cleanly (`patch -p1 --dry-run` OK) to the final association
  tree and **removes the DMA descriptor + dma_alloc_coherent requirement**:
  the frame travels inline, so the VMSPLIT-3G problem disappears for mgmt TX.
* Host test: [tools/mgmt_ctrl_selftest.c](tools/mgmt_ctrl_selftest.c) - **41/41
  checks PASS**, warning-free (header fields, payload map, minimum/maximum
  frame refusals, known/unknown station user id, deprecated WAL builder record,
  D2H g1 id13 RX decode).
* Kernel build: patched tree builds with **0 warnings / 0 errors** against the
  luofu 6.18.60 KDIR; stripped module **344,372 B sha256
  `ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e`**
  (unstripped 1,324,532 B sha `2d9c3f11...`), 56 params, vermagic 6.18.60
  ARMv7, depends cfg80211.  Evidence: `evidence/mgmt-carrier-build.log`.

### T1.6 New fire recipe / params (M2' rung, design only - the Lead fires)

Same proven boot ladder + C2 + iface + hostapd fixture as the ap-mgmt ladder
(`../wifi-stability/FINAL-INTEGRATION.md`), with the patched module:

```
insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<block> \
    allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 \
    vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 \
    rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 sta_join=0 \
    band_vendor=0 scan_band_cfg=0 mgmt_tx=1 mgmt_rx=1
```

* order: page/mask/staging receipt -> AP 5-message order -> AP-ENABLED -> then
  the client auth/assoc attempt.
* **PASS (static-lane prediction):** dmesg shows the g0 id6 frame posted and
  consumed (`hi5622_frame_post` "mgmt_tx" post + consumed announce), and either
  (a) the client's auth/assoc advances, or at minimum (b) an external observer
  sees our auth/assoc response on air.  **FAIL/ABORT:** page != 0x86807FEF,
  mask != 0, staging != 0, an exception, or a refused post - revert; do not
  retry in the same load (one shot per load, as the frame-post budget policy
  already enforces).
* the mgmt-frame TX status (device event 0x01F3) is **not decoded by the
  patched driver**; the .mgmt_tx op reports its cfg80211 status from the
  frame-post consumption (a real local fact) and hostapd does not require the
  device status to continue.  A later lane can wire 0x01F3 when its HCC
  envelope is decoded on live traffic; the host-internal g2 id13 relay evidence
  is recorded above.

---

## T2 - the DMAC RX filter: an evidence-backed NEGATIVE for luofu (do not implement)

The Hi3881V100 GPL sibling has an explicit per-VAP RX filter message
(`hmac_rx_filter.c`: a WLAN_CTX event carrying a u32 bitmap; AP-UP single-VAP
value `0xF7B9FAEA`, AP+STA-up `0x73B9FACA`, support_any clears BIT4 for
broadcast mgmt/probe requests; 0 = keep).  The Lead asked for the luofu analog.

**Static result: the luofu host drivers (2.4.15 and 2.5.24 modules) have no
such sender.**  Evidence:

1. No equivalent symbol in the 2,023-function `hmac_*` symbol table: the only
   `*filter` functions are host-side frame checks
   (`hmac_rx_filter_frame_encrypt`, `hmac_autoblacklist_filter`, etc.);
   `hmac_config_set_mgmt_frame_filters` / `wal_config_set_mgmt_frame_filters`
   are the cfg80211 mgmt-frame-registration passlist (host-side forwarding
   filter), not a device filter.
2. No immediate constants `0xF7B9..`/`0x73B9..`/`0x37B9..` anywhere in the
   2.5.24 wifi module instruction stream [V*: exhaustive immediate scan].
3. Every `hcc_msg_alloc` call site in the wifi module was enumerated with its
   payload length (73 call sites, e.g. `hmac_mgmt_reset_psm` len 2,
   `hmac_send_mgmt_to_host` len 28, `hmac_encap_scan_send_req` len 508); none
   is a VAP-state/AP-start-time u32 filter sender, and none is named/located at
   a VAP state transition.
4. The vendor's `hmac_config_change_vap_state` (0x15c5d4) does protocol-status
   checks and FSM advance - it posts no filter value.

**Decision:** do **not** invent a luofu filter message.  A wrong WLAN_CTX
sub-type is a real risk: the firmware dispatches sub-types to different
handlers, and a guessed id could hit an unrelated handler.  The Hi3881 filter
values/bit semantics are recorded here for reference only.  If mgmt RX is still
silent after the corrected T1 carrier and the T3 check, the next hypothesis is
the firmware VAP/user state or the RF path - not a missing host filter message.
(Public-research2's T2 is therefore re-classified: plausible on the sibling,
**absent on luofu**.)

---

## T3 - mgmt RX via g1 id13: consistent, keep it

* Our committed decode: D2H HCC group 1 / id 13, 60-byte prefix (12-byte HCC +
  48-byte RX info), frame after, channel +7, u16 length +17, s8 RSSI +39.
* Cross-check [V*]: the 12-byte HCC header convention is confirmed independently
  (T1.2); the per-frame 48-byte RX-info block is the luofu form of the sibling's
  `dmac_wlan_crx_event_stru` / `hmac_rx_ctl_stru` (Hi3881
  `hcc/hmac_hcc_adapt.c:35-51`, 48-byte class with vap/frame_len/channel/rssi).
* The dispatch-table cross-check in this lane pairs a D2H cfg-event id 0x0d (13)
  with `hmac_rx_process_mgmt_event` [V*: .data reloc records at data offsets
  0x5e4/0x5e8 with the ids in the following slots].  So "id 13 = management RX"
  is supported by our own module, not only by the ap-mgmt lane.
* **Correction to the ap-mgmt REPORT wording:** g2 id 13 is the *host-internal*
  WAL mgmt-tx-status relay (T1.4), while g1 id 13 is the *device->host*
  management RX event.  Keep the g1 id13 decoder; do not use g2 id13 as the
  device completion.
* Ring drain staying 0 under a client scan remains expected (mgmt is not a
  data-ring frame; sibling CRX/DRX split confirmed in REPORT.md).

---

## T4 - EAPOL/data-ring refill watermark: design note (no code yet)

The public-research2 cross-check (mt76/iwlwifi/ath10k/ath11k/brcmfmac/mwifiex)
and the sibling HALs agree: publishing free-buffer producers is the RX enable,
and sustained traffic needs a **minimum-free watermark** so the device never
starves (iwlwifi's RX_STALLED is the failure mode; ath11k keeps >= 3/4 of the
ring posted; mt76 keeps one descriptor spare).  Our current `dp_rx_drain`
refills up to the live-consumer delta and the completed-buffer count, which is
correct but only pumps when a drain action runs.  Before the 4-way/EAPOL rung:
add a refill-watermark rule to the drain (target `norm/small posted >= 3/4 of
depth`, bounded by buffers returned by completions and by `rx_drain_max`), and
classify a low-credit state in telemetry (the P3 stall classifier already has
the IDLE/PREFETCH/PROGRESS shape; add CREDIT_LOW).  This is a small,
fail-closed change; it is **not implemented in this patch** because it needs the
first data capture to tune the watermark and the corrected T1/T3 path comes
first.  Recorded here so it is not lost.

---

## Fire-recipe summary (for the Lead)

| rung | module / params | expectation | stop condition |
| --- | --- | --- | --- |
| R-A (regression) | patched module, `mgmt_tx=0 mgmt_rx=0` | identical to the proven AP start/TX/keys rungs; no new posts | any page/mask deviation |
| R-B (T1) | `mgmt_tx=1 mgmt_rx=1` | one g0 id6 mgmt frame posted+consumed on the first auth/assoc response; external observer sees the response; hostapd STA add/4-way may begin | page/mask/exception or refused post -> revert, no retry |
| R-C (T3) | as R-B | D2H g1 id13 mgmt RX frames decoded (channel/len/RSSI/frame) while the client scans/attempts | no events -> RF/VAP-state hypothesis, not a filter message (T2) |
| R-D (T4) | after association | EAPOL msg2/4 as MAC-ring completions in `dp_rx_drain`; refill watermark keeps the rings posted | ring fails when watermarked -> rethink |

One load per boot, one post per load where the budget allows, page
`0x86807FEF/7/2/0xABCD` + mask 0 + staging 0 on every rung.

---

## Reproduction

```sh
# 1) patched build (final association tree + this patch)
cd /tmp && rm -rf mgmt-fix && cp -a /tmp/wifi-assoc-build/integ mgmt-fix
cd mgmt-fix && patch -p1 < <repo>/opensource/docs/incoming/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
export KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
export STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
export PATH=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin:$PATH
make ARCH=arm CROSS_COMPILE=arm-openwrt-linux-muslgnueabi- -C $KDIR M=$(pwd) modules

# 2) host test of the corrected carrier + RX decoder
cd <repo>/opensource/docs/incoming/wifi-sibling-gpl
gcc -std=c99 -Wall -Wextra -O2 -I src -o /tmp/mgmt_ctrl_selftest \
    tools/mgmt_ctrl_selftest.c src/hi5622_mgmt_logic.c
/tmp/mgmt_ctrl_selftest     # PASS: 41/41

# 3) patch dry-run against the pinned base tree
cd <pristine-association-tree> && patch -p1 --dry-run \
    < <repo>/opensource/docs/incoming/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
```
