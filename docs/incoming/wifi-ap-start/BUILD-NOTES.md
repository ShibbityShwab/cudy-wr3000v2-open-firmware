# BUILD-NOTES - wifi-ap-start

All commands were run host-only on 2026-11-26; no image was built, no device
was contacted.  The local vendor artifacts are read in place and never
committed.

## 1. Host selftest + frame emit

    cd opensource/docs/incoming/wifi-ap-start
    ./tools/build.sh

Results:

    compile: cc -std=c99 -Wall -Wextra -Werror -pedantic -O2  -> PASS
    selftest: ap_frames_selftest: 128 passed, 0 failed
    emit: /tmp/wifi-ap-start-build/frames.txt (2 carriers, 6 frames)

## 2. Static RE probes (pinned local firmware + 2.5.24 host module)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/ap_start_re.py --selftest
    -> ap_start_re: 63/63 probes PASS
    $PY tools/ap_start_re.py --tables
    -> table A rows: 0x0064/0x449E5, 0x0065/0x46395, 0x0005/0x82879,
       0x0127/0x84DA5

Pins: firmware sha256 7fc87e20... (928,920 B), module sha256 de78ec07...
(3,564,728 B).  Override paths with HAF_FW / HAF_KO.

## 3. Generated frames freshness

    $PY tools/gen_frames_md.py --check
    -> gen_frames_md: current

## 4. Kernel-context module build (real 6.18.60 tree)

    ./tools/kernel-check.sh

The script copies the hardware-fired base tree
(opensource/docs/incoming/wifi-scan-complete/src, 748,888 B b42ff4ec) to /tmp
scratch, adds this lane src/hi5622_ap_frames.{h,c}, applies this lane's
provider/apstart-provider.patch (v3, 13 hunks), then builds the module against
build_dir/.../linux-6.18.60.  Generated artifacts are removed first so no
stale .o/.mod/Module.symvers from an older tree can leak into the build.

Result:

    hi5622_wifi.ko  770,304 B
    sha256 115119809f103abe753c335e8a386c58987f2578422cc1d359db1d0303992f94
    haf_ symbols: 14
    vapscan_ap_start: present
    warnings/errors: none
    kernel-check: OK (module-only build; no image, no device)

## 5. Patch order (reproduce manually)

    cp -a opensource/docs/incoming/wifi-scan-ap/src /tmp/ap
    cp opensource/docs/incoming/wifi-ap-start/src/hi5622_ap_frames.{h,c} /tmp/ap/
    cp opensource/docs/incoming/wifi-vap-start/src/vapstart_frames.{h,c} /tmp/ap/
    ( cd /tmp/ap && patch -p1 < .../wifi-vap-start/provider/vapstart-provider.patch )
    ( cd /tmp/ap && patch -p1 < .../wifi-ap-start/provider/apstart-provider.patch )
    make ARCH=arm CROSS_COMPILE=... -C <linux-6.18.60> M=/tmp/ap modules

## 6. Lane checks not run

* No hardware validation: this is a host-only lane; the bench ladder is the
  next attended step (recipes/ap_start_ladder.json).
* No image build, no flash, no device access, no UART, no network to the
  router.

## 7. v7 deployable module (second real AP fire follow-up, 2026-11-26)

The v2 artifact was built from the older wifi-scan-ap/src tree.  v3 rebuilds
from the CURRENT hardware-fired provider base:

    BASE = opensource/docs/incoming/wifi-scan-complete/src
           (module 748,888 B sha256 b42ff4ec..., the D2 TRUE PASS provider,
            already containing the wifi-vap-start provider integration)
    + this lane src/hi5622_ap_frames.{h,c}
    + provider/apstart-provider.patch (13 hunks)

    ./tools/build-bench-module.sh
    -> == H2D/core/provider files byte-identical to the 748,888 B fired base
       == strip check: .text byte-identical stripped vs unstripped
       artifact : /tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko
       size     : 206,708 B
       sha256   : fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020
       vermagic : 6.18.60 SMP mod_unload ARMv7 p2v8
       depends  : cfg80211
       ap builder: vapscan_ap_start present
       connect gate: hi5622_cfg11_connect present
       warnings/errors: none

v7 changes beyond v2-v6: base provenance fixed to the hardware-fired tree
(generated artifacts are removed before the build); the SET_INTERFACE op now
updates wdev->iftype (cfg80211_change_iface WARNs unless the driver does) and
treats params->use_4addr == -1 as "not specified"; and a fail-closed
.mgmt_tx op is registered so cfg80211 accepts hostapd's AP-mode
NL80211_CMD_REGISTER_FRAME subscriptions (without it hostapd reports
"Could not configure driver mode" after a successful SET_INTERFACE - see
HOSTAPD-BRINGUP.md section 9).

Hostapd flow, expected logs and readback: HOSTAPD-BRINGUP.md.
Ucode workaround: tools/push-hostapd-ucode.sh (image fix:
image/wifi-scripts-ucode.md).

