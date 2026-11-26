# CFG80211-MONITOR - the monitor-mode design for the IMAGE #3 driver

**Lane:** `monitor-mode` (task-241). Host-only design + prototype. No bench contact.

This document is the actionable delta between the IMAGE #3 merged driver
(`/tmp/wifi-image-3-module`, base manifest in `evidence/`) and a monitor interface that
(a) satisfies the 6.18.60 cfg80211 contract and (b) can actually deliver frames. It is
deliberately split into **v1 = local mirror** (implemented here, no firmware write) and
**v2 = true promiscuous** (blocked on a firmware decode, NOT implemented).

---

## 1. Behaviour matrix

| `monitor` module param | monitor iface create | `iw dev mon0 set channel` | RX mirroring | firmware writes | status |
| --- | --- | --- | --- | --- | --- |
| **0** (default) | yes (cfg80211 only, netdev is inert) | **-EOPNOTSUPP** | none | none | fail-closed baseline, unchanged from today |
| **1** | yes | accepts + stores the chandef (radiotap metadata only) | mgmt + data frames that reach the existing RX sinks are mirrored to `mon0` | **none** | v1, host-tested prototype; capture needs the F1 |
| **>1** | yes (interface code unchanged) | -EOPNOTSUPP | none; policy logs `invalid monitor mode` once | none | refused, never guessed |

Non-negotiables: `monitor=1` never programs a firmware channel; the mirrored frames are the
ones the AP VAP receives (not a promiscuous capture); no monitor TX injection; no spectral
data is produced.

## 2. The kernel contract this design must satisfy (6.18.60)

See TABLES T1 for line numbers. Summary:

1. Interface type: `interface_modes` bit + `.change_virtual_intf` storing `wdev->iftype`
   (already present in the IMAGE #3 driver; do not regress it).
2. Channel selection goes through `cfg80211_set_monitor_channel()` which **requires
   `.set_monitor_channel`**; the driver decides what "set channel" means.
3. There is no kernel monitor-RX helper: the driver owns the radiotap header and the skb
   delivery to the monitor netdev.
4. Interface combinations are not enforced for this driver (no combinations advertised); the
   monitor iface can coexist with the AP iface without a combination stanza. If combinations
   are added later, keep MONITOR in `software_iftypes` (never inside a combination limit).
5. The monitor netdev type is `ARPHRD_IEEE80211_RADIOTAP`; the AP netdev stays
   `ARPHRD_ETHER`.

## 3. Delta list (prototype, patch `patches/0001-monitor-mirror.patch`)

| # | file | change |
| --- | --- | --- |
| 1 | `src/hi5622_monitor_logic.h/.c` (new) | portable, kernel-free logic: mode policy, channel->frequency map, radiotap builder, length bounds |
| 2 | `hi5622_monitor.h/.c` (new) | kernel integration: module param `monitor`; `hi5622_monitor_set_channel()`; `hi5622_monitor_rx_mgmt()`; `hi5622_monitor_rx_data()`; monitor wdev lookup; counters; fail-closed policy |
| 3 | `hi5622_cfg80211.h` | add `struct cfg80211_chan_def mon_chandef; bool mon_valid;` + monitor counters to `struct hi5622_cfg80211` |
| 4 | `hi5622_cfg80211_ops.c` | add `hi5622_cfg11_set_monitor_channel()`; register `.set_monitor_channel` in `hi5622_cfg11_ops` |
| 5 | `hi5622_mgmt.c` | after the D2H mgmt decode (`hi5622_mgmt_deliver`), call `hi5622_monitor_rx_mgmt(ctx, &rx)` **before** the `mgmt_rx` gate, so the mirror is independent of the AP-policy param |
| 6 | `hi5622_sta_ops.c` | at the top of `hi5622_sta_ops_rx_deliver()`, call `hi5622_monitor_rx_data(ctx, mpdu, len)` **before** the `eapol_rx` gate |
| 7 | `Kbuild` | add `hi5622_monitor_logic.o hi5622_monitor.o` to `hi5622_wifi-y` |

`hi5622_monitor_rx_mgmt/data` take the cfg lock with **mutex_trylock only** (the callers run
in the HCC service thread; a busy lock drops the mirror frame and counts it, never blocks the
RX path). The monitor wdev is the first VAP wdev whose `iftype == NL80211_IFTYPE_MONITOR`;
the frame is delivered with `netif_rx` and no skb is queued if the monitor dev is down.

## 4. Radiotap layout (pinned vectors, asserted by the host selftest)

All little-endian, header starts at skb->data. `it_len` = total header length.

| variant | when | it_present (u32) | fields (after the 8-byte fixed part) | it_len |
| --- | --- | --- | --- | ---: |
| R0 | no metadata (data frame, no monitor channel stored) | 0x00000000 | none | 8 |
| R1 | channel known (data frame + stored chandef) | 0x00000008 (CHANNEL) | `u16 freq; u16 flags = 0` | 12 |
| R2 | channel + RSSI known (management frame) | 0x00000808 (CHANNEL \| DBM_ANTSIGNAL) | `u16 freq; u16 flags = 0; s8 rssi_dbm` | 13 |

Pinned byte vectors used by `tools/monitor_selftest.c`:

* R0 = `00 00 08 00 00 00 00 00`
* R1 at 2412 MHz = `00 00 0c 00 08 00 00 00 | 6c 09 | 00 00`
* R2 at 2437 MHz, rssi -62 = `00 00 0d 00 08 08 00 00 | 85 09 | 00 00 | c2`

The management source (`hi5622_mgmt_logic.h `himg_rx_frame``) supplies `freq` and `rssi`;
the data source supplies the MPDU only, so it uses the stored `mon_chandef` when present
(R1) and R0 otherwise. Field semantics are documented in the source header; `channel flags`
stay 0 because the driver does not know the per-frame PHY flags (no guessing).

## 5. Fail-closed matrix (asserted by the host selftest)

| condition | action |
| --- | --- |
| `monitor` param > 1 | policy returns error; all mirroring disabled; one rate-limited error log |
| monitor iface absent | no deliverable target; count `no_dev`, no skb |
| monitor iface down | drop; count `dev_down` |
| `mutex_trylock` fails (HCC thread) | drop; count `busy` (never block the RX path) |
| frame len 0 or > 0x618 (RX MPDU max) | refuse; count `bad_len` |
| radiotap buffer too small (internal) | refuse; count `rt_fail` - cannot happen with the fixed 13-byte max, still checked |
| channel op called with no `.set_monitor_channel` support / `monitor=0` | -EOPNOTSUPP (honest "not yet") |
| channel op with an invalid/disabled channel | -EINVAL |
| channel op accepted | store chandef; **no firmware write**; log `monitor channel set (metadata only, no tune)` |

## 6. Host-test plan (no kernel, no bench)

`tools/monitor_selftest.c` compiles `src/hi5622_monitor_logic.c` with the host compiler and
asserts: policy matrix (0/1/2/UINT_MAX); channel->MHz for 2.4 ch 1/6/13/14 and 5 GHz ch
36/100/140/165 + invalid 0/200; the three radiotap vectors byte-for-byte; length bounds
(0 and > max refused); mode naming; that a too-small output buffer is refused. The kernel
integration is checked by the module build (0 warnings) and by source inspection of the
call sites (both sinks call the mirror before their AP-policy gates).

## 7. Non-goals / v2 prerequisites

* **No promiscuous v2** until the hi5622 firmware monitor switch is decoded (TABLES T8 M-1,
  M-2). Family reference cfg id 522 is not a hi5622 message.
* **No monitor TX** (raw radiotap injection). It needs a validated TX path through the
  hidden management/data rings plus cfg80211 radiotap parsing; separate feature.
* **No channel tune**: a monitor-only channel requires a firmware channel message that the
  IMAGE #3 driver does not have for a non-AP VAP; the v1 mirror intentionally follows the AP.
* **No spectral**: spectral is the PSD feature in [SPECTRAL.md](SPECTRAL.md); monitor mode is
  not a prerequisite for it and does not produce it.
