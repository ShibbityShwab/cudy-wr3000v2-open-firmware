# The native CFE switch forwarding track - consolidated status

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** host+capture documentation consolidated 2026-11-27 (goal round 65). This is
the "keep native CFE switch forwarding as a separate track" clause of the objective:
the switch (LSW) forwarding work is intentionally *not* part of PR-1 and not part of the
Wi-Fi line. The authoritative detail lives in `opensource/docs/incoming/lsw-forwarding/`.

## What is proven (the M1a milestone)

The CPU-copy mirror workaround cannot identify the ingress jack (field-for-field
identical completions), so multi-port LAN/WAN needs the switch's own forwarding /
CPU-delivery path. The 2026-11-21 lane decoded the stock's mechanism - IFC punts
broadcast to the CPU and routes unicast into the NAPT/flow engine - and then
**replayed the decoded NAPT init on the bench**: proof image `5800c9ab` with the
NAPT init replay and the mirror off gives **native port=11 deliveries, ping 200/200
and working SSH**. That is the M1a proof.

## The track's artifacts (all read-only or fail-closed)

| the piece | the state |
| --- | --- |
| the M1a probe + the NAPT-init replay + observers | proven on the bench (the proof image `5800c9ab`) |
| the W22 draft (the kernel-side NAPT init + the PIE exception delivery) | patch drafts, not yet in the release series |
| the M1b plan (the per-port native delivery + the LAN-to-LAN matrix) | planned, no writes |
| the full CFE reversal (`cfe/`) | the static model + the stock capture + the flow-window geometry solved + the forwarded-phase vendor writes decoded + the final M2 verdict |
| the capture tooling (`capture-runtime-state.sh` / `diff-captures.py` / `plan-writes.sh`) | read-only capture + print-only planning; the generated writes need three independent flags + an attended bench |

## The safety model (unchanged)

Every write tool defaults to print-only; execution needs `--apply` plus
`LSW_WRITE_ATTENDED=1` and `LSW_RECOVERY_ARMED=1`, is limited to an explicit address
allow-list (the LSW/NAPT/L2 windows), and captures a rollback state first. The
`capture-runtime-state.sh` refuses to run without a read-only devmem.

## The track's position in the objective

**M1a is proven; the W22 release integration and the M1b per-port matrix are the next
bench-attended steps.** Nothing in this track blocks the target/DTS/image PR (PR-1)
and nothing in PR-1 depends on it. It remains a documented separate track, exactly as
the objective requires.
