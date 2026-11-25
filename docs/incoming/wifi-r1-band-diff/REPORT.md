# REPORT - R1 band diff: the ctx+0x11 byte is cleared by our own VAP-start frame (g0 id1 key 0x0065), not by the second ap_chan

**Lane:** wifi-r1-band-diff (shared task **task-183**). 2026-11-27.
**HOST-ONLY**: local driver sources + the committed firmware/vendor decodes
were read in place. Never contacted the router, never wrote the device, never
flashed, never fired. **The Lead fires.**

Evidence markers: **[V]** decoded/committed in this or a previous committed
lane and re-derived here; **[V\*]** bench/pack-measured by the Lead or a fired
lane; **[I]** inference; **[U]** undecided without a device read.

---

## 0. TL;DR

1. **The premise is falsified by the Lead's own timeline.** The word
   `0x01000028` was already stored in the live first CW-0 AP at the CW-2
   read (2026-11-27 12:00, [V\*]), **~1.5 h before the R1 restart (13:30)**.
   It is not the second start's product.
2. **The word arithmetic is single-byte.** The decoded ctx word is
   `channel | band<<8 | body[1]<<16 | index<<24` **[V]** (committed
   `wifi-fw-chan-worker/TABLES.md` T4). Therefore
   `0x01000128` = bytes `28 01 00 01` (ch40, band 1, field 0, band-1 table
   index 1) and `0x01000028` = bytes `28 00 00 01` (ch40, **band 0**,
   field 0, index 1). Only `+0x11` differs (XOR `0x00000100`); `+0x13`
   is still 1 = ch40's band-1 table index. The lane brief's
   "bytes 28 00 01 00 (+0x12 = 1)" is an endianness slip: that byte order is
   the u32 `0x00010028`, which no decode path can produce (see 1.2).
3. **Root cause [V]:** our **VAP-start frame** (g0 id1 key 0x0065, posted
   last on every successful AP start, `hi5622_scan.c` ~1972-1992) carries
   `body[2] = 0`, `body[3] = 0`. The firmware VAP-start worker CA 0x46394
   stores `body[2] -> ctx+0x11` (band) and `body[3] -> ctx+0x12` (field)
   (committed `wifi-beacon-static/TABLES.md` T5 + probe F4b;
   `wifi-fw-rf-programme/REPORT.md` 6.1). The vendor fills those bytes from
   its VAP object: `data[2] = vap+0x11` (band), `data[3] = vap+0x12`
   (committed `wifi-vap-start/TABLES.md` T6). The clean-room provider
   `vapscan_vap_start()` sets only `channel`/`dst` and leaves
   `cfg_a/cfg_b` at 0, so **every completed start overwrites the correct
   band byte 1 -> 0** (and clears the firmware band-1 VAP flag
   `VAP[+0x58] bit2 = (ctx+0x11 != 0)`, effect [I]).
4. **Why only 5 GHz ch40+ exposed it:** for a 2.4 GHz start the band byte is
   0 anyway; for band 1 ch36 the old gate aborted the sequence **before** the
   AP frames (so no VAP start was posted); only a *completed* band-1 AP start
   with `+0x11 = 1` (ch40 first CW-0 rung) runs the clobber. That is exactly
   the CW-0/R1 case.
5. **Why the R1 second start then failed:** its `ap_chan` wire body was
   byte-identical to the first start's (`cp.band = p->band = 1` from the
   cfg80211 chandef, `cp.vap_id = 0`, `cp.module = 0`; deterministic
   builders). The wait gate polled the clobbered ctx **for 300 ms and saw no
   fresh store** (the page was already golden:0 before the R1 sequence
   [V\*]), refused on band 0 and latched DIRTY. The gate's input was a stale
   host-caused state, not a bad wire body.
6. **Minimal fix:** one body field in `vapscan_vap_start()`:
   `gp.cfg_a = p->band;` (`cfg_b = 0`). Patch + host model test below.
7. **Verification:** the host model reproduces **both** words byte-exactly
   from the two decoded worker maps, both gate predicates, the controls and
   the patched source (39/39 PASS); the host build is clean (0 warnings, 59
   params); the fire step is a fresh-chip first/second-start rung with the
   read-only ctx reads and the R0 RF probe pack.

---

## 1. The stated difference, re-derived

### 1.1 The word and the byte message

The channel worker CA 0x841B8 (key 0x0124) validates `(band, channel)`
against the firmware's per-band tables and then stores **[V]** (committed
`wifi-fw-chan-worker/TABLES.md` T2/T4):

    ctx+0x10 = body[2] (channel) ; ctx+0x11 = body[0] (band)
    ctx+0x13 = 0-based per-band table index ; ctx+0x12 = body[1] (field)

so `word = channel | band<<8 | body[1]<<16 | index<<24`.

| observed word | bytes (`+0x10..+0x13`) | channel | band | field | index |
| --- | --- | ---: | ---: | ---: | ---: |
| `0x01000128` CW-0 wait PASS (11:30) [V\*] | `28 01 00 01` | 40 | **1** | 0 | 1 |
| `0x01000028` CW-2 live read (12:00) [V\*] | `28 00 00 01` | 40 | **0** | 0 | 1 |
| `0x01000028` R1 wait REFUSED (13:30) [V\*] | `28 00 00 01` | 40 | **0** | 0 | 1 |

`0x01000128 ^ 0x01000028 = 0x00000100`: **one byte changed, `+0x11`**.
`+0x13 = 1` is untouched and is precisely ch40's band-1 table index
(`wifi-fw-chan-worker` T3: band-1 table 36,40,... -> index 0,1,...), so the
last successful `0x0124` store was the ch40/band-1 one and **a later writer
changed only `+0x11` (and possibly `+0x12`)**.

### 1.2 Why `0x00010028` ("+0x12 = 1") cannot be the fired word

If the brief's byte order were literal, the word would be `0x00010028`
(ch40, band0, field1, index0). That cannot come from the channel worker:
band 0 + channel 40 is **not in the band-0 table (1..14)**, so CA 0x10172C
returns 0x3E8 and the worker returns **before any store** (committed
`wifi-fw-chan-worker/REPORT.md` sec.2/3). A band-0 store also cannot leave
`+0x13 = 0` with channel 40 (index computed from the *validated* table).
The only consistent reading of a u32 print `0x01000028` is bytes
`28 00 00 01`. The model test asserts this too.

### 1.3 Timeline (the decisive fact)

| time | event | ctx0 word (CA 0x00119768) | evidence |
| --- | --- | --- | --- |
| 2026-11-27 11:30 | CW-0 first start, wait PASS, AP-ENABLED | `0x01000128` | CW-0 ledger [V\*] |
| 2026-11-27 12:00 | CW-2 read-only gate-pack read, live CW-0 AP | `0x01000028` | CW-2 ledger [V\*] |
| 2026-11-27 13:30 | R1 second start (no reload), wait REFUSED | `0x01000028` | R1 ledger [V\*] |

The mutation happened **during the first start**, on a page-golden:1 chip,
after the wait passed and before/at AP-ENABLED - long before the restart.
**This single timeline row kills the "second start's body differs" premise.**
The only frames posted between the wait and the live read are
`ap_ssid`, `ap_cfg` and `ap_vap_start`.

---

## 2. The mechanism: our own VAP-start frame clears the band byte

### 2.1 The frame order (the beacon lane's own ladder) [V]

```
start_ap: ap_chan (0x0124) -> WAIT -> ap_ssid (0x0005) -> ap_cfg (0x0127)
          -> ap_vap_start (0x0065)
```

Source: `hi5622_scan.c` `hi5622_scan_op_start_ap()`; the g0-carrier plan is
`vap_add/ap_chan/ap_ssid/ap_cfg/ap_vap_start`; the final VAP start is posted
at the vendor-order step (the CW-0 dmesg itself records "ap_chan -> ap_ssid
-> ap_cfg -> ap_vap_start", [V\*]). The wait only brackets the **first**
frame; everything after it runs with no ctx check.

### 2.2 The VAP-start worker stores body[2]/body[3] into ctx+0x11/+0x12 [V]

Committed decode (two independent lanes, same address map):

* `opensource/docs/incoming/wifi-beacon-static/TABLES.md` T5:
  "VAP start CA 0x46394 | body[1] -> protocol arg, **body[2] -> ctx+0x11,
  body[3] -> ctx+0x12**, body[4..7] -> ctx+0x444";
  evidence `wifi-beacon-static/evidence/beacon-re-2.4.15.txt` F4b PASS line.
* `opensource/docs/incoming/wifi-fw-rf-programme/TABLES.md` T-VAP-start row
  and `REPORT.md` 6.1: "`body[2] -> ctx+0x11 (band) ; body[3] -> ctx+0x12
  (field)`"; later in the same worker `VAP[+0x58] bit2 = (ctx+0x11 != 0)`
  is computed **from the just-stored byte**.

So the **last** writer of ctx+0x11 is not the channel worker; it is the
VAP-start worker, and it writes `body[2]` there.

### 2.3 The vendor fills those bytes from its VAP object; our builder does not [V]

* Vendor body contract (committed `wifi-vap-start/TABLES.md` T6):
  `+2 cfg byte = vap+0x11`, `+3 cfg byte = vap+0x12` (defaults 0 only
  because the *host bridge* defaults them; the vendor AP copies its VAP
  object's band byte = 1 for a 5 GHz VAP).
* Body builder (committed `wifi-vap-start/src/vapstart_frames.c:146`):
  `data[2] = p->cfg_a; /* vap+0x11 */`, `data[3] = p->cfg_b;`.
* Provider (committed mirror `wifi-wpa3-sae/src/hi5622_vapscan_provider.c:257-298`,
  and the fired base tree `/tmp/beacon5g-build/hi5622_vapscan_provider.c`
  sha256 `bc7e3ede...ce828`, same code): the g0 branch sets
  `vsf_vap_start_defaults(&gp); gp.dst = p->dst; gp.channel = p->channel;`
  and **never sets `gp.cfg_a`/`gp.cfg_b`** -> both 0.
* The start_ap caller passes the band all the way into the provider:
  `hi5622_scan_post_vap_start_frame(..., (u8)p->band, ...)` binds
  `vp.band = band`; the provider simply drops it.

**Result:** the first start's VAP start stores `ctx+0x11 = 0`,
`ctx+0x12 = 0`, leaving the channel worker's channel/index bytes intact:

    before VAP start: 0x01000128  (28 01 00 01)
    after  VAP start: 0x01000028  (28 00 00 01)   <- the CW-2 / R1 word

The worst part is not the wait read: while `ctx+0x11` is 0, the firmware's
band-1 VAP flag (`VAP[+0x58] bit2`) is cleared by the same worker [I], and
any later path that re-reads `module+0x11` (e.g. the CA 0x944DE gate
"band==1") sees band 0 until another `0x0124` store lands. Nothing re-posts
one. **[I] on the beacon-engine effect; [V] on the stores.**

### 2.4 Why the first wait PASSED and only ch40 showed the bug

* The first start's wait runs **before** the AP frames; the VAP-start clobber
  comes later. So the wait word is always correct on a completed start.
* 2.4 GHz: band is 0, so `body[2]=0` is a no-op.
* Band 1 ch36: the (fired) old gate required `+0x13 == 1`, impossible for
  index 0, so the sequence aborted at the wait and **no VAP start was
  posted** (the G6a/G6b runs) - the clobber never happened there.
* Band 1 ch40: index 1 -> the old gate passes -> the sequence completes ->
  the VAP start runs -> the only case where the band byte is observably
  cleared. **CW-0 is the first completed band-1 start, and it shows it.**

---

## 3. The R1 second start: what actually happened

### 3.1 The second ap_chan body was correct [V]

`hi5622_scan_fill_ap_start()` derives the fields **fresh from the cfg80211
settings on every start** (no cached dev_priv):

| field | value | why |
| --- | --- | --- |
| `p->band` | 1 | `set->chandef.chan->band == NL80211_BAND_5GHZ`; hostapd restarts with the same ch40 config |
| `p->channel` | 40 | chandef hw_value |
| `p->vap_id` | 0 | hard-coded 0 |
| `p->module` | 0 | `s->user_id`; never assigned in this tree (kzalloc 0) -> header module index 0 = the ctx the wait reads |

`cp.band = p->band`, `cp.vap_id = p->vap_id`, `cp.module = p->module`;
`haf_build_body_chan()` then writes `body = {band, vap_id, channel, 0}`.
The builder is deterministic and zero-filled, so the second start's ap_chan
frame is **byte-identical** to the first's. The driver log "band=1
channel=40" is the truth about the wire.

### 3.2 The wait saw no fresh store

The load-wait polled ctx0 for 300 ms (60 x 5 ms) and read `0x01000028`
throughout (the R1 REFUSED line, [V\*]). A working worker stores within
microseconds of the announce being consumed (the frame post itself reports
"coalesced announce consumed after 1 polls" - the device mailbox was alive),
so 300 ms of a stale word means the **worker did not store**, not that the
store was late.

### 3.3 Ranked candidates for the missing store (not the band-byte cause)

| rank | candidate | why | discriminating read |
| ---: | --- | --- | --- |
| C1 (prime) | **the chip was already in the fault state**: the Lead recorded the page as golden:0 **before** the R1 sequence [V\*]; the 0x0124 dispatcher/worker path never ran | matches "stale for the whole 300 ms" and the "uptime 1133 s" note | re-run the SAME rung after a power cycle with page golden:1 preflight: if the second start's ctx changes to a fresh store (or the R0 probe shows the RF block) C1 is it |
| C2 | **restart-state re-post**: after host-side stop, `del_virtual_intf` clears the host latches (`vap_base_posted=false`, `ap_started=false`; code) [V], so the second start re-posts the structural VAP add (key 0x0064) and user add before `ap_chan`. If the firmware create/parent gate refuses or resets the module ctx asynchronously, the 0x0124 worker's per-link gate (`module+0x4B0 == 0` -> return 0x64, **no store**) silently skips it; the host only sees the frame post succeed | the worker's refusal is invisible to the post path (the 0x0124 worker return is only logged by the dispatcher) | read `module+0x4B0` (per-link) and `module+0x440` right after the second ap_chan; per-link 0 -> C2; nonzero -> C1/C3 |
| C3 | module-target mismatch: header module index = `s->user_id`; if it ever became nonzero the store would land in another module's ctx while the wait reads slot 0 | **not today**: `s->user_id` is never assigned (kzalloc 0), so the frame header module is 0 | `s->user_id` / the module index in the post line (add a dev_info if it ever changes) |

None of C1-C3 changes the value: the stale word was `0x01000028` **because
of the VAP-start clobber**, so any restart whose 0x0124 store does not land
reads band 0 and refuses.

### 3.4 Hypothesis verdicts from the task brief

| hypothesis | verdict | evidence |
| --- | --- | --- |
| (a) the +0x11-driving wire field is a per-VAP value that differs after del/re-add | **NO** | `p->band` from the chandef, `p->vap_id = 0`, `p->module = s->user_id = 0`; builders are zero-fill + deterministic; the log and the wire agree |
| (b) different SR slot / coalesced body, stale/partial | **NO as a body source** | the post copies the whole frame into the SR node under the service mutex, then writes the producer; the firmware reads the same node. The "0 vs 1 polls" only records *when the device cleared out[0]* - it cannot change frame bytes. The body is rebuilt from `p` each call |
| (c) the restart's p struct band/chip | **NO for band/chip** | fields are re-derived per start (3.1). The restart *does* re-post the structural VAP add (C2) - that is about the missing store, not the word |
| (d) the worker reads body[0] as something other than band | **NO** | committed decode: body[0] is the band and is validated via CA 0x10172C before any store; a band-0/ch40 request is rejected with no store, so `0x01000028` is not a channel-worker product |

**The exact root cause of the +0x11 = 0 byte: the key-0x0065 VAP-start
worker's `body[2] -> ctx+0x11` store of an unset builder field.**

---

## 4. Minimal driver fix

**File:** `hi5622_vapscan_provider.c`, function `vapscan_vap_start()`, g0
branch (base tree lines 270-279; committed mirror lines 270-278):
```
 		vsf_vap_start_defaults(&gp);
 		gp.dst = p->dst;
 		gp.channel = p->channel;
+		/* wifi-r1-band-diff (task-183): the vendor VAP-start body carries
+		 * the VAP object's band byte at data[2] (vap+0x11) and the chip
+		 * field at data[3] (vap+0x12); the firmware worker CA 0x46394
+		 * stores them to module ctx+0x11/+0x12.  Leaving cfg_a=0 cleared
+		 * the channel worker's band byte (0x01000128 -> 0x01000028) after
+		 * every completed band-1 AP start. */
+		gp.cfg_a = p->band;
+		gp.cfg_b = 0u;		/* vap+0x12 = vendor chip/field id, 0 */
 		rc = vsf_build_vap_start(f, &gp);
```
Patch: `patches/0001-vap-start-band.patch` (`patch -p1` in the staged
beacon base tree; the build script pins `hi5622_vapscan_provider.c`
sha256 `bc7e3ede...ce828` and refuses drift).

**Effect:** after every band-1 start the ctx stays `0x01000128`, the
firmware band-1 VAP flag stays set, and a restart wait reads a **correct**
band even when it gets the persisted (stale) word - so the R1's C1
discriminator can actually be run.

**Scope notes.**
* The WAL carrier (`ap_start_mode=1`, exercised by neither the fired ladder
  nor the vendor order the last test logs used) builds the 12-byte VAP-start
  body with its own envelope layout; its body[2]/[3] are zero too. That path
  needs its own decode before a fix (not touched here - the fired/default
  carrier is g0 id1, `ap_start_mode=0`).
* The `0x0124` body itself needs no change (`body[0]=band` is already
  correct on the wire).
* The fix does **not** make the wait detect a dead firmware whose stale word
  equals the expected one. The wait's semantics are "the firmware channel
  context reads the requested tuple"; the page-golden preflight and the R0
  RF probe pack remain the health gates. This is why the fire step below
  keeps an explicit golden:1 preflight.

---

## 5. Verification

### 5.1 Host test (this lane)

`tools/r1_band_model.py` is a pure model of the decoded contract - no
vendor byte is copied, only the committed layout constants:

1. builds the exact g0 id1 wire frames (header/token/key/len/body at the
   decoded offsets) for `ap_chan` and `ap_vap_start`;
2. applies the two **decoded worker store maps** in order
   (`0x0124`: body[2]->+0x10, body[0]->+0x11, body[1]->+0x12, index->+0x13
   after validation; `0x0065`: body[2]->+0x11, body[3]->+0x12);
3. asserts the exact words `0x01000128` (pre-VAP-start) and
   `0x01000028` (post-VAP-start, the CW-2/R1 value) pre-fix, and
   `0x01000128` post-fix;
4. runs the fired old predicate (band, channel, `+0x13==1`) and the decoded
   predicate (band, channel, expected index) over both words and the
   controls (band1 ch36, band0 ch6);
5. asserts a band-0/ch40 0x0124 request is **refused by validation** (so
   `0x01000028` has no channel-worker origin);
6. has a `--source` mode that parses the patched provider and derives
   `cfg_a` from it, so the test verifies the actual patch, not just the
   model;
7. keeps the first-start/second-start scenario (with and without a fresh
   0x0124 store) as explicit checks.

Run: `tools/run-host-tests.sh` (result in `evidence/host-test.txt`).

### 5.2 Host build (this lane)

`tools/build-r1-band.sh` stages the pinned beacon base tree, applies the
patch, and builds the 6.18.60 module with the project cross toolchain:
expect 0 warnings/errors, 59 params, debug-strip keeps `.text` identical.
Recorded artifact (LOCAL EVIDENCE ONLY - never committed): **356,612 B**
stripped sha256 `e8b17f72...e4abd4`, unstripped `84b5591c...7429b`,
59 params - see `evidence/build.txt`.

### 5.3 Fire step (Lead; fresh chip)

Preflight: fresh power cycle; **page golden:1** (the R1's page was already
golden:0 before it started - that precondition alone invalidated the rung);
recovery door armed. Load the patched module with the CW-0 parameters:
`band5g=1 vap5g=1 beacon_static=1 beacon_vap_start=1 ap_chan_wait_ms=300`.

1. **First start (ch40, the CW-0 rung, fixed module):**
   * wait PASS line: `word=0x01000128`;
   * after AP-ENABLED, read ctx0 **CA 0x00119768** (BAR0 `0x7D1768`):
     expect **`0x01000128`** (pre-fix it reads `0x01000028` - the clobber
     proof on hardware);
   * read the R0 probe pack (the fw-rf-programme recommendation):
     `module+0x440`, `bandobj+0xEC8`, perlink, `P+0x1A00`, `P+0xF04`,
     `R+0x3A8..`, page status - same values as before the fix (the fix does
     not change the RF state of the first start).
2. **Second start, no reload (the R1's C1 discriminator):** host-side
   `stop_ap` (stop_ap_mode=0, no firmware write), `wlan5` delete + re-add,
   second `hostapd` start on ch40.
   * expect the wait PASS (no REFUSED/DIRTY), page golden:1;
   * read ctx0 before and after the second ap_chan (BAR0 `0x7D1768`) and
     `module+0x4B0` (per-link, `0x7D1C08`), `module+0x440`
     (`0x7D1B98`), then the R0 RF probe pack;
   * **PASS = the second wait completes + page stays golden:1 + the RF
     block words show the C1 prediction.** C1 says the per-link exists on a
     fresh chip, so the re-posted 0x0124 should now actually program the
     band-1 RF -> the first 5 GHz beacons.
   * If the wait still fails: read `module+0x4B0` (per-link). 0 -> C2
     (restart re-post path); nonzero -> C1 (chip state) - and report the
     page status with the line.

Do **not** compare the second-start wait word for "freshness": with the fix
a persisted `0x01000128` is a legitimate match. Freshness evidence is the
R0 RF probe pack + the page status, not the ctx word.

---

## 6. Files and regeneration

| file | role |
| --- | --- |
| `tools/r1_band_model.py` | host model/selftest (pure Python 3, stdlib only) |
| `tools/run-host-tests.sh` | runs it against the patched base tree |
| `tools/build-r1-band.sh` | pinned host build of the fixed module (local artifact) |
| `patches/0001-vap-start-band.patch` | the fix |
| `evidence/host-test.txt`, `evidence/build.txt` | recorded outputs |

Base artifact for the build: the wifi-5ghz-beacon lane module
`/tmp/beacon5g-artifact/hi5622_wifi.ko` 356,600 B sha256
`bc1cc0ea...a1bbb` (the CW-0/R1 module, 59 params); base source pins in
the patch header. The base tree is the same one the wifi-fw-chan-worker
lane used; the fix is independent of that lane's gate patch (it touches only
`hi5622_vapscan_provider.c`).

## 7. Limits

* The R1's raw dmesg was not available to this lane (HOST-ONLY); the quoted
  R1/CW lines are the Lead's committed ledger lines [V\*].
* The `VAP[+0x58] bit2` beacon-engine consequence is [I] (decoded store +
  decoded read, no bench isolation).
* C1 vs C2 for the missing store needs the fresh-chip fire step (5.3); the
  band-byte root cause does not depend on which one it is.
