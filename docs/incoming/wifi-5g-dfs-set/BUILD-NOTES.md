# BUILD-NOTES - the dfs_set module on the byte-exact R5 base

**Lane:** wifi-5g-dfs-set (shared task **task-227**), 2026-11-26. **HOST-ONLY**:
nothing here was fired, flashed or read from the device. The rebuilt `.ko` is
**local evidence only** (`/tmp`), never committed (the repo carries no binaries).

## Result

The R5 module (`/tmp/r5-rebuilt/hi5622_wifi.ko`, 383,240 B, sha256
`24f7221b...`) plus this lane's patch produces:

    /tmp/dfs-set-artifact/hi5622_wifi.ko
    391,388 B (stripped)   sha256 1b47f0daff7e2e48d0ac6dcbc34b4135872f6a16088e8d20b88c71d47c684acb
    66 params (65 + the new dfs_set)   vermagic 6.18.60 SMP mod_unload ARMv7 p2v8
    .text sha256 a8b737a466106cfed51fecc54f4e836a23b6e2509056681a76feaeb3f558d335

Host tests inside the build: **84/84** C contract checks
([evidence/host-selftest-c.txt](evidence/host-selftest-c.txt)) + **46/46** independent Python
integration checks ([evidence/host-test-python.txt](evidence/host-test-python.txt)).

## Base provenance (why this is the byte-exact R5 base)

The base tree is the one `../wifi-r5-rebuild/tools/reproduce.sh` rebuilds from
the committed chain and pins end-to-end (see `../wifi-r5-rebuild/CHAIN.md`).
The build script asserts:

| item | pin |
| --- | --- |
| `/tmp/r5-rebuilt/hi5622_wifi.ko` (base module) | 383,240 B / `24f7221bfc4c...` |
| `/tmp/r5-build/hi5622_scan.c` (pristine) | `86d6c57ecc98...` |
| `/tmp/r5-build/Kbuild` (pristine) | `1bb510853b30...` |

and fails closed if the base is absent or drifted (it re-runs the R5 reproduce
script in that case). The patch is pinned too: `patches/0001-dfs-set.patch`,
33,313 B, sha256 `5030f4076d03...`.

## What the patch adds

1. **`hi5622_dfs_set.h/.c`** (new, pure C99): the fail-closed plan, the 29-B
   g0-id1 key-0x07E7 frame builder (body `{01}` arm / `{00}` clear), the six
   band-1 SMAC sub-base CA constants + arm-pattern helper, the pre/postcheck
   gate logic, the verdict strings and the sysfs status line. It never touches
   the wire or the device itself; the kernel layer calls it.
2. **`hi5622_scan.c`**: the `dfs_set` module parameter (0..3, 0444, default 0),
   the two fail-closed parameter gates next to the sibling `dfs_mode` gates,
   the read-only probe helper (`hi5622_scan_dfs_set_probe`: mctx/band/state/
   ec8/b4a0/b4ac/cac from firmware RAM + bit0 of the six device registers), the
   post site in the AP-start ladder, the post-read evidence lines and the
   status-line append.
3. **`Kbuild`**: `hi5622_dfs_set.o` added to `hi5622_wifi-y`.

The post site is placed **after the 0x0065 VAP-start step and before the 0x0242
band-start step** (the R5 order). Rationale: the 0x07E7 worker CA 0xB58FC's own
gate 2 is the started-module scan CA 0xA4C0E (it needs >= 1 module context with
`u8[ctx+0x0E]` in 1..3); in our ladder the only writer of that byte is the
0x0065 VAP start, so the ARM frame can only take effect after it. The vendor's
host order is "CAC armed before the MAC/BSS start", which this placement
preserves (0x07E7 before the 0x0242 MAC start).

## Knob contract (fail-closed)

| `dfs_set` | behaviour |
| ---: | --- |
| `0` (default) | no 0x07E7 frame - byte-identical to the R5 ladder |
| `1` | ARM: post body `{01}` after the 0x0065 VAP start, before the 0x0242 band start |
| `2` | ARM then CLEAR back-to-back (both frames; the readback must show CAC flag 1 then 0) |
| `3` | CLEAR only (requires the armed register pattern + CAC flag 1) |
| `>3` | refused `-EINVAL` before any frame |

Every non-zero value additionally requires the `beacon_static=1` vendor-order
ladder, band 1, a resolved module context, the helper object
`u32[mctx+0x4AC] != 0`, `mctx[0x0E] in 1..3`, and `bandobj+0xEC8 == 1`; the
ARM half also requires `u32[mctx+0x4A0] == 0` and CAC flag 0, and the sequence
aborts (no band start) if the post-read does not show the expected flag. The
full gate table is in [TABLES.md](TABLES.md) T5 and the C contract in
[REPORT.md](REPORT.md) sec. 3.

**Not implemented:** the vendor CAC state machine (the 60 s / 600 s CAC timer,
the 30 min NOL, `hmac_start_all_bss_of_band` on CAC completion). This lane
posts the SET/CLEAR frame only - the missing firmware sub-base writer. A full
CAC state machine is a later rung (see `../wifi-5g-160-dfs/REPORT-DFS.md`).

## Reproduce

    opensource/docs/incoming/wifi-5g-dfs-set/tools/build-dfs-set.sh

The script reuses `/tmp/r5-build` + `/tmp/r5-rebuilt/hi5622_wifi.ko` when
present; otherwise it calls `../wifi-r5-rebuild/tools/reproduce.sh` first
(~130 s). Outputs: `/tmp/dfs-set-build` (patched tree),
`/tmp/dfs-set-artifact/*` (module, selftest logs). The whole-file sha is
reproducible only at the canonical `/tmp` paths (out-of-tree build metadata is
path-sensitive - same R5-lane caveat); `.text` and params are stable. The
build script asserts the pinned whole-file sha itself, so a drift fails closed.

## Limits / honesty

* Host-only; the fire belongs to the Lead and follows [FIRE-RECIPE.md](FIRE-RECIPE.md).
* The baseline state (sub-base bit0 = 0 on our boot) is the committed bench datum
  of 2026-11-26 21:00; this lane re-derived the *firmware* facts, not the bench.
* The `.ko` is local evidence; no vendor firmware bytes are committed. The six
  register CA values and the frame bytes are addresses/host-built protocol
  bytes, which the project convention allows as quoted text.
