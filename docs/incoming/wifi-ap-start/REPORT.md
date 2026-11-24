# wifi-ap-start - WAL AP-config cmd 0x0127 body + hostapd start path

**Lane:** wifi-ap-start (shared task **task-121**), **host-only**, 2026-11-26.
**Status:** static reversal + byte-exact compile-verified C builders + provider
extension patch + kernel-context compile + host selftests + attended bench
ladder.  **No frame has been sent to the firmware by this lane; the bench
router was never contacted.**

Deliverables:

| path | contents |
| --- | --- |
| REPORT.md | this report: vendor AP-start sequence, 156-byte body decode, firmware dispatch, builders, ladder, limits |
| [TABLES.md](TABLES.md) | T1-T11 evidence tables |
| [src/hi5622_ap_frames.h](src/hi5622_ap_frames.h) | portable C99 API (both carriers, body builders, sequence order, wire validator) |
| [src/hi5622_ap_frames.c](src/hi5622_ap_frames.c) | implementation (156-byte body filler + g0 id1 / WAL g2 envelopes) |
| [src/ap_frames_selftest.c](src/ap_frames_selftest.c) | 128 device-free known-answer/refusal checks + --emit frame generator |
| [tools/ap_start_re.py](tools/ap_start_re.py) | 63 regenerable static probes against the pinned local firmware + 2.5.24 vendor module |
| [tools/build.sh](tools/build.sh) | host strict compile + selftest + emit |
| [tools/gen_frames_md.py](tools/gen_frames_md.py) | regenerates [recipes/frames.md](recipes/frames.md) with a freshness check |
| [tools/kernel-check.sh](tools/kernel-check.sh) | compile the extension inside the real 6.18.60 module build (zero warnings) |
| [provider/apstart-provider.patch](provider/apstart-provider.patch) | compile-ready extension of the wifi-scan-ap + wifi-vap-start provider tree: caps AP_START, AP-start sequence loop, module param |
| [recipes/ap_start_ladder.json](recipes/ap_start_ladder.json) | data-driven attended ladder with readback pack and stop rules |
| [recipes/frames.md](recipes/frames.md) | generated synthetic bench frames (hex) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact commands and verification results |

Cross-references: [../wifi-vap-start](../wifi-vap-start/) (VAP add/start family,
WAL envelope, mode-1 bench result), [../wifi-scan-ap](../wifi-scan-ap/)
(provider contract, frame-post gate), [../wifi-datapath](../wifi-datapath/)
(firmware config-key table A), [../wifi-scan-d2h](../wifi-scan-d2h/)
(firmware CA = file + 0x40000 convention, D2H catalogue),
[../wifi-vendor-userspace](../wifi-vendor-userspace/) (vendor hostapd ->
WAL flow), [../wifi-proto-seq](../wifi-proto-seq/) (g0 id1 config carrier).

## TL;DR

1. **The vendor AP start is a four-message sequence, not one frame** (host
   module wal_cfg80211_start_ap, .text 0x103634): WAL cfg cmd **0x0005**
   set SSID (36-byte body) -> WAL cfg cmd **0x006D** auth algorithm (4-byte
   body) -> WAL cfg cmd **0x0127** AP config (**156-byte body**) +
   wal_start_vap (WAL cfg cmd **0x0065**, 12-byte body).  The SSID and the
   auth algorithm are *not* inside the 156-byte body.  [V]
2. **The 156-byte body is fully mapped** from the vendor host builder and
   cross-verified against the firmware consumer (table-A worker 0x84DA4):
   +0x00 beacon interval, +0x04 DTIM, +0x08 RSN/WMM-present flag,
   +0x09/+0x49 64-byte RSN / vendor-IE copies, +0x89 hidden SSID,
   +0x8A..+0x8E HT/VHT capability bits + wireless-mode enum, +0x93
   start/change flag, +0x94..+0x98 mobility domain.  [V]
3. **Two carriers exist for the same bodies.**  The vendor host emits
   group-2 WAL cfg events (header id 0).  The firmware *statically* dispatches
   the same command keys through config-key table A at CA 0x10C23C from its
   **group-0 id-1** handler (key at hdr+0x14, body at frame+0x18); the table
   contains 0x0005 -> 0x82878 and 0x0127 -> 0x84DA4 but has **no 0x006D
   row**.  The build therefore emits both: g0_id1_static (default, the
   bench-proven config carrier) and wal_g2_vendor (vendor order, requires
   the dynamically registered group-2 handler).  [V]/[I]
4. **Provider extension is compile-ready**: applied on top of the
   wifi-scan-ap + wifi-vap-start tree it builds the module (770,304 B, sha256
   115119809f103abe753c335e8a386c58987f2578422cc1d359db1d0303992f94) against
   the 6.18.60 tree with **zero warnings**, advertises AP_START, and posts the
   whole sequence through the existing fail-closed hi5622_frame_post() gate
   (allow_frames budget).  [V]
5. **The bench gate is a READ-ONLY firmware readback**, not "hostapd
   started": after the AP-config frame is consumed, the worker writes
   beacon_interval/DTIM into VAP ctx +0x10/+0x14, hidden-SSID into
   ctx+0x18D bit0, mode/HT bits into ctx+0x18C/+0x18D, MD into
   ctx+0x138/+0x13A; the ladder PASS requires those values.  [V] firmware
   worker / [U] unexercised.

## 1. Scope, evidence and pins

Host-only RE + implementation lane.  Local evidence, read in place, never
copied into the repository:

| artifact | sha256 | use |
| --- | --- | --- |
| vendor firmware blob (928,920 B) | 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b | Thumb decode of the dispatch + workers; CA = file + 0x40000 |
| vendor host module 2.5.24 (3,564,728 B) | de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521 | ARM decode of the vendor AP-start builders (relocatable .text offsets; .text sh_offset 0x38) |
| vendor cfg80211 module 2.5.24 | 554cee15c5c154d28f8cbb90b228ce362689b005dce4b0df662c4906a5fb58a0 | read-only cross-check of the cfg80211_ap_settings field order |

Evidence markers follow the project convention: [V] verified in a local
artifact, [V*] hardware-observed in a prior session/ledger, [I] inferred,
[U] unknown.  The vendor module/firmware are local evidence only; only
derived addresses, ids, field offsets and original code appear in tracked
text.

Regenerate every static claim:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/ap_start_re.py --selftest        # 63/63 probes PASS
    $PY tools/ap_start_re.py --tables          # table-A rows for the AP commands
    $PY tools/ap_start_re.py --disasm 0x84DA4  # derived worker disassembly

## 2. The vendor AP-start path (host module, 2.5.24)

wal_cfg80211_start_ap (0x103634, size 0x324) is the registered cfg80211
.start_ap op.  Decoded flow [V]:

1. Validate wiphy/dev/settings; fetch the vendor VAP object through the netdev
   and its VAP id.
2. Validate ssid_len in 1..32 and the SSID pointer; copy the SSID to a local
   32-byte buffer; wal_send_set_ssid_event(dev, vap_id, ssid_len, ssid) ->
   **WAL cfg cmd 0x0005, body 36** {u8 len, u8 pad[2], ssid[33]}, cfg-event
   size 40 (64 B wire for the g0 carrier / 68 B for the WAL envelope).
3. Read the auth-algorithms field, validate it against {0,1,4,8} (exactly
   nl80211_auth_type OPEN=0 / SHARED=1 / SAE=4 / AUTOMATIC=8), call
   wal_cfg80211_set_auth_mode(dev, auth) -> **WAL cfg cmd 0x006D, body 4**
   {u32 auth}, cfg-event size 8 (36 B wire).
4. Zero a 156-byte stack body; set +0x00 beacon_interval and +0x04
   dtim_period (the debug string says "beacon_interval=%d, dtim_period=%d,
   hidden_ssid=%d!"), +0x89 = (hidden_ssid == ZERO_LEN), fill the PHY/IE/RSN
   fields via wal_cfg80211_fill_beacon_param (0x101774), set +0x93 = 1, wrap
   the body as {u16 cmd 0x0127; u16 body_len 156; body} and call
   wal_cfg80211_start_ap_write_msg (0x103548).
5. _write_msg copies the 156-byte body behind the 4-byte cmd/len header,
   posts a **160-byte WAL cfg event** (cmd 0x0127) with wait, then calls
   wal_start_vap -> **WAL cfg cmd 0x0065, body 12** {u8 start=1, ...,
   u32 cookie}, cfg-event size 16 (44 B wire), and waits for the firmware
   response.
6. wal_cfg80211_change_beacon (0x1018A8/0x101AAC) reuses the same cmd 0x0127
   body with +0x93 = 0 (set-beacon instead of start-beacon).

wal_cfg80211_fill_beacon_param (0x101774) fills the PHY/IE part from
cfg80211_beacon_data [V]:

| source (vendor) | target body | rule |
| --- | --- | --- |
| beacon head capability bit 0x10 (privacy) | +0x08 | 1 gates the two IE copies; 0 leaves both slots zero |
| beacon tail IE 0x30 (RSN) | +0x09..+0x48 | copy whole IE, max 64 B, refuse if larger |
| beacon tail vendor IE (OUI 00:50:f2, WMM) | +0x49..+0x88 | copy whole IE, max 64 B, refuse if larger |
| beacon head SSID IE length == 0 | +0x89 | hidden flag |
| HT cap IE (0x2D) payload byte 0 | +0x8A/+0x8B | bit5 -> +0x8A always; bit6 -> +0x8B only for the non-2.4 band hint |
| VHT cap IE (0xBF) payload byte 0 | +0x8C/+0x8D | bit5/bit6 only for the 5 GHz band hint |
| VHT cap / HT cap / ext IE presence | +0x8E | mode enum: 10 ext (HE), 6 VHT, 5 HT, else legacy from rate-IEs (4 -> 1, 8 -> 2) |
| MD IE (0x36, len 3) payload | +0x94..+0x98 | MDID u16, present, capability bit0/bit1 |
| (start vs change) | +0x93 | 1 for start_ap, 0 for change_beacon |

wal_parse_ht_vht_ie (0x1016FC/0x1076FC), wal_parse_md_ie (0xFFFF4) and
wal_cfg80211_beacon_info_temp_memcpy (0x101628) are the helpers behind the
table; wal_parse_md_ie refuses a non-3 length (no guessing).

The status fields named by the vendor are the strongest textual anchors [V]:
"beacon_interval=%d, dtim_period=%d, hidden_ssid=%d!", "en_auth_algs
error[%d]", "wal_cfg80211_start_ap::failed to start addset beacon, error[%d]",
"wal_send_set_ssid_event::fail to send ssid cfg msg!".

## 3. Firmware dispatch: static table A vs the WAL group-2 path

The vendor host sends the AP config as a **group-2 WAL cfg event (HCC header
id 0)**.  The firmware's *static* H2D receive tables (extracted catalogue)
have **no group-2 id-0 slot**; the group-2 WAL handler is therefore expected
to be registered dynamically at boot (open [U], consistent with the
wifi-vap-start bench finding that mode-0 WAL frames were consumed but had no
firmware effect).

Independently, the firmware has a **static group-0 id-1 config-event
handler** at CA 0x43D58 that dispatches on the u16 key at hdr+0x14
(payload+8), finds the worker in config-key table A at CA 0x10C23C
(217 x 8-byte rows {u16 key; u16 pad; u32 worker}), then calls

    worker(module_ctx, body_len_byte, body_ptr = frame + 0x18)

with module_ctx from the hdr[3] chip/module lookup and the body length from
frame+0x16.  This is the same carrier that the *bench-proven* C2
configuration frames ride (ctx-create key 0x0064, TXQ sync 0x00D8), and it
is why this lane's default carrier is g0 id1:

| table-A row | key | worker CA | meaning |
| ---: | ---: | ---: | --- |
| 1 | 0x0064 | 0x449E4 | module/VAP add (mode-1 VAP base; bench-proven) |
| 2 | 0x0065 | 0x46394 | VAP start (bench-proven) |
| 18 | 0x0005 | 0x82878 | **set SSID**: copies body[3..] body[0] bytes into VAP ctx +0x190 -> +0x24 (33-byte slot) |
| 110 | 0x0127 | 0x84DA4 | **AP config**: consumes the 156-byte body (below) |
| - | 0x006D | none | auth has **no static key**; the G0 carrier omits it (WPA2 security rides the RSN IE) |

The AP-config worker 0x84DA4 was decoded end-to-end [V]; it consumes exactly
the body layout of section 2:

| body offset | worker action |
| --- | --- |
| +0x00 u32 | store into VAP ctx (+0x190 target) +0x10 (beacon interval) |
| +0x04 u32 | store into VAP ctx +0x14 (DTIM) |
| +0x08 | HT/RSN mode branch: 0 -> simple VAP setup, nonzero -> RSN/vendor-IE path |
| +0x09 == 0x30 | RSN element check (element id 0x30 = 48) |
| +0x49 == 0xDD | vendor (WMM) element check |
| +0x89 | hidden-SSID bit -> ctx +0x18D bit0 |
| +0x8A | -> VAP +0x34C bit3 |
| +0x8B | passed to the firmware 20/40 helper CA 0xFDDF8 |
| +0x8C/+0x8D | -> VAP +0x360 bits 2/3 |
| +0x8E | wireless-mode enum passed to CA 0xD4048 |
| +0x94 u16 | -> VAP +0x13A (MDID) |
| +0x96/+0x97/+0x98 | -> VAP +0x138 bits 0/1/2 |
| +0x8F..0x92, +0x99..0x9B | **no firmware reader** (reserved, emitted zero) |
| +0x93 | **no firmware reader in this build** (vendor start/change flag); emitted 1 for start_ap, 0 for change_beacon |

After the config the worker calls the MAC VAP start path (CA 0x94518), which
is the firmware effect the ladder reads back.

## 4. The builders

src/hi5622_ap_frames.{h,c} implement, in pure C99 (host + kernel):

* body builders: 156-byte AP config (section 2/3 layout), 36-byte set-SSID,
  4-byte auth (allowlist {0,1,4,8}), 12-byte VAP start (flag 1, cookie 0);
* wire builders for both carriers - g0 id1 config events (total = 28 +
  body_len; token[8] + key u16 at frame+0x14 + body-len u16 at frame+0x16 +
  body at frame+0x18 + inherited 0xFF pad) and wal g2 vendor envelopes
  (total = 32 + body_len; cookie at +0x0C, WAL id 1 at +0x10, data_len at
  +0x16, seq at +0x18, cmd at +0x1C, body_len at +0x1E, body at +0x20);
* the ordered sequences: g0_id1_static = [ap_ssid, ap_cfg]; wal_g2_vendor =
  [ap_ssid, ap_auth, ap_cfg, ap_vap_start];
* a strict structural validator (haf_validate_wire) that re-checks
  header/group/id/total/tag/key/cmd/body-length/pad on every emitted frame;
* refusals for malformed input: empty/oversized SSID, head shorter than the
  36-byte fixed part, oversized tail, auth value outside the vendor
  allowlist, RSN/vendor IE that would not fit the 64-byte slot, malformed MD
  IE, bad carrier/module/dst, short buffers.

Frame sizes: ap_ssid 64 B (g0) / 68 B (WAL), ap_auth 36 B (WAL only), ap_cfg
184 B (g0) / 188 B (WAL), ap_vap_start 44 B (WAL only).

Host selftest: **128/128 PASS**, including known-answer checks for every
decoded body offset/envelope field plus mutation refusals.  The emitted
frames are in [recipes/frames.md](recipes/frames.md) (synthetic, regenerated
by tools/gen_frames_md.py, freshness-checked).

## 5. Provider extension (compile-ready)

[provider/apstart-provider.patch](provider/apstart-provider.patch) applies on
top of the wifi-scan-ap src tree **plus** the wifi-vap-start provider patch
(the mode-1 bench module base) and:

* extends hi5622_ap_start_params with frame_index/band/module/vap_id/
  hidden_ssid/auth_algs and documents the sequence contract on
  build_ap_start (-ENODATA ends the sequence);
* rewrites hi5622_scan_op_start_ap to post every provider frame through
  hi5622_frame_post() in order, labels them ap_ssid/ap_auth/ap_cfg/
  ap_vap_start, aborts on the first refusal (no partial AP start), and
  refuses a provider that emits nothing;
* adds the provider callback + module param ap_start_mode = 0 (g0 id1 static
  keys, default) | 1 (WAL g2 vendor order), filling the neutral AP params
  from the cfg80211 settings (band, hidden_ssid, auth_type, beacon head/tail,
  SSID);
* advertises HI5622_FRAME_CAP_AP_START, and adds the library object to
  Kbuild.

Verified by [tools/kernel-check.sh](tools/kernel-check.sh) against the real
6.18.60 tree: **hi5622_wifi.ko 770,304 B, sha256
115119809f103abe753c335e8a386c58987f2578422cc1d359db1d0303992f94, zero
compiler/modpost warnings**, 14 haf_ symbols, vapscan_ap_start present.  The
module is local evidence only and is not committed.

No byte reaches the wire unless allow_frames > 0; the sequence posts at most
4 frames per start_ap and every frame faces the inherited
transport/firmware-handler validation.

## 6. Bench ladder (attended; data in recipes/ap_start_ladder.json)

Preconditions: cfg80211 bench image + PD4 golden boot (page
0x86807FEF/7/2/0xABCD, state 1), recovery door armed, UART capture running,
bench link PASS.  Modules: cfg80211.ko + the patched clean-room module with
allow_frames=6 frame_gap_ms=100 h2d_send_budget=8 vap_start_first=1
vap_start_mode=1 vap_start_module=0 ap_start_mode=0.

| step | action | PASS evidence (read-only) |
| --- | --- | --- |
| B0 | golden readback pack | page/state, mask 0, staging 0 |
| C2 | proven calibration boot | frames consumed, g1 id3 responses, mask 0 |
| V1 | mode-1 VAP base (iw ... interface add wlan0, g0 id1 0x64 + 0x65) | frames consumed; module-table slot 0 and ctx0+0x190 nonzero |
| A1 | hostapd-open start -> frame 0 ap_ssid (g0 id1 0x0005, 64 B) | CONSUMED, mask 0 |
| A2 | same start_ap -> frame 1 ap_cfg (g0 id1 0x0127, 184 B) | CONSUMED, mask 0, **readback vap+0x10 == 100, vap+0x14 == 2, ctx+0x18C/0x18D changed** |
| A3 | hostapd stays up; iw dev wlan0 info | type AP, no exception, heartbeats |
| A4 | optional WAL carrier rung (ap_start_mode=1) | all 4 frames consumed, same readback; otherwise the static carrier stays the supported path |

Readback pack (BAR0 = device CA + 0x6B8000 for device-internal RAM): golden
page 0x829E28 (CA 0x170E28), module-table slot 0 = 0x829458, base offset u16
= 0x82A504, ctx0 = u32(slot0) + u16(base), VAP pointer = u32(ctx0+0x190);
the AP config worker writes VAP +0x10/+0x14 (beacon interval/DTIM), +0x64
flags, +0x138/+0x13A (MD) and ctx +0x18C/+0x18D.  Dereferencing the VAP
pointer with the +0x6B8000 window is [I] until the bench confirms it (the
non-deref gates ctx+0x18C/0x18D are enough for a first PASS).

**Claim rules:** a consumed frame is not an AP.  PASS(A2) needs the firmware
readback match; beaconing over the air is out of scope for a host-only lane
and is not observable from the same radio.  hostapd reaching start_ap is a
host-side fact only.

Stop rules: exception mask non-zero or a nonzero staging frame; golden
page/state or g+0xB4/+0xB8 change; a frame not consumed inside the bounded
poll (refused/timeout increments); any dmesg fault; H2D lane closed.  On a
stop: record mask/out[]/rd_catalog/readback/dmesg, power-cycle, analyse
offline - never re-send in the same boot.  Never write CA 0x400392F0, never
post mailbox id 5, never send group 1/4.

## 7. Expected D2H / observables

* WAL cfg events with the vendor's wait flag are acknowledged with the
  cookie echoed in a D2H frame; our provider posts fire-and-forget and reads
  the catalogue instead.  The g1 id3 config-sync family is the generic
  per-event response channel (syn_id dispatch, see wifi-proto-seq) - after
  AP-config the response may be a per-VAP MIB sync (syn_id 0x07xx).  The
  exact AP-start response syn_id remains [U]; the ladder does not require
  it.
* **g3 id19** (hmac_receive_beacon_probrsp_to_reprot) and **g3 id20**
  (hmac_report_beacon_frame) are D2H beacon reports.  They are the
  *received*-beacon report family (scan/STA side), not proof of our AP
  beaconing; an AP that starts beaconing is not reported back to the host on
  this firmware [I].
* The decisive observable is therefore the **firmware VAP context readback**
  (section 6), plus heartbeat continuity and a clean exception mask.

## 8. Open items

| # | item | status |
| ---: | --- | --- |
| 1 | group-2 WAL dynamic handler registration at boot (why mode-0 frames had no effect) | [U] |
| 2 | exact D2H acknowledgement for each WAL cfg cmd (cookie echo frame family/syn_id) | [U] |
| 3 | auth command 0x006D static key: absent from table A; auth is covered by the RSN IE for WPA2 | [U]/[I] |
| 4 | +0x93 start/change flag consumer in this firmware build | [U] (no reader found in worker 0x84DA4) |
| 5 | VAP pointer dereference window for the +0x10/+0x14 readback | [I] until the bench confirms |
| 6 | keys (add_key) and the data plane: separate lanes; start_ap only starts the VAP | not this lane |
| 7 | hardware validation of the whole sequence | not done - host-only lane |

## 9. Limits and publication boundary

* Host-only: every frame is derived statically and compile-checked; none has
  been sent to the firmware.  The ladder is readback-gated and fail-closed.
* The vendor firmware and host modules are local evidence, never committed:
  only derived addresses/ids/field offsets and original C appear here.  The
  KAT vectors are synthetic, not vendor captures.
* The PD4-patched firmware is a research artifact; these are ordinary
  post-ready H2D messages, equally valid on a vendor-firmware golden boot.
* Upstreamability: this is research code for the bench; the firmware is
  closed and the PD4 patch / vendor firmware path stays unsupportable
  upstream (see ../../ROADMAP-OPENWRT.md).

## 10. Reproduction

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ap-start
    ./tools/build.sh                       # 128/128 host checks + emit
    $PY tools/ap_start_re.py --selftest    # 63/63 static probes PASS
    $PY tools/gen_frames_md.py --check     # frames.md current
    ./tools/kernel-check.sh                # module build 770,304 B, 0 warnings
