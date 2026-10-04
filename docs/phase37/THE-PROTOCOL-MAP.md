# THE PROTOCOL MAP: the boot dialogue + steady-state message contract the port must implement (phase 37)

Merge deliverable of task `st_01a105b6`. This is the single implementable map for the port
(`opensource/lab/wifidrv1`), distilled from the four phase-37 lane reports plus the independent
verification pass. It states the protocol; it does not re-derive it.

**Sources (all four lanes cited; per-lane verdict from `docs/phase37/VERIFICATION.md`):**

| lane | report | verdict | what it contributes here |
| --- | --- | --- | --- |
| L1 | `docs/phase37/boot-dialogue-sequence.md` | **PASS** | section 1 - the ordered 4-step boot dialogue and the module-init order |
| L2 | `docs/phase37/sr-message-catalog.md` | **PASS** | section 2 - the SR-ring H2D message format the port posts |
| L3 | `docs/phase37/d2h-message-catalog.md` | **FAIL** (one wrong log line, see caveat) | section 3 - the D2H ids the host services and how it routes them |
| L4 | `docs/phase37/fw-ring-message-table.md` | **PASS** | section 3 - the device's H2D handler table and its id-5 trap |
| - | `docs/phase37/VERIFICATION.md` | checker: 599 checks, exit 0, 1 documented defect | the confidence carried into every claim below |

**VERIFICATION.md caveat, carried forward verbatim in effect.** `d2h-message-catalog.md` FAILs on one
prose line in its own section-7 log - `0x0018c0: movw r2, #0 -> .LC19` - where the binary has
`0x18c0: push {r4, r5, r6, lr}` (the real `movw r0, #0 -> .LC74` is at `0x18e0`). The symbol
`exception_info_msg_process` *is* at `.text.unlikely 0x18c0`; only that quoted instruction line is
wrong. Every load-bearing claim of L3 reproduces, and the checker confirms it explicitly. This map
uses L3's substantive claims and does **not** rest on the defective log line.

Conventions (the binary's, unchanged across the lanes): `.ko` offsets are **section-relative**
(`plat.ko`/`wifi.ko` `.text` base `0x38`); firmware offsets are **file** offsets with
runtime = file + `0x40000`. Mailbox CAs: H2D pending `out[0] = 0x40039010`, D2H pending
`out[1] = 0x40039014`, H2D doorbell `0x400392d4`, H2D ack `0x400392f0`, D2H doorbell `0x40101434`,
D2H ack `0x40101438`, D2H re-arm `0x40101414`.

Hazards from the record that bind this map: **never write CA `0x400392f0`** except as the fixed
`ack = 1` the protocol itself requires (section 1, step 2a), and **never read the RC misc window
`0x10161000`**. Nothing here needs either.

---

## 1. The boot sequence the port must replicate

Ordered, with the exact message, transport, fields, and the host/device function each step is
anchored to. This is the complete boot-time mailbox exchange; after step 4 the protocol is ring-only
(`docs/phase36/h2d-mailbox-boot-only.md`) - the mailbox carries no traffic in steady state.

### 1.0 Preconditions (no messages are sent)

Two init orders must run before any message moves - replicate them as the port's init skeleton.
Both are L1-verified against the ELF relocation table.

`plat.ko` `init_module` @ `0x1a75c`, in order:

```
0x1a760 bl plat_res_init
0x1a764 bl oal_main_init
0x1a770 bl oam_main_init
0x1a77c bl sdt_drv_main_init
0x1a788 bl low_power_init
0x1a794 bl plat_hcc_init
```

`plat.ko` `plat_hcc_init` @ `0x1a690`, in order (`bal_init`/`hcc_init` register the credit/hcc
callbacks - the transport the messages below ride):

```
0x1a694 bl plat_custom_init
0x1a6a0 bl plat_main_init
0x1a6ac bl pcie_init_static_res
0x1a6bc bl bal_init
0x1a6c8 bl hcc_init
0x1a6d4 bl plat_exception_init
```

`plat.ko` `pcie_main_init`, then `pcie_ete_init`, then `pcie_comm_init` - the message service comes
up **last** and **arms but never sends**:

```
pcie_main_init : 0x768 pcie_init_default / 0x8e8 pcie_ete_init / 0x944 pcie_comm_init
pcie_ete_init  : 0x78b8 pcie_ete_intr_init / 0x78d4 pcie_ete_rings_init
pcie_comm_init : 0x171e8 bl pcie_thread_init ; 0x171f4 b pcie_msg_init (tail call)
```

`pcie_msg_init` @ `0xb6e4` builds the message context, zeroes both mailbox words
(`0xb738` `*out[0]=0`, `0xb740` `*out[1]=0`), allocates and zeroes the 0x58-byte D2H handler table
(11 entries * 8 B, `0xb764`/`0xb774`), registers the four D2H handlers (section 3.1), installs
`pcie_msg_handle` into chip op slot `0x68` (`0xb7e8`) and `pcie_msg_send_irq` as the channel send
callback (`0xb794`/`0xb7d0`, `channel[+0x60]`). **It sends nothing.** The port's arm-only mirror of
this is the gate: no H2D bit goes out until step 1.

### 1.1 The dialogue, step by step

| # | who -> who | transport | id / register | effect |
| --- | --- | --- | --- | --- |
| 1 | host -> device | ring **+** mailbox | H2D id **3** | post SR node, advance producer, announce bit 3 |
| 2 | device -> host | mailbox `out[1]` | D2H bit **6** | wake the host receive/SR-pump thread |
| 3 | host -> device | mailbox | H2D id **5** | receive-buffer check (answer to bit 6) |
| 4 | device -> host | mailbox `out[1]` | D2H bit **2** | "platform ready", releases host init |
| 5 | (radios up) | ring only | - | `wifi.ko` hmac/wal/hdpp init; mailbox silent thereafter |

**Step 1 - host: SR descriptor post + announce (id 3).** `plat.ko` `shuangta_ete_sr_dscr_fill`
@ `0x17858` - the same routine the ops table `g_st_pcie_bus_driver` exposes
(`docs/phase25/chip-ops-table.md`). It fills one SR node, commits the producer, then announces:

```
0x178c0 ldr r2, [r4, #0xc]           ; packed producer index (low 10 bits)
0x178cc str r1, [r3, r2, lsl #3]     ; node[idx].word0 = buffer devva
0x178e0 str r2, [r3, #4]             ; node[idx].word1 = (len << 16) | flag
0x178ec bl  pcie_ete_ring_ptr_plus   ; advance producer (the commit)
0x178f0 ldr r0, [r5, #0x80]          ; the chip object
0x178f4 mov r1, #3                   ; id 3
0x178f8 bl  pcie_msg_send            ; pcie_msg_send(chip, 3)  <-- the announce
```

`pcie_msg_send(chip, id)` @ `0x160f4` writes the pending bit and rings the doorbell:

```
0x16194 ldr r2, [r6, #0x2c]          ; out[0] CA 0x40039010
0x1619c str r3, [r2]                 ; *out[0] = pending | (1<<id)
0x161a4 ldr r2, [r6, #0x34]          ; doorbell CA 0x400392d4
0x161ac orr r3, r3, r1
0x161b0 str r3, [r2]                 ; *doorbell |= 1
```

The **body** (the HCC message) travels in the SR ring node, not the mailbox - format in section 2.
This is the id-6 trigger established in `docs/phase24/id6-trigger-proven.md`.

**Step 2 - device -> host: bit 6.** The firmware's H2D dispatcher (section 3.3, file `0x818ac`) is
entered on the id-3 bit, consumes `out[0]`, and calls handler id 3 = file `0x85144` (fn
`0x000c5145`, arg `rt 0x0010C0F4`). That handler signals the pcie_msg object (`arg+0x98 = 1` via
`ldrex/strex`) and runs the message worker on `arg+0x50`; the work it drains emits the device's
notify `out[1] = 0x40` (bit 6) through `d2h_notify(0, 6)` (section 3.4) - observed live in every
takeover (`phase20/runtime-msg.md`, `phase24/handler-table.md`). Host side: id 6 ->
`pcie_trigger_ete_sending_handle` @ `0x15efc`, a 4-byte `b pcie_wkup_thread` that sets
`comm+0x28 = 1` and wakes the receive thread.

**Step 3 - host -> device: id 5.** The id-6 handler woke exactly the thread whose receive path
issues the receive-buffer check:

```
pcie_rx_handle @ 0x1655c  ->  pcie_ete_dr_get_uploadbuf @ 0x152c4
  ->  pcie_ete_rcv_buff_check @ 0x14d74
       0x15138 ldr r3, [r4, #0x68]
       0x1513c mov r1, #5              ; id 5
       0x15140 ldr r0, [r3, #0x80]     ; the chip object
       0x15144 bl  pcie_msg_send       ; pcie_msg_send(chip, 5)
```

**Hazard, decide at implementation time:** on the device, H2D id **5** is registered to a `BUG()`
trap - file `0x819dc`, raw bytes `00 23 1b 60 ff de` (`movs r3,#0` / `str r3,[r3]` / `udf #0xff`).
The vendor's own id-5 send sits on the `pcie_ete_rcv_buff_check` **failure** branch (it returns
error `0xffff74d3`), so a healthy boot either never posts id 5 or this blob's id-5 slot is a
deliberate stub. **The port must not post id 5 on its success path.**

**Step 4 - device -> host: bit 2.** The device's ETE bring-up function (file `0x86e14`) ends by
announcing ready and ringing the device-side doorbell:

```
0x86f54 movs r2, #4                  ; bit 2 = id 2
0x86f5a str  r2, [r3]                ; *out[1] = 4
0x86f5e add.w r3, r3, #0xc8000
0x86f62 add.w r3, r3, #0x420         ; device-side doorbell 0x400a1434
0x86f66 str  r2, [r3]                ; ring it
```

Host side: `pcie_msg_handle` @ `0x171f8` computes `id = lowest set bit of out[1]` = 2. **In the
mailbox layer, index 2 has no registered handler** (`pcie_msg_init` registers {1,3,6,7}), so the
vendor host takes the `0x1739c` "no handler registered" branch and drops it. The *load-bearing*
completion is the **HCC layer** group-4 id 1 = `device_plat_ready_msg_process` @ `0xeb78`, which
calls `complete()` and releases the host's init wait (section 3.2). The port must implement the HCC
completion; the bare mailbox bit 2 is a no-op it may log and drop.

**Step 5 - radios up.** After step 4 releases platform init, `hi5622v100_wifi.ko` initialises the
hmac/wal/hdpp layers on top of the completed dialogue. `wifi.ko` contains **no** `pcie_msg_send`
call site, so the mailbox dialogue above is the complete boot-time mailbox exchange; steady-state
traffic is SR/DR ring + HCC only.

### 1.2 Fixed acknowledgement rules (id-independent)

The port must mirror both sides' fixed maths - these are the same on both ends and are not
per-message:

* Host receive (`pcie_msg_handle` @ `0x171f8`): `ack 0x40101438 = 1` (`0x17254`), read + clear
  `out[1] = 0x40039014` (`0x1725c`/`0x17260`), `re-arm 0x40101414 = 1` (`0x172ac`),
  `id = 31 - clz(out[1] & -out[1])` (`0x172b4`/`0x172b8`), dispatch `table[id]` (`0x172dc`),
  argument `entry+4` (`0x172ec`).
* Device receive (firmware dispatcher @ file `0x818ac`): `*ack 0x400392f0 = 1` (`0x818b8`),
  `out[0] = 0` (`0x818be`), `*doorbell 0x400392d4 = 8` (`0x818c4`),
  `id = 31 - clz(out[0] & -out[0])` (`0x818ca`-`0x818d2`), loop lowest-bit-first, skip id > 9,
  call `table[id].fn(table[id].arg)` (`0x818e0`-`0x818e8`), clear the bit, repeat.
* **No D2H handler ever writes the device.** `pcie_msg_send` has exactly four call sites in
  `plat.ko` (`0xaf8` and `0x2920` are data references, not calls; the two real calls are the id-5
  and id-3 sends in steps 3 and 1) and `wifi.ko` has none. The D2H direction is
  **notify-and-carry**, never request-and-reply.

---

## 2. The steady-state SR-ring message formats the port must post

Every H2D message the port posts - in boot and in steady state - is one SR node plus the id-3
announce, where the node points at an **HCC message**: a 12-byte header followed by payload. The
announce bit is *always* 3; the per-message discriminator is the HCC header, not the mailbox id.

### 2.1 The SR path (one function, always id 3)

```
builder fills node {word0 = buffer devva, word1 = (len<<16)|flag}
  -> advance producer index (the commit)              shuangta_ete_sr_dscr_fill @0x17858
  -> pcie_msg_send(chip, 3)                            @0x178f4 / @0x178f8
```

The node words, quoted from the vendor's own accessors (`docs/phase25/chip-ops-table.md`, L2):

| field | width | meaning |
| --- | --- | --- |
| `word0` | u32 | buffer device VA (the HCC message object) |
| `word1` | u32 | `(len << 16) | flag`; flag bits seen live `0x6d2b | 0x4000 | 0x2000` |

### 2.2 The 12-byte HCC header (fields and who writes each)

| off | width | field | written by |
| --- | --- | --- | --- |
| +0x00 | u8 | **group / type**, low nibble 0-15, selects the per-group message map (`msg[0] & 0xf`) | builder |
| +0x01 | u8 | low nibble = alloc state (`1` = allocated); high nibble = **group/thread index** (resource-group selector used by `hcc_queue_get_by_msg`) | `hcc_msg_alloc` + builder |
| +0x02 | u8 | message field A (per message) | builder |
| +0x03 | u8 | message field B (per message) | builder |
| +0x04 | u16 | **total length = payload + 12** | `hcc_msg_alloc` (`0x11db4` `add r5,r0,#0xc`; `0x11e18` `strh r5,[r2,#4]`) |
| +0x06 | u16 | **message id**; selects the 16-byte entry in the type map | builder |
| +0x08 | u8 | message field C (per message; `wal_*` carry a sequence counter here) | builder |
| +0x09 | u8 | **retry counter**; `hcc_msg_tx` copies it from `+1` high nibble, `hcc_msg_tx_to_core` from `+1` low nibble + 1 (bails above 2) | `hcc_msg_tx` (`0x11c30`/`0x11c34`/`0x11c38`) |
| +0x0a | u16 | transport tag; live SR buffers show `0x5a5a` here. **Not written by either module** - treated as an SR/ETE-layer constant outside the HCC header. | (SR/ETE layer) |
| +0x0c | - | **payload start** (`hcc_msg_alloc` reserves `size + 12`; builders copy to `msg + 0xc`) | builder |

Allocation limits and helpers the port must honour (L2, all re-verified):

* `hcc_msg_alloc(size, clear)` @ `0x11d9c` - max size `movw r3, #0x674` (`0x11d9c`), total
  `size + 12`.
* `hcc_queue_get_by_msg(msg)` @ `0x12a00` - queue = per resource group
  (`hcc_get_group_res(msg[9])`) and `hcc_msg_get_msg_map(msg[0] & 0xf)`, indexed by the u16 id at
  `msg+6` (16-byte entries).
* The shared alloc helper `hmac_config_*` builders use @ `0x5714c` allocates `len + 0x10`
  (`add r0, r4, #0x10`) and sets group 0 (`bfc r3, #0, #4`); the WAL analogue `wal_alloc_cfg_event`
  @ `0x126ef0` sets group 2.
* `0x5a5a` is **absent** from `wifi.ko`'s `.text`/`.data`/`.rodata` entirely (L2 verified); the port
  should emit it as an SR-layer constant, not as part of the HCC header it builds.

### 2.3 The message id space to implement (HCC group / id)

`sr-message-catalog.md` catalogs every vendor builder - 60+ `(group, id, payload-len)` triples with
the exact instruction that sets each field. That table is the authoritative checklist for the port;
the entries the port cannot omit for a minimal bring-up (association, scan, key/EDCA config, user
add) are, quoted with their built size:

| group | id | builder | payload B | total B |
| --- | --- | --- | --- | --- |
| 0 | #1 | `hmac_config_send_event` (helper id 1) | dyn | dyn |
| 0 | #2 | `hmac_config_alg_send_event` (helper id 2) | dyn | dyn |
| 0 | #8 | `hmac_user_add_notify_alg` | 0xa8 | 0xb4 |
| 0 | #7 | `hmac_user_add` | 0xa8 | 0xb4 |
| 0 | #9 | `hmac_del_user_notify_dmac` | 0xc | 0x18 |
| 0 | #0xc | `wal_netdev_set_mac_addr` | 0x1c | 0x28 |
| 2 | #2 | `hmac_handle_scan_rsp_sta` / `hmac_scan_proc_scan_req_event_exception` | dyn | dyn |
| 2 | #3 | `hmac_handle_asoc_rsp_sta` / `hmac_report_connect_failed_result` | 0x1c | 0x28 |
| 2 | #5 | `hmac_handle_connect_rsp_ap` | 0x10 | 0x1c |
| 2 | #9 | `hmac_send_mgmt_to_host` | 0x1c | 0x28 |
| 2 | #0x12 | `hmac_sta_up_update_edca_params_machw` | 0x74 | 0x80 |
| 2 | #0x15 | `hmac_config_receive_all_sta_rssi` | 0x108 | 0x114 |

**Why ids repeat.** The group nibble alone does not separate messages: `hcc_queue_get_by_msg` keys
the map on the *resource group* (`msg[9]` = high nibble of `msg+1`), so two builders with the same
`(group, id)` can land on different queues - the discriminator is the payload plus the resource-group
nibble, not the id alone (e.g. `(2,0x14)` = cac_start/cac_finish; `(2,3)` =
connect_failed/asoc_rsp). The port must set byte `+1`'s high nibble correctly or the message routes
to the wrong queue. `sr-message-catalog.md` section 3 carries the per-builder instruction and raw
bytes for all 190 triples; use it as the field-by-field build reference.

---

## 3. The D2H messages the port must service, with the replies each expects

Two independent layers, and the port must implement both.

### 3.1 Layer A - the mailbox notify ids the host registers: exactly {1, 3, 6, 7}

`pcie_msg_init` @ `0xb6e4` registers four handlers; the table entry layout is
`{handler @ +0, arg @ +4}`, `id = lowest set bit of out[1]`, and `pcie_msg_register` rejects id > 10:

| D2H id | host handler (offset) | arg | what it does | reply the port owes |
| --- | --- | --- | --- | --- |
| **1** | `pcie_dev_ready_msg_handle` @ `0x8784` | chip obj | `bx lr` - an **empty stub**; the real device-ready work is HCC group-4 id 1 | **none** |
| **3** | `pcie_ete_transfer_done_handle` @ `0x8730` | chip obj | `mov r2,#0` / `mov r1,#0x1f` / `b pcie_ete_d2h_isr_handle` - walks DR channels 0..4 and drains completed receive buffers | **none** (calls `oal_pcie_transfer_done` -> BAL rx cb; no mailbox write) |
| **6** | `pcie_trigger_ete_sending_handle` @ `0x15efc` | msg obj | `b pcie_wkup_thread` -> sets `comm+0x28 = 1` and `__wake_up`s the SR/TX pump thread | **none** (the pump's own work posts new SR messages; it is not a reply to bit 6) |
| **7** | `pcie_trigger_ete_sending_handle` @ `0x15efc` | msg obj | same as id 6 | **none** |

Ids **0, 2, 4, 5, 8, 9, 10** are unregistered in this layer; a device bit there hits the
`0x1739c` "no handler registered" path. Two device bits are observed live: `out[1] = 0x40` (bit 6,
step 2) and `out[1] = 0x04` (bit 2, step 4 - dropped by the mailbox layer).

**Service semantics (what a "reply" means here loops back, not forward):** none of these handler
ids is answered by an immediate H2D message. The *only* synchronous host->device message on a
receive path is `pcie_msg_send(chip, 5)` in `pcie_ete_rcv_buff_check`, i.e. the boot step-3
receive-buffer check - host-initiated, not a reply to any id. The port's D2H service routine is
therefore: ack the mailbox (`*ack 0x40101438 = 1`), read+clear `out[1]`, re-arm
(`0x40101414 = 1`), then dispatch the lowest set bit; the *effect* per id is the drain/wake above,
and the reply obligation is **empty** for all four.

### 3.2 Layer B - the HCC message-table D2H ids (the payload that rides a D2H transfer)

The payload of a D2H message is an HCC frame; the host dispatches by `(group, id)` via
`hcc_msg_process` @ `0x1204c` (`group = bufr[0] & 0xf`, `id = u16 @ bufr+6`, dest = `table + id*16`,
handler at `dest+4`). Group tables are registered by `hcc_msg_register_tab_chip/_core`.

**Group 4 - the ready/heartbeat/excep table** (`.data+0x2660`, 5 entries; registered by
`plat_init_bal_hcc_excp` @ `0xe380`, `count=5`, `group=4`). This is the table the port must match for
handshake completion; note the record's off-by-one correction, byte-verified:

| id | handler | semantics | reply |
| --- | --- | --- | --- |
| 0 | `oam_rx_post_action_function` @ `0xdabc` | `bl oam_upload_device_log_to_sdt` - forwards a device log blob (`len-13 <= 0x673`) to SDT/netlink | none |
| 1 | `device_plat_ready_msg_process` @ `0xeb78` | checks source core 0, prints `"Device plat ready! chip id : %d"`, `0xebcc bl complete` - **releases wait #1** | **none** |
| 2 | `host_ready_msg_process` @ `0xebd8` | payload = `len-12` (bounded `len-13 <= 0x62`), `memcpy` into a per-core `0x64` record, sets ready bit, `0xed24 bl complete` - **releases wait #2** | **none** (`dmb`/`dsb`/`sev` only) |
| 3 | `exception_info_msg_process` @ `0x18c0` (`.text.unlikely`) | copies the 0x30-byte body into the exception record `108*core + 0x58` | none |
| 4 | `heartbeat_msg_process` @ `0x10214` | `0x10280 bl hrtimer_start_range_ns` - restarts the heartbeat timer | none |

**Record correction the port must bank:** group-4 id **1** is `device_plat_ready_msg_process`, id
**2** is `host_ready_msg_process`. The phase-24 table read the id field from `entry+8` (the *next*
entry) and is off by one; `phase19/fw-handshake.md` was right. The firmware's own handshake order is
`#1 "Device plat ready"` then `#2 "DEVICE READY"`, and the host's two `complete()` releases are
driven by ids 1 and 2 respectively.

**Groups 0-3 (wifi.ko tables) - the cfg80211-facing D2H events.** Registration sites re-verified:
`hdpp_main_init` @ `0x12e0` (group 3: 5 entries, call @ `0x135c`; group 0/core: 7 entries, call @
`0x136c`); `hmac_main_init` @ `0x3f6c8` (group 1: 0x1c = 28 entries, call @ `0x3f778`; group 2:
0xc = 12, call @ `0x3f788`; group 3: 8, call @ `0x3f798`); `wal_main_init` @ `0x123cac` (group 2:
0x13 = 19, call @ `0x123ce0`). The two names that matter to this project are `hmac_d2h_pm_event`,
`hmac_d2h_temp_state_update`, plus `wal_send_mgmt_to_host` / `wal_cfg80211_cac_report`. The full
id->handler lists are `[record]` in `phase25/full-event-vocabulary.md`; the port must reproduce the
hmac/wal/hdpp handler set to service these. **Reply for all of them: none** (one-way).

### 3.3 The device-side H2D handler table (what the port's peer contains)

L4 reconstructed the device's own H2D dispatch - useful because it fixes what the port may safely
post. Table at runtime `0x00118D68` (`ctx+0x20`), 8-byte entries, ids 0..9, populated exactly
**1, 3, 5, 6**; dispatched by the bit position of `out[0]`:

| id | handler (file) | arg | works? |
| --- | --- | --- | --- |
| 1 | `0x510` | 0 | yes - posts the global msg object onto the firmware queue + wakes worker |
| 3 | `0x85144` | `rt 0x0010C0F4` | yes - SR announce path (step 1); the only id with a non-NULL arg |
| 5 | `0x819dc` | 0 | **no - `BUG()` trap** (`00 23 1b 60 ff de`) |
| 6 | `0x4cce4` | 0 | yes - samples CA `0x40100100`, builds a 20-byte message, emits it (the reply path) |
| 0,2,4,7,8,9 | - | - | empty; no registration site exists |

The mailbox carries **no body**: `cmp r4, #9` bounds the id, and each handler reads only the pending
bit (and its `arg`). Bodies are the HCC frames of section 2. **Open in L4:** whether the vendor's
non-id-3 init-time H2D sends carry a body, and whether id 5 is dead in a working boot or a
deliberate stub - the port should treat id 5 as forbidden until this is settled.

---

## 4. Remaining gaps: implementable-now vs unknown

Numbered; each marked **implementable-now** (the binary/record already fixes the decision) or
**unknown** (needs an experiment or a device read before it can be coded).

1. **Boot dialogue as specified (steps 1-4, section 1).** **Implementable-now.** Every offset,
   register, id and field in the four steps reproduces under the checker; the port can arm
   `pcie_msg_init`, post an SR node with announce id 3, wake the receive thread on bit 6, perform
   the receive-buffer check, and wait for the group-4 id-1 `complete()`.
2. **HCC 12-byte header layout + SR node words (section 2.1/2.2).** **Implementable-now.** Fields,
   widths, `+0x0c` payload start, `size + 12` total, and the `0x674` alloc cap are all fixed; the
   `0x5a5a` SR tag is to be emitted as an SR-layer constant.
3. **The full H2D message-id checklist (`sr-message-catalog.md` section 2/3).** **Implementable-now
   (build the minimal subset first).** 60+ `(group,id,len)` builders with per-field instructions;
   the port can implement the ones it needs for bring-up and add the rest as the driver grows.
4. **Resource-group nibble routing (`msg+1` high nibble).** **Implementable-now** as a rule; the
   *per-message* correct value for every builder is `[record]` and must be read from
   `sr-message-catalog.md` section 3 alongside each id.
5. **D2H service routine for {1,3,6,7} with ack/clear/re-arm + lowest-set-bit dispatch.** 
   **Implementable-now.** Fixed arithmetic, no per-id reply obligation.
6. **Group-4 handshake handlers (ids 0-4) and the id 1 vs 2 correction.** **Implementable-now.**
   `.data` bytes + `.rel.data` relocs + registration all byte-verified; the port must wire id 1 to
   `device_plat_ready` and id 2 to `host_ready`.
7. **Never post H2D id 5 on the success path (section 1.1 step 3).** **Implementable-now** as a
   prohibition. *Do not* relax it until gap 10 is resolved.
8. **Group 0-3 (hmac/wal/hdpp) D2H handler set - the cfg80211-facing events.** **Partly unknown.**
   Registration sites are implementable-now; the id->handler lists are `[record]` only
   (`phase25/full-event-vocabulary.md`) and need a pass to promote them to verified.
9. **Which firmware id the D2H bit carries for each HCC event.** **Unknown.** The D2H notify call
   site is not in `FIRMWARE.bin` (no `BL`/`BLX`/literal names `d2h_notify` @ file `0x86170`), so
   the emitter->id mapping is `[inferred]` except bit 6 (observed live) and bit 2 (observed live).
   Needs a second-image / runtime read.
10. **id 5 status: dead path vs deliberate stub (section 1.1, 3.3).** **Unknown.** Needs the live
    boot trace to show whether `pcie_ete_rcv_buff_check`'s failure branch is ever taken.
11. **Entry point B's registrar unresolved (`*(*(0x170E08)+0x10)+0x24`).** **Unknown** statically;
    the argument that it writes the same table rests on the uniqueness of the four fn values plus
    the live dump - not needed by the port, but it weakens gap 3's completeness claim.
12. **Whether id 6's emitted 20-byte message is announced on `out[1]`.** **Unknown** (not a direct
    call in the image; the handler hands the buffer to the shared validate/route pair).
13. **L3's defective log line (`0x18c0`).** **Cosmetic, resolved here.** Does not affect the port;
    recorded so no downstream reader re-imports the bad line.
14. **The `[record]`-only id->handler lists for groups 0-3.** **Unknown** pending the phase-25
    vocabulary pass; blocks gap 8's completion, not the boot dialogue.

---

## 5. One-page port checklist

Arm (no sends) -> post SR node + announce id 3 -> service device bit 6 (wake pump) -> perform the
receive-buffer check -> wait for group-4 id 1 `complete()` (mailbox bit 2 is a droppable no-op) ->
radios up -> steady state: build HCC frames (12-byte header, section 2.2) into SR nodes announced
with id 3, and service D2H ids {1,3,6,7} + group-4 {0..4} + groups 0-3 with no reply obligation.
Never post H2D id 5 on the success path; never write CA `0x400392f0` outside the fixed `ack = 1`;
never read `0x10161000`.
