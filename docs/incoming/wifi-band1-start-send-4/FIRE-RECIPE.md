# FIRE-RECIPE - R4b: the 0x0124 channel post before the 0x0065 VAP start and the 0x0242 band start (ch40 AP)

**Lane:** wifi-band1-start-send-4 (task-211).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from ../wifi-band1-start-send/FIRE-RECIPE.md
and ../wifi-band1-start-send-3/FIRE-RECIPE.md: recovery armed, UART read-only
(never type to the tty), never write CA 0x400392f0, never read the RC misc
window 0x10160000..0x1016FFFF, never rmmod the vendor modules, a
page/state/mask anomaly -> power-cycle, no blind flash.

**Module under test (build with tools/build-r4b-order.sh):** the send-3 R4
lineage (65 params) with the R4b order patch.  Expected artifact:

    /tmp/r4border-artifact/hi5622_wifi.ko
    382,188 B  sha256 b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102
    modinfo -p | wc -l  = 65   (no new parameter)

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 65
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | \
        grep -E '^(vap_start_g0|vap_add_index|band_start|desc_table|dfs_mode|ap_chan_wait_ms):'

STOP if the sha256/size/param count is not this lane's artifact (see
evidence/build-artifact.txt for the builder's recorded values), or if the
send-3 R4 module is still deployed.  The vap_start_g0 parameter description is
NOT in the .modinfo (the 6.18 loader drops MODULE_PARM_DESC); the deployed
source string is the only place to confirm the R4b order, so the 65-param sha
is the gate.

Golden state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is preferred (the R4 boot left the box
with an aborted AP: page golden:1, state 0x40817A40 = 0x01000000).  Record
which path was taken.

## R1 - load (the send-3 R4 ladder, unchanged knobs)

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

The G5/G12 waits are compiled constants (300 ms each); no extra knob.  Verify
every knob (there is NO new one - the R4b is a source reorder):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback vap_add_index vap_start_module \
             vap_start_g0; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1 1 0 0 0 1

STOP if any value differs (wrong module/params).  Then run the proven boot
ladder on endpoint 0000 ONLY (the same G2 as
../wifi-5ghz-beacon/FIRE-RECIPE.md: prepare/upload/verify/seed/hcc_arm/
hcc_loop/release, release **0x5A5A only**), the C2 pair (h2d_ctx_create=3,
h2d_txq_sync72=1, h2d_status phase=ACTIVE ok=2 timeout=0), and create the
5 GHz AP interface:

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
cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add desc_table dfs_mode ap_chan vap_start_g0 band_start ap_ssid ap_cfg ap_vap_start
frame post 'vap_add' / 'desc_table' / 'dfs_mode': ... consumed ...
cfg80211: start_ap beacon_static frame N/9 'ap_chan' posted (.. B, band=1 channel=40) - vendor order: the firmware module channel context is programmed BEFORE the AP config
cfg80211: 5GHz channel activation: firmware channel context programmed after .. ms (word=0x01000128)
cfg80211: vap_start_g0 pre: mctx=0x........ type=2 band=1 ch=40 field=.. state0=0x00 state1=0 state4=0x00/0xFF count=1 entry0=0 published=0x........ d6e=0 ec8=0 b190=0x........ b440=0 b4ac=0x........ plink6c6=.. (the G1..G13 gates are open; G6 (helper object) and G14 (per-link branch) are non-gating observations)
frame post 'vap_start_g0': g0 id1 total 40 B ... consumed ... posted N/9
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, field B=0, body {00,01,01,00,00,00,00,00,00,00,00,00}) - after the 0x0124 channel post (mctx[0x10..0x13] stored) and before the 0x0242 band start
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=0 state4=0x00/0xFF vap=0x........ vap+0x10=0x........ (G12 pass - the 0x0242 band start may be posted)
cfg80211: start_ap beacon_static frame N/9 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - after the 0x01F7 mode, the 0x0124 channel post and the 0x0065 VAP start
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=.. state[4]=0x00 counter=N vap=0x........ vap+0x10=0x........ (the 0x4AF0C start guards are open / already started)
frame post 'ap_ssid' / 'ap_cfg' / 'ap_vap_start'   (unchanged)
~~~

The key R4b difference from the R4 fire: the plan lists **ap_chan before
vap_start_g0 before band_start**, the 'vap_start_g0 pre:' pack is logged
**after** the 'ap_chan posted' and channel-activation lines (and shows band=1),
and the G8 line is gone.

## R3 - the read-only probe pack (verification)

Use the committed read helper (bar0read: physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Conversion:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys = CA + 0x3B8000.

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| P1 | band-1 arm word 0x4010C220 | **0x404C4220** | **0x1029** (0 = never armed; 0x1039 = the TRX probe value after the R4 refusal, NOT a start) |
| P9 | band-1 run-state (bandobj 0x15E570+0x14D0) | 0x40817A40 | u8+4 = **0x00** (not 0xFF); u8+1 = **1** if u32[mctx+0x190]+0x10 != 0 (the CA 0x4C3FC pre-start marker; a **2** means CA 0x4AF0C also ran - the stronger pass); u32+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8 / R+0x3AC | two-step via perlink+0x2D8 | **0x01800060 / 0x03E6138A** |
| P13 | on-air beacon | 'iw dev OBS scan freq 5200' | the ch40 AP beacon/SSID |

**P1 must be read at 0x404C4220 (the corrected address), never 0x404C8220.**
The dmesg 'vap_start_g0 post:' / 'band_start post:' lines already carry
vap=u32[mctx+0x190] and vap+0x10=u32[that+0x10] read-only, so a P9/R4b
question can be answered from dmesg before the probe helper runs.

**PASS iff** the plan/order lines are as above, the 0x0065 post was consumed,
'mctx[0x0e]=1' (G12), P9 state[4]=0x00 with state[1] in {1,2} and the counter
incremented, P1 = 0x1029, P12 = 0x01800060 / 0x03E6138A, P13 = the ch40
5200 MHz beacon, page 0x86807FEF / state 1 / mask 0.

## R4 - verdict table (one rung per boot)

| observation after this fire | meaning | next single change |
| --- | --- | --- |
| plan order + all PASS probes (P1 0x1029, state[4]=0x00, P12, 5200 beacon) | the ordering was the last gap | close R4/R4b; no further change |
| **G8 still refuses** ('mctx[0x11] is not 1') even though 'ap_chan posted' and the channel-activation line passed | the 0x0124 worker did not store the band byte for this ch40 message | STOP: capture the ap_chan post line, the channel-activation word and the pack - do not re-post, do not reorder further |
| another pre-post gate refuses after the 0x0124: G9 (mctx[0x440] != 0), G11 (bandobj+0xEC8 != 0) or G13 (state not fresh) | the 0x0124 post wrote one of those bytes (a NEW firmware fact; the decode had the writer as CA 0x455D0 in the band/config-apply path [I]) | STOP: capture the 'vap_start_g0 pre:' pack; report which gate; do not relax a gate in this boot |
| G5 (u32[mctx+0x190] == 0) refuses after the 0x0124 | the VAP object vanished (stale/dead VAP) | STOP; power-cycle |
| G12 fails ('mctx[0x0e]' not 1 after 300 ms) | the 0x0065 worker did not apply the VAP start | the 0x0242 is already gated off (no 0x0242 posted); capture the post line and stop |
| 0x0242 posts but state[4] stays 0xFF | the resolver CA 0x4AE7C still returns 0 | capture mctx[0x0E], the vap/word fields; STOP |
| state[4]=0x00 but state[1]=0 | the CA 0x4C498 sub-branch word is still zero | capture vap+0x10; STOP (next rung would be the 0x440/0xEC8 or the channel-context word decode - new lane, not this boot) |
| frame post 'vap_start_g0'/'band_start' not consumed | carrier/field-B problem | STOP; do not re-post |
| page/state/mask anomaly | firmware fault | power-cycle, bench recovery, capture UART |

Never post key 0x0065 twice, never re-post a consumed frame in the same boot,
never use the 0x0242 forced start (subcmd 2) as a shortcut (it calls CA 0x4AF0C
without the pre-start and still refuses at state[4]==0xFF).

## R5 - abort rules (ordering-specific + inherited)

1. One rung per boot: this fire changes the ORDER only.  Do not combine it
   with a new frame family, a parameter change or a probe helper variant.
2. A refusal before the wire is a clean negative: capture the 'vap_start_g0
   pre:' pack + the dmesg order and STOP.  The fail-closed gates are the
   instrument.
3. If the 0x0124 or any later frame fails to post, the sequence aborts
   (-EIO) - do not retry in place.
4. Any page/state/mask or UART anomaly -> power-cycle; the recovery card is
   the physical door.
5. Never write CA 0x400392f0; never read the RC misc window; never rmmod.

## R6 - what the Lead captures (host-side, read-only)

* the full plan line and the dmesg lines: vap_add/desc_table/dfs_mode/
  ap_chan posts, the channel-activation line, the vap_start_g0 pre/post pack,
  the band_start post, the ap_* frames;
* the P1/P9/P12 values and the P13 observer scan;
* the page/state/mask triple;
* a UART capture (read-only, absolute path).

Hand the result back to the Lead for the R4b verdict; this lane stops at the
recipe.
