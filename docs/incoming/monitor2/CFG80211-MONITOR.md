# CFG80211-MONITOR - the corrected design and the exact patch delta

**Lane:** monitor2 (task-246). This is the corrected version of the task-241 design; the
patch `patches/0001-monitor-mirror.patch` implements exactly this list. Host-only; nothing
here writes firmware or touches a device.

## 1. Behaviour matrix (unchanged from task-241, validated by the host selftest)

| `monitor` param | monitor iface | `iw dev mon0 set channel` | RX mirror | firmware writes | status |
| --- | --- | --- | --- | --- | --- |
| **0** (default) | yes (netdev inert) | **-EOPNOTSUPP** | none | none | fail-closed baseline |
| **1** | yes | accepted **only while monitors-only** (see §3); stores chandef for radiotap metadata | mgmt+data frames reaching the two existing sinks are mirrored | **none** | v1, host-tested + built |
| **>1** | yes (code unchanged) | -EOPNOTSUPP | none | none | refused |

Non-negotiables: `monitor=1` never programs a channel or any firmware register; the mirror
is what the AP VAP receives, not a promiscuous capture; no monitor TX injection; no spectral
data.

## 2. The kernel contract (6.18.60, pinned tree - corrected)

1. `wiphy->interface_modes` must carry `BIT(NL80211_IFTYPE_MONITOR)` (already does).
2. `cfg80211_change_iface()` needs `.change_virtual_intf` and the driver must store the new
   `iftype` (already does; the existing op WARNs otherwise).
3. **`.set_monitor_channel` has THREE arguments in 6.18.60**:
   `int (*)(struct wiphy *, struct net_device *, struct cfg80211_chan_def *)`
   (`include/net/cfg80211.h:5196`; task-241's two-argument sketch was wrong).
4. `cfg80211_set_monitor_channel()` (`net/wireless/chan.c:1544-1552`) returns
   **-EBUSY unless `cfg80211_has_monitors_only()`** - i.e. only monitor ifaces are
   RUNNING (`net/wireless/core.h:262-267`; counters maintained on netdev up/down,
   `core.c:1446-1455`). This is the correction that changes the FIRE-RECIPE ordering.
5. There is **no kernel monitor-RX helper**: the driver owns the radiotap header and the
   skb delivery. Follow mac80211's choice: `skb->protocol = htons(ETH_P_802_2)`,
   `pkt_type = PACKET_OTHERHOST`, `ip_summed = CHECKSUM_UNNECESSARY`,
   `skb_reset_mac_header()`, then `netif_rx()` (`net/mac80211/rx.c:761`).
6. No interface combinations are advertised, so cfg80211 does not restrict coexistence; if
   they are ever added, keep MONITOR outside the limits (`software_iftypes`).

## 3. Fail-closed matrix (asserted by the host selftest where portable)

| condition | action |
| --- | --- |
| `monitor` > 1 | refused by policy; mirror disabled; no write |
| monitor iface absent | drop; `mon_drop_no_dev++` |
| monitor iface down | drop; `mon_drop_down++` (`netif_running`) |
| cfg lock busy (HCC thread) | drop; `mon_drop_busy++` (mutex_trylock - never blocks RX) |
| `len == 0` or `len > 0x618` | refuse; `mon_drop_bad_len++` |
| radiotap buffer/geometry failure | refuse; `mon_rt_fail++` (cannot happen with 13 B max, still checked) |
| skb alloc failure | drop; `mon_drop_nomem++` |
| channel op at `monitor=0` or `monitor` invalid | -EOPNOTSUPP (honest "not implemented") |
| channel op with NULL/absent chandef | -EINVAL |
| channel op accepted | store chandef; **no firmware write**; log "metadata only - no firmware tune" |

## 4. Patch delta list (what `patches/0001-monitor-mirror.patch` changes)

| # | file | change |
| --- | --- | --- |
| 1 | `hi5622_monitor_logic.h/.c` (new) | portable policy/channel/radiotap logic; host-testable with no kernel headers |
| 2 | `hi5622_monitor.h/.c` (new) | kernel integration (`monitor` param, set-channel op, mgmt/data mirror, counters) |
| 3 | `hi5622_cfg80211.h` | `mon_chandef`, `mon_valid`, 8 counters |
| 4 | `hi5622_cfg80211_ops.c` | `.set_monitor_channel` registration + one-shot mode log at monitor-iface creation |
| 5 | `hi5622_mgmt.c` | mirror call after the D2H mgmt decode, before the `mgmt_rx` gate |
| 6 | `hi5622_sta_ops.c` | entry-guard split; mirror call before the `eapol_rx` gate |
| 7 | `Kbuild` | two objects |

Radiotap vectors (pinned, in the host test): R0 8 B `00 00 08 00 00 00 00 00`; R1 12 B
(CHANNEL, flags 0 - never guessed); R2 13 B (CHANNEL | DBM_ANTSIGNAL). Data frames use the
stored chandef (R1) or R0; management frames carry freq+RSSI (R2 when the RSSI is plausible,
else RSSI 0/absent).

## 5. Non-goals (unchanged)

No promiscuous v2 (needs a decoded firmware monitor switch - vendor driver has none, and the
GPL hi3881 family cfg id 522 is a different chip's message: do not send it), no monitor TX,
no channel tune, no spectral through `mon0`. Capture remains gated by the **F1** (host-bound
`g1 id13`); the hooks are placed so the F1 fix makes them live unchanged.
