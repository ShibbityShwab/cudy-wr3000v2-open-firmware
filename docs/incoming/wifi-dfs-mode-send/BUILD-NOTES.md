# BUILD-NOTES - lane wifi-dfs-mode-send (task-190)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The `.ko` is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts; the repo's public half must never contain vendor objects,
and this artifact is our own host-built module but still stays local by the
lane convention).

## Chain

    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh
      = /tmp/beacon5g-build + /tmp/beacon5g-artifact/hi5622_wifi.ko
        356,600 B sha256 bc1cc0ea...a1bbb, 59 params (beacon5g gate)
    ../wifi-r1-band-diff/tools/build-r1-band.sh
      = /tmp/r1-band-build + /tmp/r1-band-artifact/hi5622_wifi.ko
        356,612 B sha256 e8b17f72...4abd4, 59 params (the CW-0/R1 module;
        the r1-band one-field VAP-start band fix is applied here)
    ../wifi-desc-table-send/patches/0001-desc-table-send.patch
      = the H2D 0x00F7 descriptor-table send + the fail-closed desc_table
        parameter (60 params, 360,448 B sha aac6177c...e800e3)
    this lane's patches/0001-dfs-mode-send.patch
      = the H2D 0x01F7 DFS/RF mode send + the fail-closed dfs_mode parameter
        (61 params)

`tools/build-dfs-mode-send.sh` automates the last two steps; it rebuilds the
base chain (beacon then r1-band) only when `/tmp/r1-band-build` is absent,
and pins every hop:

| pin | value |
| --- | --- |
| beacon5g base provider | sha256 `bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828` (the r1-band script's own pin) |
| r1-band PATCHED provider | sha256 `c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab` |
| r1-band base artifact | `/tmp/r1-band-artifact/hi5622_wifi.ko`, 356,612 B sha256 `e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4`, 59 params |
| sibling 0x00F7 patch | sha256 `9c42076f4f5faecf352f6aecf5d6ba8b88edb73a454e26872ec543233cdfce2f` |
| sibling `hi5622_desc_table.c` / `.h` | sha256 `976c544dbabfcb29f32530f083903ca6f708f7cbb8407576d578199a99664d9f` / `5321d1f7ff1f5e494ba65013fb815861d972e6f2161bf1ce8e0466665551f9d1` |

The r1-band build script pins the *unpatched beacon* provider bc7e3ede; the
patched tree's provider is c306e75f (same file + the one-field fix) - both are
checked when the beacon tree is present.

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped artifact | /tmp/dfsmode-artifact/hi5622_wifi.ko, **364,192 B** |
| sha256 | `f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac` |
| unstripped | 1,381,900 B |
| .text sha256 | `3fdcad2ddb1a0c2e81c3ca1dfb1f65058551e09976e3765b5d08081e7b2b6186` |
| params | 61 (desc-table-send 60 + `dfs_mode`) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines (unchanged) |
| size delta | +3,744 B vs the 360,448 B sibling 0x00F7 artifact |
| key immediate | `objdump -d hi5622_dfs_mode.o` in the tree: `movw r3, #503 @ 0x1f7` (the same MOVW form as the stock emitter) |

`dfs_mode` is a read-only (0444) uint parameter, default **0** (off =
current behaviour); the built module's .modinfo carries
`parmtype=dfs_mode:uint` (the 6.18 loader drops MODULE_PARM_DESC strings, so
the description is checked in the patched source, not in the artifact).

## Contract checks implemented by the build script

1. the beacon5g base provider (when present) equals the r1-band lane's pin;
2. the r1-band patched provider equals `c306e75f...` and the base artifact
   equals 356,612 B / `e8b17f72...` (refuse a drifted base);
3. the sibling 0x00F7 patch sha and its two portable sources equal the pins
   (refuse a different desc-table base under this lane's name);
4. the sibling patch applies to the pinned tree and its sources match;
5. this lane's patch applies to the desc-table patched tree with no
   fuzz/conflict markers;
6. `src/hi5622_dfs_mode.{c,h}` are byte-identical to the patched driver
   sources (source-drift guard);
7. module build 0 warnings/errors/undefined symbols;
8. debug-strip does not change the .text;
9. artifact contract: 61 params incl `dfs_mode`, `dfs_mode` declared as a
   read-only uint with a MODULE_PARM_DESC in the patched source, the 0x01F7
   dmesg/status feature strings, 12 cfg80211 symvers.

## Host tests

    ./tools/run-host-tests.sh

Compiles the portable model (src/) and the real patched ordering model
(hi5622_scan_logic.c from the tree) with a plain C99 host compiler and runs
`tools/order_test.c` and `tools/dfs_mode_send_test.py` (independent frame
re-derivation from the sibling carrier + source order/contract checks).
Result: **133 checks, 0 failures** (evidence/host-tests.txt).

| part | checks | result |
| --- | ---: | --- |
| 1. portable model (`src/dfs_mode_selftest.c`) | 54 | 0 failed |
| 2a. real frame_post validator (`tools/validate_frame_test.c`) | 15 | 0 failed |
| 2b. ordering contract (`tools/order_test.c`) | 14 | 0 failed |
| 2c. independent cross-check (`tools/dfs_mode_send_test.py`) | 50 | 0 failed |
| 3. artifact contract (when built) | PASS | 364,192 B, sha `f5408567...`, 61 params |

No device contacted.

## Reproduce

    ./tools/build-dfs-mode-send.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.
