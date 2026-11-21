# Upstream blocker-reduction drafts - luofu 6.18 series (2026-11-21, ZERO blockers)

**STATUS: ZERO upstream blockers remain.**  The last blocker - the
`hsan,flashinfo_reserved` loader hand-off binding - was closed by hardware
boot-test image **6e9c54d0**: the target DTS now uses
`compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved"` and
the stock loader accepted the documented first compatible (bootfip staging
lines, 2 CPUs, cmdline/rootfsb, compatible property all verified).  Adopted in
commit `71b2167`; evidence in `build/evidence-bench-gates/flashinfo-loader/`.

Current final artefacts: DTS 681 lines sha256 `381b2e8c...`; DTB 12,678 B
sha256 `62574b11...` (pad 12,680 B sha256 `8d79794e...`); DTS-0001
sha256 `886f3ad0...` (694 lines); current DTS-purpose baseline image
**6e9c54d0** (supersedes c3ef2a50; f6685b80 and 7421bb7a earlier).

## Closed blockers

- TWD level-high - boot-tested f6685b80.
- 6.18 MAC provisioning, switch MMIO-first order (DTS + in-series binding),
  cudy-compatible ordering + OpenWrt metadata - boot-tested c3ef2a50.
- flashinfo loader hand-off binding - boot-tested 6e9c54d0 (this file's
  `01-flashinfo-reserved-memory/README.md` has the full PASS record).

The in-series driver/YAML review items in `05-driver-yaml-review.md` are
normal RFC submission content, not blocker tasks.

## Rules

- No remaining hardware test is required for a documented blocker; any future
  active-DTS change still needs its own boot-test.
- No curated repos were touched; no submission/email/web action was taken.
