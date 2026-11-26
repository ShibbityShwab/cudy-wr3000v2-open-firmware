# FIRE-RECIPE - the +0x4A0-relaxed rung: the 0x07E7 ARM reaches the firmware

**Lane:** wifi-5g-dfs-relax (task-230). **This lane never fires.** The Lead
fires with the resident bench procedure (bench link PASS, recovery card READY,
human at the bench, UART capture read-only). One change per fire, one rung per
**fresh power-cycled boot**; a consumed frame is never re-posted in the same
boot. The module here is host-built and **local evidence only**.

Inherit **all** non-negotiables from `../wifi-5g-dfs-order/FIRE-RECIPE.md` and
`../wifi-5g-dfs-set/FIRE-RECIPE.md` (and their parents): recovery armed first,
UART read-only (never type to the tty), never write CA 0x400392f0, never read
the RC misc window 0x10160000..0x1016FFFF, never `rmmod` the vendor modules, a
page/state/mask anomaly or any UART exception -> power-cycle, no blind flash.
The manual bar0write3 sub-base flip stays **RETIRED**.

**Delta vs the R2 rung:** only the module sha and the host gate policy change.
The load line values and the ladder order are identical
(`0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242`). The new expected dmesg has the four
CAC words in the `dfs_set pre:` line, a new `dfs_ctx pre0x0065:` line, and a
`dfs_set arm post:` line that grades `regs_pattern=0x2d`, `cac=0`,
`ctx_present=1`, `arm_kind=register-only`.

## R0 - host preconditions (read-only)

    uname -r                                                     # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 66
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^(dfs_set|dfs_mode|band_start|desc_table):'

**Module under test** (built by [tools/build-dfs-relax.sh](tools/build-dfs-relax.sh)):

    /tmp/dfs-relax-artifact/hi5622_wifi.ko
    392,868 B  sha256 6c50d796a80b4afd1bb008ce89d0c51d25d642eb2a35101f16dc584790d27948
    66 params (unchanged: the R5 65 + dfs_set; the relax adds none)

**STOP if the sha/size/param count is not this lane's.** The previous
`wifi-5g-dfs-order` module (391,408 B / `a5ec150c...`) still has the refusing
pre-gate - if it is loaded the A-rung will print
`dfs_set REFUSED (-16)`; that is the OLD module, do not fire it again.

Golden state gate (AFTER the module boot + ladder, never before):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status          # page 0x86807FEF / state 1 / mask 0

A fresh physical power-cycle before the load is mandatory if the previous boot
left an aborted AP.

## R1 - the A-rung (the register-only ARM), fresh power-cycled boot

Load the proven R5 ladder with `dfs_set=1`, nothing else changed:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 vap_start_g0=1 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 band_start=1 band_start_fallback=0 dfs_set=1

Verify every knob:

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode band_start band_start_fallback vap_add_index vap_start_module vap_start_g0 dfs_set; do printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
    # expect: 1 1 300 1 1 1 1 0 0 0 1 1

STOP if `dfs_set` is not 1 or any other value differs. Then the 5 GHz AP start
exactly as `../wifi-band1-start-send-5/FIRE-RECIPE.md` R1 (release 0x5A5A only):

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up
    cp ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf /tmp/hostapd-5g40.conf
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

### R1a - the expected dmesg (the new lines; order is the task-228 one)

    cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add desc_table ap_chan ap_ssid ap_cfg vap_start_g0 dfs_mode band_start ap_vap_start
    ...
    cfg80211: dfs_ctx pre0x0065: b49c=0x00000000 b4a0=0x00000000 b4a4=0x00000000 b4a8=0x00000000
    cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, ...)
    cfg80211: vap_start_g0 post: mctx[0x0e]=1 ... (G12 pass ...)
    cfg80211: dfs_set pre: mctx=0x........ band=1 state=1 ec8=1 b49c=0x0104feXX b4a0=0x0104fdXX b4a4=0x00a3XX00 b4a8=0x00000001 b4ac=0x........ cac=0 ctx_present=1 regs_pattern=0x00 (the 0x0065 already allocated the CAC context (b4a0 != 0) - the ARM is the register-only arm ...)
    frame post 'dfs_set_arm': g0 id1 total 29 B ... consumed after N polls ...
    cfg80211: start_ap beacon_static frame 'dfs_set_arm' posted (29 B, g0 id1 key 0x07E7, body 0x01) - after the 0x0065 VAP start and before the 0x01F7 dfs_mode / 0x0242 band start (task-228 order fix)
    cfg80211: dfs_set arm post: cac=0 regs_pattern=0x2d ctx_present=1 arm_kind=register-only (PASS - register-only arm: the firmware wrote the six band-1 SMAC sub-base bits (0x2D); the CAC flag is skipped (the 0x0065 CAC context is present))
    cfg80211: start_ap beacon_static frame N/9 'dfs_mode' posted ... (late 0x01F7)
    cfg80211: dfs_mode post: ...
    cfg80211: start_ap beacon_static frame N/9 'band_start' posted ...
    cfg80211: band_start post: ...

### R1b - the read-only probes (E1-E8 + observations)

Conversions: **device IO phys = CA + 0x3B8000**; **firmware RAM phys =
CA + 0x406B8000**; `mctx` comes from the `dfs_set pre:` line.

    M=0x........                                # mctx (CA) from the pre line
    FW=$((M + 0x406B8000))                      # the CPU view of the module ctx
    devmem $((FW + 0x49C)) 32; devmem $((FW + 0x4A0)) 32
    devmem $((FW + 0x4A4)) 32; devmem $((FW + 0x4A8)) 32
    for o in 0x404C6000 0x404C6004 0x404C6008 0x404C60A0 0x404C60A4 0x404C60A8; do devmem $o 16; done
    devmem 0x404C4220 32; devmem 0x4082E6D8 32
    cat /sys/bus/pci/devices/0000:00:00.0/boot
    iw dev OBS scan freq 5200

| id | probe | expect on the A-rung | rule |
| ---: | --- | --- | --- |
| **E1** | the four CAC words BEFORE the 0x0065: the `dfs_ctx pre0x0065:` line | **all 0** | **hard**: a non-zero pre-state means the phase model is wrong -> FAIL, power-cycle |
| **E2** | the four CAC words after the 0x0065: the `dfs_set pre:` line | `b4a0` = the 0x0104FD90-class block; `b49c == b4a0 + 0x118`; `b4a4` = 0x00A3XX00-class (high half 0x00A3 = the config length, byte +0x4A5 = 0); `b4a8` record (bench 1); `cac=0`, `ctx_present=1` | **hard**: b49c/b4a0 relationship + b4a4 class |
| **E3** | the `dfs_set arm post:` line | **`regs_pattern=0x2d`, `cac=0`, `ctx_present=1`, `arm_kind=register-only`** | **hard PASS** |
| **E4** | phys 0x404C6000/04/08/A0/A4/A8 (the six band-1 SMAC sub-base regs) | bit0 = **{1,0,1,1,0,1} = 0x2D**, other bits preserved (record the raw values; +0x08 raw class 0xFFFF) | **hard PASS** (this is the firmware writer firing) |
| **E5** | 0x404C4220 (CA 0x4010C220) band-1 MAC enable | 0x1039 (unchanged from R5; 0 = never armed) | record |
| **E6** | RF mode `P+0xF04` / R image `R+0x3A8/+0x3AC` (the late 0x01F7) | 3 / 0x01800060, 0x03E6138A | record (the order fix moved the 0x01F7 late; `../wifi-5g-dfs-order` D7/D8/D12) |
| **E7** | page golden `$D/boot`, `$D/status` | 0x86807FEF / state 1 / mask 0 | **hard ABORT if not** |
| **E8** | `iw dev OBS scan freq 5200` | a ch40 AP `cudy-ap-test-5g40` | optional jackpot, NOT PASS-required |
| OBS-1 | band-1 SMAC/PPDU producer counter CA 0x1766D8 -> phys 0x4082E6D8 | observation; record (../wifi-rx-smac-complete T5) | record |
| OBS-2 | `u32[mctx+0x4A8]` | bench 1, no writer in the image [U] | record |

E-probe mapping: E1 = the new pre-0x0065 line; E2 = the new four-word `dfs_set
pre:` fields (+0x4A4 = the 0xA3 length, +0x4A8 recorded); E3/E4 = the six
sub-base registers = the 0x2D bits + the CAC flag = 0; E7 = the page golden;
E8 = the 5200 scan. OBS-1/OBS-2 are observations only.

### R1c - PASS / FAIL / ABORT

**PASS** iff E1 is all zero, E2 shows the bench-class context and the
`b49c == b4a0 + 0x118` relationship, E3 shows `regs_pattern=0x2d` with
`cac=0` and `ctx_present=1`, E4 shows the 0x2D bit0 pattern (the firmware
writer fired), E7 is golden, and the R1a order is exact.

**FAIL** (record E2/E3/E4, power-cycle, report; **never hand-write the
registers**):

* `dfs_set arm post: ... regs_pattern=0x00 ... (FAIL - the register-only arm
  did not write ...)` -> the worker's own gates refused the frame; the frame
  consumed/return evidence is the discriminator.
* `regs_pattern != 0x2D` in E3/E4, or an unexpected bit outside {1,0,1,1,0,1}.
* E2 does not show the 0x0104FD90-class context (the 0x0065 producer did not
  run) -> the ladder is wrong, not the relax.
* `dfs_set REFUSED (-16)` -> the OLD (dfs_order) module is loaded; STOP.
* Any other `dfs_set REFUSED (...)` -> the line names the first failing gate;
  capture the pack and power-cycle.

**ABORT / power-cycle immediately** on: a page/state/mask change, any UART
exception/oops, the ARM frame not consumed (no `consumed after` in the post
line), a post error, or the AP start aborting unexpectedly.

## R2 - the C-BASE control (corrected baseline; fresh boot)

Same module, `dfs_set=0 vap_start_g0=1`, the rest of the ladder unchanged.
Expected: the `dfs_ctx pre0x0065:` line is all zero **before** the 0x0065;
there are **no** `dfs_set` lines/frames; after the AP start the four words at
`FW+0x49C..+0x4A8` are **NONZERO** (the bench 0x0104FD90-class context - the
0x0065 producer CA 0x5F450 ran). The clean zero state is E1 only, not "after
the ladder" (the retired R1a baseline).

PASS = E1 zero + four words nonzero after the 0x0065 + no `dfs_set` lines.
This isolates the 0x0065 as the +0x4A0 writer and is the reference for E2.

## R3 - the C-NOG0 control (the ARM without the 0x0065; fresh boot)

`vap_start_g0=0 dfs_set=1`, the rest of the ladder unchanged. With the
fail-closed pre-gate the ARM is **refused before the wire** on the
started-module state gate: expect no `0x0065` frame and

    cfg80211: dfs_set pre: ... state=0 ... cac=0 ctx_present=? regs_pattern=0x00
    cfg80211: dfs_set REFUSED (-11): mctx[0x0E] is not 1..3 - the CA 0xA4C0E started-module scan returns 0 (post the 0x0065 VAP start first)

then the AP start aborting. The six sub-base registers stay 0. **PASS = the
host refuses and no register changes.** Note: the cac-context sketch expected
the worker's gate-3 no-op (CA 0xA4C0E returns 100); with this module the state
gate refuses one step earlier, so the worker is never reached - the control
still proves fail-closed behaviour (no frame, no register change, no band
start on an unverified writer).

## R4 - the ARM+CLEAR rung (`dfs_set=2`; later fresh boot, expected refusal)

With a present CAC context the register-only ARM leaves the CAC flag 0, so the
ARM_CLEAR pre-clear check refuses (by design):

    cfg80211: dfs_set arm post: cac=0 regs_pattern=0x2d ctx_present=1 arm_kind=register-only (PASS - register-only arm: ...)
    cfg80211: dfs_set REFUSED: ARM_CLEAR read back pattern=0x2d cac=0 ctx_present=1 after the ARM (the CLEAR needs the armed pattern + flag 1; with a present CAC context the ARM is register-only and never sets the flag) - clear NOT posted, use dfs_set=1 on this rung

PASS = the refusal above and the AP start aborting; the CLEAR frame is NOT
posted. Do not "fix" this by hand.

## R5 - reset / retention

Power-cycle (physical) after every rung; do not keep an armed/aborted AP state
across boots. Retain dmesg, the UART capture (absolute path), the probe pack and
`/tmp/hostapd-5g40.log`. Never commit vendor bytes; protocol bytes only as
quoted text. The module is local evidence (`/tmp`), never committed.
