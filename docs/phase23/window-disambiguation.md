# Window disambiguation: the offsets are RIGHT, the takeover decode is not equivalent to the vendor's (phase 23i, 2026-10-02)

The `out[0]` question is answered, and the answer is better than a yes/no: it isolates the last
mapping difference between a vendor boot and a takeover.

## The measurement (one boot, evidence `build/register-dumps/exp/20261002-141440/`)

The driver read the mailbox at several candidate addresses plus the config word, all in one run:

```
cfg      PCI_BASE_ADDRESS_0 = 0x40000004
msg+000  BAR0+0x3f1000      = 0x000559e7     <- 59e7:0005, the PCI vendor:device id
out[0]   BAR0+0x3f1010      = 0x40000004     <- the BAR0 config word
out[1]   BAR0+0x3f1014      = 0x00000000
r39010   BAR0+0x39010       = 0x00000000
ete+000  BAR0+0x3f2000      = 0x0000010a     <- the ETE block, correct
```

Two of those reads are **config-space values**, not device-register values: `0x3f1000` returned the
VENDOR/DEVICE id and `0x3f1010` returned the BAR0 config word. So part of the mapped window was
aliasing PCI configuration space.

## The control that settles it: the same offsets on the vendor boot

Read through `devmem` on the **vendor-managed** boot (every partition healthy, vendor stack loaded):

| offset | takeover read | vendor-boot read | verdict |
| --- | --- | --- | --- |
| `0x403f1000` | `0x000559e7` | `0x0000010B` | vendor value is the real register |
| `0x403f1010` | `0x40000004` | `0x00000000` | vendor value is the real register |
| `0x403f2000` | `0x0000010a` | `0x0000010A` | **identical - ETE decodes in both** |
| `0x403b8000` | (not read) | `0x00000101` | matches `docs/phase18/inbound-map.md` |

**Conclusion, stated precisely:**

1. **The offset table is correct.** `out[0]` at BAR0 `0x3f1010`, the ETE block at `0x3f2000`, the IO
   vector at `0x3b8000` - all confirmed against the live vendor boot and against phase 18's dump.
   The arithmetic in `register-windows.md` stands.
2. **`out[0]` was NOT a live mailbox value in that run** - it was a config-space aliasing artefact, so
   the earlier `0x40000004` is explained and must not be read as device state.
3. **The ETE block decodes correctly in BOTH states** (`0x10A`), which is why the ring decode in
   `wifidrv1-both-blocks.md` is trustworthy even though the message block was not.
4. **The remaining difference is the region-3 decode itself under a takeover.** The vendor's own
   region driver programs the viewports on the `rev==0` membar path
   (`docs/phase18/inbound-map.md` A.4: viewport 0 block at `+0x104`, ctrl2 disable/enable, base,
   limit, target - the path this project reproduced), but a takeover that programs the viewports and
   then reads is evidently not landing on the same device region for the *low* part of the message
   window, while the ETE part decodes. That is a concrete, bounded difference to chase - not a
   mystery and not a wrong constant.

## What the next run must do

Program the viewports, then read back **the viewports themselves** (every register of viewport 3) and
compare against a live vendor boot's viewport 3 - the same differential method that settled the RC
routing in phase 22 (`rc-routing.md`). The hypothesis to test: our viewport 3 host range/limit does
not match the vendor's for the low message window, so part of it falls through to config space.

## State

Run `WIFIDRV1 RESULT: PASS`; device recovered and healthy (`WIPHY=2`, `IFACE=6`, calibration
`[SUCC]` both bands, `OMO_OFF=0`, no staged module, no watchdog, 0 real faults). CA `0x400392f0`
never written, RC misc window never read, no vendor module unloaded.
