# FIRE-RECIPE - R5: the AP config frames before the 0x0065 VAP start and the 0x0242 band start (ch40 AP)

**Lane:** wifi-band1-start-send-5 (task-213).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from ../wifi-band1-start-send/FIRE-RECIPE.md,
../wifi-band1-start-send-3/FIRE-RECIPE.md and ../wifi-vap-start-gates/
FIRE-RECIPE.md: recovery armed, UART read-only (never type to the tty), never
write CA 0x400392f0, never read the RC misc window 0x10160000..0x1016FFFF,
never rmmod the vendor modules, a page/state/mask anomaly -> power-cycle, no
blind flash.

**Module under test (build with tools/build-r5-order.sh):**

    /tmp/r5-artifact/hi5622_wifi.ko
    383,240 B  sha256 24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c
    modinfo -p | wc -l  = 65   (no new parameter)

The base is the deployed gate-inverted module 9bf1d846 (382,376 B) + this
lane's order patch; the only wire-visible difference is WHEN the AP config
frames are posted.

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 65
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^(vap_start_g0|vap_add_index|band_start|desc_table|dfs_mode|ap_chan_wait_ms):'

STOP if the sha256/size/param count is not this lane's artifact (see
evidence/build-artifact.txt) or if the gate-inverted 9bf1d846 module is still
deployed.  The MODULE_PARM_DESC strings are dropped by the 6.18 loader, so the
65-param sha is the gate.

Golden state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is required if the previous boot left an
aborted AP (the R4b boot left page golden:1 but state 0x40817A40 =
0x01000219).  Record which path was taken.

## R1 - load (the same R4b ladder, no knob change)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 vap_start_g0=1 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 band_start=1 band_start_fallback=0

Verify every knob (there is NO new one - R5 is a source reorder):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode band_start band_start_fallback vap_add_index vap_start_module vap_start_g0; do printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
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
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

**Expected dmesg, in THIS order (all of):**

~~~
cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg vap_start_g0 band_start ap_vap_start
frame post 'vap_add' / 'desc_table' / 'dfs_mode': ... consumed ...
cfg80211: start_ap beacon_static frame N/9 'ap_chan' posted (.. B, band=1 channel=40) - vendor order: the firmware module channel context is programmed BEFORE the AP config
cfg80211: 5GHz channel activation: firmware channel context programmed after .. ms (word=0x01000128)
frame post 'ap_ssid': ... consumed ...
frame post 'ap_cfg': ... consumed ...        (the 0x0127: beacon interval/DTIM/IE template)
cfg80211: start_ap beacon_static: AP config posted BEFORE the 0x0065 VAP start / 0x0242 band start (R5): beacon_interval=100 dtim=2 head=.. tail=.. - the firmware beacon template is in place for the TBTT
cfg80211: vap_start_g0 pre: mctx=0x.. type=2 band=1 ch=40 field=.. state0=0x00 state1=0 state4=0x00/0xFF count=1 entry0=0 published=0x.. d6e=0 ec8=1 b190=0x.. b440=1 b4ac=0x.. plink6c6=.. (G1..G13 open; b440=1/ec8=1 is the corrected RF-enable/band-up polarity)
frame post 'vap_start_g0': g0 id1 total 40 B ... consumed ... posted N/9
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, field B=0, body {00,01,01,00,00,00,00,00,00,00,00,00}) - after the 0x0124 channel post (mctx[0x10..0x13] stored) and the AP config frames (R5), before the 0x0242 band start
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=.. state4=0x00 vap=0x.. vap+0x10=0x.. (G12 pass - the 0x0242 band start may be posted)
cfg80211: start_ap beacon_static frame N/9 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - after the 0x01F7 mode, the 0x0124 channel post, the AP config frames (R5) and the 0x0065 VAP start
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=2 state[4]=0x00 counter=N vap=0x.. vap+0x10=0x.. (already started / the guards are open)
cfg80211: start_ap beacon_static: 'ap_vap_start' posted after ap_cfg (channel=40) - vendor AP-start order complete
~~~

The R5 difference from the R4b fire: the plan lists **ap_ssid/ap_cfg BEFORE
vap_start_g0/band_start**, the 'ap_ssid'/'ap_cfg' posted lines appear BEFORE
the 'vap_start_g0 pre:' pack, and the new "AP config posted BEFORE ..." line
carries the beacon_interval/dtim/head/tail that the 0x0127 just staged.  The
0x0065/0x0242 lines and their G8/G12 latches are otherwise identical.

## R3 - the read-only probe pack (verification)

Use the committed read helper (bar0read: physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Conversion:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys = CA + 0x3B8000.

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| P1 | band-1 arm word 0x4010C220 | **0x404C4220** | **0x1029** (0 = never armed; 0x1039 = the TRX probe value, NOT a start) |
| P9 | band-1 run-state (bandobj 0x15E570+0x14D0) | 0x40817A40 | u8+4 = **0x00** (not 0xFF); u8+1 = **2** (the R4b "already started" value); u32+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8 / R+0x3AC | two-step via perlink+0x2D8 | **0x01800060 / 0x03E6138A** |
| P13 | on-air beacon | 'iw dev OBS scan freq 5200' | the ch40 AP beacon/SSID (**the rung**) |
| P14 (optional) | per-link beacon state (../wifi-fw-tbtt-arm R3 pack) | perlink+0x6B8/+0x6C0/+0x6C4/+0x6C6 | beacon buffer ptr nonzero, descriptor ptr nonzero, **+0x6C4 = 16**, **+0x6C6 = 1**, built AFTER the 0x0127 staging |

**P1 must be read at 0x404C4220 (the corrected address), never 0x404C8220.**
The dmesg 'vap_start_g0 post:' / 'band_start post:' lines already carry
vap=u32[mctx+0x190] and vap+0x10=u32[that+0x10] read-only, so some questions
can be answered from dmesg before the probe helper runs.

**PASS iff** the plan/order lines are as above, the AP config lines precede
the 0x0065, the 0x0065 post was consumed, 'mctx[0x0e]=1' (G12), P9
state[4]=0x00 with state[1]=2 and the counter incremented, P1 = 0x1029,
P12 = 0x01800060 / 0x03E6138A, **P13 = the ch40 5200 MHz beacon**, page
0x86807FEF / state 1 / mask 0.

## R4 - verdict table (one rung per boot)

| observation after this fire | meaning | next single change |
| --- | --- | --- |
| new order + all PASS probes incl. P13 5200 beacon | the order was the remaining gap: beacon state built with the staged interval/DTIM/template | close R5; no further change |
| new order PASSes but **P13 still no beacon** (P1 0x1029, state[4]=0x00, P12 RF image, P14 +0x6C6=1) | the 0x0065 beacon state was created after the staging but the TBTT/beacon still does not fire | STOP: capture P14 (the beacon buffer/descriptor contents if read-only helpers allow), the P9 counter delta over one interval and the dmesg; the next lane decodes what the 0x0065 builder reads from ctx+0x4AC vs what 0x0127 stages - not another reorder |
| new order PASSes but P14 shows +0x6C6 = 0 | the 0x0065 did not create the beacon state at all | capture the 'vap_start_g0 post:' pack and the G12 line; STOP |
| **G8 refuses** ('mctx[0x11] is not 1') | the 0x0124 worker did not store the band byte | STOP: capture the ap_chan post line, the channel-activation word and the pack - do not re-post, do not reorder further |
| a G9'(mctx[0x440])/G11'(bandobj+0xEC8)/G13 gate refuses after the AP config | the AP config phase wrote one of those bytes (a NEW firmware fact; the decode says 0x0127 stages VAP fields only) | STOP: capture the 'vap_start_g0 pre:' pack; report which gate; do not relax a gate in this boot |
| G5 (u32[mctx+0x190] == 0) refuses | the VAP object vanished (stale/dead VAP) | STOP; power-cycle |
| G12 fails ('mctx[0x0e]' not 1 after 300 ms) | the 0x0065 worker did not apply the VAP start | the 0x0242 is already gated off (no 0x0242 posted); capture the post line and stop |
| 'ap_ssid'/'ap_cfg' post fails or the sequence aborts before the 0x0065 | the AP config phase changed a carrier/build precondition | STOP: the sequence is latched DIRTY (-EIO); power-cycle, capture the failing frame line |
| 0x0242 posts but state[4] stays 0xFF | the resolver CA 0x4AE7C still returns 0 | capture mctx[0x0E], the vap/word fields; STOP |
| page/state/mask anomaly | firmware fault | power-cycle, bench recovery, capture UART |

Never post key 0x0065 twice, never re-post a consumed frame in the same boot,
never use the 0x0242 forced start (subcmd 2) as a shortcut (it calls CA 0x4AF0C
without the pre-start and still refuses at state[4]==0xFF).

## R5 - abort rules (ordering-specific + inherited)

1. One rung per boot: this fire changes the ORDER only.  Do not combine it
   with a new frame family, a parameter change or a probe helper variant.
2. A refusal before the wire is a clean negative: capture the order lines, the
   'vap_start_g0 pre:' pack and STOP.  The fail-closed gates are the
   instrument.
3. If the AP config phase, the 0x0065 or any later frame fails to post, the
   sequence aborts (-EIO) - do not retry in place; power-cycle.
4. Any page/state/mask or UART anomaly -> power-cycle; the recovery card is
   the physical door.
5. Never write CA 0x400392f0; never read the RC misc window; never rmmod.

## R6 - what the Lead captures (host-side, read-only)

* the full plan line and every dmesg line in R2, including the new
  "AP config posted BEFORE ..." line and the ap_ssid/ap_cfg frames (with
  beacon/dtim/head/tail fields);
* the P1/P9/P12 values, the P14 per-link state if the tbtt-arm helper is
  available, and the P13 observer scan;
* the page/state/mask triple;
* a UART capture (read-only, absolute path).

Hand the result back to the Lead for the R5 verdict; this lane stops at the
recipe.
