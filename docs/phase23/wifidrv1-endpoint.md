# wifidrv1: the driver's first endpoint interaction (phase 23, 2026-10-02)

Task: continue the port thread from `wifidrv0`, whose own "Limits" section named the gap - a wiphy and
a netdev, but **no hardware, no data path**. This phase adds the first hardware step and, just as
importantly, proves the safety gate behaves correctly when the hardware is not ours to take.

Every claim is **[measured]** (a value the device printed this session) or **[proven]** (quoted from
an earlier phase's disassembly/report).

## 1. What the module is

`lab/wifidrv1/wifidrv1.c` (529 lines) = the `wifidrv0` registration path (wiphy `omo-drv1` + managed
netdev `omowl1`, vendor struct offsets `0x128`/`0x1f0`/`0x540` unchanged **[proven]**,
`docs/phase14/wifidrv0.md`) **plus** an endpoint section that:

1. claims EP0 with the sequence `lab/eteprobe` already proved (`pci_enable_device`,
   `pci_request_mem_regions`, `PCI_COMMAND = 0x0007` with read-back);
2. maps **BAR0+0x39000** as a single 0x2000 window, which covers both the ETE/glue block
   (`+0x39000`, `docs/phase17/ete-engine.md` A.4) and the message/channel block (`+0x3a000`);
3. **decodes, read-only**, the ring program registers (SR ctrl/base/depth/wptr, DR base/depth/wptr),
   the message registers, and the channel-res register, bracketing the block with a re-read of a
   message register so a concurrent device write is visible;
4. never writes a BAR, never submits a descriptor, never rings a doorbell.

Safety: the entire hardware section is gated on module param `hw` (**default 0**). With `hw=0` the
module makes **no PCI access at all** and behaves exactly like `wifidrv0`. `out[5]` (CA `0x400392f0`)
is never written, and is only *read* behind a separate opt-in param.

## 2. Results, all on the live device

**`hw=0` - registration and lifecycle [measured].**

```
omo-drv1: init hw=0 read_msg5=0 (offsets: ops=296 ieee=496 priv=1344)
omo-drv1: hw=0 - registration-only load (no PCI access at all)
omo-drv1: add_virtual_intf name=omowl1 ifindex=20 rc=0
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=0 regs=absent
```

- `iw phy` lists `Wiphy omo-drv1` (index 2); `iw dev` lists `Interface omowl1` (ifindex 20);
- `ip link set omowl1 up` and `down` both succeed; `iw dev omowl1 info` reports the recorded
  offsets from the live kernel (`ops=296 ieee=496 priv=1344`, matching the vendor values the module
  hardcodes - an independent confirmation of the phase-14 ABI measurement);
- `rmmod wifidrv1` exits cleanly and leaves the vendor stack untouched (2 wiphys / 6 interfaces
  before and after).

**`hw=1` - the claim is REFUSED, by design [measured].**

```
omo-drv1: init hw=1 read_msg5=0 (offsets: ops=296 ieee=496 priv=1344)
omo-drv1: pci_request_mem_regions rc=-16 (vendor stack loaded?) - refusing
omo-drv1: hardware attach failed rc=-16 - continuing without it
```

`rc=-16` is `EBUSY`: the vendor's own driver owns the endpoint's memory regions. The module **refused
rather than fighting for them** and continued in registration-only mode. This is the designed
coexistence behaviour, and it is the correct outcome: `docs/phase15/bringup.md` proved the vendor
stack cannot be unloaded (the first `rmmod` step panics deterministically), so a port must share the
device rather than replace it.

**Consequence for the port plan, stated plainly:** with the vendor stack loaded, a from-scratch driver
cannot claim EP0 at all. The endpoint is reachable to us only in a takeover boot (vendor modules
hidden) - the configuration every `lab/` hardware module uses - or by cooperating with the vendor
stack rather than competing for the BARs. That is a design fact to build on, not a defect to fix.

## 3. Device state after the run

Removed cleanly (`rmmod wifidrv1`, staged copy deleted). Final health [measured]: `WIPHY=2`,
`IFACE=6`, both calibration bands `[SUCC]`, `OMO_OFF=0`, `hi5622v100_wifi`+`hi5622v100_plat` loaded,
our module absent, **0 real kernel faults**, pstore unchanged (blk-0/1/2 mtimes still 10:41 / 14:37 /
14:37, all pre-test). CA `0x400392f0` was never written; the RC misc window `0x10161000` was never
read; no vendor module was ever unloaded.

## 4. Where this leaves the port

| layer | state |
| --- | --- |
| board description | reconstructed (`docs/soc/luofu-r116.dts`, 1,581 lines) |
| kernel struct ABI | solved (`CONFIG_PM` delta; re-confirmed live this run: `ops=296 ieee=496 priv=1344`) |
| wiphy + netdev registration | **proven on hardware** (`omo-drv0`/`omowl0`, now `omo-drv1`/`omowl1`) |
| endpoint claim (coexisting with the vendor stack) | **measured refusal** (`EBUSY`) - reachable only in a takeover boot |
| register decode | implemented read-only; not yet exercised, because the claim cannot succeed while the vendor stack is loaded |
| data path (TX/RX) | not built; phase 22 showed the device-side accept gate holds even with rings programmed |

Next concrete step, implied by the above: run `wifidrv1 hw=1` **in a takeover boot** (vendor modules
hidden, watchdog armed first, via the proven harness), where the claim can succeed and the read-only
decode actually reports the SR/DR program registers. That is the first time this driver will see the
hardware it is being written for.
