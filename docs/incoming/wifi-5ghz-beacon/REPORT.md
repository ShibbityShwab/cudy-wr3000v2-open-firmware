# wifi-5ghz-beacon - why the 5 GHz AP is AP-ENABLED but silent, the vendor decode, and the fail-closed channel-activation candidate

**Lane:** wifi-5ghz-beacon (shared task task-175). **HOST-ONLY**: this lane
disassembled the local vendor host module and built host artifacts; it never
contacted the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The 5 GHz AP reaches AP-ENABLED with the full vendor
message order (vap_add -> deferred user_add -> ap_chan 0x0124 -> ap_ssid
0x0005 -> ap_cfg 0x0127 -> vap_start 0x0065 -> "vendor AP-start order
complete") and the page stays GOLDEN:1 / mask 0 (chip healthy), yet the
laptop's scans see neighbour BSS and not `cudy-ap-test-5g`. The 2.4 GHz AP
with the same driver does beacon (-32 dBm on the bench). The E2 fire's two
readback lines ("firmware VAP object readback=ZERO after 100 ms", "module
channel ctx readback ... band=84 ... valid=0") were the only firmware-side
evidence; this lane re-derived every host-side byte and audited those reads.

**Source artifacts (local, never committed):**

| artifact | sha256 / size | use |
| --- | --- | --- |
| vendor host module `hi5622v100_wifi.ko` 2.5.24 (not stripped) | `de78ec07...`, 3,564,728 B | primary disassembly (objdump 2.46.1, `-dr`) |
| vendor host module `hi5622v100_wifi.ko` 2.4.15 | `22fa789a...`, 3,581,748 B | the Lead's cited offset set; cross-check |
| the E2 module under bench test | `13e7bf4c...2cc1abf`, 353,652 B, 58 params | base for the patch |
| firmware PD4 image | local, uncommitted | cited only through the committed firmware lanes |

The reproduction probe is `tools/beacon5g_re.py` (55/55 PASS on both vendor
builds, `evidence/re-probes-2.5.24.txt` / `-2.4.15.txt`). All vendor module
offsets below are link-time `.text` offsets of that .ko, never a copy.

---

## 1. Result in one paragraph

The vendor host module has **no band-activation message and no band field that
our five AP-start frames miss**: the vendor AP start is exactly the same five
frames, its set-SSID second argument is the **VAP index** (not the band, as an
earlier lane read it), the 0x0124 body carries `{band, chip id, channel}`
(our driver already sends `{band, vap_id=0, channel}`, byte-identical for a
single AP on chip 0), and `hmac_band_set_channel`/`...and_bandwith` are
**host bookkeeping** writes (+0x10d2/+0xd29/+0xd28 of the host band array)
with no wire effect. What the vendor does that our ladder does not is
**synchronise**: its 0x0124 channel helper posts `wal_send_cfg_event` and
then `wal_check_and_release_msg_resp`, i.e. it waits for the firmware's
channel-config response **before** the SSID/AP-config/VAP-start frames, and its
VAP create and VAP start wait for their firmware responses too. Our
`beacon_static` ladder posts all five frames back-to-back and only waits for
the coalesced H2D announce - the transport drain, not the firmware worker.
On band 1 the 0x0124 worker is the program that drives the 5 GHz RF channel;
an AP start that runs before it lands leaves the firmware beacon engine
without a channel. The 2.4 GHz path tolerates the race. **This lane delivers
that wait as a fail-closed parameter (`ap_chan_wait_ms`, default 0), gated on
the firmware's own channel-context "programmed" flag, plus a corrected
read-only activation-gate pack (module ctx + per-band publish words).**

**A second, independent result:** the E2 readbacks were **mis-addressed**. The
tree computed the module-table CPU addresses with the wrong BAR0 window base
(off by +0x4000 / +0x8000); BAR0 = CPU + 0x6b8000 gives the committed recipe
slot 0 CPU `0x171458` / base `0x172504`, not `0x175458` / `0x17a504`. The E2
lines therefore describe unrelated RAM (the logged `0x00175468` is a pointer
in the `0x175xxx` per-bank context region, not a channel context), and the
project conclusion "zero readback is a debug heuristic / async field" cannot
be drawn from them. The patch corrects the constants and extends the log into
a decisive gate pack.

---

## 2. Vendor host decode [V]

### 2.1 The band model: host-only bookkeeping

* `hmac_band_module_init` (`tools/beacon5g_re.py` A/F): validates the module
  context byte 0 (chip id 0) and zeroes the two-band array `g_ast_band`
  (`memset_s(g_ast_band, 0x4660, 0)` = **2 x 0x2330**, exactly two band
  objects). `hmac_band_get_by_id(chip, band)` indexes it with
  `g_ast_band + 0x2330*band` and accepts chip 0 only.
* `hmac_band_set_band(band, type)` -> `strb +0xd28`;
  `hmac_band_set_channel(band, ch)` -> `strb +0xd29`;
  `hmac_band_set_channel_and_bandwith(band, ch, bw)` -> `+0xd29` and
  `+0x10d2` (**the Lead's 0x13f49c / 0x13f520 are the 2.4.15 build; 2.5.24
  has 0x13fac4 / 0x13fb48**). These are local struct writes; **no
  `hmac_config_send_event`/`wal_send_cfg_event` in either function**.
* Callers: `hmac_config_set_channel` (the 0x0124 builder, below) and
  `hmac_config_set_freq` (the STA join sync: it stores the channel in the
  band object and sends WAL cmd **0x0052**; called from `hmac_sta_sync_vap`).
  Neither is a hidden 5 GHz activation: the AP path's only RF-relevant host
  message remains 0x0124.

### 2.2 The 0x0124 builder (the Lead's "ap_chan")

`hmac_config_set_channel` (0x766fc, 0x868 B): parameter/VAP checks (including
the 5 GHz band-cap check) -> `hmac_band_set_channel_and_bandwith` (host
bookkeeping) -> radar status check -> log `vapid/channel/bw/band` ->
`hmac_multiap_report_chn_switch` -> log **"post event to dmac"** ->
`hmac_config_send_event(mac_vap, 0x0124, body)` -> 40 MHz-intolerant sync ->
`hmac_vap_sync_update`. The 4-byte body is built in the (symbol-less) WAL
channel helper that `wal_cfg80211_start_ap` calls first:

| byte | value | source in the vendor |
| ---: | --- | --- |
| 0 | band (0/1) | the chandef channel struct at `[dev+0x1f0]+0x9c`, byte 0 |
| 1 | **chip id (always 0 here)** | `OAL_NET_DEV_PRIV(dev)[2]`, set to the base netdev's chip id by `wal_hipriv_add_vap` |
| 2 | IEEE channel | same chandef struct, `u16 +0xa` |
| 3 | 0 | unused by the firmware worker |

The firmware worker (CA 0x841B8, committed) validates band/body[0] and
channel/body[2], stores `body[2]->ctx+0x10`, `body[0]->ctx+0x11`,
`body[1]->ctx+0x12`, `1->ctx+0x13` (valid), then runs the PHY channel
program. **Our driver's body is `{band, p->vap_id, channel, 0}`; with the
single AP `vap_id == 0 == chip id`, so the 2.4 and 5 GHz frames are
byte-identical to the vendor's.** (Earlier commit `wifi-5ghz-vap-msg` read
body[1] as "vendor mac_vap+2" and the start_ap priv byte as the band; both are
corrected in TABLES T3/T4: priv[0] = VAP index, priv[2] = chip id.)

### 2.3 The vendor AP start is synchronous; ours is not

`wal_cfg80211_start_ap` (0x103634) calls, in order: the channel helper
(direct call, first), `wal_send_set_ssid_event(dev, vap_idx, ...)`,
`wal_cfg80211_set_auth_mode`, `wal_cfg80211_fill_beacon_param`,
`wal_cfg80211_start_ap_write_msg` (0x0127 then `wal_start_vap` 0x0065).
The channel helper body calls `wal_send_cfg_event` **and then
`wal_check_and_release_msg_resp`** - it blocks until the firmware answers
the channel config. `wal_start_vap` likewise posts then waits. So the vendor
never lets the AP config/VAP start overtake the 0x0124 worker, while our
`hi5622_frame_post()` returns as soon as the coalesced H2D announce is
consumed (the transport drain) and the sequence continues immediately.

### 2.4 The AP VAP is created out of band (and waits)

The vendor's cfg80211 `.add_virtual_intf` is a stub, so the AP VAP is born
through the private ioctl `wal_hipriv_add_vap` (0x147b7c): it parses
`create <name> <mode> <2g|5g>`, resolves `hmac_band_type_to_band_id` and
`hmac_band_get_wiphy(band)` (the vendor has one wiphy per band), allocates
the netdev, builds the 24-byte WAL VAP-add body (mode, band id, band type,
band selector), **posts cmd 0x0064 and waits for the firmware response**,
stores the chip id into `dev_priv[2]`, register_netdev, and on a
register_netdev failure sends cmd 0x0066 (VAP delete) as cleanup. The
`hmac_config_add_vap` chain (0x15c95c) is check (band type/id/chip at
params+0x11/+0x12/+0x13) -> init (host VAP object, `hmac_vap_init_band`,
users created by `hmac_vap_add_multi_user`) -> `sync_all` (the H2D create
event) -> MAC address -> `hmac_vap_sync_add` (datapath sync). **No band/RF
activation message exists in any of it**; the band travels in the VAP-create
and channel bodies only, which our driver already sends.

---

## 3. The E2 readback audit - the zeros were mis-addressed [V]

Project convention (committed lanes, `hi5622_contract.h`): the host BAR0 of a
firmware CPU-RAM object is `CPU + 0x6b8000` (region 5). The committed,
bench-read module-table recipe is: **slot 0 CPU 0x171458 = BAR0 0x829458**
(value = context-array CA 0x00119758), **base u16 CPU 0x172504 = BAR0
0x82a504**, `ctx0 = u32(slot0) + u16(base)`. The driver had:

| read | tree constant (CPU) | actual BAR0 the driver read | committed CPU | delta |
| --- | ---: | ---: | ---: | ---: |
| module-table slot 0 | 0x175458 | 0x82d458 | 0x171458 | +0x4000 |
| base u16 | 0x17a504 | 0x832504 | 0x172504 | +0x8000 |

So `hi5622_scan_module_ctx_ca()` resolved a context from unrelated RAM and
`module_ctx+0x10` re-read a word at "`mctx`+0x10". The E2 log line
`0x00175468 (channel=104 band=84 field=23 valid=0)` is not a channel
context: channel 104/band 84 are invalid, the valid flag is 0, and the word is
in the `0x175xxx` per-bank context region (bank 1 context CA 0x175a18,
committed). Likewise `ctx+0x190 == 0` was never the firmware VAP pointer.
**Consequence:** neither the "VAP object async/always-zero" conclusion nor
the "channel ctx garbage" line is evidence about the firmware. The patch
corrects both CPU constants, masks the base as the u16 the recipe specifies,
and refuses an out-of-window context instead of dereferencing it.

---

## 4. The firmware activation gates (committed lanes, re-linked here)

The AP-start workers gate on firmware per-band objects the host cannot
create:

* the VAP-add worker (CA 0x449e4) creates the module context, then binds
  `module+0x4b0` (per-link context) / `+0x4b8` (band object) from
  `[A4FC8(0, data[0x12]) + 0x1a00]` - the per-band link/bank **publish
  word**, non-zero only when that band's bank context is live;
* the 0x0124 channel worker checks `module+0x4b0 != 0` **before** it stores
  its body at `ctx+0x10` - so a dead band-1 bind means no channel context
  and no RF channel program;
* the VAP-start worker (CA 0x46394) requires `module+0x4b8 != 0`.

Committed addresses for the read-only pack (TABLES T7): band-0 publish word
CA 0x15e408 (BAR0 0x816408, live value 0x00174bf0), band-1 publish word
CA 0x15ff70 (BAR0 0x817f70, live value 0x00175a18). **If band 1's word is 0
in the fire, the 5 GHz per-link/bank context was never published and no host
frame can turn beacons on** - that is a firmware/link-layer blocker, and the
recipe then stops (it is not the timing candidate's failure).

---

## 5. The candidate fix (fail-closed, one parameter)

`ap_chan_wait_ms` (uint, read-only after load, **default 0** = the bench-fired
vap5g module behaviour, no timing change, 2.4 GHz untouched):

* `1..1000`: on a band-1 AP (`beacon_static=1`), after the `ap_chan` frame
  is posted, poll the firmware module channel context
  `ctx+0x10` every 5 ms until the worker's own body says
  `channel == chandef channel && band == 1 && valid == 1`; then continue
  with the AP frames. A timeout **aborts before any AP frame** (fail closed;
  the existing AP-start gate marks the sequence dirty, so the rung ends and
  needs a power-cycle). If the module table is unreadable, the wait degrades
  to a bounded blind sleep and says so in dmesg (never a guessed context).
* **>1000** is refused at use time (`-EINVAL`, no frame sent); band > 1 and
  a NULL plan are refused. `beacon_static=0` or band 0 keeps the legacy path
  exactly (wait 0).

Why this is the vendor-faithful shape: the vendor blocks on the firmware's
channel-config response before the AP frames (2.3); our gate is the
firmware's own "channel programmed" state, so the rung measures the firmware
instead of assuming a sleep length. The 2.4 GHz bench-validated path is
byte- and timing-identical because the parameter defaults to 0 and the wait
only applies to band 1.

The patch also adds the read-only gate pack (`module ctx`, per-link, band
object and both band publish words) to the `ap_chan` step, and corrects the
readback constants (section 3). No wire byte of the five AP frames changes.

---

## 6. The fire and what each outcome means

The full rung is `FIRE-RECIPE.md` (G0-G7). One rung per fresh boot, no
retry. The important decision tree at G6:

| observation | verdict |
| --- | --- |
| band-1 publish word == 0 (and band 0 live) | firmware band-1 link/bank context dead -> the timing candidate cannot help; STOP, hand to a firmware link-layer lane |
| band-1 publish word == 0x00175a18, `ap_chan_wait_ms=300` timeout ("did not report channel") | the 0x0124 worker ran but did not program band 1 (band/channel validation or module ctx) -> falsifies the timing candidate; hand the ctx word to the firmware lane |
| wait succeeds, no beacon on air | channel programming done -> the gap is RF/beacon-engine (band-1 PHY/RF or TBTT), next rung is the firmware RF side |
| wait succeeds and the 5180 observer scan finds `cudy-ap-test-5g` + page golden | **PASS**: the 5 GHz AP beacons (AP-ENABLED was already proven in E2) |

---

## 7. Limits / honest unknowns

* The temperature hypothesis is graded [V] (vendor is synchronous) + [I] (the
  race explains band 1 and not band 0). Only the fire discriminates it; the
  gate pack makes the fire decisive either way.
* The per-bank object addresses are committed-lane facts ([V] from the
  boot-gate/ctx-create decodes), not re-derived here from the firmware image;
  the pack logs them read-only and never writes or gates on them for the
  legacy path.
* The E2 fire's module (58 params) is untouched by this lane; the artifact
  here is a host build only (`/tmp/beacon5g-artifact`, local evidence, never
  committed). Its 2.4 GHz behaviour is the same code as the bench-validated
  path with the new parameter at 0.
* The old readback constants are removed from the tree by the patch; the
  stale-VAP guard semantics (log-only for a fresh base post, fail closed for a
  pre-existing stale latch) are unchanged, but it now reads the real field -
  a stale/dead VAP can actually be detected for the first time.

## 8. Evidence produced by this lane

| evidence | result |
| --- | --- |
| `tools/beacon5g_re.py` on the 2.5.24 module | 55/55 PASS (`evidence/re-probes-2.5.24.txt`) |
| `tools/beacon5g_re.py` on the 2.4.15 module (Lead's cited offsets) | 55/55 PASS, F @0x13f49c/0x13f520/0x15c0e8 (`evidence/re-probes-2.4.15.txt`) |
| `tools/run-host-tests.sh` | portable planner 437 checks + patch contract 10 checks, 0 failures (`evidence/host-tests.txt`) |
| `tools/build-beacon5g-ready.sh` | E2 base pinned (13e7bf4c...), patch applies, 0 warnings, 59 params, .text identical after debug-strip, artifact contract PASS (`evidence/build-artifact.txt`, `evidence/build.log`) |
| fire | **not fired by this lane**: `FIRE-RECIPE.md` is the one-shot rung the Lead runs |

## 9. Sources

* local vendor host modules (2.5.24 `de78ec07...`, 2.4.15 `22fa789a...`);
  cited `.text` offsets: `hmac_band_set_channel` 0x13fac4/0x13f49c,
  `..._and_bandwith` 0x13fb48/0x13f520, `hmac_band_set_band`
  0x13fa88/0x13f460, `hmac_config_set_channel` 0x766fc, `hmac_config_set_freq`
  0x15b478, `hmac_config_add_vap` 0x15c95c/0x15c0e8, `wal_cfg80211_start_ap`
  0x103634, `wal_hipriv_add_vap` 0x147b7c.
* committed lanes: `../wifi-5ghz-vap-msg/REPORT.md` (the order fix; its
  "band byte at dev_priv+0x328" reading is corrected here),
  `../wifi-beacon-static/REPORT.md` (the five-frame order and worker body
  map; its "async field"/"debug heuristic" reading is corrected here),
  `../wifi-boot-gate/REPORT.md` + `../wifi-ctx-create/REPORT.md`
  (module-table recipe, per-bank contexts, publish words),
  `../wifi-5ghz/TABLES.md` (band model).
