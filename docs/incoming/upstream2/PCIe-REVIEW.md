# PCIe review items - luofu R116 (the gate 8)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** host-only review record, 2026-11-27 (goal round 60). The three PCIe
review items from `docs/incoming/upstream2/PR-READINESS.md` section 2 item 8, with
the exact binding/DT facts and the submission recommendation. Nothing here was
changed in the active DTS: the node stays `status = "okay"` for the bench line.

## 1. `acp-gate` - a bring-up property that must become an ACP power/clock domain

The two PCIe nodes carry:

~~~
pcie0: pcie@10160000 { ... acp-gate = <0x10100174 0x2000000 0x0>; status = "okay"; };
pcie1: pcie@10164000 { ... acp-gate = <0x10100174 0x4000000 0x0>; status = "okay"; };
~~~

Measured facts (pinned vendor tree, upstream-dts lane): the word at 0x10100174
gates the ACP (AMBA/PCIe coherence port) for the two RC instances; bit 25 =
RC0, bit 26 = RC1 (mask values 0x2000000 / 0x4000000). The property is a
bring-up adjacency: the driver pokes the gate register directly because no ACP
power/clock domain driver exists yet.

**Submission recommendation.** Do not carry `acp-gate` as a documented binding in
PR-1. Either (a) model the ACP as a `power-domains`/`clocks` provider with a
real driver (the clean form), or (b) keep the gate write inside the PCIe driver
as a documented bring-up TODO with no DT property. The current property has no
binding and would be an automatic `dtbs_check` failure if a binding were added
as-is.

## 2. No MSI/INTx

The PCIe controller exposes only the direct GIC SPI lines (the `radm`/`linkdown`
interrupts) and no MSI controller node; no `msi-parent`, no `interrupt-map` for
INTx. The stock loader/DT does not describe MSI either.

**Submission recommendation.** Document the absence: the controller is
INTx/MSI-less in this silicon/revision, and the PCIe host driver must be
polled/interrupt-driven over the SPI lines only. A reviewer must see this as a
deliberate statement, not an omission.

## 3. Static CFG0 viewport

The `iatu_rc`/`iatu_ep` properties encode the static iATU mapping and the CFG0
window is a static 1 MiB view (the reg property's second range). There is no
dynamic outbound-window allocation.

**Submission recommendation.** Keep as-is with a comment stating the static
design; no DT review action beyond documentation. The `iatu_rc`/`iatu_ep`
cell arrays are vendor-specific and have no mainline binding - they are part of
the same review item as the `acp-gate` (the PCIe binding proposal).

## The gate 8 verdict

Host-doable review work is complete: the three items are pinned to exact DT
facts, the submission recommendation for each is written, and none requires a
code change to the bench line. The remaining work is the PCIe binding proposal
(out of PR-1 scope per SUBMISSION-STATUS).
