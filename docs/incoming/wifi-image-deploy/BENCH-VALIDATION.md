# BENCH-VALIDATION - what to run after the T0 zero-flash deploy

**Lane:** img-deploy (task-119). Host-only lane: none of this was executed on the
router here. The deploy tooling is in this directory; this file defines the
attended validation and its pass/fail gates.

Read together with: ../wifi-image-integration/DEPLOY.md (T0/T1 options and
rollback), ../wifi-cfg80211/REPORT.md section 8 (the attended golden-boot
sequence and stop rules), ../wifi-bench-tooling/PHASES.md (C2/D1 ladders),
RUNBOOK.md + BENCH-FACTS.md (bench link, UART read-only, recovery door).

## 0. Preconditions (all must hold before any command)

1. Human at the bench; recovery door armed (tools/recovery-card.sh prints
   "STATUS: recovery door is READY"); UART capture running read-only to an
   absolute path if the session may trigger a reset (deploy-t0.sh itself does
   not reset).
2. Box on OUR image, pair B (uname -r 6.18.60), SSH 192.168.1.1 reachable;
   tools/bench-link.sh PASS.
3. The seven .apk artifacts are present in the local artifacts dir and the host
   check + transaction simulation PASS:
       ./deploy-t0.sh --check
       ./tools/verify-apks-host.sh
4. No other bench session is running; deploy-t0.sh takes
   /tmp/cudy-t0-deploy.lock and refuses a concurrent run.

## 1. Deploy (attended, reversible, no flash)

    ./deploy-t0.sh --apply --attended --yes-really

What it does, in order:
* host check (artifact sha256, apk metadata, dependency graph, tree vermagic)
  and host preflight (bench link PASS, recovery door READY);
* stages t0-device.sh + the two manifests + the seven .apk files under
  /tmp/t0-deploy-<stamp>/ on the box, then re-verifies all seven sha256 there;
* runs device mode "install": image/apk/overlay/space/kernel-package gates,
  "apk add --allow-untrusted --force-non-repository --network=no
  --repositories-file /dev/null <7 files>", then re-reads package versions,
  world entries and every payload file sha256, checks vermagic + depends and
  the no-autoload policy, writes /root/.wifi-t0-deploy.receipt;
* on any FAIL it runs the rollback automatically; on success it removes the
  /tmp staging.

Expected operator output: every line "ok ...", the device T0GATE lines all PASS,
and a final "RESULT PASS". Evidence: build/evidence-t0-deploy/<stamp>/ on the
host (summary.txt + device-install.txt + deploy.log).
Optional in the same attended window (write-free probe):
    ./deploy-t0.sh --apply --attended --yes-really --load-check
adds device mode "load": insmod /lib/modules/6.18.60/cfg80211.ko then
insmod /lib/modules/6.18.60/hi5622_wifi.ko, verifies /proc/modules,
/sys/module/hi5622_wifi, no BUG/WARNING/Oops in the last 80 dmesg lines, and
"iw dev" rc=0. One load only; leave the modules loaded and reboot before a
normal phase session.

## 2. Fresh-boot verification (read-only, after a reboot)

    ./deploy-t0.sh --verify --attended --yes-really

Gates: packages installed at the expected versions, all seven payload files
present with the expected sha256, native iw --version = 6.17, iwinfo executes
from /usr/bin with its usage text, both modules present with the 6.18.60 ARMv7
vermagic + depends=cfg80211, no /etc/modules.d entry for hi5622/cfg80211, and
the receipt present. Manual cross-check if wanted:

    uname -r == 6.18.60
    apk query --fields version kernel
        -> 6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1
    ls -l /usr/sbin/iw /usr/bin/iwinfo /lib/modules/6.18.60/*.ko
    grep -r hi5622 /etc/modules.d /etc/modules   # must be empty
    lsmod | grep -E 'cfg80211|hi5622'            # must be empty after a fresh boot
    sha256sum /usr/sbin/iw /lib/modules/6.18.60/hi5622_wifi.ko
        -> e0f4ba8c... / e0608fa6... (see REPORT.md section 1)

If this fails, roll back (section 5) - the previous /tmp-based flow is still
available and the persistent overlay is the only thing T0 changed.

## 3. Native module load from the rootfs (bench policy: one load)

    /sbin/modprobe cfg80211
    /sbin/modprobe hi5622_wifi                 # add fw_path/wifi_block_path/plat_block_path
                                               # exactly as the attended session needs
    grep -E '^cfg80211 |^hi5622_wifi ' /proc/modules
    dmesg | tail -n 40                          # expect clean probe, no BUG/Oops
    iw dev                                      # rc=0; no phy yet (wiphy registers on ready)

This proves the driver loads from /lib/modules (rootfs), not from /tmp.
Stop rule: one load, no retries, no rmmod after the release step below. Reboot
before any normal phase-matrix session (the automation expects a fresh boot).

## 4. Native wiphy + VAP ladder (the D1 path with rootfs iw)

4a. Attended golden boot with the ROOTFS driver, same device actions the
bench automation performs (wifi-cfg80211/REPORT.md section 8): prepare ->
upload -> verify -> seed -> hcc_arm -> hcc_loop -> release (0x5A5A only, never
0xCECE during boot), using the driver sysfs attributes under the PCI endpoint:

    P=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $P/prepare     # status windows=1
    echo 1 > $P/upload
    echo 1 > $P/verify
    echo 1 > $P/seed
    echo 1 > $P/hcc_arm
    echo 1 > $P/hcc_loop
    echo 1 > $P/release     # status cpu_released=1
    cat $P/status $P/cfg80211_status

Use the exact PD4 patched firmware + block files from the attended procedure;
this step is unchanged by T0. Wait for the wiphy:

    for i in 1 2 3 ... 20; do [ -e /sys/class/ieee80211/phy0 ] && break; sleep 1; done
    ls /sys/class/ieee80211/            # expect phy0 (+ phy1 for the second endpoint)
    cat $P/hcc_ready $P/hcc_status

4b. Native iw gates (rootfs iw, no /tmp, no LD_LIBRARY_PATH):

    iw phy                              # expect "Wiphy phy0" (+ phy1)
    iw phy phy0 info                    # 2.4 GHz 13 channels; 5 GHz 19 LU-plan channels
    iw dev                              # interface list (empty netdev-wise at first)
    iw phy phy0 interface add wlan0 type managed
    cat /sys/class/net/wlan0/type       # 1
    cat /sys/class/net/wlan0/address    # locally-administered style address
    cat $P/cfg80211_status              # vap0: wlan0 ... created=1
    iw dev                              # lists wlan0
    iw dev wlan0 del
    cat $P/cfg80211_status              # vap0 absent, removed=1
    iw dev                              # wlan0 gone

Pass criteria equal the standard D1 ladder (PHASES.md): wiphy registered, iw
phy/iface-add/iface-del rc=0, netdev sysfs type/address, cfg80211_status
created/removed counters. This is the "rootfs iw + rootfs driver" validation
that T0 exists for.

4c. Standard ladders afterwards (unchanged by T0; run on a fresh boot):
* C2 (six-calibration boot) - protocol mode, no iw dependency:
      python3 tools/phase_matrix.py --phase C2 --yes-really --attended ...
* D1 (cfg80211 VAP add/del) - the bench runner still stages its support
  binaries to /tmp by design (--iw-bin/--libnl-tiny are required for real
  runs). Transitional invocation with the SAME bytes now in the rootfs:
      --iw-bin <host copy of the image rootfs usr/sbin/iw>  (sha e0f4ba8c...)
      --libnl-tiny <host copy of the image rootfs libnl-tiny.so.1>
  Tooling follow-up (bench2 lane, small): allow cfg80211 mode to skip the iw
  staging when the image carries iw; the generated script already falls back to
  "command -v iw" (bench_session.py IW selection), so only the staging guard
  needs a "native iw" option. Until then section 4b is the no-/tmp D1 gate.
* D2 (scan) stays BLOCKED-DRIVER while the scan response/vap-start work is
  pending (task-117/118); T0 does not change that.

## 5. Rollback (zero flash, reversible)

    ./deploy-t0.sh --rollback --attended --yes-really

Runs apk del for the seven package names, verifies each package is gone, each
payload file is gone, the receipt is removed and the world is clean. Loaded
modules are not unloaded (no rmmod); reboot to clear them. If apk del ever
failed, pair A / stock and the image-rewrite path (T1) remain the fallbacks -
see ../wifi-image-integration/DEPLOY.md.

## 6. Acceptance summary

T0 is accepted for the bench when: section 1 reports RESULT PASS; section 2
passes after a reboot; section 3 loads the rootfs modules cleanly; section 4b
reproduces the D1 wiphy/VAP gates with rootfs iw and no /tmp staging; and
section 5 provably restores the pre-install state. Record the evidence
directory and the module/package hashes in the session ledger.
