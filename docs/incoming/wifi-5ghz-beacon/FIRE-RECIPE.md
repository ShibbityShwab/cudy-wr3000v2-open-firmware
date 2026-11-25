# FIRE-RECIPE - the 5 GHz AP's channel activation (fail-closed candidate)

**Lane:** wifi-5ghz-beacon (task-175). **The Lead fires**; this lane never
contacted the router. Module under test: the host-built `hi5622_wifi.ko` of
`BUILD-NOTES.md` / `evidence/build-artifact.txt` - debug-stripped
**356,600 B** sha256
`bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb`,
**59 params**, new `ap_chan_wait_ms` param default **0**.

**This is a two-rung experiment, one rung per fresh boot, no in-place
retry.** Rung G6a (wait 0) is the decisive read-only gate pack; rung G6b
(wait 300) is the candidate fix. If G6a already shows the firmware band-1
publish word 0, G6b is expected to time out by design and the next lane is
firmware-side - do not improvise a different value.

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK):**

* recovery door armed, UART capture running READ-ONLY, bench link PASS;
  no human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read
  RC misc 0x10161000; no UART transmit; no blind flash;
* **never run the boot ladder / hcc_arm / hcc_loop / dp_setup / release on
  endpoint 0001:00:00.0** (second host view of the same chip);
* exception mask != 0 / page change / golden loss -> power-cycle, capture
  evidence, no retry in place.

**Chip state at receipt:** healthy - page GOLDEN:1, exception mask
0x00000000, gate 0xdeaf (E2 fire). The E2 module was loaded; a new module
load normally needs a fresh power-cycle (the driver refuses to re-run the
boot ladder on a chip booted at probe). Preferred: fresh power-cycle. If the
load path detects the already-booted chip and goes golden without touching
the ladder, the rung may proceed on the healthy chip; record which happened.
(Previous lane note: "the next fire can load the module without a power cycle
if needed".)

---

## G0 - preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect bc1cc0ea...a1bbb (356,600 B) - THIS lane artifact
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 59
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^ap_chan_wait_ms:' # (uint)
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^vap5g:' # (uint)
    ls /lib/firmware/regulatory.db
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

Regulatory preflight (5 GHz AP is refused without a country domain):

    iw reg set US
    iw reg get                                 # country US / 5 GHz IR

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                      # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/param count is not this lane artifact: wrong build.

## G1 - load with band5g=1, vap5g=1 and the rung's ap_chan_wait_ms (one load)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1 ap_chan_wait_ms=<0 for G6a / 300 for G6b>

Expected dmesg: the same TWO-BAND load lines as the 5 GHz bring-up recipe
(primary endpoint "2 band(s): 2.4GHz + 5GHz", endpoint 0001 "SECONDARY host
view ... no wiphy allocated", the band5g profile lines).

    cat /sys/module/hi5622_wifi/parameters/band5g            # 1
    cat /sys/module/hi5622_wifi/parameters/vap5g             # 1
    cat /sys/module/hi5622_wifi/parameters/ap_chan_wait_ms   # 0 or 300

STOP: wrong band5g/vap5g/ap_chan_wait_ms = wrong module/params - do not
continue.

## G2 - boot ladder on endpoint 0000 ONLY (proven order)

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare
    echo 1 > $D/upload
    echo 1 > $D/verify
    echo 1 > $D/seed
    echo 1 > $D/hcc_arm
    echo 1 > $D/hcc_loop
    echo 1 > $D/release          # 0x5A5A only; NEVER 0xCECE here

Expected: all rc=0; page 0x86807FEF/7/2/0xABCD; ready handshake decoded;
"cfg80211: wiphy registered after the ready handshake (2 band(s): 2.4GHz +
5GHz ...)". (Skip the ladder only if the healthy-chip direct-load path was
taken; then record the page/state and continue - the ladder must already be
running from the E2 boot, which G0's golden gate proves.)

## G3 - wiphy assertions (read-only)

    ls /sys/class/ieee80211/            # exactly phy0
    iw phy phy0 info | sed -n '/Band /,/^$/p'
    cat $D/cfg80211_status              # bands=2 band5g=1

PASS: one wiphy; 5 GHz lists the vendor 29-channel table with HT 0x0062 /
VHT 0x00400064. FAIL: 2.4-only phy or two phys -> wrong module/params, stop.

## G4 - C2 ladder on 0000 (ordered pair)

    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                   # phase=ACTIVE ok=2 timeout=0

## G5 - create the 5 GHz AP interface (vap5g vendor order)

    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

PASS (all of):

* "cfg80211: user_add (g0 id7/id8) DEFERRED - vap5g=1 vendor order ...";
* **NO** "frame post 'user_add'" line here;
* **NO** "announce NOT consumed" / "lane closed" line;
* page still 0x86807FEF, state 1, mask 0.

FAIL: any of the above -> STOP, capture, power-cycle. No hostapd start.

## G6 - 5 GHz open AP on phy0, channel 36 (the rung)

    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf 2>&1 \
        | tee /tmp/hostapd-5g-beacon.log

Config: `../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf` (hw_mode=a,
channel=36, 20 MHz, OPEN, beacon_int 100, country US).

Expected dmesg for the vendor order (all of): "start_ap beacon_static=1 plan
(5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start"; "frame post
'vap_add' ... consumed"; "posting the DEFERRED user_add ... after the VAP
add (vap5g=1, band=1 vap=0)"; "frame post 'ap_chan' ... band=1 channel=36";
then ap_ssid/ap_cfg/ap_vap_start; hostapd "wlan5: AP-ENABLED", SSID
cudy-ap-test-5g; page golden, mask 0.

**The lane's new read-only gate pack** (logged right after the ap_chan post):

    cfg80211: start_ap beacon_static module ctx readback: slot0=... base=...
        ctx=... chan=0x... (channel=.. band=.. field=.. valid=..) vap=...
        perlink=... bandobject=...
    cfg80211: start_ap beacon_static band publish words: band0=0x........
        (live bank0 0x00174bf0) band1=0x........ (live bank1 0x00175a18) ...

and in G6b (wait=300) one of:

    cfg80211: 5GHz channel activation: waiting up to 300 ms for the firmware
        module channel context (ctx 0x..., channel=36 band=1)
    cfg80211: 5GHz channel activation: firmware channel context programmed
        after N ms (word=0x01000124)          <- wait succeeded
    cfg80211: start_ap beacon_static REFUSED - the firmware module channel
        context did not report channel=36 band=1 valid after 300 ms ...
                                               <- wait timed out, no AP frame

### G6a (wait 0): the diagnostic control - one rung

PASS (diagnostic): the two gate-pack lines are present and the band publish
line decodes as one of:

| band0 | band1 | meaning |
| --- | --- | --- |
| 0x00174bf0 | 0x00175a18 | both firmware bank contexts live: the 5 GHz failure is downstream (RF/beacon engine or timing) -> run G6b on a fresh boot |
| 0x00174bf0 | 0 | **the firmware band-1 link/bank context is dead**: no host frame can make 5 GHz beacons; STOP, hand to a firmware link-layer lane; the timing candidate cannot help |
| anything else | ... | record exactly; do not interpret a guessed context (STOP) |

No beacon is expected in G6a (it is the E2 configuration plus corrected
readbacks). Its PASS is the gate pack, not the beacon.

### G6b (wait 300): the candidate - fresh boot, one rung

PASS (all of):

1. dmesg "5GHz channel activation: firmware channel context programmed after
   N ms (word=0x01000124)" - the firmware 0x0124 worker programmed band 1
   channel 36;
2. the G6a ladder lines above + hostapd AP-ENABLED, SSID cudy-ap-test-5g;
3. page 0x86807FEF, state 1, mask 0 after the start; no exception;
4. **G7 observer: the beacon is on air.**

FAIL: a wait timeout line = the worker did not program band 1: record
ctx/word/publish words, STOP (the timing candidate is falsified; the next
step is the firmware channel worker/band-1 object, not a bigger wait). Any
"announce NOT consumed" / "sequence aborted" / hostapd failure: STOP,
power-cycle, capture.

## G7 - external observer: the on-air 5 GHz proof

    sudo iw dev OBS_IFACE scan freq 5180 | tee /tmp/scan-5g-beacon.txt

PASS: the BSS appears on 5180 MHz (ch 36), SSID cudy-ap-test-5g, DS 36,
beacon interval 100, privacy=false (optionally beacon subtype 8 in a monitor
capture at ~100 TU).

## If PASS holds

Record: current sha256, the gate-pack lines, the wait line, page line, and
the observer scan. The 5 GHz AP is on air with the vendor message order +
synchronous channel activation; the next rung is WPA2 (fresh boot, same
module, `../wifi-5ghz-bringup/ladders/hostapd-5ghz-wpa2.conf`), never a
hostapd restart on the same load.

## Why this rung (one paragraph)

Vendor decode (`REPORT.md` 2.3): the vendor's 0x0124 channel config is
synchronous - it waits for the firmware's channel-config response before the
SSID/AP-config/VAP-start frames - and its VAP create/start likewise wait.
Our ladder posts all frames back-to-back and only drains the H2D announce, so
the AP config/VAP start can overtake the 0x0124 worker that programs the
5 GHz RF channel. The parameter does not guess a sleep: it waits on the
firmware worker's own body at `module_ctx+0x10` (channel/band/valid), and
aborts before any AP frame if the firmware never reports the channel. The
E2 readback zeros were mis-addressed (REPORT 3), so G6a's corrected gate pack
is the first true look at the firmware band state - and it decides whether
the candidate can work at all.
