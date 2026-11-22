# Diff: patch 013 vs the vendor sequence

Reference: pcie-luofu.c in
target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch
(current working-tree state) vs hi_pcie.ko hi_pcie_probe.

## 1. misc+0x1c BIT(13) semantics - the main bug

Vendor: BIT(13) is a TRANSIENT DBI-write gate. Set immediately before every
batch of DBI writes (command reg, speed cap, iATU, ASPM, retrain) and cleared
immediately after. At the moment the LTSSM-enable bit is written, BIT(13) is
cleared. The steady post-init value of misc+0x1c is 0x800 | 0x02200000.

Patch 013: host_init writes misc+0x1c = BIT(13) and start_link writes
misc+0x1c = BIT(13)|BIT(11). BIT(13) is never cleared and is set while the
LTSSM enable lands. Bench result matches: LTSSM stays 0x00, DEBUG0 churns.
Writing only 0x800 (bench's "recorded vendor order" test) lets the LTSSM
field progress - consistent with BIT(13)-set inhibiting training.

The comment in the patch ("recorded vendor order asserts iATU-enable, programs
the iATU, then enables the LTSSM with the iATU-enable bit cleared") and the
LUOFU_PCIE_MISC_APP_IATU_EN name are wrong about what the bit does; it is a
DBI gate, not an iATU enable.

## 2. Missing wrapper writes

Vendor steps absent from patch 013:

  - misc+0x04 = 0x00200000 and misc+0x18 = 0x08000000 before RC mode
    (hi_pcie_reg_int; both RCs on this board).
  - misc+0x04 = 0x00000000 after link-up.
  - misc+0x1c |= 0x02200000 (irq mask) after LTSSM enable.
  - misc+0x2c |= 0x00800000 and misc+0x28 &= ~0x00001000 after link-up
    (linkdown interrupt plumbing; the patch currently has no interrupt
    handler at all - acceptable as TODO, but the writes should be recorded).

## 3. Missing DBI writes

Vendor writes the root port's own config/port registers under the gate:

  - dbi+0x04 |= 0x0007 (PCI_COMMAND IO|MEM|BME) - absent.
  - dbi+0x7c and dbi+0xa0: speed field = 2 (5 GT/s target; live LNKCAP
    0x734c12 proves the live system ran the speed=2 path) - absent.
  - dbi+0x80c |= 0x20000 (stage-2 retrain; the live "2.0 Device Link Up"
    proves it runs) - absent.

## 4. iATU programming is incompatible with the stock framework

Vendor viewport layout (8 words at dbi+0x900 + 0x200*i):
  +0x00 index, +0x04 type, +0x08 0x80000000, +0x0c base, +0x10 0,
  +0x14 limit, +0x18 target, +0x1c 0.

Mainline dw_pcie_prog_outbound_atu writes (see pcie-designware.c):
  +0x00 CTRL1 = 0x80000000 (enable), +0x04 CTRL2 = type,
  +0x08 base, +0x0c base-hi, +0x10 limit, +0x14 target, +0x18 target-hi.

Everything below +0x04 is shifted by +4 on this core, the enable word sits at
+0x08, and CTRL1 carries the plain index. The framework's writes therefore
land in the wrong registers and its CFG0 programming in luofu_pcie_map_bus()
(re-programmed on every config access) is also wrong-layout. The vendor never
re-programs the CFG0 viewport per access: it is a static window
(cfg base 0x50000000/0x68000000 -> bus 0 dev 0 fn 0) and read_conf just
indexes cfg_base + offset.

Also the ORDER differs: vendor enables LTSSM (misc+0x1c |= 0x800) BEFORE
programming the iATU, and does the iATU writes under the gate; the framework
programs iATU (gate state unspecified, wrong layout) before start_link runs.

## 5. Reset sequence

Order apb->pcs->phy->ctrl matches. Vendor inter-step delay ~0.2-0.9 ms each
(constant 0x20c498); patch 013 uses 100 us - close enough, but a 1 ms delay
is a safer match. The vendor also delays once after clk enable and again
after the last deassert.

## 6. ACP gate

Patch already applies the DT acp-gate RMW; semantics identical to
hi_acp.ko's hi_acp_hw_set_cache_attr ((v & ~mask) | value). No change needed.
Live value 0x00210321 has bits 25/26 clear (= powered) and this is not part
of hi_pcie.ko's probe anyway (bootloader state).

## 7. GPIO/PERST

No difference on this board: vendor pcie-gpios is empty, no pin is toggled,
the endpoint is powered by bootloader defaults. Patch 013's decision not to
claim GPIOs matches the vendor for this board. The enable_dev/disable_dev
mechanism is documented in vendor-pcie-sequence.md section 4 for boards that
do carry GPIOs.

## 8. DT

The IO window has already been dropped from ranges (working-tree dts) which
fixes the LOGIC-PIO 64 KiB collision that made luofu-pcie probe fail with -12
(evidence: build/uart-logs/20261122T024931Z-uart-w24boot.log, our-kernel
boot, "resource collision: [io 0x0000-0x7fffff] conflicts with PCI IO").
The vendor keeps 8 MiB IO windows, but its own resource model is not
LOGIC-PIO-limited; the endpoints use no IO space (vendor boot assigns only
MEM BARs), so dropping IO ranges loses nothing.

## 9. Status polling

Vendor polls the wrapper status registers (misc+0x100/0x110); patch 013 polls
the DBI PCIE_PORT_DEBUG0/DEBUG1. Equivalent in effect and it avoids reading
the misc window (our rule). No change required, but the post_init log should
also print the LTSSM field from DEBUG0 bits [5:0] to compare against the
vendor's 36-state names.

## 10. What patch 013 already matches

  - misc+0x00 = 0x40000000 (value correct; move earlier per fix list).
  - BIT(11) = 0x800 as LTSSM enable.
  - 4 resets, names/order apb, pcs, phy, ctrl.
  - acp-gate RMW.
  - 32-bit-only config accessors + the aligned-dword rule.
  - The measured topology assumption (one function per RC at devfn 0).
