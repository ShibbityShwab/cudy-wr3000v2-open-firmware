# FIRE-RECIPE - the 0x07E7 DFS SET rung on the R5 5 GHz AP ladder (ch40)

**Lane:** wifi-5g-dfs-set (task-227). **This lane never fires.** The Lead fires
with the resident bench procedure (bench link PASS, recovery card READY, human at
the bench, UART capture read-only). One change per fire, one rung per fresh
power-cycled boot, no in-place re-post of a consumed frame.

Inherit **all** non-negotiables from `../wifi-band1-start-send-5/FIRE-RECIPE.md`
(and its parents): recovery armed, UART read-only (never type to the tty), never
write CA 0x400392f0, never read the RC misc window 0x10160000..0x1016FFFF, never
`rmmod` the vendor modules, a page/state/mask or register anomaly -> power-cycle,
no blind flash. **The manual bar0write3 flip of the sub-base is RETIRED** - this
rung uses the firmware's own writer; do not hand-write those registers.

## R0 - host preconditions (read-only)

    uname -r                                        # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | wc -l    # 66
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^(dfs_set|dfs_mode|band_start|desc_table):'

**Module under test** (built by `tools/build-dfs-set.sh`; deploy over the LAN,
never flash):

    /tmp/dfs-set-artifact/hi5622_wifi.ko
    391,388 B  sha256 1b47f0daff7e2e48d0ac6dcbc34b4135872f6a16088e8d20b88c71d47c684acb
    66 params  (65 + the new dfs_set; the sibling R5 module has 383,240 B / 24f7221b / 65)

STOP if the sha/size/param count is not this lane's. The loader drops
MODULE_PARM_DESC strings, so the 66-param sha is the gate.

Golden state gate (read-only, AFTER the module boot + ladder, never before):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0

A fresh physical power-cycle before the load is mandatory if the previous boot
left an aborted AP.

## R1 - the baseline rung (dfs_set=0, one boot): prove the pre-state

This rung is only needed if the current bench record's baseline is stale. It is
**the same R5 load** without the new step. Read the six sub-base registers and
confirm the unarmed baseline (this is the state the ARM must change):

| probe | phys | expect on our boot (2026-11-26 21:00 record) |
| --- | ---: | --- |
| CA 0x4010E000 | 0x404C6000 | u32 bit0 = **0** (record: 0) |
| CA 0x4010E004 | 0x404C6004 | bit0 = 0 |
| CA 0x4010E008 | 0x404C6008 | bit0 = 0 (record: raw **0xFFFE**) |
| CA 0x4010E0A0 | 0x404C60A0 | [U] - first read this fire |
| CA 0x4010E0A4 | 0x404C60A4 | [U] |
| CA 0x4010E0A8 | 0x404C60A8 | [U] |
| CA 0x4010E010 | 0x404C6010 | 0xF5 (observation word, never touched) |
| CA 0x4010E020 | 0x404C6020 | 0x20 (observation word, never touched) |

Use the committed read helper (bar0read; phys addresses, O_RDONLY, refuses the RC
misc window); device IO phys = CA + 0x3B8000. **Do not use bar0write.**

## R2 - the rung (dfs_set=1, fresh power-cycled boot)

Load the proven R5 ladder plus `dfs_set=1` (one added knob, nothing else
changed):

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 vap_start_g0=1 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 band_start=1 band_start_fallback=0 dfs_set=1

Verify every knob (the 66-param sha is the module gate; the values are the
ladder gate):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode band_start band_start_fallback vap_add_index vap_start_module vap_start_g0 dfs_set; do printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
    # expect: 1 1 300 1 1 1 1 0 0 0 1 1

STOP if `dfs_set` is not 1 or any other value differs. Then the proven boot
ladder on endpoint 0000 ONLY + the C2 pair + the 5 GHz AP interface exactly as
`../wifi-band1-start-send-5/FIRE-RECIPE.md` R1 (release 0x5A5A only):

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

then the ch40 AP start (R5 R2):

    cp ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf /tmp/hostapd-5g40.conf
    sed -i 's/^ssid=.*/ssid=cudy-ap-test-5g40/; s/^channel=.*/channel=40/' /tmp/hostapd-5g40.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5g40.conf 2>&1 | tee /tmp/hostapd-5g40.log

## R3 - the expected dmesg (the new lines in the R5 sequence)

The R5 plan line is unchanged (`plan (9 steps): ...`); the new step is **not** a
plan label, it posts its own frame between `vap_start_g0` and `band_start`:

~~~
... (R5 lines: vap_add / desc_table / dfs_mode / ap_chan / 5GHz channel activation /
     ap_ssid / ap_cfg / "AP config posted BEFORE the 0x0065 ...") ...
cfg80211: start_ap beacon_static frame N/9 'vap_start_g0' posted (40 B, g0 id1 key 0x0065, ...)
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=.. state4=0x00 vap=0x.. (G12 pass ...)
cfg80211: dfs_set pre: mctx=0x........ band=1 state=1 ec8=1 b4a0=0x00000000 b4ac=0x........ cac=0 regs_pattern=0x.. (the ARM gates are open)
frame post 'dfs_set_arm': g0 id1 total 29 B in SR0 slot .. devva 0x........ producer 0x........ -> 0x........, coalesced announce consumed after N polls; ... posted N/32
cfg80211: start_ap beacon_static frame 'dfs_set_arm' posted (29 B, g0 id1 key 0x07E7, body 0x01) - after the 0x0065 VAP start and before the 0x0242 band start
cfg80211: dfs_set arm post: cac=1 regs_pattern=0x2d (PASS - the firmware armed the band-1 SMAC sub-base + CAC flag)
cfg80211: start_ap beacon_static frame N/9 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - ...
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=2 state[4]=0x00 counter=N ... 
~~~

**STOP (FAIL) if any of these appears instead:**

* `dfs_set REFUSED (...)` - a gate refused before the frame (the preceding line
  carries the probe pack and the first failing gate; capture it and
  power-cycle; do NOT relax the gate in that boot).
* The arm frame not consumed / a post error line.
* `dfs_set arm post: cac=0 ... (FAIL - ...)` - the firmware worker refused the
  ARM (its own gates); capture the pack, power-cycle.

## R4 - the read-only probe pack (verification)

Same helper/conversions as R5 (`device IO phys = CA + 0x3B8000`; firmware RAM
phys = 0x40000000 + CA + 0x6B8000). The `dfs_set pre:` dmesg line already gives
`mctx`; the CAC flag address is `mctx + 0x406B8000 + 0x4A5`.

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| D1 | six sub-base bit0s | 0x404C6000/04/08/A0/A4/A8 | **{1,0,1,1,0,1} = 0x2D**; +0x08 raw **0xFFFF** on our boot (the arm preserves the other bits; `0xFFFE -> 0xFFFF`) |
| D2 | observation words | 0x404C6010 / 0x404C6020 | 0xF5 / 0x20 unchanged |
| D3 | CAC-active flag | mctx+0x406B8000+0x4A5 (u8) | **1** |
| D4 | arm gate word | mctx+0x406B8000+0x4A0 (u32) | **0** (was 0 before the arm; the arm does not set it) |
| D5 | band-1 MAC enable | 0x404C4220 (CA 0x4010C220) | 0x1039 (unchanged from R5; 0 = never armed) |
| D6 | 5 GHz RF mode / image | via the R5 pack | mode 3 + R+0x3A8=0x01800060 / R+0x3AC=0x03E6138A |
| D7 | band-1 run-state | bandobj 0x15E570+0x14D0 -> phys 0x40817A40 | state[4]=0x00, state[1]=2 (unchanged from R5) |
| D8 | page golden | `cat /sys/bus/pci/devices/0000:00:00.0/boot` | 0x86807FEF; never a crash |
| D9 | on-air beacon | `iw dev OBS scan freq 5200` | ch40 AP `cudy-ap-test-5g40` - **the optional jackpot** (see below) |
| D10 | band-1 SMAC/PPDU producer counter | band1 object 0x175A18 + 0xCC0 = CA 0x1766D8 -> phys **0x4082E6D8** | the committed producer entry counter (wifi-rx-smac-complete T5); the 2026-11-26 bench record has the stock's band-1 counter word = `0x0E0F0F` vs ours 0 (interpretation [U]). Read-only; a **movement** after the arm would show the band-1 SMAC/ISR path came alive |

**PASS** iff D1-D3 show the firmware's writer took effect (pattern 0x2D + flag 1),
D2/D5/D6/D7 are unchanged from R5, D8 is golden, and every new dmesg line is the
expected one. D10 is an observation (no expected value this rung; record it). **D9 is NOT required for PASS of this rung** - the honest
expectation is that the beacon still needs the deeper TBTT/CAC-completion path
(`hmac_start_all_bss_of_band` is host code we do not run); if D9 passes, the
arm path also kicked the SMAC init and that is the 5 GHz jackpot - record it.

**ABORT / power-cycle immediately** if: the page read changes, any UART
exception/oops, the AP start aborts unexpectedly, or D1/D3 show a value outside
the pattern (e.g. raw +0x08 unchanged 0xFFFE with the flag 1 -> the register
stage and the flag stage disagree; capture and stop). Never hand-write the
registers to "fix" the state in that boot.

## R5 - the ARM+CLEAR rung (dfs_set=2, a later fresh boot)

Same load line with `dfs_set=2`. Expected: the arm lines as R3, then

~~~
frame post 'dfs_set_clear': g0 id1 total 29 B ... consumed ...
cfg80211: start_ap beacon_static frame 'dfs_set_clear' posted (29 B, g0 id1 key 0x07E7, body 0x00)
cfg80211: dfs_set clear post: cac=0 regs_pattern=0x00 (PASS - the firmware cleared the band-1 SMAC sub-base + CAC flag)
~~~

Probes: D3 flag = **0**, D1 bit0s = **{0,0,0,0,0,0}** (raw +0x08 0xFFFE). This
proves both worker branches and that the clear restores the pre-state. It also
shows the CLEAR path only writes when it reads the armed pattern (a CLEAR on a
non-armed sub-base is a no-op - fail-closed).

## R6 - the fail-closed negative (no fire needed unless desired)

* `dfs_set=3` on a fresh boot: the CLEAR pre-gate refuses
  (`dfs_set REFUSED (-1): CLEAR refused: the CAC-active flag is already 0`) and
  the AP start aborts **before any frame**. This is the designed negative test
  of the gate model (expected, not a failure of the rung).
* `dfs_set=4`: the parameter gate refuses at AP start (`-EINVAL` before any
  frame).

## R7 - reset for the next experiment

Power-cycle (physical), do not keep the armed CAC state across boots; the CAC
flag/sub-base state is firmware RAM/device state and a fresh boot re-establishes
the baseline. The retained artifacts: `/tmp/hostapd-5g40.log`, dmesg, the probe
pack and the UART capture (absolute path). No vendor bytes are committed;
protocol bytes quoted as text only.
