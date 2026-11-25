# wifi-rx-crx-gating - evidence tables (T1-T6)

All addresses are derived constants from the pinned local artifacts: the vendor
firmware `FIRMWARE.bin` (928,920 B, sha256 7fc87e20...) with **CPU = file offset +
0x40000**, and the vendor host module `hi5622v100_wifi.ko` 2.5.24 (sha256 de78ec07...)
with `.text`/`.data` section-relative offsets. No vendor byte is reproduced in this
repository; the firmware and module stay at their local paths.

---

## T1. Firmware CRX builder instruction map (CPU 0xBA0A0-0xBA17A) [V]

| CPU | instruction (derived annotation) | field built |
| ---: | --- | --- |
| 0xBA0A0 | `movs r1,#6`; `ldrb r0,[r6,#0xd6d]` | channel passed to the frame-pool call |
| 0xBA0AA | `mov.w sb,#1` | group value 1 (used below) |
| 0xBA0B0 | `ldr r8,=0x10380cf8`; `ldr sl,=0x20380cf8` | firmware log handles |
| 0xBA0B6 | loop head over the received-frame list | |
| 0xBA0C0 | `bl 0x1024ea` | dequeue the next frame message |
| 0xBA0C6 | `bl 0x409ea` | fetch/validate the message |
| 0xBA0CE | `bl 0x40cba` | fetch the RX descriptor pointer |
| 0xBA0F8 | `ldrh.w fp,[r0,#0x11]` | frame_len = RX-info +0x11 (u16) |
| 0xBA0FC | `ldrb r0,[r0,#6]` | forward flag = RX-info +6 |
| 0xBA0FE | `add.w r2,fp,#0x30`; `uxth r2,r2` | payload size = frame_len + 48 |
| 0xBA108 | `bl 0xbc524` | reserve the message payload buffer |
| 0xBA110 | `add.w r1,r2,#0xc`; `cmp.w r1,#0x680`; `bls` | total = len+60; cap 0x680 |
| 0xBA134 | `cbnz r0,#0xba144` | flag 0 -> drop + log 0x49f |
| 0xBA144 | `ldrb r3,[r4]` | read the existing group byte |
| 0xBA146 | `add.w fp,fp,#0x3c` | total = frame_len + 60 |
| 0xBA14A | `bfi r3,sb,#0,#4` | **hdr[0] group nibble = 1** |
| 0xBA14E | `strb r3,[r4]` | commit hdr[0] |
| 0xBA150 | `movs r3,#0xd` | **id = 13** |
| 0xBA152 | `strb r3,[r4,#6]` | commit hdr[6] |
| 0xBA156 | `strh.w fp,[r4,#4]` | **hdr[4] total = frame_len+60** |
| 0xBA15A | `strb r3,[r4,#7]` (r3=0) | id high byte 0 |
| 0xBA15C-0xBA166 | info[+2] -> hdr[1] high nibble | dst nibble |
| 0xBA168-0xBA172 | info[+1] -> hdr[2], info[+0] -> hdr[3] | metadata |
| 0xBA174 | `bl 0x4bdce` | copy the MPDU payload (length from RX-info +0x11) |
| 0xBA17A | `bl 0x102760` | firmware hcc_msg_tx (post the D2H message) |

Surrounding facts: the function is CPU 0xB9780 (Thumb 0xB9781), registered in the
firmware's own dispatch as group-2 ids 2 and 5 (table CA 0x10CC18), and the builder's
48-byte RX info is zeroed at 0xB98EA and filled from the RX descriptor at
0xB98F8-0xB9A48 before the copy at 0xB9A4C-0xB9A56 [V].

---

## T2. Host hmac D2H dispatch table (.data + 0x584, 28 entries x 12 B) [V]

Entry layout is `{u16 id; u16 pad; u32 handler; u32 flags}` (the handler word carries
the R_ARM_ABS32 relocation; the id is at the entry start). Parse reproduced by
`evidence/host-dispatch-table.txt`.

| # | id | handler | note |
| ---: | ---: | --- | --- |
| 0 | 0x00 | hmac_sdt_up_reg_val | |
| 1 | 0x01 | hmac_create_ba_event | |
| 2 | 0x02 | hmac_del_ba_event | |
| 3 | 0x03 | hmac_event_config_syn | |
| 4 | 0x05 | hmac_syn_info_event | |
| 5 | 0x09 | hmac_bandwidth_info_syn_event | |
| 6 | 0x0a | hmac_protection_info_syn_event | |
| 7 | 0x0b | hmac_ch_status_info_syn_event | |
| **8** | **0x0d (13)** | **hmac_rx_process_mgmt_event (0x49FD0)** | **the CRX family** |
| 9 | 0x0e | hmac_mgmt_rx_delba_event | |
| 10 | 0x0f | hmac_scan_proc_scanned_bss | |
| 11 | 0x10 | hmac_scan_proc_scan_comp_event | |
| 12 | 0x11 | hmac_scan_process_chan_result_event | |
| 13 | 0x12 | hmac_event_acs_response | |
| 14 | 0x14 | hmac_mgmt_send_disasoc_deauth_event | (two table slots share this id via the second list) |
| 15 | 0x15 | hmac_mgmt_send_disasoc_deauth_event | |
| 16 | 0x16 | hmac_chan_switch_to_new_chan_complete | |
| 17 | 0x18 | hmac_mgmt_tbtt_event | |
| 18 | 0x19 | hmac_dfs_radar_detect_event | |
| 19 | 0x1a | hmac_proc_disasoc_misc_event | |
| 20 | 0x1d | hmac_acs_process_rescan_event | |
| 21 | 0x1f | hmac_csi_complete | |
| 22 | 0x22 | hmac_sr_bss_color_change_event | |
| 23 | 0x23 | hmac_sr_syn_parameter_event | |
| 24 | 0x24 | hmac_chr_report_msg | |
| 25 | 0x26 | hmac_config_set_security_port_event | |
| 26 | 0x27 | hmac_update_fbt_scan_result | |
| 27 | 0x25 | hmac_pfm_detect_process_event | |

Cross-check: `hmac_rx_event_pre_process` (host 0x49C28) strips 60 bytes and validates
the u16 at data+29 against total-60, which is exactly RX-info +0x11 as written by the
firmware builder at 0xBA0F8/0xBA156 [V].

---

## T3. CRX suppression gate matrix [V]

| # | CPU | condition | on failure | host-controllable? |
| ---: | ---: | --- | --- | --- |
| G1 | 0xB97EE | per-chip RX context `[chip+0x1a00] != 0` | error log, drop | only via firmware VAP/create (already done) |
| G2 | 0xB9816 | chip object resolvable from hcc instance | error log | no |
| G3 | 0xB98F8-0xB9A48 | RX descriptor carries a complete 48-byte info block | error log | no (hardware) |
| G4 | 0xB9A84 | info[+2] <= info frame_len | drop, log 0x9a4 | no |
| G5 | 0xB9C12 / 0xB9A8E | probe/fbt class (`[chip+0x5e]==0`, `[chip+0x70]!=0`, FC 0x40) | different event class | no |
| G6 | 0xB9A9A / 0xB9B30 / 0xB9B6C | frame_len windows 0x1c..0xfe / 0x4d..0xfe | drop + log | no (hardware) |
| G7 | 0xBA134 | RX-info forward flag (+6) != 0 | drop, log 0x49f | no (hardware) |
| G8 | 0xBA110 | frame_len + 60 <= 0x680 | drop, bump chip+0xCC5 | no (hardware) |
| G9 | 0xBA048 | frame-pool allocation succeeds | drop | no |

None of G2-G9 is reachable without a frame; G1 is created by the firmware's own VAP
setup, which our AP-start sequence already clears (beacons on air + ready/D2H traffic).

---

## T4. Read-only probe plan (all host-visible through the six inbound regions) [V]/[V\*]

| object | CA / offset | expected on a healthy AP boot | source |
| --- | --- | --- | --- |
| MAC live bank status | CA 0x40050000 +0x00 | RX bit2 clear until a frame is written | rx-static |
| MAC config mask | CA 0x40040000 +0x48 | 0xfffffcc1 | [V\*] stock dump |
| norm free-ring rptr | CA 0x40050000 +0x08 | 0x20 (prefetch-only) | [V\*] |
| small free-ring rptr | CA 0x40050000 +0x0c | 0x20 (prefetch-only) | [V\*] |
| completion wptr | CA 0x40050000 +0x10 | 0x0 | [V\*] |
| firmware hcc instance | CA 0x0010C0F4 | non-zero | [V\*] golden boot |
| DR record base | [instance+0x14] | CA 0x0010F2B8 | [V] ete-engine |
| DRn producer | base + n*0x354 + 0x0c | advances per D2H deposit | [V] ete-engine |
| DRn progress | base + n*0x354 + 0x10 | trigger-maintained | [V] |
| DRn trigger counter | base + n*0x354 + 0x344 | > 0 (D2H machinery ran) | [V] |
| DRn released counter | base + n*0x354 + 0x34c | >= 0 | [V] |
| CRX catalogue | host driver `rd.seen[]` | g1 id13 absent until a frame arrives | this lane |
| **refused** | CA 0x4016xxxx | not host-visible (historic wrap bug) | event-router |

---

## T5. Firmware access census: the MAC host-interface blocks are host-only [V]

* Whole-image Thumb/ARM literal scan for values in CA 0x40040000..0x4005ffff finds only
  **two** constants: 0x40041001 (0xFA27A, stored into a stack constant array) and
  0x40042000 (0xD2044, stored as a block base into a device object). Neither is used as
  a register address for a load/store.
* Therefore firmware code never reads or writes the MAC host-interface config/live
  blocks (CA 0x40040000 / 0x40050000). Those words are consumed exclusively by the MAC
  hardware (and written by the host driver).
* Consequence for crash analysis: our RX pointer publishes (+0x38/+0x3c/+0x40/+0x44)
  cannot corrupt firmware-owned registers; they can only influence MAC DMA behaviour.

---

## T6. RX-side writes our host can issue in a session (for the crash control) [V]

| write | CA | owner | risk class |
| --- | --- | --- | --- |
| free-ring producer norm | 0x40040038 | host driver (dp_rx_drain) | MAC consumes; malformed packed index/buffer VA -> MAC prefetch fault |
| free-ring producer small | 0x4004003c | host driver | same |
| completion consumer | 0x40040040 | host driver | MAC handshake; wrong phase could desync, not DMA |
| W1C MAC status | 0x40040044 | host driver (`rx_clear_status=1` only) | could hide evidence; no DMA |
| D2H group-B published free index | device DR record +0x38 | host driver (`rd_advance=pub` only) | firmware never reads it [V d2h-direct] |
| D2H group-A consumer +0x1C | device DR record +0x1C | host driver (`rd_ab_write=1` only) | experimental |
| mailbox out[3]/out[4] ack/re-arm | CA 0x40101438 / 0x40101414 | host driver | vendor-analog, proven |
| H2D txq_addr_sync / ctx-create | ETE SR ring + mailbox | host driver | known firmware-fault class if payload pointers are wrong |

Control rung R0 in FIRE-RECIPE.md disables every row marked host driver except the
vendor-analog mailbox service; if the exception still occurs, it is not an RX-write
artefact.
