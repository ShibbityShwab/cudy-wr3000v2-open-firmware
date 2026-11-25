# REPORT-DFS - the vendor DFS/CAC machinery for the 5 GHz AP

**Lane:** wifi-5g-160-dfs (task-205), 2026-11-28. **HOST-ONLY** static decode;
no device contact, no fire. **The Lead fires.** Grade markers as in TABLES.md.

**DEPENDENCY: DFS/CAC is downstream of the 5 GHz band-1 MAC start.** A DFS AP
cannot CAC-then-beacon while the band-1 MAC is not started: the CAC completion
ends in hmac_start_all_bss_of_band, and the beacons still need the band-1
TBTT arm. The missing rung is H2D g0 id1 key 0x0242 (worker CA 0x4C524, body
{01,00,01,00,00}; ../wifi-band1-start/, task-197; wifi-band1-start-send,
task-206). Every DFS rung is GATED on the on-air beacon gate.

## 1. Result

1. **The CAC control message is H2D config event 0x07E7 [V].** The vendor
   hmac_dfs_cac_start (0xAD420) sends key 0x07E7 with a 1-byte body {01}
   (movw r1,#0x7e7 at 0xAD478; the byte set at 0xAD47C). The firmware worker
   CA 0xB58FC requires the per-module state byte [ctx+17]==1, checks the
   per-link pointer and then either arms six radar-detect registers (bit0 = 1
   of each u16) and sets its CAC-active flag obj+0x4A5=1, or disarms them and
   clears the flag - exactly the {1}=start / {0}=stop semantics.
   hmac_dfs_cac_stop (0xACD2C) and the CAC timeout completion
   (0xAE584 at 0xAE688) send the same key with body {0};
   hmac_config_cac_sync_state (0xAD1D4) is the generic 1-byte 0x07E7 sender.
2. **CAC timing [V]:** hmac_dfs_init (0xAC588) sets the per-band CAC time
   band+0xD38 = **60000 ms** and the TDWR/weather-window CAC time band+0xD3C =
   **600000 ms** (the 5600..5650 MHz window computed in
   hmac_dfs_channel_list_init; the start path also special-cases regdomain
   types 2/5). The non-occupancy period is band+0xD58 = **1800000 ms** (30
   min), the NOL list head is band+0xEA0/0xEA4. hmac_dfs_cac_start compares
   the CAC time with 59999: >59999 -> max = (cac_time * 0x45E7B273) >> 46
   (== cac_time/60000 floor) and a **63000 ms** timer; <=59999 -> a
   **60000 ms** timer. hmac_dfs_cac_timeout_fn increments band+0xED1,
   re-arms 60000 ms per tick, and on counter == band+0xED2 completes: sends
   0x07E7{0}, clears/iterates the extra channels, calls the FSM and
   **hmac_start_all_bss_of_band** (beacons start), unregisters the timer,
   clears band+0xDC0 bit1 and reports hmac_report_cac_finish. So the CAC is a
   HOST timer (vendor hcc_timer) + firmware radar-detect arming, not a
   firmware timer: our driver must run the timer.
3. **CAC state is per band [V]:** band+0xDC0 bit1 = CAC active; band+0xED3 =
   CAC band, band+0xED4 = CAC channel (stored by hmac_dfs_cac_start);
   band+0xED1/0xED2 = tick counter/max; band+0xEA8 = radar-detect counter and
   band+0xEAC+4*type = per-type counters (type 0..8), all in hmac_dfs_init's
   40-byte zeroed block at band+0xEA8.
4. **Radar detection path [V]:** the radar decision is made by the firmware
   PHY/MAC radar detector (armed by 0x07E7{1}; the vendor also uses spectral
   scan/phy events: hmac_dfs_radar_detect_event calls hmac_phy_event_stop and
   the radar enable/spectral functions shuangta_set_dfs_* exist in the host
   module). A radar hit reaches the host as a D2H event dispatched to
   **hmac_dfs_radar_detect_event (0xAC7C0)**: it parses the message payload
   (band = byte[1] >> 4, object index = byte[3], radar type = byte[12] with
   per-type counters for type <= 8, details bytes [13],[14],[15],[17]),
   increments the counters, and calls the AP FSM (hmac_fsm_call_func_ap
   event 4 arg 2 = radar detected). The downstream handlers are
   hmac_dfs_ap_up_radar_handler 0xAE218, hmac_dfs_switch_channel_for_radar
   0xADBCC, hmac_dfs_radar_wait 0xAD7A4, hmac_dfs_nol_timeout_fn 0xAB7FC,
   plus the channel-switch path hmac_chan_switch_for_dfs_report_entry
   0xAD9BC. **The exact D2H event id/group and the payload byte layout are
   NOT yet mapped by a committed lane** ([U]; it needs the host event-dispatch
   table decode or a live trace). For the first rung the driver can do
   CAC-only (arm/disarm 0x07E7, run the 60 s timer, report CAC finished) and
   leave the radar D2H path as the second step.
5. **Host-side DFS checks [V]:** wal_cfg80211_set_chan_dfs_check (0x102980)
   checks the per-channel status byte (band+0x32A+idx*4+3 & 0x7F against 4 =
   usable) and the extended-channel list before a channel set;
   wal_cfg80211_chan_dfs_check_resume (0x102B44) runs the check, then sends a
   manual 0x0124 channel event (key at 0x102F04) - the vendor's own
   post-DFS channel re-post. hmac_check_set_channel_radar_status (0x763D4) is
   called by hmac_config_set_channel before the 0x0124 send.
6. **The init'able UNII-2/2e channels [V]:** the firmware band-1 table CA
   0x117AD4 has 29 channels including **52-64 and 100-144** (plus 36-48,
   149-165, 184-196). hmac_dfs_channel_list_init flags bit0 = DFS channels
   and bit7 = the 5600..5650 MHz TDWR window (ch 100-128) per regdomain, so
   the DFS set the vendor can initialise is **{52,56,60,64} U {100,104,108,
   112,116,120,124,128,132,136,140,144}** = 16 channels. Our driver advertises
   only 36-48/52-64/100-140 today (no ch144) and marks 52-64/100-140 with
   NO_IR|RADAR|INDOOR_ONLY (image #3 module tree hi5622_cfg80211.c); ch144
   must be added for the full vendor set (HOST-IMPLEMENTATION.md).
7. **Hostapd DFS config [V for the parser, I for the bench effect]:** hostapd
   v2.12 with a DFS-capable driver enables DFS when ieee80211h=1 (the stock
   OpenWrt hostapd.sh sets ieee80211h=1 when hw_mode=a and doth>0 -
   /lib/netifd/hostapd.sh:174). The AP start on a DFS channel then runs
   hostapd_handle_dfs -> nl80211 radar detect -> our driver's
   start_radar_detection op -> cfg80211 CAC state machine; the driver later
   calls cfg80211_cac_event(NL80211_RADAR_CAC_FINISHED), hostapd proceeds to
   enable the AP. A radar event is delivered as
   cfg80211_cac_event(NL80211_RADAR_DETECTED), which hostapd turns into the
   DFS channel switch. hostapd also needs a country/DFS domain:
   country_code=US, ieee80211d=1 (the project's cfg80211 US/DFS-FCC regdomain
   fix from the wifi-regdom-fix lane is the prerequisite; without the fixed
   cfg80211 the regdb rejects US and the channels come up NO-IR).

## 2. The vendor's DFS state machine (host, decoded skeleton)

    AP start on a DFS channel
      hmac_cac_check_and_start_bss(vap) 0x13FFA8
        if vap DFS enabled: hmac_dfs_init_scan_hook; on success ->
        hmac_dfs_cac_start(vap) 0xAD420
          send 0x07E7{1}
          band+0xED3 = vap band, band+0xED4 = vap channel
          cac_time = band+0xD38 (or band+0xD3C for the 5600-5650 window,
                      regdomain type 2/5 path)
          arm hcc timer: 63000 ms x max (max = cac_time/60000) or 60000 ms
          hmac_report_cac_start (HCC report)
        timeout ticks (hmac_dfs_cac_timeout_fn 0xAE584)
          counter++; if not max -> re-arm 60000 ms
          else: send 0x07E7{0}; channel cleanup; FSM; start all BSS of band;
                unregister timer; clear band+0xDC0.bit1; report_cac_finish
      radar event (D2H) -> hmac_dfs_radar_detect_event 0xAC7C0
          counters; AP FSM event 4 arg 2
          -> hmac_dfs_ap_up_radar_handler / switch_channel_for_radar
             (CSA to a non-occupancy-cleared channel; NOL timer
             hmac_dfs_nol_timeout_fn)
      CAC abort: hmac_dfs_cac_stop 0xACD2C (0x07E7{0}, timer off, report)

## 3. Hostapd config and fire recipe (summary; details in FIRE-RECIPES.md)

hostapd/hostapd-dfs.conf: hw_mode=a, channel=52, ieee80211h=1, ieee80211d=1,
country_code=US, ieee80211n=1, ieee80211ac=1 (optional HE via ieee80211ax=1),
vht_oper_chwidth=0 (80 MHz DFS is the later rung), ssid, OPEN.

Rungs (all GATED on the band-1 MAC start / beacon gate):
* DFS-0 (read-only, no AP): confirm the channel list in the driver wiphy
  (iw phy ... info shows ch52-64/100-140 with radar/no-IR) and the corrected
  cfg80211 country (iw reg get -> country US: DFS-FCC).
* DFS-1: AP start on ch52 -> the driver arms 0x07E7{1}, hostapd shows
  "DFS: starting CAC" / NL80211_RADAR_CAC_STARTED; after 60 s the driver
  sends 0x07E7{0} and cfg80211_cac_event(FINISHED), hostapd logs
  "CAC finished" and enables the AP; the observer scan finds the SSID on
  5260 MHz. PASS: AP-ENABLED + observer beacon. FAIL: no CAC start, CAC
  never finishes, or AP-ENABLED without a beacon (then the blocker is the
  band-1 MAC start, not DFS).
* DFS-2 (radar): vendor radartool "set_radar_th"/"dfsenable" strings and the
  firmware test path are decoded, but no committed lane maps the radar D2H
  event or a safe radar injector; this rung needs the D2H decode + the
  driver's radar event wiring. [U] Do not fire it blind.

## 4. Open items / unknowns

* [U] the D2H radar event group/id and payload layout (the host handler
  CA 0xAC7C0 reads byte[1] high nibble = band, byte[3] = object index,
  byte[12] = radar type, bytes 13/14/15/17 = details; the dispatch id is not
  decoded). Decision probe: the D2H trace lanes' capture catalogue, or a
  live radar injection if a safe source is ever available.
* [U] the exact CAC-timer unit of the vendor hcc_timer API (ms assumed [I]);
  our driver uses ms directly, which matches every decoded constant.
* [I] off-channel CAC (hmac_dfs_off_chan_cac_*, band+0xD32/Band+0xED8) is a
  multi-AP/off-channel feature; not needed for the single-AP bench rung.
* [I] the vendor's 10-min TDWR CAC applies to the 5600-5650 window
  (ch 100-128); FCC CAC for the rest is 60 s. hostapd does not enforce the
  timing - the driver does.
