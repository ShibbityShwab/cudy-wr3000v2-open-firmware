# PARAMS - module parameters, image defaults and the working load incantation

Source of truth for the parameter list: the **installed module in the flashed
image**, `/lib/modules/6.18.60/hi5622_wifi.ko` (231,132 B sha
`51608fd5...`), read with `modinfo`.  The full table with types, sysfs
permissions, compiled-in source defaults and deployed values is
[refs/module-params.txt](refs/module-params.txt).

**The parameters are load-time only** (most are 0444 = read-only after load).
Passing them on `insmod` works; changing them afterwards does not.

## Why the defaults are "runtime-baked" and not only in the binary

Two independent reasons:

1. **The hardware-validated binary is frozen.**  The flashed artifact
   (242,572 B sha `44c6cc12...` staged; 231,132 B sha `51608fd5...`
   installed) is the exact module the bench fired.  Editing the source and
   rebuilding changes the artifact; the project does not replace a
   hardware-validated module with an unfired one.  The zero-risk deployment
   path keeps the validated binary and passes the packaged defaults
   explicitly - that is what `hi5622-load` does.
2. **modprobe on this image does not pass parameters** (bench-confirmed
   2026-11-27: the module loaded with `fw_path=hi_wifi/FIRMWARE.bin`,
   `h2d_send_budget=1`).  The proven path is
   `insmod <ko> key=value ...`.

The polished build ([patches/](patches/)) additionally bakes the same values
into the module's compiled-in defaults and adds `mgmt_diag`, so a bare
`insmod hi5622_wifi.ko` is also correct - but it still needs a fire before
deployment.

## Image defaults (packaged: loader + /etc/config/hi5622 + modprobe.d)

| parameter | value | why |
| --- | --- | --- |
| `fw_path` | `hi_wifi/FIRMWARE.PD4.bin` | operator-pushed research firmware (never packaged) |
| `wifi_block_path` | `hi_wifi/wifi_block.bin` | local PHY custom block (never packaged) |
| `allow_frames` | 16 | 2 VAP add/notify + 4 AP-start + scan/VAP-base + spares |
| `frame_gap_ms` | 100 | AP-start frame pacing used by the fired AP ladder |
| `h2d_send_budget` | 4 | ctx-create + txq-sync72 + TXQ addr hand-off + spare |
| `vap_start_first` | 1 | mode-1 VAP base before scan/AP (once-guarded) |
| `vap_start_mode` | 1 | g0 id1 keys wire family |
| `vap_start_module` | 0 | VAP/module index |
| `scan_dwell_ms` | 60 | explicit vendor dwell (0 also means 60) |
| `hcc_poll_ms` | 5 | HCC service cadence |
| `ap_start_mode` | 0 | g0 id1 static AP frames (vendor table A) |
| `cfg80211_auto` | 1 | register the wiphy after the ready handshake |
| `rd_ab_write` | 0 | never write the experimental D2H ring consumer |
| `hcc_cece_mode` | 0 | evidence-driven CECE policy |
| `allow_unseeded_release` | 0 | never release the chip without the custom block |
| `band_force` | unset (-1) | auto per PCI domain; set 1 for the 5 GHz endpoint |
| `scan_timeout_ms` | unset (0) | host scan window auto from the channel count |
| `scan_trust_rssi` | unset (0) | do not invent a signal measurement |

These are the values the 2026-11-27 lead tuning input asked for
(`allow_frames=16`, `vap_start_first=1`, `scan_dwell_ms=60`, ...), and they
all exist in the shipped 19-parameter surface, so **the validated module
supports this config without a rebuild**.

## Exact working incantation (validated module, bench image)

    # discover the endpoint (do not hardcode 0000:00:00.0)
    for d in /sys/bus/pci/devices/*; do
        [ "$(cat $d/vendor)" = 0x59e7 ] && [ "$(cat $d/device)" = 0x0005 ] && D=$d
    done

    insmod /lib/modules/6.18.60/cfg80211.ko
    insmod /lib/modules/6.18.60/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin \
        wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=16 frame_gap_ms=100 h2d_send_budget=4 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        scan_dwell_ms=60 hcc_poll_ms=5 cfg80211_auto=1 ap_start_mode=0 \
        rd_ab_write=0 hcc_cece_mode=0 allow_unseeded_release=0

    # C2 ladder (the real attr is h2d_ctx_create; the value selects t1/t2/t3)
    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status        # expect: ok=2 timeout=0 refused=0

    # AP (do not treat as on-air proof; see HOSTAPD.md)
    iw reg set US            # world regdomain is NO_IR - set your country
    iw phy <phy> interface add wlan0 type __ap
    ip link set wlan0 up
    hostapd -B -t -P /var/run/hi5622-hostapd.pid /etc/hi5622/hostapd-open.conf

`/usr/sbin/hi5622-load` runs exactly these steps with the values from
`/etc/config/hi5622` (no arguments needed), and reads the parameters back
from `/sys/module/hi5622_wifi/parameters/` to prove they took effect.

## Verification / gotchas

* Read back what the kernel stored:
  `for p in fw_path allow_frames h2d_send_budget vap_start_mode; do echo "$p=$(cat /sys/module/hi5622_wifi/parameters/$p)"; done`
* `vap_start_mode`, `vap_start_module`, `ap_start_mode`, `fw_path`,
  `wifi_block_path`, `scan_trust_rssi` are **0444**: set at load, read-only
  afterwards.  A wrong value means unload (forbidden after a CPU release) or
  reboot.
* If the module is already loaded with different parameters, the loader fails
  closed with a reboot instruction instead of silently keeping the old config.
* The image's installed module has 19 parameters - it does **not** have
  `mgmt_diag`.  Passing `mgmt_diag` to the validated module makes insmod fail.
  Only the polished build exposes it (default 0).
* Do not call the C2 attr `h2d_ctx_create_t1`: that name appears in some older
  lane notes but does not exist in sysfs.  The real attribute is
  `h2d_ctx_create` (write value 1|2|3).
