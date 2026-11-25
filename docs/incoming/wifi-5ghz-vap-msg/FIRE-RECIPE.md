# FIRE-RECIPE - the 5 GHz AP with the fail-closed vap5g vendor order

**Lane:** wifi-5ghz-vap-msg (task-174). **The Lead fires**; this lane never
contacted the router. Module under test: the host-built hi5622_wifi.ko of
`BUILD-NOTES`/`evidence/build-artifact.txt` - stripped **353,652 B** sha256
`13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf`,
**58 params**, new `vap5g` param default **0**.

**This is one-shot.** If a PASS/FAIL gate below does not hold, STOP, record,
power-cycle. Do not reload the module twice, do not restart hostapd, do not
improvise a rung.

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK):**

* recovery door armed, UART capture running READ-ONLY, bench link PASS;
  no human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read
  RC misc 0x10161000; no UART transmit; no blind flash;
* **never run the boot ladder / hcc_arm / hcc_loop / dp_setup / release on
  endpoint 0001:00:00.0** (second host view of the same chip);
* exception mask / page change / mask != 0 -> power-cycle, capture evidence,
  no retry in place.

---

## G0 - preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect 13e7bf4c...2cc1abf (353,652 B) - the lane artifact
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 58
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^vap5g:' # (uint)
    ls /lib/firmware/regulatory.db
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

Regulatory preflight (5 GHz AP is refused without a country domain):

    iw reg set US
    iw reg get                                 # country US / 5 GHz IR

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                      # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/param count is not the lane artifact: wrong build.

## G1 - load with band5g=1 AND vap5g=1 (one load, no retry)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1 vap5g=1

Expected dmesg: the same TWO-BAND load lines as the 5 GHz bring-up recipe
(primary endpoint "2 band(s): 2.4GHz + 5GHz", endpoint 0001 "SECONDARY host
view ... no wiphy allocated", the band5g profile lines).

    cat /sys/module/hi5622_wifi/parameters/band5g   # 1
    cat /sys/module/hi5622_wifi/parameters/vap5g    # 1

STOP: no "SECONDARY host view" line, band5g != 1 or vap5g != 1 = wrong
module/params (57-param module or a typo) - do not continue.

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
5GHz ...)".

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

## G5 - create the 5 GHz AP interface (THIS is the rung that used to wedge)

    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up

PASS (all of):

* dmesg: "cfg80211: VAP 'wlan5' added (type AP, addr ...) - netdev model
  live; firmware user_add (g0 id7/id8) deferred to start_ap (vap5g=1 vendor
  order: the AP VAP add carries the band first)";
* dmesg: "cfg80211: user_add (g0 id7/id8) DEFERRED - vap5g=1 vendor order:
  the AP VAP add carries the band first, so the user add is posted after it
  in start_ap (netdev stays live)";
* **NO** "frame post 'user_add'" line here (the user add must not be on the
  wire from interface creation);
* **NO** "announce NOT consumed", "h2d_timeout", "lane closed" line;
* page still 0x86807FEF, state 1, mask 0 (read $D/boot $D/status).

FAIL: any of the above -> the order hypothesis is falsified. STOP, capture
the exact frame log + out[0]/out[2]/mask + UART, power-cycle, report. No
retry, no hostapd start.

## G6 - 5 GHz open AP on phy0, channel 36 (the core rung)

    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf 2>&1 \
        | tee /tmp/hostapd-5g-vapmsg.log

Config: `../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf`
(hw_mode=a, channel=36, 20 MHz, OPEN, beacon_int 100, country US).

PASS (all of):

1. dmesg: "start_ap beacon_static=1 plan (5 steps): vap_add ap_chan ap_ssid
   ap_cfg ap_vap_start";
2. dmesg: "frame post 'vap_add': ... consumed 0 polls" - the band-bearing
   VAP create is the FIRST wire message of the rung;
3. dmesg: "cfg80211: start_ap vendor order: posting the DEFERRED user_add
   (g0 id7/id8) after the VAP add (vap5g=1, band=1 vap=0)";
4. dmesg: "frame post 'user_add': ... consumed" and "frame post
   'user_add_notify': ... consumed" - **the failing frame now consumed (the
   primary PASS);**
5. dmesg: "cfg80211: VAP 'wlan5' firmware user_add committed (g0 id7,
   user_id=0 vap_id=0)" (may appear twice: deferred post + add log);
6. dmesg: "'ap_chan' posted ... band=1 channel=36" + channel ctx readback
   (channel=36 band=1) -> ap_ssid -> ap_cfg -> ap_vap_start after ap_cfg;
7. hostapd: "wlan5: AP-ENABLED", SSID cudy-ap-test-5g;
8. page 0x86807FEF, state 1, mask 0 after the start; no firmware exception.

FAIL: any "announce NOT consumed" (especially after step 3),
"deferred user_add failed", "sequence aborted", hostapd "Could not set
beacon parameters" / "Could not determine operating frequency", mask != 0 or
page change -> STOP, power-cycle, capture dmesg + /tmp/hostapd-5g-vapmsg.log +
UART. Record which step failed and the frame label/polls.

## G7 - external observer: the on-air 5 GHz proof

    sudo iw dev OBS_IFACE scan freq 5180 | tee /tmp/scan-5g-vapmsg.txt

PASS: the BSS appears on 5180 MHz (ch 36), SSID cudy-ap-test-5g, DS 36,
beacon interval 100, privacy=false; beacon subtype 8 captured in monitor at
~100 TU.

## If PASS holds

Record: current sha256, the 8 PASS lines, page line, observer scan.  The
5 GHz AP is up with the vendor message order; the next rung is WPA2 (same
module, fresh boot, `../wifi-5ghz-bringup/ladders/hostapd-5ghz-wpa2.conf`),
never a hostapd restart on the same load.

## Why this order (one paragraph)

The vendor's cfg80211 .add_virtual_intf is a stub returning -EOPNOTSUPP: the
vendor never posts a user add at interface creation.  Its AP VAP is created
with the band first (band id parameter+0x12, chip +0x13) and its users follow
it; the AP start proper is channel(0x0124) -> SSID(0x0005) -> auth(0x006D) ->
AP config(0x0127) -> VAP start(0x0065).  On our one-radio/two-band wiphy the
band is not known at interface creation, so `vap5g=1` moves the user add to
after the structural VAP add in start_ap - the vendor order - while the frame
bytes keep the decoded encoding (user add on mac chip 0, band only in the VAP
create).  Full decode: `../REPORT.md` sections 2-8.
