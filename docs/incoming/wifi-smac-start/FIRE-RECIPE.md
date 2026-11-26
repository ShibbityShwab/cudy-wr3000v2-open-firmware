# FIRE-RECIPE - lane wifi-smac-start (task-215)

**The Lead fires. This lane is host-only and touches no hardware.** This recipe
is read-only: it settles the one open question from the decode (which firmware
field the Lead's "+672" datum is, and whether band1's SMAC context ever moves
from the boot device-base default to the attached-RAM-block state). Every
write is forbidden; every probe is a read of a known address.

## Preconditions (Lead side)

1. `tools/fire-preflight.sh` PASS, `tools/recovery-card.sh` READY, a human
   at the bench (the standing rules - this lane cannot arm them).
2. The box boots the R5 image (5 GHz armed but silent) - that is the state the
   decode explains.
3. Run the Lead's usual read-only probe harness; keep output under
   `build/` (gitignored). Capture the runbook line "R7 smac-start".

## R7 - the SMAC-context differential (read-only, ~60 words)

Read both bands AFTER 2.4 GHz is beaconing and the 5 GHz AP is armed
(both-band AP state, same as the 11:30 differential).

Device IO window: BAR0 offset = CA + 0x3B8000 - 0x40000000, phys = CA + 0x3B8000.
Firmware RAM window: BAR0 offset = CA + 0x6B8000, phys = 0x40000000 + CA + 0x6B8000.

| # | CA | width | expect | meaning |
| ---: | ---: | ---: | --- | --- |
| 1 | 0x18081C / 0x180830 | 4 | 0x00174BF0 / 0x00175A18 | wrapper band objects (sanity) |
| 2 | 0x174E30 | 4 | 0x00114F18 (band0) | object+576 = attach RAM block with r4=obj+0x40 |
| 3 | 0x174E5C | 4 | 0x40042000 (band0) | object+608 = device chain A base |
| 4 | 0x174E90 | 4 | **the Lead's +672 datum (0x114F18?)** | object+672 as the ISR reads it |
| 5 | 0x175C58 | 4 | 0x00114F18 expected if attached | band1 object+576 |
| 6 | 0x175C90 | 4 | 0x40062000 | band1 object+608 = device chain A base |
| 7 | 0x175CB8 | 4 | **0x40062000 (silent)** | band1 object+672 |
| 8 | 0x175C98 | 4 | ? | band1 object+640 (the r4+576 slot if r4=obj+0x20) |
| 9 | 0x174E70 | 4 | 0x00114F18 (band0) | band0 object+640 |
| 10 | two-step via #4: +228 / +232 | 4 | 0x00A00096 / 0 on working band | attached-block status |
| 11 | two-step via #7: +228 / +232 | 4 | 0 / 0 on silent band | device-IO default |
| 12 | 0x401015CC / 0x400024B0 | 4 / 2 | 0 / 0x64 on both | the refuted timer candidates (control) |
| 13 | RAM blocks 0x114F18 / 0x115128 / 0x183438 / 0x1817A0 / 0x180904 / 0x180E68 | 4 | nonzero, band-shared | attached block set |

Two-step rule: follow a pointer only if it is in firmware RAM
(0x00040000..0x00180000) or device IO (0x40000000..0x4011FFFF); any other value
is reported as "not a pointer", never dereferenced. Never read the RC misc
window (phys 0x10160000..0x1016FFFF). Never write any address.

## Interpretation matrix

| R7 result | verdict | next rung |
| --- | --- | --- |
| #4 = 0x114F18 AND #7 = 0x40062000 | band1 keeps the boot device-base default; band0 got the attach => firmware-internal attach ran for band0 only (C2) OR the attach is not re-run after the band-1 start | R8: re-read #4/#5/#6/#7 after a 2.4-only boot and after a 5G-only boot (same image); if band0 shows 0x114F18 only when 2.4 is up, C2 confirmed |
| #4 = 0x40042000 (band0 attach slot empty too) | both bands hold device defaults => the attach never runs on our stack; the discriminator is elsewhere (probe the TBTT block 0x401015xx per band instead) | R8b: read #13 blocks + TBTT block 0x40101584..AC per band |
| #5 = 0x114F18 (band1 attached) | attach is not the gap; the 5G gap is downstream of the SMAC context (TBTT/timer slot programming, bodies 0xAEB80/TBTT) | hand back to the TBTT lane with the new context map |
| #4/#5 values differ between boots by 0x20 | the Lead's "+672" offset is the firmware's obj+640 (r4+0x20) not obj+672; re-anchor all later probes on the firmware offsets from TABLES.md T3 | update TABLES.md T3 and re-run R7 |

## Conditional rungs (require a separate, explicitly-approved fire)

* **R8 host-step candidate.** Only if R7 shows the attach missing and the Lead
  decides to test a host message: try the stock-only g0-id1 config keys one at
  a time (top candidates 0x01CF, 0x03E4, 0x0073, 0x0000, 0x0069, 0x0005,
  all in the 0x84xxx family), one key per fire, re-read R7 after each. This is
  an H2D write and NOT part of this lane's read-only scope.
* **R9 firmware-patch hypothesis.** If no host key moves the context, the fix
  is a firmware-image change (vendor source needed; GPL request lane) or a
  host write of the +576/+608 fields through the firmware-RAM window. The
  latter is high-risk (it can corrupt the working 2.4 GHz context) and needs
  its own safety lane with a snapshot before the fire; do not improvise it.

## Abort rules

* Any denial from `fire-preflight`/bench-link: abort; do not work around.
* Any probe read that returns 0xFFFFFFFF/0xDEADBEEF-class values three times:
  stop, the window may have changed; re-derive addresses from TABLES.md.
* Never reboot the box just to re-read R7; the differential is only valid in
  the same boot (the 11:30 datum was taken that way).
* No write, no flash, no rmmod, no UART TX in this lane - ever.
