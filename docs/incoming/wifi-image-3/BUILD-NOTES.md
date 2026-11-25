# BUILD-NOTES - wifi-image-3: exact build record and reproducibility

**Lane:** wifi-image-3 (task-196). HOST-ONLY. The built container/module are
local artifacts; this document is the regeneration recipe.

## 1. Environment

| item | value |
| --- | --- |
| host | Linux workstation, repo root `/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2` |
| base rootfs | `/home/shibbityshwab/wifi-image-update-artifacts/root.squashfs` + `rootfs.ubi` |
| image #2 container | `/home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin` |
| module artifacts | `/home/shibbityshwab/wifi-image-3-module-artifacts/{hi5622_wifi.activity.ko,hi5622_wifi.installed.ko}` |
| ubinize | `/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/host/bin/ubinize` (mtd-utils 2.3.1) |
| ubinize cfg | `.../openwrt-lab-selectorfix-img/target/linux/hisilicon/image/luofu-ubinize.cfg` (checked-in two-volume layout) |
| mksquashfs | host squashfs-tools 4.7.5 |
| sudo | required only for the faithful squashfs extraction/mksquashfs (device nodes; all files root-owned) |

## 2. Pinned inputs (the bake aborts on drift)

    base root.squashfs   b461df0cbc804587fdb789030b368b79d3544c4caa7c5f329a4ab0828e8cb77a (3,997,696 B)
    base rootfs.ubi      d3c665f16e76811d741bcaa7a670a6467e6a8c3ad0d49f61363a0157e2832d57 (4,456,448 B)
    image #2 container   8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8 (7,188,799 B)
    kernel slice         13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f (3,372,638 B)
    module activity      545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99 (379,092 B, 64 params)
    module installed     a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed (358,544 B, 64 params)

## 3. The bake (one command, ~2 s of actual work)

    cd <repo>/opensource/docs/incoming/wifi-image-3
    ./tools/selftest.sh                 # 102 passed, 0 failed, 0 skipped
    ./tools/bake-image-3.sh             # build + verify + local artifacts + evidence
    # artifacts -> /home/shibbityshwab/wifi-image-3-artifacts/
    # evidence  -> build-evidence/ (text only; the work dir is gitignored)

Steps: input assertions -> extract the base squashfs (sudo) -> install the
image #3 module in the image's strip-kmod form -> apply the text payload ->
`mksquashfs -comp xz -b 262144 -Xbcj arm -noappend -mkfs-time 1794002400
-all-time 1794002400` -> `ubinize -Q 1003 -p 131072 -m 2048 -s 2048 -O 2048`
(checked-in cfg) -> re-assemble the sysupgrade container around the unchanged
kernel member (fixed mtime 2026-11-27 00:00:00 +0700, owner 0/0) -> verifier.

The fixed squashfs times, the fixed UBI image sequence number (`-Q 1003`) and
the fixed tar mtime make the build byte-reproducible.

## 4. Reproducibility record

Three consecutive bakes produced the identical container:
`4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53`
(7,843,840 B; build-evidence/build-runs.txt). The rootfs/UBI/squashfs hashes
are identical across the runs too. The module artifacts are fixed inputs.

## 5. The module build (how the pinned .ko artifacts were produced)

See build-evidence/module-build/source-chain.txt for the patch chain and
build-evidence/module-build/{artifacts.txt,symbol-preservation.txt,
merged-source-delta.txt,host-tests-*.txt} for the validation. Summary:

    F2 source tree (r1-band base + desc-table + dfs-mode + field-B fix)
      + wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
      + wifi-ba-agg/patches/0001-ba-skeleton.patch
      + wifi-ax-he-caps/patches/0001-he-caps.patch (cfg80211 hunk) + src/*.c/.h
      = 64-param merged module
    make ARCH=arm CROSS_COMPILE=<openwrt gcc14.4 muslgnueabi->          -C <openwrt-lab-upstream-target/.../linux-6.18.60> M=$PWD modules
    strip --strip-debug                      -> activity artifact
    openwrt scripts/strip-kmod.sh (NO_RENAME=1) -> installed form

Toolchain: `/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir/
toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-`;
KDIR 6.18.60. 0 warnings/errors. The installed form is exactly the image #2
lane's strip transformation (reproduced against the image #2 module: same
invocation -> the known 328,788 B `b4c076bf...` installed copy), which is why
the installed-form hash can be pinned.

## 6. Evidence index (all text; committed)

| file | content |
| --- | --- |
| build-evidence/build-summary.txt | the authoritative build record + all identities |
| build-evidence/build-runs.txt | the three identical runs |
| build-evidence/hashes.txt | local artifact hashes |
| build-evidence/verify.txt | verifier output (129/129) |
| build-evidence/ubi-extract.txt / base-ubi-extract.txt | UBI vol0 round-trips (new + base) |
| build-evidence/squashfs-superblock.txt | the built squashfs options |
| build-evidence/mksquashfs.log / ubinize.log | tool logs |
| build-evidence/payload-manifest.txt | the 524-file rootfs manifest (path + sha256) |
| build-evidence/module-build/ | module chain, artifacts, symbol preservation, host tests, merged-source delta |

## 7. If the module or the payload changes

1. payload change: the bake picks up `payload/` automatically; the verifier's
   byte-identity + expected-file-count (524) checks pin it - update the count if
   files are added/removed.
2. module change: rebuild the .ko, re-derive the installed form with
   `strip-kmod.sh` (NO_RENAME=1), then update the pins in
   `tools/bake-image-3.sh`, `tools/verify-image-3.sh` and `HASHES.txt`
   together, and re-run the bake three times.
3. base change: the base hashes are pinned; do not silently follow a new base -
   a new base is a new image, with its own verification.
