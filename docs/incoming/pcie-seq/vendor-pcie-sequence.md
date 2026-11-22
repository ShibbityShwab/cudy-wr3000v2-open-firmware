# Vendor PCIe bring-up sequence (exact)

Sources: hi_pcie.ko and hi5622v100_plat.ko disassembly (ARM, arm-none-linux-
gnueabihf-objdump -dr), /tmp/vendor-base.dts, stock UART logs. Addresses are
function offsets inside each module's .text unless noted.

Windows per root complex:

| window | RC0 | RC1 |
| --- | --- | --- |
| dbi   | 0x10160000 | 0x10164000 |
| misc (SoC wrapper) | 0x10161000 | 0x10165000 |
| cfg   | 0x50000000 (4 KiB) | 0x68000000 (4 KiB) |
| mem   | 0x40000000 (32 MiB) | 0x58000000 (32 MiB) |
| io    | 0x48000000 (8 MiB) | 0x60000000 (8 MiB) |

Both RCs take the same code path on this board: the DT property "hwver" is
0x10 (16) for both, and every hwver==16 branch below is taken by both. (The
module also contains hwver==32 variants used by other boards: status regs at
misc+0x200 instead of misc+0x100/0x110, linkdown irq bit 7 instead of 12,
iATU programmed through a DT resource named "iatu"; none of that applies
here.)

misc window semantics learned from the vendor module:

- misc+0x00 : mode select. 0x40000000 = RC (EP mode would write 0).
- misc+0x04 : written 0x00200000 before init, 0 after link-up ("reg_int",
  radm/interrupt related; NOT link-critical, see pcie1 note below).
- misc+0x18 : written 0x08000000 before init (same step).
- misc+0x1c : app control.
    BIT(13) 0x2000 : TRANSIENT DBI-WRITE GATE. The vendor sets it
        immediately before every batch of DBI writes and clears it
        immediately after (init_cmd_status_reg, target_link_speed, set_iatu,
        enable_aspm, check_link_status stage 2 all do |= 0x2000 ... &=
        ~0x2000 around their dbi writes). It is never left set, and at the
        moment the LTSSM-enable bit is written it is cleared. This matches
        the bench: with BIT(13) left set, LTSSM stays 0x00.
    BIT(11) 0x800 : LTSSM enable.
    BIT(21)|BIT(25) 0x02200000 : "irq mask" bits, set right after LTSSM
        enable (hwver==16 only).
- misc+0x28 : linkdown irq mask (hwver16: clear BIT(12); hwver32: BIT(7)).
- misc+0x2c : linkdown irq clear; BIT(23) 0x800000 masked for "link timeout
  flush" (hwver16).
- misc+0x100 : link status word (hwver16): wait for BIT(5)|BIT(15) = 0x8020.
- misc+0x110 : second-stage link status (hwver16): BIT(17) 0x20000; LTSSM
  state in bits [23:18] (36-state table, DETECT_QUIET..RCVRY_EQ3).
- misc+0x200 : hwver32 equivalents (bits [9:8] 0x300 stage1, 0x700 stage2).

The vendor driver READS misc+0x1c/0x28/0x2c/0x100/0x104/0x110/0x200 freely
in-kernel. Our standing rule ("never read the misc window", see
hardware-limits.md) is stricter; nothing below requires reading misc, and the
bench procedure uses the DBI DEBUG0/DEBUG1 registers instead.

Timing units: the vendor calls arm_delay_ops.delay(X) with raw constants.
With lpj = 9969664 and the kernel's us * lpj/(1e6/HZ) scaling, X/2492 = us
at HZ=250 (X/9969 at HZ=1000). So delay(0x20c498) ~= 0.86 ms (or 0.21 ms at
HZ=1000) and delay(0x0CCCCB60) ~= 86 ms (or 21.5 ms). These are the only two
constants used. The exact HZ of the vendor kernel is unverified; treat the
small delay as "about 0.2-1 ms" and the poll interval as "about 20-90 ms".

## 1. RC bring-up - hi_pcie.ko hi_pcie_probe (runs for each RC, hwver 16)

Order is exact; RMW means the vendor read-modify-writes (we cannot read misc,
so our driver must write absolute values - see fix-list).

  1. devm_clk_bulk_get("pcie_clk"); clk_bulk_prepare(); clk_bulk_enable().
     (CRG gate clock; RC0/RC1 = CRG 0x2012/0x2013.)
  2. delay(0x20c498)                                 ~0.2-0.9 ms
  3. reset_control_deassert("apb_rst"); delay(0x20c498)
     (CRG word 0x34: RC0 bit 12 / RC1 bit 16; active-low: set = released)
  4. reset_control_deassert("pcs_rst"); delay(0x20c498)   (bit 13 / 17)
  5. reset_control_deassert("phy_rst"); delay(0x20c498)   (bit 14 / 18)
  6. reset_control_deassert("ctrl_rst"); delay(0x20c498)  (bit 15 / 19)
  7. hi_pcie_reg_int:
       writel(0x00200000, misc+0x04)
       writel(0x08000000, misc+0x18)
     (hwver==16 only; since both RCs are hwver 16, both get this.
     Not link-critical: in the hwver32 variant pcie1 links up without it,
     and it is the interrupt path - but replicate for fidelity.)
  8. hi_pcie_set_mode:
       writel(0x40000000, misc+0x00)                   "PCIe:%d switch to RC mode"
     (0x40000000 = 4<<28. In EP mode (module param g_ep_enable) it writes 0.)
  9. hi_pcie_init_cmd_status_reg:
       misc+0x1c |= 0x2000        (DBI gate on)
       dbi+0x04  |= 0x0007        (RC's own config command reg: IO|MEM|BME)
       misc+0x1c &= ~0x2000       (DBI gate off)
 10. hi_pcie_target_link_speed (skipped if capability == 0):
       speed = (capability & 2) ? 2 : (capability & 1)
       misc+0x1c |= 0x2000
       dbi+0x7c = (dbi+0x7c & ~0xF) | speed     (root-port PCI_EXP_LNKCAP)
       dbi+0xa0 = (dbi+0xa0 & ~0xF) | speed     (root-port PCI_EXP_LNKCTL2)
       misc+0x1c &= ~0x2000
     Live system: capability has bit 1 set (see the "2.0 Device Link Up"
     stage below and the measured LNKCAP 0x734c12), so speed = 2 = 5 GT/s.
 11. hi_pcie_enable_ltssm:
       misc+0x1c |= 0x0800        (RMW; BIT(13) is clear at this point)
 12. hi_pcie_irq_mask:
       misc+0x1c |= 0x02200000
     Steady state after this step (assuming reset value 0):
       misc+0x1c == 0x02200800
 13. hi_pcie_set_iatu:
       misc+0x1c |= 0x2000
       for i in 0..2: write the 8 words of iatu_rc[i] to dbi+0x900+0x200*i
       misc+0x1c &= ~0x2000
 14. hi_pcie_enable_aspm - ONLY if the DT node has "aspm-enable" (it does
     not on this board, so this is SKIPPED here):
       misc+0x1c |= 0x2000; dbi+0x80 |= 0x3 (LNKCTL ASPM L0s+L1);
       misc+0x1c &= ~0x2000. (hwver32 returns without writing anything.)
 15. hi_pcie_check_link_status:
       stage 1: poll misc+0x100 until (misc+0x100 & 0x8020) == 0x8020 on
       2 consecutive reads, at most 501 iterations with delay(0x0CCCCB60)
       between; print "PCI:%d 1.1 Device Link Up" (or fail with
       "1.1 Device Link Down").
       stage 2 (only when capability & 2 - the live system runs it):
         misc+0x1c |= 0x2000
         dbi+0x80c |= 0x20000     (gen2 re-negotiation/retrain request)
         misc+0x1c &= ~0x2000
         poll misc+0x110 until BIT(17) (0x20000) set, 2 consecutive reads,
         <= 501 x delay(0x0CCCCB60); print "PCI:%d 2.0 Device Link Up".
 16. post-link: writel(0x00000000, misc+0x04)   (only on success)
 17. hi_pcie_mask_link_timeout_flush_not_irq:
       misc+0x2c |= 0x00800000
 18. hi_pcie_enable_linkdown_irq:
       misc+0x28 &= ~0x00001000
 19. devm_request_threaded_irq(linkdown irq) - GIC SPI 69 (RC0) / 70 (RC1)
 20. pci_scan_root_bus_bridge / pci_bus_size_bridges / pci_bus_assign_resources
     / pcie_bus_configure_settings / pci_bus_add_devices; then proc files
     ltssm + link_status (hi_pcie_init_dfx - no hardware writes).

Observed on the vendor boot (w24boot log): "PCIe:0 switch to RC mode" at
11.475919, "PCI:0 1.1 Device Link Up" at 11.484490, "PCI:0 2.0 Device Link
Up" at 11.491057 - i.e. the first poll already succeeds and the whole RC
bring-up per port takes < 10 ms.

## 2. Outbound iATU table (iatu_rc, written in step 13)

Viewport registers: dbi+0x900 + 0x200*i. Each entry is 8 words, written in
this order:

  +0x00  index        (0, 1, 2)          - region index
  +0x04  type         (4=CFG0, 0=MEM, 2=IO)
  +0x08  0x80000000   (constant; region enable/size word)
  +0x0c  base
  +0x10  0x00000000
  +0x14  limit        (base + 0x07ffffff)
  +0x18  target
  +0x1c  0x00000000

RC0 (DT iatu_rc):
  viewport 0: 0x00000000, 0x00000004, 0x80000000, 0x50000000, 0x00000000,
              0x57ffffff, 0x00000000, 0x00000000      (CFG0 -> bus 0 dev 0 fn 0)
  viewport 1: 0x00000001, 0x00000000, 0x80000000, 0x40000000, 0x00000000,
              0x47ffffff, 0x40000000, 0x00000000      (MEM identity)
  viewport 2: 0x00000002, 0x00000002, 0x80000000, 0x48000000, 0x00000000,
              0x4fffffff, 0x48000000, 0x00000000      (IO identity)

RC1: identical shape with cfg 0x68000000/0x6fffffff, mem
0x58000000/0x5fffffff/0x58000000, io 0x60000000/0x67ffffff/0x60000000.

IMPORTANT: this register layout does NOT match the mainline DWC layout that
pcie-designware.c writes (mainline puts the enable bit in CTRL1 at +0x00,
type in CTRL2 at +0x04, base at +0x08, limit at +0x10, target at +0x14).
On this core everything from base onwards is shifted +4: base at +0x0c,
limit at +0x14, target at +0x18, with the extra 0x80000000 word at +0x08.
The stock framework iATU programming therefore lands in the wrong
registers and must not be relied on.

## 3. iatu_ep (DT) - endpoint-mode table, NOT used in RC mode

<0x02 0x00 0x80000000 0x30000000 0x00 0x307fffff 0xab000000 0x00>:
viewport 2, type MEM, base 0x30000000 (8 MiB), target 0xab000000. Applied
only when the SoC PCIe runs as endpoint (module param g_ep_enable); irrelevant
for RC bring-up, recorded for completeness.

## 4. GPIO / PERST / endpoint power

hi_pcie.ko parses "pcie-gpios" (of_count_phandle_with_args), con-id "pcie"
(devm_gpiod_get_index), "gpio-delay" (DT: 0x32 = 50) and
"reset-duration-ms" (default 5). On THIS board the "pcie-gpios" property is
present but EMPTY (no phandles), so the GPIO loop never runs and hi_pcie.ko
toggles no pin: the link trains purely from steps 1-15 above, i.e. the
endpoint is already powered and PERST-released by the bootloader. The Wi-Fi
side ("wlan_pm_open chip_id[0]" / "wlan_power_on chip_id[0]") runs 2 seconds
later and is power-management/firmware, not link-train.

For boards that DO carry GPIOs (mechanism, not active here), per endpoint:
  gpiod_direction_output(desc, 0)
  gpiod_set_value(desc, 0)                    (PERST asserted)
  wait "reset-duration-ms" x delay(0x0CCCCB60)
  gpiod_set_value(desc, 1)                    (PERST released)
  wait "gpio-delay" (50) x delay(0x0CCCCB60)  (~1-4.5 s on this constant!)
disable_dev just drives value 0 for every gpio.

## 5. ACP gate

DT acp-gate = <0x10100174 0x02000000 0x00> (RC0) / <0x10100174 0x04000000
0x00> (RC1): RMW (v & ~mask) | value at 0x10100174. This is NOT part of the
hi_pcie.ko probe; it is the acp-gate property (handled by hi_acp.ko
notifier API, unused at boot on this system - the bootloader leaves bits 25/26
clear, matching the live read 0x00210321). Our patch 013 already applies the
same RMW from the same DT property; keep it as a defensive step.

## 6. Wi-Fi-side post-link sequence (hi5622v100_plat.ko)

Runs after the RC is enumerated (~2 s later in the vendor boot); not required
for link training, but it is the rest of the vendor order and documents what
a future Wi-Fi driver must replicate.

  pcie_init_chip_res -> pcie_main_init per (chip, bus):
    pcie_get_bus_id_host_view / pcie_get_chip_id / get_chip_type
    pci_enable_device(endpoint)
    pci_read_config_byte (class/rev check)
    oal_pcie_host_init(pdev, bus, idx):
      pci_read_config_dword(pdev, 0xff8) -> phy id
        (RC0 = 0x11521 -> phy_devid 1; RC1 = 0x11520 -> phy_devid 0;
         the "wrong version::0x1152x" print is unconditional debug noise,
         the value is never rejected)
      allocate host context; oal_pcie_change_link_state(priv, 2)
    oal_pcie_dev_init:
      ioremap the endpoint BAR windows (BAR0 0x40000000/0x58000000 and the
      BAR2 DBI window 0x41800000/0x59800000 - BAR assignment from the boot
      log)
      program ENDPOINT-side region tables through the mapped windows:
        window+0x104 + 0x200*i for 16 regions:
          {0x00000000, 0x80000000 | ((type & 7) << 8), base_lo, base_hi,
           limit, target_lo, target_hi}   (two writes to +0x104: 0 then ctrl)
        and one region at window+0x0 with the same word layout
      pci_write_config_word(endpoint, 0x04, 0x0007)  (endpoint PCI_COMMAND
      = IO|MEM|BME; issued twice in the flow)
      oal_pcie_change_link_state(priv, 3)
    oal_pcie_set_inbound_by_viewport - RC INBOUND iATU, addressed through
    the RC's own CONFIG SPACE offsets 0x900-0x91c:
      per region i:
        pci_write_config_dword(RC, 0x900, 0x80000000 | i)
        pci_read_config_dword(RC, 0x900)  (verify readback)
        pci_write_config_dword(RC, 0x908, 0)
        pci_write_config_dword(RC, 0x908, 0x80000000 | ((type & 7) << 8))
        pci_write_config_dword(RC, 0x90c, base_lo)
        pci_write_config_dword(RC, 0x910, base_hi)
        pci_write_config_dword(RC, 0x914, base + size - 1)
        pci_write_config_dword(RC, 0x918, target_lo)
        pci_write_config_dword(RC, 0x91c, target_hi)
      then pci_write_config_word(endpoint, 0x04, 0x0007)
      (region table contents are runtime-built: g_region_table, 16 x 80 B,
       from the static g_shuangta_region_types descriptors)
    oal_pcie_enable_remap -> shuangta_pcie_enable_remap:
      convert DEVICE address 0x4003a200 to a host VA through the inbound
      map; dsb; arm_heavy_mb; *(u32 *)hostva = 1   (endpoint-side "remap
      enable" flag, write goes over the PCIe MEM window into BAR0+0x3a200)
    pcie_msg_init: software message rings only (no register writes)
    oal_pcie_probe_irq_init: enable_irq etc.

## 7. Order summary (RC bring-up, the part patch 013 must replicate)

  clk on -> delay -> apb_rst off -> delay -> pcs_rst off -> delay ->
  phy_rst off -> delay -> ctrl_rst off -> delay ->
  misc+0x04 = 0x00200000; misc+0x18 = 0x08000000 ->
  misc+0x00 = 0x40000000 ->
  [gate] dbi+0x04 |= 0x7 [ungate] ->
  [gate] dbi+0x7c[3:0] = 2; dbi+0xa0[3:0] = 2 [ungate] ->
  misc+0x1c |= 0x0800 (LTSSM; BIT13 clear) ->
  misc+0x1c |= 0x02200000 ->
  [gate] iATU viewports 0..2 (8 words each, vendor layout) [ungate] ->
  (no aspm-enable here) ->
  poll link (misc+0x100 & 0x8020) ->
  [gate] dbi+0x80c |= 0x20000 [ungate] -> poll (misc+0x110 bit 17) ->
  misc+0x04 = 0 ->
  misc+0x2c |= 0x00800000; misc+0x28 &= ~0x00001000 -> linkdown irq.
