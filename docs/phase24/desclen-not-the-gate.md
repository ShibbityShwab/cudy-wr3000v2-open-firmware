# The SR descriptor length is not the gate either (phase 24t, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-233007/`,
`params=[... srpost=1 sr_desclen=0x30 msgsvc=1 send=3 pollms=25]`.

## The test and the result

`frame-field-check.md` found that the port's SR slot-0 descriptor announced 72 bytes while the
message's own header said 48, and proposed one constant as the test: announce the header's value
instead. With `sr_desclen=0x30`:

```
omo-drv1: SR ch0 posted 32 nodes word0=0x83f8e000; commit SR+0x18 <= 0x00000400 readback=0x00000400
omo-drv1:   t=25ms  out[1] 0x00000000 -> 0x00000040     bit 6 set (id 6)
omo-drv1:   t=475ms out[1] 0x00000040 -> 0x00000004     bit 2 set (id 2)
omo-drv1:     bit 2 pending -> dispatch handler[2]
omo-drv1: ---- H2D send: id 3 (bitmap 0x00000008) via out[0] + doorbell out[2] ----
omo-drv1:   out[0] 0x3f1010 0x00000000 -> 0x00000008 readback=0x00000008 match=YES
omo-drv1:   NOTE the sent bit is STILL SET - the device did not consume it
```

**No change.** The dialogue is identical (id 6, then id 2) and the device does not consume the `out[0]`
bit, exactly as with the captured length. So the disagreement between the two length fields, real as it
is, is **not** what the device is waiting on.

Worth noting what the run does confirm, on the way: the port's dispatch line prints
`bit 2 pending -> dispatch handler[2]` - which, per `handler-table.md`, **is**
`device_plat_ready_msg_process`. The device's readiness message is being received and identified
correctly by the port's own code; the record's "no device plat ready word" is what was wrong.

## What has now been eliminated, and what that leaves

Every host-side candidate has been tested:

| candidate | result |
| --- | --- |
| the address window | **was the bug** |
| the host half (ack/clear/re-arm/dispatch) | implemented; runs and holds |
| the poll missing the first word | refuted (id 6 produced by our own driver) |
| the SR descriptor post | **confirmed as the id-6 trigger** |
| the DR producer commit | refuted |
| the firmware acking at `out[5]` | refuted |
| the re-arm value (8 vs 1) | refuted |
| the SR descriptor length (72 vs 48) | refuted (this file) |

The ID-level handshake is correct (**we send id 3 = `host_ready`; the device sends id 2 =
`device_plat_ready`**). The device still does not take the notification. What is left is not a host
register value at all:

1. **Device-side accept state** - what phases 20/22 concluded, and what nothing the host can write has
   changed: eight distinct host-side candidates have now been tested against it.
2. **The queue and worker machinery** in `plat.ko` between `hcc_queue_add_msg` and `pcie_msg_send` -
   the port posts a ring and rings a bit directly, skipping the vendor's retry-tolerant queue entirely.
3. **The receive path's per-chip callbacks** the vendor binds outside `plat.ko`.

The honest summary after this run: **the host side of this port is complete and every one of its knobs
has been measured; the remaining gate is on the device's terms.** Further host-side register
experiments have a poor expected value - each new candidate takes a build and a boot cycle and the last
eight have all been negative. The two directions with real information left in them are reading
`plat.ko`'s queue/worker path (no device needed) and the vendor SDL/GPL source request.

Device state after: `W=2 I=6 OFF=0 OMO=0 FAIL=0`, calibration `[SUCC]` both bands.
