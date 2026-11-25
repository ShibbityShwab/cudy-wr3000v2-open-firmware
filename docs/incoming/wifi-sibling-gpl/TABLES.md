# TABLES - sibling GPL decode: constants and side-by-side comparison

Source pins: Hi3881V100 = `openharmony/device_soc_hisilicon` @ `7c1e2d5b35c090009bbe195f09337701c60af032`
(repo licence file: `common/platform/wifi/hi3881v100` -> **GPLv2**).  Hi1105 =
`5-Super-Rookie-5/hongmengkernel` @ `297b33b07b56adb3fa53baf618fdbc16ad9fd447` (files say
"All rights reserved", **no open licence** -> structure reference only).  Every row carries the
raw path and line range; `tools/verify_quotes.py` re-checks T0/T3/T4/T5/T6/T8 quotes.

Raw URL prefixes:

* `OH=` `https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/`
* `HM=` `https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/main/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/`
* `RAW=` `https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/`

---

## T0. Evidence inventory (what was fetched and from where)

| id | source | role | licence |
| --- | --- | --- | --- |
| S1 | Hi3881V100 full host stack (250 files: `hcc/ mac/common mac/hmac oal wal frw oam adapter`) | RX/mgmt/flows | GPLv2 |
| S2 | Hi1105 vendor tree (`platform/`, `wifi/dpe`, `wifi/hmac`, `wifi/wal`) | PCIe HCC + DMA ring HAL + NAPI | all rights reserved (reference only) |
| S3 | our vendor host modules 2.4.15 / 2.5.24 + disassembly (local, never committed) | cross-check of S1/S2 patterns on the real chip | local evidence only |
| S4 | our committed lane docs (wifi-rx-static, wifi-ap-mgmt, wifi-sta-ops, wifi-data-tx) | our decoded model | original |

Hi3881 architecture (same layer split as our vendor): `hcc` = transport service (SDIO),
`mac/common` = MAC objects/pools, `mac/hmac` = host MAC (SME + data path), `wal` = cfg80211
glue, `oal` = OS abstraction + SDIO bus, `frw` = event framework.  Our vendor has the identical
split (confirmed by the 2,023 `hmac_*` symbols in the 2.5.24 module).

---

## T1. Transport / message layer

| property | Hi3881V100 (SDIO, GPLv2) | Hi1105 (PCIe, vendor) | hi5622v100 (ours) |
| --- | --- | --- | --- |
| host->device doorbell | bitmap `1<<msg_id` written to SDIO function-1 reg 0x24; one MFARM interrupt class | PCIe shared-memory H2D/D2H ringbuf: ctl block + data block; host writes tx payload, then `wr`; firmware ISR acks | mailbox pending word + doorbell 0x400392d4 / ack 0x400392f0; ETE SR/DR rings (committed lanes) |
| host->device message ids | 32-entry enum H2D 0..19 [OH `oal/oal_sdio_comm.h:78-109`] | HCC bus message ids (`hcc_bus_send_message`, `hcc_message_register`) [HM `platform/oal/oal_hcc_bus.c:991,1027,1094,1561`] | HCC 12-B header + payload; H2D id 3 announce/boot ladder (committed wifi-transport-port) |
| device->host pending | msg bitmap read together with int status in one 64-B function-register read at 0x30 [OH `oal/oal_sdio_host.c:502-528`] | D2H ring `wr` polled by host; ack ISR completes a wait [HM `platform/oal/pcie/pcie_firmware_msg.c:337-363,386-401`] | D2H ring + mailbox ($D2H$ deliver path, committed `hi5622_hcc_deliver()`) |
| RX data carriage | SDIO data-ready IRQ + xfer count + scatter list; up to 64 device->host scatter buffers [OH `oal/oal_sdio_host.c:257-286`, `oal/oal_sdio_comm.h:128-133`] | DMA rings in host DRAM (free/completion), device is bus master | DMA rings in host DRAM (live bank), device is bus master |
| management carriage | **HCC control event** (FRW_EVENT_TYPE_WLAN_CRX) - *not* a data ring | **separate firmware-message event** to hmac (not the ring) | **D2H cfg-event** (event id 13 -> `hmac_rx_process_mgmt_event`) - *not* the ring |

## T2. RX ring geometry and entry format (where rings exist)

| property | Hi1105 HAL [HM `wifi/dpe/hal/host_hal_dscr.h:44-62`, `host_hal_dscr.c:658-787`] | our hi5622 (committed wifi-rx-static) |
| --- | --- | --- |
| normal free ring | 4095 entries x **8 B** | **256** entries x 4 B |
| small free ring | 512 entries x 8 B | **512** entries x 4 B |
| completion ring | 4095 entries x 8 B | **4095** entries x 4 B |
| entry content | 64-bit device VA of the RX netbuf | 32-bit device VA of `buffer+4` |
| buffer address rule | `HAL_RX_NETBUF_ADDR_OFFSET 4`, align 8 | `buffer+4`, align 4 |
| packed index format | 16-bit union: 15 index bits + wrap flag bit15 [HM `host_hal_ring.h:116-143`] | 15 index bits + phase bit15 (V, same bit) |
| entry-clear on read | yes, `memset` after copy [HM `host_hal_ring.c:474-524`] | **not implemented** -> proposal P1 |
| ring reset | optional `HAL_RX_RING_RESET_MASK 0x30` assert/deassert [HM `host_hal_dscr.c:839-888`] | vendor 2.5.24 RX init has **no** reset write [V] |
| RX ptr report mode | optional host-memory block written by MAC (reduce-load mode) [HM `host_hal_dscr.c:601-656`] | live register bank CA 0x40050000 (our decode) |

Note the two depths that match exactly (small 512, completion 4095): the two HALs are the same
design line, with the normal ring enlarged on Hi1105 and 64-bit devVAs instead of 32-bit CAs.
This is strong cross-evidence for our ring model itself.

## T3. Producer/consumer protocol and publish

| step | Hi1105 HAL | our model |
| --- | --- | --- |
| free ring owner | **software writes wptr, HW writes rptr** (`HAL_RING_TYPE_FREE_RING`) [HM `host_hal_ring.h:135-143`] | host publishes config +0x38/+0x3c; device advances live +0x08/+0x0c |
| completion ring owner | **HW writes wptr, software writes rptr** (`HAL_RING_TYPE_COMPLETE_RING`) | device advances live +0x10; host publishes config +0x40 |
| device->host pointer read | `hal_ring_get_hw2sw()`: free ring reads the rptr register, completion ring reads the wptr register [HM `host_hal_ring.c:198-231`] | read live bank +0x08/+0x0c/+0x10 |
| host->device pointer publish | `hal_ring_set_sw2hw()`: free ring writes the wptr register, completion ring writes the rptr register [HM `host_hal_ring.c:240-255`] | write config +0x38/+0x3c/+0x40 |
| pending/consumed arithmetic | full/empty resolved with wrap flag; one-lap case explicit [HM `host_hal_ring.c:263-315`] | same with phase bit (`hi5622_rx_ring_pending/consumed`) |
| validity wait | producer-visible-before-entry-committed tolerated: spin until entry != 0 (<=1000) [HM `host_hal_ring.c:406-463`] | **absent** (zero entry closes the lane) -> proposal P1 |
| batch consume | burst read 16 entries into a bounce buffer, then advance rptr [HM `host_hal_ring.c:356-403,806-855`] | one sysfs-driven drain, <=64 (rx_drain_max) |

## T4. RX refill / flow control

| mechanism | Hi3881V100 (SDIO) | Hi1105 (PCIe) | ours |
| --- | --- | --- | --- |
| buffer source | host builds netbuf list per transfer; reserved buffer used to drain oversized transfers [OH `oal/oal_sdio_host.c:551-608,636-713`] | allocate skb, DMA map, push devVA into free ring, publish wptr [HM `host_hal_dscr.c:229-262`] | skb pool + devVA publish (dp_setup refill) |
| refill trigger | data-ready handler -> hcc rx queue | ISR RX branch refills both rings if MPDU queue not full; free-ring-empty IRQ schedules refill [HM `host_hal_irq.c:218-237`] | `dp_rx_drain` after completions (bounded) |
| back-pressure | HCC RX high-waterline: drop-oldest when queue over high waterline [OH `hcc/hcc_host.c:1121-1131`] | "MPDU queue full" stall flags gate refill [HM `host_hal_dscr.c:229-262` + our vendor ISR decode] | lane closes on anomaly (fail-closed) |
| credit accounting | SDIO `credit_info` word: short/large/reserve/free counts + seq [OH `oal/oal_sdio_comm.h:111-120`; update `oal_sdio_host.c:445-473`]; D2H `CREDIT_UPDATE`/`FLOWCTRL_UPDATE` ids | TX-side flow control in wal; RX credits are the free-ring entries themselves | free-ring entries are the RX credit; no separate credit message |
| transfer size limits | 64 scatter x 512-B block, packet max 1544 [OH `oal/oal_sdio_comm.h:122-156`] | netbuf sizes from spec cfg (`LARGE_NETBUF_SIZE` etc.) | norm 0x63c / small 0xb0 DMA area |

## T5. RX interrupt / poll model

| property | Hi3881V100 | Hi1105 | ours |
| --- | --- | --- | --- |
| top half | SDIO function-1 sources: DREADY / RERROR / MFARM, enable mask 0x07 at func-1 reg 0x09, armed clear-status->enable->state [phase45 doc, OH `oal/oal_sdio_host.c:1079-1127`] | IRQ dispatcher -> `hal_irq_host_rx_complete_isr` | none (we poll) |
| RX completion | data-ready ISR reads xfer count, releases bus, reads scatter list | drain completion ring into a ping-pong MPDU queue, then schedule bottom half [HM `host_hal_irq.c:294-319`] | `dp_rx_drain` from sysfs (poll) |
| bottom half | HCC RX worker/flow module | NAPI poll with dynamic weight; `netif_receive_skb`/GRO [HM `wifi/hmac/rx/hmac_napi.c:84-140`] | not implemented |
| free-ring-empty | n/a | dedicated ISR flags "device consumed all free entries" -> schedule refill [HM `host_hal_irq.c:218-237`] | effectively covered by our refill; no separate signal used |

No sibling has an "enable RX" register or message beyond ring programming + fill + unmask:
this confirms the rx-static lane's conclusion for hi5622 (the 14-write program + free-ring fill
is the whole device enable).

## T6. 802.11 -> netdev + EAPOL

| step | Hi3881V100 | our vendor (local, 2.5.24) | ours |
| --- | --- | --- | --- |
| per-frame RX info | 48-B class `dmac_rx_ctl_stru` copied to `hmac_rx_ctl_stru`: amsdu_enable, buff_nums, user indices, mac_header_len, vap id, channel, is_beacon, frame_len, rssi [OH `hcc/hmac_hcc_adapt.c:35-51`] | 36-B device header at buffer+4 (our decode); pre-decoded `hmac_rx_ctl_stru` | 36-B header decode (committed) |
| user check | AP data path requires the TA user to exist/allocated: drop (+optional deauth) when user null or not associated [OH `mac/hmac/hmac_rx_data.c:790-846,1001-1056`] | same class (our sta-ops decode) | station table check in data path (sta-ops) |
| reorder/BA | BA/reorder filter before LAN delivery [OH `hmac_rx_data.c:790-846`] | present (host dpe) | not implemented |
| 802.11->802.3 | `hmac_rx_lan_frame_classify` -> defrag -> pull MAC header -> `hmac_rx_transmit_msdu_to_lan` -> `netif_rx_ni` [OH `hmac_rx_data.c:598-711,855-895,586-592`] | `hmac_rx_frame_80211_to_eth` in our module disassembly | data-tx/sta-ops EAPOL hook delivers payload (partial) |
| EAPOL | EAPOL is a normal data frame; type 6 in the log encoding, VO queue on TX, PTK detection flag [OH `hmac_rx_data.c:630-636`, `hmac_tx_data.c:286-292`] | same (data plane) | data-tx msg1/msg3 TX + eapol_rx drain hook |

## T7. AP/STA management

| topic | Hi3881V100 | our vendor (local) | ours |
| --- | --- | --- | --- |
| mgmt RX transport | HCC control event CRX -> `hmac_rx_process_mgmt_event` -> FSM [OH `mac/hmac/hmac_mgmt_classifier.c:252-300`; `mac/hmac/hmac_event.c:841-907`] | D2H cfg-event **id 0x0d (13)** -> `hmac_rx_process_mgmt_event` -> FSM (table in .data; see REPORT §5) | D2H g1 id13 decode (ap-mgmt lane) |
| AP SME | **in the host MAC**: AP-UP handler dispatches auth/deauth/assoc/disassoc/action/probe-req [OH `mac/hmac/hmac_mgmt_ap.c:1890-1956`]; responses encapsulated in host [OH `hmac_encap_frame_ap.c:214,912`] | same layer (host hmac), then notifies cfg80211 | userspace hostapd via cfg80211 |
| mgmt TX | host encap -> `hmac_tx_mgmt_send_event` (HCC control event) [OH `mac/hmac/hmac_mgmt_bss_comm.c:1453`]; cfg80211 `.mgmt_tx` op [OH `wal/wal_cfg80211.c:4681,4859`, status `:4932`] | WAL cfg-event cmd 0x01F2 + frame by device-visible DRAM address (ap-mgmt lane) | patch exists (ap-mgmt) |
| station add order | test/add exists -> `hmac_user_add()` (CTX ADD_USER event) -> `mac_vap_add_assoc_user`; then assoc response; then state sync [OH `mac/hmac/hmac_user.c:598-693,647-704` and AP assoc path around `hmac_mgmt_ap.c:1456`] | same chain decoded in sta-ops lane (`g0 id7 user_add` then `0x07d2`) | sta-ops patch; proposal P2 hardens order |
| per-VAP RX filter | explicit 32-bit DMAC filter event on VAP state change: AP UP `0xF7B9FAEA`, AP INIT `0xF7B9FEFA`, monitor `0x1`; `support_any` clears BIT4 (broadcast probe req) [OH `mac/hmac/hmac_rx_filter.c:184-222,325-381,392-452`] | **no equivalent symbol/string/constant** in 2.5.24 or 2.4.15 [V] - different firmware split, do not invent one | none (correct for our chip until proven otherwise) |
| EAPOL vs mgmt | EAPOL = data (netdev), mgmt = CRX events | same | same |

## T8. Status / error handling conventions worth copying

| convention | source | proposal |
| --- | --- | --- |
| read pointer, then ring memory, with a memory barrier / transient-tolerance | Hi1105 wait_valid | P1 bounded wait on zero entry |
| clear consumed completion entries | Hi1105 `hal_ring_get_entries` | P1 clear-after-read |
| distinguish "no work" from "corruption": wait is not an error | Hi1105 | P1 do not close lane on transient zero |
| free-ring-empty is a normal flow-control event | Hi1105 `hal_irq_host_rx_ring_empty_isr` | P3 log classification (idle vs prefetch-only vs progress) |
| drop-oldest above high waterline instead of wedging | Hi3881 HCC RX | keep our fail-closed lane; document |

## T9. Sibling coverage of our open questions

| our open question (from the committed lanes) | sibling answer | verdict |
| --- | --- | --- |
| "is the 14-write program the whole RX enable, or is there an enable/credit message?" | Hi1105: yes, ring reset/program/fill/publish only; no enable register/message.  Hi3881 has no rings (event-based) | **cross-validated: no missing enable** |
| "why no completions while a laptop scans?" | mgmt is not ring traffic (S1 CRX, S2 separate event, S3 dispatch id 13) | **architecture answer** |
| "what makes the MAC write frames?" | free-ring producer publish + armed per-ring interrupt/unmask; consumer keeps refilling | **already implemented** |
| "sub-MSDU chain accounting" | Hi1105 `hal_rx_get_sub_msdu` resolves chained devVA through the alloc list [HM `host_hal_dscr.c:442-464`]; chain entries are also ring entries | still [U] for our chip; our refusal is correct for now |
| "AP association needs add_station" | Hi3881 user_add -> assoc response (order) | **validated** |
| "per-VAP RX filter missing?" | exists in Hi3881, absent in our vendor | **do not port; chip-specific** |
