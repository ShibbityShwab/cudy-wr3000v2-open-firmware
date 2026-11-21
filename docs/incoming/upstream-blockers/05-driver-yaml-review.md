# Blocker 1 - prioritized driver/YAML review (top upstream-blocking issues)

Scope: the 26-patch kernel review form and the DTS companion.  This is a
prioritized sanity review, not a rewrite; every item below is still open unless
stated otherwise.  It is derived from the existing series self-review
(`../../../upstream/series-6.18/00-cover-letter.md` "Known review items to fix
before sending"), the recorded checkpatch categories, and the drafting work in
this directory.

## P1 - must be fixed before any posting

1. **Split DT bindings from driver patches** (checkpatch flags ~22 times).
   One binding patch per driver, before its driver; bindings currently embedded
   in 0004/0015/0016/0019-0026 must be split and the dt-bindings headers go with
   their bindings.  This also enables a real `dt_binding_check` pass.
2. **Add the MAINTAINERS entry** (`02-maintainers-proposal.md`; checkpatch flags
   ~18 new-file warnings): cover 0018 `platsmp.c`, 0020 `luofu-pie.h`, 0021
   `luofu-switch-regs.h`, 0022 `luofu-switch.h` and the driver files.
3. **DCO/provenance**: real legal name in Signed-off-by and the provenance
   paragraph (GPL-2.0 reverse-engineering basis; vendor GPL request unanswered).
   The SSH-email identity placeholders must not ship.
4. **Mechanical checkpatch cleanups**: wrap the 16 over-100-column CRG clock
   table lines (112 columns), add `spinlock_t` comments, fix two alignment
   continuations in luofu-switch.c, expand two Kconfig help texts to 4+ lines.
5. **Patch hygiene**: fold 0014 into 0004 (and decide 0019 fold/follow-up);
   regenerate 0008 against mainline so it does not apply with -75/-12 offsets.
6. **Name consistency before subsystem review**: LUOFU_FMC vs SPI_LUOFU_FMC;
   `luofu_tvsensor.c` underscore vs hyphen naming.

## P2 - reviewer-visible, design-level

7. **Thermal is dead on this board**: the sensor reads 0 and the vendor DT
   disables it; either drop 0012 from the submission or mark it clearly
   not-mergable/needs-hardware.
8. **PIE/LSW/GEMAC are gated drafts**: document the attended opt-ins
   (`luofu_pie.arm=1`, `luofu_lsw.arm=1`/`qidmap=1`, readiness
   contract) in each commit message and state that no data path/writes have run
   on hardware; the nodes stay disabled in upstream DTS.
9. **Custom binding properties need explicit justification**: `hisilicon,port`
   (measured switch port, not ordering), `hisilicon,port-bitmap`,
   `hisilicon,crg`, two-cell CRG reset specifier, `atag-offset` (see the
   flashinfo draft here).  Each must be part of a reviewed binding, not a
   driver-only property.
10. **Wi-Fi scope**: state vendor-only/blocked in every platform submission.
11. **Loader/install scope**: state the FIP/TEXT_OFFSET dependency and that
    installation is out of scope until a first-install path is validated.
12. **v6.19 nvmem dependency**: remove it for a 6.18 submission (blocker 3 draft
    here); do not claim `mac-base` compatibility on 6.18.

## P3 - follow-ups once direction is agreed

13. Real `dt_binding_check`/`dtbs_check` run is unavailable in this environment
    (no dtschema/dt-validate/yamllint); once available, every YAML in the split
    binding patches must pass.
14. Reconsider module parameters/attended gates for upstream (upstream drivers
    should not require `.arm=1` command-line opt-ins; either make the probe
    self-sufficient or keep the work out of the series until it is).
15. Port the switch binding to MMIO-first order (blocker 4 draft here) and the
    compatible ordering (blocker 5 draft here) as separate reviewed patches.

## Not claimed

No driver/YAML was rewritten in this package, no build or boot was run, and no
upstream Tested-by/Reviewed-by exists.
