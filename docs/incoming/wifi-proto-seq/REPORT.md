# wifi-proto-seq - the first D2H answers decoded, and the calibration alg set_* startup messages

**Lane:** wifi-proto-seq (shared task **task-103**), host-only static RE + frame-builder lane.
**Date:** 2026-11-24. **Bench state input:** the working full-duplex lap (PD4 golden boot ->
context-create type 1 -> TXQ config sync -> firmware responses g1 id3 / g1 id0x12).
**Deliverables:** this report, tools/ (3 tested tools), recipes/ (2 machine-readable recipes).

Evidence markers: **[V]** re-derived here from committed contract / local pinned vendor
artifacts (tools in this lane assert it); **[V*]** measured on the device by the parent lanes
and quoted; **[I]** inference; **[U]** not decidable with host-only evidence.

---

## 0. TL;DR - the five answers

1. **g1 id3 (36 B) is fully decoded.** It is a *config-sync* frame:
   payload = {u32 cookie0; u32 cookie1; u16 syn_id; u16 body_len; u8 body[body_len]; 4 B pad}.
   The captured bytes are syn_id **0x07E0 = WLAN_CFGID_VAP_MIB_UPDATE (2016)**, body_len 8,
   field B (hdr[3]) = **4 = the vap/module our type-1 context-create created**, body =
   00 01 01 04 01 00 01 00 (a MIB capability struct; data[3] = 4 is the bandwidth field the
   handler converts with mac_device_trans_bandwith_to_vht_capinfo). Cookies are 0, so it is
   an **unsolicited vap-MIB sync event**, not the TXQ-sync answer.
2. **g1 id0x12 (40 B) is decoded structurally.** Dispatched as g1 id18
   hmac_event_acs_response; the frame bytes match the firmware's g1 id18 sender
   (CA 0xA118C) exactly: payload = {u8 a; u8 core; u8 b; u8 pad; u32 payload_len; u32 c;
   u8 body[payload_len-0xC]}, observed a=0x0A, core=0, b=0, payload_len=0x1C, c=0,
   body = 00 00 00 00 00 00 01 04 0a 01 01 01 00 00 00 00. The host handler does not parse
   the body (it clones the frame to group 2 / id 8 and re-transmits) - body semantics stay
   **[U]** on the host side.
3. **The next startup message family is identified and built**: the six vendor calibration
   config events (alg set_* from /usr/bin/hi5622v100_cal_init.sh) ride **HCC group 0 /
   id 2, config key 0x0101**, carrying a fixed **270-byte alg_cfg blob**; total frame 298 B,
   SR node word1 0x012A6D2B.
4. **cfg ids and value layouts are recovered [V]**: set_2g_power_param=3502 (value 104 B at
   blob+10), 3503=2g low (104), 3504=5g (70), 3505=5g low (70), 3506=xo ppm (u32 at +10),
   3507=rssi (40 B). All flag need_d_process=1 / need_w4_device=1.
5. **The expected device answer is defined**: one D2H g1 id3 config-syn frame per message
   with syn_id **0x0101**, body_len 270, body cfg_id = request cfg id, dev_ret = 0, cookies
   echoed. The ordered bench ladder (recipes/cal-alg-ladder.json) fires **set_rssi_param
   first** (lowest blast radius), then the vendor order, one message class per boot until
   proven.

---

## 1. Method and artifacts

Host-only. Inputs read in place, never committed:

| artifact | use |
| --- | --- |
| pinned vendor firmware rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin sha256 7fc87e20...311b, 928,920 B (image convention CPU = file + 0x40000) | firmware-side builders/dispatch (tools/fw_proto_static.py, capstone) |
| local vendor modules hi5622v100_wifi-p.ko sha256 22fa789a...c7d2 and hi5622v100_plat-p.ko sha256 27c69791...ba27 (wifi-shim-port/raw, pre-policy local evidence) | 414-entry alg command table, LUT parser pointers, equipment defaults (tools/alg_cmd_table.py, minimal ELF parser) |
| local hi1105 reference source tree (host-only oracle, never committed) | WLAN_CFGID enum values / MIB struct field names [I]/[V] |
| parent-lane captures (read-only DR reader; local evidence) | the two D2H frames decoded in sec 2/3 |

---

## 2. g1 id3 - hmac_event_config_syn, decoded [V]

### 2.1 Where it comes from

* Host dispatch table: vendor D2H hmac group-1 table at .data+0x584, 28 x 12-byte records
  {u32 id; u32 handler-reloc; u32 pad}; entry 3 -> hmac_event_config_syn (verified by
  parsing the .ko .symtab and .rel.data).
* The host handler routes by a 16-bit "sync code" read at hdr+0x14 (payload+8) through a
  73-entry table (.LANCHOR1 in .rodata at 0xEB0, entries {u16 code; u16 pad; u32 fn},
  sentinel 0x1451) to per-module response handlers (hmac_sync_dmac_alg_cfg_rsp_entry,
  hmac_sync_dmac_cali_cfg_rsp_entry, ...).
* Firmware side: the single D2H config-syn sender is CA 0x85914 (asserted by
  tools/fw_proto_static.py): alloc(body_len+0x10) -> group 1, id 3 -> hdr[3] = vap object's
  id, hdr[1] high nibble = core, hdr+0xC/+0x10 = cookie pair (echoed from the request stack
  args), hdr+0x14 = syn_id, hdr+0x16 = body_len, memcpy body at hdr+0x18 (+4 B alloc pad) ->
  hcc_msg_tx. Hence payload_len = body_len + 0x10 and total = 12 + payload_len.

### 2.2 The captured frame

    01 00 00 04 24 00 03 00 00 00 5A 5A   header: group 1, core 0, fieldA 0, fieldB 4,
                                            total 0x24=36, id 3, tag 0x5A5A
    00 00 00 00   cookie0
    00 00 00 00   cookie1
    E0 07         syn_id  = 0x07E0
    08 00         body_len = 8
    00 01 01 04 01 00 01 00   body (8 B)
    00 00 00 00   alloc pad

* **syn_id 0x07E0 = WLAN_CFGID_VAP_MIB_UPDATE = 2016 [V]** (local vendor reference
  wlan_cfg_id_2.h; host dispatch table entry 28 = hmac_config_d2h_vap_mib_update).
* **field B = 4**: the config-syn dispatcher uses hdr[3] as the per-module index for the
  mac-vap lookup, and the type-1 context-create made module/vap 4 [V*].
* **body**: the handler hmac_config_d2h_vap_mib_update reads (vendor binary): data[0],
  data[1] -> object fields, **data[3] -> bandwidth** (then
  mac_device_trans_bandwith_to_vht_capinfo), data[5], data[6] -> context fields [V]. The
  modern reference struct mac_d2h_mib_update_info_stru gives the field names [I]:
  en_11ax_cap=0, en_radar_detector_cap=1, en_11n_sounding=1, en_wlan_bw_max=4,
  us_beacon_period=1, en_green_field=1, en_mu_beamformee_cap=0.
* **cookies 0/0** -> the handler's wal_set_dev_msg_response_by_addr pairing is skipped:
  this is an **unsolicited event**, not the answer to an outstanding config request.
* Attribution **[I]**: this is the hmac layer syncing the just-created vap 4's MIB
  capabilities to the host right after the context-create. It is *not* the TXQ-sync answer
  (the key-0xd8 worker at CA 0x81038 has no send path at all [V]).

---

## 3. g1 id0x12 - hmac_event_acs_response family, structural decode

### 3.1 The exact 40 bytes

    01 00 00 12 28 00 12 00 00 00 5A 5A   header: group 1, core 0, fieldA 0, fieldB 0x12,
                                            total 0x28=40, id 0x12, tag 0x5A5A
    0A 00 00 00   a
    1C 00 00 00   payload_len = 0x1C = 28 (== len(payload))
    00 00 00 00   +8 u32 c = 0
    00 00 00 00
    00 00 01 04
    0A 01 01 01
    00 00 00 00   body[16]

(The parent lane's first transcription had 36 B of payload; the byte-exact re-read is 12 B
header + 28 B payload = 40 B and matches the node length 0x00286D2B.)

### 3.2 Decode [V] structural

* Host dispatch: g1 table entry id 18 -> hmac_event_acs_response (same .data+0x584 table,
  12-byte records).
* Firmware builder: CA 0xA118C is the only builder whose stores match this frame:
  alloc(payload_len) -> group 1, id 0x12, field B = 0x12, hdr[2] = b, payload:
  +0 u8 a | +1 u8 core | +2 u8 b | +3 u8 pad | +4 u32 payload_len | +8 u32 c |
  +0xC body[payload_len-0xC]; then hcc_msg_tx. Observed: a = 0x0A, core = 0, b = 0,
  payload_len = 28, c = 0, body = 00 00 00 00 00 00 01 04 0A 01 01 01 00 00 00 00.
* The host handler does not parse the payload: it calls hcc_msg_clone(msg, 2, 8) (new group
  2, new id 8) and hcc_msg_tx on the clone. Payload/body semantics are therefore
  device-side **[U]**; the bytes above are the whole evidence.

### 3.3 What it is not

Neither response is the acknowledgement of the TXQ config sync: the key-0xd8 worker only
stores one byte through the newly created context and returns [V]. Both frames are **event**
traffic that appeared once the context existed (the same boot also carries g4 id4 HEARTBEAT
frames on dr3, i.e. the device log/event layer is alive).

---

## 4. The config-sync response contract (used as the expected-answer check)

For every device-processed config event with key 0x0101/0x0102 the firmware answers:

| field | value |
| --- | --- |
| HCC | group 1, id 3, tag 0x5A5A |
| payload | cookie0 u32 | cookie1 u32 | syn_id u16 | body_len u16 | body[body_len] | 4 B alloc pad |
| total | 12 + body_len + 0x10 |
| syn_id | 0x0101 (alg cfg) / 0x0102 (cali cfg) |
| body | the request's alg_cfg blob with dev_ret updated |

Firmware path [V] (tools/fw_proto_static.py): g0 id2 key table at CA 0x106EFC
{0x00FE -> 0xD5B41, 0x0101 -> 0xD5FA7, 0x0102 -> 0xD5F81, sentinel 0x1451}; the key-0x101
handler clears data[4..7] (dev_ret), reads data[8] (need_w4), and answers through CA 0x85914
with syn_id 0x0101. A request with data[8]=1 and a valid field-B object always gets one
answer.

---

## 5. The next startup messages - the calibration alg set_* family

### 5.1 Carrier (g0 id2, key 0x0101) [V]

The vendor builder hmac_config_alg_send_event (wifi.ko 0x60bb8) is the same config-event
carrier as the working g0 id1 frames:

    header 12 B: group=0, byte1=(dst<<4)|0x01, byte2=fieldA, byte3=fieldB(module/vap),
                 total=12+0x10+len, id=2, byte8=0, byte9=dst, tag=0x5A5A
    payload    : u32 cookie0 | u32 cookie1 | u16 key=0x0101 | u16 len |
                 alg_cfg_blob[len] | 4 B inherited pad

With the fixed 270-byte blob: payload 286 B (0x11E), frame total **298 B (0x12A)**, SR node
word1 = (298<<16)|0x6D2B = **0x012A6D2B**; the declared total equals the node length (the
engine copies the declared total).

### 5.2 alg_cfg blob [V]

| off | field |
| ---: | --- |
| +0 | u16 cfg_id |
| +2 | u8 cfg_type (0 = SET, 1 = GET) |
| +3 | u8 pkt_type (0 = irrelevant) |
| +4 | u32 dev_ret (host writes 0) |
| +8 | u8 en_need_w4_dev_return = **1** (the device answers) |
| +9 | u8 resv |
| +10 | value[] (command-specific, zero-padded to 270) |

Value offset +10 is anchored three ways [V]: the firmware power/rssi handlers take
src = blob+0xA (0xD0D44 SET branch, 0x72A39 SET branch), the xo-ppm handler loads its u32 at
blob+0xA, and the host equipment parser stores value slots at mem_block+0xA.

### 5.3 The six commands [V]

| vendor order | command | cfg id | value size | device destination / handler |
| ---: | --- | ---: | ---: | --- |
| 1 | set_2g_power_param | 3502 | 104 (0x68) | g_dev_custom_info+0x6C4, handler 0x72BEF -> 0xD0D44 |
| 2 | set_2g_low_power_param | 3503 | 104 | g_dev_custom_info+0x72C, handler 0x72BE5 |
| 3 | set_5g_power_param | 3504 | 70 (0x46) | g_dev_custom_info+0x88A, handler 0x72BDB |
| 4 | set_5g_low_power_param | 3505 | 70 | g_dev_custom_info+0x8D0, handler 0x72BD1 |
| 5 | set_xo_ppm_cali_param | 3506 | 4 (u32) | xo-ppm globals, handler 0x72B89 (0 -> default 0x4040 path) |
| 6 | set_rssi_param | 3507 | 40 (0x28) | rssi param buffer, handler 0x72A39 |

All six: type SET, flags 0x01010000 (device-processed, wait for device return), LUT parser
alg_cfg_args_param_analyse_equipment_param, fixed 270-byte blob. cfg ids/layout were
recovered by resolving the 414-entry process table names statically - the table's name words
are ordinary .rodata.str1.4 relocations (the earlier "0x0a9874 custom fixup list" reading was
the addend of entry 0; see tools/alg_cmd_table.py --selftest).

### 5.4 Values

* Per-unit factory calibration lives in /usr/local/factory/hi5622v100.cal on a stock unit.
  It is unit-specific calibration data: it is **never committed** and not present in the
  local rootfs copy.
* The vendor defaults (equipment-default table) point into g_dev_custom_info, which is
  **BSS in the plat module** (filled at runtime) - not statically recoverable host-only [V].
* For protocol bring-up the lane ships a **zero-value structural frame** (all value bytes 0)
  - byte-structurally exact, semantically neutral. At bench time the local cal file (or a
  runtime kprobe read of the vendor boot) can supply values via --values-hex.
* 5g tables are written too on a 2.4 GHz-only board; harmless (no TX is started by these
  config writes; they only populate RAM tables).

### 5.5 Field B (hdr[3]) selection

The g0 id2 handler resolves hdr[3] via CA 0xBC5D6(hdr[3]); NULL returns 0x64 with no
response [V]. Bench candidates: **4** = the module our type-1 context-create made (default),
0 and 0x12 = the boot-created module 18 fallback. One value per boot; the ladder carries the
decision rule.

---

## 6. Expected-response checks (machine-checkable)

For request cfg id X, the next new g1 id3 frame must satisfy
(tools/proto_seq_frames.py --check-response <frame-hex> X):

1. total == 298, group 1, id 3, tag 0x5A5A;
2. payload syn_id == 0x0101;
3. body_len == 270;
4. body u16 @0 == X;
5. body u32 @4 (dev_ret) == 0;
6. response field B = vap id byte of the resolved object (expected 4 = our created
   module; [I] - record what the bench sees);
7. device safety: mask CA 0x40008 == 0, handshake page 0x86807FEF/7/2/0xABCD, state == 1.

No host reply is required or sent for these responses (config-sync is notification).

---

## 7. Ordered bench ladder

Machine-readable: recipes/cal-alg-ladder.json. Summary:

    PD4 golden boot (release 0x5A5A only; page/state/mask/ready pair)
     -> P0 read-only pack (ctx190/ctx0/mask/staging)
     -> P1 h2d_ctx_create type 1        [expect ctx190 != 0, mask 0]
     -> P2 h2d_txq_sync72               [expect consumed, mask 0, no staging frame]
     -> P3 rd_catalog                   [expect g1 id3 36 B + g1 id0x12 40 B; decode them]
     -> C1 ONE calibration frame: set_rssi_param (3507), zeros, field B 4, 298 B node
           [expect consumed + ONE g1 id3 syn 0x0101 cfg 3507 dev_ret 0 + clean boot]
     -> C2 fresh boot, budget 7: create + txq + 3502, 3503, 3504, 3505, 3506 (vendor order minus the probe)
     -> C3 fresh boot, budget 8 (driver max): create + txq + canonical vendor order 3502..3507
     -> C4 optional: repeat C3 with local per-unit values

Stop rules: on the first exception (mask != 0), page change, announce not consumed, or a
malformed/absent answer -> stop the boot, read the exception record + staging area, and
bisect on a fresh boot (one variable per boot). If a message is consumed but produces no
answer, the field-B object lookup failed: retry ONE of field B = {0, 0x12} on a fresh boot.
Never send a second new message class on a boot where a stop rule fired.

### 7.1 Bench driver support (implemented)

The wifi-h2d-send lane's task-103 extension implements this path: **h2d_cfg_cal (WO)** takes
N (1..6 vendor order) or a literal cfg id 3502..3507, optionally " fb=M" for the header field
B (default 4), and the h2d_send grammar token **cfg_cal [cfg=N] [fb=M]**. It builds the
298-byte frame through h2d_build_g0_cfg_event(id=2,...) with the 270-byte blob, commits it
through the same SR node + mailbox id3 announce + consume-poll path and gates as
h2d_txq_sync, node length = declared total (298), and records cfg_cal_sent in h2d_status /
h2d_plan (read-only C1 preflight). Budget: C1 needs h2d_send_budget=3 (create + txq + one
calibration frame); the canonical six-message boot needs 8 (the driver maximum).

---

## 8. Risks and safety boundary

* The new messages are H2D config events on the proven carrier: same SR node, same mailbox
  id3 announce, same gates. No new host register writes are introduced.
* Values are RAM tables in the device; no TX/PHY activation is caused by these config writes.
  Zero-value frames are the prescribed protocol probe.
* Hard rules unchanged: release-only 0x5A5A boot, never 0xCECE during boot, never write
  CA 0x400392F0 (out[5]), no group-4 H2D, one frame per idea for a new class, stop on any
  exception.
* Never commit calibration values, vendor blobs, or vendor binaries; the tools read local
  artifacts in place and print derived annotations only.

---

## 9. Mapping rows for the cfg80211 lane (task-104)

Canonical source: this lane (opensource/docs/incoming/wifi-proto-seq/REPORT.md, sec 2/3/5)
plus the cited lanes. Rows that matter for VAP/scan integration:

| cfg80211 op | vendor kernel entry | HCC H2D | H2D payload | D2H events | evidence |
| --- | --- | --- | --- | --- | --- |
| add_virtual_intf / VAP create | wal_hipriv_add_vap | **g0 id7** hmac_user_add (0xA8) + **g0 id8** notify_alg (0xA8); delete = **g0 id9** hmac_del_user_notify_dmac (0x0C) | exact builder already committed (wifi-h2d-frames catalogue) | g1 id3 config-syn if need_w4; vap MIB update 0x07E0 | this lane sec 2; ../wifi-h2d-frames |
| scan | wal_cfg80211_scan -> hmac_enc_scan_send_req | **g0 id0xC** | 0x1FC [U] | g1 ids 15/16/17 + g2 id2 | ../wifi-vendor-userspace TABLE T6; ../wifi-h2d-catalog |
| AP start / config | wal_cfg80211_start_ap -> wal_send_cfg_event + hmac_config_send_event | **g2 WAL cfg event** + **g0 id1** | per command | g1 id3 | ../wifi-vendor-userspace |
| calibration / alg cfg | wal_hipriv_alg_cfg -> hmac_config_alg_send_event | **g0 id2 key 0x0101** | 270-byte alg blob (cfg ids 3502..3507 for the factory cal; 401.. for runtime alg) | **g1 id3 syn 0x0101 -> hmac_sync_dmac_alg_cfg_rsp_entry** | this lane sec 5/6 |
| STA join | hmac_sta_sync_join_req_params | g0 id0xF/0x10/0x11 | [U] | g2 ids 3/4/5/6, g1 id3 | ../wifi-vendor-userspace |
| observed D2H (already seen) | - | - | - | **g1 id3 36 B** (hmac_event_config_syn, syn 0x07E0 vap-MIB) and **g1 id0x12 40 B** (hmac_event_acs_response, field B 0x12) | this lane sec 2/3 |

For M0-M2 the relevant new fact is: g1 id3 is the **generic config-sync channel** used by
both the calibration path (syn 0x0101/0x0102) and per-vap capability events (syn 0x07xx);
a cfg80211 backend must register it and decode by syn_id, not by frame size.

---

## 10. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | g1 id3 handler = hmac_event_config_syn; g1 id18 = hmac_event_acs_response; group-1 table 28 x 12 B at .data+0x584 | ELF .symtab/.rel.data parse of the local vendor wifi.ko | [V] |
| E2 | config-syn payload = cookies[8] + syn_id u16 + body_len u16 + body[] + 4 B pad, total = body_len+28 | firmware sender CA 0x85914 instruction-level decode + the captured frame | [V] |
| E3 | captured syn_id 0x07E0 = WLAN_CFGID_VAP_MIB_UPDATE = 2016; body is the vap MIB struct; data[3] is the bandwidth field | 73-entry host dispatch table (.rodata 0xEB0) + local reference enum + vendor handler decode | [V]/[I] |
| E4 | g1 id0x12 frame bytes match the firmware g1 id18 sender CA 0xA118C and its payload layout | instruction-level store/branch match against the 40-byte capture | [V] |
| E5 | the host id18 handler does not parse the payload (clone to g2 id8 + tx) | plat.ko hcc_msg_clone/hcc_msg_tx decode + handler body | [V] |
| E6 | calibration carrier: g0 id2 key 0x0101, payload tokens/key/len/blob, total 298 B, node word 0x012A6D2B | firmware builder model + key table + host builder (hmac_config_alg_send_event 0x60bb8) | [V] |
| E7 | six commands -> cfg ids 3502..3507, SET, need_d/need_w4, equipment-param parser, 270-byte blob | static process-table name resolution (no runtime fixup) + LUT relocations | [V] |
| E8 | value area at blob+0xA; power 0x68/0x46; xo u32; rssi 0x28; dev destinations | firmware handler decode (0xD0D44 / 0x72B89 / 0x72A39) | [V] |
| E9 | expected answer = g1 id3 syn 0x0101, body cfg id echoed, dev_ret 0 | key-0x101 handler decode through CA 0x85914 + host rsp entry | [V] |
| E10 | vendor default values are runtime BSS (not statically recoverable) | plat.ko symbol section = .bss; defaults table points into g_dev_custom_info | [V] |
| E11 | attribution of the two events to the context-create | timing (both appear after the lap) + field B = the created module; no TXQ-sync send path exists | [I] |
| E12 | g1 id0x12 body semantics | device-side only; host handler forwards opaque | [U] |

## 11. Reproduction (host-only)

    P=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-proto-seq/tools
    $P $T/proto_seq_frames.py --selftest          # 66/66 checks
    $P $T/fw_proto_static.py --selftest           # 38/38 checks
    $P $T/alg_cmd_table.py --selftest             # 55/55 checks
    $P $T/proto_seq_frames.py --emit 3507         # 298-byte zero-value calibration frame
    $P $T/proto_seq_frames.py --decode-id3 <36-byte hex>
    $P $T/proto_seq_frames.py --decode-id12 <40-byte hex>
    $P $T/proto_seq_frames.py --check-response <298-byte hex> 3507
    $P $T/fw_proto_static.py --show               # derived firmware annotations
    $P $T/alg_cmd_table.py --show                 # the six cfg ids / sizes / parser

## 12. Open items

1. g1 id0x12 body field semantics (device-side host-context event): needs the firmware's
   host-context event consumer or a vendor-boot capture; [U] here.
2. The vendor default calibration values (g_dev_custom_info) are runtime-only; obtaining
   them needs the per-unit factory cal file (local) or a vendor-boot runtime read.
3. Field-B (module/vap) acceptance for g0 id2 on our boot: predicted 4 (our create) with
   0/0x12 fallbacks; confirmed by the C1 fire.
4. The full calibration path also includes the cali container (key 0x0102) and the .kv
   load; out of scope of this lane (see wifi-algcfg-slice / wifi-vendor-userspace).
