# H2D frames: the exact catalogue, builders and validators for the vendor post-ready messages

**Lane:** wifi-h2d-frames (shared task task-93), **host-only**, 2026-11-24.
**Status:** static reversal + compile-verified implementation + bench procedure done. The bench
router was never contacted; no image was built; no vendor binary, object code, firmware byte,
calibration value or secret is reproduced here - only derived addresses/constants, already
published frame captures and original code.

**Deliverables (this directory):**

| path | contents |
| --- | --- |
| REPORT.md | this document: the two H2D layers, the exact frame catalogue, builders/validators, the SR carrier semantics, bench procedure, evidence ledger |
| catalogue.md | generated data tables: 48 vendor H2D senders and 60 firmware receive-table slots with probe-risk classes |
| BUILD-NOTES.md | build/test evidence (host C99 -Werror + kernel 6.18.60 module compile), reproduction commands |
| src/h2d_frames.h, src/h2d_frames.c | pure-C library: 12-byte HCC header model, exact builders, refusal/policy validator, SR node + packed producer arithmetic, mailbox/post plan, D2H ready decoder |
| src/h2d_catalogue.inc | GENERATED data tables (vendor senders + firmware handlers + risk) |
| src/h2d_selftest.c | device-free known-answer + refusal suite (233 checks) and a frame CLI |
| tools/gen_catalogue.py | regenerates catalogue.md + h2d_catalogue.inc from the committed lane reports (--check gate) |
| tools/build.sh | host-only: regenerate, compile -Werror -pedantic, run the suite |
| tools/kernel-check.sh | OPTIONAL: compile-check the library inside the real 6.18.60 transport module (module-only; borrows the committed hcc-service src tree read-only) |

**Read-first inputs (committed):** ../wifi-ete-frames/REPORT.md (header/tag/length, D2H ready
frames, carrier), ../wifi-h2d-catalog/REPORT.md (firmware H2D receive tables, payload sizes,
mailbox ids, handler probe risk), ../wifi-host-replica/REPORT.md (vendor senders, pcie_msg_send
order, D2H id set), ../wifi-hcc-re/REPORT.md (SR node, packed index, ETE registers),
../wifi-hcc-service/REPORT.md + BENCH-PROCEDURE.md (working bench post, corrected boot, CECE
policy), ../../phase37/sr-message-catalog.md (48 vendor H2D builders),
../../phase38/announce-body-bytes.md (byte-exact first post-ready frame), and the ledger entries
2026-11-24 10:40 (GP8 golden boot), 11:10/11:40 (GP9/GP10), 14:00 (D2H ready pair received),
15:10 (first H2D frame consumed by the device engine), 16:10 (PD patch reach).

**Evidence markers:** **[V]** verified from a named committed artifact/decode; **[V*]**
device-measured by an earlier attended lane and cited; **[I]** inference; **[U]** unknown.

**Conventions (project-wide):** firmware CPU address -> host BAR0 = CPU + 0x6B8000; device IO CA
-> host BAR0 = 0x3B8000 + (CA - 0x40000000); image file offset -> CPU address = file + 0x40000;
.ko offsets are section-relative. The exact vector at the end of section 4.2 is the same one
phase20/tx-path.md A.5 and phase38 publish.

---

## 0. TL;DR - the ten answers

1. **H2D is two layers, and only one carries bytes.** Layer A is the mailbox: out[0]
   (CA 0x40039010) is a pending **bitmap**, no length/body; the host may post only ids **1**
   (PM sleep request) and **3** (SR announce after every ring post), id **5** is the firmware
   BUG() trap and every other id is either unregistered or D2H-service-only. Layer B is the
   HCC frame: 12-byte header + payload <= 0x674 carried by the ETE SR ring, announced by mailbox
   id 3. See sections 2 and 3.
2. **The firmware has H2D receive tables only for chip groups 0 and 2** (40+1 and 19 slots);
   there is **no group-1 and no group-4 H2D table** - group 4 is the device->host ready pair.
   A validator that admits group 4 H2D lets nothing be dispatched (section 4.1).
3. **The vendor sends nothing between the 0x5a5a release and DEVICE READY**; its first H2D
   frame is HCC **group 0, id 1** (hdpp_config_send_event, the TX-queue-table address sync at
   hmac_txq_table_addr_sync), sent after both ready waits. The frame layout is exact (section
   4.2): total = 0x1c + len, payload = token[8] | u16 cmd | u16 len | data[len] | pad[4], header
   [1] = (dst<<4)|0x01, ids 1/2, tag 0x5a5a, length <= 0x680. The **first post-ready frame of a
   live vendor boot was captured byte-exact** (phase20 A.5 / phase38): 72-byte post buffer,
   declared total 0x30, cmd 0x00d8, len 0x14, data {01 00 00 00, 0...}; the exact builder here
   reproduces it bit-for-bit, including the 0xff uninitialised tail.
4. **The other byte-exact frames** reproduced by builders here: the payload-free group-0 id-0
   stub 00 01 00 00 0c 00 00 00 00 00 5a 5a (the only safe bare probe), the steady-state group-0
   id-0x1d hmac_edca_opt_timeout_fn 72-byte all-zero-payload frame (live capture message A), and
   the 12-byte group-0 id-9 hmac_del_user_notify_dmac message whose first fields are decoded (u16
   user id; 0x0fff selects the 4-byte address at +2). See 4.2.
5. **Everything else is catalogue-only, not synthesizable.** The vendor payload structs
   (user-add 0xa8, scan request 0x1fc, EDCA 0x74, ...) live in the vendor modules and are not
   recoverable from the firmware image; 18 of the 48 vendor senders hit firmware handlers with
   word accesses that may be pointer dereferences. The library therefore refuses synthetic
   payloads by policy; payload-carrying frames must come from an exact builder or a captured
   vendor buffer. See 4.4 and 6.
6. **The carrier contract for H2D is exact** (section 5): SR0-2 at ETE block offsets
   0x400/0x450/0x4a0, host-owned group-A fields base(+0x10) = node-array device VA,
   depth-1(+0x14) = 0x1f, producer(+0x18); node = 8 B {buf dev VA, (total<<16)|0x6D2B}; packed
   producer (idx & 0x3ff) | (phase<<10) with phase bit 0x400 toggling when the low field would
   reach depth; announce mailbox id 3 (out[0] = 1<<3, then out[2] |= 1), and **never** post when
   out[0] != 0 (vendor coalescing) and never write CA 0x400392F0 (out[5]) or the device-authored
   send words.
7. **The bench already took the carrier to the wire once** (ledger 2026-11-24 15:10): a valid
   12-byte frame with correct tag/length was posted on SR0, the device **consumed the node**
   (SR0 prod=1 consumer=1, send word 0x00010001 = target/progress 1), but out[0] bit3 stayed set
   and out[5] read 0 - the firmware mailbox/H2D dispatcher never ran because the image IRQ/event
   layer is still masked (the same firmware/ROM-internal gap that the fw-postboot, irq-unmask and
   ROM lanes isolated). No frame content can close that gap; the H2D stop rule stands (5.4, 7).
8. **The code is compile-ready in both worlds:** 233/233 selftest checks pass on the host with
   -std=c99 -Wall -Wextra -Werror -pedantic; the same h2d_frames.o compiles clean inside the real
   6.18.60 transport module (hi5622_pciet_hcc.ko + h2d_frames.o, zero warnings under
   CONFIG_WERROR, 29 h2d_* symbols). See 6 and BUILD-NOTES.md.
9. **The bench procedure is one frame, one idea, no retry** (section 7): only after the ready
   pair is observed, post the exact vendor first post-ready g0 id1 frame once (integrated-library
   path or the existing hcc_sr_announce payload path), then read only: SR consumer/out[0]/out[5],
   rd_catalog/rd_status, mask/state. Hard stop rules unchanged (no out[5] write, no device RAM
   write, no mailbox id 5, no group-4 H2D, no CECE during boot).
10. **Open items are stated, not papered over:** the group-2/group-0 table-selection anomaly
    (H1 destination-nibble vs H2 group-nibble) changes which table a group-2 frame reaches; the
    first-post cmd=0x00d8 value has no producer in the analysed wifi.ko build (likely a
    different shipped build); the vendor live node for that frame declared 72 bytes while the
    header declares 48 (inherited buffer length; this lane keeps the header-consistent rule node
    length = frame total). See section 8.

---

## 1. Method, inputs, and why a catalogue is a deliverable

Static reversal is complete enough that the **header and the carrier are a closed protocol**,
while **most payload structs are not**. That asymmetry is why this lane ships code: a host that
knows the header/carrier contract but guesses a payload struct can fault the firmware (several
handlers take payload words as pointers - wifi-h2d-catalog 5.2 and Appendix B). The catalogue
therefore has three confidence tiers, and the validator enforces them:

| tier | what it is | who may send it |
| --- | --- | --- |
| **exact** | header + payload layout both decoded from vendor instructions or a live byte-exact capture | exact builders and captures in this lane |
| **catalogue** | (group,id) + payload size/class known, payload fields not decoded | only as a captured vendor buffer |
| **refused** | no firmware handler, or a NULL slot, or the lane forbidden ids | never |

Inputs (local evidence, never copied here): vendor hi5622v100_plat.ko / hi5622v100_wifi.ko
2.5.24 and 2.4.15, FIRMWARE.bin (928,920 B) - all as decoded/annotated in the committed lane
reports cited at the top. The generated catalogue is built by parsing the **committed**
wifi-h2d-catalog tables, so it can be regenerated and diffed (tools/gen_catalogue.py --check).

---

## 2. The two H2D layers

```
 layer B:  HCC frame                                  layer A:  mailbox
 +--------------------------------------+             +---------------------------------+
 | 12-byte header + payload <= 0x674    |             | out[0] pending bitmap (id 0..9) |
 | group = hdr[0]&0xf | id = u16 hdr[6] |             | out[2] bit0 = doorbell          |
 +------------------+-------------------+             | device clears out[0] = ack      |
                    |                                 +----------------+----------------+
     ETE SR ring    v                                                  ^
 SR0-2 -> node {devva,(len<<16)|0x6D2B} -- announce id 3 ---------------+
     host producer +0x18, device consumer +0x1c      out[5] = read-only device ack
```

* The mailbox message is **only a wake-up bit**: the firmware dispatcher (file 0x818AC) reads
  out[0] as a bitmap, clears it, writes 8 to the doorbell re-arm word, then calls
  table[id].fn(arg); no length, no body (wifi-h2d-catalog 1 and 3 [V]).
* The SR announce is always mailbox **id 3** (shuangta_ete_sr_dscr_fill -> pcie_msg_send(chip,3)
  [V], wifi-ete-frames 1.1). The frame body is what the firmware dispatch tables consume.

### 2.1 Mailbox catalogue (layer A) - what a host may post

| id | vendor sender / meaning | host post? | evidence |
| ---: | --- | --- | --- |
| 0 | (unregistered) | **no** | dispatcher table has no id 0 [V] |
| **1** | wlan_pm_sleep_work -> bal_msg_send(0,1): host-initiated PM sleep request; waits 50 jiffies (0.5 s at HZ=100) for the sleep-ack completion, then restores/aborts | **yes, PM path only** | host-replica 3.1 [V] |
| 2 | D2H exception/panic handler (registered by exception_mgmt_res_init) | no (D2H service) | host-replica 4.3 [V] |
| **3** | shuangta_ete_sr_dscr_fill -> pcie_msg_send(chip,3): SR-ring announce, one per committed frame batch | **yes, per frame** | ete-frames 1.1 [V] |
| 4 | D2H PM sleep-disallow callback | no (D2H service) | host-replica 4.3 [V] |
| **5** | vendor only on crash/ETE-RX-error paths; **device handler is BUG()** (movs r3,#0; str r3,[r3]; udf #0xff) | **NEVER** | h2d-catalog 3 [V] |
| 6/7 | D2H trigger-ETE-sending (wake service thread); pcie_msg_send accepts them but the vendor never sends them H2D | no (post) / service only | host-replica 4.3 [V] |
| 8/9/10 | unregistered | no | host-replica 4.3 [V] |

Encoded in code: h2d_mailbox_plan() allows only ids 1/3, refuses 5 with H2D_EPOLICY, refuses
non-vendor posts, refuses > 9, and refuses when out[0] != 0 (H2D_ESTATE: the previous word has
not been consumed - one frame, one idea). The announce plan is byte-exact: pending_write = 1<<id,
doorbell_write = out[2] | 1 (read-modify-write, never a blind store).

> **A safety nuance this lane had to correct.** wifi-h2d-inject refused *HCC message id 5* as
> well, conflating it with the mailbox id-5 trap. But HCC group 2, id 5 is a real vendor message
> (hmac_handle_connect_rsp_ap, payload 0x10) with a real firmware handler (CA 0xB9781). The
> validator here keeps the conservative lane default - HCC id 5 is refused **unless**
> H2D_F_ALLOW_ID5_HCC is passed explicitly - and documents that a future driver-replica lane may
> lift it once that payload struct exists (this file never synthesizes it).

---

## 3. The 12-byte HCC header (layer B)

Little-endian, payload at +0x0c (wifi-ete-frames 1.3, phase37/THE-PROTOCOL-MAP 2.2, phase38 3):

| off | width | field | vendor writer / value | notes |
| ---: | ---: | --- | --- | --- |
| +0x00 | u8 | group = low nibble (hdr[0]&0xf) | builder | 0 = chip, 2 = core; high nibble unused |
| +0x01 | u8 | low nibble = **alloc state = 1**; high nibble = **dst/resource-group** | hcc_msg_alloc sets 1; builder sets high nibble; hcc_msg_tx copies it to +0x09 | validator: alloc == 1 on H2D |
| +0x02 | u8 | per-message field A (hdpp: descriptor [+6]; others vap) | builder | |
| +0x03 | u8 | per-message field B (hdpp: descriptor [+1]; chip/vap id) | builder | |
| +0x04 | u16 | **total length = payload + 12** | hcc_msg_alloc (add r5,r0,#0xc) | firmware rejects 0 (wifi-ete-frames 3.2) |
| +0x06 | u16 | **message id** (dispatch key in the group) | builder | |
| +0x08 | u8 | per-message field C (wal: sequence counter) | builder | |
| +0x09 | u8 | **dst = hdr[1] >> 4** | hcc_msg_tx (ubfx #4,#4) | validator enforces equality |
| +0x0a | u16 | **transport tag 0x5a5a** | pcie_ete_tx_queue_handle stamps it on the true TX path; not written by either module | the port must emit it itself [V] |
| +0x0c | ... | payload | builder | max 0x674, total 0x680 |

**Firmware-side validation of an H2D frame** (wifi-ete-frames 3.2 [V]):

1. tag at +0xa must read 0x5a5a (file 0x8699E) - otherwise the SR error counter increments and
   the frame is dropped;
2. declared total at +0x4 must be non-zero (file 0x869B0);
3. the message header fetch (CA 0x409EA) also requires the header class field
   hdr[5] >> 1 & 7 <= 3 and message-pool membership; with the 0x680 bound the class field is
   automatically satisfied (the validator still checks it structurally);
4. dispatch on hdr[0]&0xf and u16 hdr[6] selects the handler in the group table.

The historical invalid bench frame - a 64-byte zero header - fails (1) and (2); the library has
a regression test for exactly that buffer (import returns H2D_ELEN). The working bench post of
2026-11-24 15:10 satisfied (1)(2) with the group-0 id-0 stub frame.

---

## 4. The H2D frame catalogue

### 4.1 Direction rule and the firmware receive tables

Registered by the firmware hcc_msg_register at CA 0x102866 (wifi-h2d-catalog 4 [V]):

| group | table CA | slots | ids |
| ---: | ---: | ---: | --- |
| **2** | 0x10CC18 | 19 | 2,3,4,5,6,9,a,b,c,e,1a,1b,1f,23,25,26,27,28,2a (c NULL) |
| **0** | 0x10CCFC | 40 | 0,1,2,3,6,7,8,9,a,b,c,d,e,f,10,11,12,13,15,16,17,18,19,1c,1d,1e,20,22,23,24,27,28,29,2b,2c,2d,2e,30,31 (14 NULL) |
| **0** early | 0x10CBEC | 1 | 0x2f |

**No group-1 and no group-4 H2D table exists**, and the mailbox dispatcher is separate (table at
CA 0x118D68, ids 1/3/5/6). Consequence encoded in h2d_check_semantics(): H2D frames must have
group 0 or 2 (H2D_EGROUP otherwise; group 4 is called out as device->host only), the (group,id)
must have a **non-NULL** handler (H2D_ENOHANDLER), and the lane policy applies on top of that.

The id-to-handler map, per-handler payload-access risk and the 48 vendor senders are generated
into catalogue.md and src/h2d_catalogue.inc. The ids 0..7 matrix the task asked about:

| id | group 0 | group 2 | lane rule |
| ---: | --- | --- | --- |
| 0 | handler (stub, payload-free) | - | **safe bare probe** |
| 1 | handler (config event) | (not in the g2 table; the vendor sender wal_acs_netlink_recv is a g2/1 anomaly) | exact builder exists - the vendor first post-ready is g0/1 |
| 2 | handler (alg config event) | handler | exact builder exists for g0/2 |
| 3 | handler (sdt recv reg) | handler | catalogue only (dyn payload) |
| 4 | **absent** | handler | nothing to send in g0; g2 needs a capture |
| 5 | **absent** | handler | **lane-refused** (conservative, see 2.1); mailbox id 5 is the BUG trap |
| 6 | handler | handler | catalogue only |
| 7 | handler | absent | fixed 0xa8 payload, struct not decoded |

Also note the 3 vendor senders with **no firmware handler** (g2 id 0x14 CAC start and finish,
g2 id 0x21 tx-complete-read-error): if the vendor working system really sends them, their
handlers sit in a map not found by the registrar sweep (wifi-h2d-catalog 4.5/7.4). The
catalogue marks them NO_HANDLER and the validator refuses them.

**Anomaly caveat (H1/H2).** wifi-h2d-catalog 4.5 found that 15 of the 27 vendor group-2 senders
have their handler registered in the **group-0** table. Two hypotheses remain: H1 the firmware
keys the table on the destination/resource nibble (hdr[9], copied from hdr[1]>>4) rather than on
hdr[0]&0xf; H2 the tables are group-keyed and those 15 messages are dead in this build. The
validator uses the documented group-keyed tables (the conservative reading); a payload-free bench
discriminator is specified in wifi-h2d-catalog 6.3. Builders always write the vendor group nibble
and the dst nibble in both places, so H1/H2 changes dispatch, not the bytes.

### 4.2 The exact builders (structured input -> exact frame)

All four builders set H2D_F_EXACT, which lets the policy validator accept their payload while
still refusing synthesized payloads to pointer-reading handlers.

#### (a) h2d_build_safe_probe(dst) - group 0, id 0

The firmware group-0 id-0 handler (CA 0x81695) logs and returns without touching the payload
(wifi-h2d-catalog 4.3/6.2). Wire form (the validated default probe frame):

```
group=0 id=0 dst=0 total=0x0c tag=0x5a5a
00 01 00 00 0c 00 00 00 00 00 5a 5a
```

Use it only as a liveness probe; it carries no state.

#### (b) h2d_build_g0_cfg_event(...) - the shared config-event shape (group 0, ids 1/2)

The vendor helper at wifi.ko 0x5714C (used by hmac_config_send_event id 1 and
hmac_config_alg_send_event id 2) allocates len + 0x10 payload bytes and writes key at +8, len at
+0xA, data at +0xC; hdpp_config_send_event has the same shape with a command at +8 (full
instruction evidence: phase38 3 - note the memcpy destination is hdr+0x18 = payload +0xC,
correcting wifi-ete-frames 1.2). Payload layout:

```
payload +0x00..0x07  token[8]      (NOT written by the vendor builder; inherited buffer bytes)
payload +0x08..0x09  u16 word8     (hdpp: cmd; hmac: key)
payload +0x0A..0x0B  u16 len       (data length)
payload +0x0C..       data[len]
payload +0x0C+len..  pad[4]        (NOT written by the builder)
header: group 0, id 1|2, dst/fa/fb/fc caller, total = 0x1C + len
```

Byte-exact vectors are in the selftest. h2d_build_g0_id1_hdpp_txq_sync(...) is the id-1
specialization.

#### (c) The first post-ready vendor frame, byte-exact

**This is the vendor first H2D send in a normal boot**: hmac_main_init -> hmac_board_init ->
hdpp_chip_sync_add -> hmac_txq_table_init -> hmac_txq_table_addr_sync -> hdpp_config_send_event,
i.e. the TX-queue-table address sync, group 0 id 1, sent only **after** both ready waits
(wifi-ete-frames 1.2/2.4 [V chain]). The live vendor first SR post was captured byte-exact
(phase20/tx-path.md A.5, re-derived in phase38 message B):

```
header : 00 01 00 04  30 00 01 00  00 00 5a 5a     group 0, alloc=1, dst=0,
                                                    fa=0, fb=4, total=0x30, id=1,
                                                    fc=0, dst=0, tag=0x5a5a
payload: 00 00 00 00 01 00 00 00   token (u32 0, u32 1; inherited)
         d8 00                      u16 word8 = 0x00d8 (cmd)
         14 00                      u16 len = 0x14 (20)
         01 00 00 00 + 16 x 00      data (20 B; the vendor sync payload struct)
         ff ff ff ff                pad (inherited)
buffer : 24 x ff                  (past the declared total; the ring buffer was 72 B,
                                   node word1 declared 72 while the header declares 48)
```

The builder call for the exact replay:

```c
h2d_build_g0_id1_hdpp_txq_sync(&f, /*dst*/0, /*fa*/0, /*fb*/4,
                               token = {0,0,0,0,1,0,0,0},
                               cmd = 0x00d8,
                               data = {01,00,00,00, 0...0} (20 B), len = 0x14,
                               tail = {ff,ff,ff,ff});
h2d_frame_export(&f, buf, cap, /*pad_to*/72, /*pad_byte*/0xff, &n, &err);  /* == capture */
```

The selftest asserts the 72-byte buffer byte-for-byte. Two honest caveats: cmd=0x00d8 has no
producing call site in the analysed wifi.ko teardown build (likely a different shipped build -
phase38 5/8), and the live node length (72) disagrees with the header total (48) in the vendor
own traffic. **Rule encoded here:** the node length is the frame total (the vendor transport
itself takes the length from hdr[4..5] [V]); the 72-byte buffer is reproduced only to show the
capture is understood, and h2d_plan_post() always emits (frame_total << 16) | 0x6D2B.

#### (d) h2d_build_g0_id1d_edca_opt(dst, fa, fb, fc) - group 0, id 0x1d

hmac_edca_opt_timeout_fn (wifi.ko 0xef644): payload 0x3c bytes, all zeros (source struct zeroed
before a 60-byte memcpy), total 0x48, header 00 01 00 04 48 00 1d 00 00 00 5a 5a + zeros - the
vendor live steady-state message A (phase38 6 [V], live capture in phase25). Not on the boot path;
useful as a second real message and as a capture-fidelity check.

#### (e) h2d_build_g0_id09_del_user(dst, user_id, addr) - group 0, id 9

hmac_del_user_notify_dmac: 12-byte payload, u16 user id at +0; when user_id == 0x0fff the handler
(CA 0x574F9) reads the 4 address bytes at +2..+5 (wifi-h2d-catalog 5.2 [V offsets]). Bytes:
00 01 00 00 18 00 09 00 00 00 5a 5a + ff 0f <addr4> 00 00 00 00 00 00. This is the one
state-changing message whose first fields are decoded; it is still gated behind the vendor user
table, so it is catalogued as exact-layout but **not** recommended as an early bench message.
### 4.3 Vendor sender catalogue (all 48) - what exists, what is modelled

catalogue.md lists every vendor H2D sender joined to its firmware handler and probe-risk class.
Summary of the risk classes over the 60 firmware receive slots:

| class | count | meaning | sender policy |
| --- | ---: | --- | --- |
| NO_HANDLER | 2 NULL slots + 3 sender ids with no slot | nothing dispatches | refused |
| NO_ACCESS | no early payload access found | payload-free-effective | payload-free allowed; payload needs exact/captured |
| BYTE_READ | fixed byte/halfword reads | low risk, still struct-dependent | exact/captured only |
| PTR_DEREF | word reads that may be dereferenced | **can fault the firmware with synthetic data** | exact/captured only |

18 of the 48 sender rows hit PTR_DEREF handlers (conservative reading; some word loads are false
positives of the heuristic - the point is the sender policy, not a fault oracle). PM/state-relevant
senders in the catalogue: hmac_mgmt_reset_psm (g0/0x0e, dyn payload),
hmac_sta_up_update_edca_params_machw (g0/0x12, 0x74), hmac_sta_up_update_mu_edca_params_machw
(g0/0x13, dyn), hmac_chan_restart_network_after_switch (g0/0x19, dyn), hmac_mgmt_tx_event_status
(g2/0x0d, dyn), hcc_timer_timeout_proc (g2/0x1d, dyn), hmac_sdt_recv_reg_cmd (g0/3, dyn) - all
**catalogue-only**, none synthesizable from the firmware image alone.

### 4.4 The payload-struct wall, stated plainly

The 0xa8-byte user-add records, the 0x1fc-byte scan request, the 0x74-byte EDCA params and most
group-2 payloads are memcpy_s of live producer structs inside the vendor modules. They are not
recoverable from the firmware image, and a synthetic guess can make a firmware handler
dereference a host-controlled word (wifi-h2d-catalog 5.2 and Appendix B). That is why the
deliverable is "exact builders + catalogue + refusal policy", not 48 builders. The driver-replica
lane gets the remaining structs from the vendor module disassembly, one message at a time, and
each addition becomes an exact builder with its own known-answer vector here.

---

## 5. The carrier: SR node, packed producer, announce (H2D side)

### 5.1 Registers (ETE block CA 0x4003A000)

| channel | block offset | host ring fields (program group A) |
| --- | ---: | --- |
| SR0 | 0x400 | +0x10 base = node-array device VA, +0x14 depth-1 = 0x1f, +0x18 producer (host writes), +0x1c consumer (device writes) |
| SR1 | 0x450 | same |
| SR2 | 0x4a0 | same |

Channel enable +0x00 is **firmware/device-owned** and must not be host-written; +0x08 ctrl is the
pcie_ete_sr_reg_init write (vendor writes cfg[5]=1 before the ring is live) - the working bench
module armed the vendor-faithful fields through its hcc_arm; this lane models no device write.
The device-authored send word for SR is CA 0x4003AC18 + 4*i, encoded hi16 = target packed index,
lo16 = progress (the bench read 0x00010001 after one frame [V*]); the host never writes it.

### 5.2 Node encoding

8 bytes, little-endian: word0 = buffer device VA (host DRAM in the RC inbound window
0x80000000..0x8fffffff), word1 = (frame_total << 16) | 0x6D2B (shuangta_ete_sr_dscr_fill; flags =
0xD2B|0x2000|0x4000) [V]. Example: buffer dev VA 0x84543000, 48-byte frame ->
00 30 54 84 2b 6d 30 00.

### 5.3 Producer arithmetic and the post order

Packed index = (index_low10) | (phase << 10), phase bit 0x400 (pcie_ete_ring_ptr_plus [V]).
Advance: next = idx + 1; if (next & 0x3ff) == depth then clear the low field and toggle the phase
bit. With the vendor depth 32: 0 -> 1, 31 -> 0x400, 0x400 -> 0x401, 0x41f -> 0. 32 posted
entries publish 0x400 (the vendor pre-post value).

Posting order (one frame, modelled by h2d_plan_post()):

1. frame validates (transport + firmware + handler + policy);
2. out[0] (CA 0x40039010) must read 0 - otherwise the previous announce is unconsumed; **stop**;
3. copy the frame bytes into SR0 buffer idx = producer & 0x3ff;
4. write node {buf_devva, (frame_total<<16)|0x6D2B}, barrier;
5. write the packed producer to SR+0x18;
6. announce mailbox id 3: out[0] |= 1<<3 (write the bitmap), then out[2] |= 1 (doorbell RMW);
7. bounded drain poll of out[0] bit 3 (vendor style; the bench module used <= 100 ms);
8. read-only checkpoints: out[5] (CA 0x400392F0, never write), SR0 consumer +0x1c, send word
   CA 0x4003AC18.

The vendor pcie_msg_send only writes out[0] when its coalesced pending/in-flight shadow is empty
(pending/inflight in the chip context) - so step 2 is a hard protocol precondition, not
belt-and-braces.

### 5.4 What the bench measured (2026-11-24 15:10 [V*])

The working module posted exactly the safe stub frame (group 0, id 0, dst 0, total 12, tag 0x5a5a)
on SR0 with the node of 5.2. Result:

| observable | value | reading |
| --- | --- | --- |
| SR0 producer/consumer | 1 / 1 | the device ETE engine **consumed the node** - the SR path works |
| send word CA 0x4003AC18 | 0x00010001 | device-side target/progress = 1 (packed index 1) - matches the packed arithmetic |
| out[0] after <=100 ms | bit3 still set | the firmware mailbox/H2D dispatcher never ran (event 0x4C never raised) |
| out[2] / out[5] | 0 / 0 | no device ack; out[5] is read-only in our code |

This is the same firmware/ROM-internal IRQ/event-delivery gap isolated by the fw-postboot,
irq-unmask and ROM lanes: the image dispatcher/EOI never run, so no host message can be
"received". The H2D carrier is proven to the device ring; the consumer of the mailbox is not
alive. The lane stops there (one frame, one idea, no retry).

---

## 6. Implementation, validation matrix, and build evidence

### 6.1 API surface (src/h2d_frames.h)

| group | functions |
| --- | --- |
| header/frame | h2d_frame_encode, h2d_frame_decode, h2d_frame_import (captured post buffer; declared total may be < buffer), h2d_frame_export (exact replay with vendor pad byte) |
| exact builders | h2d_build_safe_probe, h2d_build_g0_cfg_event, h2d_build_g0_id1_hdpp_txq_sync, h2d_build_g0_id1d_edca_opt, h2d_build_g0_id09_del_user, h2d_build_raw |
| carrier | h2d_sr_node_encode/decode, h2d_idx_plus/count, h2d_sr_plan, h2d_mailbox_plan, h2d_plan_post |
| D2H validator | h2d_ready_decode (group-4 id1/id2, payload cap 0x63, core, payload dword) |
| catalogue | h2d_vendor_senders_get, h2d_fw_handlers_get, h2d_fw_handler_lookup |

The library is pure C (memcpy/memset only, __KERNEL__ switches linux/types.h for stdint.h); the
data tables are generated from the committed lane reports.

### 6.2 Validation matrix (each rule has a selftest case)

| rule | error | source |
| --- | --- | --- |
| short frame (<12 B) | H2D_ESHORT | header model |
| tag != 0x5a5a | H2D_ETAG | firmware file 0x8699E [V] |
| declared total 0 / != buffer / < 12 / class > 3 | H2D_ELEN | firmware file 0x869B0; header fetch [V] |
| total > 0x680, payload > 0x674 | H2D_EBOUNDS | hcc_msg_alloc bound [V] |
| alloc nibble != 1 (H2D) | H2D_EALLOC | hcc_msg_alloc [V] |
| hdr[9] != hdr[1] >> 4 | H2D_EDST | hcc_msg_tx [V] |
| group not 0/2 (group 4 = D2H only) | H2D_EGROUP | firmware receive tables [V] |
| (group,id) missing or NULL handler | H2D_ENOHANDLER | wifi-h2d-catalog 4 [V] |
| HCC id 5 without explicit override | H2D_EPOLICY | lane safety policy |
| synthetic payload to BYTE_READ/PTR_DEREF handler | H2D_EPOLICY | wifi-h2d-catalog Appendix B [V] |
| payload size != fixed vendor size | H2D_EARG | phase37 catalogue sizes [V] |
| mailbox id != 1/3, id 5, unregistered | H2D_EPOLICY | host-replica 3.1/4.3 [V] |
| out[0] != 0 before a post | H2D_ESTATE | pcie_msg_send coalescing [V] |
| node flags != 0x6D2B / len out of range | H2D_EWIRE / H2D_EBOUNDS | shuangta_ete_sr_dscr_fill [V] |
| D2H ready payload > 0x63 / wrong group / bad id | H2D_ELEN / H2D_EGROUP / H2D_EID | host_ready_msg_process [V] |

### 6.3 Build and test evidence (host-only)

```
./tools/build.sh
  gen_catalogue: OK (committed files current)
  cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow
     -Wstrict-prototypes -Wmissing-prototypes src/h2d_selftest.c src/h2d_frames.c
  SUMMARY: 233 passed, 0 failed
  BUILD OK (host-only; no device contact)
```

Kernel-context check (BUILD-NOTES.md has the full evidence): with the committed
wifi-hcc-service/src tree copied to a scratch dir and h2d_frames.o added to the module object
list, the real 6.18.60 transport module builds clean under CONFIG_WERROR with 29 h2d_* symbols
(module 233,768 B; vermagic 6.18.60 SMP mod_unload ARMv7 p2v8). The .ko is local evidence only
and is not committed; no other lane source is modified (copy-only).

The CLI (same binary) demonstrates structured input -> exact bytes:

```
$ h2d_selftest hdpp 0 0 4 0x00d8 0100000000000000000000000000000000000000 0000000001000000 ffffffff
cfg: group=0 id=0x01 dst=0 fa=0 fb=4 fc=0 payload=36 total=48
cfg: 000100043000010000005a5a0000000001000000d80014000100000000000000000000000000000000000000ffffffff
$ h2d_selftest post 000100000c00000000005a5a 0 32 0x84543000 0 0
post: frame total=12 node[idx=0]={0x84543000,0x000c6d2b} producer 0x0->0x1; mailbox id=3 out[0]=0x8 out[2]=0x0|1
```
---

## 7. Bench procedure: the first real post-ready message

**This section is for an attended session only.** Preconditions are the existing gates (recovery
door armed/READY, bench link PASS, UART capture read-only, image + module hashes recorded, no
vendor Wi-Fi modules, one driver bound, post-release never rmmod). Read it with
../wifi-hcc-service/BENCH-PROCEDURE.md and the B3 plan; nothing here adds a new register class.
**No frame is sent before the handshake page +8 == 2 and both ready frames (g4 id1, g4 id2) have
been observed.**

### 7.1 The message

The first real post-ready message is the vendor own first H2D send:

```
00 01 00 04 30 00 01 00 00 00 5a 5a      group 0 id 1 dst 0 fa 0 fb 4 total 0x30
00 00 00 00 01 00 00 00                  token
d8 00 14 00                              cmd 0x00d8, len 0x14
01 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00                  data (20 B)
ff ff ff ff                              pad (as captured)
```

48-byte frame hex (this is what a store attribute takes):

```
000100043000010000005a5a0000000001000000d80014000100000000000000000000000000000000000000ffffffff
```

### 7.2 Path A - with the library integrated (exact, recommended)

The module calls h2d_plan_post() and writes the plan fields; the kernel-context check proves the
object compiles into hi5622_pciet_hcc.ko (BUILD-NOTES.md has the two-line Kbuild addition and a
store-function sketch). Steps:

1. Bring-up as today: prepare -> upload -> verify (diffs=0) -> custom blocks -> hcc_arm ->
   hcc_loop -> release (0x5a5a ONLY, no CECE during boot).
2. Confirm the firmware state **read-only**: handshake page 0x86807fef/7/2/0xabcd, state
   CA 0x118B60 = 1, mask 0x40008 = 0; rd_catalog shows g4 id1 then id2 (payload dword
   0x01060850); record out[0] (must be 0), out[1], out[5] (read-only).
3. Build the frame with h2d_build_g0_id1_hdpp_txq_sync (exact vector of 7.1) and call
   h2d_plan_post(&f, producer = live SR0 producer, depth = 32, buffer_devva = live node word0,
   out0_pending = live out[0], out2_doorbell = live out[2]). If the plan refuses (H2D_ESTATE),
   **do not post** - the previous announce is unconsumed.
4. Post exactly once; capture the plan read-backs (producer before/after, out[0] before/after,
   out[2] before/after) and then only read: out[5] (read-only), SR0 consumer, rd_status,
   rd_catalog, mask/state, hcc_events.

**Expected outcomes** (decision matrix, same idea as the h2d-inject session A):

| observation | interpretation | next step |
| --- | --- | --- |
| SR0 consumer advances, out[0] bit3 clears, out[5] reads 1 | mailbox + H2D dispatch path ran: the first real post-ready message was accepted | capture everything; next session plan the driver-replica message order (wifi-h2d-catalog 6.2 stage 2) |
| SR0 consumer advances, out[0] bit3 stuck, out[5] 0 | carrier consumed but the mailbox consumer is dead (current firmware state: event 0x4C not delivered) | **stop; H2D lane closed for this boot** - no retry, no payload experiments; this is the firmware/ROM IRQ-delivery gap |
| plan refused (H2D_ESTATE/H2D_EPOLICY) / out[0] != 0 | previous announce unconsumed or policy violation | stop, record, do not clear out[0] speculatively |
| abort/SError/mask change/new D2H ids | the frame produced device-side work (or a fault) | stop, capture, power-cycle (never rmmod after release) |

### 7.3 Path B - without rebuilding (payload-only via the existing module)

If the session boots the current working hi5622_pciet_hcc.ko (hcc-service + direct-read rd_*
patch) and the library is not yet integrated, the existing hcc_sr_announce store can post the
same **payload** (it rebuilds a valid header from group/id/dst and forces fa=fb=fc=0):

```
printf 'group=0 id=1 dst=0 payload=0000000001000000d80014000100000000000000000000000000000000000000ffffffff' \
    > /sys/bus/pci/devices/0000:00:00.0/hcc_sr_announce
```

(The printf form matters: the store parser in that module rejects a trailing newline; the ledger
2026-11-24 15:10 records the echo-vs-printf trap.)

Deviation to record: the header field B (fb, the vendor chip/vap id) is 0 instead of 4, and the
tool applies its own tag/length/id-5 refusals but not this lane payload-struct policy. Treat it as
a path-liveness test, not the exact vendor frame; do not follow it with a second write.

### 7.4 Hard stop rules (unchanged, non-negotiable)

* **one frame, one idea** per session; **no retry** after a timeout (a second attempt is a new
  idea, a new session, a Lead decision);
* **never write CA 0x400392F0** (out[5], firmware-owned ack) - read-only checkpoint only;
* **never post mailbox id 5**; **never send group-4 H2D**; no payload > 0x674;
* no CECE write during the ROM boot stage; CECE only post-handshake and only on spin evidence
  (the hcc-service policy);
* no device RAM writes, no out-of-window DMA, never read the RC misc window 0x10161000; UART is
  read-only; any abort/SError/hang -> capture and power-cycle (recovery door armed);
* if out[0] is stuck: record, power-cycle, close the lane - do not clear it speculatively.

---

## 8. Limits and open items

1. **Payload structs beyond the decoded fields are not reconstructible from the firmware image**
   (wifi-h2d-catalog 5.2/7.3). 45 of the 48 vendor senders are catalogue-only here. The
   driver-replica lane must decode each struct from the vendor modules and add an exact builder +
   known-answer vector.
2. **Group-2 table selection (H1/H2)** remains unresolved (wifi-h2d-catalog 4.5): the validator
   uses the documented group-keyed tables; a payload-free bench discriminator decides it (6.3
   there). If H1 holds, a group-2 frame with dst=0 legitimately lands in the group-0 table - bytes
   unchanged, dispatch changed.
3. **cmd=0x00d8 provenance** in the first post-ready frame is open (phase38 5): the analysed
   wifi.ko build has no call site passing it; a different shipped build is the leading hypothesis.
   The capture is authoritative for the bytes; the builder treats cmd as caller input.
4. **Node length for the first post**: the live vendor node declared 72 while the header declares
   48; this lane keeps the header-consistent rule (node length = frame total) because the vendor
   transport itself takes the transmitted length from the header. If a bench capture disagrees,
   the replay helper can still pad the post buffer to 72 with h2d_frame_export.
5. **No device contact**: the 15:10 bench data is cited, not re-measured. The procedure above is
   ready for the Lead attended session.
6. **HCC id-5 policy** is deliberately stricter than the protocol (group-2 id 5 is a real vendor
   message); it is a lane default with an explicit override flag, to be lifted when that struct is
   decoded.

---

## 9. Evidence index and reproduction

| claim | committed evidence |
| --- | --- |
| 12-byte header fields, tag/length checks, no H2D in the release->ready window | wifi-ete-frames 1.3/2/3.2 |
| first post-ready sender = g0 id1 hdpp_config_send_event | wifi-ete-frames 1.2; wifi-hcc-link 0; chain re-derived |
| exact first post-ready bytes / cmd 0xd8 / 72-byte buffer | phase38/announce-body-bytes.md 2-5; phase20/tx-path.md A.5 |
| steady-state g0 id1d frame | phase38 6 [V] |
| vendor senders x payload sizes (48) | phase37/sr-message-catalog.md 2; wifi-h2d-catalog 5.1 (join) |
| firmware receive tables + handler CAs, NULL slots | wifi-h2d-catalog 4 + Appendix A |
| per-handler payload-access risk classes | wifi-h2d-catalog Appendix B |
| mailbox ids, id-5 BUG trap, pcie_msg_send order | wifi-h2d-catalog 3; wifi-host-replica 3.1/4.2-4.3 |
| ETE channel offsets, group-A fields, node word1 | wifi-hcc-re 3.3/4; wifi-ete-frames 1.1 |
| packed producer index + send-word encoding | wifi-hcc-re 4; ledger 2026-11-24 15:10 [V*] |
| working bench post (valid frame consumed, mailbox dead) | ledger 2026-11-24 15:10; wifi-hcc-service 5.1 |
| D2H ready wire forms / payload 0x01060850 | wifi-ete-frames 2; ledger 2026-11-24 14:00 |
| library builds/tests | BUILD-NOTES.md; tools/build.sh; tools/kernel-check.sh |

Reproduction (host-only, no device):

```
cd opensource/docs/incoming/wifi-h2d-frames
python3 tools/gen_catalogue.py --check    # generated tables match the committed reports
./tools/build.sh                          # compile -Werror + 233-check selftest
./tools/kernel-check.sh                   # optional: kernel 6.18.60 module compile check
/tmp/wifi-h2d-frames-build/h2d_selftest catalogue    # dump the generated catalogue
/tmp/wifi-h2d-frames-build/h2d_selftest --help       # frame CLI
```

No vendor artifact is read by the shipped tools: gen_catalogue.py parses committed markdown; the
selftest and CLI are pure C. kernel-check.sh copies the committed hcc-service src tree (read-only)
and builds one out-of-tree module - module only, never an image/world build, never a device access.

---

## 10. Corrections carried forward

* wifi-ete-frames 1.2 says payload memcpy at hdr+0xC for hdpp_config_send_event; that is wrong.
  The phase38 full instruction trace shows cmd at hdr+0x14, len at hdr+0x16, memcpy destination
  hdr+0x18 (= payload +0xC). This lane builder and vectors use the corrected layout.
* "HCC message id 5 is forbidden" (h2d-inject) conflates the mailbox id-5 BUG() trap with the real
  group-2 id-5 vendor message. Mailbox id 5 stays hard-forbidden; HCC group-2 id 5 is refused by
  lane default with an explicit override.
* The pre-GP8 bench H2D smoke 64-byte zero header could never be valid (tag 0, len 0); the
  library has a regression test that imports exactly that buffer and gets H2D_ELEN. The working
  post must use a rebuilt header (tag 0x5a5a, total = payload+12).
* The vendor out[1] bit6 (0x40) is a D2H deposit notification, not a request for an H2D frame
  (wifi-ete-frames 0.3) - the H2D catalogue is not needed to make ready, only to drive the vendor
  protocol after ready.
