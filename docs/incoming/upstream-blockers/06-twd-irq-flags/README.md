# Blocker 6 - TWD timer interrupt flags: measured, boot-tested, CLOSED

**Status: CLOSED (2026-11-21).**  Adopted as a boot-tested active DTS change in
submodule commit `5b401c1` (the level-high draft below was applied to the
single-source DTS).  Current final DTS: 700 lines, sha256
`13427d8e2bf7624b77fc6735932f79b9b25bf40dff1a0c859fc5cc3515b54de4`; DTB
13,041 B sha256
`4fde76df165689cdb859a6f79223300edc8fb27d1d239abea131d25726cbc1a9` (pad
13,048 B sha256
`4b50de73bebf6241ccb1ac62907c1de9b49a7ec7bcc6f18286f638541456e890`);
new DTS-purpose hardware baseline image **f6685b80** (previous verified baseline
`7421bb7a` remains recorded).

## Measured hardware data (2026-11-21, on previous baseline image 7421bb7a)

GICD base `0x10181000`, read on the unit:

| register | value |
| --- | --- |
| GICD_CTLR | `0x00000001` |
| GICD_TYPER | `0x0000fc25` |
| GICD_ICFGR0 | `0xaaaaaaaa` |
| GICD_ICFGR1 | `0x7dc00000` |
| GICD_ICFGR2 | `0x55555555` |

On the previous baseline the kernel reported `GIC-0 29 Edge twd`; ID 29 is
TWD PPI 13 (hwirq 29 = 16 + 13).  The GICD_ICFGR1 field for ID 29 (bits 27:26)
reads `0b11`, a nonstandard/reserved encoding, but **GIC-400 PPI
configuration is read-only/fixed by the SoC integration**, so the devicetree
flag does not reprogram hardware.

## Boot-test PASS (image f6685b80)

The level-high delta was built from the current target DTS with this draft
applied and installed with the verified telnet installer (PASS; pair b,
selector 0x11):

- boot: linux-6.18.60, 2 CPUs;
- `/proc/interrupts` now reports **`GIC-0 29 Level twd`** (previously
  `Edge`);
- TWD counts increment at ~100 Hz on both CPUs;
- host ping 20/20, 0% loss;
- no Oops/BUG/Kernel panic/RCU stall; uptime stable.

**Conclusion:** the standard `IRQ_TYPE_LEVEL_HIGH` encoding is correct for
this board.  The delta is adopted (commit `5b401c1`), DTS-0001 was
regenerated (sha256
`e9c9ed58b1a198bad16283d048a0498ee02297a8e1645f194df3f713c5d236b1`, 713
lines) and the release-line validator passed with the new DTB.

## Retained draft artifact (already applied)

- `dts-twd-level-high.diff` - adds the GIC/irq includes and changes only the
  TWD `interrupts` property; applied in commit `5b401c1`, retained for
  audit.

## Remaining hardware dependency

None for blocker 6.  The measured read-only encoding stays documented here as a
hardware fact; no further DTS change is planned.
