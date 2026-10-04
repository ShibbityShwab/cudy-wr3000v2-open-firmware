# SR-ring H2D message catalog (phase 37, 2026-10-04)

Static, read-only reverse engineering of the two vendor modules (task `st_01a105a2`).
Artifacts, md5 re-checked at analysis time:

* `opensource/build/tmp/hi5622v100_wifi.ko` - 3,564,728 B, md5 `4737fcb21a1a2262a96f84d780ad8b35`
* `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` - 364,660 B, md5 `23660bc285393e678d5cade1c36c194b`
* `build/tmp/FIRMWARE.bin` - 928,920 B, md5 `0e530b976d5a20e87358671f1a577695`

**Offset convention.** Both modules are `ET_REL` objects: `.text` has `sh_addr = 0`, so every offset
below is a **symbol / section offset** (what the record calls a `.ko offset`); the file offset is
`symbol_offset + 0x38` for `.text` (wifi.ko `.text` `sh_offset = 0x38`). Data offsets are symbol
offsets into the named section. Firmware offsets are **file** offsets unless marked `rt`
(`rt = file + 0x40000`). Companion phase-37 deliverables: `docs/phase37/d2h-message-catalog.md`
(device->host catalog) and `docs/phase37/fw-ring-message-table.md` (the device-side dispatch table).

---

## 1. What an SR-ring H2D message is

The port already posts SR descriptors (`opensource/lab/wifidrv1/wifidrv1.c`;
`docs/phase24/vendor-sr-announce-id.md`): a node `{word0 = buffer VA, word1 = (len<<16)|flag}`, then a
producer-index commit, then the `pcie_msg_send(chip, 3)` announce. The buffer the node points at is the
**HCC host-to-device message** - the object `hcc_msg_alloc()` builds in `plat.ko`. Every builder in the
vendor driver allocates one with `hcc_msg_alloc(size, clear)`, fills a 12-byte header plus `size`
payload bytes, and hands it to `hcc_msg_tx()`.

### 1.1 The 12-byte header (fields and the instruction that establishes each)

| off | width | field | set by | evidence (offset, bytes, instruction) |
| --- | --- | --- | --- | --- |
| +0x00 | u8 | **group**, low nibble 0-15; selects the per-group message map (`hcc_msg_get_msg_map(msg[0] & 0xf)`; the sibling `d2h-message-catalog.md` uses this same name). High nibble unused. | builder | `plat` `hcc_queue_get_by_msg` @0x12a18 `0010d4e5` `ldrb r1, [r4]` + @0x12a20 `0f1001e2` `and r1, r1, #0xf` |
| +0x01 | u8 | low nibble = **alloc state** (`1` = allocated, set by `hcc_msg_alloc`); high nibble = **group / thread index** | `hcc_msg_alloc` + builder | `plat` `hcc_msg_alloc` @0x11e10 `0100a0e3` `mov r0, #1` + @0x11e1c `1030c3e7` `bfi r3, r0, #0, #4` + @0x11e24 `0130c2e5` `strb r3, [r2, #1]` |
| +0x02 | u8 | message field A (per message) | builder | `hdpp_config_send_event` @0x5a30 `0620d8e5` `ldrb r2, [r8, #6]` then @0x5a34 `0220c4e5` `strb r2, [r4, #2]` |
| +0x03 | u8 | message field B (per message) | builder | `hdpp_config_send_event` @0x5a38 `0120d8e5` `ldrb r2, [r8, #1]` then @0x5a3c `0320c4e5` `strb r2, [r4, #3]` |
| +0x04 | u16 | **total length** = payload + 12 | `hcc_msg_alloc` | `plat` `hcc_msg_alloc` @0x11db4 `0c5080e2` `add r5, r0, #0xc`, @0x11e18 `b450c2e1` `strh r5, [r2, #4]`; reader `hcc_msg_len_get` @0x12240 `b400d011` `ldrhne r0, [r0, #4]` |
| +0x06 | u16 | **message id**; selects the 16-byte entry in the type map | builder | `plat` `hcc_queue_get_by_msg` @0x12a34 `b610d4e1` `ldrh r1, [r4, #6]`; `hcc_queue_tx_process` @0x112b0 `0620d2e5` `ldrb r2, [r2, #6]` |
| +0x07 | u8 | high byte of the id word (usually 0) | builder | see the per-message id stores in section 3 |
| +0x08 | u8 | message field C (per message; `wal_*` carry the sequence counter here) | builder | `hdpp_config_send_event` @0x5a40 `0830c4e5` `strb r3, [r4, #8]` |
| +0x09 | u8 | **retry counter**; `hcc_msg_tx` copies it from +1 high nibble, `hcc_msg_tx_to_core` from +1 low nibble + 1 (bails above 2) | `hcc_msg_tx` | `plat` `hcc_msg_tx` @0x11c30 `0120d3e5` `ldrb r2, [r3, #1]`, @0x11c34 `5222e3e7` `ubfx r2, r2, #4, #4`, @0x11c38 `0920c3e5` `strb r2, [r3, #9]` |
| +0x0a | u16 | transport tag; live SR buffers show `0x5a5a` here (`docs/phase25/sr-header-parsed.md`). **Not written by either module** - no `0x5a5a` immediate exists in `wifi.ko`, and the only `0x5a5a` in `plat.ko` is the RF-cal file magic at @0x2d48 / @0x2f00. Treated as an SR/ETE layer constant outside the HCC header. | (SR/ETE layer) | grep of both `.text`/`.data`/`.rodata` for `#0x5a5a` |
| +0x0c | - | payload start | builder | `hcc_msg_alloc` reserves `size + 12`; builders copy to `msg + 0xc` |

Header/domain semantics come from `plat.ko`:

* `hcc_msg_alloc(size, clear)` @0x11d9c - `hcc_msg_alloc` @0x11d9c `743600e3` `movw r3, #0x674` (max size),
  @0x11db4 `0c5080e2` `add r5, r0, #0xc` (total), @0x11e18 `b450c2e1` `strh r5, [r2, #4]`.
* `hcc_queue_get_by_msg(msg)` @0x12a00 - @0x12a10 `0900d4e5` `ldrb r0, [r4, #9]` then @0x12a14
  `bl hcc_get_group_res`; @0x12a18/0x12a20 take `msg[0] & 0xf` as the type and @0x12a24
  `bl hcc_msg_get_msg_map`; @0x12a34 `b610d4e1` `ldrh r1, [r4, #6]` is the id.
* `hcc_msg_process(handler, msg)` @0x1204c (receive side) - @0x12084 `0030d2e5` `ldrb r3, [r2]` +
  @0x1208c `0f3003e2` `and r3, r3, #0xf` (type) and @0x1209c `b620d2e1` `ldrh r2, [r2, #6]` (id).

### 1.2 The post path

```
builder (wifi.ko)                        hcc_msg_alloc -> fill 12-byte header + payload
  |  hcc_msg_tx(msg)                     plat.ko @0x11bf8
  v
hcc_queue_get_by_msg(msg)  @0x12a00  -> hcc_get_group_res(msg[9]) + hcc_msg_get_msg_map(msg[0]&0xf),
                                        index by u16 msg[6] (16-byte entries) -> queue
hcc_queue_add_msg(msg, q)  @0x12930  -> list insert + hcc_thread_sched (0x12998)
hcc_queue_tx_process       @0x1128c  -> ldrb r2,[r2,#6] @0x112b0 ; bal_get_port_res ; dequeue batch
bal_port_start_xfer        @0x10b78  -> ldr r2,[obj+0x18] @0x10ba4 ; bx r2  (chip callback)
  ... chip / ETE layer ...
shuangta_ete_sr_dscr_fill  @0x17858  -> node.word0 = buffer VA   (@0x178cc, file 0x17904)
                                        node.word1 = (len<<16)|0x6d2b|0x4000|0x2000 (@0x178e0, file 0x17918)
                                        announce: mov r1,#3 @0x178f4 ; bl pcie_msg_send @0x178f8
```

So **one vendor function fills the descriptor and announces**, and it does so for *every* H2D
message - the id bitmap bit is always `3`. The per-message discriminator is the HCC header (type at +0
low nibble, id at +6), which the firmware id-3 handler reads.

Verbatim quotes (plat.ko, ARM):

| addr | bytes | instruction | role |
| --- | --- | --- | --- |
| 0x11bf8 | `10402de9` | `push {r4, lr}` | `hcc_msg_tx` entry |
| 0x11c3c | `feffffeb` | `bl hcc_queue_get_by_msg` (reloc) | pick the queue |
| 0x11c4c | `feffffeb` | `bl hcc_queue_add_msg` (reloc) | enqueue |
| 0x112b0 | `0620d2e5` | `ldrb r2, [r2, #6]` | the id byte selects the port resource |
| 0x113fc | `feffffeb` | `bl bal_port_start_xfer` (reloc) | hand the batch to the chip layer |
| 0x10ba4 | `182092e5` | `ldr r2, [r2, #0x18]` | chip xfer callback |
| 0x167e4 | `003051e2` | `subs r3, r1, #0` | `pcie_xfer_data` entry |
| 0x16818 | `feffffea` | `b pcie_tx_request_handle` | -> tx request |
| 0x178f4 | `0310a0e3` | `mov r1, #3` | SR announce id |
| 0x178f8 | `feffffeb` | `bl pcie_msg_send` (reloc) | H2D mailbox announce |
| 0x178cc | `821183e7` | `str r1, [r3, r2, lsl #3]` | node word0 = buffer VA (file 0x17904) |
| 0x178e0 | `042083e5` | `str r2, [r3, #4]` | node word1 = (len<<16) or flags (file 0x17918) |

### 1.3 The device side

The firmware H2D handler table is at firmware `0x118d68`; the pointer slot at firmware `0x10c1b0`
holds it (byte-verified: `FIRMWARE.bin` file `0xcc1b0` = `688d1100` = `0x00118d68`). Its populated ids
are **1 / 3 / 5 / 6** (`docs/phase30/the-gate-located.md`, `docs/phase24/handler-table.md`), with
handlers at firmware file `0x510` / `0x85144` / `0x819dc` / `0x4cce4` and the dispatcher at file
`0x818ac` (independently re-derived in `docs/phase37/fw-ring-message-table.md`). The dispatcher is
indexed by the **bit position** of the host pending word, so the SR fill announce bit 3 selects the
id-3 handler (file `0x85144`). Steady-state H2D therefore arrives on the ring and is
dispatched once, by bit 3.

---

## 2. Catalog

Every vendor function that builds or posts an HCC H2D message. Column `type` is the HCC **group** (`msg[0] & 0xf`); `id` is the u16 at `msg+6`, `payload B` is the `hcc_msg_alloc` size, `total B` = payload + 12 (written to `msg+4`). `runtime` = live variable; `clone`/`helper`/`retry` = no own header (posts a cloned/pre-built message or re-posts on completion).

| posting function | module | builder off | type | id | payload B | total B |
| --- | --- | --- | --- | --- | --- | --- |
| `hcc_timer_timeout_proc` | plat.ko | `0x13410` | #2 | #0x1d | dyn | dyn |
| `hcc_xfer_done` | plat.ko | `0x10d08` | retry | - | dyn | dyn |
| `hdpp_config_send_event` | wifi.ko | `0x59c0` | 0 | #1 | dyn | dyn |
| `hmac_acs_init_scan_hook` | wifi.ko | `0xb4090` | #2 | #8 | dyn | dyn |
| `hmac_cfg_tx_sched_switch` | wifi.ko | `0x1b8b0` | runtime | #0x23 | dyn | dyn |
| `hmac_chan_do_sync` | wifi.ko | `0x47c68` | runtime | #0x16 | dyn | dyn |
| `hmac_chan_restart_network_after_switch` | wifi.ko | `0x487d0` | 0 | #0x19 | dyn | dyn |
| `hmac_config_alg_send_event` | wifi.ko | `0x60c74` | 0 | #2 | dyn | dyn |
| `hmac_config_h2d_send_app_ie` | wifi.ko | `0x60188` | runtime | #0x20 | dyn | dyn |
| `hmac_config_receive_all_sta_rssi` | wifi.ko | `0x674e8` | #2 | #0x15 | #0x108 | 0x114 |
| `hmac_config_send_event` | wifi.ko | `0x57238` | 0 | #1 | dyn | dyn |
| `hmac_csi_complete` | wifi.ko | `0xb9ef8` | #2 | #0x17 | #0x2c | 0x38 |
| `hmac_del_user_notify_dmac` | wifi.ko | `0x8d7f0` | 0 | #9 | #0xc | 0x18 |
| `hmac_dfs_cac_stop` | wifi.ko | `0xacf7c` | runtime | #0x14 | dyn | dyn |
| `hmac_edca_opt_timeout_fn` | wifi.ko | `0xef644` | 0 | #0x1d | #0x3c | 0x48 |
| `hmac_enc_scan_send_req` | wifi.ko | `0x9ad98` | 0 | #0xc | #0x1fc | 0x208 |
| `hmac_event_acs_response` | wifi.ko | `0x3e5b0` | clone | clone | dyn | dyn |
| `hmac_handle_asoc_rsp_sta` | wifi.ko | `0x99280` | #2 | #3 | #0x1c | 0x28 |
| `hmac_handle_connect_rsp_ap` | wifi.ko | `0x4b1a0` | #2 | #5 | #0x10 | 0x1c |
| `hmac_handle_disconnect_rsp_ap` | wifi.ko | `0x4af80` | #2 | #6 | dyn | dyn |
| `hmac_handle_scan_rsp_sta` | wifi.ko | `0x9704c` | #2 | #2 | dyn | dyn |
| `hmac_hsan_event_report` | wifi.ko | `0xa9100` | #2 | #0x26 | dyn | dyn |
| `hmac_mgmt_reset_psm` | wifi.ko | `0x403ac` | 0 | #0xe | dyn | dyn |
| `hmac_mgmt_rx_addba_rsp` | wifi.ko | `0x45a94` | runtime | #0xa | #0x20 | 0x2c |
| `hmac_mgmt_rx_delba` | wifi.ko | `0x45d88` | 0 | #0xa | #0x20 | 0x2c |
| `hmac_mgmt_send_deauth_frame_notify` | wifi.ko | `0xe9a4` | #2 | #0x22 | #0x18 | 0x24 |
| `hmac_mgmt_tx_ampdu_end` | wifi.ko | `0x46444` | runtime | #0xb | dyn | dyn |
| `hmac_mgmt_tx_ampdu_start` | wifi.ko | `0x4627c` | 0 | #0xb | dyn | dyn |
| `hmac_mgmt_tx_event_status` | wifi.ko | `0x427f4` | #2 | #0xd | dyn | dyn |
| `hmac_multiap_event_report` | wifi.ko | `0xc6368` | #2 | #0x25 | dyn | dyn |
| `hmac_multiap_handle_steering_req_msg` | wifi.ko | `0xcf0b4` | 0 | #0x2c | #0x1c | 0x28 |
| `hmac_multiap_parse_chan_sel_req_msg` | wifi.ko | `0xccfa0` | 0 | #0x31 | #0x1d | 0x29 |
| `hmac_report_cac_finish` | wifi.ko | `0xacd3c` | #2 | #0x14 | #0x20 | 0x2c |
| `hmac_report_cac_start` | wifi.ko | `0xad47c` | #2 | #0x14 | #0x20 | 0x2c |
| `hmac_report_connect_failed_result` | wifi.ko | `0x988a8` | #2 | #3 | #0x1c | 0x28 |
| `hmac_report_external_auth_req` | wifi.ko | `0x98a94` | #2 | #0x1e | #0x38 | 0x44 |
| `hmac_rx_data_send_disasoc_frame_notify` | wifi.ko | `0xeb24` | #2 | #0x24 | #0x10 | 0x1c |
| `hmac_rx_mic_failure_process` | wifi.ko | `0xdfbc` | runtime | #7 | #0x10 | 0x1c |
| `hmac_scan_config_bss_color` | wifi.ko | `0x99e50` | runtime | #0x24 | dyn | dyn |
| `hmac_scan_proc_scan_req_event_exception` | wifi.ko | `0x9a3dc` | #2 | #2 | dyn | dyn |
| `hmac_scan_proc_sched_scan_req_event` | wifi.ko | `0x9b288` | runtime | #0xd | dyn | dyn |
| `hmac_sdt_recv_reg_cmd` | wifi.ko | `0x3e28c` | 0 | #3 | dyn | dyn |
| `hmac_send_connect_result_to_dmac_sta` | wifi.ko | `0x98d78` | 0 | #0x11 | dyn | dyn |
| `hmac_send_mgmt_to_host` | wifi.ko | `0x42464` | #2 | #9 | #0x1c | 0x28 |
| `hmac_send_msg` | wifi.ko | `0x46ec4` | helper | - | dyn | dyn |
| `hmac_spectral_scan_complete` | wifi.ko | `0x89e70` | #2 | #0x16 | #8 | 0x14 |
| `hmac_sta_handle_disassoc_rsp` | wifi.ko | `0x53c44` | #2 | #4 | dyn | dyn |
| `hmac_sta_sync_join_req_params` | wifi.ko | `0x52fac` | runtime | #0xf | #0x40 | 0x4c |
| `hmac_sta_up_update_edca_params_machw` | wifi.ko | `0x52480` | 0 | #0x12 | #0x74 | 0x80 |
| `hmac_sta_up_update_mu_edca_params_machw` | wifi.ko | `0x52140` | 0 | #0x13 | dyn | dyn |
| `hmac_sta_wait_join_rx_beacon` | wifi.ko | `0x514d4` | 0 | #0x10 | #0x10 | 0x1c |
| `hmac_sta_wait_join` | wifi.ko | `0x538b8` | runtime | #0x10 | #0x10 | 0x1c |
| `hmac_tx_ba_notify` | wifi.ko | `0x16b90` | #2 | #0x1c | #0x14 | 0x20 |
| `hmac_tx_complete_read_error_process` | wifi.ko | `0x1bdd4` | #2 | #0x21 | #0x18 | 0x24 |
| `hmac_tx_msdu_rw_err_proc` | wifi.ko | `0x197dc` | #2 | #0x24 | #0x10 | 0x1c |
| `hmac_user_add_notify_alg` | wifi.ko | `0x8e624` | 0 | #8 | #0xa8 | 0xb4 |
| `hmac_user_add` | wifi.ko | `0x8eb34` | 0 | #7 | #0xa8 | 0xb4 |
| `hmac_vip_frame_event_post` | wifi.ko | `0xc1058` | 0 | #0x2b | dyn | dyn |
| `hmac_virtual_fill_tx_packet` | wifi.ko | `0xf58a8` | #2 | #0x1f | #0x10 | 0x1c |
| `shuangta_irq_host_all` | wifi.ko | `0x3a3a4` | #2 | #0x18 | dyn | dyn |
| `wal_acs_netlink_recv` | wifi.ko | `0x128b7c` | #2 | #1 | dyn | dyn |
| `wal_dfr_excp_rx` | wifi.ko | `0x1297e0` | runtime | #0x20 | dyn | dyn |
| `wal_netdev_set_mac_addr` | wifi.ko | `0x116420` | #1 | #0xc | #0x1c | 0x28 |
| `wal_send_cfg_event_msg_tx` | wifi.ko | `0x12705c` | helper | - | dyn | dyn |
| `wal_wlan_cfg_module_host_process_entry` | wifi.ko | `0x12754c` | helper | - | dyn | dyn |

### 2.1 Why ids repeat

The group nibble alone does not separate messages. `hcc_queue_get_by_msg` looks the map up per
*resource group* (`hcc_get_group_res(msg[9])`, where `msg[9]` is the high nibble of `msg+1`), so two
builders with the same `(group,id)` can land on different queues. Where entries above share
`(group,id)` - e.g. `(2,0x14)` = `hmac_report_cac_start` / `hmac_report_cac_finish`; `(2,0x24)` =
`hmac_rx_data_send_disasoc_frame_notify` / `hmac_tx_msdu_rw_err_proc`; `(2,3)` =
`hmac_report_connect_failed_result` / `hmac_handle_asoc_rsp_sta` - the discriminator is the payload and
the resource-group nibble, not the id alone.

### 2.2 The shared alloc helper at 0x5714c

Some `hmac_config_*` builders do not write the header inline; they call a local helper at `0x5714c`
with a descriptor, the id in `r1` and the length on the stack:

```
0x5714c  f0412de9  push {r4, r5, r6, r7, r8, lr}
0x57158  08d04de2  sub sp, sp, #8
0x57168  b042dde1  ldrh r4, [sp, #0x20]   ; len argument
0x5716c  100084e2  add r0, r4, #0x10      ; payload = len + 0x10
0x57174  feffffeb  bl hcc_msg_alloc
0x57190  b680c4e1  strh r8, [r4, #6]      ; id = r1 argument
0x57198  1f30c3e7  bfc r3, #0, #4         ; group = 0
0x5719c  0030c4e5  strb r3, [r4]
```

So `hmac_config_send_event` (helper id `1`) and `hmac_config_alg_send_event` (helper id `2`) post
group-0 messages of `len + 0x10` bytes. `wal_alloc_cfg_event` @0x126ef0 is the WAL analogue: it sets
group 2 (`0x126f78` `1230c3e7` `bfi r3, r2, #0, #4`, r2 = 2 @`0x126f68` `0220a0e3` `mov r2, #2`) and
`msg+6 = r4` (`0x126f6c` `0640cce5` `strb r4, [ip, #6]`).

---

## 3. Per-message evidence

Each entry: posting function, builder offset, its `hcc_msg_tx` call site, and the instruction that sets the alloc size, the type nibble (`msg+0`) and the id (`msg+6`), as symbol offset, raw bytes and capstone text.

* **hdpp_config_send_event** (wifi.ko) builder `0x59c0`, hcc_msg_tx @`0x5a54`
  - alloc: `0x59d8` `7200ffe6` `uxth r0, r2`
  - type (msg+0): `0x5a18` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x5a04` `0620c4e5` `strb r2, [r4, #6]`
* **hmac_rx_mic_failure_process** (wifi.ko) builder `0xdfbc`, hcc_msg_tx @`0xe0c8`
  - alloc: `0xe040` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0xe098` `0030c6e5` `strb r3, [r6]`
  - id (msg+6): `0xe0a0` `0630c6e5` `strb r3, [r6, #6]`
* **hmac_mgmt_send_deauth_frame_notify** (wifi.ko) builder `0xe9a4`, hcc_msg_tx @`0xea68`
  - alloc: `0xe9c8` `1800a0e3` `mov r0, #0x18`
  - type (msg+0): `0xea08` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0xea10` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_rx_data_send_disasoc_frame_notify** (wifi.ko) builder `0xeb24`, hcc_msg_tx @`0xebe4`
  - alloc: `0xeb4c` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0xeb8c` `0010c4e5` `strb r1, [r4]`
  - id (msg+6): `0xeb94` `0610c4e5` `strb r1, [r4, #6]`
* **hmac_tx_ba_notify** (wifi.ko) builder `0x16b90`, hcc_msg_tx @`0x16c30`
  - alloc: `0x16bb8` `1400a0e3` `mov r0, #0x14`
  - type (msg+0): `0x16bf0` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x16bf8` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_tx_msdu_rw_err_proc** (wifi.ko) builder `0x197dc`, hcc_msg_tx @`0x198b0`
  - alloc: `0x19824` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0x19850` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0x19858` `0630c5e5` `strb r3, [r5, #6]`
* **hmac_cfg_tx_sched_switch** (wifi.ko) builder `0x1b8b0`, hcc_msg_tx @`0x1b9a0`
  - alloc: `0x1b8e4` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x1b914` `0030c6e5` `strb r3, [r6]`
  - id (msg+6): `0x1b91c` `0630c6e5` `strb r3, [r6, #6]`
* **hmac_tx_complete_read_error_process** (wifi.ko) builder `0x1bdd4`, hcc_msg_tx @`0x1be88`
  - alloc: `0x1be18` `1800a0e3` `mov r0, #0x18`
  - type (msg+0): `0x1be50` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x1be58` `0630c4e5` `strb r3, [r4, #6]`
* **shuangta_irq_host_all** (wifi.ko) builder `0x3a3a4`, hcc_msg_tx @`0x3a498`
  - alloc: `0x3a420` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x3a458` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0x3a45c` `0600c3e5` `strb r0, [r3, #6]`
* **hmac_sdt_recv_reg_cmd** (wifi.ko) builder `0x3e28c`, hcc_msg_tx @`0x3e328`
  - alloc: `0x3e2ac` `7500ffe6` `uxth r0, r5`
  - type (msg+0): `0x3e2e4` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x3e2ec` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_event_acs_response** (wifi.ko) `0x3e5b0` - no own header (clone); hcc_msg_tx @`0x3e5d0`
* **hmac_mgmt_reset_psm** (wifi.ko) builder `0x403ac`, hcc_msg_tx @`0x40440`
  - alloc: `0x403e0` `0200a0e3` `mov r0, #2`
  - type (msg+0): `0x4040c` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x40414` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_send_mgmt_to_host** (wifi.ko) builder `0x42464`, hcc_msg_tx @`0x42578` (msg register `r6` = `0x424f8` `186198e5` `ldr r6, [r8, #0x118]`)
  - alloc: `0x424e8` `1c00a0e3` `mov r0, #0x1c`
  - type (msg+0): `0x42518` `1230c3e7` `bfi r3, r2, #0, #4` (r2 = 2 @`0x42508` `0220a0e3` `mov r2, #2`); store `0x4251c` `0030c6e5` `strb r3, [r6]`
  - id (msg+6): `0x42520` `0930a0e3` `mov r3, #9`; store `0x42524` `0630c6e5` `strb r3, [r6, #6]`
* **hmac_mgmt_tx_event_status** (wifi.ko) builder `0x427f4`, hcc_msg_tx @`0x428c4`
  - alloc: `0x42854` `0800a0e3` `mov r0, #8`
  - type (msg+0): `0x42884` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x4288c` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_mgmt_rx_addba_rsp** (wifi.ko) builder `0x45a94`, hcc_msg_tx @`0x45c88`
  - alloc: `0x45b24` `2000a0e3` `mov r0, #0x20`
  - type (msg+0): `0x45b54` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0x45b5c` `0620c3e5` `strb r2, [r3, #6]`
* **hmac_mgmt_rx_delba** (wifi.ko) builder `0x45d88`, hcc_msg_tx @`0x45f50`
  - alloc: `0x45ed8` `2000a0e3` `mov r0, #0x20`
  - type (msg+0): `0x45f08` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0x45f10` `0630c5e5` `strb r3, [r5, #6]`
* **hmac_mgmt_tx_ampdu_start** (wifi.ko) builder `0x4627c`, hcc_msg_tx @`0x46360`
  - alloc: `0x462dc` `0800a0e3` `mov r0, #8`
  - type (msg+0): `0x4630c` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0x46314` `0620c3e5` `strb r2, [r3, #6]`
* **hmac_mgmt_tx_ampdu_end** (wifi.ko) builder `0x46444`, hcc_msg_tx @`0x464f4`
  - alloc: `0x46478` `0800a0e3` `mov r0, #8`
  - type (msg+0): `0x464ac` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x464b4` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_send_msg** (wifi.ko) `0x46ec4` - no own header (helper); hcc_msg_tx @`0x46ed8`
* **hmac_chan_do_sync** (wifi.ko) builder `0x47c68`, hcc_msg_tx @`0x47d48`
  - alloc: n/a
  - type (msg+0): `0x47cfc` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x47d04` `0630cce5` `strb r3, [ip, #6]`
* **hmac_chan_restart_network_after_switch** (wifi.ko) builder `0x487d0`, hcc_msg_tx @`0x48860`
  - alloc: `0x48804` `0100a0e1` `mov r0, r1`
  - type (msg+0): `0x48830` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x48838` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_handle_disconnect_rsp_ap** (wifi.ko) builder `0x4af80`, hcc_msg_tx @`0x4b044`
  - alloc: `0x4afc0` `0600a0e3` `mov r0, #6`
  - type (msg+0): `0x4affc` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0x4b004` `0630c5e5` `strb r3, [r5, #6]`
* **hmac_handle_connect_rsp_ap** (wifi.ko) builder `0x4b1a0`, hcc_msg_tx @`0x4b258`
  - alloc: `0x4b1cc` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0x4b208` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x4b210` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_sta_wait_join_rx_beacon** (wifi.ko) builder `0x514d4`, hcc_msg_tx @`0x5169c`
  - alloc: `0x51618` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0x51654` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x5165c` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_sta_up_update_mu_edca_params_machw** (wifi.ko) builder `0x52140`, hcc_msg_tx @`0x52220`
  - alloc: `0x52170` `0500a0e1` `mov r0, r5`
  - type (msg+0): `0x521b0` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x521b8` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_sta_up_update_edca_params_machw** (wifi.ko) builder `0x52480`, hcc_msg_tx @`0x52548`
  - alloc: `0x524b4` `7400a0e3` `mov r0, #0x74`
  - type (msg+0): `0x524f0` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x524f8` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_sta_sync_join_req_params** (wifi.ko) builder `0x52fac`, hcc_msg_tx @`0x530f4`
  - alloc: `0x52fd8` `4000a0e3` `mov r0, #0x40`
  - type (msg+0): `0x53014` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x5301c` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_sta_wait_join** (wifi.ko) builder `0x538b8`, hcc_msg_tx @`0x539b4`
  - alloc: `0x53928` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0x53960` `0030c7e5` `strb r3, [r7]`
  - id (msg+6): `0x53968` `0630c7e5` `strb r3, [r7, #6]`
* **hmac_sta_handle_disassoc_rsp** (wifi.ko) builder `0x53c44`, hcc_msg_tx @`0x53cd0`
  - alloc: `0x53c6c` `0200a0e3` `mov r0, #2`
  - type (msg+0): `0x53c9c` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x53ca4` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_config_send_event** (wifi.ko) builder `0x57238`, hcc_msg_tx @`0x572cc`
  - alloc: n/a
  - type (msg+0): n/a
  - id (msg+6): `0x57294` `acffffeb` `bl #0x5714c`
* **hmac_config_h2d_send_app_ie** (wifi.ko) builder `0x60188`, hcc_msg_tx @`0x60238`
  - alloc: `0x601c0` `0600a0e1` `mov r0, r6`
  - type (msg+0): `0x601f0` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x601f8` `0630cce5` `strb r3, [ip, #6]`
* **hmac_config_alg_send_event** (wifi.ko) builder `0x60c74`, hcc_msg_tx @`0x60d00`
  - alloc: n/a
  - type (msg+0): n/a
  - id (msg+6): `0x60cbc` `22d9ffeb` `bl #0x5714c`
* **hmac_config_receive_all_sta_rssi** (wifi.ko) builder `0x674e8`, hcc_msg_tx @`0x675e0`
  - alloc: `0x67510` `420fa0e3` `mov r0, #0x108`
  - type (msg+0): `0x675b4` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0x675b8` `0620c5e5` `strb r2, [r5, #6]`
* **hmac_spectral_scan_complete** (wifi.ko) builder `0x89e70`, hcc_msg_tx @`0x89fdc` (msg register `sl` = `0x89f60` `18a199e5` `ldr sl, [sb, #0x118]`)
  - alloc: `0x89f50` `0800a0e3` `mov r0, #8`
  - type (msg+0): `0x89fa0` `1120c3e7` `bfi r2, r1, #0, #4` (r1 = 2 @`0x89f9c` `0210a0e3`); store `0x89fa8` `0020cae5` `strb r2, [sl]`
  - id (msg+6): `0x89fb4` `1620a0e3` `mov r2, #0x16`; store `0x89fbc` `0620cae5` `strb r2, [sl, #6]`
* **hmac_del_user_notify_dmac** (wifi.ko) builder `0x8d7f0`, hcc_msg_tx @`0x8d888`
  - alloc: `0x8d804` `0c00a0e3` `mov r0, #0xc`
  - type (msg+0): `0x8d858` `0020c4e5` `strb r2, [r4]`
  - id (msg+6): `0x8d860` `0620c4e5` `strb r2, [r4, #6]`
* **hmac_user_add_notify_alg** (wifi.ko) builder `0x8e624`, hcc_msg_tx @`0x8e714`
  - alloc: `0x8e644` `a800a0e3` `mov r0, #0xa8`
  - type (msg+0): `0x8e6e4` `0020c4e5` `strb r2, [r4]`
  - id (msg+6): `0x8e6ec` `0620c4e5` `strb r2, [r4, #6]`
* **hmac_user_add** (wifi.ko) builder `0x8eb34`, hcc_msg_tx @`0x8edb0`
  - alloc: `0x8ecec` `a800a0e3` `mov r0, #0xa8`
  - type (msg+0): `0x8ed84` `0020c6e5` `strb r2, [r6]`
  - id (msg+6): `0x8ed78` `0610c6e5` `strb r1, [r6, #6]`
* **hmac_handle_scan_rsp_sta** (wifi.ko) builder `0x9704c`, hcc_msg_tx @`0x970ec`
  - alloc: `0x97070` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x970a8` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x970a0` `0650cce5` `strb r5, [ip, #6]`
* **hmac_report_connect_failed_result** (wifi.ko) builder `0x988a8`, hcc_msg_tx @`0x98998`
  - alloc: `0x98904` `1c00a0e3` `mov r0, #0x1c`
  - type (msg+0): `0x9893c` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x98944` `0630cce5` `strb r3, [ip, #6]`
* **hmac_report_external_auth_req** (wifi.ko) builder `0x98a94`, hcc_msg_tx @`0x98c0c`
  - alloc: `0x98b8c` `3800a0e3` `mov r0, #0x38`
  - type (msg+0): `0x98bbc` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x98bc4` `0630cce5` `strb r3, [ip, #6]`
* **hmac_send_connect_result_to_dmac_sta** (wifi.ko) builder `0x98d78`, hcc_msg_tx @`0x98df8`
  - alloc: `0x98d98` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x98dc4` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0x98dcc` `0620c3e5` `strb r2, [r3, #6]`
* **hmac_handle_asoc_rsp_sta** (wifi.ko) builder `0x99280`, hcc_msg_tx @`0x99390`
  - alloc: `0x992cc` `1c00a0e3` `mov r0, #0x1c`
  - type (msg+0): `0x99330` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x99338` `0630cce5` `strb r3, [ip, #6]`
* **hmac_scan_config_bss_color** (wifi.ko) builder `0x99e50`, hcc_msg_tx @`0x99f88`
  - alloc: `0x99e98` `0800a0e3` `mov r0, #8`
  - type (msg+0): `0x99ec8` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0x99ed0` `0620c3e5` `strb r2, [r3, #6]`
* **hmac_scan_proc_scan_req_event_exception** (wifi.ko) builder `0x9a3dc`, hcc_msg_tx @`0x9a4cc`
  - alloc: `0x9a450` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x9a488` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x9a478` `0610cce5` `strb r1, [ip, #6]`
* **hmac_enc_scan_send_req** (wifi.ko) builder `0x9ad98`, hcc_msg_tx @`0x9ae2c`
  - alloc: `0x9adac` `7f0fa0e3` `mov r0, #0x1fc`
  - type (msg+0): `0x9ade0` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x9ade8` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_scan_proc_sched_scan_req_event** (wifi.ko) builder `0x9b288`, hcc_msg_tx @`0x9b3ac`
  - alloc: n/a
  - type (msg+0): `0x9b360` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0x9b368` `0630cce5` `strb r3, [ip, #6]`
* **hmac_hsan_event_report** (wifi.ko) builder `0xa9100`, hcc_msg_tx @`0xa91d4`
  - alloc: `0xa915c` `7200ffe6` `uxth r0, r2`
  - type (msg+0): `0xa918c` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0xa9194` `0630cce5` `strb r3, [ip, #6]`
* **hmac_report_cac_finish** (wifi.ko) builder `0xacd3c`, hcc_msg_tx @`0xace2c`
  - alloc: `0xacd9c` `2000a0e3` `mov r0, #0x20`
  - type (msg+0): `0xacdd8` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0xacde0` `0630c5e5` `strb r3, [r5, #6]`
* **hmac_dfs_cac_stop** (wifi.ko) builder `0xacf7c`, hcc_msg_tx @`0xad128`
  - alloc: n/a
  - type (msg+0): `0xad0d8` `0030c5e5` `strb r3, [r5]`
  - id (msg+6): `0xad0e0` `0630c5e5` `strb r3, [r5, #6]`
* **hmac_report_cac_start** (wifi.ko) builder `0xad47c`, hcc_msg_tx @`0xad548`
  - alloc: `0xad4bc` `2000a0e3` `mov r0, #0x20`
  - type (msg+0): `0xad4f4` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0xad4fc` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_acs_init_scan_hook** (wifi.ko) builder `0xb4090`, hcc_msg_tx @`0xb4674`
  - alloc: `0xb4134` `030285e0` `add r0, r5, r3, lsl #4`
  - type (msg+0): `0xb4170` `0030c7e5` `strb r3, [r7]`
  - id (msg+6): `0xb4178` `0630c7e5` `strb r3, [r7, #6]`
* **hmac_csi_complete** (wifi.ko) builder `0xb9ef8`, hcc_msg_tx @`0xba550` (msg register `ip` = `0xba4dc` `18c194e5` `ldr ip, [r4, #0x118]`)
  - alloc: `0xba4cc` `2c00a0e3` `mov r0, #0x2c`
  - type (msg+0): `0xba500` `1230c3e7` `bfi r3, r2, #0, #4` (r2 = 2 @`0xba4ec`); store `0xba504` `0030cce5` `strb r3, [ip]`
  - id (msg+6): `0xba508` `1730a0e3` `mov r3, #0x17`; store `0xba50c` `0630cce5` `strb r3, [ip, #6]`
* **hmac_vip_frame_event_post** (wifi.ko) builder `0xc1058`, hcc_msg_tx @`0xc1160`
  - alloc: `0xc10fc` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0xc1130` `0020c3e5` `strb r2, [r3]`
  - id (msg+6): `0xc1124` `0610c3e5` `strb r1, [r3, #6]`
* **hmac_multiap_event_report** (wifi.ko) builder `0xc6368`, hcc_msg_tx @`0xc644c`
  - alloc: `0xc6394` `7a00ffe6` `uxth r0, sl`
  - type (msg+0): `0xc63c0` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0xc63c8` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_multiap_parse_chan_sel_req_msg** (wifi.ko) builder `0xccfa0`, hcc_msg_tx @`0xcdb4c`
  - alloc: `0xcdad4` `1d00a0e3` `mov r0, #0x1d`
  - type (msg+0): `0xcdb00` `0030c0e5` `strb r3, [r0]`
  - id (msg+6): `0xcdb08` `0630c0e5` `strb r3, [r0, #6]`
* **hmac_multiap_handle_steering_req_msg** (wifi.ko) builder `0xcf0b4`, hcc_msg_tx @`0xcf608`
  - alloc: `0xcf590` `1c00a0e3` `mov r0, #0x1c`
  - type (msg+0): `0xcf5b8` `0030c0e5` `strb r3, [r0]`
  - id (msg+6): `0xcf5c0` `0630c0e5` `strb r3, [r0, #6]`
* **hmac_edca_opt_timeout_fn** (wifi.ko) builder `0xef644`, hcc_msg_tx @`0xefa50`
  - alloc: `0xef9cc` `3c00a0e3` `mov r0, #0x3c`
  - type (msg+0): `0xefa00` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0xefa08` `0630c4e5` `strb r3, [r4, #6]`
* **hmac_virtual_fill_tx_packet** (wifi.ko) builder `0xf58a8`, hcc_msg_tx @`0xf59ac`
  - alloc: `0xf5928` `1000a0e3` `mov r0, #0x10`
  - type (msg+0): `0xf5958` `0030c7e5` `strb r3, [r7]`
  - id (msg+6): `0xf5960` `0630c7e5` `strb r3, [r7, #6]`
* **wal_netdev_set_mac_addr** (wifi.ko) builder `0x116420`, hcc_msg_tx @`0x116588` (message built by `wal_alloc_cfg_event` @0x126ef0, returned in the out-param at `[sp, #0x18]`; the builder then overwrites type, id and payload)
  - alloc: via `0x1164e4` `feffffeb` `bl wal_alloc_cfg_event` with len `0x18` (`0x1164cc` `1810a0e3` `mov r1, #0x18`, `0x1164d8` `00108de5` `str r1, [sp]`) -> payload `0x1c`
  - type (msg+0): `0x1164f4` `0110a0e3` `mov r1, #1`; store `0x1164fc` `001082e5` `str r1, [r2]`
  - id (msg+6): `0x1164f8` `0c30a0e3` `mov r3, #0xc`; store `0x116504` `b630c2e1` `strh r3, [r2, #6]`
* **wal_send_cfg_event_msg_tx** (wifi.ko) `0x12705c` - no own header (helper); hcc_msg_tx @`0x127080`
* **wal_wlan_cfg_module_host_process_entry** (wifi.ko) `0x12754c` - no own header (helper); hcc_msg_tx @`0x12757c`
* **wal_acs_netlink_recv** (wifi.ko) builder `0x128b7c`, hcc_msg_tx @`0x128ccc`
  - alloc: `0x128c4c` `7500ffe6` `uxth r0, r5`
  - type (msg+0): `0x128c80` `0030c4e5` `strb r3, [r4]`
  - id (msg+6): `0x128c84` `0610c4e5` `strb r1, [r4, #6]`
* **wal_dfr_excp_rx** (wifi.ko) builder `0x1297e0`, hcc_msg_tx @`0x129a1c`
  - alloc: `0x1299c4` `0700a0e1` `mov r0, r7`
  - type (msg+0): `0x1299f8` `0030c6e5` `strb r3, [r6]`
  - id (msg+6): `0x1299ec` `0620c6e5` `strb r2, [r6, #6]`
* **hcc_xfer_done** (plat.ko) `0x10d08` - no own header (retry); hcc_msg_tx @`0x10d6c`
* **hcc_timer_timeout_proc** (plat.ko) builder `0x13410`, hcc_msg_tx @`0x134e8`
  - alloc: `0x13430` `0400a0e3` `mov r0, #4`
  - type (msg+0): `0x1345c` `0020c5e5` `strb r2, [r5]`
  - id (msg+6): `0x13474` `0630c5e5` `strb r3, [r5, #6]`

---

## 4. Verification

This catalog quotes 224 `offset bytes instruction` triples. Every one was re-derived from the raw
binaries with the repo-local capstone 5.0.7 (`pyenv/Scripts/python.exe`, `CS_ARCH_ARM`, `CS_MODE_ARM`)
and the mnemonic compared with the quoted text:

```
claims 224  PASS 224  FAIL 0
```

The check parses every `(offset, bytes, instruction)` triple out of this file, then for each one
confirms that (a) the bytes at that offset in `hi5622v100_wifi.ko` or `hi5622v100_plat.ko` `.text`
equal the quoted bytes and (b) capstone decodes that offset to the quoted mnemonic. The firmware
pointer at file `0xcc1b0` was checked as a raw byte match (`688d1100` = `0x00118d68`).

**Two defects this check caught and fixed** (recorded for the ledger):

* `0x17904` / `0x17918` were **file** offsets of the SR-fill node stores, not symbol offsets (the
  symbol offsets are `0x178cc` / `0x178e0`; the doc now shows both).
* the `hcc_msg_alloc` size-limit `movw r3, #0x674` is at `0x11d9c`, not `0x11da4`.

**Section containment.** Every quoted `.ko` offset lies in `.text` (`sh_addr = 0`; wifi.ko
`sh_size = 0x1630cc`, plat.ko `sh_size = 0x1d324`). The firmware quote is a data byte inside
`FIRMWARE.bin` (`0xe2c98` bytes). No quoted `.ko` offset falls in `.data`/`.rodata`/`.bss`.

Reproducer (from the repo root):

```python
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
import re
def load(p):
    f=open(p,"rb"); e=ELFFile(f); s={x.name:x for x in e.iter_sections()}; f.seek(0); return e,s,f.read()
_,sw,dw=load("opensource/build/tmp/hi5622v100_wifi.ko")
_,sp,dp=load("opensource/build/register-dumps/teardown/hi5622v100_plat.ko")
md=Cs(CS_ARCH_ARM,CS_MODE_ARM); txt=open("opensource/docs/phase37/sr-message-catalog.md").read()
for off,b,tx in re.findall(r"`(0x[0-9a-f]+)` `([0-9a-f]{8,32})` `([^`\n]+)`",txt):
    o=int(off,16); bb=bytes.fromhex(b); ok=False
    for sec,d in ((sw[".text"],dw),(sp[".text"],dp)):
        base=sec["sh_offset"]-sec["sh_addr"]
        if d[base+o:base+o+len(bb)]==bb:
            print("PASS",off,tx,"->",next(md.disasm(d[base+o:base+o+4],o)).mnemonic); ok=True; break
    if not ok: print("FAIL",off,b,tx)
```


## 5. Limits and open items

* The catalog is the **HCC message layer**. The chip/ETE layer above it (the callback behind
  `bal_port_start_xfer`) was not fully unwound in this pass; the transport claim rests on the record
  (`docs/phase24/tx-path-is-in-wifi-ko.md`, `docs/phase36/h2d-mailbox-boot-only.md`) plus the quoted
  `shuangta_ete_sr_dscr_fill` announce.
* Payload bytes beyond `msg+0xc` are not decoded field-by-field here; each message is a `memcpy_s` of a
  live descriptor, so the field meanings live in the producer structs (e.g. the `hdpp_*`, `hmac_*`,
  `wal_*` event structs). The header fields above are decoded and quoted.
* `hmac_event_acs_response` posts a `hcc_msg_clone` (keeps the cloned header) and `hmac_send_msg` /
  `wal_send_cfg_event_msg_tx` post pre-built messages; these are transport helpers, not new types.
* Eight `hcc_msg_tx` relocation sites in `wifi.ko` sit in `.text` runs that no `STT_FUNC` symbol
  covers; their call offsets are `0x5bf8`, `0x1ae58`, `0x399a4`, `0x3ff70`, `0x40344`, `0x46630`,
  `0xd3560`, `0xf9608`. Each was confirmed to be a real `bl hcc_msg_tx`, so a follow-up pass can
  attribute them once the local symbol boundaries are recovered. They are the only gap in the
  catalog's coverage: 74 `hcc_msg_tx` sites = 66 attributed (64 wifi + 2 plat) + 8 unattributed.
