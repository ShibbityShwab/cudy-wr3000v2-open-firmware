# RANKED-NEXT-TRIES - wifi-public-research-2 (task-152)

Actionable list for the Lead. Evidence references are sections of `REPORT.md`
and rows of `SOURCES.tsv`; [L] = local committed artifact, [W] = verified public
source, [I] = inference, [U] = unknown.

**One-line strategy:** the client-association blocker is almost certainly the
**mgmt-TX carrier** (we post a host-internal WAL request at the firmware) plus a
**missing DMAC RX-filter program**; fix those two before spending more bench time
on the RX ring, and expect management frames on the **g1 id13 event path**, not
on the MAC rings.

---

## T0 - housekeeping: correct the ap-mgmt lane's carrier assumption (static, doc-only, P0)

* **Why:** the current association module builds the cfg80211 `.mgmt_tx` on the
  premise that the WAL cfg-event TLV `0x01F2` (32-B WAL envelope, HCC g2 id0) is
  the device-facing message [L]. The sibling source shows `0x01F2` is the
  host-internal WAL->HMAC request; the device message is a WLAN_CTX/MGMT event
  (REPORT section 4). The PD4 table-A absence of 0x01F2 [L] is expected.
* **Action:** add a correction note to `docs/incoming/wifi-ap-mgmt/LADDER-MGMT.md`
  + `REPORT.md` (M2 rung re-target), and mark the final association module's
  `mgmt_tx=1` path as **unvalidated/wrong-carrier** in the next status update.
  Do not fire the M2 ladder as written.
* **Owner:** the lane that owns wifi-ap-mgmt docs (Lead can assign); no bench.

## T1 - mgmt-TX wire format from the luofu vendor disassembly, then re-implement (static -> bench, P0)

* **Hypothesis [I]:** the luofu device-facing mgmt-TX message is the local
  analog of the sibling `FRW_EVENT_TYPE_WLAN_CTX / DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`
  event: a small control/HCC message carrying a TX-control block
  (`need_rsp`, `mgmt_frame_id` cookie, `is_vipframe`, `us_tx_user_idx =
  INVALID`) + 802.11 MAC header/frame; completion = CRX "mgmt tx status" event
  (our g2 id13 family, status+cookie).
* **Static step (no bench):** decode, in the committed
  `docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis`:
  1. `hmac_wpas_mgmt_tx` @ `0x4050c` -> helper @ `0x3fe68` -> `hcc_msg_alloc`
     / `hcc_msg_tx` call sites; name the message class/id (what does
     `hcc_msg_alloc`'s first argument encode? which header bytes are copied
     from the mgmt-tx struct/VAP?);
  2. the same wrapper in `hmac_mgmt_reset_psm` @ `0x4039c` (payload sub-type
     byte +6 = 14) and `hmac_tx_mgmt_send_event` @ `0x40384` for comparison;
  3. the TX-control struct layout and which fields the mgmt path sets
     (sibling: `need_rsp/is_vipframe/is_needretry/mgmt_frame_id/mac_head_type=1/
     frame_header_length=24/us_tx_user_idx=invalid`);
  4. the device completion path (search the D2H handler table for the
     mgmt-tx-status message; compare with the already-decoded g2 id13
     `{status, cookie}` [L]).
* **Implement (host-side, fail-closed):** replace the WAL-0x01F2 post in
  `hi5622_mgmt_logic`/`hi5622_mgmt.c` with a builder for that message; keep
  `mgmt_tx=0` default, `mgmt_tx=1` opt-in; copy the frame into a
  `dma_alloc_coherent` buffer in the device window (VMSPLIT-3G constraint
  already documented by the lane [L]); no key material in logs; latch
  "one post per load", no retry, stop on page/mask/staging anomaly.
* **Bench rung (Lead fires, after static proof):** on a fresh golden boot:
  AP 5-message order -> hostapd AP-ENABLED -> external client sends auth ->
  expect dmesg "mgmt-tx: built/sent <class> len=..", **one consumed
  announcement**, then a **g2 id13 status event with the cookie**; hostapd should
  log auth/assoc progress. PASS = status event received and the client (wpa
  supplicant) advances; FAIL = page/mask/exception (revert) or no status event
  (decode the completion message from the vendor module before retrying).
* **Risk:** medium (new message shape). Gate every rung on page 0x86807FEF golden
  / mask 0 / staging 0; fail-closed default; one shot per load.

## T2 - DMAC RX filter program (static -> bench, P0/P1)

* **Hypothesis [I]:** the luofu DMAC needs a SET_RX_FILTER WLAN_CTX event from
  the host; without it the firmware default filter may drop management classes
  (notably broadcast mgmt / probe req, BIT4), so our mgmt RX path never gets
  traffic. Sibling: AP-UP single-VAP value **0xF7B9FAEA**, AP+STA-up
  **0x73B9FACA**, AP/STA mixed values; 0 = keep; support_any clears BIT4
  (`hmac_rx_filter.c:184-436` [W]; REPORT 5.2).
* **Static step:** locate the luofu analog of `hmac_set_rx_filter_value` in the
  vendor module/disassembly (string grep for the event; the raw `hcc_msg`
  wrapper seen in T1 is likely reused) and get: the WLAN_CTX sub-type/id for
  SET_RX_FILTER plus the device->host confirmation (if any). Also check whether
  the vendor posts it at AP start / VAP-UP and in which order relative to the
  5-message AP order.
* **Implement:** fail-closed param (e.g. `rx_filter=0/1`), post once at
  AP-ENABLED (and optionally on VAP restart/stop), log old/new value; no
  retries; stop on anomaly.
* **Bench rung:** AP-ENABLED with `rx_filter=1` -> expect exactly one
  "rx_filter event sent (0xF7B9FAEA)" consumed 0 polls, page/mask clean; then run
  T3. PASS = no regression + T3 traffic appears.
* **Risk:** low (single u32 payload) but wrong id = no-op or firmware error;
  candidate for pairing with T1 in one fire (different messages, independent
  params) or separate rung first.

## T3 - mgmt-RX verification with a real client (bench, P1; needs T1/T2 or at least T2)

* **Hypothesis [W]/[L]:** client probe/auth/assoc arrive as DMAC CRX events ->
  our D2H g1 id13 decoder (60-B prefix; 48-B RX info; frame after; channel +7,
  len +17, RSSI +39), then cfg80211_rx_mgmt. **Ring drain 0 is expected.**
* **Rung:** fresh boot -> AP 5-message order -> AP-ENABLED with `mgmt_rx=1`
  (and `rx_filter=1` if landed) -> external laptop scans ch6 (and
  `wpa_supplicant` auth attempt). Capture dmesg: expect
  "mgmt rx: len=..., chan=6, rssi=..., fc=0x0040/0x00b0..." lines and
  hostapd seeing probe/auth (if hostapd subscribed to mgmt_rx). PASS = >=1 valid
  management frame decoded with plausible FC/SA/BSSID; record frame classes.
* **If nothing appears:** escalate in this order: (a) filter not programmed
  (T2), (b) g1 id13 is not the luofu mgmt-RX carrier - decode the vendor
  `hmac_send_mgmt_to_host` @ `0x42454` message construction vs the committed
  D2H ring dump and reconcile, (c) firmware state machine gate (AP must be UP;
  our ordering finding says AP start first [L]). Do **not** repeat the same rung
  without a change.

## T4 - EAPOL/data-ring capture + refill watermark (bench, P1; after T1/T3 give association)

* **Hypothesis:** once a client is associated, EAPOL msg2/4 are data frames and
  must show up as MAC RX-ring completions (our live bank/drain), giving the
  missing 4-way evidence; mgmt-path progress is not enough.
* **Rung:** association + wpa_supplicant connect -> after client sends EAPOL,
  `echo 1 > dp_rx_drain` -> expect drained>=1, capture header valid, MPDU prefix
  plausible, EAPOL ethertype 0x888e visible in the payload; hostapd 4-way
  progress. Also check refill: norm/small free-ring producers advance; no
  rx_failed.
* **Design note from Linux cross-check (REPORT 6):** add a minimum-free
  watermark/refill rule (e.g. keep >= 3/4 of the ring posted, ath11k-style;
  iwlwifi's RX_STALLED is the failure mode to avoid) before sustained traffic -
  otherwise completions may stall under load.
* **Risk:** medium (RX lane writes ring pointers; already bounded/fail-closed).

## T5 - scan-field mapping for the 13-channel question (static, P2; optional bench)

* **What public source gives:** sibling scan request = one event with
  `channel_nums` + per-channel `{band, chan_number, idx, bandwidth}` +
  `us_scan_time` (active/passive) + `max_scan_cnt_per_channel`
  (`hmac_sme_sta.c:116`, `hmac_scan.c:1673`, REPORT 7 [W]). No host-side cap;
  the ceiling is DMAC-side.
* **Action:** map our 520-B `g0 id0xC` scan request field-by-field against
  `mac_scan_req_stru`/`mac_scan_channel_stru`; check whether our builder
  fills a channel-count / per-channel-dwell field (a zero count could explain
  the 6-channel stop), and whether H1 `field10` (13 -> 7 ch, 25 -> 8 ch [L]) is
  the same semantic field (count or scan-time). If a count/dwell field is found,
  one bench rung could test 13 entries directly - but do not promise 13/13 (the
  structural analysis [L] stands until contradicted).
* **Priority note:** below association; only if scan matters for the final
  story.

## T6 - STA-join ahead-of-time checks (static, P2; do NOT fire sta_join=2 yet)

* **Why:** the sibling STA SME uses the **same WLAN_CTX/MGMT carrier** for
  auth/assoc frames (REPORT 8 [W]); with the wrong carrier, `sta_join=2` would
  fault/timeout exactly like the AP path. The sta-join lane's raw host-pointer
  risk [L] is separate.
* **Action:** after T1, re-check the sta-join ladder prerequisites; the join
  frame's device-pointer fields are the same DMA-window problem. Keep
  `sta_join=0` default.

## T7 - public-source/source-hunt follow-ups (external, P3)

* Keep the Cudy GPL request open (WR3000 2.0 page has an empty GPL section [W]).
* Watch OpenHarmony branches/mirrors for a PCIe/luofu-family drop (current
  `common/platform/wifi` has only hi3881v100 [W]); Hi1105/Hi1152 PCIe siblings
  are the closest public architecture references (Hi1105 mainline PCI ASPM quirk
  exists [W]).
* Manual (human browser) `grep.app` / GitHub code-search pass for
  `hi5622`/`luofu`/`hmac_wpas_mgmt_tx` is the only gap this lane could not
  close (grep.app blocked by a security checkpoint [W]).
* Do not expect a driver drop to change the upstream story without the firmware
  blob/GPL source.

---

## Do-not-do list (learned this lane)

1. Do not post the WAL cfg TLV `0x01F2` at the firmware again and call it
   mgmt-TX (host-internal request, REPORT 4).
2. Do not chase probe requests in the MAC RX rings/ring drain; they are CRX
   mgmt events (REPORT 5). Ring drain is for EAPOL/data after association.
3. Do not fire the ap-mgmt M2 ladder unchanged.
4. Do not copy GPL-2.0 sibling code into this MIT repo (REPORT 10).
5. Do not present the 6->7/8 channel H1 result as fixable from public material;
   the evidence still says DMAC-structural.
