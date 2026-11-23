# Chip power/reset sequencing: why the vendor ROM runs S1 and our boot does not

**Lane:** wifi-chip-reset (shared task `task-84`), **host-only**, 2026-11-23.
The bench router was never contacted, no image was built, no device was opened and no vendor
byte/binary/firmware/blob is committed here - only derived addresses, DT cell values, structure
layouts, disassembly facts and a bench plan.

**Question.** Golden stock diff: vendor boot has the chip ROM<->image handshake
CA 0x1BF008 = 2 / 0x1BF00C = 0xABCD, a valid engine door 0x4003A7C0 = 0x83FAC000 and ETE
engine-global 0x4003A000 = 0x10A; our boot reads the ROM pre-image values
(0x1BD000 / 0x1BD0D8), an invalid door and an idle engine. rom-s01 proved the value 2 is written
only by mask-ROM stage S1 and that S1 also initialises the ETE engine global/doors. Hypothesis:
the vendor's chip power/reset sequencing makes the chip ROM run S0/S1, and our image leaves the
chip coming out of reset differently.

**Answer, in one line.** The vendor's chip power/reset sequence is **not** in the bootloader, the
platform scripts or the Wi-Fi driver: it is a **PERST/power GPIO pulse on SoC GPIO0 pins 20/21/28
performed by `hi_pcie.ko` (the PCIe RC driver) at RC0 probe time**, before the RC clock is
enabled and before LTSSM/link training; our image drives the same three pins statically HIGH at
early-boot GPIO registration and **never performs the assert-low -> timed-release pulse**. That
missing pulse is the one host-controllable difference in the chip power/reset domain, and it is
benched in section 4.

---

## 0. TL;DR - answers to the four questions

**(a) The vendor's SoC-level chip power/reset actions, in order [V]:**
1. Bootloader: **none**. U-Boot 2022.07 for luofu contains no Wi-Fi/PCIe-init code, no
   materialised GPIO0/GPIO1/PCIe/sysctl base address, no `fdt set/apply` env command and its
   control DT is the base luofu DT (pcie-gpios empty). It boots the kernel 40 bytes after
   reading the slot; the Wi-Fi chip is not touched.
2. Userspace (S15 hsan_start -> `/usr/bin/wifi_init.sh`, "shuangta" branch) insmods
   `hi_pcie.ko`, then `hi5622v100_plat.ko`, then `hi5622v100_wifi.ko`; `hi_kwificlk.ko` is
   absent from the 2.5.24 rootfs (its insmod fails) and no script drives GPIO 20/21/28 or any
   SoC register.
3. **`hi_pcie.ko` probe (RC0, ~11.4 s): the endpoint reset/power pulse.** From the board DT
   overlay (board_id 3 = hsan luofu/R116) `pcie-gpios = <&porta 20 0>, <&porta 21 0>, <&porta 28 0>`,
   `gpio-delay = <50>`, `reset-duration-ms` default 5; for each pin the driver drives the
   line **output LOW (assert)**, waits 5 ms, then drives it **HIGH (release)** and waits 50 ms
   (2.5.24 "gpio-timing-quirk" path: all three asserts first, then the three releases). This
   pulse runs at probe+0x1218, **before** `clk_bulk_prepare` at probe+0x1234 and before every
   CRG reset deassert, RC-mode write and LTSSM enable.
4. Then the RC: clock gate on, apb/pcs/phy/ctrl resets deasserted, misc wrapper, RC mode, DBI
   writes, LTSSM enable, link poll -> "1.1/2.0 Device Link Up" 9 ms later, enumeration.
5. ~2.3 s later `hi5622v100_plat.ko` runs its *PCIe resource* init (the log line
   "`pcie_init_chip_res chip_id[0] bus_id[0]`" comes from `pcie_init_static_res` and means
   "chip PCI resources", **not** a chip reset), then `wlan_power_on` -> firmware download to
   CA 0x01240000 -> release CA 0x40000108 = 0x5a5a -> 0xcece gate. **No further SoC power,
   clock, reset or pin action exists anywhere in the vendor Wi-Fi stack** [V negative: 55
   hisilicon modules + the Wi-Fi modules import no gpio/regulator/clk/reset API; a local stock
   5.10.201 vmlinux and the U-Boot image materialise no GPIO/sysctl/PCIe base address].

**(b) Our image's difference [V].** Our DTS drives GPIO0 20/21/28 output-HIGH from the
`wifi-pcie-hog` at GPIO-port registration (early boot), and patch 013 deliberately contains no
board-GPIO code (the driver only does clk/resets/host_init; `host_init` starts with a
1000 ms settle). Our clean-room transport afterwards does prepare/upload/verify/release
(0x5a5a)/announce (0xcece) - also no pin action. So the chip's only reset/power transition in
our boot is the **static level change at early boot**, not the vendor's timed assert/release
pulse at RC-probe time; and that level change happens ~9-10 s earlier relative to PCIe
enumeration than the vendor's pulse does. That is exactly the chip power/reset domain
difference the hypothesis named.

**(c) Can a host action make the chip ROM run S1?** **Yes - a host action exists and it is the
vendor's own**: the GPIO0 20/21/28 assert/release pulse at RC0 probe. Nothing else in the vendor
or our software stack touches the chip's power/reset/wake domain [V negatives in 2.5 and 8]. We
cannot prove from static analysis that the pulse is *sufficient* for S1 (the mask ROM is
unreadable and S1 is ROM-internal); the next bench action decides it: replicate the pulse in
patch 013 (section 4.1) and read the handshake/engine state. If the state still reads pre-S1
after the pulse is confirmed to have run, the conclusion is definitive: **S1 is not
host-triggerable**, and the gap is ROM/firmware-internal, matching rom-s01 / wifi-ete-engine.

**(d) The single next bench action.** Attended fire of image **GP1** = current image + the
vendor pulse added to `luofu_pcie_probe()` immediately before `clk_prepare_enable()` (keep the
DTS hog for this run), then the normal transport prepare/upload/release and the read pack of
section 4.2. Decision matrix in section 4.3 and stop rules at the end of 4.2.

---

## 1. Method and artifacts

| artifact | identity | role |
| --- | --- | --- |
| `/tmp/rootfs2524b` (squashfs of the official 2.5.24 image, release time 2026-07-27) | local, gitignored | scripts + modules of the golden vendor boot |
| `rootfs-2.4.15` (device dump of the stock 2.4.15 rootfs) | local, gitignored | 2.4.15 module comparison |
| `/tmp/rootfs2524b/lib/hisilicon/ko/hi_pcie.ko` (22,504 B) and 2.4.15's (21,596 B) | local | the endpoint reset/power pulse |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` (364,660 B) | local | Wi-Fi platform driver |
| stock kernel slot dump `dumps/router-20261108/full/mtd11-kernela.bin` | local, gitignored | vendor 5.10.201 kernel + its appended board DTBs |
| stock 5.10.201 vmlinux extracted from that slot (XZ at zImage+0x3CF7) | local, gitignored | whole-kernel constant/board-code scan |
| U-Boot 2022.07 `dumps/router-20261108/uboota.bin` + env `mtd3-enva.bin` | local, gitignored | bootloader negative |
| vendor boot log `build/uart-logs/20261122T024931Z-uart-w24boot.log` | local | timing anchors |
| prior lanes: wifi-rom-s01, wifi-rom-flow, wifi-boot-kick, wifi-ete-engine, wifi-transport-port, pcie-seq | committed reports | S1 contract, handshake page, transport, RC sequence |

Evidence grades: **[V]** verified here from the local artifact with
`tools/chip_power.py` (13/13 selftest), **[V\*]** measured on the bench by an earlier attended
lane (cited), **[I]** inference, **[U]** unknown.

Conventions: chip CPU address = firmware file + 0x40000; host BAR0 convention for chip reads as
in wifi-rom-s01 (CA 0x1BF000 page at BAR0 = CPU+0x6B8000; IO registers at
BAR0 = 0x3B8000 + (CA-0x40000000)). SoC GPIO0 is a DW APB GPIO at 0x10106000 with DATA at +0x00
and DDR at +0x04 (the registers the bench wrote on 2026-11-22).

---

## 2. The vendor chip power/reset sequence (deliverable 1)

### 2.1 The bootloader does nothing to the chip [V]

* `uboota.bin` contains no occurrence of the GPIO0/GPIO1/sysctl/PCIe/UART bases as a literal or
  as a movw/movt pair (corrected ARM imm4:imm12 decode, 0 hits); its only Wi-Fi-related string is
  the reset-reason name "Wifi watchdog reset"; there is no PCIe/wifi init string at all.
* The U-Boot environment (`dumps/router-20261108/full/mtd3-enva.bin`) is minimal:
  `bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` and tftp/mtd update
  commands; no `fdt set`, no `fdt apply`, no `gpio` command.
* U-Boot's control DT (embedded FDT at `uboota.bin`+0x617C8) is the base luofu DT: the
  `pcie@0x10160000/10164000` nodes carry `pcie-gpios` **empty** and there is no GPIO hog.
* The only thing "bootfip" does with the endpoint is select/apply the board **DT overlay** to the
  kernel DT (section 2.3) - DT data, no pin action.
* The ESBC first-stage loader (`mtd0-esbc.bin`) also materialises no SoC GPIO/PCIe base.

=> On a stock boot the Wi-Fi chip's power/reset lines are left at SoC default (GPIO0 DDR=0,
pins inputs) until the vendor **kernel** acts on them.

### 2.2 Userspace chain: module load order only [V]

`/etc/init.d/hsan_start` (START=15) calls `/usr/bin/wifi_init.sh`; the board's chip is the
Hi5622v100, so the "shuangta" branch runs:

    insmod /lib/hisilicon/ko/hi_kwificlk.ko      # ABSENT from 2.5.24 rootfs -> fails
    insmod /lib/hisilicon/ko/hi_pcie.ko          # <- the RC driver, performs the pulse
    insmod /lib/hisilicon/ko/hi5622v100_plat.ko
    insmod /lib/hisilicon/ko/hi5622v100_wifi.ko
    hi5622v100_cal_init.sh

`tools/stock_chain.sh` re-derives this from the extracted rootfs and checks the negatives:
no /sys/class/gpio export of pins 20/21/28 (the only concrete export in the tree is gpio32 in
the non-shipped hi5620 "wuyi" variants; `gpio_switch` is config-driven and has no entries);
no `hi_mw` (the devmem-write wrapper) invocation in any boot script; no pcie-reset script.
So the userspace chain performs no SoC-level action on the Wi-Fi chip.

### 2.3 The board DT overlay is the only place the endpoint pins are wired [V], [V\*]

The vendor kernel image carries the base DT plus two board DTBOs. Decoded with
`chip_power.py overlays`:

| blob | size | board_id | board_name | model | pcie0 pcie-gpios | pcie1 |
| --- | ---: | ---: | --- | --- | --- | --- |
| base DT | 31,220 | - | - | - | empty | empty |
| board 0x00 overlay | 4,592 | 0 | ax3000_lite | (none) | pins **20, 21, 23** | empty |
| **board 0x03 overlay** | 4,920 | 3 | ax3000_lite | **hsan luofu/R116** | pins **20, 21, 28** | empty |

* The running board is board_id 3: the vendor boot log prints
  "Machine model: hsan luofu/R116", which only the 0x03 overlay sets.
* The overlay's pin set **20/21/28 matches exactly** the pins the bench lane found necessary and
  sufficient for both RC links to train (2026-11-22: GPIO0 20/21/28 outputs high; without them
  LTSSM stays 0x00). Board 0x00's 20/21/23 does not match, so this is not a coincidence.
* `gpio-delay = <0x32>` = 50 is in both the base DT and both overlays;
  `reset-duration-ms` is absent -> the driver default 5.
* `capability = <0x03>` in the 0x03 overlay (base DT says 1) is also what the live system does
  (it prints "2.0 Device Link Up", which only happens when capability bit 1 is set) - an
  independent confirmation that the running DT is base + board 0x03 overlay.

[U]: the exact DT of the *2.5.24* kernel build is not available locally (the device's kernelb was
overwritten by our hybrid kernel before the dump; the 2.5.24 sysupgrade container is not
extractable with the local tools). The pin set is considered stable because it matches the bench
measurement on the stock boot; see 2.7 for the 2.5.24-only DT property that must be present.

### 2.4 `hi_pcie.ko`: the endpoint reset/power pulse [V]

Probe order in `hi_pcie_probe` (`.text` 0xCCC, 0xB04 bytes; offsets are function-relative):

| # | probe offset | action |
| ---: | ---: | --- |
| 1 | 0x1024 | `devm_clk_bulk_get` (clocks acquired, not enabled) |
| 2 | 0x10D8 | `of_count_phandle_with_args(pcie-gpios, #gpio-cells)` -> 3 |
| 3 | 0x1104 | `of_find_property(gpio-timing-quirk)` -> flag at +0x384 |
| 4 | 0x1130 | if count==3 && quirk missing -> **`panic`** "...ShuangTa ep device must enable gpio-timing-quirk..." |
| 5 | 0x113C / 0x114C | read `gpio-delay`, `reset-duration-ms` (+ the `-array` variants) |
| 6 | 0x1170 | `devm_gpiod_get_index(dev, "pcie", i)` for each pin |
| 7 | 0x11F4 | `of_find_property(gpio-free-after-use)` -> flag at +0x378 |
| 8 | **0x1218** | **`blx ops[15]` = `hi_pcie_enable_dev` -> THE PULSE** |
| 9 | 0x1234 | `clk_bulk_prepare`, then enable |
| 10 | 0x126C..0x12D8 | `reset_control_deassert(apb_rst, pcs_rst, phy_rst, ctrl_rst)`, 10 us apart |
| 11 | 0x12EC.. | ops[0] reg_int, ops[1] set_mode ("PCIe:0 switch to RC mode"), DBI batch, LTSSM, iATU, link poll |

`hi_pcie_enable_dev` (.text 0x20C, 0x224 bytes; ops table entry 15 at .rodata+0x3C). Two
paths selected by the quirk flag +0x384:

* **quirk path (+0x2F8), used by 2.5.24 on this board [I]:** for every pin
  `gpiod_direction_output(desc, 0)` + delay(reset-duration); then for every pin
  `gpiod_set_value(desc, 1)` + delay(gpio-delay). Interrupts are disabled across the pulse
  (`cpsid i` / `msr CPSR`).
* **plain path (0x20C), used by the 2.4.15-era module [V]:** per pin
  dir_out(0) -> set(0) -> delay(reset-duration) -> set(1) -> delay(gpio-delay) -> next pin.

Delay units: both delays are `arm_delay_ops.const_udelay(0x0CCCCB60)`. With the ARM delay
formula `xloops = usec * UDELAY_MULT, UDELAY_MULT = 2147*HZ + 483648*HZ/1e6` and HZ=100
(the lab build from the same 5.10.201 source tree; the live system's lpj=9969664 is consistent
with HZ=100) this is exactly **1 ms per call** (`0x0CCCCB60 == 1000*214748`). The other
constant used by the RC bring-up, `0x20C498`, is then ~10 us. [I on HZ; the property name
`reset-duration-ms` and the exact 1000x ratio both fit.]

So the vendor's chip reset/power timing is:

    assert:   each of 20/21/28 driven LOW, 5 ms per pin  (>=15 ms total, all low in quirk path)
    release:  each driven HIGH, 50 ms per pin          (>=150 ms total; last pin has 50 ms settle)
    then:     RC clock enable -> CRG resets -> RC-mode/DBI/LTSSM -> link poll

Total GPIO block ~165 ms inside a 1.5 s window between "kmodloader done" (9.90 s) and
"PCIe:0 switch to RC mode" (11.475 s) in the golden log [V\*]. The link trains 9 ms after the
mode write, so the pulse is fully finished before any LTSSM activity.

Register-level equivalent (what the bench plan uses):

| item | value |
| --- | --- |
| GPIO0 controller base | 0x10106000 |
| DATA register (SWPORTA_DR) | 0x10106000 |
| DDR register (SWPORTA_DDR) | 0x10106004 |
| endpoint mask | `BIT(20)|BIT(21)|BIT(28)` = 0x10300000 |
| assert | DATA &= ~mask (all three low), DDR |= mask |
| release | DATA |= mask (all three high) |
| vendor final GPIO0 state | DDR = 0x1330000F, DATA = 0x12300004 [V\* bench read on stock] |
| our image before the hog fix | DDR = 0x0300000F, DATA = 0x0300000F [V\*] |

(0x1330000F/0x12300004 = the three endpoint pins high plus the board LEDs on 0/1/2/3/24/25, which
is why the pulse implementation must be a read-modify-write of the three bits and not a
full-word store.)

### 2.5 What the vendor Wi-Fi stack does NOT do [V negatives]

Verified with `chip_power.py` over the exact 2.5.24 modules:

* `hi5622v100_plat.ko` and `hi5622v100_wifi.ko` import **no** `gpio*`/`gpiod*`/
  `regulator_*`/`clk_*`/`reset_control_*` symbol at all. The only SoC-ish import in the
  whole Wi-Fi stack is `hi_pcie.ko`'s gpiod + reset_control + clk-bulk set for the RC itself.
* The `pcie_init_chip_res` log line is emitted by `pcie_init_static_res`, which is pure PCI
  **resource** bookkeeping (BAR/window tables); `low_power_init` (called from plat.ko's
  `init_module`) is a printk and `return 0` only. Neither resets, powers or wakes the chip.
* The chip-side writes in the vendor stack are: `shuangta_pcie_enable_remap` CA 0x4003A200 = 1,
  `shuangta_pcie_pm_init` CA 0x40000554 |= 0x220 and CA 0x400005BC = 6 (L1SS pinmux), the
  firmware upload CA 0x01240000, the release CA 0x40000108 = 0x5a5a and the 0xcece gate - all
  device-internal, none SoC-level.
* No vendor module materialises the GPIO0/GPIO1 base as a literal or movw/movt pair (corrected
  decoder, 0 hits over all 262 stock modules in `lib/modules/5.10.201` + `lib/hisilicon/ko`),
  and none contains the bench-observed DDR/DATA values 0x1330000F / 0x12300004; the stock 5.10.201
  vmlinux (extracted locally) and the U-Boot/ESBC images are likewise 0 hits for the SoC GPIO,
  sysctl and PCIe bases. A hardcoded SoC GPIO poke outside the DT gpiod path therefore does not
  exist in the vendor software.
* Userspace: see 2.2 - no SoC-level action.

### 2.6 Timeline (vendor golden boot, log anchors) [V\*]

    0.00-9.90  bootloader -> kernel -> userspace; no Wi-Fi chip action
    ~11.4      wifi_init.sh insmods hi_pcie.ko; RC0 probe:
               parse DT -> GPIO pulse (assert 5 ms/pin, release 50 ms/pin) ->
               clk enable -> resets -> RC mode -> LTSSM
    11.475919  "PCIe:0 switch to RC mode"
    11.484490  "PCI:0 1.1 Device Link Up"        (9 ms after mode)
    11.491057  "PCI:0 2.0 Device Link Up"
    11.604615  "PCIe:1 switch to RC mode"        (RC1 has empty pcie-gpios)
    11.803203  "[low_power_init]low_power_init: success"   (plat.ko init_module)
    11.92-13.48 plat.ko INI/config parsing (~1.5 s)
    13.483113  "[PCIEL]pcie_init_chip_res chip_id[0] bus_id[0]"  (resource init, not a reset)
    13.951200  "wlan_power_on chip_id[0]"
    13.954759  "[PCIEL]pcie_main_init chip[0], bus[0]"
    14.09      FIRMWARE.bin -> chip CA 0x01240000
    14.400386  "[wlan_power_on]power on, count [1], current time [320133]us"
    14.42      dev_status_check (pcie0_status=0x2008a236, pcie1_status=0x2000a236)
    15.32      "excp wifi callback register chip0"
    <=31       wi-fi up

The chip ROM therefore starts its S0/S1 boot at ~11.4 s (the only reset event in the boot), with
the RC clocks/resets already under software control but LTSSM still disabled, and the firmware is
handed over ~2.5 s later.

### 2.7 2.4.15 vs 2.5.24: the vendor added the reset-timing quirk [V]

| module | size | gpio-timing-quirk string | consequence |
| --- | ---: | --- | --- |
| 2.4.15 `hi_pcie.ko` | 21,596 B | absent | plain per-pin pulse, no DT requirement |
| 2.5.24 `hi_pcie.ko` | 22,504 B | present | with 3 pcie-gpios and no `gpio-timing-quirk` the driver **panics** at probe+0x17A4 |

The golden boot is 2.5.24 and does not panic, so the 2.5.24 kernel DT must carry
`gpio-timing-quirk` on the pcie0 node [I, but forced: the panic is unconditional on
count==3 && property-missing]. The 2.4.15-era DTBs we can read do not contain it, which is why
they are a useful (but not live) reference. The vendor's addition of a *timing* quirk for exactly
this endpoint is independent evidence that the **timing/grouping of the reset pulse matters** for
this chip - consistent with the hypothesis under test.

---

## 3. Our image's side (deliverable 2)

### 3.1 Devicetree: a static hog, not a pulse [V]

`opensource/target-skeleton/dts/luofu-r116.dts` lines 260-279:

    wifi-pcie-hog {
        gpio-hog;
        gpios = <20 GPIO_ACTIVE_HIGH>, <21 GPIO_ACTIVE_HIGH>, <28 GPIO_ACTIVE_HIGH>;
        output-high;
        line-name = "wifi-pcie-ctl";
    };

The hog is applied when the dw-apb-gpio port registers (early boot, before PCIe probe) and drives
all three lines HIGH once. There is **no low phase and no timing relation to the RC probe**; the
comment records that the stock kernel leaves the three pins high and that the roles were not
decoded. The DTS pcie0/pcie1 nodes (lines 656-712) have no `pcie-gpios` and no
`gpio-delay` - the clean-room binding does not model an endpoint reset line at all.

### 3.2 Patch 013: no board-GPIO code, and the probe order differs [V]

`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch`:

* `luofu_pcie_host_init()` (patch line 336) starts by stating the endpoint pins are owned by the
  DTS hog and that "no board GPIO poking belongs in this driver", then `msleep(1000)`
  (patch line 350) and the vendor wrapper words.
* `luofu_pcie_probe()` (patch lines ~600-650) does `clk_prepare_enable`, then **asserts all
  four CRG resets** (the vendor only deasserts), sleeps 50 ms, deasserts each with 1 ms spacing,
  sleeps 50 ms, `usleep_range(500,1500)`, then `dw_pcie_host_init` -> `host_init` -> RC
  mode/DBI/LTSSM. There is **no write to GPIO0 0x10106000/0x10106004 anywhere in the patch** and no
  DT property that could trigger one.
* Bench history [V\*]: without the hog the two links do not train (LTSSM 0x00); with GPIO0
  20/21/28 driven high they train and enumerate. A 1 s settle was needed because our probe runs
  at ~2.5 s while the vendor module runs at ~11 s - i.e. the only thing our image does today is
  "drive the lines high and wait".

### 3.3 Transport: upload/release, no chip reset [V\*]

`opensource/docs/incoming/wifi-transport-port/REPORT.md`: prepare -> upload 928,920 B to
BAR0+0x6f8000 (CA 0x01240000) -> verify -> release CA 0x40000108 = 0x5a5a -> announce 0xcece ->
poll D2H (DEVICE READY id 2 about 3 s later). No SoC GPIO/clock/reset action.

### 3.4 The delta table (vendor vs ours)

| aspect | vendor 2.5.24 | our image | why it can matter |
| --- | --- | --- | --- |
| endpoint power/reset lines | 3 pins owned by hi_pcie.ko, driven by a gpiod pulse | 3 pins owned by a DTS hog, driven HIGH once | the vendor's reset event does not exist in our boot |
| pulse shape | assert LOW 5 ms/pin (all low together in the quirk path), release HIGH 50 ms/pin | no low phase, no timing | PERST/power-enable semantics need the assert phase; a static level is not a reset |
| when | RC probe, ~11.4 s, *after* clock acquisition but *before* clock enable / CRG resets / LTSSM | early boot, at GPIO registration (~1-2.5 s), ~9 s before our PCIe probe | the chip ROM boot is moved in time relative to the RC (and to the transport) |
| RC clock/reset order | clk enable -> deassert only, then mode | clk enable -> assert+deassert all four | secondary; vendor-faithful order is GP2 follow-up |
| Wi-Fi driver SoC actions | none beyond the pulse | none | - |
| chip ROM start event | the pulse | SoC power-on default / hog release | sole identified power/reset-domain difference |

**Conclusion of the comparison [V]:** our image does not hold the chip in a *different* steady-state
pin level (both end high); it differs in **when and how** the chip is released from reset: the
vendor performs a deliberate assert-low/timed-release pulse immediately before RC bring-up, our
image performs a static level change long before it. Whether that is what makes the ROM skip S1
is exactly what the bench action in section 4 decides.

---

## 4. Decision and the single next bench action (deliverable 3)

### 4.1 Candidate change GP1 (exact)

Add the vendor pulse to patch 013, in `luofu_pcie_probe()` immediately **before**
`clk_prepare_enable(lpc->clk)` (vendor order: pulse at probe+0x1218, `clk_bulk_prepare`
at probe+0x1234). Keep the DTS hog for GP1 (lowest-risk A/B; the pulse still re-resets the chip
because it drives the pins low and back high).

    /* Board_id 3 (R116): the Wi-Fi endpoint power/reset lines are SoC GPIO0
     * pins 20/21/28 (vendor board DT overlay pcie-gpios + gpio-delay=50).
     * Vendor hi_pcie_enable_dev() asserts them LOW, waits reset-duration
     * (5 ms/pin), then releases them HIGH and waits gpio-delay (50 ms/pin),
     * before the RC clock is enabled. RMW keeps the board LED bits intact. */
    #define LUOFU_EP_GPIO_BASE  0x10106000u
    #define LUOFU_EP_GPIO_MASK  (BIT(20) | BIT(21) | BIT(28))

    static int luofu_pcie_ep_reset_pulse(struct device *dev)
    {
            void __iomem *gpio;
            u32 data, ddr;

            gpio = devm_ioremap(dev, LUOFU_EP_GPIO_BASE, 0x1000);
            if (!gpio)
                    return -ENOMEM;
            ddr = readl(gpio + 0x04) | LUOFU_EP_GPIO_MASK;
            writel(ddr, gpio + 0x04);                    /* all three outputs */
            data = readl(gpio);
            writel(data & ~LUOFU_EP_GPIO_MASK, gpio);    /* assert: LOW */
            msleep(20);                                  /* vendor: 3 x 5 ms */
            writel(data | LUOFU_EP_GPIO_MASK, gpio);     /* release: HIGH */
            msleep(100);                                 /* vendor: 3 x 50 ms */
            dev_info(dev, "endpoint reset pulse: data 0x%08x -> 0x%08x, ddr 0x%08x\n",
                     data, readl(gpio), ddr);
            return 0;
    }

and call it at the top of `luofu_pcie_probe()`:

    ret = luofu_pcie_ep_reset_pulse(dev);
    if (ret)
            return ret;

Bench variant **GP2** (only if GP1 shows the pulse but no state change, or if the links
regress): remove `wifi-pcie-hog` from the DTS and let the driver pulse be the only owner of the
pins (vendor-faithful), and/or drop our extra "assert all four CRG resets" step so the RC order
matches the vendor exactly. Do not combine GP1 and GP2 in the first run - they must stay
separable.

Do **not** (first): PCIe hot reset / secondary-bus-reset via the RC bridge control, or any
further chip-register poke - the vendor never does either, and the GPIO pulse is the vendor's own
documented mechanism.

### 4.2 Bench plan (attended, recovery armed)

1. Build GP1 (patch 013 above, hog kept). No other change; keep the DTS and the transport as they
   are. Preflight + init-shadow gates as usual.
2. Fire, boot attended. Confirm in dmesg: one "endpoint reset pulse: data ... -> ..., ddr ..."
   line, then both RC links train (LTSSM 0x11) and both 59e7:0005 endpoints enumerate. If either
   link fails, stop: the pulse level/order needs the GP2 variant before any chip-state reading.
3. Load the transport; `prepare` (6 viewports read back), `upload` (928,920 B), `verify`
   (diffs=0), `release` (0x5a5a), `announce` (0xcece); wait >=3 s for DEVICE READY, keep
   serving the gate.
4. Read pack (read-only, rom-s01 conventions; every read logged):

| # | read | CA | expected if S1 runs |
| ---: | --- | ---: | --- |
| P1 | handshake status | 0x1BF008 | **0x00000002** |
| P2 | handshake magic | 0x1BF00C | **0x0000ABCD** |
| P3 | handshake page word 0 | 0x1BF000 | bits 5/6/13/14 set |
| P4 | engine global | 0x4003A000 | **0x0000010A** |
| P5 | engine door 0 | 0x4003A7C0 | **0x83FAC000** (aligned host-DRAM pointer) |
| P6 | ETE status / ack | 0x4003A874 / 0x4003A870 | live counts, not idle |
| P7 | ETE send words | 0x4003AC00..0x4003AC20 | nonzero when the ETE is armed |
| P8 | engine door 1 | 0x4003A7D0 | golden value [U] - record it |
| P9 | D2H out[1] + transport dev_status | 0x40039014 / BAR0 0x3F1014 | ready + g4 accepted |
| P10 | GPIO0 DATA/DDR | 0x10106000 / 0x10106004 | high after the pulse (sanity) |

5. Stop rules: success -> snapshot everything, then power-cycle; any oops/device-status anomaly ->
   power-cycle immediately, no retry in-session, no `rmmod` after release.

### 4.3 Decision matrix

| P1/P2 after GP1 | meaning | next |
| --- | --- | --- |
| 2 / 0xABCD (and P4/P5/P6 live) | the GPIO pulse **is** the S1 trigger; our boot was missing the vendor chip reset/power sequence | GP2 (hog removed, vendor-faithful order), then the ETE lane and transport work |
| still 0x1BD000 / 0x1BD0D8 (pre-S1) with the log proving the pulse ran and the links trained | **definitive negative**: S1 is not host-triggerable; it is ROM-internal (mask ROM unreadable) | close the host-side power/reset hypothesis; the remaining lever is ROM/firmware-side analysis (rom-s01 pack D: ROM IRQ target CA 0x34/0x38) - do not repeat host register work |
| mixed (e.g. 0 / 0xABCD: image spinning, partner silent) | the pulse was applied but the inter-core partner (ROM S1) still did not answer | the S1 service itself is absent at firmware time; hand to the ROM/IRQ-vector lane with the pulse kept |
| links regress / no enumeration | the pulse level/order is wrong for our driver timing | GP2: remove the hog, vendor-faithful order, re-run once |

### 4.4 Why this is the right single action

* It is the **vendor's own** chip power/reset action, at the vendor's own point in the sequence,
  with the vendor's own timing - not an invented poke.
* It is the **only** power/reset-domain difference left between the two boots after every other
  action was scanned to a negative (sections 2.5 and 5).
* It is cheap and reversible: one dev_info line, read-modify-write of three GPIO bits, no new
  register windows, no chip writes.
* Both outcomes are decisive: either S1 appears (host lever confirmed, and the whole hypothesis
  chain closes) or it does not (host control over S1 disproved with the vendor sequence applied -
  a definitive negative).

---

## 5. Evidence table (new claims of this lane)

| # | claim | method | grade |
| ---: | --- | --- | --- |
| C1 | The stock bootloader performs no Wi-Fi chip power/reset/pin action | U-Boot + ESBC constant scan (corrected movw/movt), U-Boot env, U-Boot control DT, uboot_capture logs | [V] |
| C2 | The stock userspace only insmods modules; no script drives GPIO0 20/21/28 or any SoC register | `tools/stock_chain.sh` over the 2.5.24 rootfs (PASS 4/4 checks) | [V] |
| C3 | The running board is board_id 3 (R116) and its pcie0 node carries pcie-gpios = pins 20/21/28, gpio-delay=50, reset-duration default 5 | kernel-slot DTB decode + boot-log model + bench pin measurement | [V] + [V\*] |
| C4 | hi_pcie.ko drives the pulse through ops[15] = hi_pcie_enable_dev at probe+0x1218, before clk_bulk_prepare (0x1234) and before any CRG reset/LTSSM write | disassembly + ops-table relocation decode | [V] |
| C5 | The pulse is assert LOW 5 ms/pin, release HIGH 50 ms/pin (quirk path) / per-pin (2.4.15 path) | enable_dev disassembly + delay-constant unit computation (HZ=100) | [V] + [I] |
| C6 | 2.5.24 requires gpio-timing-quirk for a 3-pin endpoint (panic otherwise); 2.4.15 does not | module string/branch comparison | [V] |
| C7 | No vendor module, the stock kernel image, or the bootloader hardcodes a SoC GPIO/sysctl/PCIe base | whole-image constant scan over all 262 stock modules + stock vmlinux + U-Boot | [V] |
| C8 | plat.ko's "pcie_init_chip_res" is PCI resource bookkeeping, not a chip reset; low_power_init is printk-only; the Wi-Fi stack imports no gpio/clk/reset API | symbol/call/string disassembly | [V] |
| C9 | Our image drives 20/21/28 statically HIGH in the DTS hog and never pulses them; patch 013 has no GPIO code | DTS lines 260-279, patch 013 lines 336-350 / 600-650 | [V] |
| C10 | The vendor's only chip power/reset action is the RC-probe GPIO pulse; the only remaining host-side difference is that our image omits it | C1-C9 synthesis | [I] |
| C11 | The pulse is the single candidate host action that could make the ROM run S1; its sufficiency is decided by the GP1 read pack | C3-C10 + rom-s01 S1 contract | [I]/[U] |

---

## 6. Reproduction (host-only, reads local artifacts in place)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-chip-reset/tools/chip_power.py

    $PY $T selftest                 # 13/13 PASS - every [V] fact in this report
    $PY $T gpio                     # the vendor endpoint reset/power pulse
    $PY $T overlays dumps/router-20261108/full/mtd11-kernela.bin
                                    # base DT + board 0x00/0x03 overlays, pcie-gpios
    $PY $T dis hi_pcie_enable_dev   # the pulse, both paths
    $PY $T calls hi_pcie_probe      # probe order incl. clk_bulk_prepare
    $PY $T logmap 'power|reset'     # vendor Wi-Fi stack log map

    sh opensource/docs/incoming/wifi-chip-reset/tools/stock_chain.sh /tmp/rootfs2524b
                                    # userspace chain + 4 negatives

Vendor artifacts stay local and gitignored: the extracted rootfs, the NAND slot dumps, the
extracted stock vmlinux and U-Boot images. The tools only read them in place and print derived
facts; no vendor bytes, firmware bytes, calibration data or secrets are committed.

---

## 7. Limitations / unknowns

1. **[U] The 2.5.24 kernel DT itself** (board 0x03 overlay with `gpio-timing-quirk`) is not
   available locally; the pin set is known from the 2.4.15-era image + the stock bench
   measurement, and the quirk property is forced by the 2.5.24 driver's panic branch [I].
2. **[U] HZ of the vendor kernel** is not directly readable (no IKCONFIG in the stock vmlinux);
   HZ=100 is used from the same-source lab build and fits both constants exactly as 10 us and
   1 ms. The bench plan uses >=20 ms assert / >=100 ms settle so it is HZ-independent.
3. **[U] Which pin is PERST and which is power-enable/LDO**: the vendor treats all three
   identically (one gpiod list); the bench history only shows that all three must be high and
   that pin 28 behaves like PERST/power. The pulse drives all three the same way, so the bench
   does not need the individual roles.
4. **[U] Whether the pulse is sufficient for S1** - that is the bench outcome, by construction.
5. The claim that the chip ROM starts at the pulse (rather than at SoC power-on) is [I] from the
   vendor code shape: PERST# semantics + the vendor's "gpio-timing-quirk" for the ShuangTa
   endpoint + the absence of any other reset event. The bench observable P3 (handshake page
   word 0 bits) will show whether the page was rewritten after the pulse.

---

## 8. Negative results / do-not-chase

* No SoC-level power/reset/clock action exists in `hi5622v100_plat.ko`, `hi5622v100_wifi.ko`,
  `hi_kwificlk` (not shipped), the init scripts, `gpio_switch`, `leds.sh` or `platform.sh` [V].
* No hardcoded GPIO0/sysctl/PCIe register poke in any vendor module, the stock kernel or the
  bootloader [V].
* The bootloader does not train PCIe or touch the Wi-Fi chip [V].
* Do not re-run the pre-hog 2026-11-22 bisect or repeat the "GPIO0 20/21/28 high is required"
  measurement - it is settled; the open question is the pulse, not the level.
* Do not use a PCIe hot reset / secondary-bus reset or a further chip-register poke as the first
  lever: neither is in the vendor path, and out[5] / misc reads retain their hard rules.

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
contents are derived addresses, constants, DT cell values and structure layouts. Device untouched
by this lane.*
