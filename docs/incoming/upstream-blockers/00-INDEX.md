# Upstream blocker-reduction drafts - luofu 6.18 series (2026-11-21)

**STATUS: documentation + patch drafts only.  These drafts are NOT the release
series.**  They reduce the upstream blockers recorded in
`../../upstream/series-6.18/07-dts-upstream-bindings.md` section 6.  As of
2026-11-21 the list is **five remaining blockers**: TWD timer interrupt flags
(blocker 6) was measured and boot-tested, its level-high delta is adopted in
commit `5b401c1`, and the current final DTS/DTB and new DTS-purpose
baseline image f6685b80 supersede the previous baseline 7421bb7a (which remains
recorded).

## Remaining blockers

| # | blocker | after this package | artifact |
| --- | --- | --- | --- |
| 1 | drivers/YAMLs are still drafts | **open** - prioritized top-issue review only (no code rewrites in this pass) | `05-driver-yaml-review.md` |
| 2 | `hsan,flashinfo_reserved` has no upstream binding | **drafted** - reserved-memory child binding + compatible/loader-compatibility options; needs maintainer direction and (only if the vendor string is not first) one hardware boot-test | `01-flashinfo-reserved-memory/` |
| 3 | `fixed-layout`/`mac-base` v6.19 on 6.18 | **path defined** - 6.18 uses the already-implemented userspace/vendor MAC provisioning; DTS nvmem removal drafted; needs maintainer confirmation + boot-test | `02-mac-provisioning-6.18/` |
| 4 | switch queue-first reg-names / node name | **drafted** - binding + DTS MMIO-first reorder; driver needs no code change (by-name lookups); no in-tree users; boot-test required | `03-switch-binding-mmio-first/` |
| 5 | cudy compatible ordering + OpenWrt metadata | **drafted** - upstream compatible order + OpenWrt SUPPORTED_DEVICES/board_name diffs; boot-test + sysupgrade-metadata check required | `04-compatible-order-openwrt/` |

**Closed:** blocker 6 (TWD timer interrupt flags) - measured on hardware, level-high
draft boot-tested PASS on image f6685b80 (`/proc/interrupts`: `GIC-0 29 Level
twd`, 2 CPUs, counts ~100 Hz, ping 20/20, no errors), adopted in commit
`5b401c1`.  See `06-twd-irq-flags/README.md`.

## Current final artefacts (after the TWD adoption)

- DTS: 700 lines, sha256
  `13427d8e2bf7624b77fc6735932f79b9b25bf40dff1a0c859fc5cc3515b54de4`;
- DTS-0001: sha256
  `e9c9ed58b1a198bad16283d048a0498ee02297a8e1645f194df3f713c5d236b1`, 713
  lines;
- DTB: 13,041 B sha256
  `4fde76df165689cdb859a6f79223300edc8fb27d1d239abea131d25726cbc1a9`; pad
  step 13,048 B sha256
  `4b50de73bebf6241ccb1ac62907c1de9b49a7ec7bcc6f18286f638541456e890`;
- DTS-purpose hardware baseline: image **f6685b80** (supersedes 7421bb7a for
  DTS purposes; 7421bb7a remains the previous verified baseline).

## What is hardware-dependent (five blockers)

- **MAC nvmem removal** (blocker 3): active DTS delta - boot-test that the GMACs
  still probe with the random fallback and userspace MAC provisioning works.
- **Switch reorder** (blocker 4): active DTS delta - boot-test that the switch
  probe still validates the same resources and the node stays disabled.
- **Compatible ordering** (blocker 5): active DTS delta - boot-test the image and
  check `board_name`/sysupgrade metadata with the new first compatible.
- **Flashinfo binding** (blocker 2): hardware test only if the vendor compatible
  is no longer first/only match; the binding text itself is documentation.
- **Driver/YAML cleanups** (blocker 1): code review, not hardware.

## Rules

- These files are proposals; the release series (and the conservative DTS
  content) are otherwise unchanged.  Active deltas must be boot-tested on the
  unit before adoption, one boot-test per delta (as done for the TWD flag).
- Patch drafts are unified diffs against the tree state at their creation; they
  may need rebasing after maintainer review.
- No curated repos were touched; no submission/email/web action was taken.
