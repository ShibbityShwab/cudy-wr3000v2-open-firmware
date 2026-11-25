# FIRE-RECIPE - the explicit VAP/module index (R3b iteration 2) + ch40 AP

**Lane:** wifi-band1-start-send-2 (task-207).  **This lane never fires.**  The
Lead fires with the resident bench procedure (bench link PASS, recovery card
READY, human at the bench, UART capture read-only).  One change per fire, one
rung per fresh boot, no in-place re-post of a consumed frame.

**Module under test:** the host-built hi5622_wifi.ko of BUILD-NOTES.md /
evidence/build-artifact.txt - debug-stripped **373,056 B** sha256
`b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71`,
**64 params**, new read-only `vap_add_index` (default 0).

Base: the **fired 0x0242 module** lineage (371,212 B sha `e2312402...a028`,
63 params, fired 2026-11-28 05:00: posted+consumed, refused at the
state[4]=0xFF precondition).  This lane changes the index plumbing and adds one
read-only dmesg line; **at the fire defaults the emitted 0x0064/0x0242 bytes are
unchanged** - the new observable is the `vap_add post:` readback.

**The question this fire answers:** does the band object show the key-0x0064
bind and a band VAP list of exactly one entry (`vap_add post: ... bandobject=
nonzero bandlist_count=1`)?  If yes, the R3b state[4]=0xFF is produced later
by the 0x0242 pre-start helper CA 0x4C3FC -> CA 0x4AE7C and the next rung is
that path, NOT a VAP-add field.  If no, the readback names the failed step.

Inherit **all** non-negotiables from ../wifi-band1-start-send/FIRE-RECIPE.md:
recovery armed, UART read-only, never write CA 0x400392f0, never read the RC
misc window 0x10160000..0x1016FFFF, never rmmod the vendor modules, a
page/state/mask anomaly -> power-cycle.  Never re-post a frame already consumed
in the same boot.

---

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko   # 373056
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l   # 64
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^vap_add_index:'

STOP if the module hash/size/param count is not this lane's artifact - wrong
build.  Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh power-cycle before the load is preferred.  Record which path was taken.

## R1 - load (one load, the fired 0x0242 ladder + vap_add_index=0)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 \
        band_start=1 band_start_fallback=0

Verify the knobs (new: `vap_add_index=0`):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback vap_add_index vap_start_module; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 300 1 1 1 1 0 0 0

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

**Expected dmesg (new line in bold):**

~~~
cfg80211: start_ap beacon_static=1 plan (8 steps): vap_add desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': g0 id1 total 48 B ... handler risk=...; posted N/32
cfg80211: vap_add post: index=0 band=1 ctx=0x........ perlink=0x........ vap=0x........ bandobject=0x........ bandlist_count=1 published=0x........ (bound, band VAP list count=1 ...)
frame post 'desc_table': ... 
cfg80211: start_ap beacon_static frame 2/8 'dfs_mode' posted ...
frame post 'band_start': g0 id1 total 33 B ...
cfg80211: start_ap beacon_static frame 3/8 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - before the 0x0124 channel post
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=.. state[4]=0x.. counter=N (...)
~~~

PASS (host side): the plan line lists vap_add first; the `vap_add post:` line
appears (nonzero bandobject, bandlist_count shown); the band_start frame is
posted with field B = 0; page 0x86807FEF, state 1, mask 0.  The band_start
readback remains diagnostic (not gating).

### R2 verdict rules (this is the new result)

| `vap_add post:` observation | meaning | next |
| --- | --- | --- |
| bandobject nonzero, bandlist_count=1 | the key-0x0064 add bound the band and appended exactly one VAP | go to R3; if state[4] is still 0xFF it is the 0x4C3FC/0x4AE7C path (see R4) |
| bandobject = 0x00000000 | the key-0x0064 bind did NOT run | **STOP the 5 GHz ladder**; capture the line and hand back here - then vary `vap_add_index` (R4 controls) |
| bandlist_count = 0 | worker stopped before the list append | same as above |
| bandlist_count > 1 | the list has more than one entry (CA 0x4AE7C needs exactly 1) | fresh boot; do not re-post |

## R3 - read-only probe verification (the R3b result)

Use the committed read helper (bar0read, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  Conversion:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; **device IO phys = CA +
0x3B8000**.  The decisive rows (TABLES T6):

| # | object | **phys** | expect |
| ---: | --- | ---: | --- |
| P1 | **band-1 arm word 0x4010C220** | **0x404C4220** | **0x1029** (0 = never armed; 0x1039 = TRX probe only, NOT a start) |
| P2 | G+0x370 (NULL hook) | 0x4082CB34 | 0 |
| P4 | band-0 arm word 0x40108214 | 0x404C0214 | 0xF (reference) |
| P9 | band-1 run-state (bandobj 0x15E570+0x14D0) | **0x40817A40** | u8+1 = **2**, u8+4 = **0** (not 0xFF), u32+0x64 incremented |
| P12 | RF image R+0x3A8/0x3AC (F2 context) | two-step via perlink+0x2D8 | 0x01800060 / 0x03E6138A |
| P13 | on-air 5200 MHz beacon | observer `iw dev OBS scan freq 5200` | ch40 beacon / SSID |

**The earlier mis-addressed 0x404C8220 must not be used.**
**R3b PASS** iff P1 = 0x1029, P9 state[1] = 2 with state[4] = 0 and the
counter incremented, P2 = 0, P4 unchanged, **and** P13 = 5200 MHz ch40
beacon.

## R4 - negative controls and next rungs (fresh boot each; no re-post)

1. `vap_add_index=0 vap_start_module=1` -> START_AP REFUSED:
   `vap_add_index=0 != vap_start_module=1` (no frame).  Proves the gate.
2. `vap_add_index=19` (with `vap_start_module=19`) -> REFUSED:
   `vap_add_index=19 is outside the module table`.  Proves the bound.
3. `vap_add_index=4 vap_start_module=4 band_start=1` (optional rung):
   the key-0x0064 frame carries data[1]/data[4]/hdr[3] = 4 (host-tested bytes);
   expected `vap_add post: index=4 ...`.  Only run this after R2's line has
   been captured - it tests a different ctx, not the field value.
4. If R2 shows bandobject nonzero + count=1 but state[4] remains 0xFF and
   P1 != 0x1029: the failure is in CA 0x4C3FC -> CA 0x4AE7C (band-list scan /
   band gate CA 0x944DE / RF programme CA 0x101890), not in the VAP add.
   **Do not patch frames on that evidence** - hand the line + probes back to
   the decode lane.

Abort rules (inherit): page 0x86807fef / state 1 / mask exception ->
power-cycle, no retry; never write CA 0x400392f0; never read
0x10160000..0x1016FFFF; no fire without bench link PASS + recovery card
READY + human present; record the probe output under
`build/linux-kbuild/waves/<phase>/` (gitignored).

## Why one variable

Versus the fired 0x0242 module the only code change is the explicit VAP index
plumbing (data[4]/data[1]/hdr[3] from `vap_add_index`, equality with
`vap_start_module`) plus one read-only dmesg line.  At the fire defaults every
wire byte of the ladder is identical to the 2026-11-28 fire, so any change in
the readback or the probes is attributable to this lane's build.
