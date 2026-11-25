# wifi-fw-desc-pool - the descriptor-pool populator (C2), the ini beacon policy (C4), the band-1 start/TBTT hooks (C5)

**Lane:** wifi-fw-desc-pool (shared task **task-184**). HOST-ONLY, read-only:
decodes the pinned local vendor firmware image and vendor modules; no device
contact, no fire. **The Lead fires.**

Read in this order:

1. **REPORT.md** - the pool layout, the populator CA 0x1017F8, the two trigger
   paths (boot init and H2D 0x00F7), the C4 ini path, the C5 band-1 start
   hooks, and the re-ranked causes C1/C5a/C4/C2.
2. **TABLES.md** - every address, the pool/boot defaults, all pool users, the
   read-only probe pack, C4/C5 tables and the R0 decision tree.
3. **FIRE-RECIPE.md** - R0 read-only pack, R1 zero-rebuild 0x0124 re-post,
   R2 host patch, R3 0xF7-source fix, R4 C5 report-no-fire; abort rules.
4. **tools/fw_desc_pool_re.py** - re-derives the decode from the pinned local
   artifacts; `--selftest` (97 assertions, PASS); `--decode/--tables/--policy/
   --tbtt/--probes`.

Key results:

* The populator is **CA 0x1017F8**: it clears both channel maps
  (CA 0x117B48 14 rows / CA 0x117AD4 29 rows) to slot 0xFF and then expands the
  runtime descriptor-pool bitmaps (CA 0x1ADC98) into the row slot bytes.
* Pool layout: +0x04 u8 count; record i at +16*i with +0x08 record word
  (byte0 band 0/1, **byte2 bit7 = 0x00800000 valid**), +0x14 channel bitmap.
  Boot defaults: count=2, 2.4G bitmap 0x1FFF, 5G word **0x00000201**, bitmap
  0x1FFFFFFF. **The 5G valid bit is 0 by design in the default map.**
* Writers/triggers: firmware boot init block CA 0x485FA..0x4861E (calls the
  populator at CA 0x48620); **H2D key 0x00F7 worker CA 0x45A30** installs a
  host descriptor blob into the pool and calls the populator at CA 0x45A76;
  H2D key 0x00DA worker CA 0x43460 edits record channel fields only. Our five
  frames do not send 0x00F7 -> the default map is what runs. The 0x00800000
  bit is host-supplied data, not a firmware-set gate.
* C4: hi5622v100_plat.ko parses `beacon_2g/5g_tx_policy` (ids 0xAE/0xAF) into
  g_al_dts_params and copies them to the exported `g_dev_custom_info` blob at
  **+0x91C (2g)** and **+0x7AE (5g)**; the only send path is plat's
  `dev_custom_send` -> bal_write. Our five H2D frames cannot synthesise it.
* C5: VAP start-on-band CA 0x971D8 calls **G+0x370 = CA 0x174B34** and
  **G+0x374 = CA 0x174B38** (G = CA 0x1747C4) with the VAP context - the
  cheapest new discriminator for the silent 5 GHz beacon. The 45-name ISR blob
  (CA 0x12266C) has no code reference, so the TBTT handler map stays [U].
* Probe correction: the RF-lane TABLES T7 row 20 printed BAR0 0x872AD4 for
  CPU 0x117AD4; correct is BAR0 0x7CFAD4, phys 0x407CFAD4 (the committed
  convention CPU+0x6B8000 matches every other T7 row).

No vendor binary, firmware byte, calibration value, MAC or secret is stored in
this lane; probe output stays under build/ (gitignored).
