# NEXT DEVICE EXPERIMENT (phase 22) - queued and ready

Status: queued. Owner: the device lane (the router is a single critical section; run only when no other lane holds it).
Prepared by: lead, from three static lanes that all completed in under six minutes each.

## The verdict this experiment executes

Two independent static lanes converged on the same root cause from different evidence:

- `docs/phase22/rc-routing.md` (measured): the chip's two PCIe functions sit behind TWO root complexes.
  `0000:00:00.0` -> RC0 `pcie@0x10160000` (BAR0 `0x40000000`, BAR2 `0x41800000`); `0001:00:00.0` -> RC1
  `pcie@0x10164000` (BAR0 `0x58000000`, BAR2 `0x59800000`). The descriptor FETCH egresses EP0's port;
  the completion INTx arrives via EP1 (irq 209). Cross-boot differential: the EP0-decoded boot read our SR
  descriptors (`SR ch0 DEVICE INDEX 0x10 -> 0x400`), every EP1-only boot left them untouched (`SR+0x1c`
  frozen at `0x10`). The vendor programs BOTH endpoints' iATU.
- `docs/phase22/fw-sr-gate.md` (firmware RE): the firmware has NO per-channel fetch kick, and on EP0 the
  SR engine fetched while the firmware's own `pcie_msg_handle` never ran - so the fetch is not
  firmware-initiated at all. H1 (ranked first) is the same conclusion as above.

## The experiment: test H1-H4 in ONE boot, with the harness

Module: extend `lab/bothep` (or a sibling `lab/ep0win`) to:
1. claim BOTH endpoints (`pci_enable_device` + `pci_request_mem_regions` on each);
2. decode BOTH: region-3 viewport + the six inbound iATU viewports on each, and additionally program
   **EP0's BAR2 `0x41800000` outbound viewport 0** (`+0x000..0x018`) with the values the vendor's live
   boot shows (`0x004:0x80000000`, `0x008:0x80000000`, `0x010:0xFFFFFFFF`, `0x014:0x80000000`) and the six
   inbound viewports at `+0x104+0x200*i` with EP0 host bases/targets;
3. keep the registry/interrupt work on EP1 (irq 209, `PCI_INTERRUPT_LINE` write) as the current modules do;
4. load + release the firmware, post DR buffers, run the service thread with the SR pump and the
   post-release index re-assert (the fix in `lab/sr2`);
5. support the batch convention from `docs/phase22/exp-harness.md` so the runner can test several
   hypotheses in this one boot, writing per-entry `result.txt` with the observables.

Hypotheses in this boot, in order (stop the series at the first that changes the chip's state):
- H1: EP0 outbound window programmed (as above) -> observable: does `SR+0x1c` advance, does the H2D mask
  `out[0]` get cleared by the device, does the completion index move.
- H2: ETE interrupt block per-channel enable: sweep `0x40039508 = 0x3f201f1f` and then single bits
  -> observable: any transition in the glue status `0x400392ec` or a fetch.
- H3: producer commit edge/phase sequence variants on `SR+0x18` -> observable: `SR+0x1c` advance.
- H4: firmware H2D channel interrupt arm via glue `0x400392e8` / status `0x400392ec` -> observable:
  `out[0]` clear, id-1 reply.

Never write CA `0x400392f0`. Never read the RC misc window `0x10161000`. Arm
`/root/recover-exp.sh` with `start-stop-daemon -S -b -m` BEFORE staging; cancel it with the done flag and
run the recovery in the same run.

## How to run it

```
bash router-openwrt/tools/exp.sh        # one module, full cycle: arm -> stage -> boot -> capture -> recover -> health
bash router-openwrt/tools/batch.sh <list>   # N hypotheses in ONE boot; list = <label>|<param>
```

Both scripts are proven dry-run-clean by their author lane; the device run of them is the remaining proof
(success criterion SC2/SC3 of this run).

## Artifacts this brief consumes

- `opensource/docs/phase22/rc-routing.md` (routing verdict + exact writes)
- `opensource/docs/phase22/fw-sr-gate.md` (firmware path + H1-H5 with confirm/kill observables)
- `opensource/docs/phase22/exp-harness.md` (harness usage + module-side batch convention)
- prior device evidence: `opensource/build/register-dumps/{bothep,sibep,sr2,srt,txpath}/`
