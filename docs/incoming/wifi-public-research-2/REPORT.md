# wifi-public-research-2 - deeper public research: exact-chip hunt, Hi3881/Hi1105 sibling AP/STA flow, Linux RX-ring contracts

**Lane:** public-research2 (shared task `task-152`). **Host-only:** web research,
no bench contact, no UART, no device access by this lane; the Lead fires.
**Written:** 2026-11-25 (host clock; the project ledger already holds dated entries
later than this - all factual claims here are pinned to the fetches/manifest below,
not to those dates).
**Write scope:** `opensource/docs/incoming/wifi-public-research-2/`.
**Base state given by the Lead at claim time (not re-derived here):** Wi-Fi stack
hardware-validated through AP-ENABLED + WPA2/WPA3 key paths; RX engine live bank
`CA 0x40050000` (status 0x182 when armed, ordering AP-start -> dp_setup -> rx_arm);
**RX captures still zero**; client association is the last blocker (mgmt RX decode +
ring drain + antennas); final association module 344,472 B sha `251d08f0`,
56 params (Lead-provided artifact; not part of this lane's evidence).

Evidence markers: **[W]** fetched from the public web in this lane and pinned in
`SOURCES.tsv` (URL + byte count + sha256; `VERIFY-fetch.txt` records a 51/51
re-fetch byte/hash match at the pinned commits). **[L]** local committed artifact
(ledger / lane report / committed vendor disassembly text) - quoted for orientation,
not re-derived by this lane. **[I]** inference. **[U]** unknown.

Deliverables in this directory: this `REPORT.md`, `RANKED-NEXT-TRIES.md`
(actionable list for the Lead), `SOURCES.tsv` (verified fetch manifest),
`VERIFY-fetch.txt` (re-fetch check), `README.md` (index).

---

## 0. TL;DR - what actually changes for the association blocker

1. **The mgmt-TX carrier we implemented is probably the wrong message.** The
   `0x01F2` WAL cfg-event decoded by the wifi-ap-mgmt lane [L] is a
   **host-internal WAL -> HMAC request**, not a device message. In the
   license-clean sibling driver the dispatch table entry for
   `WLAN_CFGID_CFG80211_MGMT_TX` calls the in-host function `hmac_wpas_mgmt_tx`
   (`hmac_event.c:289` [W]); that function copies the frame and sends it to the
   DMAC firmware as a **`FRW_EVENT_TYPE_WLAN_CTX` / `DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`
   event**, which the HCC layer wraps as a control frame with an extend header
   `{nest_type=WLAN_CTX, nest_sub_type=MGMT, vap_id, config_frame=0}`
   (`hmac_mgmt_bss_comm.c:1453`, `hcc_hmac.c:97/438/540` [W]). The completion
   comes back as a **DMAC CRX "mgmt tx status" event** (`hmac_event.c:437`,
   `hmac_mgmt_bss_comm.c:2698` [W]) - i.e. the same family as the g2 id13 status
   event we already decoded [L]. **Consequence:** the absence of `0x01F2` from
   PD4 firmware table A [L] is *expected* and is not evidence that mgmt TX is
   unsupported; the next fire should target the WLAN_CTX/MGMT carrier instead.
   Two independent generations agree (Hi3881 2021 driver [W], Hi1105 Wi-Fi 7
   driver [W]), and our own committed vendor disassembly corroborates it: the
   luofu `hmac_wpas_mgmt_tx` at `0x4050c` builds a raw `hcc_msg_alloc`/
   `hcc_msg_tx` message (payload sub-type byte `6` at +6, byte 6 = MGMT in that
   generation) instead of re-posting the WAL TLV [L]. See section 4 and
   `RANKED-NEXT-TRIES.md` T1.
2. **Management RX is not data-ring traffic.** In the same architecture the
   DMAC delivers received management frames to the host as a **CRX event**
   (`dmac_wlan_crx_event_stru` with sub-type `DMAC_WLAN_CRX_RX`, 48-byte
   `hmac_rx_ctl_stru` RX-info callback), which the host AP FSM consumes and
   forwards to cfg80211 via a host-context `RX_MGMT` event
   (`hmac_ap_up_rx_mgmt` `hmac_mgmt_ap.c:1890`; probe req ->
   `hmac_send_mgmt_to_host` `hmac_mgmt_bss_comm.c:2557`; `hmac_send_event_to_host`
   `hmac_event.c:964` [W]). Data frames are a *separate* DRX/DTX path. So the
   laptop's probe requests are expected on our **HCC g1 id13 / mgmt-event
   decoder**, not on the MAC RX rings; **ring drain staying 0 under a client scan
   is expected behaviour, not a bug**. The ring matters for EAPOL/data after the
   4-way starts. See section 5 and T3.
3. **The DMAC RX filter gates mgmt forwarding, and our driver has no filter
   step.** The sibling posts a `SET_RX_FILTER` WLAN_CTX event at VAP state
   changes, with AP-UP values `0xF7B9FAEA` (single-VAP AP), `0x73B9FACA`
   (multi-VAP with STA up, both directions), and clears **BIT4** for
   "support_any" devices so broadcast management from other BSSs - *especially
   probe requests* - is not filtered (`hmac_rx_filter.c:184/240/267/307/325/392/436`
   [W]). A missing filter program is a plausible reason the DMAC forwards no
   mgmt frames to us at all. See section 5.2 and T2.
4. **Linux RX cross-check supports the ring model already implemented, and adds
   one missing detail: a fill/refill watermark.** Across mt76, iwlwifi, ath10k,
   ath11k, brcmfmac and mwifiex the pattern is identical: **publishing the
   free-buffer producer index is the RX enable** (no separate global RX-enable
   bit), completion/used entries carry **buffer addresses/IDs**, and the host
   **consumes -> refills -> republishes**, typically at a watermark. Full-MAC
   designs (mwifiex, brcmfmac) additionally run management/control on a
   *separate event ring* from data - exactly what the HiSilicon CRX/DRX split
   says. See section 6.
5. **Full 13-channel scan: public research finds no host-side cap.** The sibling
   host sends the entire cfg80211 channel list in one `mac_scan_req` event
   (per-channel `{band, chan_number, idx, bandwidth}`, `channel_nums`,
   `us_scan_time` active/passive, `max_scan_cnt_per_channel`)
   (`hmac_sme_sta.c:116`, `hmac_scan.c:1673` [W]); the 6 -> 7/8-channel ceiling
   is therefore DMAC/firmware-side, consistent with our structural-blocked
   result [L]. The useful public hint is the *field set*: our 520-byte
   `g0 id0xC` scan request and the 36/44-byte H1 band-config should be mapped
   field-by-field against `mac_scan_req_stru`/`mac_scan_channel_stru`; the
   `field10 -> count` behaviour we measured is the shape of such a
   count/dwell field. See section 7 and T6.
6. **Exact-chip source is still not public** (verified again today): OpenHarmony
   `device_soc_hisilicon` contains **only** `hi3881v100` under
   `common/platform/wifi`; Cudy WR3000 2.0's download page has an empty GPL
   section; Jeton publishes no GPL; mainline Linux has no HiSilicon Wi-Fi driver.
   No change to the upstream/legal blocker. See section 2 and 10.
7. **Licensing is a hard constraint:** the notebook's `opensource/` repo is
   **MIT**; the sibling driver files are **GPL-2.0** (directory also listed
   Apache-2.0) - so facts/structures may inform clean-room work, but **no
   verbatim source text may be copied into this repo**. See section 10.

---

## 1. Method, verification, limits

* Fetch method: `curl` of **commit-pinned** `raw.githubusercontent.com` URLs
  (OpenHarmony `7c1e2d5b...`, hongmengkernel `297b33b0...`, torvalds/linux
  `4dce97c6...`), saved under the gitignored `build/wifi-public-research-2/fetched/`.
  Every row of `SOURCES.tsv` carries URL, byte count, sha256 and the first line.
  `VERIFY-fetch.txt`: re-downloaded all 51 remote sources at the pinned commits -
  **51/51 byte count + sha256 match** (`git.kernel.org` mirror used for two
  initially rate-limited files; same SHA-256 as the GitHub object).
* Directory/work product listings used the GitHub contents API (e.g. the
  OpenHarmony `common/platform/wifi` listing). The recursive tree API returned
  HTTP 504 twice (gateway timeout, 54,881-byte error page) - recorded as a
  limitation; the per-directory listing for `common/platform/wifi` is authoritative
  for the "which chips" question.
* Web searches in EN/RU/CN for the exact part numbers and SDK names (list in
  section 2.6); product datasheets/FCC/regulatory docs are **not** publicly
  available (no register-level document found).
* JS-heavy vendor download pages were fetched raw and inspected (script payload +
  `#gpl-files` block); evidence copies kept locally.
* Local repo artifacts are read-only inputs for orientation: ledger
  (`mem-entries.md` newest entries), lane reports under `docs/incoming/wifi-*`,
  and the committed vendor disassembly text
  `docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis` (`[L]` spot checks in
  sections 4.4/5.3; not a substitute for the static-analysis lanes).
* This lane did **not** decode the luofu firmware or the vendor `.ko` beyond
  those spot checks; shared task `task-151` (sibling-GPL static analysis) covers
  the deeper static work on the same source material - coordinate, don't
  duplicate.

---

## 2. Part 1 - public source/SDK hunt for Hi5622V100 / Hi5671YV200 "luofu v2"

### 2.1 OpenHarmony `device_soc_hisilicon` - the only public source of the family

* Repo: <https://github.com/openharmony/device_soc_hisilicon> (pinned master
  commit `7c1e2d5b35c090009bbe195f09337701c60af032`; mirror
  <https://gitcode.com/openharmony/device_soc_hisilicon> - the GitHub repo is the
  one used here).
* **`common/platform/wifi` contains exactly one chip directory:
  `hi3881v100`** (GitHub contents API [W], checked 2026-11-25). The hi3881v100
  driver is the full `wal`/`hmac`/`mac`/`hcc`/`oal` stack (~240 `.c/.h`
  files; 203 files in the `mac/hmac`, `mac/common`, `wal`, `hcc`, `oal`
  directories were listed with blob SHAs in
  `build/wifi-public-research-2/fetched/oh/file-shas.txt`).
* Transport: SDIO `oal_sdio*` only; **no PCIe transport, no `0x4003xxxx`
  register map, no luofu/Hi5622 chip layer** - the earlier phase45 finding stands
  [L]. Nothing under this repo references `hi5622`, `hi5671`, `luofu`,
  `hi1105`, or `hi1152` by name (keyword search of the fetched file set and
  the `common/platform/wifi` listing [W]).
* License evidence: every fetched `.c/.h` starts with the HiSilicon GPL-2.0
  header (quoted, `hmac_mgmt_ap.c` lines 1-15 [W]); the repo-level `LICENSE`
  (39,754 B) additionally says `./common/platform` is Apache-2.0 [W]. Dual
  labelling: the file headers govern the driver code. See section 10.
* **Use for us:** architecture oracle for the *host-side* WAL/HMAC layers
  (mgmt TX/RX contracts, RX filter, station/user lifecycle, scan request shape)
  - not a drop-in driver and not chip-specific.

### 2.2 hongmengkernel `hi1105` - newer generation, same family (provenance caution)

* Mirror: <https://github.com/5-Super-Rookie-5/hongmengkernel> path
  `kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi` (HEAD
  `297b33b07b56adb3fa53baf618fdbc16ad9fd447`). Unofficial re-upload with
  stripped/GBK headers (prior `wifi-source-hunt` report [L] flagged this);
  **use for structural comparison only, prefer the OpenHarmony copy for anything
  vendored into a GPL tree**.
* This is a Wi-Fi 7 generation (11be/MLO/EHT) and is **larger/richer**, but the
  management architecture is the same: `hmac_tx_mgmt_send_event` still sends
  `FRW_EVENT_TYPE_WLAN_CTX / DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`
  (`hmac/connect/hmac_mgmt_bss_comm.c:239`), `hmac_wpas_mgmt_tx` is still the
  `WLAN_CFGID_CFG80211_MGMT_TX` handler (`:885`; WAL side
  `wal/wal_linux_cfg80211.c:651`), and the HCC adapt layer has explicit
  MGMT-specific netbuf/header handling
  (`hmac/hmac_hcc_adapt.c:170/192/222/258` [W]). **Two independent generations
  agreeing is why the section-4 conclusion is high-confidence.**

### 2.3 Cudy WR3000 2.0 GPL - re-checked, still empty (exact chip obligor)

* Page <https://www.cudy.com/en-us/pages/download-center/wr3000-2-0> fetched
  2026-11-25: HTTP 200, **1,643,850 bytes** [W]. The page contains a GPL block
  but it is empty and hidden:

  ```html
  <div id="gpl-files" class=" hidden "> <div class="download-section">GPL</div> </div>
  ```

  Page 2 (`?page=2`, 994,462 B) has the same empty block; **zero** archive
  links (`.zip/.tar.gz`) and zero cloud-drive links on either page [W].
* Firmware visible in the same page HTML (context only): image names
  `WR3000V2-R116-2.4.14-20250928-112520-sysupgrade.zip`,
  `WR3000V2-R116-2.4.15-20251030-114751-sysupgrade.zip` and
  **`WR3000V2-R116-2.5.24-20260727-122111-sysupgrade.zip`** (verified strings
  in the fetched page [W]). The 2.5.24/2.4.15 vendor userspaces are the ones
  the RE track already works against [L].
* Contrast with the **WR3000 v1 page**: fetched 2026-11-25 (HTTP 200,
  1,485,710 B [W]); its GPL block is visible (no `hidden` class) and contains a
  row **"21.02 / 16-Apr-2024"** linking to Google Drive id
  `1und0klzZ22tw4VvS7hxvMr5BmfwD-o0m` - i.e. the known **WR3000 v1 / MediaTek**
  archive, not our silicon [W] (matches the prior `wifi-source-hunt` finding
  [L]). The v2 page's block is present but produces **no rows**, which is a
  clean, verifiable difference between the two pages. The GPL request in
  `docs/GPL-REQUEST.md` remains unanswered; nothing new to cite.
* Cudy OpenWrt FAQ page (571,379 B [W]) contains no GPL/source link. A
  community thread surfaced in search (4pda, `t1087457`) lists WR3000
  **v2/v3 among the models "not worth buying for OpenWrt"** [W, search snippet
  only - not a primary source]; consistent with our "no upstreamable driver"
  position.

### 2.4 Jeton AX3000 Core (same SoC rebrand) - no source

* <https://www.jetontechno.com/product/ax3000-core/> fetched: HTTP 200,
  **103,992 bytes**, **no GPL/source link anywhere** [W]. Product pages and a
  Russian review confirm the hardware: **Hi5671YV200 CPU + Hi5622V100 Wi-Fi +
  KCT FEMs** (Jeton review [W]; also Tianyi TY-AX30C datasheet, Youhua
  SR5370-H01, Pinhom AX3000, JLCPCB part page for Hi5671YV200 [W]). Same
  silicon = same GPL obligation; still no drop.

### 2.5 Mainline Linux - nothing

* `drivers/net/wireless/Makefile` (1,010 B) and `Kconfig` (1,382 B) fetched
  at torvalds master `4dce97c6...`: **zero** matches for
  `hisi|hi3881|hi1105|hi5622|luofu`; the Kconfig source list is
  admtek/ath/atmel/broadcom/intel/intersil/marvell/mediatek/microchip/morsemicro/
  nxp/purelifi/ralink/realtek/rsi/silabs/st/ti/zydas/quantenna [W]. No mainline
  HiSilicon full-MAC Wi-Fi driver exists.

### 2.6 Other web surfaces checked

* Searches (EN/RU/CN) for `Hi5622V100`, `Hi5671YV200`, `Hi5671Y`, `luofu
  V200 SDK`, `ChenTangV100`, `hi_wifi`, `HiHK`, `Hi3881V100`,
  `hi_wifi_api.h`, OpenWrt/HiSilicon Wi-Fi attempts. Results are product
  listings/teardowns only; no datasheet, register map, SDK drop or RE writeup
  for our chip. (Hi3861/3863 IoT SDK material exists but is a different
  product line.)
* The Hi1105 **PCIe** sibling is real (a mainline PCI quirk patch "PCI: add ASPM
  quirk for Hi1105 PCIe Wi-Fi" surfaced [W]) - useful as evidence that the
  phone-chip line shares PCIe HCC transport concepts, but still no source for
  our PCIe HAL.
* OpenHarmony has moved/duplicated mirrors (gitcode/atomgit). The atomgit repo
  `OH_001_will_delete_20251230/device_hisilicon_drivers` appears to be a
  decommissioning copy; the GitHub `device_soc_hisilicon` copy is stable and is
  what we pinned.
* `grep.app` public-code search is now behind a Vercel security checkpoint
  (HTML challenge, no API results) [W] - **checked negative**, can't be used
  from a script; GitHub code search requires auth. This is a limitation, not a
  "no results" claim.

---

## 3. Part 2a - Hi3881 AP management flow (what the host must do, and where)

Architecture in the sibling (same for our luofu generation, different transport):

```
cfg80211/nl80211
  |  (wal_* : cfg80211 glue, host)
  v
wal_cfg80211_*  --(WLAN_CFGID_* internal cfg requests)-->  hmac_* (host full-MAC layer)
  |                                                          |
  |  mgmt RX: DMAC -> host  FRW_EVENT_TYPE_WLAN_CRX ... <-----+---- hcc (transport) ---> dmac (firmware)
  |  mgmt TX: host -> DMAC  FRW_EVENT_TYPE_WLAN_CTX/MGMT ----+
  |  data:    DRX/DTX event path (separate class)
  v
cfg80211_rx_mgmt / cfg80211_connect_done / station callbacks
```

AP state machine relevant to our blocker:

### 3.1 AP UP received-management dispatch (`hmac_ap_up_rx_mgmt`, `hmac_mgmt_ap.c:1890` [W])

* Entry condition: `vap_state == MAC_VAP_STATE_UP`.
* Subtype switch:
  * `AUTH` -> `hmac_ap_rx_auth_req` (`:197`): builds an **auth response** in a
    host netbuf (`hmac_encap_auth_rsp`), resets the user's PSM state, then
    `hmac_tx_mgmt_send_event(...)` (`:244`).
  * `DEAUTH` -> `hmac_ap_rx_deauth_req` (`:259`).
  * `ASSOC_REQ`/`REASSOC_REQ` -> `hmac_ap_up_rx_asoc_req` (`:1533`) (see 3.2).
  * `DISASSOC` -> `hmac_ap_up_rx_disasoc` (`:1597`).
  * `ACTION` -> `hmac_ap_up_rx_action` (`:1806`); `PROBE_REQ` ->
    `hmac_ap_up_rx_probe_req` (`:1864`).
* **Our design difference:** we run a host-SME (hostapd), so we must forward
  AUTH/ASSOC/PROBE to cfg80211 instead of handling them in the driver. The
  sibling shows what the firmware will forward and in what shape; the host-SME
  driver still needs the mgmt TX path to send the responses (section 4).

### 3.2 Association request processing -> responses + firmware user context (`hmac_ap_up_rx_asoc_req`, `:1533`)

1. `hmac_ap_get_mac_and_user` (`:1283`) - find/create the per-STA user object.
2. `hmac_ap_asoc_req_process` (`:1365`):
   * update STA user info from the assoc req IEs,
   * `hmac_config_user_info_syn` - sync user (rates/bandwidth/HT/VHT) to the DMAC,
   * PMF handling (`hmac_ap_asoc_pmf_process` `:1324`), security init
     (`hmac_init_security`/`hmac_init_user_security_port`),
   * on success: `hmac_user_add_notify_alg`, save the assoc req for the kernel,
     `hmac_handle_connect_rsp_ap` (report to WAL/cfg80211).
3. `hmac_ap_tx_asoc_rsp` (`:1477`) builds the assoc response
   (`hmac_ap_prepare_asoc_rsp` `:1430`) and sends it with
   `hmac_tx_mgmt_send_event` (`:1513`).
4. Failure path deletes the temporary user (status != SUCCESS and != TEMP).

**Mapping to us:** sta-ops lane's decoded per-STA firmware context
(`g0 id7 user_add` + `g0 id1 0x07d2 assoc-state syn`) [L] is the luofu analog
of steps 2's `hmac_config_user_info_syn`/`hmac_user_add`; the auth/assoc
**responses** are the mgmt TX carrier (section 4), not data.

### 3.3 Probe request -> host (`hmac_ap_up_rx_probe_req`, `:1864`)

It computes the current channel frequency and calls
`hmac_send_mgmt_to_host(hmac_vap, netbuf, rx_ctrl->us_frame_len, l_freq)`
(`hmac_mgmt_bss_comm.c:2557`) -> host-context event subtype
`HMAC_HOST_CTX_EVENT_SUB_TYPE_RX_MGMT` (`hmac_event.c:964`). So **probe
requests are a host-delivered management event in this family**, not a data/ring
frame. This is the key RX expectation for the association test.

### 3.4 Beacon handling

* Host sets the beacon template via a WLAN config request ->
  `hmac_config_set_beacon` (`hmac_config.c:2852`) -> `mac_vap_set_beacon` +
  `hmac_config_send_event` (`:2896/`:2914`) i.e. a device config event.
* The AP start / beacon engine itself is DMAC-side; nothing in the sibling
  suggests a host beacon-ring producer is needed for the AP (our on-air beacon
  proof [L] is consistent with firmware-autonomous beaconing once the template
  and AP start are in).

### 3.5 Station/user add and delete events into the firmware

* `hmac_user_add` (`hmac_user.c:647`) -> `hmac_user_add_send_event` (`:598`)
  -> WLAN_CTX event `DMAC_WLAN_CTX_EVENT_SUB_TYPE_ADD_USER` (`:620`);
  deletes use `DEL_USER`; alg add/PSM resets are separate WLAN_CTX sub-types.
* This matches the sta-ops lane's luofu `g0 id7 user_add`/`g0 id9 user_del`
  decode [L] at the architectural level (the exact luofu message ids differ).

---

## 4. Part 2b - THE mgmt-TX contract (decisive for client association)

### 4.1 The chain in the sibling (verified, two generations)

1. **cfg80211 op** `wal_cfg80211_mgmt_tx` builds a 12-byte request:
   `mac_mgmt_frame_stru` = `{u32 channel; u8 mgmt_frame_id; u8 pad;
   u16 us_len; u32 puc_frame}` and calls
   `wal_cfg80211_start_req(..., WLAN_CFGID_CFG80211_MGMT_TX, ...)`
   (`wal_cfg80211.c:4573-4607` [W]). **This 12-byte layout is exactly the
   descriptor the wifi-ap-mgmt lane decoded from our vendor `.mgmt_tx`
   (channel, cookie_idx, pad, frame_len, frame_addr) [L]** - so that part of the
   lane's decode is confirmed.
2. **The cfg request is host-internal.** `hmac_event.c:289` maps
   `{WLAN_CFGID_CFG80211_MGMT_TX, HI_FALSE, {0}, HI_NULL, hmac_wpas_mgmt_tx}`
   - the handler runs in the host driver; no device message is implied by the
   request itself. (Contrast: device-bound config ids appear in the luofu PD4
   firmware table A, e.g. the key ops 0x0078/0x0079/0x007A [L].)
3. `hmac_wpas_mgmt_tx` (`hmac_mgmt_bss_comm.c:2611` [W]) allocates a netbuf,
   copies the frame from the 16-bit length + host pointer in the request, fills
   the TX control block:
   * `us_mpdu_len = len`;
   * `need_rsp = TRUE`, `is_vipframe = TRUE`, `is_needretry = TRUE`;
   * `mgmt_frame_id = cookie`;
   * `frame_header = netbuf header`, `mac_head_type = 1`,
     `frame_header_length = MAC_80211_FRAME_LEN`;
   * **`us_tx_user_idx = MAC_INVALID_USER_ID`** -> mgmt TX needs **no station
     user context** (auth/assoc responses can be transmitted before the STA
     exists).
4. `hmac_tx_mgmt_send_event` (`:1453`) posts a
   **`FRW_EVENT_TYPE_WLAN_CTX` / `DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`** event
   with payload `dmac_tx_event_stru {netbuf*, us_frame_len}`, and hands it to
   `hcc_hmac_tx_data_event(...)`.
5. HCC wrapping: `hcc_hmac_tx_data_event` -> `hcc_host_tx_data_adapt`
   -> `hcc_tx_netbuf_normal` (`hcc_hmac.c:540/438/...` [W]). The control
   frame carries the **extend header** `frw_hcc_extend_hdr_stru` =
   `{nest_type, nest_sub_type, vap_id, config_frame}` filled from the frw event
   (`hcc_hmac.c:97`), with `nest_sub_type = DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`
   and `config_frame = 0` for WLAN_CTX events (`hcc_hmac.c:272-310` [W]).
   The newer generation explicitly special-cases this pair in the HCC
   cb/headroom/frame-layout code (`hmac_hcc_adapt.c:170/192/222/258` [W]).
6. **Completion**: the DMAC reports the TX result as a CRX event; the host event
   table maps `WLAN_CFGID_CFG80211_MGMT_TX_STATUS` to
   `hmac_mgmt_tx_event_status` (`hmac_event.c:437`,
   `hmac_mgmt_bss_comm.c:2698` [W]), which carries
   `dmac_crx_mgmt_tx_status_stru` (user index / tx status / frame id) and wakes
   the WAL wait. **This is the same role as the g2 id13 "mgmt tx status" event
   the wifi-ap-mgmt lane decoded [L].**

### 4.2 What this means for our implementation (falsifiable)

* Posting the WAL cfg-event TLV `0x01F2` (32-byte WAL envelope, HCC g2 id0) at
  the firmware - what the ap-mgmt lane implemented - asserts "the device
  understands the WAL->HMAC internal request". The sibling source says that
  request never leaves the host. **Prediction: no firmware handler, no tx, no
  completion - exactly the current behaviour.** The absence of 0x01F2 from PD4
  table A [L] is therefore *consistent*, not a blocker.
* The device-facing message to reproduce is the **WLAN_CTX/MGMT control event**
  (frw extend header + tx control cb + MAC header/frame). Our driver already has
  a proven TX path for data via the MAC MSDU ring [L]; the mgmt carrier may be
  either:
  * (a) the HCC/ETE **control-message** path with WLAN_CTX/MGMT semantics
    (sibling evidence: yes, control frame with frame body attached), or
  * (b) the **MAC TX ring** with an MSDU-control "path" field (our MSDU-info
    `ctl.path[14:12]` [L]) selecting management vs data - the sibling's
    `dmac_tx_ctl_stru` has mgmt-specific fields (`mgmt_frame_id`,
    `need_rsp`) and a "frame type" fetch from the MAC header
    (`dmac_get_frame_type_by_txcb`, `dmac_ext_if.h` [W]), so a single TX ring
    with a mgmt flag is architecturally plausible for the PCIe generation.
  * **Resolve statically before the next fire** by decoding the vendor luofu
    `hmac_wpas_mgmt_tx` -> device-send path (see 4.3) - do not guess on air.

### 4.3 Luofu-specific corroboration from the committed vendor disassembly [L]

Spot check (read-only, this lane did not do a full static pass):

* `hi5622v100_wifi.dis` has `hmac_wpas_mgmt_tx` at `0x4050c` (0x760 bytes),
  `hmac_tx_mgmt_send_event` at `0x40384`, `hmac_send_mgmt_to_host` at
  `0x42454` (symbols also present in the 2.5.24 module).
* `hmac_wpas_mgmt_tx` calls a helper at `0x3fe68` which does
  **`hcc_msg_alloc` + fill a raw message + `hcc_msg_tx`** - not a WAL TLV.
  The payload header observed: byte 0 type/flags bits, byte +1 vap-id nibble,
  bytes +2/+3 copied from the VAP object, **byte +6 = 6 (management sub-type in
  that generation; the same module's `hmac_mgmt_reset_psm` uses byte +6 = 14 =
  its reset-PSM sub-type)**, +7/+8 flags, frame length/pointer/extra header
  fields, then `hcc_msg_tx`. `hmac_mgmt_reset_psm` at `0x4039c` uses the same
  `hcc_msg_alloc`/`hcc_msg_tx` wrapper with sub-type 14.
* Interpretation [I]: the luofu host driver's WLAN_CTX-style events are sent as
  raw `hcc_msg` messages with a small header whose byte +6 is the WLAN_CTX
  sub-type (MGMT=6, RESET_PSM=14 in this generation), and the mgmt frame is
  attached through the tx control block. **A static lane must name the exact
  `hcc_msg` message class and decode the full payload before implementing.**
  The public sibling source provides the field *semantics* (nest type/sub type,
  tx cb, frame), not the luofu ids.

---

## 5. Part 2c - management RX and the DMAC RX filter (why captures can be 0)

### 5.1 Management RX is a CRX event, not a data-ring frame [W]

* Device->host event classes in `dmac_ext_if.h`: `FRW_EVENT_TYPE_WLAN_CRX`
  sub-types `DMAC_WLAN_CRX_RX` (received management frame), `..._SCAN_RESULT`,
  `..._SCAN_COMP`, `..._ACS_RESP`, `..._DISASSOC`, `..._DEAUTH`,
  `..._CH_SWITCH_COMPLETE` etc.; data uses `FRW_EVENT_TYPE_WLAN_DRX`
  (`dmac_ext_if.h:108-201` [W]).
* The CRX event payload is `dmac_wlan_crx_event_stru {netbuf*, puc_chtxt*}`
  (`dmac_ext_if.h:~400` [W]); the netbuf's control block is
  `hmac_rx_ctl_stru` (48-byte host netbuf CB, currently 42 used; fields:
  vap_id/amsdu/first/fragmented, `msdu_in_buffer`, buff_nums/is_beacon,
  `mac_header_len`, `us_frame_len`, `us_da_user_idx`,
  `pul_mac_hdr_start_addr`, `us_ta_user_idx`, `mac_vap_id`,
  `channel_number`, `rssi_dbm`; `dmac_ext_if.h:981-999` [W]).
* Host processing: `hmac_from_dmac_rx_control_handle` dispatches on the event
  sub-type table (`hmac_event.c:841`), CRX data comes via
  `hmac_from_dmac_rx_data_handle` (`:858`) -> `hmac_ap_up_rx_mgmt`
  (section 3.1) -> for probe req `hmac_send_mgmt_to_host` ->
  `hmac_send_event_to_host(..., HMAC_HOST_CTX_EVENT_SUB_TYPE_RX_MGMT)`
  (`hmac_mgmt_bss_comm.c:2597`, event enum `hmac_ext_if.h:91` [W]).
* `hmac_send_mgmt_to_host` copies the frame out of the RX buffer and posts
  `hmac_rx_mgmt_event_stru {puc_buf, us_len, l_freq, ac_name[16]}`
  (`hmac_mgmt_bss_comm.c:2557-2605` [W]).
* **Consequence:** in this architecture a *received probe request / auth /
  assoc* is never a MAC-ring completion. Our g1 id13 decode (60-B prefix = 12-B
  HCC + 48-B RX info, frame after it, channel +7, u16 len +17, s8 RSSI +39) [L]
  is the luofu form of this CRX event. The 48-byte info block is *generation
  specific*: the hi3881 CB has different offsets (channel at +14, rssi at +16)
  so the luofu 48-B layout must stay as decoded locally [L] - but the public
  source validates the *concept* (mgmt info block: vap, mac_header_len,
  frame_len, channel, rssi, user indices) and that the frame bytes accompany the
  event.

### 5.2 The DMAC RX filter (a missing host step in our AP start)

`hmac_rx_filter.c` [W] is a complete specification of the filter contract:

* A `u32` filter value is posted as a **WLAN_CTX / SET_RX_FILTER event**
  (`hmac_send_rx_filter_event`, `:325`; event header at `:367`).
* Value table (AP-relevant, `:184-310` [W]): **single-VAP AP + state UP =
  `0xF7B9FAEA`** (single-VAP AP INIT `0xF7B9FEFA`); with an STA already up, the
  AP VAP uses `hmac_get_staup_ap_rx_filter` = **`0x73B9FACA`**
  (multi-STA mode defaults `0x73B9FADA`); STA-side defaults differ
  (wait-scan/listen `0x37B9...`/`0x33B9...`); 0 means **"keep the previous
  value, do not configure"**.
* Multi-VAP selection rules: AP already up -> AP gets
  `hmac_get_staup_ap_rx_filter` when a STA is up, else the single-VAP value;
  STA-up AP config `hmac_get_apup_sta_rx_filter`; etc. (`:392-434`).
* **BIT mapping evidence** (promiscuous block, `:344-355`): BIT3+BIT12 =
  report multicast/broadcast **data**, BIT11 = unicast **data**, **BIT4 =
  report multicast/broadcast management**, **BIT13 = unicast management**.
* **The probe-request rule** (`:436`): *"devices supporting ANY (STA & AP etc.)
  must be able to receive broadcast management frames from other BSSs,
  especially probe req - do not filter, clear BIT4"*:
  `rx_filter_val = (mac_vap->support_any == HI_TRUE) ? (rx_filter_val & (~BIT4)) : rx_filter_val;`
* The filter is re-posted on VAP state changes (e.g. background scan path
  `hmac_scan.c` toggles the filter via `hmac_set_rx_filter_value` around a
  scan), and AP start handles the INIT->AP_WAIT_START case specially
  (`hmac_scan.c:1800+` [W]).

**Implication:** if our AP-start sequence never posts the equivalent
SET_RX_FILTER to the luofu DMAC, the firmware may be running a default filter
that drops some or all management classes (especially broadcast mgmt/probe req,
BIT4), which would make both the mgmt-RX test and association fail in exactly the
way observed. This is a concrete, cheap next-try (T2) *before* assuming the RX
decoder is wrong.

### 5.3 Which frames come from where (summary for the association test)

| traffic | expected host path in this family | ring drain? |
| --- | --- | --- |
| probe req / auth req / assoc req (client -> AP) | DMAC CRX mgmt event (our g1 id13) -> cfg80211_rx_mgmt | **no** |
| auth rsp / assoc rsp / probe rsp (AP -> client) | WLAN_CTX/MGMT event -> DMAC TX (section 4) | **no** |
| EAPOL msg2/4 (client -> AP) | data RX path (MAC rings; our live bank/drain) | **yes** |
| EAPOL msg1/3 + data (AP -> client) | data TX path (MSDU ring/TXQ, already proven) | - |
| scan results (our scan lane) | scan CRX events / D2H scan messages | - |

So **the 4-way handshake needs both**: mgmt event path for association and ring
path for EAPOL. Our lane ordering finding (AP start first -> dp_setup ->
rx_arm) [L] remains correct for the ring; the mgmt path has its own gating
(firmware state machine + filter).

---

## 6. Part 3 - Linux drivers' RX free-ring / credit patterns (cross-check)

Fetch manifest: all six files pinned in `SOURCES.tsv` (torvalds/linux
`4dce97c6...`), 51/51 re-verified.

| driver | publication = enable? | completion/used entries | refill/credit rule | source |
| --- | --- | --- | --- | --- |
| mt76 (Mediatek) | host fills RX descriptors, then `mt76_dma_kick_queue` (`mt76_dma_rx_fill_buf` `dma.c:776-820`) | device writes head index; host processes `q->head` and moves `q->tail` in `mt76_dma_rx_process` (`:1001-1100`) | refill after processing (`mt76_dma_rx_fill(dev, q, true)` `:1097`); queue kept one slot short (`q->queued < q->ndesc - 1`) | [W] |
| iwlwifi (Intel) | `iwl_pcie_rxq_inc_wr_ptr` writes the RX write pointer to the device (`rx.c:168-222`) | device fills buffers; host reads/steals pages up to the read index (header comment `:55-110`) | `iwl_pcie_rxq_restock` (`:351`) refills `rx_free` and updates the pointer; explicit **RX_STALLED** if no buffers (comment `:80`) | [W] |
| ath10k | ring fill `__ath10k_htt_rx_ring_fill_n` (`htt_rx.c:135`) with a descriptor "attention" word cleared before posting | firmware writes used ring / alloc index in host memory | `fill_level` keeps ring nominally half-empty; refill timer retries (`:240-280`) | [W] |
| ath11k | `ath11k_dp_rxbufs_replenish` (`dp_rx.c:344`) gets entries from the refill SRNG and `access_end` publishes (`:413`) | host reaps completion/REO rings via `dst_get_next_entry`; reaped counts drive replenish (`:2662-2790`) | refill when free slots > 3/4 of max, or explicit `req_entries` | [W] |
| brcmfmac (Broadcom) | host posts RXBUF messages (buffer physaddr + len + request_id) and `write_complete`/ring-bell (`msgbuf.c:932-1020`) | device sends RX_COMPLETE with `request_id`/len/offset; host resolves skb and updates count (`:1029/1176-1200`) | `rxbufpost` counter vs `max_rxbufpost` from firmware; refill on threshold (`:1029-1035`) | [W] |
| mwifiex (Marvell) | data path `process_recv_data` (`pcie.c:1526`) and a **separate event ring** `process_event_ready` (`:1901`) each with their own read/write pointers | event buffers are separate from data buffers | ring pointers per path | [W] |

**Conclusions that transfer to our chip:**

1. **There is no separate global "RX enable" register in any of these drivers.**
   RX becomes live by (a) ring/descriptor setup + (b) publishing the free-buffer
   producer index + (c) unmasking IRQs (or polling). This directly supports the
   rx-static lane's conclusion that the vendor 14-write MAC program is the whole
   device enable and that the missing half was the *consumer* [L].
2. **Completion entries carry buffer identity (physaddr/id/cookie)**, matching
   our "completion entry = devVA(buffer+4)" decode [L]. The host must translate
   back to its buffer, free/refill it, and republish.
3. **Refill watermarks matter** - iwlwifi can literally enter RX_STALLED when the
   free list empties, ath10k/ath11k deliberately keep the ring partially empty,
   mt76 keeps one descriptor spare, brcmfmac watches an outstanding-post counter.
   Our `dp_rx_drain` currently refills "<= live rptr delta" and publishes; for
   sustained traffic (EAPOL/4-way) a **fill watermark / minimum-free rule**
   should be added (e.g. keep >= 3/4 of the ring posted, like ath11k) to avoid
   starving the MAC consumer. This is a design note, not a blocker for the first
   capture.
4. **Full-MAC vs data path:** mwifiex and brcmfmac are the useful precedents for
   "management/control arrives on a different event ring than data" - consistent
   with the HiSilicon CRX/DRX split and the g1 id13 mgmt event. (iwlwifi/mt76/
   ath place mgmt frames on the data RX path - i.e. this is architecture-specific,
   which is why our "probe request on the data ring" expectation was wrong.)

---

## 7. Part 2d - public scan-channel hints (for the 13-channel question)

What the sibling host driver hands the firmware (`hmac_sme_sta.c:116`,
`hmac_scan.c:1673-1740`, `hmac_scan.h`, `wal_scan.c:296-330` [W]):

* cfg80211's scan request is converted into `mac_scan_req_stru`:
  * `channel_nums` (count),
  * `ast_channel_list[]` entries `{band, chan_number, idx, en_bandwidth}` -
    **all requested channels in one request** (no chunking/cap in the host),
  * `us_scan_time` = `WLAN_ACTIVE_SCAN_TIME` or `WLAN_PASSIVE_SCAN_TIME`,
  * `max_scan_cnt_per_channel` (foreground scan: `FGSCAN_SCAN_CNT_PER_CHANNEL 2`
    for the mesh path; the field is present in the request),
  * `need_switch_back_home_channel`, `scan_func`, `is_p2p0_scan`, cookie,
  * the event is `WLAN_CTX / SCAN_REQ` and is sent via `hcc_hmac_tx_control_event`.
* Scan results arrive as `WLAN_CRX / SCAN_RESULT` and completion as
  `WLAN_CRX / SCAN_COMP` events (not data frames) - consistent with our scan
  results being D2H messages [L].
* AP scan special case: when hostapd asks for a scan while the AP VAP is still
  in `MAC_VAP_STATE_INIT`, the host moves the VAP to `AP_WAIT_START` and
  programs the RX filter around it [W] - the vendor explicitly handles
  "scan-before-AP-start"; our project instead found that **AP start must come
  first** for the data path [L], which is not contradicted (different context:
  DP setup vs scan).

**What this says about 13 channels:** the host-side code has no 6/13 cap; the
limit lives in the DMAC scan engine. So the public material **does not offer a
workaround**; it offers the **field dictionary** to decode our own 520-byte
`g0 id0xC` scan request: count, per-channel list, per-channel scan time/count,
and a possible "max channels per request" field. The measured
`field10 -> channel count` behaviour in the H1 band-config [L] fits a
"count/pacing" field of this family; a static lane should map the luofu scan
request against `mac_scan_req_stru`/`mac_scan_channel_stru` and check whether
our builder ever fills a channel count/scan-time field at all (if the firmware
derives 6 from a zeroed field, that would be the ceiling). **No public evidence
that 13/13 is reachable**; consistent with the structural negative [L].

---

## 8. Part 2e - Hi3881 STA management flow (for the sta-join lane)

`hmac_sme_sta.c` [W] is the host STA SME (the luofu vendor driver does the same
in its own `hmac_sme_sta`/`hmac_mgmt_sta`; our sta-join lane already decoded
the luofu messages [L]). Flow:

* `hmac_process_scan_req` (`:428`) -> scan event (section 7) ->
  `hmac_handle_scan_rsp_sta` (`:621`) -> select BSS.
* `hmac_sta_initiate_join` (`:487`): `hmac_prepare_join_req` (`:244`) ->
  join event to DMAC (`hmac_send_connect_result_to_dmac_sta` at `:635` is the
  result-report side; the join config goes through the config/user-sync path).
* `hmac_sta_initiate_auth` (`:553`) -> auth frame via the **mgmt TX path**
  (same contract as section 4).
* `hmac_sta_initiate_asoc` (`:587`) -> assoc frame via mgmt TX.
* Responses: `hmac_handle_auth_rsp_sta` (`:730`),
  `hmac_handle_asoc_rsp_sta` (`:845`) -> `hmac_report_assoc_state_sta`
  (`:942`) and connect-result events to cfg80211 (`hmac_handle_*_rsp` /
  `hmac_report_connect_failed_result` `:702`).
* Disconnect/deauth are reported via host-context events
  (`DISASOC_COMP_STA` `hmac_mgmt_sta.c:2040` [W]).
* User sync to DMAC: `hmac_config_user_info_syn` (rates/bandwidth/protocol),
  and for STA the connect result is pushed to the DMAC
  (`hmac_send_connect_result_to_dmac_sta`) - matches the sta-join lane's
  `g0` WAL/connect-SME decode [L].

**For us:** the STA join sequence is the mirror image of the AP association
chain; it is **also blocked by the mgmt TX carrier** for auth/assoc frames. Do
not fire `sta_join=2` (raw host-pointer risk noted by that lane [L]) before the
mgmt carrier is resolved - the public source shows the STA frames use exactly
the same WLAN_CTX/MGMT carrier, so a wrong carrier would fault/timeout the same
way.

---

## 9. Synthesis - what I would do next (detail in RANKED-NEXT-TRIES.md)

**Association (the last blocker) is now a two-path problem, not a mystery:**

1. **T1 (P0): re-target `.mgmt_tx` to the WLAN_CTX/MGMT event carrier.**
   Static step first (no bench): decode the luofu vendor `hmac_wpas_mgmt_tx`
   `0x4050c` -> `0x3fe68` -> `hcc_msg_alloc`/`hcc_msg_tx` path and name the
   exact device message (class/id, header bytes, frame attachment, tx-control
   fields). Then implement it in the driver (replacing the WAL 0x01F2 post) and
   expect the g2 id13 status/cookie completion. Pass = hostapd sends auth rsp and
   the external client moves past "SCANNING/DISCONNECTED" to auth/assoc.
2. **T2 (P0/P1): post the DMAC RX filter at AP start/VAP up.** Static step: find
   the luofu sub-type/id for SET_RX_FILTER (sibling value table above) in the
   vendor `hmac_set_rx_filter_value` path; implement it fail-closed; pass = no
   page/mask regression and mgmt RX appears.
3. **T3 (P1): fire the mgmt-RX decode with a real client.** With T1/T2 in, run
   the AP + external client scan and confirm g1 id13 events (probe/auth/assoc)
   with a plausible 802.11 frame; only then expect association.
4. **T4 (P1): EAPOL on the data ring.** After association, run `dp_rx_drain`
   under the 4-way; expect data-ring completions with the 36-B header/MPDU; add
   the refill watermark from section 6.
5. **T5-T7:** scan field mapping (section 7), STA-join prerequisites
   (section 8), and source-hunt follow-ups (keep the Cudy GPL request warm; watch
   OpenHarmony for a PCIe/luofu family drop; manual grep.app/GitHub-code-search
   pass by a human browser if deeper public-code coverage is wanted).

**What does NOT change:** no public Hi5622/luofu driver or firmware means the
upstream track stays blocked [L]/[W]; the scan 13-channel ceiling stays
structural; the PCIe HAL/transport remains original work.

---

## 10. Licensing constraints (must-read before any code reuse)

1. **Project license:** `opensource/LICENSE` is **MIT** (Copyright 2026
   ShibbityShwab) [L]. The sibling driver code is **GPL-2.0** per-file
   (HiSilicon copyright) and the OpenHarmony repo additionally lists
   `./common/platform` as **Apache-2.0** [W]. **Do not paste GPL-2.0/Apache-2.0
   source into this MIT repo.** Facts, interfaces, struct *layouts* and protocol
   semantics learned from it are fine to use in original clean-room code; copied
   code text is not. If we ever intentionally adapt GPL-2.0 code, it must live in
   a clearly GPL-2.0-labelled file/module and the publication story changes -
   avoid unless unavoidable.
2. **Short quotations** of sibling source are used in this report as evidence
   (names, numbers, tiny fragments) - not as vendored code. Keep it that way in
   future reports.
3. **hi1105 mirror is unofficial** (headers stripped/GBK, no per-file license
   text in some files) - treat as a structural reference only; prefer the
   OpenHarmony `hi3881v100` copy for anything needing clean provenance.
4. **Cudy/Jeton GPL obligation** is still unmet by the vendors: Cudy WR3000 2.0
   GPL section exists but is empty [W]; Jeton has no GPL link [W]. The project's
   GPL request remains the correct pressure point; do not imply we have source.
5. **No vendor binaries/blobs/firmware/calibration** were fetched or committed by
   this lane; the fetched *public GPL source text* lives only in the gitignored
   `build/wifi-public-research-2/fetched/` and is reproducible from
   `SOURCES.tsv` (URL + sha256). The committed report contains only URLs,
   hashes, line references and short factual quotes.
6. **Do not copy the committed vendor disassembly** into new tracked text beyond
   the existing reference file; cite addresses as this lane did.

---

## 11. Checked and negative / limitations

* No public source for Hi5622V100 / Hi5671YV200 / luofu / the PCIe HAL; no
  datasheet, register map, SDK drop, or RE writeup found (EN/RU/CN searches)
  [W].
* OpenHarmony contains only the SDIO hi3881v100 driver under
  `common/platform/wifi` [W]; recursive-tree API returned 504 (gateway
  timeout) - per-directory listing used instead; that's the authoritative answer
  for the chip directory question.
* Cudy WR3000 2.0 GPL: page has an empty hidden GPL block on pages 1 and 2, no
  archive/cloud links [W]. Firmware downloads exist (2.4.15).
* Jeton AX3000 Core: no GPL/source link [W].
* Mainline Linux: no HiSilicon Wi-Fi driver [W].
* `grep.app` public code search blocked by a Vercel checkpoint (no API results)
  [W]; GitHub code search needs auth - **not checked**, so "no public code"
  claims rest on the repo/namespace searches above, not on a global code search.
* `git.kernel.org`/raw GitHub rate-limited two files transiently; retried and
  51/51 verified.
* The luofu 48-byte RX-info layout and the exact luofu `hcc_msg` ids remain [U]
  locally; the public source gives semantics, not luofu ids. The static lane
  (task-151) should finish that mapping.

---

## 12. Reproduce this research

Local evidence (gitignored, not committed):
`build/wifi-public-research-2/fetched/{hi3881,hi1105,linux,oh,web}/`.

```sh
# 1. verify the fetch manifest (all rows with raw.githubusercontent URLs)
cd <repo>/build/wifi-public-research-2
#    SOURCES.tsv rows: url <TAB> bytes <TAB> sha256 <TAB> local evidence path + first line
#    VERIFY-fetch.txt: result of a full re-download at the pinned commits
# 2. spot-check a key file
sha256sum fetched/hi3881/mac_hmac_hmac_mgmt_ap.c fetched/hi3881/mac_hmac_hmac_mgmt_bss_comm.c
# 3. pins used
#    OpenHarmony: 7c1e2d5b35c090009bbe195f09337701c60af032
#    hongmengkernel: 297b33b07b56adb3fa53baf618fdbc16ad9fd447
#    torvalds/linux: 4dce97c6c265544342e716e62fd24bbb232590c5
```

Committed (this directory): `REPORT.md`, `RANKED-NEXT-TRIES.md`,
`SOURCES.tsv`, `VERIFY-fetch.txt`, `README.md`.

---

*End of report. Every public-source claim above is traceable to a row in
`SOURCES.tsv`; local claims are marked [L] and belong to the lane reports /
ledger they cite.*
