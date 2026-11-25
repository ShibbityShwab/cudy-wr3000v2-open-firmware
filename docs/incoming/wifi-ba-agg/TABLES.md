# wifi-ba-agg - evidence tables (T1-T10)

All host-module addresses are `.text` offsets of the pinned LOCAL vendor
module `hi5622v100_wifi.ko` 2.5.24 (3,581,748 B, ELF ARM, not stripped; local
artifact, never committed). Firmware CPU address (CA) = file offset + 0x40000,
Thumb-2; the pinned firmware is 928,920 B sha256
`7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`.
Reproduce with the OpenWrt ARM objdump / `.provision-venv/bin/python
tools/ba_decode_check.py`. No vendor byte, firmware byte or calibration value
is reproduced here - only derived addresses, sizes and field maps.

Evidence markers: **[V]** re-verified from the named artifact, **[I]**
inference from the decode, **[U]** unknown / parked.

## T1. Host BA symbol map [V]

| function | `.text` | size | role |
| --- | ---: | ---: | --- |
| `hmac_ba_rx_init` | 0xdb580 | 0x58 | per-RX-session pointer guard; zeroes `session+4` |
| `hmac_ba_rx_win_init` | 0xdb440 | 0x140 | allocate/init the RX reorder window (seq window + 0x1800 ring) |
| `hmac_ba_rx_add` | 0xdb6e0 | 0x208 | create one RX BA session (alloc 0x1824 window, timer, state=2) |
| `hmac_ba_rx_del` | 0xdb5d8 | 0x108 | tear down one RX BA session (timer unregister, kfree window) |
| `hmac_ba_rx_update_release_seqnum` | 0xdbbfc | 0xbc | 12-bit seq window advance/validate (seq >= 0x1000 rejected) |
| `hmac_ba_rx_buffered_data_software_check` | 0xdbcb8 | 0x308 | **the software reorder scoreboard**: gap detection + in-order release |
| `hmac_ba_rx_buffered_data_release` | 0xdc19c | 0x390 | release the buffered chain to the bridge |
| `hmac_ba_rx_data_buffer` | 0xdc6b8 | 0x2c4 | buffer one out-of-order MPDU at seq |
| `hmac_ba_rx_data_release` | 0xdc52c | 0x18c | in-order release path |
| `hmac_ba_check_rx_aggr` | 0xdbadc | 0xec | A-MPDU receive sanity (BA agreement present + delimiters) |
| `hmac_ba_flush_reorder_q` | 0xdbad0 | 0x0c | flush wrapper into the timeout flush |
| `hmac_ba_timeout_fn` | 0xdb208 | 0x170 | per-RX-session timer callback: builds host-core msg id 8 with the timer cookie |
| `hmac_ba_timeout_param_get` | 0xdc97c | 0x104 | cookie -> (vap, user) lookup (cookie: chip+6, vap+7, uid+8) |
| `hmac_ba_timeout_proc` | 0xdca80 | 0x6b4 | **reorder timeout**: flush gaps, update release seq, restart timer |
| `hmac_ba_tx_init` | 0xdb8e8 | 0x60 | zero one TX BA session (offset 0x610+tid*12) |
| `hmac_ba_tx_add` | 0xdb948 | 0x78 | set TX session +1 from caller (+1 byte) |
| `hmac_ba_tx_del` | 0xdba4c | 0x84 | clear TX session +1, clear low 7 bits of +2 |
| `hdpp_user_rx_init` | 0x1d64 | 0xdc | 8x `hmac_ba_rx_init` (`user+0x670+tid*0x80`) + RX bitmaps |
| `hdpp_tx_tid_init` | 0x13ca4 | 0x38 | 8x `hmac_ba_tx_init` (`user+0x610+tid*12`) |
| `hmac_tx_ba_setup` | 0xaf180 | 0x104 | create/start one TX BA session |
| `hmac_tx_ba_session_process` | 0x16cf0 | 0x340 | TX BA state machine timer/retry/notify |
| `hmac_tx_ba_session_incr` / `decr` | 0xaf830 / 0xaf75c | 0x90/0xd4 | pending-session counter |
| `hmac_tx_ba_notify` | 0x16b90 | 0x160 | builds g0 id **0x1c** (hdr[6]=28) |
| `hmac_create_ba_event` | 0x3ece4 | 0x1c4 | D2H msg id **2** handler: TX BA setup request from fw |
| `hmac_del_ba_event` | 0x3eea8 | 0x244 | D2H msg id **3** handler: tear down TX BA sessions + DELBA frames |
| `hmac_mgmt_rx_delba_event` | 0x49220 | 0x208 | D2H msg id **0x0f** handler: peer DELBA -> TX action frame |
| `hmac_ba_action_msg_process` | 0xaf284 | 0x278 | D2H msg id **0x1d** handler: the unified BA action state machine |
| `hmac_mgmt_rx_action_ba` | 0x460c4 | 0x1a8 | received action category-3 dispatch: 0=ADDBA req, 1=ADDBA rsp, 2=DELBA |
| `hmac_mgmt_rx_addba_req` | 0x45588 | 0x2b8 | AP-role ADDBA request handler (alloc RX session) |
| `hmac_mgmt_rx_addba_req_process` | 0x45398 | 0x1f0 | after TX ADDBA rsp: sync user context |
| `hmac_mgmt_rx_addba_rsp` | 0x45a84 | 0x2f4 | builds g0 id **0x0a** subtype **1** (ADDBA response relay) |
| `hmac_mgmt_rx_addba_rsp_verify` | 0x45888 | 0x1fc | validates the received ADDBA rsp against the TX session |
| `hmac_mgmt_rx_delba` | 0x45d78 | 0x34c | builds g0 id **0x0a** subtype **2** (DELBA relay) |
| `hmac_mgmt_tx_addba_req` | 0x44734 | 0x278 | build+TX an ADDBA request action frame |
| `hmac_mgmt_tx_addba_rsp` | 0x44a70 | 0x328 | build+TX an ADDBA response action frame |
| `hmac_mgmt_tx_delba` | 0x44f30 | 0x248 | build+TX a DELBA action frame |
| `hmac_mgmt_encap_addba_req` | 0x43e50 | 0x198 | **ADDBA request frame bytes** |
| `hmac_mgmt_encap_addba_rsp` | 0x43fe8 | 0x19c | **ADDBA response frame bytes** |
| `hmac_mgmt_encap_delba` | 0x44184 | 0x16c | **DELBA frame bytes** |
| `hmac_mgmt_tx_action` | 0x490e4 | 0x13c | category-3 dispatcher for the three TX builders |
| `hmac_mgmt_tx_ampdu_start` / `end` | 0x4626c / 0x46434 | 0x1c8/0x184 | build g0 id **0x0b** start/end (hdr[12]=0/1) |
| `hmac_mgmt_tx_priv_req` | 0x48ef0 | 0xc8 | req[0]=0 -> ampdu_start, 1 -> ampdu_end |
| `hmac_update_user_tx_addba_req` | 0x44454 | 0x218 | TX session state=1, dialog/policy, 500 ms timer, user sync item 4 |
| `hmac_set_addba_req_para_sync` | 0x44670 | 0xc0 | per-user TX ADDBA parameter sync (32-byte descriptor) |
| `hmac_always_tx_set_on` | 0xf410c | 0x6f4 | vendor "always tx" auto path (add_ba_req/rsp) |
| `hmac_always_tx_add_ba_req` / `_rsp` / `_del_ba` | 0xf3a94 / 0xf3d10 / 0xf3f98 | | auto BA frame path |
| `hmac_config_addba_req` | 0x5b6fc | 0x210 | WAL cmd 0xBA handler -> ADDBA request |
| `hmac_config_delba_req` | 0x5c4cc | 0x254 | WAL cmd 0x324 handler -> DELBA |
| `hmac_config_set_baw_size` | 0x5ac3c | 0x150 | WAL cmd 0x32C: `user+0xf44`/`+0xf46` u16 BA window sizes |
| `hmac_config_send_bar` | 0x57f98 | 0x84 | config event key 0x00D7 |
| `hmac_config_dump_ba_bitmap` | 0x57cb8 | 0x90 | config event key 0x00F1 |
| `hmac_config_set_ampdu_aggr_num` | 0x579d8 | 0x5c | config event key 0x01DB |
| `hmac_config_set/get_ampdu_tx_on` | 0x564b0/0x56e58 | 0xc0/0x58 | per-VAP `vap+0x18c -> +0x49` + vap sync |
| `hmac_config_ampdu_start` | 0x56318 | 0x8 | **NOP, returns 0** (vendor does not host-start aggregation) |
| `hmac_config_ampdu_end` | 0x5b070 | 0x1b0 | WAL cmd -> `hmac_mgmt_tx_priv_req` -> g0 id 0x0b end |
| `hmac_config_auto_ba_switch` | 0x5b220 | 0x164 | vap sync |
| `hmac_config_amsdu_ampdu_switch` | 0x5af08 | 0x168 | per-VAP `+0x18c -> +0x4a` |
| `hdpp_user_sync_write` | 0x2a18 | 0x154 | per-user context sync writer (item id in stack arg 5) |

WAL config table (`.rodata` file offset 0x24a80, 12-byte rows
`{handler, cmd, 0}`): `wal_config_addba_req` cmd **0xBA**, `delba_req` cmd
**0x324**, `set_baw_size` cmd **0x32C**, `ampdu_start` cmd
`wal_config_ampdu_start` (0x1620c8), `ampdu_end` (0x1620c4) [V]. The vendor
userspace (custom hostapd/supplicant) drives BA through these vendor commands
**and** through the D2H 0x1d message; stock hostapd has neither, so the
clean-room driver must do it in-kernel [I, high confidence].

## T2. Per-TID session state [V]

User object (vendor hmac/hdpp user, one big struct; the two regions below are
distinct):

| region | base | stride | count | init | notes |
| --- | ---: | ---: | ---: | --- | --- |
| HDDP TX TID | user+0x610 | 12 | 8 | `hdpp_tx_tid_init` | +0 tid, +1 state, +2 (u16) flags/count, +4, +8 |
| TX BA (hmac) | user+0x88*tid | 0x88 | 8 | `hmac_tx_ba_setup` | +0x84 state (0 idle / 1 ADDBA-pending / 2 active), +0x85 dialog token, +0x86 policy, +0x10 timer object, +0x78 timer data ptr |
| HDDP RX BA | user+0x670 | 0x80 | 8 | `hdpp_user_rx_init` | +0 tid, +4 window ptr, +0x18 timer object; window ptr non-NULL = present |
| RX bitmaps | user+0xa74 | 0x40 total | - | zeroed | 8 TIDs x 8 B; `user+0xa84` = the u16 "user index" used as every BA message's object key |
| RX misc | user+0xab4 (0x10 B), +0xac4 (u16 0xffff) | - | - | initialized | reorder timeout/valid bitmaps |

RX reorder window (allocated 0x1824 B per RX session, window ptr at session+4):

| offset | width | field | source |
| ---: | ---: | --- | --- |
| +0x00 | u16 | start seq | `win_init` arg2 |
| +0x02 | u16 | end seq = (start+win-1) & 0xFFF | computed |
| +0x04 | u16 | next release seq = (start-1) & 0xFFF | computed |
| +0x08 | u16 | (start-1) & 0xFFF | computed |
| +0x0A | u16 | window size (BAW) | `win_init` arg3 |
| +0x0C | u32 | 0 | init |
| +0x10 | u32 | 1 | init |
| +0x14 | u16 | 0 | init |
| +0x15 | u8 | window state: 0 = init (win_init), 2 = active (rx_add) | |
| +0x16 | u16 | pending out-of-order count | decremented on release |
| +0x18 | u16 | (start-0x100) & 0xFFF | init |
| +0x1C..+0x1820 | 0x1800 | 256-entry x 24 B reorder ring | init loop |
| +0x181C | 8 B | zeroed (tail bookkeeping) | init |
| +0x1820 | - | end (0x1824 alloc) | |

Ring entry use in `hmac_ba_rx_buffered_data_software_check` [V offsets]: slot
= window + 24*seq_low8; +28 u8 valid flag, +30 u16 stored seq, +8 list head
(freed via `hmac_rx_free_netbuf_list(window+24*seq+8)`); the -1 gap check uses
`(seq - next) & 0x800` (12-bit window math). Slot-base vs flag-offset relation
is [I] (the exact base is the only ambiguity; the functional fields are [V]).

## T3. H2D group 0, id **0x0A** - the BA relay (host -> fw) [V]

Message = 12 B header + **32 B payload = 44 B total** (`hcc_msg_alloc(32)`,
vendor shape; earlier lanes use the same wire shape). Header: hdr[6]=0x0A,
hdr[7]=0, hdr[8]=0, hdr[1].high nibble/hdr[2]/hdr[3] = the hmac-dev id triple
(chip id in hdr[3] = 0 for both bands), payload = hdr+12.

Subtype 1 = ADDBA response relay (`hmac_mgmt_rx_addba_rsp`):

| payload | width | value | source |
| ---: | ---: | --- | --- |
| +0x00 | u16 | 0x0103 (type 3, subtype 1) | literal |
| +0x02 | u16 | **user index** | user+0xa84 |
| +0x07 | u8 | **TID** 0..7 | ADDBA param >> 2 & 0xF |
| +0x09 | u8 | **ADDBA status code** low byte (0 = success -> configure; nonzero -> clear path) | rsp status |
| +0x0C | u16 | **BA window size** (10-bit buffer size, clamped to the user max `user+0xf44/+0xf46`) | rsp param >> 6 |
| +0x10 | u16 | BA timeout | rsp bytes 7..8 |
| +0x12 | u8 | A-MSDU supported (BA param bit 0) | rsp param & 1 |
| +0x14 | u16 | dialog token | rsp byte 2 |
| +0x15 | u8 | BA policy (BA param bit 1) | rsp param bit 1 |
| rest | - | 0 (sent zeroed) | |

Firmware handler: CA 0x5FF64 (fully decoded in `../wifi-scan-full/REPORT.md`
sec. 3: object key lookup 0xD6A1E(payload+2), per-TID context = obj + slot*0x20
+ 0xEC; +0x0C -> ctx+0x10 and ctx+0x1A=max(1,v); +0x10 -> ctx+0x18; +0x12 ->
ctx+0x17; ctx+0x13=2 enable; +0x14 u16 compared to ctx+0x15; +0x09 nonzero ->
clear path).

Subtype 2 = DELBA relay (`hmac_mgmt_rx_delba`):

| payload | width | value |
| ---: | ---: | --- |
| +0x00 | u16 | 0x0203 (type 3, subtype 2) |
| +0x02 | u16 | user index (user+0xa84) |
| +0x07 | u8 | TID (`(delba_param >> 12) & 0xF`) |
| +0x08 | u8 | initiator bit (`(delba_param >> 11) & 1`) |
| +0x14 | u8 | 0 (clear flag; builder leaves it zero - the fw reads it, so our builder must zero the payload) |

After the relay both builders write the per-TID BA state through
`hdpp_user_sync_write(dev, uid, buf, len)` [V call sites; wire encoding of the
user-sync writer is out of scope - see T7 note]:
- ADDBA rsp: buf = {tid, 2 (success) | 0 (failure), 0, 0}, len 4, sync item 4.
- DELBA: buf = {tid, 0, 0, 0}, len 4, item 4.
- ADDBA req process: len 6, item 8 (RX start-seq record).
- TX ADDBA request update: buf = {tid, tx_state, policy, 0}, len 3/4, item 4.
- RX BA session add/remove: buf = {tid}, len 1, item 7
  (`hmac_ba_reset_rx_handle`).

## T4. H2D group 0, id **0x0B** - A-MPDU TX start/end [V]

Message = 12 B header + **8 B payload** (`hcc_msg_alloc(8)`). hdr[6]=0x0B,
hdr[1].high/hdr[2]/hdr[3] device triple. Payload:

| payload | width | start (hdr[12]=0) | end (hdr[12]=1) |
| ---: | ---: | --- | --- |
| +0 | u8 | 0 | 1 |
| +1 | u8 | TID | TID |
| +2 | u16 | seq (TX flow dword 2..3) | - (0) |
| +4 | u16 | user key (user+0xa84) | user key |
| +6 | u8 | flow flag (dword 6) | - (0) |
| +7 | u8 | 0 | 0 |

Send gates: start is skipped unless TX BA session state (user+tid*0x88+0x84)
!= 0, or the flow flag is nonzero; end has no gate (vendor). Firmware handler
g0 id 0x0B at CA 0x5E2F4: payload[0]=0 -> `0x95188(module,payload)` (program
the per-TID aggregation context); payload[0]=1 -> `0xD6A1E(payload+4)` object
lookup then clear `obj+(tid+8)*0x20+6` (u16) and `obj+tid*0x20+0xF1` (u8).

## T5. H2D group 0, id **0x1C** - TX BA notify [V]

Message = 12 B header + **20 B payload** (`hcc_msg_alloc(20)`), hdr[6]=0x1C,
hdr[8]=0, payload[0..1] = u16 from the peer-session object (+0x0A), payload[2]
= TID, payload[3] = value 0/1/2 from the TX BA state machine. Firmware dispatch
row id 0x1C -> CA 0x5DFA0 (handler builds a follow-up MAC-core message with id
byte 0x0B); full fw semantics parked [U] - the host-side wire contract is
complete.

## T6. Other BA-relevant H2D / host-core messages [V]

| message | id / key | payload | source |
| --- | --- | --- | --- |
| config event (g0 id1) | key **0x00D7** | BAR request | `hmac_config_send_bar` |
| config event | key **0x00F1** | dump BA bitmap request | `hmac_config_dump_ba_bitmap` |
| config event | key **0x01DB** | A-MPDU aggregation number | `hmac_config_set_ampdu_aggr_num` |
| host-core msg (`hcc_msg_tx_to_core`) | id **8** | 4-byte payload = timer cookie pointer | `hmac_ba_timeout_fn`; cookie: +6 chip, +7 vap id, +8 u16 uid -> `hmac_ba_timeout_proc` (host-local core dispatch table, .data rows {handler,0,id}) |
| host-core/TX mgmt event | g0 id 6 | the mgmt TX carrier already proven on bench | used by ADDBA/DELBA frames |

## T7. D2H BA events (firmware -> host) [V field offsets / I semantics]

Dispatch table in host `.data` at offset 0x588, 12-byte rows
`{handler, 0, id}` (registered D2H handlers; same table holds scan/ACS/DFS
events, so the ids are the firmware->host event ids):

| D2H id | handler | payload (hdr+12 unless stated) | host action |
| ---: | --- | --- | --- |
| **2** | `hmac_create_ba_event` | [0..1] uid (hdr[12..13]), [2] TID (hdr[14]), [3] peer index 0..18 (hdr[15]), [4] protocol mode (hdr[16]) | validate chip/uid/vap, check TX session, `hmac_tx_ba_setup` -> ADDBA request |
| **3** | `hmac_del_ba_event` | same fields; [4] protocol mode | for each active TX TID: DELBA action frame (`hmac_mgmt_tx_action` 0x0203 descriptor) |
| **0x0F** | `hmac_mgmt_rx_delba_event` | hdr[12..13] uid, hdr[19] tid, hdr[20] initiator, hdr[21] reason | DELBA action frame to the peer MAC (user+0xa70+14) |
| **0x1D** | `hmac_ba_action_msg_process` | hdr[1]>>4 chip, hdr[3] chip idx, hdr[12..13] uid, [14] TID, [15] action 0/1/2, action 1: embedded action frame bytes at hdr+20 | 0 = TX BA setup; 1 = ADDBA response relay (uses the embedded 9+ frame bytes); 2 = DELBA action frame (initiator=1) |

D2H id 0x1D action 1 calls `hmac_mgmt_rx_addba_rsp(dev, user, hdr+20, 9)` - i.e.
the firmware relays a received ADDBA response frame to the host, which
parses/validates it and sends the T3 relay back. The host-side user-sync
mechanism (external writer) is a separate layer; the sync *call sites* are in
T3, the writer's wire encoding is [U] and not needed for the minimal path
because T3's g0 0x0A message carries the same BA parameters to the firmware
BA contexts.

## T8. Over-the-air action frames [V bytes]

All three: 802.11 management action, FC `[0..1] = 0xD0 0x00` (type mgmt
subtype action), duration 0, addr1/2/3 from (peer user MAC / own SA /
BSSID), category 3 at frame[24]. Frames are built into an skb and TXed through
`hmac_tx_mgmt_send_event` (the proven mgmt-TX carrier).

**ADDBA request (33 B)** - `hmac_mgmt_encap_addba_req`:
[24]=3, [25]=0, [26]=dialog token, [27..28]=BA parameter set =
`(baw<<6)|(policy<<1)|amsdu|(tid<<2)`, [29..30]=BA timeout (TU), [31..32]=
starting sequence control (vendor sends 0).

**ADDBA response (33 B)** - `hmac_mgmt_encap_addba_rsp`:
[24]=3, [25]=1, [26]=dialog token, [27..28]=status code (vendor: arg status,
low byte at 27, high 0), [29..30]=BA parameter set (same formula),
[31..32]=BA timeout (vendor sends 0).

**DELBA (30 B)** - `hmac_mgmt_encap_delba`:
[24]=3, [25]=2, [26]=0, [27]=parameter byte =
`(tid<<4)|(initiator<<3)`, [28]=reason code (vendor 0x27 in the WAL path,
0 in the fw-event path), [29]=0.

Input arguments (ADDBA req): buffered-size u16 (params[0..1]), BA policy
(params[6]), A-MSDU (params[7]), dialog token (params[5]), BA timeout
(params[8..9]), TID (4th arg, shifted <<2); rsp: same + status (stack arg).
[V]

## T9. Firmware dispatch (group-0 H2D table) [V]

Table CA **0x10CCFC**, 12-byte rows `{u32 id; u32 fn|Thumb; u32 aux}`:

| id | handler CA | role |
| ---: | ---: | --- |
| 0x0A (10) | 0x5FF64 | BA relay configure/clear (decoded in wifi-scan-full) |
| 0x0B (11) | 0x5E2F4 | A-MPDU TX start/end (T4) |
| 0x1C (28) | 0x5DFA0 | TX BA notify (T5) |

Sibling BA-relevant rows also present: id 0x02/0x03 firmware scan messages,
id 0x06 mgmt TX, id 0x1B deauth. D2H senders for ids 2/3/0x0f/0x1d are in the
firmware and reach the host through the registered D2H table (T7); their CA
senders are not needed for the host contract and are parked [U].

## T10. Fail-closed contract + host-test matrix

| item | value / expectation |
| --- | --- |
| module param `ba_enable` | uint, default **0** |
| `ba_enable=0` | every entry point returns `-EOPNOTSUPP`; zero frames built; zero messages posted; no state change |
| `ba_enable=1` | sessions may be created; still no data-path hook in the skeleton (inert until the integration rung) |
| `ba_enable>1` | `-EINVAL`, no frame |
| BAW window | requested clamped to 1..256, skeleton default 64; parsed peer BAW > 256 rejected |
| BA timeout | parsed peer timeout used only if <= 0xFFFF TU; 0 accepted (no timeout) |
| invalid action frame | wrong category/action/short frame -> drop, no reply, no relay |
| unknown D2H id | ignored, no state change |
| relay payload | full 32 B zeroed before the field writes (esp. +0x14 clear flag) |
| H2D message length | exactly the vendor length: 44 / 20 / 8 B total (12 B header) |

Host test matrix (see `src/ba_selftest.c`): frame byte-for-byte for all three
action frames, T3/T4/T5 payload byte maps, relay round-trip, parser rejects,
state machine (idle -> pending -> active -> deleted), the D2H 0x1D three
actions, fail-closed disabled behaviour, out-of-range/oversized inputs.
