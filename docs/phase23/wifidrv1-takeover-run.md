# wifidrv1 takeover run: the driver's first endpoint ownership (phase 23b, 2026-10-02)

Follow-up to `wifidrv1-endpoint.md`. Phase 23 established that with the vendor stack loaded the
endpoint claim returns `EBUSY`, so the decode can only run in a takeover boot. This is that run.

Harness: `tools/exp.sh wifidrv1.ko hw=1 verbose=1`, launched detached via
`tools/wifidrv1-detached.sh` (watchdog armed before staging). `EXP_DONE_CMD` was overridden to the
driver's own marker (`omo-drv1: init done`) because `wifidrv1` does not print the experiment
modules' `: done (` line - without that override the harness would have timed out on a run that had
in fact succeeded.

**Verdict: `WIFIDRV1 RESULT: PASS` - the EP0 claim succeeded and the read-only decode ran.**
Evidence: `build/register-dumps/exp/20261002-120525/` (dmesg.txt, module-log.txt, interrupts.txt,
lsmod.txt, health.txt).

## What the takeover boot produced

```
omo-drv1: init hw=1 read_msg5=0 (offsets: ops=296 ieee=496 priv=1344)
omo-drv1: BAR0 base=0x40000000 (config) BAR0_hi=0x0 cfg[0x004]=0x0006 MEM|MASTER=set
omo-drv1: SR_CTRL   BAR0+0x00008 = 0x00000000
omo-drv1: SR_BASE   BAR0+0x00010 = 0x00000000
...  (all ring + message registers read 0x00000000)
omo-drv1: msg1 bracket before=0x00000000 after=0x00000000 (stable)
omo-drv1: add_virtual_intf name=omowl1 ifindex=13 rc=0
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded
```

Two findings, one of which is a defect in this module that this run exposed:

**[measured] The claim works in a takeover boot.** `pci_request_mem_regions` succeeded, BAR0
enumerated at `0x40000000`, the region-3 viewport decodes (cfg `MEM|MASTER` set), and the wiphy +
netdev registered on top of it (`regs=decoded`). This is the first time the port's own driver has
owned this endpoint end to end.

**[proven] The ring/message registers read zero because the module mapped the WRONG WINDOW for the
SR/DR registers.** The offsets it used (`0x008/0x010/0x014/0x018` and `0x030/0x034/0x038`, inherited
from `lab/eteprobe`) are offsets inside the **message** block at `BAR0+0x39000`, not the ETE ring
block. The ETE block's device CA is `0x4003a000` (static resource `.data+0x2944`,
`docs/phase17/ete-engine.md` A.1), whose BAR0 offset is **`0x3f2000`** - the "offset correction
(measured, phase 20b)" note in `docs/phase20/runtime-msg.md`. The all-zero read is therefore not a
surprise finding about the hardware; it is the expected read of the message block's unused low
offsets. `lab/eteprobe`'s own constants carry the same labelling collision, which is how it
propagated.

**Corrected** in the same phase (commit `1aef42a`): the module now maps **two** windows -
`omo_msg` at `BAR0+0x39000` (the six mailbox CAs) and `omo_ete` at `BAR0+0x3f2000` (the ring program
registers) - and decodes all **3 SR channels** (base `0x400`, stride `0x114`) and **4 DR channels**
(base `0x590`, stride `0x6c`) with base/depth/wptr/rptr/ctrl each, as `docs/phase20/runtime-msg.md`
specifies. The corrected module has not yet been rebuilt and re-run; that is the next action.

## Device state after the run

`health.txt` from the harness: `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0
LOADER=0 RECOVER=0`, and an independent post-run probe agrees: 2 wiphys, 6 interfaces, calibration
`[SUCC]` on **both** bands, no `.omo-off` leftovers, no staged module, no watchdog, **0 real kernel
faults** (pstore unchanged). CA `0x400392f0` was never written; the RC misc window `0x10161000` was
never read; no vendor module was unloaded. The run also exercised the harness end to end in the
correct safety order for a non-experiment module.
