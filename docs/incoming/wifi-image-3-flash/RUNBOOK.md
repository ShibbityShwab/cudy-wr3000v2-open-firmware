# RUNBOOK - attended IMAGE #3 flash (rootfs-only + full container)

**Lane:** wifi-image-3-flash (shared task task-201), 2026-11-28.
**Status: procedure written, NOT executed.** This lane never contacted the router,
the bench link, the UART, TFTP or the network; nothing was written or rebooted.
**The Lead fires, attended.**

**Board:** Cudy WR3000 v2.0 (Hi5671YV200 + Hi5622V100, 2x Cortex-A9, 128 MB NAND,
128 MB DDR3; stock OpenWrt 22.03.6 / kernel 5.10.201).

**Authority chain.** This runbook implements the install answer in
[../wifi-image-3/FLASH-FEASIBILITY.md](../wifi-image-3/FLASH-FEASIBILITY.md) on the
transport validated by [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md);
the physical invariants are root BENCH-FACTS.md. If a statement conflicts with
BENCH-FACTS.md / the newest ledger entry, BENCH-FACTS.md wins. If the bench differs
from what this runbook expects at any step: **STOP**, record, do not improvise.

**The flash unit:** the IMAGE #3 sysupgrade container
7,843,840 B sha256 `4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53`
(built + verified 129/129, not flashed). Pinned identities and provenance:
[README.md](README.md); the detailed check list: [PREFLIGHT.md](PREFLIGHT.md).

---

## 0. TL;DR (one screen)

1. **Recovery door + human + UART**: human at the bench, `tools/bench-link.sh` PASS,
   `tools/recovery-card.sh --arm` then READY, read-only UART capture running
   (section 2). No human = no fire.
2. **Host preflight**: `./tools/preflight-image-3.sh` -> PASS (23/0 on the bench
   host; committed output in build-evidence/preflight-run.txt). No rebuild after it.
3. **Switch to stock pair A** and get a stock root shell (section 4).
4. **Stock read-only gates** (section 5): cmdline `ubi.mtd=rootfsa`, UBI on mtd13,
   selector cell `0x10`, env `bootflag=a`, kernelb slot `1794a531...`,
   kernela recorded. Produce the witness files and run the **strict fire-gate
   preflight** -> PASS fire-ready.
5. **Write**: Path A rootfs-only (section 6, recommended: `ubiformat /dev/mtd14` only)
   or Path B full container (section 7: kernelb + rootfsb through
   `tools/stock-telnet-install.sh --apply`). Then the **pair-b handover**
   (env `bootflag=b` + selector `0x11`, read back, reboot).
6. **Post-boot receipts** R1-R10 (section 8): kernel pin, module hash/params,
   regdomain US, HE AP, 5 GHz gates, page golden, firmware re-push.
7. **Rollback** = the pair switch back to A + a previous rootfs UBI (section 10);
   physical RESET+TFTP is the last resort. **STOP conditions: section 11.**

---

## 1. What is being flashed, and the two write paths

| item | size | sha256 |
| --- | --- | --- |
| sysupgrade container (the flash unit) | 7,843,840 B | `4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53` |
| kernel slice (member `kernel`; **unchanged**) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` |
| kernelb slot (mtd12; 0xFF-padded slice) | 8,650,752 B | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` |
| rootfs UBI (member `root`) | 4,456,448 B | `90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3` |
| driver module installed (`/lib/modules/6.18.60/hi5622_wifi.ko`, 64 params) | 358,544 B | `a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed` |
| cfg80211.ko (regdomain fix) | 2,317,952 B | `bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91` |
| hostapd (802.11ax build, v2.12) | 1,618,388 B | `fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6` |
| kernel package pin (rootfs apk db) | - | `6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1` |

The kernel slice is **byte-identical** to the image #2 / ABI-image slice already in
kernelb; the whole delta is the **rootfs** (regdomain `cfg80211.ko`, ax hostapd,
the merged 64-param module, the image #3 text policy payload). Therefore:

* **Path A - rootfs-only (recommended, minimal write).** Write **mtd14 only** with the
  container's `root` member; leave kernelb untouched; hand pair B over. Requires the
  on-box kernelb to already hash `1794a531...` (the strict preflight proves it).
* **Path B - full sysupgrade container.** Write **both** pair-b members from the
  container (kernelb = the same validated slice + rootfsb) with the project installer,
  which verifies uploads, both read-backs, the untouched active pair, the handover and
  the booted identity. Use it when kernelb's content cannot be confirmed, or as the
  reconstruction path. The extra kernel erase/write is redundant but safe (same bytes).

**Not an install path:** the vendor stock `sysupgrade` / LuCI web-flash UI. The
container's own `INSTALL-STATUS.txt` says **NOT FLASHABLE-UNTIL-INSTALLED**; its
`platform_do_upgrade` has never run on a device, there is no factory image, and the
baked command line pins `ubi.mtd=rootfsb` (pair B only). The bench installs are the
manual pair-B writes below.

---

## 2. Preconditions - the recovery door is mandatory

Everything in this section must hold **before** any write. The preflight and the
installer re-check the mechanical parts; the human/console parts are the Lead's.

### 2.1 Human at the bench

Recovery is **physical** (power-off + RESET held through the counter + TFTP); an
agent cannot recover the box. **No human at the bench = no fire.** One person owns
the power and RESET button for the whole session.

### 2.2 Bench link PASS

```
tools/bench-link.sh          # must print PASS at the end
```

The box adapter is auto-detected (known box MAC 00:e0:4c:68:02:14 first); it must be
NetworkManager-unmanaged and carry both `192.168.1.88/24` (TFTP recovery server /
our image) and `192.168.10.44/24` (vendor LAN / HTTP source). Never restart
NetworkManager during a bench action; the internet path is the phone tether, not the
bench adapter (BENCH-FACTS.md host network map).

### 2.3 Recovery door READY (TFTP armed)

```
tools/recovery-card.sh --arm   # starts the TFTP server on UDP 69 (needs passwordless sudo)
tools/recovery-card.sh         # re-check: the last line must be
                               # "STATUS: recovery door is READY (host + image + server)."
```

The card checks the three physical things:

* bench link PASS (2.2);
* `tftp/recovery.bin` present: **22,024,351 B md5 bc40580943e8a9865bb8288d3294f687**
  (the official 2.5.24 recovery image - the one proven by the 2026-11-23 recovery;
  the older 2.4.15 image is stale and must not be used);
* TFTP server running on UDP 69, log `build/tftp-server.log`.

**The physical step (only the human can do it):** power the router OFF, hold RESET,
power ON still holding. The read-only console must print
`RESET button is pressed for: 0/1/2 second(s)` -> `Resetting...` -> `recovery button press`,
then the loader runs `TFTP from server 192.168.1.88` and requests `recovery.bin`.
A short hold leaves `0 second(s)` and the box boots normally. Do this dry-run
**before** the flash: if the door cannot be armed, no fire.

**Keep the TFTP server running** until the post-boot receipts pass. After a recovery
the stock LAN returns at `192.168.10.1`; the recovered overlay is empty (reinstall
the project key / start dropbear with `-R` - RUNBOOK.md section 6).

### 2.4 Read-only UART capture

```
tools/uart-capture.sh start --label image3-flash --timeout 7200
tools/uart-capture.sh check      # live capture; record the ABSOLUTE log path
```

The capture opens the tty **read-only**. **Never transmit on the UART**: no typed
keys, no `echo`/`printf`/`cat >` to the tty, no `screen`/`minicom`/`picocom`, no
read-write tty open, no U-Boot abort-key floods (host-to-router TX is dead - burnt
resistor). The UART log is the boot witness: a stock boot prints
`Uncompressing Linux` / `Data Size: 4,294,096` / the 5.10.201 banner; our image
prints the 6.18.60 banner. A capture without the image-identifying line is not
evidence.

### 2.5 Artifact freeze

The IMAGE #3 artifacts are final: **do not rebuild** between the preflight and the
flash. The container and its siblings live in
`/home/shibbityshwab/wifi-image-3-artifacts/` (local, gitignored) and the module
artifacts in `/home/shibbityshwab/wifi-image-3-module-artifacts/`. If the artifacts
do not match the pins in README.md: STOP (wrong build).

### 2.6 Stock root channel

The stock firmware has no SSH/dropbear/opkg; the transport is its **root telnet**
channel (bench-LAN only), provisioned by the MQTT broker path:

```
tools/box-provision.sh --bdinfo dumps/router-20261108/full/mtd6-bdinfo.bin \
    --stage base --host 192.168.10.1 --telnetd
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r; cat /proc/cmdline'
# expect: uid=0(root) ... 5.10.201 ... ubi.mtd=rootfsa
```

If the telnet channel is refused (stock overlay lost the provisioning payload), the
alternative documented channel is the crafted encrypted config backup
([../cudy-backup-root/REPORT.md](../cudy-backup-root/REPORT.md), root RUNBOOK.md
section 6). Both are equally privileged; use one, not both.

### 2.7 Evidence directory

```
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p build/image3-flash/$STAMP
# tee/record every section's transcript into it; /tmp is NOT evidence.
```

---

## 3. Host preflight (the fire gate)

### 3.1 Artifact-only pass (before touching the box)

```
cd opensource/docs/incoming/wifi-image-3-flash
./tools/preflight-image-3.sh | tee /tmp/image3-preflight-host.txt
```

Expected tail: `preflight-image-3: 23 passed, 0 failed, 0 warnings` and
`RESULT: PASS - host artifacts are the pinned IMAGE #3 build. NOT fire-ready yet ...`.
Exit code 0. This proves, from the built bytes: container size/sha256; 6 tar
members; CONTROL + fip-manifest; kernel member 13e81214... and the derived kernelb
slot 1794a531...; rootfs UBI 90ae340c... and UBI# magic; 34 PEBs; kernelb/mtd12 exact
fit; mtd14 free space 19,922,944 B; the sibling artifact set + SHA256SUMS; the two
module identities. **A FAIL is a stop** - do not fire a different build.

### 3.2 Manual equivalent (if you prefer to see the commands)

```
cd /home/shibbityshwab/wifi-image-3-artifacts
sha256sum -c SHA256SUMS
sha256sum image-3-sysupgrade.bin kernel-slice.bin rootfs.ubi root.squashfs \
          cfg80211.ko hostapd hi5622_wifi.installed.ko
#   container 4cd85b5d... / kernel 13e81214... / rootfs 90ae340c... / squashfs ecbba4f9...
#   cfg80211 bc4d9c73... / hostapd fef44f3b... / module a717c86d...
wc -c image-3-sysupgrade.bin kernel-slice.bin rootfs.ubi
#   7843840 / 3372638 / 4456448
{ cat kernel-slice.bin; head -c 5278114 /dev/zero | tr '\0' '\377'; } | sha256sum
#   the kernelb slot identity: 1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
# free space: 24379392 - 4456448 = 19922944
```

### 3.3 The strict fire gate (after section 5 produced the witness files)

```
./tools/preflight-image-3.sh --strict \
    --stock-witness /tmp/image3-stock-witness.txt \
    --proc-mtd /tmp/image3-proc-mtd.txt \
    --path rootfs-only
```

Expected tail: `RESULT: PASS - fire-ready for the ROOTFS-ONLY path` (or the
container-path equivalent with `--path container`). Exit code 0. It re-checks the
host artifacts **and** the stock pair-A witness: kernelb is the validated slot,
cmdline uses rootfsa, UBI is on mtd13, cell 0x10, env bootflag a, /proc/mtd sizes.
If Path A is chosen and the witness shows a different kernelb: the preflight fails
by design - use Path B (which rewrites and verifies kernelb) or recover the slot.

---

## 4. Switch the box to stock pair A and get a stock root shell

The stock telnet transport only exists on the **stock** boot; the installer/write
sequence must run from a consistent stock pair A (pair A is the rollback at all
times).

### 4.1 Leave our image (pair B) for stock A

From our running image (SSH at `192.168.1.1` - the bench host carries
`192.168.1.88/24`; note `tools/rsh.sh` defaults to the STOCK address, so
prefix `ROUTER_HOST=root@192.168.1.1`):

```
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh \
    'SEL=$(ls -d /sys/devices/platform/*selector* | head -1); echo $SEL; cat $SEL/current_pair; echo a > $SEL/next_pair; cat $SEL/next_pair'
# expected: .../boot-selector, current_pair b, next_pair a
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sync; reboot'
```

On the 2026-11-26 session that sysfs write was permission-denied and a **plain
reboot** came up on stock pair A (the loader's documented failover path). Both are
acceptable; confirm the result on the read-only UART (stock banner 5.10.201 /
`Uncompressing Linux` vs our 6.18.60). If the box comes back on our image, retry
the selector write once; if it still does not take, STOP (the pair state is not
what this runbook assumes).

### 4.2 Confirm the stock LAN

```
ping -c 2 192.168.10.1                     # stock LAN up
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r; cat /proc/cmdline'
# expected: uid=0(root) ... 5.10.201 ... ubi.mtd=rootfsa
```

If the stock root channel is missing, re-provision it (section 2.6) and re-check.
Do not write from a stock boot whose pair state was not verified in section 5.

---

## 5. Stock read-only gates + the on-box witness (the other half of the fire gate)

Run everything here from the stocks pair-A root shell (section 4.2), **read-only**.
Every line must match; any deviation is a STOP (section 11) - do not "fix" it by
writing from this boot.

### 5.1 The gates

| read | command (stock shell) | expected |
| --- | --- | --- |
| running kernel | `uname -r` | `5.10.201` (stock) |
| running root | `cat /proc/cmdline` | uses `ubi.mtd=rootfsa`, **not** rootfsb |
| attached UBI | `cat /sys/class/ubi/ubi0/mtd_num` | `13` (rootfsa) |
| selector cell | `cat /sys/devices/platform/sysenv/boot_reg` | `0x10` (pair A) |
| U-Boot env | `ln -sf /bin/fw_printenv /tmp/fw_getenv; /tmp/fw_getenv bootflag` | `bootflag=a` |
| kernelb (the slot Path A must not change) | `dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum` | `1794a531...54460e` (**required by Path A**) |
| kernela (rollback witness) | `dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum` | record it |
| partition table | `cat /proc/mtd` | mtd11/12 `00840000`, mtd13/14 `01740000`, erase `00020000` |

Why each one matters: if the stock boot itself is on rootfsb (or env bootflag=b, or
cell 0x11/0x21) the pair is mixed and `ubiformat /dev/mtd14` would format the
**active** rootfs - the classic mixed-pair failure (our kernel + stock rootfs, no
management IP). Fix the pair to a consistent A first (two-stage pair rule, root
RUNBOOK.md), reboot to stock, and restart this section.

### 5.2 Produce the two witness files (host side)

```
T() { python3 tools/telnet-exec.py --host 192.168.10.1 -- "$@" | tr -d '\r'; }
T 'id; uname -r'

W=/tmp/image3-stock-witness.txt
{
  printf 'cmdline %s\n'  "$(T 'cat /proc/cmdline' | grep -m1 'ubi.mtd=')"
  printf 'ubimtd %s\n'   "$(T 'cat /sys/class/ubi/ubi0/mtd_num' | grep -m1 -E '^[0-9]+$')"
  printf 'cell %s\n'     "$(T 'cat /sys/devices/platform/sysenv/boot_reg' | grep -m1 -E '0x[0-9a-fA-F]+')"
  printf 'bootflag %s\n' "$(T 'fw_printenv bootflag' | grep -m1 '^bootflag=')"
  printf 'kernelb %s\n'  "$(T 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum' | grep -m1 -E '^[0-9a-f]{64}')"
  printf 'kernela %s\n'  "$(T 'dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum' | grep -m1 -E '^[0-9a-f]{64}')"
} | tee $W

python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /proc/mtd' \
    | tr -d '\r' | grep -E '^mtd[0-9]+:' | tee /tmp/image3-proc-mtd.txt
```

The telnet channel echoes commands and prompts; the greps above isolate the value
lines. If your shell output cannot be parsed cleanly, type the six `key value`
lines by hand from the read-only outputs - the preflight only needs the pairs.
`cmdline` keeps the whole rest of the line; the other keys use their first
token (a raw `sha256sum` line with a trailing `  -` is fine, and
`bootflag=` is stripped).

### 5.3 Fire-gate preflight (host)

```
cd opensource/docs/incoming/wifi-image-3-flash
./tools/preflight-image-3.sh --strict \
    --stock-witness /tmp/image3-stock-witness.txt \
    --proc-mtd /tmp/image3-proc-mtd.txt --path rootfs-only \
    | tee /tmp/image3-fire-gate.txt
# expected tail: RESULT: PASS - fire-ready for the ROOTFS-ONLY path: ...
```

Save the output with the session evidence (section 12). From here on, the write is
the only remaining decision: section 6 (Path A) or section 7 (Path B).

---

## 6. Path A - the rootfs-only write (recommended; mtd14 only)

This is the strict minimal write: **one** `ubiformat` of mtd14, no kernel write,
no pair-A write. The sequence is the image-3 feasibility sequence
([../wifi-image-3/FLASH-FEASIBILITY.md](../wifi-image-3/FLASH-FEASIBILITY.md) section 3)
with the pair-A gates of section 5 in front of it.

### 6.1 Host: extract and serve the rootfs UBI

```
cd /home/shibbityshwab/wifi-image-3-artifacts
sha256sum -c SHA256SUMS
mkdir -p ~/stage-image3
cp rootfs.ubi ~/stage-image3/rootfs.ubi          # or:
#   tar -xOf image-3-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > ~/stage-image3/rootfs.ubi
sha256sum ~/stage-image3/rootfs.ubi
#   MUST be 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3 (4,456,448 B)

cd ~/stage-image3 && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/image3-http.log 2>&1 &
sleep 1
curl -s -o /dev/null -w '%{http_code}\n' http://192.168.10.44:8000/rootfs.ubi   # 200
```

The host address `192.168.10.44` is on the bench adapter (2.2); keep the HTTP
server up only until the stock shell has fetched the UBI, then stop it.

### 6.2 Stock shell: fetch, verify, write mtd14 (read every read-back)

Run these through the stock root telnet channel, **step by step**; record the
transcript. Do not continue if a line does not match.

```
# --- step 1: fetch + verify the UBI (no write) ---
cd /tmp
wget -q -O /tmp/i3-r.ubi http://192.168.10.44:8000/rootfs.ubi; echo WGET=$?
sha256sum /tmp/i3-r.ubi
#   MUST be 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3

# --- step 2: write ROOTFSB ONLY (mtd14; pair A / mtd13 untouched) ---
ubiformat /dev/mtd14 -y -f /tmp/i3-r.ubi ; echo UBI=$?
#   expect UBI=0 (a refusal usually means mtd14 is the active rootfs - STOP, section 5)

# --- step 3: the kernel slot must still be the validated slice ---
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum
#   STILL 1794a531... ; any change = STOP + recovery card

# --- step 4: record pair-A (rollback) ---
dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum

# --- step 5: handover env + cell for the SAME pair, no window between them ---
ln -sf /bin/fw_printenv /tmp/fw_setenv
/tmp/fw_setenv bootflag b
fw_printenv bootflag                 # bootflag=b
echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
cat /sys/devices/platform/sysenv/boot_reg    # 0x11
sync
reboot
```

Why the pair-B handover: our image's baked command line pins `ubi.mtd=rootfsb`,
so it may only be booted as pair B; the env bootflag selects the kernel slot and the
selector cell records the handover. Set both, read both back, then reboot - a
cell-only or env-only edit mixes the pairs.

### 6.3 What happens next (watch the read-only UART)

* the loader boots kernelb (**unchanged**, the validated slice) with the new rootfsb;
* our 6.18.60 banner and userspace lines appear; the box comes up at
  `192.168.1.1` (SSH, fresh host key; the bench host carries `192.168.1.88/24`);
* stock pair A is untouched and remains the rollback.

Wait up to ~240 s for ping/SSH. If the box stays silent: wait 2 minutes, then a
plain power-cycle (no RESET hold) retries the loader path; if it stays silent, the
recovery card is armed - physical RESET+TFTP (section 10.4).

Then run the **post-boot receipts** (section 8) before anything else.

### 6.4 STOP rules specific to Path A

* any section-5 gate or the fire-gate preflight fails: no write;
* UBI sha mismatch on the box: never `ubiformat` a bad file - re-fetch/serve;
* `ubiformat` rc != 0: STOP (pair A still boots);
* kernelb read-back changed: STOP, recovery card;
* handover read-back wrong: do **not** reboot into pair B; set pair A back
  (`bootflag a` + cell `0x10` together), read back, reboot to stock.

---

## 7. Path B - the full sysupgrade-container write (kernelb + rootfsb)

Use this path when the on-box kernelb identity cannot be confirmed, or as the
reconstruction/insurance path. The container's two members are written by the
project installer over the same stock root telnet transport; the tool verifies the
uploads, the whole kernel-slot read-back, the untouched active pair, the handover and
the booted identity. Source: [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md)
sections 6-7 (validated 2026-11-21/26), retargeted to IMAGE #3.

### 7.1 Host: stage + inspect (read-only on the box)

```
cd ~/Documents/GitHub/cudy-wr3000v2
mkdir -p ~/stage-image3
tools/stock-telnet-install.sh --inspect \
    --image /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin \
    --staging ~/stage-image3 \
    --http-base http://192.168.10.44:8000
```

The inspect splits the container into `~/stage-image3/kernel.bin` +
`rootfs.ubi` and prints the plan. It must contain all of:

* `image kernel 3372638 B sha 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f (slot sha 1794a531...)`
* `image root 4456448 B sha 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3`
* `stock selector cell 0x10 -> active pair a, write pair b (active untouched)`
* the active kernel slot sha (pair A - recorded) and `INSPECT ONLY - nothing written.`

The tool itself re-checks bench-link PASS and recovery-card READY before it would
write, refuses a stock boot whose cmdline uses rootfsb or whose env bootflag is b,
refuses a pair-a target and env/cell disagreement - the same hard gates as section 5.

### 7.2 Host: serve the split members

```
cd ~/stage-image3 && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/image3-http.log 2>&1 &
sleep 1
curl -s -o /dev/null -w 'kernel %{http_code}\n' http://192.168.10.44:8000/kernel.bin
curl -s -o /dev/null -w 'rootfs %{http_code}\n' http://192.168.10.44:8000/rootfs.ubi
```

### 7.3 The write + handover + boot verification

```
cd ~/Documents/GitHub/cudy-wr3000v2
tools/stock-telnet-install.sh --apply --attended --recovery-armed \
    --image /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin \
    --staging ~/stage-image3 \
    --http-base http://192.168.10.44:8000 \
    | tee /tmp/image3-installer.txt
```

Expected lines, in order (any deviation is a STOP - do not retry a failed write):

* `uploaded members verified` (both sha256 match the split members);
* `read-back matches the expected slot hash` (kernelb 8,650,752 B = `1794a531...`);
* `ubiformat OK`; `active kernel slot unchanged`;
* `env bootflag=b verified`; `selector verified: 0x11`;
* `ping OK after N s`, then `image: 6.18.60 CPUs: 2 (cmdline: ubi.mtd=rootfsb + project marker verified)`;
* final `PASS: wrote pair b, handover verified, our image booted.`

If the tool fails after the kernel write but before the handover: pair A is still
the active pair (the tool never touches it and sets the handover last). Recover to
stock A, inspect the box, and do not re-write in the same boot. If the kernel
read-back or `ubiformat` fails: STOP before the handover (pair A still boots).

### 7.4 Manual equivalent

If the installer is unavailable, the manual commands are the tool's steps:
kernelb `mtd erase kernelb` + `mtd write kernel.bin kernelb` + whole-slot
read-back hash; `ubiformat /dev/mtd14 -y -f rootfs.ubi`; active-pair re-read;
env+cell handover; reboot. The exact command block is
[../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md) section 12
with the IMAGE #3 hashes above (kernel member 13e81214..., rootfs 90ae340c...,
kernelb slot 1794a531...). Do not improvise a different order.


---

## 8. Post-boot receipts (the flash is only installed when these pass)

The box now boots IMAGE #3 (kernelb unchanged, the new rootfsb). Run the receipts
below **before** any Wi-Fi rung. R1-R5 are safe boot receipts (no device writes, no
firmware needed); R6-R8 are rung receipts and are only run when the operator
deliberately enables one bench-proven rung.

Host note: the flashed image answers SSH at **192.168.1.1**; `tools/rsh.sh`
defaults to the stock address, so prefix it:

```
R() { ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "$@"; }
R 'uname -r'
```

(SSH host keys are new after the rootfs write - `tools/rsh.sh` already disables
strict host-key checks. The project key is not in the fresh overlay; the image
enables root SSH with the OpenWrt blank-root-password patch. Re-provision the
project key after the receipts if wanted.)

### 8.1 R1 - kernel pin, kernelb slot, pair record

```
R 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num'
#   6.18.60 | "ubi.mtd=rootfsb ... luofu_switch ..." | 14

R 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
#   (the validated slice; unchanged by Path A, written+verified by Path B)

R 'apk --repositories-file /dev/null --no-network query --fields version kernel'
#   Version: 6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1

R 'SEL=$(ls -d /sys/devices/platform/*selector* 2>/dev/null | head -1); cat $SEL/current_pair; cat $SEL/raw'
#   b / 0x00000011   (the image re-records the running pair)
```

STOP if: kernelb is not `1794a531...`, the cmdline does not carry
`ubi.mtd=rootfsb` + the `luofu_switch` project marker, ubi0 is not mtd14, or the
kernel package pin is not `6f995278`. A wrong rootfs identity means the written
UBI was not IMAGE #3: roll back (section 9), do not enable gates.

### 8.2 R2 - the module hash/params and the userspace carries

```
R 'sha256sum /lib/modules/6.18.60/hi5622_wifi.ko /lib/modules/6.18.60/cfg80211.ko /usr/sbin/hostapd'
#   a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed  hi5622_wifi.ko
#   bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91  cfg80211.ko
#   fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6  hostapd

R 'wc -c < /lib/modules/6.18.60/hi5622_wifi.ko'
#   358544
R 'modinfo /lib/modules/6.18.60/hi5622_wifi.ko | grep -E "^(version|vermagic|depends|license):"'
#   vermagic: 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends: cfg80211 ; license: GPL
R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | wc -l'
#   64
R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | grep -cE "^(band5g|vap5g|ap_chan_wait_ms|desc_table|dfs_mode|he_caps|ba_enable):"'
#   7   (the IMAGE #3 feature params exist on the merged module)

R 'hostapd -v 2>&1 | head -1'
#   hostapd v2.12   (the CONFIG_DRIVER_11AX_SUPPORT build; identity pinned above)
```

STOP if any hash differs: the rootfs written is not the verified IMAGE #3 rootfs.

### 8.3 R3 - the image policy is fail-closed (nothing loads or transmits at boot)

```
R 'uci show hi5622 | grep -E "band5g|vap5g|ap_chan_wait_ms|desc_table|dfs_mode|he_caps|ba_enable"'
#   hi5622.main.band5g=0 ; vap5g=0 ; ap_chan_wait_ms=300 ; desc_table=0 ; dfs_mode=0 ;
#   he_caps=0 ; ba_enable=0        (all new gates ship 0 = inert)

R 'uci get hi5622.main.autoload; uci get hi5622.ap.enabled'
#   0
#   0   (bench policy: load by hand; no AP at boot)

R 'grep -c "^hi5622_wifi " /proc/modules || true'
#   0   (the driver is NOT loaded at boot)

R 'ls /etc/hi5622/'
#   FIRMWARE-POLICY.txt IMAGE-3-NOTES.txt hostapd-open.conf hostapd-open.proven.conf
#   hostapd-open-ax.conf hostapd-open-5g40.conf hostapd-wpa2.conf hostapd-wpa2-hardened.conf
#   hostapd-wpa2-wpa3.conf hostapd-wpa3-sae.conf
```

STOP if any added gate is non-zero at boot, if autoload/ap.enabled is non-zero, or if
the module is loaded: the image is not the shipped fail-closed policy.

### 8.4 R4 - the regdomain is US and the 5 GHz band is IR (the cfg80211 fix)

```
R 'modprobe cfg80211; dmesg | tail -3'
#   no "regulatory.db ... failed" / pkcs7 error line (the REQUIRE_SIGNED_REGDB fix)
R 'iw reg set US; iw reg get'
#   country US: DFS-FCC
R 'iw reg get | grep "5170 - 5250"'
#   the 5 GHz IR range line, and it must NOT carry NO-IR / PASSIVE-SCAN
```

This loads only `cfg80211` (no device writes, no boot ladder). If you prefer not to
load it before the rung, the same "country US + 5 GHz IR" receipt appears after
`hi5622-load` with `hi5622.ap.country=US`; but the regdomain fix is only
proven by this read.

### 8.5 R5 - the vendor firmware re-push (policy; required before any load)

The rootfs write wiped the overlay, so the by-hand firmware/block are gone. They are
**never packaged and never committed**; push them attended:

```
# host: verify the local artifacts (never committed, vendor-derived)
sha256sum /tmp/hi5622-pd4/FIRMWARE.PD4.bin /tmp/blocks/wifi_block.bin
#   cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1  928,920 B (PD4)
#   0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32    2,472 B (block)

R 'mkdir -p /lib/firmware/hi_wifi'
tools/rscp.sh /tmp/hi5622-pd4/FIRMWARE.PD4.bin root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
tools/rscp.sh /tmp/blocks/wifi_block.bin root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin
R 'sha256sum /lib/firmware/hi_wifi/*'
#   both hashes as above
```

If `/tmp` was cleaned, rebuild with `/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python`
`opensource/docs/incoming/wifi-irq-unmask/tools/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4`
and `opensource/docs/incoming/wifi-driver-clean/tools/mkblocks.py --wifi-capture <local 618-word capture> --outdir /tmp/blocks`
(see ../BENCH-NEXT-SESSION/README.md section 3.4). The loader fails closed (insmod
refused with a fatal message) if either file is missing.

### 8.6 R6 - the module load, C2 and the page golden receipt (rung receipt)

The image loads **nothing** at boot. One rung per boot; module parameters are
load-time only; **no rmmod** after a CPU release; never a boot ladder on
`0001:00:00.0` (second host view).

```
R '/usr/sbin/hi5622-load'
#   hi5622-load: kernel 6.18.60; module .../hi5622_wifi.ko; ...
#   hi5622-load: module parameters verified from /sys/module/hi5622_wifi/parameters (insmod applied them)
#   hi5622-load: wiphy: phy0
#   hi5622-load: C2 ladder OK (frames_sent=2, no timeout/refusal)
#   hi5622-load: done

R 'D=/sys/bus/pci/devices/0000:00:00.0; cat $D/boot; cat $D/status'
#   handshake page CA 0x... = 0x86807fef / 0x00000007 / 0x00000002 / 0x0000abcd (+8==2:1 golden:1)
#   gate CA 0x... (16-bit read) = 0xcece (image handler post-boot)
#   state CA 0x...=0x00000001 mask CA 0x...=0x00000000 boot_slot CA 0x...=0x...
#   g+0xb4 CA 0x...=0x00000000 g+0xb8 CA 0x...=0x00000000
#   mailbox: out0=... ... d2h_doorbell=...
#   transport: windows=1 fw_loaded=1 fw_verified=1 fw_size=928920 ... cpu_released=1 ...
```

PASS = `golden:1`, `state ...=0x00000001`, `mask ...=0x00000000`,
`transport ... fw_verified=1 ... cpu_released=1`, C2 `ok=2 timeout=0 refused=0`, and
no firmware-exception line in `dmesg`. Any `golden:0` / non-zero mask / exception /
H2D timeout: **power-cycle, capture, no retry in place** (section 10).

### 8.7 R7 - the HE AP (2.4 GHz 802.11ax; bench-proven class)

Boot-time artifact proof (safe, before any rung): `hostapd -v` = v2.12 and
`grep -c '^ieee80211ax=1' /etc/hi5622/hostapd-open-ax.conf` = 1. Rung proof, on a fresh
boot with `he_caps=1` + the ax fixture (a full recipe with every gate:
[../wifi-ax-he-caps/FIRE-RECIPE.md](../wifi-ax-he-caps/FIRE-RECIPE.md) G1-G7):

```
R 'uci set hi5622.main.he_caps=1; uci set hi5622.ap.enabled=1; uci set hi5622.ap.country=US; uci set hi5622.ap.config=/etc/hi5622/hostapd-open-ax.conf; uci commit hi5622'
R '/usr/sbin/hi5622-load'
#   dmesg: "cfg80211: HE (802.11ax) caps advertised on 2.4GHz (AP iftype): ... MAC 01 00 ... PHY0 0x02 ..."
#   hostapd reaches "wlan0: AP-ENABLED" and prints NO "Driver does not support HE"

R 'iw phy phy0 info | sed -n "/HE Iftypes/,+14p"'
#   an "HE Iftypes: AP" block for 2.4 GHz: HE MAC cap byte0 0x01, PHY cap byte0 0x02,
#   MCS/NSS rx80/tx80 0xfffa/0xfffa, no 6 GHz HE line
```

On-air proof is external (observer lane): the beacon carries HE Capabilities
(ext 0x23) + HE Operation (ext 0x24) and the checker prints
`HE-IE-VERDICT|PASS`. Page/mask must still be golden after the AP start.

### 8.8 R8 - the 5 GHz gate receipt and the F2 RF programme (optional rung)

The gates ship fail-closed (R3). The F2 rung enables them on a fresh boot and proves
the RF chain; it does **not** produce a 5 GHz beacon yet (the TBTT arm is the open
piece, fw-tbtt-arm lane).

```
R 'uci set hi5622.main.band5g=1; uci set hi5622.main.vap5g=1; uci set hi5622.main.desc_table=1; uci set hi5622.main.dfs_mode=1; uci set hi5622.main.ap_chan_wait_ms=300; uci set hi5622.ap.enabled=1; uci set hi5622.ap.country=US; uci set hi5622.ap.ifname=wlan5; uci set hi5622.ap.config=/etc/hi5622/hostapd-open-5g40.conf; uci commit hi5622'
R '/usr/sbin/hi5622-load'
R 'cat /sys/module/hi5622_wifi/parameters/band5g /sys/module/hi5622_wifi/parameters/vap5g /sys/module/hi5622_wifi/parameters/desc_table /sys/module/hi5622_wifi/parameters/dfs_mode'
#   1 1 1 1
```

Then the F2 RF receipt exactly as the field-B recipe reads it
([../wifi-fw-dfs-worker/FIRE-RECIPE.md](../wifi-fw-dfs-worker/FIRE-RECIPE.md)):
mode byte `P+0xF04 = 3`, `R+0x3A8 = 0x01800060`,
`R+0x3AC = 0x03E6138A` (the ch36-64 RF image), band-1 context bound.
**Do not claim a 5 GHz beacon or on-air proof** from this rung: the beacon is gated on
the TBTT arm and remains open. BA stays fail-closed (`ba_enable=0`, no peer).

---

## 9. Rollback - the pair switch first

### 9.1 Pair switch back to stock A (primary rollback)

Pair A is never written by the rootfs-only path, so the stock image is always the
rollback. From our image (SSH at 192.168.1.1):

```
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh \
    'SEL=$(ls -d /sys/devices/platform/*selector* | head -1); echo a > $SEL/next_pair; cat $SEL/next_pair'
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sync; reboot'
```

A plain reboot is the documented failover fallback (observed 2026-11-26). Confirm on
the read-only UART (stock 5.10.201 banner / `Uncompressing Linux`); the stock LAN
returns at 192.168.10.1. If the box is on stock and must stay there, make the pair
consistent: set env `bootflag=a` and selector cell `0x10` **together**, read
both back, reboot.

### 9.2 Restore a previous rootfs to mtd14

Only from a consistent stock pair A (section 5 gates), same write shape:

| target rootfs | size | sha256 | source |
| --- | --- | --- | --- |
| verified **image #2** rootfs | 3,801,088 B | `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` | image-2 container member `root` |
| **wifi-image-update** rootfs (IMAGE #3 base) | 4,456,448 B | `d3c665f16e76811d741bcaa7a670a6467e6a8c3ad0d49f61363a0157e2832d57` | update lane artifact |
| **IMAGE #3** rootfs (re-write the same image) | 4,456,448 B | `90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3` | IMAGE #3 container member `root` |

```
# host: extract the rollback member from the image-2 container, e.g.
tar -xOf final-image-2-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > /tmp/rollback-rootfsb.ubi
sha256sum /tmp/rollback-rootfsb.ubi     # expect the table value
# serve it as rootfs.ubi (section 6.1) and on the stock shell:
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # 1794a531... still
ubiformat /dev/mtd14 -y -f /tmp/rollback-rootfsb.ubi ; echo UBI=$?
# hand back to b: env bootflag b + cell 0x11 together, read back, sync, reboot
```

### 9.3 Never mix, and never roll back for a rung failure

* If the rootfs write failed, do **not** hand the selector to B: keep/hand pair A
  (`bootflag a` + cell `0x10`). A half-written pair B is not bootable; pair A is.
* A booted IMAGE #3 whose AX/5 GHz rung fails is **not** a rollback trigger: the image
  is fail-closed; power-cycle and re-read the rung recipe. Roll back only for
  install-level failures (no boot, wrong kernel/rootfs identity).

### 9.4 Last resort - physical RESET+TFTP recovery

Recovery card (armed, section 2.3): power off, hold RESET through the counter, TFTP
`recovery.bin` from 192.168.1.88. This costs a stock restore on the affected pair
only. Afterwards the stock LAN is 192.168.10.1, the overlay is empty: reinstall the
project key, start dropbear with `-R` after deleting zero-byte host keys, verify
2 wiphys + calibration `[SUCC]` on both bands (root RUNBOOK.md section 6).

---

## 10. STOP conditions (fail-closed; do not retry in place)

**Before the write**

1. Any `tools/preflight-image-3.sh` FAIL, or the strict fire gate does not say
   fire-ready: no write.
2. Recovery card not READY / TFTP server down / no human at the bench / no read-only
   UART capture: no write.
3. The IMAGE #3 artifacts do not match the pinned hashes: wrong build.
4. The stock boot is not a consistent pair A (cmdline uses rootfsb, UBI on mtd14,
   env `bootflag=b`, cell 0x11/0x21): fix the pair first, reboot, restart.
5. Path A chosen while kernelb is not `1794a531...`: the rootfs-only write would
   pair a different kernel with IMAGE #3 - use Path B or recover the slot.

**The write**

6. Uploaded rootfs sha mismatch on the box: never `ubiformat` a bad file.
7. `ubiformat` returns non-zero (usually mtd14 is the active rootfs): STOP; pair A
   still boots.
8. kernelb changes during a rootfs-only write: STOP, recovery card.
9. The active pair changes: STOP - that must never happen.
10. The handover read-back is not `bootflag=b` + cell `0x11`: do not reboot into
    pair B; restore pair A (`bootflag a` + cell `0x10`), reboot to stock.

**The boot**

11. No ping/SSH answer within ~240 s: wait 2 minutes, plain power-cycle (no RESET);
    still silent -> physical recovery (the card is armed).
12. The booted kernel is not 6.18.60 (stock fallback / unexpected): inspect the
    selector/env on the read-only UART; do not re-fire blindly.
13. Post-boot receipt mismatch (kernelb, cmdline marker, module hash/params, kernel
    apk pin): the rootfs identity is in question - roll back (section 9).
14. Page not golden, mask non-zero, nonzero staging, firmware exception, C2 not
    `ok=2 timeout=0 refused=0`, or an H2D timeout/stall: **power-cycle, capture,
    no retry in place**.
15. The firmware/block hashes after R5 differ: push again; the loader fails closed
    without them - never bypass the check.

**Hard bench rules (all sessions)**

16. Never transmit on the UART (read-only capture only).
17. Never `rmmod` after a CPU release; module parameters are load-time only; one
    load per boot.
18. Never run a boot ladder on `0001:00:00.0` (second host view).
19. Never write CA `0x400392f0`; never read the RC misc window `0x10161000`.
20. No blind flash; the NAND is degraded (ECC/JFFS2 errors recorded) - one write
    cycle per session, never cycle the box for an undocumented experiment.

---

## 11. Evidence to keep + teardown

Under `build/image3-flash/$STAMP/` (gitignored; /tmp is **not** evidence):

* `sha256sum -c SHA256SUMS` output and the artifact hashes;
* the host preflight output and the strict fire-gate output;
* the stock witness files (`image3-stock-witness.txt`, `image3-proc-mtd.txt`)
  and the stock read-only gates transcript;
* the write transcript (Path A `ubiformat` + read-backs, or the Path B installer
  output with `--inspect` and `--apply`);
* the UART log path + the boot-identifying line (6.18.60 banner);
* the handover read-backs (env + cell) and the pre/post kernel slot hashes;
* the post-boot receipts R1-R8 (raw outputs);
* the recovery-card check before and after.

Teardown: stop the staging HTTP server (`pkill -f 'http.server 8000'`); stop the
UART capture (`tools/uart-capture.sh stop`); stop the TFTP server
(`tools/recovery-card.sh --stop`) **only after** the box is stable and the receipts
pass; run `tools/bench-teardown.sh --apply`. If the Lead owns the repo record: append the ledger
entry, regenerate STATUS, run `tools/check-repo.sh`, and commit - the product
rule is **commit only inside opensource/**.

---

## 12. What this runbook does NOT do

* It is not the vendor stock `sysupgrade` path and does not make the container a
  stock-flashable image.
* It does not prove the exact 64-param module combination (its three parents are
  bench-proven; the combination ships fail-closed). The AX/5 GHz rungs above are the
  first test of the combination when the Lead enables them.
* It does not fix the open RX-association gate (the RX H2D trace decode) and does not
  produce a 5 GHz beacon (the TBTT arm is still open).
* It is not automated: every write is attended and recovery-armed.
* It is a bench research image, not a release.

---

## Appendix A - container members and the manual split

```
cd /home/shibbityshwab/wifi-image-3-artifacts
tar -tvf image-3-sysupgrade.bin
#   sysupgrade-cudy_wr3000-v2/ (dir)
#   sysupgrade-cudy_wr3000-v2/CONTROL              63 B
#   sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt 2212 B
#   sysupgrade-cudy_wr3000-v2/fip-manifest.txt    945 B
#   sysupgrade-cudy_wr3000-v2/kernel          3372638 B
#   sysupgrade-cudy_wr3000-v2/root            4456448 B
tar -xOf image-3-sysupgrade.bin sysupgrade-cudy_wr3000-v2/kernel > kernel-slice.bin
tar -xOf image-3-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root   > rootfs.ubi
sha256sum kernel-slice.bin rootfs.ubi
#   13e81214... / 90ae340c...
```

## Appendix B - identities needed for rollback and comparisons

| item | identity |
| --- | --- |
| IMAGE #3 container | 7,843,840 B `4cd85b5d...` |
| image #2 container | 7,188,799 B `8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8` |
| image #2 rootfs UBI | 3,801,088 B `31e7a39c...` |
| wifi-image-update rootfs UBI | 4,456,448 B `d3c665f1...` |
| kernel slice (all three) | 3,372,638 B `13e81214...` |
| kernelb slot | 8,650,752 B `1794a531...` |
| image #2 installed module | 328,788 B `b4c076bf...` |
| image #2 hostapd (SAE) | 1,032,951 B `451dd49f...` |
| IMAGE #3 installed module | 358,544 B `a717c86d...` |
| IMAGE #3 hostapd (ax) | 1,618,388 B `fef44f3b...` |
| recovery image | 22,024,351 B md5 `bc40580943e8a9865bb8288d3294f687` |

## Appendix C - the preflight

The committed host-artifact output (23 PASS / 0 FAIL) is
[build-evidence/preflight-run.txt](build-evidence/preflight-run.txt); the artifact-free
tool tests are [build-evidence/selftest.txt](build-evidence/selftest.txt). The check
table, witness formats and exit codes are in [PREFLIGHT.md](PREFLIGHT.md).

