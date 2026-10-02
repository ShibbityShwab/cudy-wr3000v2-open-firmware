# Both device-code leads are negative (phase 24n, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-223208/`, `params=[... srpost=1 msgsvc=1 rearm_val=8 pollms=25]`.

Phase 24m read the Wi-Fi **firmware's own** `pcie_msg_handle` and found two differences from the host
side that looked like they could explain the silence. Both were tested; both are negative.

## Lead 1: the firmware's ack register is `out[5]` - it never asserts

The poll now reads `out[5]` (CA `0x400392f0`) alongside `out[0]`/`out[1]` - a *read*, which the
project rule allows:

```
omo-drv1:   t=0    out[0]=0x00000000 out[1]=0x00000000 out[5]=0x00000000 (baseline)
omo-drv1:   t=25ms out[0] 0x00000000 -> 0x00000000, out[1] 0x00000000 -> 0x00000040, out[5] 0x00000000 -> 0x00000000
omo-drv1:   t=450ms out[0] 0x00000000 -> 0x00000000, out[1] 0x00000040 -> 0x00000004, out[5] 0x00000000 -> 0x00000000
```

**`out[5]` stays zero through the whole dialogue.** So in this configuration the firmware does not
acknowledge at `out[5]` - the hypothesis that the host had been missing the firmware's answer in an
unread register is **refuted**, at least for the sequence a takeover reaches. (It remains possible that
`out[5]` is used later in the dialogue, once a transfer completes - the register is a *send* arm on the
host side.)

## Lead 2: the third write is 8 on the device, 1 on the host - it makes no difference

```
omo-drv1:   t=0ms pending 0x00000004 (was 0x00000004)
omo-drv1:     bit 2 pending -> dispatch handler[2]
omo-drv1:     ack   out[3] 0x4b9438 <= 0x00000001 readback=0x00000000
omo-drv1:     clear out[1] 0x3f1014 <= 0 readback=0x00000000
omo-drv1:     rearm out[4] 0x4b9414 <= 0x00000008 readback=0x00000000     <- 8, not 1
omo-drv1:     after service: out[0]=0x00000000 out[1]=0x00000000 glue=0x00000000
omo-drv1:   mailbox service done: 1 words serviced in 10000 ms; final out[1]=0x00000000
```

The write took (`<= 0x00000008`), the readback is 0 as for the value 1 (self-clearing either way), and
**the behaviour is identical to `rearm_val=1`**: service succeeds, the pending word clears, and the
firmware produces nothing further. So the host/device asymmetry in that value does not gate anything a
takeover can observe. `rearm_val` stays a parameter; 1 remains the default because it is the value the
host-side disassembly gives.

## What has now been ruled out, cumulatively

The dialogue has been probed from every angle the host can reach, and each candidate has been tested
rather than assumed:

| candidate | result | evidence |
| --- | --- | --- |
| the mailbox is silent because of the address window | **was the bug** | `ROOT-CAUSE-window-base.md` |
| the host half is missing | implemented; runs | `host-half-executed.md` |
| the id-6 word is missing because the poll was blind | refuted; id 6 is produced by our own driver | `port-elicits-dialogue.md` |
| the SR descriptor post is the trigger | **confirmed by A/B** | `id6-trigger-proven.md` |
| the DR producer commit gates a deposit | refuted | `dr-commit-not-the-gate.md` |
| the firmware acks at `out[5]` | refuted | this file |
| the re-arm value must be 8 | refuted | this file |

## Where the frontier actually is

Every host-reachable register and sequence is now either implemented and working (claim, viewports,
rings, SR post, firmware load, release, signature, first words, ack/clear/re-arm/dispatch) or tested
and eliminated. What is left is not a host register:

1. **The HCC command dialogue.** There is still no id-1 "device plat ready" word and no payload; the
   414-entry command table, the calibration commands and the radio configuration that would give the
   chip data to transfer are not built. An unconfigured, unassociated Wi-Fi chip plausibly has nothing
   to DMA - which would make "no DR deposit" the *expected* result rather than a fault.
2. **The per-chip callbacks** (`cbs->rx` and the BAL layer) that the vendor binds outside `plat.ko`;
   these are host structures a takeover cannot stand up.
3. **The interrupt source**, which can only assert once a transfer completes.

The next honest step is therefore **not** another host register write but the command dialogue - either
by porting the vendor's command construction from `hi5622v100_wifi.ko` (available locally) or by
tracing what the firmware's own state machine waits for after id 2. That is a larger piece of work than
any step so far, and it is the thing standing between this port and a working radio.
