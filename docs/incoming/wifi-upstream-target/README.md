# wifi-upstream-target - OpenWrt target + kernel series readiness (task-107)

Lane: host-only, 2026-11-24. Nothing here touches the router, the bench link, the
UART or any vendor binary.

## What this lane did

1. reviewed the target tree (target/linux/hisilicon/*, board DTS, image recipe) and
   replaced the stale "schematic/untested" caveats with the current bench-verified
   status;
2. fixed the host-fixable target defects found in that review: retired the bench-only
   duplicate pair recorder and enabled the real one, fixed the boot-selector sysfs
   name (hardware is boot-selector, scripts looked for luofu-selector), dropped the
   never-included profile sketch, and committed the patch-013a GP1 endpoint-reset
   delta that was only in the working tree;
3. build-verified the result in an isolated OpenWrt main tree (kernel prepare/compile
   and a full image build; see BUILD-VERIFICATION.md);
4. classified all 67 target patches for submission: 26 keep (7 regenerated with
   folded hunks), 17 fold, 24 drop (bench-only) -> a 30-patch kernel series + DTS-0001
   proposal (PATCH-CLASSIFICATION.md, submission-series.md);
5. updated the roadmap with the 2026-11-24 reality, including the Wi-Fi protocol
   result and the "not upstreamable yet" boundary.

## Files

| file | content |
| --- | --- |
| REPORT.md | the lane report: findings, fixes, evidence, remaining blockers |
| BUILD-VERIFICATION.md | exact build commands, logs, artifacts and hashes |
| PATCH-CLASSIFICATION.md | all 67 patches, class and submission verdict |
| submission-series.md | ordered v2 series, new commit-message drafts, acceptance criteria |
| CONFIG-AUDIT.md | defconfig sync proof, cfg80211 bench delta and submission-clean config delta |
| CFG80211-BENCH-IMAGE.md | exact rebuild/deploy/flash steps for the cfg80211 bench kernel (task-104 handoff) |
| dts-endpoint-proposal.dtsi | PROPOSAL ONLY Wi-Fi endpoint nodes (not applied) |

## Boundary

- The real tree changes are in target-skeleton/ (committed separately); this directory
  is documentation only.
- The upstream review form (../../upstream/series-6.18/) was **not** modified: its
  regeneration needs the fold decisions to be applied to the target patches and the
  full validation pipeline (see submission-series.md).
- Publication of the opensource half is still NOT READY (history purge pending).
