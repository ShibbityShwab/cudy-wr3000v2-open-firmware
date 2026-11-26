# FIRE-RECIPE - the dfs_order rung: the 0x07E7 ARM BEFORE the 0x01F7 (ch40)

**Lane:** wifi-5g-dfs-order (task-228). **This lane never fires.** The Lead
fires with the resident bench procedure (bench link PASS, recovery card READY,
human at the bench, UART capture read-only). One change per fire, one rung per
fresh power-cycled boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from `../wifi-band1-start-send-5/FIRE-RECIPE.md`
(and its parents) and `../wifi-5g-dfs-set/FIRE-RECIPE.md`: recovery armed, UART
read-only (never type to the tty), never write CA 0x400392f0, never read the RC
misc window 0x10160000..0x1016FFFF, never `rmmod` the vendor modules, a
page/state/mask or register anomaly -> power-cycle, no blind flash. The manual
bar0write3 flip of the sub-base stays RETIRED.

**What changed vs the dfs_set recipe:** only the LADDER ORDER and the module
sha. The load line values are identical. The new expected dmesg has the
`dfs_set` lines BEFORE the `dfs_mode` lines. Two probe groups are new:
**D3** (the four-word mctx CAC pre-state, `+0x49C..+0x4A8`) and **D11** (the
RF-ordering consequence of moving the 0x01F7 after the 0x0124 post).

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 66
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^(dfs_set|dfs_mode|band_start|desc_table):'

**Module under test** (built by [tools/build-dfs-order.sh](tools/build-dfs-order.sh);
deploy over the LAN, never flash):

    /tmp/dfs-order-artifact/hi5622_wifi.ko
    391,408 B  sha256 a5ec150cfc7d7b8df1b87bd59f5847f0e720b772e69524c76f2d26620babdb7e
    66 params  (unchanged: the R5 65 + dfs_set; the order patch adds none)

STOP if the sha/size/param count is not this lane's (the previous dfs_set
module was 391,388 B / `1b47f0da...` - that is the OLD order, do not fire it).

Golden state gate (read-only, AFTER the module boot + ladder, never before):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh physical power-cycle before the load is mandatory if the previous boot
left an aborted AP.

## R1 - the two isolation rungs (one fresh boot each; run these BEFORE R2)

R1a and R1b are what turn the [I] causal attribution into a measured fact.
If R1b's expectation fails, **do not fire R2** - the causal model is wrong
and the next hypothesis is the 0x0065/CA 0xF593C path (see REPORT sec. 7).

### R1a - the clean pre-state (`dfs_mode=0 dfs_set=0`)

Load the R5 ladder minus both knobs (`desc_table=1 band_start=1` unchanged,
`dfs_mode=0`, `dfs_set=0`). Expect: no `dfs_mode`/`dfs_set` lines, plan
(8 steps) `vap_add desc_table ap_chan ap_ssid ap_cfg vap_start_g0 band_start
ap_vap_start`, and the probe **D3 = the four mctx CAC words all 0**:

| probe | phys | expect |
| --- | ---: | --- |
| D3a `u32(mctx+0x49C)` | mctx+0x406B8000+0x49C | **0** |
| D3b `u32(mctx+0x4A0)` | +0x4A0 | **0** |
| D3c `u32(mctx+0x4A4)` | +0x4A4 | **0** |
| D3d `u32(mctx+0x4A8)` | +0x4A8 | **0** |

and D1 sub-base bit0s all 0. This is the stock's clean pre-state on our boot;
record the raw words.

### R1b - the 0x01F7 is the CAC starter (`dfs_mode=1 dfs_set=0`)

Same load with `dfs_mode=1`. The 0x01F7 now posts AFTER `vap_start_g0`
(NOT before `ap_chan`). Expected dmesg order:
`... vap_start_g0 posted -> dfs_mode posted -> dfs_mode post: ... -> band_start ...`.
Then read **D3 again: expected NONZERO** (at least `u32(mctx+0x4A0) != 0`,
the bench's `0x0104fd90`-class pointer). That proves the 0x01F7 (in its new
position) is what makes the firmware CAC go active.
STOP + report if D3 stays all zero: the bench attribution is wrong, do not
fire R2; the next candidate is the 0x0065 worker's CA 0xF593C/0x5F450 path.
Also read D11 here (the 0x01F7 in the late slot should still write
`P+0xF04 = 3` and the R image - that is the price/benefit of the reorder).

## R2 - the fixed rung (`dfs_mode=1 dfs_set=1`, fresh power-cycled boot)

Load the proven R5 ladder plus the two knobs, nothing else changed:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 vap_start_g0=1 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 band_start=1 band_start_fallback=0 dfs_set=1

Verify every knob (the 66-param sha is the module gate; the values are the
ladder gate):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode band_start band_start_fallback vap_add_index vap_start_module vap_start_g0 dfs_set; do printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
    # expect: 1 1 300 1 1 1 1 0 0 0 1 1

STOP if `dfs_set` is not 1 or any other value differs. Then the proven boot
ladder + the 5 GHz AP interface exactly as
`../wifi-band1-start-send-5/FIRE-RECIPE.md` R1 (release 0x5A5A only):

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

then the ch40 AP start (R5 R2):

    cp ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf /tmp/hostapd-5g40.conf
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

## R3 - the expected dmesg (the NEW order)

The plan line must read the new order (9 steps with both knobs):

    cfg80211: start_ap beacon_static=1 plan (9 steps): vap_add desc_table ap_chan ap_ssid ap_cfg vap_start_g0 dfs_mode band_start ap_vap_start

The ladder lines, in this order (the 0x07E7 lines now come BEFORE the 0x01F7
lines - that is the whole fix):

~~~
... 'ap_chan' posted ... 'ap_ssid'/'ap_cfg' posted ...
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, ...)
cfg80211: vap_start_g0 post: mctx[0x0e]=1 ... (G12 pass ...)
cfg80211: dfs_set pre: mctx=0x........ band=1 state=1 ec8=1 b4a0=0x00000000 b4ac=0x........ cac=0 regs_pattern=0x00 (the ARM gates are open)
frame post 'dfs_set_arm': g0 id1 total 29 B in SR0 slot .. devva 0x........ producer 0x........ -> 0x........, coalesced announce consumed after N polls; ... posted N/32
cfg80211: start_ap beacon_static frame 'dfs_set_arm' posted (29 B, g0 id1 key 0x07E7, body 0x01) - after the 0x0065 VAP start and before the 0x01F7 dfs_mode / 0x0242 band start (task-228 order fix)
cfg80211: dfs_set arm post: cac=1 regs_pattern=0x2d (PASS - the firmware armed the band-1 SMAC sub-base + CAC flag)
cfg80211: start_ap beacon_static frame N/9 'dfs_mode' posted (29 B, g0 id1 key 0x01F7, body mode=3) - after the 0x0065 VAP start and the 0x07E7 dfs_set step (task-228 order fix: the ARM precedes this DFS/RF mode)
cfg80211: dfs_mode post: bandobject=0x........ published=0x........ attach=1 (the 0x01F7 worker's mode-write preconditions are met)
cfg80211: start_ap beacon_static frame N/9 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - ...
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=2 state[4]=0x00 counter=N ...
~~~

**STOP (FAIL) if any of these appears instead:**

* `dfs_set pre: ... b4a0=0x........ ` with **nonzero** b4a0 - the pre-state
  is still dirty; the order change did not remove the writer. Capture the four
  D3 words and power-cycle; do NOT relax the gate. Re-rank the cause
  (REPORT sec. 7).
* `dfs_set REFUSED (...)` - a gate refused before the frame; the preceding
  line names the first failing gate. Capture + power-cycle.
* The `dfs_mode` posted line appearing BEFORE the `dfs_set_arm` posted line -
  the wrong module (old order) or the patch did not take; STOP.
* The arm frame not consumed / a post error line; or
  `dfs_set arm post: cac=0 ... (FAIL ...)` - the firmware worker refused:
  capture the pack, power-cycle.
* Any UART exception/oops, page change, or the AP start aborting unexpectedly
  -> ABORT/power-cycle immediately.

## R4 - read-only probe pack (verification)

Same helper/conversions as the dfs_set recipe (`device IO phys = CA + 0x3B8000`;
firmware RAM phys = 0x40000000 + CA + 0x6B8000). The `dfs_set pre:` line gives
`mctx`; `mctx+0x406B8000` is the CPU view.

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| D1 | six sub-base bit0s | 0x404C6000/04/08/A0/A4/A8 | **{1,0,1,1,0,1} = 0x2D**; +0x08 raw 0xFFFF |
| D2 | observation words | 0x404C6010 / 0x404C6020 | 0xF5 / 0x20 unchanged |
| **D3** | **mctx CAC words** | **mctx+0x406B8000 +0x49C /+0x4A0 /+0x4A4 /+0x4A8** | **pre (the `dfs_set pre:` line): all zero; after the ARM: record the raw four words (the ARM's helpers legitimately populate them - the PASS flag is the +0x4A5 CAC flag = 1, not these words)** |
| D4 | CAC-active flag | mctx+0x406B8000+0x4A5 (u8) | **1** |
| D5 | arm gate word | mctx+0x406B8000+0x4A0 (u32) | 0 at the pre line (the gate) |
| D6 | band-1 MAC enable | 0x404C4220 (CA 0x4010C220) | 0x1039 (unchanged from R5; 0 = never armed) |
| D7 | RF mode byte | two-step: ctx0 = u32(0x171458)+u16(0x172504); perlink = u32(ctx0+0x4B0); P = u32(perlink+0xE20); **u8(P+0xF04)** | **3** (the late 0x01F7 still writes it) |
| D8 | RF image | R = u32(perlink+0x2D8); R+0x3A8 / R+0x3AC | 0x01800060 / 0x03E6138A (the dfs_mode recipe's values) |
| D9 | page golden | `cat /sys/bus/pci/devices/0000:00:00.0/boot` | 0x86807FEF; never a crash |
| D10 | on-air beacon | `iw dev OBS scan freq 5200` | ch40 AP `cudy-ap-test-5g40` - optional jackpot, NOT required for PASS |
| D11 | band-1 SMAC/PPDU producer counter | CA 0x1766D8 -> phys 0x4082E6D8 | observation; record (wifi-rx-smac-complete T5) |
| **D12** | **RF-order consequence** | **R+0x410 (bit0, the CA 0x89840 predicate latch) and R+0xF87 (channel-range branch flag; ch36-64 -> 0)** | **record.** The 0x0124-time CA 0xA2FA2 programme no longer runs (mode byte not yet 3), so these may differ from the dfs_mode recipe. If D7/D8 are right, the late 0x01F7 delivered the RF image and the trade is benign; if the R image is stale too, report it as an RF regression but the D1/D3/D4 writer PASS still stands |

**PASS** iff the dmesg order in R3 is exact, D1 shows 0x2D, D4 shows the CAC
flag 1, D7/D8 show the late 0x01F7 wrote the mode/R image (or D12 explains a
stale R image), D5/D6/D9 are unchanged/golden, and every gate line says the
expected thing. D10 is not required; D11/D12 are observations.

**ABORT / power-cycle immediately** if: the page read changes, any UART
exception/oops, the AP start aborts unexpectedly, or D1/D4 show a value outside
the pattern. Never hand-write the registers to "fix" the state in that boot.

## R5 - the ARM+CLEAR rung (`dfs_set=2`, a later fresh boot)

Same load line with `dfs_set=2` (still `dfs_mode=1`). Expected: the arm lines
as R3, then

~~~
frame post 'dfs_set_clear': g0 id1 total 29 B ... consumed ...
cfg80211: start_ap beacon_static frame 'dfs_set_clear' posted (29 B, g0 id1 key 0x07E7, body 0x00)
cfg80211: dfs_set clear post: cac=0 regs_pattern=0x00 (PASS - the firmware cleared the band-1 SMAC sub-base + CAC flag)
~~~

Probes: D4 flag = **0**, D1 bit0s = {0,0,0,0,0,0} (raw +0x08 0xFFFE). This
proves both worker branches and that CLEAR restores the pre-state.

## R6 - the fail-closed negatives (no fire needed unless desired)

* `dfs_set=3` on a fresh boot: the CLEAR pre-gate refuses
  (`dfs_set REFUSED (-1): CLEAR refused: the CAC-active flag is already 0`) and
  the AP start aborts before any frame (designed negative).
* `dfs_set=4`: parameter gate refuses at AP start (-EINVAL, before any frame).

## R7 - reset for the next experiment

Power-cycle (physical), do not keep the armed CAC state across boots. Retain
`/tmp/hostapd-5g40.log`, dmesg, the probe pack and the UART capture (absolute
path). No vendor bytes are committed; protocol bytes quoted as text only.
