# Upstream submission - the final package assessment (the goal round 67)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** 2026-11-27. This page is the submission-lane's final reconciliation after the
2026-11-27 bench day. It does not replace `docs/upstream/SUBMISSION-STATUS.md` (the
definitive per-item checklist); it records what changed and what a posting would carry.

## What is now TRUE (the 2026-11-27 day)

1. **The target assembles and a full image builds byte-reproducibly** - the 85-file target,
   the board DTS (the DTB `1c200daf`, the 24/24 board contract), `make -j16 world` RC=0, the
   6,267,199 B image `b991f4c4`. (Host-built, never booted on a device.)
2. **All five host-doable PR gates are CLEARED** - the bench-only base-files removed, the
   Kconfig stub deleted, the package lists decided, the `config-6.18` submission-clean (the
   lab gates + the rootfsb + the CMDLINE_FORCE + the DEVMEM removed), the stale comments fixed.
3. **The PCIe review items are documented** (`upstream2/PCIe-REVIEW.md`).
4. **The DT bindings are extracted and validated** - the 12 `hisilicon,luofu-*` YAMLs in
   `target-skeleton/docs/bindings/`, all parsing as valid YAML, as the review surface.
5. **The install path is honestly split** (`upstream2/INSTALL-PATH.md`): Path A (the bench
   image #3's rootfs-only install) executed and accepted; Path B (the target sysupgrade +
   the kernel-slot write + the A/B selector) never run.

## What PR-1 would carry (ready for a reviewer)

The target + the board DTS + the minimal boot plumbing (the mach 001, the TEXT_OFFSET 002/003,
the CRG 004 (+014 folded), the watchdog 005, the restart 010, the pinctrl 011, the SMP 018, the
clock tree 019) + the three host tools + the per-board `config-6.18`. The bindings ride as their
own review surface (the extracted YAMLs). The bench-only material is gone.

## What PR-1 still needs (the owner decisions, not editing)

1. **The MAINTAINERS entry + the real Signed-off-by identities** (the owner).
2. **The dtbs_check against the submission-time mainline** (the expected `wifi-pcie-hog`
   warning is documented; the check itself needs the mainline's dt-schema).
3. **The v2 review form generation** - the SUBMISSION-STATUS section 0 says the frozen 26-patch
   form is STALE and the planned 30-patch v2 form (the 26 keep / 17 fold / 24 drop classification)
   is **not generated**. That generation is the one remaining host-doable packaging item.
4. **The one attended boot of a target-built image** (the install Path B bootstrap).

## The verdict

**The target/DTS/image/PR-1 material is in genuine review form on every host-checkable axis.**
What remains is: the v2 form generation (host work, well-specified but not done), the owner
identities, the submission-time dtbs_check, and the bench-attended bootstrap. The submission
cannot be posted today, but nothing in PR-1's scope is unreviewed or bench-only anymore.
