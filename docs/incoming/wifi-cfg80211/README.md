# wifi-cfg80211 - cfg80211 full-MAC skeleton (M0-M2) for the Hi5622V100 clean-room driver

Host-only deliverable of the **wifi-cfg80211** lane (shared task **task-104**,
2026-11-24).  It builds the first real cfg80211 interface for the clean-room
Wi-Fi driver **on top of the already working full-duplex HCC protocol**:

    clean driver tree (wifi-h2d-send, 400,732 B / 0 warnings, bench-validated)
      + wiphy registration gated on the HCC ready handshake
      + real netdev VAP model via add_virtual_intf / del_virtual_intf
      + -EOPNOTSUPP M2 stubs (scan / get_station / dump_station)
      + portable, host-tested VAP model + 39-op cfg80211 <-> HCC message map

**Status: host-only, compile-clean, zero warnings, not bench-fired by this
lane.**  Kernel module **hi5622_wifi.ko** 504,800 B sha256 7d01e340...,
vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211, 0
compiler/modpost warnings (project 6.18.60 tree, cfg80211 provided to modpost
via src/cfg80211-provider.symvers).  Host selftests: cfg80211 logic
**447/447**, inherited H2D logic **242/242**, driver source selftest
**138/138**.

| file | what |
| --- | --- |
| REPORT.md | design, wiphy/VAP model, **cfg80211-op <-> HCC message mapping table**, milestones M0-M6, integration diff, verification, bench gate, risks |
| BUILD-NOTES.md | exact build result (size/sha256/vermagic/warnings/selftest counts), build line, toolchain, tree-quirk workarounds |
| cfg80211.config | kernel config fragment handoff for the target/defconfig lane (task-107): CONFIG_CFG80211=m required |
| src/ | working driver tree (wifi-h2d-send + new cfg80211 layer): hi5622_cfg80211*.{c,h} + updated main.c/service.c/hi5622_driver.h/Kbuild |
| tools/build.sh | canonical build: host cfg80211 selftest -> inherited H2D selftest -> kernel module build -> provider-symbols check -> driver selftest |
| tools/cfg80211_logic_selftest.c | 447 device-free checks: VAP lifecycle, registration gate, 39-op map, frame refs, status formatting |
| tools/gen_cfg80211_symvers.py | validates/regenerates the cfg80211 provider symvers against the real net/wireless exports |
| tools/selftest.py | source/contract selftest (integration wiring, ops/stub discipline, safety, no vendor material) |

Quick start (host, no device):

    ./tools/build.sh

What this lane does **not** do: it sends no H2D message and touches no device;
the VAP-add (g0 id7/id8) and scan (g0 id0xC) HCC payload builders are still
open and are task-103 (wifi-proto-seq)'s deliverable - the op map records every
one of them, and the driver model refuses to fake success (-EOPNOTSUPP) until
they exist.  No vendor binary, firmware byte, calibration value or per-unit
identity is committed; the PD4 patch image and Wi-Fi PHY block remain local
bench inputs.

Bench gate (attended only, after task-103/bench planning): PD4 golden boot ->
ready pair -> driver dmesg "cfg80211: wiphy registered after the ready
handshake" -> iw phy / iw dev enumerate the clean wiphy -> create one VAP via
iw phy <phy> interface add ... -> iw dev <vap> del.  See REPORT.md sec 9 for
the exact sequence and stop rules.
