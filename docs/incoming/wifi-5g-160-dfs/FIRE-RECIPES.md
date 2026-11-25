# FIRE-RECIPES - the GATED 160 MHz and DFS rungs

Lane wifi-5g-160-dfs (task-205). **The Lead fires; this lane never touches the
device.** Every rung below is **GATED on the 5 GHz band-1 MAC start** (H2D g0
id1 key 0x0242, body {01,00,01,00,00}, field B = the fresh band-1 module
context; ../wifi-band1-start/, tasks 197/206): until the on-air gate passes
(observer scan on the 5 GHz channel finds the SSID and the page stays
0x86807fef/7/2/ABCD with state 1, mask 0), no rung may run. A 160/DFS start
before the beacon gate tests nothing.

Bench rules for every rung: bench link PASS, recovery door READY, UART
read-only (tools/uart-capture.sh), one AP per module load, no blind retries,
power-cycle on exception mask or page change, never write CA 0x400392F0, never
read 0x10160000-0x1016FFFF, no rmmod of the vendor modules, no flash from this
lane.

## Rung 0 - the shared prerequisites (read-only, no AP)

P0.1 (bench, read-only): the band-1 start gate evidence is current (LEDGER:
the 0x0242 rung PASS, observer sees the 5 GHz SSID).
P0.2 (bench, read-only): 'iw reg get' -> country US: DFS-FCC; 'iw phy phy0
info' shows the 5 GHz band with ch52-64/100-140 (radar/NO-IR) and the VHT cap
word 0x400064 in the wiphy dump (the driver's cfg80211_status/iw output).
P0.3 (host): run ../wifi-fw-rf-programme readback pack - R = the RF block
pointer chain; the ch36-64 image marker R+0x3AC == 0x03E6138A (expected after
the F2 fire) and R+0x410 bit0 = the CA 0x101890 predicate. This is the
baseline for the 160 rung. Stop if R+0x3AC is 0.
P0.4 (host, optional): P-160a probe (PROBES.md) to pin the 160 selector value
before rung 160-2; without it 160-2 is an experiment, not a validation.

## A. 160 MHz rungs

160-1 - VHT80 on ch36 (isolate the width path from 160):
  Load: band5g=1 vap5g=1 desc_table=1 dfs_mode=1 ap_chan_wait_ms=300
        (he_caps=0; the new ht_bw=1 once the driver patch lands).
  Run: hostapd/hostapd-160.conf with vht_oper_chwidth=1 and
       vht_oper_centr_freq_seg0_idx=42 (ch36 80 MHz); SSID open, ch36.
  PASS: AP-ENABLED; observer scan on 5180 sees the SSID and the VHT
  Operation IE with channel width 1 (80 MHz); page golden; no exception.
  Read: module ctx+0x12 (0x119768) bit: should read the 80 selector (2 [I]).
  FAIL: ap_chan frame refusal, page/mask change -> power-cycle, no retry.
160-2 - VHT160 on ch36 (CCFS1 = ch50):
  Load: same, with the patched driver (ht_bw=1).
  Run: hostapd/hostapd-160.conf (vht_oper_chwidth=2,
       vht_oper_centr_freq_seg0_idx=50), channel=36.
  PASS: AP-ENABLED on ch36 with the 160 chandef accepted (hostapd logs the
  HE/VHT Operation with width 2); observer sees the SSID and a VHT Operation
  IE with channel width 2 / CCFS1 50; page golden. The RF predicate readback
  (R+0x410 bit0) is allowed to be 0 - the descriptor subsystem is inactive in
  the shipped image (../wifi-fw-rf-programme C4); the width proof is the
  on-air IE + the module ctx+0x12 selector read.
  FAIL: cfg80211 refuses the chandef (then the wiphy/cfg80211 flag path is
  wrong - stop), ap_chan refusal, page/mask change.
  Note: ch36-64 includes DFS channels 52-64; in US a 160 MHz AP whose span
  includes DFS channels requires the DFS/CAC machinery. If the observer/rung
  160-2 is launched before the DFS rung, the span must be treated as
  non-DFS-clean and the rung is a capability experiment only; the clean
  160 MHz rung is on 100-128 after DFS-1/DFS-2 pass, or on 149-177 if the
  driver advertises it (it does not today).
160-3 - HE160 on ch36 (after 160-2 PASS and he_caps=1): same as 160-2 with
  ieee80211ax=1 and he_oper_chwidth=2; PASS = HE Operation + VHT Operation
  width 2 on air.

## B. DFS rungs

DFS-0 - read-only prerequisites: as P0.1-P0.2, plus confirm the stock carrier
country path (the image #3 loader sets ap.country=US; the wifi-regdom-fix
cfg80211 is in the image).
DFS-1 - CAC + AP on ch52 (60 s CAC):
  Load: band5g=1 vap5g=1 desc_table=1 dfs_mode=1 ap_chan_wait_ms=300,
        with the patched driver (dfs_cac=1) and hostapd/hostapd-dfs.conf.
  Run: hostapd -dd -t on wlan5 with channel=52; hostapd requests radar
  detection after start; the driver sends 0x07E7{1}, runs the 60 s CAC timer,
  sends 0x07E7{0}, cfg80211_cac_event(FINISHED).
  PASS: hostapd log shows the CAC started (NL80211_RADAR_CAC_STARTED) and
  finished ~60 s later; AP-ENABLED; observer scan on 5260 sees the SSID and a
  beacon; page golden. Read: band+0xED1/0xED2 (counter/max), band+0xDC0 bit1
  (CAC active -> clears), band+0xED3/0xED4 (CAC band/channel). 0x07E7
  consumption visible in the H2D trace/files.
  FAIL: no 0x07E7 on the wire after the radar-detect request (op not wired or
  gate refused), CAC never finishes (timer path), AP-ENABLED but no beacon
  (then the blocker is the band-1 MAC start, not DFS) - stop and report.
DFS-2 - radar detection (NOT READY): the radar D2H event dispatch id and a
  safe radar injector are not available (REPORT-DFS.md open item). Do not
  fire a radar path blind. The radar-tool strings (dfsenable/cacenable/
  set_radar_th...) and the firmware radar-detect registers are decoded; a
  future rung can use the vendor radartool command only after the D2H mapping
  and a safe test source exist.

## Global stop rules

* Page 0x86807fef/... or state != 1 or exception mask != 0 -> power-cycle,
  no retry, collect the page/state/mask and stop the session.
* A rung that posts no frame must be explained before the next rung; never
  shot-gun frames.
* One new variable per rung (width selector in 160-2; CAC in DFS-1; radar
  later).
* Every write is logged; the user-visible outcomes are the observer beacon/IE
  and the read-only readbacks - not a driver log line.
