# Offline check of the frames the port posts (phase 24r, 2026-10-02)

With the handler table known (`handler-table.md`: id 3 -> `host_ready_msg_process`), the frames the
port puts in the SR ring can be checked **offline** against the rules that handler enforces - without a
device cycle.

The handler's validation, restated: a 16-bit length at message `+4`; a **zero top nibble at `+1`**;
`len - 13 <= 0x62`; payload copied from `+0xc`.

## Result

| | SR slot 0 (`omo_sr_msg`) | SR slot 1 (alg frame) |
| --- | --- | --- |
| actual size | 72 | 298 |
| descriptor length (node word1) | 72 | 298 |
| header length field @+4 | **48** | 298 |
| descriptor == size | YES | YES |
| **header == size** | **NO** | YES |
| `+1` top nibble | 0 (ok) | 0 (ok) |
| `len-13 <= 0x62` | PASS (35) | **FAIL (285)** |

Two findings, of different weight:

1. **Slot 0's two length fields disagree.** The SR node's descriptor length says 72 (the frame's size),
   while the message's own header at `+4` says 48. Slot 1 is **self-consistent** (298 and 298), which
   makes the disagreement in slot 0 look like a real inconsistency rather than a misreading of the
   layout. If the device trusts the header, it reads a 48-byte message where 72 bytes were announced -
   and a body that does not match its own descriptor is a candidate explanation for a silently dropped
   message.
2. **The alg frame exceeds the ready-message bound** (`298 - 13 = 285 > 98`). That is *expected* if it
   belongs to a different handler - it is an `alg get_2g_power_param` command, not a ready message -
   but it is worth stating that the port posts it in the *same ring*, at the *same time*, with the
   *same* id-bitmap send. If the device's dispatcher applies the ready-message validation to whichever
   frame it dequeues first, the alg frame would be rejected and the queue would stall at slot 0/1.

## The cheap test this suggests

Change the slot-0 descriptor length from `0x48` to `0x30` (matching the message's own header) and see
whether the device consumes the `out[0]` bit it never consumed before. That is a one-constant change,
and it targets the one field the port sets that the frame's own bytes contradict.

The alternative reading - that the header's `+4` is a *payload* length rather than a total - would make
48 + 12 = 60, still not 72, so the disagreement does not dissolve either way. Worth being explicit that
this is a **candidate**, not a proven defect: the frame is a live capture, so the vendor's own bytes
carry whatever the device accepted in that capture.

## Caveat carried forward

No offline check can settle it: only the device can say whether it accepts the frame. The value of this
pass is that it found a *specific, editable* field to test, rather than another register to write.
