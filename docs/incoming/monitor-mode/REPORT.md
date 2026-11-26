# REPORT - monitor mode + spectral on the Hi5622V100 clean-room stack

**Lane:** `monitor-mode` (shared task **task-241**), **HOST-ONLY**, 2026-11-29.
This lane contacted no device, opened no UART, wrote no flash and fired nothing. Companion
files: [CFG80211-MONITOR.md](CFG80211-MONITOR.md) (design), [SPECTRAL.md](SPECTRAL.md)
(decode), [TABLES.md](TABLES.md) (every line reference), [FIRE-RECIPE.md](FIRE-RECIPE.md)
(the attended ladder), [BUILD-NOTES.md](BUILD-NOTES.md) (the prototype build).

**Grades:** **[V]** verified here from a named local artifact (disassembly, symbol table,
string table, kernel source); **[V\*]** decoded by a committed earlier lane and cited;
**[M]** measured on the bench by an earlier lane; **[I]** inference; **[U]** unknown.

**Context pinned by this lane:** the IMAGE #3 merged module source tree
`/tmp/wifi-image-3-module` (scratch, not committed; 97 files, 64 module params) [V]; the
device-version vendor module `rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko`
(3,564,728 B sha256 `de78ec07...`) [V]; the older `rootfs-2.4.15/lib/hisilicon/ko/
hi5622v100_wifi.ko` (3,581,748 B sha256 `22fa789a...`) [V]; the pinned 6.18.60 kernel tree
(`openwrt-lab-selectorfix-img/.../linux-6.18.60`) [V]; and the GPL-2.0 sibling driver tree
(`hi3881-openharmony/.../hi3881v100/driver`, used as a *family reference*; this lane vendors
no text from it) [V].

---

## 0. Verdict (read this first)

1. **Monitor mode's cfg80211 interface half already works; its capture half cannot work
   today.** The image #3 driver advertises `NL80211_IFTYPE_MONITOR`, creates and changes
   monitor netdevs (ARPHRD_IEEE80211_RADIOTAP) and advertises the full monitor management
   frame bitmap [V, TABLES T2]. It has **no channel op** and **no RX delivery**, and the RX
   delivery chain itself is gated by the F1 (`g1 id13` host-bound RX copy, task-236) [V\*]:
   zero 802.11 frames reach the host today, so no monitor capture can show anything before
   that gate opens.
2. **The vendor driver on this chip has no monitor mode at all.** In both the 2.4.15 and the
   2.5.24 `hi5622v100_wifi.ko` there is **no symbol and no string** containing
   `monitor`/`promis`/`radiotap` (only the unrelated FBT timer), no `set_monitor_channel`
   op, and the cfg80211 `add_virtual_intf` is a fail-stub that logs and returns `-11`
   (0x103a80: `mvn r0, #10`); `change_virtual_intf` only stores the requested iftype in
   the wdev (0x103958) [V, TABLES T3]. The chip wiphy *advertises* the monitor bit
   (`interface_modes = 0x1070` = AP_VLAN|WDS|MONITOR|NAN [V\*], live `iw phy` "AP + STA +
   monitor" [M]) but the feature is not implemented beneath it.
3. **There is therefore no vendor message to port for monitor capture.** The only reference
   is the driver-family GPL-2.0 source (hi3881 OpenHarmony tree), which shows the *shape*:
   `WLAN_CFGID_MONITOR_EN` (cfg id **522**) sent as a WAL config event, hipriv
   `set_monitor [0|1|2|3|4]` (4 report classes: mcast/ucast data, mcast/ucast mgmt), a
   4-bit ptype filter, a per-VAP RX filter whose monitor value is **0x1**, and a receive
   callback carrying `(buf, len, rssi)` [V\*, TABLES T4]. Whether the hi5622 firmware
   implements an equivalent switch is **unproven [U]** - do not send a family-derived
   message to the chip without a decode.
4. **The achievable host-only step is a local-mirror monitor (v1)** - mirror the frames the
   AP VAP already receives (management via the D2H mgmt event, data via the completion-ring
   drain) onto a monitor netdev with a radiotap header, fail-closed behind `monitor=0/1`.
   It is implemented here as a prototype patch on the IMAGE #3 merged tree, host-tested and
   compile-clean (BUILD-NOTES). Once the F1 lands, this turns `mon0` into the bench's second
   observer of the client's auth/assoc exchange - at zero firmware risk.
5. **Spectral is a host-driven PSD (power-spectral-density) register block, not a firmware
   message feature.** The device-version module has 37 `hal_{set,get}_psd_*` accessors over
   **three 32-bit registers at bank +0x430 / +0x434 / +0x438**; every bitfield is decoded in
   [SPECTRAL.md](SPECTRAL.md) / TABLES T6 [V]. The control flow is
   `wal_config_alg_cfg_param_host_entry` -> `hmac_alg_spectral_scan_config_param` ->
   `hmac_get_spectral` -> `hmac_spectral_set_scan_param_proc_SHUANGTA` -> 19 PSD setters
   [V]; init/reinit runs from `hmac_main_init` and `hmac_protection_info_syn_event` [V].
   No firmware H2D message and no firmware-side string was found in the path [V/I].
6. **What spectral still lacks is one decode, not a feature decision:** the register bank's
   **CA base** (the pointer the setters use), and the **report/data memory** (the LMI event
   address/length words at +0x528/+0x52c/+0x530 and the chip-DDR data window). Naming that
   decode is this lane's concrete next step; until it lands, **no spectral register write may
   be attempted on the bench** (FIRE-RECIPE S-rungs are marked blocked).
7. **The 2.5.24 release deleted the 24 host-side `set_spectral_*` argument parsers**
   (phase10 [V\*]) while the alg-cfg command table is unchanged; the get side still answers
   on the live device [M]. The stock set path is therefore *dead on our firmware version* -
   a clean-room implementation should expose its own fail-closed register interface instead
   of reviving alg-cfg strings.

---

## 1. Current state in the IMAGE #3 merged tree (facts)

The monitor-relevant surface of `/tmp/wifi-image-3-module` (64 module params) [V]:

| item | where | state |
| --- | --- | --- |
| `interface_modes` | `hi5622_cfg80211.c:756-758` | STATION \| AP \| **MONITOR** |
| monitor mgmt bitmap | `hi5622_cfg80211.c:663-666` | `tx = rx = 0xffff` |
| `add_virtual_intf` | `hi5622_cfg80211_ops.c:104+` | monitor netdev created; `ARPHRD_IEEE80211_RADIOTAP` at line 171 |
| `change_virtual_intf` | `hi5622_cfg80211_ops.c:333+` | monitor <-> ether type switch at lines 357-362 |
| `.set_monitor_channel` | `hi5622_cfg11_ops` (`hi5622_cfg80211_ops.c:505+`) | **absent** - `iw dev mon0 set channel` fails with `-EOPNOTSUPP` [V, kernel contract T1] |
| monitor RX delivery | - | **absent** (no radiotap builder, no `netif_rx` to a monitor dev anywhere) |
| RX sources | `hcc.c:108` -> `hi5622_mgmt_deliver`; `hi5622_dp_txrx.c:1492` -> `hi5622_sta_ops_rx_deliver` | both exist; both are silent until the F1 opens |
| `iface_combinations` | - | not advertised (kernel does not enforce combinations unless the driver calls `cfg80211_check_combinations`) [V, T1] |
| `software_iftypes` | - | not set (optional; only needed if combinations are ever added) [V, T1] |

The two RX sources are the natural mirror points: the D2H management event already carries
`{freq, channel, rssi, subtype}` (decoded in `hi5622_mgmt_logic.h:159-166`), and the data
path delivers a validated MPDU at `cpu + HIL_RX_PAYLOAD_OFF` (`hi5622_dp_txrx.c:1492`,
`hi5622_sta_ops.c:468`) [V].

## 2. The 6.18.60 kernel contract for a full-MAC monitor (what the driver must supply)

The kernel gives a full-MAC driver **no monitor-RX helper** - there is no
`cfg80211_rx_monitor()` in 6.18.60; the monitor radiotap frame must be built and delivered
by the driver itself. The contract the driver must meet [V, TABLES T1]:

1. `wiphy->interface_modes` must carry `BIT(NL80211_IFTYPE_MONITOR)` (it does).
2. `cfg80211_change_iface()` requires `interface_modes` **and** `.change_virtual_intf`,
   and **WARNs** unless the driver stores the new `iftype`
   (`net/wireless/util.c:1191,1237`; monitor branch at 1261) - the existing op does store it.
3. `iw dev mon0 set channel` reaches `cfg80211_set_monitor_channel()`
   (`net/wireless/chan.c:1544`), which **fails with -EOPNOTSUPP when
   `.set_monitor_channel` is NULL**; `nl80211` dispatches SET_CHANNEL for monitor ifaces
   there (`net/wireless/nl80211.c:3831`). This lane's prototype adds the op.
4. Combinations are only enforced when the driver calls `cfg80211_check_combinations()`
   (`net/wireless/util.c:2579`); with `n_iface_combinations == 0` the kernel does not
   restrict the number of interfaces. If combinations are ever advertised, software iftypes
   must not be listed inside a combination (`net/wireless/core.c:727-731`) and
   `software_iftypes` marks iftypes that do not consume slots
   (`net/wireless/util.c:2928-2930`). Recommendation: keep no combinations for now; add
   `software_iftypes = BIT(MONITOR)` only together with a combination stanza later.
5. The monitor netdev's `type` must be `ARPHRD_IEEE80211_RADIOTAP` (already set) and every
   delivered skb must start with a **radiotap header** followed by the 802.11 MPDU.

## 3. Vendor evidence: monitor mode does not exist in this chip's driver

Extracted here from the local 2.5.24 module (device version) and the 2.4.15 module [V]:

* `wal_cfg80211_add_virtual_intf` @0x103a80: pushes lr, logs `oam_error_log0`, returns
  `mvn r0, #10` (= -11). A 52-byte stub. In cfg80211 terms the vendor driver **cannot
  create any interface** through nl80211.
* `wal_cfg80211_change_virtual_intf` @0x103958: loads `dev->ieee80211_ptr` (`[dev+496]`),
  stores the requested type at `wdev+4` (the `iftype` field), logs an `oam_warning_log0`,
  returns 0. It changes **no firmware state**.
* `.set_monitor_channel` is in the documented **NULL** set of the vendor op table
  [V\* `wifi-driver-re/REPORT.md` sec 2.2].
* `nm`/`strings` over both modules: **zero** hits for `monitor` (apart from the unrelated
  `hmac_fbt_start_monitor_timer`), `promis`, or `radiotap` [V].
* The wiphy still advertises `interface_modes = 0x1070` (AP_VLAN|WDS|MONITOR|NAN)
  [V\* `wifi-driver-re` sec 2.3] and the live vendor box shows "AP + STA + monitor"
  [M `wifi-re-report/TABLES.md:599`] - an advertised capability with no implementation.
* The chip's HMAC VAP create types are **1/2/3** (2 = AP, 3 = STA; 1 unassigned/unknown
  [U]) per the g0 id1 key 0x0064 worker decode [V\*, `wifi-vap-start/TABLES.md` T5/T9].
  Nothing in that enum is a monitor mode, and there is no host-visible monitor VAP record.

**Consequence:** monitor capture on this chip must be *built*, not ported: either (a) mirror
frames the firmware already delivers (the v1 here), or (b) decode/hope for a firmware
promiscuous switch (no evidence on this chip [U]; family reference only, see T4).

## 4. The family reference (GPL-2.0): what a monitor switch looks like in this driver family

The GPL-2.0 OpenHarmony hi3881 driver source (a *sibling* of this driver family, kept local,
**not vendored here**) implements monitor mode under `_PRE_WLAN_FEATURE_PROMIS` [V\*,
TABLES T4]:

| piece | family reference | hi5622 chip |
| --- | --- | --- |
| user command | hipriv `set_monitor [0..4]` (`wal_hipriv.c:3047,6162`) | **no such string/symbol** [V] |
| cfg id | `WLAN_CFGID_MONITOR_EN = 522` (`wlan_mib.h:1052`), dispatch at `hmac_event.c:206` | no equivalent decoded [U] |
| driver send | `hmac_config_set_monitor_switch` sets `mac_dev->promis_switch` and calls `hmac_config_send_event(MONITOR_EN,...)` (`hmac_config.c:4451`) | no equivalent [V] |
| report classes | `wlan_monitor_enum`: OFF, MCAST_DATA, UCAST_DATA, MCAST_MANAGEMENT, UCAST_MANAGEMENT (`wlan_types.h:431`) | - |
| filter | 4-bit ptype filter `{mdata_en, udata_en, mmngt_en, umngt_en}` (`hi_wifi_api.h:680`); per-VAP 32-bit DMAC RX filter: AP UP `0xF7B9FAEA`, AP INIT `0xF7B9FEFA`, **monitor `0x1`** (`hmac_rx_filter.c:392-452` [V\*]) | no equivalent constant in 2.5.24/2.4.15 [V\*, `wifi-sibling-gpl/TABLES.md:114`] |
| RX delivery | `promis_switch` branches in `hmac_rx_data.c:917`, `hmac_rx_filter.c:340`, `hmac_mgmt_classifier.c:303/325`; userspace callback `(buf, len, rssi)` (`hi_wifi_api.h:737`) | - |

The family delivers **raw MPDUs plus an RSSI byte to a callback**, no radiotap, no channel
per frame, and it is gated by a build feature this chip's build does not carry. It is a design
reference only: **do not send cfg id 522 to the hi5622 firmware** - its config table is
different and an unknown config event is a write with unknown effect (project rules).

## 5. The dependency: the F1 RX gate (task-236)

Monitor capture rides the same host-bound RX copy as every AP/STA frame. The current decode
state of that chain [V\*, `wifi-rx-d2h-post/REPORT.md` task-226]:

* the local firmware notification is `g2 id3` (local-dispatch queue 1, handler CA 0xBBC9C,
  no host delivery), and the **host-bound RX copy is `g1 id13`** which has never been seen
  on the host [V\*];
* the F1 of 2026-11-26 measured the SMAC ISR producer moving (`band_obj+0xCC0` +105) while
  the host catalogue showed no RX frames [M];
* the open candidates are C5 (queue not drained), C6 (CRX gates), C7 (transport customer);
  task-236 (`rx-local-dispatch`) is the lane pursuing them.

So monitor mode has **one real blocker**: `g1 id13`. The v1 design below is deliberately
placed at the last mile so it starts working the moment `g1 id13`/its payload path lands:
the two existing delivery functions already receive a verified MPDU (mgmt and data), and the
mirror needs nothing from the firmware.

## 6. Prototype delivered by this lane (local-mirror monitor v1)

Patch `patches/0001-monitor-mirror.patch` on the IMAGE #3 merged tree (base manifest in
`evidence/`; build facts in BUILD-NOTES) [V]:

* `monitor=0` (default): nothing changes, the monitor iface is inert (fail-closed).
* `monitor=1`: frames that reach the two RX sinks are mirrored to the first monitor netdev
  as radiotap + 802.11 MPDU (`netif_rx`), with the channel/RSSI radiotap fields present
  when the source carries them; counters/logs are rate-limited.
* `monitor>1` is refused by policy (disabled + error log) - never guessed.
* `.set_monitor_channel` stores a validated chandef for the radiotap CHANNEL field; it
  **never writes firmware** (the mirror follows the AP's channel). With `monitor=0` it
  returns `-EOPNOTSUPP` - honest: no firmware tune exists yet.
* portable logic (`src/hi5622_monitor_logic.{c,h}`) is host-testable without a kernel:
  mode policy, channel->frequency map (2.4 GHz 1-14, 5 GHz 36-165), radiotap construction
  (8/12/13-byte variants, pinned byte vectors), and length bounds.

This does not claim promiscuous capture: it captures what the AP VAP receives (neighbour
beacons when the firmware forwards them, probes, and - the point - the client's
auth/assoc/deauth once the F1 lands). A true sniffer still needs the firmware switch of
section 4, which this lane could not honestly decode.

## 7. Spectral: decoded surface and the missing decode

The full detail is in [SPECTRAL.md](SPECTRAL.md); the essential facts [V]:

* the feature is the **PSD** block: `hal_set_psd_cfg0_cfg_psd_{fft_size,fft_period,count}`
  (+0x430), `..._cfg1_..._{bottom_noise,rssi_nb,the_power_nb,the_num_nb}` (+0x434),
  `..._cfg2_..._{rpt_mode,agc_lock_en,fftin_len_type,ofdm_det_en,11b_det_en,nb_det_en,
  psd_en,ant_index,p2dbm_offset,deci_coef_man,deci_coef_sel,auto_cg_disable,
  auto_rst_disable,fft_cken}` (+0x438); 19 setters + 17 getters, all plain read-modify-write
  of one mapped bank pointer [V];
* the host command path is the WAL alg-cfg param host entry ->
  `hmac_alg_spectral_scan_config_param` (0x8a530) -> `hmac_get_spectral` (0x896ec) ->
  `hmac_spectral_set_scan_param_proc_SHUANGTA` (0x8ad08) -> PSD setters [V];
* init/reinit: `hmac_spectral_init` (0x8a320) from `hmac_main_init+0xf4` and from
  `hmac_protection_info_syn_event+0x51c` [V]; it memsets two 120-byte per-band state
  blocks and calls `shuangta_init_spectral_param_defaults` - no H2D [V];
* the data side: `shuangta_set_spectral_{psd_en,data_addr,rd_len}`,
  `shuangta_get_spectral_scan_data{in,out}_abnormal`,
  `hmac_spectral_{dlist_choose,alloc_addr_SHUANGTA,get_count,scan_complete}`, and the
  debug dumper `shuangta_spectral_print_ddr_data_to_file` [V]; the report plumbing uses the
  LMI/event registers `cfg_lmi_ctrl1_init_event_addr` (+0x528),
  `cfg_lmi_ctrl2_lmi_psd_en` (+0x52c bit 9), `cfg_lmi_ctrl3_event_mem_max_num`
  (+0x530), `cfg_lmi_ctrl6_event_rpt_num_en` (+0x53c) [V].

Missing (the next decode, host-only, ~one lane): **(a)** the register bank's **CA base**
(the three registers are at `bank+0x430/0x434/0x438`; the pointer comes from the driver's
mapped-bank table - the same .rodata CA table family the RX lanes already decoded);
**(b)** the **report/data memory** semantics (what address the host programs in
`init_event_addr`, where the PSD reports land, and how the host reads that chip memory);
**(c)** the activation sequence/timings and the report format (count/FFT-size driven).
Until those are decoded, spectral has **no PASS-capable rung** and no register write is
allowed.

## 8. What this lane did not do (limits / honesty)

* No bench contact, no fire, no UART, no network to the router; no image or module deployed.
* The monitor prototype is **compile-clean and host-tested, not bench-proven**; its PASS rung
  (FIRE-RECIPE R1) is cfg80211-only and **cannot capture a frame before the F1 lands**.
* The firmware promiscuous switch for the hi5622 is **not decoded** and must not be guessed;
  the family cfg id 522 is a reference, not a message.
* Spectral register writes are **not validated on silicon**; the register map is a static
  decode of the device-version module. The bank base is unknown, so not even a read-only probe
  of these registers is possible yet.
* The spectral data-return format is undecoded; no claim is made that `monitor=1` gives
  spectral data.
* Some vendor semantics are inferred [I] (e.g. that rpt_mode/ant_index have the obvious
  meaning); they are marked so in TABLES/SPECTRAL.
