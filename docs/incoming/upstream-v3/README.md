# upstream-v3 - upstream platform track: DTS-0001 v3 + series status + publication pass

**Lane:** upstream-v3 (shared task **task-161**), **host-only** (no router, no
bench link, no UART, no TFTP, no flash; no vendor binary touched). **Session
date 2026-11-29.** This package advances the **platform track** of the OpenWrt /
kernel submission (the Wi-Fi stack stays research-only).  It supersedes the
status page of [../wifi-upstream-final/README.md](../wifi-upstream-final/README.md)
(2026-11-27) where they disagree.

**Verdict in one paragraph.** The platform submission is closer but still
**not postable**: this lane regenerated the stale **DTS-0001** entry from the
current single-source board DTS (the retired `wifi-pcie-hog` is gone; the
entry now matches the 2026-11-24 GP1 hardware-validated line), fixed the
**release line** to include the load-bearing `013a` endpoint-reset patch
(**55 -> 56 patches**) and re-ran its validator **PASS**, re-ran both host
validators for the 26-patch review form (**PASS**: apply/identical trees/
checkpatch 0 errors / 73 warnings; no build in this lane - the last recorded
full build for these byte-identical patches is RC=0), and closed the
host-side publication gaps that were fixable: `COPYING` (GPL-2.0),
`CONTRIBUTING.md` (SPDX + DCO), all **77 broken relative links** (checker
section G now PASS), and the five false-positive credential-pattern strings
in lane self-tests (checker section F tree scan now PASS).  The publication
checker moved from **13 PASS / 10 FAIL / 9 WARN** to **18 PASS / 6 FAIL /
8 WARN**; the six remaining FAIL groups are owner-only history/licence/prose
decisions (shallow clone, 42 SPDX / 52 copyright / 356 DCO history,
`clean-room` wording, vendor-`.o` history, 4 DTS copies) and are itemised in
[PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md).  The planned 30-patch
**v2 review form is still not generated**: its single remaining content
decision is the `013a` -> DT `reset-gpios` fold in patch 0013 (+ binding +
DTS) plus one attended boot.  Nothing was sent anywhere; **no push**.

## Read in this order

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the main report: what changed, evidence, series status, publication fixes, the Wi-Fi research-only boundary, limits |
| [DTS-0001-V3.md](DTS-0001-V3.md) | the regenerated DTS-series entry: contract, nodes, hashes, regeneration and validation record, remaining `reset-gpios` decision |
| [SERIES-STATUS.md](SERIES-STATUS.md) | L0/L1/L2/L3 state table, apply/checkpatch/build evidence, the four posting gates |
| [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md) | checker before/after, fixes applied, remaining owner-only blockers, exact re-run commands |
| [EVIDENCE.md](EVIDENCE.md) | claim -> evidence map (hashes, files, commands, log paths) |
| [artifacts/](artifacts/) | copy of the delivered DTS-0001 v3 patch (the canonical file is `upstream/series-6.18/dts-series/`) |
| [tools/fix-publication-links.py](tools/fix-publication-links.py) | idempotent fixer for the 77 broken relative links (section G) |

## One-screen state (2026-11-29)

| area | state | evidence |
| --- | --- | --- |
| **L1 release line** | **56 include / 11 exclude**; `013a` added; validator **PASS** 56/56, 0 fuzz, 0 rejects, strict git apply all 56, DTS + DTS-0001 contract OK; logs `upstream/series-6.18/validation/release-line-v3/` | [SERIES-STATUS.md](SERIES-STATUS.md) |
| **DTS-0001 (L3)** | regenerated from the 702-line single source (690-line filtered payload, no hog); patch sha256 `1fd1d10a...`; applies to the L1 tree, compiles, loader hand-off contract + `enable-method` verified; pad 12,560 B | [DTS-0001-V3.md](DTS-0001-V3.md) |
| **L2 review form** | frozen 26-patch v1, unchanged; `gen-series.py --check` clean; `run-validation.sh` **PASS** (26/26 patch(1) + git am, identical trees, 3 offsets, 0 fuzz, checkpatch 0 errors / 73 warnings).  **v2 (30 patches) NOT generated** - needs the `reset-gpios` fold decision | [SERIES-STATUS.md](SERIES-STATUS.md) |
| **Target / image** | unchanged by this lane; target fixes committed (013a, recorder/selector); target PR still gated on the install-path run (previous lanes) | `05-release-line.md`, `../wifi-upstream-final/TARGET-IMAGE-INSTALL.md` |
| **Publication** | checker **18 PASS / 6 FAIL / 8 WARN** (was 13/10/9): COPYING + CONTRIBUTING added, 77 links fixed (G PASS), tree credential false positives removed (F PASS); remaining FAILs are owner-only | [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md) |
| **Wi-Fi (research only)** | **not part of the submission and never will be in this form**: PD4-patched vendor firmware + research modules; WPA3-SAE AP on air, TX completed, RX armed etc. are bench results, not an upstreamable driver | [REPORT.md](REPORT.md) section 5 |
| **Posted?** | **No.** Nothing was sent to any list, maintainer or repository; no push | this page |

## Hard boundaries kept

* **Host-only**: no device command, no bench link, no UART/TFTP/flash; every
  hardware statement quotes the parent ledger or a committed lane.
* **No vendor bytes / no secrets**: firmware/ROM/module identities are SHA-256
  only; no vendor object, extracted DTS, calibration/EFUSE data, per-unit
  MAC/serial or key material was added.  The DTS-0001 patch contains only our
  board DTS prose + DTS source.
* **Commit inside `opensource/` only**, no push (Lead decision), no history
  rewrite, no release decision.

## Re-verify (host-only)

```sh
cd opensource
# release line + DTS-0001 contract (needs a pristine linux-6.18.60 tree)
upstream/series-6.18/tools/check-release-line.sh --use-tree <pristine> /tmp/rl
# review-form apply/checkpatch
upstream/series-6.18/tools/run-validation.sh --use-tree <pristine> /tmp/rv
cd upstream/series-6.18 && python3 tools/gen-series.py --check
cd ../.. && bash tools/publish-prep-check.sh        # expect 18/6/8 (dry run)
```
