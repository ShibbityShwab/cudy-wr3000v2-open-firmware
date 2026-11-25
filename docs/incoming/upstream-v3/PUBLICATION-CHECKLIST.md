# Publication checklist - v3 pass (2026-11-29)

**What this is.** The publication-readiness checks run by this lane, the fixes
it applied, and the owner-only actions that remain.  All checks are
**read-only** (no network, no file writes by the checker, no history rewrite,
no push).  **Publication is still NOT READY and is an owner decision.**

## 1. Result

`cd opensource && bash tools/publish-prep-check.sh` (dry run):

| run | summary line (checker counter) | emitted lines |
| --- | --- | --- |
| 2026-11-27 finalization (recorded in `../wifi-upstream-final/PUBLICATION-CHECKS.md`) | 15 PASS / 9 FAIL / 8 WARN | - |
| this session, before fixes | **13 PASS / 10 FAIL / 9 WARN** | - |
| this session, after fixes | **18 PASS / 6 FAIL / 8 WARN** | 23 PASS / 12 FAIL / 9 WARN |

The checker's summary counter undercounts because several checks run inside
command-substitution subshells (their `PASS_COUNT`/`FAIL_COUNT` increments are
lost); the emitted lines are the authoritative count.  Fixing that would need
the checker to spill counters to a temp file, which would break its documented
"no file is created" guarantee - so it is **documented here, not changed**.

Why the state was worse than the 2026-11-27 record: five newer lane
self-tests contained the literal `BEGIN ... PRIVATE KEY` strings they search
for (false-positive credential hits in the tree scan) and new links had been
added (77 broken relative links in 9 files; the 2026-11-27 record said 23 in
6).  This lane fixed both classes.

## 2. Fixes applied by this lane (host-side)

| check | fix |
| --- | --- |
| A required files / licence texts | added **`COPYING`** - canonical GPL-2.0 text (the SPDX text from the kernel tree) with the repository licence-split note; checker: "COPYING is the GPL-2.0 text" PASS |
| A NOTICE links | `NOTICE.md` now references `docs/PUBLISH-PREP.md` (blocker map / release sequence) alongside PUBLISH-READINESS/GPL-REQUEST; checker NOTICE check PASS |
| B DCO convention | added **`CONTRIBUTING.md`** - licence split, SPDX convention, provenance statement ("GPL-compatible reverse engineering, not clean-room"), **Developer Certificate of Origin** + `Signed-off-by` convention, no-vendor-bytes/no-secrets rules; checker "CONTRIBUTING.md documents the DCO sign-off convention" PASS |
| C provenance | unchanged - the GPL-2.0 reverse-engineering statement was already present in README/NOTICE (PASS); the positive "clean-room" wording FAIL stays owner-only (section 3) |
| F secrets (tracked tree) | reworded the false positives in five lane self-tests so the literal key-header strings no longer appear in the tree: `docs/incoming/wifi-data-tx/tools/run-host-check.sh`, `docs/incoming/wifi-sta-ops/tools/run-host-check.sh`, `docs/incoming/wifi-final-image/tools/selftest.sh`, `docs/incoming/wifi-image-integration/tools/selftest.sh`, `docs/incoming/wifi-image-polish/tools/validate.py`.  The checks still test for real key material (patterns built at run time: `BEGIN RSA PRIVATE[ ]KEY` in BRE greps, string concatenation in Python).  Shell syntax (`bash -n`) and `py_compile` verified; the checker's **tree** scan is now PASS (the **history** scan still finds the old versions - owner purge). |
| G relative markdown links | fixed **all 77 broken links in 9 files** with the idempotent [tools/fix-publication-links.py](tools/fix-publication-links.py): parent-notebook escaping links become code text; same-directory typos corrected; the closeout proposed README gets the correct `../../../` prefix.  Checker: **"all 1395 relative markdown links resolve"** PASS |
| submission records | DTS-0001 hash/payload, L1 56-patch status, DTB measurements and the v3 validator record refreshed in `05-release-line.md`, `06-dts-series-dependency.md`, `submission/README.md`, `submission/MANIFEST.sha256` (105/105 verified) and `validation/sha256sums.txt` (all entries verify). |

## 3. Remaining FAILs - owner-only / history (not fixable in a host lane)

1. **Shallow clone** - the history scan cannot see before the shallow boundary;
   owner action: `git fetch --unshallow` on a full clone, then re-run the
   checker before any publication decision.
2. **42 of 108 `lab/` kernel-source files lack `SPDX-License-Identifier`.**
   Do NOT blanket-add: those files include vendor-derived build glue and parser
   material; each needs a provenance decision first.  The rest of `lab/` uses
   GPL-2.0/GPL-2.0-only correctly (checker PASS).
3. **52 of 55 `lab/` C/H/S files have no Copyright line.**  Needs the
   copyright-holder convention (CONTRIBUTING proposes the header format) and an
   owner decision on the holder string.
4. **356 of 356 commits have no `Signed-off-by`.**  History rewrite or the
   curated-repository path - owner only.  New commits must be `git commit -s`
   from now on (CONTRIBUTING.md).
5. **Positive `clean-room` claim in `README.md`** (checker flags un-negated
   occurrences; lines 1, 5, 12, 23, 46, 124 - `README.md` is the closeout
   lane's proposed root README).  Suggested wording: replace "clean-room
   OpenWrt + Wi-Fi bring-up" with "reverse-engineering OpenWrt + Wi-Fi
   bring-up"; replace "reverse-engineering and clean-room bring-up project"
   with "reverse-engineering bring-up project"; scope any remaining
   "clean-room" mention to the independently written host code with an explicit
   "not clean-room" qualifier.  Owner wording decision.
6. **History objects** - 10 vendor `.o` blobs reachable from `master` and both
   `origin/*` refs, one `__pycache__/*.pyc` blob, and the historical
   credential-pattern self-tests.  Fix = history purge/rewrite (owner, see
   `docs/PUBLISH-PURGE-RUNBOOK.md`) or publish from fresh curated repositories
   (recommended path, `docs/PUBLISH-PREP.md` section C).
7. **4 tracked `.dts` copies** (`docs/soc/luofu-r116-pinned.dts`,
   `docs/soc/luofu-r116.dts`, `lab/luofu-kernel/dts/luofu-r116.dts`,
   `target-skeleton/dts/luofu-r116.dts`).  Owner must collapse to one
   single source (or mark the historical copies clearly / exclude them from the
   published tree).  DTS-0001 is a **patch**, not a `.dts` copy, and this lane
   deliberately added no new `.dts` file.

## 4. Remaining WARNs

dirty worktree at scan time (expected: other lanes are writing concurrently;
the release candidate must be a clean checkout), narrow fetch refspec, HEAD is
ahead of origin (unpushed work), no dedicated `PROVENANCE.md` (the statement
lives in README/NOTICE; draft in PUBLISH-PREP 6.3), no sent GPL-request copy
(the dated no-reply record is the accepted alternative), vendor-derived build
glue still tracked under `lab/luofu-kernel/nand/` (3 files), 157 unreachable
objects in the local object DB (purge residue - owner-approved gc), target tree
still labelled SCHEMATIC/UNTESTED (should read "not upstream-accepted"),
inactive device profile file (`profiles/cudy_wr3000-v2.mk.unused`).

## 5. Re-run (host-only, read-only)

```sh
cd opensource
bash tools/publish-prep-check.sh                 # dry run; expect FAIL (owner items)
# full list of remaining blocker text:
bash tools/publish-prep-check.sh | grep -E '^FAIL|^WARN'
# verifiers used by this lane
cd upstream/series-6.18 && python3 tools/gen-series.py --check
cd ../.. && python3 docs/incoming/upstream-v3/tools/fix-publication-links.py --check   # expect 0
```

Publication remains a **human decision**: the checker's own header says a PASS
is not a publication decision, the owner must first close the history/licence
items and choose the publication path, and nothing in this lane was pushed.
