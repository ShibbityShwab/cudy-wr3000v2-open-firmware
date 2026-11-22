# Fix list for patch 013 + bench test procedure

All values are from the vendor sequence (vendor-pcie-sequence.md). The misc
window must never be read: every misc write below is an absolute full-word
writel of the complete intended value (the vendor read-modify-writes; we
cannot, and the difference only matters if the bootloader leaves bits in
misc+0x1c - see "open question" in the bench procedure).

## Fix list (concrete, in code order)

### A. Driver: pcie-luofu.c

A1. Rename/redocument BIT(13): LUOFU_PCIE_MISC_APP_DBI_GATE = BIT(13) (not
    "IATU_EN"). BIT(11) stays LUOFU_PCIE_MISC_APP_LTSSM_EN.
    Steady-state constant: LUOFU_PCIE_MISC_APP_STEADY = BIT(11)|BIT(21)|BIT(25)
    = 0x02200800 (LTSSM + irq-mask bits; gate bit clear).

A2. luofu_pcie_host_init (runs after resets, before the framework programs
    iATU) - replace the two current writes with the vendor order:

      /* reg_int (vendor hi_pcie_reg_int, both RCs on this board) */
      writel(0x00200000, lpc->misc + 0x04);
      writel(0x08000000, lpc->misc + 0x18);
      /* RC mode (vendor hi_pcie_set_mode) */
      writel(0x40000000, lpc->misc + 0x00);
      /* gate-on; RC command reg; speed cap = 2 (live LNKCAP 0x734c12);
         gate-off (vendor hi_pcie_init_cmd_status_reg +
         hi_pcie_target_link_speed with capability&2) */
      writel(0x00002000, lpc->misc + 0x1c);
      luofu_pcie_write_dbi(pci, pci->dbi_base, 0x04, 4,
                           luofu_pcie_read_dbi(pci, pci->dbi_base, 0x04, 4) | 0x7);
      luofu_pcie_write_dbi(pci, pci->dbi_base, 0x7c, 4,
                           (luofu_pcie_read_dbi(pci, pci->dbi_base, 0x7c, 4) & ~0xF) | 2);
      luofu_pcie_write_dbi(pci, pci->dbi_base, 0xa0, 4,
                           (luofu_pcie_read_dbi(pci, pci->dbi_base, 0xa0, 4) & ~0xF) | 2);
      writel(0x00000000, lpc->misc + 0x1c);
      /* LTSSM enable FIRST (vendor order; BIT13 clear - this is the fix for
         the bench's LTSSM==0x00), then leave the gate SET so the framework's
         iATU writes land while gated; start_link overwrites the viewports
         with the vendor layout anyway. */
      writel(0x00000800, lpc->misc + 0x1c);
      writel(0x00002000, lpc->misc + 0x1c);

A3. luofu_pcie_start_link - replace with the vendor tail, in order:

      /* irq-mask bits (vendor hi_pcie_irq_mask) + keep LTSSM */
      /* iATU viewports 0..2, vendor layout, under the gate: */
      writel(0x00002000, lpc->misc + 0x1c);   /* already set by host_init */
      for (i = 0; i < 3; i++)
          for (j = 0; j < 8; j++)
              writel(luofu_iatu[i][j], pci->dbi_base + 0x900 + 0x200*i + 4*j);
      /* gate off; steady value LTSSM + irq mask, BIT13 clear */
      writel(0x02200800, lpc->misc + 0x1c);
      /* stage-2 retrain (vendor check_link_status, live system runs it) */
      writel(0x00002000, lpc->misc + 0x1c);
      luofu_pcie_write_dbi(pci, pci->dbi_base, 0x80c, 4,
                           luofu_pcie_read_dbi(pci, pci->dbi_base, 0x80c, 4) | 0x20000);
      writel(0x02200800, lpc->misc + 0x1c);

    with luofu_iatu[3][8] =
      RC0: {0x0,0x4,0x80000000,0x50000000,0x0,0x57ffffff,0x0,0x0},
           {0x1,0x0,0x80000000,0x40000000,0x0,0x47ffffff,0x40000000,0x0},
           {0x2,0x2,0x80000000,0x48000000,0x0,0x4fffffff,0x48000000,0x0}
      RC1: cfg 0x68000000/0x6fffffff, mem 0x58000000/0x5fffffff/0x58000000,
           io 0x60000000/0x67ffffff/0x60000000.
    (Derive from a per-node DT "iatu_rc" property like the vendor, or
    hardcode with a channel selector; the values are SoC constants.)

A4. luofu_pcie_map_bus: drop the per-access dw_pcie_prog_outbound_atu
    re-programming entirely. The CFG0 viewport written in A3 is static and
    covers bus 0 dev 0 fn 0; return pp->va_cfg0_base + where when
    (root bus && devfn == 0 && link up), else NULL. Keep
    pci_generic_config_read32/write32.

A5. post_init (runs after the framework's link wait succeeded): add
    writel(0x00000000, lpc->misc + 0x04);   /* vendor post-link */
    and optionally the linkdown plumbing writes:
    writel(0x00800000, lpc->misc + 0x2c);
    (misc+0x28 needs an RMW the write-only rule forbids; skip it or set the
    full word only if the bench decides the window may be read - see
    hardware-limits.md).

A6. Probe: keep the reset order apb/pcs/phy/ctrl; bump the inter-step delay
    from 100 us to ~1 ms (vendor constant 0x20c498 ~= 0.2-0.9 ms) and add the
    same delay after clk enable and after the last deassert (vendor does).

A7. Comments: fix the "[U]" notes - the mode value, the BIT(13) gate
    semantics, the iATU layout and the order are now all recorded facts.

### B. DT / binding (target-skeleton/dts/luofu-r116.dts + the yaml example)

B1. IO ranges: already dropped from ranges in the working tree - keep them
    dropped (LOGIC PIO is 64 KiB system-wide; 2x 8 MiB IO windows cannot
    coexist and the endpoints use no IO space).
B2. Optional: add "iatu_rc" (24-cell array per RC, vendor values) and read
    it in A3 instead of hardcoding; also record "capability = <0x02>" in a
    comment (live system: speed=2 + stage-2 retrain; the decompiled
    /tmp/vendor-base.dts says 0x01 - discrepancy, see hardware-limits.md).
B3. Keep acp-gate as is (semantics verified against hi_acp.ko).

## Bench test procedure (host-only, recovery door armed first)

Preconditions: recovery door READY (tools/recovery-card.sh), bench link OK,
snapshot calibrated; our kernel with the dts working-tree changes (pcie nodes
enabled, acp-gate, no IO ranges). Watch build/uart-logs capture; never read
the misc window (devmem on 0x10161000 is forbidden); DBI reads stay 32-bit
aligned.

  1. Boot and verify probe: expect "luofu-pcie 10160000.pcie: host bridge"
     without the -12 IO collision, then link messages. Pass criteria:
     post_init logs DEBUG1 LINK_UP set and a config read of the endpoint
     through the cfg window (0x50000000/0x68000000) returning 59e7:0005.
     With the vendor sequence the link comes up within ~10 ms of the LTSSM
     enable (vendor boot: 11.4759 -> 11.4845).
  2. If LTSSM (DEBUG0 bits [5:0]) stays 0x00: the gate bit was still set at
     LTSSM-enable time - instrument the driver to print the exact values it
     wrote (never read misc back) and confirm 0x800 preceded any 0x2000.
     Expected progression otherwise: 0x00 DETECT_QUIET -> 0x11 DETECT_ACT ->
     0x21/0x22 POLL -> CONFIG/L0 stages -> DEBUG1 LINK_UP.
  3. If LTSSM progresses but no L0: (a) verify the endpoint is powered:
     the vendor boot proves no GPIO is needed on this board - compare the
     bootloader state (measure the Wi-Fi PERST/rail if available, out of RC
     scope); (b) try adding the iATU step earlier/later relative to LTSSM
     (vendor: after); (c) check 0x10100174 bits 25/26 are clear (this is the
     ACP register, NOT the misc window - reading it is fine);
     (d) re-check the 4 reset lines deasserted in the 0x34 CRG word.
  4. If link up but enumeration fails: re-check the static CFG0 viewport
     words (A3/A4) - a config access with a wrong-layout viewport returns
     garbage VID/DID.
  5. Regression pass: boot the vendor image once and confirm the stock link
     messages again (no permanent state expected on the RC side).
  6. Open question to resolve on the bench (recovery armed): whether
     misc+0x1c may be read in-kernel like the vendor does. If yes, switch
     the steady-state writes to RMW to preserve bootloader bits; if no, keep
     absolute writes of 0x02200800. Do not test reads from userspace.

## Expected diff summary vs today

  +4 misc writes (0x04, 0x18, 0x00 moved, 0x04=0 post-link)
  -BIT(13) no longer persistent; gate discipline around all DBI writes
  +dbi 0x04 |= 7, 0x7c/0xa0 speed=2, 0x80c |= 0x20000
  +vendor-layout iATU (3 viewports x 8 words) instead of framework layout
  +static CFG0 config window (no per-access reprogram)
  +1 ms reset spacing
  comments/binding text corrected
