# FIRE-RECIPE - the H2D 0x0242 band-1 MAC-start send (R3b) + ch40 AP

**Lane:** wifi-band1-start-send (task-206).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

**Module under test:** the host-built hi5622_wifi.ko of BUILD-NOTES.md /
evidence/build-artifact.txt - debug-stripped **371,212 B** sha256
'e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028',
**63 params**, new 'band_start' (and 'band_start_fallback') params, defaults 0.

Base: the **F2 fire module** lineage (364,184 B sha 12427b86..., 61 params,
fired 2026-11-27 23:30: the 5 GHz RF was PROGRAMMED).  This lane adds exactly
one frame family (0x0242 [+ the 0x0243 fallback rungs]) after the 0x01F7
dfs_mode step; every other byte/param is identical.

**The question this fire answers:** does posting the stock's WAL
'set_green_ap' band-start frame - a g0-id1 config event **key 0x0242, field B
= the AP-start module (0), body {01,00,01,00,00}** (subcmd 1 sync start) -
make the firmware call the per-band MAC start CA 0x4AF0C, set **0x1029** in
the band-1 arm word **0x4010C220**, and put the 5200 MHz ch40 beacon on air?
R3b is closed iff the corrected probe **0x4010C220 == 0x1029** (phys
**0x404C4220**) / the run-state says started **and** the observer sees the
beacon.

Inherit **all** non-negotiables from ../wifi-5ghz-beacon/FIRE-RECIPE.md:
recovery armed, UART read-only, never write CA 0x400392f0, never read the RC
misc window 0x10160000..0x1016FFFF, never rmmod the vendor modules, a
page/state/mask anomaly -> power-cycle.  Never re-post a frame already
consumed in the same boot.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # 371212
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l   # 63
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^band_start(_fallback)?:'   # (uint) each

STOP if the module hash/size/param count is not this lane's artifact - wrong
build.  Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is preferred (a new module load refuses to
re-run the boot ladder on a chip booted at probe).  Record which path was
taken.

## R1 - load (one load, desc_table=1 dfs_mode=1 band_start=1)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 \
        band_start=1

Verify the knobs:

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1 1 0

STOP if any value differs (wrong module/params).

Then run the proven boot ladder on endpoint 0000 ONLY (same G2 as
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
    grep -E '^(ssid|hw_mode|channel)=' /tmp/hostapd-5g40.conf   # cudy-ap-test-5g40 / a / 40
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

**Expected dmesg, in this order (all of):**

~~~
cfg80211: start_ap beacon_static=1 plan (8 steps): vap_add desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'desc_table': g0 id1 total 68 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 1/8 'desc_table' posted (68 B, g0 id1 key 0x00F7, ...) - before the 0x0124 channel post
cfg80211: desc_table post: module ctx=0x........ bandobject=0x........ (...)
frame post 'dfs_mode': g0 id1 total 29 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 2/8 'dfs_mode' posted (29 B, g0 id1 key 0x01F7, body mode=3) - after the 0x00F7 table and before the 0x0124 channel post
cfg80211: dfs_mode post: bandobject=0x........ published=0x........ attach=1 (the 0x01F7 worker's mode-write preconditions are met)
frame post 'band_start': g0 id1 total 33 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 3/8 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - after the 0x01F7 mode and before the 0x0124 channel post
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=1 state[4]=0x.. counter=N (the 0x4AF0C start guards are open)
cfg80211: start_ap beacon_static frame 4/8 'ap_chan' posted (...)
cfg80211: 5GHz channel activation: firmware channel context programmed after N ms (word=0x...)
~~~

PASS (host side): the plan line lists dfs_mode then band_start then ap_chan;
the band_start frame is posted **after** dfs_mode and **before** ap_chan; the
frame_post line reports a **consumed** announce (no "announce NOT consumed" /
no "lane closed"); page 0x86807FEF, state 1, mask 0; sysfs shows
'band_start: param=1 (band1-post) ... posted=1'.

The 'band_start post:' state line is diagnostic, not gating: bandobject=ZERO
or state[4]=0xFF means the worker/CA 0x4AF0C guards refused and the frame
could not arm the band - do not re-post; the probes below decide.

### R2b - the optional fallback rung (only if R2 did not arm / for the F3/F4 test)

The decoded fallback ladder is one extra load (fresh boot) with
**band_start=1 band_start_fallback=1**: after the primary frame the driver
reads the per-band start marker state[1] and, if it is not 2, posts the
**forced-start** 0x0242 (body {02,00,00,00,00}) and then the **0x0243 nudge**
(both inside the same band_start step, still before ap_chan).  Expected dmesg
adds:

    cfg80211: start_ap beacon_static frame 4/8 'band_start' fallback posted (33 B, g0 id1 key 0x0242, subcmd=2 forced start, state[1] was 1)
    cfg80211: start_ap beacon_static frame 5/8 'band_start' fallback posted (29 B, g0 id1 key 0x0243, band-ready nudge, state[1] was 1)
    cfg80211: band_start fallback: start marker state[1]=2 for module=0 (band-1 start armed (read 0x4010C220 for 0x1029))

Do not combine the fallback rung with the R2 verdict - it is a separate rung
(one change per fire).  The fallback is skipped with a logged line when the
band-object/VAP guard refuses (a later start frame cannot help then).

## R3 - read-only probe verification (this is the result)

Use the committed read helper (bar0read, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Conversion:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; **device IO phys = CA +
0x3B8000**.  The corrected pack is TABLES T7; the decisive rows:

| # | object | **phys** | expect |
| ---: | --- | ---: | --- |
| P1 | **band-1 arm word 0x4010C220** | **0x404C4220** | **0x1029** (0 = never armed) |
| P2 | G+0x370 (the NULL hook) | 0x4082CB34 | **0** (proves the direct 0x4AF0C path, not the hook) |
| P3 | G+0x374 (control) | 0x4082CB38 | 0x00057639 |
| P4 | band-0 arm word 0x40108214 | 0x404C0214 | 0xF (unchanged reference) |
| P5 | run bits 0x40101220 | 0x404B9220 | band-1 bit0 = 1 |
| P6 | band-1 gate 0x4011480C | 0x404CC80C | bit0 = 1 |
| P7 | band-1 MAC reset/status 0x40114500/51C/520 | 0x404CC500/51C/520 | 51C and 520 = 0xFF |
| P8 | band-1 init marker 0x40114548/54C | 0x404CC548/54C | 0x6101 |
| P9 | band-1 run-state (bandobj 0x15E570 + 0x14D0) | **0x40817A40** | u8 +1 = **2** (started), u8 +4 != 0xFF, u32 +0x64 incremented |
| P11 | RF mode byte P+0xF04 (F2 context) | CPU 0x15F474 | 3 (already set by the F2 fire) |
| P12 | RF image R+0x3AC / R+0x3A8 (F2 context) | two-step via perlink+0x2D8 | 0x03E6138A / 0x01800060 |
| P13 | on-air 5200 MHz beacon | observer 'iw dev OBS_IFACE scan freq 5200' | ch40 beacon / SSID cudy-ap-test-5g40 |

**The earlier R3 read 0x404C8220 for the 0x4010C220 frame was mis-addressed
(an extra 0x4000).  The correct device IO address is 0x404C4220.  Do not use
0x404C8220.**  Note the decode-lane FIRE-RECIPE P1.16 prints the band-1
run-state phys as 0x4081D240; the correct phys (its own tool's derivation) is
**0x40817A40**.

**R3b PASS** iff P1 = 0x1029, P9 state[1] = 2 with the counter incremented,
P2 = 0, P4 unchanged, P7/P8 consistent, **and** P13 = 5200 MHz ch40 beacon.

**0x1039 caveat:** the enable-TRX probe CA 0xE8520 writes 0x1039 to
0x4010C220; 0x1039 contains 0x1029 (subset), so a 0x1039 read is NOT proof of
a start.  Expect exactly 0x1029 and cross-check P9 state[1] = 2.

## R4 - failure modes and next moves

| observation | meaning | next |
| --- | --- | --- |
| P1 = 0x1029, P9 state[1] = 2, 5200 beacon | **R3b CLOSED** - 5 GHz AP start and beacon achieved | record; the last 5 GHz blocker is retired |
| P1 = 0x1029 / state[1] = 2, no beacon | the band-1 MAC start is armed; the remaining gap is the beacon/TBTT engine | hand to the tbtt-arm/beacon-tx lanes (no re-post) |
| P1 unchanged, state[1] != 2, dmesg bandobject=ZERO | the 0x4C524 worker's ctx+0x4B8 is absent - the VAP-add bind did not run | hand back to the boot-gate/VAP-bind lanes |
| P1 unchanged, state[1] != 2, dmesg state[4]=0xFF | no bound VAP index - CA 0x4AF0C refuses at 0x4AF32 | re-check the F1 VAP-add readback; not a start-frame problem |
| P1 = 0x1039 only | the enable-TRX probe wrote it (contains 0x1029) | treat as inconclusive; read P9; no re-post |
| fallback rungs posted, state[1] stays != 2 | CA 0x4AF0C refused (guards above) | stop, capture the full pack, analyse offline |
| frame not posted (-errno) / announce not consumed | transport refused the frame | stop the ladder; do not retry in the same boot |

Negative controls (fresh boot each, no AP frames): band_start=2 -> "REFUSED -
band_start=2 is not a valid band-start parameter (0=off, 1=post; no frame
sent"; band_start=1 beacon_static=0 -> "needs the beacon_static=1 vendor
order"; band_start_fallback=1 alone -> "needs band_start=1"; band_start=0
control (with desc_table=1 dfs_mode=1) -> no band_start line, P1 stays 0 and
the ladder is byte-identical to the F2 fire.

Abort rules (inherit): page 0x86807fef / state 1 / mask exception ->
power-cycle, no retry; never write CA 0x400392f0; never read
0x10160000..0x1016FFFF; no fire without bench link PASS + recovery card
READY + human present; record the probe output under
'build/linux-kbuild/waves/<phase>/' (gitignored).

## Why one variable

The only change vs the F2 fire module is the additional 33-byte 0x0242 frame
after the 0x01F7 dfs_mode post and before the 0x0124 channel post (plus its
param and dmesg/sysfs lines).  The 0x00F7 table, 0x01F7 mode, the r1-band
VAP-start band byte fix, the beacon_static order and the 0x0124 wait are
identical, so an R3b verdict or a re-rank is unambiguous.
