# BUILD-NOTES - the dfs_set RELAX module on the dfs_order base

**Lane:** wifi-5g-dfs-relax (shared task **task-230**), 2026-11-26.
**HOST-ONLY**: nothing here was fired, flashed or read from the device. The
rebuilt `.ko` is **local evidence only** (`/tmp`), never committed (the repo
carries no binaries).

## Result

The dfs_order tree (module `a5ec150c`, lane wifi-5g-dfs-order) plus this
lane's patch produces:

    /tmp/dfs-relax-artifact/hi5622_wifi.ko
    392,868 B (stripped)   sha256 6c50d796a80b4afd1bb008ce89d0c51d25d642eb2a35101f16dc584790d27948
    66 params (unchanged - the relax patch adds no parameter)
    .text sha256 5653b024ef7d53ec9694c1905ea170316d618b3b024f5a2afc464197e28790df
    unstripped 12d7a44707e442073b611045c876b001475a04bed45ab793d792afa37ac033eb (1,446,240 B)

Host tests inside the build: **101/101** C contract checks
([evidence/host-selftest-c.txt](evidence/host-selftest-c.txt), this lane's
extended selftest) + **55/55** independent Python RELAX checks
([evidence/host-test-relax.txt](evidence/host-test-relax.txt)) + **46/46**
sibling ORDER checks as a regression
([evidence/host-test-order.txt](evidence/host-test-order.txt), the unmodified
`../wifi-5g-dfs-order/tools/dfs_order_test.py`).

## Base provenance (byte-exact dfs_order tree)

The base tree is `/tmp/dfs-order-build`, the tree of the host-built order
module `a5ec150c` (391,408 B) rebuilt by
[`../wifi-5g-dfs-order/tools/build-dfs-order.sh`](../wifi-5g-dfs-order/tools/build-dfs-order.sh)
(which itself re-derives the dfs_set tree `1b47f0da` from the byte-exact R5
base `24f7221b`). The build script asserts:

| item | pin |
| --- | --- |
| `/tmp/dfs-order-artifact/hi5622_wifi.ko` (base module) | 391,408 B / `a5ec150c...` |
| `/tmp/dfs-order-build/hi5622_scan.c` (base) | `d2e894d4b24b...` |
| `/tmp/dfs-order-build/hi5622_scan_logic.c` (base) | `4400d546f4da...` |
| `/tmp/dfs-order-build/hi5622_dfs_set.h` (base) | `bb25e2faa4c9...` |
| `/tmp/dfs-order-build/hi5622_dfs_set.c` (base) | `f7631f7cbfcd...` |

and fails closed if the base is absent or drifted (it re-runs the order build
script in that case).

## What the patch changes (exactly three files, no frame byte, no new param)

| file | change |
| --- | --- |
| **hi5622_dfs_set.h** | the two arm kinds + the four CAC-word offsets (`+0x49C/+0x4A0/+0x4A4/+0x4A8`) documented; the probe gains `b49c/b4a4/b4a8` and `ctx_present`; `precheck` now takes a writable probe (it records `ctx_present`); the `-EBUSY` ARM refusal is removed from the contract; the new postcheck signature (`const struct hi5622_dfs_set_probe *p, u8 mode`) and `hi5622_dfs_set_arm_kind()` are declared |
| **hi5622_dfs_set.c** | `precheck`: `p->ctx_present = p->b4a0 != 0u ? 1u : 0u`; no `-EBUSY`; the CAC-flag gate and the CLEAR gates are unchanged. Verdict: the register-only-arm message replaces the old "worker would return" string. `postcheck`: ARM/ARM_CLEAR require the 0x2D armed pattern then `ctx_present ? cac==0 : cac==1`; CLEAR requires `cac==0`; NULL/bad mode `-EINVAL`. New `arm_kind()` -> "register-only"/"full"/"?". |
| **hi5622_scan.c** | the probe reads `+0x49C/+0x4A0/+0x4A4/+0x4A8` and derives `ctx_present`; the `dfs_set pre:` line prints the four words + `ctx_present=`; a new read-only `dfs_ctx pre0x0065:` line runs immediately before the 0x0065 frame post (E1); the `dfs_set arm post:` line prints `ctx_present=`/`arm_kind=` and the register-only PASS/FAIL text; the ARM_CLEAR pre-clear refusal records `ctx_present` and stays fail-closed. The ladder order, the frame builders and the module parameters are untouched. |

## Patched-input pins

| file | sha256 |
| --- | --- |
| patches/0001-dfs-relax.patch | `a7e1a2862776...` |
| hi5622_dfs_set.c (patched) | `9c64594dc4a5...` |
| hi5622_dfs_set.h (patched) | `f8bb83b3d5a9...` |
| hi5622_scan.c (patched) | `75b12c4dc4b6...` |

## Reproduce

    opensource/docs/incoming/wifi-5g-dfs-relax/tools/build-dfs-relax.sh

Outputs: `/tmp/dfs-relax-build` (patched tree), `/tmp/dfs-relax-artifact`
(module, the C selftest binary, the three test logs). The whole-file sha is
reproducible only at the canonical `/tmp` paths (out-of-tree build metadata is
path-sensitive - the same R5/dfs_order caveat); `.text`, params and the
patched-input shas are stable.

## Notes / false starts / limits

* The precheck signature had to change from `const` to writable: it now
  records `ctx_present` (per the cac-context spec). The probe fill also
  derives the field so the `dfs_set pre:` line prints it before the precheck
  call - the value is consistent either way.
* The first build failed with `conflicting types for hi5622_dfs_set_precheck`
  (the header was updated but the `.c` definition still had `const`); fixed
  before the pinned build. The pinned artifact is from the clean build above.
* `C-NOG0` behaves differently from the cac-context sketch: the fail-closed
  state gate refuses the ARM before the wire, so the worker's gate-3 no-op is
  not reachable from this module (see [FIRE-RECIPE.md](FIRE-RECIPE.md) R3).
* Host-only; the fire belongs to the Lead and follows
  [FIRE-RECIPE.md](FIRE-RECIPE.md). The local `.ko` carries no vendor bytes;
  register addresses and the 29-B frame bytes are addresses/host-built protocol
  bytes.
