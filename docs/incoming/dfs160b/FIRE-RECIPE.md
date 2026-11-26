# FIRE-RECIPE - the 160 MHz width selector + the DFS/CAC/NOL feature

**Lane:** dfs160b (shared task **task-245**).  **This lane never fires.** The
Lead fires with the resident bench procedure: bench link PASS, recovery card
READY, a human at the bench, UART capture read-only.  One change per fire,
one rung per **fresh power-cycled boot**; a consumed frame is never re-posted
in the same boot; a page/state/mask anomaly or any UART exception -> physical
power-cycle.

**Inherited verbatim** from `../wifi-5g-dfs-relax/FIRE-RECIPE.md` and its
parents (`../wifi-5g-dfs-order/FIRE-RECIPE.md`,
`../wifi-5g-dfs-set/FIRE-RECIPE.md`, `../wifi-5g-160-dfs/FIRE-RECIPES.md`):
recovery armed first; UART read-only (never type to the tty); never write CA
0x400392f0; never read the RC misc window 0x10160000..0x1016FFFF; never
`rmmod` the vendor modules; the manual bar0write3 sub-base flip stays
**RETIRED**; no blind flash.

## Module under test (local evidence only, never committed)

Built by [tools/build-dfs160b.sh](tools/build-dfs160b.sh) on the pinned
`wifi-5g-dfs-relax` A-rung lineage (base pins in the script):

    /tmp/dfs160b-artifact/hi5622_wifi.ko
    383,020 B  sha256 006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183
    68 params  (66 base + ht_bw + dfs_cac)   vermagic 6.18.60
    .text 164,980 B sha256 5e14008fab4707afdf55ab3ea864129abfc65979c3e4b0b2e572f6593ead5faa

**STOP if the sha/size/param count is not this lane's.**  Do not load the
sibling task-240 module for the `ht_bw` rung: its width pin is the pre-5.19
UAPI (REPORT sec. 6), so a real 160 MHz chandef is refused there.

## G0 - the gates before the first frame (read-only, every rung)

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                  # page 0x86807FEF / state 1 / mask 0
    uname -r                               # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -cE '^parm:'   # 68
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^(ht_bw|dfs_cac|dfs_set|dfs_mode|band_start|desc_table|band5g|vap5g):'

**G1 (box):** pair B = IMAGE #3, page golden, a fresh physical power-cycle
before the load if the previous boot left an aborted AP.
**G2 (5 GHz on-air):** the 160 MHz / DFS **on-air** PASS is gated on the
band-1 beacon TX (the ledger 2026-11-27 05:30 verdict and the tx-scheduler
lane).  The **T1 rungs** (host + firmware state) are valid now; the **T2
jackpot** (an AP beacon observed by the observer on the target frequency) is
claimed only when D0 shows a 5 GHz AP on air today.  Do not mark a T2 rung
FAIL for the missing beacon: the host/PASS gates below decide it.
**G3 (bench):** bench link PASS + recovery READY + human present.

## D0 - the on-air baseline (read-only, before the first load)

    iw dev OBS scan freq 5200 | grep -E 'SSID|DS Parameter set|VHT Operation'

Record whether ANY 5 GHz AP is on air (the parent context says one is).  This
only decides whether the T2 jackpot applies; it does not gate the T1 rungs.

## R1 - the 160 MHz width-selector rungs

### R1a - the width rung (`ht_bw=1`, 160 MHz ch36)

Load the proven R5 A-rung ladder **plus this lane's width parameter**
(everything else exactly as `../wifi-5g-dfs-relax/FIRE-RECIPE.md` R1; the
ladder's own `dfs_set` stays 0 so only ONE variable changes):

    insmod /lib/modules/HI_RELEASE/cfg80211.ko        # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko       fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin       allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1       vap_start_mode=1 vap_start_module=0 vap_add_index=0 vap_start_g0=1       hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1       beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0       sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1       vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1 band_start=1       band_start_fallback=0 dfs_set=0 ht_bw=1

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback dfs_set ht_bw; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
    # expect: 1 1 300 1 1 1 1 0 0 1

Then the 5 GHz AP start with the lane's 160 MHz fixture
([hostapd/hostapd-160.conf](hostapd/hostapd-160.conf), ch36,
vht_oper_chwidth=2, seg0=50), release 0x5A5A only:

    iw reg set US
    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up
    cp hostapd/hostapd-160.conf /tmp/hostapd-160.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-160.conf 2>&1 | tee /tmp/hostapd-160.log

Expected dmesg (the new lines; the R5 A-rung lines are inherited):

    cfg80211: start_ap 160MHz width selector: chandef width=5 -> H2D key 0x0124 body[1]=3 (160 MHz)
    cfg80211: start_ap beacon_static frame N/M 'ap_chan' posted (... band=1 channel=36)
    cfg80211: desc_table post: module ctx=0x........ bandobject=0x........

### R1b - the legacy control (same chandef, `ht_bw=0`)

Fresh power-cycled boot, identical load line with `ht_bw=0`, identical 160 MHz
hostapd config.  Expected: **no** width-selector line, the 0x0124 wire byte
stays 0, `u32[mctx+0x10]` = `0x00000124` (bw byte 0), page golden.  This is
the byte-identical-wire control for the "legacy default" claim.

### R1c - optional 80 MHz rung (only after R1a PASS)

Same chassis on ch36 with `vht_oper_chwidth=1 vht_oper_centr_freq_seg0_idx=42`
(80 MHz): expect `width=3 -> body[1]=2 (80 MHz)` and `u32[mctx+0x10] =
0x00020124`.  Record; it pins the middle selector values.

### R1 probes (read-only; conversions and more in TABLES T11)

    M=0x........                          # mctx from the desc_table post line
    FW=$((M + 0x406B8000))                # firmware RAM view (phys)
    devmem $FW 32                         # ctx+0x00..
    devmem $((FW + 0x10)) 32              # channel|band<<8|bw<<16|idx<<24
    cat /sys/bus/pci/devices/0000:00:00.0/boot

| id | expect R1a | rule |
| ---: | --- | --- |
| D2 | the width-selector line: `width=5 -> body[1]=3` | **hard PASS** |
| D3 | ctx word `0x00030124` for ch36/160 | **hard PASS** |
| D4 | the ap_chan frame consumed (the ladder post line) | **hard** |
| D5 | R1b: no selector line, ctx word `0x00000124` | **hard PASS** (the control) |
| D13 | page golden 0x86807FEF / state 1 / mask 0 | **ABORT if not** |
| D14 | observer scan: the 160 MHz AP beacon/wide VHT operation | T2 jackpot, gated on G2 |

**R1 PASS** iff D2+D3 (R1a) and D5 (R1b) and D4 and D13.  **FAIL** on a
missing/incorrect selector line, a ctx word whose bandwidth byte is not 3, or
a ctx word without channel 0x24/band 1.  **ABORT/power-cycle** on a page
change, an exception, or a frame not consumed.

## R2 - the DFS/CAC rungs

### R2a - the cold CAC measurement (the honest fail-closed pack)

Fresh power-cycled boot; same R5 load line with `ht_bw=0` and
**`dfs_cac=1`, `dfs_set=0`** (mutually exclusive; the CAC runtime owns the
0x07E7 frames).  Start hostapd on **ch52** with
[hostapd/hostapd-dfs.conf](hostapd/hostapd-dfs.conf) (no `dfs_offload`).
hostapd sends NL80211_CMD_RADAR_DETECT before start_ap.

Expected (the fail-closed outcome, which is the PASS for this rung):

    cfg80211: dfs_cac ARM REFUSED (-11): ... - live pack mctx=0x........ band=0/1
              state=0 ec8=... cac=... ctx_present=... b4a0=0x........ regs=0x00
              (the firmware ARM worker needs the 0x0065 VAP start's started
              module context; no frame posted)

and hostapd logging the failed CAC start, then stopping.  **PASS** = the
refusal line + the pack + **no 0x07E7 frame on the wire** + page golden.
Record `mctx`, `state`, `ec8`, `b4ac` - they decide R2b.  (If the state is
already 1..3 - e.g. a leftover AP start in the same boot - the jackpot branch
is R2b's expected sequence and is graded there.)

### R2b - the warm CAC rung (the sanctioned path that opens the gate)

Fresh power-cycled boot; the SAME load line as R2a.  The 0x0065 VAP-start
step is the only writer of the module ctx state the ARM worker needs, so:

1. first run one **non-DFS 5 GHz AP start** (the inherited R5 recipe, ch40;
   `hostapd-5g40.conf` from `../wifi-5ghz-bringup/ladders/`) -> AP-ENABLED;
2. stop it **host-side** (SIGTERM to hostapd; `stop_ap_mode=0` keeps the
   firmware AP live and marks `ap_stopped_hostside`; the next start takes the
   restart_plain path);
3. start hostapd on **ch52** with the DFS fixture.  RADAR_DETECT now runs the
   precheck against the **started** module context:

Expected sequence (the new lines):

    cfg80211: dfs_cac CAC STARTED on ch52 (5260 MHz): 0x07E7{01} ARM posted,
              cac_ms=60000 (tdwr=0), ticks=1 x 60000 ms after the 63000 ms
              first timer, NOL 1800000 ms ...
    frame post 'dfs_cac_arm': g0 id1 total 29 B ... consumed after N polls ...
    (about 63 s later)
    cfg80211: dfs_cac CAC FINISHED on ch52 (cac_ms=60000, 1 tick(s)) - 0x07E7{00} CLEAR posted
    cfg80211: start_ap DFS gate PASSED for ch52 - a CAC finished on this channel inside the 1800000 ms NOL window
    cfg80211: start_ap beacon_static frame N/M 'ap_chan' posted (... band=1 channel=52)

Probes during/after the CAC (conversions: TABLES T11; `M` = mctx from the
ladder lines):

| id | when | expect | rule |
| ---: | --- | --- | --- |
| D6 | at STARTED | `cac_ms=60000 tdwr=0 ticks=1`, first timer 63000 ms | **hard** |
| D7 | 1-5 s after STARTED | six SMAC regs phys 0x404C6000/04/08/A0/A4/A8: bit0 {1,0,1,1,0,1}=0x2D (D7-a) | **hard PASS**: the firmware writer fired |
| D8 | same instant | `u8[FW+0x4A5]` (CAC flag) = 0 (register-only arm; the 0x0065 context is present) | record (hard for the register-only class) |
| D9 | after FINISHED | elapsed >= 63000 ms; hostapd received NL80211_RADAR_CAC_FINISHED | **hard** |
| D7-b | after FINISHED | the six bit0s read 0 (the CLEAR wrote them back) | **hard** |
| D10 | after FINISHED | the `start_ap DFS gate PASSED` line, then the 0x0124 ch52 frame | **hard PASS** |
| D13 | every step | page golden | **ABORT if not** |
| D14 | end | observer scan freq 5260: the DFS AP on air | T2 jackpot, G2-gated |

**R2b PASS** iff D6 + D7-a + D9 + D7-b + D10 + D13.  **FAIL** (record the
dmesg, power-cycle, report): `dfs_cac ARM REFUSED` even after the warm-up
(the state did not survive the stop - report the pack, it is a first-class
result), no `consumed after` on the ARM frame, `regs_pattern != 0x2D`, the
CLEAR not posted, the finish earlier than 63000 ms, or a start_ap that
aborts.  **Never hand-write the six registers.**

### R2c - the TDWR rung (only after R2b PASS; one long fire)

Fresh boot, same `dfs_cac=1` load; the warm-up exactly as R2b, then hostapd
on **ch120** with the DFS fixture (sed channel=120).  Expected:

    cfg80211: dfs_cac CAC STARTED on ch120 (5600 MHz): ... cac_ms=600000 (tdwr=1),
              ticks=10 x 60000 ms after the 63000 ms first timer ...
    (tick 1/10 ... tick 9/10, one per ~60 s)
    ... CAC FINISHED on ch120 (cac_ms=600000, 10 tick(s)) ...   (~603 s)

**PASS** = D6 (cac_ms=600000, tdwr=1, ticks=10) + 10 tick lines + finish
elapsed >= 600000 ms + D7-a/D7-b + D13.  This rung pins the 30-min-class
TDWR CAC constant end-to-end.  Do not shorten it by hand.

## R3 - negative controls (one fresh boot each; expect refusals, no frames)

| id | load | expect |
| ---: | --- | --- |
| N1 | `dfs_cac=1 dfs_set=1` | start_ap `REFUSED ... mutually exclusive` before any frame |
| N2 | `ht_bw=2` | start_ap `REFUSED - ht_bw=2 is not a valid width-selector parameter` |
| N3 | `dfs_cac=2` | start_ap `REFUSED - dfs_cac=2 is not a valid DFS-CAC parameter` |
| N4 | `ht_bw=1 beacon_static=0` | start_ap `REFUSED ... need the beacon_static=1 vendor order` |
| N5 | `dfs_cac=1` on a 2.4 GHz AP (band5g=0) | the 2.4 GHz ladder runs pre-feature: no CAC gate line, no 0x07E7 frame, page golden |
| N6 | sibling `../dfs160` module | **do not fire**; if it is loaded, `ht_bw=1` refuses a real 160 MHz chandef (its width pin is the old UAPI) |

**PASS** = the named refusal line + no frame posted + page golden.

## PASS / FAIL / ABORT summary

- **PASS** is per rung, exactly the probes above; the T2 on-air jackpot never
  converts a T1 PASS into a FAIL (G2).
- **FAIL**: record dmesg + probes, power-cycle, report; a consumed frame is
  never re-posted in the same boot.
- **ABORT / immediate power-cycle** on: a page/state/mask change, any UART
  exception/oops, a frame not consumed, a post error, an AP start that aborts
  unexpectedly, or any attempt to write the CRM/RC forbidden windows.
- **Retain**: dmesg, the UART capture (absolute path), the probe pack,
  `/tmp/hostapd-*.log`, and the `iw dev OBS scan` outputs.  No vendor bytes
  or secrets are committed; protocol bytes only as quoted text.
