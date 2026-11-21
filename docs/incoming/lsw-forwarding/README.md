# LSW native forwarding - plan and first artifacts (2026-11-21)

Status: **planning + prototype artifacts only. Nothing in this directory has been run on
hardware. No register write is performed by anything here unless a human runs the write tool
explicitly with the documented flags and an attended bench.**

This directory is the "native L2/VLAN forwarding" lane requested after the 2026-11-21
multi-port decision gate ([../../MULTI-PORT-PLAN.md](../../MULTI-PORT-PLAN.md)): the CPU-copy
mirror workaround cannot identify the ingress jack (field-for-field identical completions),
so multi-port LAN/WAN needs the switch's own forwarding / CPU-delivery path.

## Contents

| file | purpose | risk |
| --- | --- | --- |
| `IMPLEMENTATION-PLAN.md` | the engineering plan: architecture, three ordered implementation routes, exact writes we can justify, netdev/coexistence model, bring-up sequence, risks, patch design | none |
| `MINIMAL-EXPERIMENT.md` | the first experiment to run when the bench is available - **command-line changes and read-only captures only, zero register writes** | none |
| `UNKNOWNS-AND-READS.md` | the [U] list with the exact attended reads/commands that would answer each item | none (read-only) |
| `capture-runtime-state.sh` | run **on the box** (stock or our image): read-only register/proc/devmem capture into a directory, with the exact address list | read-only |
| `diff-captures.py` | run **on the host**: diff two captures, classify counter-like vs persistent words, emit the candidate write plan | none |
| `plan-writes.sh` | run **on the host**: turn a candidate write plan into a reviewed, revertible on-box write script (rollback capture, preflight state check, readback verification, revert-on-mismatch); the generated script executes only with `--apply` plus `LSW_WRITE_ATTENDED=1` and `LSW_RECOVERY_ARMED=1` | write tool (generated, on-box only) |

## Safety rules encoded here

1. `capture-runtime-state.sh` has no write path at all and refuses to run if it cannot find a
   read-only devmem.
2. `plan-writes.sh` defaults to **print-only**. Execution needs three independent flags and is
   limited to an explicit address allow-list (LSW windows only; the `queue`/`flow` DDR windows,
   the CRG page and the PIE windows are outside the write allow-list).
3. Every proposed write carries its rollback word, captured immediately before the write; the
   plan refuses to write an address whose rollback value is missing.
4. The bench workaround (eth2 mirror + CPU-copy delivery) stays armed during the first
   experiments; a native attempt for one port must remove that port from the mirror mask first
   (patch 066 `mirror_src_mask`) so frames are not delivered twice.
5. The repository's hard rules are unchanged: UART read-only, recovery armed for every fire,
   no blind writes, no vendor binaries in the published half.
