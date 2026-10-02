# First write path: the oops, the cause, and the fix (phase 23k, 2026-10-02)

The first run with the write path enabled (`wr=1`) **panicked the router**. The cause was a bug in this
module, not the hardware. Recorded in full because the failure mode is instructive.

## The crash (pstore, verbatim)

```
[   39.834168] omo-drv1: ---- first write path: ETE ring programming ----
[   39.843885] Unable to handle kernel paging request at virtual address c9045508
[   39.860282] Internal error: Oops: 7 [#1] SMP ARM
[   39.865034] Modules linked in: wifidrv1(O+) ...
[   40.110838] PC is at omo_wifidrv1_init+0x98c/0x1000 [wifidrv1]
[   40.116666] LR is at omo_wifidrv1_init+0x980/0x1000 [wifidrv1]
[   40.393987] Kernel panic - not syncing: Fatal exception
```

Everything up to that line had worked: EP0 claimed, all six inbound viewports programmed with matching
readbacks, the SR/DR node arrays allocated (`SR ch0 nodes 272 bytes @ 0x83777000`, ...), and the
decode printed.

## The cause, from the address arithmetic

The faulting virtual address is the tell:

```
0xc9045508 - 0x1508 = 0xc9044000      <- the omo_msg base
```

`omo_msg` was an `ioremap(bar0_base + 0x3f0000, 0x1000)` - **0x1000 bytes**, covering
`0x3f0000..0x3f0fff`. But the write path's first act is binding write #1, the
`pcie_ete_intr_init` access at CA `0x40039508` = BAR0 offset `0x3f1508`, i.e. `omo_msg + 0x1508` -
**0x508 bytes past the end of the mapping**. The kernel faulted on the first such access.

The read-only code had been fine because it only touches `0x010..0x2f0`. Adding a 0x1508 access without
enlarging the map is exactly the kind of error that a read-only revision cannot expose.

## The fix (commit `35d63f5`)

```c
#define OMO_MSG_WIN    0x3f0000UL
#define OMO_MSG_BYTES  0x2000UL   /* covers 0x3f1010..0x3f1508 */
#define OMO_ETE_WIN    0x3f2000UL
#define OMO_WIN_BYTES  0x1000UL   /* the ETE block, +0x408..+0x6e8 */
```

Then **every** access offset was checked against its window rather than spot-checked:

| window | largest offset used | window size | fits |
| --- | --- | --- | --- |
| message (`omo_msg`) | `0x1508` | `0x2000` | yes |
| ETE (`omo_ete`) | `0x710` | `0x1000` | yes |

overflow count: 0.

## What went right: the safety design

This is the scenario the harness and the hard rules exist for, and they behaved exactly as designed:

- the run was **detached**, so a panicking box did not strand an agent;
- the **watchdog was armed before staging**, and the recovery ran;
- on the first probe after boot the harness reported `!! FAIL [run] device did not return fresh or
  experiment never finished` and proceeded to recovery and the health gate;
- the router came back **fully healthy**: `W=2`, `I=6`, calibration `[SUCC]` on both bands, vendor
  modules restored, `OMO_OFF=0`, no staged module, no watchdog, `FAIL=0` unhandled faults;
- pstore captured the crash text, which is how the bug was identified in one read.

Slot A remains stock, so even a boot-looping slot B would have been recoverable.

## Lesson recorded

"The module loaded and programmed the viewports" says nothing about the next line. **Every MMIO access
must be checked against the size this driver actually mapped** - and a new access added to a working
read-only path is the highest-risk edit in the module, because nothing in the read path exercises it.

## Status

Fixed, rebuilt (CI `37024551829`), **not yet re-run on hardware**. The next device step is the same
run again - viewport differential plus write path - with the corrected mapping.
