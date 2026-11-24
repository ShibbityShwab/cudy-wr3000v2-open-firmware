# wifi-beacon-static — beacon / AP data-path reversal and host completion

**Lane:** wifi-beacon-static (shared task task-140).  **Written:** 2026-11-24.
**Status:** static reversal complete; patch host-built + compile-verified on the
6.18.60 tree; **NOT FIRED** (no bench contact by this lane).  The attended
ladder is in [LADDER-BEACON.md](LADDER-BEACON.md); the Lead fires it.

Evidence markers follow the project convention: [V] verified in a local
artifact, [V*] hardware-observed in a prior (Lead) session, [I] inference,
[U] unknown.

## TL;DR

1. **The vendor AP start is FIVE ordered messages, not four** (host module
   2.5.24 and 2.4.15, both reversed): *module channel config* (WAL cfg command
   **0x0124**) → set SSID (**0x0005**) → auth (**0x006D**) → AP config
   (**0x0127**) → VAP start (**0x0065**).  The channel config is the FIRST
   call in `wal_cfg80211_start_ap`; the AP config is sent BEFORE the VAP start
   inside `wal_cfg80211_start_ap_write_msg`.  [V]
2. **The answer to the task's question is NO: the host does not post a beacon
   template and there is no bcn ring message in this design.**  The firmware
   SMAC owns beacon generation and TBTT transmission: the firmware image
   carries `smac_tbtt_2g_isr`/`smac_tbtt_5g_isr` and the
   `smac_msg_proc_beacon_tx_suspend_req`/`_resume_req` handler names, and it
   has no fixed 802.11 beacon template (the frame is assembled at runtime from
   the VAP context).  The host AP path carries only a 156-byte parameter body
   (beacon interval, DTIM, RSN IE copy, WMM/vendor-IE copy, HT/VHT capability
   bits, wireless-mode enum, hidden-SSID, mobility domain).  [V]
3. **The clean-room bench path leaves the firmware AP/channel context
   unconfigured when the AP starts.**  It (a) never sends the module channel
   config, and (b) posts the mode-1 VAP base = *user_add + VAP start* BEFORE
   `ap_ssid`/`ap_cfg`, whereas the vendor starts the VAP AFTER the AP config.
   The bench evidence so far is hostapd "AP-ENABLED" plus consumed frames — a
   host-side fact, not an on-air fact.  This is the static candidate for the
   missing beacons.  [V]/[V*]/[I]
4. **Fail-closed patch** (`beacon_static=0` default = the bench-proven path
   unchanged; `beacon_static=1` = vendor order): provider capability
   `CHAN_CFG` + channel frame builder (g0 id1 key 0x0124 / WAL cmd 0x0124,
   4-byte body), pure ordering plan, and the ordered execution *user_add →
   channel → SSID → AP config → VAP start* with a read-only module-context
   channel readback line.  Module: 1,002,872 B / stripped 275,304 B, zero
   warnings, vermagic 6.18.60, cfg80211 imports unchanged (6), strip .text
   identical.  [V]
5. **Verification split:** the host-side PASS is the module-context channel
   readback (`ctx+0x10` packed channel/band/field/valid); the decisive on-air
   PASS needs an **external observer** (phone/laptop scan or a second monitor
   radio).  Our own RX capture is 0 and the g3 id19/20 D2H events are
   *received*-beacon reports (scan/STA side), not proof of our AP beaconing —
   this matches the wifi-ap-start lane's §7 conclusion.  [V*]/[I]

## 1. Scope, evidence and pins

Host-only RE + implementation lane.  Local evidence read in place, never copied
into the repository:

| artifact | sha256 | use |
| --- | --- | --- |
| vendor firmware blob 928,920 B (local research copy, PD4 variant; AP-family code/table regions are identical to the wifi-ap-start lane's stock pin 7fc87e20) | cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1 | firmware worker/table decode; CA = file + 0x40000 |
| vendor host module 2.5.24 (3,564,728 B) | de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521 | AP-start sequence disassembly (relocatable .text offsets) |
| vendor host module 2.4.15 (3,581,748 B) | 22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2 | independent second version (same sequence, different addresses) |

Regenerate every static claim with `tools/beacon_re.py` (33 probes; see
[evidence/](evidence/)).  The firmwares/host modules are local evidence only;
tracked text carries derived addresses/ids/offsets and original C.

## 2. The vendor host AP-start path (both versions)

`wal_cfg80211_start_ap` is a strict order; the channel helper is a local
(unnamed) function between `wal_cfg80211_chan_dfs_check_resume` and
`wal_cfg80211_start_ap_write_msg`:

| step | 2.5.24 .text | 2.4.15 .text | evidence |
| --- | --- | --- | --- |
| `wal_cfg80211_start_ap` | 0x103634 | 0x10342c | H1 |
| call 1: channel helper (direct branch) | 0x102e84 (called at 0x10368c) | 0x102c7c (called at 0x103484) | H1d/H1e |
| call 2: `wal_send_set_ssid_event` | 0x1284ec | 0x1280ac | H1a |
| call 3: `wal_cfg80211_set_auth_mode` | 0x101584 | 0x10137c | H1b |
| call 4: `wal_cfg80211_start_ap_write_msg` | 0x103548 | 0x103340 | H1c |
| write_msg: AP config cfg event (cmd 0x0127) + wait | 0x103594 | 0x10338c | H3a/H3c |
| write_msg: `wal_start_vap` (cmd 0x0065) + wait | 0x1035a4 | 0x10339c | H3b/H3c |

The channel helper is the **channel-info builder** `wal_cfg80211_set_channel_info`
(static; identified by its own log strings, H2b–H2d) → `wal_cfg80211_fill_channel_param`
→ `wal_cfg80211_do_chan_set`, which calls `wal_send_cfg_event` (H2a) with a
**4-byte body**.  The vendor AP start is therefore: channel → SSID → auth →
AP config → VAP start.

### 2.1 Channel-config body (4 B) [V]

The body is written from the cfg80211 chandef and read back by the firmware
channel worker (CA 0x841B8); field map (both sides agree):

| off | host source | firmware worker store | meaning |
| ---: | --- | --- | --- |
| +0 | `chandef.chan->band` (u32 ldrb) | module ctx +0x11 | band (0 = 2.4 GHz, 1 = 5 GHz) |
| +1 | vendor `mac_vap+2` [I] | module ctx +0x12 | vendor per-VAP field; 0 for our single AP [I] |
| +2 | `chandef.chan->hw_value` | module ctx +0x10 | channel number |
| +3 | not written by the host | unused by the worker | — |

The worker then calls the PHY/band channel program (CA 0xFDCE4 family) and
stores a channel-valid flag at module ctx +0x13.  A zero module-context channel
(the clean-room path today) is a channel 0/band 0 configuration.

## 3. Firmware side: dispatchers and the beacon engine

### 3.1 Config-key table A rows for the whole AP-start family [V]

Table A at CA 0x10C23C (217 × 8-byte rows `{u16 key; u16 pad; u32 worker|thumb}`):

| key | worker CA | role |
| ---: | ---: | --- |
| 0x0064 | 0x449e4 | module/VAP context add (mode-1 base) |
| 0x0065 | 0x46394 | VAP start |
| 0x0005 | 0x82878 | set SSID |
| **0x0124** | **0x841b8** | **module channel config (vendor's FIRST AP-start message)** |
| 0x0127 | 0x84da4 | AP config (156-byte body) |

All five have static g0 id1 config-key slots, which is why the clean-room g0
carrier can carry the whole vendor sequence.

### 3.2 Worker field maps (probe F2–F4) [V]

* **channel worker CA 0x841B8** (F2): validates (band, channel) via helper CA
  0x10172C → ctx +0x13; stores body[2]→ctx+0x10, body[0]→ctx+0x11,
  body[1]→ctx+0x12; first action is a VAP-object null check
  (module ctx +0x4B0).  Then PHY channel program (0xFDCE4), 0xF4FD8,
  0x40684 and (if a flag at +0x1C8) 0x97008.
* **AP-config worker CA 0x84DA4** (F3): writes VAP ctx (+0x10 interval,
  +0x14 DTIM, IE slots, HT/VHT/MD fields — mapped by the wifi-ap-start lane),
  then calls the MAC/VAP start path **CA 0xD41AC → 0x94518 → 0x961AE**
  (bytes at 0x84F06..).  The AP config is a *parameter* message, not a beacon
  frame.
* **VAP-start worker CA 0x46394** (F4): body[1] → protocol arg of CA 0xD4048
  (`mac_vap_init_by_protocol` family), body[2]→ctx+0x11, body[3]→ctx+0x12,
  body[4..7]→ctx+0x444; then the VAP state machine (VAP object from
  ctx+0x190, user/peer table setup CA 0xBF128, rate/band helpers, 0x5E95C /
  0x971D8 / 0x5F450).  This is the message that actually starts the VAP; the
  vendor sends it **after** the AP config.

### 3.3 The firmware beacons; the host does not supply the frame [V]

* The firmware image contains the SMAC TBTT ISRs (`smac_tbtt_2g_isr`,
  `smac_tbtt_5g_isr`) and the beacon-TX suspend/resume request handlers
  (`smac_msg_proc_beacon_tx_suspend_req`, `..._resume_req`) — the beacon
  engine and the TBTT timer live in the device firmware (probe F5).
* The firmware image has **no fixed 802.11 beacon template** (frame control
  0x0080 + broadcast DA) — the beacon frame is assembled at runtime from the
  VAP context (probe F6).
* The vendor host's cfg80211 AP path (`start_ap`/`change_beacon`) carries no
  beacon frame: the 156-byte AP-config body holds only the RSN/vendor-IE
  copies, HT/VHT capability bits, wireless-mode enum, hidden-SSID flag and
  mobility-domain fields.  The beacon head/tail from cfg80211 are only
  *parsed* for those IEs (wifi-ap-start lane's `wal_cfg80211_fill_beacon_param`
  map).  There is no bcn-ring/bcn-table message in the start path; host-side
  "beacon" symbols are received-beacon report code
  (`hmac_report_beacon_frame`, `hmac_receive_beacon_probrsp_to_reprot`) and
  user-space config handlers.
* **Conclusion:** beacons are generated and transmitted autonomously by the
  firmware after a correct VAP start; the host's job is the parameter/state
  sequence.  A "missing beacon template message" is **not** the blocker.

## 4. What the clean-room bench path is missing (static)

The bench-proven path (wifi-ap-start v7 + wifi-stability) posts:

    [user_add, VAP start]   (mode-1 base, iftype AP, once per load)
    [ap_ssid, ap_cfg]       (g0 keys 0x0005, 0x0127)

while the vendor posts:

    user_add  ... (at interface creation)
    channel 0x0124 -> SSID 0x0005 -> auth 0x006D -> AP config 0x0127 -> VAP start 0x0065

Two host-side gaps follow:

1. **No module channel config.**  Module ctx +0x10..+0x13 (channel/band/
   field/valid) are never programmed; the firmware AP is expected to start its
   beacon engine on a configured module channel.
2. **VAP start ordering.**  The clean-room VAP start is posted before the AP
   config, i.e. before the VAP context holds the beacon interval/DTIM/IE slots.
   The vendor starts the VAP only after those parameters exist.

Both are consistent with the bench observation set: the frames are consumed,
hostapd reaches AP-ENABLED, and the firmware takes no exception — but "AP
enabled" is a host/cfg80211 fact, while the SMAC beacon engine was never given
the configured channel/state in the right order.  This is an inference from
static evidence ([I]); the ladder decides it.

Auth (0x006D) has no static g0 key in table A, so the g0 carrier omits it (WPA2
security rides the RSN IE copy; already documented by the wifi-ap-start lane).
It is not a beaconing input [I].

## 5. The patch (fail-closed, default off)

[patches/0001-beacon-static.patch](patches/0001-beacon-static.patch) applies on
top of the wifi-stability final combined tree (stripped pin 269,688 B sha256
b5dfb883...).  It touches 7 files:

| file | change |
| --- | --- |
| `hi5622_frames_api.h` | new cap `HI5622_FRAME_CAP_CHAN_CFG` (bit 9), `struct hi5622_chan_cfg_params`, `build_chan_cfg` op, `channel` fields for AP/vap-start params |
| `hi5622_ap_frames.{c,h}` | 4-byte channel body builder + `haf_build_chan_frame` (g0 key / WAL cmd 0x0124) |
| `hi5622_scan_logic.{c,h}` | cap name + contract validation; pure ordered plan `hi5622_beacon_plan()` (host-tested) |
| `hi5622_vapscan_provider.c` | channel builder callback + capability; channel byte in the g0 VAP-start body |
| `hi5622_scan.c` | `beacon_static` (default **0**) / `beacon_vap_start` (default 1) params; ordered execution (user_add → channel → AP sequence → VAP start) with bounded VAP-object wait, dirty latch on abort, read-only module-ctx channel readback log |

Order when `beacon_static=1`:

    user_add (0x0064) -> channel (0x0124) -> SSID (0x0005) -> AP config (0x0127) -> VAP start (0x0065)

`beacon_static=0` (compiled default) executes the exact bench-proven legacy
path; the new code is behind the parameter and every new frame goes through the
same fail-closed `hi5622_frame_post()` gate (`allow_frames` budget).  WAL
carrier (`ap_start_mode=1`) already ends its own sequence with the VAP start
after the AP config, so the extra step is skipped and only the channel frame is
added.

## 6. Host verification

| check | result | evidence |
| --- | --- | --- |
| strict C99 host compile of the patched portable sources (`-Wall -Wextra -Werror -pedantic`) | PASS | evidence/host-selftest.txt |
| `beacon_selftest` (channel-frame KAT g0+WAL, refusals, unchanged default sequences, plan order/truncation, cap contract, channel bounds) | **65/65 PASS** | evidence/host-selftest.txt |
| static-RE probes, 2.5.24 host module + firmware | **33/33 PASS** | evidence/beacon-re-2.5.24.txt |
| static-RE probes, 2.4.15 host module + firmware | **33/33 PASS** | evidence/beacon-re-2.4.15.txt |
| module compile-verify, 6.18.60 tree, zero warnings/errors | PASS | evidence/module-build.txt |
| module identity | unstripped 1,002,872 B sha256 320ad838b9d4…, stripped 275,304 B sha256 e633f847cff8…, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, depends=cfg80211, 6 cfg80211 imports | evidence/module-build.txt |
| strip .text identity | PASS (.text byte-identical after `--strip-debug`) | evidence/module-build.txt |
| fail-closed defaults | `beacon_static=0` (no initializer) and `beacon_vap_start=1` asserted by build-host static gates | evidence/host-selftest.txt |

The module is **NOT fired**; `/tmp/wifi-stability-artifact`-style .ko artifacts
stay local and are never committed.

## 7. Bench verification plan (design only — see LADDER-BEACON.md)

* **B1 control rung** (`beacon_static=1 beacon_vap_start=0`): channel frame only,
  proves the 0x0124 frame is consumed and honored.  PASS = plan log shows 4
  steps, `ap_chan` posted, AP-ENABLED, and the new readback line shows
  `module_ctx+0x10 u32` with channel/band matching the hostapd channel, valid
  byte non-zero, page/mask clean.
* **B2 full vendor order** (`beacon_static=1 beacon_vap_start=1`): plus VAP
  start after `ap_cfg`.  Host PASS as B1; **on-air PASS requires an external
  observer** (phone/laptop Wi-Fi scan sees the SSID on the configured channel;
  a second monitor radio captures the beacon at ~100 TU with the VAP BSSID).
* **B3 WPA2 rung**: same order with the WPA2 hostapd fixture; the observer
  checks the privacy bit + RSN IE in the beacon (station association is still
  blocked on the M3 add_key path, not this lane).
* Stop rules: exception mask non-zero, golden page/state lost, H2D lane closed,
  frame not consumed, or a VAP-object readback still zero after the bounded
  wait → record dmesg, power-cycle, analyse offline.  Never re-post in the same
  boot; never write CA 0x400392F0; never post mailbox id 5; never send group 1/4.

## 8. Open items / evidence levels

| # | item | status |
| ---: | --- | --- |
| 1 | Is the missing module channel context the actual reason no beacons were observed? | [I] — ladder B1/B2 decides |
| 2 | Does the firmware require the VAP start strictly after the AP config (vs. tolerate the pre-start)? | [I] — B2 decides |
| 3 | Channel body byte +1 (vendor `mac_vap+2`) semantics | [I] — sent 0 (single VAP); worker stores it only |
| 4 | Channel-valid flag at module ctx +0x13 exact value | [U] — readback logs the byte; PASS needs non-zero only |
| 5 | g3 id19/20 as our-AP beacon proof | NOT usable: received-beacon report family [I] (same conclusion as wifi-ap-start §7) |
| 6 | 5 GHz endpoint channel config (second wiphy) | not this lane; the same 0x0124 path applies with band=1 [I] |
| 7 | On-air proof | needs external observer (our RX path capture is 0) [V*] |

## 9. Publication boundary

Host-only.  No vendor firmware/object bytes, no calibration, no secrets are
committed; only derived addresses, ids, field offsets and original C.  The
patch is research code for the bench; the vendor firmware path remains
unsupportable upstream (see ../../ROADMAP-OPENWRT.md).  The ladder is a design
artifact, not a fired result.

## 10. Reproduction

    cd opensource/docs/incoming/wifi-beacon-static
    ./tools/build-host.sh          # 65/65 host checks, static fail-closed gates
    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/beacon_re.py --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin                            --wifi-ko /tmp/bootkick/hi5622v100_wifi.ko    # 2.5.24
    $PY tools/beacon_re.py --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin                            --wifi-ko /tmp/vmods/hi5622v100_wifi.ko       # 2.4.15
    ./tools/build-module.sh        # heavy: 6.18.60 compile-verify, identity
