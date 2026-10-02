# ep0win-run: the H1-H4 boot, executed (phase 22, 2026-10-02)

Task: the queued experiment from `docs/phase22/NEXT-EXPERIMENT.md`, finally run against hardware.
First device execution of the batch harness with the **EP0 outbound window** programmed while the
interrupt stays on the sibling endpoint - the "both halves live at once" configuration that no
earlier lane ever achieved.

Every claim below is **[measured]** (a value the device printed in this run) or **[inferred]** (a
mechanism derived from those values). Evidence dir:
`build/register-dumps/exp/20261002-114425-batch/` (runner log
`build/register-dumps/detached/ep0win.log`).

## 1. What was run

- Module: `lab/hccaccept` at `5ee29b7`, CI-built (run 37002219665, `completed/success`,
  artifact `hccaccept-ko`, `vermagic=5.10.201`).
- List: `lab/hccaccept/batch-h1.list`, 4 entries, `stopfirst=0` so every entry records.
- Params: `domain=0 domain2=1 program=1 outwin=1 devvabase=0x80000000 devvaend=0xffffffff
  hostcabase=0x80000000 release=1 useirq=1 irq=207 hostirq=207 useirq2=1 irq2=209 hostirq2=209
  rings=1 acpoff=0 enable=0 intr=0 srctrl=0 seq=0 svc=0 scanlen=0 hccwin=1500 stopfirst=0`.
- Runner: `tools/ep0win-detached.sh` (new; corrects `sr-carrier-detached.sh`, which passed the
  literal string `stage` to a script that takes a module path, and pointed at the pre-h1 artifact).
- Harness verdict: `BATCH RESULT: PASS hypotheses=4` - the run, the capture and the recovery all
  completed; this is a **harness** PASS, not a hypothesis PASS.

## 2. The setup was real: both endpoints claimed, EP0's window programmed

| step | measured line |
| --- | --- |
| EP0 claimed (`0000:00:00.0`, rc-routing's RC0) | `BAR0 base=0x40000000 (config space), BAR2=0x41800000 (iatu_bar1)` |
| sibling claimed (`0001:00:00.0`, RC1) | `ep0 BAR0 base=0x58000000, BAR2=0x59800000 (iatu)` |
| six inbound viewports on BOTH | `ep1 programmed 6 viewports` / `ep0 programmed 6 viewports`, each `readback=… match=YES` |
| **H1's act** - EP0 outbound | BEFORE `[0x004]=0 [0x008]=0 [0x010]=0x00000fff [0x014]=0` -> AFTER `[0x004]=0x80000000 [0x008]=0x80000000 [0x010]=0xffffffff [0x014]=0x80000000` (byte-for-byte the vendor's live window) |
| both IRQ lines requested | `request_irq(207) rc=0` and `ep0 request_irq(209) rc=0` |

Both endpoints enumerate as `59e7:0005` with `resource0 0x40000000-0x40ffffff` /
`resource2 0x41800000-0x41803fff` (domain 0) and `0x58000000` / `0x59800000` (domain 1) - the
mapping in `docs/phase22/rc-routing.md` is confirmed live.

## 3. Result: H1 does not explain the missing fetch - the fetch already happened

The per-entry table, identical across all four:

```
 #  label                  out0_cleared  sr1c        glue        irq
 1  h1a-baseline           n             0x00000400  0x00000000  100021
 2  h1b-sr-reassert-edge   n             0x00000400  0x00000000  100042
 3  h1c-dr-reassert-mbox   n             0x00000400  0x00000000  100063
 4  h1d-sr72-edge-iso      n             0x00000400  0x00000000  100084
```

`SR+0x1c = 0x400` is the **fetched** index (phase-20f `txpath` recorded exactly `0x10 -> 0x400` on
an EP0 boot). So in this configuration the SR engine's descriptor fetch is **not** the blocker: it
completed. Failure modes 3 and 4 of `NEXT-EXPERIMENT.md` are the ones realised - "the fetch happens
but the device never consumes the node", and "nothing in the mailbox changes". H1's window write is
necessary context but demonstrably not sufficient, and H2's `0x40039508` sweep and H3's commit-edge
variants (the `h1b`/`h1d` entries) changed nothing: glue status pinned at `0x00000000`, `out[0]`
never cleared by the device.

## 4. What DID move: the host half is now provably wired end to end

This run is the first with the interrupt live **and** the message service running, and the recovered
host half from `docs/phase20/msg-host-half.md` operated on real device traffic:

```
[post0 +1550ms] fw BSS +0x00 CA=0x01322c18 0x40080000 -> 0x00000000     <- chip released, BSS zeroed
[post0 +1570ms] pending out[1] CA=0x40039014 = 0x00000004                <- firmware's first word
[svc post0] ACK   out[3] CA=0x40101438 <= 0x00000001 readback=0x00000000
[svc post0] CLEAR out[1] CA=0x40039014 0x00000004 -> 0x00000000 readback=0x00000000
[svc post0] REARM out[4] CA=0x40101414 <= 0x00000001 readback=0x00000000
[svc post0] id=2 has no handler (unregistered)
```

The ack/re-arm CAs **read back `0`** - the endpoint does not latch our host-side writes - and the
chip's `out[0]` H2D mask toggles (`0 -> 0x8 -> 0`) on the device's own schedule, i.e. it is
self-driven, not driven by our service. Interrupt accounting is unambiguous:

```
done      (irq=207 irq_taken=0      irq_handled=0 msgs=0)        <- EP0's line never fires
done ep0  (irq=209 irq_taken=100085 irq_handled=7)               <- EP1's line serves everything
```

**So the phase-21 "irq_taken=0 in takeover" blocker is half-solved and half-ill-posed:** the
completion interrupt fires and is taken 100,085 times on irq 209, exactly as a vendor boot does. What
is missing is not the interrupt line but **the firmware's willingness to service the host**: the
device raises `out[1]` (id 2, "DEVICE READY") once per entry, our handler acks/clears/re-arms, and
the device then does nothing further - and `id=2 has no handler (unregistered)` names the concrete
gap on our side, a dispatch-table entry the vendor's `pcie_msg_init` installs and this takeover
does not.

## 5. Verdict and what it changes

- **H1: killed as a sufficient cause** [measured]. The fetch works with EP0's window programmed;
  programming both RCs' windows did not produce a single additional device action.
- **H2/H3/H4 in the `h1b/h1d` forms: negative** [measured] - no observable responded.
- **The live blocker moves to the message-service contract**: our ack/re-arm writes do not stick, and
  the id-2/id-6 handler table is unregistered. That is a *host-side* software gap, not a window,
  clock or interrupt-line gap, and it is testable by extending the dispatch table rather than by
  touching more registers.
- Harness status: the exp/batch harness is now **proven on hardware** by its own author lane
  (4 hypotheses, one boot, per-entry observables, automatic recovery, health gate PASS).

## 6. Device state after the run

Recovery ran and the router ended healthy: `WIPHY=2 IFACE=6`, calibration `[SUCC]` on **both**
bands (`get_2g_power_param`, `get_5g_power_param`), `hi5622v100_wifi`+`hi5622v100_plat` restored,
`OMO_OFF=0`, no staged module, no loader, no `/root/recover-exp.sh`, no `/root/rd-watch.sh`, no
`/root/omo-batch-list`, slot B (`bootflag=b`) still `omo-minimal-0.3 stock-2.5.24-20260727-122111`.
CA `0x400392f0` was never written; the RC misc window `0x10161000` was never read.
