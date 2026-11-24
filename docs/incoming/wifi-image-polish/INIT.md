# INIT - boot integration, the loader, and the bench/production switch

## Design in one sentence

One loader (`/usr/sbin/hi5622-load`) owns the whole bring-up, one rc.common
service (`/etc/init.d/hi5622-wifi`) decides *whether* it runs at boot, and one
uci file (`/etc/config/hi5622`) is the single policy source - bench default is
**autoload off**, so the image never touches the Wi-Fi chip without an operator.

## Boot / start chain

    rc.common S96hi5622-wifi                (only when hi5622.main.autoload=1)
      -> /usr/sbin/hi5622-load
           1. firmware preflight: /lib/firmware/{FIRMWARE.PD4.bin,wifi_block.bin}
              must exist (never packaged; fail closed if missing)
           2. insmod cfg80211.ko          (driver imports its symbols;
                                           target-kernel build, NOT mac80211
                                           backports)
           3. insmod hi5622_wifi.ko with explicit packaged parameters
              (module boot ladder -> upload/verify/release -> ready handshake,
              ~30 s by design)
           4. verify each parameter in /sys/module/hi5622_wifi/parameters/
           5. wait for /sys/class/ieee80211/phy* (hi5622.main.wiphy_timeout=30 s)
           6. C2 ladder: h2d_ctx_create=3 -> h2d_txq_sync72=1, then read
              h2d_status (expect ok=2 timeout=0 refused=0)
           7. if hi5622.ap.enabled=1: optional iw reg set <country>, then
              iw phy <phy> interface add wlan0 type __ap, ip link up,
              hostapd -B -P /var/run/hi5622-hostapd.pid <config>,
              wait for "type AP" (hostapd AP-ENABLED)

The loader takes no required arguments; `hi5622-load --no-c2 --no-ap` is the
pure load path, `HI5622_DRY_RUN=1 hi5622-load` prints the plan on any host
(used by the lane selftest).

## The bench/production switch

| uci option | shipped | meaning |
| --- | --- | --- |
| `hi5622.main.autoload` | **0** | bench policy: nothing runs at boot; operator starts the service. Set 1 (+ `/etc/init.d/hi5622-wifi enable`) for boot loading. |
| `hi5622.ap.enabled` | **0** | start hostapd on the AP VAP after the C2 ladder. |
| `hi5622.main.strict` | 1 | fail the service when a gate does not come up; 0 = log and continue (diagnostics only). |
| `hi5622.main.c2_ladder` | 1 | run ctx-create + txq-sync72 after load (needed by the AP path; skip with --no-c2 for pure-driver tests). |

Bench default deliberately means: **flashing the image does not change the
Wi-Fi chip state on boot**.  The driver's boot ladder performs device writes on
load, so autoloading it without an operator would violate the attended-session
rule.  Production-style opt-in is two uci sets plus enabling the service.

Boot-time cost of autoload=1: about 35-45 s (the module's fixed boot-ladder
sleeps: 1-3 s steps plus the 30 s ready wait; the rc script runs at S96 so the
rest of the system is already up).  This is why the default is off.

## Files installed (rootfs payload)

    /etc/config/hi5622                  policy (see table above)
    /etc/init.d/hi5622-wifi             START=96 STOP=20, USE_PROCD=0
    /etc/rc.d/S96hi5622-wifi            -> ../init.d/hi5622-wifi (staged by install-into-image.sh)
    /etc/modprobe.d/hi5622-wifi.conf    packaged defaults mirror for manual modprobe
    /etc/hi5622/hostapd-*.conf          AP fixtures (HOSTAPD.md)
    /etc/hi5622/FIRMWARE-POLICY.txt     operator-facing firmware push policy
    /usr/sbin/hi5622-load               the loader

## Semantics that matter

* **Never unloads the driver.**  Project rule: no `rmmod` after a CPU release.
  `service stop` only stops hostapd; unloading requires a reboot.  The loader
  refuses to reload a module that is already loaded with different parameters.
* **Explicit insmod, not modprobe.**  Bench-confirmed: modprobe on this image
  does not pass parameters; insmod does.  The loader spells every parameter
  out and then verifies it from sysfs.
* **Wiphy enumeration, not hardcoded phy0.**  The wiphy index advances across
  module load/unload cycles within a boot; the loader takes the last
  `/sys/class/ieee80211/phy*`.
* **PCI discovery, not hardcoded address.**  The endpoint is found by
  vendor/device `0x59e7:0x0005` (the bench saw `0000:00:00.0`, but that is
  not a contract).
* **hostapd is started directly**, not via OpenWrt's wifi scripts/netifd:
  the driver is a research full-MAC driver and `/etc/config/wireless` is not
  used.  Do not enable wpad; the image ships hostapd-mini only.
* **No hotplug.**  The PCIe endpoint is built-in; there is nothing removable.

## Failure modes and the expected message

| symptom | cause | action |
| --- | --- | --- |
| `firmware missing: /lib/firmware/hi_wifi/FIRMWARE.PD4.bin` | reflash/overlay wipe removed the manual PD4 push | push firmware + wifi_block (PACKAGING.md), rerun |
| insmod fails with "file not found"/probe error | missing/renamed firmware, or cfg80211 not loaded first | check the two preflight files; the loader loads cfg80211 before the driver |
| parameter verify mismatch | insmod did not apply the request (or a different module loaded) | reboot; never rmmod after release |
| no wiphy after 30 s | ready handshake did not arrive, chip not booted | inspect `dmesg | tail`, transport sysfs `status`/`boot`; power-cycle, do not reuse the boot |
| C2 ladder not `ok=2 timeout=0` | HCC core not ACTIVE / budget exhausted / chip exception | stop; check page/mask invariants, power-cycle |
| hostapd does not reach `type AP` in 10 s | AP frames not accepted, or ucode/config problem | check hostapd output/dmesg; verify `/usr/share/hostap/hostapd/common.uc` exists (image has wifi-scripts) |

Hard rules still apply: recovery door armed before writes, never write the
active flash pair, never write CA `0x400392F0`, never `0xCECE` during boot,
one idea at a time.
