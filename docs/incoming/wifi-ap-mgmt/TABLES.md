# wifi-ap-mgmt TABLES - decoded WAL mgmt-tx family + D2H mgmt RX

**Lane:** wifi-ap-mgmt (shared task task-143), host-only, 2026-11-24.
Attribution: all addresses/offsets here are **derived facts** decoded from
local, gitignored vendor artifacts; no vendor byte is committed.

Evidence markers: **[V]** decoded from a local artifact (vendor host module
2.4.15 sha256 `22fa789a...` / 2.5.24 sha256 `de78ec07...`, or the PD4
firmware image `cfe24bfb...`; CA = firmware file offset + 0x40000);
**[V*]** bench-observed in a prior attended session; **[I]** inference;
**[U]** unknown / not pinned.

Both host modules carry the **same instruction stream** for every function in
this table: the 2.5.24 copy is the 2.4.15 copy shifted by **+0x208** in
`.text` (checked for all mgmt-tx functions and the do_tx TLV pattern).

---

## T1 - The vendor cfg80211 .mgmt_tx family (2.4.15 / 2.5.24) [V]

| item | 2.4.15 `.text` | 2.5.24 `.text` | size | role |
| --- | ---: | ---: | ---: | --- |
| `wal_cfg80211_mgmt_tx` | 0x1056D8 | 0x1058E0 | 0x754 | cfg80211 op; validates, builds descriptor, 8-attempt try loop |
| `wal_cfg80211_mgmt_tx_pre_proc` | 0x1052D0 | 0x1054D8 | 0x1DC | vif/user validation only |
| `wal_cfg80211_mgmt_tx_try` | 0x1054AC | 0x1056B4 | 0x22C | retries do_tx, cookie bookkeeping, status callback |
| `wal_mgmt_do_tx` (local, no symbol) | 0x1042E4 | 0x1044EC | - | builds the WAL cfg-event TLV and waits for the completion |
| `wal_cfg80211_mgmt_tx_status` | 0x105E2C | 0x106034 | 0x1F8 | HCC group-2 id **13** D2H handler (completion) |
| `wal_send_cfg_event` | 0x126C98 | 0x1270D8 | 0x474 | WAL cfg-event builder/sender (shared with AP start/keys) |
| `wal_add_cookie_to_array` | 0x105238 | 0x105440 | 0x98 | allocates the descriptor's cookie slot |
| `wal_check_cookie_timeout` | 0x105178 | 0x105380 | 0x88 | expires stale cookie slots |
| `wal_cfg80211_mgmt_frame_register` | 0x104634 | 0x10483C | 0x64 | frame subscription (hostapd REGISTER_FRAME gate) |

The vendor ops table maps these to cfg80211 op #61 (`mgmt_tx`), #62
(`mgmt_tx_cancel_wait`, 8-byte stub) and #67
(`update_mgmt_frame_registrations`) - see `wifi-driver-re/REPORT.md` §2.

## T2 - `wal_cfg80211_mgmt_tx` control flow [V]

Inputs (`struct cfg80211_mgmt_tx_params`, kernel 5.10 layout):
`+0 chan`, `+4 offchan (u8)`, `+8 wait (u32)`, `+12 buf`, `+16 len`,
`+20 no_cck`, `+24 n_csa_offsets`, `+28 csa_offsets`.

| step | 2.4.15 | what happens |
| --- | ---: | --- |
| parameter gate | 0x1056DC-0x105748 | params/wdev/cookie/buf non-NULL; `wdev->netdev` non-NULL; else `-EINVAL` |
| pre_proc | 0x105750 | vif/iftype/mac-res validation, no wire write |
| vif lookup | 0x10575C | `netdev->ieee80211_ptr` (netdev+0x328) -> wal vif; `mac_res_get_mac_chip` + `vif->[0]` user index -> mac-res VAP entry |
| cookie | 0x1057D4-0x1057F4 | per-chip 64-bit cookie counter at mac_res+0x18/+0x1C; `*cookie` = counter, then incremented |
| **probe-request short circuit** | 0x1057FC -> 0x1059C8 | if frame control `(fc & 0xfc) == 0x50` (probe request): **complete the cookie immediately** via `oal_cfg80211_mgmt_tx_status` and return - no frame is posted to firmware [V] |
| channel copy | 0x10580C-0x1058C0 | 56-byte `ieee80211_channel` copy; channel number from `center_freq`: 2484->14, 2.4G `(f-2407)/5`, 5G `(f-5000)/5`, 4.9G `(f-4910)/5` [V] |
| descriptor build | 0x1058C0-0x105904 | see T3; cookie slot index stored at descriptor+4 |
| AP/STA branch | 0x1058F0 | `vif->iftype` at vif+0xFA8: 1 = STA (offchannel branch 0x104848 when `wiphy->flags & 0x100000`), 2 = AP; other iftypes only log |
| try loop | 0x105914 -> 0x10596C | `wal_cfg80211_mgmt_tx_try(vif, &desc, len, wdev, offchan, cookie, wait, buf)` |

`wal_cfg80211_mgmt_tx_try` (`0x1054AC`): calls `wal_mgmt_do_tx(netdev, &desc,
offchan)` up to **8** times; a result of 1 means success. On success it copies
the cookie-array entry to `*cookie`, clears it and calls
`oal_cfg80211_mgmt_tx_status(wdev, ack=1, cookie, buf, len, ...)`; on failure it
clears the slot and reports `ack=0`. Timeout is bounded per iteration by
jiffies + `msecs_to_jiffies(...)`.

`wal_mgmt_do_tx` (`0x1042E4`): status fields `vif+0x3C8 = 0`,
`vif+0x3CC = 0x8B2D`, then builds the 16-byte TLV in T3 and calls
`wal_send_cfg_event(netdev, 1, 16, &tlv, 0, 0)`; then waits (50 ms slices,
bounded) on the wait queue `vif+0x3BC` until `vif+0x3C8 == 1` and returns
`vif+0x3CC`. Zero-length descriptor copy is refused with the log
`{wal_mgmt_do_tx::memory copy failed}`.

## T3 - WAL cfg-event envelope + the mgmt-tx TLV [V]

The WAL cfg-event **wire frame** (what the vendor emits on both this path and
the AP-start path) is:

    HCC header 12 B | cookie u32 | wal-id u32=1 | pad u16 | data_len u16 |
    seq u32 | cmd u16 | body_len u16 | body[body_len]

* `hcc_msg_alloc(body_len + 16)` allocates the message; the HCC header is
  written by `wal_alloc_cfg_event` (group 2, dst nibble from the vif,
  fa/fb from vif bytes 1/0, header id 0) [V].
* `data_len = 4 + body_len`; total wire length = **32 + body_len**.
* `wal_send_cfg_event` writes `{u32 event = 1; u16 pad = 0; u16 len =
  body_len; u32 seq; payload}` starting 16 bytes into the HCC message data
  (i.e. 4 bytes after the 12-byte header: class/cookie field), copies the
  body at offset 32 and publishes the message [V]. Sequence number comes from
  the global `g_wal_config_seq_num` atomic.

**mgmt-tx TLV** (decoded from `wal_mgmt_do_tx`):

| offset | size | value |
| ---: | ---: | --- |
| +0 (cmd) | u16 | **0x01F2** |
| +2 (len) | u16 | 12 |
| +4 | u32 | channel number (from the cfg80211 chandef) |
| +8 | u8 | cookie-array slot index |
| +9 | u8 | 0 (pad; never written) |
| +10 | u16 | frame length (params->len) |
| +12 | u32 | **device-visible host-DRAM address of the frame** (`params->buf`) |

The frame bytes are **not in the message** - the descriptor only references
them by address, and the vendor passes the cfg80211 frame pointer because its
kernel has a 2 GiB/2 GiB VMSPLIT where kernel VAs land in the device's
host-DRAM window 0x8000_0000..0x8FFF_FFFF. This kernel is VMSPLIT_3G, so the
clean-room driver must pass the **DMA address of its own coherent copy**
(same rule already stated in `hi5622_driver.h` for the ETE/HCC buffers).

## T4 - completion event: HCC group 2 id 13 `wal_cfg80211_mgmt_tx_status` [V]

Handler `0x105E2C` (2.5.24 `0x106034`), data pointer = `hcc_msg->data`:

| field | offset | meaning |
| --- | ---: | --- |
| nibbles | data[1] | high nibble = dst/chip selector (must be 0 on the in-kernel path), low nibble alloc |
| vap id | data[3] | mac-res VAP index |
| status | data[12] u32 | copied to `vif+0x3CC` (try's return value; 1 = success) |
| cookie | data[16] u8 | cookie-array slot echoed back |

It sets `vif+0x3C8 = 1` and wakes `vif+0x3BC`; `wal_cfg80211_mgmt_tx_try`
then reports the cfg80211 TX status. Direction: device -> host (HCC group 2
wal table, id 13).

## T5 - D2H management-frame RX: HCC group 1 id 13 [V]

Receiver chain (vendor host module):

| function | 2.4.15 | 2.5.24 | role |
| --- | ---: | ---: | --- |
| `hmac_rx_process_mgmt_event` | 0x49FD0 | 0x49FE0 | group-1 id-13 handler |
| `hmac_rx_event_pre_process` | 0x49C28 | 0x49C38 | strips the 60-byte prefix, copies 48-byte RX info |
| `hmac_rx_process_mgmt_event_proc` | 0x49F20 | 0x49F30 | dispatches the frame to the AP or STA FSM |
| `hmac_ap_up_rx_mgmt` | 0x4E71C | 0x4E790 | AP-side management processing (auth/assoc) |
| `hmac_rx_mgmt_send_to_host` | 0x4277C | 0x4288C | builds the freq/len, tail-calls send-to-host |
| `hmac_send_mgmt_to_host` | 0x42454 | 0x42564 | in-kernel hand-off message (group 2 id 9) |
| `wal_send_mgmt_to_host` | 0x126194 | 0x1265D4 | group-2 id-9 handler: `cfg80211_rx_mgmt` |
| `oal_cfg80211_rx_mgmt` | 0x80C | 0x80C | calls `cfg80211_rx_mgmt_khz(wdev, freq*1000, 0, frame, len, gfp)` |

**Wire layout** (data pointer = start of the 12-byte HCC header):

    HCC header 12 B | RX info 48 B | 802.11 frame (total - 60) B

* `hmac_rx_event_pre_process` reads `total` and, when `total > 59`, moves the
  frame pointer `+60` and the frame length `-60` [V].
* It copies 48 bytes from `data+12` into `hcc_msg->cb` (the 48-byte RX info)
  and cross-checks `u16 @ data+29` against `total-60` [V].

**RX info field map** (offsets are relative to the 48-byte info block):

| off | size | meaning | evidence |
| ---: | ---: | --- | --- |
| +7 | u8 | **channel number**; `hmac_rx_mgmt_send_to_host` reads `msg+31 = cb+7` | [V] |
| +17 | u16 | **frame length** = total - 60; read as `msg+41 = cb+17` | [V] |
| +39 | s8 | **RSSI**; frame-notify callback computes `clamp(100 + cb+39, 0, 100)` | [V] |
| others | - | not consumed by the mgmt path (rate/timestamp/status [U]) | [U] |

**Channel -> frequency** (vendor formula, `hmac_rx_mgmt_send_to_host`):

| channel | frequency |
| --- | --- |
| 1..13 | 2407 + 5*ch |
| 14 | 2484 |
| 182..196 | 4000 + 5*ch |
| 32..177 | 5000 + 5*ch |

The clean-room decoder additionally bounds the band plan (refuses 0, 15..31,
178..181, 197+) - the info byte is untrusted device input.

Delivery: `oal_cfg80211_rx_mgmt` passes `sig_dbm = 0` (unknown) to
`cfg80211_rx_mgmt_khz`; the monitor-style callback gets the clamped RSSI
instead. In the 6.18 target the exported symbol is
`cfg80211_rx_mgmt_ext` (the `_khz`/`MHz` forms are static inlines that build
`struct cfg80211_rx_info`; `freq` is in **kHz**).

## T6 - target-kernel cfg80211 contract (6.18.60) [V]

| 6.18 API | exported? | use |
| --- | --- | --- |
| `cfg80211_rx_mgmt_ext(wdev, &info)` | `EXPORT_SYMBOL` in `net/wireless/mlme.c` | RX delivery |
| `cfg80211_mgmt_tx_status_ext(wdev, &status, gfp)` | `EXPORT_SYMBOL` in `net/wireless/nl80211.c` | TX status |
| `cfg80211_rx_mgmt_khz` / `cfg80211_mgmt_tx_status` | headers only (static inline) | do not import |

The project's cfg80211-provider.symvers contract therefore grows **10 -> 12
lines** with `cfg80211_rx_mgmt_ext` and `cfg80211_mgmt_tx_status_ext`.

## T7 - firmware static config-key table A vs the WAL cmd [V]

PD4 firmware (and the stock pin) config-key table A at **CA 0x10C23C** (217
rows `{u16 key; u16 pad; u32 worker}`) is what the bench-proven **g0 id-1
carrier** dispatches through. Checked keys:

| key | worker CA | meaning |
| ---: | ---: | --- |
| 0x0124 | 0x841B9 | module channel config (proven on bench) |
| 0x0127 | 0x84DA5 | AP config (proven on bench) |
| 0x0005 | 0x82879 | SSID (proven on bench) |
| 0x0065 | 0x46395 | VAP start (proven on bench) |
| 0x01F0 | 0x49C8D | nearest neighbour; a 6-byte-body dispatcher, **not** a frame TX worker |
| **0x01F2** | **absent** | the WAL mgmt-tx command is **not** a g0 config key |

=> The vendor mgmt-tx command can only ride the **WAL group-2 cfg-event
envelope** (T3), not the g0 id-1 carrier used by the earlier AP-start/keys
lanes. Whether the PD4 firmware's WAL dispatch table accepts 0x01F2 is [U]
(the firmware-side WAL table was not located statically); the ladder decides
it.

## T8 - patch inventory (14 files, all original C) [V]

| file | change |
| --- | --- |
| `hi5622_mgmt_logic.c/.h` | **new** portable decode/build logic (host-tested) |
| `hi5622_mgmt.c/.h` | **new** kernel glue: cfg80211 op, DMA frame buffer, D2H delivery |
| `hi5622_frames_api.h` | cap `HI5622_FRAME_CAP_MGMT_TX` (bit 15), `struct hi5622_mgmt_tx_params`, `build_mgmt_tx` pointer |
| `hi5622_scan_logic.c` | cap name + contract validator entry + cap-format loop bound |
| `hi5622_vapscan_provider.c` | WAL cfg-event mgmt-tx builder + capability |
| `h2d_frames.h/.c` | WAL cfg-event command allowlist entry {0x01F2, 12} (keeps the fail-closed validator) |
| `hcc.c` | D2H delivery hook (group1 id13, both receive paths) |
| `hi5622_cfg80211_ops.c` | `.mgmt_tx` body -> `hi5622_mgmt_op_tx()` (stays -EOPNOTSUPP at mgmt_tx=0) |
| `hi5622_cfg80211.c` | teardown hook for the DMA frame buffer + include |
| `Kbuild` | two new objects |
| `cfg80211-provider.symvers` | +2 imports (12-line contract) |

## T9 - interface with wpa3-sae lane (task-144) [V]

* wpa3-sae owns `wiphy->mgmt_stypes` and widened the AP TX mask to
  `0x3c2a` (auth bit 11 was already present) - this lane does **not** touch
  it; take the wpa3-sae version on merge.
* Overlap files are `hi5622_cfg80211_ops.c` (their one
  `.set_default_mgmt_key` table line vs this lane's `hi5622_cfg11_mgmt_tx`
  function body) and `hi5622_cfg80211.c` (their capability block vs this
  lane's teardown hook). Both hunks are disjoint; keep both.
* This lane does not touch key/IGTK paths, `set_default_mgmt_key` or the
  cipher latch.

## T10 - the next AP-usability blocker (not implemented here) [I]

With the host-SME design (deliver auth/assoc to hostapd via
`cfg80211_rx_mgmt`, transmit hostapd's responses via `.mgmt_tx`), hostapd's
`handle_assoc()` calls `hostapd_sta_add()` -> `NL80211_CMD_NEW_STATION`
**before** it sends the assoc response. cfg80211 rejects that without a
`.add_station` driver op, so association cannot complete even with a perfect
mgmt TX/RX path. The candidate paths:

1. implement `.add_station/.del_station` that create/destroy the firmware
   per-STA user context (g0 id7 user_add family, already built by the
   vap-scan provider; AP-mode parameters need the same H2D work), or
2. implement the vendor **full-MAC AP SME** instead: decode/build
   `hmac_mgmt_encap_auth_rsp` (0xA018C) / `hmac_mgmt_encap_asoc_rsp_ap`
   (0x9EEF8) in the driver, transmit them itself and notify hostapd with
   `cfg80211_new_sta`; hostapd then only does the EAPOL/key exchange.

Either is a separate task; the vendor's own `.add_station` is an 8-byte stub
because the vendor does AP SME in the hmac layer, which is consistent with
option 2.
