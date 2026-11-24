# PUBLICATION NOTES - close-out state of the publish surface

**Lane:** wifi-closeout (task-131), host-only, 2026-11-27.
**Authoritative run:** the submission-finalization lane (task-130) executed the full
publication checks and recorded them in
[`../wifi-upstream-final/PUBLICATION-CHECKS.md`](../wifi-upstream-final/PUBLICATION-CHECKS.md)
(committed in opensource 75c8ed4). This file does not duplicate that run; it records the
close-out summary, the checks this lane re-ran, and the resulting review items.

## 1. Headline: the publish surface is NOT clean yet

- `tools/check-repo.sh`: **PASS** (repository consistency).
- `opensource/tools/publish-prep-check.sh`: **FAIL - 15 PASS / 9 FAIL / 8 WARN** (second run
  after documentation edits gave an identical summary, so the failures are structural, not
  flaky).
- The curated public half can be read and used, but it is **not** publish-ready until the
  history purge and the documentation fixes land. Do not present a "publication complete"
  claim.

## 2. The nine publication failures (as recorded by task-130; details/citations there)

| # | finding | severity | owner/route |
| --- | --- | --- | --- |
| 1 | 10 vendor `.o` files remain reachable from **all three refs** in the pre-policy history | blocking for a clean publish | history purge (runbook below) |
| 2 | a `.pyc` blob in the history | blocking for a clean publish | history purge |
| 3 | 23 relative links in 6 files escape the repository root (largest: `docs/incoming/vendor-userspace/REPORT.md`) | doc fix | lane/doc owner |
| 4 | 4 tracked `.dts` copies (duplicated DTS artifacts) | doc/artifact cleanup | doc owner |
| 5 | missing `COPYING` | license hygiene | repo root |
| 6 | missing `CONTRIBUTING` | contribution hygiene | repo root |
| 7 | missing DCO (Developer Certificate of Origin) statement | contribution hygiene | repo root |
| 8 | the "Wi-Fi + LAN-switch statement" is missing from the publish/readme surface | policy/accuracy | README/ROADMAP owner |
| 9 | no upstream submission statement (what is/is not submitted) in the publish surface | policy/accuracy | README/ROADMAP owner |

Warnings include: the shallow-clone caveat on the history scan (a full-clone purge check is
still required), and one **false positive** ("private key" pattern) in
`docs/incoming/wifi-image-integration/tools/selftest.sh:81` - it is a test fixture string, not
a key.

## 3. What this lane re-ran (host-only, 2026-11-27)

```sh
# current tracked binaries: must be empty (no vendor objects/blobs in the working tree)
cd opensource && git ls-files | grep -Ei '\.(o|ko|bin|img|dtb|apk|ipk|so|a|pyc)$'
# -> (empty)

# large tracked files (review list; register dumps are text analysis artifacts)
git ls-files -z | xargs -0 -r ls -la | sort -k5 -rn | head

# secret patterns in the current tree
grep -rIl --exclude-dir=.git -E 'BEGIN (RSA|OPENSSH|EC) PRIVATE KEY' .
```

Results at close-out:

- **No tracked vendor binary/object/firmware blob in the current tree** (0 matches; the
  requirement is met in the working tree - the *history* is the open issue).
- Large tracked files are text: legacy register dumps under `dumps/` (e.g.
  `dumps/reg_all.txt`), the phase-49 GIC study, the LSW/Wi-Fi specs, and one vendored
  GPL-2.0 source file under `docs/incoming/wifi-algcfg-slice/vendor/hi3881-wal/` (published
  HiSilicon GPL source; a license/provenance review item, not a binary).
- The false-positive key pattern is the only hit of the key scan in the current tree.
- This lane adds **no** vendor bytes: all new files are Markdown/shell/Python text carrying
  only sizes, sha256 hashes, register addresses and frame field values already published by
  the owning lanes.

## 4. The binding rules (do not relax them for a nicer release note)

1. No vendor binaries, firmware images, ROM dumps, calibration bytes or secrets in tracked
   text or in the curated public repos - ever. Vendor-derived artifacts stay local and
   gitignored.
2. The ledger (`mem-entries.md`) is append-only; the newest dated entry wins over older docs.
3. Curated repositories are the publish surface: `cudy-wr3000v2-openwrt` (the port) and
   `cudy-wr3000v2-cve` (security research). The root notebook is intentionally public
   research material, but "public" never means "safe to paste a secret".
4. The PD4-patched research firmware and the Wi-Fi research driver are **never** published as
   bytes and are never presented as upstreamable.
5. The Wi-Fi + LAN-switch + GPL-source status must be stated honestly in the publish surface
   (this is publication-failure #8/#9 above; the new
   [OVERVIEW-README.md](OVERVIEW-README.md) carries the statement and is ready to replace
   `opensource/README.md`).

## 5. Required order to reach a clean publish

1. Land the documentation fixes (broken links, DTS copies, COPYING/CONTRIBUTING/DCO,
   Wi-Fi/LAN-switch + submission statements) - items 3-9.
2. Execute the history purge exactly per the parent notebook's `PUBLISH-PURGE-RUNBOOK.md`
   (owner-only decision; the purge is blocked on the owner, not on tooling).
3. Re-run `tools/publish-prep-check.sh` on a **full clone** (not shallow) and confirm the
   history scan is clean.
4. Update `docs/PUBLISH-READINESS.md` and `NOTICE.md` with the purge receipt; the
   task-130 dated update block is the current state text.
5. Only then treat "publication checks PASS" as a true statement.

**Evidence status:** the items above are recorded in
[`../wifi-upstream-final/PUBLICATION-CHECKS.md`](../wifi-upstream-final/PUBLICATION-CHECKS.md)
and the task-130 dated blocks in [`../../ROADMAP-OPENWRT.md`](../../ROADMAP-OPENWRT.md),
[`../../PUBLISH-READINESS.md`](../../PUBLISH-READINESS.md) and
[`../../upstream/SUBMISSION-STATUS.md`](../../upstream/SUBMISSION-STATUS.md).
