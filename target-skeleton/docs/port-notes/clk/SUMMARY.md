# CRG clock-tree lane - integration record (task-35, agent driver-crg-clocks)

Date: 2026-11-18. Host-only. **No device, router, UART, TFTP, flash or bench tool
was touched; nothing was booted; no clock/rate/PLL claim is made for hardware.**

This directory is the incoming package for the CRG clock-tree lane. The kernel-side
implementation lives in
`patches-6.18/019-arm-clk-luofu-clock-tree.patch`; the shared-file integration patch is
`shared.patch` (this directory); the local-only evidence bundle is
`build/linux-kbuild/waves/upstream-6.18-clk-20261118/` (gitignored).

## 1. What was delivered

| artifact | path | role |
| --- | --- | --- |
| draft kernel patch 019 | `target-skeleton/target/linux/hisilicon/patches-6.18/019-arm-clk-luofu-clock-tree.patch` | CRG driver clock-tree extension + dt-bindings header extension + YAML binding extension; sha256 `9d86498b405e43cc05b77b46b5ce74d4dfa5256a3dd6f444690d85e0cbc0513c` |
| incoming shared patch | `docs/incoming/clk/shared.patch` (this directory) | target DTS clock tree, TARGET-DESIGN/README/SKELETON-SMOKE updates, MISC-SOC-BLOCKS PLL-decode correction; sha256 `7409e35551a88d9873f6d69ebaf01bae5da44dcc9c4c8919d1fe72f6a4f84509` |
| this record | `docs/incoming/clk/SUMMARY.md` | integration notes, validation results, gates |
| local evidence bundle | `build/linux-kbuild/waves/upstream-6.18-clk-20261118/` | vendor module disassembly + PLL decode note, validation logs, hashes, applied trees |

## 2. What was decoded (summary; details in `01-PLL-decode.md` in the bundle)

The vendor `hi_clk.ko` (GPL, not stripped; 15,556 B; md5
`3b331b9058e13bc409dd8f0040af3b11`, identical in the 2.4.15 and 2.5.24 rootfs) was
disassembled host-side (`hi_pll_recalc_rate`, `hi_of_register_clk_pll`, the shared
writer at `0x4c4`, the mux ops). Established:

- the PLL control model is two consecutive words at `ctrl-offset` (`0x198`/`0x1e0`)
  acknowledged by `status-offset 0x90` bits 30 (CPU) / 27 (LSW); word0[23:12] is the
  integer feedback field, word0[29:24] a 6-bit feedback divider, word1[23:0] the
  fractional field, word0[10:8] and word0[6:4] are divider-select fields, bit 3 of word0
  byte 0 selects the integer-only path, bit 7 of the same byte is the writer's busy bit;
- the recalc equation is `rate = parent * (int + frac/2^24) / fbdiv`, then each non-zero
  divider-select field (plus the vendor `div-factor` property, absent here = 0) is
  applied as an integer division. Field positions and arithmetic order are [V] from the
  module; the **on-board value is [U]** (the boot-programmed words have never been read
  back);
- the set path (`hi_pll_set_rate` → writer `0x4c4`) sets busy bit 7, composes the
  words, clears busy and polls the status bit - decoded, but **not implemented**: the
  observed non-table call path composes a zero feedback-divider field while the recalc
  path divides by it, so the encoding is not verified and a wrong store can hang the
  core;
- the mux word `0x138` holds bit 3 (CPU PLL source select) and bit 1 (efuse ring source
  select); `table = <0 1>` for both, so the masked bit value is the parent index. The
  CPU mux must never be writable; the efuse mux is only ever flipped by an efuse
  programming path, which this tree does not have (the cells are one-way fences);
- the per-gate parents (from the vendor devicetree): apb_clk for sfc/gpio0/gpio1/i2c0/
  led_pwm/pcie0/pcie1, ahb_clk for pie_clk0, gemacN_clk for gemac_clk0..4 and mdio_clk0,
  lsw_dp_clk for lsw_dp_clk0 and lsw_pfe_clk for lsw_pfe_clk0.

## 3. What patch 019 implements

- `drivers/clk/hisilicon/crg-luofu.c`:
  - the 16 gate clocks are parented on the real input clocks instead of the single
    `apb_clk` parent; the provider resolves its seven input clocks from the node
    `clocks`/`clock-names` with `of_property_match_string` +
    `of_clk_get_parent_name()` (no global-name guesswork);
  - the two PLLs and the two muxes are registered as read-only clock outputs of the same
    provider (PLL `.recalc_rate`/`.is_enabled` only, no `.set_rate`; mux
    `.get_parent` only, `.set_parent` returns `-EOPNOTSUPP`);
  - gate clocks get `CLK_IGNORE_UNUSED` so `clk_disable_unused()` cannot gate a
    bootloader-enabled clock (the NAND boot path is behind the SFC gate);
  - probe stays free of register accesses (register-free reads only happen when a
    consumer asks for a rate/parent/status);
  - the "provisional 0x20-group offsets" wording is retired (the raw vendor-DTB decode
    in MISC-SOC-BLOCKS 5.2 makes the offsets [V]).
- `include/dt-bindings/clock/hisilicon,luofu-crg.h`: four new indices appended after the
  16 gate IDs (no renumbering): `LUOFU_CLK_PLL_CPU` 16, `LUOFU_CLK_PLL_LSW` 17,
  `LUOFU_CLK_CPU_MUX` 18, `LUOFU_CLK_EFUSE_MUX` 19; `LUOFU_CLK_NR` 20.
- `Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml`: required
  `clocks`/`clock-names` (osc, ahb, apb, gemacN, lsw_dp, lsw_pfe, efuse_ring),
  read-only PLL/mux description, updated example. No new Kconfig symbol or config line
  (`CONFIG_LUOFU_CRG` unchanged).

## 4. What shared.patch changes (apply from the `opensource/` root)

    cd opensource
    git apply target-skeleton/docs/incoming/clk/shared.patch

Base state: `opensource` HEAD `0fe742f` ("image: factory-MAC provisioning from bdinfo
0xde00"). The patch applies with `git apply` and `patch -p1` from the `opensource/`
root. Files:

| file | change |
| --- | --- |
| `target-skeleton/dts/luofu-r116.dts` | four missing fixed clocks (`gemacN_clk` 125 MHz, `lsw_dp_clk` 166 MHz, `lsw_pfe_clk` 333 MHz, `efuse_ring_clk` 20 MHz), CRG `clocks`/`clock-names`, updated clock-tree comments; PLL/mux ID note in the header comment |
| `docs/TARGET-DESIGN.md` | 6.2 CRG row and 7.1 CRG row updated to "written as patch 019" with the remaining TODO list |
| `target-skeleton/README.md` | new status paragraph for the CRG clock-tree session |
| `docs/SKELETON-SMOKE.md` | new "2026-11-18 (CRG clock-tree session)" validation section |
| `docs/MISC-SOC-BLOCKS.md` | section 5.3 PLL arithmetic corrected to the decoded equation/bit polarity; 5.4 gains a delivery-status note |

## 5. Validation (host-only, in an isolated clone)

Isolated btrfs-reflink copy of the shared clone (`/home/shibbityshwab/openwrt-lab-clk`,
`openwrt main` `27663a0`, `linux-6.18.60`, the tree's own OpenWrt GCC 14.4.0). The
shared clone was not modified. All logs are in the local evidence bundle.

| check | result |
| --- | --- |
| pristine-series apply | fresh `linux-6.18.60`: patches 001-019 in order with `patch -p1` RC=0, no `.rej`/`.orig`; final files byte-identical to the series tree |
| patch 019 apply | on a pristine+001-018 tree: `patch -p1 --dry-run` RC=0, apply RC=0, `git apply --check` RC=0, applied files byte-identical |
| `make target/linux/prepare` | **RC=0**; patch 019 applied (3 patching-file lines) |
| `make target/linux/compile` | **RC=0**, **0 warnings**, 0 errors in the whole log; `arch/arm/boot/zImage` 3,264,560 B sha256 `4456b0ec74533fa26f3a33e3aeec17776006ba251bcca613aeea5e72967d1237`; `drivers/clk/hisilicon/crg-luofu.o` 43,900 B |
| TEXT_OFFSET | unchanged: `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`; both `.head.o.cmd` files carry `-DTEXT_OFFSET=0x00608000`; `System.map`: `_text = stext = 0xc0608000`; CRG objects linked (`luofu_crg_probe`, `luofu_crg_pll_recalc_rate`, `luofu_crg_mux_get_parent`) |
| config derivation | no new symbols: `make kernel_oldconfig` five runs RC=0; re-derived target `config-6.18` byte-idempotent at sha256 `4106877fdb6dad7ee9d5d2895ed6eb878da0c6a70ff4067d664be3da380a2a1f` (300 symbols, same set as the 001-018 lane); kernel `.config` byte-identical to that lane (`1b9b084e...`); `CONFIG_LUOFU_CRG=y`, `CONFIG_COMMON_CLK=y` |
| `dt_binding_check` | RC=0 for the extended YAML; example preprocesses and compiles (`DTEX`/`DTC [C]`); no diagnostic names the file; only the pre-existing `realtek,rtl8152.yaml` note and the unavailable-`yamllint` skip |
| board DTS | cpp RC=0; patched-kernel `dtc` RC=0 with **zero warnings (stderr 0 bytes)**; DTB 11,346 B sha256 `a31ab53ff4d5c828b05bc29e281c259eb46ec5115f9c1f90aff9675646a7f7da`; the DTB built from the `shared.patch`-applied tree is byte-identical |
| `dt-validate` | RC=0; only the same six pre-existing findings as the 001-018 lane (arm,sp804; two UART clock-names); **none** names the CRG node or a new clock |
| checkpatch | `crg-luofu.c` 0/0, dt-bindings header 0/0, YAML 0/0; bare patch file only reports the expected working-format metadata ("Missing commit description") and the series-wide "DT binding docs should be a separate patch" note |
| shared.patch | `git apply --check` RC=0, `git apply` RC=0, `patch -p1 --dry-run` RC=0, apply RC=0 on a fresh `git archive` of HEAD `0fe742f`; all five applied files byte-identical to the delivered copies |

## 6. What is NOT claimed / open

- **No hardware behaviour.** The driver has never run; no clock rate, PLL lock/status,
  mux parent or gate state is claimed. The PLL arithmetic is decoded from the vendor
  module, not measured; the derived rates are [U] until an attended readback.
- The attended checks remaining: one boot with a read-only probe to print
  `/sys/kernel/debug/clk/clk_summary` (CPU/LSW PLL rates, `cpu_mux`/`efuse_mux`
  parents, gate parents) and to read the two control words at `0x198`/`0x1e0` plus the
  `0x90` status bits. No write experiment is needed for this read-only stage; any future
  `.set_rate` work additionally needs the writer encoding verified.
- The upstream review-form series (`opensource/upstream/series-6.18/`) is **not**
  updated (still 001-018-form); patch 019 is not yet in review form.
- No vendor binaries/blobs are introduced into the repository: the evidence bundle is
  local-only and gitignored; shared.patch and patch 019 add text only.
- `docs/SKELETON-SMOKE.md` at HEAD `0fe742f` contains the bdinfo MAC session twice
  (once with escaped backticks); that is a pre-existing integration defect outside this
  lane's write scope and was left untouched (this lane appends after both copies).

## 7. Regeneration commands (host-only)

~~~sh
# isolated clone (never the shared one)
cp -a --reflink=auto /home/shibbityshwab/openwrt-lab /home/shibbityshwab/openwrt-lab-clk
rsync -a <repo>/target-skeleton/target/linux/hisilicon/ /home/shibbityshwab/openwrt-lab-clk/target/linux/hisilicon/
cp <repo>/target-skeleton/dts/luofu-r116.dts <repo>/target-skeleton/dts/luofu-r116-overlay.dtso \
   /home/shibbityshwab/openwrt-lab-clk/target/linux/hisilicon/dts/
rm -rf /home/shibbityshwab/openwrt-lab-clk/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
cd /home/shibbityshwab/openwrt-lab-clk
make target/linux/prepare V=s
make target/linux/compile V=s -j16

# binding + board DTS (patched kernel tree provides dtc and dt-bindings)
K=/home/shibbityshwab/openwrt-lab-clk/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
PATH=<dtschema-venv>/bin:$PATH make -C $K ARCH=arm dt_binding_check \
    DT_SCHEMA_FILES=Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml
cpp -nostdinc -I $K/include -I $K/arch/arm/boot/dts -undef -D__DTS__ \
    -x assembler-with-cpp -o /tmp/luofu.dts <dts>/luofu-r116.dts
$K/scripts/dtc/dtc -I dts -O dtb -o /tmp/luofu.dtb /tmp/luofu.dts

# config derivation
yes "" | make kernel_oldconfig      # repeat; the emitted target config must not change

# local evidence
build/linux-kbuild/waves/upstream-6.18-clk-20261118/
~~~
