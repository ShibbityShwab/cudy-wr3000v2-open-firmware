# wifi-fw-rf-programme - the firmware 5 GHz RF programme + the band-1 beacon engine

**Lane:** wifi-fw-rf-programme (shared task **task-182**). HOST-ONLY, read-only:
decodes the pinned local vendor firmware image; no device contact, no fire.
**The Lead fires.**

Read in this order:

1. **REPORT.md** - the corrected call graph, the CA 0x101890 predicate, the
   CA 0xA2FA2/CA 0x89924 RF register image, the beacon path, and the ranked
   candidate causes C1-C5.
2. **TABLES.md** - every address, field window, register image word, the
   read-only probe pack (CPU / BAR0 / physical) and the decision tree.
3. **FIRE-RECIPE.md** - R0 read-only probe pass, R1 zero-rebuild ordering
   test, R2 host patch, R3 per-band beacon policy; abort rules.
4. **tools/fw_rf_programme_re.py** - re-derives the decode from the pinned
   local image + the lab objdump; --selftest asserts the windows/tables the
   report relies on (223/223 PASS).

Key results:

* CA 0x101890 is a **band-1-only predicate**, not a programmer: it returns a
  descriptor bit and writes nothing; its result is latched into bit 0 of the
  RF block word +0x410 by CA 0x89840.
* The actual 5 GHz RF programme is **CA 0xA2FA2** (inline image, ch 100-196)
  or **CA 0x89924** (ch 36-64), writing a ~30-word RF/PHY register image at
  R = [[perlink+0xE20+0x1A00]+0x2D8], offsets +0x3A8..+0x418.
* Every gate on that path is a **silent early-return**, so the three gate
  bytes reading 1 today does not prove the programme ran when the 0x0124
  frame was posted (candidate C1).
* The beacon engine is per band: VAP start CA 0x46394 -> CA 0x971D8 (global
  callbacks), AP config CA 0x84DA4 stores the beacon interval in VAP+0x13A,
  and the image carries separate smac_tbtt_5g_isr / smac_tbtt_2g_isr names.
* The vendor ini sets **beacon_2g_tx_policy=0 / beacon_5g_tx_policy=2**
  (parsed by hi5622v100_plat.ko) - a per-band beacon setting the five-frame
  ladder cannot synthesise.

No vendor binary, firmware byte, calibration value, MAC or secret is stored in
this lane; the probe output stays under build/ (gitignored).
