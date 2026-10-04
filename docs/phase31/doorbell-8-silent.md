# Doorbell 8 is also silent - and the interrupt block content is stable, not garbage (phase 31b, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-053006/`, `params=[... fwctx=1 intrsamp=1]`.

## The A/B

```
pre:      irqblock=41f0e8bd b872f7fa 3023f890 bf282b01 429d2301 f7f9d29e b148fbf8 30a9f894  ack=0
doorbell  out[2] <= 0x1 (bit 0):   NO CHANGE across 100 ms
doorbell  out[2] <= 0x8 (bit 3):   NO CHANGE across 100 ms
done:     unchanged, ack=0
```

**The firmware's own re-arm constant (8) latches nothing, exactly like 1.** The doorbell value was not
the gate. That retires the last single-value lead on the doorbell itself.

## A correction to my own claim, from the data

The previous run's report said the interrupt block "reads garbage, changing per boot". **Wrong**: the
8 words are **byte-identical across the two runs** (both boots read
`41f0e8bd b872f7fa 3023f890 bf282b01 429d2301 f7f9d29e b148fbf8 30a9f894`). Stable content is not
garbage. Moreover `0xe8bd` is a Thumb `pop` encoding - the content looks like **code or a code-like
table**, not interrupt registers. Both facts suggest the read at BAR0 `0x519100` is not landing on the
interrupt bitmap the firmware's literal `0x40161100` means - the firmware CPU's address map and the
host's PCIe CA map may differ for this range, unlike the mailbox CAs (which are provably shared).

## Where this leaves the doorbell->interrupt link

1. The doorbell write (values 1 and 8) produces no change anywhere the host can currently see.
2. The one register the firmware demonstrably touches (the enable bitmap at ITS 0x40161100) may not
   be reachable at the host address I computed - the mapping needs re-deriving for the 0x4016xxxx range
   before any further sampling there.
3. The vendor's init of that block is not in plat.ko (scan: zero references to 0x4016xxxx); the next
   static check is wifi.ko.

The chain stays: fully armed firmware, doorbell that never produces an observable interrupt.

