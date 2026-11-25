# wifi-fw-h2d-stall - the firmware H2D stall (out[0]=0x00000008), decoded

Lane: fw-h2d-stall (shared task **task-181**), 2026-11-27. Host-only RE; **the Lead fires**.

| file | what |
| --- | --- |
| `REPORT.md` | the out[0]/H2D flow decoded (host writer + firmware dispatcher + PD4 poller + native IRQ 0x4C path), the clear-before-dispatch rule, the two bench frames behind the stall, the ranked hypotheses H1-H5 with falsifiers, and the [U] leftovers |
| `FIRE-RECIPE.md` | the discriminating one-boot-per-arm recipe (R-D0..R-D5) with the read-only exception-record observable (BAR0 0x6F800C), the PC decode table, the decision table and the stop rules |
| `tools/verify_decode.py` | re-derives every quoted offset/mnemonic/symbol from the reader's own local artifacts (no vendor bytes committed) |

Start with the one-liner in `REPORT.md` §0: **out[0]=0x8 is the unconsumed H2D SR-announce bit
(bit 3), and the dispatcher clears it *before* calling handlers - so a stuck announce means the
previous dispatch faulted or wedged, not that the announced frame is malformed.** The decisive
bench read is the firmware exception record (BAR0 `0x6F800C`), which the PD4 patch preserves.

No bench contact was made by this lane; no fire was executed. Local artifacts are never committed.
