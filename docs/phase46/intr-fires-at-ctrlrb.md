# THE GATE IS LOCALIZED: the H2D interrupt FIRES at the ctrl-rb and still never reaches the firmware (phase 46, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-144441/`, params include intrsamp=1 with the
phase-46 raw/masked status sampling.

## The measurement

```
[intrsamp] pre: ack=0 raw(0x2e4)=0x08 masked(0x2ec)=0x08
[intrsamp] doorbell out[2] <= 0x1 (bit 0): CHANGE iter 0: raw 0x08 -> 0x09, masked 0x08 -> 0x09
[intrsamp] doorbell out[2] <= 0x8 (bit 3): no further change
[intrsamp] done: ack=0 raw=0x09 masked=0x09
```

Per the verified sibling register map (phase 45, the mp17c pcie_ctrl_rb_regs.h fetched and checked):
0x2e4 bit 0 = `host2device_tx_intr_raw_status`, 0x2ec bit 0 = the masked status.  The doorbell
write (0x2d4 bit 0) **sets the raw status bit 0 and the masked status bit 0**.  (The pre-existing
bit 3 = `device2host_rx_intr` latched from the boot dialogue, never cleared because the clear
register is the forbidden CA 0x400392f0.)

## What this settles - the first mechanism-level localization

1. The doorbell generates the interrupt: raw + masked both latch.  (Phases 31-35's "no observable
   effect" was sampling the wrong register - the ack - while the status latches sat one register
   away.)
2. The mask is open (bit 0 unmasked, the vendor's own 0x20 value).
3. The ack stays 0 and the dispatcher never runs (phase 35's proof).

**So the gate is downstream of the ctrl-rb: the interrupt latches in the ctrl-rb status but is
never delivered to the firmware's interrupt controller (line 0x4C).**  The remaining link is the
ctrl-rb -> firmware interrupt-controller routing/enable - the 0x4016xxxx block the phase-34 run
proved PCIe-unreachable, or a source-enable in the firmware's interrupt-controller init.

## Next steps

1. [static] Which interrupt-controller source id is the ctrl-rb host2device line, and does the
   firmware's init ENABLE it?  The firmware's enable fn (0x86ff4) writes the bitmap at 0x40161100;
   enumerate the register(id,...) calls in pcie_msg_init and their ids - if the ctrl-rb's source id
   is never registered/enabled, that is the gate's last link.
2. [static] Mine the sibling tree for the firmware-side interrupt-controller mapping (the source-id
   list, the enable register semantics) - the mp17c tree holds the full register set.
3. [live, only if 1-2 name a host-visible register] the named write.

