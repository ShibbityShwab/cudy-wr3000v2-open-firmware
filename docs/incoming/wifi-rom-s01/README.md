# wifi-rom-s01 - ROM stage S0/S1, the CA 0x1BF000 handshake and the ETE engine-global gap

Host-only lane (`task-83`, 2026-11-23). Read `REPORT.md` for the full reconstruction; this file is
the short index.

## What is here

| file | purpose |
| --- | --- |
| `REPORT.md` | the reconstruction: handshake contract, partner analysis, ETE engine-global/doors, ROM vector decode, ROM contract map S0/S1, discriminating read-only pack, definitive host-side conclusion |
| `tools/rom_s01.py` | self-checking image/module analyzer: `selftest` (70 checks), `handshake`, `rom`, `engine`, `pack` |

## Headline results

* **CA 0x1BF000 is the ROM<->image boot contract page**: `+0` progress bitmap, `+8` inter-core
  handshake status, `+C` magic 0xABCD. Entry A (CPU 0x46970) writes `{0,0xABCD}`, cleans the
  cache line, rings CA 0x40161F00 = 0x20000 and spins until the partner writes **2**. The partner
  is mask-ROM code (S1), not image code: no image site writes 2 or can materialise the D1 values
  `0x001BD000 / 0x001BD0D8`, which are the ROM boot-log `{arena base, base+0xD8}` still present
  because in that boot entry A never executed its handshake store.
* **Golden vendor boot** reads `2 / 0xABCD` - the contract is confirmed end-to-end.
* **ETE engine-global (+0x000..0x3FF) and doors (+0x7C0/+0x7D0) have no image or vendor-host
  writer**; the doors appear only as static CA fields paired with blocks 0/1. The firmware itself
  snapshots `{0x4003A000, 0xC28}` into PKTRAM (BAR0 **0x2B5800**) on any exception-taking boot -
  the decisive free read for the engine-global state.
* **ROM vector window** (readable at BAR0 == CPU): vectors dispatch to target words at CA 0x20/24/
  28/2C/30 + IRQ **0x34** + FIQ **0x38** (never read); CA 0x1E4 = 0x4005C double-indirects into
  the image header handler table. Mask 0x0A proves the ROM->image exception ABI is live.
* **Read-only pack** (REPORT section 6): handshake-page time series, boot-log arena, entry-A state,
  hybrid crash-dump ETE snapshot, live ETE window, ROM vector/handler words - plus the same set on
  a stock vendor boot as the golden diff. **Correction:** event-controller CAs 0x4016xxxx are not
  host-readable; earlier "0" reads were region-4 aliases.
* **Host-side conclusion:** no host write can wake the ETE engine; remaining work is read-only
  (golden diff + ROM vector target) and then ROM-side. No further host register work is justified.

## Reproduce

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-rom-s01/tools/rom_s01.py
$PY $T selftest   # 70/70 PASS
$PY $T all        # full printed reconstruction + pack
```

The tool reads the local firmware image and vendor modules in place and prints derived facts only;
it never opens a device and never copies vendor bytes.
