# BUILD-NOTES - the dfs_order module (ARM before the dfs_mode) on the dfs_set base

**Lane:** wifi-5g-dfs-order (shared task **task-228**), 2026-11-26. **HOST-ONLY**:
nothing here was fired, flashed or read from the device. The rebuilt `.ko` is
**local evidence only** (`/tmp`), never committed (the repo carries no binaries).

## Result

The dfs_set tree (module `1b47f0da`, lane wifi-5g-dfs-set) plus this lane's patch
produces:

    /tmp/dfs-order-artifact/hi5622_wifi.ko
    391,408 B (stripped)   sha256 a5ec150cfc7d7b8df1b87bd59f5847f0e720b772e69524c76f2d26620babdb7e
    66 params (unchanged - the order patch adds no parameter)
    .text sha256 832802cc212d23ea9c71066d435fddad40a3233a82e38b31e5bc1de1293e5249
    unstripped 23604c182729f399e731b8511f40bb5349d6f4fe2d7fa3d248943d0c0a9b387f (1,444,260 B)

Host tests inside the build: **84/84** C contract checks
([evidence/host-selftest-c.txt](evidence/host-selftest-c.txt), the sibling lane's
C selftest is unchanged and still passes) + **46/46** independent Python
order/contract checks ([evidence/host-test-order.txt](evidence/host-test-order.txt)).

## Base provenance (byte-exact dfs_set tree)

The base tree is `/tmp/dfs-set-build`, the tree of the host-built dfs_set module
`1b47f0da` (391,388 B) rebuilt by
[`../wifi-5g-dfs-set/tools/build-dfs-set.sh`](../wifi-5g-dfs-set/tools/build-dfs-set.sh)
from the byte-exact R5 base `24f7221b` (that lane's CHAIN.md). The build script
asserts:

| item | pin |
| --- | --- |
| `/tmp/dfs-set-artifact/hi5622_wifi.ko` (base module) | 391,388 B / `1b47f0daff7e...` |
| `/tmp/dfs-set-build/hi5622_scan.c` (base) | `08d3aefecfac...` |
| `/tmp/dfs-set-build/hi5622_scan_logic.c` (base) | `15c50d34fa7e...` |
| `/tmp/dfs-set-build/hi5622_dfs_set.h` (base) | `18f951b90811...` |

and fails closed if the base is absent or drifted (it re-runs the dfs_set build
script in that case, which itself re-derives the R5 base if needed).

## What the patch changes (exactly three files)

| file | change |
| --- | --- |
| **hi5622_scan.c** | the dfs_mode (0x01F7) block is moved from before the 0x0124 channel post to after the dfs_set (0x07E7) block: new execution order `0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242`; the dfs_set block is otherwise byte-identical (gates, frames, post-reads unchanged); the module-parameter docs and the two dmesg position strings are updated (the "after the 0x0065 VAP start ... before the 0x01F7 dfs_mode / 0x0242 band start" text is the compiled order evidence) |
| **hi5622_scan_logic.c** | the ordering plan emits `HI5622_BEACON_STEP_DFS_MODE` after `HI5622_BEACON_STEP_VAP_START_G0` (plan output: `vap_add desc_table ap_chan ap_ssid ap_cfg vap_start_g0 dfs_mode band_start ap_vap_start`); the plan comments record the task-228 rule and the skipped 0x0124-time CA 0xA2FA2 programme |
| **hi5622_dfs_set.h** | documentation only: the post site is now "after the 0x0065 VAP start and BEFORE the 0x01F7 dfs_mode step and the 0x0242 band start" |

No other file, no new module parameter, no changed frame builder or gate.

## Patched-input pins

| file | sha256 |
| --- | --- |
| patches/0001-dfs-order.patch | `978c3d5b5795...` |
| hi5622_scan.c (patched) | `d2e894d4b24b...` |
| hi5622_scan_logic.c (patched) | `4400d546f4da...` |
| hi5622_dfs_set.h (patched) | `bb25e2faa4c9...` |

## Reproduce

    opensource/docs/incoming/wifi-5g-dfs-order/tools/build-dfs-order.sh

Outputs: `/tmp/dfs-order-build` (patched tree), `/tmp/dfs-order-artifact`
(module, selftest logs, pins). The whole-file sha is reproducible only at the
canonical `/tmp` paths (out-of-tree build metadata is path-sensitive - the same
R5-lane caveat); `.text` and params are stable.

## False starts / notes

* `MODULE_PARM_DESC` strings are not present in this build's `.ko` (`modinfo -p`
  shows names + types only - the same for the dfs_set base artifact), so the
  module-side order gate is the compiled dmesg format strings, not the parameter
  description text.
* The patch was generated as `diff -ruN` against `/tmp/dfs-set-build` and applies
  with `patch -p1`; it carries timestamps only in the per-file headers.

## Limits / honesty

* Host-only; the fire belongs to the Lead and follows [FIRE-RECIPE.md](FIRE-RECIPE.md).
* The bench attribution of the CAC pre-state to the 0x01F7 is [I] from the
  pair-A boot comparison; the fire's R1a/R1b rungs measure the causal claim
  before the fixed rung.
* The `.ko` is local evidence; no vendor firmware bytes are committed. Register
  addresses and the frame bytes are addresses/host-built protocol bytes.
