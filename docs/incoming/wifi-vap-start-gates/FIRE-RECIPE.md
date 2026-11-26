# FIRE-RECIPE - R4b order + the corrected G9'/G11' gates (ch40 AP, band 1)

**Lane:** wifi-vap-start-gates (task-212). **This lane never fires.** The Lead
fires with the resident bench procedure (bench link PASS, recovery card READY,
human at the bench, UART capture read-only). One change per fire, one rung per
fresh boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from
../wifi-band1-start-send-4/FIRE-RECIPE.md (and its parents): recovery armed,
UART read-only (never type to the tty), never write CA 0x400392f0, never read
the RC misc window 0x10160000..0x1016FFFF, never rmmod the vendor modules, a
page/state/mask anomaly -> power-cycle, no blind flash.

**Module under test:** the R4b tree (send-3 lineage + the R4b order patch, 65
parameters, 382,188 B sha `b9926c1c...1102` is the fired R4b artefact) with
**this lane's two gate inversions** applied:

    patches/0001-g9-g11-rf-enable-invert.patch   (hi5622_vap_start_g0.c/.h)
    patches/0002-gate-selftest-update.patch      (host selftest, optional for the build)

Expected build: identical 65 parameters, only `hi5622_vap_start_g0.c/.h`
change; the artefact sha/size MUST be recorded by the builder (there is no
pinned sha for the gate-inverted build in this lane - do not confuse it with
the fired R4b `b9926c1c` / send-3 `bb0434c2` / send-2 `b8f6fe35`
artefacts).

## R0 - build + host tests (host only, no device)

    # 1) from the R4b gate lane's pinned chain, reproduce /tmp/r4border-build
    #    (see ../wifi-band1-start-send-4/tools/build-r4b-order.sh), then:
    cd /tmp/r4border-build
    patch -p1 < .../wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch
    # rebuild with the same Kbuild invocation the R4b build script used
    # -> hi5622_wifi.ko; verify modinfo -p | wc -l == 65
    patch -p1 -d .../opensource/docs/incoming/wifi-band1-start-send-4 \
          < .../wifi-vap-start-gates/patches/0002-gate-selftest-update.patch
    .../wifi-band1-start-send-4/tools/run-host-tests.sh   # TREE_OVERRIDE=/tmp/r4border-build

Host tests must pass (the C selftest now starts its default probe at
`b440=1, ec8=1` and asserts the inverted refusals). Record the new .ko size,
sha256 and 65-param count.

## R1 - device preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 65
    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

STOP if the sha256/size/param count is not the gate-inverted build recorded in
R0, or if the send-3/R4b module is still deployed. A fresh power-cycle is
preferred; record which path was taken.

## R2 - load (exactly the R4b knobs - no new parameter)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko      # if not loaded
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

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback vap_add_index vap_start_module \
             vap_start_g0; do printf '%s=' $p; \
             cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1 1 0 0 0 1

STOP if any value differs. Run the proven boot ladder on endpoint 0000 ONLY,
the C2 pair, then create the 5 GHz AP:

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

## R3 - the ch40 AP start (the rung; identical plan to R4b)

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
cfg80211: vap_start_g0 pre: mctx=0x........ type=2 band=1 ch=40 field=.. state0=0x00 state1=0 state4=0x00/0xFF count=1 entry0=0 published=0x........ d6e=0 ec8=1 b190=0x........ b440=1 b4ac=0x........ plink6c6=.. (the G1..G13 gates are open; G9/G11 = the RF-programme enable, 1 = normal)
frame post 'vap_start_g0': g0 id1 total 40 B ... consumed ... posted N/9
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, field B=0, body {00,01,01,00,00,00,00,00,00,00,00,00}) - after the 0x0124 channel post (mctx[0x10..0x13] stored) and before the 0x0242 band start
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=0 state4=0x00/0xFF vap=0x........ vap+0x10=0x........ (G12 pass - the 0x0242 band start may be posted)
cfg80211: start_ap beacon_static frame N/9 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - after the 0x01F7 mode, the 0x0124 channel post and the 0x0065 VAP start
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=.. state[4]=0x00 counter=N vap=0x........ vap+0x10=0x........ (the 0x4AF0C start guards are open / already started)
frame post 'ap_ssid' / 'ap_cfg' / 'ap_vap_start'   (unchanged)
~~~

The only difference from the R4b fire is the **G9/G11 pack values and the
verdict text**: `d6e=0 ec8=1 ... b440=1` and the gates open. If the pack
instead reads `ec8=0` and/or `b440=0`, the corrected gates REFUSE
(-EAGAIN) and the fire stops before the wire: that is the real "the RF
programme would be skipped" state, **not** a reason to relax anything.

## R4 - read-only probe pack (verification)

Same helper/conversion as ../wifi-band1-start-send-4/FIRE-RECIPE.md R3:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys = CA + 0x3B8000.

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| P1 | band-1 arm word 0x4010C220 | **0x404C4220** | **0x1029** |
| P9 | band-1 run-state (bandobj 0x15E570+0x14D0) | 0x40817A40 | u8+4 = **0x00**; u8+1 = **1** (or 2 if CA 0x4AF0C also ran); u32+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8 / R+0x3AC | two-step via perlink+0x2D8 | **0x01800060 / 0x03E6138A** (already programmed at ap_chan by the R4b fire's successors; no re-program expected) |
| P13 | on-air beacon | `iw dev OBS scan freq 5200` | the ch40 AP beacon/SSID |
| page | 0x86807FEF / state 1 / mask 0 | | golden |

PASS iff: the plan/order lines are as above, the pack shows `ec8=1 b440=1`
and the gates-open verdict, the 0x0065 post was consumed, G12
`mctx[0x0e]=1`, the 0x0242 posted, P9 state[4]=0x00 (state[1] in {1,2},
counter incremented), P1 = 0x1029, P12 unchanged, P13 = the ch40 beacon, page
golden.

## R5 - verdict table (one rung per boot)

| observation | meaning | next single change |
| --- | --- | --- |
| all PASS (pack ec8=1 b440=1, G12=1, P1 0x1029, state[4]=0x00, P12, 5200 beacon) | the gate inversion was the last gap | close R4/R4b/R5; no further change |
| corrected G9'/G11' refuses (`ec8=0`/`b440=0`) after ap_chan | the 0x0124/RF-programme enable path did not run in this boot: the RF programme would be skipped | STOP: capture the pack + the channel-activation line; do not relax the gate, do not post the 0x0065 |
| G12 fails (mctx[0x0e] not 1 after the wait) | the 0x0065 worker did not apply the VAP start | the 0x0242 is gated off; capture the post line + pack; STOP |
| 0x0065 posted, G12=1, but 0x0242 post/state[4] still 0xFF | the 0x0242 resolver CA 0x4AE7C still returns 0 | capture count/entry/0x0E/state; STOP - this lane's gates are not the cause |
| state[4]=0x00 but state[1]=0 | the CA 0x4C498 sub-branch word u32[mctx[0x190]+0x10] is 0 | capture vap+0x10; STOP (the next lane is the pre-start sub-branch, not the gates) |
| frame post 'vap_start_g0'/'band_start' not consumed | carrier/field-B problem | STOP; do not re-post |
| page/state/mask anomaly | firmware fault | power-cycle, bench recovery, capture UART |
| PASS except the beacon is blank/absent | the plan posts the start_vap before ap_ssid/ap_cfg (stock posts the AP config first) | next single rung: move ap_ssid/ap_cfg before vap_start_g0 (NOT a gate change) |

Never post key 0x0065 twice, never re-post a consumed frame in the same boot,
never use the 0x0242 forced start (subcmd 2) as a shortcut.

## R6 - abort rules

1. One rung per boot: the only change vs the R4b fire is the two gate
   comparisons. Do not combine it with a new frame, parameter or order change.
2. A refusal before the wire is a clean negative: capture the
   'vap_start_g0 pre:' pack + dmesg order and STOP.
3. If the 0x0124 or any later frame fails to post, the sequence aborts -EIO -
   do not retry in place.
4. Any page/state/mask or UART anomaly -> power-cycle; the recovery card is the
   physical door.
5. Never write CA 0x400392f0; never read the RC misc window; never rmmod.

## R7 - what the Lead captures (host-side, read-only)

the full plan line and all post lines; the `vap_start_g0 pre:` (now with
`ec8=1 b440=1`) / post lines; the band_start post; the P1/P9/P12 values; the
P13 observer scan; the page/state/mask triple; a read-only UART capture. Hand
the result back to the Lead for the R5 verdict; this lane stops at the recipe.
