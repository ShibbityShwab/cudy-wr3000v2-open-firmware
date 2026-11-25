# FIRE-RECIPE - R4: key-0x0065 VAP start before the 0x0242 band start (ch40 AP)

**Lane:** wifi-band-start-prestart (task-208).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from
../wifi-band1-start-send/FIRE-RECIPE.md: recovery armed, UART read-only, never
write CA 0x400392f0, never read the RC misc window 0x10160000..0x1016FFFF,
never rmmod the vendor modules, a page/state/mask anomaly -> power-cycle.

**Module under test (to be built by the implementer from this spec):** the
fired-base lineage of ../wifi-band1-start-send-2 (vap_add_index, 373,056 B sha
b8f6fe35...) plus exactly one new frame family and the read-only probes of
section 2.  New params (all 0444, default 0 = current bytes unchanged):

    vap_start65    0 = do not post the key-0x0065 frame (default)
                   1 = post it once per AP start, after the key-0x0064 VAP add
                       and before the 0x0242 band start
    vap_start65_probe 1 = log the G1-G14 read-only probe pack (default 1)

Nothing else changes: the 0x0064/0x0242 frames are byte-identical to the fired
send-2 artifact, the plan order otherwise unchanged.

**The question this fire answers:** with the VAP bound (count=1) and the
key-0x0065 VAP start posted, does the firmware set mctx[0x0E]=1, does the 0x0242
pre-start CA 0x4C3FC then write state[4]=0x00 (instead of 0xFF), and does CA
0x4AF0C set **0x4010C220 = 0x404C4220 (arm bits 0x1029)**?  If state[4] is 0x00
but state[1] stays 0, the missing piece is the CA 0x4C3FC success sub-branch's
[mctx+0x190]+0x10 (section 4 row R4b) - the next single-change rung.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # this lane's artifact
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | \
        grep -E '^(vap_add_index|vap_start65|vap_start65_probe):'

Replace the comments with the BUILDER's recorded hash/size/param count from the
lane's evidence/build-artifact.txt when the module is built.  STOP if the
module is not this lane's artifact.  Also STOP if the new key-0x0065 frame
builder does not reproduce, byte for byte, the 40-B frame of section 1.

Golden state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is preferred.  Record which path was taken.

## R1 - load (one load, the fired send-2 ladder + the one new knob)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko       # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 \
        vap_start65=1 vap_start65_probe=1 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 \
        band_start=1 band_start_fallback=0

Verify every knob (expect 1 1 300 1 1 1 1 0 0 0 1 1 for the band/AP knobs,
vap_add_index=0, vap_start65=1, vap_start65_probe=1).  STOP on any difference.

Then run the proven boot ladder on endpoint 0000 ONLY (same G2 as
../wifi-5ghz-beacon/FIRE-RECIPE.md: prepare/upload/verify/seed/hcc_arm/
hcc_loop/release, release 0x5A5A only), the C2 pair (h2d_ctx_create=3,
h2d_txq_sync72=1, h2d_status phase=ACTIVE ok=2 timeout=0), and create the AP:

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

## R2 - the rung (vap_add then the new vap_start65 then band_start)

    cp ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf /tmp/hostapd-5g40.conf
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' \
        /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

**Expected dmesg, in this order (all of):**

    cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add vap_start65 desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg ap_vap_start
    frame post 'vap_add': ...            (key 0x0064, 33-B body carrier as fired)
    cfg80211: vap_add post: ... bandlist_count=1 ...
    frame post 'vap_start65': ... (40 B, g0 id1 key 0x0065, field B=0, body {00,01,01,00,00,00,00,00,00,00,00,00})
    cfg80211: vap_start65 pre: G1..G11,G13 pass (log the values)     <- section 2
    cfg80211: vap_start65 post: mctx[0x0e]=1 mctx[0x11]=1            <- G12 after the wait
    frame post 'desc_table' / 'dfs_mode'                              (unchanged)
    frame post 'band_start': ... (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0)
    cfg80211: band_start post: module=0 bandobject=nonzero state[1]=1 state[4]=0x00 counter=N
    frame post 'ap_chan' / 'ap_ssid' / 'ap_cfg' / 'ap_vap_start'      (unchanged)
    cfg80211: 5GHz channel activation: ... word=... (unchanged)

PASS (host side): the plan lists vap_start65 between vap_add and band_start;
the frame_post line reports a **consumed** announce; the vap_start65 post line
shows mctx[0x0e]=1; the band_start post line shows **state[1]=1 and
state[4]=0x00**; page 0x86807FEF / state 1 / mask 0.

**R3b is retried only after this pack.**  If state[4] is still 0xFF, the
key-0x0065 path itself did not take effect; do not post the 0x0242 again in the
same boot - capture and stop.

## R3 - the read-only probe pack (the pre-fire gate; fail-closed)

The probes are object-relative (TABLES T10).  Resolve mctx with the existing
bench-proven chain (../wifi-band1-start-send-2: slot CA 0x175458, base CA
0x17A504, mctx = slot + base; bandobj = mctx+0x4B8; perlink = mctx+0x4B0).
The driver logs, before posting the key-0x0065 frame:

    cfg80211: vap_start65 pre: mctx=0x%08x type=%u band=%u ch=%u field=%u
              state0=0x%02x state1=%u state4=0x%02x count=%u entry0=%u
              published=0x%08x d6e=%u ec8=%u b190=0x%08x b440=%u b4ac=0x%08x
              plink6c6=%u (%s)

Gates (all must pass or **no frame is posted for this rung**):

| id | check | pass | on failure |
| --- | --- | --- | --- |
| G1 | mctx nonzero | yes | stop: wrong ladder/readback |
| G2 | bandobj+0x12 == 1 | yes | stop: VAP-add/list issue, not this rung |
| G3 | bandobj[0] == vap_add_index | yes | stop: duplicate different index |
| G4 | u32[bandobj+0x1A00] != 0 | yes | stop: bind gate was not met |
| G5 | u32[mctx+0x190] != 0 (after a bounded wait, 300 ms) | yes | **STOP**: posting key 0x0065 would fault at CA 0x46470 |
| G6 | u32[mctx+0x4AC] != 0 | yes | warn only (helper logs; continue) |
| G7 | u8[mctx+4] == 2 | yes | stop: create type is not AP |
| G8 | u8[mctx+0x11] == 1 | yes | stop: board band field is not 5 GHz |
| G9 | u8[mctx+0x440] == 0 | yes | stop: band-config flag set; the 0x944DE RF branch is open (separate analysis) |
| G10 | u8[bandobj+0xD6E] == 0 | yes | stop: CA 0x4C3FC band-id gate would fail before state[4] |
| G11 | u8[bandobj+0xEC8] == 0 | yes | stop: band already up |
| G13 | state[0]==0, state[1]==0, state[4] in {0x00,0xFF} | yes | stop: not a fresh band |

Then post the 40-B key-0x0065 frame, wait frame_gap_ms (>= 200 ms suggested;
the vendor path is asynchronous), and log G12 = u8[mctx+0x0E] plus
u16[mctx+0x11] and the same state[0..4]:

    cfg80211: vap_start65 post: mctx[0x0e]=%u mctx[0x11]=%u state1=%u state4=0x%02x

Only if G12 == 1 may the existing 0x0242 sync-start frame be posted.  If
G12 != 1, do not post 0x0242 (it cannot succeed - CA 0x4C3FC would return at CA
0x4AE7C again) and stop after capturing the pack.

## R4 - decision table (one rung per boot)

| observation after this fire | meaning | next single change |
| --- | --- | --- |
| 0x4010C220 == 0x404C4220, state[1]=2, beacon on air | the 0x0065 VAP start was the gap | close R3b; no further change |
| state[4]=0x00 but state[1]=0 | pre-start success sub-branch failed: u32[mctx+0x190] or its +0x10 word is 0 (CA 0x4C498-0x4C4A0) | R4b: move the ap_chan (channel) post **before** band_start (the +0x10 word is channel-context data), one change, fresh boot |
| state[4] still 0xFF | 0x4AE7C still returned 0 | capture: (a) mctx[0x0E] after the wait; (b) mctx[4]; (c) count/entry; then STOP - do not re-post |
| frame post 'vap_start65' not consumed | carrier/field-B problem | STOP; do not post 0x0242 |
| page/state/mask anomaly | firmware fault (possible if G5 raced) | power-cycle, bench recovery; reduce the pre-post wait, re-check G5 |

## R5 - alternatives and negative controls (do not mix in one boot)

* **N1 (no 0x0242, probe-only):** with vap_start65=1 and band_start=0 the
  pack should show mctx[0x0E] flip 0 -> 1 and state[4] unchanged (0).  This is
  the cleanest proof that the 0x0065 event is applied and is the recommended
  first fire if the Lead wants the smallest risk.
* **N2 (reproduce the gap):** vap_start65=0 on a fresh boot (the send-2
  behaviour) reproduces mctx[0x0E]=0 and state[4]=0xFF - one boot, expected
  negative; only if the bench wants the controlled pair.
* **N3 (cheap alternative rung, no new frame family):** move the existing
  ap_vap_start step (WAL g2 cmd 0x0065, 44 B) from plan position 8 to before
  the band_start step.  This is a plan-order change only (stock order) and
  needs no new frame builder.  If the g2 command reaches the same firmware
  VAP-start handler, G12 flips to 1.  Try N3 only if the g0-id1 frame cannot be
  built for this fire; N1/R2 above is the primary.
* **R4b (second change):** only after the R2 fire shows state[4]=0x00,
  state[1]=0.  Move ap_chan before band_start.  Do not combine with R2.
* Never post key 0x0065 twice, never re-post a consumed frame in the same
  boot, and never use key 0x0242 subcommand 2 (forced start) as a shortcut: it
  calls CA 0x4AF0C without the pre-start and still refuses at state[4]==0xFF.

## R6 - what the Lead captures (host-side, read-only)

* full dmesg lines: vap_add post, vap_start65 pre/post, band_start post, the
  channel activation line, frame announces consumed counts;
* the probe pack values for G1-G14;
* P1 readback of **0x4010C220** (expect 0x404C4220 after a successful start;
  0 before), and the run bits 0x40101220 / gate 0x4011480C from
  ../wifi-band1-start-send TABLES T-probes;
* UART capture (read-only, absolute path) and the page/state/mask triple.
