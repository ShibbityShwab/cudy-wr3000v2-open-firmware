# PUBLISH-PURGE-RUNBOOK - owner-only history purge for the legacy open-firmware repository

**Status: PREPARED and DRY-RUN VALIDATED (2026-11-21); NOT EXECUTED. Owner-only.**
This runbook rewrites the public history of
`ShibbityShwab/cudy-wr3000v2-open-firmware` (the legacy repo) to remove the ten vendor TR6560
BSP object files from every ref. It is destructive by design, is not a device action, and must not
be run by an agent or by anyone without explicit, dated owner authorisation.

**Companions:** `PUBLISH-PREP.md` (strategy, blockers B0/B1, curated-repo path),
`PUBLISH-READINESS.md` (legacy checklist, item 1 remains unchecked until this purge is done
and verified), `../tools/publish-prep-check.sh` (read-only checker). Nothing in this runbook is
executed by writing the file; the dry run described in Appendix B ran only inside throwaway
directories and never contacted a remote for a write.

**Evidence grades:** `[V]` verified by a local command, a fresh clone, or a fetched public
record; `[I]` inferred; `[U]` unknown; `[X]` refuted. Re-audit the live refs at
the start of execution (Phase P0); this page's SHA values are a snapshot.

---

## 0. What this runbook does, and what it does not

Does:

1. Takes a private, offline backup of both public branches of the legacy repo (all refs, all
   objects, plus a refs/object inventory and hashes).
2. Works on a **fresh, full (`--no-single-branch`) clone**, never on the shallow local
   submodule and never on the curated repos.
3. Rewrites the history with `git filter-repo --force --invert-paths` for the **ten exact
   vendor object paths** listed in Appendix A, plus one additional accidental binary path (a
   committed Python bytecode file, Appendix A row 11): after the rewrite **no binary blob of any
   kind** remains in history. Every other path is preserved.
4. Verifies, with hard gates and explicit HOLD/ABORT points, that all eleven old blob ids are absent
   from every ref and from the rewritten object database, and that no other content changed.
5. Force-pushes the rewritten `master` and deletes **or** rewrites the second branch
   (`omo/phase22-hccaccept`), verifies from a fresh clone of the remote, and requests a
   GitHub cached-view purge.
6. Records the evidence needed for the owner's release/audit trail.

Does not:

- touch the router, UART, TFTP or any fire lane (the publication path is host-only);
- rewrite the root notebook, the two curated repositories, or the local submodule working clone;
- remove files from the current published HEAD of `master` (that tree is already clean);
  it removes the eleven objects from **history**; the vendor objects disappear from the other
  branch's tree;
- archive, privatise, delete or recreate the repository (a separate containment decision);
- guarantee that copies already downloaded, third-party clones, search-engine caches or GitHub
  cached views are erased. The cache-purge request and residual-risk statement cover what is
  possible;
- touch secrets. The ten vendor objects are third-party binary content, not credentials.

---

## 1. Audit snapshot (2026-11-21, before any change)

All values below are `[V]` from a fresh full clone, `git ls-remote` and the public
GitHub API on 2026-11-21 ~15:00 +07 (08:00Z). Re-check in P0; if the live values differ, update
`EXPECTED_MASTER`/`EXPECTED_BRANCH` and re-run the dry-run gates before proceeding.

| item | value |
| --- | --- |
| repo URL | `https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git` (public, not archived, 0 forks) |
| `refs/heads/master` | `5b401c1230d416ccd0bfe21b102963b60798cc2a` (2026-11-21 15:07 +07, "dts: adopt boot-tested TWD level-high interrupt encoding (upstream blocker #6 closed)") |
| `refs/heads/omo/phase22-hccaccept` | `15703f5ee222b898191ccd90ae1fcb0e24c179b5` (2026-10-12, ancestor of master: 162 commits behind) |
| default branch | `master`; both branches unprotected |
| total commits reachable | 795 |
| object lines in `git rev-list --objects --all` | 6,299 |
| vendor `.o` blobs reachable anywhere | **10** (Appendix A rows 1-10) |
| all binary blobs in history | **11**: the ten vendor `.o` plus one accidental Python bytecode file (Appendix A row 11) |
| master tree | 706 files, **0 vendor objects** (the HEAD cleanup already landed) |
| second-branch tree | 434 files, **10 vendor objects** (the branch still publishes them) |
| branch serving a vendor object | `raw.githubusercontent.com/.../omo/phase22-hccaccept/lab/luofu-kernel/nand/triductor/tri_fmc.o` returns HTTP 200, 30,592 B `[V]` |
| master serving the same path | HTTP 404 (path absent from master tree) `[V]` |
| old blob ids served by GitHub API | `GET /repos/.../git/blobs/<sha>` returns 200 for the old ids before purge `[V]` |
| local submodule clone | `opensource/` is **shallow** (2 boundaries, 102 visible commits of 795) - never use it as the purge source `[V]` |
| PUBLISH-READINESS item 1 | unchecked, open (correct: the purge has not run) |

**Refs move.** During this re-audit the master tip advanced from `0c82cb8...` to `6e35c29...` and
then to `5b401c12...` (concurrent lanes pushed new commits every few minutes). Phase P0 is a mandatory
re-audit: if the live SHAs differ from
the environment block below, update them and re-run the dry-run gates (Appendix B) before P4.

The ten vendor objects are the TR6560 BSP vendor build outputs (DWARF path
`/home1/develop/release/3.1.0-rc7/release_THG6500-TAX2_3.1.0-rc7/.../linux-5.10.138`), not
our work. Appendix A lists paths, sizes and blob ids.

---

## 2. Non-negotiable pre-flight checklist (do not start a phase before its box is ticked)

Owner completes this block in writing (date + initials) in the private release log.

| # | condition | how to check | HOLD/ABORT if not met |
| --- | --- | --- | --- |
| P-01 | Owner authorisation recorded, dated, naming **this** repo and the ten paths | written approval in the private log | H0: do not start |
| P-02 | Owner has push/admin rights on the legacy repo and 2FA available | log in to GitHub as the owner; `git push --dry-run` (with no ref changes) | H0: stop |
| P-03 | No other lane is pushing to the legacy repo during the window | `git ls-remote "$REPO"` twice, minutes apart; announce freeze-request to collaborators | H1: reschedule |
| P-04 | Offline/private backup medium available (not a public host, not a repo) | mount/locate it; it must hold at least 10 MB + evidence | H1: stop |
| P-05 | `git-filter-repo` available or installable (distro package, pipx, or project venv`--target`) | `git filter-repo --version`; see P3 install block | H2: install first |
| P-06 | Live refs equal this snapshot (master/branch SHAs, 795 commits, 11 binary blobs: 10 vendor + 1 accidental) | P0 audit commands | H1: update runbook values + re-dry-run |
| P-07 | Backup created and `git bundle verify` passes; refs/object inventory + sha256 recorded | P1 | H1: no rewrite |
| P-08 | Fresh full clone made, not shallow, both branches present | P2 | H1: stop |
| P-09 | Dry-run gates (Appendix B) reproduced on the same refs if refs changed since 2026-11-21 | P2/P3 local run | H2: stop |
| P-10 | Cache-purge channel available (GitHub Support; owner account) and request text ready | P8 | H7: do not declare done |
| P-11 | Rollback plan (section 8) read; the backup copy verified again immediately before push | P6 HOLD | H5: abort |
| P-12 | The owner has read the "what is not possible" statement (section 1 of PUBLISH-PREP / section 8.4 here) | written acknowledgement | H0 |

---

## 3. Environment block (paste once, at the start of the owner session)

All commands below assume this block has been pasted into a shell with a clean environment. It is
deliberately conservative: `set -euo pipefail`, no automatic writes, no hidden network writes, and a
`HOLD` helper that requires a typed word at every risky point. If any `HOLD` prompt is not
answered with the exact word `HOLD`, the script exits without doing the next action.

```sh
set -euo pipefail
export REPO="https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git"
export WORK="/offline/publish-purge-20261121"     # OWNER: an offline/private absolute path
export EVID="$WORK/evidence"
export EXPECTED_MASTER="5b401c1230d416ccd0bfe21b102963b60798cc2a"
export EXPECTED_BRANCH="15703f5ee222b898191ccd90ae1fcb0e24c179b5"
export EXPECTED_COMMITS="795"
export EXPECTED_BLOBS="10"
export EXTRA_PATH="target-skeleton/tools/__pycache__/fip-slice.cpython-314.pyc"
export EXTRA_SHA="87332fdce8e4d0708437fb2d558895ea068d46bb"
mkdir -p "$WORK" "$EVID"

HOLD() {
  printf '\n============================================================\n'
  printf 'HOLD: %s\n' "$*"
  printf 'Type HOLD and press Enter to continue; anything else aborts.\n'
  printf '============================================================\n'
  read -r ANSWER
  [ "$ANSWER" = "HOLD" ] || { echo "ABORTED at HOLD"; exit 90; }
}
ABORT() { printf '\nABORT: %s\n' "$*"; printf 'Do not proceed. Record the state and ask the owner.\n'; exit 91; }
EXPECT_EQ() { # EXPECT_EQ <label> <actual> <expected>
  if [ "$2" = "$3" ]; then printf 'PASS  %s = %s\n' "$1" "$2"
  else ABORT "$1 mismatch: got [$2], expected [$3]"; fi
}
```

From here on, every phase writes its capture files under `$EVID/`. These captures never
leave the private backup; they contain old SHA values and object lists, not secrets.

---

## 4. Execution phases (owner-only; run one block at a time)

The phases are ordered; each has a HOLD that must be passed before the next block. If any command
fails, the shell exits (the environment block sets `set -euo pipefail`) and nothing further is
attempted. Every phase writes its capture to `$EVID`; keep the files with the private
backup.

### Phase P0 - freeze and re-audit the live refs

```sh
mkdir -p "$EVID"
git ls-remote "$REPO" | tee "$EVID/ls-remote-before.txt"
LIVE_MASTER=$(git ls-remote "$REPO" refs/heads/master | cut -f1)
LIVE_BRANCH=$(git ls-remote "$REPO" refs/heads/omo/phase22-hccaccept | cut -f1)
EXPECT_EQ "master ref" "$LIVE_MASTER" "$EXPECTED_MASTER"
EXPECT_EQ "second-branch ref" "$LIVE_BRANCH" "$EXPECTED_BRANCH"
HOLD "P0: refs match the audited snapshot; the freeze is announced; no other lane will push during the window; the owner is at the keyboard"
```

If the refs differ from the snapshot: stop, update `EXPECTED_MASTER`/`EXPECTED_BRANCH`
and `EXPECTED_COMMITS` in the environment block, repeat the local dry-run gates (Appendix B)
against the new refs, and only then continue. **Never proceed with stale expected SHAs**: the
force-with-lease in P6 exists to catch exactly this, and a mismatch there is a HOLD, not a retry.

### Phase P1 - offline/private backup (do this before anything else)

The mirror and bundle contain the ten vendor objects. They are **third-party GPL object code**:
keep the backup offline/private, never commit it, never push it, never attach it.

```sh
git clone --mirror "$REPO" "$WORK/pre-mirror.git"
git -C "$WORK/pre-mirror.git" bundle create "$WORK/pre-purge-all.bundle" --all
git -C "$WORK/pre-mirror.git" bundle verify "$WORK/pre-purge-all.bundle" \
  | tee "$EVID/bundle-verify.txt"
sha256sum "$WORK/pre-purge-all.bundle" | tee "$EVID/pre-purge-all.bundle.sha256"
git -C "$WORK/pre-mirror.git" for-each-ref \
  --format='%(refname) %(objectname)' | tee "$EVID/refs-before.txt"
git -C "$WORK/pre-mirror.git" rev-list --objects --all > "$EVID/objects-before.txt"
awk '/\.(o|ko)$/ {print $1, $2}' "$EVID/objects-before.txt" \
  | sort -k2 > "$EVID/vendor-objects-before.txt"
cut -d' ' -f1 "$EVID/vendor-objects-before.txt" \
  | sort -u > "$EVID/vendor-shas-before.txt"
EXPECT_EQ "vendor object paths" "$(wc -l < "$EVID/vendor-objects-before.txt" | tr -d ' ')" "$EXPECTED_BLOBS"
# extra accidental binary found in the 2026-11-21 re-audit (Appendix A row 11)
git -C "$WORK/pre-mirror.git" cat-file -e "$EXTRA_SHA" \
  || ABORT "P1: extra path blob not found in the backup; re-audit before rewriting"
cat "$EVID/vendor-objects-before.txt" > "$EVID/purged-objects-before.txt"
printf '%s\n' "$EXTRA_PATH" >> "$EVID/purged-objects-before.txt"
sort -k2 "$EVID/purged-objects-before.txt" -o "$EVID/purged-objects-before.txt"
cat "$EVID/vendor-shas-before.txt" > "$EVID/purged-shas-before.txt"
printf '%s\n' "$EXTRA_SHA" >> "$EVID/purged-shas-before.txt"
sort -u "$EVID/purged-shas-before.txt" -o "$EVID/purged-shas-before.txt"
EXPECT_EQ "purged object paths (10 vendor + 1 accidental)" "$(wc -l < "$EVID/purged-objects-before.txt" | tr -d ' ')" "11"
sha256sum "$EVID/objects-before.txt" "$EVID/vendor-objects-before.txt" \
  | tee "$EVID/baseline.sha256"
HOLD "P1: backup bundle verifies; vendor-objects-before (10), purged-objects-before (11) and purged-shas-before are captured; the backup is on the offline/private medium"
```

### Phase P1c - binary inventory must match the audited set (run in the P2 clone, before P4)

Because the legacy tree is still receiving commits, re-check that no new binary blob has appeared
since the 2026-11-21 audit. In the fresh P2 clone:

```sh
cd "$WORK/rewrite"
if [ -f tools/publish-prep-check.sh ]; then
  bash tools/publish-prep-check.sh > "$EVID/checker-before-rewrite.log" 2>&1 || true
  sed -n '/== E\. git history/,/== F\./p' "$EVID/checker-before-rewrite.log" \
    | tee "$EVID/binary-inventory-before.txt"
  HOLD "P1c: the non-vendor binary list above must contain ONLY target-skeleton/tools/__pycache__/fip-slice.cpython-314.pyc, and the all-history binary count must be 11. If a new binary appeared, add it to the EXTRA path/sha list and re-run the local dry run before P4"
else
  ABORT "P1c: checker missing from the fresh clone; cannot verify the binary inventory"
fi
```

If the bundle does not verify, or the vendor list is not exactly ten, **ABORT**: no rewrite is
allowed without a verified backup.

### Phase P2 - fresh full clone (never the local shallow submodule)

```sh
git clone --no-single-branch "$REPO" "$WORK/rewrite"
cd "$WORK/rewrite"
EXPECT_EQ "shallow clone" "$(git rev-parse --is-shallow-repository)" "false"
git branch --track omo/phase22-hccaccept origin/omo/phase22-hccaccept
git rev-list --count --all | tee "$EVID/commit-count-before.txt"
EXPECT_EQ "commit count" "$(cat "$EVID/commit-count-before.txt")" "$EXPECTED_COMMITS"
git for-each-ref --format='%(refname) %(objectname)' | tee "$EVID/rewrite-refs-before.txt"
for ref in $(git for-each-ref --format='%(refname)'); do
  echo "$ref vendor=$(git rev-list --objects "$ref" | grep -cE '\.(o|ko)$')"
done | tee "$EVID/per-ref-before.txt"
echo "master_tree_vendor=$(git ls-tree -r --name-only master | grep -cE '\.(o|ko)$')" \
  | tee "$EVID/tree-vendor-before.txt"
echo "branch_tree_vendor=$(git ls-tree -r --name-only omo/phase22-hccaccept | grep -cE '\.(o|ko)$')" \
  | tee -a "$EVID/tree-vendor-before.txt"
EXPECT_EQ "master tree vendor objects" "$(sed -n 's/^master_tree_vendor=//p' "$EVID/tree-vendor-before.txt")" "0"
EXPECT_EQ "branch tree vendor objects" "$(sed -n 's/^branch_tree_vendor=//p' "$EVID/tree-vendor-before.txt")" "10"
HOLD "P2: fresh full clone has both branches, 795 commits and exactly ten reachable vendor paths; baseline captured"
```

Any failure here means the clone or the assumptions are wrong. Do not "fix" a mismatch by
continuing; ABORT and re-audit.

### Phase P3 - install/verify `git-filter-repo` (no system-wide changes required)

Use a distro package or pipx if available. The dry run used the project venv's pip with a
local `--target` install, which needs no system changes and keeps the tool inside the
offline work directory:

```sh
if ! git filter-repo --version >/dev/null 2>&1; then
  mkdir -p "$WORK/tools"
  PYTHON="python3"
  if ! "$PYTHON" -m pip --version >/dev/null 2>&1; then
    echo "python3 has no pip; set PYTHON to a pip-capable python (the project virtualenv's python works) and re-run P3"
    ABORT "pip unavailable for git-filter-repo install"
  fi
  "$PYTHON" -m pip install --quiet --disable-pip-version-check \
    --target "$WORK/tools" git-filter-repo
  export PYTHONPATH="$WORK/tools"
  export PATH="$WORK/tools/bin:$PATH"
fi
git filter-repo --version | tee "$EVID/filter-repo-version.txt"
HOLD "P3: git filter-repo runs and its version is captured"
```

The tool normally removes the `origin` remote after a successful rewrite (a safety default);
P6 re-adds it explicitly. Do not be surprised by this, and do not add the remote back earlier:
the rewrite must not be able to push accidentally.

### Phase P4 - rewrite the history (throwaway clone; no remote involved)

Run in the fresh clone from P2. The working tree must be clean; the rewrite removes only the eleven
paths in Appendix A and rewrites every commit that contained them.

```sh
cd "$WORK/rewrite"
git status --porcelain | tee "$EVID/status-before-rewrite.txt"
test -s "$EVID/status-before-rewrite.txt" && ABORT "working tree not clean before rewrite"

git filter-repo --force --invert-paths \
  --path lab/luofu-kernel/nand/parsers/ofpart_triductor.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_hal_nand.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_bbt.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_check.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_drv_yyxxxx.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_mtd.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_spi_nand_drv.o \
  --path lab/luofu-kernel/nand/triductor/tri_fmc.o \
  --path lab/luofu-kernel/nand/triductor/tri_mtd_parts.o \
  --path "$EXTRA_PATH" \
  2>&1 | tee "$EVID/filter-repo-rewrite.log"

git for-each-ref --format='%(refname) %(objectname)' | tee "$EVID/refs-after-rewrite.txt"
cp .git/filter-repo/commit-map "$EVID/commit-map.txt"
wc -l "$EVID/commit-map.txt" | tee "$EVID/commit-map-lines.txt"
sha256sum "$EVID/commit-map.txt" | tee "$EVID/commit-map.sha256"
HOLD "P4: rewrite completed; commit-map captured; expect that origin is now removed (checked in P6)"
```

If filter-repo exits non-zero, or the commit-map is missing/short, **ABORT**. Do not attempt to
resume or rerun on a half-written clone; delete `$WORK/rewrite` and start P2 again from the
verified backup.

---

## 5. Verification gates - all must pass before any push

Run the gates in one bash block. Every gate prints PASS/FAIL and any FAIL calls `ABORT`,
which stops the shell. The HOLD at the end is the final gate before P6.

```sh
cd "$WORK/rewrite"

# V1 - no vendor object path is reachable from any ref
echo '--- V1 ---'
V1=$(git rev-list --objects --all | grep -E '\.(o|ko)$' | wc -l | tr -d ' ' || true)
EXPECT_EQ "V1 reachable vendor paths" "$V1" "0" | tee "$EVID/gate-V1.txt"

# V2 - per-ref audit: every local ref reaches zero vendor paths
echo '--- V2 ---'
for ref in $(git for-each-ref --format='%(refname)'); do
  n=$(git rev-list --objects "$ref" | grep -cE '\.(o|ko)$' || true)
  echo "$ref vendor=$n"
  [ "$n" = "0" ] || ABORT "V2: ref $ref still reaches $n vendor paths"
done | tee "$EVID/gate-V2.txt"

# V3 - none of the eleven old blob ids is reachable
echo '--- V3 ---'
git rev-list --objects --all | awk '{print $1}' | sort -u > "$EVID/reachable-shas-after.txt"
V3=$(comm -12 "$EVID/purged-shas-before.txt" "$EVID/reachable-shas-after.txt" | wc -l | tr -d ' ')
EXPECT_EQ "V3 old-blob ids (11) still reachable" "$V3" "0" | tee "$EVID/gate-V3.txt"

# V4 - old blob ids are absent from the object database after reflog expire + gc
echo '--- V4 ---'
git reflog expire --expire=now --all
git gc --prune=now --quiet
PRESENT=0
while read -r sha; do
  if git cat-file -e "$sha" 2>/dev/null; then echo "STILL PRESENT $sha"; PRESENT=$((PRESENT + 1)); fi
done < "$EVID/purged-shas-before.txt"
EXPECT_EQ "V4 old blob ids (11) present in object database" "$PRESENT" "0" | tee "$EVID/gate-V4.txt"

# V5 - history shape: commit count preserved; master tree unchanged; branch still an ancestor
echo '--- V5 ---'
EXPECT_EQ "V5 commit count" "$(git rev-list --count --all)" "$EXPECTED_COMMITS" | tee "$EVID/gate-V5a.txt"
OLD_MASTER_TREE=$(git --git-dir="$WORK/pre-mirror.git" rev-parse "$EXPECTED_MASTER^{tree}")
NEW_MASTER_TREE=$(git rev-parse "master^{tree}")
EXPECT_EQ "V5 master tree id" "$NEW_MASTER_TREE" "$OLD_MASTER_TREE" | tee "$EVID/gate-V5b.txt"
git merge-base --is-ancestor omo/phase22-hccaccept master \
  && echo "V5 branch is an ancestor of master: PASS" \
  || ABORT "V5: rewritten second branch is not an ancestor of master"
# note: the accidental binary is not in either tip tree, so V5b (master tree id) is expected to stay
# identical; if a future extra binary IS in a tip tree, V6's filtered-listing check is authoritative

# V6 - content equivalence: remove the eleven known paths from the old listings; diff must be empty
echo '--- V6 ---'
git --git-dir="$WORK/pre-mirror.git" ls-tree -r --full-tree "$EXPECTED_MASTER" \
  | grep -vF -f <(awk '{print $2}' "$EVID/purged-objects-before.txt") | sort \
  > "$EVID/old-master-filtered.txt"
git ls-tree -r --full-tree master | sort > "$EVID/new-master.txt"
diff -u "$EVID/old-master-filtered.txt" "$EVID/new-master.txt" \
  > "$EVID/gate-V6-master.diff" || ABORT "V6: rewritten master differs outside the eleven purged paths"
git --git-dir="$WORK/pre-mirror.git" ls-tree -r --full-tree "$EXPECTED_BRANCH" \
  | grep -vF -f <(awk '{print $2}' "$EVID/purged-objects-before.txt") | sort \
  > "$EVID/old-branch-filtered.txt"
git ls-tree -r --full-tree omo/phase22-hccaccept | sort > "$EVID/new-branch.txt"
diff -u "$EVID/old-branch-filtered.txt" "$EVID/new-branch.txt" \
  > "$EVID/gate-V6-branch.diff" || ABORT "V6: rewritten second branch differs outside the eleven purged paths"
echo "V6 listing diffs: PASS (empty)" | tee "$EVID/gate-V6.txt"

# V7 - author/subject metadata: only rewritten short-hash references inside commit messages may differ
echo '--- V7 ---'
git --git-dir="$WORK/pre-mirror.git" log --format='%an|%ae|%ad|%s' "$EXPECTED_MASTER" \
  > "$EVID/metadata-old.txt"
git log --format='%an|%ae|%ad|%s' master > "$EVID/metadata-new.txt"
diff -u "$EVID/metadata-old.txt" "$EVID/metadata-new.txt" > "$EVID/gate-V7.diff" || true
cat "$EVID/gate-V7.diff"
HOLD "V7: read the diff above. It must contain only rewritten short-commit-hash references inside commit-message text (the dry run found exactly one). Any author, date or subject change is a HOLD, not a pass"

# V8 - commit-map completeness and mapping of the two audited refs
echo '--- V8 ---'
EXPECT_EQ "V8 commit-map lines (commits + header)" "$(wc -l < "$EVID/commit-map.txt" | tr -d ' ')" \
  "$((EXPECTED_COMMITS + 1))" | tee "$EVID/gate-V8a.txt"
grep -E "^($EXPECTED_MASTER|$EXPECTED_BRANCH) " "$EVID/commit-map.txt" \
  | tee "$EVID/gate-V8b.txt" || ABORT "V8: commit-map does not map both audited refs"
[ "$(wc -l < "$EVID/gate-V8b.txt" | tr -d ' ')" = "2" ] || ABORT "V8: commit-map does not map both audited refs"
# V9 - independent read-only checker: no binary blob of any kind remains in history
echo '--- V9 ---'
if [ -f tools/publish-prep-check.sh ]; then
  bash tools/publish-prep-check.sh > "$EVID/checker-rewritten.log" 2>&1 || true
  grep -q 'PASS  no binary blob in any reachable history' "$EVID/checker-rewritten.log" \
    || ABORT "V9: checker does not report a binary-blob-free history"
  echo "V9 checker binary-blob gate: PASS" | tee "$EVID/gate-V9.txt"
else
  ABORT "V9: checker not present in the rewritten tree; run it separately before any push"
fi
HOLD "V1-V9 all pass; the commit map and gate evidence are captured; proceed to the branch/push decision"
```

If any gate fails: **ABORT**. The public refs are still untouched at this point, so the safe action
is to delete `$WORK/rewrite`, keep the backup, and re-run the dry run against the current
refs. Never force the failing state through the push phases.

Optional but recommended read-only check on the rewritten clone (copy the checker from the local
checkout; it is not part of the rewritten history):

```sh
if [ -x tools/publish-prep-check.sh ]; then
  ./tools/publish-prep-check.sh > "$EVID/checker-rewritten.log" 2>&1 || true
else
  echo "checker not present in this tree; run the read-only checker from the working checkout" \
    | tee "$EVID/checker-note.txt"
fi
sed -n '/== E\. git history/,/== F\./p' "$EVID/checker-rewritten.log" | tee "$EVID/checker-section-E.txt"
HOLD "Checker section E above must show the history as clean (no vendor objects on any ref). Other FAILs are the known legacy content items and are expected here"
```

---

## 6. Branch and push plan (the only steps that change the public remote)

### Phase P5 - decide the second branch, in writing

The second branch `omo/phase22-hccaccept` is an **ancestor** of master (15703f5 is 162
commits behind 0c82cb8) and is stale. Two valid options; choose exactly one and record it in the
private log before P6.

| option | when | P6 action |
| --- | --- | --- |
| **A - delete (recommended)** | the branch has no independent value; it is an old snapshot and its tree is the only public tree that still serves vendor objects | delete it after the master force-push |
| B - rewrite and keep | a historical branch must remain visible | force-update it to its rewritten commit after the master push |
| C - stop | owner wants neither | ABORT before any push; do not run P6 |

The dry run validated both A and B against local bare remotes (Appendix B).

### Phase P6 - re-add origin, re-check, and force-push with explicit leases

```sh
cd "$WORK/rewrite"

# filter-repo removed origin on purpose; add it back explicitly only for this phase
git remote add origin "$REPO" 2>/dev/null || true
git remote -v | tee "$EVID/remote-before-push.txt"

# Re-audit the live remote: nobody may have pushed while we were rewriting
LIVE_MASTER=$(git ls-remote "$REPO" refs/heads/master | cut -f1)
LIVE_BRANCH=$(git ls-remote "$REPO" refs/heads/omo/phase22-hccaccept | cut -f1)
EXPECT_EQ "live master before push" "$LIVE_MASTER" "$EXPECTED_MASTER"
EXPECT_EQ "live branch before push" "$LIVE_BRANCH" "$EXPECTED_BRANCH"

# Re-verify the backup immediately before the irreversible step
git -C "$WORK/pre-mirror.git" bundle verify "$WORK/pre-purge-all.bundle" \
  | tee "$EVID/bundle-verify-just-before-push.txt"

NEW_MASTER=$(awk '$1 == "refs/heads/master" {print $2}' "$EVID/refs-after-rewrite.txt")
NEW_BRANCH=$(awk '$1 == "refs/heads/omo/phase22-hccaccept" {print $2}' "$EVID/refs-after-rewrite.txt")
EXPECT_EQ "new master (rewritten)" "$NEW_MASTER" "$(git rev-parse master)"
grep -q "^$EXPECTED_MASTER $NEW_MASTER " "$EVID/commit-map.txt" \
  || ABORT "P6: commit-map does not map old master to the new master"
HOLD "P6: the next two commands rewrite the public repository. Backup re-verified; refs re-audited; option (A/B) recorded. Type HOLD to push, anything else aborts"

# 6a. force master with an explicit lease against the audited old SHA
git push --force-with-lease=refs/heads/master:"$EXPECTED_MASTER" \
  origin refs/heads/master:refs/heads/master 2>&1 | tee "$EVID/push-master.log"

# 6b. branch handling - exactly ONE of the two blocks below, matching the P5 decision
# Option A (delete):
git push origin --delete omo/phase22-hccaccept 2>&1 | tee "$EVID/push-branch-delete.log"
# Option B (rewrite and keep):
# git push --force-with-lease=refs/heads/omo/phase22-hccaccept:"$EXPECTED_BRANCH" \
#   origin "$NEW_BRANCH":refs/heads/omo/phase22-hccaccept 2>&1 | tee "$EVID/push-branch-rewrite.log"

git ls-remote "$REPO" | tee "$EVID/ls-remote-after-push.txt"
HOLD "P6: push commands returned. Inspect ls-remote-after-push.txt and the two push logs before P7"
```

HOLD/ABORT semantics for P6:

- **[H] lease rejected** ("stale info" / "rejected"): the remote moved after P0. Do **not** retry
  with a guessed SHA. ABORT and return to P0 with fresh expected values. The remote is still in its
  old state (a rejected lease changes nothing).
- **[H] authenticated push denied**: fix the owner's credentials; do not embed a token in the
  command line or shell history. Nothing changed if the push was rejected.
- **[H] master push succeeded, branch operation failed**: the old branch is still reachable and
  still serves the ten objects. Immediately finish the branch operation (retry once you know why),
  or restore both refs from the backup (section 8) and contain. Do not leave this state pending.
- **[H] network lost mid-push**: re-run `git ls-remote "$REPO"` and compare with
  `$EVID/ls-remote-after-push.txt` expectations; treat any unexpected ref as a HOLD and
  go to section 8.

### Phase P7 - fresh-clone verification of the actual public remote

```sh
rm -rf "$WORK/verify-remote"
git clone --no-single-branch "$REPO" "$WORK/verify-remote"
cd "$WORK/verify-remote"
EXPECT_EQ "public clone shallow" "$(git rev-parse --is-shallow-repository)" "false"
EXPECT_EQ "public clone commit count" "$(git rev-list --count --all)" "$EXPECTED_COMMITS" \
  | tee "$EVID/verify-remote-commit-count.txt"

# V1/V2 again, but against the real remote
V1=$(git rev-list --objects --all | grep -E '\.(o|ko)$' | wc -l | tr -d ' ' || true)
EXPECT_EQ "public clone reachable vendor paths" "$V1" "0" | tee "$EVID/verify-remote-V1.txt"
for ref in $(git for-each-ref --format='%(refname)'); do
  n=$(git rev-list --objects "$ref" | grep -cE '\.(o|ko)$' || true)
  echo "$ref vendor=$n"
  [ "$n" = "0" ] || ABORT "P7: public ref $ref still reaches $n vendor paths"
done | tee "$EVID/verify-remote-per-ref.txt"

# old blob ids must not be present in the public clone
PRESENT=0
while read -r sha; do
  if git cat-file -e "$sha" 2>/dev/null; then echo "STILL PRESENT $sha"; PRESENT=$((PRESENT + 1)); fi
done < "$EVID/purged-shas-before.txt"
EXPECT_EQ "public clone old blob ids (11) present" "$PRESENT" "0" \
  | tee "$EVID/verify-remote-old-blobs.txt"

git for-each-ref --format='%(refname) %(objectname)' | tee "$EVID/verify-remote-refs.txt"
EXPECT_EQ "public master tip" "$(git rev-parse refs/remotes/origin/master)" "$NEW_MASTER" \
  | tee "$EVID/verify-remote-master-tip.txt"
HOLD "P7: the fresh clone of the public remote is clean (V1/V2/old-blob checks PASS) and the refs match the P6 capture"
```

Expected ref shapes: option A - only `refs/remotes/origin/master` (plus HEAD); option
B - `master` plus the rewritten `omo/phase22-hccaccept`. A deleted branch that
still appears, or a master tip different from `$NEW_MASTER`, is a HOLD.

At this point the git history is clean, but GitHub may still serve **cached/unreachable objects** by
old commit/blob URL. Record the state before asking support:

```sh
cd "$WORK"
while read -r sha; do
  code=$(curl -s -o /dev/null -w '%{http_code}' \
    "https://api.github.com/repos/ShibbityShwab/cudy-wr3000v2-open-firmware/git/blobs/$sha")
  echo "$sha $code"
done < "$EVID/vendor-shas-before.txt" | tee "$EVID/github-blob-status-before-purge.txt"
HOLD "P7: old blob URLs may still return 200 here - that is the cache/unreachable-object residue, not a rewrite failure; continue to the support purge"
```

### Phase P8 - GitHub Support cached-view purge (owner account)

Send the request below from the owner's account (support.github.com -> contact, or the support
email address in the owner's GitHub account). Do not open a public issue about this: it would point
more people at the old SHAs.

```text
Subject: Request to purge cached views after a history rewrite in
         ShibbityShwab/cudy-wr3000v2-open-firmware

Hello,

I am the owner of the repository ShibbityShwab/cudy-wr3000v2-open-firmware. On <DATE> I
force-pushed a rewritten history to remove ten third-party vendor GPL object files that had been
accidentally committed in <YYYY-MM> and were reachable on both public branches. The rewrite kept
all 795 commits and all other content; the repository has no forks, no pull requests and no tags.
The rewritten refs are: master <NEW_MASTER_SHA>, omo/phase22-hccaccept <deleted or NEW_BRANCH_SHA>.
The ten old blob ids are: <list from vendor-shas-before.txt>. The old commit ids were:
<old master and old branch SHA, plus the old refs listed in refs-before.txt>.

Please purge cached views / unreachable-object access for those old refs and blob ids so they are
no longer served through the GitHub UI or API. I have already rewritten and force-pushed the
repository and verified the new refs from a fresh clone.

Thank you,
<owner name / account>
```

Record the ticket/message id and any reply in `$EVID/support-purge.txt`. After support
confirms, re-run the blob-URL loop above; the expected result is `404` for every old blob id,
and the old commit pages should also return 404. If a 200 persists after the support reply,
follow up once with the ticket id; do not delete the repository as a "fix".

Residual risk (must be recorded, do not overclaim): copies already downloaded by third parties,
search-engine/archive caches, and third-party clones cannot be retracted by a force-push or a
cached-view purge. The rewrite stops future retrieval from this repository's refs; it is not an
erasure of the internet.

### Phase P9 - records and post-purge actions

1. Preserve `$EVID` and `$WORK/pre-mirror.git`/bundle on the offline/private medium;
   keep the bundle until the owner explicitly decides it can be destroyed. It contains the vendor
   objects and must never be committed, pushed or attached.
2. Update `PUBLISH-READINESS.md` item 1 to checked **only after** P7 and P8 pass; record
   the date, the old/new refs and the commit-map sha in the item's evidence note (this is the publish
   lane's edit, not part of the purge shell block).
3. Append a dated parent-ledger entry (owner/publish lane) with: old refs, new refs, commit-map
   sha256, old blob ids, verification results, support ticket id, residual-risk acceptance.
4. Decide separately (not part of this runbook): archive read-only vs make private the legacy repo,
   update its description, and update the root notebook's published submodule pointer. Those are
   containment decisions from `PUBLISH-PREP.md` section 5.
5. Do not create tags or releases on the rewritten history unless the owner asks for a specific
   annotated tag; the audited snapshot had no tags.
6. Do not delete `$WORK` while the support purge is pending; the evidence is needed to
   answer it.

---

## 7. Rollback plan

Rollback is owner-only and destructive. The public state before P6 is fully captured in the
verified mirror/bundle.

### R0 - before P6 (no public change yet)

Delete the rewrite clone and start over: `rm -rf "$WORK/rewrite" "$WORK/verify-remote"`.
The mirror/bundle and the evidence remain. Nothing on GitHub changed.

### R1 - P6 partially applied (one ref changed, one not; or an unexpected ref state)

Restore **both** old refs from the verified mirror, then contain:

```sh
# from the verified pre-purge mirror; this restores the old (contaminated) refs
git -C "$WORK/pre-mirror.git" push --force origin \
  refs/heads/master:refs/heads/master \
  refs/heads/omo/phase22-hccaccept:refs/heads/omo/phase22-hccaccept
git ls-remote "$REPO" | tee "$EVID/ls-remote-after-rollback.txt"
```

**Restoring the old refs re-exposes the ten vendor objects.** The moment the rollback is chosen,
apply containment in the same window: archive the repository read-only or make it private (owner
action), record the exposure window in the ledger, then re-run the dry run/runbook when ready.
Never leave a partially restored, contaminated public repo unattended.

### R2 - P6 succeeded but P7 verification fails

Do not restore the old history as the first reaction: restoring re-exposes the vendor objects.
Instead:

1. Record the exact failing gate and `git ls-remote` output.
2. If the failure is a wrong tip/ref shape: correct it by force-pushing the intended rewritten refs
   from `$WORK/rewrite` again (lease against the currently published value), then re-run P7.
3. If the rewritten content is wrong (V6-class failure found after push): decide with the owner
   between (a) fix-forward with a corrected rewrite from the backup and another force-push, or
   (b) rollback R1 + immediate containment. Either way the decision and the reason are recorded.
4. Do not run `git gc`, delete the repository, or delete the backup while diagnosing.

### R3 - backup unverifiable or missing

**ABORT before P4/P6.** No rewrite and certainly no push without a verified backup. If the
problem is discovered after a push (unlikely, because P6 re-verifies), treat it as R2 and keep all
remaining copies; the external evidence is then the GitHub state plus the support ticket.

### R4 - cache purge fails or is refused

The git history is still clean; the residual is cached/unreachable-object access. Follow up with
GitHub Support, keep the recorded 200/404 evidence, and state the residual honestly. A refused
cache purge is not a reason to delete the repository or to rewrite again.

### R5 - after a successful purge, the old objects are still served by third parties

Nothing in this runbook can retract copies already downloaded, search-engine or archive caches, or
third-party clones. Record it as accepted residual risk; do not repeat the rewrite.

---

## 8. Evidence checklist (private; keep with the offline backup)

| file | produced in | contains | check |
| --- | --- | --- | --- |
| `ls-remote-before.txt` | P0 | live refs before | matches expected SHAs |
| `pre-purge-all.bundle` + `.sha256` + `bundle-verify.txt` | P1/P6 | full backup + integrity | `git bundle verify` PASS |
| `refs-before.txt`, `objects-before.txt`, `baseline.sha256` | P1 | all old refs/objects/hashes | object list reproducible |
| `vendor-objects-before.txt`, `vendor-shas-before.txt` | P1 | the ten vendor blobs | exactly 10 |
| `purged-objects-before.txt`, `purged-shas-before.txt` (built from vendor + the extra binary) | P1 | the 11-blob purge set | exactly 11 |
| `commit-count-before.txt`, `per-ref-before.txt`, `tree-vendor-before.txt` | P2 | baseline shape | 795 commits; 10 vendor paths reachable on each ref; tree counts 0/10 |
| `filter-repo-version.txt`, `filter-repo-rewrite.log` | P3/P4 | tool version + rewrite log | version recorded; exit 0 |
| `commit-map.txt` + `commit-map.sha256` + `gate-V8b.txt` | P4/V8 | old-to-new mapping | 794 lines; both audited refs mapped |
| `gate-V1.txt` ... `gate-V7.diff` | P5 | verification evidence | every gate PASS; V7 diff reviewed |
| `checker-section-E.txt` (and full checker log) | P4 optional | independent read-only scan | no vendor object on any ref |
| `push-master.log`, `push-branch-delete.log` or `push-branch-rewrite.log` | P6 | what the remote accepted | expected ref update/rejection semantics |
| `ls-remote-after-push.txt` | P6 | remote refs after push | matches intended shape |
| `verify-remote-*.txt` | P7 | fresh-clone proof | V1/V2/old-blobs PASS; tip matches |
| `github-blob-status-before-purge.txt`, `support-purge.txt`, post-purge status | P7/P8 | cache residue + support record | 404 after support |
| owner authorisation + this checklist initialled | P-01/P-12 | approval trail | dated and signed |

Storage rules: the mirror/bundle and anything with object bytes stay **offline/private** (they are
third-party GPL object code). Evidence without object bytes may be kept in the private release log.
Never commit any of it, never attach it to a public issue, never paste old SHAs into a public
comment without the owner's decision.

---

## 9. Owner sign-off (fill in before P6; keep with the evidence)

```text
History purge sign-off

Date/time (local + UTC):
Repository: https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git
Owner account:
Authorisation reference (ledger entry / message):
Expected master before:       5b401c1230d416ccd0bfe21b102963b60798cc2a
Expected second branch before: 15703f5ee222b898191ccd90ae1fcb0e24c179b5
Backup bundle sha256:
Bundle verify result:          PASS / FAIL
Filter-repo version:
Branch decision (A delete / B rewrite):
HOLDs passed:                  P0 P1 P2 P3 P4 V7 P6 P7 P8
New master after:             (from commit-map / P4)
New second branch after:      (deleted / from commit-map)
P7 fresh-clone verification:  PASS / FAIL
Support purge ticket id:      (after P8)
Residual risk accepted:       copies already fetched / caches cannot be fully retracted
Signed-off-by (owner):
```

---

## Appendix A - the purge target set: eleven blobs (ten vendor `.o` + one accidental binary)

Paths are relative to the repository root. Blob ids are Git SHA-1 object ids and are exactly what
the V3/V4 gates compare.

| # | blob id | bytes | path |
| --- | --- | --- | --- |
| 1 | `9338af226b58a7dc25d00114fca91e194484d32d` | 6,084 | `lab/luofu-kernel/nand/parsers/ofpart_triductor.o` |
| 2 | `3bea6e81f8605d36e90f04e281d83ee49d311f4b` | 13,892 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_hal_nand.o` |
| 3 | `2ccc6e8ae1c2961a4445ae1cb02d438d5afe1106` | 6,740 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_bbt.o` |
| 4 | `c26f09bd73c7dd3be271b2f2dbf3733a813f016a` | 13,364 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_check.o` |
| 5 | `5a9ffba570bb44444c11a493e5ec19858b8f9c1b` | 29,844 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_drv_yyxxxx.o` |
| 6 | `98dac5fe55aeb7088f03793ebb9d8473769aca42` | 38,652 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_mtd.o` |
| 7 | `e37cec2e70b83b758af6d98c8189d237885b43bb` | 36,036 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand.o` |
| 8 | `0b836d1b5d67e76ccfcd677e8683ae5fa27f9455` | 28,888 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_spi_nand_drv.o` |
| 9 | `4f7b1fae2d1fb0b90ae2da426f03ce4812dc2247` | 30,592 | `lab/luofu-kernel/nand/triductor/tri_fmc.o` |
| 10 | `a97f4e69895991b7b6b7ecb3265ad8c7da3c5648` | 11,324 | `lab/luofu-kernel/nand/triductor/tri_mtd_parts.o` |
| 11 | `87332fdce8e4d0708437fb2d558895ea068d46bb` | 54,588 | `target-skeleton/tools/__pycache__/fip-slice.cpython-314.pyc` (accidental, non-vendor) |

Do not extend this list without re-auditing: at the snapshot these eleven are the only binary blobs
in the legacy history. Rows 1-10 are the vendor target. Row 11 is an accidental Python bytecode file
committed on 2026-11-19; it is **not** in either tip tree (so HEAD content does not change) but it
is a binary blob in history and is included so the purge leaves no binary blob at all. The three vendor-derived text remnants (two `Makefile` files and
`ofpart_triductor.h`) are a separate curation decision (PUBLISH-PREP section 3, B6); they are
not part of this binary purge and removing them would change HEAD content, not just history.

## Appendix B - dry-run results (2026-11-21, throwaway directories only; NOT executed on any remote)

What was actually run: full `git clone --no-single-branch` clones of the public legacy repo into
throwaway `/tmp` directories, the exact P4 filter-repo command (first pass with the then-current ten
vendor paths, final pass with the eleven-path set), all P5 gates, a reflog-expire+gc, push
simulations against **local bare clones** (not GitHub), and fresh-clone verifications from those
local remotes. No push to GitHub, no remote ref change, no branch
deletion on GitHub, no cache purge, no commit, and no write to the project's repositories.

First pass (2026-11-21 ~14:57 +07, then-current refs, ten paths; superseded by the final pass below):

| dry-run item | observed |
| --- | --- |
| baseline refs (first pass) | master `0c82cb81ae032d1fe992e901ec406e329962dcb0`; branch `15703f5ee222b898191ccd90ae1fcb0e24c179b5` |
| baseline history (first pass) | 793 commits; 6,277 `rev-list --objects` lines; 10 vendor paths; every ref reached 10 |
| baseline trees | master 689 files / 0 vendor objects; branch 434 files / 10 vendor objects; branch is 162 commits behind master and an ancestor |
| rewrite | `git filter-repo --force --invert-paths` with the ten Appendix-A paths exited 0; origin remote removed by the tool as expected |
| new refs (reference only) | master `27fee4a586c1cb63c78cb3858ae634fa0dcbc4de`; branch `1cd28946d8b7abef10aa1ff80f4c331269111a9d` |
| V1/V2/V3/V4 | 0 reachable vendor paths; every ref 0; 0 old blob ids reachable; 0 old blob ids present in the object database after `reflog expire + gc --prune=now` |
| V5 (first pass) | 793 commits preserved; master tree id identical; branch still an ancestor of master |
| V6 | old-vs-new listings identical after removing exactly the ten paths (master and branch) |
| V7 | metadata diff was **one line**: a short old commit hash referenced inside one commit message was remapped by filter-repo (`1ef70ad` -> `598f574`). No author/date/subject changes |
| V8 | commit-map 794 lines; both audited refs mapped; sha256 `207a22b0826b8056ef242fbe9b9bb2f8533fe3565ba572c85ce1564bc516fe6e` (same input refs/tool) |
| filter-repo version | `a40bce548d2c` (installed into /tmp with the project venv pip `--target`; no system install) |
| push simulation | wrong-lease push rejected; correct-lease master push accepted; branch delete and branch rewrite both accepted on local bare remotes |
| fresh-clone simulation (first pass) | 0 vendor paths, 793 commits, old blob ids absent; ref shapes as expected for both branch options |
| checker on rewritten clone | section E (history) clean on every ref; remaining FAILs were the known legacy content/license items, not history |

**Final pass (2026-11-21 ~15:09 +07, current refs, eleven-path set).** Baseline: master
`5b401c1230d416ccd0bfe21b102963b60798cc2a`, branch `15703f5ee222b898191ccd90ae1fcb0e24c179b5`,
795 commits, 6,299 object lines, 11 binary blobs (ten vendor .o + one .pyc); master tree 706 files /
0 vendor, branch tree 434 files / 10 vendor, branch 164 commits behind and an ancestor. Rewrite with
the eleven paths exited 0. New refs (reference only): master
`603414df68fcce2cb831ae093ee5c7fb7857bc29`, branch `1cd28946d8b7abef10aa1ff80f4c331269111a9d`.
795 commits preserved; V1 vendor paths 0; all eleven old blob ids unreachable and absent from the
object database after reflog expire + gc; master tree id unchanged (`4b0c803f...`); branch ancestor
preserved; V6 listings identical after removing exactly the eleven paths; V7 diff still exactly the
single short-hash remap; V8 commit-map 796 lines, sha256
`cf7b365dd45e0f9acbe492ba7f97c908584321f9891c6b7a50203741f45ae504`; checker section E reports
"no binary blob in any reachable history" PASS. Earlier passes (793- and 794-commit snapshots)
additionally validated push/lease/delete/rewrite mechanics and fresh-clone verification against
local bare remotes; those mechanics are unchanged by the tip movement or the extra path.

These results are the evidence that the procedure is mechanically sound. They do **not** prove the
GitHub-specific steps (push permissions, cached views, support response); those remain owner-run.

## Appendix C - troubleshooting

| symptom | meaning | action |
| --- | --- | --- |
| `git filter-repo` not found | tool not installed/on PATH | P3: distro package, pipx, or the venv `--target` block; re-check version |
| `ModuleNotFoundError: git_filter_repo` after `--target` install | script cannot see the module | set both `PATH` and `PYTHONPATH` to the target dir (P3 does this) |
| filter-repo says "expected freshly packed repo" / dirty | clone has local changes | work on the fresh P2 clone only; do not rewrite the working notebook/submodule |
| lease rejected / stale info | remote moved after P0 | HOLD; re-audit and restart P0; nothing was changed |
| push denied (403/401) | credentials/2FA | fix owner credentials; never put a token on the command line |
| commit count changed | unexpected pruning/extra refs | ABORT: the expected count is 795; investigate before push |
| V6 diff non-empty outside the ten paths | wrong path list/tool behavior | ABORT: do not push; re-run dry run and compare with Appendix B |
| old blob URL 200 after push | cached/unreachable object | not a rewrite failure; proceed to P8 support purge |
| old blob URL 200 after support reply | cache purge incomplete | follow up once with ticket id; record residual; do not rewrite/delete |
| branch delete denied by protection | branch protected | both branches were unprotected at audit; if protection was added, update the rule first (owner) |
| GitHub UI still shows old files | UI cache/commit view | support purge; verify via fresh clone and API, not the browser |

## Appendix D - do not run (owner-only and out of scope)

The following are **not** part of this runbook and must not be executed by an agent or without a
separate owner decision: any command that creates or force-pushes a ref outside P6; repository
deletion or recreation; changing visibility; archiving (that is PUBLISH-PREP containment, a
separate decision); touching the curated repos or the root notebook; rewriting tags/releases (none
exist at audit); running anything against the router/UART/TFTP/fire lanes; committing the backup,
the evidence or the ten vendor objects anywhere; pasting old SHAs or object ids into public issues.

This runbook was prepared and dry-run validated on 2026-11-21. It has **not** been executed
against GitHub.
