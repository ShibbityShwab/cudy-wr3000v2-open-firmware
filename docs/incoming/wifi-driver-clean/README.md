# wifi-driver-clean - clean-room Hi5622V100 Wi-Fi transport driver (task-94)

Host-only restructure lane.  The research modules of the Wi-Fi bring-up
(transport-port, hcc-service, d2h-direct) and the protocol core (hcc-core) are
merged into ONE clean, self-contained, compile-clean kernel driver tree with
the corrected boot/service protocol baked in - no build-time patching, no
duplicate module names, no inherited unsafe sysfs steps.

**Status: host-only.  Compile-clean against the project 6.18.60 tree; selftest
133/133 PASS; nothing here has been run on the device.  This driver replaces
the bench instrument modules but is still a bench instrument (no cfg80211/
mac80211, no data path).**

| file | what |
| --- | --- |
| REPORT.md | the lane report: merge map, corrected protocol, fixes, build evidence, limits |
| BUILD-NOTES.md | exact build result (size/sha256/vermagic/warnings), toolchain, selftest |
| BENCH-PROCEDURE.md | the attended sysfs ladder for the next session |
| src/ | the driver tree: main/transport/ete/d2h/hcc/service + contract headers + baked-in hcc_core |
| tools/build.sh | canonical build: compiles against the project kernel tree, reports size/sha/vermagic/warnings, runs the selftest |
| tools/selftest.py | 133 device-free contract/logic checks (window map F4, blocks, release gates, allowlist, layout) |
| tools/mkblocks.py | stages the local Wi-Fi PHY custom block from a stock capture (never committed) |

Quick start (host):

    ./tools/build.sh                 # clean module build + selftest
    ./tools/mkblocks.py --selftest   # local custom-block staging tool

Module: hi5622_wifi.ko, GPL, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8.
No vendor binary/object/firmware/calibration byte is committed; the Wi-Fi
PHY custom block is loaded from a local /lib/firmware file at bench time.
