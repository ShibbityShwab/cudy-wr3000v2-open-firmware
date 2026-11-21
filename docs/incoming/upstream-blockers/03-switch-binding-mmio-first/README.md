# Blocker 4 - switch binding: MMIO-first reg-names

## Current state

The in-series binding (`Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml`,
added by 0016, extended by 0021) lists the two engine DDR regions first:
`reg-names = "queue", "flow", "dp", ...`, so a node unit address must be the
queue DDR base - awkward for an MMIO device and flagged as a blocker.

The driver looks resources up **by name**
(`platform_get_resource_byname`/`devm_platform_ioremap_resource_byname`)
and validates the two DDR regions by name/size; it does not depend on array
order.  The enum order in `luofu-switch.c` is internal and can stay.

## Proposal

- Binding: 15 MMIO windows first (order `dp, ips, fam, pe, eqm, eps, prbs,
  pfe_glb, pa, ifc, l2, napt, mcc, opp, pub`), then the two DDR regions
  `queue`, `flow`; update the `reg` description.  The node unit
  address stays `ethernet-switch@11000000` (first reg becomes the dp window).
- DTS: reorder `reg` and `reg-names` to the same order; values,
  sizes, clocks, resets and port children unchanged.
- Driver: **no code change required** (by-name resource lookup); keep the enum
  order and by-name array as-is.  Only comments that mention "devicetree address
  order" should be refreshed if the patch is applied.

## Compatibility notes

- There are no in-tree/upstream users of this draft binding yet; the DTS ABI can
  change before the binding is merged.  The V6.18 target DTS and the upstream
  DTS-0001 must be updated together.
- The compiled DTB changes only in property order/unit-address-consistent
  reg order; register values, resources, clocks, resets and probe behaviour are
  unchanged (driver by-name).  Still an **active DTS delta**: boot-test the
  switch probe (resource validation, node disabled, no writes) before relying on
  it.
- If a reviewer prefers separate DDR/memory nodes or a `memory-region` for
  the engine DDR, that is a larger binding redesign; this draft is the minimal
  safe reorder.

## Draft artifacts

- `binding.diff` - reg/reg-names/example reorder in the switch binding.
- `dts.diff` - matching DTS `reg`/`reg-names` reorder.

These are not applied to the release series; apply only with a boot-test and a
binding-review round.
