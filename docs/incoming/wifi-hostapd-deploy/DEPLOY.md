# DEPLOY - zero-flash hostapd package deploy (T0 style)

**Lane:** wifi-hostapd-deploy (task-125).  Host-only lane: **none of the
attended steps below was executed by this lane.**  They are the Lead's call
(human at the bench, recovery door armed, UART capture read-only).

This is the same mechanism as the hardware-validated T0 userspace deploy
(../wifi-image-deploy): install the built `.apk` files into the RUNNING
pair-B rootfs over the persistent overlay.  **No flash write, no boot-selector
change, no reboot, no module load/unload.**  Rollback is `apk del` + remove
the config dir.

## What gets installed

| package | version | purpose |
| --- | --- | --- |
| hostapd-mini | 2026.08.07~831364bf-r7 | the hostapd v2.12 binary + /usr/sbin/hostapd |
| hostapd-common | 2026.08.07~831364bf-r7 | init script, APUP ucode script, ACL, capability files |
| ucode-mod-nl80211 | 2026.10.28~dfc64d41-r1 | hostapd runtime dependency (ucode stack) |
| ucode-mod-rtnl | 2026.10.28~dfc64d41-r1 | hostapd runtime dependency (ucode stack) |

Plus two bench fixtures copied to `/root/hostapd-deploy/`: hostapd-open.conf
and hostapd-wpa2.conf (mode 600).  None of the four packages has a `kernel=`
dependency: the open kmod-ABI pin is untouched and the modules stay /tmp.

## Modes

Host-only (never contacts the router):

    ./deploy-hostapd.sh --check --evidence build-evidence/host-check.txt

Attended (needs `--attended --yes-really`, a PASSING bench link and a READY
recovery door; the script refuses otherwise):

    ./deploy-hostapd.sh --apply    --attended --yes-really
    ./deploy-hostapd.sh --verify   --attended --yes-really
    ./deploy-hostapd.sh --rollback --attended --yes-really

Options: `--artifacts DIR` (default `/home/shibbityshwab/hostapd-lane-artifacts`,
falling back to the tree's `bin/packages/arm_cortex-a9/base`), `--owrt DIR`,
`--host user@ip` (default `root@192.168.1.1`), `--evidence FILE`,
`--keep-on-failure`.

## Host check (--check)

87 gates, no device contact: pinned table sha256; per-apk size+sha256; real
host `apk adbdump` metadata (name/version/arch/installed-size); full dependency
closure against `tools/image-baseline-packages.txt`; payload extraction with
the 8-file sha256 manifest (no extras/omissions); ELF32 ARM EABI5 PIE + musl
interpreter + 8 DT_NEEDED libraries resolved to baseline packages; config
validity with a tree-source hostapd v2.12 (+ broken-config negative control);
ladder-recipe consistency (107 checks); script syntax + static safety audit.
A failed host check aborts every mode before any device action.

## Device-side gates (preflight/install/verify/rollback)

All printed as OK/BAD/KV lines and re-parsed by the host; the device script
runs over the project ssh key and is staged in `/tmp/hostapd-deploy-stage`.
Every mutating mode is fail-closed on a dirty preflight.

* preflight: uid 0; apk v3 (`/usr/bin/apk`, `/lib/apk/db/installed`);
  overlay root + `/overlay` mounted; >= 8 MiB free; `/lib/apk/db` and
  `/etc/apk` writable; staging tables+apks present with matching sizes and
  sha256; no package already installed at a different version; no
  `/etc/rc.d` wpad autostart link; records loaded cfg80211/hi5622_wifi.
* install: counts missing packages; `apk add --simulate` resolves the
  transaction (authoritative dependency check, fail-closed) -> `apk add
  --allow-untrusted --force-non-repository --no-network --repositories-file
  /dev/null` -> per-package installed-version read-back -> `/etc/apk/world`
  entries -> the 8 installed-file sha256 values -> config fixtures copied to
  `/root/hostapd-deploy` (sha verified) -> `hostapd -v` shows
  `hostapd v2.12` -> no autostart link -> receipt
  `/root/.wifi-hostapd-deploy.receipt` (only on a fully green install).
* verify: read-only repetition of the read-back gates.
* rollback: `apk del` the set, confirm packages/files are gone, remove
  `/root/hostapd-deploy` and the receipt.  No reboot.

Automatic rollback: if an `--apply` fails after it mutated the package db
(install_mutated=1), the tool rolls back its own transaction unless
`--keep-on-failure` is given (then inspect the evidence first and roll back
explicitly).  An idempotent verify never deletes a pre-existing install.

## Failure matrix

| symptom | meaning | action |
| --- | --- | --- |
| host check FAIL | artifact/pin/tool drift | fix on the host; no device action |
| bench link / recovery gate FAIL | bench not armed | do not fire |
| device preflight FAIL (kernel/apk/overlay) | wrong image or wrong box | stop; the right box runs the apk-based bench image |
| `apk add --simulate` FAIL | missing baseline dependency or version conflict | stop; capture the simulate log; the host check should have caught closure |
| `apk add` FAIL | package db mutation failed | auto-rollback (or inspect with --keep-on-failure) |
| installed-file sha mismatch | corrupt transfer | roll back and re-run the deploy |
| hostapd -v missing/odd | wrong package set | roll back; re-check BUILD-NOTES identities |

## Fallback (documented alternative, not preferred)

If the box cannot use apk (package db broken), the same payload can be pushed
to `/tmp` manually: extract the four `.apk` files with the host
`apk extract` into a tree, push `usr/sbin/hostapd`, the two
`usr/lib/ucode/*.so` modules, the hostapd-common files and the two config
fixtures into `/tmp/hostapd-deploy/`, then run
`LD_LIBRARY_PATH=/tmp/hostapd-deploy/usr/lib /tmp/hostapd-deploy/usr/sbin/hostapd ...`.
This is the same bytes as the apk set, gives no package-db tracking and no
clean rollback, and is therefore only a fallback - the `.apk` deploy is the
supported path.

## Running image vs built image note

The deploy targets the RUNNING pair-B rootfs, not the built image.  The
running cfg80211 image and the integration image are both apk v3 /
arm_cortex-a9 musl; the host-side closure check uses the integration image's
reproduced package list, and the on-device `apk add --simulate` is the
authoritative check (it verifies every dependency against the box's own db
before any mutation).  If the three baseline ucode modules
(`ucode-mod-fs/ubus/uloop`) are ever absent from a running image, the
simulate step fails closed and the missing package must be added to the set
first.

## Evidence

Host: `build-evidence/host-check.txt`, `build-evidence/selftest.txt`,
`build-evidence/config-validation.txt` (committed, text only).
Attended: pass `--evidence build/wifi-hostapd-deploy/<stamp>.txt` (repository
root `build/` is gitignored) and record the device output files from
`/tmp/hostapd-deploy-stage/` (`apk-simulate.log`, `apk-add.log`,
`apk-del.log`) with the session evidence.
