# wifi-upstream-final - submission finalization package (task-130)

**Lane:** wifi-upstream-final (shared task **task-130**), **host-only**, final state
**2026-11-27**. This package is the decision-ready consolidation of the upstream submission
package: the kernel series, the target/DTS/image/install status, the honest AP-milestone
documentation, and the GPL/PD4 boundary. It is **documentation only**: no router, bench link,
UART, TFTP, flash or vendor binary was touched, and **nothing here has been posted upstream**.

**Final verdict in one paragraph.** The platform half is a reviewable, build-verified RFC
draft and the Wi-Fi half is a real but research-only bench result. The frozen 26-patch
review form + DTS-0001 (host validation 2026-11-22) must **not** be posted as-is: since
2026-11-24 the hardware-validated line uses the repo-committed patch 013a endpoint-reset
pulse instead of the retired DTS `wifi-pcie-hog`, and DTS-0001 still carries the hog, so
the review form is stale. Prepare the planned **v2 series (30 kernel patches + DTS-0001)**
first (fold decisions and regeneration steps are in
[UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md)). The Wi-Fi AP milestone is genuine on
the bench - hostapd reached **AP-ENABLED** with SSID `cudy-ap-test` on 2026-11-27 and the
four AP-start messages were consumed by the vendor firmware - but it runs against the
**PD4-patched research firmware** with a research provider module, so it is not an
upstreamable Wi-Fi driver, not a product AP, and must never be shipped: see
[AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md). The GPLv2 source request is still
unanswered (sent 2026-10-01) and PD4 is research-only: see
[GPL-PD4-STATUS.md](GPL-PD4-STATUS.md). Publication is **still NOT READY** (history purge,
licences, statements): see [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md).

## Read in this order

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | **the main report**: final state, what was finalized, the submission package, the upstream/research boundary, go/no-go recommendation, evidence, limits |
| [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) | the kernel series head: L0/L1/L2 layers, the frozen v1 form and why it is stale, the planned v2 (30 + DTS-0001), fold decisions, acceptance criteria and regeneration procedure, do-not-send list |
| [DTS-0001-FINAL.md](DTS-0001-FINAL.md) | DTS-0001 classification: what the review-form DTS entry is, the two required deltas (hog retirement, `reset-gpios` fold), the regeneration contract and gates |
| [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) | target skeleton, image recipe, CONFIG audit, bench-image identity and the three install/flash paths with their hardware status |
| [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) | the 2026-11-27 AP milestone: exact evidence, what it proves and does not prove, why it cannot be upstreamed, reviewer-facing wording |
| [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) | GPLv2 request status, the PD4 research patch and its audit boundary, the exact legal/technical blockers and the decision tree |
| [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) | the publication checks run for this finalization, their results, the blocker list, and the owner-only actions that remain |
| [NEXT-STEPS.md](NEXT-STEPS.md) | ordered next actions (host-only / attended / owner) for the maintainer and the Lead |
| [EVIDENCE.md](EVIDENCE.md) | claim -> evidence map: ledger entries, commits, artifact hashes, verifier commands |

## One-screen state (2026-11-27)

| area | state | evidence |
| --- | --- | --- |
| **Platform kernel series** | RFC review form exists: 26 patches + DTS-0001 (host validation 2026-11-22: 26/26 apply, 0 errors / 73 checkpatch warnings; DTS binding RC=0). **Stale since the 2026-11-24 GP1 delta** -> v2 (30 + DTS-0001) must be generated before posting. Never sent, no maintainer has seen it. | [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md), `../../../upstream/series-6.18/` |
| **Target / image** | L0 = 67 patches; isolated-tree build PASS (defconfig, kernel prepare/compile, full image); bench image reproduced **byte-for-byte** (container `df6a78c3`, kernel slice `13e81214`, slot `1794a531`, rootfs `62344e80`) with an ABI-correct rootfs. Not upstream-accepted; target PR config cleanup and a real install-path run remain. | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| **Install / flash** | T0 userspace zero-flash deploy **hardware-PASS** (native iw/iwinfo/regdb persistent); kernel+rootfs-from-one-build flash **runbook ready, NOT executed**; target `platform_do_upgrade` never ran end to end. | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md), `../wifi-abi-image/FLASH-RUNBOOK.md` |
| **Wi-Fi (bench)** | first live AP on the clean-room host stack: hostapd **AP-ENABLED** (`cudy-ap-test`), 4 AP-start messages consumed (vap_add 48 B, vap_start 40 B, ap_ssid 64 B, ap_cfg 184 B), golden readback intact. **Research-only**: PD4-patched vendor firmware, provider module from `/tmp`, no beacon/association/data-plane proof. | [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) |
| **GPL / firmware** | GPLv2 request sent 2026-10-01, **no reply held**; no vendor-clean mailbox delivery path proven; PD4 is a research artifact that must never ship. | [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) |
| **Publication** | **NOT READY**: legacy history still reaches ten vendor `.o` files, missing `COPYING`/`CONTRIBUTING`/DCO, Wi-Fi + LAN-switch statement missing, four tracked DTS copies; owner-only path in `../../PUBLISH-PREP.md`. | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |

## Hard boundaries this package keeps

* **Host-only.** No device command was run for this package. Every hardware statement is quoted
  from a dated ledger entry or a committed lane artifact; [EVIDENCE.md](EVIDENCE.md) maps them.
* **No vendor bytes, no per-unit values, no secrets.** Firmware/ROM/module identities appear as
  SHA-256 only. The PD4 recipe is a patch recipe plus verifier, never firmware bytes.
  Synthetic AP/scan frames live in the owning lanes; this package carries sizes and hashes only.
* **Research != upstream.** PD4, the clean-room Wi-Fi driver stack and the AP bring-up are not
  in the kernel series and must not be presented as, or attached to, an upstream submission.
* **No root-repository edits.** This lane commits only inside `opensource/`; the parent-repo
  ledger/STATUS updates are listed as Lead actions in [NEXT-STEPS.md](NEXT-STEPS.md).
* **No history rewrite, no push of a "release".** Publication is an owner decision; the checks
  here only measure readiness.

## How to re-verify this package (host-only)

```sh
# 1. repository consistency (parent repo, read-only)
tools/check-repo.sh                                  # expect "REPO CHECK: PASS"

# 2. the kernel review form is drift-free (26 patches)
cd opensource/upstream/series-6.18
python3 tools/gen-series.py --check                  # expect "series up to date (26 patches)"

# 3. the publication state (read-only; FAIL is the expected honest state)
cd opensource && bash tools/publish-prep-check.sh    # expect FAIL with the blocker list

# 4. device-free lane selftests behind the final-state claims
PY=$PWD/.provision-venv/bin/python
$PY opensource/docs/incoming/wifi-fw-audit/tools/verify_pd4.py --selftest       # 14/14
$PY opensource/docs/incoming/wifi-fw-audit/tools/pd4_variants.py --selftest     # 14/14
( cd opensource/docs/incoming/wifi-abi-image && bash tools/selftest.sh )        # 89/89
( cd opensource/docs/incoming/wifi-ap-start && bash tools/build.sh )            # build OK
( cd opensource/docs/incoming/wifi-hostapd-deploy && bash tools/selftest.sh )   # 14/14
```

The exact results recorded for this finalization (including the one known drift) are in
[PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) section 2.

## Related artefacts (not modified by this lane)

* [../wifi-upstream-target/REPORT.md](../wifi-upstream-target/REPORT.md) - the 2026-11-24
  target/series checkpoint this package finalizes (classification, build verification, config audit).
* `../../../upstream/series-6.18/` - the frozen review form (v1) and release-line manifest.
* [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md) - PD4 audit, the primary record.
* [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md) - the one-flash runbook.
* [../wifi-ap-start/README.md](../wifi-ap-start/README.md),
  [../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md) - AP-path lanes.
* [../wifi-re-report/REPORT.md](../wifi-re-report/REPORT.md) - the umbrella reverse-engineering
  report (v4 integration of the AP milestone is task-129; this package cites the ledger and the
  owning lanes directly so it does not depend on that in-flight revision).
* [../../ROADMAP-OPENWRT.md](../../ROADMAP-OPENWRT.md), [../../PUBLISH-READINESS.md](../../PUBLISH-READINESS.md),
  [../../GPL-REQUEST.md](../../GPL-REQUEST.md), [../../PUBLISH-PREP.md](../../PUBLISH-PREP.md) -
  the long-form curated docs, updated with dated 2026-11-27 blocks by this lane.
