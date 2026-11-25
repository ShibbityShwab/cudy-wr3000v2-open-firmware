# FIRE-RECIPE - R4: the 40-B key-0x0065 VAP start before the 0x0242 band start (ch40 AP)

**Lane:** wifi-band1-start-send-3 (task-209).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

**Module under test:** the host-built hi5622_wifi.ko of
[BUILD-NOTES.md](BUILD-NOTES.md) / [evidence/build-artifact.txt](evidence/build-artifact.txt)
- debug-stripped **382,072 B** sha256
'bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711',
**65 params**, new read-only 'vap_start_g0' (default 0).

Base: the **band1-start-send-2 tree** (373,056 B sha 'b8f6fe35...5b71', 64
params) which itself is the fired 0x0242 lineage (371,212 B sha
'e2312402...a028', 63 params).  This lane adds exactly one frame family
(40-B g0-id1 key 0x0065) as a new plan step between 'vap_add' and
'desc_table', plus the read-only probe pack; **at the default
vap_start_g0=0 every wire byte is the send-2 artifact's**.

**The question this fire answers:** with the VAP bound (band VAP list count=1)
and the key-0x0065 VAP start posted, does the firmware set mctx[0x0E]=1
(G12), does the 0x0242 pre-start CA 0x4C3FC then store **state[4] = 0x00
instead of 0xFF**, and does CA 0x4AF0C set **0x404C4220 = 0x1029** with the
ch40 beacon on air?  If state[4]=0x00 but state[1] stays 0, the missing piece
is the CA 0x4C3FC success sub-branch's [mctx+0x190]+0x10 word -> recipe
**R4b** (one change, fresh boot).

Inherit **all** non-negotiables from ../wifi-band1-start-send/FIRE-RECIPE.md:
recovery armed, UART read-only (never write the tty), never write CA
0x400392f0, never read the RC misc window 0x10160000..0x1016FFFF, never
rmmod the vendor modules, a page/state/mask anomaly -> power-cycle.  Never
re-post a frame already consumed in the same boot.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # 382072
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l   # 65
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | \
        grep -E '^(vap_start_g0|vap_add_index):'

STOP if the module hash/size/param count is not this lane's artifact - wrong
build.  Also STOP if the new 40-B frame builder does not reproduce, byte for
byte, the KAT of [evidence/frame.txt](evidence/frame.txt) (the host build
already asserts it; a mismatch means the wrong patch was applied).

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is preferred.  Record which path was taken.

## R1 - load (one load, the send-2 ladder + vap_start_g0=1)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 \
        vap_start_g0=1 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 \
        band_start=1 band_start_fallback=0

The G5/G12 waits are compiled constants (**300 ms each**); no extra knob.  The
pack is logged unconditionally when the rung is armed (there is no separate
probe parameter).

Verify every knob (new: vap_start_g0=1):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback vap_add_index vap_start_module \
             vap_start_g0; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1 1 0 0 0 1

STOP if any value differs (wrong module/params).  Then run the proven boot
ladder on endpoint 0000 ONLY (same G2 as ../wifi-5ghz-beacon/FIRE-RECIPE.md:
prepare/upload/verify/seed/hcc_arm/hcc_loop/release, release **0x5A5A only**),
the C2 pair (h2d_ctx_create=3, h2d_txq_sync72=1, h2d_status phase=ACTIVE
ok=2 timeout=0), and create the 5 GHz AP interface:

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

## R2 - the ch40 AP start (the rung)

    cp ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf /tmp/hostapd-5g40.conf
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' \
        /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

**Expected dmesg, in this order (all of):**

~~~
cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add vap_start_g0 desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ... posted N/32
cfg80211: vap_add post: index=0 band=1 ... bandobject=0x........ bandlist_count=1 published=0x........ (bound, band VAP list count=1 ...)
cfg80211: vap_start_g0 pre: mctx=0x........ type=2 band=1 ch=.. field=.. state0=0x00 state1=0 state4=0x00 count=1 entry0=0 published=0x........ d6e=0 ec8=0 b190=0x........ b440=0 b4ac=0x........ plink6c6=.. (the G1..G13 gates are open; ...)
frame post 'vap_start_g0': g0 id1 total 40 B ... consumed ... posted N/32
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, field B=0, body {00,01,01,00,00,00,00,00,00,00,00,00}) - after the 0x0064 VAP add and before the 0x00F7/0x0242 steps
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=0 state4=0x00 vap=0x........ vap+0x10=0x........ (G12 pass - the 0x0242 band start may be posted)
frame post 'desc_table' / 'dfs_mode'                     (unchanged frames)
frame post 'band_start': g0 id1 total 33 B ... consumed ...
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=.. state[4]=0x00 counter=N vap=0x........ vap+0x10=0x........ (the 0x4AF0C start guards are open / already started)
frame post 'ap_chan' / 'ap_ssid' / 'ap_cfg' / 'ap_vap_start'   (unchanged)
cfg80211: 5GHz channel activation: ... word=...           (unchanged)
~~~

PASS (host side): the plan lists 'vap_start_g0' between 'vap_add' and
'desc_table'; the 'vap_start_g0 pre:' pack passes and the 0x0065 post is
**consumed**; the 'vap_start_g0 post:' line shows 'mctx[0x0e]=1' (G12 pass);
the 'band_start post:' line shows **state[4]=0x00** (not 0xFF); page
0x86807FEF / state 1 / mask 0.  If state[4] is still 0xFF, the 0x0065 path
did not take effect: do not post the 0x0242 again in the same boot - capture
and stop.

## R3 - the read-only probe pack (verification)

Use the committed read helper (bar0read, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Conversion:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; **device IO phys = CA +
0x3B8000**.  The corrected rows (TABLES T7):

| # | object | **phys** | expect |
| ---: | --- | ---: | --- |
| P1 | **band-1 arm word 0x4010C220** | **0x404C4220** | **0x1029** (0 = never armed; 0x1039 = TRX probe only, NOT a start) |
| P9 | **band-1 run-state** (bandobj 0x15E570+0x14D0) | **0x40817A40** | u8+4 = **0x00** (not 0xFF); u8+1 = **1** if u32[mctx+0x190]+0x10 != 0 (the CA 0x4C3FC pre-start marker; a **2** means CA 0x4AF0C also ran - the stronger pass); u32+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8 / R+0x3AC (F2 context) | two-step via perlink+0x2D8 | **0x01800060 / 0x03E6138A** |
| P13 | on-air 5200 MHz beacon | observer 'iw dev OBS scan freq 5200' | ch40 beacon / SSID |

**P1 must be read at 0x404C4220 (the corrected address), never 0x404C8220.**
The dmesg 'vap_start_g0 post:' / 'band_start post:' lines already carry
vap=u32[mctx+0x190] and vap+0x10=u32[that+0x10] read-only, so a P9/R4b
question can be answered from dmesg before the probe helper runs.

**PASS (R4) iff** the plan/pre/post lines are as above, mctx[0x0e]=1,
P1 = 0x1029, P9 state[4] = 0x00 with state[1] in {1,2} and the counter
incremented, P12 = 0x01800060 / 0x03E6138A, and P13 = the ch40 5200 MHz
beacon.

## R4 - verdict table, abort and fallback rules (one rung per boot)

| observation after this fire | meaning | next single change |
| --- | --- | --- |
| P1 = 0x1029, state[4]=0x00, state[1]=2, P13 = ch40 beacon | the 0x0065 VAP start was the gap | close R4; no further change |
| state[4]=0x00 but state[1]=0 | the 0x4C3FC pre-start succeeded but its success sub-branch failed: [mctx+0x190]+0x10 is zero (dmesg vap+0x10=0x00000000) | **R4b**: move the 'ap_chan' (module channel) post **before** 'band_start' - one change, fresh boot.  Keep vap_start_g0=1 (do not combine with anything else) |
| state[4] still 0xFF | CA 0x4AE7C still returned 0 | capture: (a) 'vap_start_g0 post: mctx[0x0e]=', (b) mctx[4], (c) bandobj count/entry, (d) 'vap+0x10'; then STOP - do not re-post |
| 'vap_start_g0 pre:' refusal (any G1..G11/G13) | the firmware precondition is not met (G5 = the fault gate: b190=0 -> never posted) | STOP: fix the ladder/precondition, never force the frame; G5 in particular means power-cycle (a stale/absent VAP object) |
| G12 FAIL (mctx[0x0e]=0) | the firmware did not apply the 0x0065 event (carrier/field-B/timing) | the driver itself refuses the 0x0242 ('band_start REFUSED - vap_start_g0=1 but the G12 read ...'), so **no 0x0242 was sent**; capture and hand back - no re-post |
| frame post 'vap_start_g0' not consumed | carrier/field-B problem in our frame | STOP; do not post the 0x0242 |
| page/state/mask anomaly | firmware fault (only possible if G5 raced) | power-cycle, bench recovery; re-check G5, do not retry |

Negative controls (fresh boot each, never combined with R2):

* **N1 (probe-only, smallest risk):** vap_start_g0=1 band_start=0 with the
  same pack: expect the plan to list the step and the 'vap_start_g0 post:'
  line to show mctx[0x0e]=1, while state[4] stays 0x00 (no 0x0242).  This
  proves the 0x0065 event is applied without firing the 0x0242 at all.
* **N2 (reproduce the gap, expected negative):** vap_start_g0=0 band_start=1
  on a fresh boot reproduces the send-2 behaviour: mctx[0x0e]=0 and
  state[4]=0xFF.  One boot, only if the bench wants the controlled pair.
* vap_start_g0=2 -> START_AP REFUSED before any frame; vap_start_g0=1 with
  beacon_static=0 -> START_AP REFUSED.  These two are host-only proofs (no
  frame), safe to run without a fresh boot.
* Never post key 0x0065 twice, never re-post a consumed frame in the same
  boot, and never use key 0x0242 subcommand 2 as a shortcut (it calls CA
  0x4AF0C without the pre-start and still refuses at state[4]==0xFF).

## R5 - what the Lead captures (host-side, read-only)

* full dmesg lines: plan, vap_add post, vap_start_g0 pre/post, desc_table,
  dfs_mode, band_start post, ap_chan and the channel activation line;
* the P1/P9/P12 probe values and the P13 observer scan;
* UART capture (read-only, absolute path) and the page/state/mask triple;
* record under build/linux-kbuild/waves/<phase>/ (gitignored).

## Why one variable

Versus the send-2 artifact the only wire change is one optional 40-B frame
gated by one read-only parameter, plus read-only dmesg lines.  At
vap_start_g0=0 the emitted 0x0064/0x0242 bytes are byte-identical to the
send-2 module, so any change in the outcome or the probes is attributable to
this lane's build.
