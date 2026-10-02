# wifidrv1: the full sequence now runs - claim, viewports, rings, release (phase 23r, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-162146/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 wr=1 release=1 verbose=1]`.

## What ran, in order, in one takeover boot

```
omo-drv1: BAR0 base=0x40000000 (config) cfg[0x004]=0x0006 MEM|MASTER=set
omo-drv1: mapped message BAR0+0x3f0000 and ETE BAR0+0x3f2000
omo-drv1: programming the six inbound viewports
omo-drv1:   v0..v5 ... (all six read back = the vendor reference)
omo-drv1: ---- out[0] disambiguation (labels are ABSOLUTE BAR0 offsets) ----
omo-drv1:   out[0]   BAR0+0x3f0010       = 0x40000004
omo-drv1: ---- first write path: ETE ring programming ----
omo-drv1:   SR ch0..2 / DR ch3..6 base/depth/wptr readback match=YES
omo-drv1:   glue chn_res 0x400392e8 [0x02e8] <= 0x00000000 match=YES
omo-drv1: ---- write path done: writes that failed readback = 0 ----
omo-drv1: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-drv1: release readback = 0x00005a5a match=YES
omo-drv1: add_virtual_intf name=omowl1 ifindex=13 rc=0
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded
```

**Everything the port knows how to do now runs in one boot, with zero failed readbacks**, and the
release — the act that starts the Wi-Fi CPU — is among them.

## Two corrections this run settles

1. **The labels are honest now.** `out[0]` prints as `BAR0+0x3f0010`, which is the address the read
   actually uses, and the value `0x40000004` at that address **matches the vendor boot exactly**
   (verified read-only on the vendor boot: `0x403f0010 = 0x40000004`). The message block decodes
   correctly; the earlier "aliasing" was never real.
2. **The release works from this driver.** `0x5a5a -> CA 0x40000108`, readback match. The Wi-Fi CPU
   is now started by our own module, in a sequence that also owns the viewports and the rings.

## What was NOT observed, stated plainly

The mailbox registers read the same values before and after the release in this boot
(`out[0]=0x40000004`, `out[1]=0`); the module logs the decode **before** the ring/release sequence and
does not poll afterwards, so **this run does not show the released firmware's first words** - phase 19
saw them at ~+1.85 s after release. To see them here, the module needs a post-release poll of the
mailbox. That is the next increment, and it is small: a loop reading `out[1]` for a bounded window
after the release write.

## Device state

Recovered healthy: `W=2`, `I=6`, calibration `[SUCC]` on both bands, `OFF=0`, vendor modules
present, our module removed, **`FAIL=0`** real faults. One RTNL warning appeared during `rmmod`
(`free_netdev+0x194: RTNL assertion failed`) - the netdev teardown path needs to hold the RTNL lock;
it is a warning, not a fault, and the module exits cleanly (`omo-drv1: exit done`). Worth fixing, noted
here rather than left implicit.

CA `0x400392f0` never written; the RC misc window `0x10161000` never read; no vendor module unloaded.
