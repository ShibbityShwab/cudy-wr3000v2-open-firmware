# Upstream blocker-reduction drafts - luofu 6.18 series (2026-11-21, final deltas adopted)

**STATUS: documentation + patch drafts only for the remaining work.**  The three
active-DTS blockers (6.18 MAC provisioning, switch MMIO-first order, cudy
compatible ordering) were **boot-tested PASS on image c3ef2a50** and adopted
(commit `6c2cc2f`); TWD was adopted earlier (image f6685b80).  **Two
blockers remain**: draft drivers/YAMLs and the flashinfo binding.  Current final
DTS 681 lines sha256 `9d96ac31...`; DTB 12,650 B sha256 `a0af2899...`
(pad 12,656 B sha256 `01b74561...`); DTS-0001 sha256 `c029fd44...`
(694 lines).  Current DTS-purpose baseline image **c3ef2a50** supersedes
f6685b80; 7421bb7a is the earlier verified baseline.

## Remaining blockers

| # | blocker | after this package | artifact |
| --- | --- | --- | --- |
| 1 | Drivers + YAMLs are still drafts | **open** - prioritized top-issue review; the in-series switch binding YAML still needs the MMIO-first reorder (DTS side boot-tested, binding-side change pending) | `05-driver-yaml-review.md`, `03-switch-binding-mmio-first/binding.diff` |
| 2 | `hsan,flashinfo_reserved` has no upstream binding | **drafted** - reserved-memory child binding + compatible/loader options; needs maintainer direction; one loader boot-test only if the vendor string is not first | `01-flashinfo-reserved-memory/` |

**Closed by hardware boot-tests:**

- TWD level-high (image f6685b80) - 2 CPUs, `GIC-0 29 Level twd`, counts ~100 Hz, ping 20/20.
- 6.18 MAC provisioning (image c3ef2a50) - nvmem cells removed, userspace MACs ...:73/...:74.
- Switch MMIO-first (image c3ef2a50) - switch data path works, br-lan up, ping 15/15.
- cudy compatible ordering + OpenWrt metadata (image c3ef2a50) - `board_name=cudy,wr3000-v2`, compatible order verified.

## What is hardware-dependent (remaining)

- Flashinfo binding: one loader boot-test only if the vendor compatible is moved
  from first position; otherwise documentation-only.
- Driver/YAML cleanups: code review, not hardware.

## Rules

- These files are proposals for the remaining work; adopted deltas are in the
  target DTS / target image recipe.  Any new active delta needs its own unit
  boot-test.
- Patch drafts are unified diffs against the tree state at their creation; they
  may need rebasing after maintainer review.
- No curated repos were touched; no submission/email/web action was taken.
