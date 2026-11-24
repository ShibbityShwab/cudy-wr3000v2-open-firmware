# BUILD-NOTES - wifi-5ghz host build + evidence

**Host-only.**  No device access, no firmware write, no image, no flash.  The
`.ko` is local evidence only and is never committed.

## 1. What is built

The FINAL ALL-PATCH module tree (hardware-validated on 2026-11-29:
`opensource` 008aca5, stripped **308,080 B sha256 `5b587949...`**, 47
params) plus this lane's [patches/0001-band5g.patch](../patches/0001-band5g.patch):

| file | change |
| --- | --- |
| `hi5622_band5g.h/.c` | new: vendor 14/29 band tables, freq<->channel<->index, AP/H1 band-config gates, cap words |
| `hi5622_cfg80211.c` | `band5g` param; 29-ch/HT 0x0062/VHT 0x00400064 profile on the 5 GHz endpoint |
| `hi5622_scan.c` | 5 GHz AP-start gate; H1 channel derivation from the scan frequency |
| `hi5622_vapscan_map.c` | scan-request `num`/`idx` from the vendor 14/29 tables |
| `Kbuild` | `hi5622_band5g.o` |

The patch is a plain `diff -u` with `a/`/`b/` labels and applies with
`patch -p1` (verified `--dry-run` on the regenerated final all-patch
tree: [evidence/patch-apply.txt](../evidence/patch-apply.txt)).

## 2. Reproduce

    # host test of the portable logic (no kernel tree needed)
    ./tools/selftest.sh                      # 98 checks

    # static RE (local vendor artifacts; paths are evidence, never committed)
    ../../../../.provision-venv/bin/python tools/band5g_re.py \
        --wifi-ko  /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko \
        --wifi-ko-old /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko \
        --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin

    # patched module build (needs the 6.18.60 kernel tree; the script locates
    # /home/shibbityshwab/openwrt-lab-{upstream-target,selectorfix-img})
    ./tools/build-band5g.sh

`tools/build-band5g.sh` uses an existing final all-patch integration tree
(`/tmp/wifi-allpatch-build/integ`) when present; otherwise it first runs
`../wifi-stability/tools/build-all-patch-module.sh` (chain: base 44c6cc12 ->
polish -> dp-tuning -> scan R4 -> stability -> rx-static -> beacon-static
(+guard fix) -> wpa-static -> sta-static).  Environment overrides:
`CHAIN_TREE`, `KDIR_OVERRIDE`, `STAGING_OVERRIDE`.

## 3. Canonical host build (2026-11-24, this lane)

    artifact  : /tmp/wifi-band5g-artifact/hi5622_wifi.ko (debug-stripped)
    stripped  : 312,432 B sha256 2bee439131eaa31d1d79048998c203770c1c3caba401711896ededd1c9e12207
    unstripped: 1,150,092 B sha256 2b8ff159cf6ea07eda28737fb531e92f916e9d3b555dca66a096689d4120e676
    params    : 48 (47 chain + band5g)
    vermagic  : 6.18.60 SMP mod_unload ARMv7 p2v8
    warnings  : 0 compiler/modpost warnings or errors
    strip     : .text byte-identical after debug-strip
    cfg80211  : 10-line provider contract + cfg80211_bss_iter import (unchanged)

The stripped sha256 is **build-path dependent** (the vendor-style
`.note.gnu.build-id` hashes debug info that embeds the absolute build
directory; same caveat as wifi-stability/FINAL-INTEGRATION.md).  The canonical
number above is from `/tmp/wifi-band5g-build`; a rebuild in another directory
keeps the size/params and changes only the sha.  Evidence:
[evidence/artifact-identity.txt](../evidence/artifact-identity.txt),
[evidence/build-verify.txt](../evidence/build-verify.txt).

## 4. Toolchain

Same as the rest of the Wi-Fi lanes: OpenWrt GCC 14.4.0
(`staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/
arm-openwrt-linux-muslgnueabi-`) against the project 6.18.60 tree
(`.../linux-hisilicon_luofu/linux-6.18.60`).  The host selftest uses the
system `gcc` with `-std=c99 -Wall -Wextra -Werror -pedantic`.

## 5. Safety / publication

* No vendor binary, firmware image, calibration byte, MAC/serial or secret is
  contained in this directory.  The vendor artifacts are referenced by local
  path only; `evidence/` holds derived facts (addresses/counts/words) and
  test logs.
* The module artifact is local only (gitignored); see [.gitignore](../.gitignore).
* The ladder is a design document: this lane did not contact the bench router.
