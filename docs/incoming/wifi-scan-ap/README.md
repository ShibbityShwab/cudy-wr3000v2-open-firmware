# wifi-scan-ap - cfg80211 scan / AP / VAP-message wiring for the Hi5622V100 clean-room driver

Host-only deliverable of the **wifi-scan-ap** lane (shared task **task-113**,
2026-11-24/25), extending the hardware-validated wifi-cfg80211 M0-M2 tree
(`../wifi-cfg80211/`, clean-room module 505,076 B) with the cfg80211 ops that
map to firmware messages, and integrating the **task-110 wifi-vap-scan frame
builders** (`../wifi-vap-scan/`, opensource commit 0e4ad45):

    scan / abort_scan          -> g0 id0xC request built by the vapscan provider
                                  + cfg80211 BSS reporting from D2H g1 15/16/17 / g2 2
    start_ap / stop_ap         -> WAL group-2 (gap G1) design + provider stub
    add/del_virtual_intf       -> g0 id7/id8 user add + g0 id9 del through the provider
    hi5622_frame_post()        -> ONE fail-closed wire path (allow_frames=0 default)
    hi5622_vapscan_provider    -> registers the g0 id7/id8/id9/id0xC builders
                                  + the task-118 g1 id15/16/17 + g2 id2 decoders
    hi5622_frame_ops           -> generic provider registry (empty default; the
                                  vapscan adapter is the one committed provider)

The H2D layout question is closed for these four messages (task-110 [V]
vendor-derived specs); what remains open is the **D2H** side: the g1 id15
trailing-descriptor layout is [U], so the best-effort decoder is tolerant: the provider advertises **caps 0x1f**
(`scan_req|scan_parse|user_add|user_add_notify|user_del`), so `.scan` is
available and posts a real 520-byte g0 id0xC frame once the post gate is open
(`allow_frames>0`); the g1 id15 trailing descriptor is [U], so the decoder
reports only what the 802.11 body proves and 'no BSS entries, bounded
scan_done' is a valid outcome.  AP start/stop payloads remain WAL gap G1 [U].

**Status: host-only, compile-clean (0 warnings), host-selftested, NOT
bench-fired by this lane.** Module **704,720 B** sha256 `20d38c5c...`, 9
cfg80211 imports, depends=cfg80211; host selftests **284/284** (new, incl. the builder
mapping + the integrated vendor-verified task-118 D2H decoders) + 454/454 and 242/242 (inherited
regressions); provider-symbols check 9/9; `allow_frames=0` fail-closed default (scan decode is
always on, tolerant).  Bench session 2026-11-24: the first real firmware
management frame was posted (g0 id7 user_add 180 B, consumed in 0 polls); the
follow-up g0 id8 notify refusal and the scan-cap mismatch are fixed here (see
REPORT.md sec 3.1).
The M0 wiphy and M1 netdev lifecycle this builds on are hardware-validated on
our image (STATUS.md sec 3k, 2026-11-25). The scan/AP/user-message code paths
are compile-verified and host-tested only.

| file | what |
| --- | --- |
| REPORT.md | design, op<->message table, provider contract, integration diff, fail-closed gate, risks, evidence markers |
| BUILD-NOTES.md | exact build result (size/sha/vermagic/warnings/selftest counts), build line, tree quirks |
| TEST-LADDER.md | exact userspace ladder (`iw scan`, hostapd OPEN/WPA2, wpa_supplicant), expected observables (mostly negative controls today), stop rules |
| configs/ | hostapd.conf (WPA2-CCMP ch6), hostapd-open.conf, wpa_supplicant.conf + README (placeholder PSKs only) |
| src/ | working driver tree (wifi-cfg80211 + new scan/provider/post layer + integrated task-110 vapscan builders + the documented integration edits) |
| tools/build.sh | canonical: host scan selftest -> inherited cfg80211/H2D regressions -> kernel module -> provider check -> static selftest |
| tools/scan_logic_selftest.c | 284 device-free checks: provider contract, validators, event classifier, scan model/completion, exact g0 id9 round-trip, task-110 builder mapping (id7/id8/id9/id0xC byte offsets) and the task-118 g1 id15/16/17 + g2 id2 decoders |
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
