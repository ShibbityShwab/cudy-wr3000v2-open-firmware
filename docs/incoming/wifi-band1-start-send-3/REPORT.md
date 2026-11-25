# REPORT - the 40-B g0-id1 key-0x0065 VAP start before the 0x0242 band start (R4)

**Lane:** wifi-band1-start-send-3 (shared task **task-209**).  **HOST-ONLY:**
implementation + host build + host tests + decode consolidation; no device
contact, no write, no fire.  **The Lead fires.**  Companions:
[README.md](README.md), [TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[BUILD-NOTES.md](BUILD-NOTES.md),
[patches/0001-vap-start-g0.patch](patches/0001-vap-start-g0.patch),
[tools/](tools/), [src/](src/), [evidence/](evidence/).

Grades: **[V]** re-derived from the committed sibling decodes/artifacts by
this lane's host tests; **[V\*]** bench-measured by a committed lane; **[I]**
inference; **[U]** needs the fire.

---

## 0. Answer in one paragraph

The band-start root cause is decoded and this lane implements the missing
message: the 0x0242 pre-start helper **CA 0x4C3FC** writes state[4] only when
the band VAP resolver **CA 0x4AE7C** returns the module context, and for the
single bound VAP the resolver requires **mctx[4] == 2** (AP) **AND
mctx[0x0E] in {1,2,3}**.  On a fresh 5 GHz AP mctx[0x0E] is 0, and exactly one
code path sets it 0->1: the H2D g0-id1 config-event **key 0x0065** worker
**CA 0x46394**, AP branch **CA 0x46418-0x4641A**.  The stock posts that frame
(`hmac_config_start_vap_event`, 12-byte body, from the BSS-start path
0x4826C) before the green_ap enable; our ladder only posts its WAL group-2
equivalent as plan step 8, *after* the 0x0242 has already failed at
0x4C4EE (state[4]=0xFF) and CA 0x4AF0C has refused at 0x4AF32.  This lane adds
the **40-byte g0-id1 key-0x0065 frame** as a new AP-start plan step directly
between `vap_add` and `desc_table`, behind a fail-closed read-only
`vap_start_g0` parameter (0 default, 1 posts, >1 refused), with the decoded
**G1..G14** probe gates: G5 (mctx[0x190] != 0, bounded 300 ms wait) gates the
frame because the worker dereferences that pointer at CA 0x46470, and G12
(mctx[0x0E] == 1 after a bounded wait) gates the 0x0242.  The R4b second
observable (`[mctx+0x190]+0x10` zero -> state[4]=0x00 but state[1]=0) is
logged read-only on both post lines.  At the defaults the emitted bytes are
byte-identical to the send-2 artifact; with `vap_start_g0=1` the plan is
`vap_add vap_start_g0 desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg
ap_vap_start`.  Host tests: **351 checks / 0 failures** (byte-exact 40-B
frame + sibling-builder cross-check, plan order, fail-closed matrix, every
G1-G14 gate).  Artifact: 382,072 B, sha256 `bb0434c2...`, **65 params**.  Not
fired.

## 1. Inputs and base [V]

| item | value |
| --- | --- |
| root cause + frame spec | ../wifi-band-start-prestart (task-208): REPORT/TABLES T1-T11, FIRE-RECIPE R4 |
| base tree (this lane's direct base) | ../wifi-band1-start-send-2 applied to the fired 0x0242 tree: /tmp/vapbound-build, 373,056 B sha256 `b8f6fe35...5b71`, 64 params |
| fired 0x0242 lineage | ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree = /tmp/bandstart-build, artifact 371,212 B sha256 `e2312402...a028`, 63 params |
| sibling 0x0065 frame/builder | ../wifi-vap-start/src/vapstart_frames.c `vsf_build_vap_start` (40-B g0-id1 KAT, there body[2]=0) |
| sibling 0x0242 builder | ../wifi-band1-start-send/src/hi5622_band_start.c (field-B agreement + the 0x0242 readback) |
| build | 6.18.60 kernel tree + arm-openwrt-linux-muslgnueabi gcc 14.4.0 (OpenWrt staging) |
| artifact | /tmp/g0start-artifact/hi5622_wifi.ko (LOCAL ONLY, never committed) |

## 2. Why exactly one new frame - and why this one [V]

The pre-start (CA 0x4C3FC) is not a count check: it calls the resolver and
stores state[4] = resolver-VAP[0] only when the resolver returns non-NULL
(`ldrb r0,[r0,#0]; strb r0,[r4,#4]` at 0x4C448); its failure branch stores
**0xFF** at 0x4C4EE.  The resolver needs bandobj+0x12 == 1 (the R3b readback's
count=1 satisfies it) AND the per-VAP gate mctx[4]==2 && mctx[0x0E] in
{1,2,3}.  mctx[0x0E] has exactly one 0->1 writer: the key-0x0065 AP branch.
The stock order is VAP add (0x0064) -> VAP start (0x0065,
hmac_config_start_vap_event in the BSS-start path) -> ... -> green_ap enable
(0x0242).  Our send-2 ladder posts the WAL g2 VAP-start equivalent after the
0x0242, too late.  Hence the new frame family is **key 0x0065 on the g0-id1
carrier**, posted between the 0x0064 add and every later step.

No other change is needed for this rung: the same 0x0064 frame binds the VAP,
the same 0x0242 frame starts the band, and the same plan order is kept.

## 3. The frame, byte exactly [V]

    header 12 B: 00 01 00 <field B = vap_add_index> 28 00 01 00 00 00 5a 5a
    token   8 B: 00 00 00 00 01 00 00 00
    key u16    : 65 00                  (0x0065 VAP start, worker CA 0x46394)
    len u16    : 0c 00                  (12-byte body)
    body 12 B  : 00 01 01 00 00 00 00 00 00 00 00 00
    tail  4 B  : ff ff ff ff
    total = 40 B;  hdr total field 0x0028;  SR node word1 0x00286D2B

body[0] = the stock do_start_vap arg (0), body[1] = vap+0x0F state (1),
**body[2] = vap+0x11 band = 1 (5 GHz)**, body[3] = vap+0x12 field (0),
body[4..7] = the vap+0x414 u32 word (0).  The host test re-derives it from the
carrier geometry only, and cross-checks it byte-for-byte against the real
sibling builder `vsf_build_vap_start` for every module index 0..0x12 [V].

## 4. What the patch changes [V]

On the pinned send-2 tree (2 new files, 4 edited + Kbuild):

| file | change |
| --- | --- |
| `hi5622_vap_start_g0.h/.c` (new) | the 40-B builder, the plan (post only at 1 + beacon_static + band 1), the G1..G14 probe struct/gate/verdicts, the G12 post-check, the sysfs status line |
| `hi5622_scan_logic.h/.c` | new plan step `HI5622_BEACON_STEP_VAP_START_G0` directly after `VAP_ADD`; `hi5622_beacon_plan()` takes `vap_start_g0`; step name `vap_start_g0` |
| `hi5622_scan.c` | the read-only 444 `vap_start_g0` parameter (default 0), the before-any-frame gates (">1", "requires beacon_static"), the plan call, the probe reader + G1..G13 gate + 0x0065 post + G12 read, the G12 gate on the 0x0242, the R4b readback in the band_start state/log, the sysfs status line |
| `hi5622_band_start.h` | the band_start read-state snapshot gains `vap_object` / `vap_object_p10` (the CA 0x4C498/0x4C4A0 R4b words); the guard-name gains the R4b verdict |
| `Kbuild` | builds `hi5622_vap_start_g0.o` |

The probe reader fills the whole pack from the existing bench-proven readback
chain (slot0/base -> mctx -> bandobj/perlink) - no probe, no write, no new
address mapping.  G5 waits up to 300 ms for the asynchronous mctx+0x190
population; G12 waits the same bounded constant after the post.  A restart
after a host-side stop never re-posts the 0x0065 frame.

At the fire defaults (`vap_start_g0=0`) no byte changes: the 0x0064/0x0242
frames and the plan are the send-2 artifact's.

## 5. Fail-closed behaviour and dmesg [V]

| trigger | behaviour |
| --- | --- |
| `vap_start_g0 > 1` | start_ap REFUSED before any frame |
| `vap_start_g0=1` without beacon_static=1 | start_ap REFUSED before any frame |
| any G1..G11/G13 gate fails | the pack is logged and the start aborts; the 0x0065 frame is NOT posted (G5 in particular: a fault at CA 0x46470 is prevented) |
| G6 (mctx+0x4AC == 0) | warn only, continue |
| G12 != 1 after the post | the 0x0242 is NOT posted; with `band_start=1` the start aborts (-EIO) with the pack already logged; with `band_start=0` the probe-only N1 rung continues |
| frame post not consumed | sequence aborts, no later frame |
| restart after host-side stop | 0x0065 not re-posted (a live VAP is never started twice) |

Expected dmesg (the important lines): `start_ap beacon_static=1 plan (9
steps): vap_add vap_start_g0 desc_table dfs_mode band_start ap_chan ap_ssid
ap_cfg ap_vap_start`; `frame post 'vap_add': ...`; `vap_add post: ...
bandlist_count=1 ...`; `vap_start_g0 pre: mctx=... type=2 band=1 ...
b190=... (...)`; `frame post 'vap_start_g0': ... 40 B ...`; `vap_start_g0
post: mctx[0x0e]=1 mctx[0x11]=1 state1=... state4=... vap=... vap+0x10=...
(G12 pass ...)`; `frame post 'desc_table' / 'dfs_mode'`; `frame post
'band_start'`; `band_start post: ... state[1]=... state[4]=0x00 ...
vap=... vap+0x10=...`; `ap_chan` and the AP frames.

## 6. Host tests [V]

`./tools/run-host-tests.sh` (evidence/host-tests.txt): **351 checks, 0
failures**.

| part | checks | content |
| --- | ---: | --- |
| src/vap_start_g0_selftest.c | 278 | the byte-exact 40-B KATs (field B 0 and 4), field B over 0..0x12, the sibling-builder cross-check, the plan/build fail-closed matrix, every G1..G14 gate (code + verdict text), G12 0/1/2/3/4/0xFF, the status line, and the real `hi5622_beacon_plan()` order (9/8/5-step, enum + names, max clamp) |
| tools/vap_start_g0_frame_test.py | 73 | independent Python re-derivation of the KATs from the carrier geometry, and the source contract on the patched tree: param/ladder gates, plan insertion point and order, G5-before-post and G12-before-0x0242 order, probe offsets, geometry constants, the R4b readback, Kbuild |

No device contacted.

## 7. Artifact [V]

| field | value |
| --- | --- |
| stripped | /tmp/g0start-artifact/hi5622_wifi.ko, **382,072 B** |
| sha256 | `bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711` |
| unstripped | 1,422,988 B, sha256 `05ca89cc9e348cf3f99270d4c5c94b2dc9e3a09e3ebbbf3cfa290e84b6ea0cc4` |
| .text sha256 | `3d3b45ffe483a7740c93aa6458a02461bf1226578371350d55df85ea8b972cc3` |
| params | **65** (64 send-2 + `vap_start_g0`) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text identical |
| cfg80211 symvers | 12 lines |
| size delta | +9,016 B vs the send-2 373,056 B artifact |

Reproduce: `./tools/build-vap-start-g0.sh` (reproduces the send-2 tree from
the fired 0x0242 base + the committed send-2 patch, then applies this lane's
patch; all pins in the script) and `./tools/run-host-tests.sh`.

## 8. Honest limits [U]

* **Not fired.**  Whether the firmware accepts the 40-B 0x0065 frame, sets
  mctx[0x0E]=1, lets CA 0x4AE7C return the VAP and makes CA 0x4AF0C arm the
  0x4010C220 0x1029 bits is the Lead's fire (FIRE-RECIPE).
* **The decoder is fire-gated.**  G5/G12 are read-only; the driver never
  writes firmware RAM and never retries a consumed frame.  If G12 != 1 the
  ladder stops instead of posting the 0x0242 (fail closed).
* **Band 1 only.**  `vap_start_g0=1` on a band-0 AP start is a clean
  no-post; this lane does not model a 2.4 GHz VAP start (the 2.4 GHz path
  does not need the CA 0x4AE7C resolver).
* **Rebase note.**  The deliverable is the pinned send-2 base the Lead asked
  for.  On the merged image-#3 module the touched regions
  (`hi5622_scan.c`, `hi5622_scan_logic.*`, `hi5622_band_start.h`) may
  differ; the patch is not promised to apply there without a rebase.
* **The WAL g2 alternative (N3).**  Moving the existing `ap_vap_start` WAL
  group-2 step (44 B) before `band_start` is an untested plan-order-only
  alternative; this lane's 40-B g0-id1 path is the primary rung and the
  sibling recipe keeps N3 as a fallback only.
