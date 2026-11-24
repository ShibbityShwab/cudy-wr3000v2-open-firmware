# wifi-image-deploy - T0 zero-flash bench-image apk deploy (task-119)

**Lane:** img-deploy, host-only. **Date:** 2026-11-26.
Install the wifi-image-integration bench packages (native iw/iwinfo +
clean-room cfg80211/hi5622_wifi modules) into the RUNNING pair-B rootfs over
SSH, with no flash write, no boot-selector change and no reboot. Fully
reversible with apk del. Then use the rootfs binaries for the bench ladders
instead of pushing them to /tmp.

Two sets are supported:
* **full** (default): the seven packages incl. both kmod packages. This needs
  the running image's kernel package version to match the kmod ABI pin
  (currently 6.18.60~6f995278...-r1) - the device gate fails closed otherwise
  and the deploy auto-rolls back (this happened on 2026-11-26; see REPORT.md
  section 7).
* **--userspace-only**: only iw, iwinfo, libiwinfo, libiwinfo-data and
  wireless-regdb (no kernel= dependency, no modules). Gives native iw/iwinfo
  immediately on any image whose package manager/deps match.

Inputs are the artifacts from ../wifi-image-integration (built image container
df6a78c3..., unchanged kernel slice 13e81214..., seven .apk packages). Nothing
was flashed or executed on the router by this lane - the attended deploy is the
Lead's call.

## Quick start

Host-only checks (never contact the router):

    ./tools/selftest.sh
    ./deploy-t0.sh --check
    ./tools/verify-apks-host.sh --evidence build-evidence/host-apk-simulation.txt

Attended deploy / verify / rollback:

    ./deploy-t0.sh --apply --attended --yes-really
    ./deploy-t0.sh --verify --attended --yes-really
    ./deploy-t0.sh --rollback --attended --yes-really

Userspace-only set (no kmod kernel-ABI pin; use when the image kernel package
does not match the kmod pin, or when only native iw/iwinfo are wanted):

    ./deploy-t0.sh --userspace-only --apply --attended --yes-really
    ./deploy-t0.sh --userspace-only --verify --attended --yes-really
    ./deploy-t0.sh --userspace-only --rollback --attended --yes-really

Rollback policy: an explicit --rollback removes the selected set. An automatic
rollback after a FAILED run only undoes that run's own install
(install_mutated=1); an idempotent verify never deletes a pre-existing set.
Add **--keep-on-failure** to any --apply to keep the set even after a failure
(ro: inspect the evidence, then roll back explicitly).

Optional write-free probe of the rootfs modules in the same window:

    ./deploy-t0.sh --apply --attended --yes-really --load-check

Then follow BENCH-VALIDATION.md (fresh boot, native iw/iwinfo/driver, D1
wiphy+VAP gate with rootfs iw).

## What is verified (see REPORT.md for the full evidence)

* image is apk-based (apk-tools 3.0.5, ADB v3 packages, arm_cortex-a9);
* both kmods depend on kernel=6.18.60~6f995278...-r1, and that hash is
  recomputed from the 6.18.60 tree's own .config.set (vermagic) - the packages
  are ABI-pinned to the validated kernel slice;
* a host-side del/add/del transaction against the real built-image rootfs
  reproduces byte-identical payload trees (install = built image, rollback =
  pre-T0 baseline), is idempotent, and fails closed on a mismatched installed
  kernel package; 61/61 simulation gates PASS;
* userspace-only set: host check 40/40 PASS (all five packages proven to have
  no kernel= dependency) and transaction simulation 63/63 PASS (install payload
  == built image minus the two module payloads, no modules installed, apk-v3
  world entries present and removed, idempotent, rollback byte-identical to the
  baseline). Full-set simulation is 68/68;
* hardware-found gate defects fixed and regression-tested (REPORT.md F8): the
  manifest column comparison, apk-v3 world lines for pre-existing installs,
  the iwinfo execution gate (accepts "wlan0 ESSID: unknown" with rc=0 and
  records the output) and rollback-of-pre-existing-install; selftest 105/105;
* package hooks are stock OpenWrt boilerplate only (no custom code);
* no autoload entry; module load stays manual and write-free at probe.

## Files

| file | what |
| --- | --- |
| deploy-t0.sh | host orchestrator: --check (default) / --apply / --verify / --rollback / --load-check / --userspace-only / --keep-on-failure |
| device/t0-device.sh | device-side modes preflight/install/rollback/load/verify |
| tools/verify-apks-host.sh | byte-level host apk-transaction simulation |
| tools/selftest.sh | device-free lane selftest |
| tools/expected-packages.txt, tools/expected-files.txt | pinned sha256 manifests |
| build-evidence/ | recorded host-simulation + host-check transcripts, full and userspace-only (text only) |
| REPORT.md | analysis, findings, safety, limits |
| BENCH-VALIDATION.md | attended validation procedure + acceptance gates |

## Safety / hard rules honoured

Writes only into the persistent overlay (/usr, /usr/lib, /usr/share,
/lib/modules, /lib/firmware, /lib/apk db/world + a /root receipt); staging in
/tmp only. No mtd/ubiformat/nvram/sysupgrade, no boot-selector write, no
reboot, no rmmod, no /dev/mem, no UART. Device actions require --attended and
--yes-really plus a PASSING bench-link check and a READY recovery door. A
failed install that changed the package db triggers an automatic apk del
rollback; --keep-on-failure disables that, and an idempotent verify-only run
never auto-rolls back. No vendor firmware, binaries or calibration bytes are
committed in this directory.

## Honest limits

Host-only lane: the install was not executed on the router here; the running
image's package state is checked by the on-device preflight (it fails closed on
kernel/version/overlay mismatch). T0 is a bench convenience, not a product
upgrade, and does not prove the image boots. Wiphy/VAP functionality still
requires the attended golden boot; the scan response gap (tasks 117/118) is
unchanged by T0. The vendor firmware blob remains local evidence only.

Known ABI gap (measured 2026-11-26): the running image's kernel package
(6.18.60~5de0e425...) does not match the kmod .apk pin (6.18.60~6f995278...);
the full set therefore fails closed on that box by design. Userspace-only works
now. The canonical fix is to rebuild the image from the current tree and flash
it (self-consistent kernel package + kmods); rebuilding the kmods against the
old pin is a non-canonical local fallback. See REPORT.md section 7 and
BENCH-VALIDATION.md section 7.
