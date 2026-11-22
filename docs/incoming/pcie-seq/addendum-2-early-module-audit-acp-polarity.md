# Addendum 2 (2026-11-22): early-module audit - ONE divergence found: ACP gate polarity

D3 confirmed stock live misc+0x1c = 0x02200800 (our steady state matches
byte-for-byte); D4 CRG words match; D1/D2 applied; the link still does not
train. This addendum audits every early vendor module for boot-time writes
to the PCIe/ACP/system register space (0x1010015c, 0x10100160, 0x10100174,
any 0x10100xxx, the RC misc windows 0x10161000+/0x10165000+), re-verifies
hi_pcie_set_mode, and answers the ACP-fabric question.

## 1. THE ONE FINDING: our acp-gate polarity is INVERTED vs the vendor

Writer: hi_acp.ko, init_module (module loaded by /etc/rc.d/luofu at ~1.05 s,
BEFORE hi_pcie.ko probes at 11.47 s). For every available "hsan,acp-*" node
it applies the node's DT properties through hi_acp_hw_set_cache_attr
(val = (old & ~mask) | (value & mask); isb):

  - "acp-gate" property (3 u32s), applied with the value INVERTED
    (mvneq r2,r2 at init+0x328) when the node is NOT dma-coherent:
      pcie0 (not coherent): 0x10100174 |= 0x02000000   (bit 25 SET)
      pcie1 (not coherent): 0x10100174 |= 0x04000000   (bit 26 SET)
      pie   (coherent):     0x10100174 &= ~0x01000000  (bit 24 cleared)
    The "OFF"/"ON" prints match of_dma_is_coherent and are cosmetic.
  - "acp-pre-cfg-attr" (6 u32s = 2 triples), applied AS-IS for every node
    that has it: ONLY the pie node carries it, so the boot writes are
    0x1010015c = 0x1b1b1b1b and 0x10100160 = 0x16161717 - pie only, never
    for the PCIe ports (their nodes have no pre-cfg-attr property).
  - "acp-later-cfg-attr": applied only via hi_acp_notifier_call_chain();
    no caller exists anywhere (no module imports it; the kernel source tree
    contains no reference), so it never runs at boot.

OUR PATCH 013 does the OPPOSITE of the vendor: it reads acp-gate
<0x10100174 0x2000000/0x4000000 0x0> and computes v &= ~mask | value, i.e.
CLEARS bits 25/26. The vendor's proven-working boot state has them SET.
This is the only divergence in the entire audited register space, and it is
present on every bench boot.

FIX (vendor semantics): apply val = (old & ~mask) | ((~value) & mask) - with
the DT value 0 that is val = old | mask:
  pcie0: writel(readl(0x10100174) | 0x02000000, 0x10100174);
  pcie1: writel(readl(0x10100174) | 0x04000000, 0x10100174);
(0x10100174 is in the ACP block, NOT the forbidden misc window - it is
readable.)

Mechanism note (honest): the gate opens/closes the ACP attribute path for
the port's AXI master traffic. The vendor keeps the path CLOSED for the
non-coherent PCIe RC (bit SET) and OPEN for the coherent pie (bit clear).
Running the RC with the path OPEN (our state) routes its master traffic
through the attribute-checked fabric without valid coherency attributes;
whether that can hold the wrapper LTSSM in DETECT_QUIET is not provable
host-side, but the polarity must be fixed regardless - with the gate open,
the first inbound/RADM transaction after link-up is at risk of a fabric
fault/hang. One bench boot tests it.

## 2. hi_pcie_set_mode re-verified: exactly 0x40000000

.text.unlikely@0x1838: r3 = [priv+0x9c] (4 when the module param g_ep_enable
== 0, its insmod default; the rc.d script passes no param); r2 = [priv+8]
(misc base); r3 = r3 << 28; str r3, [r2]. A single plain 32-bit store of
0x40000000 into misc+0x00, no read-modify-write, no extra bits, no variable
beyond the param. In EP mode (g_ep_enable=1) it stores 0x00000000. The
"PCIe:%d switch to RC mode" print precedes the store. CONFIRMED.

## 3. Everything else in the audited space: NEGATIVE

Method: per module, full objdump -dr; resolved every movw/movt pair and
literal-pool word in 0x10000000-0x20000000 and 0x14880000-0x14900000; listed
every str instruction; cross-checked all undefined-symbol imports of the
hi_crg_*/hi_sysctrl_*/hi_acp_* helper families across ALL 60 modules.

  - hi_sysctrl.ko (system-control block 0x10100000): probe performs NO
    register writes (devm_ioremap only). Its exported read/write helpers
    are imported by ZERO modules - the sysctrl space is never written by any
    vendor module at boot.
  - hi_clk.ko ("hsan,clk" provider): probe writes nothing; its gate/PLL/mux
    ops write only the CRG (0x14880000) and only when a consumer acts. The
    PCIe gate enable is a SET of CRG+0x20 bits 0x0c/0x0d at hi_pcie.ko probe
    time (bits already set by U-Boot; the gate class is write-1-set-only).
    Nothing in the audited space.
  - hi_clk_corrector.ko, hi_basic.ko, hi_kreset.ko, hi_krstinfo.ko,
    hi_wdg.ko, hi_cpufreq.ko, hi_kpie.ko, hi_kpmupwm.ko, hi_efuse.ko,
    hi_kjent_rng.ko: no constants in the audited ranges; probes write no
    registers (struct bookkeeping only). hi_wdg/hi_kreset act on CRG
    watchdog/reset words only when used.
  - Sweep of all 60 modules for 0x1010xxxx/0x1016xxxx/0x100xxxxx constants:
    the only hits are unrelated flag/mask values in hi5622v100_wifi.ko
    (0x1000000c etc.), hi_lsw_pfe.ko (0x3c000000) and hi_xt_fwd.ko
    (0x6000000) - no register addresses.
  - RC misc windows: hi_pcie.ko is the sole writer (the complete audited
    set from vendor-pcie-sequence.md). No other module carries the window
    constants.
  - ACP fabric attributes for the PCIe ports: NONE beyond the acp-gate
    itself (no acp-pre-cfg-attr, no acp-later-cfg-attr on the pcie nodes;
    nothing else writes 0x1010015c/0x10100160 for pcie).
  - Endpoint power: the vendor DTS has exactly ONE regulator node
    (cpu_supply "vdd_logic" via hi_kpmupwm/pwm-regulator - the CPU core
    rail). No Wi-Fi/endpoint supply exists in software; the endpoint rails
    and PERST are board/bootloader-fixed, so the vendor kernel does nothing
    for the endpoint before link-up and neither can we.

## 4. If the ACP polarity fix does not train the link: hardware checklist

Everything RC-side is now either vendor-identical (misc+0x1c = 0x02200800,
CRG gates/resets, full write sequence, exact mode word) or vendor-proven
with the one polarity delta above. The remaining unknowns are physical.
Probe checklist (bench, human-attended, recovery armed):

  1. Rails: meter the Hi5622 supplies (core/1.2/1.8/3.3 V) on the two Wi-Fi
     chips at power-on; compare vendor boot vs our boot - expect identical
     (same U-Boot, no software-controlled Wi-Fi rail exists).
  2. PERST: scope the endpoint PERST line on the vendor boot - expect
     released (high) before the kernel starts; verify the identical state on
     our boot. The DWC RC's own PERST# output drives it during training;
     with LTSSM held in DETECT_QUIET the RC may never assert it - a stuck
     endpoint reset and a held LTSSM are indistinguishable without the
     scope, so MEASURE the line, do not assume.
  3. Vendor-side target captures (misc reads are now authorized on the
     vendor side): while the stock link is UP read 0x10161100 (misc+0x100
     link status, expect 0x8020 bits set), 0x10161110 (misc+0x110, LTSSM in
     bits [23:18] = 0x11 L0, bit 17 = speed-change done) and
     0x10100174 (expect bits 25/26 SET - live confirmation of section 1).
     These three words are the vendor target state to compare against any
     future instrumented read on our image.
  4. GPIO defaults: diff our pinctrl patch 011 pin defaults against the
     vendor hi_kpinctrl tables (no wifi/pcie group exists in the vendor
     tables - if any pin on our side is driving a board line the vendor
     leaves alone, that is a suspect; the LED/uart/gpio groups are the only
     ones defined).
  5. Minimal-sequence experiment: if 1-3 all match, run our kernel with the
     framework's dw_pcie_setup_rc DBI writes neutralized (restore 0x8BC,
     0x80C, RC BARs/bus-number/class/int-line words captured pre-host_init
     after setup_rc runs, keeping only the vendor write set) to rule out any
     remaining framework/wrapper interaction - the vendor never executes
     those DBI writes on this core.

## 5. Consolidated vendor-visible boot-time state for the PCIe ports

  misc+0x1c = 0x02200800 (live-confirmed); misc+0x00 = 0x40000000;
  misc+0x04 = 0 then 0x200000 then 0; misc+0x18 = 0x08000000;
  0x10100174 = bits 25/26 SET (this addendum's finding);
  CRG 0x14880020 = 0x02f8300b; CRG 0x14880034 = 0x000ff309;
  dbi+0x728 = 0x11 (L0); dbi+0x082 = 0x7012 (DL_ACTIVE, gen2);
  cfg+0x000 = 0x59e7 (endpoint VID).
