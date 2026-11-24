# Bench observables for the scan D2H path (read-only)

Lane wifi-scan-d2h, task-118. Use AFTER the VAP object/start lands (see
../REPORT.md section 7). Nothing here writes a device register; all reads go
through the deployed driver attributes (rd_status / rd_catalog / scan status)
and dmesg.

## 0. Preconditions

- Golden boot: page CA 0x1BF000 = 0x86807FEF / 7 / 2 / 0xABCD, state = 1, mask = 0.
- C2-style message ladder completed (ctx-create type 1 + TXQ sync + calibrations).
- Firmware VAP object 0 created/started (wifi-vap-scan lane). Without it every
  scan request is refused by CA 0x6494C with return 100 and there will be no
  D2H result frames - that is not a decode failure.

## 1. Ladder

| step | action | expected |
| --- | --- | --- |
| S0 | read rd_status / hcc_status | phase ACTIVE, h2d budget free |
| S1 | post g0 id0xC 520-byte scan request (channel list = 2.4 GHz 1-13) | frame consumed 0 polls, no exception, mask 0 |
| S2 | read rd_catalog immediately | g1 0x11 entries appear (one per channel), each total 0x7C = 124 B |
| S3 | read rd_catalog again ~1-5 s | g1 0x0F entries (one per BSS, variable total >= ~72), g1 0x10 completion, g2 0x02 completion |
| S4 | read scan sysfs status + dmesg "cfg80211: scan DONE" | bss_events > 0, results > 0 for a visible AP; scan_done(aborted=0) |
| S5 | final boot pack | page golden, state 1, mask 0, no exception staging |

## 2. Classification (see REPORT.md 10.2)

- only g1 0x03 / boot catalogue -> firmware refused the scan (VAP/state
  precondition) or the request was consumed without scheduling: NOT a decode gap.
- g1 0x11 > 0 but no g1 0x0F -> scan ran, no BSS heard on those channels.
- g1 0x0F > 0 and driver parse_errors/invalid_results > 0 -> decode gap:
  capture the full payload (not just the 16-byte dmesg dump) and extend the
  decoder. The decoder returns -EOPNOTSUPP, never a fabricated BSS.
- g1 0x0F > 0 and inform_failures > 0 -> channel/frequency not present in our
  wiphy band table (e.g. ch14 or a 5 GHz channel outside the LU plan).

## 3. Expected sizes

| frame | total | note |
| --- | --- | --- |
| g1 0x11 channel result | 124 (0x7C) fixed | 12-byte header + 112-byte payload, sender CA 0x641B4 alloc 0x70 |
| g1 0x0F BSS result | 12 + frame_len + 20 | frame_len = 802.11 beacon/probe-resp length; minimal beacon with SSID+DS IE ~ 72 total |
| g1 0x10 completion | small ([U] >= 13) | sender not pinned; status byte is payload[0] |
| g2 0x02 STA completion | small ([U]) | status byte payload[0xD] (frame view) |

## 4. Commands (examples; adapt to the session runner)

~~~
# inside the bench session (read-only):
cat /sys/.../rd_catalog | grep -E 'g=1 id=(15|16|17)|g=2 id=2'
dmesg | grep -E 'D2H DIRECT.*g=1 id=1[567]|scan DONE'
# driver scan status file (scan-ap module): see the lane's sysfs name
~~~

Record the exact rd_catalog lines (count / last_total / last_len) with the
epoch time before and after so the 5 s cfg80211 abort window is visible.
