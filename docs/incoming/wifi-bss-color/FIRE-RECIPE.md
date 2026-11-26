# FIRE-RECIPE - g0 id36 hmac_scan_config_bss_color (AP BSS-colour posting)

**Lane:** wifi-bss-color (task-233). **The Lead fires; this lane never contacted the
router.** Module under test: the host-built `hi5622_wifi.ko` of
[BUILD-NOTES.md](BUILD-NOTES.md) - stripped **382,404 B** sha256
`d87fbdcb38a8f37b93f88d4ec39a2f9de0cd426551d3231a857cd67c92befdcb`, **65 params**
(64 image #3 params + `bss_color`, default **0** - this rung sets it to 1 at runtime).

**This is one rung. If any PASS gate fails: STOP, record, power-cycle. No second load,
no `rmmod`, no hostapd restart, no improvised variant in the same boot.**

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK / image #3):** human at the
bench; recovery door armed; read-only UART capture to an absolute path; bench link PASS;
never type to the tty; never `rmmod` after a CPU release; never write CA 0x400392f0;
never run the boot ladder on endpoint `0001:00:00.0`; no blind flash; one rung per fresh boot.

## Why this rung

The vendor posts the g0 id36 8-byte BSS-colour bitmap at scan completion (REPORT sec. 1-2).
Our driver has no scan-colour bitmap path, so the rung probes the same firmware frame with
the AP's own cfg80211 HE BSS colour: hostapd `he_bss_color=25` ->
`NL80211_ATTR_HE_BSS_COLOR` -> `settings->beacon.he_bss_color` -> one 20-byte g0 id36 post
in `start_ap` -> dmesg line. The on-air HE Operation IE is firmware-built (it already
shows BSS Color 25 on image #3), so the rung grades **frame acceptance + AP health**, and
the on-air check is a **differential observation**, not an assumed write.

## G0 - preconditions (read-only; image #3 already flashed+accepted)

    R() { ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "$@"; }
    R 'uname -r'                                                    # 6.18.60
    R 'sha256sum /lib/modules/6.18.60/hi5622_wifi.ko'               # the lane artifact (above)
    R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | wc -l'      # 65
    R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | grep bss_color'  # bss_color: (uint)
    R 'hostapd -v'                                                  # v2.12
    R 'grep -c "^ieee80211ax=1" /etc/hi5622/hostapd-open-ax.conf'   # 1
    R 'D=/sys/bus/pci/devices/0000:00:00.0; cat $D/boot; cat $D/status'  # page 0x86807FEF/state 1/mask 0

The vendor firmware pair must already be re-pushed (image #3 ACCEPTANCE R5); the C2 image
is fail-closed at boot (nothing loaded, no AP). STOP if the module sha is not the lane
artifact: wrong build.

## G1 - stage the 2.4 GHz HE fixture with he_bss_color=25 (one file)

    tools/rscp.sh opensource/docs/incoming/wifi-bss-color/ladders/hostapd-2g-bss-color.conf \
        root@192.168.1.1:/etc/hi5622/hostapd-2g-bss-color.conf

Fixture: 2.4 GHz, channel 6, OPEN, `ieee80211ax=1`, `he_bss_color=25` (purpose-built:
isolates the colour frame from the WPA key path and makes the colour deterministic - the
hostapd default is a random 1..63 colour, and the shipped ax fixture has no colour line).

## G2 - load the image #3 stack WITHOUT starting hostapd (one load, no retry)

    R 'uci set hi5622.main.he_caps=1; uci set hi5622.ap.enabled=1; uci set hi5622.ap.country=US; \
       uci set hi5622.ap.config=/etc/hi5622/hostapd-2g-bss-color.conf; uci commit hi5622'
    R '/usr/sbin/hi5622-load --no-ap'
    # expected: the usual C2/load lines + "cfg80211: HE (802.11ax) caps advertised on 2.4GHz ..."
    R 'cat /sys/module/hi5622_wifi/parameters/bss_color'            # 0 (compiled default)
    R 'cat /sys/module/hi5622_wifi/parameters/allow_frames'         # 32 (image #3 default; >0 required)

## G3 - enable the knob at runtime (before the first start_ap)

    R 'echo 1 > /sys/module/hi5622_wifi/parameters/bss_color'
    R 'cat /sys/module/hi5622_wifi/parameters/bss_color'            # 1

The parameter is 0644 (load-time value writable before use); this rung demonstrates the
intended order: no AP yet, so no frame is posted by this write.

## G4 - start the HE AP (one hostapd instance)

    R 'hostapd -B -t -P /var/run/hi5622-hostapd.pid /etc/hi5622/hostapd-2g-bss-color.conf'
    R 'iw dev wlan0 info | grep -E "type|ssid"'
    # expected: AP-ENABLED, ssid cudy-ap-test-bsscolor (the lane fixture's SSID)

The `start_ap` path then posts the AP config and, after it, one g0 id36 frame.

## G5 - PASS gates (all required; capture dmesg + status)

    R 'dmesg | grep -E "BSS Color|g0 id36|start_ap committed"'
    # REQUIRED exact lane line (25 = 0x19 -> bitmap 1<<25 = 0x02000000):
    #   cfg80211: BSS Color 25 posted to firmware (g0 id36 hmac_scan_config_bss_color,
    #   bitmap 0x0000000002000000, chip=0, 20 B)
    R 'grep frame_post /sys/bus/pci/devices/0000:00:00.0/cfg80211_status'
    # expected: posted counter increased by 1 relative to the G0 pre-load value;
    #           refused/timeout/bad unchanged; allow_frames=32 budget not exhausted
    R 'dmesg | grep -iE "trap|abort|oam|exception|firmware error"'
    # expected: empty
    R 'D=/sys/bus/pci/devices/0000:00:00.0; cat $D/boot; cat $D/status'
    # expected: page 0x86807FEF / state 1 / mask 0 (golden, unchanged)
    # UART: no new firmware exception line after the post.

## G6 - on-air observation (read-only, one scan)

A second host (laptop) scans once and records the beacon HE Operation element (ext id
0x24): the BSS-colour byte (low 6 bits) is logged before/after. Expectation:
**unchanged at 25** - the firmware beacon builder owns that IE (../wifi-ax-he-caps,
../wifi-ax-advanced T9); if it changes with `he_bss_color=37` on a later fresh boot, the
host path influences the firmware colour and that is a NEW PASS-with-finding. If it stays
25 in both observations, record "firmware-constant" and close the differential.

## FAIL / STOP (no retry in the same boot)

* hostapd up but no `BSS Color ... posted` line -> check the warning text: the attr was
  missing/disabled (`bss_color=1 but start_ap carried no enabled HE BSS colour`) or the
  post was refused. STOP; do not re-arm by reloading.
* `refused/timeout/bad` incremented, or an exception/UART error/mask != 0/page != golden
  -> power-cycle, keep the UART capture, hand the evidence to the lane.
* The frame must never be posted by hand outside `start_ap`: the lane only wires the
  start_ap path, and the bitmap is the hostapd colour (no shell-built bitmaps).

## Evidence to keep

build/uart-logs/<abs-log>, `dmesg` from G2-G5, `cfg80211_status` frame_post lines,
hostapd log, laptop scan pcap + HE Operation decode, and the golden page/state/mask.
`/tmp` is not evidence - copy to `build/wifi-bss-color/<stamp>/` and append to the ledger.

## Teardown

    R 'kill $(cat /var/run/hi5622-hostapd.pid)'     # stop AP; do NOT rmmod
    # the box stays as-is for the next attended rung or is power-cycled (one rung per boot).
