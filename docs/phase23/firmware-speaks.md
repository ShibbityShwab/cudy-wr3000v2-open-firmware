# The firmware speaks: our driver releases the CPU and reads its first word (phase 23y, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-183509/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 fw=1 release=1 verbose=1]`.

## The result

```
omo-drv1: mapped message BAR0+0x3f1000 and ETE BAR0+0x3f2000   <- corrected base
omo-drv1: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
omo-drv1: firmware readback diffs=0 match=YES
omo-drv1: [sig pre ] BSS=b5e0ed57/1151f103/aada1209/a71b7f5e dcoldo=ffffffff pbank=ffffffff abank=ffffffff tcxo=0/1
omo-drv1: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-drv1: release readback = 0x00005a5a match=YES
omo-drv1: [sig post] BSS=00000000/00000000/00000000/00000000 dcoldo=260d4184 pbank=00000312 abank=0000010c tcxo=1/2
omo-drv1: [sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
omo-drv1: ---- post-release mailbox poll (500 ms interval, 8000 ms total) ----
omo-drv1:   t=0  out[0]=0x00000000 out[1]=0x00000000 (baseline)
omo-drv1:   t=500ms out[0] 0x00000000 -> 0x00000000, out[1] 0x00000000 -> 0x00000004
omo-drv1:     out[1] bit 2 set (id 2)
omo-drv1:   poll done: 1 transitions in 8000 ms; final out[0]=0x00000000 out[1]=0x00000004
```

**The port's own driver now does the whole sequence and hears the firmware answer**:

1. claims EP0 (BAR0 `0x40000000`, `MEM|MASTER` set);
2. maps the corrected windows (message `0x3f1000`, ETE `0x3f2000`);
3. writes FIRMWARE.bin to `BAR0+0x6f8000` and verifies it byte-for-byte (`diffs=0`);
4. **releases the CPU** (`0x5a5a` -> CA `0x40000108`, readback match);
5. **proves the CPU started** - 9/9 signature registers changed, `dcoldo_vset 0xffffffff -> 0x260d4184`,
   `pbank_code -> 0x00000312`, BSS zeroed;
6. and **reads the firmware's first HCC message** - `out[1] 0 -> 0x04` (bit 2, id 2) at +500 ms.

This reproduces phase 19's central result (the `0x4` word, bit 2) from *our own driver*, with the
CPU-start signature and the firmware verification alongside it in the same boot.

## Why it took so long, recorded so it does not recur

The word had been there all along. The message window was mapped at `0x3f0000` instead of `0x3f1000`,
so the poll watched `0x3f0014` while the firmware wrote `0x3f1014` - and every "the firmware produced
nothing" conclusion in this session was a consequence of that (see `ROOT-CAUSE-window-base.md`).

What finally broke it open was adding an observable I had been **assuming** rather than measuring: the
CPU-start signature. A release readback proves the register latched; only the signature proves the CPU
ran. Once the chip was shown to be running, "the firmware is silent" stopped being tenable and the
address question followed immediately.

## Device state

Recovered healthy: `W=2`, `I=6`, calibration `[SUCC]` on both bands, `OFF=0`, vendor modules loaded,
our module removed, `FAIL=0` real faults. CA `0x400392f0` never written; RC misc window never read;
no vendor module unloaded.

## What this does and does not open

**Does**: the host half of the message service is now demonstrably connected - the firmware's words are
readable, the dead-address class of bug is closed, and every subsequent experiment (the ack/re-arm
write, the id-2/id-6 dispatch, the H2D send) now runs against registers that actually exist.

**Does not**: make the dialogue complete. The firmware emits its single ready word, as phase 19
recorded; whether it advances past that still depends on the device-side gate phases 20/22 identified.
But the decisive difference is that from here, host actions are observed on the right registers.
