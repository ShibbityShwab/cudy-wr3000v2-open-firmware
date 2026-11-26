# appie2 - the vendor WEXT `set_vendor_ie` call order, independent pass (the app-IE slot storage)

**Lane:** appie2, shared task **task-244**, 2026-11-26. **HOST-ONLY**: no bench contact,
no device, no firmware write, no flash. Parent lanes: **wifi-app-ie** (task-234) and
**app-ie-image3** (task-235); the class sweep of 2026-11-27 showed the app-IE frame is
posted and consumed but the firmware per-VAP slot stays empty for classes 4/3/9/1.

This lane is the independent second pass on the same question as **appie-wext2**
(task-239): what does the vendor's WEXT `nlwifi_set_app_ie` path actually do, and what
should the bench replicate? It deliberately does **not** ship a second kernel patch
(one patch lineage is enough); it ships a re-derived decode, the missing consumer-side
decode, a host KAT of the call order, and a fire recipe that removes one rung the
first pass proposed.

## Headline findings

1. The WEXT handler is confirmed: priv-ioctl cmd `0x8eec` reads
   `{u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[<=496]}`, builds the 508-byte
   cfg-event body `{u16 0x0276 SET_VENDOR_IE; u16 0x01f8; u32 ie_len; u8 type; u8 mode 0;
   u8 pad[2]; u8 ie[496]}`, and sends it with `wal_send_cfg_event(..., sync=1)`; the
   envelope goes out through `hcc_msg_tx` (host-to-firmware transport).
2. The hmac consumer is confirmed in order: `hmac_config_set_vendor_ie` ->
   `hmac_config_set_app_ie_to_vap` (first strips the first `0x7f` IE, has a class-7
   special path) -> **`mac_vap_save_app_ie` (host mirror) -> `hmac_config_h2d_send_app_ie`
   (g0 id 0x20 post)**; the post only runs after a successful mirror save.
3. **New:** the group-2 / id-0 envelope is a **host-internal container**, not a wire
   message. The host driver builds the same envelope itself in `wal_recv_config_cmd`
   (registered from `wal_main_init` through the OAM hook block `g_st_wal_drv_func_hook`):
   it resolves the netdev by the 16-byte name in the message, takes the context trio from
   `dev+0x328`, fills group 2 / id 0 / tag 0x5a5a, and calls `wal_config_process_pkt`
   **locally**. The host dispatch then walks 12-byte rows `{u16 id; u16 pad; u32 h4;
   u32 h8}`; the `0x0276` row selects `wal_config_set_vendor_ie` from the +8 slot when
   the dispatch mode argument is 3.
4. **Consequence for the bench:** there is no firmware g2/id0 consumer to drive the slot
   store, so posting the 536-byte envelope to the firmware cannot be the storage
   mechanism (and would waste a fire). The storage mechanism is the host hmac chain:
   mirror save, then the g0 id 0x20 post, on the running AP's netdev hmac context. The
   first pass ranked the envelope as hypothesis H3; this lane demotes it. See
   `FIRE-RECIPE.md`.

## Contents

| file | content |
| --- | --- |
| `REPORT.md` | the decode, the corpus of new findings, deltas to appie-wext2, open questions, grades |
| `TABLES.md` | W1-W9: ABI, body, envelope, chain order, consumer, dispatch rows, tables, response path |
| `src/appie2_replica.{h,c}` | clean-room host model: WEXT parse -> 0x7f strip -> body -> envelope -> row dispatch -> mirror+post order |
| `src/appie2_selftest.c` | host KAT: 59/59 PASS (bounds, geometry, ctx trio, mode slot choice, mirror-before-post, host-internal envelope) |
| `tools/run-host-tests.sh` | builds/runs the KAT with the host C compiler only |
| `FIRE-RECIPE.md` | revised host-only fire ladder (delayed running-AP post first; envelope rung removed) |
| `evidence/` | `HOST-TESTS.txt` (captured), `DECODE-NOTES.txt` (anchors and how they were re-derived) |

Frame constants and layouts are **decoded protocol shapes quoted as numbers**; no vendor
binary, object, firmware byte, calibration value, MAC or secret is reproduced or committed.
