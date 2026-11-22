# Addendum 1 (2026-11-22): bench still DETECT_QUIET - PHY/version verdict, DBI LTSSM recipe, remaining order fixes

Follow-up to the A1-A7 bench run (image b97767f1): LTSSM still 0x00 at
post_init and +40 s, DEBUG0 upper bits churn (0x892b00/0x17b500 pcie0,
0xb34000/0xb8cf00 pcie1) - controller logic clocked, LTSSM never leaves
DETECT_QUIET. This addendum answers the four questions, reports the
remaining deviations, and gives the exact DBI poll recipe.

## 1. "wrong version" / phy-id paths: THERE IS NO PHY INIT (verified exhaustively)

- oal_pcie_host_init (hi5622v100_plat.ko @0xbefc, 0x1dc bytes) performs ZERO
  register writes. Verified instruction-by-instruction: its complete store
  set is kmalloc'd-struct fields only (priv struct at kmalloc(0xa20):
  [r4+16]=pdev, [r4+20]=bus, [r4+196]=&g_st_rcregion_devarray+24*idx,
  [r4+24]=phy_devid, mutex init, change_link_state(priv, 2)). The
  "wrong version::0x1152x" print is gated ONLY on the module log level
  (LANCHOR0[0] >= 0) - it prints for EVERY value; the version is never
  compared. It is debug noise with a misleading name.
- The value itself: pci_read_config_dword(endpoint, 0xff8) - the ENDPOINT's
  PCIe PHY id register (0x1152 = phy family, LSB = revision: pcie0 endpoint
  rev 1 = 0x11521, pcie1 endpoint rev 0 = 0x11520). phy_devid = val & 0xF
  only selects which slot in the per-chip priv array the probe fills
  (oal_pcie_probe @0x4d0 stores g_pcie_devs[phy_devid] = priv, checks
  phy_devid < board chip count). No PLL/PHY register writes depend on it.
- Timing proof: this whole path runs at 13.9-14.4 s in the vendor boot
  ("pcie_init_chip_res" 13.483, "pcie_main_init" 13.954) - 2.4 s AFTER the
  link is up ("PCI:0 1.1 Device Link Up" 11.484). It cannot be part of
  training.
- Completeness proof for the RC side: a full store audit of hi_pcie.ko
  (every str in every function) shows the ONLY hardware writes are:
    misc+0x00 (set_mode), misc+0x04/misc+0x18 (reg_int), misc+0x1c
    (gate/LTSSM/irq-mask), misc+0x28/misc+0x2c (linkdown irq),
    dbi+0x04 (cmd), dbi+0x7c/0xa0 (speed), dbi+0x80 (aspm, skipped here),
    dbi+0x80c (SPEED_CHANGE), dbi+0x900-0x91c (iATU).
    No PHY registers, no other misc offsets, no DBI PHY regs exist in the
    vendor RC driver.
- U-Boot does nothing PCIe either (console capture: no PCIe lines, "Net:
  eth0: pie@0x10a70000" only), and hi_crg.ko's probe only ioremaps (no CRG
  stores). So the ENTIRE vendor PHY init = clock gate + 4 reset deasserts +
  wrapper LTSSM bit. There is nothing else to find in the modules.

## 2. Between set_mode and the LTSSM write: nothing else exists

Vendor order is fixed by the ops table and contains exactly:
  set_mode (misc+0x00 = 0x40000000)
  -> init_cmd_status_reg: gate |= 0x2000; dbi+0x04 |= 0x7; gate &= ~0x2000
  -> target_link_speed: gate |= 0x2000; dbi+0x7c[3:0] = 2; dbi+0xa0[3:0] = 2;
     gate &= ~0x2000
  -> enable_ltssm: misc+0x1c |= 0x800
No PHY configuration through misc offsets or DBI PHY registers exists
(store audit above). Our port already contains all of these steps.

## 3. DBI LTSSM register, expected values, vendor timing (measured facts)

- LTSSM: PCIE_PORT_DEBUG0 = dbi+0x728, bits [4:0], mask 0x1f,
  L0 = 0x11. The vendor's own 36-entry state table (g_pcie_ltssm_status,
  .data, 30 B/entry: DETECT_QUIET=0, DETECT_ACT=1, POLL_ACTIVE=2, ...,
  L0=17=0x11, ..., RCVRY_EQ3=35=0x23) and the 6.18 constant
  PORT_LOGIC_LTSSM_STATE_L0 = 0x11 agree. The team's own live measurement on
  the vendor system read dbi+0x728 = 0x11 while the link was UP.
- Link state: dbi+0x082 (the DBI config-space PCI_EXP_LNKSTA) = 0x7012 on
  the live vendor system: BIT(13) DL_ACTIVE + speed field 2 (5 GT/s).
- DEBUG1 = dbi+0x72c (LINK_UP bit).
- Endpoint check: cfg window read at 0x50000000 (RC0) / 0x68000000 (RC1) =
  0x59e7 (VID) on the live vendor system.
- Vendor timing (w24boot log, both RCs): "PCIe:0 switch to RC mode"
  11.475919 -> "PCI:0 1.1 Device Link Up" 11.484490 -> "PCI:0 2.0 Device
  Link Up" 11.491057. I.e. L0 is reached in < 8.6 ms of the set_mode write
  (the first status poll after LTSSM-enable passes immediately; the LTSSM
  enable itself sits ~1-3 us after set_mode inside the same probe), and the
  gen2 speed-change completes by ~15 ms. Any RC sequence that takes > ~10 ms
  to leave DETECT_QUIET is already wrong - there is no hidden PHY warm-up.
- Poll recipe for our driver (all DBI, all aligned dword reads):
    loop (max ~1 s, sleep 10-20 ms):
      ltssm = readl(dbi+0x728) & 0x1f
      lnksta = readl(dbi+0x082)
      if (ltssm == 0x11 && (lnksta & BIT(13))) -> up
      debug1 = readl(dbi+0x72c)   // log-only
  The wrapper status registers (misc+0x100/0x110) are the vendor's
  equivalent; do not read them under our rule.

## 4. IRQ-mask write: CONFIRMED equivalent

Vendor: misc+0x1c |= 0x800 (LTSSM), then later misc+0x1c |= 0x2200000
(mask) - two RMWs, ~us apart. Our single absolute write 0x02200800 produces
the identical final value; nothing depends on the intermediate state.
0x02200800 vs 0x02200000|0x800 are the same word. No change needed.

## 5. The remaining real deviations (ranked) - and the one fix that matters most

The bench regression vs the earlier "0x800 alone" test points at the bits we
destroy or the enable toggling we do that the vendor never does:

D1 (FIX, exactness): the vendor's LTSSM bit, once written, STAYS SET
    forever (its gate dance is |= 0x2000 ... &= ~0x2000, which preserves
    bit 11). Our port writes absolute values, so every gate-on write
    (0x2000) CLEARS the LTSSM bit and the final 0x02200800 re-sets it -
    bit 11 toggles 0->1->0->1 across the boot. If the wrapper's LTSSM
    machine treats enable-clear as machine-reset (typical), every gate
    window re-resets it to DETECT_QUIET. FIX: make every gate-on window
    value 0x2800 (BIT(13)|BIT(11)) so the LTSSM bit stays set continuously,
    and keep the steady-state write 0x02200800. Vendor-equivalent values
    for all our misc+0x1c writes: gate-on = 0x2800, gate-off-with-LTSSM =
    0x0800, steady = 0x02200800.

D2 (FIX, fidelity): PORT_LOGIC_SPEED_CHANGE (dbi+0x80c |= BIT(17)) - the
    vendor applies it ONLY after stage-1 link-up (check_link_status stage 2).
    Our start_link applies it pre-link. Note the 6.18 framework ALSO sets it
    pre-link inside dw_pcie_setup_rc (val |= PORT_LOGIC_SPEED_CHANGE - same
    register, same bit), so our extra write is redundant, not novel; but to
    match the vendor exactly, remove it from start_link and apply it in
    post_init after the framework's link wait succeeded (the framework
    pre-link one is harmless - every mainline DWC board runs that way).

D3 (BENCH CHECK, the last unverifiable difference): the vendor RMWs
    misc+0x1c and therefore PRESERVES every pre-existing bit of the
    register; our absolute writes destroy all bits outside
    {13, 11, 21, 25}. If the wrapper's reset value carries other bits
    (e.g. PHY power enables), the vendor keeps them and we kill them -
    exactly matching "controller logic alive, LTSSM never trains". This is
    the only remaining divergence that fits the symptom and cannot be
    resolved host-side. BENCH: with the recovery door armed, on the VENDOR
    image, one dword read of 0x1016101c via devmem (the vendor kernel reads
    this offset constantly in-kernel; the read-panic that created our rule
    was a userspace read of 0x10161000+0x00). If it reads nonzero bits
    outside {0x800, 0x2000, 0x2200000}: (a) our driver must switch to RMW
    semantics for misc+0x1c (permit a kernel-side read of this one offset,
    which the vendor does all the time), or (b) OR the captured reset value
    into every absolute write. If it reads 0 or only known bits, D1/D2 are
    the whole story.

D4 (BENCH CHECK, cheap): read the CRG words on OUR kernel (0x14880020 and
    0x14880034 - CRG, not the forbidden window). Vendor live state:
    0x20 = 0x02f8300b (pcie0_clk bit 0x0c and pcie1_clk bit 0x0d SET; the
    gate class is write-1-set-only latches, so U-Boot's set state survives
    our kernel's "clk: Disabling unused clocks" - the write-0 is inert and
    cannot be the problem) and 0x34 = 0x000ff309 (all pcie reset bits SET =
    released). If our kernel's words differ from these, the clock/reset
    driver is the gap; if equal, the RC window state is the only remaining
    RC-side variable (D3).

D5 (checked, NOT a gap): 6.18 dw_pcie_setup_rc (verified against
    torvalds/linux v6.18) writes: 0x8BC |= BIT(0) (DBI ro-wr-en, cleared
    after), outbound ATU (we overwrite viewports 0-2 with vendor words),
    RC BARs 0x10/0x14, interrupt line 0x3c, bus numbers 0x18, command 0x04
    = IO|MEM|MASTER|SERR (0x147 - the vendor writes 0x7 there; the extra
    SERR bit is harmless), config presets, class (writew), and
    0x80C |= SPEED_CHANGE. It does NOT write 0x710 (no DLL_LINK_EN
    clobber - the 6.18 header keeps the classic layout:
    PORT_LINK_DLL_LINK_EN = BIT(5) at 0x710, PORT_LINK_MODE_MASK =
    GENMASK(21,16); 0x80C: N_FTS GENMASK(7,0), SPEED_CHANGE BIT(17),
    width GENMASK(12,8)). None of these touch the wrapper LTSSM.

D6 (checked, NOT a gap): hi_kpinctrl.ko carries NO pcie/PERST/wifi pin
    groups (full table audit: sfc/uart0/uart1/mdio1/spi/i2c/rgmii0/led
    groups only) and its probe drives no pins; the vendor does nothing for
    the endpoint before link-up. The endpoint is powered and PERST-released
    by the board/bootloader by default. If D1-D4 all pass and the link is
    still down, the remaining unknown is the endpoint side (measure the
    PERST line / rail at the chip) - hardware probing, outside the RC
    driver.

## 6. Updated recommended misc+0x1c write sequence for patch 013

  host_init (after reg_int + mode + gated cmd/speed):
      writel(0x00000800, misc+0x1c);   // LTSSM on, gate clear (vendor step 7)
      writel(0x00002800, misc+0x1c);   // gate on, LTSSM KEPT ON (D1)
  start_link:
      [write 3 vendor viewports x 8 words]   // under gate, LTSSM on
      writel(0x02200800, misc+0x1c);   // gate clear, LTSSM + irq mask
      // no SPEED_CHANGE here (D2)
  post_init (after framework link-up):
      writel(dbi+0x80c, readl(dbi+0x80c) | BIT(17));   // vendor stage 2 (D2)
      writel(0x00000000, misc+0x04);
  (if D3 confirms a nonzero reset value: OR it into the 0x800/0x2800/
   0x02200800 writes, or switch these three writes to RMW.)

## 7. Answer summary for the four questions

  1. Version-dependent PHY init: NONE EXISTS. oal_pcie_host_init has zero
     register writes; "wrong version" is unconditional debug noise; the phy
     id (endpoint cfg 0xff8) only picks the priv-array slot; the path runs
     2.4 s after link-up. Stop looking for PLL/PHY writes in the modules -
     the store audit proves the complete write set is the one in
     vendor-pcie-sequence.md.
  2. Between set_mode and LTSSM: only cmd-reg (dbi+0x04 |= 7) and speed-cap
     (dbi+0x7c/0xa0 = 2) under the gate. No misc PHY offsets, no DBI PHY
     regs. Complete.
  3. DBI LTSSM: dbi+0x728, mask 0x1f, L0 = 0x11 (measured live-vendor
     dbi+0x728 = 0x11); link check dbi+0x082 == 0x7012 (DL_ACTIVE + gen2);
     DEBUG1 dbi+0x72c. Vendor reaches L0 < 9 ms after the LTSSM write
     (11.475919 -> 11.484490), gen2 done by ~15 ms.
  4. IRQ-mask: 0x02200800 == 0x02200000|0x800, order irrelevant. Confirmed.
     The order issues that DO matter are D1 (LTSSM bit must never be
     cleared once set - use 0x2800 gate windows) and D2 (SPEED_CHANGE
     post-link).
