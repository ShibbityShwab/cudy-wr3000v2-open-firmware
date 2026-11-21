# LSW native forwarding - plan and first artifacts (2026-11-21)

Status: **planning + prototype artifacts only.** No register write is performed by anything here
unless a human runs the write tool explicitly with the documented flags and an attended bench.
Hardware rounds so far: E0-a mirror-off (exception path works natively, ARP only), E0-b
stock-vs-ours read-only capture, the M1a probes (**both NULL** - tagact write ignored, up_port no
effect), the consistent-stock procfs capture that resolved the mechanism (IFC punts broadcast to
CPU and routes unicast into the NAPT/flow engine), and finally **M1a PROVEN**: proof image
5800c9ab with the decoded NAPT init replay and the mirror off gives native port=11 deliveries,
ping 200/200 and working SSH. Next: W22 (kernel-side NAPT init + PIE delivery, drafts and release
integration in [w22/](w22/W22-DESIGN.md)) and M1b (per-port / LAN-to-LAN, [m1b/](m1b/M1B-PLAN.md)).
See [E0A-ANALYSIS.md](E0A-ANALYSIS.md), [E0B-ANALYSIS.md](E0B-ANALYSIS.md), [E0B-RESULTS.md](E0B-RESULTS.md).
The current hardware baseline is image c3ef2a50 (E0-b ran on the 6e9c54d0 family; the M1a proof
image is 5800c9ab). A new full switch/CFE reversal phase is in [cfe/](cfe/README.md): a read-only
stock carveout + MMIO table dump (dump request and collector ready) plus the static decode of the
vendor CFE tables and an optional M2 offload design (not needed for the release line).

This directory is the "native L2/VLAN forwarding" lane requested after the 2026-11-21
multi-port decision gate ([../../MULTI-PORT-PLAN.md](../../MULTI-PORT-PLAN.md)): the CPU-copy
mirror workaround cannot identify the ingress jack (field-for-field identical completions),
so multi-port LAN/WAN needs the switch's own forwarding / CPU-delivery path.

## Contents

| file | purpose | risk |
| --- | --- | --- |
| `IMPLEMENTATION-PLAN.md` | the engineering plan: architecture, three ordered implementation routes, exact writes we can justify, netdev/coexistence model, bring-up sequence, risks, patch design (revision after E0-a: sections 2.1, 4.4) | none |
| `E0A-ANALYSIS.md` | host-side analysis of the mirror-off run: what status=37/err=1 means (class marker, not frame error), what is actually missing (unicast-to-CPU), milestone split M1a/M1b, revised E0-b target | none |
| `E0B-ANALYSIS.md` | host-side decode of the E0-b captures and the reviewed M1a write proposal: tagact entry 1 decode, pfe_glb up_port decode, L2 counter decode, exclusions, expected observable, risks | none |
| `m1a/` | the reviewed M1a probe: plan files, fail-closed on-box scripts (tagact entry 1; pfe_glb up_port), read-only observer, attended session runbook (both probes ran NULL - see SESSION.md closeout) | generated write scripts - on-box only with flags |
| `m1a/napt-init/` | decoded NAPT/flow init replay: proposal, fail-closed on-box script, read-only observers and the proof record (proof-image/RESULT.md) - **M1a proven** | generated write script - on-box only with flags |
| `w22/` | release integration: kernel-side W22 NAPT init and PIE exception-delivery patch drafts, design and release-line validation plan | patch drafts only; not yet in the release series |
| `m1b/` | next hardware experiment: per-port native delivery and LAN-to-LAN test matrix, observations and rollback rules | none (plan) |
| `cfe/` | full switch/CFE reversal lane: static CFE table model, our-image dump validation (flow-window geometry solved), stock-/dev/mem limitation, remaining read request, optional M2 design + verdict | read-only reads only |
| `MINIMAL-EXPERIMENT.md` | the first experiment to run when the bench is available - **command-line changes and read-only captures only, zero register writes** | none |
| `UNKNOWNS-AND-READS.md` | the [U] list with the exact attended reads/commands that would answer each item | none (read-only) |
| `capture-runtime-state.sh` | run **on the box** (stock or our image): read-only register/proc/devmem capture into a directory, with the exact address list | read-only |
| `diff-captures.py` | run **on the host**: diff two captures, classify counter-like vs persistent words, emit the candidate write plan | none |
| `plan-writes.sh` | run **on the host**: turn a candidate write plan into a reviewed, revertible on-box write script (rollback capture, preflight state check, readback verification, revert-on-mismatch); the generated script executes only with `--apply` plus `LSW_WRITE_ATTENDED=1` and `LSW_RECOVERY_ARMED=1` | write tool (generated, on-box only) |

## Safety rules encoded here

1. `capture-runtime-state.sh` has no write path at all and refuses to run if it cannot find a
   read-only devmem.
2. `plan-writes.sh` defaults to **print-only**. Execution needs three independent flags and is
   limited to an explicit address allow-list (LSW windows only; the NAPT window and the L2 window,
   the `queue`/`flow` DDR windows, the CRG page and the PIE windows are refused by name).
3. Every proposed write carries its rollback word, captured immediately before the write; the
   plan refuses to write an address whose rollback value is missing.
4. The bench workaround (eth2 mirror + CPU-copy delivery) stays armed during the first
   experiments; a native attempt for one port must remove that port from the mirror mask first
   (patch 066 `mirror_src_mask`) so frames are not delivered twice.
5. The repository's hard rules are unchanged: UART read-only, recovery armed for every fire,
   no blind writes, no vendor binaries in the published half.
