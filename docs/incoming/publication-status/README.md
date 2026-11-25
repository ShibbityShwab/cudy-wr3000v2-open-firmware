# publication-status - curated-repos sync prep and the honest publication checklist (task-172, step 1)

**Lane:** `publication-status` (part of shared task `task-172`, the publication track). **Status:**
host-only review + prepared sync; **nothing was pushed, fetched from or written to a curated
repository, and no history was rewritten.** Prepared 2026-11-25 at the RX-hunt closeout. This page
is the checklist status; the executable sync plan is [SYNC-PLAN.md](SYNC-PLAN.md), the computed
per-file delta is [surface-delta.txt](surface-delta.txt).

**One-line status:** the notebook and the legacy `opensource/` submodule are current locally (the
submodule's new commits are **not pushed**); the curated `cudy-wr3000v2-openwrt` repo is clean but
**stale** (source pin `a0aa6b1`, 2026-11-22; **251 source commits** and **92 changed files** behind);
the curated `cudy-wr3000v2-cve` repo is current. The new Wi-Fi RX-hunt material is intentionally
**not** part of the curated port repo (policy: research record in the notebook, not a release). The
remaining publication blockers are unchanged and mostly **owner-only** (history purge, legacy repo
containment, root LICENSE/NOTICE pair, DCO/SPDX, the curated-repo push decision).

---

## 1. Publication surfaces - state at this review

| surface | local state | remote state | sync status | what is new since the last sync |
| --- | --- | --- | --- | --- |
| **root notebook** `ShabbityShwab/cudy-wr3000v2` | `master` = `d27b649` ("Full remaining-work swarm", 4 lanes) + the submodule-pointer dirt from this swarm; `tools/check-repo.sh` = **PASS** | `origin/master` = `d27b649` (**in sync, 0 ahead**) | current | the RX-hunt wave (ledger), the security pass (tracked vendor blob removed from HEAD), the lane reports |
| **legacy `opensource/` submodule** `cudy-wr3000v2-open-firmware` (pub. 2026-09-30, pre-policy history) | `master` = `e7d6cd4` (`04c0521` + **5 unpushed commits**: the two lane REPORTs, report v7, this status, the rx-init-msg lane); **13 dirty files** are the in-flight platform-fold lane | `origin/master` = `04c0521` | **5 commits ahead - NOT pushed** (Lead's go required) | the RX-hunt lane reports (woc/cmp), RE report v7, publication-status, the concurrent lanes' commits |
| **curated `cudy-wr3000v2-openwrt`** (fresh clean history, 2026-11-18) | `/home/shibbityshwab/repos/cudy-wr3000v2-openwrt`, `main` = `4bd4360` (324 files, worktree clean) | `origin/main` = `4bd4360` (in sync) | **STALE**: source snapshot pin `a0aa6b1` (2026-11-22, W23/full-reversal sync) -> **251 source commits behind** | 92 changed files vs the source: 21 docs, 28 upstream-series files, 40 target-skeleton files, README/NOTICE/.gitignore (see `surface-delta.txt`); the in-flight upstream-v4/platform-fold series is a **moving target - wait for it** |
| **curated `cudy-wr3000v2-cve`** (fresh clean history) | `/home/shibbityshwab/repos/cudy-wr3000v2-cve`, `9b1e7d1`, 13 files, clean | `origin/main` = `9b1e7d1` | current | nothing (CVE research unchanged) |

The reference policy for what belongs where is `PUBLISH.md` (root) + `opensource/docs/PUBLISHING.md`
(curated repo): the notebook is the intentionally public research record; `cudy-wr3000v2-openwrt`
is the curated host-validated port (target/series/DTS/install + retained **text-only** RE lane
reports); vendor binaries, firmware, calibration data, per-unit identifiers and the Wi-Fi research
modules stay **out** of both.

## 2. What the new material is, and where it goes

| new material (this wave) | notebook (root + `opensource/`) | curated `cudy-wr3000v2-openwrt` |
| --- | --- | --- |
| Wi-Fi RX-hunt lane reports (`wifi-rx-vendor-diff`, `-format`, `-crx-gating`, `-linux-parity`, `-capture-plan`, `-woc-class`, `-completion-phase`) | yes - committed under `opensource/docs/incoming/` | **no** (research/bench + firmware-adjacent; the curated repo keeps only its existing text-only RE subset) |
| `wifi-re-report` v7 (F28-F30, 6.26-6.29, T46-T49, EV-343..EV-372, C65-C71; verifier 358/358) | yes | **no** (research record; it is not part of the port) |
| vendor architecture decode (17,059-symbol init chain, config events) | yes (report + lane docs; no vendor byte) | **no** |
| stock control / PD4 discriminator / security pass | yes (ledger + this status) | no |
| root-notebook security pass: the tracked vendor recovery blob removed from HEAD (`tftp/` now ignored) | yes | n/a |
| upstream series / release line / DTS-0001 updates + target-skeleton port updates | yes | **yes - the main sync target** (`upstream/`, `target-skeleton/`, selected `docs/`), but only after the in-flight `upstream-v4`/platform-fold lane lands |
| `opensource/docs/incoming/publication-status` | yes | n/a |

## 3. Read-only checks run for this status (all PASS/recorded)

| check | command | result |
| --- | --- | --- |
| project repo policy (root) | `tools/check-repo.sh` | **PASS** (10/10: docs/tools, shadow guard, kernel snapshot, no vendor `.ko`, no vendor binaries in the published half, STATUS freshness, retired phrases, links, tool refs) |
| publish-readiness (legacy half) | `opensource/tools/publish-prep-check.sh` | **FAIL** - summary **19 PASS / 7 FAIL / 6 WARN** (the honest pre-existing state; no new failure from this wave) |
| curated-vs-source delta | `publication-status/surface-delta.txt` | 92 changed + 16 curated-only files; 251 source commits since pin `a0aa6b1` |
| curated repo cleanliness | `git -C /home/shibbityshwab/repos/cudy-wr3000v2-openwrt status` | clean; 324 tracked files; tip = origin/main = `4bd4360` |
| report verifier | `wifi-re-report/tools/verify_report.py` | **PASS (358/358)** |
| secret/credential scan of the new files | `git grep` for keys/MACs in the changed paths | no secret, no real MAC, no vendor byte (the report/lanes carry placeholders and derived constants only) |

The publish-prep **7 FAILs** are the known open items, not this wave's regressions: shallow-clone
history scan (procedural - run on a full clone), the missing `COPYING`/DCO/SPDX/Copyright items
(B2), the "clean-room" wording in README/NOTICE (item 5), the **ten legacy vendor `.o` blobs still
reachable from history** (item 1 - owner-only purge), the binary/trailer history blobs
(`luofu-r116.dtb` x2 + the `__pycache__` pyc), the tracked credential-pattern strings in the
selftest scripts (false positives: they are grep patterns used to *detect* keys - a lane/owner
cleanup could split the literals), and the **7 tracked `.dts` copies** (item 7: one upstream-style
source required). The 6 WARNs are the schematic target, the inactive profile file, the
vendor-derived build glue, the narrow fetch refspec, the missing PROVENANCE.md and the missing
GPL-request sent copy.

## 4. Checklist status

| # | item | state | evidence | owner |
| ---: | --- | --- | --- | --- |
| 1 | legacy `opensource/` history purge (ten vendor `.o` + 1 pyc; B0/B1) | **OPEN** - runbook prepared/dry-run validated, never executed | `opensource/docs/PUBLISH-PREP.md`, `PUBLISH-PURGE-RUNBOOK.md`; publish-prep-check FAIL | owner (force-push/branch ops) |
| 2 | legacy repo containment (archive/private) | **OPEN** | `PUBLISH.md`; `PUBLISH-PREP.md` | owner |
| 3 | root notebook HEAD sanitized (secrets/MACs) | **PASS at HEAD** (security pass 2026-11-26); pre-policy history remains dirty | ledger security-pass entries; `git ls-files` binary scan | notebook lane (new commits); owner (history) |
| 4 | no vendor binaries/blobs in tracked text (new work) | **PASS** - 0 tracked binaries in either repo; the one exposed blob was removed from HEAD and `tftp/` ignored; exposure recorded | security pass ledger entry; binary scan | all lanes |
| 5 | no secrets in new tracked text | **PASS** - placeholders only; `.omo-secrets/` perms 700/600; the 10 offensive-security tools use placeholders/args | security-pass entry | all lanes |
| 6 | report v7 + RX-hunt lane docs | **DONE** (committed locally in `opensource/`, unpushed) | commits on `opensource master` since `04c0521` | this lane (done) |
| 7 | curated `cudy-wr3000v2-openwrt` content current vs source | **STALE** - sync prepared, **not executed** (no push without the Lead's go; waits for the upstream-v4 series) | `SYNC-PLAN.md`, `surface-delta.txt` | Lead decision + sync lane |
| 8 | curated `cudy-wr3000v2-cve` current | **PASS** (unchanged, 13 files) | local clone = origin/main | owner |
| 9 | root `LICENSE`/`NOTICE` pair (MIT/GPL split) | **OPEN** | `PUBLISH.md`; `PUBLISH-READINESS.md` item 2 | owner/lane |
| 10 | DCO/SPDX/Copyright headers (lab/kernel code, commits) | **OPEN** (publish-prep FAIL) | publish-prep-check section B | lanes + owner |
| 11 | curated repo push (after sync) | **BLOCKED on the Lead's explicit go** | this page, `SYNC-PLAN.md` | Lead |
| 12 | public notebook push of the new commits (root + legacy submodule) | **BLOCKED on the Lead's explicit go** (root is in sync; the submodule is 5 ahead) | surface table | Lead |

## 5. What the Lead must decide (in order)

1. **Let the in-flight lanes land** (upstream-v4/platform-fold series regeneration, rx-init-decode
   report) and confirm the `opensource` worktree is clean; the curated sync must not snapshot a
   dirty tree.
2. **Approve or defer the pushes**: the legacy `opensource` submodule is 5 commits ahead (the new
   documentation is public-only, no vendor bytes); the root notebook has only the submodule-pointer
   commit pending.
3. **Decide the curated sync** (see `SYNC-PLAN.md`): if approved, run the prepared copy, re-run the
   read-only gates, commit in the curated clone, and only then push with the explicit go.
4. **Owner-only items** can proceed independently: history purge/legacy containment, root
   LICENSE/NOTICE, DCO/SPDX cleanup, the missing GPL-request sent copy, the false-positive
   credential-pattern cleanup.

## 6. Honest limits of this status

- The curated clone used for the delta is the local one at `4bd4360` (== its `origin/main` as last
  fetched 2026-11-21); the live remote was **not** fetched (no network action was taken). Re-fetch
  before acting if the remote may have moved.
- The delta classification (what belongs in the curated repo) follows `docs/PUBLISHING.md`'s
  curation rules; the 92-file list is computed, the keep/drop judgement for the retained
  `docs/incoming/` subset is the sync lane's review.
- The publish-prep FAIL/WARN counts are from a read-only run of the committed checker on the
  current worktree (a shallow clone + dirty worktree), so the history section is explicitly
  incomplete until the owner runs it on a full clean clone.
- This page contacted no device, UART, TFTP or network; it produced no push and no curated-repo
  write.
