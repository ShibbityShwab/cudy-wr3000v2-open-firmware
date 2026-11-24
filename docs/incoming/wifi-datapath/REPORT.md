# Wi-Fi data plane: hdpp/alg/wal message formats + host skb<->frame data path

**Lane:** wifi-datapath (shared task task-105). **Host-only static RE + compile-verified
builders.** 2026-11-24. **Deliverables:** this file, FRAME-SPECS.md, BENCH-LADDER.md,
src/ (dp_frames.h/.c + dp_selftest.c), tools/ (build.sh, fw_dp_tables.py),
tables/firmware-config-keys.md (generated).

**Read-first inputs (committed):** ../../phase20/tx-path.md (HCC frame + SR carrier),
../../phase43/THE-CHANNEL-MAP.md and channel-register-offsets.md (MAC slot-5 ring block),
../wifi-ctx-create/REPORT.md (g0 id1 key table + create path), ../wifi-h2d-catalog/REPORT.md
(firmware receive tables + handler CAs), ../wifi-h2d-frames/REPORT.md (config-event wire
shape), ../wifi-vendor-userspace/TABLES.md T6 (post-ready command trace),
../wifi-driver-re/REPORT.md (HCC header/dispatch, hdpp_main_init tables),
../wifi-ete-frames/REPORT.md (ETE rings), ../wifi-datapath-slice/REPORT.md (hdpp/alg census),
../../phase37/sr-message-catalog.md (vendor H2D senders), ../../phase37/d2h-message-catalog.md.

**Evidence markers:** [V] verified here from a named local artifact (firmware image / vendor
.ko disassembly / committed report); [V*] device-measured by an earlier attended lane and
cited; [I] inference; [U] undecided without a device read or vendor source.

**Address conventions:** firmware CPU = image file offset + 0x40000; host BAR0 of firmware
RAM = CPU + 0x6B8000 (region 5); device IO CA -> host BAR0 = 0x3B8000 + (CA - 0x40000000)
(region 3, window CA 0x40000000..0x4011FFFF); vendor .ko offsets are link-time .text
offsets. The pinned firmware image is the local recovery-extract FIRMWARE.bin, 928,920 B,
sha256 7fc87e20...311b (identical in the 2.4.15 and 2.5.24 rootfs). Vendor host-module
offsets are from the shipped 2.4.15 build (wifi.ko md5 1745ef77...) and were cross-checked
on the shipped 2.5.24 build (md5 4737fcb2...) where numbers differ. All binaries stay local
evidence; only decoded addresses/constants and original text/code are committed.

---

## 0. TL;DR - the ten answers

1. **There are two data planes, and Wi-Fi frames never travel in an HCC message.** The HCC
   message plane (12-byte header + payload on the ETE SR/DR rings) carries only control and
   event messages. User/AP/STA frames use a different hardware path: the MAC host-interface
   ring block at device CA 0x40040000 (band 0) / 0x40060000 (band 1), slot 5 of the vendor's
   10-word per-band channel table. The ETE register file (CA 0x4003A000) and the MAC ring
   file (CA 0x40040000) are distinct blocks owned by distinct modules: plat.ko contains zero
   words in 0x4004xxxx..0x4007xxxx and wifi.ko contains zero words in 0x4003xxxx [V]
   (../../phase43/THE-CHANNEL-MAP.md 1.3).
2. **The TX data path is a per-TID MSDU-info ring of 8-byte entries** {u32 buf_devva;
   u32 ctl} with ctl[11:0]=frame length, ctl[14:12]=tx path class, ctl[15]=header flag,
   ctl[19:16]=tid, ctl[21:20]=vap type (vendor writes 1/2/3) [V from
   hmac_tx_msdu_ring_push + hmac_tx_frame_control_dscr_set disassembly]. Each frame also
   carries a 20-byte host-built DMA descriptor at skb->data-0x14, DMA-mapped before the
   802.11 MPDU [V]. Rings are allocated per user/TID by shuangta_tx_msdu_ring_buf_alloc
   (non-cached alloc, depth from the device object, 8-byte entries) [V].
3. **The READY handshake seeds the TX data path.** The g4 id2 payload dword 0x01060850 is
   the device-RAM address of the MSDU-info ring pointer table; the vendor host reads it from
   the D2H read message (wlan_get_d2h_read_msg), maps it with memmap_get_dev_acp_addr and
   writes the result into MAC slot-5 register +0x10 for both bands
   (shuangta_set_msdu_info_ring_ptr_table_base) [V static chain]. The live register dump
   shows 0x40040010 = 0x02060850, the ACP alias of the ready payload [V*]. The next host
   step is the vendor's own first post-ready frame: the TXQ-table address sync, group 0 id 1,
   key 0x0320, 4-byte payload = device VA of the 0x2524-byte TXQ table [V from
   hmac_txq_table_init/addr_sync].
4. **The RX data path is three rings plus a BA-info buffer:** two free-buffer rings
   (rx_norm_data_free_ring 256 x 0x63c-byte buffers, rx_small_data_free_ring 512 x 0xb0-byte
   buffers), one completion ring (rx_data_cmp_ring, 4095 entries) and tx_ba_info (256
   entries). Every RX buffer starts with a 0x28-byte device-written descriptor followed by
   the 802.11 MPDU; the vendor parses that descriptor, converts 802.11->Ethernet and hands
   the skb to the stack [V from shuangta_host_rx_get_msdu_info_dscr +
   hmac_rx_ring_entry_proc* + hdpp_rxdata_*].
5. **The direct-read constraint is an ETE-only constraint.** It exists because the ETE
   engine's device-group-A -> host-group-B copy never runs on our boot, so D2H messages are
   read directly out of the device-side group-A ring. The MAC data rings are a separate DMA
   path; if the MAC block is programmed and the firmware's data path is initialized, RX
   deposits are written straight into host DRAM and do not need the ETE engine. That is this
   lane's working hypothesis [I]; BENCH-LADDER B2/B5 test it with explicit stop rules. TX
   ring consumption is likewise MAC-engine work, not ETE work [I].
6. **Data-path message family map (group/id):** TX queue setup = g0 id1 key table (0x0064
   create, 0x00d8 TXQ config sync, 0x0320 TXQ-table address sync + per-object config keys);
   users/STA/AP = g0 ids 7/8/9 (user add/add-notify/del, 0xa8/0xa8/0x0c payloads), g0 0xC
   scan request (0x1fc), g0 0xF/0x10 STA join (0x40/0x10), g0 0x12 EDCA (0x74), g0 0x1d
   EDCA-opt (0x3c), g2 WAL cfg events (AP start / scan-rsp / connect / disconnect; exact ids
   [U]); D2H side: g1 ids 15/16/17 scan result/complete/chan-result, g2 id2 scan rsp STA,
   g2 ids 3..6 asoc/disassoc/connect/disconnect, core-g0 ids 0..3 TX-complete/RX-data
   events, chip-g3 id5 rx-schedule [V tables, [I] pairing].
7. **The 217/88-entry firmware key tables are decoded and committed**
   (tables/firmware-config-keys.md): the group-0 id-1 handler CA 0x43D58 reads
   key = u16 hdr+0x14 and dispatches to a Thumb worker from table A (CA 0x10C23C, 217 slots)
   then table B (CA 0x103BF8, 88 slots); each table ends with a sentinel {key 0x1451,
   worker 0} [V]. All vendor wire keys from the shipped host modules (0x78, 0x311, 0x320)
   are present - cross-checked by tools/fw_dp_tables.py [V]. This table is the hdpp command
   namespace.
8. **Payload structs beyond the config-event family are not synthesizable** (vendor
   memcpy_s of live structs): user-add 0xa8, scan 0x1fc, EDCA 0x74, WAL group-2 bodies are
   catalogue-only [U]. Only the config-event shape (g0 id1/id2) is byte-exact today, plus
   the four builders shipped here (create, 0xd8 capture replay, 0x320 TXQ address sync,
   generic g0 cfg). A synthetic payload sent to a PTR_DEREF handler can fault the firmware -
   the library refuses by construction (validation only; no arbitrary payload emitter).
9. **The host data-path architecture is specified end to end** (section 9): bring-up order
   from the current working duplex loop -> TXQ address sync -> MAC slot-5 RX ring
   programming -> user/VAP/STA/scan messages -> netdev TX (skb -> 802.11 -> 8-byte ring
   entry) and NAPI RX (free ring -> completion -> skb -> eth). The current clean driver has
   no MAC data-path code at all: every register in this report must be added.
10. **Verification today: 77/77 host selftest checks and 35/35 firmware-table checks pass**;
    the builders are compile-clean under -std=c99 -Wall -Wextra -Werror -pedantic and were
    not fired at the device by this lane (the previously validated create+sync lap used the
    same g0 id1 geometry, which is why the captured vectors are trustworthy).

---

## 1. Scope, method, inputs

| input | what was used |
| --- | --- |
| pinned FIRMWARE.bin (local) | group-0 receive handler CAs, config key tables A/B, worker entry points (tools/fw_dp_tables.py) |
| hi5622v100_wifi.ko 2.4.15 md5 1745ef77 (local) | TX ring/descriptor builders, TXQ table/addr-sync, RX parser + 802.11->eth conversion, MAC ring register accessors, message builders |
| hi5622v100_wifi.ko 2.5.24 md5 4737fcb2 (local) | cross-check of the TXQ address sync + hdpp_config_send_event call sites (the build the live vendor capture came from) |
| hi5622v100_plat.ko (local) | ETE rings/mailbox only (no MAC block words) |
| committed lane reports | HCC header/carrier, ready handshake, create path, probe-risk classes, device register dump reg_all.txt |

Method: ELF symbol/relocation tables + capstone disassembly of the named functions
(opensource/lab/ko_disasm.py for the vendor modules; lane-local scanners for call-site and
argument recovery), plus address-arithmetic decoding of the firmware tables. No vendor byte,
object code, calibration value or per-unit identity is committed.

Reproduce:

    cd opensource/docs/incoming/wifi-datapath
    ./tools/build.sh
    /path/to/.provision-venv/bin/python tools/fw_dp_tables.py --selftest
    /path/to/.provision-venv/bin/python tools/fw_dp_tables.py --emit-md tables/firmware-config-keys.md

---

## 2. The two data planes

    CONTROL / EVENT PLANE (already working on the clean-room host)
    host HCC messages  --ETE SR ring-->  firmware HCC dispatch (group,id)
    firmware  --ETE DR ring (direct-read because the engine A->B copy is idle)-->  host

    Wi-Fi FRAME PLANE (this lane; not implemented in our driver yet)
    host DRAM  <--MAC slot-5 ring block DMA-->  device MAC/hdpp engine
      TX: per-TID MSDU-info ring (8-byte entries) -> 802.11 MPDUs
      RX: free-buffer rings (256/512) + completion ring (4095) <- 802.11 MPDUs

Both planes are initialized by the same post-ready host sequence, which is why the working
HCC loop is the right foundation: the vendor driver's hmac_main_init programs the MAC rings
and TXQ table inside the same init chain that sends the first g0 id1 frame.

### 2.1 The MAC host-interface register block (slot 5)

CA 0x40040000 (band 0) / 0x40060000 (band 1); eight blocks of one band's MAC, stride 0x2000;
this one is the host interface. Vendor live values from build/register-dumps/reg_all.txt
(cited in ../../phase43/channel-register-offsets.md 5.3) [V*]:

| reg | field | live value | host writes |
| ---: | --- | --- | --- |
| +0x00 | tx_ba_info_buff_depth (bits 15:0) | 0x100 (256) | init |
| +0x04 | tx_ba_info_buf_addr (device VA) | 0x83fd7000 | init |
| +0x10 | msdu_info_ring_ptr_table_base (device VA) | 0x02060850 | init = ACP(ready id2 payload) |
| +0x1c | rx_norm_buff_len[15:0] / rx_small_buff_len[31:16] | 0x00b0063c (0x63c / 0xb0) | init |
| +0x20 | rx_norm_data_free_ring addr | 0x844c7000 | init (pcie_if_hostca_to_devva) |
| +0x24 | rx_small_data_free_ring addr | 0x844c8000 | init |
| +0x28 | rx_data_cmp_ring addr | 0x83734000 | init |
| +0x2c | rx_norm_data_free_ring size (bits 11:0) | 0x100 (256) | init |
| +0x30 | rx_small_data_free_ring size | 0x200 (512) | init |
| +0x34 | rx_data_cmp_ring size | 0xfff (4095) | init |
| +0x38 | rx_norm free wptr (idx[14:0], phase bit15) | 0x8000 | host producer |
| +0x3c | rx_small free wptr | 0x8000 | host producer |
| +0x40 | rx_data_cmp rptr | 0 | host consumer |
| +0x44 | host MAC interrupt status (W1C) | 0 | ISR |
| +0x48 | host MAC interrupt mask | 0xfffffcc1 | init/ISR |
| +0x4c | rx_data_cmp wptr | 0 | device |
| +0x50 | rx_norm free rptr | 0 | device |
| +0x54 | rx_small free rptr | 0 | device |

All base-address registers are written with pcie_if_hostca_to_devva() output, i.e. host-DRAM
ring buffers seen through a device address window [V]. The index registers are packed the
same way as the ETE rings: index[14:0] | phase<<15 [V]
(../../phase43/channel-register-offsets.md 5.1 and the vendor hal_* accessors).

---

## 3. Firmware-side receive map: where each data-plane message lands

The group-0 receive table CA 0x10CCFC (40 x 12 B) and the group-2 table CA 0x10CC18
(19 x 12 B) are registered from the firmware boot chain and enumerated in
../wifi-h2d-catalog/REPORT.md 4 + Appendix A; the (group,id) -> handler-CA rows are
reproduced in src/dp_frames.c (dp_msg_catalogue) so the library can validate a frame against
the real receive map without re-reading the image.

Data-plane rows of interest (handler CA is firmware CPU):

| grp | id | message | payload B | firmware handler CA | status |
| ---: | ---: | --- | ---: | ---: | --- |
| 0 | 1 | hdpp config event (key table A/B) | dyn | 0x43D59 | exact frame family |
| 0 | 2 | alg config event (calibration/EDCA/ATF) | dyn | 0xD6069 | exact frame family; alg cfg ids [V catalogue] |
| 0 | 7 | hmac_user_add | 0xA8 | 0x9832D | catalogue only [U] struct |
| 0 | 8 | hmac_user_add_notify_alg | 0xA8 | 0x96225 | catalogue only [U] |
| 0 | 9 | hmac_del_user_notify_dmac | 0x0C | 0x974F9 | first fields decoded (u16 user; 0x0fff -> addr at +2) [V] |
| 0 | 0xA | hmac_mgmt_rx_delba | 0x20 | 0x5FF65 | [U] |
| 0 | 0xC | hmac_enc_scan_send_req | 0x1FC | 0x6494D | [U] struct; next cfg80211 scan milestone |
| 0 | 0xF | hmac_sta_sync_join_req_params | 0x40 | 0x62EC1 | [U] |
| 0 | 0x10 | hmac_sta_wait_join(_rx_beacon) | 0x10 | 0x5E28D | [U] |
| 0 | 0x12 | hmac_sta_up_update_edca_params_machw | 0x74 | 0x5DE7D | [U] |
| 0 | 0x13 | hmac_sta_up_update_mu_edca_params_machw | dyn | 0x968A5 | [U] |
| 0 | 0x1D | hmac_edca_opt_timeout_fn | 0x3C | 0xB568D | exact builder exists (all-zero payload) [V] |
| 2 | 2 | hmac_handle_scan_rsp_sta | dyn | 0xB9781 | [U] |
| 2 | 3 | hmac_handle_asoc_rsp_sta | 0x1C | 0xBBC9D | [U] |
| 2 | 5 | hmac_handle_connect_rsp_ap | 0x10 | 0xB9781 | [U] |
| 2 | 0x15 | hmac_config_receive_all_sta_rssi | 0x108 | 0x60B4D | [U] |
| 2 | 0x1C | hmac_tx_ba_notify | 0x14 | 0x845E1 | [U] |

Device->host events that complete/feed the data path (hdpp_main_init groups chip-g3 and
core-g0, hmac_main_init chip-g1/g2 - ../wifi-driver-re/REPORT.md 3.6):

| space | ids | meaning | status |
| --- | --- | --- | --- |
| core g0 | 0/1 | TX complete / TX event | [V] table, format [U] |
| core g0 | 2/3 | RX data event / RX schedule | [V] table, format [U] |
| chip g3 | 5 | rx-schedule request | [V] table |
| chip g1 | 15/16/17 | scan result / scan complete / channel result | [V] table, format [U] |
| chip g1 | 3 | config/alg/cali sync dispatcher (command completions) | [V*] (g1 id3 response received on bench) |
| chip g2 | 2/3/4/5/6 | scan-rsp / asoc / disassoc / connect / disconnect | [V] table |

---

## 4. The g0 id1 hdpp command table (decoded)

The firmware group-0 id-1 handler CA 0x43D58 decodes key = u16 hdr+0x14 (payload+0x08),
subkey = u8 hdr+0x16, data = hdr+0x18 and dispatches to a worker from two static tables
(../wifi-ctx-create/REPORT.md 2/3; re-decoded and asserted here):

| table | CA | slots | format |
| --- | ---: | ---: | --- |
| A | 0x10C23C | 217 | 8 B {u16 key; u16 pad; u32 worker} |
| B | 0x103BF8 | 88 | idem |

Both end with a sentinel slot {key 0x1451, worker 0}; live workers are Thumb addresses
(bit0 set) [V]. The full decoded map is committed as tables/firmware-config-keys.md; the
data-plane keys:

| key | entry | worker CA | role | evidence |
| ---: | ---: | ---: | --- | --- |
| 0x0004 | A0 | 0x811CD | module flag write via created ctx (+0x52) | [V] table |
| 0x0064 | A1 | 0x449E5 | context create (only H2D route to CA 0xFE6BC -> ctx+0x190) | [V]/[V*] bench |
| 0x0078 | A78 | 0xA5825 | key install (vendor: hmac_user_add_key / hmac_install_rekey) | [V] table + sender |
| 0x00D8 | A41 | 0x81039 | TXQ config sync (data[0] -> ctx+0x190+0x4c) | [V] table + vendor capture |
| 0x0128 | B28 | 0x8313D | hmac band config (vendor hmac_config_alg) | [V] table + sender |
| 0x0311 | A178 | 0x436F9 | waveapp scene config | [V] table + sender |
| 0x0320 | A164 | 0x81445 | TXQ-table address sync (payload = 4-byte devVA) | [V] table + host disasm |

Cross-check that makes this table authoritative: every hdpp_config_send_event /
hmac_config_send_event key present in the shipped 2.5.24 host module (0x78 twice, 0x311,
0x320) resolves in these tables (tools/fw_dp_tables.py --selftest asserts it) [V]. The
vendor producer of the captured 0x00d8 frame is not in the shipped teardown build (known
phase38 caveat: the live box carried an overlay-patched wifi.ko), but the firmware consumer
is proven: the 0x00d8 worker starts with ldrb data[0]; ldr [ctx+0x190]; strb
[ctx190+0x4c], which is exactly why the bench needs the key-0x64 create first
(../wifi-ctx-create/REPORT.md 8) [V*/V].

### 4.1 The config-event wire shape (g0 id1/id2)

Payload = token[8] | u16 key/cmd | u16 len | data[len] | pad[4]; total = 0x1C + len; header
byte[0]=0 (group), byte[1]=0x01 (alloc state), byte[2]=field A, byte[3]=field B (the module
index the handler resolves), byte[4..5]=total, byte[6..7]=id (1 hdpp / 2 alg), byte[8]=field
C, byte[9]=dst, byte[0xa..0xb]=tag 0x5a5a [V] (phase20 A.5 + phase38 + wifi.ko 0x59c0
hdpp_config_send_event: strh command at hdr+0x14, strh len at hdr+0x16, memcpy at hdr+0x18).

---

## 5. Vendor host senders (what produces data-plane traffic)

### 5.1 The post-ready init chain (host side)

From hmac_main_init -> hmac_board_init -> hdpp_chip_sync_add (../wifi-vendor-userspace T6):

| # | vendor function | message | payload | evidence |
| ---: | --- | --- | --- | --- |
| 1 | shuangta_rx_host_init_dscr_queue | none (MAC block registers) | ring addr/size/len, both bands | [V] disasm |
| 2 | shuangta_ba_info_res_alloc | none (MAC block registers) | tx_ba_info addr/depth | [V] disasm |
| 3 | hmac_tx_msdu_info_ring_table_addr_init | none (MAC block register) | +0x10 = ACP(ready id2 payload) | [V] disasm |
| 4 | hmac_txq_table_init/addr_sync | g0 id1 key 0x0320 | 4 B device VA of the TXQ table (host DRAM noncached, 0x2524 B) | [V] disasm |
| 5 | per-object sync writers (hdpp_*_sync_write) | g0 id1 keys (individual) | object-specific | [V] family, keys [U] |
| 6 | hmac_config_alg_send_event | g0 id2, key = alg cfg id | calibration/EDCA/ATF data | [V] family |

hmac_txq_table_init (wifi.ko 0x1b0b4 in 2.4.15) allocates the 0x2524-byte table via
oal_noncache_alloc, stores the host pointer at device+0x928 and the DMA address at
device+0x948, then calls shuangta_ba_info_res_alloc,
hmac_tx_msdu_info_ring_table_addr_init and hmac_txq_table_addr_sync; the last one is exactly
hdpp_config_send_event(dev, cmd=0x320, len=4, data=&devva) [V].

### 5.2 The vendor host-side data-ring builders

| function (2.4.15 wifi.ko) | role | evidence |
| --- | --- | --- |
| hmac_tx_msdu_dscr_init | zero the per-TID ring list heads + 0x40 default | [V] |
| hmac_tx_msdu_ring_init | per (user,tid) ring: depth from device obj, shuangta_tx_msdu_ring_buf_alloc | [V] |
| shuangta_tx_msdu_ring_buf_alloc | noncached alloc of depth*8 B, ring struct {base,dma,idx,...} | [V] |
| hmac_tx_msdu_dscr_add | 20-byte DMA descriptor at skb->data-0x14, dma_map, inqueue | [V] |
| hmac_tx_msdu_ring_push | build 8-byte entry, write via shuangta_set_tx_msdu_info_ring, publish packed index, DMB/DSB+SEV | [V] |
| hmac_tx_msdu_ring_post_proc | completion: dma_unmap, stats, free descriptor | [V] |
| shuangta_host_rx_get_msdu_info_dscr | parse the 0x28-byte RX descriptor into a 0x30-B msdu-info | [V] |
| hmac_rx_ring_entry_proc_frame | 802.11 header/address resolution, defrag, 802.11->eth, netif | [V] |
| hdpp_rxdata_netbuf_enqueue / hdpp_rxdata_sched | RX packet enqueue/schedule hooks | [V] names |

### 5.3 The alg (g0 id2) layer

hmac_config_alg_send_event(dev, key, len, data) builds the same config-event shape with
id=2; the first 8 data bytes go into the token, then key at payload+8, len at +0xa, data at
+0xc (wifi.ko 0x60c74) [V]. Keys are the vendor alg cfg ids (the 414-entry
g_ast_alg_cfg_process_info_table, e.g. 0x0dae = get_2g_power_param; phase20 A.5 live
capture). Boot order: after ready the vendor applies six calibration set_* messages
(../wifi-vendor-userspace T5/T6), each answered by a g1 id3 sync response; those are the
first g0 id2 sends a clean-room host must reproduce. The alg cfg-id values are committed
vendor tables (../wifi-algcfg-slice) and are not repeated here.

---

## 6. TX data path (host -> air)

### 6.1 Objects

    per band (device object; 0x58-byte user slots, 0x58-byte TID slots):
      user/tid tx-ring struct at device+0xadc+tid*0x58 ... (list head)
      ring base pointer  [ring+0xb1c]
      ring post module   [ring+0xb20]
      msdu counters at +0xaec/+0xaf0, in-ring flag +0xb2a
    device-wide TXQ table: 0x2524 B host-DRAM noncached, devVA synced by g0 id1 key 0x0320
    per-TID MSDU-info ring: depth*8 B, one entry per posted frame

### 6.2 The 8-byte ring entry

| off | field | source in the vendor builder |
| ---: | --- | --- |
| +0 | u32 buf_devva | shuangta_get_msdu_info_ring_addr(skb) (skb->data-0x10) -> pcie_if_hostca_to_devva |
| +4 | u32 ctl | bits[11:0] = skb->len; [14:12] tx path class; [15] header flag; [19:16] tid/priority; [21:20] vap type 1/2/3 (hmac_tx_frame_control_dscr_set writes 0x10/0x20/0x30) |

Uncertainty: the exact host-VA -> device-VA transform for a frame buffer (the builder adds
0xC0600000 before pcie_if_hostca_to_devva) must be verified on the bench before a clean-room
host DMAs real frames; the address model is an [I] here and a B-ladder read-only check (B2).
The library exposes the encoder so the descriptor bits can be exercised host-only.

### 6.3 Producer index (packed)

index[14:0] | phase<<15; after every post: next = index+1; if next == depth then index = 0,
phase ^= 1. The vendor publishes with dmb ish / dsb ishst + sev around the index store
(hmac_tx_msdu_ring_push 0x19174) [V]; a clean-room host must issue the same barriers before
the device's MAC engine reads the entry.

### 6.4 TX completion

The device reports completions through the tx_ba_info buffer (256 entries; device VA written
at MAC+0x04) and/or the core-g0 id0/1 TX events; shuangta_get_tx_msdu_dscr_info copies one
16-byte completion record out of the BA-info entry [V width], and
hmac_tx_complete_event_handle (984 B) parses it [V function, field map [U]]. The 16-byte
record is the natural first target for a follow-up lane: small, host-parseable, and the
existing D2H event router can deliver the event.

---

## 7. RX data path (air -> host)

### 7.1 Rings

| ring | entries | entry/buffer size | filled by | consumed by |
| --- | ---: | --- | --- | --- |
| rx_norm_data_free_ring | 256 | buffer 0x63c B | host posts free buffers | device writes frame + descriptor |
| rx_small_data_free_ring | 512 | buffer 0xb0 B | host | device |
| rx_data_cmp_ring | 4095 | completion record | device | host |
| tx_ba_info | 256 | 16-byte BA records | device | host |

### 7.2 The 0x28-byte device-written RX descriptor

shuangta_host_rx_get_msdu_info_dscr (wifi.ko 0x3843c) reads the descriptor at the start of
the RX buffer and fills a 0x30-byte host msdu-info record. Observed field accesses (offset
from buffer start; width/extract from disassembly) [V]:

| off | access | extract | candidate meaning |
| ---: | --- | --- | --- |
| 0x08 | u16 | [11:0] | first segment/MPDU length |
| 0x0c | u16 | [15:4] | word split across +0x0c/+0x0e |
| 0x0e | u16 | [11:0] | second length |
| 0x10 | u32 | full | device address/pointer field |
| 0x14 | u16 | full | |
| 0x16 | u16 | full | |
| 0x18 | u16 | [11:0] | length |
| 0x19 | u8 | [6:5] | band/vap? |
| 0x1b | u8 | [7:4],[3:0] | flags |
| 0x1c | u16 | [15:4] | |
| 0x1e | u16 | full - skb data length used for skb_put | payload length (must be >= 0x28) |
| 0x20 | u16 | [11:0] | length |
| 0x21 | u8 | [6:4] | flags |
| 0x23 | u8 | [3:0],bit7 | tid? / flags |
| 0x28 | - | - | 802.11 MPDU starts here (0x28-byte descriptor) |

After parsing, the vendor maps the buffer, calls skb_put(skb, len+0x28), then advances
skb->data by 0x28 and shrinks skb->len accordingly - the descriptor is stripped and the
payload becomes the 802.11 MPDU [V]. hmac_rx_ring_entry_proc then resolves VAP/user/tid from
descriptor +0x20/+0x21/+0x23/+0x28, runs defrag/BA/decrypt paths and finally
hmac_rx_ring_entry_proc_frame converts 802.11->Ethernet (A-MSDU/sub-MSDU handled by
shuangta_host_rx_get_msdu_info_dscr / shuangta_rx_get_next_sub_msdu chains, sub-MSDU skbs
linked through skb->next) [V].

The exact bit semantics of the descriptor are [I]/[U]; the offset/width table above is the
observed access list and is the input for a future byte-exact decode with a vendor capture.

---

## 8. The direct-read constraint: what it does and does not limit

Facts (ledger 2026-11-24 14:00 and 2026-11-25): the ETE engine's A->B copy is
firmware-triggered and never runs on our boot (status CA 0x4003A874 = 0, send words 0, DR
fills static 0x10); D2H messages are therefore read directly from the device group-A DR ring
and their frame bodies through the ACP window. H2D still works because the device ETE engine
consumes SR nodes (proven on bench).

Why the frame data path is different:

* RX frame deposits are performed by the MAC/hdpp engine into free-ring buffers and
  signalled through the completion ring and/or the host-MAC interrupt (slot-5 +0x44/+0x48),
  not through the ETE block. The vendor pcie_ete_* functions never touch CA 0x4004xxxx; the
  MAC block shares only the address-conversion helper pcie_if_hostca_to_devva [V].
* TX frame fetches are performed by the device MAC engine from host-DRAM MSDU rings; the
  host only publishes ring entries (and the TXQ table address once) [V].

Therefore the direct-read workaround is not expected to block data traffic provided the
firmware's MAC data path is initialized and the MAC block is programmed. That is what
BENCH-LADDER B2 tests with a read-only register probe first and one ring-programming step
second; until measured the claim stays [I].

Operational consequences:

* keep the ETE direct reader as the message service and never depend on the engine for it;
* for data, poll/interrupt on the MAC block interrupt status (+0x44) with mask (+0x48)
  instead of the ETE completion word;
* the ACP window is the only path to device-RAM rings (MSDU ptr table at 0x02060850); host
  DRAM rings are mapped through the same conversion helper;
* coherency: the vendor keeps rings in non-cached memory (oal_noncache_alloc; MSDU entries
  are DMA-read by the device) and dma-maps frame buffers - a clean-room host must do the
  same (dma_alloc_coherent for rings/descriptors).

---

## 9. Host skb <-> frame data-path specification

This is the architecture the clean-room driver (hi5622_wifi.ko / cfg80211 lane) must add to
the working HCC transport. It is staged so each step has a host-only test and one bench
observable.

### 9.1 Bring-up order (after the working duplex loop)

    0. golden boot -> ready pair -> context-create (0x64) -> TXQ sync (0xd8)   [DONE, bench]
    1. TXQ-table address sync: g0 id1 key 0x0320, data = devVA(txq_table)     [builder ready]
    2. MAC slot-5 init, band 0 then band 1:
         +0x10 = ACP(ready id2 payload)                  (MSDU ptr table)
         +0x1c = (small<<16)|norm  = 0x00b0063c           (buffer lengths)
         +0x20/+0x24/+0x28 = devVA(free rings + cmp ring)
         +0x2c/+0x30/+0x34 = 256/512/4095
         +0x00/+0x04 = tx_ba_info depth/addr (0x100 / devVA)
         +0x48 = 0xfffffcc1, then unmask the bits actually handled
       (all writes read-modify-write, one band first, read back every word)  [no message]
    3. user/VAP context: g0 id7 user-add (0xa8) / g0 id8 alg-notify, g2 WAL VAP/AP start
    4. algorithm/EDCA: g0 id2 calibration set_* (six), g0 0x12 EDCA per STA/AP
    5. link: scan g0 0xC -> results g1 15/16/17/g2 2; STA join g0 0xF/0x10; AP start g2
    6. data: netdev TX -> 8-byte ring entries; RX completion -> skb -> eth

### 9.2 TX (ndo_start_xmit)

    skb (Ethernet, 802.3)
      -> ethertype/vlan parse, 802.11 header build (vendor equivalent hmac_tx_encap: a
         20-byte DMA descriptor at skb->data-0x14 followed by the 802.11 header/MPDU;
         pskb_expand_head ensures >= 0x14 headroom)
      -> map the frame buffer for DMA -> buf_devva
      -> per (user,tid) ring: entry = {buf_devva, ctl(frame_len, path_class, hdr_flag,
         tid, vap_type)}   (dp_tx_msdu_entry_encode)
      -> publish packed producer index (dp_tx_idx_next) with dmb/dsb + sev
      -> return TX_OK; skb is freed from the completion path (hmac_tx_msdu_ring_post_proc
         equivalent) after the BA-info completion record / TX event

Ring policy: one ring per user/TID; stop the queue when the ring is full (published vs
consumed index from the hdpp object stats); never reuse an entry before its completion.

### 9.3 RX (NAPI poll)

    host pre-fills the rx_norm free ring with buffers (dma_alloc_coherent or page_pool),
    advances +0x38 packed wptr
      -> device writes 0x28-B descriptor + MPDU into a free buffer and posts a completion
         record on rx_data_cmp_ring (+0x4c device wptr, +0x40 host rptr)
      -> NAPI/ISR: read completion records up to the device wptr; for each completed buffer:
           parse descriptor (section 7.2), strip 0x28, handle A-MSDU/sub-MSDU chains,
           resolve VAP/user/tid, decrypt if needed, build skb (or zero-copy skb frag),
           802.11->eth, netif_receive_skb, then repost the buffer to the free ring
         advance cmp rptr (+0x40) and free-ring indices per the device contract
      -> errors: same filter path (FCS/decrypt/deauth) and drop

### 9.4 Register/address helpers the driver must own

* device CA -> host BAR0: 0x3b8000 + (ca - 0x40000000) (region 3, 0x120000 B), already used
  by the transport; the MAC block CAs are inside it [V report], but an earlier lane recorded
  a bus error reading BAR0 0x402a004 while the declared region covers that CA [V*] -
  BENCH-LADDER B2 resolves this before any host write; do not add an extra iATU viewport
  (a prior session rebooted the box doing exactly that).
* host DRAM ring buffer -> device VA: the vendor pcie_if_hostca_to_devva conversion
  (implemented in our transport already for ETE rings; the data path reuses it).
* firmware-RAM reads (read-only observables): BAR0 = CPU + 0x6B8000 (region 5).
* monitor mode: same data rings; not required for the AP/STA milestones.

### 9.5 Milestones (proposed split for the cfg80211 work)

| M | deliverable | depends on | host-only test | bench observable |
| ---: | --- | --- | --- | --- |
| D0 | MAC block register model + RMW writers/readers | this report | ring-constant selftest (done) | B2 read pack |
| D1 | RX rings alloc + program + NAPI skeleton, no frames | D0 | ring arithmetic selftest | device cmp wptr advances with beacons/acks |
| D2 | TX ring + 8-byte entry + hdpp TXQ addr sync | D1 + 0x320 builder | entry encode selftest | no exception; device consumes ring entries |
| D3 | user/VAP/AP messages (needs struct RE) | g0 id7/8, g2 WAL | catalogue validation | firmware ACKs / beacon starts |
| D4 | scan (g0 0xC) + result path | scan struct RE | catalogue | g1 15/16/17 event decode |
| D5 | STA join/connect | g0 0xF/0x10 structs | catalogue | g2 3/4/5/6 events |

---

## 10. Minimum viable first data message sequence

From the current validated state (create 0x64 + TXQ sync 0xd8 consumed, mask 0, firmware
responded g1 id3/g1 id0x12), the smallest new steps toward frame data are:

1. **TXQ-table address sync** - g0 id1, id=1, fb=0, key 0x0320, len 4, data=devVA of a
   host-allocated zeroed 0x2524-byte non-cached table. Builder: dp_build_txq_addr_sync().
   This is the vendor's own init message [V]; it is the only new byte-exact frame this lane
   proposes firing now.
2. **MAC slot-5 RX ring programming** - host register writes only (no message), band 0
   first; register set/values in 2.1/9.1. First step is a single read of CA 0x40040000 to
   prove the window works (B2), then RMW writes with read-back.
3. **user-add / VAP-add** - g0 id7/g2 WAL cfg; payload structs not recovered yet, so
   catalogue-only and need a struct-RE lane or a vendor capture.
4. **scan request** g0 id0xC (0x1fc) once the scan struct is recovered; result events are
   g1 15/16/17 and g2 2.
5. **STA join** g0 0xF (0x40) / 0x10 (0x10) for STA mode, AP start via WAL cfg group 2 for
   AP mode, then data frames on the rings.

---

## 11. The frame-builder library (verification)

src/dp_frames.h/.c implements only what static evidence supports:

| API | purpose | vector/verification |
| --- | --- | --- |
| dp_frame_validate/decode | HCC header model + firmware hard checks (tag, total, alloc nibble, dst, group<=2) | refusal cases in selftest |
| dp_build_g0_cfg | generic group-0 id1/id2 config event | structural checks |
| dp_build_ctx_create | g0 id1 key 0x64 type/parent/user | byte-exact vs the bench-validated 48-B frame (+72-B 0xff export) |
| dp_build_txq_sync_capture | g0 id1 key 0xd8 vendor capture replay | byte-exact 48-B vector |
| dp_build_txq_addr_sync | g0 id1 key 0x320, 4-byte devVA | total 0x20, key/len/data layout asserted |
| dp_tx_msdu_entry_encode | 8-byte TX MSDU-info entry | bitfield roundtrip + refusals |
| dp_tx_idx_pack/unpack/next | packed 15-bit idx + phase bit15 | wrap vectors 0->1, 31->0x8000, 0x801f->0 |
| dp_msg_catalogue/lookup | (group,id) -> sender/fw-handler/catalogue size | handler CAs + sizes asserted |

Build/test evidence:

    $ ./tools/build.sh
    == compile (-std=c99 -Wall -Wextra -Werror -pedantic) ==
    == run selftest ==
    SUMMARY: 77 passed, 0 failed
    BUILD OK (host-only; no device contact)

    $ python3 tools/fw_dp_tables.py --selftest
    SUMMARY: 35 passed, 0 failed

---

## 12. Open items / probes (what stays [U])

1. TX frame buffer address transform (+0xC0600000 then hostca_to_devva): bench read-only
   check against a descriptor dump (B2).
2. RX descriptor bit semantics (only offsets/widths observed): needs one vendor-boot buffer
   capture (read a host-DRAM free-ring buffer after a beacon) - read-only, one shot.
3. TX completion record layout (16 B from tx_ba_info): probe with the first TX ring post;
   parse the shuangta_get_tx_msdu_dscr_info copy.
4. MAC block reachability (region-3 window bus-error discrepancy): B2 single read.
5. WAL group-2 cfg ids (AP start etc.): not recovered; needs a vendor kprobe capture or GPL
   source (same gap as wifi-vendor-userspace G1).
6. user-add 0xa8 / scan 0x1fc / EDCA 0x74 structs: payload-struct wall; each needs its own
   builder + known-answer vector once recovered.
7. Engine A->B copy: not needed for the data-path hypothesis; still the clean fix for
   message RX (task-106 lane).

---

## 13. Evidence index

| claim | evidence |
| --- | --- |
| HCC message plane vs MAC frame plane separation; MAC block offsets/live values | ../../phase43/THE-CHANNEL-MAP.md 1.3/2; ../../phase43/channel-register-offsets.md 5; build/register-dumps/reg_all.txt [V*] |
| HCC 12-byte header + config-event payload shape; first post-ready frame | ../../phase20/tx-path.md A.4/A.5; ../../phase38/announce-body-bytes.md; ../wifi-h2d-frames/REPORT.md 3/4.2 [V] |
| g0 id1 key tables A/B, sentinel, worker keys | tools/fw_dp_tables.py --selftest (35/35); tables/firmware-config-keys.md [V] |
| create path (key 0x64, ctx+0x190) | ../wifi-ctx-create/REPORT.md + lane tool 40/40 [V]; bench lap 2026-11-25 05:10 [V*] |
| TXQ table init + address sync (0x320, 4-byte devVA) | wifi.ko 2.4.15/2.5.24 disassembly hmac_txq_table_init/addr_sync; hdpp_config_send_event 0x59c0 [V] |
| MSDU ring ptr table seeded by ready id2 payload | wifi.ko hmac_tx_msdu_info_ring_table_addr_init + wlan_get_d2h_read_msg; ledger 2026-11-24 14:00 (id2 payload 0x01060850) [V]/[V*] |
| TX ring entry 8 B + ctl bits; 20-B DMA descriptor; packed index | wifi.ko hmac_tx_msdu_ring_push, hmac_tx_frame_control_dscr_set, hmac_tx_msdu_dscr_add, shuangta_tx_msdu_ring_buf_alloc [V] |
| RX rings/sizes + 0x28-B descriptor + 802.11->eth | wifi.ko shuangta_host_rx_get_msdu_info_dscr 0x3843c, hmac_rx_ring_entry_proc* 0xf258/0x101e8, shuangta_rx_host_init_dscr_queue 0x37888 [V]; live values phase43 5.3 [V*] |
| ready handshake g4 id1/id2 + payload | ledger 2026-11-24 14:00; ../wifi-d2h-direct/REPORT.md [V*] |
| message map (group/id/sender/handler/size) | ../wifi-h2d-catalog/REPORT.md + catalogue.md; ../../phase37/sr-message-catalog.md; ../wifi-vendor-userspace/TABLES.md T6 [V]/[I] |
| D2H event tables | ../wifi-driver-re/REPORT.md 3.6 [V] |
| builders compile/test | tools/build.sh, 77/77 [V] |

---

## 14. License

All committed text and code in this lane is original writing (GPL-2.0 for the C library,
matching the kernel it links against). No vendor code/text, object code, firmware bytes,
calibration data or per-unit values are reproduced. Vendor binaries/firmware remain local
evidence only; the decoded tables contain protocol addresses/constants, no vendor bytes.
