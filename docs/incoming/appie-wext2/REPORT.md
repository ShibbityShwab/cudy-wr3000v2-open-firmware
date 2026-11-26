# REPORT - replicating the vendor WEXT `set_vendor_ie` call sequence (the app-IE slot storage)

**Lane:** appie-wext2, shared task **task-239**, 2026-11-26. **HOST-ONLY** - no
bench contact, no device, no firmware write, no flash, no vendor artifact or
byte committed. Inputs: the sibling lanes' committed decode
(`../wifi-app-ie/`, `../app-ie-image3/`) and the **local** vendor driver
disassembly (`hi5622v100_wifi.ko`, gitignored local evidence; CAs/symbols are
quoted, bytes are protocol layouts).

## 1. The starting point and the question

The app-IE H2D frame (g0 id 0x20, builder CA 0x600cc) is fully proven at the HCC
layer: the fires posted a byte-exact 85 B frame with four 0xDD IEs, the ring
consumer reported `consumed 0 polls`, and the ladder stayed healthy. But the
firmware per-VAP class slot read back empty for **all four vendor classes** the
class sweep tried (4, 3, 9, 1), and the laptop scan never saw the 0xDD IE. "Posted
and consumed" is a statement about the HCC ring, **not** about the firmware
handler's return code - the handler can fail its VAP lookup silently.

The lead's framing after the sweep: the slot storage must be the vendor's WEXT
`nlwifi_set_app_ie` call sequence. This lane decodes that sequence exactly.

## 2. What the WEXT path actually is (decoded)

### 2.1 Entry and userspace ABI [V]

The Hi5622 private-ioctl dispatcher `wal_witp_wifi_priv_cmd_proc_set`
(CA 0x118d94) switches on `cmd = u32 at the argument` and maps
`cmd - 0x8ee1` up to 11 to handlers. Index 11 -> **cmd 0x8eec** ->
`wal_ioctl_set_vendor_ie` (CA 0x11820c): the kernel side of the vendor's
`nlwifi_set_app_ie` (called by `softapd` per the committed vendor-userspace
report). The userspace struct is
`{u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[<=496]}`.

### 2.2 The cfg-event RPC instead of a bare frame [V]

The handler does **not** call the g0 id 0x20 builder directly. It packs a
508-byte body `{u16 event 0x0276 (SET_VENDOR_IE); u16 len 0x01f8; u32 ie_len;
u8 type; u8 mode 0; u8 pad[2]; u8 ie[496]}` and calls
`wal_send_cfg_event(dev, 1, 0x1fc, body, sync=1, &resp)` (CA 0x126c98, alloc
CA 0x126ab0). That builds a **fixed 536-byte HCC frame**, group 2 / id 0
(`hdr[0]` low nibble = 2, `hdr[6..7] = 0`), whose context trio
(`hdr[1] bits7:4 / hdr[2] / hdr[3]`) is read from `*(u32 *)(dev + 0x328)` - the
**netdev's hmac/WAL context** - and whose payload at `hdr+16` is
`{u32 rpc_tag 1; u16 reserved; u16 msg_len 0x1fc; u32 seq; u16 event 0x0276;
u16 0x01f8; u32 ie_len; u8 type; u8 mode 0; u8 pad[2]; u8 ie[496]}`. It then
**waits up to 2000 jiffies for the cfg-event response** and returns the handler
status (`wal_check_and_release_msg_resp`). Full geometry: TABLES W2/W3.

Two other vendor callers use the same event id (beacon extended IE, an alternate
ioctl entry, and the OKC IE path), so 0x0276 is a generic app-IE RPC.

### 2.3 The consumer-side hmac chain [V]

`wal_config_set_vendor_ie` (CA 0x12b960, a pure thunk, reached through a
dispatch table - no direct .text caller) ->
`hmac_config_set_vendor_ie` (CA 0x60a6c; `type = req[4]`; class 16 also mirrors
the blob to `vap+0x38c`, len `ie_len-6`) -> `hmac_config_set_app_ie_to_vap`
(CA 0x60298; strips a 0x7f IE via `mac_find_ie`, class-7 special path, then):
(a) **`mac_vap_save_app_ie` (CA 0x2ac18) - the host mirror** of the very slot
table the firmware handler writes (`vap + 20*type + 0x24c`; `ie_len 0` clears,
else realloc/len/key-zero); then (b) **`hmac_config_h2d_send_app_ie` (CA
0x600cc)** - the direct g0 id 0x20 frame we already ship. So the vendor's order
is *mirror first, then post*, and the whole sequence runs on a **running AP
netdev**.

### 2.4 Group-nibble correction [V]

The HCC **group is the low nibble of `hdr[0]`** and the id is `u16` at
`hdr[6..7]`: the app-IE builder clears bits 0..3, the channel-switch builder
clears them, and the cfg-event builder inserts 2 there. Sibling TABLES T1's
"group = hdr[0] high nibble" is a documentation error (harmless there because the
stock value is 0). The cfg event is therefore a **g2 id0** message; the census's
12 "g2 id0" records are torn config-key-shaped reads, not this frame, so no
cfg-event-shaped send exists in the stock capture ([U], it does not settle the
consumer location).

## 3. Why the class sweep could not see the slot, and what is still open

The class sweep varied one parameter (the class byte) of the direct frame. The
decode shows seven concrete deltas between the vendor sequence and the AP-start
posting (TABLES W6): the RPC envelope, the running-AP timing, the netdev context
trio, the host mirror, the 0x7f strip, the 496 B cap and the response wait. Any
one of them can produce "ring consumed, slot empty, no IE on air". Ranked:

* **H1 - timing/context (most likely, cheapest to test).** The WEXT request is a
  runtime update on a **running** AP; our frame was posted inside `start_ap()`
  before the AP was enabled. **Fire W1**: post the same frame from a delayed work
  ~1 s after `start_ap()` returns.
* **H2 - the VAP index in `hdr[3]`.** The vendor trio comes from the netdev's
  hmac context (first bytes); ours is `vap_start_module` (0 in every fire). If
  the firmware handler's `lookup(0, hdr[3])` fails, it returns without storing
  and the ring still shows consumed. **Fire W2**: sweep `hdr[3] = 0..7` with the
  existing module and read the slot after each post (no rebuild, one load).
* **H3 - the WAL cfg-event envelope is the mechanism** (the consumer stores the
  slot in the cfg-event context, the bare g0 id 0x20 receive handler has an extra
  precondition). **Fire W3**: post the vendor-exact 536 B g2/id0 event 0x0276
  frame from this lane's patch and watch for the cfg-event response in the D2H
  drain plus the slot read-back.
* **H4 - the host mirror** (`mac_vap_save_app_ie`) is host memory only and
  cannot alone explain an empty firmware slot; it matters for our own host frame
  builders (assoc/probe) and is included in the port for sequence fidelity.

Open [U]: **where the g2/id0 cfg event is consumed.** The firmware registers
group-2 receive tables (local firmware image: 19 ids at file 0xccc18) with **no id
0**, so a bare firmware post of this frame would be dropped; the host driver
registers both chip tables and one core table (CA 0x12e0: core table group 0,
7 entries). The candidate consumers are a host WAL "core" dispatch (then the WEXT
path is a host RPC that internally posts the g0 id 0x20 frame - i.e. our existing
frame plus the host mirror/order) or a firmware queue service outside the msg
tables. Fire W3 discriminates: a D2H cfg-event response proves a consumer; the
slot read-back proves the storage path either way. This lane does **not** claim
it.

## 4. The replication (host-side model + kernel port)

### 4.1 Portable model `src/hi5622_app_ie_wext.{h,c}` [h]

Original clean-room C99 (compiles in the kernel and host selftest):
* the WEXT ABI constants (`cmd 0x8eec`, 496 B cap, type <= 19);
* `hi5622_aiw_wext_parse()`: userspace struct -> `hmac_config_set_vendor_ie`
  request (ie_len/type/**mode 0**) with fail-closed bounds;
* `hi5622_aiw_strip_7f()`: the vendor's `mac_find_ie(0x7f)` strip;
* `hi5622_aiw_mirror_save()`: the host-mirror slot table
  (vap+20*type+0x24c semantics: clear on ie_len 0, grow-if-needed, len, key
  zeroed, type <= 19);
* `hi5622_aiw_cfg_event_build()`: the **536-byte fixed** g2/id0 event 0x0276
  frame from TABLES W3, fail-closed (ie_len <= 496, type <= 19, ctx bytes,
  sequence counter), zero pad bytes (protocol-safe; the vendor leaves the two
  reserved bytes uninitialised);
* `hi5622_aiw_status_line()`.

Host KAT: `src/app_ie_wext_selftest.c` (byte-exact builder vector, geometry,
ABI, 0x7f strip, mirror save/clear, full refusal matrix, sequence monotonicity,
both frame shapes independent). `tools/run-host-tests.sh` compiles and runs it
with the host C compiler only.

### 4.2 Kernel port `patches/0002-app-ie-wext2.patch` [h]

Applies on top of `../app-ie-image3/patches/0001-app-ie-image3.patch` on the
IMAGE #3 merged module tree (the tree the bench runs). It adds two fail-closed
module parameters and reuses the existing `hi5622_frame_post()` ladder plumbing:

| parameter | values | meaning |
| --- | --- | --- |
| `app_ie_mode` | 0 (default) / 1 | 0 = direct g0 id 0x20 frame (current behaviour); 1 = the vendor WEXT envelope: the 536 B **g2/id0 event 0x0276** RPC frame |
| `app_ie_when` | 0 (default) / 1 | 0 = inside `start_ap()` (current); 1 = **deferred ~1 s after `start_ap()` returns** (the WEXT running-AP order; a delayed work) |

It also runs the **host-mirror save before the post** (vendor order) and logs
both, adds the new counters to the `app_ie:` status line, and keeps every
task-234/235 fail-closed refusal (bad parameter, malformed IE list, missing
ladder/VAP start). Default values reproduce the app-ie-image3 behaviour
byte-for-byte (same frame, same place, same read-back).

Build identity and any build limitation are recorded in `BUILD-NOTES.md`;
the fire ladder is `FIRE-RECIPE.md` (W0 control, W1 delayed post, W2 hdr[3]
sweep with the unmodified app-ie-image3 module, W3 WEXT envelope).

## 5. Grade summary

| claim | grade |
| --- | --- |
| WEXT entry cmd 0x8eec, userspace ABI, cfg-event 0x0276 body, 536 B frame geometry, trio source `dev+0x328` | [V] disassembly |
| the hmac chain (thunk -> set_vendor_ie -> set_app_ie_to_vap -> mirror -> g0 0x20 builder), 0x7f strip, class-16 mirror | [V] disassembly |
| group = hdr[0] low nibble | [V] |
| where the g2/id0 cfg event is consumed; whether it is required for the slot store | [U] - fire W3 |
| our replication (model + patch): compiles/tests/byte geometry | [h] (host) |
| on-air/slot result of each rung | not tested here (host-only lane) |

## 6. Evidence map

| evidence | content | regenerate |
| --- | --- | --- |
| `evidence/HOST-TESTS.txt` | host KAT output | `tools/run-host-tests.sh` |
| `evidence/DECODE.txt` | the disassembly commands/anchors used (addresses only, no bytes) | quoted in TABLES.md W1-W5 |
| `evidence/BUILD.txt` | patched module identity + warnings | `tools/build-app-ie-wext2.sh` |
| `patches/0002-app-ie-wext2.patch` | kernel integration on top of 0001 | apply to base tree + 0001 |
| vendor `hi5622v100_wifi.ko` disassembly | local, gitignored | never committed (no vendor objects) |
