# Phase-38 implementation notes: what the driver work that follows must build (2026-10-04)

Consolidation of the four phase-38 lane reports into one note for the port/driver work that
follows. Every item below is attributed to the lane that produced it; the lanes were independently
re-derived and all four passed in `VERIFICATION.md` (this directory).

Lanes, and what each contributes:

| lane report | contributes to this note |
| --- | --- |
| `announce-body-bytes.md` | §1 (the definitive first test frame) and part of §3 |
| `d2h-vocabulary-verified.md` | §2 (the verified D2H vocabulary tables) |
| `open-gaps-static-attempt.md` | §3 (resolved/remaining gaps) |
| `web-sdk-research.md` | §4 (web findings that change the plan) |
| `VERIFICATION.md` | provenance: all four lanes PASS, all quoted offsets/URLs re-derived |

Artifacts the lanes share (md5s re-confirmed in `VERIFICATION.md`):

| artifact | path | md5 |
| --- | --- | --- |
| plat module | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` |
| wifi module | `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` |
| firmware | `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` |

Conventions used throughout: `.ko` offsets are `.text`-relative (`file = offset + 0x38`); firmware
offsets are file offsets, runtime = file + `0x40000`.

---

## 1. The definitive first test frame to post

*(source: lane `announce-body-bytes.md`)*

The first frame the port posts is the vendor's boot-time SR announce body: **message B**, the 72-byte
buffer the vendor's first SR node points at. It is an HCC **group 0 / id 1** message, declared total
length `0x0030` (48), announced by an SR node whose `word1` declares `0x0048` (72) - the node/message
length mismatch is the vendor's own arrangement, reproduce it.

The 72 bytes, in address order:

```
offset  0  1  2  3   4  5  6  7   8  9  a  b   c  d  e  f
0x00   00 01 00 04  30 00 01 00  00 00 5a 5a  00 00 00 00
0x10   01 00 00 00  d8 00 14 00  01 00 00 00  00 00 00 00
0x20   00 00 00 00  00 00 00 00  00 00 00 00  ff ff ff ff
0x30   ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff ff
0x40   ff ff ff ff  ff ff ff ff
```

As words (`+00`..`+44`):

```
+00 = 0x04000100   +04 = 0x00010030   +08 = 0x5A5A0000   +0c = 0x00000000
+10 = 0x00000001   +14 = 0x001400D8   +18 = 0x00000001   +1c..+28 = 0
+2c..+44 = 0xFFFFFFFF x7
```

### 1.1 How it must be posted

* **One SR node, buffer = exactly the 72 bytes above, node declaring length 72.** The post is done by
  `shuangta_ete_sr_dscr_fill` @ `plat.ko 0x17858`; the node writes are `node[idx].word0 = buffer
  devva` (`0x178cc`), `node[idx].word1 = (len << 16) | flag` (`0x178e0`), followed by
  `mov r1,#3` (`0x178f4`) and `bl pcie_msg_send` (`0x178f8`) - i.e. the **H2D id-3 announce**.
  (`announce-body-bytes.md` §1)
* **The announce must be pending BEFORE the CPU release** - a host announcement is only read if it is
  pending at release (`START-HERE.md` point 3, re-used by the lane). Content is not the gate: the
  record shows the firmware consumes the id-3 announce and continues regardless of body
  (`announce-body-bytes.md` §0/§7).
* **The `0x5a5a` tag at `+0x0a` must be emitted by the port itself.** No instruction in either vendor
  module writes it: `wifi.ko` `.text`/`.data`/`.rodata` contain **zero** `5a 5a` byte pairs, and
  `plat.ko`'s only `#0x5a5a` immediates are the RF-calibration file magics at `0x2d58`/`0x2f10`, not
  on the SR path. The tag (and the 8-byte token field `+0x0c`..`+0x13`) are inherited in the recycled
  buffer, which is why the port must write them. (`announce-body-bytes.md` §3)
* **Node `word1` flag: the live vendor node reads `0x00486000` (flag `0x6000`)**, while
  `shuangta_ete_sr_dscr_fill` composes `(len<<16)|0x6000|0xd2b`. Use the observed `0x6000` form;
  the divergence is recorded, not resolved (`announce-body-bytes.md` §8).

### 1.2 The parsed header (what the firmware will read)

```
group/type   : 0            (+00 low nibble)
alloc state  : 1            (+01 low nibble, set by hcc_msg_alloc)
res-group    : 0            (+01 high nibble)
field A/B    : 0x00 / 0x04  (+02 / +03)
total length : 0x0030 = 48  (+04 u16)  -> header 12 + body 36
message id   : 0x0001 = 1   (+06 u16)
field C      : 0x00         (+08)
retry        : 0            (+09)
tag          : 0x5a5a       (+0a u16, not module-written -> the port writes it)
body         : +0c 00000000  +10 00000001  +14 001400d8  +18 00000001  +1c..+28 0  +2c ffffffff
```

Read `+0x14` the way the builder writes it (cmd u16, len u16): `cmd = 0x00d8`, `len = 0x0014` (20),
payload `{1,0,0,0,0}`. Both readings agree on the discriminator that matters: **group 0, id 1,
`0x5a5a` at `+0x0a`.** (`announce-body-bytes.md` §4)

### 1.3 The competing candidate (know it, do not post it first)

**Message A** is `(group 0, id 0x1d, len 72, all-zero body)` - the steady-state frame the live
vendor ring carries, built by `hmac_edca_opt_timeout_fn` @ `wifi.ko 0xef644`. A's builder runs only
once the hmac layer is up, i.e. *after* the boot dialogue, so A cannot be the step-1 post; it is not
the first frame. `announce-body-bytes.md` §6 has A's builder in full. The port action is the same
either way. (`announce-body-bytes.md` §0/§6)

---

## 2. The verified D2H vocabulary

*(source: lane `d2h-vocabulary-verified.md`; byte-verified against `hi5622v100_wifi.ko`)*

Six message tables in `.data` (file offset of `.data` = `0x243ab0`), **79 slots total: 77
byte-verified, 2 absent**; every `id -> handler` pair matches the phase-25 record, and table order is
*not* id order. Entry layout: 12-byte stride `{u32 id @+0, u32 handler @+4, u32 pad @+8}`, handler
word carrying an `R_ARM_ABS32` reloc with stored addend 0.

| group | side | table | `.data` off | entries | verified | absent |
| --- | --- | --- | --- | --- | --- | --- |
| Group 0 | core | hdpp `tab_core` | `0x40` | 7 | 7 | 0 |
| Group 1 | chip | hmac table (#1) | `0x584` | 28 | 28 | 0 |
| Group 2 | chip | hmac table (#2) | `0x6d4` | 12 | 10 | 2 |
| Group 2 | chip | wal table (#2) | `0x1c74` | 19 | 19 | 0 |
| Group 3 | chip | hdpp `tab_chip` | `0x4` | 5 | 5 | 0 |
| Group 3 | chip | hmac table (#3) | `0x764` | 8 | 8 | 0 |
| **total** | | | | **79** | **77** | **2** |

Registrar calls (re-derived, not assumed): `hdpp_main_init` passes `(3, .data+0x04, 5)` and
`(0, .data+0x40, 7)`; `hmac_main_init` passes `(1, .data+0x584, 28)`, `(2, .data+0x6d4, 12)`,
`(3, .data+0x764, 8)`; `wal_main_init` passes `(2, .data+0x1c74, 19)`.

### 2.1 Group 0 - core hdpp `tab_core` (7)

| idx | id | handler symbol |
| --- | --- | --- |
| 0 | 0 | `hmac_tx_complete_event_handle` |
| 1 | 1 | `hmac_tx_event_process` |
| 2 | 2 | `hmac_rx_process_data_event` |
| 3 | 4 | `hmac_tx_complete_notify_other_core_event_handle` |
| 4 | 3 | `hmac_rx_process_data_event` |
| 5 | 7 | `hmac_mac_exception_proc` |
| 6 | 8 | `hmac_ba_timeout_proc` |

### 2.2 Group 1 - chip hmac table #1 (28)

| idx | id | handler symbol | idx | id | handler symbol |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | `hmac_sdt_up_reg_val` | 14 | 20 | `hmac_mgmt_send_disasoc_deauth_event` |
| 1 | 1 | `hmac_create_ba_event` | 15 | 21 | `hmac_mgmt_send_disasoc_deauth_event` |
| 2 | 2 | `hmac_del_ba_event` | 16 | 22 | `hmac_chan_switch_to_new_chan_complete` |
| 3 | 3 | `hmac_event_config_syn` | 17 | 24 | `hmac_mgmt_tbtt_event` |
| 4 | 5 | `hmac_syn_info_event` | 18 | 25 | `hmac_dfs_radar_detect_event` |
| 5 | 9 | `hmac_bandwidth_info_syn_event` | 19 | 26 | `hmac_proc_disasoc_misc_event` |
| 6 | 10 | `hmac_protection_info_syn_event` | 20 | 29 | `hmac_acs_process_rescan_event` |
| 7 | 11 | `hmac_ch_status_info_syn_event` | 21 | 31 | `hmac_csi_complete` |
| 8 | 13 | `hmac_rx_process_mgmt_event` | 22 | 34 | `hmac_sr_bss_color_change_event` |
| 9 | 14 | `hmac_mgmt_rx_delba_event` | 23 | 35 | `hmac_sr_syn_parameter_event` |
| 10 | 15 | `hmac_scan_proc_scanned_bss` | 24 | 36 | `hmac_chr_report_msg` |
| 11 | 16 | `hmac_scan_proc_scan_comp_event` | 25 | 38 | `hmac_config_set_security_port_event` |
| 12 | 17 | `hmac_scan_process_chan_result_event` | 26 | 39 | `hmac_update_fbt_scan_result` |
| 13 | 18 | `hmac_event_acs_response` | 27 | 37 | `hmac_pfm_detect_process_event` |

(Note the on-disk tail order: ... 36, 38, 39, 37.)

### 2.3 Group 2 - chip hmac table #2 (12)

| idx | id | handler symbol | status |
| --- | --- | --- | --- |
| 0 | 22 | *none* | **absent (null)** |
| 1 | 23 | *none* | **absent (null)** |
| 2 | 24 | `hmac_spectral_scan_complete` | local |
| 3 | 25 | `hmac_phy_event_complete` | local |
| 4 | 26 | `hmac_phy_event_complete` | local |
| 5 | 27 | `hmac_mgmt_deauth_msg_process` | local |
| 6 | 28 | `hmac_ba_action_msg_process` | local |
| 7 | 29 | `hcc_timer_process` | **import** (`SHN_UNDEF`) |
| 8 | 33 | `hmac_mgmt_read_error_msg_process` | local |
| 9 | 34 | `hmac_mgmt_send_deauth_frame_process` | local |
| 10 | 35 | `hmac_del_user_msg_process` | local |
| 11 | 36 | `hmac_rx_data_send_disasoc_frame_process` | local |

Ids **22 and 23 are reserved with no handler** (handler word `0x00000000`, no relocation) - the
phase-25 `*unnamed*` pair, now an explicit null. Id **29 is the only import** in the whole
vocabulary (`hcc_timer_process`, bound against the HCC/platform module at load).

### 2.4 Group 2 - chip wal table #2 (19)

| idx | id | handler symbol | idx | id | handler symbol |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | `wal_config_process_pkt` | 10 | 12 | `wal_cfg80211_init_evt_handle` |
| 1 | 1 | `wal_acs_netlink_recv_handle` | 11 | 13 | `wal_cfg80211_mgmt_tx_status` |
| 2 | 2 | `wal_scan_comp_proc_sta` | 12 | 20 | `wal_cfg80211_cac_report` |
| 3 | 3 | `wal_asoc_comp_proc_sta` | 13 | 21 | `wal_receive_all_sta_rssi_proc` |
| 4 | 4 | `wal_disasoc_comp_proc_sta` | 14 | 30 | `wal_report_external_auth_req` |
| 5 | 5 | `wal_connect_new_sta_proc_ap` | 15 | 31 | `wal_process_packet_xmit` |
| 6 | 6 | `wal_disconnect_sta_proc_ap` | 16 | 32 | `wal_dfr_power_down_dev` |
| 7 | 7 | `wal_mic_failure_proc` | 17 | 37 | `wal_multiap_report_proc` |
| 8 | 8 | `wal_acs_response_event_handler` | 18 | 38 | `wal_hiwifi_report_proc` |
| 9 | 9 | `wal_send_mgmt_to_host` | | | |

### 2.5 Group 3 - hdpp `tab_chip` (5) and hmac table #3 (8)

| table | idx | id | handler symbol |
| --- | --- | --- | --- |
| hdpp `tab_chip` | 0 | 0 | `hmac_voice_aggr_event` |
| hdpp `tab_chip` | 1 | 4 | `hmac_device_wow_data_report` |
| hdpp `tab_chip` | 2 | 5 | `hmac_rx_schedule_req` |
| hdpp `tab_chip` | 3 | 7 | `hdpp_stat_save_tx_ppdu_record_process` |
| hdpp `tab_chip` | 4 | 8 | `hdpp_stat_save_rx_ppdu_record_process` |
| hmac #3 | 0 | 9 | `hmac_smps_update_device_capbility` |
| hmac #3 | 1 | 10 | `hmac_pfm_hiex_rx_local_msg` |
| hmac #3 | 2 | 15 | `hmac_bsd_update_msg` |
| hmac #3 | 3 | 16 | `hmac_multiap_report_11v_event` |
| hmac #3 | 4 | 17 | `hmac_d2h_pm_event` |
| hmac #3 | 5 | 18 | `hmac_d2h_temp_state_update` |
| hmac #3 | 6 | 19 | `hmac_receive_beacon_probrsp_to_reprot` |
| hmac #3 | 7 | 20 | `hmac_report_beacon_frame` |

### 2.6 How the vocabulary is used

The per-event discriminator is the **12-byte HCC header inside the ring payload** - `byte[0] & 0xf` =
group, `u16 @ +6` = id - dispatched by the host at `hcc_msg_process` @ `0x1204c`. The D2H **mail bit
is a channel signal, not a per-event id** (see §3.1). So: post the descriptor, raise the ring bit,
and set the HCC header per event. (`open-gaps-static-attempt.md` §2.2)

---

## 3. Resolved / remaining open gaps

*(primary source: lane `open-gaps-static-attempt.md`; §3.4 also draws on `announce-body-bytes.md`)*

### 3.1 Resolved

* **Gap 9 (which D2H notify id each HCC event carries) is answered as posed: it has no per-event
  answer.** The firmware has exactly one notify primitive, `d2h_notify` @ file `0x86170`, reachable
  statically through ops slot +0x20 (table rt `0x10F250`, word at file `0xCF270` = `0x000C6171`) and
  the `bx` wrapper at file `0x81D70` - the phase-37 map missed both because the table word carries the
  Thumb bit and the wrapper tail-calls with `bx`. It is stamped by *channel/lifecycle* events, not
  message type. (`open-gaps-static-attempt.md` §1.2, §2.2)
* **The static notify-id set is exhaustive: {2, 5, 6, 8}**, from five sites - id 2 (inline ETE
  bring-up writer `0x86F5A`), id 6 (`0x86224`, a *generic* ring-descriptor post), id 5 (`0x86998`
  direct and `0xE7E` via wrapper, the D2H channel bring-up/pump), id 8 (`0x4F4EE`, which also writes
  CA `0x40030100`). The host registers handlers for `{1, 3, 6, 7}` (`plat.ko pcie_msg_init` @
  `0xb6e4`); **6 is the only device id with a host handler** - bits 2, 5, 8 take the host's `0x1739c`
  "no handler" path. (`open-gaps-static-attempt.md` §2.1)
* **The D2H bits batch** (shadow `g+0xb8` / armed `g+0xb4`, flushed by the D2H kick IRQ handler at
  file `0x86108`, device IRQ `0x4E`), and the host copes (`pcie_msg_handle` loops, `bics r5,r5,r3,lsl
  r6` @ `plat.ko 0x172f8`). A single `out[1]` read can legally carry several bits.
  (`open-gaps-static-attempt.md` §1.1)
* **No other `out[1]`/doorbell writer exists.** Address-provenance scan: literal `0x40039014` occurs
  only at file `0x97dc` (ctx init) and `0x86fbc` (step-4 inline writer); `0x40101434` only at
  `0x97d8`. Writers are exactly: ctx init `0x978c`, the inline step-4 pair `0x86F5A`/`0x86F66`,
  `d2h_notify` (`0x861b0`/`0x861c0`), the IRQ-`0x4E` flush (`0x86156`/`0x86166`).
  (`open-gaps-static-attempt.md` §1.1)
* **Gap 10: H2D id 5 is a dead path in a working boot; the firmware's trap is a deliberate stub.**
  `plat.ko` posts id 5 at exactly one site, `0x15144`, the *failure* tail of
  `pcie_ete_rcv_buff_check` (`0x14d74`) - a tagged, non-empty receive buffer takes the success return
  at `0x14e00` and never reaches the tail. The device's id-5 handler is file `0x819DC`, three
  identical `BUG()` stubs (`00 23 1b 60 ff de` x3). Since the two images ship as a working pair and
  posting id 5 would fault the device, id 5 is not posted; the `BUG()` corroborates the dead path.
  (`open-gaps-static-attempt.md` §3)
* **Gap 12: on every statically visible path, the id-6 handler's 20-byte reply is NOT announced on
  `out[1]`.** The handler (file `0x4CCE4`) hands the reply to `msg_send` (file `0xc2760`), which
  routes it through the HCC queue and wakes a queue worker - no `out[1]` or doorbell write anywhere
  in that tree. (`open-gaps-static-attempt.md` §4.2)
* **Three phase-37 map corrections** the reader must not re-import: (i) `d2h_notify` *does* have
  static callers (two direct + ops slot + wrapper); (ii) step 4's D2H doorbell is **`0x40101434`**,
  not `0x400a1434` (which occurs nowhere in the image); (iii) the map's `0x86f5e str r2,[r3]` writes
  `1`, not `4` (the `4` goes to `out[1]` one instruction earlier at `0x86f5a`).
  (`open-gaps-static-attempt.md` §5)

### 3.2 Remaining open - and the one live read that closes each

| open item | status | the read that closes it |
| --- | --- | --- |
| Per-payload `(group,id) <-> mail bit` correlation | open - four of five notify sites (1-4) are statically unreachable (their function addresses occur nowhere as a branch/literal/`adr` target); only site 5 (id 8, `0x4F120`) is reachable | capture `out[1]` (CA `0x40039014`) at every D2H IRQ while dumping the DR ring payload's HCC header; or the one-shot marker: post H2D id 3 and read `out[1]` before/after (`open-gaps-static-attempt.md` §2.3) |
| H2D id 5 truly never posted in a given boot | concluded dead; premise is runtime state | sample CA `0x40039010` (`out[0]`) for bit 5 across a full boot; bit absent + `pcie_ete_rcv_buff_check` returning 0 confirms (`open-gaps-static-attempt.md` §3.3) |
| Whether a runtime callback announces the id-6 reply on `out[1]` | verified negative for all static paths; the bit-6 ring-post function `0x861E0` is statically unreferenced | drive H2D id 6 and capture `out[1]` + the DR ring; the reply is identifiable by header `byte[0]&0xf==2`, `byte[6]==0x2a` (`open-gaps-static-attempt.md` §4.2) |

### 3.3 Open items carried by the announce lane

* **Body `cmd`/tag provenance is open.** `0xd8` at `+0x14` and the tag/body words at
  `+0x0a`/`+0x0c`/`+0x10` have no producing instruction in the analysed modules: the only shape that
  fits is `hdpp_config_send_event` (`wifi.ko 0x59c0`, the *unique* alloc+`strh`@`+0x14`/`+0x16`
  builder), yet its three call sites (`0x16f4`, `0x2924`, `0x1af64`) all pass other cmds. Concrete
  hypothesis: the record's live-boot baseline used a **different `wifi.ko` build** (`e21629d2…`,
  3,387,392 B) than the teardown artifact analysed (`4737fcb2…`, 3,564,728 B). Closing it needs that
  shipped image (diff) or a device-side trace. (`announce-body-bytes.md` §5/§8)
* **A vs B** - B is named the boot body, A remains a candidate but cannot be the step-1 post; the
  port action is identical (§1.3). (`announce-body-bytes.md` §0/§5)
* **Node `word1` flag** divergence (`0x6000` observed vs `0x6000|0xd2b` composed) is recorded, not
  resolved. (`announce-body-bytes.md` §8)
* **Payload semantics beyond `+0x18`** are a `memcpy_s` of a producer struct; field meanings live in
  that struct. (`announce-body-bytes.md` §8)

---

## 4. Web findings that change the plan

*(source: lane `web-sdk-research.md`; all 6 cited URLs re-fetched and byte counts matched in
`VERIFICATION.md`)*

**Bottom line: nothing public documents this device (`hi5622v100`/luofu), the mailbox block
`0x40039000-0x40039600`, or these message ids.** What exists is the same vendor driver architecture,
published under GPLv2, for sibling chips. What that changes for the plan:

1. **Do not expect (or wait for) a datasheet or the vendor source to arrive - the port stays
   self-contained.** An exhaustive 24-query sweep plus GitHub API searches found no register map, no
   firmware source, and no `hi5622`/`hi5621`/"luofu" repo; the exact register addresses return only TI
   TM4C123 headers. The remaining instruments stay as `THE-GATE-MAP.md` lists them: a device-side
   trace, or the Jeton vendor-source request (set aside by the human's 2026-10-04 decision).
   (`web-sdk-research.md` "Does not give" 1, §F5/F1/F2)
2. **The GPLv2 Hi3881V100 stack is the reference for vocabulary and structure - not for bytes.**
   OpenHarmony's `device_soc_hisilicon` supplies an HCC frame-header definition, a
   `hcc_message_register(handler, msg, cb, data)` API, the send idiom `oal_bus_send_msg(bus,
   H2D_MSG_*)` ("post a small message, payload rides the ring") - matching this device's
   `pcie_msg_*` design - and the full 32-slot H2D/D2H message-id enum (`oal_sdio_comm.h`).
   **Caveat that changes the plan: never map ids 1:1** - it is an SDIO transport with *different*
   assignments (there H2D 3 = `TEST`, D2H 2 = `FLOWCTRL_OFF`, D2H 6 = `DISALLOW_SLEEP`; here H2D 3 =
   SR announce, D2H 2 = platform ready, D2H 6 = wake-host rx). Use it to bound the family, not to
   name this device's bits. (`web-sdk-research.md` §A1/A2/A3)
3. **Header layouts are family-related but NOT byte-compatible.** The public `hcc_header` is a
   4-byte bitfield inside a **64-byte TCM** header with a 12-bit `pay_len` at a different offset and
   **no `0x5a5a` tag** - this device's 12-byte header must stay as reverse-engineered here.
   (`web-sdk-research.md` §A1)
4. **The public PCIe sibling uses a different mechanism, so the mailbox+doorbell model still has to
   be built from our own measurements.** Hi1105 (`hongmengkernel`, `pcie_firmware_msg.c`) uses a
   descriptor-based `frw_ringbuf_t` H2D/D2H ring + interrupt, whereas this device's boot path uses a
   **mailbox pending bitmap + doorbell/ack** (`0x40039010`/`0x400392d4`/`0x400392f0`). Useful only
   for transport/ring vocabulary; it contains no doorbell and no `0x4003…` registers.
   (`web-sdk-research.md` §B1/B2/B3)
5. **Structural cross-check available: the `ete` ring naming.** Hi1105's `platform/oal/pcie/` tree
   carries an `ete/` subdirectory and `sdio/`+`pcie/` split - the same `shuangta_ete_*`/`pcie_ete_*`
   vocabulary as this device, confirming the architecture family and giving a place to look for
   naming clues. (`web-sdk-research.md` §B4)
6. **Reuse licensing:** §A files are **GPLv2** (permission-clean to reuse); §B files are a vendor
   kernel tree whose license must be checked before any code reuse. (`web-sdk-research.md` "Gives" 4)

---

## 5. Verification of this note

`VERIFICATION.md` in this directory re-derived all four lane reports independently against the same
three md5-pinned artifacts and returned **PASS** for every lane:

| lane | verdict | basis |
| --- | --- | --- |
| `announce-body-bytes.md` | PASS | 59/59 byte triples + mnemonics; 0 wifi `5a5a`; unique alloc+`strh` builder; 3 call sites |
| `d2h-vocabulary-verified.md` | PASS | 6 tables / 79 slots / 77 relocs / 2 nulls / 1 import; all ids + sampled symbols reproduce |
| `open-gaps-static-attempt.md` | PASS | 43/43 firmware + 19/19 plat.ko offsets; doorbell/pool/stub/provenance claims reproduce |
| `web-sdk-research.md` | PASS | 6/6 URLs return content; byte counts exact; content-level tokens present |

No claim carried into this note failed re-derivation.
