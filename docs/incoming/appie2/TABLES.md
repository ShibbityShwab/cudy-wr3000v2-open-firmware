# TABLES - WEXT set_vendor_ie call order and the host cfg-event dispatcher

Lane appie2 (task-244). Grades: **[V]** verified in the local vendor driver disassembly
(addresses/layouts re-derived in evidence/DECODE-NOTES.txt); **[I]** inference; **[U]**
unproven. Wire layouts are protocol shapes quoted as numbers; no vendor bytes.

All function addresses are section-relative .text addresses of the local driver module
(all ELF sections start at 0, so resolve the section before reading bytes).

## W1 - the WEXT private-ioctl entry [V]

Dispatch: wal_witp_wifi_priv_cmd_proc_set -> cmd 0x8eec -> wal_ioctl_set_vendor_ie
(**0x11820c**), the kernel side of nlwifi_set_app_ie.

Userspace request (read by the handler; all little-endian):

| off | size | field | notes |
| ---: | ---: | --- | --- |
| 0 | u32 | cmd | must be 0x8eec |
| 4 | u32 | ie_len | <= 496 enforced by the copy into the 496-byte kernel field |
| 8 | u8 | type | slot class |
| 9 | u8[3] | pad | ignored |
| 12 | u8[ie_len] | ie | IE blob |

The whole input is copied into a zeroed 592-byte kernel object, so bytes past ie_len are
zero in the body.

## W2 - the 508-byte cfg-event body [V]

wal_ioctl_set_vendor_ie writes the body at a stack object and calls
wal_send_cfg_event(dev, 1, 0x1fc, body, sync=1, &resp).

| off | size | field | value |
| ---: | ---: | --- | --- |
| 0 | u16 | event | **0x0276 SET_VENDOR_IE** |
| 2 | u16 | payload len | **0x01f8** (504 = 8 + 496) |
| 4 | u32 | ie_len | from userspace |
| 8 | u8 | type | from userspace |
| 9 | u8 | mode | **0** on the WEXT path |
| 10 | u8[2] | pad | 0 |
| 12 | u8[496] | ie | zero padded to 496 |

Total 508 bytes. After the call: if a response was returned,
wal_check_and_release_msg_resp (0x109370) releases it and the WEXT status becomes the
handler status.

## W3 - the 536-byte group-2 / id-0 envelope [V]

wal_send_cfg_event (0x126c98) builds it through wal_alloc_cfg_event (0x126ab0) and sends
it via hcc_msg_tx (host-to-firmware transport).

| off | size | field | value |
| ---: | ---: | --- | --- |
| 0 | u8 | byte0 | low nibble **2** = group 2; high nibble preserved |
| 1 | u8 | byte1 | high nibble = ctx[2] & 0xf; low nibble 1 (dest nibble) |
| 2 | u8 | byte2 | ctx[1] |
| 3 | u8 | byte3 | ctx[0] |
| 6 | u16 | id | **0** |
| 10 | u16 | tag | **0x5a5a** |
| 12 | u32 | private | host-private request/wait word |
| 16 | u32 | rpc tag | 1 |
| 20 | u16 | reserved | 0 |
| 22 | u16 | msg len | 0x01fc (508) |
| 24 | u32 | sequence | g_wal_config_seq_num monotonic |
| 28 | u8[508] | body | W2 |

Total **536 B** fixed. Context trio source: *(u32 *)(dev + 0x328) - the netdev hmac/WAL
context; ctx[0]/ctx[1]/ctx[2] map as in the table above.

## W4 - the hmac consumer chain and its order [V]

wal_config_set_vendor_ie (0x12b960, thunk) -> hmac_config_set_vendor_ie (0x60a6c) ->
hmac_config_set_app_ie_to_vap (0x60298):

1. mac_find_ie(0x7f, len, ie) (0x1daac); if found, memmove_s() the tail over it and
   reduce ie_len (first 0x7f IE only).
2. type == 7 special path (adaptive 11r ability bookkeeping).
3. **mac_vap_save_app_ie (0x2ac18) - host-mirror save.**
4. on success only: **hmac_config_h2d_send_app_ie (0x600cc) - the direct g0 id 0x20
   app-IE H2D frame.**

Order is mirror -> post; a mirror failure returns before any H2D frame.

## W5 - the host-side consumer: the envelope is host-internal [V]

wal_main_init (0x12386c) stores four handlers into g_st_wal_drv_func_hook (bss 0x35808)
and registers the block with the platform OAM layer:

| slot | handler |
| ---: | --- |
| +0 | wal_recv_memory_cmd (0xfb9ac) |
| +4 | wal_recv_reg_cmd (0xfbc44) |
| +8 | **wal_recv_config_cmd (0xfd604)** |
| +12 | wal_recv_global_var_cmd (0xfbbec) |

wal_recv_config_cmd (config-cmd message shape):

| off | size | field |
| ---: | ---: | --- |
| 0 | u8[16] | netdev name (resolved with dev_get_by_name(init_net, ...)) |
| 16 | ... | cfg-event bytes (an event record: u16 event, u16 len, payload) |

It allocates len-12 bytes with hcc_msg_alloc, fills an envelope with group 2 in the low
nibble, id 0, the dev+0x328 context trio, tag 0x5a5a, copies the event record to payload
offset 16, and calls **wal_config_process_pkt (0xfcb94) locally**. The envelope is
**never transmitted** on this path.

## W6 - the 12-byte dispatch row [V]

wal_config_process_pkt parses payload records and searches event-id tables; on a hit it
calls the handler. wal_config_process_entry (0xfc714) selects:

| row off | size | field |
| ---: | ---: | --- |
| 0 | u16 | event id |
| 2 | u16 | pad |
| 4 | u32 | handler for dispatch mode **4** (0 = unsupported) |
| 8 | u32 | handler for dispatch mode **3** (0 = unsupported) |

Row stride 12 bytes. Null handler -> the vendor logs "not supported" and does not call.
Relocation cross-check: the .rodata words relocated to .text land at row+8, fixing the
layout above (a null +4 slot and a populated +8 slot is the app-IE row's shape).

## W7 - the two host tables and the app-IE row [V]

| table | location | rows | note |
| --- | --- | ---: | --- |
| production host row table | .rodata 0x85f8 | **290** (count from movw #290) | searched by wal_config_process_pkt |
| g_ast_board_wid_op_debug | .rodata 0x24980 | **170** (wal_config_get_debug_wid_arrysize returns 170) | debug copy |
| g_ast_board_wid_op_new_debug | .rodata 0x25178 | 1 (wal_config_get_debug_wid_new_arrysize returns 1) | debug override |

Row for the app-IE event (identical in the production table at index 123 and in the debug
copy): **id 0x0276, +4 slot null, +8 slot = wal_config_set_vendor_ie**. Other rows are
present in the same tables (e.g. 0x02de -> wal_config_get_sta_11h_abillty as its +8
handler), which the host model uses as a decoy to prove the lookup is by id.

## W8 - the response / status path [V decode, [I] field semantics]

* WEXT caller: wal_check_and_release_msg_resp (0x109370) on the response object; on a
  non-zero status the handler logs and returns it.
* Config-cmd response: wal_recv_cfg_cmd_rsp (0xfd474) builds an OAM-SDT report whose
  first byte is 0x4d and whose tail is the cfg payload, through the OAM-SDT hooks
  (get_oam_sdt_func_hook, oam_alloc_data2sdt, oam_report_data2sdt).
* The firmware link of the WEXT request and the userspace/OAM link of the config command
  are the two ends of the same vendor transaction; the exact handoff between them is
  open (REPORT section 7, item 1).

## W9 - the fire-relevant delta summary

| # | vendor shape | caught by the class sweep? |
| ---: | --- | --- |
| D1 | chain runs on a **running** AP (runtime config update), not inside start_ap() | no |
| D2 | context trio from **netdev hmac ctx** (dev+0x328), not the VAP-start module | no |
| D3 | host-mirror save **before** the H2D post | no |
| D4 | first 0x7f IE stripped, ie_len reduced | no |
| D5 | type 0..19, mode 0, payload up to 496 B | partly (class byte only) |
| D6 | the g2/id0 envelope is **host-internal**; the only firmware-bound frame is the inner g0 id 0x20 | not applicable |

D1/D2 are the ranked candidates for the bench; D6 removes the envelope fire rung from
the first pass's ladder (see FIRE-RECIPE.md).
