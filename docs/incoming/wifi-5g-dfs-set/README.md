# wifi-5g-dfs-set - the H2D key 0x07E7 (DFS SET): the band-1 SMAC sub-base's firmware writer, wired into the R5 5 GHz ladder

**Lane:** `wifi-5g-dfs-set` (shared task **task-227**), 2026-11-26.
**HOST-ONLY**: firmware decode + clean-room driver patch + host build; no device
contact, no fire. The Lead fires with the committed procedure.

## One paragraph

The band-1 SMAC chain is the 5 GHz beacon's last lever: the bench (2026-11-26
21:00) found the band-1 SMAC sub-base `[0x4010E000]` bit0 = **1** on the stock
and **0** on our R5 boot, and that a manual register flip alone did not put the
beacon on air. The committed `wifi-5g-smac-keys` decode names the missing
writer: the H2D g0-id1 config-event key **0x07E7** (the DFS SET), the only
stock-only handler that writes that sub-base - and our ladder never posted it.
This lane decodes the frame and the firmware worker fully (29-B carrier, 1-byte
body `01` = ARM / `00` = CLEAR, six sub-base u16 bit0 writes + the CAC-active
flag, worker CA 0xB58FC), adds a fail-closed `dfs_set` knob + the frame builder
+ the posting step to the byte-exact R5 module (after the 0x0065 VAP start,
before the 0x0242 band start), builds and tests it on the R5 base, and writes
the fire rung with probes, expected and abort rules.

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the full decode: frame, worker, gates, CAC machinery, driver integration, build, fire summary, limits |
| [TABLES.md](TABLES.md) | every address/key/register/gate/pin in one place |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the ch40 5 GHz AP rung: baseline (dfs_set=0), ARM rung (dfs_set=1), probes D1-D9, PASS/FAIL/ABORT, ARM+CLEAR rung (dfs_set=2), negative rungs |
| [BUILD-NOTES.md](BUILD-NOTES.md) | the R5-base build chain, the patch, the module pins, reproduce |
| [tools/fw_dfs_set_re.py](tools/fw_dfs_set_re.py) | the firmware re-derivation (**89/89** selftest) |
| [tools/build-dfs-set.sh](tools/build-dfs-set.sh) | host build on the byte-exact R5 base + host tests |
| [tools/dfs_set_frame_test.py](tools/dfs_set_frame_test.py) | independent integration/order test (**46/46**) |
| [src/dfs_set_selftest.c](src/dfs_set_selftest.c) | the C contract test (**84/84**) |
| [patches/0001-dfs-set.patch](patches/0001-dfs-set.patch) | the driver patch on the R5 tree (33,313 B) |
| [evidence/](evidence/) | tool selftest/dump, build run, host tests, pins |

## The result in pins

* frame: `key 0x07E7, body {01}/{00}, 29 B`, ARM pattern `{1,0,1,1,0,1} = 0x2D`;
* module on the R5 base: **391,388 B / 66 params / sha256
  `1b47f0daff7e...`** (`.text` `a8b737a46610...`), local evidence only;
* knob: `dfs_set` 0=off (default), 1=ARM, 2=ARM+CLEAR, 3=CLEAR, >3 refused;
* order: `... vap_start_g0(0x0065) -> dfs_set(0x07E7) -> band_start(0x0242) ...`;
* host tests: 84 C + 46 Python, 0 failures.

## Read in this order

1. [REPORT.md](REPORT.md) sec. 0 (TL;DR) and sec. 2 (the decode).
2. [TABLES.md](TABLES.md) T3 (registers) and T7 (the bench datum + probes).
3. [FIRE-RECIPE.md](FIRE-RECIPE.md) - the Lead's procedure.
4. [BUILD-NOTES.md](BUILD-NOTES.md) - how the module was built.

## Reproduce

    .provision-venv/bin/python tools/fw_dfs_set_re.py <FIRMWARE.bin> --selftest   # 89/89
    tools/build-dfs-set.sh                                                      # ~2-3 min

The .ko is never committed (the repo carries no binaries); the patched tree and
the module stay in `/tmp`.

## Honesty / limits

* The vendor CAC **state machine** (60 s / 600 s CAC timer, 30 min NOL,
  `hmac_start_all_bss_of_band` completion) is **not** implemented: this lane
  posts the SET/CLEAR frame only - the missing firmware sub-base writer.
* The beacon on 5200 MHz is **not guaranteed** by this rung; PASS is the
  firmware writer verified (pattern 0x2D + CAC flag 1 + unchanged R5 state +
  page golden). The beacon is the optional jackpot.
* The manual bar0write3 sub-base flip is retired; never hand-write these
  registers during the fire.
* The module and the build are host artifacts; the box is untouched by this lane.
