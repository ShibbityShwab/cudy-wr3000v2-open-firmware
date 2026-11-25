# FIRE-RECIPE - the HE (802.11ax) capability advertisement (AX-1)

**Lane:** wifi-ax-he-caps (task-177).  **The Lead fires**; this lane never
contacted the router.  Module under test: the host-built `hi5622_wifi.ko` of
`BUILD-NOTES.md` - stripped **356,564 B** sha256
`475975d32c9e56e2de1974d28ea983f1e0e7e36c4a0465ae4eeb7ea19755cdea`,
**59 params**, new `he_caps` param default **0** (this rung loads it as 1).

**This is one-shot.**  If any PASS gate below does not hold, STOP, record,
power-cycle.  Do not reload the module twice, do not restart hostapd, do not
improvise a rung.

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK):**

* recovery door armed, UART capture running READ-ONLY, bench link PASS;
  no human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read
  the RC misc window 0x10161000; no UART transmit; no blind flash;
* **never run the boot ladder / hcc_arm / hcc_loop / dp_setup / release on
  endpoint 0001:00:00.0** (second host view of the same chip);
* exception mask / page change / mask != 0 -> power-cycle, capture evidence,
  no retry in place.

The core rung is **2.4 GHz**, because that is the band whose beacons are
already proven on air (-32 dBm) - it isolates the new variable (the HE
capability advertisement) from the still-open 5 GHz beaconing rung (5G-4).
The 5 GHz variant (Rung B) is optional and only after 5G-4 passes.

---

## G0 - preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect 475975d32c9e56e2de1974d28ea983f1e0e7e36c4a0465ae4eeb7ea19755cdea
        # (356,564 B) - the lane artifact
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 59
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^he_caps:' # (uint)
    cat /sys/module/hi5622_wifi/parameters/he_caps   # 0 before the load
    ls /lib/firmware/regulatory.db
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status          # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/param count is not the lane artifact: wrong build.

## G1 - load with he_caps=1 (2.4 GHz profile, one load, no retry)

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 band5g=0 vap5g=0 he_caps=1

Expected: the same probe lines as the bench-validated 2.4 GHz profile, plus
the HE advertisement line (G3) - it is emitted when the wiphy is built, i.e.
at probe, *before* the boot ladder:

    cfg80211: HE (802.11ax) caps advertised on 2.4GHz (AP iftype): MAC 01 00
    00 00 00 00, PHY0 0x02, MCS rx80/tx80 0xfffa/0xfffa rx160/tx160
    0xffff/0xffff (2SS MCS0-9, no PPE, no 6GHz) - hostapd ieee80211ax=1 gate
    unlocked

    cat /sys/module/hi5622_wifi/parameters/he_caps   # 1

STOP: no HE advertisement line while dmesg is otherwise healthy, or
`he_caps != 1` = wrong module/params.  (An invalid `he_caps>1` refuses the
wiphy at probe with a dev_err line - that is fail-closed; power-cycle and
re-load.)

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
"cfg80211: wiphy registered after the ready handshake (1 band(s): 2.4GHz ...)".

## G3 - wiphy HE advertisement assertions (read-only; host-side PASS)

    ls /sys/class/ieee80211/                    # exactly phy0
    iw phy phy0 info | sed -n '/HE Iftypes/,+14p'
    cat $D/cfg80211_status | head -1            # ... band5g=0 he_caps=1

PASS (all of):

* `iw phy phy0 info` shows an **`HE Iftypes: AP`** block for the 2.4 GHz
  band (nl80211 per-band iftype data; iw >= 6.17 is on the image);
* its decoded caps are the contract: `HE MAC Capabilities` byte0 0x01
  (HTC HE; iw prints it as "HTC HE Supported"), `HE PHY Capabilities` byte0
  0x02 (40 MHz in 2.4 GHz), MCS/NSS rx80/tx80 `0xfffa` (2 SS MCS0-9), no
  HE 6 GHz capability line;
* `/sys/.../cfg80211_status` shows `he_caps=1`.

FAIL: no "HE Iftypes: AP" block -> the patch/param did not reach the wiphy;
STOP (hostapd cannot pass its HE gate and the on-air rung is meaningless).

## G4 - C2 ladder on 0000 (ordered pair)

    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                   # phase=ACTIVE ok=2 timeout=0

## G5 - create the 2.4 GHz AP interface

    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up

PASS: no "announce NOT consumed"/"h2d_timeout"/"lane closed" line; page/
mask still golden.  (band5g=0: legacy per-endpoint path, bench-proven; the
user add ordering is not the variable of this rung.)

## G6 - start the HE AP on channel 6 (the host-side core rung)

    /usr/sbin/hostapd -dd -t /tmp/hostapd-2g-he.conf 2>&1 \
        | tee /tmp/hostapd-2g-he.log

Config: `ladders/hostapd-2g-he.conf` - open 2.4 GHz AP, hw_mode=g,
channel=6, **ieee80211n=1**, **ieee80211ax=1**, beacon_int 100, DTIM 2.
(The image's hostapd-mini/SAE binary is built with CONFIG_IEEE80211AX; the
gate that matters is the driver advertisement from G3.)

PASS (all of):

1. dmesg: "start_ap beacon_static=1 plan (5 steps): ..." and the AP-start
   frames posted/consumed (vendor order; same lines as the proven 2.4 GHz
   AP start);
2. dmesg: a frame log shows the AP-config body carrying the extension IE:
   the "ap_cfg" frame is posted with the hostapd beacon tail that now holds
   HE Capabilities (ext 0x23) + HE Operation (ext 0x24); the driver maps
   that to AP-config wireless mode 10 (HE) - this is the 156-byte body
   offset +0x8e (decode: ../wifi-ap-start TABLES T7);
3. hostapd: **"wlan0: AP-ENABLED"** and **no** "Driver does not support HE"
   (the latter is the decisive host-side failure of AX-1);
4. page 0x86807FEF, state 1, mask 0; no firmware exception.

FAIL: "Driver does not support HE" -> cfg80211 HE advertisement shape wrong
(record `iw phy phy0 info` + dmesg; stop); AP fails before start -> normal
stop rules.

## G7 - the on-air HE IE proof (external observer; the decisive PASS)

Run on the observer host (never on the router), with the AP from G6 live:

    # A. decisive scan class: iw >= 6.17 prints HE capabilities from the
    #    beacon's extension IE
    sudo iw dev OBS_IFACE scan freq 2437 | tee /tmp/scan-2g-he.txt

PASS (all of):

* the BSS `cudy-ap-test` appears on 2437 MHz, DS 6, beacon interval 100;
* the BSS block contains **`HE capabilities:`** (parsed from ext-id 35) with
  MAC cap 0x01 (HTC HE) and PHY cap 0x02 (40 MHz in 2.4 GHz), and
  **`HE Operation`** (ext-id 36);
* no HE 6 GHz capability is printed (the chip has no 6 GHz radio).

Byte-level confirmation (optional but decisive for the exact contract): the
observer lane captures a radiotap pcap, then this lane's offline checker
returns the verdict line `HE-IE-VERDICT|PASS|...`:

    cd ../wifi-onair-observer && sudo tools/run-all.sh --label he-caps --with-monitor
    # evidence dir: <observer>/evidence/<ts>-he-caps/ (or the lane's EVID)
    python3 <this lane>/tools/check-he-ie.py \
        --pcap <evidence>/beacons.pcap --ssid cudy-ap-test --band 2g

PASS requires the verdict PASS (the tool compares the exact T10 bytes:
2.4 GHz HE Capabilities element = 24 B, `ff 16 23 01 00 00 00 00 00 02 00 ...
fa ff fa ff`).  INCONCLUSIVE (no beacon / no monitor capture) is **not** a
pass: record it as SKIP and do not claim the on-air half; FAIL-ABSENT means
the firmware beacon does not carry the HE IE even though the hostapd tail
did (see "What a FAIL means").

Cleanup: hostapd Ctrl-C, observer `tools/cleanup.sh`, teardown as usual.

## Rung B (OPTIONAL) - 5 GHz HE advertisement, only if 5G-4 passed

Same module, fresh boot, `band5g=1 vap5g=1 he_caps=1`, the 5 GHz bring-up
load line from `../wifi-5ghz-vap-msg/FIRE-RECIPE.md` G1, then
`ladders/hostapd-5g-he.conf` (hw_mode=a, channel=36, ieee80211n=1,
ieee80211ax=1).  G3 PASS additionally requires `HE Iftypes: AP` for the
5 GHz band with PHY cap byte0 0x0c (40/80 + 160 MHz) and rx160/tx160 0xfffa.
G7 becomes `iw dev OBS scan freq 5180` and the checker `--band 5g`; the
expected element is 28 B.  Do not combine this rung with G0-G7 on one load:
one rung per boot.

## What a FAIL means (falsification, one line each)

| gate | failure | what it falsifies / means |
| --- | --- | --- |
| G3 | no HE Iftypes block | the patch is not in this module or the parameter was not applied - host-side bug, not a hardware result |
| G6 | "Driver does not support HE" | the cfg80211 advertisement shape is wrong for hostapd 2026.08.07 (bytes/types_mask); fix host-side, no on-air conclusion |
| G6 | AP-ENABLED but no HE frames | the HE IEs never reached start_ap - inspect the tail log, host-side |
| G7 | FAIL-ABSENT | **the meaningful negative**: hostapd's beacon tail carried HE IEs and the driver mapped mode=10, but the firmware beacon does not advertise HE. HE-on-air then needs the firmware capability path (INI/firmware config), not this host patch; move it to a new lane |
| G7 | FAIL-MISMATCH | the beacon carries HE IEs but not the contract caps (e.g. hostapd config forced different PHY bits) - record the observed bytes; host-side/hostapd-config issue |
| G7 | INCONCLUSIVE | the capture did not happen (no beacon/monitor); no AX-1 verdict - do not claim PASS |

## What to record on a PASS

module sha256 + param count; the G1 HE advertisement dmesg line; the G3
`iw phy phy0 info` HE block; the G6 hostapd AP-ENABLED log without the HE
refusal; the G7 observer scan block with `HE capabilities`/`HE operation`
and the `HE-IE-VERDICT|PASS` line + json; page/mask golden; UART capture
path.  That set is the AX-1 evidence pack.
