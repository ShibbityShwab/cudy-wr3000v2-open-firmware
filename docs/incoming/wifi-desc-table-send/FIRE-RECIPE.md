# FIRE-RECIPE - the 0x00F7 descriptor-table send + ch40 AP (C1)

**Lane:** wifi-desc-table-send (task-188). **This lane never fires.** The Lead
fires with the resident bench procedure (bench link PASS, recovery card READY,
human at the bench, UART capture read-only). One change per fire, one rung per
fresh boot, no in-place retry.

**Module under test:** the host-built `hi5622_wifi.ko` of BUILD-NOTES.md /
evidence/build-artifact.txt - debug-stripped **360,448 B** sha256
`aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3`,
**60 params**, new `desc_table` param default **0**.

**The question this fire answers:** does installing the host 0x00F7 table -
with the 5 GHz descriptor record **armed** (byte2 bit7 = 0x00800000) and
country **"US"** - make the firmware band-1 predicate (CA 0x101890) return 1,
write the ch36..64 RF image (R+0x3AC = 0x03E6138A) and put the ch40 beacon on
air?  C1 is closed iff the pool/map/predicate/RF-image all flip **and** the
observer sees the beacon.

Inherit **all** non-negotiables from ../wifi-5ghz-beacon/FIRE-RECIPE.md:
recovery armed, UART read-only, never write CA 0x400392f0, never read the RC
misc window 0x10161000..0x1016ffff, never rmmod the vendor modules, never
touch endpoint 0001:00:00.0, page/state/mask anomaly -> power-cycle.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # 360448
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l   # 60
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^desc_table:'  # (uint)

STOP if the module hash/size/param count is not this lane's artifact - wrong
build.  Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

The chip state at receipt must be healthy; a fresh power-cycle before the load
is preferred (a new module load refuses to re-run the boot ladder on a chip
booted at probe).  If the healthy-chip direct-load path is taken, record which
happened.

## R1 - load (one load, desc_table=1)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1

Verify the knobs:

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1

STOP if any value differs (wrong module/params).

Then run the proven boot ladder on endpoint 0000 ONLY (same G2 as
../wifi-5ghz-beacon/FIRE-RECIPE.md: prepare/upload/verify/seed/hcc_arm/
hcc_loop/release, release **0x5A5A only**), the C2 pair (`h2d_ctx_create=3`,
`h2d_txq_sync72=1`, `h2d_status` phase=ACTIVE ok=2 timeout=0), and create the
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

```
cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add desc_table ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'desc_table': g0 id1 total 68 B in SR0 slot ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 1/6 'desc_table' posted (68 B, g0 id1 key 0x00F7, cc=US, 2 records, band1 word=0x00800201 armed, 5G bitmap=0x000000ff) - before the 0x0124 channel post
cfg80211: desc_table post: module ctx=0x........ bandobject=0x........ (the 0x00F7 worker's band-object test can pass)
cfg80211: start_ap beacon_static frame 2/6 'ap_chan' posted (...)
cfg80211: 5GHz channel activation: firmware channel context programmed after N ms (word=0x...)
...
```

(the posted counter does not count the structural VAP-add helper frame; the
desc_table step is plan step 2 of 6 and frame 1 of 6)

PASS (host side): the plan line lists `desc_table` **second**; the
`desc_table` frame is posted **before** `ap_chan`; the frame_post line
reports a **consumed** announce (no "announce NOT consumed" / no
"lane closed"); page still 0x86807FEF, state 1, mask 0.

The band-object line is diagnostic, not gating: `bandobject=0` means the
worker installs the pool then returns 0x64 (band feature bits skipped) - the
pool/map probes below still decide whether the table landed.

FAIL/gate negative controls (fresh boot each, optional but cheap; no AP
frames are sent):

    # desc_table > 1 must refuse before any frame:
    insmod ... desc_table=2 ...        # dmesg "start_ap REFUSED - desc_table=2 is not a valid descriptor-table parameter (0=off, 1=post; no frame sent"
    # desc_table=1 without beacon_static=1 must refuse:
    insmod ... beacon_static=0 desc_table=1 ...
        # dmesg "start_ap REFUSED - desc_table=1 needs the beacon_static=1 vendor order (...)"

## R3 - read-only probe verification (this is the result)

Use the committed read helper (`bar0read`, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Read:

    phys = 0x40000000 + CPU + 0x6B8000

| # | object | CPU | phys | pass |
| ---: | --- | ---: | ---: | --- |
| 1 | pool header cc + count (8 B) | 0x1ADC98 | 0x40865C98 | **"US"** 55 53, count **02** |
| 2 | pool rec1 word | **0x1ADCB0** | **0x40865CB0** | **0x00800201** (bit23 set) |
| 3 | pool rec1 bitmap | 0x1ADCBC | 0x40865CBC | **0x000000FF** |
| 4 | band-1 map ch36 row (idx 0) | 0x117AD4 | 0x407CFAD4 | byte1 = **01** |
| 5 | band-0 map ch1 row (idx 0) | 0x117B48 | 0x407CFB48 | byte1 = 00 |
| 6 | O = u32(P+0x1A00) | 0x178238 | 0x40830238 | **nonzero** |
| 6a | P+0xF04 mode byte | 0x17773C | 0x4082F73C | **nonzero** (byte 3) |
| 7 | ch40 map row (idx 1) | 0x117AD8 | 0x407CFAD8 | byte1 = **02** (rec1's slot; install ran) |

Pointer steps (all reads): `ctx0 = u32(0x171458) + u16(0x172504)`
(phys 0x40829458 / 0x4082A504); `perlink = u32(ctx0+0x4B0)` (phys chain);
P = perlink+0xE20; `O = u32(P+0x2820)`; `R = u32(O+0x2D8)`; then

| # | object | read | pass |
| ---: | --- | --- | --- |
| 8 | R+0x410 | u32 | **bit0 = 1** (the band-1 predicate saw the armed record) |
| 9 | R+0x3AC | u32 | **0x03E6138A** (ch36..64 RF image) |
| 10 | observer | `iw dev OBS_IFACE scan freq 5200` | **5200 MHz beacon**, SSID cudy-ap-test-5g40 |

**C1 CLOSED** iff: 1 = "US"/02, 2 = 0x00800201, 3 = 0xFF, 4 byte1 = 01,
7 byte1 = 02, 6/6a nonzero, 8 bit0 = 1, 9 = 0x03E6138A, 10 beacon present.

## R4 - failure modes and next moves

| observation | meaning | next |
| --- | --- | --- |
| pool cc still "99" | the install was skipped or the frame never reached the worker | check the dmesg frame_post line + frame bytes; resend with another cc only if the gate string is the cause (worker could not run -> hand back) |
| cc changed, rec1 word bit23 clear | frame truncated or body misplaced | re-check the 68 B offsets vs TABLES T2/T3; builder unit tests are green, so capture the wire bytes |
| row 4 still 0xFF, cc/word good | populator did not run (worker early-out) | read the band-object dmesg line; 0 -> worker returned 0x64 after the install |
| 8 bit0 = 1 but 9 stale | the armed bit is not the RF-image gate | hand back to the RF lane; C1 re-ranked |
| 9 = 0x03E6138A but no beacon | RF image present, descriptor closed | C2/C4/C5 lanes (sibling probes), not this lane |

Abort rules (inherit): page 0x86807fef / state 1 / mask exception ->
power-cycle, no retry; never write CA 0x400392f0; never read
0x10160000..0x1016FFFF; no fire without bench link PASS + recovery card READY
+ human present; record the probe output under
`build/linux-kbuild/waves/<phase>/` (gitignored).

## Why one variable

The only change vs the bench-fired r1-band module is the additional 0x00F7
frame before 0x0124 (plus its param and dmesg/sysfs lines).  The r1-band
VAP-start band byte fix, the beacon_static order and the 0x0124 wait are
identical, so a C1 verdict or a re-rank is unambiguous.
