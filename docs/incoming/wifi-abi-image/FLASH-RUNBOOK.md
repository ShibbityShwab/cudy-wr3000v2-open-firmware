# FLASH-RUNBOOK - attended kernel+rootfs flash of the ABI-consistent bench image

**Lane:** wifi-abi-image (shared task task-128), host-only lane. **Date:** 2026-11-26.
**Status: procedure written, NOT executed by this lane.** Every write is on the router and
requires a human at the bench, the recovery door armed and a read-only UART capture.

Purpose: install the bench image whose **rootfs apk db carries the kernel package version the
kmod .apk files pin** (6.18.60~6f995278...-r1), so the T0 full-set deploy (cfg80211 + hi5622 kmods)
installs on the device instead of failing closed at the kernel-ABI gate. Kernel and rootfs are
written from **one build** (the rebuilt image, byte-identical to the recorded bench image
df6a78c3).

This is the canonical P1 path of
`../wifi-image-deploy/REPORT.md` section 7.3. It complements `../wifi-image-integration/DEPLOY.md`
T1 (rootfs only): here BOTH members are written from the same container and read back.

---

## 0. TL;DR

1. Verify the host artefacts (`build/abi-image-artifacts/SHA256SUMS`).
2. Arm the bench: `tools/bench-link.sh`, `tools/recovery-card.sh --arm`,
   `tools/uart-capture.sh start`.
3. Save the flash-contract witnesses (running pair, selector, kernel-slot hashes).
4. Switch the box to **stock pair A** (recorded pair A) and get a stock root shell with the
   crafted backup (`../cudy-backup-root`, hardware-validated 2026-11-23).
5. Serve the artefacts; run `tools/stock-telnet-install.sh --inspect`, then
   `--apply --attended --recovery-armed`. It writes **kernelb + rootfsb** from the one container,
   verifies the uploads / write read-backs / active pair unchanged, sets env `bootflag=b` and the
   selector cell 0x11 together, reboots and verifies our image booted.
6. Post-boot ABI receipt: `apk query --fields version kernel` must print
   **6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1**. Then the T0 full-set verify/apply passes.
7. Rollback: stock pair A is untouched; selector/env back to A, or physical RESET+TFTP.

**Hard rules:** never write the active pair; never write CA `0x400392F0`; never read the RC misc
window `0x10161000`; never `rmmod` the vendor modules; never send anything on the UART (host->router
TX is dead - the capture is read-only). The tool refuses a stock boot whose cmdline uses rootfsb /
whose env bootflag=b because `ubiformat /dev/mtd14` on a mounted rootfsb silently leaves our kernel
with the stock filesystem (mixed, no management IP). If the box is not on a clean recorded pair A,
fix that first (RUNBOOK "Post-recovery install: two-stage pair rule").

---

## 1. What is being flashed (rebuilt by this lane; evidence in build-evidence/)

Container: `build/abi-image-artifacts/bench-image-sysupgrade.bin`

| item | size | sha256 |
| --- | --- | --- |
| sysupgrade container | 6,533,439 B | `df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310` |
| kernel slice (member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` |
| full kernel slot (kernelb, 8,650,752 B padded) | 8,650,752 B | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` |
| rootfs UBI (member `root`) | 3,145,728 B | `62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746` |

The kernel slice is byte-identical to the hardware-validated cfg80211 slice, so the kernel write
is **provably redundant**; it is written anyway because the installer writes both members of one
container and verifies the whole kernel slot read-back (the flash contract). What actually changes
is the rootfs: its `/lib/apk/db/installed` carries `kernel = 6.18.60~6f995278...-r1` (the running
pair-B rootfs carries `6.18.60~5de0e425...-r1`, an older build's bookkeeping - that is the whole
cause of the T0 kernel-ABI failure).

Also in the artefacts dir: the seven deployable `.apk` files of the same build (5 userspace +
2 kmods; hashes in `../wifi-image-deploy/tools/expected-packages.txt`), `kernel-slice.bin`,
`rootfs.ubi`, `root.squashfs`, `SHA256SUMS`. Rebuild verification is in `../REPORT.md` and
`build-evidence/`.

---

## 2. Preconditions (all must hold; stop if any fails)

1. **Human at the bench.** Recovery is physical: power-off + RESET held through the counter + TFTP.
2. **Bench link PASS** (read-only check, fix with `--apply` if needed):
   ```sh
   tools/bench-link.sh
   ```
3. **Recovery door READY** (TFTP server on UDP 69 + recovery image digest):
   ```sh
   tools/recovery-card.sh --arm
   tools/recovery-card.sh          # re-check, must say READY
   ```
   Keep the TFTP server running for the whole flash; stop it only after the post-boot checks pass.
4. **Read-only UART capture** to an absolute path (never transmit on the UART):
   ```sh
   tools/uart-capture.sh start --label abi-image-flash --timeout 1800
   tools/uart-capture.sh check
   ```
5. **Artefacts verified on the host** (nothing from this section touches the router):
   ```sh
   cd build/abi-image-artifacts
   sha256sum -c SHA256SUMS
   sha256sum kernel-slice.bin rootfs.ubi bench-image-sysupgrade.bin
   ```
   Expect the three hashes in section 1. Also verify the full kernel slot hash (it is what the
   installer's read-back compares):
   ```sh
   { cat kernel-slice.bin; head -c $((8650752 - 3372638)) /dev/zero | tr '\0' '\377'; } | sha256sum
   # 1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
   ```
6. **No concurrent world build / no make in the shared tree.** The artefacts are final; do not
   rebuild between this verification and the flash.
7. **Recovery inputs present** for the stock root channel (host files, gitignored):
   `dumps/router-20261108/full/mtd6-bdinfo.bin` and `.provision-venv/bin/python` (craft_backup
   needs pycryptodome); project SSH key `.omo-secrets/router_ed25519`.
8. **Box healthy on our image pair B** (read-only SSH):
   ```sh
   tools/rsh.sh 'uname -r; cat /proc/cmdline; cat /proc/mtd; cat /sys/class/ubi/ubi0/mtd_num'
   ```
   Expect 6.18.60, `ubi.mtd=rootfsb` + the project cmdline marker, and ubi0 on mtd14 (rootfsb).
   Stop if the box is not on our pair B image.

---

## 3. Save the rollback witnesses and the flash contract (read-only, before any write)

```sh
mkdir -p build/abi-image-flash/$(date -u +%Y%m%dT%H%M%SZ)
# editor note: keep the transcript of every section below in that dir (tee)

# 1) which pair is running (our kernel's selector driver; node name may be boot-selector)
SEL=$(ls -d /sys/devices/platform/*selector* 2>/dev/null | head -1)
tools/rsh.sh "cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num; ls $SEL; cat $SEL/current_pair; cat $SEL/raw"

# 2) running pair-B kernel slot hash (kernelb = mtd12) - must already be 1794a531...
tools/rsh.sh 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'

# 3) overlay inventory worth keeping (the flash creates a FRESH empty overlay; /overlay is lost)
tools/rsh.sh 'ls -la /overlay; cat /etc/config/dhcp /etc/config/network 2>/dev/null | head -80'

# 4) stock pair A kernel slot hash (kernela = mtd11) - recorded so we can prove it unchanged later
tools/rsh.sh 'dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum'
```

Record the two slot hashes and the selector value in the transcript. **Accept the overlay loss:**
SSH host keys, uci config and any `/overlay` receipts are replaced by the image defaults. The image
enables dropbear with the stock OpenWrt blank-root-password patch, so a fresh image answers root SSH
with no key (host keys are new: use `-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null`,
as `tools/rsh.sh` already does). Re-provision the project key after the first boot if wanted.

---

## 4. Switch the box to stock pair A

From our running image, move the boot-selector cell to A and reboot. The loader sees env
`bootflag=b` against a recorded A and takes its documented failover path (re-stamps the env to a
and boots pair A = stock 2.5.24).

```sh
# our kernel exposes the A/B boot-selector at /sys/devices/platform/<selector>/{current_pair,raw,next_pair}
SEL=/sys/devices/platform/boot-selector
tools/rsh.sh "ls $SEL && cat $SEL/current_pair && echo a > $SEL/next_pair && cat $SEL/current_pair && cat $SEL/raw"
# expected: current_pair was b, after the write it is a (raw 0x00000010)
tools/rsh.sh 'sync; reboot'          # connection drops
```

Watch the **read-only UART capture** (do not type anything): a stock boot shows the stock uImage
(`Uncompressing Linux` / `Data Size: 4,294,096`) and the 5.10.201 kernel; our image shows
`Linux-6.18.60`. If the box comes back on our image, the cell write did not take - re-check the
selector node name with `ls /sys/devices/platform/*selector*` and retry once.

Wait for the stock LAN: ping `192.168.10.1` and open the stock LuCI
`http://192.168.10.1/` (bench link carries 192.168.10.44/24).

---

## 5. Get a root shell on stock (crafted config-backup channel)

Hardware-validated 2026-11-23; full format/risks in `../cudy-backup-root/REPORT.md` and
`RUNBOOK.md` section 6a. It only ADDS files and reboots the box; it writes no flash.

```sh
REPO=$(pwd)
PY=$REPO/.provision-venv/bin/python
$PY opensource/docs/incoming/cudy-backup-root/craft_backup.py emit \
    --bdinfo $REPO/dumps/router-20261108/full/mtd6-bdinfo.bin \
    --ssh-key $REPO/.omo-secrets/router_ed25519.pub \
    -o /tmp/abi-crafted_backup.bin
# expect: members etc/rc.local, etc/omo_login.sh, etc/uci-defaults/99-omo-arm,
#         root/.ssh/authorized_keys, etc/dropbear/authorized_keys, etc/config/dropbear;
#         "self-check decrypt: OK"
```

Upload it. Simplest is the **LuCI UI** (System -> Backup / Flash Firmware -> Restore backup:
choose the file, wait for MD5/Size/Generate time, click Restore). For automation, a LuCI session
cookie is required (log in to stock first; see REPORT section 4):

```sh
B=http://192.168.10.1/cgi-bin/luci
COOKIE='sysauth_https=...'          # full cookie header from the browser session
TOKEN=$(curl -s -b "$COOKIE" "$B/admin/system/backup" \
        | sed -n 's/.*name="token" value="\([^"]*\)".*/\1/p' | head -1)
# 1) upload + decrypt (the handler runs crypt -d)
curl -s -b "$COOKIE" \
  -F "cbid.backup.1.restore.upload=true" \
  -F "cbid.backup.1.restore=@/tmp/abi-crafted_backup.bin;filename=backup.bin" \
  -F "token=$TOKEN" -F "cbi.submit=1" \
  "$B/admin/system/backup" >/dev/null
# 2) accept the restore (tar -tzf check, 302 to reboot?backup=)
curl -s -b "$COOKIE" -L \
  -d "token=$TOKEN" -d "cbi.submit=1" -d "cbid.backup.1.proceed=1" \
  "$B/admin/system/backup" >/dev/null
# 3) run the restore chain (plain reboot/apply only reboots)
curl -s -b "$COOKIE" "$B/admin/system/reboot/apply?backup=true" >/dev/null
```

The box reboots (~1 min). The payload starts an unauthenticated root telnet on the bench LAN and
keeps it via /etc/rc.local. Confirm root:

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r'
# expect: uid=0(root) ... 5.10.201
```

This root telnet is bench-LAN only; it is gone once our image boots (fresh overlay). Close it on
the stock side with the cleanup in REPORT section 4 if the stock pair is ever kept.

---

## 6. Stock-side read-only checks + the inspect plan

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- \
  'cat /proc/cmdline' \
  'cat /sys/devices/platform/sysenv/boot_reg' \
  'fw_printenv bootflag 2>/dev/null' \
  'cat /sys/class/ubi/ubi*/mtd_num 2>/dev/null; cat /proc/mtd'
```

Expected: cmdline uses `ubi.mtd=rootfsa` (NOT rootfsb), selector cell 0x10, env `bootflag=a`,
the attached ubi device is on mtd13 (rootfsa), and mtd14 is inactive. If any of that is wrong:
**stop**. First fix the pair to a consistent A (RUNBOOK two-stage rule:
`ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag a; echo 0x10 > /sys/devices/platform/sysenv/boot_reg; sync; reboot`)
and restart this section.

Stage the container and start the HTTP server the stock `wget` will read from (the host address
is derived, never remembered - the adapter carries 192.168.10.44):

```sh
HTTP_IP=$(ip -4 -o addr show | awk '$4 ~ /^192\.168\.10\./ {sub(/\/.*/, "", $4); print $4; exit}')
echo "HTTP_IP=$HTTP_IP"                       # expect 192.168.10.44
mkdir -p ~/stage-abi-image
cd ~/stage-abi-image && python3 -m http.server 8000 --bind "$HTTP_IP" >/tmp/abi-http.log 2>&1 &
sleep 1
curl -s -o /dev/null -w '%{http_code}\n' "http://$HTTP_IP:8000/kernel.bin"   # 200 after the split below
```

Then the inspect (read-only on the box; it prints the plan and exits):

```sh
cd "$REPO"
tools/stock-telnet-install.sh --inspect \
  --image build/abi-image-artifacts/bench-image-sysupgrade.bin \
  --staging "$HOME/stage-abi-image" \
  --http-base "http://$HTTP_IP:8000"
```

The inspect output must say/contain:
* image kernel sha == `13e81214...` and **slot sha == `1794a531...`**;
* image root sha == `62344e80...`;
* stock selector cell 0x10 -> active pair a, **write pair b (active pair untouched)**;
* active kernel slot sha (stock pair A) recorded;
* `INSPECT ONLY - nothing written`.

Any refusal/error message is a stop. The tool itself re-checks bench-link PASS and the recovery
card READY before it will write.

---

## 7. Apply: stock-assisted kernelb + rootfsb write from the one container

```sh
tools/stock-telnet-install.sh --apply --attended --recovery-armed \
  --image build/abi-image-artifacts/bench-image-sysupgrade.bin \
  --staging "$HOME/stage-abi-image" \
  --http-base "http://$HTTP_IP:8000"
```

What the tool does, in order (all through the stock root telnet shell):
1. splits the container into kernel.bin + rootfs.ubi in the staging dir and prints their hashes;
2. stock `wget` both members over the bench HTTP server to /tmp and re-checks sha256;
3. `mtd erase kernelb` + `mtd write /tmp/fi-k.bin kernelb`, then reads the whole 8,650,752-byte
   slot back and requires **1794a531...**;
4. `ubiformat /dev/mtd14 -y -f /tmp/fi-r.ubi` (rootfsb - inactive because stock runs pair A);
5. re-reads the pair-A kernel slot and requires it unchanged (stock pair A is the rollback);
6. `fw_setenv bootflag b` (read back), then writes the selector cell 0x11 through
   `/sys/devices/platform/sysenv/boot_reg` and reads it back - **env and cell for the same pair,
   no window between them**;
7. `sync; reboot`, polls ping 192.168.1.1, then SSH, and PASSes only if `uname -r` is 6.18.60,
   `/proc/cmdline` contains `ubi.mtd=rootfsb` and the project cmdline marker.

Expected final line: `PASS: wrote pair b, handover verified, our image booted.`

**Stop rules (do not retry a failed write):**
* kernel slot read-back != 1794a531: STOP; the kernel write did not take. Pair A still boots
  (selector/env are untouched until step 6); recover to stock A, inspect the box, do not re-write
  on the same boot.
* `ubiformat` refuses/fails: STOP (it usually means the rootfs is the active one; re-check
  section 6). Pair A still boots.
* active pair changed: STOP - this must never happen; go to the recovery card.
* after the reboot the box does not answer within 240 s: the loader should fall back to pair A
  (see SELECTOR-0x21.md). Wait 2 minutes; then a plain power-cycle (no RESET hold) re-attempts the
  loader path. If it stays silent: **physical RESET-hold + TFTP recovery** (recovery card is armed).

Keep the UART capture running through the reboot; a successful boot prints the 6.18.60 banner,
the GP1 endpoint reset line and the userspace heartbeats.

---

## 8. Post-boot ABI receipt (the point of the flash)

```sh
tools/rsh.sh 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num; cat /proc/mtd'
# expect: 6.18.60, ubi.mtd=rootfsb + marker, ubi0 -> mtd14, 17 partitions

# kernel slot really is the validated slice
tools/rsh.sh 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
# expect: 1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e

# THE ABI RECEIPT: installed kernel package version == the kmod .apk pin
# (network-free query of the local db, the same form the T0 device script uses)
tools/rsh.sh 'apk --repositories-file /dev/null --no-network query --fields version kernel'
# expect: Version: 6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1
tools/rsh.sh 'grep -E "^(P|V):" /lib/apk/db/installed | grep -A1 "^P:kernel$"'

# native userspace/modules from the rootfs (no /tmp staging)
tools/rsh.sh 'ls -l /usr/sbin/iw /usr/bin/iwinfo /lib/modules/6.18.60/cfg80211.ko /lib/modules/6.18.60/hi5622_wifi.ko; iw -v; iwinfo'
# optional: compare the sha256s with build-evidence/abi-verify.txt

# the pair is recorded so the next reboot stays on pair B
tools/rsh.sh 'cat /sys/devices/platform/boot-selector/current_pair; cat /sys/devices/platform/boot-selector/raw'
# expect: b / 0x00000011   (the image's /etc/init.d/luofu_record_pair re-records it)
```

If `apk query --fields version kernel` does NOT print the 6f995278 pin, the rootfs written is not
the one verified in section 1 - stop and investigate (the kernel slice could still be the validated
one; the mismatch would be in the rootfs). Do not "force" apk past the kernel= dependency.

---

## 9. Post-flash bench enablement (host, T0 full-set deploy now passes)

The failing T0 run of 2026-11-26 was the kernel-ABI gate. On the new image the same command set
is now an idempotent verify (the seven packages are already installed in the image rootfs):

```sh
cd opensource/docs/incoming/wifi-image-deploy
./deploy-t0.sh --check --attended --yes-really
./deploy-t0.sh --apply --attended --yes-really --keep-on-failure
./deploy-t0.sh --verify --attended --yes-really
./deploy-t0.sh --load-check --attended --yes-really
```

Expected: the kernel-dep pre-gate PASSes (running kernel package == kmod pin), the package set is
already present (installed_mutated=0), payload sha256 read-backs PASS, and `--load-check` loads
cfg80211 + hi5622_wifi write-free from the rootfs. Reboot once and re-run `--verify` to prove the
install state survives (it is in the image rootfs now, not in /tmp or a volatile overlay).

Then the normal attended Wi-Fi ladder (golden boot -> C2 -> D1 -> D2 -> E1, the one-command
matrix) is the next bench session's business, not this runbook's.

---

## 10. Cleanup and evidence

```sh
# stop the staging HTTP server (never leave it running)
pkill -f 'http.server 8000' || true
# keep the UART capture as the boot witness; stop it once the box is stable
tools/uart-capture.sh stop
# after the post-boot checks PASS: stop the TFTP server (leave it armed if the box is still suspect)
tools/recovery-card.sh --stop
```

Evidence to keep under `build/abi-image-flash/<stamp>/`: the `--inspect` transcript, the
`--apply` transcript (hashes + read-backs), the UART log path, the pre/post selector and slot
hashes, the `apk query --fields version kernel` receipt, and `sha256sum -c SHA256SUMS` output.
`/tmp` is not evidence.

---

## 11. Rollback (in order of preference)

1. **Stock pair A is untouched** and is the primary rollback. From our image:
   `echo a > /sys/devices/platform/boot-selector/next_pair` then `sync; reboot`; the loader
   fails over to pair A (stock 2.5.24, 192.168.10.1). From stock the same cell/env writes apply.
2. **Our image boots but the wrong state persists:** re-run section 7; the tool is idempotent and
   only writes pair B. There is no kernel rollback needed (the slice is unchanged from the
   validated 13e81214).
3. **Boot failure with no fallback** (loader does not return to pair A): physical recovery -
   `tools/recovery-card.sh` card, power-off + RESET held through the counter, TFTP
   `recovery.bin` from 192.168.1.88. This is the last resort and costs a stock restore on the
   affected pair only.
4. Never mix: do not hand the selector to pair B if the rootfs write failed. A half-written pair B
   is not bootable; pair A is.

---

## 12. Appendix - manual equivalent (if the installer is unavailable)

Only with the recovery card armed, UART capturing and the box on a consistent stock pair A. These
are exactly the tool's steps; keep every read-back.

```sh
# on the stock root telnet shell (192.168.10.1), after the split+HTTP server of section 6:
cd /tmp
wget -q -O /tmp/fi-k.bin http://192.168.10.44:8000/kernel.bin
wget -q -O /tmp/fi-r.ubi http://192.168.10.44:8000/rootfs.ubi
sha256sum /tmp/fi-k.bin /tmp/fi-r.ubi
#   fi-k.bin 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
#   fi-r.ubi 62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746

# kernelb (mtd12) - whole-slot read-back must be 1794a531...
mtd erase kernelb
mtd write /tmp/fi-k.bin kernelb
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum

# rootfsb (mtd14) - only when the running rootfs is rootfsa (check /sys/class/ubi/ubi*/mtd_num)
ubiformat /dev/mtd14 -y -f /tmp/fi-r.ubi

# pair A unchanged (rollback witness)
dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum

# handover: env bootflag and selector cell for the SAME pair, no window
ln -sf /bin/fw_printenv /tmp/fw_setenv
/tmp/fw_setenv bootflag b; fw_printenv bootflag
echo 0x11 > /sys/devices/platform/sysenv/boot_reg; cat /sys/devices/platform/sysenv/boot_reg
sync
reboot
```

If any read-back mismatches: do not reboot into pair B; leave the selector on A (or set
`bootflag=a` + cell 0x10) and reboot to stock.

---

## 13. What this runbook does NOT do

* It does not reboot between phases or run the Wi-Fi ladder; that is the bench session package.
* It does not include hostapd/wpad (separate lane); if that lands before the fire, rebuild the
  image again - but then the kmod pin is still 6f995278 and only the rootfs hash changes.
* It does not prove the image "boots as a product"; it is a bench install with the physical
  recovery door armed.
* It does not touch the U-Boot env blocks directly (the loader's own failover path re-stamps
  `bootflag`; the tool sets it through the shell exactly once).
