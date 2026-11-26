# BUILD-NOTES - the gate-inverted module (host build, never committed)

**Lane:** wifi-vap-start-gates (task-212). **HOST-ONLY:** the vendor firmware
and the kernel build tree are read in place; the resulting `.ko` is LOCAL
EVIDENCE ONLY (never committed, never flashed by this lane).

## What was built

The fired R4b source tree (`/tmp/r4border-build`, the tree of the R4b module
382,188 B sha `b9926c1c...1102`, send-3 lineage + the R4b order patch, 65
params) with **one** additional patch:
`patches/0001-g9-g11-rf-enable-invert.patch` - the two gate comparisons of
`hi5622_vap_start_g0.c:hi5622_vap_start_g0_precheck()`:
`G9 u8[mctx+0x440] 0 -> 1` and `G11 u8[bandobj+0xEC8] 0 -> 1`
(with `-EAGAIN` refusals and the corrected verdict strings).

## Artefact (local, /tmp only)

    /tmp/vsg-artifact/hi5622_wifi.ko
    382,376 B (stripped); unstripped 1,423,132 B
    sha256 9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82
    unstripped sha256 99eeb5b97594062bfa407b6b9505ab838d7d4f88c5d1c853b785411b77496777
    .text sha256 f9ce407d6d60314d07ed996b01bfdbf666764d0a8c91c71c44b1b473371ee550
    vermagic=6.18.60 SMP mod_unload ARMv7 p2v8
    modinfo -p | wc -l = 65   (no new parameter)

**Not the fired R4b artefact:** the R4b module is 382,188 B sha
`b9926c1c...1102`; the inverted build is 382,376 B sha `9bf1d846...cf82`.
Record the deployed sha in the fire log (FIRE-RECIPE R0).

## Build steps (reproduced 2026-11-28, host only)

    # KDIR/STAGING from the OpenWrt lab tree (same discovery as the R4b build)
    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS=$STAGING/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    rm -rf /tmp/vsg-build && cp -a /tmp/r4border-build /tmp/vsg-build
    cd /tmp/vsg-build && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order build.log
    patch -p1 < .../wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch
    export STAGING_DIR=$STAGING
    make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M=/tmp/vsg-build modules
    # 0 warnings / 0 errors
    ${CROSS}strip --strip-debug -o /tmp/vsg-artifact/hi5622_wifi.ko /tmp/vsg-build/hi5622_wifi.ko

`tools/build-gates.sh` performs exactly these steps (plus the host tests) and
prints the artefact pins.

## Host tests (patched tree + patched test source)

The C selftest source lives in ../wifi-band1-start-send-4/src/ and its default
probe asserted the OLD gate polarity (`b440=0, ec8=0`). The one-file update
is `patches/0002-gate-selftest-update.patch` (apply to a *copy* while this
lane does not edit the committed send-4 lane):

    patch -p1 -d <copy of wifi-band1-start-send-4> < patches/0002-gate-selftest-update.patch

Then (against the patched /tmp/vsg-build):

    gcc -std=c99 -Wall -Wextra -Werror -O2 \
        -I/tmp/vsg-build -I<send-4>/src -I<wifi-vap-start>/src -I<wifi-band1-start-send>/src \
        -o /tmp/vsg-artifact/vap_start_g0_selftest \
        /tmp/vsg-build/hi5622_vap_start_g0.c /tmp/vsg-build/hi5622_scan_logic.c \
        <patched send-4>/src/vap_start_g0_selftest.c \
        <wifi-vap-start>/src/vapstart_frames.c \
        <wifi-band1-start-send>/src/hi5622_band_start.c
    /tmp/vsg-artifact/vap_start_g0_selftest      # 293 checks, 0 failures
    python3 <send-4>/tools/r4b_order_frame_test.py /tmp/vsg-build   # 93 checks, 0 failures

Evidence: `evidence/build-artifact.txt`, `evidence/host-tests.txt`.

## Why the test change is a *test* change only

The gate semantics changed from "0 = fresh" to "1 = normal"; any probe fixture
that models a normal band-1 state must now use `b440=1, ec8=1` (that is the
state the R4b fire actually fired in). The 0x0065 frame bytes are untouched:
the 40-B KATs in the selftest still pass unchanged.
