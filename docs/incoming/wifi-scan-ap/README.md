# wifi-scan-ap - cfg80211 scan / AP / VAP-message wiring for the Hi5622V100 clean-room driver

Host-only deliverable of the **wifi-scan-ap** lane (shared task **task-113**,
2026-11-24), extending the hardware-validated wifi-cfg80211 M0-M2 tree
(`../wifi-cfg80211/`, clean-room module 505,076 B) with the cfg80211 ops that
map to firmware messages:

    scan / abort_scan          -> provider-built g0 id0xC request
                                  + cfg80211 BSS reporting from D2H g1 15/16/17 / g2 2
    start_ap / stop_ap         -> WAL group-2 (gap G1) design + provider stub
    add/del_virtual_intf       -> g0 id7/id8 user add + g0 id9 del through the provider
    hi5622_frame_post()        -> ONE fail-closed wire path (allow_frames=0 default)
    hi5622_frame_ops           -> registry for the payload builders (empty by default)

The payload layouts for g0 id0xC (508 B) and g0 id7/id8 (168 B) are still [U]
and are the **wifi-vap-scan lane's** (task-110) deliverable. This lane therefore
wires the complete cfg80211 side around a narrow provider contract and fails
closed (`-EOPNOTSUPP`, no device write) until those builders are dropped in.

**Status: host-only, compile-clean (0 warnings), host-selftested, NOT
bench-fired by this lane.** Module **622,544 B** sha256 `77987ed0...`, 9
cfg80211 imports, depends=cfg80211; host selftests 149/149 (new) + 454/454 and
242/242 (inherited regressions); provider-symbols check 9/9; provider registry
empty (the default), `allow_frames=0` fail-closed default.
The M0 wiphy and M1 netdev lifecycle this builds on are hardware-validated on
our image (STATUS.md sec 3k, 2026-11-25). The scan/AP code paths are
compile-verified and host-tested only.

| file | what |
| --- | --- |
| REPORT.md | design, op<->message table, provider contract, integration diff, fail-closed gate, risks, evidence markers |
| BUILD-NOTES.md | exact build result (size/sha/vermagic/warnings/selftest counts), build line, tree quirks |
| TEST-LADDER.md | exact userspace ladder (`iw scan`, hostapd OPEN/WPA2, wpa_supplicant), expected observables (mostly negative controls today), stop rules |
| configs/ | hostapd.conf (WPA2-CCMP ch6), hostapd-open.conf, wpa_supplicant.conf + README (placeholder PSKs only) |
| src/ | working driver tree (wifi-cfg80211 + new scan/provider/post layer + the documented integration edits) |
| tools/build.sh | canonical: host scan selftest -> inherited cfg80211/H2D regressions -> kernel module -> provider check -> static selftest |
| tools/scan_logic_selftest.c | 149 device-free checks: provider contract, validators, event classifier, scan model, exact g0 id9 round-trip |
| tools/selftest.py | source/contract checks: ops table, one scan_done, fail-closed defaults, D2H hook, baseline parity, configs, docs, no vendor material |
| tools/gen_cfg80211_symvers.py | regenerates/validates the 9-symbol cfg80211 provider symvers (Module.symvers grammar) |
| tools/src-baseline.sha256 | inherited-tree parity baseline (19 unmodified files) + the documented integration-edit list |

Quick start (host, no device):

    ./tools/build.sh

What this lane does **not** do: it does not send a frame on hardware, does not
claim a working `iw scan`/hostapd AP (no builders, provider registry empty,
`allow_frames=0`), and does not touch the bench router. The bench ladder is
written so every step's *expected failure* is asserted; see TEST-LADDER.md.

Bench gate (attended only, after the builders land): see TEST-LADDER.md sec 2-6.
