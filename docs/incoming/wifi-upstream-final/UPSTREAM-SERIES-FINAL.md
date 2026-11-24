# Upstream series - final state (2026-11-27)

**Scope.** The luofu 6.18 kernel patch series that would go to the kernel/OpenWrt review, its
three layers, the frozen v1 review form, the required v2 generation, and the go/no-go gates.
Host-only; **nothing has been posted anywhere**; no maintainer has reviewed any of this.

## 1. The three layers (do not conflate them)

| layer | what it is | where |
| --- | --- | --- |
| **L0** | the lab patch stack applied by the target build: **67 files** (001-060, 062-067, plus `013a`) | `target-skeleton/target/linux/hisilicon/patches-6.18/` |
| **L1** | the release line: L0 minus bench-only SMP diagnostics; **55 patches** in the manifest (must become **56** - see section 3) | `upstream/series-6.18/release-line/` |
| **L2** | the upstream review form (what can be sent to review): **v1 = 26 kernel patches + DTS-0001**, frozen 2026-11-22; **v2 = 30 + DTS-0001** (planned) | `upstream/series-6.18/series/`, `dts-series/` |

A release/publish snapshot keeps L1; the review submission is L2. The 24 DROP patches stay in
L0 for lab use and never go to either L1-clean or L2.

## 2. The frozen v1 review form (what actually exists today)

`upstream/series-6.18/` is the complete review artefact: 26 format-patch files, DTS-0001, the
cover letter, per-patch commit messages, MAINTAINERS proposal, tree/order rationale and a
host-only validation record.

Recorded validation (2026-11-22, host-only, no hardware):

* 26/26 patches apply to pristine `linux-6.18.60` with both `patch(1)` and `git am`,
  identical resulting trees; 3 offset hunks, 0 fuzz;
* `checkpatch.pl`: **0 errors / 73 warnings** (patch 0013 alone: 0 errors / 17 warnings);
* PCIe binding `dt_binding_check` RC=0 (dtschema 2026.9);
* release-line validator PASS: 55/55, 0 fuzz / 0 rejects, strict `git apply` 55/55;
* DTS-0001 compiled DTB 12,697 B sha256 `288a5c3f...`, pad 12,704 B; board DTB 12,865 B
  sha256 `d8b41ac3...`;
* `submission/MANIFEST.sha256` (105 entries) and `validation/sha256sums.txt` (93 entries,
  `sha256sum -c` 93/93) recorded.

**Re-run this session (2026-11-27):** `python3 tools/gen-series.py --check` ->
**"series up to date (26 patches)"**, exit 0. There is no drift between the generated form and
the L0 patches *as of the v1 mapping*.

**Hardware caveat already recorded in v1:** the 2026-11-22 DTS additions needed one regression
boot as a unit; the device-tree hog-only path had not been boot-tested without the driver
workaround at freeze time. The 2026-11-24 GP1 work then changed exactly that (section 3).

## 3. Why v1 must NOT be posted now (four concrete stale points)

All four are host-verifiable from the committed tree; none is a judgement call.

1. **The hardware-validated PCIe reset path changed after the freeze.** The running GP1/PD4 line
   (2026-11-24 onward) uses the committed patch
   `013a-arm-pci-luofu-endpoint-reset-pulse.patch` (sha256
   `d4b544a3...6386b`): at RC0 probe the driver drives SoC GPIO0 pins 20/21/28 low for 20 ms
   then high for 100 ms. The box does not run without it (the chip mask ROM never enters S0/S1
   and the link never trains). Review-form patch 0013 contains **no** board-GPIO code, so the
   frozen form does not describe the validated boot.
2. **DTS-0001 still carries the retired `wifi-pcie-hog`.** The current single-source board DTS
   (`target-skeleton/dts/luofu-r116.dts`, 702 lines, sha256 `df0d8e59...6557`) contains **no**
   `wifi-pcie-hog` and **no** `reset-gpios`; the hog was retired on 2026-11-24 because it
   released the endpoint lines before the driver's assert pulse and defeated it. The frozen
   DTS-0001 (sha256 `33956380...55fa`) still contains the hog (verified: one occurrence).
3. **L1 is missing 013a.** `release-line/INCLUDED.txt` has 55 entries and
   `release-line/manifest.tsv` has **0** occurrences of `013a` (verified this session). The
   release-line validator therefore validates a patch set that is not the booted line; it must
   grow to **56** and be re-run.
4. **The v1 form predates the 2026-11-24 target-repo fixes** (real boot-pair recorder enabled,
   bench-only duplicate retired, `boot-selector` sysfs path fixed, 013a committed, hog gone).
   The kernel series is not the target PR, but the review form and its commit messages no longer
   describe the tree the hardware record is attached to.

**Consequence:** the correct next artefact is the planned v2, regenerated from the current
target patches after the fold content changes are made - not a hand-edited v1.

## 4. The planned v2 series (30 kernel patches + DTS-0001)

Source: [../wifi-upstream-target/PATCH-CLASSIFICATION.md](../wifi-upstream-target/PATCH-CLASSIFICATION.md)
(full 67-patch verdict table) and [../wifi-upstream-target/submission-series.md](../wifi-upstream-target/submission-series.md)
(ordered form, commit-message drafts, acceptance criteria). This is the target form; it has
**not been generated yet**.

Classification of the 67 L0 files: **26 KEEP** (of which 7 are regenerated with folded hunks),
**17 FOLD** (no separate entry), **24 DROP** (bench-only diagnostics/probes/workarounds).

| # | subject | v2 source |
| --- | --- | --- |
| 0001 | ARM: hisilicon: add luofu machine support | v1 0001 |
| 0002 | ARM: add luofu TEXT_OFFSET 0x00608000 | v1 0002 |
| 0003 | ARM: head: materialise a non-immediate TEXT_OFFSET | v1 0003 |
| 0004 | clk: hisilicon: add luofu CRG clock/reset provider | v1 0004 |
| 0005 | watchdog: add luofu watchdog | v1 0005 |
| 0006 | mtd: spi-nand: add luofu FMC SPI-MEM controller | v1 0006 |
| 0007 | net: mdio: add luofu MDIO controller | v1 0007 |
| 0008 | net: phy: add Genius PHY driver | v1 0008 |
| 0009 | net: ethernet: add luofu GEMAC | 0009 + folds 038/041/051 |
| 0010 | power: reset: add luofu restart driver | v1 0010 |
| 0011 | pinctrl: add luofu pinctrl | v1 0011 |
| 0012 | thermal: add luofu tvsensor | 0012 + folds 066/067 |
| 0013 | PCI: dwc: add luofu PCIe host | 0013 + **folded 013a as DT `reset-gpios`** (rewritten) |
| 0014 | clk: hisilicon: luofu-crg: fix reset polarity | v1 0014 |
| 0015 | net: ethernet: add luofu PIE packet engine (RFC) | v1 0015 |
| 0016 | net: ethernet: add luofu internal switch probe (RFC) | v1 0016 |
| 0017 | soc: hisilicon: add luofu boot-selector ABI | v1 0017 |
| 0018 | ARM: hisilicon: add luofu SMP release | v1 0018 (only with DTS-0001) |
| 0019 | clk: hisilicon: luofu-crg: add clock tree | 0019 + fold 027 |
| 0020 | net: ethernet: luofu-pie: add ring engine (RFC) | 0020 + fold 048 |
| 0021 | net: ethernet: luofu-switch: add baseline probe (RFC) | v1 0021 |
| 0022 | net: ethernet: luofu-switch: add gated qid-map write path (RFC) | 0022 + fold 044 |
| 0023 | net: ethernet: luofu-gmac: add netdev readiness gate (RFC) | v1 0023 |
| 0024 | net: ethernet: luofu-pie: add RX completion decode (RFC) | 0024 + fold 053 |
| 0025 | net: ethernet: luofu-switch: add egress/probe-configuration stage (RFC) | v1 0025 |
| 0026 | net: ethernet: luofu-gmac: add bdinfo MAC nvmem source | v1 0026 |
| 0027 | net: ethernet: luofu-switch: program the EQM queue-manager tables (RFC) | new, from 032 (+031 group) |
| 0028 | net: ethernet: luofu-switch: initialise the PFE/IFC forwarding tables (RFC) | new, from 036 + 043 + 046 |
| 0029 | net: ethernet: luofu-switch: initialise the NAPT flow engine (RFC) | new, from 059 |
| 0030 | net: ethernet: luofu-pie: deliver native exception-class RX frames (RFC) | new, from 060 |
| DTS-0001 | ARM: dts: hisilicon: add luofu-r116 board | regenerate from the single source (section 5) |

The bench gates and hardcoded addresses in the RFC patches (PIE/LSW data path) must become DT
state or disappear; the review must not claim an Ethernet data path. Commit-message drafts for
0027-0030 exist in the v2 source document and already carry explicit "unverified" lists.

## 5. What must be regenerated (in this order)

1. **Content folds in the L0 target patches** (with the hardware evidence), not in the generated
   files: folds 027, 038, 041, 044, 048, 051, 053, 066, 067, plus the 013a rewrite into 0013
   as a `reset-gpios` DT property (the binding must describe it), plus folds 031/032/036/043/046/059/060
   into the four new RFC entries.
2. **DTS change:** add the endpoint `reset-gpios` (or a board-level reset-controller child) to
   the PCIe RC node, keep the hog retired. This is an **active-DTS change**: project rule = one
   attended boot before it can be called hardware-validated (see [DTS-0001-FINAL.md](DTS-0001-FINAL.md)).
3. **L1 release line:** add 013a to `release-line/INCLUDED.txt` and `manifest.tsv` (55 -> 56),
   re-run `tools/check-release-line.sh` against pristine `linux-6.18.60`.
4. **Regenerate L2 v2** from L0 with `tools/gen-series.py` (never hand-edit `series/`): 30
   patches + DTS-0001 from the filtered single-source DTS (`tools/gen-dts-series-patch.py`,
   `tools/dts-upstream-filter.py`).
5. **Validate:** sequential `patch(1)` and `git am` apply on pristine `linux-6.18.60` with
   identical trees, `checkpatch.pl`, `dt_binding_check` for every binding, dtc `W=1` for the
   DTS, full kernel compile with OpenWrt GCC 14.4.0, full image chain from the series,
   `gen-series.py --check`, regenerate `validation/sha256sums.txt` and
   `submission/MANIFEST.sha256`.
6. **Refresh the submission package** (`upstream/series-6.18/submission/`): the 2026-11-22
   v3 package (cover-email draft, blocked-items table, MANIFEST) is tied to v1 and must be
   regenerated with the v2 hashes and the final blocker wording from
   [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) and [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md).

## 6. Acceptance criteria (from the v2 plan) - current status

Status: unchecked unless stated; `[x]` = met by a recorded artefact today.

**Host (mechanical)**
- [ ] 30-patch series applies to pristine `linux-6.18.60` with `patch(1)` and `git am`, identical trees (v1 met this with 26/26);
- [ ] `checkpatch.pl` 0 errors (warnings triaged; 0 for new files) (v1: 0 errors / 73 warnings);
- [ ] full kernel compile RC=0 with the OpenWrt GCC 14.4.0 toolchain, `W=1` clean for touched files;
- [ ] `dt_binding_check` RC=0 for every binding; dtc `W=1` clean for the DTS (v1 PCIe binding RC=0);
- [ ] the target image chain builds host-side from exactly this series (target tree already builds in isolation, 2026-11-24);
- [ ] `gen-series.py --check` clean and validation sha256sums regenerated ([x] v1 form: clean; v2 not generated).

**Design (review)**
- [ ] no `luofu_*` module parameters / bench gates in the submission patches (gates become DT data or are removed);
- [ ] no debugfs/printk diagnostics left behind (the DROP list stays out);
- [ ] every compatible has a reviewed binding and a documented evidence grade;
- [ ] vendor-derived tables are described with their derivation method, not dumped;
- [ ] reset/clock/power claims limited to the hardware-verified subset, unverified items listed;
- [ ] MAINTAINERS entry and tree-placement rationale updated.

**Hardware ("do not send a claim you have not measured")**
- [ ] every active-DTS change boot-tested on the unit;
- [ ] platform group (0001-0014, 0017-0019) boots the unit: 2 CPUs, serial, NAND/UBI;
- [ ] PCIe/thermal/pinctrl/GPIO: the specific feature exercised, unverified paths listed;
- [ ] PIE/LSW RFC group: no traffic/support claim in the cover letter; the attending gate described as board bring-up tooling;
- [ ] SMP patch only together with DTS-0001 carrying the two loader hand-off nodes.

**External (not kernel)**
- [ ] the Wi-Fi answer (GPL source / closed blob / rewrite scope) stated as **out of scope** for this series;
- [ ] the target PR is separate and only after the install path ran end to end on a recovered unit;
- [ ] publication checklist (history purge / curated repos, licences, statements) separate from the review form.

## 7. Decisions the maintainer must make before generation

1. **013a board GPIO vs DT data.** Hardcoded GPIO0 pins are bench-acceptable but not upstream
   form. The fold into 0013 must carry the lines as DT data (`reset-gpios` on the RC node or a
   board-level reset-controller child) and the binding must describe it. This is the only
   content change that also needs an active-DTS boot test.
2. **PIE/LSW data-path claims.** 0020/0024/0027-0030 describe a data path only exercised through
   attended module parameters on the bench. Either submit the RFC group with explicit
   "unverified" lists, or hold them until the gates are replaced by DT state and a normal boot
   proves the path. **Do not claim Ethernet or Wi-Fi support in the cover letter.**
3. **DTS-0001 regeneration and SMP coupling.** DTS-0001 must be regenerated after the 013a fold
   (the current entry carries the retired hog) and 0018 must ship with it. If SMP is held back,
   DTS-0001 still carries the loader hand-off nodes and the platform boot; decide the split
   explicitly in the cover letter.
4. **SMP diagnostics are not the SMP fix.** Only 0018 + the two DTS loader hand-off nodes fix
   SMP; the probes (028/029/033/034/035/037/039/062-065) must never be sent.
5. **Wi-Fi scope.** The series carries no Wi-Fi driver and must say so. At most, the cover letter
   may point at the published research notebook for the Wi-Fi protocol result - with the
   [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) wording and no support claim.

## 8. Do-not-send list (explicit)

* the 24 DROP patches: bench SMP/PIE/LSW probes, diagnostics and CPU-copy mirror workarounds
  (028-030, 033-035, 037, 039, 040, 042, 045, 047, 049, 050, 052, 054-058, 062-065);
* bench gates, module parameters, attended-arm paths and `CONFIG_CMDLINE_FORCE` lab cmdlines;
* the PD4 patch **or any patched firmware**, any vendor blob, any calibration/EFUSE/ROM data,
  any extracted vendor DTS or vendor object code;
* the Wi-Fi clean-room driver/provider modules and the AP-start patch (research artifacts);
* the 2026-11-22 v1 `submission/` package and cover-email draft (stale - regenerate).

## 9. Go/no-go recommendation

**No-go for posting today; go for one host-only generation pass.** The platform series is the
right first submission and has no new external code blockers, but the artefact that exists (v1)
is not the hardware-validated line after 2026-11-24. Posting it would send a PCIe driver without
the reset pulse and a DTS with a retired node - exactly the kind of mismatch that gets an RFC
rejected. The v2 generation is a well-specified host-only task (section 5); after it and the one
attended boot for the `reset-gpios` change, the platform group can be posted with the Wi-Fi
exclusion statement. The target PR stays separate and gated on the install-path run.

**Go/no-go gates for the first posting:**
1. v2 review form generated + all host acceptance checks green + `MANIFEST.sha256` refreshed;
2. L1 at 56 patches and the release-line validator PASS on pristine `linux-6.18.60`;
3. one attended boot of the `reset-gpios` DTS change (recovery armed) with the 2-CPU/PCIe/NAND
   checks;
4. cover letter carries the Wi-Fi/PD4/GPL exclusion wording ([GPL-PD4-STATUS.md](GPL-PD4-STATUS.md));
5. no vendor bytes, no PD4, no research modules anywhere in the posted bundle.

## 10. Evidence

| claim | evidence |
| --- | --- |
| 67 L0 files; 26 keep / 17 fold / 24 drop; v2 = 30 + DTS-0001 | [../wifi-upstream-target/PATCH-CLASSIFICATION.md](../wifi-upstream-target/PATCH-CLASSIFICATION.md), [../wifi-upstream-target/submission-series.md](../wifi-upstream-target/submission-series.md) |
| v1 form frozen, 26 patches, validation record | [../../../upstream/series-6.18/README.md](../../../upstream/series-6.18/README.md), `04-validation.md`, `submission/README.md` |
| `gen-series.py --check` clean (26) this session | command in [README.md](README.md); output recorded in [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |
| 013a committed, sha256 `d4b544a3...` | `target-skeleton/target/linux/hisilicon/patches-6.18/013a-*.patch` |
| current DTS 702 lines `df0d8e59...`, no hog, no reset-gpios | `target-skeleton/dts/luofu-r116.dts`; sha256 re-run this session |
| DTS-0001 `33956380...` still contains the hog | `upstream/series-6.18/dts-series/0001-*.patch`, grep re-run this session |
| L1 = 55, no 013a | `release-line/INCLUDED.txt`, `manifest.tsv`, grep re-run this session |
| GP1 pulse is load-bearing on hardware | parent-repo ledger 2026-11-24 (GP1), `STATUS.md` 3c; [../wifi-upstream-target/REPORT.md](../wifi-upstream-target/REPORT.md) section 3.1 |
