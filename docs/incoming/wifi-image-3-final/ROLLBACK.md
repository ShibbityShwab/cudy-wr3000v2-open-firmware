# IMAGE #3 - rollback

**Lane:** image-flash-verify (task-218), 2026-11-26.  Derived from
[../wifi-image-3-flash/RUNBOOK.md](../wifi-image-3-flash/RUNBOOK.md) section 9;
identities re-verified in [GO-NO-GO.md](GO-NO-GO.md) section 2.

**Recovery is physical.**  An agent cannot recover the box: a human owns the
power/RESET button and the armed TFTP door
(`recovery.bin` 22,024,351 B md5 `bc40580943e8a9865bb8288d3294f687`).
**Never mix pairs**: the kernel slot and the rootfs slot must always belong to
the same pair (the loader reads the env bootflag for the kernel slot and the
selector cell for the pair record).

---

## 1. Primary rollback - pair switch back to stock A

Pair A (stock 2.5.24) is **never written by Path A**; it stays bootable.  From
the booted IMAGE #3 (SSH at 192.168.1.1):

```sh
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh \
  'SEL=$(ls -d /sys/devices/platform/*selector* | head -1); echo $SEL; echo a > $SEL/next_pair; cat $SEL/next_pair'
# expect next_pair a
ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sync; reboot'
```

The sysfs write was permission-denied once (2026-11-26) and a **plain reboot**
came up on stock pair A (the loader's documented failover path); if the write
does not take, reboot plainly, once, and confirm on the read-only UART:
stock 5.10.201 banner / `Uncompressing Linux`, not our 6.18.60 banner.  The
stock LAN returns at **192.168.10.1**.  If the box comes back on our image
again, the pair state is not what the runbook assumes - STOP, inspect the
read-only selector/env reads from stock only.

If the box must **stay** on stock, make the pair consistent from the stock
root shell (read-only witness values first, then both writes **together**):

```sh
T() { python3 tools/telnet-exec.py --host 192.168.10.1 -- "$@"; }
T 'ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag a; fw_printenv bootflag'   # bootflag=a
T 'echo -n 0x10 > /sys/devices/platform/sysenv/boot_reg; cat /sys/devices/platform/sysenv/boot_reg'   # 0x10
T 'sync; reboot'
```

---

## 2. Restore a previous rootfs to mtd14 (only from a consistent stock pair A)

All three rollback rootfs images pair with the **same kernel slice**
`13e81214...` / kernelb slot `1794a531...`, so restoring one does not change
the kernel slot (re-read kernelb afterwards and require `1794a531...`):

| target rootfs | size | sha256 | source (local) |
| --- | --- | --- | --- |
| verified **image #2** rootfs | 3,801,088 B | `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` | `tar -xOf /home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root` |
| **wifi-image-update** rootfs (IMAGE #3 base) | 4,456,448 B | `d3c665f16e76811d741bcaa7a670a6467e6a8c3ad0d49f61363a0157e2832d57` | `/home/shibbityshwab/wifi-image-update-artifacts/rootfs.ubi` |
| **IMAGE #3** rootfs (rewrite the same image) | 4,456,448 B | `90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3` | the IMAGE #3 container member `root` |

From consistent stock pair A (section 3 of the flash sequence already verified
the pair state), host side:

```sh
# example: image #2 rootfs
tar -xOf /home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin \
    sysupgrade-cudy_wr3000-v2/root > /tmp/rollback-rootfsb.ubi
sha256sum /tmp/rollback-rootfsb.ubi     # expect the table value
mkdir -p ~/stage-rollback && cp /tmp/rollback-rootfsb.ubi ~/stage-rollback/rootfs.ubi
cd ~/stage-rollback && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/rollback-http.log 2>&1 &
```

Then on the stock shell (step by step, read every read-back):

```sh
T 'cd /tmp'
T 'wget -q -O /tmp/rb.ubi http://192.168.10.44:8000/rootfs.ubi; echo WGET=$?'   # 0
T 'sha256sum /tmp/rb.ubi'                       # MUST match the table sha256
T 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'   # still 1794a531... (STOP otherwise)
T 'ubiformat /dev/mtd14 -y -f /tmp/rb.ubi; echo UBI=$?'            # UBI=0
# hand back to pair b: env bootflag b + cell 0x11 together, read both, sync, reboot
T 'ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b; fw_printenv bootflag'   # b
T 'echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg; cat /sys/devices/platform/sysenv/boot_reg'   # 0x11
T 'sync; reboot'
```

If the rootfs write fails, **do not** hand the selector to B: keep/hand pair A
(`bootflag a` + cell `0x10` together, read back, reboot to stock).

---

## 3. Last resort - physical RESET + TFTP recovery

Costs a stock restore on the affected pair only; the overlay comes back empty.

```sh
tools/recovery-card.sh --arm     # TFTP server on UDP 69 (needs sudo)
tools/recovery-card.sh           # last line: STATUS: recovery door is READY
```

**The human does the physical step:** power the router OFF, hold RESET, power ON
still holding.  The read-only console must print
`RESET button is pressed for: 0/1/2 second(s)` -> `Resetting...` ->
`recovery button press`; the loader then runs `TFTP from server 192.168.1.88`
and requests `recovery.bin` (22,024,351 B md5 `bc405809...`).  A short hold
prints `0 second(s)` and boots normally - dry-run the hold **before** the
flash (RUNBOOK 2.3).

Afterwards: stock LAN at 192.168.10.1, empty overlay - reinstall the project key
/ start dropbear with `-R` after deleting zero-byte host keys, then verify
2 wiphys + calibration `[SUCC]` on both bands (the root `RUNBOOK.md` at the
repo root, section 6).  Stop the TFTP server only after
the box is stable (`tools/recovery-card.sh --stop`).

---

## 4. Rules

* **Never mix** the pairs: env bootflag and selector cell are written and read
  back **together**; a cell-only or env-only edit mixes them.
* **Do not roll back for a rung failure.**  The image is fail-closed; if an
  AX/5 GHz rung fails, power-cycle and re-read the rung recipe.  Roll back only
  for **install-level** failures (no boot, wrong kernel/rootfs identity, wrong
  module/package pin).
* **No blind flash**: the NAND is degraded (ECC/JFFS2 errors recorded); one
  write cycle per session.  Path A is a single `ubiformat`; Path B adds a
  redundant kernel erase/write of identical bytes.
* Keep the read-only UART capture running across the rollback and record its
  absolute log path with the boot-identifying banner line.
