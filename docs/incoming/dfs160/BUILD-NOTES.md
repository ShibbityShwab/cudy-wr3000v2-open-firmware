# BUILD-NOTES - pinned base, patch, artifacts and reproduce

**Lane:** dfs160 (task-240), 2026-11-26.  **HOST-ONLY**: built on the local
host against the project 6.18.60 kernel tree; no device contact, no firmware
write, no image, no flash.  The `.ko` is **local evidence only and is never
committed**; the committed outputs are the sources, the patch, the tools and
the text evidence.

## 1. The base

The patch is authored and tested on the **wifi-5g-dfs-relax A-rung lineage**
(the module that was fired on 2026-11-27 01:00 and passed the register-only
ARM: 66 params, order
`0x0065 vap_start_g0 -> 0x07E7 dfs_set ARM -> 0x01F7 dfs_mode -> 0x0242
band_start`).  The tree is reproduced by the sibling lane:

    ../wifi-5g-dfs-relax/tools/build-dfs-relax.sh    # -> /tmp/dfs-relax-build

Pinned base files (asserted by `tools/build-dfs160.sh`):

| file | sha256 |
| --- | --- |
| Kbuild | `8f97661898373f8b65befcc3a62b6b00407101430e2924d74f142904d518c9e9` |
| hi5622_scan.c | `75b12c4dc4b60dc458153497b1fc4cac6927d5ede85b98f5fdb2b8efdbb71273` |
| hi5622_scan.h | `bce2eb14175a28b9776ffeafea7815f1914596be5aef0f0928cc45e6ecae8ec9` |
| hi5622_scan_logic.c | `4400d546f4da045643bad1588b005ca6ad4774ba82d2d80a6d8c4b904e355f3c` |
| hi5622_scan_logic.h | `6af774ef76b730c8f45d621bf338d12b041b96a3792be985df59ac5092a603e8` |
| hi5622_cfg80211.h | `73998e99cf098dbc3e36e12f7c7427b39a1000e46cf97c10a2c48ec250f8e49e` |
| hi5622_cfg80211.c | `81d7084a712b6585afab1b8fa2c8cd550304f2392c4476a7a9b8be1fd6128eb1` |
| hi5622_cfg80211_ops.c | `5bb68c77e7cf04d5b59ae2d35d8b4f65949072a5d34102e596bd4d1067dae6ac` |
| hi5622_frames_api.h | `393ad0af2d225a8cb66d5edc8292746875e7796507a9ee4dc6eceb9240d80bc3` |
| hi5622_ap_frames.h | `07780062a3b4cd2a6da3cbadb461ec26c29a88ed576e477a60519e10f53fe638` |
| hi5622_ap_frames.c | `1bb16d359c5f838b0b4c15510c87f9e0e321f6567d53d924b8abe439bf39885f` |
| hi5622_vapscan_provider.c | `b66da889342e35fb868c154c49c467dd7a0e1b85df6599b5574ca85513a7b764` |
| cfg80211-provider.symvers | `bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7` |

## 2. The patch

`patches/0001-dfs160.patch` (sha256
`98bc135ae4542fe79ce41f6ec34ad7f182ef292aef5517429488b5d68b1ec449`,
1,456 lines, 15 files, 4 new).  Applied with `patch -p1` to a fresh copy of
the pinned base it reproduces the built tree file-for-file (verified in
`evidence/BUILD.txt`); the patch also carries the four lane sources verbatim
(the build script asserts `src/*` == patched tree files).

Changes: the new portable model and runtime, two Kbuild objects, the
`cfg80211_cac_event` provider symvers row, the `ht_bw` + `dfs_cac` gates and
the 0x0124 selector in `hi5622_scan.c`, the `bandwidth` field through
`hi5622_frames_api.h` -> `hi5622_ap_frames.{h,c}` ->
`hi5622_vapscan_provider.c`, the runtime/ch144/status wiring in
`hi5622_cfg80211.{c,h}`, and the two DFS ops in
`hi5622_cfg80211_ops.c` (full map: TABLES T9).

## 3. The build

Command: `tools/build-dfs160.sh` (full log `/tmp/dfs160-artifact/build.log`,
summary in [evidence/BUILD.txt](evidence/BUILD.txt)):

    base copy -> patch -p1 -> make ARCH=arm CROSS_COMPILE=<OpenWrt 6.18.60 gcc> \
        -C <selectorfix-img 6.18.60 kernel tree> M=/tmp/dfs160-work modules
        -> 0 warnings, 0 errors
    strip-kmod.sh (OpenWrt, NO_RENAME=1) -> the installed form

| artifact | size | sha256 |
| --- | ---: | --- |
| installed (/tmp/dfs160-artifact/hi5622_wifi.ko) | **384,072 B** | **`0313dcfd5b88af683b9c02221d0e38020885a21200480f9ea4b02be2b89a02ff`** |
| installed `.text` | 165,236 B | **`ec2e7d9f603845657942a40a0867eef6a1ee5eabbccc7c212481406bc543783c`** |
| unstripped | 1,494,040 B | `b850cb11a760b4e10f68e782b6f9845ab587d1fd44b163941bdd8a18d4037e31` |

Parameters: **68** (66 base + `dfs_cac` + `ht_bw`); `modinfo -p` lists both.
The installed sha is pinned for `WORK=/tmp/dfs160-work` because the module
embeds build-path strings - another build path yields another file hash but
the **same `.text` hash** (the stable functional identity; the fire recipe
accepts either the installed sha or the .text sha).  The kernel tree must be
one with `Module.symvers` (the build script picks
`/home/shibbityshwab/openwrt-lab-selectorfix-img` first) because the module
imports cfg80211 symbols from the provider symvers.

## 4. Host tests

`tools/run-host-tests.sh` compiles `src/hi5622_dfs160.c +
src/dfs160_selftest.c` with the host `cc` (`-std=c99 -Wall -Wextra -Werror`)
and runs it: **133 checks, 0 failed** ([evidence/HOST-TESTS.txt](evidence/HOST-TESTS.txt)).
Coverage: the whole width map and segment checks, 160/80 CCFS1, DFS set +
TDWR window, ch144, the CAC plan (60 s / 600 s / longer request / TDWR floor),
the vendor 63 s + 60 s tick schedule (603 s total), the state machine, the
AP-start gate, radar/NOL, and the status line.

## 5. What is not here

No .ko, no firmware bytes, no vendor object, no calibration value, no MAC.
The patched tree and the module stay in `/tmp`; only source, patch, tools,
docs and text evidence are committed.  The radar D2H event is not decoded
([U]) and no radar event path is shipped.

## 6. Rebase note (next integration step)

The base is the fired A-rung lineage, not IMAGE #3's merged module tree.  The
portable model and the runtime are lineage-agnostic; the rebase onto IMAGE #3
is a separate step because that lineage has no 0x0065/0x0242 ladder steps (the
A-rung order - the prerequisite for the 0x07E7 ARM - does not exist there
yet), so the DFS order integration must come with the band-1-start merge.
`ht_bw` + ch144 do not depend on that order and are mechanical to rebase.
