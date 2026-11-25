# PROBES - read-only probes for when the 5 GHz beacon lands

Lane wifi-5g-160-dfs (task-205). All probes are read-only. The BAR0/CA reads
use the committed readback helpers of ../wifi-fw-rf-programme/ (bar0read;
BAR0 address = 0x40000000 + CPU + 0x6B8000, and the helper refuses the RC misc
window 0x10160000-0x1016FFFF). CA addresses are the image-linear convention;
the boot's ctx0 base is pinned by the committed lanes (ctx0 = CA 0x119758;
band0 object = CA 0x15CA08, band1 object = CA 0x15E570) - if the readback does
not match, do not guess: stop and re-derive from the boot readback.

## A. 160 MHz probes

P-160a - pin the bandwidth selector value (the one open decode):
  After a KNOWN vendor/stock 160 MHz AP (or, on our bench, after 160-1/160-2
  if the selector is already patched): read the module channel word
  CA 0x119768 / BAR0 0x7D1768 (ctx0 + 0x10) = channel | band<<8 | bw<<16 |
  idx<<24 (../wifi-fw-chan-worker TABLES T2). The byte at bits 16-23 is the
  bandwidth selector; record it with the AP width (80 -> expected 2, 160 ->
  expected 3 [I]). This single value decides HOST-IMPLEMENTATION.md A1.
P-160b - capability: 'iw phy phy0 info' must show the 5 GHz band VHT cap
  with the 160 bit (0x400064) and the channel list; 'iw dev wlan5 info' the
  AP channel. Host-only parse.
P-160c - on-air width: the observer (../wifi-onair-observer/) scans the
  channel and parses the VHT Operation IE (channel width + CCFS1) and the HE
  Operation IE for HE160; expected 160-2: width 2 / CCFS1 50 for ch36-64, or
  width 2 / CCFS1 114 for ch100-128.
P-160d - RF readback: the ../wifi-fw-rf-programme R-block pack (R+0x3A8..
  R+0x418, R+0x410 bit0 predicate). Expected: the ch36-64 image marker
  R+0x3AC == 0x03E6138A; R+0x410 bit0 may stay 0 in the shipped config
  (descriptor subsystem inactive) - not a failure by itself.
P-160e - 0x0124 frame trace: the H2D trace (../wifi-rx-h2d-window/ or the
  driver's h2d trace) shows the 0x0124 key, len 4, body {band, bw, channel, 0};
  record the bw byte for P-160a.

## B. DFS probes

P-DFS1 - hostapd DFS state: 'hostapd -dd' log lines for CAC start/finish and
  the DFS channel switch; nl80211 monitor (if available) shows
  NL80211_CMD_RADAR_DETECT and the two CAC events.
P-DFS2 - CAC object readback (after the 0x07E7{1} post, during CAC):
  band1 object CA 0x15E570: band+0xDC0 (CA 0x15F330 -> BAR0 0x7CF330) bit1 = 1
  while CAC runs; band+0xED1/0xED2 (CA 0x15F441/0x15F442 -> BAR0
  0x7CF441/0x7CF442) = tick counter/max; band+0xED3/0xED4 (CA 0x15F443/
  0x15F444) = CAC band/channel. After completion: bit1 = 0 and the timer is
  unregistered.
P-DFS3 - radar counters (after any radar event): band+0xEA8 (CA 0x15F318)
  total radar detections, band+0xEAC+4*type (CA 0x15F31C...) per type 0..8.
  All zero when no radar occurred - expected for DFS-1.
P-DFS4 - channel status bytes: band+0x32A+idx*4+3 (CA 0x15E570+0x32A+idx*4+3)
  & 0x7F; 4 = usable per the vendor check (wal_cfg80211_set_chan_dfs_check);
  record before/after CAC and after a radar event (the NOL state changes).
P-DFS5 - 0x07E7 trace: the H2D trace shows key 0x07E7 len 1 body {01} at CAC
  start and {00} at finish/stop; also the 0x0136 radartool string if the
  vendor radartool path is exercised (not for DFS-1).

## C. Host-only probes (no device)

P-H1 - cfg80211 160 eligibility: check the driver wiphy VHT cap word and the
  channel flags (NO_160MHZ must be absent) - the image #3 module tree
  hi5622_cfg80211.c; the 6.18 rule is net/wireless/chan.c:1270-1286.
P-H2 - hostapd config parse: the wifi-hostapd-deploy lane's validate-configs.sh
  is the established host check; confirm the config keys in this lane's
  hostapd/ fixtures and TABLES.md T7.
P-H3 - the decode assertions: tools/fw_5g_160_dfs_re.py --selftest (this lane).
