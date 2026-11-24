# wifi-scan-complete - all-13-channel scan completion + beacon IE decode

Host-only deliverable of the **wifi-scan-complete** lane (shared task
**task-123**, 2026-11-26). It extends the bench-fired provider module of the
**wifi-scan-ap** lane (task-113) with the **wifi-vap-start** provider patch
(task-117, caps 0x19f) and adds the two things the 2026-11-26 scan milestone
still lacked:

1. **all 13 channels complete** - the firmware scanned 6 of the 13 requested
   2.4 GHz channels in the provider's fixed 5 s bound (~830 ms/channel) and the
   host reported finish-abort; the scan window is now sized from the channel
   count (auto, 14,700 ms for 13 channels), a new completion rule closes the
   scan successfully when every requested channel was reported, and
   `scan_timeout_ms` / `scan_dwell_ms` module parameters let the bench tune it;
2. **full cfg80211 BSS entries** - the g1 id15 802.11 body + 20-byte trailer is
   decoded into the complete BSS field set (BSSID, SSID, DS / HT-operating
   channel, capability ESS/IBSS/privacy, beacon interval, TSF, HT capabilities,
   RSN/WMM/VHT/country/extension presence) and the validated IE chain is handed
   to cfg80211_inform_bss(), so `iw dev wlan0 scan` lists real BSS entries;
   one dmesg line per BSS makes the bench result unambiguous.

**Status: host-only, compile-clean, host-selftested; NOT bench-fired by this
lane.** The parent scan/vap-start path it builds on is hardware-validated
(2026-11-26: mode-1 VAP create -> scan -> firmware results). The exact build
artifact and test counts are in BUILD-NOTES.md; the attended bench procedure is
TEST-LADDER.md; the RE analysis and evidence markers are in REPORT.md.

| file | what |
| --- | --- |
| REPORT.md | bench evidence, scan timing/completion RE, beacon IE decode, implementation diff, verification, open [U], risks |
| BUILD-NOTES.md | exact build result (size/sha/vermagic/warnings/selftest counts), build line, tree quirks |
| TEST-LADDER.md | attended bench ladder for 13/13 channel completion + BSS entries, expected dmesg, decision table, stop rules |
| src/ | working driver tree = wifi-scan-ap src + wifi-vap-start provider patch + the scan-complete changes |
| tools/build.sh | canonical: host selftests -> kernel module build -> provider check -> static selftest |
| tools/scan_complete_selftest.c | 195 device-free checks: auto window, channel-coverage completion, beacon/probe-response IE decode, map round-trip, negative cases |
| tools/scan_logic_selftest.c | inherited provider/scan selftest (updated for the task-117 VAP caps; 290/290) |
| tools/selftest.py | source/contract checks incl. the scan-complete rule and the inherited-tree parity baseline |
| patches/0001-scan-complete.patch | integration diff over the bench-validated base tree (wifi-scan-ap src + task-117 provider patch) |

Quick start (host, no device, no network):

    ./tools/build.sh

The module keeps every fail-closed default of the trees it extends:
`allow_frames=0`, no frame reaches the wire unless the bench raises the budget,
no vendor firmware/calibration byte is copied or committed, and the AP-start
payload gap (WAL AP-config 0x127) remains [U] - `start_ap` still returns
-EOPNOTSUPP.

What this lane does **not** do: it does not fire the bench, does not claim a
userspace-validated scan yet (the ladder in TEST-LADDER.md is the proof), does
not invent the g1 id15 trailer semantics (signal stays 0 mBm unless
`scan_trust_rssi=1` is explicitly loaded for a correlation run), and does not
touch the bench router or the root notebook.
