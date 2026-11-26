# IMAGE #3 - exact flash command sequence (Lead, attended)

**Lane:** image-flash-verify (task-218), 2026-11-26.  Derived from
[../wifi-image-3-flash/RUNBOOK.md](../wifi-image-3-flash/RUNBOOK.md) and verified
against the real artifacts ([GO-NO-GO.md](GO-NO-GO.md) sections 2-3).
**The container is NOT a vendor-`sysupgrade` image and must never be fed to the
stock sysupgrade / LuCI web UI** (its own `INSTALL-STATUS.txt` says
NOT-FLASHABLE-UNTIL-INSTALLED; its `platform_do_upgrade` has never run, its baked
cmdline is pair-B only).  "Full path" below means the project installer writing
both container members; "rootfs-only" means one `ubiformat` of mtd14.

All commands run **from the repo root** unless a `cd` is shown.
`STAMP=$(date -u +%Y%m%dT%H%M%SZ); mkdir -p build/image3-flash/$STAMP` first and
tee every section into it (`/tmp` is not evidence).

---

## 0. Choose the path

| on-box `kernelb`/mtd12 (kernelb slot identity = `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e`) | write |
| --- | --- |
| **already `1794a531...`** (expected) | **Path A - rootfs-only** (one `ubiformat /dev/mtd14`; kernelb untouched, verified by the strict gate) |
| anything else, or the Lead prefers the reconstruction path | **Path B - full container** (the installer writes kernelb with the same validated slice + rootfsb, and verifies both read-backs) |

The strict gate enforces the decision: `--path rootfs-only` **fails** unless the
live kernelb is the validated slot; `--path container` accepts a different
kernelb (it rewrites it) and still enforces the stock pair-A state.

---

## 1. Preconditions (all must hold before any write)

```sh
tools/bench-link.sh              # last line must be PASS
tools/recovery-card.sh --arm     # starts the TFTP server (UDP 69; needs sudo)
tools/recovery-card.sh           # last line: STATUS: recovery door is READY
tools/uart-capture.sh start --label image3-flash --timeout 7200
tools/uart-capture.sh check      # record the ABSOLUTE log path
```

* A human owns the power and RESET button for the whole session.
* No UART transmit ever (read-only capture only).
* **C4 - rescue the vendor firmware before the overlay is wiped.**  The current
  box carries the by-hand artifacts in its overlay; the flash replaces it:
```sh
mkdir -p /tmp/rescue-hi_wifi
tools/rscp.sh root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin /tmp/rescue-hi_wifi/
tools/rscp.sh root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin  /tmp/rescue-hi_wifi/
sha256sum /tmp/rescue-hi_wifi/*
#   expect cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1  FIRMWARE.PD4.bin (928,920 B)
#          0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32  wifi_block.bin   (2,472 B)
# If wifi_block.bin is absent on the box too: rebuild PD4 with
#   .provision-venv/bin/python opensource/docs/incoming/wifi-irq-unmask/tools/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4
# and the block from the local 618-word capture with
#   python3 opensource/docs/incoming/wifi-driver-clean/tools/mkblocks.py --wifi-capture <capture> --outdir /tmp/blocks
# (no capture on the host today - the box copy is the reliable source).
```
* **Artifact freeze.**  Re-run the host preflight immediately before the fire:
```sh
cd opensource/docs/incoming/wifi-image-3-flash && ./tools/preflight-image-3.sh
#   expect: preflight-image-3: 23 passed, 0 failed, 0 warnings
#           RESULT: PASS - host artifacts are the pinned IMAGE #3 build ...
cd -
```
  A FAIL is a stop.  Do not rebuild between this and the write.

---

## 2. Switch the box to stock pair A

```sh
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh \
  'SEL=$(ls -d /sys/devices/platform/*selector* | head -1); echo $SEL; cat $SEL/current_pair; echo a > $SEL/next_pair; cat $SEL/next_pair'
# expected: current_pair b, next_pair a.  If the write is permission-denied
# (observed 2026-11-26), a plain reboot is the documented fallback and comes up
# on stock pair A - do the plain reboot either way next:
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sync; reboot'
```

Confirm on the **read-only UART** that stock 5.10.201 boots
(`Uncompressing Linux`, the 5.10.201 banner).  If it comes back on our 6.18.60
image: retry the selector write once; if it still does not take, **STOP**.
Then:
```sh
ping -c 2 192.168.10.1
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r; cat /proc/cmdline'
#   expect: uid=0(root) ... 5.10.201 ... ubi.mtd=rootfsa
```
If the telnet root channel is missing, re-provision it
([../wifi-image-3-flash/RUNBOOK.md](../wifi-image-3-flash/RUNBOOK.md) section 2.6)
and re-check.  Do not write from a stock boot whose pair state was not verified.

---

## 3. Stock read-only gates + live witness + strict fire gate

Run on the stock pair-A root shell, read-only.  Every expected value must match;
any deviation is a STOP.

```sh
T() { python3 tools/telnet-exec.py --host 192.168.10.1 -- "$@" | tr -d '\r'; }
T 'uname -r'                                     # 5.10.201
T 'cat /proc/cmdline'                            # uses ubi.mtd=rootfsa (NOT rootfsb)
T 'cat /sys/class/ubi/ubi0/mtd_num'              # 13
T 'cat /sys/devices/platform/sysenv/boot_reg'    # 0x10
T 'fw_printenv bootflag'                         # bootflag=a
T 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'   # 1794a531...54460e  (required by Path A)
T 'dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum'   # record it (rollback witness)
T 'cat /proc/mtd'                                # mtd11/12 00840000, mtd13/14 01740000, erase 00020000
```

Produce the two witness files (host side):
```sh
W=/tmp/image3-stock-witness.txt
{
  printf 'cmdline %s\n'  "$(T 'cat /proc/cmdline' | grep -m1 'ubi.mtd=')"
  printf 'ubimtd %s\n'   "$(T 'cat /sys/class/ubi/ubi0/mtd_num' | grep -m1 -E '^[0-9]+$')"
  printf 'cell %s\n'     "$(T 'cat /sys/devices/platform/sysenv/boot_reg' | grep -m1 -E '0x[0-9a-fA-F]+')"
  printf 'bootflag %s\n' "$(T 'fw_printenv bootflag' | grep -m1 '^bootflag=')"
  printf 'kernelb %s\n'  "$(T 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum' | grep -m1 -E '^[0-9a-f]{64}')"
  printf 'kernela %s\n'  "$(T 'dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum' | grep -m1 -E '^[0-9a-f]{64}')"
} | tee $W
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /proc/mtd' | tr -d '\r' | grep -E '^mtd[0-9]+:' | tee /tmp/image3-proc-mtd.txt
```

The strict fire gate (host):
```sh
cd opensource/docs/incoming/wifi-image-3-flash
./tools/preflight-image-3.sh --strict \
    --stock-witness /tmp/image3-stock-witness.txt \
    --proc-mtd /tmp/image3-proc-mtd.txt --path rootfs-only | tee /tmp/image3-fire-gate.txt
#   expect (exit 0): preflight-image-3: 30 passed, 0 failed, 0 warnings
#                    RESULT: PASS - fire-ready for the ROOTFS-ONLY path ...
# If it FAILS on "kernelb is not the validated slot": do NOT use Path A -
# rerun with --path container and use Path B (section 5).
cd -
```
Keep the TFTP/recovery door armed until the post-boot acceptance passes.

---

## 4. Path A - rootfs-only (mtd14 only; recommended)

### 4.1 Host: extract and serve the UBI
```sh
cd /home/shibbityshwab/wifi-image-3-artifacts
sha256sum -c SHA256SUMS
mkdir -p ~/stage-image3
cp rootfs.ubi ~/stage-image3/rootfs.ubi
sha256sum ~/stage-image3/rootfs.ubi
#   MUST be 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3 (4,456,448 B)
cd ~/stage-image3 && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/image3-http.log 2>&1 &
sleep 1
curl -s -o /dev/null -w '%{http_code}\n' http://192.168.10.44:8000/rootfs.ubi   # 200
```

### 4.2 Stock shell: fetch, verify, write mtd14, read every read-back
```sh
T 'cd /tmp'
T 'wget -q -O /tmp/i3-r.ubi http://192.168.10.44:8000/rootfs.ubi; echo WGET=$?'   # WGET=0
T 'sha256sum /tmp/i3-r.ubi'
#   MUST be 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3
# --- write ROOTFSB ONLY (mtd14; pair A / mtd13 untouched) ---
T 'ubiformat /dev/mtd14 -y -f /tmp/i3-r.ubi; echo UBI=$?'                          # UBI=0 (refusal => mtd14 is active => STOP)
# --- kernel slot must STILL be the validated slice ---
T 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e ; any change = STOP + recovery card
# --- record pair-A rollback witness ---
T 'dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum'
# --- handover env + cell for the SAME pair, no window between them ---
T 'ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b; fw_printenv bootflag'   # bootflag=b
T 'echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg; cat /sys/devices/platform/sysenv/boot_reg'   # 0x11
T 'sync; reboot'
```
STOP: UBI sha mismatch on the box; `ubiformat` rc != 0; kernelb changed; read-back
not `bootflag=b` + `0x11`.  On a handover read-back failure do **not** reboot
into pair B: set pair A back (`bootflag a` + cell `0x10`) together, read both,
reboot to stock.

### 4.3 Watch the read-only UART
Loader boots **unchanged kernelb** with the new rootfsb; the 6.18.60 banner
appears and the box comes up at 192.168.1.1.  Wait up to ~240 s.  Silent: wait
2 min, plain power-cycle (no RESET); still silent: the armed physical
RESET+TFTP recovery ([ROLLBACK.md](ROLLBACK.md) section 3).  Then run
[ACCEPTANCE.md](ACCEPTANCE.md).

---

## 5. Path B - full container (kernelb + rootfsb, project installer)

Prerequisite: the strict gate with `--path container` PASSes (section 3; it
issues a WARN, not a FAIL, when kernelb differs because this path rewrites and
verifies it), stock pair A is active, recovery READY.

### 5.1 Host: stage + inspect (read-only on the box)
```sh
mkdir -p ~/stage-image3
tools/stock-telnet-install.sh --inspect \
    --image /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin \
    --staging ~/stage-image3 \
    --http-base http://192.168.10.44:8000 | tee /tmp/image3-installer-inspect.txt
```
The output must contain:
* `image kernel 3372638 B sha 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f (slot sha 1794a531...)`
* `image root   4456448 B sha 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3`
* `stock selector cell 0x10 -> active pair a, write pair b (active untouched)`
* the active kernel slot sha (pair A - record it) and `INSPECT ONLY - nothing written.`

### 5.2 Host: serve the split members
```sh
cd ~/stage-image3 && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/image3-http.log 2>&1 &
sleep 1
curl -s -o /dev/null -w 'kernel %{http_code}\n' http://192.168.10.44:8000/kernel.bin      # 200
curl -s -o /dev/null -w 'rootfs %{http_code}\n' http://192.168.10.44:8000/rootfs.ubi      # 200
cd -
```

### 5.3 The write + handover + boot verification
```sh
tools/stock-telnet-install.sh --apply --attended --recovery-armed \
    --image /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin \
    --staging ~/stage-image3 \
    --http-base http://192.168.10.44:8000 | tee /tmp/image3-installer.txt
```
Expected lines, in order (any deviation is a STOP; do not retry a failed write):
* `uploaded members verified`
* `read-back matches the expected slot hash` (kernelb = `1794a531...`, 8,650,752 B)
* `ubiformat OK` ; `active kernel slot unchanged`
* `env bootflag=b verified` ; `selector verified: 0x11`
* `ping OK after N s`, then `image: 6.18.60 CPUs: 2 (cmdline: ubi.mtd=rootfsb + project marker verified)`
* `PASS: wrote pair b, handover verified, our image booted.`

The installer itself re-checks bench-link PASS + recovery READY, refuses a stock
boot whose cmdline uses rootfsb / env `bootflag=b`, refuses a pair-A target and
env/cell disagreement, verifies both uploads and the whole kernel-slot read-back,
and only then sets the handover and reboots.  If it fails after the kernel write
but before the handover: pair A is still active - recover to stock A, inspect,
do not re-write in the same boot.

---

## 6. Immediately after the boot (both paths)

```sh
ping -c 2 192.168.1.1
R() { ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "$@"; }
R 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num'
#   6.18.60 | "ubi.mtd=rootfsb ... luofu_switch ..." | 14
R 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
```
The fresh overlay has no project key; the image enables root SSH with the
OpenWrt blank-root-password patch (`tools/rsh.sh` disables strict host-key
checks).  Re-provision the project key afterwards if wanted.  Then run
[ACCEPTANCE.md](ACCEPTANCE.md) R1-R4 before anything else, and R5 before any
Wi-Fi load.

---

## 7. Evidence + teardown

* `build/image3-flash/$STAMP/`: the artifact hashes, the live strict-gate
  output, the witness files, the write transcript (Path A read-backs or the
  Path B installer output), the UART log path + the 6.18.60 banner line, the
  handover read-backs, the pre/post kernel-slot hashes, the R1-R8 raw outputs,
  and the recovery-card state before/after.
* Stop the staging HTTP server (`pkill -f 'http.server 8000'`); stop the UART
  capture; stop the TFTP server (`tools/recovery-card.sh --stop`) **only after**
  the box is stable and the acceptance passes; then
  `tools/bench-teardown.sh --apply`.
* If the Lead owns the repo record: append the ledger entry, regenerate
  `STATUS.md`, run `tools/check-repo.sh`, and commit - **only inside
  `opensource/`** (the project rule).
