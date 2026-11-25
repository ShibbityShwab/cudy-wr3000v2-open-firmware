# wifi-rx-watermark - EAPOL/data RX free-ring refill watermark (T4)

**Lane:** wifi-rx-watermark, shared task **task-156**. **Host-only** (this lane
never touches the bench; the Lead fires). **Status:** implemented, host-tested
(127/127 checks), final module rebuilt and identity-verified; **the exact
combination is NOT fired**.

**Write scope:** `opensource/docs/incoming/wifi-rx-watermark/`. The `.ko` is
local evidence only and is never committed.

## Why (T4)

The RX drain may re-publish only buffers that a validated completion handed
back, and one `dp_rx_drain` action consumes at most `rx_drain_max` (64)
completions. The old rx-static drain re-published only the live-consumer delta
of that one action and then advanced its snapshot, so a completion backlog
larger than 64 leaked posted free-ring entries **permanently**: with 256 posted
and a 200-entry burst only 120 stay posted while 136 returned buffers sit
unused in the refill queue. Under sustained EAPOL/data traffic that is the
iwlwifi `RX_STALLED` shape (the sibling Hi1105 HAL treats free-ring-empty as a
first-class refill event). T4 adds a sustained refill **watermark** to the drain.

## Artifact (host-built, NOT fired; the Lead/final-image lane bakes it)

| item | value |
| --- | --- |
| chain | final association tree `251d08f0` + wifi-sibling-gpl `0001-mgmt-tx-hcc-g0id6-carrier.patch` (`ab9e18a4` module) + this lane `0001-rx-refill-watermark.patch` |
| stripped | **347,388 B sha256 `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6`** |
| unstripped | 1,332,996 B sha256 `f11835e8c47839d6ccba368ea64800912bfe5e93bd5ef5581f868b73ce858dc7` |
| params | **57** (baseline 56 + `rx_refill_wm`); vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends `cfg80211` |
| local path | `/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko` (durable gitignored copy: `build/wifi-rx-watermark/`) |
| build warnings | 0; strip leaves `.text` byte-identical |
| corrected mgmt carrier | yes - `himg_build_hcc_mgmt_tx` + `himg_parse_rx_mgmt` present in the .ko |

The sha is for that exact file: a Linux module's build-id (20-byte GNU note, ld
hashes the pre-strip DWARF `comp_dir`) depends on the build directory. Rebuilds
in another directory are code/param identical but carry a different sha - quote
the sha of the exact baked file.

## New module parameter

```
rx_refill_wm=75      # uint, percent of ring depth, 0..100, DEFAULT 75
```

* `75` = keep `>= 3/4` of both RX free rings posted (ath11k-style; 192/256 and
  384/512 slots for the norm/small rings).
* `0` = watermark pass disabled: byte-for-byte the old rx-static drain
  behaviour (the control rung).
* `>100` is clamped to `100` at use (clamping can only under-refill, never
  over-publish).
* **Fail-closed at boot:** the pass is inert unless `rx_enable=1` **and**
  `dp_rx_arm` succeeded; a plain boot/autoload never moves RX ring pointers.
  Inside a drain it only re-publishes buffers already returned by validated
  completions, bounded by `depth - posted` and by `rx_drain_max` per action; any
  ring/descriptor/publish anomaly closes the RX lane (no retry).

## Deliverables

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | T4 bug analysis, design, evidence basis, host results, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact rebuild chain, verification assertions, artifact identity |
| [TEST-LADDER.md](TEST-LADDER.md) | host ladder results + attended bench ladder (the Lead fires) |
| [patches/0001-rx-refill-watermark.patch](patches/0001-rx-refill-watermark.patch) | the 3-file driver patch (`hi5622_rx_live.{h,c}`, `hi5622_dp_txrx.c`) |
| [src/](src/) | committed copies of the post-patch portable logic (for the host test) |
| [tools/rx_wm_selftest.c](tools/rx_wm_selftest.c), [tools/run-host-tests.sh](tools/run-host-tests.sh) | ring state-machine host test (127 checks) |
| [tools/build-final-module.sh](tools/build-final-module.sh) | reproducible rebuild (assoc tree + mgmt carrier patch + this patch) |
| [evidence/](evidence/) | host test output, identity, patch verify, build chain |

## Reproduce

```sh
cd <repo>/opensource/docs/incoming/wifi-rx-watermark
./tools/run-host-tests.sh                 # 127 checks, 0 failures, RESULT: PASS
./tools/build-final-module.sh             # -> /tmp/wifi-rx-watermark-artifact/
# if /tmp was cleaned, regenerate the association tree first:
REBUILD_ASSOC=1 ./tools/build-final-module.sh
```

The fire recipe (with `rx_refill_wm=75`) and the ladder/stop rules are in
[TEST-LADDER.md](TEST-LADDER.md).
