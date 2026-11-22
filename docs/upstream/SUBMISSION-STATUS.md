# Upstream submission status - HiSilicon luofu / Cudy WR3000 v2.0 (board R116)
<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** 2026-11-22. **This page is the definitive submission checklist for the
upstream packaging lane** (task-65): per-item status, evidence pointers, remaining
work and the proposed `openwrt/openwrt` series split.  It is host-only packaging
state; no build was run and no device was touched to produce it.  The live state of
the box and the bench record remain the parent repository's `STATUS.md`,
`mem-entries.md` (newest dated entry wins) and `EVIDENCE-INDEX.md`.

**Source-of-truth artefacts.**  `opensource/target-skeleton/dts/luofu-r116.dts` is the
single DTS source; `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`
is the lab patch stack (L0); the generated review form and its validation live under
`opensource/upstream/series-6.18/`.  This page does not replace the detailed docs in
that directory; it reconciles them with the two new hardware proof points of
2026-11-22.

Evidence grades follow the project convention: **[V]** verified on hardware or from a
named artefact, **[I]** inferred, **[U]** unmeasured, **[X]** blocked/excluded.

---

## 1. Verdict (one screen)

- **Target stack:** the L0 patch stack (66 files) boots a 2-CPU OpenWrt 6.18.60
  baseline on this board and the board DTS content is hardware boot-tested up to the
  **2026-11-21** baselines (images 7421bb7a, f6685b80, c3ef2a50, 6e9c54d0).  The
  `target/linux/hisilicon` tree itself has **never been built/booted as an OpenWrt
  target image from this copy** - bench images were assembled by the slice toolchain.
- **Kernel review form:** 26 patches + the DTS series entry (DTS-0001) are a
  reviewable draft: last full host validation 2026-11-21 (26/26 apply, 0 errors /
  58 checkpatch warnings).  **Never posted upstream; no subsystem maintainer has
  seen it.**
- **NEW 2026-11-22 - PCIe bring-up is solved on the 6.18 kernel [V].**  The link
  failure was board GPIO state, not a missing PCIe register program: GPIO0 pins
  20/21/28 must be outputs driven HIGH before training.  With that fix the link
  trains (LTSSM L0) and **both Hi5622 endpoints enumerate** on our kernel.
- **NEW 2026-11-22 - Wi-Fi transport bring-up works [V].**  The clean-room
  `hi5622_pciet` transport module passes the stage-B1 gate: firmware upload
  928,920 B with **verify diffs=0**, CPU released (0x5a5a), D2H announce (0xcece),
  and **DEVICE READY (D2H message id 2, bitmap 0x4)**.  This is readiness evidence
  for a Wi-Fi driver series, **not** an upstream Wi-Fi driver.
- **Single biggest remaining blocker:** **no mainline Wi-Fi driver.**  The only
  working radio stack is the vendor 5.10 binary (closed firmware + vendor MAC); the
  GPLv2 source request is unanswered, and the clean-room work is a framework/
  transport port (23.9%-28.5% name/byte coverage) without an in-tree mac80211
  driver or data path.  A "complete OpenWrt submission" cannot be claimed without
  it.
- **Runner-up blockers:** the target image recipe/install path has never run an
  end-to-end OpenWrt image on hardware, and the generated submission artefacts
  (`series/`, `MANIFEST.sha256`, validation logs, DTS-0001) are **stale** after the
  2026-11-22 patch-013/DTS edits and must be regenerated before anything is posted.

---

## 2. Layer model (do not conflate)

| layer | what it is | count | this page |
| --- | --- | --- | --- |
| **L0** lab patch stack | `target-skeleton/target/linux/hisilicon/patches-6.18/`, applied in filename order by the OpenWrt target build | **66** (001-060 + 062-067, no duplicate numbers) | status per patch group in section 3 |
| **L1** release line | L0 minus the bench-only SMP diagnostic patches; the target stack a release/publish snapshot carries | **55** (`upstream/series-6.18/release-line/`) | target PR content in section 8.1 |
| **L2** upstream review form | kernel-only submission form | **26** (+ docs 00-07) | kernel PRs in section 8.2 |
| **L3** DTS series entry | board DTS + loader hand-off nodes, needed by kernel patch 0018; cannot ride inside the kernel-only form | **1** (`dts-series/0001-...`) | regenerated in section 7, posted with 0018 |
| target skeleton | `target/linux/hisilicon` + `dts/` + `tools/` + docs | tree | target PR in section 8.1 |

---

## 3. Checklist (definitive)

Status meanings: **DONE** = no work outside the already-tested artefact; **PARTIAL** =
content exists and is partly validated, work listed; **BLOCKED** = external dependency
or hardware campaign required, not an editing task.

| # | item | status | evidence | remaining work |
| --- | --- | --- | --- | --- |
| 1 | Board DTS content up to 2026-11-21 | **DONE** | boot-tested images 7421bb7a / f6685b80 / c3ef2a50 / 6e9c54d0, `07-dts-upstream-bindings.md` sections 6.1-6.3; last blocker list: zero | keep frozen; no further active change without a boot test |
| 2 | Board DTS 2026-11-22 additions (PCIe nodes enabled, GPIO hog, dma-ranges) | **PARTIAL** | host `cpp+dtc` RC=0, 0 diagnostics; hardware facts [V] (section 4); DTB 12,865 B, pad step 12,872 B | one regression boot of the rebuilt image; dma-ranges pending the stage-B2 bench verdict (section 4.4) |
| 3 | Loader hand-off DTS entry DTS-0001 | **PARTIAL** | `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`, sha256 `886f3ad0...`, 681-line payload; dependency record `06-dts-series-dependency.md` | regenerate from the 711-line DTS (payload and hashes stale), re-run the DTS contract check, boot-test with 0018 |
| 4 | Target skeleton tree (`target/linux/hisilicon`) | **PARTIAL** | files complete (Makefile, Kconfig stub, `luofu/{target.mk,config-6.18,config-5.10,profiles}`, `image/*`, `base-files/*`, patch stack); image/Makefile + luofu.mk host stages implemented | copy into an OpenWrt tree and build/boot the target image; decide the target-level Kconfig stub (delete vs move); remove bench-only files from the upstream target PR |
| 5 | Image recipe (uImage + FIP trailer + UBI rootfs + sysupgrade container) | **PARTIAL** | host stages implemented and byte-validated by `fip-slice.py`/`luofu-image.py`; an earlier build of this recipe booted to userspace 2026-11-19 (UP, 1 CPU); current layout never booted (`image/luofu.mk` header) | build current layout, attended boot, verify pad/gate/UBI/overlay; no factory/install stanza until an install path runs |
| 6 | Install / sysupgrade path | **BLOCKED** | only verified install is the manual stock-telnet broker path (`tools/stock-telnet-install.sh`); `platform_do_upgrade` never executed; A/B selector protocol beyond 0x10/0x11 [U] | run one end-to-end `platform_do_upgrade` with recovery armed; settle the selector protocol |
| 7 | Target kernel config `luofu/config-6.18` | **PARTIAL** | used by the bench images; `config-5.10` is a reference only | re-derive against the chosen OpenWrt release; drop bench diagnostics; document every non-default symbol |
| 8 | base-files / board.d / MAC provisioning | **PARTIAL** | bench images run board.d + luofu-netdev/mac; MACs from bdinfo nvmem is draft patch 0026; `heartbeat` and `luofu-pair-record` are bench-only | exclude bench-only scripts + service from the upstream target PR; settle the MAC source (nvmem vs userspace) |
| 9 | L2 review-form mechanics | **PARTIAL** | 2026-11-21 validation: 26/26 patch(1) + `git apply`, 0 errors / 58 warnings (`04-validation.md`); stale after the 2026-11-22 patch-013 edit | regenerate `series/`, messages, MANIFEST, logs; re-run checkpatch and tree-equivalence (section 7) |
| 10 | Core platform 0001-0003 (mach-hisilicon, TEXT_OFFSET) | **PARTIAL** | RFC group; 0002/0003 textofs patches | post first for arm-soc direction; decide whether the TEXT_OFFSET change should be a generic ARM improvement |
| 11 | CRG reset/clock 0004 + 0014 + 0019 | **PARTIAL** | ready-for-review class; resets/clocks exercised by bench images (PCIe/GPIO/LSW); 0019 read-only, decoded rates unmeasured | fold 0014 into 0004; post CRG first, 0019 as clock-tree follow-up after one read-only readback |
| 12 | Watchdog 0005 | **PARTIAL** | ready-for-review; bench: watchdog0 identity/timeout/sysfs verified (2026-11-21) | split binding; post |
| 13 | FMC/NAND 0006 | **PARTIAL** | RFC; storage (17 partitions, UBI, overlay) works in bench images | wait for the ESMT chip entry dependency; split binding; review naming (LUOFU_FMC vs SPI_LUOFU_FMC) |
| 14 | MDIO/PHY 0007/0008 | **PARTIAL** | ready-for-review; used by the bench LAN path | regenerate 0008 against mainline (currently -75/-12 offsets); split bindings; post |
| 15 | GMAC 0009 | **PARTIAL** | draft that refuses probe; netdev registration gated behind 0023 | needs the PIE/LSW attended campaign; do not post as working Ethernet |
| 16 | Pinctrl 0011 | **PARTIAL** | RFC; pinctrl block exercised by bench images | binding split + pin-state review |
| 17 | Thermal 0012 (+ release-line 0066/0067) | **PARTIAL** | 0012 ready-for-review; W23 rewrite 0066/0067 bench-proven [V] but release-line-only | upstream-clean the W23 content into 0012 (or a follow-up) and split the binding |
| 18 | SMP 0018 | **PARTIAL** | RFC; 2-CPU operation hardware-verified via DTS loader hand-off (DTS-only fix) | post as RFC together with DTS-0001; settle `hisilicon,luofu-smp` method naming |
| 19 | Boot selector 0017 | **PARTIAL** | RFC; 0x10/0x11 cells measured; 0x21 manual value is a bench handover; full sysupgrade protocol [U] | settle the sysupgrade/A-B story before posting |
| 20 | **PCIe host 013** | **PARTIAL** | **2026-11-22 [V]**: link trains, both endpoints enumerate; patch cleaned in commit 4e959f2; checkpatch 0 errors / 19 warnings | remove the hardcoded GPIO workaround once the DTS hog is boot-tested (section 4.1); replace/justify `acp-gate`; MSI/INTx still absent; static CFG0 single-function only; split binding; no endpoint driver upstream |
| 21 | PIE/GEMAC/LSW data path 0015/0016/0020-0026 | **BLOCKED** | drafts flagged "do not merge"; wired LAN works only through release-line gated patches (NAPT + CPU delivery); native L2/VLAN forwarding is the separate LSW project | attended arm/egress/decode campaigns; redesign the PIE/GEMAC split before netdev review |
| 22 | **Wi-Fi driver** | **BLOCKED** | vendor 5.10 binary only (no mac80211); GPL request unanswered; clean-room transport B1 works [V] (section 4.2) but there is no in-tree mac80211/cfg80211 driver or data path | GPL follow-up (source); framework port + datapath slices; until then mark Wi-Fi "[X] not supported" in any submission |
| 23 | DT bindings (all in-series YAMLs) | **PARTIAL** | in-series bindings exist; PCIe `acp-gate` now documented (commit 4e959f2), `num-lanes` covered by the dtschema pci-bus-common schema | split bindings out of driver patches; run `dt_binding_check`/dtbs_check (no dtschema in this checkout); add MAINTAINERS entries |
| 24 | MAINTAINERS + DCO sign-off | **BLOCKED** | all messages use a placeholder identity | owner must supply a real legal name for `Signed-off-by`, add the MAINTAINERS entry as its own patch |
| 25 | Generated submission artefacts | **PARTIAL** (stale) | `gen-series.py --check` fails on `series/0013`; manifest/sha sums/validation pre-date the 2026-11-22 edits | regenerate everything listed in section 7 before posting |
| 26 | SPDX / licensing / provenance | **DONE** | 37 new kernel files: 36 carry GPL-2.0 SPDX; the one exception is the ABI sysfs doc (patch 017), where kernel practice omits SPDX (checkpatch's SPDX rule does not apply to that file type); all target-tree sources now carry SPDX (this pass added it to the two bench init scripts); no vendor binary/object/DTB/calibration data in the stack | keep it that way; publication history purge is tracked separately (item 27) |
| 27 | Publish-readiness / history purge | **BLOCKED** | `opensource/docs/PUBLISH-READINESS.md` open: pre-policy vendor `.o` objects in history, GPL reply, DCO/licence files | separate track; do not present the notebook as a submission-ready public repo |

**Checklist counts (27 items): 2 DONE, 20 PARTIAL, 5 BLOCKED.**

---

## 4. New hardware proof points (2026-11-22) and what they changed

All evidence below is host-side reading of the bench record and the committed
artefacts; the bench work itself was done by the hardware lanes (see parent
`mem-entries.md` 2026-11-22 17:05-18:20).

### 4.1 PCIe link trains - root cause was board GPIO state [V]

- **Symptom:** our 6.18 image ran the complete recorded vendor RC register
  sequence, every readable register matched the stock vendor's live state, and the
  link still never left detect.quiet (LTSSM 0x00, config reads 0xffffffff).
- **Root cause:** board GPIO state.  The stock vendor kernel leaves GPIO0 at
  **DDR=0x1330000f / DATA=0x12300004** (pins 20, 21, 28 are outputs driven HIGH).
  Our image left 20/21/28 as inputs because the board DTS had no node for them.
  Runtime bisect: with all three outputs+high the link trains (LTSSM 0x11 L0,
  DEBUG0=0x03d15111, config read cfg[0]=0x000559e7); any of the three low drops the
  link; restoring recovers it.  Stock live target: DEBUG0 dbi+0x728 = 0x03f4a611
  (L0), cfg window 0x50000000 = 0x59e7, ACP 0x10100174 = 0x06210321.
- **Fix:** `host_init` in patch 013 (committed f835732) drives the three pins; the
  submission form is the DTS gpio-hog added in commit 32d44e6
  (`wifi-pcie-hog` on `&porta`, output-high).  The hardcoded driver workaround is
  explicitly labelled bring-up-only and must be deleted after one regression boot
  proves the hog alone.
- **Endpoint enumeration [V]:** both Hi5622 endpoints are visible/probed on the 6.18
  link: `0000:00:00.0` BAR0 0x40000000 / BAR2 0x41800000 and `0001:00:00.0` BAR0
  0x58000000 / BAR2 0x59800000, vendor/device 59e7:0005, class 0x02800000, rev 0xe5
  (transport lane probe; parent ledger 2026-11-22 17:45).
- **Reconciliation note:** `docs/incoming/pcie-seq/fix-list-patch-013.md` predates
  this breakthrough; its bench-procedure step 3(a) ("the vendor boot proves no GPIO
  is needed on this board") and patch 013's old "no GPIO is claimed" TODO are
  **superseded** by the GPIO0 20/21/28 result above.  The rest of the fix list
  (misc write discipline, gate/steady values, iATU, CFG0 accessor, delays) matches
  the bench-proven program.
- **PCIe submission caveats still open:** the hardcoded GPIO workaround (until the
  hog boots), the `acp-gate` register poke (review item: should become an ACP
  power/clock domain, not a pcie driver property), no MSI/INTx, static CFG0
  viewport for the single-function topology, and no mainline endpoint driver (the
  Wi-Fi driver series is the consumer; the DT nodes being enabled is fine once the
  driver exists, see section 8).

### 4.2 Wi-Fi transport brings the chip up [V] - readiness, not a driver

The clean-room PCIe transport module (`docs/incoming/wifi-transport-port/build/`)
passes the **stage-B1 gate** on the working 6.18 link (parent ledger 2026-11-22
18:20):

1. `prepare`: both endpoints bound, six **endpoint** iATU viewports programmed and
   read back OK, windows=1;
2. `upload`: 928,920 B firmware copied to BAR0+0x6f8000 in chunks, no stall;
3. `verify`: byte-for-byte read-back of the target window, **diffs=0**;
4. `release`: 0x5a5a -> CA 0x40000108, cpu_released=1;
5. `announce`: 0xcece -> CA 0x4000010c, gate_announced=1;
6. `poll`: D2H bitmap 0x00000004 -> **"DEVICE READY (D2H message id 2)"**;
   status words match the stock vendor run (efuse_chip_id 0x186, dcoldo_vset
   0x260d4184, dcoldo_efuse 0x8108, pbank_code 0x312, chip CPU executing).
   Observation: ready1 (D2H id 1 / plat-ready) was not seen in the poll window;
   record as an observation, not a failed checkpoint.

**What this means for the submission:** it proves the transport contract is
implemented by original GPL-2.0 code on the 6.18 tree and that the hardware hand-off
(firmware -> CPU release -> D2H ready) works.  It does **not** make a Wi-Fi driver:
the module is a bench instrument with sysfs single-step actions and a polled D2H
scan, not a cfg80211/mac80211 driver; the hmac/wal data path (the majority of the
vendor delta) is not ported.  Do not present it as Wi-Fi support.

### 4.3 Endpoint iATU is the compact 7-word layout [V]

The Hi5622 endpoint's own inbound iATU (behind BAR2, the six firmware regions) is
**not** the DWC 8-word layout: per viewport `{ctrl2@+0x00, base_lo@+0x04,
base_hi@+0x08, limit_lo@+0x0c, target_lo@+0x10, target_hi@+0x14}` - **no limit_hi
word** - and **base/limit are absolute PCI bus addresses** (BAR0 resource start +
region offset), not BAR-local offsets.  Programming them with the 8-word/relative
assumption was the reason the firmware window stayed undecoded during early
transport bring-up; with the compact/absolute form the window decodes and the
firmware verify passes (4.2).  Recorded in `docs/phase18/inbound-map.md`
(Part A, membar/rev==0 path) and now cited in the patch-013 driver header, because
the RC driver does not own this register file - the Wi-Fi driver does.

### 4.4 Changes made to the submission artefacts for these facts (this pass)

| commit | content |
| --- | --- |
| `5a6a500` | board DTS: identity `dma-ranges` on both PCIe nodes (content from the lead, wifi-transport stage-B2 prerequisite). **Provisional:** the B2 bench A/B verdict is open; this hunk is a separate revertable commit and the window may need adjustment. |
| `4e959f2` | patch 013: hardware-verified GPIO notes + stock GPIO0 values, hardware-verified status replacing "never run", compact 7-word/absolute endpoint-iATU note, dma-ranges inbound note, corrected start_link comment, documented `acp-gate` binding property, minor checkpatch cleanups. 0 errors / 19 warnings. |
| `32d44e6` | board DTS: `wifi-pcie-hog` on `&porta` pins 20/21/28 output-high; remove the two obsolete `wifi@0,0` stub children; refresh the stale header; add `dt-bindings/gpio/gpio.h`. |
| (this file) | checklist + series split; ROADMAP proof-point update; SPDX headers on the two bench-only init scripts. |

No device code was changed beyond documentation/fix comments: the driver register
program stays exactly the bench-proven one.

---

## 5. DTS review notes for this pass

- **Host compile check (no build):** `cpp -nostdinc -D__DTS__` + `dtc` RC=0 with
  **zero diagnostics**, including the warning sets used before
  (`unit_address_vs_reg`, `simple_bus_reg`, `avoid_unnecessary_addr_size`,
  `unique_unit_address_if_enabled`).  Compiled DTB **12,865 B**; the stock bootfip
  gate needs an 8-byte-aligned totalsize, so `Build/luofu-pad-dtb` folds it to
  **12,872 B** (verified with the checked-in `tools/pad-dtb.py`).
- **No dt-schema tooling in this checkout** (no `dtschema`, `jsonschema`,
  `yamllint`), so `make dtbs_check`/`dt_binding_check` could not be run - only
  manual review against the submission-time mainline bindings (fetched from
  `torvalds/linux`) and the dtschema-bundled PCI schemas:
  - `pci-host-bridge.yaml` lives in the **dtschema package**, not the kernel tree;
    the patch-013 `$ref` is valid.
  - `num-lanes` was already covered by `pci-bus-common.yaml` (dtschema) - no
    binding change was needed.
  - PCIe `acp-gate` was an undocumented property under
    `unevaluatedProperties: false`; it is now documented (commit 4e959f2), with the
    review caveat that it is a bring-up workaround.
  - the GPIO hog child form matches mainline `snps,dw-apb-gpio.yaml`
    (`^.+-hog(-[0-9]+)?$` child pattern).  **That pattern exists in current
    mainline but not in the v6.18 tag** (the v6.18 binding keeps
    `additionalProperties: false` on port nodes; the hog-support series is from
    2026-05).  Validate against submission-time mainline; if the DTS is ever
    validated against v6.18 bindings a warning is expected.
- **Deliberately not changed** (from `07-dts-upstream-bindings.md` section 3, 14
  deferred active deltas): node renames, `status = "okay"` removals, std
  macro renderings, sp804/WOE/Wi-Fi/thermal removals, model/compatible changes,
  switch unit address/reg order etc.  Each needs its own boot-tested patch; do not
  ride them along with prose cleanups.
- **Single-source caveat for the DTS submission:** the skeleton DTS is also the
  release-line DTS, and it carries release-line-only content (W23 thermal
  properties incl. the bench helper `hisilicon,threshold-mc`).  DTS-0001 is
  generated verbatim from this file, so the regeneration in section 7 will drag the
  thermal delta into the upstream DTS unless the generator grows a release-line
  filter.  Decide before posting: either (a) strip release-line-only properties
  when generating DTS-0001, or (b) keep them and justify them to the thermal
  maintainers.  Recommendation: (a).

---

## 6. SPDX / licensing / provenance audit (2026-11-22)

- **New kernel files in the patch stack:** 37 new-file hunks; **36** carry a
  `SPDX-License-Identifier` in their first lines.  The single exception is
  `Documentation/ABI/testing/sysfs-driver-luofu-selector` (patch 017); mainline ABI
  test docs carry no SPDX and checkpatch's SPDX rule does not apply to that file
  type.  **No action needed, documented as intentional.**
- **Target tree:** all Makefile/Kconfig/script/python/DTS/overlay/ubinize files
  carry GPL-2.0 SPDX after this pass; the two bench-only init scripts
  (`base-files/etc/init.d/heartbeat`, `luofu-pair-record`) were the only
  exceptions and are now fixed.
- **Review form files:** per-patch commit messages are generated; bindings are
  dual-licensed GPL-2.0-only OR BSD-2-Clause as required for DT.  Patches
  themselves are not SPDX-tagged (normal for mail-form patches).
- **No vendor artefacts/secrets:** the stack contains no vendor `.o`/`.ko`,
  firmware blob, extracted DTB or calibration data; the only numeric identifiers
  are SoC/PCI IDs (0x59e7:0x0005) and GPIO register values.  No MAC/serial values
  or credentials were added (per-unit values stay in gitignored `.omo-secrets/`).
- **Publication note:** the notebook's pre-policy history is not clean
  (`PUBLISH-READINESS.md`); that is a separate track and does not block the code
  submission, but it must be resolved before calling any repo "publicly ready".

---

## 7. Stale generated artefacts - must regenerate before posting

`python3 upstream/series-6.18/tools/gen-series.py --check` currently fails on
**only** `series/0013-arm-pci-luofu-host.patch`; everything else in the review form
is byte-current.  The DTS-side entry is stale because the board DTS changed.

| artefact | recorded state | current state |
| --- | --- | --- |
| `series/0013-arm-pci-luofu-host.patch` | sha256 `8a8454f7...` (also in `submission/MANIFEST.sha256` and `validation/sha256sums.txt`) | source patch sha256 `2a43c953...` (856 lines), 2026-11-22 |
| `release-line/manifest.tsv` row 013 | sha256 `73c413dd...` (manifest sha256 `2bb7413e...`) | source patch sha256 `2a43c953...` |
| `commit-messages/0013-arm-pci-luofu-host.txt` / `01-commit-messages.md` | still says "never run on hardware", "Wi-Fi nodes stay disabled", "iATU-enable/LTSSM ... never validated" | now false: link trains, endpoints enumerate, nodes enabled; needs a rewritten message before `gen-series.py` |
| `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` | sha256 `886f3ad0...`, 681-line DTS payload | source DTS 711 lines, sha256 `f24451ca...` (includes PCIe/hog/dma-ranges + thermal delta) |
| `validation/*` (checkpatch, apply, git-am, sha256sums, release-line logs) | 2026-11-21 runs | pre-date commits 5a6a500/4e959f2/32d44e6 |
| `06-dts-series-dependency.md`, `07-dts-upstream-bindings.md` | hashes/line counts for the old DTS | must be refreshed after DTS-0001 regen |
| `04-validation.md` checkpatch table | 0 errors / 58 warnings, `git apply` 26/26 | must be re-run for the new patch 013 (0/19 on the patch alone) |

**Regeneration commands** (from `upstream/series-6.18/README.md` / `07-dts-upstream-bindings.md`; run in a host workspace with an extracted linux-6.18.60 tree):

```sh
# 1. rewrite commit-messages/0013-arm-pci-luofu-host.txt for the hardware-proven state
# 2. regenerate the review form + index + run the validation harness
python3 tools/gen-series.py
python3 tools/gen-series.py --check
tools/run-validation.sh            # checkpatch + apply + tree equivalence, refreshes validation/
# 3. regenerate the DTS series entry from the single-source DTS, then re-run the
#    release-line validator (applies/compiles DTS-0001, checks the loader contract
#    and exercises the pad step) and refresh validation/sha256sums.txt
python3 tools/gen-dts-series-patch.py \
    --pristine <extracted linux-6.18.60> \
    --target-dts ../../target-skeleton/dts/luofu-r116.dts \
    --out dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch
tools/check-release-line.sh --use-tree <extracted linux-6.18.60> <workdir>
```

Until these run, the submission is **not postable** even though the underlying
content is now hardware-backed.

---

## 8. Proposed submission split

The consolidated 66/55/26-patch stacks are **review material, not a posting plan**.
Proposed split, target+DTS first and drivers second:

### 8.1 `openwrt/openwrt` - target PRs (DTS + skeleton first)

**PR-1 "add HiSilicon luofu target + Cudy WR3000 v2.0 board DTS"** (the target+DTS
PR; reviewable only after the kernel-side dependencies in 8.3 have a home):

- `target/linux/hisilicon/`: Makefile, subtarget `luofu/` (target.mk,
  `config-6.18`, profiles cleanup, delete the schematic `Kconfig` stub or move it
  to the tree's own location), `image/` (Makefile, luofu.mk, ubinize cfg),
  `base-files/` (**bench-only `heartbeat` and `luofu-pair-record` removed**;
  board.d/netdev/mac helpers kept only where they are part of the real board
  support), `dts/luofu-r116.dts` + overlay.
- Kernel patch stack subset: **minimal boot plumbing only** - 001 mach, 002/003
  TEXT_OFFSET, 004 CRG (+014 folded), 005 watchdog, 010 restart, 011 pinctrl,
  018 SMP, 019 clock tree (if accepted with 004).  No networking/PCIe/thermal
  drivers in PR-1: the target boots to userspace, no more.
- Dependencies/notes: the DTS must be the same content posted as DTS-0001 (with the
  loader hand-off nodes), the base-DTB pad requirement must be stated in the PR
  description, and the image recipe must carry an honest "no install path yet"
  marker (no factory stanza).

**PR-2..N "add driver support to the luofu target"** (follow-up PRs; each added
after its kernel-side series is accepted, or at least posted and ACKed):

- PR-2 storage: FMC/NAND 006 (+ config, board partition map already in DTS).
- PR-3 wired network: MDIO/PHY/GMAC 007/008/009 first for a single-port wired
  target, then the PIE/LSW data-path series 015/016/020-026 as separate PRs once
  their kernel side is mergeable.
- PR-4 thermal: 0012 (+ W23 content 0066/0067 upstream-cleaned).
- PR-5 PCIe: 013 + the PCIe devicetree enablement (if not already in the base DTS).
- PR-6 boot selector / A/B install: 017 (only after the sysupgrade protocol is
  settled).
- Wi-Fi: no PR until a kernel driver exists (section 4.2: transport is not a
  driver).  Keep the board page's Wi-Fi claim at "[X] not supported".

**Target-PR prerequisites that are real gates:** item 4 (tree builds/boots as an
OpenWrt target), item 5 (current image layout boot), item 6 (install path), section 7
(regenerate artefacts for the kernel subset it carries).

### 8.2 Kernel side (`linux`, summary of `00-cover-letter.md` sequence)

1. **RFC for direction, arm-soc + Russell King:** 0001-0003.
2. **Ready-for-review, one subsystem per series:** 0004 (+0014 folded, 0019 as
   immediate clock-tree follow-up) -> clk/reset; 0005 -> watchdog; 0007 -> netdev
   MDIO; 0008 -> netdev PHY; 0012 -> thermal.  Bindings split out one patch per
   driver, before its driver patch; MAINTAINERS entry its own patch.
3. **RFC with open-work statement:** 0011 pinctrl, 0006 FMC (after the chip entry),
   **0013 PCIe** (now hardware-backed; note the remaining items in section 3 row 20),
   0010 restart + 0017 selector (needs the sysupgrade story).
4. **Do not send yet:** 0009 GMAC, 0015 PIE, 0016 LSW, 0020-0026 follow-ups -
   refusing/partial drafts; see `00-cover-letter.md` for the exact blocking campaign
   of each.
5. **SMP 0018 as RFC together with DTS-0001** (loader hand-off contract; bench-
   verified 2 CPUs).  Do not post the SMP bench diagnostics (062-065) as the fix.
6. **After the bindings are accepted:** the board DTS to
   `arch/arm/boot/dts/hisilicon/`, then the OpenWrt target PRs (8.1).

### 8.3 Ordering / dependency graph (text form)

```
L2 kernel:
  0001..0003 RFC -> arm-soc direction
  0004(+0014) CRG -> { 0005 wdt, 0007 mdio, 0011 pinctrl, 0012 thermal, 0018 smp, 0013 pcie,
                      0019 clock-tree, net drivers }
  DTS-0001 (board DTS + loader hand-off) <-> 0018 (SMP)  [post together]
  0013 PCIe <-> board DTS pcie nodes <-> (future) Wi-Fi driver
L1/target:
  001..019 minimal stack -> PR-1 target+DTS
  006/007/008/009/012/013/015/016/017/020..026 -> PR-2..N follow-ups
Wi-Fi:
  GPL reply (vendor source) and/or clean-room mac80211 driver -> its own series;
  the transport module is readiness evidence only.
```

---

## 9. Owner decisions still required (carried from 00-cover-letter / README)

1. Real legal name for `Signed-off-by` (currently a placeholder identity in every
   message, binding and MAINTAINERS proposal).
2. Approve the MAINTAINERS entry and post it as its own patch.
3. Split bindings out of the driver patches (mechanical, needed for review).
4. Fold 0014 into 0004; decide 0019 folding or follow-up; regenerate 0008 against
   mainline; rename `LUOFU_FMC` -> `SPI_LUOFU_FMC`.
5. Fix the checkpatch review items listed in `04-validation.md` (line wrap, help
   text, spinlock comments) when touching the patches for the split.
6. Approve posting 0018 as RFC with DTS-0001 (or together) - the DTS entry is the
   dependency.
7. Approve 0019 staging (read-only clock tree; decoded values need one read-only
   readback).
8. Approve or defer PIE/LSW staging (0020/0021, 0022/0023, 0024/0025/0026) - all
   currently "do not send".
9. **New (2026-11-22):** approve the PCIe submission plan - keep `acp-gate` as a
   documented bring-up property vs. move it to an ACP driver; and the DTS hog
   regression boot before deleting the hardcoded driver workaround.
10. **New:** decide the DTS single-source handling of release-line-only thermal
    properties (section 5) before DTS-0001 is regenerated.
11. **New:** decide how the provisional identity `dma-ranges` (commit 5a6a500) is
    carried if the stage-B2 verdict changes it.

---

## 10. Next actions, in dependency order

1. **Boot-test the 2026-11-22 DTS** (rebuilt target image): link trains, both
   endpoints enumerate, hog alone drives 20/21/28, no regression in MTD/LED/keys.
   Then delete the hardcoded GPIO block from patch 013 (bug/cleanup), rerun the
   checks.  *(hardware lane, attended)*
2. **Regenerate the submission artefacts** (section 7), including the 0013 commit
   message rewrite; re-run checkpatch/apply/tree-equivalence.
3. **Regenerate DTS-0001** from the single-source DTS (with the thermal-delta
   decision) and re-run the release-line validator.
4. **Rewrite the L2 0013 commit message / cover-letter paragraph** to the
   hardware-proven state (link trains, endpoints enumerate, GPIO quirk, compact
   endpoint iATU).
5. **Build the target image from the skeleton tree inside OpenWrt** (item 4/5) and
   boot it attended; record the recipe result.
6. **Run the install/sysupgrade path once** with recovery armed (item 6).
7. **Start the kernel posting sequence** (8.2) as soon as 1-4 are done; the PCIe
   RFC can go as soon as the hog-boot question is settled.
8. Continue the Wi-Fi clean-room work in parallel (framework/datapath slices; GPL
   follow-up) - it is the critical path for a *complete* submission, not a packaging
   item.

---

## 11. Evidence pointer index

| topic | pointer |
| --- | --- |
| PCIe GPIO root cause, bisect, W25 images | parent `mem-entries.md` 2026-11-22 17:05 / 17:20; parent `STATUS.md`; build/ evidence (gitignored) |
| PCIe register state vs vendor live | parent `mem-entries.md` 2026-11-22 12:45 / 13:20; `docs/incoming/pcie-seq/*` |
| endpoint iATU map (compact/absolute) | `docs/phase18/inbound-map.md` Part A; `docs/incoming/wifi-transport-port/REPORT.md`; ledger 2026-11-22 17:45 / 18:20 |
| Wi-Fi transport B1 gate | parent `mem-entries.md` 2026-11-22 18:20; `docs/incoming/wifi-transport-port/{REPORT,SUMMARY,BENCH-PLAN}.md`, `build/` |
| Wi-Fi framework census/port | `docs/incoming/wifi-framework-port/`, `docs/incoming/wifi-*/REPORT.md`, ledger 2026-11-22 07:20-14:45 |
| board DTS history, bindings review, boot tests | `upstream/series-6.18/07-dts-upstream-bindings.md`; images 7421bb7a / f6685b80 / c3ef2a50 / 6e9c54d0 |
| release line, L0/L1 classification | `upstream/series-6.18/05-release-line.md`, `release-line/manifest.tsv` |
| review form + validation | `upstream/series-6.18/{README,00-cover-letter,01-commit-messages,04-validation}.md`, `series/`, `validation/` |
| DTS series dependency | `upstream/series-6.18/06-dts-series-dependency.md`, `dts-series/0001-...` |
| target skeleton status | `target-skeleton/README.md`, `target-skeleton/RELEASE-NOTES.md`, `target-skeleton/target/linux/hisilicon/*` |
| design/roadmap | `docs/TARGET-DESIGN.md`, `docs/ROADMAP-OPENWRT.md`, `docs/REMAINING-DRIVERS.md` |

---

## 12. Change log of this pass (2026-11-22, submodule commits)

| commit | file(s) | what |
| --- | --- | --- |
| `5a6a500` | `target-skeleton/dts/luofu-r116.dts` | identity PCIe `dma-ranges` (lead content, provisional; separable/revertable) |
| `4e959f2` | `target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch` | hardware-verified GPIO/status comments, compact endpoint-iATU note, `acp-gate` binding property, checkpatch cleanups |
| `32d44e6` | `target-skeleton/dts/luofu-r116.dts` | `wifi-pcie-hog` (GPIO0 20/21/28 output-high), remove obsolete PCIe stub children, refreshed header, GPIO binding include |
| same pass | `target-skeleton/target/linux/hisilicon/base-files/etc/init.d/{heartbeat,luofu-pair-record}` | GPL-2.0 SPDX headers |
| same pass | `docs/upstream/SUBMISSION-STATUS.md` (this file) | definitive checklist + series split |
| same pass | `docs/ROADMAP-OPENWRT.md`, `target-skeleton/RELEASE-NOTES.md` | 2026-11-22 proof-point updates |

Everything above is host-only: **no build was run, no device was touched, and no
vendor artefact/secret was added.**
