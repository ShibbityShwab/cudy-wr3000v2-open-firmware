# BUILD-NOTES - wifi-driver-integration on Linux 6.18.60

**Lane:** wifi-driver-integration (task-64). **Written:** 2026-11-22.
**Scope:** host-only; no device access, no OpenWrt image build, no vendor
binary/source.  All build products live OUTSIDE the repository (default
workdir `/home/shibbityshwab/wifi-driver-port-work`); only text is committed.

## Result: three modules, all clean under the tree's -Werror

| module | sources | size | sha256 | vermagic | notes |
| --- | --- | ---: | --- | --- | --- |
| `hi5622_pciet.ko` | transport skeleton + **integration delta** + `pciet_iface.c` | 142,268 B | `d6fda0db53db1f028c650473b8744f0307e72390bc76b3092b63edc2a127f455` | 6.18.60 SMP mod_unload ARMv7 p2v8 | 72 text funcs, 24 global defined; exports `hi5622_transport_{register,unregister}_client` (+ non-exported `hi5622_tr_state_str`) |
| `hi5622_hdpp_drv.ko` | the hdpp-style driver skeleton (7 objects) | 202,068 B | `d38be1990389561cfc1b441824d682213d78c0e9f0e58ccbfe0932bc5bc73190` | 6.18.60 SMP mod_unload ARMv7 p2v8 | 84 text funcs, 47 global defined; exactly **2** undefined `hi5622_*` symbols (the transport exports), resolved via `KBUILD_EXTRA_SYMBOLS` |
| `hi5622_hdpp_mock.ko` | driver skeleton + simulated transport | 236,508 B | `39aa2b611f1ed09da8dbffa602644f82e788c7e417ee753b528102d6f0c6da05` | 6.18.60 SMP mod_unload ARMv7 p2v8 | 101 text funcs, 49 global defined; **self-contained** (0 undefined `hi5622_*` symbols); insmod-able on any 6.18 test kernel, NO device/BAR access |

**Hash-stability note:** the sha256s above are from the verified rebuild of
2026-11-22 against the tree state of that moment (submodule `aedbc30`,
`CONFIG_MODULE_STRIPPED=y`).  Kernel-module bytes also embed the kernel
tree's symbol/versioning state, so a **kernel tree rebuild can change a module
hash even with identical sources** (the lead's earlier build of the same
patched transport gave `416d1db0...`, this rebuild `d6fda0db...` with the same
sizes and same symbol sets).  The reproducible evidence is the source text +
`tools/build-modules.sh`, not a fixed sha; treat a hash mismatch across tree
rebuilds as expected and re-verify the *symbol/export checks* instead.

License of all three: `GPL`; built with `arm-openwrt-linux-muslgnueabi-gcc`
(GCC 14.4.0) against
`/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`.

Compiler output: **zero cc warnings under the tree's -Werror**.  The only
modpost notes are `missing MODULE_DESCRIPTION()` - a tree artifact of
`CONFIG_MODULE_STRIPPED=y` (MODULE_INFO_STRIP drops the tag at modpost time,
the tag is present in every source) and the same note the wifi-datapath-slice
lane documented for its module.  `tools/build-modules.sh` treats any *other*
warning as a failure.

## Build line

The normative, reproducible entry point is:

```sh
opensource/docs/incoming/wifi-driver-integration/tools/build-modules.sh
```

It performs, into `$WIFI_DRIVER_PORT_WORK` (default `~/wifi-driver-port-work`):

1. copies the four transport source/build files + `../interface/{hi5622_transport_iface.h,pciet_iface.c}`,
   applies `../transport-delta/0001-pciet-integration-iface.patch` (`git apply -p1`),
   builds `hi5622_pciet.ko`;
2. copies `driver/` + `interface/hi5622_transport_iface.h` and builds
   `hi5622_hdpp_drv.ko` with `KBUILD_EXTRA_SYMBOLS=<transport>/Module.symvers`;
3. copies `driver/` + `mock/` + the interface header and builds the
   self-contained `hi5622_hdpp_mock.ko`;
4. verifies vermagic, the transport export set, the driver's unresolved-symbol
   set (exactly 2) and that the mock is self-contained.

Equivalent raw commands (all variables explicit):

```sh
KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
export STAGING_DIR CROSS_COMPILE
make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$PWD/transport modules
make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$PWD/driver \
     KBUILD_EXTRA_SYMBOLS=$PWD/transport/Module.symvers modules
make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$PWD/mock modules
```

`CROSS_COMPILE` must end with `-`; no `make world` / OpenWrt image build is
run anywhere in this lane.

## Build-time fixes needed while porting to 6.18 (for the next lane)

| # | issue | fix |
| --- | --- | --- |
| 1 | `asm/unaligned.h` no longer exists on this 6.18 ARM tree | `#include <linux/unaligned.h>` |
| 2 | `rtnl_lock()/unlock` implicit declaration in netdev teardown | `#include <linux/rtnetlink.h>` |
| 3 | `pciet.h` is a self-contained header that does not include `linux/pci.h`; `pciet_iface.c` used `ctx->pdev->dev` first | include `<linux/pci.h>` before `hi5622_pciet.h` |
| 4 | the transport's `hi5622_pciet.h` exposes `struct hi5622_pci` including `d2h[]`, `sr0/dr0`, flags; the iface relies on exactly those fields | the delta patch does not modify the header; if transport renames fields, rebuild the iface |
| 5 | doc-comment text containing `_*/_*` terminated a C comment (two files) | spell it `_* / _*` |
| 6 | transport's `pciet_writel_checked()` is static | the iface writes the two phase37 1.2 words through `ctx->bar0` directly, with compile-time `BUILD_BUG_ON`s that they are not CA 0x400392f0 |

## Reproduce the verification

```sh
W=~/wifi-driver-port-work
CROSS=$(dirname ...)/arm-openwrt-linux-muslgnueabi-
${CROSS}nm -g --defined-only $W/transport/hi5622_pciet.ko | grep hi5622_transport || true
${CROSS}nm -u $W/driver/hi5622_hdpp_drv.ko | grep hi5622
strings $W/driver/hi5622_hdpp_drv.ko | grep -m1 vermagic=
```

Expected: two `T hi5622_transport_*` in the transport; exactly the same two
`U hi5622_transport_*` in the driver; no `hi5622_transport_*` undefined in the
mock; `vermagic=6.18.60 SMP mod_unload ARMv7 p2v8` for all three.

## What was NOT built / attempted

* No OpenWrt image build, no `make world`; no target/package wiring (the lead
  owns that).
* cfg80211/mac80211 integration: **not buildable on this tree** -
  `CONFIG_CFG80211 is not set` in `linux-6.18.60/.config`.  The wiphy is a
  documented stub (see REPORT section 6).
* The alg sync protocol is compiled but disabled by default (its D2H response
  id is [U]).
* No module was loaded on the bench by this lane; no device access was made.
  The mock module is the only executable smoke test and it touches no hardware.
