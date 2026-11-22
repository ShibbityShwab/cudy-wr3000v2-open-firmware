# pcie-seq: vendor RC bring-up register sequence (Cudy WR3000 v2 / Hi5671Y luofu)

Reverse-engineering lane output for the PCIe root-complex bring-up on the
HiSilicon Hi5671YV200 "luofu" SoC. Goal: the exact vendor register sequence
so the 6.18 driver
`target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch`
can train the link to the two Hi5622v100 Wi-Fi endpoints (59e7:0005).

Files (text only, no blobs):

- `vendor-pcie-sequence.md`  - deliverable 1: the complete vendor init
  sequence (every register write, address, value, order) for the RC bring-up
  (hi_pcie.ko) and the Wi-Fi-side post-link steps (hi5622v100_plat.ko),
  including the iatu_rc/iatu_ep window programming, GPIO/PERST mechanics and
  timings.
- `diff-vs-patch-013.md`    - deliverable 2: divergence of patch 013 from
  the vendor sequence.
- `fix-list-patch-013.md`   - deliverable 3: the concrete fix list (exact
  writes/order) plus a bench test procedure.
- `hardware-limits.md`      - deliverable 4: what cannot be controlled from
  the RC driver (endpoint PERST/power GPIOs, iomux, inbound iATU, MSI, misc
  read policy, live-DTB discrepancies).

Method / evidence:

- `hi_pcie.ko` (the RC bring-up driver, prints "PCIe:0 switch to RC mode"
  and "PCI:0 1.1/2.0 Device Link Up") disassembled with
  build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump -dr.
  Key functions: hi_pcie_probe (0xb8c), hi_pcie_reg_int (0x2f4),
  hi_pcie_set_mode (.text.unlikely, "PCIe:0 switch to RC mode"),
  hi_pcie_init_cmd_status_reg (0x614), hi_pcie_target_link_speed (0x6a4),
  hi_pcie_enable_ltssm (0x0), hi_pcie_irq_mask (0x31c),
  hi_pcie_set_iatu (0x71c), hi_pcie_enable_aspm (0x658),
  hi_pcie_check_link_status (0x354), hi_pcie_enable_dev (0x20c),
  hi_pcie_disable_dev (0x5c0). The call order is fixed by the ops table
  (hi_pcie_data, .rodata): ops[0]=reg_int, [1]=set_mode,
  [2]=init_cmd_status_reg, [3]=target_link_speed, [4]=enable_ltssm,
  [7]=irq_mask, [13]=set_iatu, [14]=enable_aspm (only when DT has
  "aspm-enable"), [17]=check_link_status, [9]=mask_link_timeout_flush_not_irq,
  [8]=enable_linkdown_irq.
- `hi5622v100_plat.ko` (Wi-Fi plat; prints "[PCIEL]pcie_main_init",
  "oal_pcie_get_phy_devid", "oal_pcie_enable_remap", "pcie_msg_init")
  disassembled likewise: oal_pcie_get_phy_devid (0xbe5c),
  oal_pcie_host_init (0xbefc), oal_pcie_enable_remap(.part.0) (0x82c0/0x6a94),
  shuangta_pcie_enable_remap (0x1ae3c), pcie_msg_init (0xb6e4),
  pcie_inbound_viewport_switch (0x9264), pcie_inbound_region_cfg (0x94e0),
  oal_pcie_set_inbound_by_viewport (0x97a8), oal_pcie_dev_init (0xa090),
  pcie_main_init (0x704).
- Vendor DTB decompiled at /tmp/vendor-base.dts (pcie0/pcie1 nodes: iatu_rc,
  iatu_ep, acp-gate, gpio-delay=0x32, empty pcie-gpios).
- Stock UART logs: build/uart-logs/20261107T191003Z-poweron-uboot.log and
  build/uart-logs/20261122T024931Z-uart-w24boot.log (vendor boot: "PCIe:0
  switch to RC mode" at 11.47 s, "PCI:0 1.1/2.0 Device Link Up" ~9 ms later;
  the same file also contains an earlier OUR-kernel boot whose luofu-pcie
  probe failed with -12 on an IO-window resource collision, since fixed by
  dropping the IO ranges).

Regeneration: re-run the objdump commands above against
/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi_pcie.ko and
hi5622v100_plat.ko. Nothing here is derived from vendor binary blobs; only
decoded register facts are recorded.

Caveats: vendor delay constants 0x20c498 / 0x0CCCCB60 are raw values passed
to arm_delay_ops.delay (lpj=9969664 on the live system); the absolute time
depends on CONFIG_HZ and is only approximate here (see vendor-pcie-sequence.md).
The decompiled /tmp/vendor-base.dts appears to be an older variant than the
running image (see hardware-limits.md "live-DTB discrepancy").
