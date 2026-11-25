# SYNC-PLAN - prepared sync of the curated `cudy-wr3000v2-openwrt` repository (NOT executed, NOT pushed)

**Status: PREPARED ONLY.** No push, no force, no remote write, no fetch, no history rewrite was
performed. This plan exists so the Lead/owner can approve and run a mechanical, verifiable sync when
the in-flight upstream/platform lanes have landed. Companion: [README.md](README.md) (checklist
status), [surface-delta.txt](surface-delta.txt) (the computed per-file delta).

**Target:** `/home/shibbityshwab/repos/cudy-wr3000v2-openwrt` (`origin`
`https://github.com/ShibbityShwab/cudy-wr3000v2-openwrt.git`, branch `main`).
**Source of truth:** the notebook's `opensource/` submodule (current local HEAD).
**Current snapshot pin (last sync):** source `a0aa6b1` (2026-11-22 W23 / full-reversal sync; see
`docs/PUBLISHING.md` in the curated repo). **Delta now:** 251 source commits, 92 changed files
(docs 21, upstream 28, target-skeleton 40, README/NOTICE/.gitignore) + 16 curated-only files whose
content is curated-specific (licences, the curated PUBLISHING/README, the bench install tools).

---

## 0. Hard preconditions (do not start otherwise)

1. **The Lead's explicit go** for this sync; the push remains a separate, explicit decision.
2. The source tree is **settled and clean**: `git -C <repo>/opensource status --porcelain` is empty
   (now it is **13 dirty files** - the in-flight platform-fold lane), and the upstream series is
   final (the platform lane is regenerating the `013a -> DT reset-gpios` fold and the review form;
   the current `upstream/series-6.18/` is a moving target).
3. The curated clone is **clean and at its remote tip**: `git -C <cur> status --porcelain` empty and
   `main == origin/main` (currently `4bd4360`, true locally; re-fetch first if the remote may have
   moved - that is a network action).
4. No release claim is implied: the curated repo remains "curated research material, not a
   firmware release" (its README/NOTICE wording is part of the sync).

## 1. What is synced (mapping)

| curated path | source | mode |
| --- | --- | --- |
| `upstream/series-6.18/` | `opensource/upstream/series-6.18/` | **mirror the tree** (review form + release line + DTS-0001 + validation + tools), after the platform series is final |
| `target-skeleton/` | `opensource/target-skeleton/` | **mirror the tree** (target/linux/hisilicon, DTS, image recipe, tools, port notes); keep nothing stale |
| `docs/TARGET-DESIGN.md`, `docs/ROADMAP-OPENWRT.md`, `docs/GPL-REQUEST.md`, `docs/SKELETON-SMOKE.md`, `docs/TARGET-SKELETON.md`, `docs/REMAINING-DRIVERS.md`, `docs/TRAILER-FORMAT.md`, `docs/BENCH-READONLY-INVENTORY.md`, `docs/INVENTORY-2026-11-18.md`, `docs/LSW-SPEC.md`, `docs/PIE-DATAPATH.md`, `docs/MISC-SOC-BLOCKS.md`, `docs/SMP-RE.md`, `docs/MULTI-PORT-PLAN.md`, `docs/SELECTOR-0x21.md`, `docs/DOCUMENTATION-HUNT.md`, `docs/VENDOR-SKB-BRIDGE-PORT.md`, `docs/PUBLISH-PREP.md`, `docs/PUBLISH-READINESS.md`, `docs/PUBLISH-PURGE-RUNBOOK.md` | same path under `opensource/docs/` | copy (the delta currently changes 5 of these) |
| `docs/REVERSAL-REPORT.md` | **superproject** `REVERSAL-REPORT.md` (not `opensource/`) | copy (curated-only path) |
| `docs/incoming/<lane>/` (retained subset: `loader-re`, `lsw-forwarding`, `multi-port-probe`, `upstream-blockers`, `vendor-userspace`, `wifi-thermal-re`, plus any new **port-relevant, text-only** lane the sync owner picks) | `opensource/docs/incoming/<lane>/` | copy; **no vendor binary/firmware/calibration/PD4 or Wi-Fi research module trees** |
| `README.md`, `NOTICE.md`, `.gitignore`, `LICENSE-MIT`, `LICENSE-GPL-2.0` | source where the same path exists; curated-specific files stay curated | review + update the rewritten README/NOTICE only if the summary claims change |
| `tools/` (stock-telnet installer etc.) | curated-specific | keep (do not copy the notebook's offensive tools) |
| `docs/PUBLISHING.md`, `docs/README.md`, `docs/INSTALL-RECOVERY-LESSONS.md` | curated-specific | **edit** (pin block + snapshot line), not overwrite |

**Never synced:** vendor `.ko/.o`, firmware/`FIRMWARE.PD4.bin`, rootfs, calibration data, extracted
DTS copies, register dumps, the Wi-Fi research module trees or their fired artifacts, `.omo-secrets`,
per-unit MAC/serial values. The notebook keeps those; the curated repo only ever gets derived text
+ original code.

## 2. Procedure (copy/paste; dry-run first)

```sh
# 0. variables
REPO=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
SRC=$REPO/opensource
CUR=/home/shibbityshwab/repos/cudy-wr3000v2-openwrt

# 1. gates
git -C "$SRC" status --porcelain            # MUST be empty
git -C "$SRC" log --oneline -1             # record the source pin for PUBLISHING.md
git -C "$CUR" status --porcelain           # MUST be empty
git -C "$CUR" fetch origin && git -C "$CUR" status -sb   # optional network; confirm main==origin/main

# 2. dry-run the two mirrored trees (rsync -n: no writes)
rsync -an --delete --itemize-changes "$SRC/upstream/series-6.18/" "$CUR/upstream/series-6.18/"
rsync -an --delete --itemize-changes "$SRC/target-skeleton/" "$CUR/target-skeleton/"

# 3. dry-run the selected docs (example: the changed list from surface-delta.txt)
for f in TARGET-DESIGN.md ROADMAP-OPENWRT.md SKELETON-SMOKE.md TRAILER-FORMAT.md MULTI-PORT-PLAN.md; do
  rsync -an "$SRC/docs/$f" "$CUR/docs/$f"
done

# 4. apply (after reviewing the dry-run item lists)
rsync -a --delete "$SRC/upstream/series-6.18/" "$CUR/upstream/series-6.18/"
rsync -a --delete "$SRC/target-skeleton/" "$CUR/target-skeleton/"
# ... selected docs as reviewed (the full changed list is surface-delta.txt)

# 5. edit the curated provenance pages (not copied): docs/PUBLISHING.md (new sync section + source pin),
#    docs/README.md (snapshot line), README/NOTICE only if the claims changed.
```

## 3. Verification gates (all read-only; run in the curated clone before any commit)

```sh
git -C "$CUR" status --short
git -C "$CUR" ls-files | grep -Ei '\.(o|ko|a|so|bin|img|dtb|elf|ubi|squashfs|apk|pyc)$' || echo 'no binary files'
git -C "$CUR" grep -nE 'BEGIN (RSA|OPENSSH|EC|PRIVATE) KEY|/home/|/root/\.ssh|\.omo-secrets' || echo 'no secret/host-path patterns'
git -C "$CUR" grep -nE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' | grep -v '02:00:00:00:00' || echo 'no non-documentation MACs'
opensource/tools/publish-prep-check.sh            # the curated-repo-relevant sections (run from $SRC)
# plus: relative markdown links resolve; the selected docs carry no vendor byte; README/NOTICE wording
# still says "curated research material, not a release".
```

A sync is acceptable only if: 0 binary files in the tracked tree, 0 secret/host-path patterns, the
selected docs are text-only, the report/docs cross-references resolve, and the curated repo still
declares itself **not a release**.

## 4. Commit in the curated clone (still no push)

```sh
git -C "$CUR" checkout -b sync/<date>-rxhunt            # optional branch; or commit on main
git -C "$CUR" add -A
git -C "$CUR" commit -m "sync: source pin <source-pin> - upstream series/DTS + target-skeleton refresh + docs update (RX-hunt closeout)"
git -C "$CUR" log --oneline -3
```

Then **STOP**. The push (`git push origin main`) is the Lead/owner's explicit decision, exactly as
`PUBLISH.md` requires. If the decision is no, `git -C "$CUR" reset --hard origin/main` (in the
curated clone only) discards the local sync commit; the remote was never touched.

## 5. Explicitly out of scope (do NOT do in a sync)

- No `git push`, `push --force`, `filter-repo`, branch deletion, tag, visibility change, cache
  purge, or any action on the legacy repo's history (owner-only; `PUBLISH-PURGE-RUNBOOK.md`).
- Do not add the Wi-Fi research/firmware material to the curated repo (policy in README §2).
- Do not copy the notebook's offensive tools, the ledger, the dumps, or anything under
  `build/`, `firmware/`, `rootfs-*/`, `dumps/`, `.omo-secrets/`.
- Do not "fix" the historical vendor objects in the curated repo: its fresh history is clean by
  construction; the legacy repo's history is the owner's purge question.
- Do not push the notebook's new commits as part of the sync unless separately approved (the
  notebook and the curated repo are different publication surfaces).

## 6. State this plan was prepared from

- source: `opensource` HEAD with the RX-hunt closeout commits (v7 report + two lane reports + this
  status); the concurrent lanes' commits also present; **worktree had 13 in-flight files** when this
  plan was written - the plan says gate 1 must clear them.
- curated clone: `main` = `4bd4360` = `origin/main` (2026-11-21), 324 tracked files, clean.
- delta: 92 changed + 16 curated-only files; 251 source commits since pin `a0aa6b1`
  (`surface-delta.txt` has the full list and the generation command).
