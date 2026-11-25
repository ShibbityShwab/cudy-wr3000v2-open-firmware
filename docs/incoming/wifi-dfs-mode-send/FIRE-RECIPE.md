# FIRE-RECIPE - the 0x01F7 DFS/RF mode send + ch40 AP (F1)

**Lane:** wifi-dfs-mode-send (task-190). **This lane never fires.** The Lead
fires with the resident bench procedure (bench link PASS, recovery card READY,
human at the bench, UART capture read-only). One change per fire, one rung per
fresh boot, no in-place retry.

**Module under test:** the host-built hi5622_wifi.ko of BUILD-NOTES.md /
evidence/build-artifact.txt - debug-stripped **364,192 B** sha256
'f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac',
**61 params**, new 'dfs_mode' param default **0**.

**The question this fire answers:** does arming the firmware RF/DFS mode with
the stock 'set_country_for_dfs' shape - a g0-id1 config event **key 0x01F7,
len 1, body {0x03}** - make the firmware write the band object's mode byte
'P+0xF04 = 3', run the ch36..64 RF programme CA 0xA2FA2 (R+0x3AC =
0x03E6138A, R+0x3A8 = 0x01800060) and put the ch40 beacon on air?  F1 is
closed iff the mode byte / R image / predicate bit flip **and** the observer
sees the beacon.

Inherit **all** non-negotiables from ../wifi-5ghz-beacon/FIRE-RECIPE.md:
recovery armed, UART read-only, never write CA 0x400392f0, never read the RC
misc window 0x10161000..0x1016ffff, never rmmod the vendor modules, never
touch endpoint 0001:00:00.0, page/state/mask anomaly -> power-cycle.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # 364192
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l   # 61
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^dfs_mode:'  # (uint)

STOP if the module hash/size/param count is not this lane's artifact - wrong
build.  Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

The chip state at receipt must be healthy; a fresh power-cycle before the load
is preferred (a new module load refuses to re-run the boot ladder on a chip
booted at probe).  If the healthy-chip direct-load path is taken, record which
happened.

## R1 - load (one load, desc_table=1 dfs_mode=1)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1

Verify the knobs:

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1

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
cfg80211: start_ap beacon_static=1 plan (7 steps): vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'desc_table': g0 id1 total 68 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 1/7 'desc_table' posted (68 B, g0 id1 key 0x00F7, cc=US, 2 records, band1 word=0x00800201 armed, 5G bitmap=0x000000ff) - before the 0x0124 channel post
cfg80211: desc_table post: module ctx=0x........ bandobject=0x........ (...)
frame post 'dfs_mode': g0 id1 total 29 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 2/7 'dfs_mode' posted (29 B, g0 id1 key 0x01F7, body mode=3) - after the 0x00F7 table and before the 0x0124 channel post
cfg80211: dfs_mode post: bandobject=0x........ published=0x........ attach=1 (the 0x01F7 worker's mode-write preconditions are met)
cfg80211: start_ap beacon_static frame 3/7 'ap_chan' posted (...)
cfg80211: 5GHz channel activation: firmware channel context programmed after N ms (word=0x...)
~~~

(the posted counter does not count the structural VAP-add helper frame; the
desc_table step is plan step 2 of 7 and frame 1 of 7, dfs_mode 2/7, ap_chan
3/7)

PASS (host side): the plan line lists desc_table then dfs_mode then ap_chan;
the dfs_mode frame is posted **after** desc_table and **before** ap_chan; the
frame_post line reports a **consumed** announce (no "announce NOT consumed" /
no "lane closed"); page still 0x86807FEF, state 1, mask 0.

The dfs_mode readback line is diagnostic, not gating: published=0 or attach=0
means the 0x01F7 worker's preconditions are absent (it logs 1442/1448 and does
not write the mode byte) - the probes below then decide whether the band
object is missing (hand back, do not re-post).

Optional negative controls (fresh boot each, no AP frames sent):

    # dfs_mode > 1 must refuse before any frame:
    insmod ... dfs_mode=2 ...
        # dmesg "start_ap REFUSED - dfs_mode=2 is not a valid DFS-mode parameter (0=off, 1=post; no frame sent"
    # dfs_mode=1 without beacon_static=1 must refuse:
    insmod ... beacon_static=0 dfs_mode=1 ...
        # dmesg "start_ap REFUSED - dfs_mode=1 needs the beacon_static=1 vendor order (...)"
    # dfs_mode=0 control (with desc_table=1): no 0x01F7 line, mode byte stays 0,
    # R image stale - proves the 0x01F7 frame is the only change.

## R3 - read-only probe verification (this is the result)

Use the committed read helper (bar0read, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  The chain is
the **corrected** one of ../wifi-fw-rf-descriptor (task-189):

    phys = 0x40000000 + CPU + 0x6B8000
    ctx0     = u32(0x171458) + u16(0x172504)      # CPU reads
    perlink  = u32(ctx0+0x4B0)                    # follow
    P        = u32(perlink+0xE20)                 # band object; CPU 0x176838
    O        = u32(P+0x1A00)                      # published per-link; CPU 0x15FF70
    R        = u32(perlink+0x2D8)                 # RF block; CPU 0x175CF0

| # | object | read | pass |
| ---: | --- | --- | --- |
| 1 | **mode byte u8(P+0xF04)** | CPU **0x15F474** | **3** (after the 0x01F7 arm; 0 = never armed) |
| 2 | **R+0x3A8** | two-step (R -> +0x3A8) | **0x01800060** |
| 3 | **R+0x3AC** | two-step (R -> +0x3AC) | **0x03E6138A** (ch36..64 RF image) |
| 4 | **R+0x410** | two-step (R -> +0x410) | **bit0 = 1** (the CA 0x101890 predicate saw the armed 0x00F7 record) |
| 5 | on-air beacon | observer 'iw dev OBS_IFACE scan freq 5200' | **5200 MHz ch40 beacon**, SSID cudy-ap-test-5g40 |
| 6 | (context) 0x00F7 pool/map | CPU 0x1ADC98 / 0x1ADCB0 / 0x117AD4 | "US"/02, rec1 0x00800201, map byte1 01 (sibling pack) |
| 7 | (context) P / O | CPU 0x176838 / 0x15FF70 | P = 0x0015E570 (the band object), O nonzero |

**F1 CLOSED** iff: 1 = 3, 2 = 0x01800060, 3 = 0x03E6138A, 4 bit0 = 1,
5 = 5200 beacon.  If mode = 3 and the R image/bit are right but no beacon,
F1 is closed and the next lane is the beacon/engine (sibling C4/C5).

## R4 - failure modes and next moves

| observation | meaning | next |
| --- | --- | --- |
| dfs_mode frame posted, mode byte still 0, dmesg published=0/attach=0 | the 0x01F7 worker's preconditions are absent (band object not bound) | do not re-post; hand back to the boot-gate lane (P/O provenance is external, sibling rf-descriptor neg. census) |
| dfs_mode frame posted, published/attach OK, mode still 0 | the frame did not reach the worker (wire/table) | check the frame_post line is consumed, capture the 29 B wire bytes, re-check key row 93 |
| mode = 3 but R+0x3AC stale | the RF image path is gated elsewhere | hand back to the rf-programme lane; F1 re-ranked |
| mode = 3, R image good, R+0x410 bit0 = 0 | the 0x00F7 predicate did not latch | re-check the sibling pool/map probes (T7 row 6); the mode arm is sufficient for the mode byte only |
| mode = 3, R good, bit0 = 1, no beacon | descriptor + mode both armed | beacon/engine lanes (sibling C4/C5), not this lane |
| mode = 3 but the wrong R variant (e.g. 0x056B0597) | ch100+ branch (P+0xF87 = 1) - wrong channel | start ch36..64 (ch40) only |
| worker logged 1442/1448 | precondition absent (covered above) | probe P/O/attach; no blind re-post |

F1b (only if mode 3 is rejected or the R image is wrong): repeat with modes
1,2,4,5 by changing only HI5622_DFS_MODE_MODE in the lane model (one fire
per mode, keep the readback; do not blind-loop).

Abort rules (inherit): page 0x86807fef / state 1 / mask exception ->
power-cycle, no retry; never write CA 0x400392f0; never read
0x10160000..0x1016FFFF; no fire without bench link PASS + recovery card
READY + human present; record the probe output under
'build/linux-kbuild/waves/<phase>/' (gitignored).

## Why one variable

The only change vs the sibling 0x00F7 fire module is the additional 29-byte
0x01F7 frame after the 0x00F7 post and before 0x0124 (plus its param and
dmesg/sysfs lines).  The r1-band VAP-start band byte fix, the beacon_static
order, the 0x00F7 table and the 0x0124 wait are identical, so an F1 verdict or
a re-rank is unambiguous.
