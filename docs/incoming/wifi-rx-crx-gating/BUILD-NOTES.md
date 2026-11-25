# BUILD-NOTES - wifi-rx-crx-gating (task-164)

Host-only verification log. Nothing here was run on the device; the Lead fires.

## 1. Base tree and patch

| item | value |
| --- | --- |
| base | FINAL IMAGE #2 chain: final association tree `251d08f0` + wifi-sibling-gpl `0001-mgmt-tx-hcc-g0id6-carrier.patch` (module ab9e18a4) + wifi-rx-watermark `0001-rx-refill-watermark.patch` |
| local base copy used | `/tmp/wifi-rx-watermark-build/rebuild` (host-only, not committed) |
| patch | `patches/0001-crx-delivery-gate.patch` |
| files | 4: `hi5622_crx_gate.c` (new, 303 lines), `hi5622_crx_gate.h` (new, 202 lines), `hi5622_dp.c` (+1 include, +9 attribute lines), `Kbuild` (+1 object) |
| apply | `patch -p1` from the driver tree root; `--dry-run` then apply both clean |
| round-trip | after apply, the four files are byte-identical to `src/` copies + the patch (see `evidence/patch-verify.log`) |
| no overlap | the patch does not touch `hi5622_dp_txrx.c` / `hi5622_rx_live.*` (watermark) or the mgmt carrier files, so it stacks last |

## 2. Kernel build (project 6.18.60 ARM tree)

Environment (same as the sibling lanes):

```sh
export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
make ARCH=arm CROSS_COMPILE=$CROSS -C $KDIR M=<tree> modules
```

| result | value |
| --- | --- |
| build | **RC=0, 0 warnings** |
| unstripped module | **1,351,256 B sha256 `48fbc51b19465814046e60e1752342d67fc149bba60e4a2b244893ce8c04318e`** |
| stripped (strip-debug) | **352,020 B sha256 `a9d3835aebdc01b9a898cfec2c0db9c90b563ac232da96302a951178fd0fc855`** |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends | `cfg80211` |
| identity warning | the sha is for this exact file/build directory (ld build-id hashes the pre-strip DWARF comp_dir); rebuilds elsewhere are code-identical but carry a different sha - quote this file's sha only |

Log: `evidence/kernel-build.log` (tail).

## 3. Host selftest

```sh
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Isrc \
   tools/crx_gate_selftest.c src/hi5622_crx_gate.c -o /tmp/crx_gate_selftest
/tmp/crx_gate_selftest
```

Result: **66 checks, 0 failures** (`evidence/selftest.log`). Coverage: the firmware
builder contract arithmetic, valid decode at minimum/default/maximum frame sizes,
20 refusal classes, all 8 verdicts (including the measured bench shape), the name/
reason surface.

## 4. Reproducibility

`tools/build.sh` re-runs the whole chain: stage the base tree, apply the patch, prove
the patched tree matches `src/`, build+run the host selftest, run the publication
checks, and (unless `SKIP_KERNEL=1`) build the module. Base tree override:
`BASE=<path> tools/build.sh`.

## 5. Publication checks

* lane contains no `.o/.ko/.mod/.bin/.dis` files;
* every source/tool file carries `SPDX-License-Identifier: GPL-2.0`;
* no firmware image, module byte, calibration value, MAC/serial or secret is committed;
  evidence files hold only derived addresses/constants and our own output.
