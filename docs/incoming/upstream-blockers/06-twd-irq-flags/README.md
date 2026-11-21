# Blocker 6 - TWD timer interrupt flags: measured hardware data and draft

## Measured on hardware (2026-11-21, known-good image 7421bb7a)

GICD base `0x10181000`, read by the lead on the unit:

| register | value |
| --- | --- |
| GICD_CTLR | `0x00000001` |
| GICD_TYPER | `0x0000fc25` |
| GICD_ICFGR0 | `0xaaaaaaaa` |
| GICD_ICFGR1 | `0x7dc00000` |
| GICD_ICFGR2 | `0x55555555` |

Our kernel reports `GIC-0 29 Edge twd` in `/proc/interrupts`.  ID 29 is
TWD PPI 13 (hwirq 29 = 16 + 13).  The ICFGR1 field for ID 29 is bits 27:26 =
`0b11` - a nonstandard/reserved encoding - but **GIC-400 PPI configuration is
read-only/fixed by the SoC integration**, so the DTS flag does not reprogram
hardware.  The observed "Edge" description comes from the fixed read-only value,
not from the devicetree.

Standard Cortex-A9 platforms (e.g. vexpress-v2p-ca9) describe the TWD timer with
`IRQ_TYPE_LEVEL_HIGH` (their 0xf04 flags; for this 2-CPU board the equivalent
is `GIC_CPU_MASK_SIMPLE(2) | IRQ_TYPE_LEVEL_HIGH` = `0x304`).

## Recommendation

Draft the board DTS as:

    interrupts = <GIC_PPI 13 (GIC_CPU_MASK_SIMPLE(2) | IRQ_TYPE_LEVEL_HIGH)>;

This follows the standard binding convention and is likely acceptable upstream.
It is an **active DTS delta** and must be boot-tested before adoption (lead has
offered to run the install/boot test).  It is a separate delta from the
conservative cleanup and is not bundled with it.  The measured ICFGR value is
documented here as the hardware fact.

## Draft artifact

- `dts-twd-level-high.diff` - adds the GIC/irq includes and changes only the
  TWD `interrupts` property; no other active change.

## Remaining hardware dependency

One boot-test with the level-high draft: the PPI configuration does not change
(hardware read-only), but the active DTS delta rule still requires a boot-test
before adoption.  After that, blocker 6 is closed as far as the devicetree goes
(the measured read-only encoding remains documented).
