# REPORT - the vendor WEXT `set_vendor_ie` call order (independent pass)

**Lane:** appie2, shared task **task-244**, 2026-11-26. **HOST-ONLY** - no bench contact,
no device, no firmware write, no flash, no vendor artifact or byte committed. Inputs: the
local gitignored vendor driver disassembly (addresses and layouts re-derived here, see
`evidence/DECODE-NOTES.txt`), the sibling lanes' committed docs (`../wifi-app-ie/`,
`../app-ie-image3/`, `../appie-wext2/`) and the 2026-11-27 class-sweep result.

This lane is the independent verification pass requested for the same question as
**appie-wext2** (task-239). Its job is to re-derive the chain from the local evidence,
settle what the first pass left open, and say what to do on the bench - not to add a
second kernel patch.

## 1. The question and the starting point

The app-IE H2D frame (g0 id 0x20) is proven at the HCC layer: the fires posted a
byte-exact frame with the vendor 0xDD IEs, the ring consumer reported it consumed, and
yet the firmware per-VAP class slot stayed empty for every class the class sweep tried
(4, 3, 9, 1) and no 0xDD IE appeared on air. "Posted and consumed" says nothing about
the firmware handler's return code or its slot lookup. The class sweep therefore could
not be the vendor's storage mechanism, and the vendor's userspace app-IE entry point is
the WEXT private ioctl `nlwifi_set_app_ie`.

## 2. What the WEXT path does (re-derived)

### 2.1 Entry, ABI, body [V]

The private-ioctl dispatcher `wal_witp_wifi_priv_cmd_proc_set` maps cmd `0x8eec` to
`wal_ioctl_set_vendor_ie` (0x11820c). The handler reads the userspace struct
`{u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[ie_len]}`, copies at most 496 IE bytes
into a zeroed 592-byte kernel object (a larger `ie_len` fails the copy), then writes:

```
body[0..1]  u16 0x0276     SET_VENDOR_IE
body[2..3]  u16 0x01f8     payload length 504 = 8 + ie_len_max
body[4..7]  u32 ie_len
body[8]     u8  type       firmware/mirror slot class
body[9]     u8  mode = 0   path A (the WEXT path never sends mode != 0)
body[10..11] u8[2] pad = 0
body[12..]  u8[496] ie     zero padded
```

and calls `wal_send_cfg_event(dev, 1, 0x1fc, body, sync=1, &resp)`, then waits on
`wal_check_and_release_msg_resp(&resp)` and returns the handler status.

### 2.2 The send shim and the envelope [V geometry, [I] footer semantics]

`wal_send_cfg_event` (0x126c98) allocates through `wal_alloc_cfg_event` (0x126ab0),
fills the envelope header and payload, stamps a monotonic sequence from
`g_wal_config_seq_num`, and hands the message to `wal_send_cfg_event_msg_tx`
(0x126c1c) -> `hcc_msg_tx` (imported from the platform transport module). With
`sync=1` the request goes onto the WAL message-request queue and the caller holds the
response object; with `sync=0` the queue insertion is skipped and the message is only
sent. The envelope geometry (TABLES W3): fixed 536 bytes, **group 2 in the low nibble**
of byte 0, **id 0** at bytes 6..7, tag 0x5a5a at bytes 10..11, and the context trio
`{hdr[3], hdr[2], hdr[1] high nibble}` read from `*(u32 *)(dev + 0x328)` - the
netdev's hmac/WAL context.

### 2.3 The consumer chain and its order [V]

The dispatch table entry for event 0x0276 resolves to `wal_config_set_vendor_ie`
(0x12b960, a one-instruction thunk), which calls `hmac_config_set_vendor_ie`
(0x60a6c). That calls `hmac_config_set_app_ie_to_vap` (0x60298), which:

1. runs `mac_find_ie(0x7f, ie_len, ie)` and, when found, `memmove_s()`s the tail over
   the first 0x7f IE and reduces `ie_len` (the 0x7f strip);
2. has a class-7 special path (adaptive 11r ability bookkeeping);
3. calls **`mac_vap_save_app_ie` (0x2ac18) - the host-mirror slot save**;
4. only if that succeeded, calls **`hmac_config_h2d_send_app_ie` (0x600cc) - the
   direct g0 id 0x20 frame**.

So the vendor order is **mirror first, post second**, and the whole sequence runs in the
netdev's hmac context (a running AP). The class sweep skipped the mirror, the running-AP
timing and the context trio.

## 3. The finding the first pass left open: where the g2/id0 envelope is consumed

The first pass recorded an open question: the firmware's group-2 receive tables (local
firmware image, 19 ids at file 0xccc18) have no id 0, so a firmware-side consumer of the
536-byte envelope was unproven and the envelope was ranked as a fire hypothesis (H3).

This lane decoded a **second, host-side producer and consumer pair** for the same
envelope:

* `wal_main_init` (0x12386c) registers `g_st_wal_drv_func_hook` (bss 0x35808), a block of
  four host handlers `{memory cmd, register cmd, config cmd, global-var cmd}`, through the
  platform OAM hook registration (`oam_wal_func_fook_register`).
* **`wal_recv_config_cmd` (0xfd604)** is the config-cmd handler. It reads a message whose
  first 16 bytes are the netdev name, resolves `dev_get_by_name(init_net, name)`, takes
  `ctx = *(u32 *)(dev + 0x328)`, allocates a message of `len - 12`, writes an
  **HCC envelope header with group 2 in the low nibble, id 0, context trio from ctx,
  tag 0x5a5a**, copies the cfg-event bytes to payload offset 16, and calls
  **`wal_config_process_pkt` (0xfcb94) on the host** - it never transmits the envelope.
* `wal_config_process_pkt` parses the payload records (`u16 event; u16 len; ...`) and
  dispatches by event id over 12-byte rows `{u16 id; u16 pad; u32 h4; u32 h8}`; the
  production host table (290 rows at rodata 0x85f8) and the debug copy
  (`g_ast_board_wid_op_debug`, 170 rows at 0x24980) both contain a row for **id 0x0276
  with a null +4 slot and `wal_config_set_vendor_ie` in the +8 slot**.
  `wal_config_process_entry` (0xfc714) selects the +8 slot when its dispatch mode
  argument is 3 and the +4 slot when it is 4; so the app-IE event is a **mode-3 host
  dispatch**.

Conclusion: the group-2 / id-0 container is the **host-internal event-RPC envelope**. It
is used to hand a cfg event to the host WAL dispatcher with the target netdev's context;
the only firmware-bound message on the app-IE path is the inner g0 id 0x20 frame. A raw
536-byte g2/id0 frame posted to the firmware therefore has no slot-storing consumer - the
break the project is chasing is on the H2D side of this host chain (the g0 id 0x20 frame's
firmware handler and its VAP lookup), not on the envelope. This resolves the first pass's
open [U] in favour of a **host** consumer and demotes its H3 fire rung.

The status report path is consistent with this: the WEXT caller waits on
`wal_check_and_release_msg_resp` (0x109370), and the config-cmd response is reported out
through `wal_recv_cfg_cmd_rsp` (0xfd474), which builds an OAM-SDT report (leading byte
0x4d) with the payload and posts it through the OAM-SDT hooks. Not every field of that
report path is decoded; it does not affect the bench action.

## 4. What this means for the IMAGE #3 gap

The host side of the app-IE path is now decoded end to end (userspace ABI -> body ->
envelope -> dispatch -> mirror -> g0 id 0x20). The bench question is the same one the
project has for the RX gate: does the firmware's g0 id 0x20 handler accept and store the
frame from our stack? The class sweep proved the HCC ring consumes it; the slot readback
says the handler did not store it. The deltas that can cause that are the ones decoded
here: **timing** (the vendor runs the chain on a running AP, not inside `start_ap()`),
**context** (the trio comes from the netdev hmac context, not from the VAP-start module),
**order** (mirror save before the post; the mirror is host memory and cannot itself fill
the firmware slot, but a post that races the mirror's own state machine is a different
call shape), and **the arguments** (ie_len up to 496 after an optional 0x7f strip, type
0..19, mode 0).

## 5. The host model and its KAT

`src/appie2_replica.c` is original clean-room C99 that models the sequence and the
fail-closed rules:

* WEXT request parse (cmd `0x8eec`, `ie_len <= 496`, `type <= 19`, mode forced to 0);
* the 0x7f strip (first matching IE only, malformed blob refused);
* the 508-byte body (event/len/ie_len/type/mode/pad/IE placement);
* the 536-byte group-2/id-0 envelope (low-nibble group, id 0, tag 0x5a5a, context trio
  mapping, 12-byte RPC prefix, body at hdr+28);
* the 12-byte row dispatch with mode 3 -> +8 slot and mode 4 -> +4 slot, unknown event or
  null slot -> fail closed;
* the hmac consumer order: mirror save then H2D post, with an envelope-tx counter that
  must stay 0 (the host-internal envelope is never transmitted).

The KAT is **59/59 PASS** (`evidence/HOST-TESTS.txt`; regenerate with
`tools/run-host-tests.sh`). It is a model/KAT of the decoded shapes, not a kernel port:
it proves the sequence and the bounds, not the on-device slot behaviour.

## 6. Deltas to the first pass (appie-wext2, task-239)

| topic | appie-wext2 | this lane |
| --- | --- | --- |
| WEXT entry, ABI, 508-B body, envelope geometry | decoded [V] | re-derived independently, matches [V] |
| hmac chain order (mirror -> post) | decoded [V] | re-derived independently from 0x60298, matches [V] |
| where the g2/id0 envelope is consumed | **[U]**, ranked H3 fire | **host** `wal_config_process_pkt` via `wal_recv_config_cmd` [V]; H3 envelope fire demoted |
| dispatch row layout / 0x0276 -> set_vendor_ie | not decoded | row `{u16 id; u16 pad; u32 h4; u32 h8}`, +8 slot, mode 3 [V] |
| kernel patch | ships `0002-app-ie-wext2.patch` | none (avoids a second conflicting patch lineage) |
| fire ladder | W0/W1/W2/W3 (envelope) | W0/H1/H2 (no envelope rung), see `FIRE-RECIPE.md` |

## 7. Still open (and why it does not block the bench)

1. **Who issues the host config command in the vendor flow** - userspace softapd after an
   OAM/SDT notification, or the platform OAM layer on the firmware's behalf. Settling it
   needs a userspace trace, not the driver disassembly; it does not change our action,
   which is to execute the hmac chain in our driver.
2. **Exact OAM netlink command id and platform framing** around `wal_recv_config_cmd`
   (platform module side). Not needed: our clean-room driver calls the chain directly.
3. **Firmware side of event 0x0276** (if any). The host path is proven to do the store
   work; the firmware-bound inner frame is the thing the bench tests.

## 8. Grade summary

| claim | grade |
| --- | --- |
| WEXT cmd 0x8eec, userspace ABI, 508-B body, sync send, response release | [V] disassembly |
| 536-B envelope geometry, group in the low nibble, id 0, ctx trio from dev+0x328, tag | [V] from two independent builders (send shim and `wal_recv_config_cmd`) |
| hmac chain and its order (0x7f strip, mirror save, then g0 id 0x20 post) | [V] disassembly |
| g2/id0 envelope consumed by the **host** dispatcher, not transmitted | [V] `wal_recv_config_cmd` call shape (local `wal_config_process_pkt`, no tx) |
| row layout 12 B `{u16 id; u16 pad; u32 h4; u32 h8}`, 0x0276 row -> +8 slot, mode 3 | [V] dispatcher code + relocation alignment |
| why the vendor flow reaches the host dispatcher (who sends the cmd to whom) | [I] - open item 1 |
| our model/KAT: compiles, 59/59 PASS, geometry and order | [h] host only |
| on-device slot/on-air result of each fire rung | not tested here (host-only lane) |

## 9. Evidence map

| evidence | content | regenerate |
| --- | --- | --- |
| `evidence/HOST-TESTS.txt` | host KAT output (59/59) | `tools/run-host-tests.sh` |
| `evidence/DECODE-NOTES.txt` | every anchor used, how it was re-derived, row-layout cross-check | quoted in TABLES.md |
| `src/appie2_replica.{h,c}`, `src/appie2_selftest.c` | the model and its KAT | as above |
| local vendor `hi5622v100_wifi.ko` | disassembly source | local, gitignored; never committed |
