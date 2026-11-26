# BUILD-NOTES - the IMAGE #3 base, the rebase patch and the artifact pins

**Lane:** app-ie-rebase (task-235), 2026-11-26.  **HOST-ONLY:** built on the
host against the project 6.18.60 kernel tree; no device contact, no firmware
write, no image, no flash.  The `.ko` is **local evidence only and is never
committed**; the committed outputs are the source, the patch, the tools and
the text evidence.

## 1. The base

The rebase target is the **IMAGE #3 merged module tree**
`/tmp/wifi-image-3-module` (scratch; not committed). A control build of that
tree reproduces the shipped IMAGE #3 module exactly - installed **a717c86d...**
(358,544 B), `.text` **494ccd42...**, 64 params - see
`evidence/BASE-CONTROL-BUILD.txt` and the 97-file
`evidence/base-tree.sha256`. Base pins asserted by the build script:

| file | sha256 |
| --- | --- |
| `Kbuild` | `b4f98773547d0d7643be7c401d7d25cd40eae896b6ac49383647177f1731087e` |
| `hi5622_scan.c` | `8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7` |
| `hi5622_scan_logic.c` | `75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c` |
| `hi5622_scan_logic.h` | `b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e` |
| `cfg80211-provider.symvers` | `bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7` |
| `patches/0001-app-ie-image3.patch` | `09523f988b7a0256565278017a4e82687d7dd6363abbccc5722de595550adb7d` |
| `src/hi5622_app_ie.h` | `e0f3b39f9b15d410ed70a62a7e7977433d3454158457a7b9c7c4e0902929432c` |
| `src/hi5622_app_ie.c` | `19c9cbf7d822a05eac919ac4bf660058b38b86d67b4a912feac0470be5a306ba` |

## 2. The patch

`patches/0001-app-ie-image3.patch` (829 lines, against the IMAGE #3 tree,
`patch -p1`): adds `hi5622_app_ie.c/.h` (byte-identical to task-234),
`Kbuild` one object line, `hi5622_scan.c` (include, the two parameters and
outcome statics, the fail-closed gate, the cfg80211 0xDD collector, the app-IE
post after the `ap_vap_start` step, the read-only firmware slot read-back, the
`cfg80211_status` line, the collector free), `hi5622_scan_logic.{c,h}` (the
`app_ie` plan step + signature + name). Verified: applied to a clean copy of
the base it reproduces the built tree file-for-file
(`evidence/PATCH-ROUNDTRIP.txt`).

## 3. The build and the pins (what the fire checks)

Command: `tools/build-app-ie-image3.sh` (full log `/tmp/app-ie-image3/build.log`,
summary in `evidence/BUILD.txt`):

    base copy -> patch -p1 -> make -j4 ARCH=arm CROSS_COMPILE=<OpenWrt 6.18.60 gcc> \
        -C <KDIR> M=/tmp/app-ie-image3/build modules  -> 0 warnings, 0 errors
    strip-kmod.sh (OpenWrt, NO_RENAME=1) -> the installed form

| artifact | size | sha256 |
| --- | ---: | --- |
| installed (`/tmp/app-ie-image3/hi5622_wifi.ko`) | **364,516 B** | **`2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2`** |
| installed `.text` | 163,052 B | **`c1ad137974b6866bbc7cdc1971288ade203bf9dfcca6421b73343ef3d50b9417`** |
| unstripped | 1,461,464 B | `43a1ef000cddcb8a76ee5525d64393e173a2b76919ea604d0b6c8e4271f520a7` |

Parameters: **66** (the IMAGE #3 64 + `app_ie`, `app_ie_type`); `modinfo -p`
shows both. vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, depends `cfg80211`,
license GPL. The installed sha is pinned for `WORK=/tmp/app-ie-image3/build`
because the module embeds build-path strings - a package build from another
path yields a different file hash but the **same `.text` hash**; the fire
recipe accepts either check (`FIRE-RECIPE.md` H0).

The rebased module adds the 0x20 integration code to the accepted IMAGE #3
module; with `app_ie=0` (default) the new code posts nothing and the
AP-start behaviour is the IMAGE #3 behaviour. The only wire delta when the
rung is enabled is the single g0 id 0x20 frame.

## 4. KAT

Two layers, both re-run for this lane:

* **Host KAT** - `tools/run-host-tests.sh` compiles `src/hi5622_app_ie.c` +
  the task-234 self-test (`../wifi-app-ie/src/app_ie_selftest.c`, unchanged)
  with the host `cc` (`-std=c99 -Wall -Wextra -Werror`) and runs it:
  **55 checks, 0 failures** (`evidence/HOST-TESTS.txt`), including the
  byte-exact stock-frame reconstruction.
* **Module KAT** - `tools/kat-image3.sh`: **17/17 PASS**
  (`evidence/KAT.txt`): artifact pins, the 66 params, the `app_ie`/`app_ie_type`
  parameters, the `.text` pin, the integration dmesg format strings inside
  the module, the source/patch pins, the IMAGE #3 base-tree manifest, and the
  host KAT.

## 5. What is not here

No `.ko`, no firmware bytes, no vendor object, no calibration value, no MAC.
The vendor modules/firmware used for the original decode are local, gitignored
evidence; only decoded addresses, instruction behaviour and protocol bytes as
text are committed.

## 6. Reproduce

    tools/build-app-ie-image3.sh     # base manifest + patch + build + pins + KAT
    tools/kat-image3.sh              # verify the delivered /tmp/app-ie-image3 artifact
