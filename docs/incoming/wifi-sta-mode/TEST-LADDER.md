# TEST-LADDER - STA (client) mode: join + host-SME auth/assoc + CRX completion

**Lane:** `wifi-sta-mode` (task-155), host-only. **Written:** 2026-11-25.
**Status: designed, NOT fired.** The Lead fires; this lane never touches the
bench router. The module is host-built and verified only.

Safety context (BENCH-FACTS / STATUS): UART is read-only; recovery door armed
before any fire; one attended session per boot; the golden boot page/mask/
staging rules below are hard stop conditions.

---

## 0. Topology and RF preconditions

The router joins an **external AP** (the router is the client). Choose the AP
side by what is available and controlled:

| option | how | notes |
| --- | --- | --- |
| A (preferred) | host laptop Wi-Fi card as a software AP (`hostapd` open first, WPA2-PSK second) on ch6 | fully controlled, no phone flakiness, PCAP/observer available |
| B | a second router / dedicated test AP, open then WPA2 | keep BSSID/freq recorded |
| C | phone hotspot (`slowphone` in the older ladder) | last resort: hotspot power saving/scan behaviour adds variables |

**RF precondition (current bench constraint):** the router currently has no
antennas attached -> ~44 dB path loss; the laptop MT7922 has also been flaky.
Attach antennas first; keep AP and router in the same room, <=2 m, and
verify the AP is visible from a normal client before firing. The mgmt rungs
(B2/B3) are the first on-air evidence; do not chase data-rung failures before
the CRX auth/assoc evidence exists.

Record for every rung: BSSID, channel, SSID, AP security, module sha256,
dmesg, HCC events, page/mask/staging, and host-side observer capture if
option A/B.

---

## H - host rungs (no device; reproduce before booking a fire)

| rung | action | pass |
| --- | --- | --- |
| **H0** | `gcc -std=c99 -Wall -Wextra -Werror -pedantic ... tools/sta_mode_selftest.c src/hi5622_sta_sme.c && ./a.out` | `checked 140, failed 0` |
| **H1** | `./tools/build-sta-mode-module.sh` | 0 warnings; 58 params; stripped sha256 `8c0adf93...`; 14 cfg80211 symvers; `.text` strip-identical |
| **H2** | patch dry-run: association tree + sibling-gpl 0001 + sta-join 0001 + this 0001 | applies with no rejects/orig files |
| **H3** | confirm no vendor binary/firmware/calibration/secret in this lane dir | `git ls-files opensource/docs/incoming/wifi-sta-mode` only text |

---

## B - bench rungs (Lead fires, attended, recovery armed)

Common boot ladder for every rung (proven; see wifi-transport-port /
wifi-stability docs): deploy cfg80211-enabled image + module, insmod, then
`prepare/upload/verify/seed/hcc_arm/hcc_loop/release 0x5A5A`, wait for the
golden page and the ready pair, then the C2 sequence (ctx_create type 1 ->
txq_sync72 -> six calibration messages). **Every rung starts from a fresh
boot and a fresh module load** (sta_join/sta_connect are 0444: one connect
per load, never flipped live).

Reference load for STA rungs (AP-only params kept inert; adjust
`allow_frames` to the session budget, >= 8 for a live connect):

```sh
insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
    fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
    allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
    hcc_poll_ms=5 hcc_trace=1 rx_enable=1 \
    sta_join=0 sta_connect=0 sta_connect_timeout_ms=4000 \
    ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
    mgmt_tx=1 mgmt_rx=1 sta_ops=0 eapol_rx=0 data_tx=1 data_tx_peer=1 \
    data_tx_protect=0 data_tx_timeout_ms=200
```

STOP RULES (hard, from the bench pack): page CA `0x1BF000` must stay
`0x86807FEF / 0x7 / 0x2 / 0xABCD`, state CA `0x118B60 = 1`, exception mask
CA `0x400008 = 0`, staging CA `0x40000C = 0`. Any deviation, any exception,
any mask/staging nonzero -> **power-cycle, no retry**. No repeated connect in
one load. Never write the CA exception-mask page; reads only.

| rung | setup | expected evidence (pass) |
| --- | --- | --- |
| **B0** | module load (sta_join=0/sta_connect=0) + full boot ladder + C2 | golden page/state, ready pair id1+id2, **no new posts**, wiphy registered, iface add works |
| **B1 (STA iface)** | `iw phy phyX interface add wlan0 type managed; ip link set wlan0 up` | wlan0 UP/LOWER_UP, `iw dev wlan0 info` shows type managed |
| **B2 (dry)** | reload with `sta_join=1 sta_connect=1`; boot+C2; `iw dev wlan0 connect <ssid> freq 2412 bssid <AP MAC>` | dmesg: join frame built+validated (dry run), `sta mode DRY: auth request 30 B and assoc request ...`; **nothing posted** (frame counters unchanged); page/mask clean |
| **B3 (live mgmt)** | reload with `sta_join=2 sta_connect=2 allow_frames>=8`; boot+C2; iface; `iw dev wlan0 connect <ssid> freq 2412 bssid <AP MAC>` (open AP) | join post consumed; auth request post consumed; CRX auth response line; assoc request post consumed; CRX assoc response line; `cfg80211_connect_done(status=0)`; `iw dev wlan0 link` shows Connected; AP-side observer shows STA associated; page/mask clean |
| **B4 (data)** | after B3: ping AP/gateway; then optional `wpa_supplicant` WPA2-PSK connect instead of open | data RX ring completions in `dp_rx_drain` (EAPOL/ARP/ping), TX completions advance; WPA2: 4-way handshake complete (EAPOL msg2/4 on the ring + msg1/3 via ndo_start_xmit), key install lines |
| **B5 (disconnect)** | `iw dev wlan0 disconnect` after B3/B4 | WAL 0x00d0 post consumed, `cfg80211_disconnected` reported, state off, page/mask clean; no retry (one disconnect per load) |
| **B6 (negatives)** | (a) `sta_connect=0` default: connect refused, no posts; (b) `sta_join=2 sta_connect=1`: -EINVAL; (c) connect to a non-present BSSID: no CRX response -> bounded timeout, `cfg80211_connect_done(status=-1, timeout_reason=AUTH/ASSOC)`; (d) corrupt/other-BSSID response: refused by the SME (rx_bad) | each fails closed with a distinct log, page/mask clean |

### Grep-able expected dmesg (feature strings)

```
sta mode: host-SME connect started (... g0 id6; waiting for the CRX auth response ...)
sta mode: auth response OK -> assoc request posted (state assoc_sent)
sta mode: assoc response OK (... cfg80211_connect_done(status=0) on the CRX path ...)
sta mode: connect timeout (state failed, timeout_reason=...) - failing the connect
sta mode: disconnect reported to cfg80211 (...)
```

### Capture set per rung

module sha256 + params, full dmesg (`hcc_trace=1`), HCC events/direct-reader
stats, `cfg80211_status`, frame-post counters (`frames_posted/refused`),
page/state/mask/staging read pack, AP-side log/observer capture, and the exact
`.connect` request (`iw` line / wpa_supplicant config with the PSK
placeholder, never a real secret in tracked text).

---

## Not-fired statement

This lane ran **no device session**: no module load, no register read/write,
no frame on air. The ladder above is the handoff for the Lead's fire.
