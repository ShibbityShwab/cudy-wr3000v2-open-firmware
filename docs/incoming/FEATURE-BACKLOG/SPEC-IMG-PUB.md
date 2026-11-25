# BACKLOG SPEC - image, integration, publication (IMG-1..2, PUB-1..3)

## IMG-1 - the image bake with the regdomain fix + the latest modules
- The bake source: the OWRT tree (config-6.18 with EXPERT+ONUS+REQUIRE off; the built
  cfg80211.ko; the latest driver module).
- PASS: the baked image verifies (the same verify-final-image-2.sh 84/84 style checks + the new
  cfg80211/module hashes pinned).
- Test: bake-final-image-2.sh (a long build) or the targeted squashfs+ubi patch (the fix README
  has the exact options).
- State: READY (the config + the module are in the tree).

## IMG-2 - the image #2 install (rootfs-only)
- The procedure: docs/incoming/BENCH-NEXT-SESSION (the P2) + wifi-final-image-2/FLASH-FEASIBILITY.
- PASS: the post-boot receipts (the kernel pin, the installed module, the SAE hostapd, the AP
  bring-up, the regdomain US).
- Deps: the bench + the recovery armed.

## PUB-1 - the platform/upstream v2 30-patch review form
- Blockers: the 16 unexecuted content folds; the fold-pipeline decision; one attended boot of
  the 013a->DT reset-gpios; the generator rebuild.
- PASS: the v2 review form generated + validated (the same L1/L2 checks).
- State: BLOCKED (docs/incoming/upstream-v4/V2-REVIEW-FORM-STATUS.md).

## PUB-2 - the legacy history purge
- OWNER-ONLY (the pre-policy history: the old credential + the vendor blob exposure).
- PASS: the history clean (the owner's action).

## PUB-3 - the curated-repos sync
- The plan: docs/incoming/publication-status/SYNC-PLAN.md (prepared, not executed; no push).
- PASS: the curated repos current (the owner's go).
