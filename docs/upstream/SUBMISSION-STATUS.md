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
  reviewable draft: final host validation 2026-11-22 (26/26 apply, identical trees,
  0 errors / **73** checkpatch warnings; patch 0013 alone 0/17; PCIe binding
  `dt_binding_check` RC=0).  **Never posted upstream; no subsystem maintainer has
  seen it.**
- **NEW 2026-11-22 - PCIe bring-up is solved on the 6.18 kernel [V].**  The link
  failure was board GPIO state, not a missing PCIe register program: GPIO0 pins
  20/21/28 must be outputs driven HIGH before training.  Bench images W25d/W26a
  train the link (LTSSM L0) and **both Hi5622 endpoints enumerate**; **W27b**
  (image sha256 `4af1d1fa...`) then proved the DTS `wifi-pcie-hog` alone drives
  the pins - patch 013 contains **no board-GPIO code** (workaround removed, commit
  `ad711d1`).
- **NEW 2026-11-22 - Wi-Fi transport bring-up works [V].**  The clean-room
  `hi5622_pciet` transport module passes the stage-B1 gate: firmware upload
  928,920 B with **verify diffs=0**, CPU released (0x5a5a), D2H announce (0xcece),
  and **DEVICE READY (D2H message id 2, bitmap 0x4)**.  **Stage B2 also passed**
  (image W26a): the RC inbound window is programmed from `dma-ranges`, the ETE
  rings are set up and the H2D smoke doorbell completes cleanly.  This is strong
  readiness evidence for a Wi-Fi driver series, **not** an upstream Wi-Fi driver.
- **Single biggest remaining blocker:** **no mainline Wi-Fi driver.**  The only
  working radio stack is the vendor 5.10 binary (closed firmware + vendor MAC); the
  GPLv2 source request is unanswered, and the clean-room work is a framework/
  transport port (23.9%-28.5% name/byte coverage) without an in-tree mac80211
  driver or data path.  A "complete OpenWrt submission" cannot be claimed without
  it.
- **Runner-up blockers:** the target image recipe/install path has never run an
  end-to-end OpenWrt image on hardware.  The generated submission artefacts are no
  longer stale: `series/`, `MANIFEST.sha256`, the DTS series entry and all
  validation logs were regenerated against the final patch 013 and **PASS**
  (`gen-series.py --check` clean, release-line validator PASS, 0 errors).
  Remaining pre-post work is in section 7.

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
| 2 | Board DTS 2026-11-22 additions (PCIe nodes enabled, GPIO hog, dma-ranges) | **PARTIAL** | host `cpp+dtc` RC=0, 0 diagnostics; hardware facts [V] (section 4); dma-ranges now hardware-validated by the B2 pass on image W26a (section 4.2); DTB 12,865 B, pad step 12,872 B | **hog-only regression boot DONE 2026-11-22 [V]**: image W27b (sha256 4af1d1fa..., rebuilt from this DTS + patch 013 WITHOUT the driver workaround) booted with link up on both RCs, both endpoints enumerated, GPIO0 DDR/DATA = 0x1330000f (the hog alone drove pins 20/21/28); window sizing may still be refined by the Wi-Fi driver |
| 3 | Loader hand-off DTS entry DTS-0001 | **DONE** | `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`, sha256 `33956380...`, 712 lines (699-line payload = single-source DTS after the release-line W23 filter); applies + compiles (DTB 12,697 B `288a5c3f...`) and is byte-identical to the filtered target; the DTS content is boot-tested (2026-11-21 baselines + W27b for the 2026-11-22 additions) | post with kernel patch 0018; no further regeneration needed unless the board DTS changes |
| 4 | Target skeleton tree (`target/linux/hisilicon`) | **PARTIAL** | files complete (Makefile, Kconfig stub, `luofu/{target.mk,config-6.18,config-5.10,profiles}`, `image/*`, `base-files/*`, patch stack); image/Makefile + luofu.mk host stages implemented | copy into an OpenWrt tree and build/boot the target image; decide the target-level Kconfig stub (delete vs move); remove bench-only files from the upstream target PR |
| 5 | Image recipe (uImage + FIP trailer + UBI rootfs + sysupgrade container) | **PARTIAL** | host stages implemented and byte-validated by `fip-slice.py`/`luofu-image.py`; an earlier build of this recipe booted to userspace 2026-11-19 (UP, 1 CPU); current layout never booted (`image/luofu.mk` header) | build current layout, attended boot, verify pad/gate/UBI/overlay; no factory/install stanza until an install path runs |
| 6 | Install / sysupgrade path | **BLOCKED** | only verified install is the manual stock-telnet broker path (`tools/stock-telnet-install.sh`); `platform_do_upgrade` never executed; A/B selector protocol beyond 0x10/0x11 [U] | run one end-to-end `platform_do_upgrade` with recovery armed; settle the selector protocol |
| 7 | Target kernel config `luofu/config-6.18` | **PARTIAL** | used by the bench images; `config-5.10` is a reference only | re-derive against the chosen OpenWrt release; drop bench diagnostics; document every non-default symbol |
| 8 | base-files / board.d / MAC provisioning | **PARTIAL** | bench images run board.d + luofu-netdev/mac; MACs from bdinfo nvmem is draft patch 0026; `heartbeat` and `luofu-pair-record` are bench-only | exclude bench-only scripts + service from the upstream target PR; settle the MAC source (nvmem vs userspace) |
| 9 | L2 review-form mechanics | **DONE** | final 2026-11-22 re-run: 26/26 patch(1)+git am, identical trees, 3 offset hunks, 0 fuzz/0 rejects, **0 errors / 73 warnings** (patch 0013: 0/17), `gen-series.py --check` clean (`04-validation.md` section 32) | keep the form in sync whenever a source patch/message changes (documented commands) |
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
| 20 | **PCIe host 013** | **PARTIAL** | **2026-11-22 [V]**: link trains, both endpoints enumerate (W25d/W26a); **W27b (image sha256 `4af1d1fa...`) proves the DTS `wifi-pcie-hog` alone drives GPIO0 20/21/28, so the board-GPIO workaround was removed (commit `ad711d1`)**; final patch `beedd07b...` restored the submission notes (comments/binding only, code identical to ad711d1 after comment strip); checkpatch 0 errors / 17 warnings | replace/justify `acp-gate` (review item); MSI/INTx still absent; static CFG0 single-function only; split binding; no endpoint driver upstream |
| 21 | PIE/GEMAC/LSW data path 0015/0016/0020-0026 | **BLOCKED** | drafts flagged "do not merge"; wired LAN works only through release-line gated patches (NAPT + CPU delivery); native L2/VLAN forwarding is the separate LSW project | attended arm/egress/decode campaigns; redesign the PIE/GEMAC split before netdev review |
| 22 | **Wi-Fi driver** | **BLOCKED** | vendor 5.10 binary only (no mac80211); GPL request unanswered; clean-room transport **B1+B2** works [V] (section 4.2) but there is no in-tree mac80211/cfg80211 driver or data path | GPL follow-up (source); framework port + datapath slices; until then mark Wi-Fi "[X] not supported" in any submission |
| 23 | DT bindings (all in-series YAMLs) | **PARTIAL** | in-series bindings exist; PCIe `acp-gate` documented and `num-lanes` covered by dtschema pci-bus-common; **PCIe `dt_binding_check` RC=0** (dtschema 2026.9, `validation/dt-binding-check.log`); board-DTS `dtbs_check` still not run | split bindings out of driver patches; run full `dtbs_check` against submission-time mainline (the `wifi-pcie-hog` needs the post-v6.18 `snps,dw-apb-gpio` hog pattern); add MAINTAINERS entries |
| 24 | MAINTAINERS + DCO sign-off | **BLOCKED** | all messages use a placeholder identity | owner must supply a real legal name for `Signed-off-by`, add the MAINTAINERS entry as its own patch |
| 25 | Generated submission artefacts | **DONE** | regenerated against final patch 013 (2026-11-22): `series/0013` `45c47afd...`, release-line manifest `98931166...`, `validation/sha256sums.txt` 93/93 OK, `submission/MANIFEST.sha256` 105 entries OK, DTS-0001 unchanged `33956380...`; `gen-series.py --check` clean | re-run the documented commands if a source patch/message/DTS changes |
| 26 | SPDX / licensing / provenance | **DONE** | 37 new kernel files: 36 carry GPL-2.0 SPDX; the one exception is the ABI sysfs doc (patch 017), where kernel practice omits SPDX (checkpatch's SPDX rule does not apply to that file type); all target-tree sources now carry SPDX (this pass added it to the two bench init scripts); no vendor binary/object/DTB/calibration data in the stack | keep it that way; publication history purge is tracked separately (item 27) |
| 27 | Publish-readiness / history purge | **BLOCKED** | `opensource/docs/PUBLISH-READINESS.md` open: pre-policy vendor `.o` objects in history, GPL reply, DCO/licence files | separate track; do not present the notebook as a submission-ready public repo |

**Checklist counts (27 items): 5 DONE, 17 PARTIAL, 5 BLOCKED.**  (Items 3, 9 and 25
were completed by the final 2026-11-22 regeneration/validation pass; item 2 is left
PARTIAL only because the lead's edit requires the Wi-Fi-side DMA-window sizing
refinement, although its hog-only hardware boot is DONE.)

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
- **Fix:** the board pins are board data, described by the DTS gpio-hog added in
  commit 32d44e6 (`wifi-pcie-hog` on `&porta`, output-high).  The intermediate
  bench images carried a hardcoded `host_init` workaround; **W27b (image sha256
  `4af1d1fa...`) then proved the hog alone suffices** - built from the board DTS
  with patch 013 containing no board-GPIO code it boots with both RC links trained
  (LTSSM 0x11), both endpoints enumerated and GPIO0 DDR/DATA = 0x1330000f.  The
  workaround was therefore removed (commit `ad711d1`), and the submission-facing
  patch comments were restored without re-adding any board-GPIO code.
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
- **PCIe submission caveats still open:** the `acp-gate` register poke (review
  item: should become an ACP power/clock domain, not a pcie driver property), no
  MSI/INTx, static CFG0 viewport for the single-function topology, and no mainline
  endpoint driver (the Wi-Fi driver series is the consumer; the DT nodes being
  enabled is fine once the driver exists, see section 8).  The board-GPIO item is
  **closed** (W27b + commit `ad711d1`).

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

**Stage-B2 also passed [V]** (parent ledger 2026-11-22 18:35, image **W26a** sha
`d03a62e5`): the 6.18 DWC framework programs the RC **inbound iATU** from the new
`dma-ranges` (verified readback: vp0 ctrl2=0x80000000 base=0x80000000
limit=0x8fffffff target=0x80000000); B1 reran clean after a power-cycle; the ETE
channels were programmed (SR0 ch 0x400 base 0x8360c000, DR0 ch 0x590 base
0x83634000, depth-1 0x1f - inside the expected 0x8xxxxxxx device window) and the
H2D smoke test (64 B on SR0, doorbell mask 8 / id 3) completed with no abort and
the device status live.  Verdict: the transport contract (BAR/iATU map, upload,
release, gate, D2H mailbox, ETE ring programming, H2D doorbell) is reproduced
end-to-end by clean-room code on Linux 6.18.  Timing lesson for the driver: the
device posts DEVICE READY about **3 s** after the release, so a one-shot poll must
wait.

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
| `5a6a500` | board DTS: identity `dma-ranges` on both PCIe nodes (content from the lead, wifi-transport stage-B2 prerequisite). **B2 has now passed** on image W26a with this window (RC inbound vp0 0x80000000..0x8fffffff verified); the hunk stays a separate revertable commit because the final DMA window sizing may still be refined by the Wi-Fi driver. |
| `4e959f2` | patch 013 (first packaging pass): hardware-verified GPIO notes + stock GPIO0 values, hardware-verified status replacing "never run", compact 7-word/absolute endpoint-iATU note, dma-ranges inbound note, corrected start_link comment, documented `acp-gate` binding property, minor checkpatch cleanups. |
| `ad711d1` | patch 013: hardcoded board-GPIO workaround removed after the W27b hog-only boot proof (commit by the lead; pristine dry-run rc=0). |
| final doc pass | patch 013 submission notes restored on top of `ad711d1` (comments + binding text only; code identical after comment strip), final patch sha256 `beedd07b...`, 833 lines; 0013 message rewritten for W25d/W26a/W27b; review form + all generated artefacts regenerated and validated. |
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
- **dt-schema tooling:** a dtschema 2026.9 venv is available in the lab build
  artefacts, so `make ARCH=arm dt_binding_check DT_SCHEMA_FILES=.../hisilicon,luofu-pcie.yaml`
  was run on the fully-patched tree: **RC=0, 0 schema errors** (yamllint step
  skipped; log `upstream/series-6.18/validation/dt-binding-check.log`).  The board
  DTS is still **not** run through `dtbs_check` (the `wifi-pcie-hog` child requires
  the post-v6.18 `snps,dw-apb-gpio` hog pattern).  Manual review against the
  submission-time mainline bindings (fetched from `torvalds/linux`) and the
  dtschema-bundled PCI schemas:
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
- **Tooling note:** `checkpatch.pl` must be run on a copied patch file (or an
  absolute path from outside this repo).  Pointing it at the in-repo relative
  path in this checkout mis-detects the patch as a source file and prints one
  spurious trailing-whitespace error.  On a copy, the final patch 013 is **0 errors
  / 17 warnings** (block-comment style + the known binding-split warning).
- **Single-source caveat - resolved:** the skeleton DTS is also the release-line
  DTS and carries release-line-only content (W23 thermal properties incl. the bench
  helper `hisilicon,threshold-mc`).  Decision (a) was implemented:
  `upstream/series-6.18/tools/dts-upstream-filter.py` is the single definition of
  the release-line -> upstream DTS delta, `gen-dts-series-patch.py` applies it and
  `check-release-line.sh` fails if the W23 properties leak into DTS-0001 (decision
  recorded in `07-dts-upstream-bindings.md` section 8).

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

## 7. Generated submission artefacts - regenerated and validated (final, 2026-11-22)

`python3 upstream/series-6.18/tools/gen-series.py --check` is **clean** (26
patches), the release-line validator **PASSes**, and all hashes/validation logs were
regenerated against the **final patch 013** (board-GPIO workaround removed, submission
notes restored) and the current board DTS.  The table records the final state; the
commands below reproduce it.

| artefact | final state |
| --- | --- |
| patch 013 source (`patches-6.18/013-arm-pci-luofu-host.patch`) | sha256 `beedd07b4f63346c451554ff99a62eac2d029b9c41835a99831ecb1c5c83ccd7`, 833 lines; C payload 653 lines + 140-line binding; code identical to `ad711d1` after comment-strip (only comments/binding text restored, board-GPIO block absent) |
| `series/0013-arm-pci-luofu-host.patch` | sha256 `45c47afd6fd83e5d8b5c5861f0715ec82a135c46c5058907668fb33ab53ada34`; regenerated from the rewritten message |
| `commit-messages/0013-arm-pci-luofu-host.txt` / `01-commit-messages.md` | sha256 `0d42436d37c7797712c292ef52437b5bb41dc240bd2fe96aa3c068564bdd72dc`; records the W25d/W26a/W27b hardware bring-up and that the board GPIO pins are DTS data (no driver workaround) |
| `release-line/manifest.tsv` row 013 / manifest | row sha256 `beedd07b...`; manifest sha256 `9893116668b68d7f8bd3b52b9f3b9e7b2e2ac9e4e2a9be56e9b5e6cff45821fe` |
| `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` | sha256 `33956380db9ca5c7b91404596d6078d2f35e4a349978e41e4a1c5d5b9b5155fa`, 712 lines (699-line payload = single-source DTS after the release-line W23 filter; unchanged in the final pass) |
| `validation/*` (checkpatch, apply, git-am, release-line, dt-binding) | refreshed from the final run: 26/26 + 0/73, 55/55 strict, `dt_binding_check` RC=0; `validation/sha256sums.txt` 93 entries, sha256 `35c465bcad05a6d121eb21c8e58c71991617448bb4714c751f069ddd774a3849`, `sha256sum -c` 93/93 OK |
| `submission/MANIFEST.sha256` | regenerated, **105 entries**; `sha256sum -c MANIFEST.sha256` passes inside `submission/` |
| `04/05/06/07` docs, series README, cover letter, submission README | updated with the final digests; `04-validation.md` sections 31-32 record the re-validation runs |

**Reproduction commands** (from `upstream/series-6.18/README.md`; run in a host workspace with an extracted linux-6.18.60 tree; all steps currently PASS):

```sh
python3 tools/gen-series.py           # regenerate series/ + 01-commit-messages.md
python3 tools/gen-series.py --check   # must print "series up to date (26 patches)"
tools/run-validation.sh --use-tree <extracted-6.18.60> <workdir>
tools/check-release-line.sh --use-tree <extracted-6.18.60> <workdir>
python3 tools/gen-dts-series-patch.py \
    --pristine <extracted-6.18.60> \
    --target-dts ../../target-skeleton/dts/luofu-r116.dts \
    --out dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch
# PCIe binding check on the patched tree (dtschema in PATH; 2026.9 used)
make -C <patched-tree> ARCH=arm dt_binding_check \
    DT_SCHEMA_FILES=Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml
```

Remaining pre-post work is the non-packaging work in section 10 (Wi-Fi driver,
target image/install path); the packaging artefacts themselves are postable once the
owner decisions in section 9 are settled.

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
9. **New (2026-11-22, partly resolved):** the DTS hog regression boot is DONE
   (W27b) and the hardcoded workaround is deleted (`ad711d1`); still decide the
   long-term `acp-gate` model (documented bring-up property vs. ACP driver).
10. **Resolved:** the DTS single-source handling of release-line-only thermal
    properties (section 5) - `tools/dts-upstream-filter.py` is the single filter
    definition, enforced by the generator and the validator.
11. **Resolved/keep-separate:** `dma-ranges` passed the stage-B2 bench (W26a) and is
    committed as its own revertable commit `5a6a500`; keep it separable until the
    Wi-Fi driver finalises the window sizing.

---

## 10. Next actions, in dependency order

1. ~~Boot-test the 2026-11-22 DTS and delete the hardcoded GPIO block~~ **DONE
   2026-11-22**: W27b (image sha256 `4af1d1fa...`) booted from the board DTS with
   patch 013 carrying no board-GPIO code; both RC links trained, both endpoints
   enumerated, GPIO0 DDR/DATA = 0x1330000f; workaround removed (`ad711d1`).
2. ~~Regenerate the submission artefacts~~ **DONE 2026-11-22**: section 7.
3. ~~Regenerate DTS-0001 / re-run the release-line validator~~ **DONE 2026-11-22**:
   DTS-0001 `33956380...` unchanged in the final pass, validator PASS.
4. ~~Rewrite the L2 0013 commit message / cover-letter paragraph~~ **DONE
   2026-11-22**: final message records W25d/W26a/W27b and the DTS-owned GPIO pins;
   cover letter and validation docs updated.
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
| PCIe GPIO root cause, bisect, W25d/W26a images | parent `mem-entries.md` 2026-11-22 17:05 / 17:20 / 18:20; parent `STATUS.md`; build/ evidence (gitignored) |
| **PCIe hog-only proof (W27b), workaround removal** | parent ledger 2026-11-22 (W27b entry); image sha256 `4af1d1fa...`; commit `ad711d1` |
| final regeneration + validation (review form, release line, DTS-0001, binding check) | `upstream/series-6.18/04-validation.md` sections 31-32; `validation/`, `submission/MANIFEST.sha256` |
| PCIe register state vs vendor live | parent `mem-entries.md` 2026-11-22 12:45 / 13:20; `docs/incoming/pcie-seq/*` |
| endpoint iATU map (compact/absolute) | `docs/phase18/inbound-map.md` Part A; `docs/incoming/wifi-transport-port/REPORT.md`; ledger 2026-11-22 17:45 / 18:20 |
| Wi-Fi transport B1 gate | parent `mem-entries.md` 2026-11-22 18:20; `docs/incoming/wifi-transport-port/{REPORT,SUMMARY,BENCH-PLAN}.md`, `build/` |
| Wi-Fi transport B2 (ETE rings + H2D smoke, dma-ranges inbound window) | parent `mem-entries.md` 2026-11-22 18:35; image W26a sha `d03a62e5`; `docs/incoming/wifi-transport-port/build/pciet_main.c` (commit 9700250) |
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
| follow-up, same pass | this file | record the stage-B2 pass (image W26a) and the dma-ranges validation |
| `ad711d1` (lead) | patch 013 | board-GPIO workaround removed after the W27b hog-only boot proof |
| final doc pass | patch 013 source, 0013 message, series/, dts-series/, release-line/, validation/, submission/, all docs above | workaround-free patch with submission notes restored; 0013 message rewritten for W25d/W26a/W27b; review form + DTS-0001 + manifest + sha sums + validation logs regenerated; final re-validation PASS (26/26 + 0/73, 55/55 strict, dt_binding_check RC=0) |
| final doc pass | `docs/incoming/wifi-transport-port/{REPORT,SUMMARY}.md`, `build/BUILD-NOTES.md` | transport lane docs brought current with the A/B1/B2 bench results (module sha 416d1db0..., stale BUILD-NOTES hash noted) |

Everything above is host-only: **no build was run, no device was touched, and no
vendor artefact/secret was added.**
