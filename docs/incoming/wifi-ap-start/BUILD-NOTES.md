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

The script copies the committed wifi-scan-ap src tree to /tmp scratch, adds
this lane src/hi5622_ap_frames.{h,c} and the wifi-vap-start builder library,
applies the wifi-vap-start provider patch (base) and this lane's
provider/apstart-provider.patch, then builds the module against
build_dir/.../linux-6.18.60.

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
