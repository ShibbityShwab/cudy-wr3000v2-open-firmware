# upstream-v4 - 013a -> DT reset-gpios fold, series regeneration, v2 review-form status

**Lane:** `upstream-v4` / `platform-fold` (shared task **task-171**), **host-only**
(no router, no bench link, no UART, no TFTP, no flash; no vendor binary
touched). **Session date 2026-11-29.**  Supersedes the open-item statements of
[../upstream-v3/README.md](../upstream-v3/README.md) where they disagree; the
in-tree docs carry matching v4 update notes.

## Verdict in one paragraph

The **013a -> DT `reset-gpios` fold is done**: the endpoint reset pulse is now
board data on the RC0 node (active-low `reset-gpios` + `reset-assert-us`/
`reset-deassert-us`), consumed by the PCIe driver through the GPIO-consumer
API, with the binding updated; 013a is deleted, L0 is 66 patches and the
release line is **55 include / 11 exclude**.  DTS-0001 was regenerated from the
new 715-line single source, the review form was regenerated from the new 0013
and both host validators re-ran **PASS** (L2 26/26 patch(1)+git am, identical
trees, checkpatch 0 errors / 73 warnings; L1 55/55, 0 fuzz, DTS + DTS-0001
contracts OK) plus a W=1 single-object compile RC=0.  The **30-patch v2 review
form is still not generated**: the fold removed its last content *decision*,
but 16 further content folds, a fold-pipeline design decision, one attended
boot of the active-DTS change and the v2 generator/package rebuild remain.
Nothing was posted or pushed; no device was touched.

## One-screen state (2026-11-29)

| area | state | evidence |
| --- | --- | --- |
| **the fold** | 0013 carries optional `reset-gpios`/`reset-assert-us`/`reset-deassert-us` (assert -> hold 20 ms -> release -> settle 100 ms before RC clock/LTSSM); binding documents them; RC0 DTS node supplies `<&porta 20/21/28 GPIO_ACTIVE_LOW>` | [REPORT.md](REPORT.md) section 2; [artifacts/](artifacts/) |
| **release line (L1)** | **55 include / 11 exclude** (013a folded into 013); validator **PASS** 55/55, 152 file events, 3 offset hunks, **0 fuzz, 0 rejects**, strict git apply all 55; DTS contract + DTS-0001 identity OK | `upstream/series-6.18/validation/release-line-v4/summary.txt`; [validation/release-line-v4/](validation/release-line-v4/) |
| **DTS-0001 (L4)** | regenerated v4: sha256 `6fa1c690...`, 716 lines / 703-line payload, applies + compiles (12,680 B `2a5eb334...`, 8-byte aligned), byte-identical to the filtered single source, no W23 leak | [REPORT.md](REPORT.md) section 3; validator summary |
| **L2 review form** | regenerated 26-patch form (new 0013 body + message); 26/26 patch(1) and `git am`, identical trees, 3 offsets, 0 fuzz, checkpatch **0 errors / 73 warnings**; `gen-series.py --check` clean | [validation/series-v4/](validation/series-v4/) |
| **compile** | folded `pcie-luofu.c` compiles **RC=0 at W=1** with OpenWrt GCC 14.4.0 (single object, reflink copy of the target build tree) | [validation/compile/pcie-luofu-compile.log](validation/compile/pcie-luofu-compile.log) |
| **v2 review form (30 patches)** | **NOT generated**; needs the 16 remaining content folds, the fold-pipeline decision (L0 rewrite vs upstream composition tree), one attended boot, and the v2 generator/package rebuild | [V2-REVIEW-FORM-STATUS.md](V2-REVIEW-FORM-STATUS.md) |
| **hardware** | GP1 (2026-11-24) remains the hardware-validated line; the v4 DT-property form is an **active change and has NOT been boot-tested** - one attended boot is the gate | [REPORT.md](REPORT.md) section 6 |
| **posted/pushed?** | **No.** Nothing sent to any list/maintainer/repo; no push | this page |

## Read in this order

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | main report: the fold, regenerated artefacts, validation evidence, hardware status, limits, reproduction |
| [V2-REVIEW-FORM-STATUS.md](V2-REVIEW-FORM-STATUS.md) | the 30-patch v2 question: what the fold did/did not unblock, the 16 remaining folds, the fold-pipeline decision, the attended-boot gate, acceptance-criteria status, ordered next steps |
| [EVIDENCE.md](EVIDENCE.md) | claim -> artefact/log map |
| [artifacts/](artifacts/) | delivery copies of the four regenerated artefacts (canonical files stay in the tree) |
| [validation/](validation/) | host validation logs: L2 series, L1 release line, compile; plus compiled DTBs from our own DTS |
| [../../upstream/series-6.18/](../../upstream/series-6.18/) | the relocated artefacts are tested there; the v4 notes are in its `README.md`, `05`/`06`/`07` and `04-validation.md` section 33 |

## Hard boundaries kept

* **Host-only**: no device command, no bench link, no UART/TFTP/flash; every
  hardware statement quotes the parent ledger or a committed lane.
* **No vendor bytes / no secrets**: firmware/ROM/module identities are SHA-256
  only; no vendor object, extracted DTS, calibration/EFUSE data, per-unit
  MAC/serial or key material was added.  The DTBs committed in the validation
  logs are compiled from our own board DTS (same practice as the v3 logs).
* **Commit inside `opensource/` only**, no push, no history rewrite, no release
  decision.

## Re-verify (host-only)

```sh
cd opensource
# L2 review form apply/checkpatch (pristine linux-6.18.60 tree)
upstream/series-6.18/tools/run-validation.sh --use-tree <pristine-6.18.60> /tmp/v4-l2
# L1 release line + DTS/DTS-0001 contract (55 patches)
upstream/series-6.18/tools/check-release-line.sh --use-tree <pristine-6.18.60> /tmp/v4-l1
# drift check of the generated form
(cd upstream/series-6.18 && python3 tools/gen-series.py --check)   # -> 26 patches
```
