# Host-driver reversal — Cudy WR3000 v2.0 (HiSilicon luofu R116, Hi5622V100) Wi-Fi

**Lane:** `wifi-driver-re` (host-only, read-only). **Written:** 2026-11-21.
**Scope:** definitive host-driver reversal for a clean-room 6.18 port or a GPL source review:
(1) the cfg80211/wiphy ops map; (2) the H2D/D2H message service; (3) the HCC mailbox/ETE ring
protocol; (4) the firmware-memory/shared-RAM contract; (5) the calibration container; (6) the
power/link/scan/STA/AP handlers and the exact SDK symbols/headers a GPL request must name.

**Builds on** `wifi-thermal-re/REPORT.md` (firmware-load interface, PCIe inbound map, calibration
container, thermal). That lane decoded the firmware transfer and the calibration container; **this
lane extends, not redoes**, and re-verifies only where a new address is cited.

**Method / limits.** Local files only. The two vendor modules at
`/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/` (2.4.15 set, byte-identical to the parent
repo's `rootfs-2.4.15/`): `hi5622v100_plat.ko` (371,708 B, md5 `f688618b…f992f7d4`) and
`hi5622v100_wifi.ko` (3,581,748 B, md5 `1745ef77…f3dc8731`). Tooling:
`arm-openwrt-linux-muslgnueabi-objdump/readelf/nm` (openwrt toolchain), `pyelftools 0.33` +
`capstone 5.0.7` in `.provision-venv`, and the 5.10.201 kernel tree
`build/linux-kbuild/linux-5.10.201` for `struct cfg80211_ops`/`struct wiphy` field order. **All
addresses are module link-time offsets** (section vaddr 0), matching the thermal lane's convention.
**No device was touched**; no vendor binary/firmware/calibration blob is committed; per-unit
MAC/serial/die-id/calibration values are redacted.

**Evidence markers:** [V] re-verified by this lane from a named local artifact (host-side, read-only);
[V*] device-measured by an earlier attended lane (cited doc); [I] inference; [U] unknown.

---

## 1. Artifacts and tooling

| artifact | location | establishes |
| --- | --- | --- |
| vendor modules (2.4.15) | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/` | every address in this report |
| kernel 5.10.201 headers | `build/linux-kbuild/linux-5.10.201/include/net/cfg80211.h`, `regulatory.h` | cfg80211_ops/wiphy field order, regulatory flags |
| prior RE record | `opensource/docs/phase{8,17,18,22,27,42,43,49}`, `DRIVER-BLACKBOX.md`, `PORT-SURFACE.md`, `wifi-thermal-re/REPORT.md` | device-measured doorbells/rings ([V*]), firmware-load, calibration |
| disassembler | openwrt `objdump`; pyelftools reloc reader | fresh function/struct decode |

Module identity is re-verified in this lane (md5 above matches the thermal lane's 2.4.15 set), so
addresses are directly comparable with `wifi-thermal-re`. Section layout (pyelftools): wifi.ko
`.text` 0x16285c, `.rodata` 0x25184, `.rodata.str1.4` 0xafec3, `.data` 0x16768, `.bss`
0x4d910; plat.ko `.text` 0x1d324, `.rodata` 0x1463, `.data` 0x2d3d, `.bss` 0x4ee4.

---

## 2. cfg80211 / wiphy ops map (complete)

### 2.1 How the ops struct was recovered

The driver is a **cfg80211 full-MAC**: it registers its own wiphy+netdevs and imports 25
cfg80211/wiphy symbols + 1 mac80211 helper (`ieee80211_get_channel_khz`); 309 undefined imports
total in wifi.ko, 126 in plat.ko. The `struct cfg80211_ops` is a non-const `.data` object with
`.rel.data` relocations to the `wal_cfg80211_*` callbacks. The struct base was pinned two
independent ways: (a) the first relocation (`add_virtual_intf`) sits at `.data+0xebc`, three
NULL slots (`suspend/resume/set_wakeup`) before it; (b) the `wiphy_new_nm` call site loads the
ops pointer from the literal pool:

```
wal_cfg80211_init  @ 0x10686c
  0x10699c: mov r0, fp               ; fp loaded from literal @0x106ce4 = 0x00000eb0
  0x1069a4: mov r1, #4               ; sizeof_priv = 4 bytes
  0x1069a8: bl  wiphy_new_nm         ; wiphy_new_nm(ops=0xeb0, 4, NULL)
  0x106900: bl  wiphy_apply_custom_regulatory
  0x106908: bl  wiphy_register
```

So `struct cfg80211_ops` is at **wifi.ko `.data + 0xeb0`**, `sizeof_priv = 4` (a single word —
a band/board handle), and the wiphy uses the **custom regulatory** path (the INI
`[CUSTOM_REGDOMAIN_CFG]` table), not the kernel's global regdb.

### 2.2 The full callback map (kernel 5.10 field order)

Field indices assume **CONFIG_NL80211_TESTMODE is off** (verified: `external_auth` lands at field
103, which only fits if `testmode_cmd/testmode_dump` are absent). 39 of the 112 slots are
populated; every other slot is NULL (unimplemented). Addresses are link-time `.text` offsets.

| field | cfg80211_ops member | internal function | addr | size |
| --- | --- | --- | ---: | ---: |
| 3 | `add_virtual_intf` | `wal_cfg80211_add_virtual_intf` | 0x103878 | 0x34 |
| 4 | `del_virtual_intf` | `wal_cfg80211_del_virtual_intf` | 0x103938 | 0x34 |
| 5 | `change_virtual_intf` | `wal_cfg80211_change_virtual_intf` | 0x103750 | 0x78 |
| 6 | `add_key` | `wal_cfg80211_add_key` | 0x1007f4 | 0x350 |
| 7 | `get_key` | `wal_cfg80211_get_key` | 0x100b44 | 0x254 |
| 8 | `del_key` | `wal_cfg80211_remove_key` | 0x100d98 | 0x264 |
| 9 | `set_default_key` | `wal_cfg80211_set_default_key` | 0x100ffc | 0x19c |
| 10 | `set_default_mgmt_key` | `wal_cfg80211_set_default_mgmt_key` | 0x101198 | 0x190 |
| 12 | `start_ap` | `wal_cfg80211_start_ap` | 0x10342c | 0x324 |
| 13 | `change_beacon` | `wal_cfg80211_change_beacon` | 0x1018a4 | 0x1f4 |
| 14 | `stop_ap` | `wal_cfg80211_stop_ap` | 0x101ba4 | 0x184 |
| 15 | `add_station` | `wal_cfg80211_add_station` | 0x1046e8 | 0x8 (stub) |
| 16 | `del_station` | `wal_cfg80211_del_station` | 0x102268 | 0x29c |
| 17 | `change_station` | `wal_cfg80211_change_station` | 0x1038c0 | 0x8 (stub) |
| 18 | `get_station` | `wal_cfg80211_get_station` | 0x102504 | 0x47c |
| 19 | `dump_station` | `wal_cfg80211_dump_station` | 0x1061d8 | 0x140 |
| 33 | `change_bss` | `wal_cfg80211_change_bss` | 0x1038c8 | 0x8 (stub) |
| 37 | `scan` | `wal_cfg80211_scan` | 0x100074 | 0x314 |
| 43 | `connect` | `wal_cfg80211_connect` | 0x100388 | 0x2b4 |
| 45 | `disconnect` | `wal_cfg80211_disconnect` | 0x10063c | 0x1b8 |
| 49 | `set_wiphy_params` | `wal_cfg80211_set_wiphy_params` | 0x101348 | 0x34 |
| 54 | `set_bitrate_mask` | `wal_cfg80211_set_bitrate_mask` | 0x103ff4 | 0x34 |
| 55 | `dump_survey` | `wal_cfg80211_dump_survey` | 0x104698 | 0x50 |
| 56 | `set_pmksa` | `wal_cfg80211_set_pmksa` | 0x10396c | 0x1cc |
| 57 | `del_pmksa` | `wal_cfg80211_del_pmksa` | 0x103c28 | 0x1e0 |
| 58 | `flush_pmksa` | `wal_cfg80211_flush_pmksa` | 0x103e08 | 0xfc |
| 59 | `remain_on_channel` | `wal_cfg80211_remain_on_channel` | 0x103904 | 0x34 |
| 60 | `cancel_remain_on_channel` | `wal_cfg80211_cancel_remain_on_channel` | 0x1038d0 | 0x34 |
| 61 | `mgmt_tx` | `wal_cfg80211_mgmt_tx` | 0x1056d8 | 0x754 |
| 62 | `mgmt_tx_cancel_wait` | `wal_cfg80211_mgmt_tx_cancel_wait` | 0x1038b8 | 0x8 (stub) |
| 63 | `set_power_mgmt` | `wal_cfg80211_set_power_mgmt` | 0x104ea4 | 0x24c |
| 67 | `update_mgmt_frame_registrations` | `wal_cfg80211_mgmt_frame_register` | 0x104634 | 0x64 |
| 70 | `sched_scan_start` | `wal_cfg80211_sched_scan_start` | 0x101d28 | 0x4e4 |
| 71 | `sched_scan_stop` | `wal_cfg80211_sched_scan_stop` | 0x10220c | 0x5c |
| 77 | `get_channel` | `wal_cfg80211_get_channel` | 0x104028 | 0x2bc |
| 78 | `start_p2p_device` | `wal_cfg80211_start_p2p_device` | 0x1038ac | 0x8 (stub) |
| 79 | `stop_p2p_device` | `wal_cfg80211_stop_p2p_device` | 0x1038b4 | 0x4 (stub) |
| 80 | `set_mac_acl` | `wal_cfg80211_set_mac_acl` | 0x106598 | 0x188 |
| 103 | `external_auth` | `wal_cfg80211_external_auth` | 0x1046f0 | 0x158 |

**Notably NULL** (the driver does *not* implement these, which fixes its behaviour class):
`suspend/resume/set_wakeup`, `set_default_beacon_key`, all mesh/OCB callbacks,
`set_txq_params`, `set_monitor_channel`, `abort_scan`, `auth/assoc/deauth/disassoc`,
`update_connect_params`, `join_ibss/leave_ibss`, `set_mcast_rate`, **`set_tx_power` and
`get_tx_power`**, `set_wds_peer`, `rfkill_poll`, CQM, antenna, rekey, TDLS, radar/CAC/CSA,
NAN, PMK, `tx_control_port`, PMSROWE, TID-config. It is therefore a **connect/disconnect SME
driver** (uses `cfg80211_connect`/`cfg80211_connect_done`, not auth/assoc), and **tx-power is
private-ioctl only** (the `wal_ioctl_set_txpower/get_txpower` private handlers, not the cfg80211
ops — see the `.rel.rodata` ioctl table at 0x9bcc/0x9bd0). Power limits are data-driven from the
INI `[HOST_WIFI_NORMAL]`/`[HOST_WIFI_PWR_LIMIT_LU]` tables.

### 2.3 wiphy registration, bands and features

`wal_cfg80211_init` (`0x10686c`) loops per band: `hmac_board_get_instance` →
`hmac_band_get_by_id` → `hmac_band_id_to_band_type` → `hmac_device_check_2g_enable` /
`hmac_device_check_5g_enable`, then `wiphy_apply_custom_regulatory` + `wiphy_register`.
Observed wiphy field writes (link-time offsets; field names via 5.10 `struct wiphy`): an
`iface_combinations`-style pointer/value 0x93ec at `[wiphy+16]`, `interface_modes = 0x1070`
(AP_VLAN | WDS | MONITOR | NAN) at `[wiphy+20]`, `max_scan_ssids = 12`, a 128-word scan
buffer at `[wiphy+34]`, `max_sched_scan_ssids`-family values, and two per-band tables at
`[wiphy+0xcc]` (2g) / `[wiphy+0xd0]` (5g). Live `iw phy` readouts (DRIVER-BLACKBOX §9,
[V*]): 2.4 GHz advertised 30.0 dBm; 5 GHz 23.0/24.0 dBm; 5 GHz runs 160 MHz on ch 44, 2.4 GHz
40 MHz on ch 6; UCI `txpower=100`. The regulatory flags come from the INI country tables, and the
device is configured `country=US` while the shipped INI carries only BZ/DE/LU rows (a mismatch
already flagged in DRIVER-BLACKBOX §10).

### 2.4 The private-ioctl surface (sibling to cfg80211)

The `wal_config_*` / `wal_ioctl_*` / `wal_hipriv_*` tables in `.rel.rodata` (offsets
0x8a20-0x9be0) register the private wext/iwpriv surface: `alg` (0x0101) and `cali` (0x0102)
dispatch (DRIVER-BLACKBOX §12), `setcountry/getcountry`, `set_txpower/get_txpower`,
`set_hwaddr`, `get_channel`, `get_mode`, `set_wme_params`, `get_chipid/get_dieid`,
and ~200 `wal_hipriv_*`/m2u/isolation/11v handlers. This is the vendor userland ABI a 6.18 port
must reproduce or drop.

---

## 3. H2D / D2H message service (decoded)

The message service is **layered**: a PCIe mailbox/ETE transport (`pcie_msg_*`, plat.ko), an HCC
message object/dispatch layer (`hcc_msg_*`, plat.ko), and the MAC/MLME command/event catalog
(`hmac_*`, wifi.ko).

### 3.1 The message header (decoded from hcc_msg_process + hmac_chan_initiate_switch_to_new_channel)

The `hcc_msg` object has a header pointer at **object offset 0x118**. The header is:

```
struct hcc_msg_header {          /* at *(hcc_msg_obj + 0x118) */
    u8  type;    /* +0x00  message type; low 4 bits = group (0=chip, 1/2=core) */
    u8  bid;     /* +0x01  band id; bits[7:4]=dst band, bits[3:0]=src */
    u8  vap_id;  /* +0x02 */
    u8  chip_id; /* +0x03 */
    u16 flag;    /* +0x04  nonzero => count stats (hcc_msg_debug) */
    u16 id;      /* +0x06  message/command id (the dispatch key) */
    u8  rsvd;    /* +0x08 */
    u8  dst;     /* +0x09  destination core/band, set by hcc_msg_tx */
    u8  params[];/* +0x0a  payload (channel-switch uses +0x0c..+0x12) */
};
```

Evidence: `hmac_chan_initiate_switch_to_new_channel` (`0x46f1c`) fills
`hdr->id=21` (@+6), `hdr->vap_id`, `hdr->chip_id`, `hdr->bid` (bfi band bits), and params
@+0x0c..+0x12; `hcc_msg_process` reads `type&0xf` (@+0) and `id` (@+6, u16); `hcc_msg_tx`
reads `bid` (@+1) bits[7:4] and stores the destination band @+9.

### 3.2 Dispatch algorithm (hcc_msg_process, plat.ko 0x1204c)

`hcc_msg_process(reg_tab, msg_obj)` — the D2H dispatch core:

```
hdr   = *(msg_obj + 0x118)
group = hdr->type & 0xf                 ; 0=chip, 1/2=core
id    = *(u16*)(hdr + 6)
item_array = *(reg_tab + 0x10)          ; array of 12-byte group records
count      = item_array[group*12 + 0]   ; max message id
handler_tab= item_array[group*12 + 4]   ; array of 16-byte handler records
if (id < count) {
    entry = handler_tab + id*16
    func  = entry[1]                    ; handler at +4
    if (func) func(...)
}
```

The **handler record is 16 bytes**: `{u32 id; u32 func; u32 aux; u32 stats}` (`func` at +4; the
stats counter at +12 is incremented by `hcc_msg_debug` 0x120fc). `hcc_msg_get_item`
(`0x12198`) exposes the same indexing.

### 3.3 Registration (hcc_msg_register_tab family, plat.ko 0x117e4..0x11a94)

* `hcc_msg_register_tab_chip(group_id, handler_tab, count)` → `customise(0, …)`.
* `hcc_msg_register_tab_core(group_id, handler_tab, count)` → `customise(1,…)` **and**
  `customise(2,…)` (one registration serves both core groups).
* `customise` merges the **input table** (12-byte records `{u16 id, u32 func, u32 aux}`) into the
  16-byte dispatch records (`entry[id].func = input.func`).

Registration sites ([V], `.rel.text` call sites):

| caller | call | group | count | input table |
| --- | --- | --- | ---: | --- |
| `hmac_main_init` (`0x3f6c8`) | `…_register_tab_chip` | r7 | 28 | `.data+0x584` |
| `hmac_main_init` | `…_register_tab_chip` | 2 | 12 | `0x6d4` |
| `hmac_main_init` | `…_register_tab_chip` | 3 | 8 | `0x764` |
| `hdpp_main_init` (`0x12e0`) | `…_register_tab_chip` | 3 | 5 | `g_hdpp+4` |
| `hdpp_main_init` | `…_register_tab_core` | 0 | 7 | `g_hdpp+64` |

### 3.4 H2D transmit path (host → device)

```
hmac_send_msg            wifi.ko 0x46eb4   (arg: board, msg_obj)
  -> hcc_msg_tx          plat.ko 0x11bf8   ; sets hdr->dst, hcc_queue_add_msg
  -> hcc_thread_process_tx  plat 0x11418   ; TX thread drains queue
  -> pcie_msg_send       plat.ko 0x160f4   ; post SR node
  -> pcie_msg_send_irq   plat.ko 0x174a8   ; the doorbell write (see §4.2)
```

`hmac_hcc_msg_tx_send` (`0x40320`) is the same entry for the MAC layer. `hmac_send_msg`
(`0x46eb4`) on TX failure frees the message and falls through to `hmac_chan_tx_cali_sync`
(`0x46d9c`).

### 3.5 D2H receive path (device → host)

```
IRQ -> pcie_msg_handle     plat.ko 0x171f8
   read D2H bitmap (0x40039014), write 0 to drain it
   for each set bit id (0..10): call d2h_handler_table[id].func(arg)
   -> posts the message up to hcc_msg_process (group/id dispatch) ->
        the registered hmac_*/hdpp_* handlers below
```

The per-id D2H handler table at the PCIe layer is registered by `pcie_msg_init` (`0xb6e4`) via
`pcie_msg_register` for ids **1, 3, 6, 7** (the raw PCIe message ids).

### 3.6 The D2H command/event catalog

**Boot messages** (plat.ko `.data+0x2660`, 5 records × 12 B `{u32 id, func, u32 core}`):

| id | handler | addr |
| ---: | --- | ---: |
| 0 | `oam_rx_post_action_function` | 0xdabc |
| 1 | `device_plat_ready_msg_process` | 0xeb78 |
| 2 | `host_ready_msg_process` | 0xebd8 |
| 3 | `exception_info_msg_process` | 0x18c0 |
| 4 | `heartbeat_msg_process` | 0x10214 |

This **extends** the thermal lane's "id 1 / id 2" decode: there are five boot messages, not two
(the thermal lane under-read the 12-byte record stride). `exception_panic_msg_process` (`0x136c`)
is a sibling panic handler.

**MAC event catalog** (wifi.ko `.data+0x584`, 28 records; the firmware→host async event ids). The
function pointers sit at a 12-byte stride with ids interleaved; the complete handler name list
([V], `.rel.data` relocations):

```
hmac_sdt_up_reg_val            hmac_create_ba_event          hmac_del_ba_event
hmac_event_config_syn          hmac_syn_info_event           hmac_bandwidth_info_syn_event
hmac_protection_info_syn_event hmac_ch_status_info_syn_event hmac_rx_process_mgmt_event
hmac_mgmt_rx_delba_event       hmac_scan_proc_scanned_bss    hmac_scan_proc_scan_comp_event
hmac_scan_process_chan_result_event  hmac_event_acs_response hmac_mgmt_send_disasoc_deauth_event
hmac_chan_switch_to_new_chan_complete  hmac_mgmt_tbtt_event  hmac_dfs_radar_detect_event
hmac_proc_disasoc_misc_event   hmac_acs_process_rescan_event hmac_csi_complete
hmac_sr_bss_color_change_event hmac_sr_syn_parameter_event   hmac_chr_report_msg
hmac_config_set_security_port_event  hmac_update_fbt_scan_result  hmac_pfm_detect_process_event
hmac_spectral_scan_complete    hmac_phy_event_complete       hmac_mgmt_deauth_msg_process
hmac_ba_action_msg_process     hcc_timer_process             hmac_mgmt_read_error_msg_process
hmac_mgmt_send_deauth_frame_process  hmac_del_user_msg_process  hmac_rx_data_send_disasoc_frame_process
hmac_smps_update_device_capbility  hmac_pfm_hiex_rx_local_msg  hmac_bsd_update_msg
hmac_multiap_report_11v_event  hmac_d2h_pm_event             hmac_d2h_temp_state_update
hmac_receive_beacon_probrsp_to_reprot  hmac_report_beacon_frame
```

This is the firmware→host event surface a clean-room host must consume: scan results
(`hmac_scan_proc_*`), BA add/del, ACS, DFS radar, CSI, spectral, TBTT, channel-switch-complete,
and the config/alg sync dispatcher `hmac_event_config_syn` (`0x6b0e0`) — the entry that then
routes `hmac_sync_dmac_alg_cfg_rsp_entry` (`0x87b58`) / `hmac_sync_dmac_cali_cfg_rsp_entry`
(`0x87f8c`) / `hmac_chan_tx_cali_sync` (`0x46d9c`) — matching the kprobe observation in
DRIVER-BLACKBOX §15 ("`hmac_event_config_syn+0x150`").

### 3.7 Completion / wait, request/response pairing

* **Host→device sync/request**: the `wal_sync_query_*` / `wal_ioctl_sync_*` family
  (`wal_ioctl_sync_get_no_rsp` `0x10afac`, `wal_ioctl_sync_query_info` `0x10b394`,
  `wal_sync_query_dmac_sta_info` `0x117c2c`) sends a request and waits on a completion;
  `wal_msg_request_add_queue` (`0x126808`) / `wal_set_dev_msg_response_by_addr`
  (`0x126908`) / `wal_set_host_msg_response_by_addr` (`0x1269d8`) / `wal_check_and_release_msg_resp`
  (`0x109370`) pair a request id with its response buffer.
* **Request id word**: per DRIVER-BLACKBOX §15, `hmac_sync_dmac_alg_cfg_rsp_entry` carries a
  per-request id word (`0x0d01xxxx`, 2g/5g variants) used to match request→response.
* **Boot completion**: `multi_chip_loading` (`0xf2ac`) uses `wait_for_completion_timeout`
  (200 jiffies for id-1 plat-ready, 2000 jiffies for id-2 host-ready) — already decoded in the
  thermal lane.
* **H2D doorbell back-pressure**: `pcie_msg_wait_for_clr` (`0x173dc`) spins until the doorbell
  word drains before the next write (the "keep the mailbox drained" discipline, §4).

---

## 4. HCC mailbox / ETE ring protocol

### 4.1 Node layout (consolidated from phase42/43, re-verified where cited)

* **ETE channel block** at device CA `0x4003a000` (host BAR0+0x3b8000 region-3 window): **7
  channels × 0x50 bytes** — SR0..SR2 at `+0x400/+0x450/+0x4a0`, DR0..DR3 at
  `+0x590/+0x5e0/+0x630/+0x680`. Per-channel: `+0x00` enable (bit0), `+0x08` ctrl,
  `+0x10/0x14/0x18/0x1c` program-group A (base/depth-1/producer/consumer), `+0x28` resource,
  `+0x30/0x34/0x38/0x3c` program-group B, `+0x48` second enable. SR uses group A for the host
  ring, DR uses group B for the host ring.
* **Ring node = 8 bytes**: `word0 = device VA of the buffer`, `word1 = (len<<16) | flags`,
  observed flag `0x6d2b` (0xd2b | 0x2000 | 0x4000).
* **MAC host-interface ring block** at CA `0x40040000` (band-0 slot 5) / `0x40060000` (band-1):
  RX free/cmp ring base+size @ `+0x20..+0x34`, six wptr/rptr @ `+0x38..+0x54`, int status
  `+0x44`, int mask `+0x48`.

### 4.2 Doorbell / mailbox write sequence (fresh disassembly, [V])

`pcie_msg_send_irq` (plat.ko `0x174a8`), the H2D kick, decoded:

```
ctx = pcie msg context
*(ctx+0x40) = 8;            ; write 8 to the H2D pending-mask word  CA 0x40039010
pcie_msg_wait_for_clr(ctx+0x2c);   ; poll the doorbell word until 0
spin_lock(ctx+0x50)
if (*(ctx+0x48)) {
    *(ctx+0x2c) = *(ctx+0x48);    ; write pending value to doorbell  CA 0x400392d4
    *(ctx+0x48) = 0;
    *(ctx+0x34) |= 1;             ; set bit 0 of the twin/ack word
}
spin_unlock
```

`pcie_msg_handle` (`0x171f8`), the D2H receive, decoded:

```
*(ctx+0x0c) = 1;              ; ack/clear
bitmap = *(ctx+0x04);         ; read the D2H message bitmap  CA 0x40039014
*(ctx+0x04) = 0;              ; drain it (device P2 precondition)
*(ctx+0x10) = 1;              ; re-enable
for each set bit id (0..10):
    func = *(ctx+0x20 + id*8); arg = *(ctx+0x20 + id*8 + 4);
    if (func) func(arg);
```

The CA↔register mapping (doorbell addresses are **device-measured** facts from phase42/43/49,
[V*]; the register-write order above is new [V]):

| role | CA | host window (region-3 rule `BAR0=0x3b8000+(CA-0x40000000)`) |
| --- | --- | --- |
| H2D pending-mask (`out[0]`) | `0x40039010` | `0x3f1010` |
| D2H message bitmap (`out[1]`) | `0x40039014` | `0x3f1014` |
| H2D doorbell (`out[2]`) | `0x400392d4` | `0x3f12d4` |
| twin copy B (copy A + 0x800) | `0x40039800` | `0x3f1800` |
| copy A clear (forbidden) | `0x400392f0` | — |
| CPU release | `0x40000108` | `0x3b8108` |
| announce gate (`0xcece`) | `0x4000010c` | `0x3b810c` |

### 4.3 IRQ path

* `do_request_irq` (`0x1081c`) → `bal_irq_enable` (`0x10c78`) → `pcie_irq_enable`
  (`0x7fb0`) → `oal_enable_pcie_irq` (`0x7d5c`). Interrupts are **INTx** (IRQ 207/209), not
  MSI, even though the endpoint is MSI-capable ([V*]).
* `shuangta_host_chip_irq_init` (`0x3a6cc`) / `shuangta_irq_host_all` (`0x3a394`) /
  `shuangta_irq_host_mac_isr` (`0x39a1c`) are the wifi.ko host-MAC ISR family; the MAC int
  status/mask words are the slot-5 block `+0x44/+0x48`.
* The **unresolved clean-room gate** (phase22-27/49): the device's message service is entered only
  after the host writes the `0xcece` announce magic to CA `0x4000010c`, and the doorbell→
  line-0x4C delivery was never consummated in the from-scratch line (SGI-bank competition,
  `0x4C` pending/never-consumed). This lane does **not** reopen that device-side gate — it is
  host-only — but it now documents the *host* side the gate feeds.

---

## 5. Firmware-memory / shared-RAM contract (consolidated)

From the thermal lane (§2.4, [V]/[V*]) — unchanged, restated as the contract:

* **Six inbound regions** (`g_shuangta_region_types` `.data+0x29d8`): 0 `0x00000000-0x001bffff`
  (ROM_WRAM), 1 `0x00400000-0x00417fff` (TCM_NOACP), 2 `0x01000000-0x011dffff` (PKTRAM_NOACP),
  3 `0x40000000-0x4011ffff` (IO), 4 `0x02000000-0x021dffff` (ACP), 5 `0x01200000-0x01417fff`
  (ACP).
* **Firmware target** CA `0x01240000` (region 5) → host BAR0+0x6f8000 (alias BAR0+0x40000).
* **Transfer**: contiguous `memcpy_s` of the whole 928,920-byte blob through a 0x80000-byte bounce
  buffer (chunks 524,288 + 404,632 B); no rings/doorbell in the firmware transfer.
* **CPU release**: `bal_write(0x40000108, 0x5a5a, 4)` — descriptor `{0x40000108, 0x5a5a, 4}` at
  `.data+0x26a4` (re-verified this lane, [V]).
* **Shared memory**: host DRAM rings (SR `0x844d9000..`, DR `0x844d5000..`, device-measured
  [V*]) posted as ETE node arrays; device RAM ring groups at `0x0106xxxx`.
* **Status words**: D2H bitmap `0x40039014` (drained by host), H2D pending `0x40039010`/
  doorbell `0x400392d4`, announce gate `0x4000010c`.

---

## 6. Calibration container (consolidated from phase8 + thermal lane)

The dual-band store `/usr/local/factory/wifi_cali_data.kv` (8,912 B = `0x22d0`) and the 2g store
`wifi_cali_data_2g.kv` (2,336 B = `0x920`) are **verbatim images of driver RAM structs**
written by one `oal_kernel_file_write`:

| function | addr | role |
| --- | ---: | --- |
| `hmac_save_cali_data_to_file` | 0xc4044 | dispatcher: calls 2g then 5g |
| `hmac_save_cali_data_to_file_2g` | 0xc3f4c | writes 2g struct (0x920) |
| `hmac_save_cali_data_to_file_5g` | 0xc3e54 | writes dual struct (0x22d0) |
| `hmac_save_cali_data_verify_2g` | 0xc3d3c | rewrites RAM `0xfeedfeed` → `0x5a5a5a5a` / `0xa5a5a5a5` |
| `hmac_save_cali_data_verify_5g` | 0xc3c20 | same, tail offset 0x22cc |
| `hwifi_rf_cali_file_load_5g` (reader) | plat 0x2e44 | alloc 0x22d0, `kernel_read`, checks `[0]==0x5a5a5a5a` && `[0x22cc]==0xa5a5a5a5` |
| `hwifi_rf_cali_file_load_2g` (reader) | plat 0x2c94 | 0x920 / 0x91c twin |

On-disk layout: `ZZZZ (0x5a5a5a5a) | payload (len−8) | a5a5a5a5`. The payload is an **opaque
driver struct** (no DWARF in the .ko) — field-by-field semantics remain [U]; only the container
offsets, counts, and observed 38-word periodicity are established (phase8 §5). The two blobs are
**different objects, not a prefix/nesting** (longest common prefix 5 bytes). Per-unit values are
redacted; no blob is committed here.

---

## 7. Power / link / scan / STA / AP handlers + SDK symbols (GPL request)

### 7.1 The handler chains (link-time addresses)

| area | entry | addr |
| --- | --- | ---: |
| **power on** | `wlan_power_on` → `wlan_bal_init_process` → `hwifi_rf_cali_file_load` → `firmware_download_function` → `plat_init_bal_hcc_excp` → `bal_irq_enable` | 0xe46c / … / 0xe380 / 0x10c78 |
| **D2H GPIO/PM** | `wlan_pm_d2h_gpio_irq_callback` | plat 0xe734 |
| **scan** | `wal_cfg80211_scan` → `wal_cfg80211_start_scan` → `hmac_cfg80211_start_scan_sta`; results via `hmac_scan_proc_scanned_bss`/`_scan_comp_event`/`_chan_result_event` | 0x100074 / 0x127c54 / 0x975d8 / 0x600..0x618 |
| **STA assoc** | `wal_cfg80211_connect` → `hmac_sta_sync_vap` / `hmac_sta_sync_join_req_params`; done via `cfg80211_connect_done` | 0x100388 / 0x52c70 / 0x52f38 |
| **AP** | `wal_cfg80211_start_ap` → `wal_cfg80211_start_ap_write_msg` → `hmac_vap_sync_add`/`hmac_config_add_vap_sync_all` | 0x10342c / 0x103340 / 0x92e4c / 0x15be44 |
| **link** | `wal_pcie0/1_link_down_work` (PCIe link-down recovery) | wifi.ko |
| **data path** | `hdpp_*` (`hdpp_stat_rx_*`, `hdpp_user/vap/device_sync_*`, `hmac_tx_msdu_ring_*`) | §3 of DRIVER-BLACKBOX |

### 7.2 The SDK symbol families a GPL request must name

The vendor C is organized in HiSilicon WLAN SDK layers; a GPL request must ask for the
**Hi5622V100 / "ShuangTaV100" / ChenTang host Wi-Fi SDK** (the tree that produces
`hi5622v100_plat.ko` + `hi5622v100_wifi.ko`), and name these symbol families (counts = symbol
strings, not functions):

| family | wifi.ko strings | role / likely SDK headers |
| --- | ---: | --- |
| `hmac_*` | 2,023 | host MAC / MLME (`hmac_*.h`, `hmac_config.c`, `hmac_scan.c`, `hmac_ba.c`, `hmac_11i.c`) |
| `wal_*` | 1,014 | WLAN abstraction / cfg80211 glue (`wal_cfg80211.c`, `wal_ioctl.c`, `wal_config.c`) |
| `hdpp_*` | 238 | host data path (`hdpp_*.h`, rings/queues) |
| `alg_*` | 239 | algorithm layer (`alg_cfg.c`, rate control, calibration params) |
| `shuangta_*` | 147 | chip-codename-specific layer |
| `hal_*` | 99 | hardware abstraction (register accessors) |
| `oal_* / oam_*` | 43 / 33 | OS / operation abstraction |
| `hcc_* / pcie_* / bal_*` | (plat.ko) | host-comm / PCIe / bus-abstraction (`hcc_msg.c`, `pcie_msg.c`, `bal_msg.c`) |
| `mac_* / dmac_*` | 367 / — | device-MAC / firmware-side MAC |
| `smac_*` | (FIRMWARE.bin) | firmware SMAC (the closed side — not in the host .ko) |

**Also required** (the "wider closure" already flagged in PORT-SURFACE.md / thermal lane): the
`HW_RTOS_NS` export-namespace provider (211 tags on wifi.ko / 46 on plat.ko), the
`hi_woc_wifi_rtx.ko` (`hi_wifi_*` `__symbol_get` hooks) and `hi_kcfe_lrn.ko`
(`hi_cfe_flush_*`) runtime hooks, `ksecurec`, and `hi_kpie`. Without the vendor tree the port
is blocked at link time regardless of this lane's map.

---

## 8. Verdict and corrections

**Functional map + interface contract: achievable, and now essentially complete on the host side.**
The ops map (39/112 callbacks), the message header + dispatch + registration, the doorbell write
sequence, the boot handshake (5 messages), the D2H event catalog, and the calibration container are
documented with addresses and disassembly snippets. A clean-room author can now, from this report,
write the host-side skeleton (wiphy registration, message framing, doorbell access) without the
vendor tree.

**Recompiling the 3.6 MB module from disassembly: not viable, and this lane re-confirms it.**
`hi5622v100_wifi.ko` has 4,247 functions / 1,452,124 B of `.text`, no DWARF, 309 undefined
imports (25 cfg80211 + 1 mac80211 + 211 namespaced + 72 others), and a vendor C MAC/MLME of 2,023
`hmac_*` symbols. The honest ceiling of a host-only pass is *this report's interface map*, not a
buildable driver.

**A clean-room 6.18 AP/STA path remains out of reach.** The blocker is no longer ignorance of the
host message format (resolved here) but (a) the **closed firmware** whose D2H service was never
entered from a takeover (phase22-27/49 gate: `0xcece` announce + line-0x4C/SGI competition) and
(b) the vendor MAC binary. The shortest route is still **vendor GPL source** (the module declares
`license=GPL`), now precisely nameable via §7.2.

**Corrections / extensions to prior docs (this lane):**

1. **Boot D2H catalog is 5 messages, not 2.** The thermal lane read `.data+0x2660` as
   "id 1 device_plat_ready, id 2 host_ready". The records are 12-byte `{id, func, core}` and the
   table holds **id 0 `oam_rx_post_action_function`, id 1 plat-ready, id 2 host-ready, id 3
   `exception_info_msg_process`, id 4 `heartbeat_msg_process`** (verified via `.rel.data`
   relocations, [V]).
2. **The cfg80211 ops map is now complete** (39 callbacks named+addressed), replacing the earlier
   "117 cfg80211-related symbols" (DRIVER-BLACKBOX) and the "15/25 imports" counts with the exact
   struct at `.data+0xeb0` and `sizeof_priv=4`.
3. **The H2D doorbell sequence is decoded** (`pcie_msg_send_irq`): pending-mask `0x40039010`=8 →
   drain-poll doorbell `0x400392d4` → write pending → set bit0 of the twin word. Prior docs had
   the addresses ([V*]) but not the write order ([V]).
4. **The D2H receive loop is decoded** (`pcie_msg_handle`): read+clear the bitmap at
   `0x40039014`, dispatch per-bit ids 0..10 through an 8-byte `{func,arg}` table.

---

## 9. Evidence ledger (what a reviewer can re-derive)

| claim | artifact / command | grade |
| --- | --- | --- |
| ops map (39 callbacks, struct @0xeb0, wiphy_new_nm) | pyelftools reloc read of wifi.ko `.rel.data` (0xebc-0x104c) + `objdump -d` `wal_cfg80211_init` 0x10686c + kernel cfg80211.h field order | [V] |
| message header + dispatch + registration | `objdump -d` plat.ko `hcc_msg_process` 0x1204c, `hcc_msg_register_tab*` 0x117e4-0x11a94, `hcc_msg_tx` 0x11bf8; wifi.ko `hmac_send_msg` 0x46eb4, `hmac_chan_initiate_switch_to_new_channel` 0x46f1c | [V] |
| D2H/H2D doorbell sequence | `objdump -d` plat.ko `pcie_msg_send_irq` 0x174a8, `pcie_msg_handle` 0x171f8 | [V] |
| boot catalog (5 msgs) + firmware descriptor | pyelftools `.data` dump of plat.ko 0x2660-0x26ac + `.rel.data` | [V] |
| MAC event catalog | pyelftools `.rel.data` dump of wifi.ko 0x584-0x7bc | [V] |
| ring/node/mailbox CAs | phase42/43/49 ([V*], device-measured) | [V*] |
| firmware-load + calibration | thermal lane REPORT §2.4/§2.5 + phase8 | [V]/[V*] |

**Redaction:** no MAC, serial, die-id, PIN, secret or per-unit calibration value is reproduced; no
vendor `.ko`, firmware blob, INI or `.kv` is committed. Anything needing a vendor binary to
reproduce is marked [V]/[V*] with the local artifact named.

**What this lane did NOT do:** no device contact, no write to any vendor artifact, no module load,
no attempt to build a driver, no commit of any binary/firmware/calibration blob.

---

## 10. Bottom line

The Hi5622V100 **host** driver is now mapped at interface level: the cfg80211 surface, the
H2D/D2H message format and dispatch, the mailbox/ETE ring protocol, the boot handshake, and the
calibration container are documented with addresses and disassembly. That is the maximum a
host-only, read-only pass can deliver, and it materially raises the clean-room line's ceiling
(the protocol is no longer a black box). The two things that remain out of reach without vendor
GPL source — the closed firmware's D2H behaviour and the 3.6 MB of vendor MAC code — are exactly
what a GPL request (now nameable via §7.2) would resolve.
