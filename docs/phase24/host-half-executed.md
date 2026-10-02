# The host half runs: ack, clear, re-arm, dispatch - on the real registers (phase 24c, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-192518/`, `params=[hw=1 program=1 fw=1 release=1 msgsvc=1]`.

## The sequence, executed for the first time

```
omo-drv1: ---- mailbox service: ack/clear/re-arm + dispatch (10000 ms) ----
omo-drv1:   pending out[1] BAR0+0x3f1014 = 0x00000004 (baseline)
omo-drv1:   t=0ms pending 0x00000004 (was 0x00000004)
omo-drv1:     bit 2 pending -> dispatch handler[2]
omo-drv1:     ack   out[3] 0x4b9438 <= 0x00000001 readback=0x00000000
omo-drv1:     clear out[1] 0x3f1014 <= 0 readback=0x00000000
omo-drv1:     rearm out[4] 0x4b9414 <= 0x00000001 readback=0x00000000
omo-drv1:     after service: out[0]=0x00000000 out[1]=0x00000000 glue=0x00000000
omo-drv1:   mailbox service done: 1 words serviced in 10000 ms; final out[1]=0x00000000
```

This is the recovered host half from `docs/phase20/msg-host-half.md` actually performed against
registers that exist - the thing that had never happened, because every earlier attempt read or wrote
the page below the real one.

## What the registers say

| observation | meaning |
| --- | --- |
| `out[1] = 0x4` (bit 2) pending at entry | the firmware asserted its ready message and held it, as a device waiting for a host ack would |
| `ack out[3] <= 1` reads back **0** | self-clearing, exactly as phase 20 measured - the ack is a pulse, not a latched bit |
| `clear out[1] <= 0` reads back **0**, and stays 0 for the rest of the window | **the host clear takes effect and holds.** Phase 20's "the device never clears \`out[0]\`" describes the *device* not clearing its own mask; here the *host* clears it, and the write sticks |
| `rearm out[4] <= 1` reads back **0** | also self-clearing |
| glue status stays `0` throughout | nothing further asserted |
| `final out[1] = 0` after 10 s | the firmware did **not** re-assert after being serviced |

## What it means, and what it does not

**Does:** the port's driver can now read the firmware's pending message, acknowledge it, clear it, and
re-arm - the complete host half, verified by readback, on the device. Combined with the earlier result
(firmware loaded, CPU released with a 9/9 start signature, first word observed), the host/device
message path is connected end to end for the first time in this project **on the correct registers**.

**Does not:** produce a second message. The firmware emitted one word (bit 2, id 2) and after being
serviced produced nothing more in the 10 s window. That is consistent with phase 19's finding that the
takeover sees a single ready word - and the question of what the firmware needs to advance past it is
unchanged: the device-side gate phases 20/22 named. The difference now is that a host action
(ack/clear/re-arm) demonstrably reaches the device and has an effect, so that gate can be investigated
from a working baseline rather than through a dead address window.
