# upstream-v4 - the 013a -> DT reset-gpios fold, series regeneration, v2 review-form status

**Lane:** upstream-v4 / `platform-fold` (shared task **task-171**), **host-only**
(no router, no bench link, no UART, no TFTP, no flash; no vendor binary
touched). **Session date 2026-11-29.** Supersedes the open-item statements of
[../upstream-v3/README.md](../upstream-v3/README.md) where they disagree.

## Result in one paragraph

(to be filled: fold + validation + v2 status)

## Read in this order

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the main report: the fold, the regenerated artefacts, validation evidence, the v2 review-form status and the exact remaining blockers |
| [V2-REVIEW-FORM-STATUS.md](V2-REVIEW-FORM-STATUS.md) | the 30-patch v2 question: what the fold did and did not unblock, the remaining content folds, the attended-boot gate, and the generation checklist |
| [EVIDENCE.md](EVIDENCE.md) | claim -> evidence map (hashes, files, commands, log paths) |
| [artifacts/](artifacts/) | delivered copies of the regenerated artefacts (canonical files stay in the tree) |
| [validation/](validation/) | host validation logs (series apply/checkpatch, release line, DTS/DTS-0001) |

## Hard boundaries kept

* **Host-only**: no device, no bench link, no UART/TFTP/flash; hardware claims
  only quote the parent ledger / committed lanes.
* **No vendor bytes / no secrets**: identities are SHA-256 only; no vendor
  object, extracted DTS, calibration data, per-unit MAC/serial or key material.
* **Commit inside `opensource/` only**, no push, no history rewrite.
