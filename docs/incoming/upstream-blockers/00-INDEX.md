# Upstream blocker-reduction drafts - luofu 6.18 series (2026-11-21, one blocker left)

**STATUS: one blocker remains - the flashinfo reserved-memory binding/loader
acceptance.**  All active-DTS deltas are adopted after hardware boot-tests
(TWD f6685b80; MAC/switch/compatible c3ef2a50), and the in-series switch binding
`reg`/`reg-names` reorder was adopted with the review form rerun
(26/26 patch(1)+git am, checkpatch 0/58; release-line validator PASS).

## Remaining blocker

| # | blocker | status | artifact |
| --- | --- | --- | --- |
| 1 | `hsan,flashinfo_reserved` / loader hand-off binding | **drafted, host-side final option chosen** - documented `hisilicon,luofu-flashinfo` compatible first with the vendor string as fallback; needs maintainer direction plus the one loader boot-test described in the README (not yet run, not claimed) | `01-flashinfo-reserved-memory/` |

**Closed by hardware boot-tests / adopted without hardware:** TWD level-high,
MAC-nvmem removal, switch MMIO-first order (DTS and in-series binding),
cudy-compatible ordering + OpenWrt metadata.  The remaining draft-driver/YAML
review items are normal RFC content (the prioritized review is in
`05-driver-yaml-review.md`), not a DTS blocker task.

## Rules

- The flashinfo compatible-order change is the only hardware test left; exactly
  what to test is specified in `01-flashinfo-reserved-memory/README.md`.
- No curated repos were touched; no submission/email/web action was taken.
