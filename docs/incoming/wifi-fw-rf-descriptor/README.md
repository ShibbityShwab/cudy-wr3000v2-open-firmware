# wifi-fw-rf-descriptor - what sets the RF descriptor pointer / mode gate

**Lane:** `wifi-fw-rf-descriptor` (shared task **task-189**). **HOST-ONLY**:
this lane read the pinned local vendor firmware image and host module in place;
it never contacted the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The last 5 GHz blocker: the band-1 RF programme
(CA 0x40684 -> CA 0xA2FA2) never runs. The committed rf-programme lane read the
chain as `perlink+0xE20+0x1A00` (inline) and the probe pack therefore reads
CPU 0x178238. This lane re-derives the chain from the instructions and finds
that +0xE20 is a **pointer dereference**, O is the band object's **published
per-link**, and the gate that stays 0 is the RF **mode byte** `P+0xF04`,
which the stock host arms with a **separate H2D config event, key 0x01F7**.

Deliverables:

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | corrected chain, writers, host trigger, band differential, probes, limits |
| [TABLES.md](TABLES.md) | every address, instruction, table entry and probe value |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the read-only chain probe, the 0x01F7 arm, rungs and abort rules |
| [tools/fw_rf_descriptor_re.py](tools/fw_rf_descriptor_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 62/62 PASS) |
| evidence/ | `selftest.txt`, `decode.txt`, `probes.txt`, `frame.txt` |

Bottom line (details in REPORT.md):

1. the chain is `perlink = *(ctx+0x4B0); P = *(perlink+0xE20); O = *(P+0x1A00);
   R = *(O+0x2D8)`; P is a pointer and equals the band object (= `ctx+0x4B8`).
2. O is written by the pre-HCC boot task at CA 0x48A68 (published per-link) and
   the bench already measured it non-zero.
3. the RF mode gate byte `P+0xF04` has exactly one writer in the image:
   CA 0xB5D1C inside CA 0xB5B9C, the **g0-id1 config-event worker for H2D key
   0x01F7** (table A entry 93). The only shipped host emitter is
   `hmac_config_set_country_for_dfs` (WAL command `set_country_for_dfs`).
   Our five-frame ladder never sends it -> mode stays 0 -> silent skip.
4. no image instruction writes `perlink+0xE20` or `perlink+0x2D8`: those two
   pointers come from outside the pinned image; the probe must read them.
5. the corrected read-only probe pack and the 0x01F7 fire recipe are in
   TABLES.md / FIRE-RECIPE.md.
