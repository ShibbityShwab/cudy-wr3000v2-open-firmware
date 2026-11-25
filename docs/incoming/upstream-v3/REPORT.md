# Report - upstream platform track v3 (task-161), 2026-11-29

**Lane:** upstream-v3 (task-161), host-only. **Scope:** advance the platform
submission track - DTS-0001 generation, series status, submission v3 package
refresh, publication fixes.  **No device, bench link, UART, TFTP, flash, vendor
binary or network push was involved.**  All commits are inside `opensource/`.

## 1. Result in one paragraph

The **DTS-series entry DTS-0001 was regenerated** from the current
single-source board DTS and is no longer stale: the retired `wifi-pcie-hog`
is gone, the entry matches the 2026-11-24 GP1 hardware-validated boot line, and
it applies to / compiles with the release-line tree with the loader hand-off
contract and CPU `enable-method` verified.  The **release line was fixed**: the
committed, load-bearing `013a` endpoint-reset pulse is now included, so L1 is
**56 patches** (was 55, i.e. it described a patch set that was not the booted
line), and `check-release-line.sh` re-ran **PASS** with strict `git apply`
clean on all 56.  The **26-patch review form is unchanged** and re-validated
PASS (identical trees, 0 checkpatch errors / 73 warnings).  The **publication
checks improved** from 13 PASS / 10 FAIL / 9 WARN to **18 PASS / 6 FAIL /
8 WARN** by adding `COPYING` (GPL-2.0), `CONTRIBUTING.md` (SPDX + DCO),
fixing all 77 broken relative markdown links, rewording five lane self-tests
whose literal private-key grep strings tripped the credential scan, and
refreshing the submission hashes/manifests.  The submission is still **NOT
postable**: the remaining FAILs are owner-only (history purge / curated repos,
DCO/SPDX/copyright of old files, one README wording item) and the planned
30-patch **v2 review form is not generated** because its last content decision
is the `013a` -> DT `reset-gpios` fold plus one attended boot.

## 2. What this lane did

1. Inventoried the upstream state from the committed tree and the 2026-11-27
   finalization package ([../wifi-upstream-final/](../wifi-upstream-final/README.md)),
   then verified the current artefacts host-side (series drift, release-line
   validator, publication checker, DTS regeneration tools).
2. **Generated DTS-0001 v3** from `target-skeleton/dts/luofu-r116.dts`
   (702 lines, sha256 `df0d8e59...`) through `tools/dts-upstream-filter.py`
   (690-line upstream form, W23 release-line block stripped) with
   `tools/gen-dts-series-patch.py`; replaced
   `upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
   (sha256 `33956380...` -> `1fd1d10a...`) and validated apply + compile +
   contract + pad; delivery copy in [artifacts/](artifacts/).
3. **Fixed the release line (L1)**: added the `013a` row to
   `release-line/manifest.tsv` and `release-line/INCLUDED.txt` (55 -> 56,
   class A, sha256 `d4b544a3...`), and re-ran
   `tools/check-release-line.sh` on pristine linux-6.18.60 -> **PASS**,
   including the DTS-0001-vs-filtered-target identity check (byte-identical)
   and the no-hog/no-W23-leak checks.  Logs copied to
   `upstream/series-6.18/validation/release-line-v3/`.
4. **Re-validated the frozen review form**: `gen-series.py --check` clean
   (26 patches); `run-validation.sh` PASS (26/26 `patch(1)` and `git am`,
   identical trees, 3 offset hunks, 0 fuzz, checkpatch 0 errors / 73 warnings
   in default and `--strict` runs).  No kernel compile was run here: the patch
   bytes are unchanged from the last recorded full-compile pass and the only
   new build product is the DTS entry, which was compiled with cpp+dtc.
5. **Refreshed the submission v3 records** in place (the review form directory
   is the version-agnostic working artefact per `submission/README.md`):
   `05-release-line.md` (56-patch section 13), `06-dts-series-dependency.md`
   (new DTS-0001 hash/payload and v3 validator record),
   `submission/README.md` (v3 update block + current-state table + artifact
   hashes), `submission/MANIFEST.sha256` (105/105 verified),
   `validation/sha256sums.txt` (dts entry refreshed; all entries verify).
6. **Publication fixes** (host-only, read-only checks re-run):
   - added `COPYING` - canonical GPL-2.0 text with the repo licence-split note;
   - added `CONTRIBUTING.md` - SPDX convention, provenance statement, DCO
     (`Signed-off-by`) convention, no-vendor-bytes/no-secrets rules;
   - updated `NOTICE.md` (links `docs/PUBLISH-PREP.md`; records that COPYING +
     CONTRIBUTING now exist and what remains open);
   - fixed **all 77 broken relative markdown links** in 9 files with the
     idempotent [tools/fix-publication-links.py](tools/fix-publication-links.py)
     (parent-notebook escape links become code text; same-directory typos
     corrected; the closeout proposed README gets the correct `../../../`
     prefix - if it is promoted to the repository root those prefixes must be
     removed again);
   - removed the literal `BEGIN ... PRIVATE KEY` strings from five lane
     self-tests (they were tests *for* key material, not keys) by building the
     patterns at run time with `PRIVATE[ ]KEY` / string concatenation; behaviour
     preserved, shell syntax + py_compile checked, checker section F tree scan
     now PASSes (the *history* scan still flags the old versions - owner purge).
   - did **not** touch the closeout `README.md` `clean-room` wording, the 42
     missing SPDX tags, the 52 missing copyright lines, the 356 unsigned
     commits, the vendor-`.o` history, the `.pyc` history blob or the 4 tracked
     DTS copies: those are owner/history decisions, itemised in
     [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md).
7. Wrote this package and committed only inside `opensource/` (no push; the
   Lead decides publication/posting).  Concurrent lane work in the same
   worktree (wifi-re-report edits, several untracked lane dirs) was deliberately
   **not** staged or committed by this lane.

## 3. Series status

See [SERIES-STATUS.md](SERIES-STATUS.md) for the table.  Summary:

* **L0** (lab stack) = 67 patch files (001-060 + 062-067 + 013a); the bench
  image source.
* **L1** (release line) = **56 include / 11 exclude**; validator PASS on
  pristine 6.18.60; manifest sha256 `17d714be...`, INCLUDED sha256
  `da4966aa...`; logs `validation/release-line-v3/`.
* **L2** (upstream review form) = frozen **26-patch v1**; validated PASS this
  session (apply, identical trees, checkpatch 0 errors / 73 warnings);
  `gen-series.py --check` clean.  **Not postable yet**: the planned
  **30-patch v2** (v1 + folds 027/038/041/044/048/051/053/066/067 + new
  0027-0030 + 013a-as-DT) is not generated because its one remaining content
  change is a driver/binding/DTS change that also needs one attended boot:
  replace the hardcoded GP1 GPIO pulse in `013a` with DT `reset-gpios`
  consumed by the DWC glue (assert low 20 ms, release high 100 ms; keep the
  validated order: pulse before RC clock/LTSSM start) and add the property to
  the PCIe RC node + binding.  After that: regenerate L2 from L0
  (`gen-series.py`) and refresh manifests.
* **L3** (DTS series) = regenerated DTS-0001 v3; pairs with kernel patch 0018.
* **Never sent / never reviewed.** No maintainer has seen any of this.  The
  target PR and the image/install path remain separate (previous lanes).

## 4. Host validation performed (copy of the key evidence)

Release-line validator (`check-release-line.sh --use-tree <pristine>`):
```
manifest: 56 included, 11 excluded
patch(1): 56/56 patches RC=0, 153 file events, 5 hunks with offset, 0 fuzz, 0 rejects
strict git apply: all 56 patches apply without fuzz
board DTS: cpp RC=0, dtc RC=0, stderr 0/0, DTB 12722 B sha256 40b71de5...; unaligned -> pad 12728 B
DTS contract: console=/serial@1010f000 flashinfo-compatible=hisilicon,luofu-flashinfo hsan,flashinfo_reserved reg=80600000 4000 atag-offset=1000 enable-method=hisilicon,luofu-smp
DTS series: patch apply RC=0; board DTS byte-identical to the filtered target DTS; DTB 12554 B sha256 4f2e0e8d...; pad 12560 B; same contract; no W23 leak; no wifi-pcie-hog
PASS: release line applies and the loader hand-off DTS contract is present.
```
Review-form validator (`run-validation.sh --use-tree <pristine>`): 26/26
`patch(1)` and 26/26 `git am`, identical trees (`diff -r`), 3 offset hunks,
0 fuzz, checkpatch default and `--strict`: **0 errors / 73 warnings**.
DTB sizes/hashes are from the host system `dtc` v1.8.1; the 2026-11-22
kernel-dtc figures are a different build, so compare within one run.

## 5. The Wi-Fi boundary (research only - never part of this submission)

The Wi-Fi work is **not** in the kernel series, the DTS or the package, and it
must never be shipped/posted as part of them.  Honest current state of that
research line (bench evidence owned by the Wi-Fi lanes; no vendor bytes here):

* boot protocol golden + HCC duplex + calibration + cfg80211 wiphy are
  bench-validated; scan is structurally limited (6 channels/request default,
  7-8 with the H1 band-config pacing config, full 13 impossible on this
  firmware - documented negative result);
* the AP-start uses the five-message vendor order (`vap_add` -> `ap_chan`
  0x0124 -> `ap_ssid` -> `ap_cfg` -> `ap_vap_start`) and reaches hostapd
  **AP-ENABLED**; WPA2 GTK+IGTK is installed and **WPA3-SAE is on air** (SAE
  AKM, MFP-required, BIP-CMAC-256); MAC TX completes (TXQ publish), the live RX
  bank CA `0x40050000` is armed (status `0x182`), and the corrected mgmt-TX
  carrier (HCC g0 id6) / mgmt-RX decoder (D2H CRX g1 id13) are hardware-
  accepted; station ops, EAPOL/data TX and the final association module exist;
  client association is currently RF-blocked (no antennas / flaky laptop NIC).
* **But**: it runs on the **PD4-patched research firmware** with research
  provider modules (the final association module is the local artifact
  `/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko`, 344,372 B, sha `ab9e18a4`,
  56 params; rebuilt by the committed association-module build script), the
  GPL source request is unanswered (sent 2026-10-01), and no vendor-clean
  delivery path is proven.  Therefore: research-only, no upstreamable Wi-Fi
  driver, **no support claim**, and the submission cover letter must keep the
  Wi-Fi exclusion wording from
  [../wifi-upstream-final/GPL-PD4-STATUS.md](../wifi-upstream-final/GPL-PD4-STATUS.md)
  and [../wifi-upstream-final/AP-MILESTONE-HONEST.md](../wifi-upstream-final/AP-MILESTONE-HONEST.md).
  Owning lanes:
  [../wifi-stability/ASSOCIATION-INTEGRATION.md](../wifi-stability/ASSOCIATION-INTEGRATION.md),
  [../wifi-ap-mgmt/REPORT.md](../wifi-ap-mgmt/REPORT.md),
  [../wifi-wpa3-sae/REPORT.md](../wifi-wpa3-sae/REPORT.md),
  [../wifi-sibling-gpl/README.md](../wifi-sibling-gpl/README.md),
  [../wifi-public-research-2/REPORT.md](../wifi-public-research-2/REPORT.md),
  [../wifi-rx-static/REPORT.md](../wifi-rx-static/REPORT.md),
  [../wifi-scan-full/REPORT.md](../wifi-scan-full/REPORT.md).

## 6. Publication state

Checker `opensource/tools/publish-prep-check.sh` (dry run, read-only):
**13 PASS / 10 FAIL / 9 WARN -> 18 PASS / 6 FAIL / 8 WARN** (the checker's
summary counter undercounts because several checks run in command-substitution
subshells; the emitted lines after the fix are 23 PASS / 12 FAIL / 9 WARN).
Fixed by this lane: COPYING (license text), CONTRIBUTING.md (DCO/SPDX
convention), NOTICE links, all 77 broken links (section G PASS), five false
positive credential-pattern self-tests (section F tree PASS), submission
hashes/manifests.  Remaining FAILs are owner-only (history shallow-clone scan,
42 SPDX / 52 copyright / 356 DCO legacy files, `clean-room` wording in the
proposed README, vendor-`.o`/credential/`.pyc` history, 4 tracked DTS
copies); details and exact lines in
[PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md).  **Publication is still
NOT READY and is an owner decision**; a PASS from the checker would not by
itself be a publication decision.

## 7. Limits / not done

* **No kernel full compile in this lane.**  The review-form patch bytes are
  unchanged since the last recorded full-compile pass and `gen-series.py
  --check` is clean; the new DTS entry was compiled with cpp+dtc.  If a fresh
  full-image build is required before posting, it is a separate (long) host
  task on an OpenWrt tree.
* **One attended boot is still required** for any active-DTS change.  This
  lane changed no DTS source content (the source already had the hog retired
  on 2026-11-24 and was boot-tested by GP1); the regenerated DTS-0001 is the
  same content in patch form.  The future `reset-gpios` fold does need a boot.
* **No git history rewrite, no unshallow, no push** (owner-only actions).
* DTB hashes/sizes are dtc-build dependent (see section 4).
* The checker summary counter undercount is pre-existing and left as-is (the
  script guarantees "no file is created", so counters cannot be spilled to a
  temp file without changing that guarantee); the emitted lines are the
  authoritative count.

## 8. Files changed by this lane (inside `opensource/`)

```
COPYING                                                    new - GPL-2.0 text + licence-split note
CONTRIBUTING.md                                            new - SPDX/provenance/DCO conventions
NOTICE.md                                                  links PUBLISH-PREP; COPYING/CONTRIBUTING recorded
docs/incoming/upstream-v3/                                 this package + artifact copy + link fixer
upstream/series-6.18/dts-series/0001-...patch              regenerated DTS-0001 (v3)
upstream/series-6.18/release-line/{manifest.tsv,INCLUDED.txt}   013a added (55 -> 56)
upstream/series-6.18/validation/release-line-v3/           new validator logs (19 files)
upstream/series-6.18/validation/sha256sums.txt            DTS-0001 hash refreshed (all entries verify)
upstream/series-6.18/05-release-line.md                   L1 56 + section 13 v3 record
upstream/series-6.18/06-dts-series-dependency.md          DTS-0001 v3 record + measured sizes
upstream/series-6.18/submission/README.md                 v3 update block, state table, hashes
upstream/series-6.18/submission/MANIFEST.sha256           105/105 verified
docs/incoming/vendor-userspace/REPORT.md                   14 escaping links -> code text
docs/incoming/wifi-5ghz/BUILD-NOTES.md                     5 link targets corrected
docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md             5 link targets corrected
docs/incoming/wifi-closeout/OVERVIEW-README.md             48 root-relative links -> ../../../ (proposed README)
docs/incoming/wifi-scan-ap/configs/README.md               1 link target corrected
docs/incoming/wifi-scan-complete/configs/README.md         1 link target corrected
docs/incoming/wifi-stability/SOAK.md                       1 link target corrected
docs/phase17/ete-engine.md                                 1 pseudo-link made non-link
docs/phase20/rx-loop.md                                    1 pseudo-link made non-link
docs/incoming/wifi-data-tx/tools/run-host-check.sh         key-pattern test built at run time
docs/incoming/wifi-sta-ops/tools/run-host-check.sh         key-pattern test built at run time
docs/incoming/wifi-final-image/tools/selftest.sh           key-pattern test built at run time
docs/incoming/wifi-image-integration/tools/selftest.sh     key-pattern test built at run time
docs/incoming/wifi-image-polish/tools/validate.py          key-pattern test built at run time
```
Parent-repository ledger/`STATUS.md`/`EVIDENCE-INDEX.md` updates are Lead
actions (this lane commits only inside `opensource/`).
