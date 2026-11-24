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

## 1b. Userspace-only deploy (when the image kernel package != the kmod pin)

Use this on the current box (kernel package 6.18.60~5de0e425... vs the kmod .apk
pin 6.18.60~6f995278...) or whenever only native iw/iwinfo are wanted. The five
packages have no kernel= dependency, so no kernel-ABI gate applies:

    ./deploy-t0.sh --userspace-only --apply --attended --yes-really

Expected: host check PASS (40 gates incl. "has no kernel= dependency" x5),
staging of exactly the five .apk files, device gates PASS, then on later runs
it is idempotent ("already installed ... idempotent verify"). The iwinfo gate
accepts the real rc=0 output even when disconnected ("wlan0 ESSID: unknown")
and records it in the evidence; the world-entry gate accepts apk v3
"name><hash" lines and does not fail a pre-existing manual install for its
missing world entry. If anything does fail, the automatic rollback only undoes
this run's own install; add --keep-on-failure to keep the set for inspection.
It installs
/usr/sbin/iw 6.17, /usr/bin/iwinfo, libiwinfo (+data) and
/lib/firmware/regulatory.db; it installs NO modules and does not touch
/lib/modules. Verify / rollback:

    ./deploy-t0.sh --userspace-only --verify --attended --yes-really
    ./deploy-t0.sh --userspace-only --rollback --attended --yes-really

Cross-check on the box: /usr/sbin/iw --version -> 6.17; iwinfo runs;
apk query --fields version iw/iwinfo/libiwinfo20230701/libiwinfo-data/
wireless-regdb show the pinned versions; /lib/modules/6.18.60 still has no
cfg80211.ko/hi5622_wifi.ko. Native iw/iwinfo are then usable for the bench
ladders; the modules stay in the proven /tmp insmod flow until the ABI path in
section 7 is taken.

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

Runs apk del for the selected package names (seven for the full set, five for
--userspace-only), verifies each package is gone, each payload file is gone,
the receipt is removed and the world is clean. This removes the set even if it
pre-existed the deploy; an automatic rollback after a FAILED run does not do
that unless the run itself installed something (and --keep-on-failure
suppresses it). Loaded modules are not unloaded (no rmmod); reboot to clear
them. If apk del ever
failed, pair A / stock and the image-rewrite path (T1) remain the fallbacks -
see ../wifi-image-integration/DEPLOY.md.

## 6. Acceptance summary

T0 is accepted for the bench when: section 1 reports RESULT PASS; section 2
passes after a reboot; section 3 loads the rootfs modules cleanly; section 4b
reproduces the D1 wiphy/VAP gates with rootfs iw and no /tmp staging; and
section 5 provably restores the pre-install state. Record the evidence
directory and the module/package hashes in the session ledger.
For the current box (kernel package mismatch), the userspace-only variant is
accepted when section 1b reports PASS, section 2 passes after a reboot for the
five-package set, native iw/iwinfo work (section 4b's iw phy/iw dev part runs
once the modules are loaded via the /tmp flow), and section 1b rollback
restores the pre-install state.

## 7. Closing the kmod ABI (after the 2026-11-26 gate)

Measured on the bench: running image kernel package
6.18.60~5de0e425f1313eeec82615317fdc1f76-r1, kmod .apk pin
6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1 (the current tree's kernel
configuration hash). The flashed kernel slice is still the validated 13e81214
bytes, so the module vermagic matches; apk is enforcing build provenance, not
an ELF-ABI failure. Two paths, with a canonical decision:

**P1 (canonical): rebuild the image from the current tree and flash it.** The
build produces one kernel package version (6f995278), a rootfs apk db carrying
that version, and kmod .apk files pinned to it. Flash in an attended window
(T1 rootfs rewrite in ../wifi-image-integration/DEPLOY.md, or kernel slice +
rootfs from one build). Note: the kernel= version is recorded in the rootfs apk
db, not in the kernel image, so a kernel-slice-only write cannot fix the
mismatch - the rootfs from the same build must be installed as well. Then the full-set T0 deploy (section 1) is consistent
and survives reboot. This is reproducible, matches what a real image does, and
keeps apk's dependency checking meaningful - it is the path the target/upstream
work expects.

**P2 (fallback, not canonical): rebuild the two kmod .apk files against the
running image's kernel package version (5de0e425).** The wifi-openwrt-pkg lane
used this technique for its own package. It pins modules to an old build's
kernel configuration that the tree no longer reproduces and must be redone on
every image/tree change; keep it as a local, labelled bench artifact only.
Never force apk past the kernel= dependency and never make a metadata override
the image's dependency policy.

Interim (safe now): deploy the userspace-only set (section 1b) for native
iw/iwinfo and keep using the proven /tmp insmod flow for the two modules; the
choice between P1 and P2 stays open and P1 is the recommended close.
