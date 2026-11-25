# BUILD-NOTES - the merged-module base, the TWT change, and the artifact pins

**Lane:** wifi-ax-twt-impl (shared task **task-199**), 2026-11-28.
**HOST-ONLY**: this lane built on the local host against the project 6.18.60
kernel tree; it never contacted the router, never wrote firmware, never
produced an image and never flashed.  The `.ko` is **local evidence only and
is never committed**; the committed outputs are the source, the patches, the
tools and the text evidence.

## 1. The chain (what is "the current merged-module base")

The current bench-proven driver is the **merged 64-param module** of IMAGE #3
(../wifi-image-3, task-196): the F2 5 GHz chain tip + the image #2 RX
watermark + the BA skeleton + the HE (802.11ax) wiphy advertisement, every
added gate fail-closed.  Its source tree was not preserved as one tree, so
this lane reconstructs it deterministically from the **committed sibling
patches** and proves the reconstruction: the rebuilt base `.text` sha256 is
exactly the IMAGE #3 module's `.text`.

```
F2 5 GHz chain tip
  = r1-band base (../wifi-5ghz-beacon + ../wifi-r1-band-diff tools)
    + ../wifi-desc-table-send/patches/0001-desc-table-send.patch
    + ../wifi-dfs-mode-send/patches/0001-dfs-mode-send.patch
    + ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch   (field B = 0x00)

merged 64-param base (== IMAGE #3 module)
  = F2
    + ../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
    + ../wifi-ba-agg/patches/0001-ba-skeleton.patch
    + ../wifi-ax-he-caps/patches/0001-he-caps.patch
    + patches/0000-merge-he-kbuild-fix.patch
      (the HE patch's Kbuild hunk was written against the pre-F2 Kbuild and
       cannot apply there; the IMAGE #3 lane made the same object-line edit
       by hand after hi5622_dfs_mode.o - this patch is that edit, so the
       reconstruction stays fully committed-source driven)

TWT module (this lane)
  = merged 64-param base
    + patches/0001-twt-enable.patch
```

If the F2 tree is absent, `tools/build-twt-impl.sh` rebuilds it by running
the two sibling build scripts and applying the three committed F2 patches -
never from a vendor artifact.

## 2. Source pins (the build script enforces all of them)

### F2 5 GHz chain tip

| file | sha256 |
| --- | --- |
| `hi5622_scan.c` | `8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7` |
| `hi5622_vapscan_provider.c` | `c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab` |
| `hi5622_dfs_mode.h` (field-B fix) | `cd3b76aa75c5153654ca1e981d0f767fc100e7cb8e83406c55a43d0b4ce3c70b` |
| `hi5622_dfs_mode.c` | `977f285c58ef1ffdfca7812c4014da7602dc6a3f3b6fbb7839e1caedfdd13892` |
| `hi5622_desc_table.h` (field-B fix) | `5321d1f7ff1f5e494ba65013fb815861d972e6f2161bf1ce8e0466665551f9d1` |
| `hi5622_desc_table.c` | `5af935f29aa9a329aba365da1753c5edd896b74cfe1c0c50665de13b39797810` |
| `Kbuild` | `900fb204ad817e787a0cde9201a02c6dc34d69d893ef0e4f06d140630fcb0759` |

### Merged 64-param base (== the IMAGE #3 module source)

| file | sha256 |
| --- | --- |
| `hi5622_scan.c` | `8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7` |
| `hi5622_scan_logic.c` | `75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c` |
| `hi5622_scan_logic.h` | `b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e` |
| `Kbuild` | `b4f98773547d0d7643be7c401d7d25cd40eae896b6ac49383647177f1731087e` |
| `hi5622_cfg80211.c` (HE) | `1af4d7f3facbf19ffad945ea0c4c7e9b11d5b1b04eace6421b9e52b45ec384e5` |
| `hi5622_dp_txrx.c` (RX wm) | `76da6d4b3bd5ad6e165656bb9c9ade5a9ed6ef1df97a2dcf810bd0a2638b6d6e` |
| `hi5622_rx_live.c` | `4bf35afbb496bd35d3db455ea648bd06e3fb158cd20d7a9e569d75d4728ff36a` |
| `hi5622_rx_live.h` | `afa37eccfbbb322087da680f877c2003eb8bf159fb8a802c208791452b9d0b4c` |
| `hi5622_ba.c` | `ae51aa12dae45252bac774c8d7a86cf039ccfd59bdf3ffc3ed9f62388936c777` |
| `hi5622_he_caps.c` | `de2d78b5da48d9774099a7e03adc522a431a1ee8a029a70005393ae353273b9b` |

### TWT module sources (merged base + patches/0001-twt-enable.patch)

| file | sha256 |
| --- | --- |
| `Kbuild` | `1430f7affb44cc8ecec84767bbe34acb66957dfacfcc4662091a45449f35bfc0` |
| `hi5622_scan.c` | `d64740875c9ab0ddb1e9db85a2f3fd9540256d656d70e705225e7d326442daaf` |
| `hi5622_scan_logic.c` | `73614030e3e7894f2500515798db01320d17c8c616ebbb1b5a6af1ec4e89e080` |
| `hi5622_scan_logic.h` | `955b30c16c7dbec3ebd808abed7bde254c404a0a3bf1021720abc7d1bcb72536` |
| `hi5622_ax_twt.c` (= `src/hi5622_ax_twt.c`) | `65e3963323d3cc99694d313e7310c2108dbceff5a34ff7c66d33f3e0e2d48dd4` |
| `hi5622_ax_twt.h` (= `src/hi5622_ax_twt.h`) | `940efe65427562db3e6dc46045094bca590d7d7f728ac7d0c1f7e0ad9a1793a1` |

## 3. Artifact identity (local evidence, never committed)

| form | size | sha256 |
| --- | ---: | --- |
| merged base `.text` (proof of reconstruction) | 160,592 B | `494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1` |
| **TWT module `.text`** (path-independent pin) | **162,196 B** | **`1d7d6ddae70a9044c917381c52a124fed6adf6546489fece2b0ab6de9f5eb8e4`** |
| TWT module unstripped | 1,454,516 B | `76b967c6ad02ef5e228907822c963131c0f3a36dc51167e3b1eac64668b49dba` (path-dependent DWARF comp_dir) |
| TWT module activity (`strip --strip-debug`) | 383,376 B | `d271d195272ce68802c6d13100bfa91d136cdaee6499270a70d98971efbaf9b2` |
| **TWT installed form** (`scripts/strip-kmod.sh` NO_RENAME=1) | **362,540 B** | **`ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93`** |

Both `.text` and the installed form are **path-independent** (verified by
rebuilding in two different trees: the full-file unstripped hash is not).
The build script pins both and aborts on any drift.

Module facts: **65 params** (merged 64 + `twt`); vermagic
`6.18.60 SMP mod_unload ARMv7 p2v8`; `depends: cfg80211`; license GPL;
**0 warnings, 0 errors**; 5 new global symbols
(`hi5622_ax_twt_build`, `_field_b`, `_mode_name`, `_precheck`,
`_status_line`) and **0 global symbols lost** vs the merged base
(evidence/module-identity.txt).

## 4. Reproduce (host only)

Requirements: the project 6.18.60 kernel tree and the OpenWrt GCC 14.4
toolchain used by the sibling lanes (auto-detected under
`/home/shibbityshwab/openwrt-lab-upstream-target` or
`...-selectorfix-img`; override with `KDIR_OVERRIDE` /
`STAGING_OVERRIDE`), `rsync`, `patch`, `python3`, and optionally
`scripts/strip-kmod.sh` for the installed-form pin.

    cd opensource/docs/incoming/wifi-ax-twt-impl
    ./tools/build-twt-impl.sh            # reconstruct + patch + build + pin
    ./tools/run-host-tests.sh            # 54/54 + 12/12 + 62/62 (no toolchain)
    TREE=/tmp/ax-twt-build ./tools/run-host-tests.sh   # includes plan-order

Tree locations (all local, /tmp): F2 `/tmp/dfsmode-build`, merged base
`/tmp/ax-twt-merged-base`, TWT tree `/tmp/ax-twt-build`, artifacts
`/tmp/ax-twt-artifact`.  Override with `F2_TREE`, `PRISTINE_TREE`,
`WORK_TREE`, `ART_DIR`.

Expected last lines of `build-twt-impl.sh` (evidence/build.txt):

```
merged base: .text 494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1 (== IMAGE #3), unstripped ... B
installed form: 362540 B sha256 ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93 (.text == the pin)
== ARTIFACT (local evidence only; never committed)
   .text           162196 B  sha256 1d7d6ddae70a9044c917381c52a124fed6adf6546489fece2b0ab6de9f5eb8e4
   params          65 (merged base 64 + twt)
```

## 5. What the change touches (patch shape; evidence/diff-stat.txt)

    hi5622_ax_twt.c     +170 (new portable model)
    hi5622_ax_twt.h     +152 (new contract)
    hi5622_scan.c       +166/-1 (param, gate, plan arg, post+read-back, sysfs)
    hi5622_scan_logic.c  +11/-1 (plan step + name)
    hi5622_scan_logic.h  +17/-5 (enum, signature, comments)
    Kbuild                +1   (hi5622_ax_twt.o)
    6 files, 517 insertions(+), 7 deletions(-)

The TWT model is a leaf object: it includes only its own header (and
`linux/kernel.h`/`string.h` in the kernel, `stdio.h`/`string.h` on the
host), so the change cannot regress any existing code path when `twt=0`.
