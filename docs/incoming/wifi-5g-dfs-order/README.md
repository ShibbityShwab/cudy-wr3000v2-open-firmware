# wifi-5g-dfs-order - the 0x07E7 DFS-SET ARM BEFORE the 0x01F7 dfs_mode

**Lane:** `wifi-5g-dfs-order` (shared task **task-228**), 2026-11-26. **HOST-ONLY**:
no device contact, no flash, no firmware write. The rebuilt `.ko` is local
evidence only (`/tmp`); the repo carries no binaries.

## The finding this lane fixes

The dfs_set fire of 2026-11-26 23:00 (module `1b47f0da` from
[`../wifi-5g-dfs-set`](../wifi-5g-dfs-set/)) was **refused by its own
fail-closed pre-gate**:

    cfg80211: dfs_set pre: mctx=0x00119758 band=1 state=1 ec8=1
              b4a0=0x0104fd90 b4ac=0x010067a8 cac=0 regs_pattern=0x00
              (u32[mctx+0x4A0] gate is nonzero ...)
    cfg80211: dfs_set REFUSED (-16)

The stock reference boot has the mctx CAC words `+0x49C..+0x4A8` **all zero**
(clean pre-state); on our boot they were already populated. The cause: our
ladder posted the **0x01F7 dfs_mode** (the CAC-starting DFS/RF mode
activation) **before** the **0x07E7 DFS-SET ARM**, so the ARM gate
`u32[mctx+0x4A0] == 0` could never pass. **The 0x07E7 ARM must precede the
0x01F7.**

## The fix (this lane's patch)

The 0x07E7 worker's started-module gate needs the 0x0065 VAP start, and the
0x0065 needs the 0x0124 channel post; the only consistent slot is

    vap_add -> desc_table(0x00F7) -> ap_chan(0x0124) -> ap_ssid -> ap_cfg(0x0127)
      -> vap_start_g0(0x0065) -> dfs_set(0x07E7 ARM) -> dfs_mode(0x01F7)
      -> band_start(0x0242) -> ap_vap_start

The 0x01F7 step therefore moves after the 0x0124 channel post; the 0x01F7
worker still writes the RF mode byte `P+0xF04 = 3` and the `R+0x3A8..R+0x418`
image itself, but the 0x0124-time CA 0xA2FA2 RF programme no longer runs
(mode gate not yet open). The fire's D11 probes that consequence. The
dependency analysis and the rejected alternatives are in [REPORT.md](REPORT.md).

## Deliverables

| file | what |
| --- | --- |
| [patches/0001-dfs-order.patch](patches/0001-dfs-order.patch) | the reorder on top of the dfs_set tree (3 files, comments included) |
| [tools/build-dfs-order.sh](tools/build-dfs-order.sh) | host rebuild + pins + host tests (fail-closed) |
| [tools/dfs_order_test.py](tools/dfs_order_test.py) | independent Python order/contract test (46 checks) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the fire rung: load line, expected dmesg, the new D3 pre-state probe, D11 RF-order probe, PASS/FAIL/ABORT |
| [REPORT.md](REPORT.md) | the dependency analysis, the reorder, effects, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | base pins, artifact pins, reproduce |
| [evidence/](evidence/) | pins, C selftest, Python order test, source-order excerpt |

## The module (local artifact, not committed)

    /tmp/dfs-order-artifact/hi5622_wifi.ko
    391,408 B (stripped)   sha256 a5ec150cfc7d7b8df1b87bd59f5847f0e720b772e69524c76f2d26620babdb7e
    66 params (unchanged from dfs_set; the patch adds no parameter)
    .text sha256 832802cc212d23ea9c71066d435fddad40a3233a82e38b31e5bc1de1293e5249
    host tests: 84/84 C contract + 46/46 Python ORDER

## Reproduce

    opensource/docs/incoming/wifi-5g-dfs-order/tools/build-dfs-order.sh

It uses `/tmp/dfs-set-build` when present; otherwise it first re-runs
[`../wifi-5g-dfs-set/tools/build-dfs-set.sh`](../wifi-5g-dfs-set/tools/build-dfs-set.sh)
(which re-derives the byte-exact R5 base `24f7221b` if needed). Every input
and output sha is asserted; a drift fails closed.

## Limits / honesty

* Host-only. The bench attribution "the 0x01F7 makes the firmware CAC go
  active" is the pair-A stock-vs-ours comparison of the 2026-11-26 bench
  (`[I]` inferred from the zero stock pre-state); the fire rungs R1a/R1b in
  [FIRE-RECIPE.md](FIRE-RECIPE.md) test it directly.
* The full CAC state machine (60 s/600 s timer, NOL, `hmac_start_all_bss_of_band`)
  is still not implemented - this lane changes the ORDER only.
* No vendor bytes are committed; the protocol constants are quoted text.
