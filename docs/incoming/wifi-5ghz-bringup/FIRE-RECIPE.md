# FIRE-RECIPE - 5 GHz one-radio/two-band AP ladder (DESIGNED, NOT FIRED)

**Lane:** wifi-5ghz-bringup (task-154). **The Lead fires**; this lane never
contacted the router. Module under test: the host-built hi5622_wifi.ko of
BUILD-NOTES.md - stripped **351,008 B** sha256
1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53, **57 params**.
Nothing in this file is executed by the lane.

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK):**

* recovery door armed, UART capture running READ-ONLY, bench link PASS;
  no human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read
  RC misc 0x10161000; no UART transmit; no blind flash;
* one AP start per module load (the stability gate refuses a second; repeated
  hostapd kill/restart cycles crashed the firmware via the stale-VAP latch on
  2026-11-25 - use a FRESH boot for the WPA2 rung if both are wanted);
* exception mask / page change / mask != 0 -> power-cycle, capture evidence,
  no retry in place;
* **never run the boot ladder, hcc_arm/hcc_loop, dp_setup or release on
  endpoint 0001:00:00.0** - it is the second host view of the same chip; a
  second ladder writes the same firmware SRAM through the other RC and
  re-releases a running CPU (REPORT sec. 3). The sysfs attributes exist on
  both devices; that is not permission.

---

## G0 - preconditions (read-only)

    uname -r                                  # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect 13366151... (stripped 351,008 B) - the lane artifact
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'
        # expect 57
    ls /lib/firmware/regulatory.db            # regdb present (final image: yes)
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin # pushed by hand, never packaged
    ls /lib/firmware/<local wifi block>       # pushed by hand, never committed

Regulatory preflight (5 GHz AP is refused without a country domain):

    iw reg get                                # world "00": 5 GHz is NO_IR
    iw reg set US                             # any country that allows 36-48 IR
    iw reg get                                # expect a US / country rule set
    # If iw reg set fails (no regdb / no cfg80211 reg hint): record it and
    # STOP the 5 GHz AP rung - cfg80211 will refuse the beacon with NO_IR.

Golden-state gate (proven transport attrs, read-only):

    D=/sys/bus/pci/devices/0000:00:00.0       # check ls; domain 0 = primary
    cat $D/boot $D/status                     # page/state/mask, staging
    # expected page 0x86807FEF / state 1 / mask 0 / staging 0 (golden)
    ls $D | head                              # attrs on 0001 EXIST - do not use

---

## G1 - load the module with band5g=1 (one load, no retry)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=1

Expected dmesg at load (the new lines in **bold**):

* both endpoints probed, cfg80211 layer:
  - endpoint 0000: "wiphy allocated for endpoint 0000:00:00.0 (2 band(s):
    2.4GHz + 5GHz ...)"
  - endpoint 0001: "endpoint 0001:00:00.0 is the SECONDARY host view of the
    same Wi-Fi chip (domain 1; one HCC service, domain 0) - no wiphy allocated
    here ..." (**this is the fix**, not an error)
  - "band5g=1 ONE-RADIO/TWO-BAND profile active - this wiphy carries 2.4 GHz
    (13 ch) + 5 GHz (29 vendor ch, HT 0x0062, VHT 0x00400064)"
  - band5g profile line from the band5g patch: "cfg80211: band5g=1 5GHz wiphy
    profile active: 29 channels ... HT cap 0x0062 ..."
* module params applied (insmod): cat /sys/module/hi5622_wifi/parameters/band5g
  -> 1.

STOP: no "SECONDARY host view" line = wrong module (old 56-param build).

---

## G2 - boot ladder on endpoint 0000 ONLY (the proven order)

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare
    echo 1 > $D/upload
    echo 1 > $D/verify
    echo 1 > $D/seed
    echo 1 > $D/hcc_arm
    echo 1 > $D/hcc_loop
    echo 1 > $D/release          # 0x5A5A only; NEVER 0xCECE here

Expected (from the proven association-module fire): all rc=0; page
0x86807FEF/7/2/0xABCD; ready handshake decoded (g4 id1 + id2) on the 0000
service; dmesg "cfg80211: ready handshake decoded ... scheduling wiphy
registration" then "cfg80211: wiphy registered after the ready handshake
(2 band(s): 2.4GHz + 5GHz ...) - M0 gate met".

**DO NOT** write prepare/upload/verify/seed/hcc_arm/hcc_loop/release on
0001:00:00.0.

---

## G3 - wiphy assertions (read-only)

    ls /sys/class/ieee80211/            # exactly phy0 (one radio; both bands)
    iw phy phy0 info | sed -n '/Band /,/^$/p'
    cat $D/cfg80211_status              # bands=2 band5g=1 band=2.4GHz

PASS criteria:

* one wiphy only; it lists Band 1: 2.4 GHz ch 1..13 and Band 2: 5 GHz with the
  vendor 29-channel table (36..64, 100..144, 149..165, 184..196). Channels the
  kernel regdb disables are still listed with a disabled flag - the table must
  contain them.
* 5 GHz shows HT 20/40 (0x0062) and VHT (0x00400064) profile flags.
* /sys/bus/pci/devices/0001:00:00.0/cfg80211_status says the layer is not
  initialised (expected - the secondary host view has no wiphy).

If instead iw shows phy0 2.4-only, the module is the old build or band5g=0.

---

## G4 - C2 ladder on 0000 (ordered pair)

    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                   # expect phase=ACTIVE ok=2 timeout=0

Then the proven data-plane bring-up for the AP/beacon path (same as the
association fire): echo 1 > $D/dp_probe (one READ-ONLY MAC word read),
echo 1 > $D/dp_setup, and echo 1 > $D/dp_rx_arm if the RX bank rung is wanted.
Do not mix the sysfs dp_tx_frame instrument with ndo_start_xmit.

---

## G5 - 5 GHz open AP on phy0, channel 36 (the core rung)

    iw phy phy0 interface add wlan5 type __ap
    ip link set wlan5 up
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf 2>&1 | tee /tmp/hostapd-5g.log

Config: ladders/hostapd-5ghz-open.conf (hw_mode=a, channel=36, 20 MHz, OPEN,
beacon_int 100, country_code=US; substitute the interface name if the image
names it differently).

PASS criteria (all of):

* dmesg vendor order with band=1: "vap_add" -> "ap_chan posted ... band=1
  channel=36" -> "ap_ssid" -> "ap_cfg" -> "ap_vap_start after ap_cfg" (the
  proven 5-message order); 2.4 GHz frames are NOT part of this run.
* hostapd: "wlan5: AP-ENABLED", SSID cudy-ap-test-5g, type AP.
* page golden, state 1, mask 0 after the start; no firmware exception line.
* if start_ap is refused: record the exact dmesg line. The known refusals and
  their meaning: "start_ap REFUSED - band 1 ... 5 GHz AP needs band5g=1" (wrong
  module/param), "Could not determine operating frequency" (no 5 GHz band on
  the phy = G3 failed), channel disabled / -EPERM (regdomain, G0).

STOP: exception mask / page change -> power-cycle, keep /tmp/hostapd-5g.log +
UART capture, no retry.

---

## G6 - external observer: the on-air 5 GHz proof (decisive)

On the observer host (the same host as the wifi-onair-observer harness; the
router's RF needs the antennas attached):

    sudo iw dev OBS_IFACE scan freq 5180 | tee /tmp/scan-5g.txt
    # or the lane harness:  sudo tools/scan-ap.sh --freq 5180
    # optional definitive beacon capture (monitor): tools/capture-beacon.sh

PASS criteria:

* the BSS appears on 5180 MHz (ch 36) with SSID cudy-ap-test-5g, DS parameter
  set 36, beacon interval 100, privacy=false for the open rung; RSSI strong at
  the bench;
* beacon frames (subtype 8) captured at ~100 TU in the monitor path - the 5 GHz
  variant of the 2.4 GHz on-air proof (2026-11-25 11:00 ledger entry);
* no 2.4 GHz-only BSS on 5180 (band isolation sanity).

Expected-FAIL (documented, unchanged): client association/4-way handshake
still needs the management/data path and the RF chain; the 5 GHz rung is
beacon-on-air + AP-start, exactly the 2.4 GHz proof scope.

---

## G7 - optional 5 GHz WPA2 GTK rung (fresh boot/load, not a hostapd restart)

Choose the WPA2 config at G5 instead of restarting hostapd (the stale-VAP latch
makes a second AP start on one load unsafe):

    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-wpa2.conf   # passphrase from the local fixture, never committed

PASS criteria: AP-ENABLED; dmesg key_add GTK (g0 id1 0x0078, 96 B, idx=1,
cipher 0xfac04) + set_default_key (0x0079, 68 B, idx=1 multi=1) consumed 0
polls - the hardware-proven 2.4 GHz key path with band=1.

---

## Recorded risk table (fire-gated unknowns)

| # | risk | mitigation / what the fire records |
| --- | --- | --- |
| R1 | world regdomain keeps 5 GHz NO_IR | G0 iw reg set US + iw reg get assertion; abort AP rung if it fails |
| R2 | user_add at iface creation carries band 0 while the AP VAP add/start carry band 1 | first dmesg inspect after G5; if the firmware rejects, record the error and stop (do NOT retry) |
| R3 | the firmware scan request carries one chip_id; a mixed 2.4+5 GHz scan is not expressible | 5 GHz scan rung must be 5 GHz-only (iw dev wlan5 scan freq 5180); mixed requests are logged and use the first channel's band |
| R4 | only one wiphy is registered (by design) | G3 assertion; a second phy means old module |
| R5 | 5 GHz RF path weak without antennas (~44 dB loss noted in the project state) | attach antennas; observer scan near the bench |

---

## Rollback / recovery

Only host-side commands exist in this recipe until G2; after G2 the only reset
is a physical power-cycle (+ the proven UART-READ-ONLY capture and the
recovery door). No flash, no image change, no rmmod.
