# Upstream blocker-reduction drafts - luofu 6.18 series (2026-11-21)

**STATUS: documentation + patch drafts only.  These drafts are NOT the release
series and are not applied anywhere.**  They reduce the six blockers recorded in
`../../../upstream/series-6.18/07-dts-upstream-bindings.md` section 6 and the
submission package.  Nothing here was boot-tested; any active-devicetree or
target change drafted here must be boot-tested on the unit before adoption.

## Priority table

| # | blocker | after this package | artifact |
| --- | --- | --- | --- |
| 1 | drivers/YAMLs are still drafts | **open** - prioritized top-issue review only (no code rewrites in this pass) | `05-driver-yaml-review.md` |
| 2 | `hsan,flashinfo_reserved` has no upstream binding | **drafted** - reserved-memory child binding + compatible/loader-compatibility options; needs maintainer direction and (only if the vendor string is dropped from first position) one hardware boot-test | `01-flashinfo-reserved-memory/` |
| 3 | `fixed-layout`/`mac-base` v6.19 on 6.18 | **path defined** - 6.18 uses the already-implemented userspace/vendor MAC provisioning; DTS nvmem removal drafted; needs maintainer confirmation + boot-test | `02-mac-provisioning-6.18/` |
| 4 | switch queue-first reg-names / node name | **drafted** - binding + DTS MMIO-first reorder; driver needs no code change (by-name lookups); no in-tree users; boot-test required | `03-switch-binding-mmio-first/` |
| 5 | cudy compatible ordering + OpenWrt metadata | **drafted** - upstream compatible order + OpenWrt SUPPORTED_DEVICES/board_name diffs; boot-test + sysupgrade-metadata check required | `04-compatible-order-openwrt/` |
| 6 | TWD timer interrupt flags | **hardware-measured, draft ready** - PPI configuration is read-only/fixed; standard level-high DTS flag drafted; active DTS delta needs the lead's boot-test | `06-twd-irq-flags/` |

## What is hardware-dependent

- **TWD flags** (blocker 6): one boot-test with the level-high draft; the PPI
  configuration is read-only, so no register-programming risk is expected.
- **MAC nvmem removal** (blocker 3): active DTS delta - boot-test that the GMACs
  still probe with the random fallback and the userspace MAC provisioning works.
- **Switch reorder** (blocker 4): active DTS delta - boot-test that the switch
  probe still validates the same resources and the node remains disabled.
- **Compatible ordering** (blocker 5): active DTS delta - boot-test the image
  and check `board_name`/sysupgrade metadata with the new first compatible.
- **Flashinfo binding** (blocker 2): hardware test only if the vendor compatible
  is no longer the first/only match; the binding text itself is documentation.

## What is not hardware-dependent

- Upstream acceptance of the flashinfo reserved-memory binding (maintainer
  direction), the v6.19 nvmem follow-up, binding review comments on the switch
  model, and the driver/YAML cleanups in the priority review.

## Rules

- These files are proposals; the release series and the conservative DTS are
  unchanged (DTS still 13,041-byte-DTB, sha256 `49fe5c47...`).
- Patch drafts are unified diffs against the current tree/series state; they are
  illustrative and may need rebasing after maintainer review.
- No curated repos were touched; no submission/email/web action was taken.
