# wifi-fw-desc-table - the H2D 0x00F7 descriptor-table message (C1)

**Lane:** wifi-fw-desc-table (shared task **task-187**). **HOST-ONLY**: decodes the
pinned local vendor modules and firmware image in place; no device contact, no
flash, no fire. **The Lead fires.** No vendor binary, firmware byte, calibration
value, MAC or secret is stored here; the only bytes committed are *host-built
protocol bytes* (our own frame), as in the sibling lanes.

Read in this order:

1. **REPORT.md** - the vendor emitter (`hmac_config_set_country`), the frame
   builder (`hmac_config_send_event`), the 8+16*N descriptor blob, the firmware
   worker CA 0x45A31, the gate CA 0x1037BC decoded as an ARM **strcmp** dedupe,
   when the vendor sends it, and the two decoy 0xF7 users.
2. **TABLES.md** - every address, the frame table, the blob/record layout, the
   boot-default pool, the read-only probe pack and the decision tree.
3. **FIRE-RECIPE.md** - the byte-exact 68-byte frame, the host implementation
   notes, the verification rungs (R0 host test, R1 install probe, R2 predicate/
   RF/beacon, R3 fallbacks) and the abort rules.
4. **tools/fw_desc_table_re.py** - re-derives the decode from the pinned local
   artifacts, re-emits the exact frame, `--selftest` (115 assertions, PASS -
   see evidence/selftest.txt), `--frame` / `--tables` / `--probes`.

## Key results

* **Emitter [V]**: the only vendor module code that puts key **0x00F7** into a
  g0-id1 H2D frame is **`hmac_config_set_country` (.text+0x63B6C)**, call site
  **.text+0x63C3C**: `hmac_config_send_event(ctx, 0xF7, 8+body[4]*16, body)`.
  The builder (`.text+0x5717C`) writes **key u16 at hdr+20**, **len u16 at
  hdr+22**, **data at hdr+24** into a 12-byte HCC header of **group 0, id 1**.
* **Blob [V]**: `8 + N*16` bytes = `{cc[2], 0, chip_id, N, 0,0,0}` + N records of
  `{u32 word (+0), u8 (+4), pad, u16 (+6), 4 pad, u32 channel bitmap (+12)}`.
  Record word byte0 = band (0=2.4G, 1=5G), byte1 = bandwidth code (2=20 MHz,
  4=40 MHz, 5=80 MHz), **byte2 bit7 = 0x00800000 = the host-armed "valid" bit**.
  **No CRC/length/type inside the blob**; N <= 20 (destMax 0x148 = 8+20*16).
* **Gate [V]**: **CA 0x1037BC is ARM `strcmp(pool, body)`**, not a permission
  check: install is *skipped* when the pool's leading string (its country code,
  NUL-terminated) equals the incoming blob's. The boot default pool string is
  **"99"**, so a table with **cc = "US"** always installs; **a table with
  cc = "99" is silently ignored** (only the old pool is re-expanded).
  Refusals that do return: memcpy_s overflow -> **0x8B2D**; missing band object
  (ctx+0x4B8 == 0) -> **0x64**.
* **When [V] path / [I] order**: the vendor triggers the regdomain/country sync
  during init / regulatory setup - ini path `hwifi_config_init_ini` ->
  **`hwifi_config_init_ini_country` CA 0x1558** -> `wal_hipriv_setcountry`
  CA 0x122D0C -> `wal_regdomain_update` CA 0x1226F0; and the cfg80211/iwpriv
  country path reaches the hmac twin through the WAL ops table (.data+0x8900,
  thunk **`wal_config_set_country` CA 0x12AE0C**). That is **before** the
  AP/VAP start ladder; our host should send 0xF7 **after 0x0065 and before
  0x0124** (or re-post 0x0124 afterwards).
* **Why the valid bit [V read / I policy]**: the only consumer is the band-1
  predicate **CA 0x101890** - it returns 1 as soon as `record+2 & 0x80`, and no
  shipped-module code writes that bit (the vendor WAL blob filler CA 0x121E10
  writes only byte2 bits 0/1/2; the host twin `mac_init_channel_list`
  CA 0x205E4 does not touch byte2 at all). Arming the 5 GHz records in the
  host-supplied table is therefore a **host policy decision**, and it is the
  C1 fix candidate.
* **Decoys that must not be copied [V]**: `wal_regdomain_update` CA 0x1226F0
  sends a **group-2 WAL cmd 0xF7** whose body is an 8-byte `{0x000400F7,
  host pointer}` record - the blob is *not* inline, so it cannot satisfy the
  firmware worker (which reads the blob at hdr+24); and
  `wal_ioctl_get_reg_domain` sends g2 cmd 0xF7 with a 2-byte GET body. Only the
  g0-id1 hmac path matches the firmware memcpy contract.
