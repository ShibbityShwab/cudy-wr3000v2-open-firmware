# H2D message catalog: the firmware's own dispatch tables, ids, handlers and payload expectations

Lane: `h2d-catalog` (shared task `task-74`), 2026-11-23. **Host-only**: static analysis of
the firmware image and the vendor modules on the host; no device contact, no build, no vendor
bytes committed. Runtime address convention: **CA = file offset + 0x40000** (bench-verified by the
record: image uploaded to CA `0x01240000`, region-0 alias CA `0x40000`; live reads of
CA `0x10C0F4`, CA `0x172130` and the exception frame at BAR0 `0x6F8008` match this mapping).

Inputs: `FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20…` (local evidence only, never copied here);
vendor `hi5622v100_plat.ko` / `hi5622v100_wifi.ko` (local only); the committed record under
`opensource/docs/phase37/` (prerequisite reading, cross-referenced, not re-derived).

---

## 0. TL;DR

1. **New, byte-verified: the firmware has its own HCC message-registration tables** - the
   device-side twin of the host's `hcc_msg_register_tab_chip`. They are filled by the firmware's
   `hcc_msg_register` at CA `0x102866` from three static tables in the image:
   * **group 2**: 19 entries, table CA `0x10CC18` (file `0xCCC18`), registered at file `0x833C`;
   * **group 0**: 40 entries, table CA `0x10CCFC` (file `0xCCCFC`), registered at file `0x8346`;
   * **group 0, early single**: 1 entry (id `0x2F`), table CA `0x10CBEC` (file `0xCCBEC`),
     registered at file `0x7BC8`.
   Every entry is `{u16 id; u16 pad; u32 handler; u32 aux=0}`; the full id -> handler catalog is
   in section 4 / Appendix A. This closes, on the device side, the open item in
   `phase37/fw-ring-message-table.md` §7.3 ("the ids the vendor's other init-time H2D sends
   carry are not established"): it is the firmware's *receive* map for HCC messages.
2. **The mailbox layer is confirmed as before**: the H2D mailbox dispatcher (file `0x818AC`)
   indexes a single runtime table at CA `0x118D68`; populated ids are **1, 3, 5, 6**
   (handlers file `0x510`, `0x85128/0x85144`, `0x819DC`, `0x4CCE4`). Id 5 is a
   `BUG()` trap (bytes `00 23 1b 60 ff de`); the table carries **no body**.
3. **Cross-reference with the vendor host's senders**: all 20 explicit group-0 H2D senders in the
   committed vendor catalog have a handler in the firmware's group-0 table (20/20). The vendor's
   group-2 senders split: 9 match the group-2 table; 15 have their handler registered in the
   **group-0** table; 3 have no handler (ids `0x14` x2, `0x21`). That split is an **open
   anomaly** (section 4.5) with two live hypotheses; it does not affect the group-0 catalog.
4. **What a minimal host should send**: during boot, **no HCC H2D frame is required** (the boot
   dialogue is device->host only; the current boot fault is firmware-internal, boot-kick §21).
   The first safe host->device probes are (a) mailbox id **1** (wakes the firmware's own global
   message queue, no payload) and (b) one SR-carried HCC frame with group 0 id **0**, whose
   firmware handler `0x81695` is a print-and-return stub that reads no payload. **Never post
   mailbox id 5** (trap). Real config frames (group 0 ids 1/2/3/7/8/9/…) should only be sent once
   their vendor payload structs are replicated - several handlers dereference payload words as
   pointers.

---

## 1. Layer model (why there are two different "H2D id spaces")

| layer | carrier | selector | firmware consumer | catalog here |
| --- | --- | --- | --- | --- |
| **A. mailbox** | CA `0x40039010` pending bitmap + doorbell CA `0x400392D4` | bit index (id 0..9) | dispatcher file `0x818AC`, table CA `0x118D68`, 8-byte `{fn,arg}` entries | section 3 |
| **B. HCC frame** | SR ring node -> host DRAM buffer | 12-byte HCC header: `group = hdr[0]&0xf`, `id = u16 hdr[6]` | firmware HCC message layer (this lane) | section 4 |

The mailbox message is **just a wake-up bit**: no length, no body (dispatcher loop at file
`0x818AC` reads `out[0]` as a bitmap, calls `table[id].fn(table[id].arg)`). The body-bearing
traffic is the SR-ring HCC frame; the vendor host announces every SR post with mailbox id **3**
(`shuangta_ete_sr_dscr_fill` -> `pcie_msg_send(chip,3)`).

Firmware mailboxes are acknowledged/turned around by the dispatcher itself: `ack=1` at CA
`0x400392F0`, `out[0]=0`, `doorbell=8` - id-independent.

---

## 2. Method

1. Re-decode the mailbox anchors (dispatcher, registrations, handlers) from `FIRMWARE.bin` and
   re-assert the record's bytes - `tools/fw_h2d_tables.py --verify` prints `20/20 PASS`.
2. Find the firmware's HCC registration call sites by locating the twin of the host's
   `customise()` path: a function that (a) reads a per-group record array of 12-byte
   `{u32 max_id; u32 item_array; ...}` entries, (b) writes 16-byte dispatch items
   `{id, fn, aux, stats}`. The registrar is CA `0x102866` (file `0xC2866`); its call sites are
   found by a full Thumb `BL` sweep (only three in the image).
3. Decode the three static registration tables from the literals the call sites load
   (`file 0x84E0 -> CA 0x10CC18`, `file 0x84E4 -> CA 0x10CCFC`, `file 0x7DA8 -> CA 0x10CBEC`).
4. Disassemble each handler entry to prove it is a real Thumb function (push/`sub sp` prologue,
   NULL-message log path via the firmware's `hcc_msg_get_hdr` twin CA `0x409EA`) and extract
   payload-field accesses heuristically (`tools/` - see Appendix B).
5. Join with the committed vendor send catalog (`phase37/sr-message-catalog.md` §2) -
   `tools/h2d_join_vendor.py` - and classify coverage / anomalies.

Verification: `fw_h2d_tables.py --verify` asserts 20 byte-level claims (registrar instructions,
instance literal, registration literals, group/count immediates, mailbox anchors). The three table
dumps come from the same tool.

---
## 3. Layer A - the H2D mailbox catalog (re-verified, ids 1/3/5/6)

Dispatcher: file `0x818AC`. Body, file offsets: `push {r3-r7,lr}`; `r6=r0` (message ctx);
`*[ctx+0xc]=1` (ack CA `0x400392F0`); read `out[0]` CA `0x40039010` into r5 and clear it;
write 8 to CA `0x400392D4` (re-arm); then loop: lowest set bit -> id; `cmp #9` bound;
`table = *(ctx+0x20)`; entry 8 bytes `{fn,arg}`; `fn(arg)`; clear bit; repeat.
Table pointer static value: file `0xCC1B0` holds CA `0x118D68`. The table itself lives in RAM
(evidence: `phase30/the-gate-located.md` live dump; `phase37/fw-ring-message-table.md` §2.1;
the file bytes at the same numeric offset belong to the separate early-boot image).

Registrations: entry point A file `0x8187A` (id 3 + arg CA `0x0010C0F4`, called from the
firmware `pcie_msg_init` at file `0x9838`); entry point B file `0x7C1DC` (ids 1/5/6, called at
files `0x8226`/`0x7CF0`/`0x88F8`). File `0x510` id-1 handler bytes re-verified
(`03 4b 03 f5 1d 71 d3 f8 70 02` = `ldr r3,[pc,#0xc]`; `add.w r1,r3,#0x274`; `ldr.w r0,[r3,#0x270]`; then tail-call file `0x86D0C` - enqueue global message + run worker).

| mailbox id | handler CA / file | expected message content | action | evidence |
| ---: | --- | --- | --- | --- |
| **1** | `0x00040511` / file `0x510` | **none** - bit only | posts the firmware's global message object (CA `0x00117C00`, field `+0x270` queue, `+0x274` item) onto its message queue and runs the message worker | [V] record + re-checked |
| **3** | `0x000C5145` / file `0x85144` -> body `0x85128` | **none** at mailbox layer; the body is the SR ring | signals `arg+0x98=1` (the firmware's `pcie_thread` event), then runs the message worker on `arg+0x50` (arg = CA `0x0010C0F4`) | [V] |
| **5** | `0x000C19DD` / file `0x819DC` | - | **TRAP** - `movs r3,#0; str r3,[r3]; udf #0xff` | [V] bytes `00 23 1b 60 ff de` |
| **6** | `0x0008CCE5` / file `0x4CCE4` | none (bit only) | samples device word CA `0x40100100`, splits five bit-fields, builds a 20-byte group/type message and emits it (the id-6 **reply** primitive) | [V] record |
| 0,2,4,7,8,9 | - | - | no registration site; dispatcher drops them | [V] |

Host-side rules (unchanged from the record, restated because the minimal-host sequence uses them):
write `1<<id` to CA `0x40039010` then OR bit0 into CA `0x400392D4`; never write CA
`0x400392F0` (firmware-owned ack). Only ids **1, 3, 6** are safe to post; **5 is forbidden**.

---

## 4. Layer B - the firmware's HCC H2D dispatch tables (new)

### 4.1 The registrar and the instance layout

Firmware registrar CA `0x102866` (file `0xC2866`, Thumb), byte-verified prologue
`f7 b5 20 4c 03 46 d4 f8 4c 04`:

    r4 = *CA 0x171B6C                  ; global HCC instance (literal file 0xC28EC)
    map  = *(r4+0x44C)                ; message map; *(map+0x10) = number of groups
    if group >= n_groups -> fail 0x8B2D
    recs = *(r4+0x458)                ; 12-byte group records: {u32 max_id; u32 item_array; u32 ?}
    rec  = recs + group*12
    itemarr = *(rec+4)                ; 16-byte dispatch items: {u32 id; u32 fn; u32 aux; u32 stats}
    for each 12-byte record {u16 id; u16 pad; u32 fn; u32 aux} in the static table:
        if id >= max_id -> fail
        itemarr[id].id = id ; itemarr[id].fn = fn

The map (CA `0x171B6C`) is the **chip resource** of the global HCC instance (same arrangement as
the host's `hcc_get_group_res(0)`: map at resource `+4`, record array at resource `+0x10`;
here resource base = instance `+0x448`). The instance is BSS; only its pointer is static
(file `0xC28EC` = `6c 1b 17 00` = CA `0x00171B6C`, matching the bench-observed registry CA).

### 4.2 The three registration sites and tables

| site (file) | group | table CA (file) | count | ids (ascending) |
| --- | ---: | --- | ---: | --- |
| `0x833C` | **2** | `0x10CC18` (`0xCCC18`) | `0x13` = 19 | 2,3,4,5,6,9,a,b,c,e,1a,1b,1f,23,25,26,27,28,2a |
| `0x8346` | **0** | `0x10CCFC` (`0xCCCFC`) | `0x28` = 40 | 0,1,2,3,6,7,8,9,a,b,c,d,e,f,10,11,12,13,15,16,17,18,19,1c,1d,1e,20,22,23,24,27,28,29,2b,2c,2d,2e,30,31 |
| `0x7BC8` | 0 | `0x10CBEC` (file `0xCCBEC`) | 1 | 2f |

Two entries are present but NULL: group 2 id `0x0C`, group 0 id `0x14` (registered as
"exists, no handler" - the dispatcher would skip them like an absent id).

The two registering code sites sit in the firmware's boot task (the same routine that later sends
the `Device plat ready` / `DEVICE READY` frames at file `0x7D28`); this matches the cold-boot
observation that the message layer is initialised before the ready announce.

### 4.3 The full id -> handler catalog

All handler addresses are CAs; **file = CA - 0x40000** (Thumb functions are odd; the code starts at
`(CA-0x40000)&~1`). Full tables are reproduced by
`tools/fw_h2d_tables.py <FIRMWARE.bin>`; Appendix A repeats them verbatim.

**Group 2 table (CA `0x10CC18`, 19 entries) - ids/handlers**

| id | handler CA | handler file | id | handler CA | handler file |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 02 | `0x000B9781` | `0x79781` | 27 | `0x0008CF85` | `0x4CF85` |
| 03 | `0x000BBC9D` | `0x7BC9D` | 0a | `0x000956A9` | `0x556A9` |
| 04 | `0x0008F52D` | `0x4F52D` | 0b | `0x00060B4D` | `0x20B4D` |
| 05 | `0x000B9781` | `0x79781` | 0c | **NULL** | - |
| 06 | `0x00092889` | `0x52889` | 0e | `0x0008E2AD` | `0x4E2AD` |
| 09 | `0x000627FD` | `0x227FD` | 1a | `0x000608BD` | `0x208BD` |
| 1b | `0x000845E1` | `0x445E1` | 1f | `0x0004C083` | `0x0C083` |
| 23 | `0x0004B1B1` | `0x0B1B1` | 25 | `0x000811E3` | `0x411E3` |
| 26 | `0x0008785D` | `0x4785D` | 28 | `0x000C5ED1` | `0x85ED1` |
| 2a | `0x0008F669` | `0x4F669` | | | |

**Group 0 table (CA `0x10CCFC`, 40 entries) - ids/handlers**

| id | handler CA | handler file | id | handler CA | handler file |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 00 | `0x00081695` | `0x41695` | 0f | `0x00062EC1` | `0x22EC1` |
| 01 | `0x00043D59` | `0x03D59` | 10 | `0x0005E28D` | `0x1E28D` |
| 02 | `0x000D6069` | `0x96069` | 11 | `0x00062E1D` | `0x22E1D` |
| 03 | `0x000B5FE1` | `0x75FE1` | 12 | `0x0005DE7D` | `0x1DE7D` |
| 06 | `0x000601FD` | `0x201FD` | 13 | `0x000968A5` | `0x568A5` |
| 07 | `0x0009832D` | `0x5832D` | 14 | **NULL** | - |
| 08 | `0x00096225` | `0x56225` | 15 | `0x00063A2D` | `0x23A2D` |
| 09 | `0x000974F9` | `0x574F9` | 16 | `0x00062D89` | `0x22D89` |
| 0a | `0x0005FF65` | `0x1FF65` | 17 | `0x0005F951` | `0x1F951` |
| 0b | `0x0005E2F5` | `0x1E2F5` | 18 | `0x0005F8BD` | `0x1F8BD` |
| 0c | `0x0006494D` | `0x2494D` | 19 | `0x00061649` | `0x21649` |
| 0d | `0x0005DE79` | `0x1DE79` | 1c | `0x0005DFA1` | `0x1DFA1` |
| 0e | `0x000A4A65` | `0x64A65` | 1d | `0x000B568D` | `0x7568D` |
| 1e | `0x00087801` | `0x47801` | 20 | `0x00085631` | `0x45631` |
| 22 | `0x0008F669` | `0x4F669` | 23 | `0x0008D4BD` | `0x4D4BD` |
| 24 | `0x000B55E1` | `0x755E1` | 27 | `0x00081B19` | `0x41B19` |
| 29 | `0x0004BC51` | `0x0BC51` | 28 | `0x000B5FB3` | `0x75FB3` |
| 2c | `0x0004AAE5` | `0x0AAE5` | 2d | `0x0004A8E9` | `0x0A8E9` |
| 31 | `0x0004A9E1` | `0x0A9E1` | 2b | `0x000A1521` | `0x61521` |
| 2e | `0x0008CFC5` | `0x4CFC5` | 30 | `0x000938F7` | `0x538F7` |

**Group 0, early single entry**: id `0x2F` -> handler CA `0x00040C19` (file `0x00C19`).

### 4.4 Handlers are real message processors (not data)

Every non-NULL handler disassembles as a Thumb function and follows the same shape:
`push {r4..lr}`; NULL-message error log (the firmware log helper CA `0x42C28` takes
`r0=format`, `r1=line`); then `bl CA 0x409EA` = the firmware's message-header fetch/validate
(it checks the message-pool membership and the header class field `hdr[5]>>1&7 <= 3`, then
tail-calls CA `0x408F8`), after which the handler reads header fields (`hdr[3]` = per-message
field B etc.) and payload at `hdr+0xC`. Examples:

* group 0 id 1 handler file `0x03D59`: after the header fetch it reads payload `+0x8` (u16 key
  into a 0x6C8-stride handler table), payload `+0xA` (u8 sub-key) and hands `hdr+0x18`
  (= payload `+0xC`) as data pointer to the per-key worker found in the table.
* group 0 id 2 handler file `0x96068`: reads payload `+0x0` and `+0x4` (two words, used as
  call arguments), payload `+0x8` (u16 key), payload `+0xA` (u16 sub-key), payload `+0xC`
  (data pointer) - same "descriptor table" pattern.
* group 0 id 9 handler file `0x574F8`: payload `+0x0` = u16 user id; if `0x0FFF` it reads
  payload `+0x2..+0x5` (4 address bytes) and looks the user up.
* group 0 id 0 handler file `0x41695`: **stub** - logs and returns 0; reads no payload at all.
  This is the safest possible H2D HCC probe frame (see section 6.3).
* group 2 id 2/5 handler file `0x79780` (one handler for both): payload `+0x8` = u16 key;
  payload `+0xC` used as a structure pointer; group 2 id 3 handler `0x7BC9C` reads payload
  `+0x0` as a pointer and payload `+0x4`/`+0x8` fields.

Appendix B lists the heuristic payload-offset reads for every handler (byte/half/word access kind).
The immediate practical consequence: **handlers with `ldr` (word) payload reads may dereference
attacker/host-supplied pointers**; only send syntactically valid vendor structs there.

### 4.5 Direction, space, and the one open anomaly

Confidence:

* **[V]** The code path, instance offsets, registration literals, group/count immediates and all
  table entries are byte-verified; the item layout is exactly the host's HCC dispatch item layout
  (16-byte `{id,fn,aux,stats}` filled from 12-byte `{u16 id; u16 pad; u32 fn; u32 aux}` records).
* **[V]** The tables are **receive** handler tables (register `fn` into dispatch items; handlers
  take the message object; nothing here sends).
* **[I]** They are the firmware's **H2D** (host->device over SR) dispatch, i.e. the device twin of
  the host's `hcc_msg_process`: (a) they are the only HCC registration sites in the image, (b)
  they live in the chip resource of the global instance (SR0 is the chip/control channel), (c)
  every one of the 20 explicit group-0 senders in the committed vendor catalog has a handler here,
  with the same ids the vendor builder stores into `msg[6]`, and (d) several handlers' payload
  keys match the vendor side's per-message structs.
* **[U] / open anomaly**: the vendor's *group-2* senders are only partly keyed to the firmware's
  group-2 table: 9 of 27 map there (ids 2,3,4,5,6,9,1f,25,26), 15 have their handler in the
  firmware's **group-0** table, 3 have none (`0x14` CAC start/finish pair, `0x21`). Two live
  hypotheses:
  1. **H1 (preferred)**: the firmware's HCC dispatcher keys the map on the *resource-group /
     destination nibble* (the field the host's `hcc_msg_tx` writes to `hdr[9]` from `hdr[1]>>4`),
     not on `hdr[0]&0xf`. Then "group 0 / group 2" in these tables really means destination
     resource 0 / 2, and all group-0 and most group-2 host sends (which default to destination 0)
     legitimately land in the same table.
  2. **H2**: the dispatcher keys on `hdr[0]&0xf` as expected, and the vendor's group-2 ids whose
     handlers sit in table 0 are either dispatched through a *core* map registered elsewhere (not
     found in this image by the registrar sweep) or are dead in this firmware build.
  A third candidate (ignore-group linear search across tables) is contradicted: the two id sets
  overlap (2,3,6,9,a,b,c,e,23,27,28), so groups must be distinguished.
  **Decisive experiment** (bench, host-only, no firmware change): post one SR-carried probe frame
  with a header that is identical except for the group nibble, destined to the payload-free stub
  (group 0 id 0) vs. a payload-free variant with group 2 id 0 (no handler) and with group 2 id 18
  (handler in table 0). Observing which drops/logs distinguishes H1 from H2 without touching a
  handler that reads payload (section 6.3). Until that runs, treat the *group-0* catalog as [I]-solid
  and the group-2 mapping as [I] but suspect for the 15 "borrowed" ids.

---
## 5. Cross-reference with the vendor driver's sent messages

### 5.1 (group, id) coverage: vendor senders vs. firmware handlers

Source for the vendor side: opensource/docs/phase37/sr-message-catalog.md section 2 - the 48 builders
with an explicit (group, id) in that catalog (entries whose group/id is only runtime-known, and
clone/helper/retry entries, are not joinable statically). Re-generated by
"tools/h2d_join_vendor.py FIRMWARE.bin --all"; the handler column is resolved against the tables of
section 4. "WARNING" marks the anomaly of section 4.5 (handler registered in the group-0 table for a
vendor group-2 send).

vendor senders parsed: 48 (explicit group/id only)

| grp | id | vendor sender | payload/total B | firmware handler CA/file | note |
| --- | --- | --- | --- | --- | --- |
| 0 | 0x01 | hdpp_config_send_event | dyn/dyn | 0x00043d59 / 0x03d59 | handler in table of its own group |
| 0 | 0x01 | hmac_config_send_event | dyn/dyn | 0x00043d59 / 0x03d59 | handler in table of its own group |
| 0 | 0x02 | hmac_config_alg_send_event | dyn/dyn | 0x000d6069 / 0x96069 | handler in table of its own group |
| 0 | 0x03 | hmac_sdt_recv_reg_cmd | dyn/dyn | 0x000b5fe1 / 0x75fe1 | handler in table of its own group |
| 0 | 0x07 | hmac_user_add | #0xa8/0xb4 | 0x0009832d / 0x5832d | handler in table of its own group |
| 0 | 0x08 | hmac_user_add_notify_alg | #0xa8/0xb4 | 0x00096225 / 0x56225 | handler in table of its own group |
| 0 | 0x09 | hmac_del_user_notify_dmac | #0xc/0x18 | 0x000974f9 / 0x574f9 | handler in table of its own group |
| 0 | 0x0a | hmac_mgmt_rx_delba | #0x20/0x2c | 0x0005ff65 / 0x1ff65 | handler in table of its own group |
| 0 | 0x0b | hmac_mgmt_tx_ampdu_start | dyn/dyn | 0x0005e2f5 / 0x1e2f5 | handler in table of its own group |
| 0 | 0x0c | hmac_enc_scan_send_req | #0x1fc/0x208 | 0x0006494d / 0x2494d | handler in table of its own group |
| 0 | 0x0e | hmac_mgmt_reset_psm | dyn/dyn | 0x000a4a65 / 0x64a65 | handler in table of its own group |
| 0 | 0x10 | hmac_sta_wait_join_rx_beacon | #0x10/0x1c | 0x0005e28d / 0x1e28d | handler in table of its own group |
| 0 | 0x11 | hmac_send_connect_result_to_dmac_sta | dyn/dyn | 0x00062e1d / 0x22e1d | handler in table of its own group |
| 0 | 0x12 | hmac_sta_up_update_edca_params_machw | #0x74/0x80 | 0x0005de7d / 0x1de7d | handler in table of its own group |
| 0 | 0x13 | hmac_sta_up_update_mu_edca_params_machw | dyn/dyn | 0x000968a5 / 0x568a5 | handler in table of its own group |
| 0 | 0x19 | hmac_chan_restart_network_after_switch | dyn/dyn | 0x00061649 / 0x21649 | handler in table of its own group |
| 0 | 0x1d | hmac_edca_opt_timeout_fn | #0x3c/0x48 | 0x000b568d / 0x7568d | handler in table of its own group |
| 0 | 0x2b | hmac_vip_frame_event_post | dyn/dyn | 0x000a1521 / 0x61521 | handler in table of its own group |
| 0 | 0x2c | hmac_multiap_handle_steering_req_msg | #0x1c/0x28 | 0x0004aae5 / 0x0aae5 | handler in table of its own group |
| 0 | 0x31 | hmac_multiap_parse_chan_sel_req_msg | #0x1d/0x29 | 0x0004a9e1 / 0x0a9e1 | handler in table of its own group |
| 1 | 0x0c | wal_netdev_set_mac_addr | #0x1c/0x28 | 0x0006494d / 0x2494d | handler in table of its own group |
| 2 | 0x01 | wal_acs_netlink_recv | dyn/dyn | 0x00043d59 / 0x03d59 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x02 | hmac_handle_scan_rsp_sta | dyn/dyn | 0x000b9781 / 0x79781 | handler in table of its own group |
| 2 | 0x02 | hmac_scan_proc_scan_req_event_exception | dyn/dyn | 0x000b9781 / 0x79781 | handler in table of its own group |
| 2 | 0x03 | hmac_handle_asoc_rsp_sta | #0x1c/0x28 | 0x000bbc9d / 0x7bc9d | handler in table of its own group |
| 2 | 0x03 | hmac_report_connect_failed_result | #0x1c/0x28 | 0x000bbc9d / 0x7bc9d | handler in table of its own group |
| 2 | 0x04 | hmac_sta_handle_disassoc_rsp | dyn/dyn | 0x0008f52d / 0x4f52d | handler in table of its own group |
| 2 | 0x05 | hmac_handle_connect_rsp_ap | #0x10/0x1c | 0x000b9781 / 0x79781 | handler in table of its own group |
| 2 | 0x06 | hmac_handle_disconnect_rsp_ap | dyn/dyn | 0x00092889 / 0x52889 | handler in table of its own group |
| 2 | 0x08 | hmac_acs_init_scan_hook | dyn/dyn | 0x00096225 / 0x56225 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x09 | hmac_send_mgmt_to_host | #0x1c/0x28 | 0x000627fd / 0x227fd | handler in table of its own group |
| 2 | 0x0d | hmac_mgmt_tx_event_status | dyn/dyn | 0x0005de79 / 0x1de79 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x14 | hmac_report_cac_finish | #0x20/0x2c | - | NO handler registered |
| 2 | 0x14 | hmac_report_cac_start | #0x20/0x2c | - | NO handler registered |
| 2 | 0x15 | hmac_config_receive_all_sta_rssi | #0x108/0x114 | 0x00063a2d / 0x23a2d | WARNING: handler in the g0 table, not g2 |
| 2 | 0x16 | hmac_spectral_scan_complete | #8/0x14 | 0x00062d89 / 0x22d89 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x17 | hmac_csi_complete | #0x2c/0x38 | 0x0005f951 / 0x1f951 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x18 | shuangta_irq_host_all | dyn/dyn | 0x0005f8bd / 0x1f8bd | WARNING: handler in the g0 table, not g2 |
| 2 | 0x1c | hmac_tx_ba_notify | #0x14/0x20 | 0x0005dfa1 / 0x1dfa1 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x1d | hcc_timer_timeout_proc | dyn/dyn | 0x000b568d / 0x7568d | WARNING: handler in the g0 table, not g2 |
| 2 | 0x1e | hmac_report_external_auth_req | #0x38/0x44 | 0x00087801 / 0x47801 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x1f | hmac_virtual_fill_tx_packet | #0x10/0x1c | 0x0004c083 / 0x0c083 | handler in table of its own group |
| 2 | 0x21 | hmac_tx_complete_read_error_process | #0x18/0x24 | - | NO handler registered |
| 2 | 0x22 | hmac_mgmt_send_deauth_frame_notify | #0x18/0x24 | 0x0008f669 / 0x4f669 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x24 | hmac_rx_data_send_disasoc_frame_notify | #0x10/0x1c | 0x000b55e1 / 0x755e1 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x24 | hmac_tx_msdu_rw_err_proc | #0x10/0x1c | 0x000b55e1 / 0x755e1 | WARNING: handler in the g0 table, not g2 |
| 2 | 0x25 | hmac_multiap_event_report | dyn/dyn | 0x000811e3 / 0x411e3 | handler in table of its own group |
| 2 | 0x26 | hmac_hsan_event_report | dyn/dyn | 0x0008785d / 0x4785d | handler in table of its own group |

- vendor group 0: 20 matched / 20 explicit senders
- vendor group 1: 1 matched / 1 explicit senders
- vendor group 2: 24 matched / 27 explicit senders

## firmware table entries with no explicit vendor sender
- g2 (group 2): ids ['0xa', '0xb', '0xc', '0xe', '0x1a', '0x1b', '0x23', '0x27', '0x28', '0x2a']
- g0 (group 0): ids ['0x0', '0x6', '0xd', '0xf', '0x14', '0x15', '0x16', '0x17', '0x18', '0x1c', '0x1e', '0x20', '0x22', '0x23', '0x24', '0x27', '0x28', '0x29', '0x2d', '0x2e', '0x30']
- g0-early (group 0): ids ['0x2f']

Readings:

* **Group 0 is fully covered (20/20).** For every explicit group=0 H2D sender the firmware has a
  handler with the same id. This is the strongest single direction argument for the table's role.
* **Group 1 has only one explicit sender** (wal_netdev_set_mac_addr, id 0x0C) and **no group-1 table
  exists**; id 0x0C happens to exist in the group-0 table too (handler 0x6494D). Either that message
  is misgrouped in the catalog, or the resource nibble explains it (H1).
* **Group 2 split** (see 4.5): 9 own-table, 15 borrowed from the group-0 table, 3 without a handler
  (0x14 CAC start/finish, 0x21 tx-complete-read-error). The three "no handler" ids are the most
  interesting negative: if the vendor sends them in a working system, their handlers must be
  registered in a map this lane did not find (a core map / a second registration function to hunt
  before the driver replica sends them).

### 5.2 Payload layouts - what is inferable, and what is not

The wire form is fixed (12-byte HCC header, section 1). The header fields the firmware itself
consumes are now confirmed from the handlers: hdr[3] (message field B) is used as user/vap id by
group-0 handlers, hdr[4..5] is the total length (payload+12), hdr[6..7] is the id. Payload starts at
hdr+0xC. Further payload layouts are per-message vendor structs; the vendor catalog gives the
builder size, and the firmware handler's payload accesses (Appendix B) constrain the first fields.
What is solid today:

| group/id | vendor builder | payload bytes (vendor) | firmware handler evidence for payload layout | confidence |
| --- | --- | ---: | --- | --- |
| 0/01 | hmac_config_send_event / hdpp_config_send_event | dynamic (caller buffer) | handler 0x03D59: u16 key at payload+8, byte at payload+0xA, data at payload+0xC. Host builder hmac_config_send_event(vap, key, len, src) via shared helper file 0x5714C: writes key (strh) at payload+8, len (strh) at payload+0xA, memcpy(src,len) at payload+0xC; payload[0..7] is not written by this builder (reserved or an undecoded preamble) | [V] offsets; [I] semantics |
| 0/02 | hmac_config_alg_send_event | dynamic | same helper + handler 0x96068: 2 words at +0/+4 passed as an 8-byte argument, u16 at +8, u16 at +0xA, data ptr at +0xC. The helper writes +8/+0xA/+0xC only; where the +0/+4 words are filled (alg builder after the helper) is not yet decoded | [V] offsets; [I] semantics |
| 0/03 | hmac_sdt_recv_reg_cmd | dynamic | handler 0x75FE1: word at +0, byte at +6, word at +8 | [I] |
| 0/07 | hmac_user_add | 0xA8 | handler 0x5832D: many byte/word reads within +0..+0xC, then object lookup by user id | [I] |
| 0/08 | hmac_user_add_notify_alg | 0xA8 | handler 0x56225: u16 at +0, u16 at +8, byte fields at +0x32/+0x8A/+0x91/+0xA5 | [I] |
| 0/09 | hmac_del_user_notify_dmac | 0x0C | handler 0x574F9: u16 user id at +0; if 0x0FFF then 4 address bytes at +2..+5 | [V] offsets |
| 0/0C | hmac_enc_scan_send_req (and catalogued wal_netdev_set_mac_addr) | 0x1FC / 0x1C | handler 0x2494D: byte fields at +0x13B, word ptr at +0x184 (scan-request-shaped) | [I] |
| 2/02, 2/05 | hmac_handle_scan_rsp_sta / hmac_handle_connect_rsp_ap | dynamic / 0x10 | one handler 0x79780 for both: u16 key at payload+8, data-structure pointer at payload+0xC - do not send synthetic payloads | [V] offsets |
| 2/03 | hmac_handle_asoc_rsp_sta / hmac_report_connect_failed_result | 0x1C | handler 0x7BC9C: payload+0 used as pointer, fields at +4/+8 | [I] |

Everything else: handler address and id are [V], the payload struct is not recoverable from this
image alone (the structs live in the vendor modules, not the firmware). For a from-scratch host,
those messages are blocked on the vendor source (or on a field-by-field black-box experiment).

---
### 5.3 Cross-reference with the two sibling lanes (both committed while this lane ran)

* **wifi-h2d-inject** (commit f64956f) built a validated, hard-bounded HCC frame injector: sysfs
  attributes h2d_inject / h2d_check / h2d_stats on the transport module; the 12-byte HCC frame
  model of hcc-re section 3.1 (dst nibble in +1 high nibble and +9; tag 0x5A5A at +0xA; message id
  5 refused in parser/builder/validator; announce hard-wired to mailbox id 3). Its report section 8
  lists "device H2D HCC dispatch table is [U]" as a limit and section 7.5 lists group=0 id=1/2 as
  "payload blobs not modelled". This lane's section 4 closes exactly that open item: the
  firmware's (group, id) receive map is now known, and section 5.2 gives the first payload fields
  for ids 0/1, 0/2, 0/3, 0/7/8/9, 0/0xC, 2/2/3/5/9. The two tools compose directly: the injector
  can run this lane's stage-1 probe and the 6.3 discriminating probe with no new host code.
* **wifi-host-replica** (commit 8978bd1, SPEC.md) fixes the same wire contract from the vendor
  side: the 12-byte header (group / bid / field A / field B / total length / id / field C / dst /
  tag / payload), the mailbox id-3 announce sequence, and "for the boot handshake the host sends
  nothing". Sections 6.1/6.2 here are consistent with that SPEC; the additions are (a) the
  device-side handler-existence proof per (group, id) and (b) the section 4.5 anomaly - a frame
  with a non-zero dst nibble may be the only way to reach the group-2 table.

## 6. Which messages a minimal host should send, and in what order

### 6.1 What the catalog changes for the bring-up plan

The boot deadlock analysed in wifi-boot-kick is **not** caused by a missing H2D message: the
device->host publish path parks on the host's out[1] service and the wiped 0xcece gate, and the
remaining fault is in the firmware's own DR descriptor-fill path (boot-kick sections 18-21).
Therefore:

1. **No HCC H2D frame is required to boot.** Do not add speculative config frames hoping to
   unblock the boot: they are handled by workers that expect the vendor structs, and several
   handlers dereference payload words - a synthetic frame can fault the firmware.
2. The H2D catalog is the checklist for the **driver-replica stage** (after boot is stable) and for
   two safe path-liveness probes.

### 6.2 Minimal sequence (recommended)

Stage 0 - boot service (mandatory, unchanged from boot-kick C1e):

1. Before release: start the D2H service loop - on out[1]!=0: ack CA 0x40101438=1, read+clear out[1],
   re-arm CA 0x40101414=1; keep CA 0x4000010C = 0x0000CECE served; release CA 0x40000108 = 0x00005A5A.
2. Keep the armed word (g+0xB4) held at 0 only if the documented fallback (boot-kick 19.4) is
   being run; that is not an H2D message.

Stage 1 - two safe liveness probes (optional, each independently revertible):

3. **Mailbox id 1** (no body): write 0x2 to CA 0x40039010, OR bit0 into CA 0x400392D4. The firmware
   enqueues its own global message object and runs the message worker. Expected: no fault, worker
   activity in the firmware counters (mask/state/DR fills per the boot-kick read set).
4. **HCC probe frame** (one SR node, group 0 id **0**, zero payload):
   - 12-byte header in a DMA-coherent host buffer: [0]=0x00 (group 0), [1]=0x01 (alloc nibble;
     destination nibble 0), [2]=0, [3]=0, [4..5]=0x000C (total length 12), [6..7]=0x0000 (id 0),
     [8]=0, [9]=0, [10..11]=0x5A5A (observed SR framing tag, see 5.2 caveat);
   - SR node word0 = buffer devva, word1 = (12<<16) | 0x6D2B (present/valid bits per the record),
     commit the producer index, then **mailbox id 3** announce (0x8 to CA 0x40039010, doorbell bit).
     With the committed wifi-h2d-inject module this probe is literally the sysfs line
     echo 'group=0 id=0 dst=0' > /sys/bus/pci/devices/0000:00:00.0/h2d_inject after h2d_arm and
     h2d_check (which already sets the tag/len/announce fields correctly).
   Expected: firmware dispatcher id 3 wakes the worker; the group-0 id-0 handler (file 0x41695) only
   logs and returns 0. Success = no new exception mask bit, no state change, no DR advance caused by
   the frame (a pure receive-path liveness proof). Do not use id 5 (mailbox trap) and do not send
   payload-carrying frames in this stage.

Stage 2 - driver replica (only when the vendor structs exist):

5. Use the section 4 catalog as the receive-side checklist. The group-0 senders present in the
   firmware table, in the vendor's own build order (per sr-message-catalog.md): config events
   (0,1)(0,2)(0,3) -> user add (0,7)(0,8) -> set-MAC (0,0xC) -> association/scan responses
   (2,2)(2,3)(2,5) -> EDCA (0,0x12)(0,0x13) -> mgmt/tx-path events. Each must set the header fields
   exactly (group, front nibble in +1 = destination resource, field B in +3 = user/vap id, id in +6,
   total length in +4) or the firmware's own header validation (hdr[5]>>1&7 <= 3 at CA 0x409EA)
   drops it.
6. Never post mailbox id **5**. Never write CA 0x400392F0. Never repurpose the group-4 D2H
   handshake ids as H2D (they are device->host).

### 6.3 The decisive probe for the section 4.5 anomaly

After the stage-1 id-0 frame works, run the discriminating set with an identical frame except
hdr[0]/hdr[9]: (a) group 0, id 0 (handler in table 0, payload-free stub); (b) group 2, id 0 (no
handler in either table); (c) group 2, id 0x18 (handler 0x1F8BD only in table 0); (d) group 0,
id 0x18 (same handler). H1 predicts the destination nibble in hdr[9] selects the table, so varying
hdr[9] 0 vs 2 while keeping hdr[0] fixed changes which frames reach the handler; H2 predicts only
the hdr[0]=0 frames are handled. The probe needs only log/UART evidence (read-only) and the
mask/state read set; no payload-carrying frame and no writes beyond the documented mailbox/SR path.

---
## 7. Open items, stated plainly

1. **The dispatcher function was not located byte-for-byte.** The registrar, instance layout, tables
   and handlers are proven; the function that consumes hdr[0]/hdr[9] and calls item[id].fn was not
   isolated in the image (candidate regions: boot-task code file 0x7E00-0x8356 which touches the map
   and does indirect calls; the second image file >= 0xD0000; the +0x448/+0x458 accessors at file
   0x923xx). This is the main reason the group-2 anomaly (4.5) stays open. A targeted hunt is
   worthwhile before the driver replica: search the image for a function that reads a byte, masks
   0xF, indexes a 12-byte record array and calls a 16-byte item's +4 pointer.
2. **The group-2/group-0 split** is unresolved (H1 vs H2, section 4.5); the decisive bench probe is
   in 6.3 and does not need firmware modification.
3. **Payload structs beyond the first fields are not recoverable from the firmware image** (5.2).
   The vendors' build code is the only static source; for a clean-room host this is a real blocker
   for group-0 id 7/8 (0xA8-byte user records), 0xC (0x1FC-byte scan request), and most of group 2.
4. **Three group-2 host sends have no firmware handler at all** (0x14 x2, 0x21); either dead in this
   build or dispatched in a map not found here.
5. **The 0x5A5A tag placement is still [I]** for the host->device direction: the record's live SR
   buffers show it at hdr+0xA, the vendor modules do not write the constant, and one host builder
   writes the payload length there before the transport hand-off. The stage-1 probe is designed to
   be insensitive: the firmware id-3 handler does not parse the buffer (it only wakes the worker),
   and the group-0 id-0 handler reads no payload; a wrong tag placement would only matter at the
   first payload-carrying message.

## 8. Verification and reproduction

Host-only, read-only. Nothing vendor is committed; FIRMWARE.bin and the .ko files stay local.

    PY=<repo>/.provision-venv/bin/python
    FW=<local>/FIRMWARE.bin (928,920 B)

    # 20 byte-level claims: registrar code, instance literal, table literals,
    # group/count immediates, mailbox anchors
    $PY tools/fw_h2d_tables.py "$FW" --verify        # -> VERIFY: 20/20 byte claims PASS

    # decode the three registration tables (id -> handler CA/file)
    $PY tools/fw_h2d_tables.py "$FW"

    # join with the committed vendor send catalog, coverage verdict per sender
    $PY tools/h2d_join_vendor.py "$FW" --all

    # heuristic payload-access extraction per handler (Appendix B)
    $PY tools/payload_offsets.py "$FW"

The tool constants (table CAs, call sites, literals) are the ones byte-asserted in the --verify list;
changing the firmware image will fail the verifier.

## 9. Evidence index (file offsets, all in FIRMWARE.bin unless noted)

| item | offset / CA | how verified |
| --- | --- | --- |
| registrar function | file 0xC2866 (CA 0x102866) | --verify + full disassembly |
| registrar instance literal | file 0xC28EC = CA 0x00171B6C | --verify |
| group-2 registration call | file 0x833C (r0=2, r1=CA 0x10CC18, r2=0x13) | --verify bytes + disassembly |
| group-0 registration call | file 0x8346 (r0=0, r1=CA 0x10CCFC, r2=0x28) | --verify bytes + disassembly |
| early single registration | file 0x7BC8 (r0=0, r1=CA 0x10CBEC, r2=1) | --verify bytes + disassembly |
| group-2 table | CA 0x10CC18, 19x12 B | decoded, handler addresses disassembled |
| group-0 table | CA 0x10CCFC, 40x12 B | decoded, handler addresses disassembled |
| early group-0 table | CA 0x10CBEC, 1x12 B | decoded |
| mailbox dispatcher | file 0x818AC | --verify bytes |
| mailbox table pointer | file 0xCC1B0 = CA 0x00118D68 | --verify |
| mailbox id-5 trap | file 0x819DC (00 23 1b 60 ff de) | --verify |
| mailbox id-1 handler | file 0x510 | --verify bytes |
| mailbox id-3 handler | file 0x85144 -> 0x85128 | --verify bytes |
| msg header fetch/validate | CA 0x409EA (file 0x9EA) | disassembled |
| firmware log helper | CA 0x42C28 (file 0x2C28) | disassembled |

Cross-reference documents (not re-derived here): phase37/sr-message-catalog.md (vendor H2D
builders), phase37/fw-ring-message-table.md (mailbox layer), phase30/the-gate-located.md (live
mailbox table dump), incoming/wifi-hcc-re/REPORT.md (transport/HCC contract),
incoming/wifi-boot-kick/REPORT.md (current boot state).

---

## Appendix A - generated table dump (tool output)

Output of `tools/fw_h2d_tables.py FIRMWARE.bin` (handler CA and file offset; NULL = registered
slot with no handler). This is the authoritative id -> handler map for this lane.

```
image: 928920 bytes (0xe2c98); CA = file + 0x40000
registrar: CA 0x102866 (file 0xc2866)

== group 2: table CA 0x10cc18 (file 0xccc18), 19 entries, registered at file 0x833c
   idx   id   handler CA handler file        aux
     0  0x2   0x000b9781    0x0079781        0x0
     1  0x3   0x000bbc9d    0x007bc9d        0x0
     2  0x4   0x0008f52d    0x004f52d        0x0
     3  0x5   0x000b9781    0x0079781        0x0
     4  0x6   0x00092889    0x0052889        0x0
     5  0x9   0x000627fd    0x00227fd        0x0
     6 0x27   0x0008cf85    0x004cf85        0x0
     7  0xa   0x000956a9    0x00556a9        0x0
     8  0xb   0x00060b4d    0x0020b4d        0x0
     9  0xc         NULL            -        0x0
    10  0xe   0x0008e2ad    0x004e2ad        0x0
    11 0x1a   0x000608bd    0x00208bd        0x0
    12 0x1b   0x000845e1    0x00445e1        0x0
    13 0x1f   0x0004c083    0x000c083        0x0
    14 0x23   0x0004b1b1    0x000b1b1        0x0
    15 0x25   0x000811e3    0x00411e3        0x0
    16 0x26   0x0008785d    0x004785d        0x0
    17 0x28   0x000c5ed1    0x0085ed1        0x0
    18 0x2a   0x0008f669    0x004f669        0x0

== group 0: table CA 0x10ccfc (file 0xcccfc), 40 entries, registered at file 0x8346
   idx   id   handler CA handler file        aux
     0  0x0   0x00081695    0x0041695        0x0
     1  0x1   0x00043d59    0x0003d59        0x0
     2  0x2   0x000d6069    0x0096069        0x0
     3  0x3   0x000b5fe1    0x0075fe1        0x0
     4  0x6   0x000601fd    0x00201fd        0x0
     5  0x7   0x0009832d    0x005832d        0x0
     6  0x8   0x00096225    0x0056225        0x0
     7  0x9   0x000974f9    0x00574f9        0x0
     8  0xa   0x0005ff65    0x001ff65        0x0
     9  0xb   0x0005e2f5    0x001e2f5        0x0
    10  0xc   0x0006494d    0x002494d        0x0
    11  0xd   0x0005de79    0x001de79        0x0
    12  0xe   0x000a4a65    0x0064a65        0x0
    13  0xf   0x00062ec1    0x0022ec1        0x0
    14 0x10   0x0005e28d    0x001e28d        0x0
    15 0x11   0x00062e1d    0x0022e1d        0x0
    16 0x12   0x0005de7d    0x001de7d        0x0
    17 0x13   0x000968a5    0x00568a5        0x0
    18 0x14         NULL            -        0x0
    19 0x15   0x00063a2d    0x0023a2d        0x0
    20 0x16   0x00062d89    0x0022d89        0x0
    21 0x17   0x0005f951    0x001f951        0x0
    22 0x18   0x0005f8bd    0x001f8bd        0x0
    23 0x19   0x00061649    0x0021649        0x0
    24 0x1c   0x0005dfa1    0x001dfa1        0x0
    25 0x1d   0x000b568d    0x007568d        0x0
    26 0x1e   0x00087801    0x0047801        0x0
    27 0x20   0x00085631    0x0045631        0x0
    28 0x22   0x0008f669    0x004f669        0x0
    29 0x23   0x0008d4bd    0x004d4bd        0x0
    30 0x24   0x000b55e1    0x00755e1        0x0
    31 0x27   0x00081b19    0x0041b19        0x0
    32 0x29   0x0004bc51    0x000bc51        0x0
    33 0x28   0x000b5fb3    0x0075fb3        0x0
    34 0x2c   0x0004aae5    0x000aae5        0x0
    35 0x2d   0x0004a8e9    0x000a8e9        0x0
    36 0x31   0x0004a9e1    0x000a9e1        0x0
    37 0x2b   0x000a1521    0x0061521        0x0
    38 0x2e   0x0008cfc5    0x004cfc5        0x0
    39 0x30   0x000938f7    0x00538f7        0x0

== group 0 (early single): table CA 0x10cbec (file 0xccbec), 1 entries, registered at file 0x7bc8
   idx   id   handler CA handler file        aux
     0 0x2f   0x00040c19    0x0000c19        0x0
```

## Appendix B - handler payload-access candidates (heuristic, tool output)

Output of `tools/payload_offsets.py FIRMWARE.bin`. `+N[W]` = a word load of payload offset N
(candidate pointer dereference - do not send synthetic data to such a handler); `[B]/[H]` = byte /
halfword reads. Offsets larger than the vendor payload size for that message are false positives of
the simple register-tracking heuristic (e.g. +0x4ac, +0xd60 are usually a reloaded r0 for a
different object); treat this as a probe-safety list, not a struct decoder.

```
## g2 (group 2), table CA 0x10cc18
- id 0x02 fn 0x000b9781: (no early payload access found)
- id 0x03 fn 0x000bbc9d: (no early payload access found)
- id 0x04 fn 0x0008f52d: +0x0[B], +0x4[H], +0x6[B], +0x8[B], +0x9[B], +0x11[B], +0x4ac[W], +0xd60[B], +0xd61[B], +0xde4[B]
- id 0x05 fn 0x000b9781: (no early payload access found)
- id 0x06 fn 0x00092889: +0x0[W], +0xc[W], +0x10[B], +0x11[B], +0x12[B], +0x13[B]
- id 0x09 fn 0x000627fd: +0x4ac[W], +0x51f[B]
- id 0x27 fn 0x0008cf85: (no early payload access found)
- id 0x0a fn 0x000956a9: +0xf0[B], +0xf1[B], +0x119[B], +0x294[W], +0x298[W], +0x2b0[W], +0x2cc[W], +0x4e6[B]
- id 0x0b fn 0x00060b4d: +0xc4[B], +0xc5[B], +0xc6[B], +0xc7[B], +0xc8[B], +0xc9[B], +0xca[B], +0xebc[B]
- id 0x0e fn 0x0008e2ad: (no early payload access found)
- id 0x1a fn 0x000608bd: (no early payload access found)
- id 0x1b fn 0x000845e1: +0x184[W], +0x4a4[W]
- id 0x1f fn 0x0004c083: (no early payload access found)
- id 0x23 fn 0x0004b1b1: +0x0[W], +0x3[B], +0x4[B], +0x86[H], +0x2a4[W]
- id 0x25 fn 0x000811e3: (no early payload access found)
- id 0x26 fn 0x0008785d: +0x0[W], +0x4[B], +0x8[W]
- id 0x28 fn 0x000c5ed1: (no early payload access found)
- id 0x2a fn 0x0008f669: +0x0[B], +0x2[B], +0x3[B], +0x4[H], +0xd60[B], +0xde4[B]
## g0 (group 0), table CA 0x10ccfc
- id 0x00 fn 0x00081695: (no early payload access found)
- id 0x01 fn 0x00043d59: +0x8[H], +0xa[B], +0x4a4[W], +0x4ac[W]
- id 0x02 fn 0x000d6069: +0x0[W], +0x8[H], +0xa[H], +0x1a8[W], +0xe14[W]
- id 0x03 fn 0x000b5fe1: +0x0[W], +0x6[B], +0x8[W]
- id 0x06 fn 0x000601fd: +0x4ac[W]
- id 0x07 fn 0x0009832d: (no early payload access found)
- id 0x08 fn 0x00096225: +0x0[H], +0x8[H], +0x32[B], +0x46[B], +0x84[B], +0x8a[B], +0x91[B], +0xa5[B], +0x4a0[W], +0x4a4[W], +0x4ac[W]
- id 0x09 fn 0x000974f9: (no early payload access found)
- id 0x0a fn 0x0005ff65: +0x0[B], +0x1[B], +0x2[H], +0x7[B], +0x9[B], +0xc[H], +0x10[H], +0x11[B], +0x14[B], +0x14[H], +0x15[B], +0x4ac[W]
- id 0x0b fn 0x0005e2f5: +0x0[B], +0x1[B], +0x2[B], +0x4[H], +0x6[B]
- id 0x0c fn 0x0006494d: +0x30[H], +0x32[H], +0x34[H], +0x13b[B], +0x184[W], +0x254[W], +0x258[W], +0x25c[W], +0x4ac[W]
- id 0x0d fn 0x0005de79: +0x0[B], +0x1[B], +0x184[W], +0x298[W], +0x2bc[B], +0x2d8[B], +0x2f4[B], +0x310[B], +0x4a4[W], +0x4ac[W]
- id 0x0e fn 0x000a4a65: +0x0[H], +0xc[H], +0x13[B], +0x1f8[B], +0x228[B], +0x230[B], +0x4ac[W]
- id 0x0f fn 0x00062ec1: +0x5[B], +0x6[H], +0x8[W], +0x10[W], +0x36[B], +0x181[B], +0x184[W], +0x452[B], +0x4a0[W], +0x4a4[W]
- id 0x10 fn 0x0005e28d: +0x0[B], +0x1[B], +0x4[H], +0xe[H], +0x4a0[W]
- id 0x11 fn 0x00062e1d: +0x5[B], +0x8[W], +0x181[B], +0x4a0[W], +0x4a4[W]
- id 0x12 fn 0x0005de7d: +0x0[B], +0x1[B], +0x184[W], +0x298[W], +0x2bc[B], +0x2d8[B], +0x2f4[B], +0x310[B], +0x4a4[W], +0x4ac[W]
- id 0x13 fn 0x000968a5: +0x0[B], +0x0[W], +0x184[W], +0x4a4[W]
- id 0x15 fn 0x00063a2d: +0x0[B], +0x1[B], +0x2[B], +0x3[B], +0x4[B], +0x5[B], +0x6[B], +0x8[B], +0x9[B], +0xa[B], +0x184[W], +0x488[B], +0x489[B], +0x48a[B], +0x48b[B], +0x48c[B], +0x48d[B], +0x4ac[W]
- id 0x16 fn 0x00062d89: (no early payload access found)
- id 0x17 fn 0x0005f951: (no early payload access found)
- id 0x18 fn 0x0005f8bd: +0x6[B], +0x294[W], +0x4a4[W]
- id 0x19 fn 0x00061649: +0x4[B], +0x5[B], +0x4a0[W], +0x4a4[W], +0x4ac[W], +0x4e7[B], +0x510[B], +0x523[B]
- id 0x1c fn 0x0005dfa1: (no early payload access found)
- id 0x1d fn 0x000b568d: (no early payload access found)
- id 0x1e fn 0x00087801: +0x0[W], +0x4[B], +0x8[W], +0x4a4[W], +0xcb1[B], +0xcbb[B]
- id 0x20 fn 0x00085631: (no early payload access found)
- id 0x22 fn 0x0008f669: +0x0[B], +0x2[B], +0x3[B], +0x4[H], +0xd60[B], +0xde4[B]
- id 0x23 fn 0x0008d4bd: +0x0[B], +0x1[B], +0x2[B], +0x184[W]
- id 0x24 fn 0x000b55e1: +0x0[W], +0x4ac[W]
- id 0x27 fn 0x00081b19: +0x0[B], +0x1[B], +0x182[B], +0x183[B], +0x4ac[W]
- id 0x29 fn 0x0004bc51: +0x0[B], +0x1[B], +0x2[H], +0x102[B]
- id 0x28 fn 0x000b5fb3: +0x0[W], +0x6[B], +0x8[W]
- id 0x2c fn 0x0004aae5: (no early payload access found)
- id 0x2d fn 0x0004a8e9: +0xcdc[B], +0xcdd[B], +0xcde[B], +0xcdf[B], +0xce0[B], +0xce1[B], +0xce2[B]
- id 0x31 fn 0x0004a9e1: +0xe1[B]
- id 0x2b fn 0x000a1521: +0x0[B], +0x1[B], +0xc[H], +0x184[W], +0x4ac[W], +0xac0[H]
- id 0x2e fn 0x0008cfc5: (no early payload access found)
- id 0x30 fn 0x000938f7: +0x0[B], +0x1[B]
## g0-early (group 0), table CA 0x10cbec
- id 0x2f fn 0x00040c19: (no early payload access found)
```
