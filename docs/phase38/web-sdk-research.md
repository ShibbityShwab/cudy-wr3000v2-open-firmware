# Phase 38 - public-source survey: HiSilicon Wi-Fi PCIe mailbox / HCC message service (2026-10-04)

Task `st_01a106dc`. Scope: **static web research only** - no device access, no register writes, no
re-disassembly. Every URL below was fetched by this task; the fetch result is stated per source
(bytes returned and first-line evidence, or the exact failure). Queries were run against a web-search
tool (github / forum / datasheet / vendor angles); the full query list is in the last section.

Established facts taken as given (not re-derived here): boot dialogue = SR post + H2D id-3 announce ->
device D2H bit 6 -> host receive-buffer check -> device-ready bit 2; the firmware H2D id-5 slot is a
`BUG()` trap; the HCC frame is a 12-byte header + payload (+0x0c start, +0x04 u16 total len, +0x06 u16
message id, +0x00 group/type low nibble, +0x01 alloc-state low nibble + resource-group high nibble,
+0x0a the 0x5a5a SR tag); the mailbox block is device CA `0x40039000-0x40039600` with H2D doorbell
`0x400392d4` and ack `0x400392f0`; steady state is ring-only.

## Bottom line

**Nothing public documents this exact device (`hi5622v100` / "luofu"), this exact mailbox register
block, or these exact message ids.** No vendor datasheet, no register map, no firmware source is on
the open web.

What *does* exist is the **same vendor driver architecture, published under GPLv2** for two sibling
chips, and it is directly useful as a vocabulary and structure reference:

- **HiSilicon Hi3881V100 (SDIO)** - full GPLv2 host driver stack `hcc / hmac / wal / oal / frw` in
  OpenHarmony's `device_soc_hisilicon`, including the **HCC frame-header definition** and the
  **complete H2D/D2H mailbox message-id enum**.
- **HiSilicon Hi1105 (PCIe)** - a vendor kernel tree (`hongmengkernel`) with an **HCC-over-PCIe bus
  adaptation** (`oal_hcc_bus.c`) and a **PCIe H2D/D2H firmware-message ring** (`pcie_firmware_msg.c`).

Both are "partial": same layer names and message-namespace family, **different transport and different
register-level mechanism** than this device. Neither exposes `0x40039010 / 0x400392d4 / 0x400392f0`
or the 12-byte header layout.

## Verdict summary

| # | source | URL | fetch status | verdict |
|---|--------|-----|--------------|---------|
| 1 | OpenHarmony `device_soc_hisilicon` hi3881v100 - **HCC frame header** (`hcc_comm.h`) | see §A1 | 200, 9,621 B | **useful** |
| 2 | same - **HCC host layer** (`hcc_host.c`) | see §A2 | 200, 51,018 B | **useful** |
| 3 | same - **H2D/D2H mailbox msg-id enum** (`oal_sdio_comm.h`) | see §A3 | 200, 6,190 B | **useful** |
| 4 | same - FRW event-type enum (`frw_event.h`) | see §A4 | 200, 12,428 B | **partial** |
| 5 | same - hmac/wal/oal/frw file trees + misc headers | §A5 | 200 (listings + files) | **partial** |
| 6 | `hongmengkernel` hi1105 - **HCC bus (SDIO/PCIE/USB)** (`oal_hcc_bus.c`) | see §B1 | 200, 50,778 B | **useful** |
| 7 | same - **PCIe H2D/D2H firmware-message ring** (`pcie_firmware_msg.c`) | see §B2 | 200, 20,365 B | **useful** |
| 8 | same - PCIe register/config headers (`pcie_reg.h`, `pcie_reg_v100.h`) | §B3 | 200, 295 B / 3,838 B | **partial** |
| 9 | same - repo + full `hi11xx/hi1105/{platform,wifi}` tree | §B4 | 200 (listings) | **partial** |
| 10 | `themactep/wifi-hi3881` (Hi3881 driver tree) | see §C1 | 200, 1,709 B + tree | **partial** |
| 11 | `gtxaspec/ws73v100-wifi` (WS73V100 driver tree) | see §C2 | 200 (tree, truncated) | **partial** |
| 12 | HiSilicon official **Hi1105** product page | see §D1 | 200, 1,011 B | **partial** |
| 13 | H105 (Hi1105) WiFi/BT/GNSS module PDF (v-linktech) | see §D2 | 200, 29,356 B raw PDF | **noise** |
| 14 | hi5622v100 catalogue blurb (electronica Shanghai) | see §D3 | 200, 2,599 B | **noise** |
| 15 | WIFI6-SR5370 module page (Youhua) | see §D4 | 200, 719 B | **noise** |
| 16 | OpenIPC issue #1646 - Hi1131 SDIO wifi boot log | see §E1 | 200 (issue + comments API) | **partial** |
| 17 | OpenWrt forum - "Support for Huawei AX3 Pro" (Hi1152) | see §E2 | **0 bytes / no content** | **noise** |
| 18 | DeepWiki page for `hongmengkernel` hi11xx drivers | see §E3 | **JS-only, 66 B** | **noise** |
| 19 | gitcode mirror of `hcc_host.c` | see §F1 | **rate-limited** | **noise** |
| 20 | codeberg / lore.kernel.org / patchwork: Hi1105 PCIe ASPM quirk | see §E4 | **bot-wall / timeout** | **noise** |
| 21 | gitee `device_soc_hisilicon` mirror | see §F2 | **0 bytes** | **noise** |
| 22 | Hi3861/Hi3881 user-guide + HiSTB WiFi dev-guide PDFs (gitee) | see §F3 | **>5 MB / not parsed** | **noise** |
| 23 | aithinker Hi3861LV100 product-brief PDF | see §F4 | header only | **noise** |
| 24 | `0x400392d4 / 0x40039010 / 0x400392f0` exact-address query | see §F5 | results are TI TM4C123 headers | **noise (explicit)** |

---

## A. OpenHarmony `device_soc_hisilicon` - Hi3881V100 (SDIO) stack, GPLv2

Repo root fetched OK: `https://github.com/openharmony/device_soc_hisilicon` (200; README returned,
1,909 B). Driver root:
`.../common/platform/wifi/hi3881v100/driver/`. The directory API listing returned these trees:
`hcc/`, `mac/hmac/`, `wal/`, `oal/`, `frw/`, `oam/` - the same layer split the `hi5622v100` modules use.

### A1. `hcc/hcc_comm.h` - the HCC frame header (USEFUL)
`https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/hcc/hcc_comm.h`
Fetched: 200, 9,621 B, GPLv2 header. **This is the closest public artefact to the device's HCC header.**
It defines, packed:

```c
struct hcc_header {           /* 4 bytes; hcc total header is 64 bytes */
    hi_u16 sub_type: 4;       /* hcc sub type */
    hi_u16 main_type: 3;      /* hcc main type */
    hi_u16 pad_hdr: 6;        /* aligns the hcc tcm hdr to 64 B */
    hi_u16 pad_payload: 2;
    hi_u16 more: 1;           /* aggregation */
    hi_u16 seq: 4;            /* debug seq */
    hi_u16 pay_len: 12;       /* payload length, excludes the extend hdr */
} __OAL_DECLARE_PACKED;
#define HCC_HDR_TOTAL_LEN 64
```

plus the "extend" header that carries the routing nibbles:

```c
struct hcc_extend_hdr {
    frw_event_type_enum_uint8 nest_type;
    hi_u8 nest_sub_type;
    hi_u8 chip_id: 2; hi_u8 device_id: 2; hi_u8 vap_id: 4;
    hi_u8 config_frame;
};
```

and the 8-byte event header `hcc_mac_event_hdr_stru { type, sub_type, chip_id, device_id, vap_id,
reserve, hi_u16 us_length }`, with the note *"us_length = payload length + event header length - 2"*.
Enums: `hcc_action_type_enum { WIFI=0, OAM=1, TEST=2 }`, `wifi_sub_type { CONTROL=0, DATA=1 }`,
`#define HCC_CONFIG_FRAME 0x80`, plus `HCC_FC_*` flow-control flags.

**Relation to the device (stated as a resemblance, not an identity):** the device's 12-byte header also
packs two nibble fields into the first two bytes (+0x00 sub/type nibble, +0x01 alloc/resource nibble)
and carries a 16-bit length - the same family of packed-nibble routing header - but here the header is
4 B of bitfield inside a **64-byte TCM** header, the length is a 12-bit `pay_len` at a different
offset, and there is no `0x5a5a` tag. **Do not treat these layouts as byte-compatible.**

### A2. `hcc/hcc_host.c` - the HCC host layer (USEFUL)
`.../hi3881v100/driver/hcc/hcc_host.c` - fetched 200, 51,018 B, 1,402 lines.
What it contains (`grep`): `hcc_message_register(hcc_handler, msg, cb, data)` (line 1330),
`hcc_flowctrl_deadlock_detect_worker`, `hcc_config_frame`, and the send idiom
`oal_bus_send_msg(handler->bus, H2D_MSG_*)` used with `H2D_MSG_DEVICE_MEM_INFO`, `H2D_MSG_TEST`,
`H2D_MSG_FLOWCTRL_ON`, `H2D_MSG_PM_WLAN_OFF`; device->host notifications set
`D2H_MSG_FLOWCTRL_ON/OFF`.
**Why useful:** it confirms the vendor's H2D model = *a small bitmap-ish "message" posted through the
bus send entry, with the real payload travelling separately*, exactly the shape already recorded for
this device (`pcie_msg_send(chip, id)` writes a pending bit + doorbell; the body rides the SR ring).
The names differ (this is the SDIO bus), but the pattern is identical.

### A3. `oal/oal_sdio_comm.h` - the H2D/D2H mailbox message-id enum (USEFUL)
`.../hi3881v100/driver/oal/oal_sdio_comm.h` - fetched 200, 6,190 B.
It contains the **full public message-id namespace** for this driver family:

```c
/* device -> host */
D2H_MSG_WLAN_READY=0, FLOWCTRL_UPDATE=1, FLOWCTRL_OFF=2, FLOWCTRL_ON=3, WAKEUP_SUCC=4,
ALLOW_SLEEP=5, DISALLOW_SLEEP=6, DEVICE_PANIC=7, POWEROFF_ACK=8, OPEN_BCPU_ACK=9,
CLOSE_BCPU_ACK=10, CREDIT_UPDATE=11, HIGH_PKT_LOSS=12, HALT_BCPU=13, HEARTBEAT=14,
[WOW_WIFI_REDAY=15], HOST_SLEEP_ACK=16, BEFORE_DEV_SLEEP=17, DEV_WKUP=18,
HEART_BEAT_OPEN_ACK=19, HEART_BEAT_CLOSE_ACK=20, D2H_MSG_COUNT=32
/* host -> device */
H2D_MSG_FLOWCTRL_ON=0, DEVICE_INFO_DUMP=1, DEVICE_MEM_DUMP=2, TEST=3, PM_WLAN_OFF=4,
SLEEP_REQ=5, PM_DEBUG=6, RESET_BCPU=7, QUERY_RF_TEMP=8, HCC_SLAVE_THRUPUT_BYPASS=9,
DEVICE_MEM_INFO=10, STOP_SDIO_TEST=11, PM_BCPU_OFF=12, [HALT_BCPU=13], [HOST_SLEEP=16,
HOST_DISSLEEP=17], HEART_BEAT_OPEN=18, HEART_BEAT_CLOSE=19, H2D_MSG_COUNT=32
```

**Relation to the device:** the device's ids (H2D 3 = SR announce, H2D 5 = receive-buffer check,
D2H bit 2 = platform ready, D2H bit 6 = wake host rx) fall **inside the same 32-slot namespace** but
carry **different meanings** than the SDIO enum (here H2D 3 = `TEST`, D2H 2 = `FLOWCTRL_OFF`,
D2H 6 = `DISALLOW_SLEEP`). So: same namespace family, **per-transport assignment - never map 1:1.**

### A4. `frw/frw_event.h` - FRW event-type enum (PARTIAL)
`.../hi3881v100/driver/frw/frw_event.h` - fetched 200, 12,428 B. Defines `frw_event_type_enum`:
`HIGH_PRIO=0, HOST_CRX, HOST_DRX, HOST_CTX, DMAC_TO_HMAC_CFG=4, WLAN_CRX, WLAN_DRX, WLAN_CTX,
WLAN_DTX, WLAN_TX_COMP=9, TBTT, TIMEOUT, DMAC_MISC=12, HCC=13`. It is the traffic-class vocabulary
behind the HCC `nest_type`, useful for reading frame routing, but it is the hi3881's set.

### A5. Other files in the same tree (PARTIAL)
Fetched 200: `hcc/hcc_comm.c` (1,207 B, header guard only), `hcc/hcc_hmac_if.h` (3,445 B),
`hcc/hcc_host.h` (14,498 B), `hcc/hcc_task.h` (1,658 B), `hcc/hcc_hmac.c` (21,883 B),
`oal/oal_sdio_host_if.h` (12,303 B). Full listings retrieved for `hcc/` (12 files), `mac/hmac/`
(50+ files incl. `hmac_main.c`, `hmac_event.c`), `wal/` (30 files incl. `wal_main.c`,
`wal_event_msg.c`, `wal_cfg80211.c`), `oal/` (40 files), `frw/` (10 files). These are the same layer
names as the `hi5622v100` modules; useful to confirm architecture and terminology, no device-specific
register detail.

---

## B. `hongmengkernel` - Hi1105 (PCIe) vendor tree

Repo: `https://github.com/5-Super-Rookie-5/hongmengkernel` - fetched 200 (root listing).
Contains `kernel/linux-5.10-lts/drivers/connectivity/{hi11xx, hisi}`, and `hi11xx/{hi1102a, hi1105}`.
The Hi1105 is a **PCIe** wifi part (the HiSilicon product page in §D1 lists "PCIe(1.8 Gbps)/SDIO"),
making this the closest public sibling to the PCIe `hi5622v100` device.

### B1. `.../hi1105/platform/oal/oal_hcc_bus.c` - HCC bus over SDIO/PCIE/USB (USEFUL)
Fetched 200, 50,778 B, 1,717 lines. File header: *"hcc bus, adaptation SDIO/PCIE/USB etc"*.
Contains: `HCC_BUS_SDIO_CAP | HCC_BUS_PCIE_CAP` capability model,
`hcc_set_pcie_switch_flag()`, `oal_pcie_get_one_alive_bus_dev()`, `hcc_message_register()` /
`hcc_message_unregister()` (bus-level, lines 992/1028), `hcc_bus_send_message(hi_bus, H2D_MSG_TEST)`
(line 1562), and includes `pcie_linux.h`, `pcie_host.h`, `pcie_chip.h`.
**Useful:** it shows the *same* "register a callback per message id, then push a small message over the
bus" design used by this device's `pcie_msg_init`/`pcie_msg_send`, and confirms the vendor's PCIe path
is a first-class transport for the HCC layer.

### B2. `.../oal/pcie/pcie_firmware_msg.c` - PCIe H2D/D2H message ring (USEFUL)
Fetched 200, 20,365 B. Implements a **shared-memory H2D/D2H message ring over PCIe**:
`frw_ringbuf_t` with `.rd/.wr` at mapped control addresses (`oal_writel`/`oal_readl`),
`oal_pcie_firmwre_{h2d,d2h}_msg_buf_{rd,wr}_update`, `oal_pcie_firmware_msg_send_proc`,
`oal_pcie_firmware_msg_d2h_rx_isr`, `oal_pcie_firmware_msg_int_func_register/_int_mask`.
**Relation to the device:** this is the vendor's PCIe "message service", i.e. the conceptual analogue of
the device's `pcie_msg_init`/`pcie_msg_send` - but the **mechanism differs**: Hi1105 uses a
descriptor-based ring buffer + an interrupt, whereas the `hi5622v100` boot path uses a **mailbox
pending bitmap + doorbell/ack registers** (`0x40039010 / 0x400392d4 / 0x400392f0`). Useful for the
transport/ring vocabulary; it does not contain the register block or a doorbell.

### B3. `.../oal/pcie/pcie_reg.h` + `pciev100/pcie_reg_v100.h` (PARTIAL)
Fetched 200: `pcie_reg.h` (295 B, include shim) and `pciev100/pcie_reg_v100.h` (3,838 B) which holds
**PCIe config/ATU** register macros (`hi_pci_iatu_inbound_ba`, ...). Searched for `0x4003`, doorbell,
D2H, H2D - **none present**. These are bus-config registers, not the Wi-Fi device's internal mailbox.

### B4. `hi1105/{platform,wifi}` trees (PARTIAL)
Listings fetched 200: `platform/` = `{board, driver, excp, factory, firmware, frw, inc, main, oal,
oam, pm, sdt}`; `wifi/` = `{chip, customize, dpe, hmac, inc, main, oam_adapter, wal}`;
`platform/oal/` = `{external, oal_bus_if.c, oal_circ_queue.c, oal_hcc_bus.c, oal_hcc_host.c,
oal_hcc_test.c, oal_sdio_host.c, oal_hardware.c, pcie/, sdio/}`;
`platform/oal/pcie/` = `{chip, conn_dmi, edma, ete, pcie_dbg.c, pcie_firmware.c, pcie_firmware_msg.c,
pcie_host.c, pcie_linux.c, pcie_reg.h, pcie_rc_porting, pciev100, pciev151, pciev500, phy}`.
Note **`ete`** and the `sdio/`+`pcie/` split - the same "ete" ring naming seen in this device's
`shuangta_ete_*`/`pcie_ete_*` code. Useful as a structural cross-check.

### B5. DeepWiki `hongmengkernel` page (NOISE - see §E3)

---

## C. Other driver trees on GitHub

### C1. `themactep/wifi-hi3881` (PARTIAL)
`https://github.com/themactep/wifi-hi3881` - repo page fetched 200 (1,709 B) plus recursive tree via
API (200; 168 paths: `app/`, `driver/`, `firmware/`, `components/`, `include/`, `build/config`,
`Makefile_liteos`). It is a LiteOS/OpenHarmony Hi3881 driver tree; grep of the tree for
`hcc|hmac|wal` found **no** host HCC/HMAC/WAL source (it is the RTOS-side driver). Verdict partial:
same chip family, not the Linux HCC stack (which lives in §A).

### C2. `gtxaspec/ws73v100-wifi` (PARTIAL)
Found via repo search `q=ws73v100` (API 200, 1 unique hit). Tree API (`?recursive=1`) fetched 200 but
**truncated at 51 KB** - it begins with `application/dft/...` and includes
`application/dft/bsle_dft_driver/bsle_dft_hcc/bsle_dft_hcc_proc.c` (an HCC-ish naming). WS73V100 is a
HiSilicon Wi-Fi 6 part; the repo is a driver+firmware+`open_source` drop. Verdict partial: worth a
deeper look later, but the fetched slice showed only the DFT/BSLE application layer, no host PCIe
mailbox source.

Also: GitHub repo-search API returned **total_count 0** for `hi1105+wifi` and `hi1152`, and no
`hi5622`/`hi5621`/"luofu" repo exists (searched directly; see query list).

---

## D. Chip / module / vendor pages

### D1. HiSilicon official Hi1105 product page (PARTIAL)
`https://www.hisilicon.com/en/products/connectivity/smartphone-wearable/smartphone/Hi1105` - fetched
200, 1,011 B. Gives silicon facts only: 802.11a/b/g/n/ac/ax, 2x2 160 MHz (2.4 Gbps),
**"Data Interfaces: PCIe (1.8 Gbps) / SDIO (500 Mbps)"**. Confirms a HiSilicon Wi-Fi part with a
native **PCIe** host interface (supports the §B relevance) but contains no protocol/register content.

### D2. H105 (Hi1105) module datasheet PDF (NOISE)
`http://www.v-linktech.com/static/upload/file/20240502/1714641336145436.pdf` - fetched 200, 29,356 B
of raw `%PDF-1.5` bytes (not text-extracted). An 11-page WiFi/BT/GNSS **module** product brief; no
register or mailbox content. Noise for this task.

### D3. hi5622v100 catalogue blurb (NOISE)
`https://onlinecatalogue.electronicachina.com.cn/journal/products/6419` - fetched 200, 2,599 B
(mostly site navigation; the product copy names Hi5622V100 as a Wi-Fi 6 AX3000 dual-band chip for
PON gateways). Marketing only. The search summary additionally claimed "hi5622v100 = Hi1152"; that
claim is **not** verifiable from the fetched page text and is recorded here as unsourced.

### D4. WIFI6-SR5370 module page (NOISE)
`https://en.youhuatech.com/products/show-9.html` - fetched 200, 719 B. Marketing page for an AX3000
router/module ("HiSilicon Inside"). No chip model, no protocol.

---

## E. Forums, upstream Linux, wiki pages

### E1. OpenIPC issue #1646 - Hi1131 SDIO wifi boot log (PARTIAL)
`https://github.com/OpenIPC/firmware/issues/1646` fetched 200 (2,471 B: Hi3518EV200 U-Boot log, not
wifi); comments fetched via `https://api.github.com/repos/OpenIPC/firmware/issues/1646/comments`
(200, 23,831 B). One comment carries a Hi1131 wifi bring-up log: `wifi_hi1131_init===>>`,
`oal_sdio_func_probe::shutdown wifi after init sdio`, `ubia_wow_init[...] default wow server[...]`,
`hisi sdio load sucuess, sdio enum done`. Verdict partial: **real vendor driver log** confirming the
`hi1131` (SDIO) sibling stack and the `ubia_*`/`oal_sdio_*` symbol family, but again SDIO and no
register data.

### E2. OpenWrt forum - Huawei AX3 Pro / Hi1152 (NOISE)
`https://forum.openwrt.org/t/support-for-huawei-ax3-pro/66551/57` - fetched **0 bytes, twice**
(empty body; JavaScript/anti-bot). Search context says the thread concludes there is *"zero mainline
support for the HiSilicon SOC, nor the Hi1152 wireless chipset"* - relevant as a status statement, but
**I could not retrieve the page**, so it is not citable beyond the search snippet.

### E3. DeepWiki `hongmengkernel` hi11xx page (NOISE)
`https://deepwiki.com/5-Super-Rookie-5/hongmengkernel/3.2-hisilicon-connectivity-drivers-(hi11xx-wifibtgnss)`
- fetched 200 but only 66 B of body ("Loading..."): client-side rendered, no content delivered to a
plain fetch. Not usable directly; the underlying repo (§B) is the real source.

### E4. Hi1105 PCIe ASPM quirk in upstream Linux (unretrievable)
The search found a real upstream artifact - a `PCI/ASPM: Avoid L0s and L1 on Hi1105 [19e5:1105] Wi-Fi`
commit (shawn.lin@rock-chips.com). It would prove Hi1105 is a shipped PCIe Wi-Fi device, but **every
mirror blocked automated fetch**: codeberg (bot challenge, 349 B interstitial), lore.kernel.org
(Anubis "not a bot" page, 1,075 B), patchwork.ozlabs.org (timed out after 30 s). Recorded as
**unverified**; not cited as evidence.

---

## F. Attempted, no content returned

- **F1** `https://gitcode.com/openharmony/device_soc_hisilicon/blob/master/.../hcc/hcc_host.c` -
  fetched 200 but body = "Access rate limit exceeded. Please log in to continue." (191 B). Use the
  raw.githubusercontent.com URL in §A instead.
- **F2** `https://gitee.com/openharmony/device_soc_hisilicon` - fetched 200, **0 bytes**.
- **F3** gitee PDFs (`Hi3861V100/Hi3861LV100/Hi3881V100 WiFi 芯片用户指南`, `HiSTB-doc ... WiFi 使用指南`)
  - **"Response too large (exceeds 5MB limit)"** / signed URL. Not retrieved.
- **F4** `https://docs.aithinker.com/_media/wiki/hi3861lv100_产品简介.pdf` - fetched 200, 107 B
  (site chrome only; PDF body not parsed).
- **F5** Exact register addresses `0x400392d4 / 0x40039010 / 0x400392f0`: the only hits are **TI
  TM4C123 (Tiva) MCU headers** that happen to alias `0x40039000` (e.g.
  `github.com/adimalla/Minimal-TCP-IP-Stack/blob/master/tm4c123gh6pm.h`) - a different silicon family
  entirely. **No public HiSilicon register map documents these addresses** (explicit negative).
- `https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/.../hi1105/wifi/hcc/hcc_host.c` -
  404 (wrong guessed path; the hi1105 hcc code lives under `platform/oal/`, see §B).

---

## What this gives the port, and what it does not

**Gives (usable now):**
1. A **GPLv2 reference implementation of the same `hcc` layer** (§A1/A2) - including a packed-nibble
   frame header, a per-message-id registration API (`hcc_message_register`), and the "post a small
   message, payload rides the ring" send idiom that matches this device's `pcie_msg_*` design.
2. The **public H2D/D2H message-id namespace** for the family (§A3), to compare against the device's
   observed ids.
3. An **HCC-over-PCIe** implementation with an H2D/D2H ring and interrupt (§B1/B2) - the closest
   published transport to a PCIe Wi-Fi message service, plus the `ete` ring naming (§B4).
4. Permission-clean reuse: §A files are GPLv2; §B files are a vendor kernel tree (check its license
   before reuse).

**Does not give:**
1. Any datasheet, register map, or documentation for **hi5622v100 / luofu**, or for the mailbox block
   `0x40039000-0x40039600`. Searched exhaustively; **none is public** (§F5).
2. The device's **12-byte HCC header layout** (§A1's header is a 4-byte bitfield in a 64-byte TCM
   header - related family, **not byte-compatible**).
3. The **doorbell/ack register pair** or the pending-bitmap word semantics - the public PCIe sibling
   uses a ring + interrupt, not a mailbox+doorbell (§B3).
4. A 1:1 message-id map: the family's 32-slot namespace exists publicly (§A3) but the **per-id
   assignments differ by transport** and must not be copied across.

## Queries run (24 total; github / forum / datasheet / vendor angles)

1. `hi5622v100` 2. `hisilicon wifi driver "hcc_msg" OR "hcc_msg_tx_to_core" linux`
3. `hisilicon "hmac_main_init" OR "hmac_tx_event_process" wifi driver`
4. `HiSilicon PCIe wifi mailbox doorbell register 0x40039000` 5. `hisilicon hsan wifi driver open source github`
6. `openwrt hisilicon wifi "hcc" mailbox "d2h" driver`
7. `HiSilicon WiFi6 PCIe host device protocol H2D D2H doorbell datasheet`
8. `github hisilicon wifi driver hi5622 OR hi5621 OR "luofu"` 9. `"shuangta" hisilicon ete pcie ring`
10. `hisilicon wifi chip "hdpp" "wal" "hmac" message table`
11. `site:github.com hisilicon wifi driver hcc hmac hi1105 OR hi3881 OR hi1131`
12. `"hi1152" OR "hi5622" wifi driver source code hisilicon` 13. `hisilicon wifi "hcc_host.c" OR "hcc_msg.c" source code`
14. `hisilicon hi1105 wifi driver linux kernel source hmac wal hdpp oal`
15. `OpenHarmony device_soc_hisilicon wifi pcie hcc host driver source`
16. `hisilicon wifi pcie "sr ring" OR "dr ring" host driver hi1105`
17. `hisilicon wifi driver "pcie_msg_send" OR "pcie_msg_init"` 18. `"hi3881" wifi pcie driver source hisilicon github openwrt`
19. `"hsan" hisilicon wifi driver` 20. `hi1105 wifi pcie driver mailbox hcc h2d d2h registers`
21. `HiSilicon wifi "hcc" mailbox "msg" register pcie hi1151 OR hi1152`
22. `0x400392d4 OR 0x40039010 OR 0x400392f0 hisilicon`
23. `"hcc_bus_send_message" OR "frw_ringbuf_t" hisilicon wifi` 24. `hi1152 openwrt wifi support driver forum`

Plus GitHub REST API queries: repo search `hi1105+wifi`, `hi1152`, `hi3881`, `ws73v100`,
`hisilicon+wifi`, `hi3861+wifi+driver`; and tree/contents listings for the repos in §A-C.
