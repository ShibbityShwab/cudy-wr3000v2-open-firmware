# Sibling-GPL static analysis: Hi3881V100 / Hi1105 RX + flow-control + AP/STA paths, compared with our hi5622v100 model

**Lane:** wifi-sibling-gpl (shared task task-151).  **Host-only**: web research + static
analysis; no bench contact, no device access, no image.  **Written:** 2026-11-25.
**Base context:** our decoded model from the committed lanes (wifi-rx-static, wifi-ap-mgmt,
wifi-sta-ops, wifi-data-tx, wifi-beacon-static):

* MAC RX live bank **CA 0x40050000** (+0x00 status, +0x08 norm free rptr, +0x0c small free rptr,
  +0x10 completion wptr); config block CA 0x40040000 +0x38/+0x3c free-ring producers,
  +0x40 completion consumer, +0x48 mask 0xfffffcc1;
* norm free ring 256 x 4 B, small free ring 512 x 4 B, completion ring 4095 x 4 B;
* completion entry = device VA of buffer+4; 36-B device header at buffer+4 (len +0x1a, next
  +0x20, MPDU at +0x24); packed 15-bit index + phase bit;
* TX path = MSDU ring + TXQ-table publish (hardware-proven); AP start = 5-message vendor order
  (vap_add -> ap_chan 0x0124 -> ap_ssid -> ap_cfg -> ap_vap_start, AP-ENABLED);
* RX: `dp_rx_arm` proves the MAC consumed published free entries (status 0x182, norm/small
  rptr=0x20 after AP-first ordering) but **captures remain zero** while a laptop scans ch6.

Evidence markers: **[V]** verified in a fetched public source (raw path + lines given);
**[V*local]** decoded from the local vendor module (2.4.15 / 2.5.24, not committed);
**[V\*]** hardware-observed in a prior attended lane (quoted from the committed reports);
**[I]** inference; **[U]** unknown.  GPLv2 quotes are study-only; nothing is copied into our tree.

---

## 0. TL;DR - the ten answers

1. **The data rings are not where management frames come from.**  Hi3881V100's GPL stack has an
   explicit two-family RX event split: `FRW_EVENT_TYPE_WLAN_CRX` (management/control frames,
   scan results, peer reports) handled by `hmac_rx_process_mgmt_event` -> AP/STA FSM, and
   `FRW_EVENT_TYPE_WLAN_DRX` (data frames) handled by the data path [V OH
   `mac/hmac/hmac_event.c:841-907`, `mac/hmac/hmac_mgmt_classifier.c:252-300`].  In **our own
   vendor 2.5.24 module** the D2H cfg-event dispatch table pairs **event id 0x0d (13)** with
   `hmac_rx_process_mgmt_event`, and that handler is the *only* caller of the AP/STA FSM
   dispatch in the RX direction [V*local: .data table entries at offsets 0x5e4/0x5e8;
   cross-checked by ap-mgmt lane's g1 id13 decode].  The vendor's ring-entry processing
   (`hmac_rx_ring_entry_proc`) never dispatches management frames [V*local].  Therefore
   "laptop scans -> zero completion-ring entries" is **expected architecture, not an RX defect**;
   the scan probes must be observed on the **D2H management event path** (our `mgmt_rx` hook),
   and ring captures require **data** frames (EAPOL msg2/4, ARP, DHCP, ping from/to an associated
   client).  This is the single most important correction this lane contributes.
2. **Sibling HALs cross-validate "the 14-write program is the whole enable".**  The Hi1105 RX
   start sequence is: optional ring-reset assert (mask 0x30) -> program base lsb/msb + size +
   zero all pointers -> deassert reset -> fill the free ring (alloc/DMA-map/push devVA) ->
   publish the producer wptr [V HM `wifi/dpe/hal/host_hal_dscr.c:839-888`,
   `host_hal_dscr.c:508-536`].  There is **no separate RX-enable register, bit or H2D message** in
   either sibling [V].  What the host owes the hardware is the *consumer/refill* half - exactly
   what our `dp_rx_drain` implements.
3. **The two ring depths that we decoded match Hi1105 exactly** (small free ring 512 entries,
   completion ring 4095 entries) and the packed pointer format matches (15 index bits + wrap/phase
   bit 15) [V HM `host_hal_dscr.h:44-62`, `host_hal_ring.h:116-143`].  Hi1105 uses 8-B entries
   (64-bit devVA) and a 4095-entry normal ring vs our 4-B / 256; the rest of the design line is the
   same.  This is independent cross-evidence that our ring model is right.
4. **Two sibling-derived hardening changes are worth taking into our RX consumer** (P1 in
   [PATCH-PROPOSAL.md](PATCH-PROPOSAL.md), host-tested): a bounded **completion-entry validity
   wait** - Hi1105 spins until an entry is non-zero (<=1000 iterations) because the producer
   pointer can become visible before the entry payload is committed [V
   `host_hal_ring.c:406-463,474-524`] - and **clear-after-read** of consumed completion entries
   [V `host_hal_ring.c:474-524`].  Our current drain treats a zero/invalid entry as corruption
   and closes the lane; a transient could therefore masquerade as a fatal error.
5. **RX flow control is the free-ring credit itself.**  Both siblings keep the device fed by
   republishing free-ring entries and treat "device consumed all free entries" as a normal
   refill event: Hi1105 has a dedicated free-ring-empty ISR [V `host_hal_irq.c:218-237`], Hi3881
   has an HCC RX high-waterline drop-oldest policy plus SDIO credit words [V
   `hcc/hcc_host.c:1121-1131`, `oal/oal_sdio_comm.h:111-120`].  Our prefetch of 0x20 entries is
   the same normal number the vendor stock dump shows [V*]; refill works off live-rptr deltas
   (correct pattern), so flow control is not the current blocker.
6. **Management TX is one host->firmware event in both worlds** (Hi3881: `hmac_tx_mgmt_send_event`
   control event [V `mac/hmac/hmac_mgmt_bss_comm.c:1453`]; our vendor: WAL cfg-event 0x01F2 with
   the frame by DMA address, decoded by the ap-mgmt lane) - hostapd's auth/assoc responses travel
   that path, and the vendor AP SME's own response encapsulation is host code [V
   `mac/hmac/hmac_encap_frame_ap.c:214,912`].
7. **AP association ordering is confirmed by the GPL sibling**: the AP must add the client's
   station/user context **before** the assoc response and then sync the associated state -
   `hmac_user_add()` posts the CTX ADD_USER event, then the response is encapsulated, then the
   user/asoc state is synchronised [V `mac/hmac/hmac_user.c:598-693,647-704`,
   `mac/hmac/hmac_mgmt_ap.c:1456`].  This matches the sta-ops lane's decoded vendor chain
   (`g0 id7 user_add` -> assoc response -> `0x07d2 asoc-state sync`) and supports P2: a
   fail-closed `.mgmt_tx` guard that refuses an assoc/reassoc response for a peer with no station
   entry, instead of silently letting the firmware drop it.
8. **Per-VAP RX filtering is a sibling-specific mechanism that our chip does not have.**  Hi3881
   sends an explicit 32-bit DMAC RX filter on VAP state transitions (AP UP 0xF7B9FAEA etc., with
   the `support_any` BIT4 exception for broadcast probe requests) [V
   `mac/hmac/hmac_rx_filter.c:184-222,436-437,392-452`].  Our vendor 2.5.24 module has **no
   equivalent symbol, string or constant** [V*local]; do not invent one for hi5622.
9. **EAPOL is data everywhere**: Hi3881 delivers it as a normal data frame to the netdev where
   hostapd/wpa_supplicant picks it up (it is only *classified* as type 6 in a log) [V
   `mac/hmac/hmac_rx_data.c:630-636`; TX-side EAPOL/PTK flags `hmac_tx_data.c:286-292`]; our
   vendor does the same on the data path.  So EAPOL RX rides on the same data-ring drain that is
   currently empty - another reason full association (or any associated data source) is required
   before expecting ring captures.
11. **Follow-up (Lead request): the mgmt-TX device carrier is now decoded and
    implemented** - raw HCC group 0 id 6 control message (48-byte device TX
    control + inline frame), completion via the DMAC->HMAC config-syn event
    0x01F3; a corrected patch (5 files, applies to the final association tree,
    builds with 0 warnings) and a 41/41 host test are in
    [MGMT-CARRIER-CORRECTION.md](MGMT-CARRIER-CORRECTION.md).  The
    public-research2 "SET_RX_FILTER" hypothesis is refuted for luofu by static
    evidence (no sender exists in either vendor module).
12. **No public source documents our exact chip** (no hi5622/luofu register map); the sibling
    value is architectural.  The concrete deliverables are: this report + TABLES.md, a
    host-tested RX refinement proposal (P1/P3), an association-ordering guard proposal (P2), and
    a bench ladder that tests the *right* path (D2H mgmt events) instead of the data ring.

> **Lane follow-up (Lead request, 2026-11-25):** the management-TX carrier was
> corrected after this report's first version.  The device-facing message is a
> **raw HCC group 0 / id 6 control message** (frame inline after a 48-byte
> device TX-control block; completion = DMAC->HMAC config-syn event 0x01F3),
> **not** the WAL cfg-event 0x01F2 (which is the host-internal WAL->HMAC
> request).  Full decode, corrected builder, compiled patch, host test and the
> new fire recipe are in [MGMT-CARRIER-CORRECTION.md](MGMT-CARRIER-CORRECTION.md).
> The same follow-up finds the luofu **SET_RX_FILTER message does not exist**
> (an evidence-backed negative) and confirms the g1 id13 management-RX decode.
> The RX-analysis sections 0-11 below remain valid; read item 11 of the TL;DR
> with the correction file.

---

## 1. Scope, sources, method, licensing

### 1.1 Sources (fetched with git clone; read locally; never committed)

| tag | repository / pin | licence | what we read |
| --- | --- | --- | --- |
| **S1** | `openharmony/device_soc_hisilicon` @ `7c1e2d5b35c090009bbe195f09337701c60af032` (master), path `common/platform/wifi/hi3881v100/driver/` (250 files, 13 MB) | **GPLv2** (repo LICENSE section (2) names `common/platform/wifi/hi3881v100`; per-file GPLv2 header) | `hcc/`, `mac/common`, `mac/hmac`, `oal/`, `wal/`, `frw/` |
| **S2** | `5-Super-Rookie-5/hongmengkernel` @ `297b33b07b56adb3fa53baf618fdbc16ad9fd447` (main), path `kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/` (wifi 17 MB, platform 6.6 MB) | **no open licence** (no LICENSE/COPYING in repo; files say "All rights reserved") - structure reference only | `platform/oal/pcie/`, `platform/oal/oal_hcc_bus.c`, `wifi/dpe/hal`, `wifi/dpe/resource`, `wifi/hmac/rx`, `wifi/wal` |
| **S3** | local vendor host modules hi5622v100 2.4.15 / 2.5.24 + relocatable disassembly (local evidence; never committed) | vendor binary, local only | symbol/reloc/instruction probes used for [V*local] claims |
| **S4** | our committed lane docs | project | base model, cited inline |

Raw URL prefixes used in citations:
`OH=` `https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/`;
`HM=` `https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/main/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/`.

### 1.2 Method

1. sparse git clones of S1/S2 to `/tmp` (working trees only; nothing linked into the repo);
2. full-file reads of the RX/mgmt/flow-control files + symbol index (grep) on the whole trees;
3. an independent cross-check of the same patterns in our own vendor module S3 via
   relocatable-module disassembly (function names, call graph, .data dispatch tables);
4. every numeric/structural claim recorded in TABLES.md with a raw path + line range;
5. `tools/verify_quotes.py` re-fetches the cited raw files and re-checks the quoted snippets at
   the cited lines (the only "test" that can be done for a web-source analysis);
6. the proposed RX model + association guard is implemented as original clean-room host code
   (`tools/rx_model_selftest.c`) and tested (see §7).

### 1.3 Licensing / clean-room statement

* S1 is GPLv2: quoting with attribution is permitted; we still **do not copy the source** into
  our tree, only derived facts, because our driver is an independent clean-room implementation and
  we do not want GPLv2 vendor code provenance mixed in.
* S2 is **not open-licenced**: it may be read for architecture vocabulary, but nothing from it can
  be reused, and none of its constants are carried into our code as code.  This also means no
  upstream/driver-similarity argument may cite it as a licence-compatible origin.
* Our deliverables contain no vendor binary, no firmware byte, no calibration value, no per-unit
  identity; the only "vendor" references are public source paths with line numbers.

---

## 2. S1 - Hi3881V100 GPL stack: architecture and the RX data path

### 2.1 Layer split (identical to our vendor's hcc/hmac/wal/oal/frw layout)

* `hcc/` - transport service: framing, message-id registry, per-queue flow control, TX/RX
  aggregation.  HCC header is a 4-B packed bitfield inside a **64-B total header** plus a
  routing "extend" header [V `hcc/hcc_comm.h:138-162`].
* `mac/common/` - MAC objects: buffers, VAPs, users, frames/IE parsing.
* `mac/hmac/` - host MAC: SME (AP/STA state machines), RX/TX data path, 11i/keys, scan,
  block-ack, UAPSD, WoW.
* `wal/` - cfg80211/netdev glue and ioctls.
* `oal/` - OS abstraction + SDIO bus/transport.
* `frw/` - event framework (event types: `HIGH_PRIO, HOST_CRX, HOST_DRX, HOST_CTX,
  DMAC_TO_HMAC_CFG, WLAN_CRX, WLAN_DRX, WLAN_CTX, WLAN_DTX, WLAN_TX_COMP, TBTT, TIMEOUT,
  DMAC_MISC, HCC`).

This is the exact same architecture as the hi5622v100 modules we reverse (2,023 `hmac_*` symbols
in the 2.5.24 module alone), so function-level behaviour transfers as *architecture*, not as code.

### 2.2 SDIO transport RX

The SDIO path is **event/FIFO-based, not DMA-ring-based** (the important difference to our PCIe
chip, and the reason Hi1105 is the better ring reference):

* interrupt classes: `DREADY` (data ready), `RERROR`, `MFARM` (message from ARM), enabled by
  writing mask `0x07` to function-1 register `0x09` in the order clear-status ->
  enable-sources -> open-state -> claim-IRQ [phase45 report, V `oal/oal_sdio_host.c:1079-1127`];
* data-ready ISR: read transfer count from `0x0c`, validate length, release the SDIO host, run
  the registered RX callback (which builds a netbuf list and bulk-reads the FIFO), re-claim host
  [V `oal/oal_sdio_host.c:257-286`];
* scatter list: up to 64 device->host buffers of 512-B blocks, packet max 1544 [V
  `oal/oal_sdio_comm.h:122-156`]; a reserved buffer drains oversized transfers [V
  `oal_sdio_host.c:551-608`];
* flow-control credits: a 32-bit `credit_info` word at extend-register `0x3c` packs
  short/large/free-packet counts plus a sequence byte [V `oal/oal_sdio_comm.h:111-120`,
  update at `oal_sdio_host.c:445-473`]; device->host messages `FLOWCTRL_UPDATE/OFF/ON` and
  `CREDIT_UPDATE` carry these events [V `oal_sdio_comm.h:49-75`].

### 2.3 HCC RX: the data-plane event and the per-frame RX control block

`sdio_transfer_rx_handler()` builds the RX netbuf list, transfers it, then
`hcc_rx_netbuf_list_handler()` walks up to `HISDIO_DEV2HOST_SCATT_MAX` (64) aggregated HCC
frames and applies a **high-waterline drop-oldest** policy when the RX queue is over its limit
[V `hcc/hcc_host.c:1088-1148,1150-1177`]:

```c
scatt_count = oal_netbuf_list_len(netbuf_head);
if (scatt_count > HISDIO_DEV2HOST_SCATT_MAX) { ... scatt_count = 0; }
...
if (hcc_rx->queues[DATA_LO_QUEUE].flow_ctrl.enable == HI_TRUE) {
    if (oal_netbuf_list_len(&...data_queue) > ...high_waterline) {
        hcc_transfer_rx_handler_replace(hcc_handler, netbuf);   /* drop the oldest */
    } ...
}
```

The HCC data event payload is a netbuf; the driver reconstructs the per-frame RX control from a
48-B `dmac_rx_ctl_stru` prepended by the device and copies it into `hmac_rx_ctl_stru` [V
`hcc/hmac_hcc_adapt.c:35-51,53-109`].  The decoded fields are the same vocabulary as our 36-B
device header (and the ap-mgmt lane's 48-B D2H mgmt info):

```c
hmac_rx_ctl->amsdu_enable / buff_nums / da_user_idx / is_first_buffer /
is_fragmented / mac_header_len / ta_user_idx / vap_id / msdu_in_buffer /
us_frame_len / mac_vap_id / channel_number / is_beacon / rssi_dbm
```

There are two event families on the HCC side, and this is the architectural key for our RX
question [V `mac/hmac/hmac_event.c:841-907`]:

* `FRW_EVENT_TYPE_WLAN_CRX` (control/mgmt): `DMAC_WLAN_CRX_RX`, `NEW_PEER_REPORT`,
  `SCAN_RESULT`, `PROMIS` -> `g_ast_hmac_wlan_crx_event_sub_table`;
* `FRW_EVENT_TYPE_WLAN_DRX` (data): `RX_AP` / `RX_STA` -> `g_ast_hmac_wlan_drx_event_sub_table`.

Management frames are not data: `hmac_rx_process_mgmt_event()` takes a CRX event netbuf, gets
the VAP, finds the frame body and feeds it to the state machine via
`hmac_rx_vap_state_proc()` [V `hmac_mgmt_classifier.c:252-300`].  In the AP `UP` state that
dispatches to `hmac_ap_up_rx_mgmt()`, which handles auth / deauth / assoc/reassoc req / disassoc
/ action / **probe req** [V `mac/hmac/hmac_mgmt_ap.c:1890-1956`].  This is the sibling proof of
the split: management frames are processed by host SME code fed from a control event, never from
the data-frame path.

### 2.4 Data path (DRX) and 802.11 -> netdev + EAPOL

`hmac_rx_process_data_ap()` (and the STA twin) takes the DRX event, moves the netbufs into a
list, applies the reorder/BA filter, then `hmac_rx_lan_frame()` classifies each MPDU [V
`hmac_rx_data.c:956-999,855-895`].  The classification requires the transmitting **user to
exist and to be allocated**; a missing user drops the frame (with an optional deauth) [V
`hmac_rx_data.c:790-846,1001-1056`].  Then the MPDU is defragmented, the MAC header is pulled,
the SA/DA are extracted and the MSDU is pushed to the LAN via `oal_netif_rx_ni(netbuf)` [V
`hmac_rx_data.c:598-711,560-596`].  EAPOL is just a data frame; it is only classified/logged
as type 6 [V `hmac_rx_data.c:630-636`], and the TX side marks EAPOL frames for the VO queue and
sets the PTK-4/4 detector flag [V `hmac_tx_data.c:286-292`].

Implication for us: ring captures require (a) data frames on air, (b) a firmware user context
matching the transmitter for the AP data path (in the vendor's host-SME design), and (c) the
device forwarding data into the ring.  A laptop that is merely scanning never creates (a)/(b).

### 2.5 AP/STA management paths

* **AP SME is host code**: the FSM dispatch table (built by `hmac_fsm_init`) maps (state, event)
  to handlers; the AP UP entry for management RX is `hmac_ap_up_rx_mgmt`, and the AP-UP misc
  functions handle probe/association housekeeping [V `mac/hmac/hmac_mgmt_ap.c:1890-1956`; our
  vendor has the same FSM table with the same symbol names - cross-check S3].
* **Responses are encapsulated in the host**: `hmac_encap_auth_rsp()`,
  `hmac_mgmt_encap_asoc_rsp_ap()` etc. build the 802.11 response and `hmac_tx_mgmt_send_event()`
  posts it as an HCC control event [V `hmac_encap_frame_ap.c:214,912`,
  `hmac_mgmt_bss_comm.c:1453`].  The cfg80211 `.mgmt_tx` op exists for the normal
  hostapd/wpa_supplicant path as well [V `wal/wal_cfg80211.c:4681,4859,4932`].  In our vendor
  the equivalent is the WAL 0x01F2 cfg-event decoded by ap-mgmt.
* **Station/user lifecycle order**: `hmac_user_add()` validates, allocates, initialises, sends
  the DMAC **ADD_USER** context event, adds the user to the MAC VAP, then bumps the associated
  user count [V `hmac_user.c:647-704`, event at `598-645`].  On the AP association path the
  user is added first and the assoc response encapsulated after [V `hmac_mgmt_ap.c:1456`
  context], then the user's associated state is synchronised to the device.  **Our sta-ops patch
  implements exactly this order** (g0 id7 user_add -> response -> 0x07d2 asoc-state); P2 below
  makes the order enforced instead of assumed.
* **Per-VAP RX filter event** (a Hi3881-specific mechanism; see §3 and T7): the host sends a
  32-bit receive-filter value to the DMAC on VAP state changes [V `hmac_rx_filter.c:392-452`],
  with table values AP UP 0xF7B9FAEA, AP INIT 0xF7B9FEFA, monitor 0x1 and a
  `support_any` BIT4 exception for broadcast probe requests [V `hmac_rx_filter.c:184-222,
  436-437`].  Our vendor module has no such message; do **not** invent one for hi5622 [V*local].

---

## 3. S1 - what transfers to us, and what does not

Transfers (architecture-level):

1. The management/data RX split and the rule that management frames are control events.
2. mgmt-tx as a single host->device control event carrying the whole frame (our vendor instead
   carries a device-visible DRAM address, which the ap-mgmt lane solved with dma_alloc_coherent).
3. user/station context before assoc response, then associated-state sync.
4. RX flow-control = keep the device fed; drop-oldest / refill on pressure rather than wedging.
5. EAPOL = data frame to netdev; PTK detection only as a hint, never as authorization.

Does not transfer:

1. SDIO function registers, the 64-B HCC header, credit-word layout, or the SDIO interrupt enable
   (different transport; our chip is PCIe with rings).
2. The per-VAP DMAC RX filter values/message (chip/firmware specific; absent from our vendor).
3. EAPOL-on-DRX assumptions that depend on the firmware's own user table - ours is hostapd-driven,
   so the station table check has to be ours (sta-ops already does it).

---

## 4. S2 - Hi1105 PCIe: HCC-over-PCIe, HAL rings, ISR/NAPI (the closest ring sibling)

### 4.1 HCC over PCIe (firmware message ring)

The Hi1105 platform layer implements an HCC bus over SDIO/PCIe/USB [V `platform/oal/oal_hcc_bus.c`:
`hcc_message_register()` at line 991, `hcc_message_unregister()` at 1027, send via
`hcc_bus_send_message()` at 1094/1561] and a PCIe firmware-message ring
[V `platform/oal/pcie/pcie_firmware_msg.c:41-118`]:

* two shared-memory `frw_ringbuf_t` objects (H2D and D2H), each with a **control block**
  (`rd`/`wr`/size) and a **data block**;
* host->device send: write the payload into the H2D data block, re-read the firmware's `rd`,
  reinit the completion, publish `wr`, then wait for the D2H interrupt
  [V `pcie_firmware_msg.c:218-243`];
* device->host receive: wait for the completion, update the local copy of the firmware's `wr`,
  read the D2H data block, publish the D2H `rd` [V `pcie_firmware_msg.c:337-363`];
* the D2H RX ISR only completes a wait object [V `pcie_firmware_msg.c:386-401`].

This is the structural analogue of our mailbox + window + ETE rings: a small control/ack plane
with a data plane.  It adds no new insight for our RX problem, but it is a useful public
confirmation that the vendor family's "message" transport is a ring + interrupt wait, not a
per-message doorbell for payload data.

### 4.2 HAL RX rings (the part that maps to our `dp_rx_*`)

The Hi1105 `wifi/dpe/hal` layer is a ring HAL with exactly our abstractions:

* ring types: `HAL_RING_TYPE_FREE_RING` (software writes the write pointer, hardware writes the
  read pointer) and `HAL_RING_TYPE_COMPLETE_RING` (hardware writes the write pointer, software
  writes the read pointer) [V `host_hal_ring.h:135-143`];
* pointer struct: 15 index bits + one wrap/phase bit [V `host_hal_ring.h:116-133`] - the same
  bit-15 convention as our packed index;
* `hal_ring_get_hw2sw()` / `hal_ring_set_sw2hw()`: the **only** pointer direction per ring
  [V `host_hal_ring.c:198-255`];
* pending/consumed arithmetic with explicit full/empty and one-lap cases
  [V `host_hal_ring.c:263-315`];
* completion read: burst-copy entries, **zero them after reading**, advance read pointer
  [V `host_hal_ring.c:356-403,474-524`];
* validity wait: spin until the entry word is non-zero (<=1000 iterations) because the write
  pointer may be seen before the entry payload is committed
  [V `host_hal_ring.c:406-463`];
* free-ring production: allocate skb, DMA map, push the device address into the free ring, bump
  the write pointer, publish [V `host_hal_dscr.c:87-262`, `host_hal_ring.c:631-687`];
* RX start sequence (init + start): optional ring reset (mask 0x30) -> program base/size/zero
  pointers -> optional "ring pointer report host address" mode -> deassert reset -> add buffers
  [V `host_hal_dscr.c:839-888,508-536`];
* geometry: normal free ring 4095 x 8 B, small free ring 512 x 8 B, completion ring 4095 x 8 B,
  entry = device VA of the netbuf, fixed offset 4 in the buffer, 8-byte alignment
  [V `host_hal_dscr.h:44-62`].

The two identical depths (512 / 4095) and the identical pointer format are strong independent
cross-evidence for our ring model (our third ring is 256 because our firmware's normal buffers
are smaller/fewer; the current vendor stock dump also shows norm/small prefetch = 0x20).

### 4.3 ISR / NAPI

* `hal_irq_host_rx_complete_isr`: if the RX queue is not inited, log status/mask and return;
  otherwise mark RX-completion activity, drain the completion ring into a ping-pong MPDU queue,
  and if the queue was empty schedule the bottom half [V `host_hal_irq.c:294-319,256-276`];
* `hal_irq_host_rx_ring_empty_isr`: a **dedicated free-ring-empty interrupt** that schedules a
  refill of consumed free entries [V `host_hal_irq.c:218-237`];
* bottom half = NAPI poll with dynamic weight; it delivers `netif_receive_skb`/GRO, then
  `napi_complete` and re-schedules if the budget was exceeded [V `wifi/hmac/rx/hmac_napi.c:84-140`];
* the poll model is a *completion-drain plus a deferred processing queue*, exactly the shape of
  our vendor's `hal_device_rx_mpdu_que_pop` -> `hmac_rx_complete_proc_finish` ->
  `hmac_rx_process_data_event` chain [V*local 2.5.24 disassembly].

---

## 5. Cross-check in our own vendor module (S3): the management RX dispatch id

The ap-mgmt lane decoded the D2H management-frame carrier as HCC group 1 / id 13.  This lane
independently recovered the **handler table** in the vendor 2.5.24 module's `.data` section
(relocatable module; table entries are `{handler pointer; ...; event id}` triples).  The table
contains, in id order:

```
... handler hmac_ch_status_info_syn_event   id 0x0b
... handler hmac_rx_process_mgmt_event      id 0x0d
... handler hmac_mgmt_rx_delba_event        id 0x0e
... handler hmac_scan_proc_scanned_bss      id 0x0f
... handler hmac_scan_proc_scan_comp_event  id 0x10
... handler hmac_scan_process_chan_result_event id 0x11
```

[V*local 2.5.24: R_ARM_ABS32 relocation entries at data offsets 0x5dc/0x5e8/0x5f4/0x600/0x60c/
0x618 with the id words at the following 4-byte slots]

So **event id 13 -> `hmac_rx_process_mgmt_event`** in our own driver, matching the ap-mgmt
lane's g1 id13 exactly.  Cross-check of the call graph: the only callers of
`hmac_fsm_call_func_ap`/`hmac_fsm_call_func_sta` from the RX direction are inside
`hmac_rx_process_mgmt_event_proc` [V*local], i.e. mgmt frames enter the AP/STA FSM only through
this event path.  The ring path is separate: `hmac_rx_complete_proc_finish` drains the
completion ring and feeds `hmac_rx_ring_entry_proc`, which classifies crypto/DA and calls
`hmac_rx_frame_80211_to_eth` for data frames [V*local]; it never calls the FSM or the mgmt
handlers.  That is the concrete basis for the §6 conclusion.

Additional local cross-checks:
* our vendor's RX init function (`shuangta_rx_host_init_dscr_queue`) programs the ring
  address/size and buffer length, fills both free rings and publishes the producers - **no reset
  mask write and no enable bit/message** [V*local, matching the rx-static lane's conclusion];
* our vendor's data-event handler (`hmac_rx_process_data_event`) pops the MPDU queue, processes
  completions, and refills free rings when the stall flags are set [V*local], the same
  drain/refill shape as Hi1105.

---

## 6. What our RX path is missing / why captures are zero: ranked hypotheses

**H1 (architecture, high confidence): mgmt frames are not on the data ring.**
The completion ring receives **data** MPDUs only; management frames (probe req/resp, auth, assoc)
are delivered as D2H cfg-events (event id 13 -> `hmac_rx_process_mgmt_event`) and go to the
cfg80211/hostapd path.  A scanning laptop produces only management (and maybe some probe
request/response) traffic, so **zero completion entries while scanning is the expected result**.
*Check:* enable `mgmt_rx=1` and watch the D2H id-13 counter/log while the laptop scans; expect
events, not ring completions.  *Consequence:* the "RX zero captures" finding must be re-scoped:
ring captures need an associated client generating data (EAPOL msg2/4, ARP, DHCP, ping), which is
exactly the association chain we are trying to unblock.

**H2 (no data source yet, high confidence, corollary):** even with H1 fixed, the ring stays empty
until a client is associated AND the device forwards data into the ring.  Pre-association the only
frames are mgmt/control.  The data-tx/EAPOL capability cannot be validated by a ring drain until a
client associates; the EAPOL msg1 TX ladder rung is the correct first data-direction proof.

**H3 (consumer robustness, medium): completion-entry visibility ordering.**  Our drain closes the
lane if the first entry is zero/invalid.  The Hi1105 HAL explicitly tolerates the
producer-visible-before-entry-commit window with a bounded spin and clears entries after reading
[V §4.2].  If our MAC ever exposes the completion wptr before the entry payload (or a stale zero
entry sits in the ring after an AP restart / lane re-arm), our code would report corruption and
close the lane without evidence.  *Proposal P1* (host-tested).

**H4 (flow control, low-medium): free-ring starvation.**  Both siblings treat refill as a
first-class event (free-ring-empty ISR; high-waterline policy).  Our drain refills only inside a
drain action (bounded, after validation).  If a future workload outruns it, the MAC will stop at
the prefetch watermark and RX will stall silently.  *Proposal P3:* classify/report this state
(idle vs prefetch-only vs progress) instead of only "no new completion".  Right now the observed
prefetch=0x20 with zero completions is *not* starvation - the device has 256 published entries
and consumed 32.

**H5 (firmware-side filtering, low):**  Hi3881 needs a per-VAP RX filter event; our vendor module
has no equivalent [V*local], so the hi5622 firmware most likely filters from the VAP/user
configuration it already has.  Do not port the Hi3881 bitmap.  If the D2H mgmt path is also silent
with a known-client association attempt, the next suspect is the VAP/user state in the firmware,
not a missing filter message.

**H6 (RF/air, cannot be evaluated host-only):** the bench reports ~44 dB path loss with no
antennas; a laptop scan transmitting at typical client power should still reach the MAC, but this
must be distinguished from the software hypotheses by the H1 check and by an associated-client
data test.  The sibling analysis cannot decide it.

**H7 (ordering discrepancy, [U]):** the vendor initialises the RX rings once in `hal_device_init`
(before any AP start) and never re-initialises them, while the bench found that AP start after
dp_setup clears the status word (0x02 vs 0x182).  This is an open item: instrument the live bank
after each AP-start message to find which write clears it; the vendor's "init once at probe" order
suggests the vendor AP-start path does not reset the host-interface block.  Not a sibling
question - recorded here because it contradicts a naive "vendor order" assumption.

---

## 7. Concrete patch proposal (details + code in [PATCH-PROPOSAL.md](PATCH-PROPOSAL.md))

### P1 - completion-entry validity wait + clear-after-read (RX, fail-closed)

Files: `hi5622_rx_live.c/.h` (portable), `hi5622_dp_txrx.c` (glue), plus host selftest.
Shape, derived from the Hi1105 HAL (`hal_host_comp_ring_wait_valid`,
`hal_ring_get_entries`):

1. New module param `rx_entry_wait` (0..256, default 32).  When a completion-ring entry is
   zero, re-read the entry with `cpu_relax()` up to `rx_entry_wait` times (memory barrier each
   time) before deciding.
2. If it stays zero: count `rx_spurious_wptr`, do **not** consume it and do **not** close the
   lane; return "no commit" (like the vendor's wait-valid).  If `rx_spurious_wptr` reaches a
   hard cap (default 64) without any progress, close the lane (fail-closed) - a persistent
   producer/entry desync is a real defect, a transient is not.
3. A non-zero but out-of-pool/invalid entry remains a **hard** error and closes the lane (no
   behaviour change; the vendor also treats bad addresses as errors).
4. After a valid entry is consumed, zero that completion-ring slot before publishing the consumer
   index (clear-after-read), so a later lane re-arm/restart cannot read a stale entry.
5. New portable rules are asserted by `tools/rx_model_selftest.c` (strict C99, host build, no
   kernel, no vendor bytes).

### P3 - RX stall classification telemetry (log-only, no behaviour change)

Files: `hi5622_rx_live.c/.h`, `hi5622_dp_txrx.c`.
`dp_rx_drain` with no pending completions currently logs a flat "no new completion".  Replace
with a classified verdict derived from two snapshots (arm vs now):

| class | condition | message |
| --- | --- | --- |
| `IDLE` | cmp wptr and both free rptrs unchanged since arm | "armed, no RX activity (mgmt traffic is expected on the D2H mgmt path, not here)" |
| `PREFETCH_ONLY` | free rptr advanced, cmp wptr never did | "MAC consumer alive (prefetched N free entries); no frames forwarded to host rings" |
| `PROGRESS` | cmp wptr advanced | "completions have been produced; drain keeps up" |
| `SPURIOUS_WPTR` | P1 zero-entry path triggered | "producer visible without a committed entry; waited N times" |

This turns the next bench test into an unambiguous reading of the same sysfs file instead of an
interpretation of "zero".

### P2 - association ordering guard (mgmt/assoc, fail-closed)

File: `hi5622_mgmt.c` (and the station table helper from sta-ops).
Rule: in `.mgmt_tx`, for an 802.11 **assoc/reassoc response** subtype (FC type mgmt, subtype 0x10
/ 0x30 - masked like the vendor), require a matching entry in the clean-room station table; if it
is absent, refuse with `-ENOENT` and log the sibling-derived rule, instead of letting the
firmware drop a response for an unknown user.  Evidence: Hi3881 AP association adds the user
context before the response is encapsulated [V §2.5], and our sta-ops patch already posts the
user_add before hostapd's response; this guard enforces the order and makes a violation visible.
For probe responses/auth responses the guard is not applied (no user context exists yet), so
hostapd's normal AP sequence is not restricted.

> Note: P2 is a *proposal/guard*, not a claimed fix.  The actual association still depends on the
> firmware accepting the mgmt-tx carrier (ap-mgmt ladder M2) and on the D2H mgmt RX delivery
> (ap-mgmt ladder M1); both are [U] on hardware until fired.

---

## 8. Host verification performed by this lane

1. `tools/verify_quotes.py` - fetches every cited raw source file and asserts each quoted line
   segment still exists at the cited line number.  It also checks the repo licence lines used in
   §1.3.  Run: `python3 tools/verify_quotes.py` (network required; prints PASS/FAIL per quote).
   Result in §8 of the final lane run: see the tool output committed beside this report
   (`evidence/verify-quotes.txt`).
2. `tools/rx_model_selftest.c` - strict C99 host test of the P1/P3 model: packed index phase
   advance/wrap, pending/consumed arithmetic including one-lap cases, zero-entry wait policy,
   clear-after-read, spurious-count cap, stall classification, refill budget, the P2 guard
   decision table, and refusal of invalid/nonzero out-of-pool entries.  94 checks, no kernel, no
   vendor data.  Build/run:
   `gcc -std=c99 -Wall -Wextra -O2 -o /tmp/rx_model tools/rx_model_selftest.c && /tmp/rx_model`.
   Result: see `evidence/rx-model-selftest.txt`.
3. No kernel patch was applied to the project build tree by this lane: the proposal is deliberately
   against the *final association tree* interface (`hi5622_rx_live.h`), and applying/building it
   is left to the integration lane that owns the module artifact.  This keeps the parent's pinned
   association artifact untouched.

---

## 9. Bench ladder implication (host-designed, NOT fired by this lane)

This lane must not fire the bench; the useful rows for the Lead / next static-integration lane:

* **L-A (mgmt path identity)**: module with `mgmt_rx=1 mgmt_tx=1`, fresh golden boot, AP start
  first -> dp_setup -> rx_arm.  Laptop (or phone) scans ch6.  **PASS**: D2H g1 id13 events appear
  (ap-mgmt telemetry) while `dp_rx_drain` correctly reports `IDLE`/`PREFETCH_ONLY` - proves H1
  and separates mgmt from data.  **FAIL**: no D2H events either -> H5/H6 (firmware VAP state or
  RF), not a ring bug.
* **L-B (ring data proof)**: after L-A, associate a client (open or WPA2 pre-shared) -> 4-way ->
  DHCP/ping.  Expect completion entries (EAPOL msg2/4, ARP, DHCP, ICMP) and `PROGRESS`.
  This is the real RX-data ladder rung; it cannot pass before association.
* **L-C (P1 robustness)**: with `rx_entry_wait=32`, force a zero-entry transient by reloading
  the module/lane in a controlled way (or by re-arming after a stop) and verify the lane reports
  `SPURIOUS_WPTR`/waits instead of closing; with `rx_entry_wait=0` the old behaviour is
  reproducible (fail-closed), proving the change is gated.
* **L-D (P2 ordering)**: with the guard, hostapd's normal association must reach the assoc
  response only after `add_station`; a negative control (deliberately skip add_station) must
  log/refuse the response.

---

## 10. Open questions / honest limits

* [U] Whether our firmware delivers **probe requests** to the host on the D2H mgmt event path or
  handles them internally (the vendor's host AP SME handles them; our clean-room delegates to
  hostapd).  L-A decides.
* [U] The exact hi5622 D2H id/gid assignment for data events (rings -> host) vs mgmt; only the
  data ring mapping is decoded.  The vendor's `.data` handler table shows data events
  (`hmac_rx_process_data_event`) but their ids were not fully enumerated in this lane.
* [U] Sub-MSDU chaining and A-MSDU remain refused by our drain; the Hi1105 HAL resolves chained
  devVAs through its alloc list, which is the reference if we ever decode our chain accounting.
* [U] The AP-start-resets-MAC ordering (H7) needs live-bank instrumentation; sibling code has no
  equivalent reset so it cannot answer it.
* The Hi1105 tree is not licence-clean for reuse; any later lane that wants to *reuse* its
  mechanisms must find an independent GPLv2/public origin (Hi3881 is GPLv2 but SDIO; the common
  ring/flow-control facts are generic enough to be re-derived).
* No public source documents hi5622v100 / luofu; this lane's value is architectural, not a
  register map.  Do not cite S2 as a licence-compatible driver origin.

---

## 11. Reproduction

```sh
# 1) fetch public siblings (network; not committed)
git clone --depth 1 --filter=blob:none --sparse https://github.com/openharmony/device_soc_hisilicon.git /tmp/sib-hi3881
git -C /tmp/sib-hi3881 sparse-checkout set common/platform/wifi/hi3881v100
git clone --depth 1 --filter=blob:none --sparse https://github.com/5-Super-Rookie-5/hongmengkernel.git /tmp/sib-hi1105
git -C /tmp/sib-hi1105 sparse-checkout set kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi \
    kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform

# 2) re-check every quoted line against the live raw sources
python3 opensource/docs/incoming/wifi-sibling-gpl/tools/verify_quotes.py

# 3) host-test the proposed RX model
gcc -std=c99 -Wall -Wextra -O2 -o /tmp/rx_model \
    opensource/docs/incoming/wifi-sibling-gpl/tools/rx_model_selftest.c
/tmp/rx_model
```

Commit: this lane only, docs/tools under `opensource/docs/incoming/wifi-sibling-gpl/`.
