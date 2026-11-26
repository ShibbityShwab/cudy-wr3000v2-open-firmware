# dfs160b - the 5 GHz 160 MHz width selector + the DFS/CAC/NOL feature

**Lane:** shared task **task-245** (owner `dfs160b`). **HOST-ONLY:** static
decode + portable model + clean-room driver patch + reproducible host build;
no device contact, no UART, no fire, no flash. The Lead fires.
**Commits:** only inside `opensource/`. **Never committed:** vendor binaries,
firmware bytes, module artifacts, calibration values, MACs or secrets.

This lane is the independent implementation of the feature the committed
decode lanes prepared; it reads those lanes as the ONLY source of protocol
facts and does not carry vendor bytes:

| source lane | what it pins |
| --- | --- |
| [../wifi-5g-160-dfs/](../wifi-5g-160-dfs/) (task-205) | the 0x0124 body[1] width selector, the DFS channel set incl. the ch144 gap, the CAC/NOL table (T4/T5/T7/T8), the host-implementation spec |
| [../wifi-5g-dfs-set/](../wifi-5g-dfs-set/) (task-227) | the H2D g0-id1 key 0x07E7 ARM/CLEAR 29-B carrier, the six band-1 SMAC sub-base writes, the 0x0065/0x0242 slot |
| [../wifi-5g-dfs-order/](../wifi-5g-dfs-order/) (task-228) | the forced order `0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242` |
| [../wifi-5g-dfs-relax/](../wifi-5g-dfs-relax/) (task-230) | the +0x4A0 pre-gate relax: register-only vs full ARM |
| [../wifi-5g-cac-context/](../wifi-5g-cac-context/) (task-229) | the +0x49C/+0x4A0/+0x4A4 context writer CA 0x5F450 and the ARM gate |

## The feature (the four deliverables)

1. **The 160 MHz width selector** - the H2D g0-id1 channel config key
   `0x0124` carries the 4-byte body `{band, bandwidth, channel, 0}`; byte[1]
   is a bandwidth selector (0=20M, 1=40M, 2=80M, 3=160M [I]), NOT a VAP index.
   The port maps the cfg80211 chandef width onto it (fail closed for 80+80 and
   320 MHz, which the vendor wiphy VHT cap `0x00400064` does not advertise),
   keeps the legacy byte-identical default behind a knob, and validates the
   160 MHz single-segment CCFS1.
2. **The DFS key 0x07E7 / CAC / NOL** - ARM `{01}` / CLEAR `{00}` 1-byte
   body over the 29-B carrier, tick arithmetic (63 s first tick, 60 s per
   tick, max = cac_ms/60000), CAC 60000 ms / TDWR 600000 ms, NOL 1800000 ms,
   cfg80211 `start_radar_detection`/`end_cac` ops, and the AP-start DFS gate.
3. **The driver integration** - the clean-room patch on the fired dfs-relax
   A-rung lineage, reproducible host build with pinned base and artifact
   identity (`ht_bw`, `dfs_cac` knobs; fail-closed defaults).
4. **The fire recipe** - the gated bench rungs, probes, PASS/FAIL/ABORT and
   negative controls (the Lead fires; this lane never touches the device).

## Status

| deliverable | state |
| --- | --- |
| lane opened, sources pinned | DONE |
| portable model + host selftest | DONE: src/hi5622_dfsb.{h,c} + src/dfs160b_selftest.c, 269 checks / 0 failures, evidence/HOST-TESTS.txt |
| driver integration patch + build | DONE: patches/0001-dfs160b.patch (14 files), reproducible host build, installed 383020 B sha256 006a94cf..., .text 164980 B sha256 5e14008f..., 68 params, 0 warnings (evidence/BUILD.txt) |
| fire recipe + report/tables | IN PROGRESS (next) |

## Standing rules

Read [../wifi-5g-160-dfs/README.md](../wifi-5g-160-dfs/README.md) first: both
features are **downstream of the 5 GHz band-1 MAC start** (the 0x0242 rung).
Until that rung is proven by the on-air gate, no lane here authorises a bench
fire. No blind register writes; the A-rung recipe is the only sanctioned
entry and it stays fail-closed.
