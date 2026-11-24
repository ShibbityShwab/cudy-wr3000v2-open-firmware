# Publication checks - finalization run (2026-11-27)

**What this file is.** The publication-readiness checks run for this finalization, with the exact
commands, the results recorded, and the owner-only actions that remain. All checks are
**read-only** (no file was created/modified, no network fetch, no history rewrite, no push), and
this lane edits nothing outside its own directory plus the three curated docs updated with dated
blocks. **Publication is still NOT READY.**

## 1. Check matrix

| # | check | kind | result |
| --- | --- | --- | --- |
| 1 | parent `tools/check-repo.sh` | repo consistency, read-only | **PASS** (all 10 checks) |
| 2 | `opensource/tools/publish-prep-check.sh` | publish readiness A-H, read-only | **FAIL** (15 PASS / 9 FAIL / 8 WARN) - the honest current state |
| 3 | `git -C opensource ls-files` binary scan | no tracked vendor/binary files | **PASS** (0 files matching `.o/.ko/.a/.so/.bin/.img/.dtb/.elf/.ubi/.squashfs`) |
| 4 | history reachability of vendor objects | section E of check 2 | **FAIL** (10 vendor `.o` blobs reachable from all 3 refs; see 3.2) |
| 5 | `python3 tools/gen-series.py --check` (review form) | kernel-series drift | **PASS** - "series up to date (26 patches)" |
| 6 | PD4 audit harness | device-free | **PASS** - `verify_pd4.py --selftest` 14/14; `pd4_variants.py --selftest` 14/14 |
| 7 | ABI-image lane selftest | device-free | **PASS** - 89 passed, 0 failed |
| 8 | AP-start builders | device-free C99 compile + selftest | **PASS** - `ap_frames_selftest: 128 passed, 0 failed`; `build: OK` |
| 9 | hostapd-deploy lane selftest + ladder | device-free | **PASS** - selftest 14/14; `check-ladder: 107 passed, 0 failed` |
| 10 | re-report umbrella verifier | device-free cross-check | **123/124** - one known cross-lane drift (3.3); re-report4 task-129 is updating that report in flight, not a finalization blocker |

## 2. Recorded results

### 2.1 Parent-repo consistency (`tools/check-repo.sh`) - PASS

```
PASS  required docs/tools exist
PASS  live tools are executable
PASS  shadow guard PASS
PASS  tracked kernel snapshot PASS (manifest)
PASS  no tracked vendor .ko in the root repo
PASS  no tracked vendor .o/.ko in the published half
PASS  STATUS.md (2026-11-27 04:35 +07) is not older than the newest ledger entry (2026-11-27 04:30)
PASS  no retired-claim phrases in live docs
PASS  relative markdown links resolve
PASS  no doc references a missing live tool
REPO CHECK: PASS
```

Note: the root `STATUS.md` passes the freshness gate but its **body** only goes through the
2026-11-26 record (it predates the 2026-11-27 AP milestone); regenerating it is a Lead action
([NEXT-STEPS.md](NEXT-STEPS.md)).

### 2.2 Publish-prep checker (`opensource/tools/publish-prep-check.sh`) - FAIL 15P / 9F / 8W

Context: HEAD `9b180f3` on `master`, visible history **302 commits reachable from 3 refs**,
**shallow clone** (the script itself FAILs the history scan until the owner runs
`git fetch --unshallow` on a full clone), worktree dirty at scan time (in-flight lane edits).
A second run after this lane's documentation edits returned the **identical summary**
(15 PASS / 9 FAIL / 8 WARN): the new pages add no new publication failure.

**The nine FAILs, triaged:**

| FAIL | real blocker? | note |
| --- | --- | --- |
| shallow clone: history scan incomplete | procedural | run the checker on a full clone before any publication decision |
| missing `COPYING` (GPL-2.0 text) and `CONTRIBUTING.md` | **yes** (B2) | lab/ kernel code is GPL-2.0 but only MIT `LICENSE` exists |
| 42/108 `lab/` files lack SPDX id; 52/55 C/H/S lack Copyright; CONTRIBUTING missing; 302/302 commits lack Signed-off-by | **yes** (B2/DCO) | publication checklist item 4 |
| Wi-Fi + LAN-switch status statement missing in README/NOTICE | **yes** (item 9) | must state vendor-only/closed firmware/unanswered GPL request |
| 10 vendor `.o` blobs reachable from master / origin-HEAD / origin-master | **yes** (B1/history) | removed from HEAD 2026-11-17 but still in published history; owner purge or curated repos |
| one `__pycache__/*.pyc` blob in history | purge-set | add to the purge set (already in `PUBLISH-PURGE-RUNBOOK.md`) |
| "credential pattern" in history + tree: `docs/incoming/wifi-image-integration/tools/selftest.sh:81` | **false positive** | the matched text is the self-test's own `grep 'BEGIN ... PRIVATE KEY'` pattern, not key material; fix the checker allowlist or reword the test |
| 23 broken relative markdown links in 6 files (first: `docs/incoming/vendor-userspace/REPORT.md` links `../../../../gist-...`, `dumps/...`, `tools/luadump.py` escaping the repository root) | **yes for a clean release** | either point at in-repo artefacts or keep as plain code text; this lane's own docs use plain code text for parent-repo paths for that reason |

**The warnings worth acting on:** no dedicated `PROVENANCE.md` (the provenance statement lives
inside README/NOTICE); no sent GPL-request copy/receipt (dated no-reply record satisfies the
checklist alternative; owner decides on storing the copy); vendor-derived build glue/header
still tracked under `lab/luofu-kernel/nand/`; 4 tracked `.dts` copies
(`docs/soc/*.dts`, `lab/luofu-kernel/dts/`, `target-skeleton/dts/luofu-r116.dts`) where the
checklist wants a single source; 125 unreachable objects in the local object DB (purge residue;
expire/gc is owner-approved); the target tree warning still says "SCHEMATIC/UNTESTED" (stale
label - the tree is bench- and build-verified; the target-PR wording should say "not
upstream-accepted" instead).

### 2.3 One known cross-lane drift (not a submission blocker)

`opensource/docs/incoming/wifi-re-report/tools/verify_report.py` returned **123/124**: the failing
check is "scan/VAP provider module hash matches the owning lane source [report=True lane=False]" -
the umbrella report pins an older provider-module hash than the current owning-lane artifact.
The re-report lane is updating that report to v4 (task-129, in flight) and owns the fix; it does
not affect the kernel series, the target tree or the AP result (the AP result is cited from the
ledger and the owning lane, not from the umbrella report).

## 3. This lane's own publication hygiene

* The commit is **documentation only**: markdown files under
  `opensource/docs/incoming/wifi-upstream-final/` plus dated update blocks in
  `docs/ROADMAP-OPENWRT.md`, `docs/PUBLISH-READINESS.md` and `docs/upstream/SUBMISSION-STATUS.md`.
* **No vendor bytes**: no firmware/ROM/module/calibration content; identities are SHA-256 only;
  no vendor object code; no extracted DTS; no per-unit MAC/serial; no secrets or key material.
* **No link escapes the repository root**: parent-repo paths (e.g. the ledger, root `STATUS.md`)
  are written as code text, not relative markdown links (the checker's section G).
* Untracked/modified paths observed at finalization time and deliberately **not** committed by
  this lane: `docs/incoming/wifi-ready-emit/` (untracked lane output, pre-existing) and
  `docs/incoming/wifi-re-report/REPORT.md` (modified by the in-flight re-report4 lane).

## 4. Publication blocker list (owner-only actions)

1. **History purge or the fresh curated repos.** The legacy repository still serves ten vendor
   `.o` files and one `.pyc` blob from history on all refs. The prepared owner-only paths:
   `PUBLISH-PURGE-RUNBOOK.md` (legacy purge) and `PUBLISH-PREP.md` sections C1-C4 (fresh curated
   repositories; recommended). No history rewrite or push was performed by this lane.
2. **Licences/DCO:** add `COPYING` (GPL-2.0), `CONTRIBUTING.md` (SPDX + Copyright + DCO
   convention), per-file headers, and sign-off convention; fix the "clean-room" provenance
   wording wherever it survives (README/NOTICE already carry the GPL-2.0 reverse-engineering
   statement).
3. **Statements:** add the Wi-Fi + LAN-switch status sentence to README/NOTICE (copy-ready text in
   [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) section 5).
4. **Single DTS source:** collapse the four tracked DTS copies to the single source (or mark the
   historical copies clearly and exclude them from the published tree).
5. **Links:** fix the 23 relative links that escape the repository root
   (`docs/incoming/vendor-userspace/REPORT.md` and the other five files).
6. **Checker false positive:** reword `docs/incoming/wifi-image-integration/tools/selftest.sh`
   line 81 (or allowlist it in the checker) so a clean run does not flag its own key-pattern test.
7. **Re-run on a clean full clone** with `--strict` after 1-6; record the run (the owner release
   log). Publication remains a **human decision** - a PASS from the checker is not a publication
   decision by itself (the checker says this too).

## 5. What the Lead must do in the parent repository (not this lane)

* append the dated ledger entry (2026-11-27) for this finalization lane and
  `python3 tools/gen-ledger-index.py`;
* add the AP milestone (2026-11-27 04:30) + this finalization to the root `STATUS.md` and
  regenerate it (its body currently ends at the 2026-11-26 record);
* update `EVIDENCE-INDEX.md` with the finalization/AP entries and `CURRENT-TRUTHS.md` if the
  curated truth list restates the upstream/Wi-Fi boundary;
* run `tools/check-repo.sh` and commit the parent repo (submodule pointer + ledger/STATUS);
* decide the untracked `docs/incoming/wifi-ready-emit/` lane output: commit it (with its lane
  report) or remove it before publication.

## 6. Re-run commands

```sh
# parent repo
tools/check-repo.sh
# publication readiness (read-only; run on a clean full clone for the history section)
cd opensource && bash tools/publish-prep-check.sh --strict
# kernel review form drift
cd opensource/upstream/series-6.18 && python3 tools/gen-series.py --check
# this lane's linked docs (relative links from the finalization package)
python3 - <<'EOF'
import pathlib, re
root = pathlib.Path("opensource")
pkg = root/"docs/incoming/wifi-upstream-final"
bad = []
for md in pkg.glob("*.md"):
    for m in re.finditer(r"\[[^\]]*\]\(([^)#]+)", md.read_text()):
        t = (md.parent/m.group(1)).resolve()
        if m.group(1).startswith(("http", "mailto")): continue
        if not t.exists(): bad.append((md.name, m.group(1)))
print("broken:", bad if bad else "none")
EOF
```
