# BACKLOG SPEC - 5 GHz (5G-1..5G-6)

## 5G-1 - the dual-band wiphy - DONE
- Source: the band5g patch (the band5g=1 profile: 29 channels, HT 0x0062, VHT 0x400064) +
  the secondary-endpoint exclusion (endpoint 0001 gets no wiphy).
- PASS (achieved): one phy, Band1 2412 + Band2 5180; the 0001 "SECONDARY host view" log.
- Evidence: docs/incoming/wifi-5ghz-bringup/; the bench 2026-11-26.

## 5G-2 - the regdomain (country US, 5 GHz IR) - DONE
- Root cause: CONFIG_CFG80211_REQUIRE_SIGNED_REGDB hidden behind CERTIFICATION_ONUS/EXPERT;
  the regdb rejected -> world -> NO-IR.
- Fix: the cfg80211.ko rebuild (see docs/incoming/wifi-regdom-fix/README.md).
- PASS (achieved): iw reg get -> country US: DFS-FCC; 5150-5250 IR.
- Image: the OWRT tree carries the config + the built module; a re-bake includes it.

## 5G-3 - the 5 GHz AP order - DONE
- Source: the rx-5g-vap lane's decode (the vendor never posts g0 id7/id8 at interface
  creation; the VAP create precedes its users; the AP order 0x0124->0x0005->0x006D->0x0127->
  0x0065 with band=dev_priv[0]).
- Fix: the vap5g=1 deferral (module 13e7bf4c, docs/incoming/wifi-5ghz-vap-msg/).
- PASS (achieved): AP-ENABLED, ssid cudy-ap-test-5g, the full order, page golden.

## 5G-4 - the 5 GHz beaconing - IN PROGRESS (lane rx-5g-beacon, task-175)
- The gap: AP-ENABLED + the full order, but no beacons on air; the firmware VAP object
  readback=ZERO (the E2 evidence) + the module channel ctx readback garbage (channel=104
  band=84). The 2.4 GHz with the same code beacons (so the readback is a heuristic).
- The source: the vendor hmac_band_set_channel @0x13f49c (writes the channel byte at
  band_struct+0xd29), hmac_band_set_channel_and_bandwith @0x13f520, the module/band context
  structs, the dev_priv[0] band selection, the band switch/RF timing.
- PASS: the observer's 5180 scan finds cudy-ap-test-5g (RSSI strong) + page golden.
- FAIL: still no beacon -> the firmware-side band activation needs the RF/cal path.
- Test: the fire5g3 script (module + ladder + C2 + AP-first) + the laptop's
  iw dev wlan0 scan freq 5180.
- Deps: none (the chip is healthy; a fresh module load).

## 5G-5 - DFS/radar (the DFS channels 52-144)
- Source: the vendor hmac_dfs_cac_start/stop (@0x?), the set_radar_th/get_radar_th/
  dfsenable/cacenable commands (docs/incoming/wifi-firmware-re), the radar detection ISR.
- PASS: an AP on a DFS channel completes the CAC and beacons.
- Test: a fire on ch52 with the hostapd DFS config; the radar event handling.
- State: NOT STARTED.

## 5G-6 - 160 MHz
- Source: the VHT 0x400064's 160 MHz bit + the vendor's bandwidth config
  (hmac_band_set_channel_and_bandwith) + the hostapd's ht_capab/vht_capab 160.
- PASS: a 160 MHz AP beacon + an 80+80/160 client association.
- Test: the fire with the 160 MHz config + the observer's width check.
- State: NOT STARTED.
