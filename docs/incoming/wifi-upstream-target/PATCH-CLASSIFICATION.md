# Kernel patch series classification for submission (2026-11-24)

Source of truth: `target-skeleton/target/linux/hisilicon/patches-6.18/` (L0, **67 files**:
001-060 + 062-067 plus `013a`). The release-line manifest
(`../../upstream/series-6.18/release-line/manifest.tsv`, L1 = 55) predates `013a` and is
otherwise hash-identical to the 66 mapped files (verified this session).

Three-layer reminder: **L0** = lab patch stack applied by the target build; **L1** =
release line (L0 minus bench-only SMP diagnostics); **L2** = upstream review form
(`../../upstream/series-6.18/series/`, frozen at 26 patches 001-026 + DTS-0001).

Submission verdicts are for **L2** (what can be sent to the kernel/OpenWrt review).
A release/publish snapshot keeps L1 instead; the two must not be conflated.

## Summary

- **KEEP** (become review-form entries; 7 of them are regenerated with folded hunks): 26
- **FOLD** (no separate upstream entry; hunks move into a KEEP patch or a new RFC entry): 17
- **DROP** (bench-only diagnostics/probes/workarounds; release line or lab only): 24
- total 67 of 67

Resulting submission form: **26 kernel patches + 4 new RFC entries (proposed 0027-0030)
+ DTS-0001**, see `submission-series.md`. Every DROP keeps existing only in the L0/L1 lab
stack; none of them changes a shipped-feature claim.

## Full table

| # | patch | release class | submission verdict | disposition / reason |
| --- | --- | --- | --- | --- |
| 001 | `001-arm-mach-hisilicon-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 002 | `002-arm-kconfig-textofs.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 003 | `003-arm-textofs-nonimmediate.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 004 | `004-arm-clk-reset-luofu-crg.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 005 | `005-arm-watchdog-luofu-wdt.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 006 | `006-arm-mtd-nand-luofu-fmc.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 007 | `007-net-mdio-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 008 | `008-net-phy-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 009 | `009-net-gmac-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 010 | `010-arm-luofu-restart.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 011 | `011-pinctrl-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 012 | `012-arm-thermal-luofu-tvsensor.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 013 | `013-arm-pci-luofu-host.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 013a | `013a-arm-pci-luofu-endpoint-reset-pulse.patch` | (unmapped) | **FOLD** | fold into 0013 (PCIe): carry the board endpoint reset lines as a DT property (reset-gpios) consumed by the DWC glue instead of hardcoded GPIO0 in the driver; same assert/release pulse, same hardware-verified sequence |
| 014 | `014-arm-crg-reset-polarity.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 015 | `015-net-pie-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 016 | `016-net-lsw-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 017 | `017-soc-luofu-boot-selector.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 018 | `018-arm-mach-hisilicon-smp.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 019 | `019-arm-clk-luofu-clock-tree.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 020 | `020-net-pie-datapath-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 021 | `021-net-lsw-init-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 022 | `022-net-lsw-qidmap-write-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 023 | `023-net-gmac-netdev-connect-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 024 | `024-net-pie-completion-decode-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 025 | `025-net-lsw-egress-luofu.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 026 | `026-net-gmac-bdinfo-mac-nvmem.patch` | A | **KEEP** | upstream-review-form base patch; regenerate in v2 with the folded hunks listed below |
| 027 | `027-arm-clk-luofu-readonly-mux-fix.patch` | A | **FOLD** | fold into 0019 (CRG clock tree): 6.18 clk-core compatibility fix belongs in the clock-tree patch itself |
| 028 | `028-arm-hisilicon-smp-diagnostics.patch` | D | **DROP** | bench diagnostic (SMP release trace) |
| 029 | `029-arm-hisilicon-smp-release-bits.patch` | D | **DROP** | bench hypothesis, superseded by the DTS loader hand-off fix |
| 030 | `030-net-pie-txrx-trace.patch` | C | **DROP** | default-off PIE TX/RX trace gate (bench diagnostic) |
| 031 | `031-net-lsw-eps-channel-map.patch` | B | **FOLD** | fold into the LSW init RFC (0016/0021/0022/0025 group): EPS channel-map init is part of the switch bring-up table set |
| 032 | `032-net-lsw-eqm-qminit.patch` | B | **FOLD** | new RFC entry, proposed 0027 (LSW QM/EQM queue-manager init) |
| 033 | `033-arm-hisilicon-smp-release-diagnostics.patch` | D | **DROP** | bench diagnostic (SMP release dumps) |
| 034 | `034-arm-hisilicon-smp-release-probe.patch` | D | **DROP** | bench probe (SMP pen-stub/marker) |
| 035 | `035-arm-hisilicon-smp-release-marker.patch` | D | **DROP** | bench probe (SMP marker protocol iteration) |
| 036 | `036-net-lsw-pfe-init.patch` | B | **FOLD** | new RFC entry, proposed 0028 (LSW PFE/IFC forwarding-engine + default tables); folds with 043/046 |
| 037 | `037-arm-hisilicon-smp-early-probe.patch` | D | **DROP** | bench probe (early setup_arch release experiment) |
| 038 | `038-net-gmac-maxframe-init.patch` | B | **FOLD** | fold into 0009/0023 (GEMAC): max-frame setup is part of the MAC configuration |
| 039 | `039-arm-hisilicon-smp-early-safe.patch` | D | **DROP** | bench probe (early-probe safe variant) |
| 040 | `040-net-gmac-w9-diag.patch` | B | **DROP** | bench diagnostic split (W9); the formula fix is folded into 0009/0023/038 |
| 041 | `041-net-gmac-macconf-nodoorbell.patch` | B | **FOLD** | fold the functional MAC-config replay into 0009/0023; the doorbell bisection gates are bench-only and are dropped |
| 042 | `042-net-gmac-w9-master-off.patch` | B | **DROP** | bench experiment (GEMAC W9 master switch, default off) - not a driver feature |
| 043 | `043-net-lsw-ifc-default-table.patch` | B | **FOLD** | fold into proposed 0028 (LSW PFE/IFC default tables) |
| 044 | `044-net-lsw-ifc-gate-token-fix.patch` | B | **FOLD** | fold into 0022 (the luofu_switch.* option names are the final ones) |
| 045 | `045-net-lsw-state-dump.patch` | C | **DROP** | read-only LSW state dump (W11 diagnostic) |
| 046 | `046-net-lsw-ifc-fix-eps-ext.patch` | B | **FOLD** | fold into proposed 0028 (LSW PFE/IFC extended EPS/DP stage) |
| 047 | `047-net-pie-gmac-rxstate-dump.patch` | C | **DROP** | read-only PIE/GEMAC RX-state dump (W13 diagnostic) |
| 048 | `048-net-pie-rxcfg-w13.patch` | B | **FOLD** | fold into 0020 (PIE): RXCFG is the engine's RX enable word |
| 049 | `049-net-lsw-pie-page-scan.patch` | C | **DROP** | read-only LSW/PIE page scan (W14 diagnostic) |
| 050 | `050-net-lsw-pagedump-w15.patch` | C | **DROP** | read-only LSW page-content dump (W15 diagnostic) |
| 051 | `051-net-gmac-macregs-w16.patch` | B | **FOLD** | fold into 0009/0023 (GEMAC): static MAC configuration words |
| 052 | `052-net-pie-rxcompletion-trace.patch` | C | **DROP** | PIE RX-completion trace gate (diagnostic) |
| 053 | `053-net-pie-rxdecode-vendor-exact.patch` | B | **FOLD** | fold into 0024 (PIE RX completion decode): vendor-exact decode is the implementation, not a follow-up |
| 054 | `054-net-lsw-mirror-cputest-w17.patch` | B | **DROP** | bench CPU-copy mirror experiment base (W17) - release-line workaround for wired LAN, not an upstream driver |
| 055 | `055-net-lsw-mirror-stable-rxstroke.patch` | B | **DROP** | bench stable CPU mirror + RX stroke (W18) - same workaround |
| 056 | `056-net-lsw-mirror-postlink-w19.patch` | B | **DROP** | bench post-link CPU mirror re-latch (W19) - same workaround |
| 057 | `057-net-pie-cpucopy-w20.patch` | B | **DROP** | bench PIE port-0 CPU-copy delivery (W20) - same workaround |
| 058 | `058-net-luofu-canonical-bench-config.patch` | B | **DROP** | canonical bench kernel command line/config (enables the attended gates) - release-line only |
| 059 | `059-net-lsw-napt-init.patch` | B | **FOLD** | new RFC entry, proposed 0029 (LSW NAPT/flow-engine init) |
| 060 | `060-net-pie-exception-delivery.patch` | B | **FOLD** | new RFC entry, proposed 0030 (PIE native exception-class delivery) |
| 062 | `062-arm-hisilicon-smp-scu-gic.patch` | D | **DROP** | bench diagnostic (SCU/GIC reference dumps) |
| 063 | `063-arm-luofu-cpu1-up-trigger.patch` | D | **DROP** | bench diagnostic (CPU1 up retrigger via sysfs) |
| 064 | `064-arm-hisilicon-smp-handoff-probe.patch` | D | **DROP** | bench probe (decompressor/stext hand-off markers) |
| 065 | `065-arm-hisilicon-smp-trampoline.patch` | D | **DROP** | bench probe (sentinel protocol + re-poll trampoline) |
| 066 | `066-thermal-luofu-tvsensor-w23.patch` | B | **FOLD** | fold into 0012 (thermal): correct v1/hwver-3 read path and gated enable/trim sequence |
| 067 | `067-dt-bindings-thermal-luofu-tvsensor-w23.patch` | B | **FOLD** | fold into 0012 (thermal binding) |

Pin the source hashes: `sha256sum` of every file is recorded in
`../../upstream/series-6.18/release-line/manifest.tsv`; only `013a` is new:

```
d4b544a398099e23793e39e4cebc9b81533b749bea1498cae9ef3d5f4636386b  013a-arm-pci-luofu-endpoint-reset-pulse.patch
```

## Runner-up decisions the reviewer must make

1. **013a board GPIO vs DT**: the driver currently hardcodes GPIO0 pins 20/21/28.
   That is acceptable for the bench but not for upstream; the fold into 0013 must
   carry the lines as DT data (`reset-gpios` on the RC node or a board-level
   reset-controller child) and the binding must describe it. The DTS change itself
   is a one-attended-boot gate (active-DTS rule).
2. **PIE/LSW data-path claims**: 0020/0024/0027-0030 describe a data path that is
   only exercised through attended module parameters on the bench. Submit them as
   RFC with explicit unverified lists, or hold them until the gates are replaced by
   device-tree state and a normal boot proves the path. Do not claim Ethernet/Wi-Fi
   support in the cover letter.
3. **DTS-0001 must be regenerated** after 013a moves into the DTS (the current
   DTS-0001 still carries the `wifi-pcie-hog` retired by GP1). Regenerate from the
   single source `target-skeleton/dts/luofu-r116.dts` with the filtering tool, and
   re-run the loader hand-off contract check (the SMP fix depends on it).
4. **SMP diagnostics are not the SMP fix**: 028/029/033/034/035/037/039/062-065 are
   bench probes. Only 0018 + the two DTS loader hand-off nodes fix SMP. Never send the
   probes.

## L1 (release/publish) delta

- L1 must grow 55 -> 56: add `013a` (the GP1 pulse is required on every boot and is in
  the hardware-validated line; the GP1 image carries it).
- `release-line/INCLUDED.txt`, `manifest.tsv` row and `validation/sha256sums.txt`
  need the new row/hash; the release-line validator must be re-run (55 -> 56).
- The 24 DROP patches stay in L0 for lab use; they are already excluded from L1 except
  the class-B/C ones (031,032,036,038,040-044,046,048,051,053-060,066,067). Those are
  in L1 today and must be **reviewed one by one** when the release line is next
  refreshed: the bench-gate machinery is not needed by a release image, but the
  functional init writes in them are what makes the wired data path work on the bench.

## Evidence

- Manifest: `../../upstream/series-6.18/release-line/manifest.tsv`
- Review form: `../../upstream/series-6.18/README.md`, `series/`, `commit-messages/`
- Validation: `../../upstream/series-6.18/04-validation.md` (host-only, 26-patch pass)
- Hardware record of the patches that matter: `../../target-skeleton/RELEASE-NOTES.md`
  and the parent `STATUS.md` sections 3b-3j.
