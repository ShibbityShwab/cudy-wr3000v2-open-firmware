# VERIFY - independent audit of the task-241 monitor-mode claims

**Lane:** monitor2 (task-246). HOST-ONLY. This file re-derives the load-bearing claims of
commit `1a2d068` from local artifacts instead of trusting the prose. Grades: **[V]** verified
here; **[V\*]** committed lane, re-checked; **[M]** bench; **[I]** inference; **[U]** open.

## 1. Artifact integrity (the reason this lane exists)

| claim in task-241 / 1a2d068 | audit result |
| --- | --- |
| "prototype patch `patches/0001-monitor-mirror.patch` on the IMAGE #3 merged tree" | **FALSE as committed**: `git show --stat 1a2d068` lists six .md files only; `patches/` is an empty directory and no patch file exists anywhere in the tree. |
| "`src/hi5622_monitor_logic.{c,h}`, `src/hi5622_monitor.c`, `tools/monitor_selftest.c`, `evidence/`" | **FALSE as committed**: dirs exist but are empty; a filesystem search for `hi5622_monitor*` and `monitor_selftest.c` under /tmp and $HOME found nothing. |
| "BUILD-NOTES.md (the prototype build)" (referenced by REPORT/FIRE-RECIPE/README) | **missing file** - no BUILD-NOTES.md exists in that directory. |
| "implemented here, host-tested and compile-clean" | **not reproducible from the commit**; this lane rebuilt the deliverable from the design text (see BUILD-NOTES). |

## 2. Driver / kernel contract claims

| claim | verdict |
| --- | --- |
| IMAGE #3 driver advertises `MONITOR` and creates monitor netdevs | **[V]** `hi5622_cfg80211.c:756-758` interface_modes = STATION\|AP\|MONITOR; monitor mgmt stypes at :663-669; `hi5622_cfg80211_ops.c:171-172` sets `ARPHRD_IEEE80211_RADIOTAP`; change-iface at :358-361. |
| `.set_monitor_channel` absent -> `iw dev mon0 set channel` = -EOPNOTSUPP | **[V]** the `hi5622_cfg11_ops` initializer has no such member; kernel `net/wireless/chan.c:1544-1552` returns -EOPNOTSUPP when NULL. |
| kernel contract is 2-argument `(wiphy, chandef)` | **correction [V]**: 6.18.60 `include/net/cfg80211.h:5196` is **`(struct wiphy *, struct net_device *, struct cfg80211_chan_def *)`**. The patch and prototype use the real 3-arg form. |
| "monitor channel can be set whenever the op exists" | **correction [V]**: `cfg80211_set_monitor_channel()` first requires `cfg80211_has_monitors_only()` (`net/wireless/core.h:262-267`: num_running_ifaces == num_running_monitor_ifaces && > 0). Counters move on netdev up/down (`core.c:1446-1455`). **Practical rule: `ip link set mon0 up` with the AP iface down, then set the channel; once the AP runs, further SET_CHANNEL returns -EBUSY.** |
| there is no kernel monitor-RX helper; the driver builds radiotap itself | **[V]** no `cfg80211_rx_monitor` in the tree; mac80211 does its own header (`net/mac80211/rx.c:761` sets `skb->protocol=htons(ETH_P_802_2)`, pkt_type PACKET_OTHERHOST) - the prototype follows that practice. |
| vendor driver has no monitor implementation to port | **[V]** 2.5.24 disassembly (`/tmp/2524.dis`): `wal_cfg80211_add_virtual_intf` @0x103a80 is a 52-byte stub ending `mvn r0,#10` (= -11); `wal_cfg80211_change_virtual_intf` @0x103958 only stores the requested iftype at `[wdev+4]` and returns 0. 2.4.15 module strings: the only `monitor` hit is `hmac_fbt_start_monitor_timer`; zero `promis`/`radiotap` hits. |
| vendor wiphy advertises monitor anyway | **[V\*]** cited from `wifi-driver-re`; not re-derived here (needs the vendor wiphy table read, out of scope). |

## 3. Spectral claims

| claim | verdict |
| --- | --- |
| PSD is three 32-bit regs at bank +0x430/+0x434/+0x438 with bitfield accessors | **[V]** re-checked against the accessor disassembly: `fft_size` = `bfi [r1,#0x430], bit0 width3`, `fft_period` = bit4 width12, `psd_en` = `bfi [r1,#0x438], bit8 width1`, `p2dbm_offset` = bit12 width10, `bottom_noise` = +0x434 low byte, `the_power_nb` = +0x434 bits16..22. |
| "19 setters + 17 getters" (SPECTRAL.md) / "37 accessors" (REPORT.md) | **correction [V]**: local symbol tables give **21 `hal_set_psd_*` + 16 `hal_get_psd_*` = 37**. (The two numbers in task-241 cannot both be right; 19+17=36.) |
| init is host-side only (no H2D) | **[V]** `shuangta_init_spectral_param_defaults` @0x3b4e4 writes only the mapped bank through the setters; no frame builder in the path. |
| defaults | **[V] new here**: fft_size=2, fft_period=40, count=2, bottom_noise=-85, rssi_nb=-70, the_power_nb=12, the_num_nb=12, rpt_mode=1, agc_lock_en=1, fftin_len_type=1, ofdm_det_en=1, 11b_det_en=1, nb_det_en=0, psd_en=0, ant_index=0, p2dbm_offset=22 (all written at init/band re-init). |
| bank CA base unknown (S-1 open) | **half-closed here [V]**: the chain `state+0x150 -> 64-byte oal_mem_alloc object -> [obj+0x10] -> mapped bank VA` is decoded, together with the mapping helper that fills obj+0x10 from the 3rd word of a per-band 10-word CA table; the remaining check is which .rodata table the reloc at 0x3aa78 points at (see SPECTRAL-S1.md). |
| "2.5.24 deleted the 24 host-side set_spectral parsers" | **[V\*]** cited from phase10; not re-derived here. Note the low-level setters themselves are live in 2.5.24 (they are called from `shuangta_init_spectral_param_defaults` and `hmac_spectral_set_scan_param_proc_SHUANGTA`), so only the host command parsers are gone. |

## 4. What this audit changes for the Lead

1. Do **not** cite the task-241 "prototype patch/src/tools/build" as existing artifacts -
   use this lane's `patches/0001-monitor-mirror.patch`, `src/`, `tools/`, `evidence/`.
2. Use the corrected FIRE-RECIPE ordering for the monitor channel (monitors-only rule), or
   R1 fails with -EBUSY on a live AP.
3. The monitor v1 capture remains blocked on the **F1 (g1 id13)**; nothing in this lane
   changes that. The mirror hooks are placed so that the F1 fix lights them up unchanged.
