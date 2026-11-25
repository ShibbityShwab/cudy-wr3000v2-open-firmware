# FIRE-RECIPE - the 5 GHz channel-worker correction: one zero-rebuild rung, then the corrected gate

**Lane:** wifi-fw-chan-worker (task-180). **HOST-ONLY lane; the Lead fires.**
This lane never contacted the router.

**What changed since the beacon lane's recipe.** The G6b rung is void: its
wait gate required the module channel-context byte `+0x13 == 1`, but that
byte is the firmware's **channel-table index**, and band 1 channel 36 is index
0 by construction (REPORT section 4, TABLES T4). The word it read,
`0x00000124`, is the correct successful store for ch36. Consequence:

* the G6b REFUSED line (`did not report channel=36 band=1 valid after 300 ms,
  word=0x00000124`, mem-entries 2026-11-27 05:30) is a host-side false
  negative - **the ordering candidate has not been tested**;
* the old gate only passes channels whose band-1 index is 1: **ch40**
  (word 0x01000128). Band-1 ch36 -> word 0x00000124, ch44 -> 0x0200012c,
  ch196 -> 0x1c0001c4 (old gate passes none of those);
* the beacon lane's expected success line `word=0x01000124` is also wrong for
  ch36: the decoded success word is **0x00000124**.

## Rung CW-0 (PRIMARY, zero rebuild): same module, 5 GHz AP on ch40

**Artifact:** the already-built beacon-lane module, unchanged - debug-stripped
**356,600 B**, sha256
`bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb`,
**59 params**, `ap_chan_wait_ms` present (default 0). Nothing is rebuilt for
this rung.

**Why it is decisive.** ch40's band-1 table index is 1, so the existing gate
(`byte+0x13 == 1`) passes for the first time, and the AP frames are posted
**after** the 300 ms wait completes rather than back-to-back with the 0x0124
frame (the G6a configuration). If the ordering/race hypothesis is right, this
is the cheapest possible test of it; if the beacon still does not appear, the
wait/ordering is not the missing piece and the read-only gate pack (CW-2)
decides whether CA 0x101890 - the 5 GHz RF programme - ran at all.

### CW-0.0 preconditions (read-only; inherit the beacon lane's G0 verbatim)

    tools/bench-link.sh            # PASS (host side)
    tools/recovery-card.sh         # READY - recovery armed, human at bench
    # UART capture running READ-ONLY (tools/uart-capture.sh, absolute path)
    uname -r                                             # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko     # expect bc1cc0ea...a1bbb (356,600 B)
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 59
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^ap_chan_wait_ms:'
    ls /lib/firmware/regulatory.db /lib/firmware/hi_wifi/FIRMWARE.PD4.bin
    iw reg set US; iw reg get            # country US / 5 GHz IR
    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                # page 0x86807FEF / state 1 / mask 0

STOP on any mismatch: wrong module, wrong page, mask != 0, no recovery.

### CW-0.1 the 5 GHz AP config on channel 40 (the only change)

    cp opensource/docs/incoming/wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf \
       /tmp/hostapd-5ghz-ch40.conf
    sed -i 's/^channel=36$/channel=40/' /tmp/hostapd-5ghz-ch40.conf
    grep -E '^(hw_mode|channel|country_code|ieee80211n|ieee80211ac)=' \
       /tmp/hostapd-5ghz-ch40.conf     # hw_mode=a, channel=40, OPEN, country US

The 5 GHz AP must be created on the same chandef: hostapd with
`channel=40` gives the driver chandef channel 40 (index 1); no other config
change.

### CW-0.2 load (one load per boot) and ladder

Exactly the beacon lane's G1/G2/G3/G4/G5, with **ap_chan_wait_ms=300**:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=300

Then the proven order (endpoint 0000 only, never 0001):
`prepare upload verify seed hcc_arm hcc_loop release` (= 0x5A5A only), then
`echo 3 > $D/h2d_ctx_create`, `echo 1 > $D/h2d_txq_sync72`,
`cat $D/h2d_status` (phase=ACTIVE ok=2 timeout=0),
`iw phy phy0 interface add wlan5 type __ap; ip link set wlan5 up`, then the
rung:

    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-ch40.conf 2>&1 | tee /tmp/hostapd-5g-ch40.log

### CW-0.3 expected lines (the pass/fail evidence)

The five-frame vendor order (all present): "start_ap beacon_static=1 plan (5
steps)"; "frame post 'vap_add'"; "posting the DEFERRED user_add ... after the
VAP add (vap5g=1, band=1 vap=0)"; "frame post 'ap_chan' ... band=1
channel=40"; ap_ssid/ap_cfg/ap_vap_start; hostapd AP-ENABLED, SSID
`cudy-ap-test-5g`.

The channel activation line (G6b shape, now with the corrected word):

    cfg80211: 5GHz channel activation: waiting up to 300 ms for the firmware
        module channel context (ctx 0x00119758, channel=40 band=1)
    cfg80211: 5GHz channel activation: firmware channel context programmed
        after N ms (word=0x01000128)          <- the pass line for CW-0

then the read-only gate pack (unchanged module):
`... module ctx readback: ... chan=0x01000128 (channel=40 band=1 field=0
valid=1) ...` and the two publish words (`band0=0x00174bf0
band1=0x00175a18` expected).

STOP (capture, power-cycle, no retry) on: a wait timeout (word should be
0x01000128; a timeout means the worker did not store - the ctx/publish lines
say why), any `announce NOT consumed` / `lane closed`, hostapd failure,
page change or mask != 0.

### CW-0.4 observer: the on-air proof (the pass condition)

    sudo iw dev OBS_IFACE scan freq 5200 | tee /tmp/scan-5g-ch40.txt

PASS = all of: (1) the "programmed ... word=0x01000128" line; (2)
AP-ENABLED `cudy-ap-test-5g`; (3) page 0x86807FEF, state 1, mask 0; (4) the
BSS on **5200 MHz** (ch 40), SSID `cudy-ap-test-5g`, DS 40, beacon interval
100, open.

### CW-0 decision tree

| observation | verdict |
| --- | --- |
| PASS (ch40 beacons on air) | **the ordering/race candidate is real**: the 5 GHz AP can beacon once the 0x0124 worker completes before the AP frames. Next: CW-1 (corrected gate on ch36) and then the beacon lane's WPA2 rung; record that ch36 failed only because of the host gate |
| wait passes, AP-ENABLED, page golden, **no beacon on 5200** | the wait/ordering is not the lever. The RF programme is the suspect: read the CW-2 pack (below) -> if the gates are 0, CA 0x101890 was skipped and that is the next lane; if the gates are 1, the gap is inside the 0x101890 RF path/beacon engine |
| wait timeout (unexpected for ch40) | the worker did not store: capture ctx word/publish words/per-link, STOP (host bug or dead per-link, not a timing result) |
| page golden:0 / exception mask sets | power-cycle; capture the first-frame evidence per PD4 rules; do not retry in place |

No beacon on 5200 is an **honest negative** and still closes the timing
candidate; do not repeat CW-0 on the same boot with other channels.

## Rung CW-2 (read-only): the 5 GHz RF-gate pack (the decisive classification)

Needs the corrected gate module built from this lane's patch.  Host build
(local; the Lead deploys the .ko to the router's module dir):

    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh   # pinned base tree (once)
    ./tools/build-chan-worker.sh
    # artifact: /tmp/chanworker-artifact/hi5622_wifi.ko
    #   358,256 B sha256 9a5b048e7e21a4212c83d75fa9542afac9c9de89e93089c25694849cf7775fa6
    #   59 params, clean build (see BUILD-NOTES.md / evidence/build-artifact.txt)

The patch adds **read-only** dmesg reads and fixes the wait predicate; it
changes no wire byte and adds no register write (`patches/0001-chan-gate-fix.patch`,
sha256 `7f72d0ed...20dccd`).  If the patched module is not deployed, the
same words can be read through the phase-25 read-only /dev/mem + BAR0 window
mechanism (h2d-trace RUNBOOK style) - never via a write.

The pack reads (CPU; BAR0 = CPU + 0x6B8000):

| read | CPU | meaning |
| --- | ---: | --- |
| ctx word | 0x119768 | expected 0x00000124 (ch36) / 0x01000128 (ch40) |
| ctx index byte | 0x11976B | the table index (0 for ch36; 1 for ch40) |
| ctx create type | 0x11975C | must be 2 (the AP create type) |
| module+0x440 | 0x119B98 | 1 = CA 0x101890 reaches the RF programme |
| band object | 0x119C10 | expected 0x0015E570 |
| bandobj+0xEC8 | 0x15F438 | 1 = CA 0x944DE passes |
| per-link | 0x119C08 | expected 0x00175A18 |
| perlink+0x1C8 | 0x175BE0 | non-zero = CA 0x97008 channel-apply runs |

Decision: `module[4]==2 && module+0x440==1 && bandobj+0xEC8==1` ->
CA 0x101890 (the 5 GHz RF programme) was invoked; any term 0 -> it was
skipped silently (the worker still returned 0 and the ctx word was stored) and
that gate is the 5 GHz failure - next lane: decode who sets module+0x440
(writer CA 0x455D0 from bandobj+0x19A1, and the config path CA 0x7100C) and
bandobj+0xEC8.

## Rung CW-1 (corrected gate on ch36; the beacon lane's G6b done right)

With the built corrected module (`9a5b048e...775fa6`; `ap_chan_wait_ms=300`): the
wait predicate is the decoded store - `word == channel | band<<8 |
body[1]<<16 | index(band,channel)<<24` - and the gate logs the index as an
index. Re-run the beacon lane's G6a/G6b rung unchanged with the 5 GHz AP on
**ch36**; the expected pass line is `word=0x00000124` (index 0). PASS is the
same as CW-0.4 but on 5180 MHz. CW-1 is the fix the beacon lane intended; CW-0
is its zero-rebuild control.

## Evidence to record (each rung)

1. `sha256sum` + `modinfo -p` of the loaded module and the exact insmod line;
2. the whole dmesg (or the `[   ] cfg80211:` lines) including the gate-pack
   and the activation lines, to an absolute path;
3. the hostapd log and `cat $D/boot $D/status` after the start;
4. the observer scan of the AP's channel (5200 for CW-0, 5180 for CW-1);
5. which rung (CW-0/CW-1/CW-2), which boot, and the decision-tree verdict.

## Safety (non-negotiable; inherit BENCH-FACTS / LESSONS / RUNBOOK)

* recovery door armed and READY before the fire; UART capture READ-ONLY;
  no human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read
  RC misc 0x10161000; no UART transmit (host-to-router TX is dead and must
  never be attempted); no blind flash;
* never run the boot ladder / hcc_arm / hcc_loop / dp_setup / release on
  endpoint 0001:00:00.0 (second host view of the same chip);
* one rung per fresh boot; exception mask != 0 / page change / golden loss
  -> power-cycle, capture, no in-place retry;
* the CW-2 pack is read-only: no register write, no firmware byte change, no
  image change. The patch only fixes the host wait predicate and adds dmesg
  reads.

## Why this rung (one paragraph)

The G6b negative was produced by a host gate that required a "valid" byte the
firmware never had: ctx+0x13 is the channel-table index (REPORT section 4).
The firmware had already stored the exact ch36 context (`0x00000124`) before
the first poll, and no wait length could ever satisfy `index == 1`. So the
only untested question is the one the beacon lane meant to ask: does letting
the 0x0124 worker finish before the AP frames matter? CW-0 answers it with the
already-built module on ch40 (index 1 -> the old gate passes) at the cost of a
hostapd config line; CW-2 classifies any negative with read-only firmware
state; CW-1 is the corrected fix for ch36. This lane's decode, not a longer
timeout, is what makes the rung decidable.
