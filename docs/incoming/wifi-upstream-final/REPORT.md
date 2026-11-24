# Submission finalization - lane report (task-130), 2026-11-27

**Lane:** wifi-upstream-final (shared task **task-130**), host-only. This is the report of the
finalization pass over the upstream submission package; the decision-ready pages are the other
files in this directory ([README.md](README.md) indexes them).
**Scope:** finalize the upstream series + target/image/install status + honest AP/GPL/PD4
documentation + publication checks. **No router, bench link, UART, TFTP, flash or vendor binary
was touched; nothing was posted; no history was rewritten.**

## 1. Result in one paragraph

The submission package is now consolidated and its state is unambiguous: the **platform half**
(kernel series + DTS + target + image recipe + install tooling) is a reviewable, build-verified
RFC that must **not** be posted in its current generated form (the 2026-11-24 GP1 delta made the
frozen v1 review form stale), and the route to the v2 form is fully specified. The **Wi-Fi half**
is a real bench milestone - the first live AP on the clean-room host stack, hostapd
**AP-ENABLED** with the four AP-start messages consumed by the vendor firmware - but it runs on
the **PD4-patched research firmware** with a research provider module and therefore stays a
published research/interoperability artefact, not an upstream driver or product feature. The
**GPLv2 source request is still unanswered** (sent 2026-10-01) and no host-clean boot path is
proven, so the Wi-Fi exclusion statement is mandatory in every submission. **Publication is still
NOT READY** (history purge, licences/DCO, statements, single DTS source, broken links). The
honest next step is one host-only v2 generation pass plus one attended boot, then the platform
series can be posted with Wi-Fi out of scope; the image/install path has one flash and one
`platform_do_upgrade` run to close.

## 2. What this lane did (and did not)

**Did:**
1. re-read the live record (parent ledger newest entries, `STATUS.md`, the 2026-11-24/25/26/27
   lane deliverables) and fixed the final state per artifact;
2. verified the current submission artefacts host-side: `gen-series.py --check` clean (26
   patches), DTS/L1 staleness checks (next section), lane selftests re-run;
3. wrote this finalization package: the upstream series/DTS status, target/image/install status,
   the honest AP milestone page, the GPL/PD4 status page, publication checks, next steps and
   the evidence map;
4. updated the three curated docs with dated 2026-11-27 blocks:
   `../../ROADMAP-OPENWRT.md`, `../../PUBLISH-READINESS.md`, and
   `../../upstream/SUBMISSION-STATUS.md`;
5. ran the publication checks and recorded the blocker list (read-only; see
   [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md)).

**Did not:** post/send anything; change any kernel patch, DTS, target file or image; rewrite
history; push a release; touch the parent repository's ledger/STATUS (Lead actions in
[PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) section 5); contact the vendor.

## 3. Final state snapshot (2026-11-27)

| area | state | detail page |
| --- | --- | --- |
| Platform kernel series | L0 67 patches; L1 55 (must become 56); **L2 v1 26 + DTS-0001 frozen 2026-11-22, clean drift check, but stale after GP1**; v2 = 30 + DTS-0001 planned, not generated; never sent, never reviewed | [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) |
| DTS-0001 | frozen artefact contains the retired hog; current source has no hog and no reset-gpios; must be regenerated with the 0013 `reset-gpios` fold and one attended boot | [DTS-0001-FINAL.md](DTS-0001-FINAL.md) |
| Target skeleton | build-verified 2026-11-24 (defconfig/kernel/world RC=0); target fixes committed; not upstream-accepted; target PR separate | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| Image recipe | real chain; bench image reproduced byte-for-byte (`df6a78c3`) with ABI-correct rootfs; container deliberately marked NOT FLASHABLE-UNTIL-INSTALLED; no factory image | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| Config audit | derived delta clean; bench-only cfg80211 block; target PR must remove lab cmdline/gates + DEVMEM, decide ARCH_HISI and Wi-Fi package metadata | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| Install/flash | T0 userspace zero-flash **hardware-PASS**; hostapd deploy host-verified; kernel+rootfs flash **runbook ready, not executed**; target `platform_do_upgrade` **unproven** | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| Wi-Fi bench | AP milestone real, research-only, PD4-dependent, no beacon/association/data-plane/persistence proof | [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) |
| GPL/firmware | request unanswered; PD4 research-only (H1 consume-only); no vendor-clean path proven; exact blockers and decision tree recorded | [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) |
| Publication | **NOT READY**: history vendor blobs, licences/DCO, missing statements, 4 DTS copies, 23 escaping links; owner-only purge/curated-repos path prepared | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |

## 4. The submission package, layer by layer

* **L0 - lab stack (67 patches).** The target build's patch set; the bench-verified boot source.
  Includes 24 bench-only diagnostics/probes/workarounds that never leave the lab.
* **L1 - release line (55, must become 56).** L0 minus bench-only SMP diagnostics; the release/
  publish snapshot. Verified this session: `release-line/INCLUDED.txt` = 55 entries,
  `manifest.tsv` contains **no 013a** - the committed, load-bearing endpoint-reset patch is
  missing, so the validator currently describes a patch set that is not the booted line.
* **L2 - upstream review form (v1 = 26 + DTS-0001).** The complete reviewed-artefact structure
  exists: 26 format-patches, DTS-0001, cover letter, per-patch commit messages, MAINTAINERS
  proposal, tree/order rationale, host validation records (2026-11-22: 26/26 apply, identical
  trees; 0 errors / 73 checkpatch warnings; PCIe binding `dt_binding_check` RC=0; release-line
  PASS 55/55; manifests recorded) and a documentation-only submission bundle
  (`upstream/series-6.18/submission/`, "nothing submitted anywhere"). **Re-run this session:
  `gen-series.py --check` -> "series up to date (26 patches)".**
* **Why v1 is not postable (verified this session):**
  1. patch 0013 has no endpoint-reset code, while the hardware-validated line needs the committed
     `013a` pulse (GP1: without it the mask ROM never runs S0/S1 and the link never trains);
  2. DTS-0001 still carries the retired `wifi-pcie-hog` (current DTS has neither hog nor
     `reset-gpios`; the hog was retired 2026-11-24 because it defeats the pulse);
  3. L1 lacks 013a (see above);
  4. the v1 form predates the 2026-11-24 target fixes and the GP1 line generally.
* **The planned v2 (30 + DTS-0001).** 26 keep (7 regenerated with folds) + 17 fold + 24 drop;
  four new RFC entries (EQM/PFE-IFC/NAPT/PIE exception delivery), commit-message drafts and
  acceptance criteria already written in
  [../wifi-upstream-target/submission-series.md](../wifi-upstream-target/submission-series.md).
  Generation procedure, decisions and go/no-go gates: [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md).

## 5. The boundary: upstreamable platform work vs research-only Wi-Fi

| upstreamable platform work (in the series) | research-only Wi-Fi path (not in the series) |
| --- | --- |
| ARM luofu machine + TEXT_OFFSET wiring | clean-room PCIe transport module |
| CRG clock/reset + clock tree + watchdog | HCC protocol host implementation, PD4 firmware patch decision |
| FMC SPI-NAND, MDIO/PHY/GEMAC | cfg80211 provider module (wiphy/VAP/scan/AP-start) |
| pinctrl, thermal, PCIe host (with the endpoint reset pulse as DT data) | frame builders + bench provider modules + hostapd fixtures |
| PIE/LSW **RFC** bring-up behind an attended gate (no support claim) | the live AP milestone (PD4-patched firmware, /tmp modules) |
| boot-selector ABI, restart, board DTS (+ loader hand-off contract) | vendor firmware blob, calibration/EFUSt tables, ROM/exception instrumentation |
| target skeleton, image recipe, install tooling (no factory image yet) | data-plane rings/TXQ sync (POST-ONLY + host setup; no started-radio completion proof) |

The Wi-Fi side is **ready evidence for a future driver series** and a publication-grade
interoperability result; it is not a driver, not a firmware replacement and not a support claim.

## 6. The AP milestone in submission context (honest)

On 2026-11-27 hostapd reached `wlan0: AP-ENABLED` (SSID `cudy-ap-test`) and the driver posted
the four AP-start messages (`vap_add` g0 id1 48 B, `vap_start` 40 B, `ap_ssid` 64 B,
`ap_cfg` 184 B), each consumed by the vendor firmware in 0 polls, with the golden readback pack
intact (mask 0, staging 0, VAP pointer `0x01010F18`, ctx0+0x18C updated). It ran on the bench
stack: our 6.18.60 cfg80211 image + `/tmp` `cfg80211.ko` + ap-start v7 provider module
(`fd8e1439...`) + **PD4-patched research firmware** + hostapd-mini v2.12 (zero-flash package
deploy + ucode workaround) + an open 2.4 GHz fixture.

This is the first live AP on the clean-room stack and the capstone of the reverse-engineering
program. It does **not** prove over-the-air beaconing, a client association, WPA2, data-plane
TX/RX completion, persistence or product integration, and it cannot be upstreamed while the
firmware is closed/PD4-dependent and no upstreamable Wi-Fi driver exists. The exact proven/not-
proven lists and reviewer wording are in [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md);
the legal/technical blockade is in [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md).

## 7. GPL / PD4 in one paragraph

The GPLv2 source request was sent 2026-10-01; no reply is held and no sent copy/receipt is in the
repository (a known evidence gap; the request is not a project gate and the project continues by
reverse engineering). The Wi-Fi path needs **PD4**, a research-only patch to the closed vendor
firmware; its audit delivered byte-identical rebuild, minimal two-segment delta, cave safety,
ranked variants and the settled **H1 consume-only** result (the duplex is the non-mailbox
ETE/polled H2D path). No vendor-clean delivery path is proven; one host-only candidate (spare
inbound iATU viewport + one-shot event 0x4C) is untested and would be a new device-write class.
Consequence: the platform series may be posted (it contains only our GPL-2.0 code and no vendor
binary), Wi-Fi must be declared out of scope, and no patched firmware/vendor bytes may ever be
published or shipped. See [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md).

## 8. Target skeleton, image recipe, config audit (summary)

The target tree builds in an isolated OpenWrt main tree (defconfig/kernel/world RC=0,
2026-11-24) and its committed fixes (013a, hog retirement, recorder/selector) are the current
bench line. The image recipe is real (kernel FIP slice + squashfs-in-UBI rootfs + empty overlay
volume + sysupgrade container) and the bench image was rebuilt **byte-for-byte** with an
ABI-correct rootfs. The config is a clean derived delta; a target PR still needs the bench
cmdline/gates and `CONFIG_DEVMEM` removed and the Wi-Fi-stack decision made explicit. Details:
[TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md).

## 9. Flash / install runbooks (summary)

* T0 userspace zero-flash deploy: **hardware-PASS** (49/49 gates, persistent native iw/iwinfo/
  regdb) and safe (auto-rollback).
* hostapd zero-flash package deploy: host-verified (88/88 + 14/14 + 107/107), device flow and
  failure matrix documented; the flashed image still needs the wifi-scripts package fix.
* **kernel+rootfs-from-one-build flash: runbook ready, NOT executed** - this is the canonical
  ABI close (the apk db kernel= string lives in the rootfs) with the ABI receipt defined.
* target `platform_do_upgrade` / release-image path: **unproven**, must run end to end on a
  recovered unit before a target PR ships images; the container is deliberately marked
  NOT FLASHABLE-UNTIL-INSTALLED.

## 10. Publication state (summary)

`tools/check-repo.sh` **PASS** (all 10 checks). `opensource/tools/publish-prep-check.sh`
**FAIL 15 PASS / 9 FAIL / 8 WARN** - the honest publication state: shallow-clone history scan,
ten vendor `.o` blobs reachable from all refs in the legacy history, a `.pyc` blob, missing
`COPYING`/`CONTRIBUTING`/SPDX/DCO, the Wi-Fi+LAN-switch statement missing, 23 relative links
escaping the repo root, four tracked DTS copies, and one false-positive credential pattern. The
owner-only path (fresh curated repos recommended, or the prepared legacy purge runbook) is in
[../../PUBLISH-PREP.md](../../PUBLISH-PREP.md) / `../../PUBLISH-PURGE-RUNBOOK.md`; no rewrite or
push was performed. Full matrix, triage and Lead actions: [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md).

## 11. What remains - the four gates to a posted platform series

1. **v2 generation:** fold content changes (013a -> DT `reset-gpios`, the listed folds, four new
   RFC entries), regenerate the 30-patch form + DTS-0001, refresh L1 to 56, re-run the full host
   validation and manifests ([UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) section 5).
2. **One attended boot** of the `reset-gpios` active-DTS change (2 CPUs, both PCIe endpoints,
   NAND/UBI, serial) - project rule for every active-DTS change.
3. **Cover-letter truth:** Wi-Fi out of scope, PD4 never ships, GPL request unanswered, RFC
   status and no support claims ([GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) section 5).
4. **No vendor bytes / no patched firmware / no research modules** in the posted bundle.

Then the **target PR** is a separate later artefact gated on the install-path run
([TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) section 4d), and the **Wi-Fi closure** items
(beaconing, WPA2, radio-up data plane, host-clean boot option) are research priorities, not
submission prerequisites ([NEXT-STEPS.md](NEXT-STEPS.md) P1/P2).

## 12. Recommendation

**No-go for posting today; go for one host-only generation pass.** The platform series is the
right first submission and has no external code blocker left, but the artefact that exists is not
the hardware-validated line after 2026-11-24; sending it would be exactly the mismatch that gets
an RFC rejected. Generate v2, do the one attended boot, then post the platform group with the
Wi-Fi exclusion statement. Keep the Wi-Fi work published as research with the honest wording in
[AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md). Keep publication as an owner decision until the
checklist closes.

## 13. Limitations of this report

* All hardware statements are quoted from the parent ledger, `STATUS.md` and committed lane
  artefacts; this lane did not touch the device. The strongest claims ([V\*]) are bench-session
  read-backs, not upstream Tested-by evidence.
* The v2 fold decisions are engineering judgement from the 2026-11-24 classification; no fold
  was applied by this lane (that is the P0 host task).
* The publication checker's history section is incomplete on a shallow clone (it says so); the
  blocker list is therefore a floor, not a ceiling.
* The umbrella RE report is at v3 in the committed tree while v4 (AP milestone) is being
  integrated by task-129; this package cites the ledger and owning lanes directly and does not
  depend on the in-flight revision. One known cross-lane hash drift in that report's verifier is
  recorded in [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) 2.3.
* `opensource/docs/incoming/wifi-ready-emit/` was untracked at finalization time and is not this
  lane's output; it is deliberately not committed here.

## 14. Files this lane touched

```
opensource/docs/incoming/wifi-upstream-final/   README, REPORT, UPSTREAM-SERIES-FINAL,
                                                DTS-0001-FINAL, TARGET-IMAGE-INSTALL,
                                                AP-MILESTONE-HONEST, GPL-PD4-STATUS,
                                                PUBLICATION-CHECKS, NEXT-STEPS, EVIDENCE
opensource/docs/ROADMAP-OPENWRT.md              2026-11-27 section + top status refresh
opensource/docs/PUBLISH-READINESS.md            2026-11-27 update block
opensource/docs/upstream/SUBMISSION-STATUS.md   2026-11-27 finalization update block
```

Cross-reference for the project close-out (task-131, `../wifi-closeout/`): the publication run and
the final-state page mapping are self-contained here; the close-out index may cite this package
instead of duplicating the checker output.
