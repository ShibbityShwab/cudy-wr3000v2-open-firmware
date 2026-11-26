# BUILD-NOTES - lane dfs160b (task-245): the reproducible host build

**HOST-ONLY.**  No device access, no firmware write, no flash.  The `.ko` and
the patched tree live in `/tmp` and are LOCAL EVIDENCE ONLY - this repo
stores no binary artifacts (the lane commits sources, the patch, the build
script and the logs of derived values).

## Base lineage

The patch is authored against the **bench-fired `wifi-5g-dfs-relax` A-rung
tree** (`0x0065 VAP start -> 0x07E7 ARM -> 0x01F7 dfs_mode -> 0x0242 band
start`), the same clean-room 6.18.60 module family the other image #3 fire
lanes build against.  It is reproduced by
[`../wifi-5g-dfs-relax/tools/build-dfs-relax.sh`](../wifi-5g-dfs-relax/tools/build-dfs-relax.sh)
at `/tmp/dfs-relax-build`; `tools/build-dfs160b.sh` re-derives it on demand
in the sibling lane and fails closed on every sha drift.

Base file pins (sha256; `tools/build-dfs160b.sh` asserts each):

| file | sha256 |
| --- | --- |
| Kbuild | `8f97661898373f8b65befcc3a62b6b00407101430e2924d74f142904d518c9e9` |
| hi5622_scan.c | `75b12c4dc4b60dc458153497b1fc4cac6927d5ede85b98f5fdb2b8efdbb71273` |
| hi5622_scan.h | `bce2eb14175a28b9776ffeafea7815f1914596be5aef0f0928cc45e6ecae8ec9` |
| hi5622_cfg80211.c | `81d7084a712b6585afab1b8fa2c8cd550304f2392c4476a7a9b8be1fd6128eb1` |
| hi5622_cfg80211.h | `73998e99cf098dbc3e36e12f7c7427b39a1000e46cf97c10a2c48ec250f8e49e` |
| hi5622_cfg80211_ops.c | `5bb68c77e7cf04d5b59ae2d35d8b4f65949072a5d34102e596bd4d1067dae6ac` |
| hi5622_frames_api.h | `393ad0af2d225a8cb66d5edc8292746875e7796507a9ee4dc6eceb9240d80bc3` |
| hi5622_ap_frames.h | `07780062a3b4cd2a6da3cbadb461ec26c29a88ed576e477a60519e10f53fe638` |
| hi5622_ap_frames.c | `1bb16d359c5f838b0b4c15510c87f9e0e321f6567d53d924b8abe439bf39885f` |
| hi5622_vapscan_provider.c | `b66da889342e35fb868c154c49c467dd7a0e1b85df6599b5574ca85513a7b764` |
| cfg80211-provider.symvers | `bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7` |

## The patch (14 files, 4 new)

`patches/0001-dfs160b.patch` - sha256
`61177c2a2e502d0858f0069e71923f49f520f2b07713b21468002d5091686c6d`,
git-apply verified; the four new files are byte-identical to the committed
`src/` files (asserted by the build script and the Python patch test).

| file | change |
| --- | --- |
| hi5622_dfsb.h / .c | NEW: the portable 160 MHz/DFS/CAC/NOL model |
| hi5622_dfsb_k.h / .c | NEW: the kernel runtime + cfg80211 ops + parameters |
| Kbuild | + `hi5622_dfsb.o hi5622_dfsb_k.o` |
| hi5622_frames_api.h | `hi5622_chan_cfg_params.bandwidth` + the two AP centre freqs |
| hi5622_scan.c | the `ht_bw`/`dfs_cac` gates, the start_ap plan call, `cp.bandwidth`, the exported live probe |
| hi5622_scan.h | the probe declaration (+ the dfs_set include) |
| hi5622_ap_frames.h/.c | `bandwidth` in the haf params; `body[1] = bandwidth` |
| hi5622_vapscan_provider.c | copy/validate the selector into the 0x0124 builder path |
| hi5622_cfg80211.c | ch144 in the template list, the TDWR `dfs_cac_ms`, the runtime init/exit |
| hi5622_cfg80211_ops.c | `.start_radar_detection` / `.end_cac` |
| cfg80211-provider.symvers | + `cfg80211_cac_event` (the luofu tree builds without CFG80211; the OpenWrt kernel provides the symbol at load time) |

The kernel glue carries seven `_Static_assert`s pinning the nl80211 width
enum values to the model constants (the sibling-lane defect class: a UAPI
drift becomes a build error, not a silent wrong selector).

## Build command (what the script runs)

    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/\
         target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    rsync -a <pinned base>/ /tmp/dfs160b-build-work/     # excludes build artefacts
    ( cd /tmp/dfs160b-build-work && patch -p1 -s < <lane>/patches/0001-dfs160b.patch )
    make -j4 ARCH=arm CROSS_COMPILE=$CROSS -C $KDIR M=/tmp/dfs160b-build-work modules
    NO_RENAME=1 sh <openwrt>/scripts/strip-kmod.sh /tmp/dfs160b-build-artifact/hi5622_wifi.ko

## Artefact pins (installed form, OpenWrt strip NO_RENAME=1)

    /tmp/dfs160b-build-artifact/hi5622_wifi.ko
    383,020 B  sha256 006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183
    68 params (66 base + ht_bw + dfs_cac)
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8; depends cfg80211
    .text 164,980 B sha256 5e14008fab4707afdf55ab3ea864129abfc65979c3e4b0b2e572f6593ead5faa
    unstripped 1,493,920 B sha256 f6c6481ab4c3c9b7c287466ac989b7a26d52166597e53596e40ba1066e8dd499

The build is deterministic from base+patch (the script rebuilt it twice; same
pins).  Build warnings/errors: **0 / 0**.

## Host tests

    tools/run-host-tests.sh        # 269 checks / 0 failures (C, userspace only)
    tools/dfs160b_patch_test.py    # independent integration/order test (Python)

Both are userspace-only: no kernel headers, no device, no vendor artifact.
The C test is in `src/dfs160b_selftest.c`; it exercises the width mapping
(incl. the real 6.18 value 5 = 160 MHz and 4 = 80+80 refusal), the channel
table/ch144/TDWR, the 160/80 CCFS1, the full CAC tick sequences (60 s = one
63 s tick; TDWR 600 s = 63 s + 9 x 60 s; the u32-wrap NOL gate), the 29-B
0x07E7 ARM/CLEAR KAT, the order contract and the start_ap plan/gate matrix.

## Reproduce (both steps, host only)

    opensource/docs/incoming/wifi-5g-dfs-relax/tools/build-dfs-relax.sh   # the base
    opensource/docs/incoming/dfs160b/tools/build-dfs160b.sh              # the feature

## Limits

- The artifact is **not** the image #3 module lineage directly: it is the
  fired dfs-relax A-rung lineage (the current fire lineage for the DFS/160
  feature).  A future rebase onto a newer image module follows the same patch
  (the feature delta is self-contained in the new files + the named edits).
- No bench contact.  Nothing here proves the boot, the frame consumption or
  the on-air result; those are FIRE-RECIPE rungs.
