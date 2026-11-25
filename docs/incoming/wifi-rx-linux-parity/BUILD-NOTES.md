# BUILD-NOTES - wifi-rx-linux-parity (task-166)

Host-only lane. Everything below was executed on the host; no bench/device
contact, no image build, no vendor binary or per-unit data in tracked files.

## Base tree

The **final association tree** used by the last bench waves lives only in
gitignored /tmp trees of earlier lanes and is not committed. This lane therefore
states its base exactly as the current *published* RX source state:

* wifi-rx-static lane result: docs/incoming/wifi-rx-static/src +
  patches/0001-rx-live-enable.patch (the 6-file RX enable/consumer layer);
* wifi-rx-watermark lane patch: docs/incoming/wifi-rx-watermark/patches/
  0001-rx-refill-watermark.patch (applies to the rx-static src with line
  offsets - the two lanes' published forms compose);
* this lane's patch: patches/0001-rx-linux-parity.patch (6 files: 2 new, 4
  edited).

tools/build.sh reproduces the reconstruction and proves that applying this
lane's patch to the reconstructed tree reproduces src/ byte-for-byte.

## Patch scope (6 files)

    +++ b/hi5622_dp.c
    +++ b/hi5622_dp_txrx.c
    +++ b/hi5622_dp_txrx.h
    +++ b/hi5622_rx_parity.c   (new)
    +++ b/hi5622_rx_parity.h   (new)
    +++ b/Kbuild

## Host verification (all reproduced by tools/build.sh)

| check | result |
| --- | --- |
| base reconstruction (rx-static src + watermark patch) | PASS |
| patch applies and recreates src/ byte-for-byte (6 files) | PASS |
| host selftest tools/rx_parity_selftest.c | **118 checks, 0 failures** |
| kernel-header compile of the portable model (6.18.60) | RC=0 |
| kernel compile of the patched glue objects (hi5622_dp.o, hi5622_dp_txrx.o, hi5622_rx_live.o, hi5622_rx_parity.o, dp_txrx.o, dp_rings.o) | **6/6 objects, 0 errors, 0 warnings** |
| compare_stock unit tests | **11/11 pass** |
| publication checks (no binaries/objects, SPDX, no secret markers) | PASS |

The kernel compile uses a **hybrid committed header base**: wifi-dp-air/src
(headers + several sources) plus wifi-dp-tuning's dp_txrx.c/.h (the later TXQ
interface). The glue probe deliberately links only the patched objects, so
modpost reports the rest of the driver's symbols as unresolved - expected and
not a compile failure. The gate is compiler errors/warnings = 0.

Logs: evidence/build.txt, evidence/host-test.txt, evidence/kernel-compile.txt,
evidence/patch-verify.txt, evidence/compare-stock-tests.txt,
evidence/compare-stock-demo.txt.

## Full-module build (needs the integration lane)

The final association tree is not committed, so the full-module link could not
be run here. Integration recipe:

    cd <final association tree>          # assoc 251d08f0 + corrected mgmt
                                         # carrier + watermark patch state
    patch -p1 < <lane>/patches/0001-rx-linux-parity.patch

or, to let build.sh do it (adds the full-module build step):

    RX_FULL_BASE=<final association tree> ./tools/build.sh

Then build the module as usual (the tree's Kbuild already includes
hi5622_rx_parity.o after this patch):

    export STAGING_DIR=<openwrt>/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C <openwrt>/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60 \
         M=<tree> modules

Expected: 0 warnings, 63 params (57 baseline + rx_entry_wait + rx_ring_kick +
rx_ctl_enable + rx_ctl_off + rx_ctl_set + rx_ctl_clr + rx_legacy_bank = 64;
count it from modinfo -p and quote the artifact sha256 of the exact baked file -
build-id differs per path, so bake/compare the exact file).

## What this lane did NOT do

* no bench fire, no boot, no insmod, no image bake;
* no full-module link (tree not committed - see above);
* no IRQ/NAPI enable, no teardown path;
* no write to any register during the read-only probe; the two experiments are
  off by default and refuse without explicit parameters;
* no sub-MSDU chain decode (still refused);
* no vendor firmware/object/text copied into the lane; the Linux sources are
  cited for analysis only and no code was copied from them.
