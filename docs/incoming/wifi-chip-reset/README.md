# wifi-chip-reset - the vendor chip power/reset sequence and the missing S1 trigger

Host-only lane (**task-84**, 2026-11-23). The bench router was never contacted; no image was
built; no vendor binary, firmware byte, calibration value or secret is committed. Read
`REPORT.md` for the full analysis; this file is the short index.

## What is here

| file | purpose |
| --- | --- |
| `REPORT.md` | the vendor chip power/reset sequence, the comparison with our image, the host-action decision, the GP1 bench plan + decision matrix, evidence table |
| `tools/chip_power.py` | self-checking analyzer for the vendor modules and board DTBs: `selftest` (13 checks), `gpio` (the endpoint reset/power pulse), `overlays` (board DTBO -> pcie-gpios pins), `dis/calls/callers/logmap/consts/diff` |
| `tools/stock_chain.sh` | prints the stock userspace chain and checks the four negatives (no GPIO 20/21/28 export, no SoC write wrapper invocation, no pcie-reset script) from an extracted rootfs |

## Headline results

* **The vendor's chip power/reset sequence is a GPIO pulse, not a bootloader/script action.**
  Board DT overlay board_id 3 (hsan luofu/R116) gives the two PCIe nodes
  `pcie-gpios = <&porta 20 0>, <&porta 21 0>, <&porta 28 0>` and `gpio-delay = <50>`; the vendor RC
  driver `hi_pcie.ko` (ops[15] = `hi_pcie_enable_dev`, invoked at probe+0x1218, **before**
  `clk_bulk_prepare` at 0x1234) drives all three pins LOW (assert), waits
  `reset-duration-ms` (5 ms/pin), then HIGH (release) and waits `gpio-delay` (50 ms/pin) - the
  2.5.24 "gpio-timing-quirk" path groups the three asserts and then the three releases. That is
  the only SoC-level chip power/reset action in the whole stock boot: the bootloader does
  nothing, the userspace only insmods modules, no module in the 262-module stock set hardcodes
  the GPIO/sysctl/PCIe base, and the Wi-Fi stack (plat.ko/wifi.ko) imports no
  gpio/clk/regulator/reset API at all. The platform log line "`pcie_init_chip_res`" is PCI
  **resource** bookkeeping, not a chip reset.
* **Our image never performs it.** The DTS `wifi-pcie-hog` drives GPIO0 20/21/28 output-HIGH once
  at early-boot GPIO registration; patch 013 contains no board-GPIO code (the driver has
  `clk_prepare_enable` -> resets -> `host_init` with `msleep(1000)` -> RC/DBI/LTSSM) and
  `host_init` explicitly defers the pins to the hog. Same final pin levels, but no assert pulse
  and a different timing relative to PCIe enumeration.
* **Decision: a host action exists - the vendor's own pulse - and it is the single next bench
  action.** GP1 = current image + the pulse added to `luofu_pcie_probe()` before the clock
  enable (keep the hog for GP1). Expected observables after the normal transport
  prepare/upload/release: handshake CA 0x1BF008 = 2, CA 0x1BF00C = 0xABCD, engine door
  0x4003A7C0 = 0x83FAC000, engine global 0x4003A000 = 0x10A, live ETE status/send, D2H ready + g4.
  If the state stays pre-S1 with the pulse confirmed and the links trained, the conclusion is
  definitive: **S1 is not host-triggerable** (ROM-internal) and host-side register work is over.
* **Vendor evidence that reset timing matters for this endpoint:** the 2.4.15 RC module pulses
  pins per-pin with no DT requirement, while the 2.5.24 module requires `gpio-timing-quirk` for
  a 3-pin endpoint and panics without it - the vendor added a reset-timing fix for exactly this
  chip.

## Reproduce

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=$PWD/tools
    $PY $T/chip_power.py selftest          # 13/13 PASS
    $PY $T/chip_power.py gpio              # the vendor pulse + register recipe
    $PY $T/chip_power.py overlays dumps/router-20261108/full/mtd11-kernela.bin
    sh $T/stock_chain.sh /tmp/rootfs2524b        # userspace chain + 4 negatives

The tools read the locally extracted stock rootfs / NAND kernel-slot dump in place (both
gitignored local evidence) and print derived facts only. `PLAT_KO` / `HI_PCIE_KO` / `SLOT`
override the default paths.

## Constraints honoured

No device contact, no builds, no vendor binaries/firmware bytes/calibration/secrets in this
directory, no superproject commit (the lane's writes stay under
`opensource/docs/incoming/wifi-chip-reset/`).
