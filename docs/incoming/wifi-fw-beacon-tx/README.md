# wifi-fw-beacon-tx - the 5 GHz beacon TX path after the RF is programmed

**Lane:** `wifi-fw-beacon-tx` (shared task **task-192**). **HOST-ONLY**:
this lane read the pinned local vendor firmware image in place; it never
contacted the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The F2 fire (2026-11-27 23:30) programmed the 5 GHz
RF: the field-B-corrected g0-id1 key 0x01F7 (len 1, body {0x03}, hdr[3]=0)
made the firmware write mode 3 and the full ch36-64 RF register image for the
band-1 object (R = device CA 0x400B2000). The AP is AP-ENABLED and the page is
golden - but no 5 GHz beacon reaches the air, while the 2.4 GHz AP beacons with
the same driver and the same message family. This lane decodes what the
firmware does between "AP enabled" and "beacon on air", which per-band state
must exist for band 1, and what to read (and which minimal re-post) can close
the gap without another blind fire.

Deliverables:

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the beacon TX chain, the per-band state, the ranked causes and what stays [U] |
| [TABLES.md](TABLES.md) | every address, table, instruction and probe |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | R0 read-only probe pass, interpretation, the minimal re-post ladder, abort rules |
| [tools/fw_beacon_tx_re.py](tools/fw_beacon_tx_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 105/105 PASS) |
| evidence/ | `selftest.txt`, `decode.txt`, `tables.txt`, `probes.txt`, `frames.txt` |

Bottom line (details in REPORT.md):

1. **Band binding is per module context.** `hdr[3]` ("field B") selects one of
   **19 module contexts** (ctx[k] = CA 0x119758 + k*0xB40). The g0-id1 key
   **0x0064 VAP-add** worker CA 0x449E4 binds ctx[body[4]] to the band in
   **body[0x12]** (0 = 2.4 GHz, 1 = 5 GHz) by writing ctx+0x4B0 (per-link),
   ctx+0x4B4 (bank ctx) and ctx+0x4B8 (band object, CA 0x15CA08 + 0x1B68*band;
   band1 = 0x15E570 = the F2 bench P). Every later per-band message
   (0x0124/0x0127/0x0065...) acts on whatever band that ctx is bound to.
2. **The AP config (0x0127) has no band field and no beacon-enable field.** It
   stages only VAP state: beacon_interval -> VAP+0x10, dtim -> VAP+0x14,
   MD present/caps -> VAP+0x138 bits 0..2, MDID -> VAP+0x13A, hidden_ssid,
   HT/VHT hints. The band is carried by the VAP-add, not by 0x0127.
3. **The per-link beacon state is created by the VAP start (0x0065) on the
   FIRST start of a VAP on that band** (fields [V], role names [I]): per-link
   +0x6B8 (280 B allocation), +0x6BC (aux object), +0x6C0 (92 B allocation),
   +0x6C4 = 16 and **+0x6C6 = 1 (beacon state enabled)**. The worker then calls the
   band-start CA 0x971D8, which invokes the runtime callback table
   G = CA 0x1747C4: **G+0x370 (MAC start)** and **G+0x374 (beacon start)**.
4. **The per-band MAC/TBTT hardware differs and is initialised per band.**
   band0 = device CA 0x40110000, band1 = 0x40114000 (delta 0x4000), each with
   its own 23-entry init table (band0 CA 0x105714 / band1 CA 0x105770; band1
   sets 0x548/0x54C = 0x6101 vs 0x0101 and 0x588/0x594 = 0x35/0x24 vs
   0x33/0x22). CA 0x4AF0C resets/starts the band selected by u8(perlink+0x11D)
   (0 -> 0x401105xx, 1 -> 0x401145xx) and sets the SMAC IRQ enable
   (0x4010C220 bits 1|8|0x20|0x1000, 0x40101220 = 1 for band 1 / 2 for band 0).
   The enable-TRX worker CA 0xE8520 has a band-1-only register pair
   0x4010E150/0x4010E154 (band0: 0x4010820C/0x40108214).
5. **No vendor "extra beacon message" exists in tables A/B.** 0x0067 is the
   VAP stop/teardown worker, not a beacon enable; the beacon transmit is armed
   internally by the VAP start -> band start hook path above. The key table scan
   and the worker decode are in TABLES T8/T9.
6. **The per-band TBTT ISR address stays [U]**: the image carries the
   `smac_tbtt_5g_isr`/`smac_tbtt_2g_isr` **names** (CA 0x1229E8/0x1229FC,
   name blob CA 0x12266C..0x122BA0) but no code pointer and no reference to the
   blob; the arming register pair is identified per band (T6) but which of the
   per-band block registers is the TBTT timer/bucket must be read on device
   (FIRE-RECIPE R0/R3). This lane lists the read-only probes that decide it.

No vendor binary, firmware byte, calibration value, MAC or secret is stored in
this lane; probe output stays under build/ (gitignored).