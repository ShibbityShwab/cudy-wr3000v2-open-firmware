# Firmware loaded, released, and still silent - the poll result (phase 23u, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-172556/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 wr=1 fw=1 release=1 verbose=1]`.

## What ran

```
omo-drv1: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
omo-drv1: firmware written to BAR0+0x6f8000 (928920 bytes)
omo-drv1: firmware readback diffs=0 match=YES
omo-drv1: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-drv1: release readback = 0x00005a5a match=YES
omo-drv1: ---- post-release mailbox poll (500 ms interval, 8000 ms total) ----
omo-drv1:   t=0  out[0]=0x40000004 out[1]=0x00000000 (baseline)
omo-drv1:   poll done: 0 transitions in 8000 ms; final out[0]=0x40000004 out[1]=0x00000000
omo-drv1:   NOTE no mailbox transition in this window - the firmware produced nothing
```

**The firmware is now genuinely placed and verified (diffs=0, byte-for-byte against the source), the
release is confirmed, and the firmware still produced nothing in 8 s** - where phase 19 saw `out[1]`
go `0 -> 0x40` at +1.85 s under the same release write.

## So the missing-firmware theory was wrong, and that is worth stating

The previous run released a chip with no image and produced nothing; this run placed the image
correctly and produced nothing *either*. The loader was a real omission worth fixing, but it was not
the cause of the silence. The cause is something else, and the two candidate explanations left are:

1. **the poll window or cadence** - phase 19's first word arrived at +1.85 s; this poll covers 8 s at
   500 ms, so cadence is not obviously the problem, but phase 19's boot **also stood up more state
   before releasing** (its sequence includes the message-service context and interrupt work);
2. **the device-side gate** the record keeps landing on: the firmware's dispatcher is reached only
   through the glue ISR route, and phase 20/22 measured that a raw takeover never enters it.

## A comparison that is NOT a difference - corrected here

Reading the mailbox on the **vendor** boot right now:

| offset | vendor boot | my takeover |
| --- | --- | --- |
| `out[0]` `0x3f0010` | `0x40000004` | `0x40000004` |
| `out[1]` `0x3f0014` | `0x00000000` | `0x00000000` |
| doorbell `0x3f02d4` | `0x00000000` | `0x00000000` |
| chn_res `0x3f02e8` | `0x00000000` | `0x00000000` |
| intr `0x3f1508` | `0x3F201818` | (written by us, readback matched) |

**Identical.** An earlier draft of this note read `out[0] = 0x40000004` as "something asserted a
message before any host write" - that was wrong: the vendor boot shows the same value, so it is simply
the register's resting state (bit 2 + bit 30), not evidence of activity. Recorded so the next reader
does not build on it.

## Which leaves the ISR route as the single remaining candidate

Everything host-side that the record identifies as testable has now been exercised *by this driver*:
claim, viewports (vendor-identical), ring programming (0 failures), firmware load (diffs=0), release
(readback match), and a post-release poll that observes rather than assumes. The mailbox resting state
matches the vendor's exactly. What has never been stood up is the **glue ISR route** -
`oal_pcie_transfer_done` reading and clearing the PCIe glue status and fanning out to the message
handler - which phase 20 and phase 22 both name as the point a raw takeover never reaches.

That is the next experiment, and it is a different kind of work from the register pokes: it means
standing up a service thread with a status-poll loop plus the handler fan-out, in the shape the vendor
runs.
