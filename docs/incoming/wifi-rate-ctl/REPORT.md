# wifi-rate-ctl - the vendor alg_cfg engine and the rate-control command surface

**Lane:** wifi-rate-ctl (shared task task-179, "RC-1"). **Date:** 2026-11-25.
**Mode:** host-only static RE + a fail-closed driver patch + host tests + a fire
recipe.  This lane never contacted the router; the Lead fires.

**Evidence grading (the notebook convention):** [V] verified against a local
artifact (addresses/bytes/tables disassembled or read from the pinned vendor
module); [I] inferred from the decoded code but not bench-observed; [U] unknown
or not statically decidable.  The vendor .ko, the firmware and the calibration
file stay local and are never committed; every address below is a
symbol/.text/.data offset in that local copy (sha256
22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2).

---

## 1. Result (the RC-1 gates)

| gate | result | evidence |
| --- | --- | --- |
| the alg engine tables decoded (process table, LUT, response output tables) | [V] 414 process rows, 416 LUT entries, 4 output tables, 57 rate-family wire layouts | tools/decode_alg_engine.py --selftest 41/41 PASS, evidence/alg-engine.json |
| the hmac_config_alg path decoded | [V] two carriers: the device alg-cfg config event (g0 id2 key 0x0101, proven on the bench by the calibration ladder) and the raw text/op command path (g0 id1 cmd 0x0128); the special "sch usr_bw" inline branch decoded | function disassembly 0x58e18; hmac_config_alg_send_event 0x60bb8; hmac_config_send_event 0x5717c |
| the per-rate TX power tables decoded | [V] TPC command family ids 1801..1809 + 4 value enums (tpc_mode / tpc_pow_lvl / pow_mode / resp_chain_sel / waterfilling); equipment per-rate power arrays 3502..3511 sizes [V] (values are per-unit, runtime-only) | section 7 |
| the minimal config to make the rate mode deterministic | [V]+[I] SET rate_mode auto (cfg id 401, one value byte 0) + GET rate_mode readback; the vendor's default rate adaptation is firmware-internal and the vendor userspace never configures it (rootfs evidence) | sections 6.3, 8 |
| a fail-closed patch | [V] new hi5622_rate_ctl module surface on the wifi-h2d-send tree: default-off parameter, per-idea write attributes, strict grammar, the proven SR-node/mailbox gates, D2H sync capture; host selftest + compile evidence | patches/0001-rate-ctl.patch, BUILD-NOTES.md |
| host tests | [V] portable blob builder/parser selftest (KAT wire bytes + response parse + fail-closed matrix) + the decode-tool selftest | tools/run-host-tests.sh, evidence/ |
| a fire recipe with the rate-selection observable | [V] (design) R0 preconditions, R1 SET auto, R2 GET readback, R3 GET current-ration, stop rules, machine-checkable PASS criteria | FIRE-RECIPE.md |

The one-line answer to "what does the driver need to enable the vendor's rate
adaptation": **nothing is required for the default** - the firmware rate
adaptation runs on its own and the vendor host never configures it - **but the
rate mode is host-selectable**: the deterministic-enable frame is one 39-byte
g0 id2/key-0x0101 config event whose 11-byte alg_cfg blob is

    cfg id 401 LE = 91 01 | type SET = 00 | pkt_type 00 | dev_ret 00000000 |
    need_w4 01 | resv 00 | value 00 (auto)
    => blob 91 01 00 00 00 00 00 00 01 00 00  inside the config-event frame
    (section 4).  The observable is the firmware's g1 id3 config-sync answer
    plus the GET readback (section 8).  Setting a static/fixed rate instead is
    the rate_mode fixed value 1, or the per-packet-type fixed blocks
    hi_rate/legacy_rate (section 5.4).

---

## 2. Scope and method

The vendor driver's "alg engine" is the host half of a text/config command
surface that programs the firmware's PHY/MAC algorithms: rate control (this
lane), TX power/TPC, EDCA, aggregation, sounding, OFDMA and calibration.  The
task is to decode it well enough to (a) send the rate-control config messages
correctly and (b) observe the rate selection.

Method: a minimal ELF32-ARM reader (tools/decode_alg_engine.py) reads the local
unstripped vendor module, resolves the R_ARM_ABS32 relocations of the data
tables, and joins three host structures:

1. the process table g_ast_alg_cfg_process_info_table - the command catalogue
   (name, cfg id, SET/GET, processing flags);
2. the dispatch LUT g_alg_cfg_lut - per (cfg id, access) entry: an optional
   dedicated parser, the expected argument count, the alg_cfg blob length, and
   up to 10 field descriptors that encode the value bytes;
3. the response output tables g_ast_hmac_config_alg_*_output_table - the
   per-id and per-family response formatters.

The decode tool's --selftest pins every number used below (41 checks).

---

## 3. The alg_cfg engine (host side)

### 3.1 Command catalogue: g_ast_alg_cfg_process_info_table

.data+0x35c8, 414 rows x 12 B:

    +0  u32  name offset into .rodata.str1.4 (resolved statically)
    +4  u16  cfg id
    +6  u8   cfg type: 0 = SET, 1 = GET
    +7  u8   reserved
    +8  u8   need_h_process  +9 u8 need_w4_host
    +10 u8   need_d_process  +11 u8 need_w4_device

The rate-control family (ids 401..422, 1801..1809) is device-processed with a
device answer: flags 0x01010000 = need_d_process + need_w4_d [V].  That is why
a SET is expected to produce a firmware config-sync response.

Command name discovery: each name word is an ordinary R_ARM_ABS32 relocation
into .rodata.str1.4; the full name list resolves statically (the earlier
"runtime-patched names" reading was the relocation addend). [V]

### 3.2 Dispatch: g_alg_cfg_lut

.data+0x4934, 416 entries x 176 B.  A LUT entry is found by the u32 key at +0;
the key is (scope << 16) | cfg_id and scope bit 0 is the GET flag (so 401 =
SET rate_mode, 0x00010191 = GET rate_mode).  The scope high bytes further
distinguish command classes (0x0000/0x0001 vs 0x0100/0x0101); see TABLES.md.

Entry layout [V]:

    +0   u32 key
    +8   u32 dedicated parser fn (NULL = the generic field-descriptor path)
    +12  u8  argc            (number of value arguments)
    +13  u8  reserved
    +14  u16 alg_cfg blob length in bytes (10-byte header + value bytes)
    +16  field descriptor[0]   (16 B each, up to 10)
         +0 u8 kind (0 numeric, 1 enum-name match, 2 fixed binary)
         +1 u8 sub-kind (kind 0: 0 = int, 1 = enum-index; kind 1: match length)
         +2 u8 value size in bytes
         +3 u8 reserved
         +4 u16 value offset inside the alg_cfg blob
         +8 u32 value-table pointer (kind 1)
         +12 u32 value-table size pointer (kind 1)

The generic executor is the unnamed function at .text+0x149d78 (called from
wal_wlan_cfg_alg_process_entry 0x14ae44): for commands without a dedicated
parser it walks the field descriptors, converts each argument (atoi /
enum-name match / MAC), writes the value bytes at the descriptor offset, and
finally stores the blob length from LUT+14 into the config request. [V]
alg_cfg_analysis_args (0x14ac38) builds the 10-byte header and the argument
context: it detects an optional leading 802.11 packet-type token
(ucast_data, mcast_data, bcast_data, mumimo_data, ofdma_data, ...,
prot_ctrl, ndp, ndpa, resp_ctrl, other_ctrl) and resolves the command name in
the process table. [V]

### 3.3 The two carriers

**A. The device alg-cfg config event (the rate path).**  The device-processed
commands travel as an HCC config event:

    HCC group 0, id 2, key 0x0101  (vendor builder hmac_config_alg_send_event,
                                    .text+0x60bb8)
    payload = cookie0 u32 | cookie1 u32 | u16 key(0x0101) | u16 len | blob[len]
    frame total = 12 (HCC header) + 16 (cookies/key/len) + len

wal_config_alg_cfg_param_host_entry (.text+0xfc284) is the dispatcher: for a
device-processed cfg id it takes blob = *(void**)(req+0) and
len = *(u16*)(req+10) and calls the builder with key 0x0101 [V].  This is the
carrier the calibration ladder already proved on the bench (298-byte frame,
270-byte blob, answered by the firmware with g1 id3 / syn 0x0101) [V bench,
wifi-proto-seq].  Bytes and limits: h2d_build_g0_cfg_event in the driver.

**B. The raw text command path (WEXT/debug).**  hmac_config_alg (.text+0x58e18,
508 B) is reached from wal_config_alg (0x161fc0, a 4-byte thunk) and receives a
packed argv: data[0] = argument count, data[1..count] = per-string offsets
relative to data+8.  It has one inline special case - the first two strings
equal "sch" and "usr_bw" - which resolves the user by MAC
(mac_vap_get_user_by_addr), converts the value with oal_atoi, and sets a
per-user bandwidth-limit flag (the error strings
{alg_schedule_config_user_bw_limit:mac_vap_find_user fail} /
{hmac_config_alg::hmac_config_send_event failed[%d].} are .LC189/.LC190) [V].
Everything else is forwarded verbatim through hmac_config_send_event (0x5717c)
as **HCC group 0 id 1, payload = u16 command id (0x0128 = 296 for alg) | u16
len | data** [V] - a different, text-level carrier that the firmware alg debug
parser would read.  This lane does not use carrier B (the binary carrier A is
the proven one and the rate family is device-processed).

### 3.4 The response path: the output tables

The firmware's config-sync answer for key 0x0101 is a g1 id3 frame whose body
is the request blob with dev_ret updated and (for GET) the value filled.
hmac_sync_dmac_alg_cfg_rsp_entry (.text+0x87b58) dispatches on body[0] (cfg id)
through the 32-range table g_ast_hmac_config_alg_output_table (.data+0x99c,
rows {u16 start, u16 end, u32 handler}) [V]:

| id range | family handler |
| --- | --- |
| 401..422 | alg_autorate_config_param_output_entry (22 per-id rows, g_ast_hmac_config_alg_autorate_output_table .data+0x870) |
| 1801..1809 | alg_tpc_bbit_config_param_output_entry (6 per-id rows, g_hmac_config_alg_tpc_output_table .data+0x96c) |
| 3501..3525 | alg_eqment_config_param_output_entry (calibration family) |
| 4304..4308 | alg_ns_rate_config_param_output_entry |
| ... 28 more ranges ... | see evidence/TABLES-generated.md |

The per-id formatters are the source of the response field map: each
alg_cfg_process_*_output function reads the body value bytes at +10.. and
prints the iwpriv ack (e.g. "[iw]rate_mode set succ!", "tpc_mode = %u",
{alg_cfg_process_curr_ration_output} prints tx_phymode/tx_bw/tx_nss/tx_gi/
tx_mcs for the queried MAC) [V].  Two gaps: **1803 (tpc_code), 1807/1808
(far/near distance switch) have no row in the TPC output table** [V] - their
GET answers would hit the family dispatcher's "cfg_id invalid" path; do not
rely on them as observables.

---

## 4. The wire format (what the patch sends)

alg_cfg blob (10-byte header + value), all little-endian [V] (firmware handler
model and host parser agree):

    +0  u16 cfg_id
    +2  u8  cfg_type     0 = SET, 1 = GET
    +3  u8  pkt_type     0 = irrelevant (the leading packet-type token)
    +4  u32 dev_ret      host writes 0; the device overwrites it
    +8  u8  need_w4_dev_return   1 = the device answers (our SETs/GETs use 1)
    +9  u8  reserved     0
    +10 value bytes      per-command (TABLES.md)

Examples (cfg id / access -> blob hex):

| message | blob hex | frame total |
| --- | --- | ---: |
| SET rate_mode auto | 91 01 00 00 00 00 00 00 01 00 00 | 39 B |
| SET rate_mode fixed | 91 01 00 00 00 00 00 00 01 00 01 | 39 B |
| GET rate_mode | 91 01 01 00 00 00 00 00 01 00 00 | 39 B |
| SET ar_dev_param id 12 (tx_rate_aging_time) = 100 | 99 01 00 00 00 00 00 00 01 00 0c 64 00 00 00 | 43 B |
| SET tpc_pow_lvl ctrl(2) level 3 | 0a 07 00 00 00 00 00 00 01 00 02 03 | 40 B |
| GET get_current_ration for 02:00:00:00:00:01 | a5 01 01 00 00 00 00 00 01 00 02 00 00 00 00 01 (30 B) | 58 B |

Carrier frame: HCC group 0 id 2, key 0x0101, token/fields per
h2d_build_g0_cfg_event: 12-byte header {group 0 | hdr[1] = (dst<<4)|0x01 |
hdr[2] = field A | hdr[3] = field B (module/context, default 4) | total | id 2
| byte8 0 | byte9 dst | tag 0x5A5A}, payload = 8-byte token (zero) | key 0x0101
| len | blob | 4-byte inherited pad.  On the bench the field-B module context
that answered the calibration ladder was 4 (the type-1 context-create module)
[V bench, wifi-proto-seq]; the patch keeps 4 as the default and exposes it as
a parameter (section 9).

Expected answer: g1 id3, tag 0x5A5A, payload = cookie0 | cookie1 | u16
syn_id=0x0101 | u16 body_len | body | 4 B pad, body = the request blob with
dev_ret changed (SET) or value filled (GET).  The patch captures exactly that
(section 9).

---

## 5. The rate-control command family (decoded)

Full machine-readable table: TABLES.md and evidence/TABLES-generated.md; the
decode selftest pins the layouts.

### 5.1 The autorate family (401..422), device-processed, blob length from LUT

| id | SET command | GET command | blob | value layout at +10 |
| ---: | --- | --- | ---: | --- |
| 401 | rate_mode | get_rate_mode | 11 | u8 enum: auto=0, fixed=1 (g_ast_alg_cfg_rate_mode_tbl) |
| 402 | fec_coding | get_fec_coding | 11 | u8 enum: bcc=0, ldpc=1 |
| 403 | mu_fec_coding | get_mu_fec_coding | 12 | u8 pkt index @10, u8 enum @11 |
| 404 | protect_mode | get_protect_mode | 14 | 4 x u8 enum: none=0, rts=1, scts=2 |
| 405 | he_ltf_type | get_he_ltf_type | 12 | u8 pkt index @10, u8 enum @11: 1x=0, 2x=1, 4x=2 |
| 406 | dcm_enable | get_dcm_enable | 12 | u8 pkt index @10, u8 enable @11 |
| 407 | freq_bw_mode | get_freq_bw_mode | parser | dedicated parser alg_cfg_args_analysis_freq_bw_mode |
| 408 | debug_log_switch | get_debug_log_switch | 11 | u8 enable/disable |
| 409 | ar_dev_param | get_ar_dev_param | 15 | u8 knob id (0..25) @10, u32 value @11 |
| 410 | tx_cnt | get_tx_cnt | 14 | 4 x u8 counters |
| 411 | hi_rate | get_hi_rate | parser | see 5.4 |
| 412 | he_sigb_rate | get_he_sigb_rate | 11 | u8 MCS enum (g_alg_cfg_he_sigb_rate_mcs_table, 6 rows) |
| 413 | mu_tx_cnt | get_mu_tx_cnt | 11 | u8 counter |
| 414 | mu_rate | get_mu_rate | parser | dedicated parser; value writes u8 pkt @10, u8 user @11?, u8 nss @12 [V writes / I semantics] |
| 415 | mu_rts_mode | get_mu_rts_mode | 11 | u8 enable/disable |
| 416 | legacy_rate | get_legacy_rate | parser | see 5.4 |
| 417 | preamble_type | get_preamble_type | 11 | u8 enum: short=0, long=1 |
| 418 | gi_type (write-only) | - | 11 | u8 enum: normal=0, fixed=1 (g_ast_alg_cfg_gi_type_tbl) |
| 419 | ar_probe (write-only) | - | 11 | u8 enum: normal=0, slow=1 (g_ast_alg_cfg_ar_probe_tbl) |
| 420 | sigb_dcm | get_sigb_dcm | 11 | u8 enable/disable |
| 421 | (read-only) | get_current_ration | 30 | 6-byte MAC @10; GET answer fills the value area with the tx/rx rate report |
| 422 | ar_scen_mode (write-only) | - | 11 | value descriptor is empty in the LUT [V]; layout [U] - do not use until probed |

### 5.2 The TPC family (1801..1809), device-processed, blob length from LUT

| id | SET | GET | blob | value layout at +10 |
| ---: | --- | --- | ---: | --- |
| 1801 | tpc_mode | get_tpc_mode | 11 | u8 numeric (mode) |
| 1802 | tpc_pow_lvl | get_tpc_pow_lvl | 12 | u8 enum: data=0, mag=1, ctrl=2 @10; u8 level @11 |
| 1803 | tpc_code | get_tpc_code | parser | dedicated parser alg_cfg_args_analysis_tpc_code; value writes at +10..+13 [V], exact argument grammar [I]; no response formatter [V] |
| 1804 | pow_mode | get_pow_mode | 11 | u8 enum: init=0, sleep=1, third=2 |
| 1805 | resp_chain_sel | get_resp_chain_sel | 11 | u8 numeric |
| 1806 | set_sta_id | - | parser | dedicated parser, write-only |
| 1807 | far_dist_switch | get_far_dist_switch | parser | dedicated; no response formatter [V] |
| 1808 | near_dist_switch | get_near_dist_switch | parser | dedicated; no response formatter [V] |
| 1809 | waterfilling_mode | get_waterfilling_mode | 11 | u8 numeric |

### 5.3 The 26 autorate tuning knobs (ar_dev_param, id 409)

The firmware's rate-adaptation tunables, name -> knob id
(g_ast_alg_cfg_ar_dev_param_tbl .data+0x344c, 26 rows) [V]:

| id | knob | id | knob |
| ---: | --- | ---: | --- |
| 0 | sudden_good_delta_gdpt_ratio | 13 | rx_rate_aging_time |
| 1 | sudden_bad_delta_gdpt_ratio | 14 | descend_bw_event_trig_per_thrd |
| 2 | descend_protocol_pre_thrd | 15 | probe_update_best_opt_en |
| 3 | 11b_ascend_protocol_per_thrd | 16 | probe_update_best_same_nss_th |
| 4 | 11a_ascend_protocol_per_thrd | 17 | probe_update_best_diff_nss_th |
| 5 | ascend_bw_rate_keep_cnt_thrd | 18 | vht_priv_rate_probe_switch |
| 6 | descend_bw_min_pktcnt_thrd | 19 | ht_priv_2_vht_probe_switch |
| 7 | ascend_bw_probe_pktcnt_min_thrd | 20 | suspect_collision_ppdu_cnt_th |
| 8 | descend_bw_probe_pktcnt_min_thrd | 21 | cfg_vi_spec_per |
| 9 | probe_gi_start_intvl_pktnum | 22 | cfg_vo_spec_per |
| 10 | probe_reduction_strategy | 23 | gi1_with_2xltf_en |
| 11 | probe_per_init_strategy | 24 | descend_bw_gdpt_better_thrd |
| 12 | tx_rate_aging_time | 25 | ascend_bw_gdpt_better_thrd |

These are the vendor algorithm's own knobs: they tune the goodput/PER
thresholds, bandwidth up/down probes, rate aging and GI probes of the firmware
rate controller.  Defaults are firmware-resident (not statically recoverable);
the host can read them back with GET ar_dev_param and is not expected to
change them for a working AP.  The two adjacent switches: ar_probe normal/slow
(id 419) and ar_scen_mode normal/move (id 422, value layout [U]).

### 5.4 Fixed-rate overrides (hi_rate 411, legacy_rate 416)

These are the **static-rate** controls (what the rate mode "fixed"
complements).  Both are dedicated-parser commands: their LUT blob length is 0
and the parser writes the value; the wire length is therefore [U] until
bench-probed (a candidate is 10 + the value writes).  Value writes [V]:

* hi_rate (alg_cfg_param_analysis_hi_rate .text+0x14bab0): argv syntax
  "<pkt_type-name> hi_rate <rank> <protocol> nss<N> mcs<M> <gi>" (5 args; the
  example "ucast_data hi_rate 0 11ac nss1 mcs9 long" in .LC189 matches).
  Value: +10 u8 rank (0..3), +11 u8 protocol (protocol table: 11b=0, 11ag=1,
  11n=2, 11n_green=3, 11ac=4, 11ax_su=8, 11ax_mu=9, 11ax_er_su=10), +12 u8
  nss-1 (0..2 args), +13 u8 mcs (0..32), +14 u8 GI enum (non-HT:
  short=1/long=0; HT+: gi1=0/gi2=1/gi4=2).  Only pkt types ucast_data(0)/
  ndp(9)/ndpa(10)/other_ctrl(12) are accepted [V].
* legacy_rate (alg_cfg_param_analysis_legacy_rate .text+0x14c1f0): value
  +10 u8 pkt_type (0..3 argument), +11 u8 legacy rate enum
  (g_ast_alg_cfg_legacy_rate_tbl: 1M=0, 2M=1, 5.5M=2, 11M=3, 6M=11, 9M=15,
  12M=10, 18M=14, 24M=9, 36M=13, 48M=8, 54M=12), +12 u8 protocol (0 = 11b,
  1 = OFDM; decoded by the parser from the rate value), +13 u8 rank (third
  argument only in the prot_ctrl 3-argument form) [V].
* mu_rate (414): dedicated parser writes u8 at +10 (pkt type 3/4 = mumimo/
  ofdma data), +11/+12 (user/nss) [V writes; grammar I].

---

## 6. The vendor rate adaptation: what exists and what the host must do

### 6.1 The algorithm is in the firmware

Every rate-control host-side object is a config surface only: the host process
table, LUT, parsers and response formatters contain no rate-selection
algorithm (no PER/throughput math), and the two "accepted rate" controls
(hi_rate/legacy_rate) are explicit fixed overrides.  The actual adaptation
(probe/fallback logic over the 26 ar_dev_param knobs) runs in the firmware
(the firmware image carries the handlers; the bench-proven config event is how
the host talks to them). [V for the host side; I for the exact firmware module
boundaries]

### 6.2 The vendor userspace never configures it

Rootfs evidence (recovery-extract): the only wifi calibration script
(/usr/bin/hi5622v100_cal_init.sh) sends the six power/rssi alg set_* commands;
mac80211.sh issues exactly one alg command ("alg \"atf_mode 2\"") and the
usual txpower/frag/rts privates; **no script, binary or hostapd path issues
rate_mode, ar_dev_param, ar_probe, hi_rate or legacy_rate** (grep over the
extracted rootfs).  Also note the stock "iwpriv Hisilicon0 alg ..." scripts
name a priv command (alg) that this module's command table does not carry (the
table has alg_cfg -> wal_hipriv_alg_cfg); the calibration script path is
therefore not proof of the runtime rate path. [V]  Consequence: on a stock
boot the firmware's own rate adaptation is active; the host does nothing.

### 6.3 The minimal deterministic config for our driver

1. Observe first (no write): GET rate_mode (cfg id 401, type GET) and read the
   returned value byte: auto (0) = the firmware's adaptation is active;
   fixed (1) = a static rate mode is in force.
2. Enable adaptation deterministically: SET rate_mode value 0 (auto).  This is
   the one message that names the rate mode.
3. Do not send hi_rate/legacy_rate unless a fixed rate is the experiment: they
   pin static rates and are the "vs static" contrast.
4. Optional tuning: SET ar_dev_param knob values (only with a reason; defaults
   are vendor-tuned) and tpc_pow_lvl for the per-rate power experiments
   (RC-2/RC-3).

Blob length: the LUT length field (11 for rate_mode) is the host contract [V];
that the firmware accepts the same length is the bench check [I] (the response
echoes the blob with dev_ret updated, which is the verification).

---

## 7. Per-rate TX power tables (RC-2 input)

The TPC command family above is the host control; the *tables* are:

* **per-rate power curves** (equipment family): set_2g_power_param (3502,
  104-byte value), set_2g_low_power_param (3503, 104), set_5g_power_param
  (3504, 70), set_5g_low_power_param (3505, 70), set_2g/5g_all_curve_param
  (3508/3509), curve_factor (3510/3511), upc (3512/3513), power_cali
  (3516/3517), power_diff (3519) - all device-processed with the equipment
  parser, 270-byte blob [V, and already wire-proven for 3502..3507 by the
  calibration ladder].  The *values* are per-unit factory calibration
  (local-only, never committed).
* **TPC enums** [V]: tpc_mode (numeric), tpc_pow_lvl mode {data=0, mag=1,
  ctrl=2} + level, pow_mode {init=0, sleep=1, third=2}, resp_chain_sel
  (numeric), waterfilling_mode (numeric), tpc_code protocol {11b=0, ofdm=1}.
* **the report table** g_wal_max_rate_info_table (192 B = 24 x {u32 key, u32
  max-rate}) is the rate->bitrate table used by hmac_config_get_rate_info for
  the WEXT iwconfig rate display (key packing [U]); it is the natural
  formatter for a rate observable but not a control.

Per-rate power *application* (which value lands in which PHY table and when) is
RC-3 territory; this lane records the control surface only.

---

## 8. The rate-selection observables

Ordered by strength (the fire recipe uses 1-3; 4 is optional):

1. **The config-sync answer (g1 id3, syn 0x0101).**  A SET with need_w4=1
   makes the firmware answer with the request blob and its dev_ret (0 =
   accepted).  This is the same contract the calibration ladder proved.
   PASS = one answer whose body[0] == the sent cfg id and body[4..7] == 0.
2. **The GET readback value.**  After the SET, GET rate_mode and decode
   body[10]: 0 = auto.  This is the *rate selection mode* read from the
   firmware itself, i.e. the "vs static" comparison: mode auto vs mode fixed.
3. **GET get_current_ration (id 421) for a MAC.**  The vendor's formatter
   prints tx_phymode/tx_bw/tx_nss/tx_gi/tx_mcs and the rx mirror; the 30-byte
   GET blob carries the 6-byte MAC.  On an AP with a live user this is the
   *selected rate* report.  Without an associated station the firmware may
   answer a dev_ret error (user not exist path) - record it either way; it is
   still an answer to the query.
4. **On-air (optional):** from the existing on-air observer environment, a
   monitor capture of the AP's beacons/mgmt reports the TX rate in radiotap; a
   rate_mode fixed (or a legacy_rate/hi_rate fixed block, once its wire length
   is probed) that changes that field is a direct rate-selection observation.
   This is an experiment for a later session when the AP module carries the alg
   path.

---

## 9. What was delivered

* tools/decode_alg_engine.py - the static decoder (41-check selftest, JSON +
  markdown emission).  Reproduces every table in this report.
* TABLES.md - the curated rates/TPC/value-enum tables;
  evidence/TABLES-generated.md - the generated full dump.
* src/hi5622_rate_ctl_logic.{h,c} - portable, pure C: alg_cfg blob builder,
  config-event frame builder, D2H config-sync parser, strict grammar/value
  tables, fail-closed decision matrix.
* src/rate_ctl_logic_selftest.c - host KATs and matrix tests.
* patches/0001-rate-ctl.patch - the fail-closed driver patch (new module
  surface: default-off rate_ctl parameter, WO attrs rate_mode / rate_mode_get
  / ar_dev_param_set / ar_dev_param_get / tpc_pow_lvl / ration_get, RO
  status/log; D2H sync capture; same SR-node/mailbox gates and one-idea budget
  as the proven H2D path).
* FIRE-RECIPE.md - the attended ladder R0..R3 with stop rules.
* BUILD-NOTES.md - the pinned build chain and artifact identity.
* evidence/ - decode selftest, generated tables, host tests, build.

---

## 10. Limits / unknowns ([U])

* The firmware-side acceptance of the *shorter* rate blobs (11..15 B) is [I]:
  the calibration family proved 270 B; the response echo is the bench check.
* ar_scen_mode (422) and the dedicated-parser wire lengths (hi_rate/
  legacy_rate/mu_rate/tpc_code/freq_bw_mode) are not statically fixed; the
  patch refuses them by default (explicit length parameter required).
* The device-internal relation between rate_mode fixed and a specific rate
  (which rate is chosen when fixed) is [U]; the fixed blocks are the explicit
  route.
* get_current_ration body layout for the answer value area is [I] (derived
  from the output formatter reads); the recipe records the raw bytes.
* The 26 ar_dev_param default values and threshold units are
  firmware-resident [U] (GET reveals them at runtime).

## 11. Provenance / license

Clean-room host analysis.  The vendor module is read in place from the local
recovery extraction and is never committed nor copied into tracked text; the
committed material is original GPL-2.0 code/tools plus derived annotations
(ids, offsets, symbol names, enum names).  No vendor object, firmware byte,
calibration value or per-unit identity is included.
