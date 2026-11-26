# IMAGE #3 - post-flash acceptance checklist

**Lane:** image-flash-verify (task-218), 2026-11-26.  Derived from
[../wifi-image-3-flash/RUNBOOK.md](../wifi-image-3-flash/RUNBOOK.md) section 8;
every pinned value is re-verified in [GO-NO-GO.md](GO-NO-GO.md) section 2.

**The flash is "installed" when R1-R4 pass.**  R5 must pass before any Wi-Fi
load.  R6-R8 are **rung receipts**, run only when the operator deliberately
enables one bench-proven rung - **one rung per fresh boot, never a second load,
never `rmmod`**.  The image is fail-closed: `autoload=0`, no driver load, no AP
at boot.

The flashed image answers SSH at **192.168.1.1** (the bench host also carries
192.168.1.88/24).  `tools/rsh.sh` defaults to the stock address, so:

```sh
R() { ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "$@"; }
```

Host/transport note: the fresh overlay has no project key; the image enables
root SSH with the OpenWrt blank-root-password patch.  Re-provision the key after
the receipts if wanted.

---

## A0 - boot identity (before R1)

* Read-only UART capture shows the **6.18.60** banner (not stock 5.10.201 /
  `Uncompressing Linux`).  Record the absolute log path + the identifying line.
* `ping 192.168.1.1` answers; SSH works.
* The box booted from pair **b**: env `bootflag=b`, selector cell `0x11`
  (recorded by the handover; the image's `luofu_record_pair` re-records the
  running pair at boot).

---

## R1 - kernel pin, kernelb slot, pair record, overlay

```sh
R 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num'
#   6.18.60 | "ubi.mtd=rootfsb ... luofu_switch ..." | 14

R 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
#   (the validated slice; unchanged by Path A, written+verified by Path B)

R 'apk --repositories-file /dev/null --no-network query --fields version kernel'
#   Version: 6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1

R 'SEL=$(ls -d /sys/devices/platform/*selector* 2>/dev/null | head -1); cat $SEL/current_pair; cat $SEL/raw'
#   b / 0x00000011   (the image re-records the running pair)

R 'ubinfo /dev/ubi0 | sed -n "1,12p"; df -h /overlay'
#   volume 0 "squashfs" static, volume 1 "rootfs_data" dynamic autoresized
#   into the free PEBs of mtd14 (~19 MB; the preflight arithmetic is
#   24,379,392 - 4,456,448 = 19,922,944 B free in the slot); /overlay writable
```

**STOP if:** kernelb is not `1794a531...`, the cmdline does not carry
`ubi.mtd=rootfsb` + the `luofu_switch` marker, ubi0 is not mtd14, the kernel
package pin is not `6f995278`, or the overlay is not writable.  A wrong rootfs
identity means the written UBI was not IMAGE #3: roll back
([ROLLBACK.md](ROLLBACK.md)), do not enable gates.

---

## R2 - module hash/params + userspace carries

```sh
R 'sha256sum /lib/modules/6.18.60/hi5622_wifi.ko /lib/modules/6.18.60/cfg80211.ko /usr/sbin/hostapd'
#   a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed  hi5622_wifi.ko
#   bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91  cfg80211.ko
#   fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6  hostapd

R 'wc -c < /lib/modules/6.18.60/hi5622_wifi.ko'                       # 358544
R 'modinfo /lib/modules/6.18.60/hi5622_wifi.ko | grep -E "^(version|vermagic|depends|license):"'
#   vermagic: 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends: cfg80211 ; license: GPL
R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | wc -l'            # 64
R 'modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | grep -cE "^(band5g|vap5g|ap_chan_wait_ms|desc_table|dfs_mode|he_caps|ba_enable):"'   # 7
R 'hostapd -v 2>&1 | head -1'                                         # hostapd v2.12
```

**STOP if any hash differs**: the rootfs written is not the verified IMAGE #3
rootfs.  (The ongoing research fires load newer **per-fire** modules from
`/tmp`; those do not change these shipped /lib/modules hashes - the image #3
merged 64-param module is the image baseline, decision stands.)

---

## R3 - the image policy is fail-closed

```sh
R 'uci show hi5622 | grep -E "band5g|vap5g|ap_chan_wait_ms|desc_table|dfs_mode|he_caps|ba_enable"'
#   hi5622.main.band5g=0 ; vap5g=0 ; ap_chan_wait_ms=300 ; desc_table=0 ;
#   dfs_mode=0 ; he_caps=0 ; ba_enable=0        (all new gates ship 0 = inert)
R 'uci get hi5622.main.autoload; uci get hi5622.ap.enabled'           # 0 then 0
R 'grep -c "^hi5622_wifi " /proc/modules || true'                     # 0
R 'ls /etc/hi5622/'
#   FIRMWARE-POLICY.txt IMAGE-3-NOTES.txt hostapd-open.conf hostapd-open.proven.conf
#   hostapd-open-ax.conf hostapd-open-5g40.conf hostapd-wpa2.conf hostapd-wpa2-hardened.conf
#   hostapd-wpa2-wpa3.conf hostapd-wpa3-sae.conf
```

**STOP if** any added gate is non-zero at boot, autoload/ap.enabled is non-zero,
or the module is loaded: the image is not the shipped fail-closed policy.

---

## R4 - regdomain US / 5 GHz usable (the cfg80211 fix)

```sh
R 'insmod /lib/modules/6.18.60/cfg80211.ko 2>/dev/null; dmesg | tail -3'
#   no "regulatory.db ... failed" / pkcs7 error line (REQUIRE_SIGNED_REGDB fix;
#   an already-loaded EEXIST is harmless - hi5622-load also insmods it)
R 'iw reg set US; iw reg get'
#   country US: DFS-FCC
R 'iw reg get | grep "5170 - 5250"'
#   the 5 GHz range line; record its flags - the AP rung uses ch40 (5200 MHz
#   in the US regdomain) and must not be blocked by NO-IR/PASSIVE-SCAN
```

The decisive receipt is the bench-proven 5 GHz AP start on ch40 under
`country US` (the F2 fire reached AP-ENABLED; see R8).

---

## R5 - the vendor firmware re-push (required before any load)

The rootfs write wiped the overlay, so the by-hand firmware/block are gone;
they are never packaged and never committed.  Push them attended:

```sh
# host: verify the rescued/regenerated local artifacts
sha256sum /tmp/rescue-hi_wifi/FIRMWARE.PD4.bin /tmp/rescue-hi_wifi/wifi_block.bin
#   cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1  928920 B (PD4)
#   0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32    2472 B (block)

R 'mkdir -p /lib/firmware/hi_wifi'
tools/rscp.sh /tmp/rescue-hi_wifi/FIRMWARE.PD4.bin root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
tools/rscp.sh /tmp/rescue-hi_wifi/wifi_block.bin   root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin
R 'sha256sum /lib/firmware/hi_wifi/*'
#   both hashes as above
```

If neither the box rescue nor the local capture is available: rebuild PD4 with
`.provision-venv/bin/python opensource/docs/incoming/wifi-irq-unmask/tools/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4`
(verified byte-identical) and the block with
`python3 opensource/docs/incoming/wifi-driver-clean/tools/mkblocks.py --wifi-capture <618-word capture> --outdir /tmp/blocks`.
The loader fails closed (insmod refused with a fatal message) if either file is
missing - **never bypass that check**.

---

## R6 - module load, C2 and the page-golden receipt (rung receipt)

One rung per fresh boot; module parameters are load-time only; **no rmmod** after
a CPU release; never a boot ladder on `0001:00:00.0` (second host view).

```sh
R '/usr/sbin/hi5622-load'
#   hi5622-load: kernel 6.18.60; module .../hi5622_wifi.ko; ...
#   hi5622-load: module parameters verified from /sys/module/hi5622_wifi/parameters
#   hi5622-load: wiphy: phy0
#   hi5622-load: C2 ladder OK (frames_sent=2, no timeout/refusal)
#   hi5622-load: done

R 'D=/sys/bus/pci/devices/0000:00:00.0; cat $D/boot; cat $D/status'
#   page 0x86807FEF / state 0x00000001 / mask 0x00000000 (golden:1)
#   transport ... fw_loaded=1 fw_verified=1 fw_size=928920 ... cpu_released=1 ...
```

PASS = `golden:1`, state `0x00000001`, mask `0x00000000`, transport
`fw_verified=1 ... cpu_released=1`, C2 `ok=2 timeout=0 refused=0`, and no
firmware-exception line in `dmesg`.  **Any `golden:0` / non-zero mask /
exception / H2D timeout: power-cycle, capture, no retry in place.**  (Page
golden is checked **after** the boot/release; the pre-boot page is the ROM
state and is not the gate - a bench lesson.)

---

## R7 - the HE AP (2.4 GHz 802.11ax) - rung receipt

Boot-time artifact proof (safe, before any rung): `hostapd -v` = v2.12 and
`grep -c '^ieee80211ax=1' /etc/hi5622/hostapd-open-ax.conf` = 1.

```sh
R 'uci set hi5622.main.he_caps=1; uci set hi5622.ap.enabled=1; uci set hi5622.ap.country=US; uci set hi5622.ap.config=/etc/hi5622/hostapd-open-ax.conf; uci commit hi5622'
R '/usr/sbin/hi5622-load'
#   dmesg: "cfg80211: HE (802.11ax) caps advertised on 2.4GHz (AP iftype): ... MAC 01 00 ... PHY0 0x02 ..."
#   hostapd reaches "wlan0: AP-ENABLED" and prints NO "Driver does not support HE"

R 'iw phy phy0 info | sed -n "/HE Iftypes/,+14p"'
#   an "HE Iftypes: AP" block for 2.4 GHz: HE MAC cap byte0 0x01 (HTC HE),
#   HE PHY cap byte0 0x02 (40 MHz in 2.4 GHz), MCS/NSS rx80/tx80 0xfffa/0xfffa,
#   no 6 GHz HE line
```

On-air proof (external observer lane): the beacon carries the **HE Capabilities
IE** (ext 0x23) + the **HE Operation IE** (ext 0x24) and the checker prints
`HE-IE-VERDICT|PASS`.  **Known-good bench facts to match/record:** HE Cap
`HE MAC 0x00011a081044` / `HE PHY 0x00200e926f09ef08110c00`, HE Operation
params `0x003FF4`, **BSS Color 25**.  Page/mask must still be golden after the
AP start.

---

## R8 - the 5 GHz gate receipt + the F2 RF programme (optional rung)

The gates ship fail-closed (R3).  This rung enables them on a fresh boot and
proves the RF chain.  **It does not and cannot produce a 5 GHz beacon yet.**

```sh
R 'uci set hi5622.main.band5g=1; uci set hi5622.main.vap5g=1; uci set hi5622.main.desc_table=1; uci set hi5622.main.dfs_mode=1; uci set hi5622.main.ap_chan_wait_ms=300; uci set hi5622.ap.enabled=1; uci set hi5622.ap.country=US; uci set hi5622.ap.ifname=wlan5; uci set hi5622.ap.config=/etc/hi5622/hostapd-open-5g40.conf; uci commit hi5622'
R '/usr/sbin/hi5622-load'
R 'cat /sys/module/hi5622_wifi/parameters/band5g /sys/module/hi5622_wifi/parameters/vap5g /sys/module/hi5622_wifi/parameters/desc_table /sys/module/hi5622_wifi/parameters/dfs_mode'
#   1 1 1 1
```

Then the F2 RF receipt exactly as
[../wifi-fw-dfs-worker/FIRE-RECIPE.md](../wifi-fw-dfs-worker/FIRE-RECIPE.md)
reads it: mode byte `P+0xF04 = 3`, `R+0x3A8 = 0x01800060`,
`R+0x3AC = 0x03E6138A` (the ch36-64 RF image), band-1 context bound, page/mask
still golden, and the per-link beacon state present (+0x6C4=16, +0x6C6=1).
The config is `ssid=cudy-ap-test-5g40, hw_mode=a, channel=40, ieee80211n=1,
ieee80211ac=1`.

**Do not claim a 5 GHz beacon or on-air proof from this rung.**  The RF is
programmed but the beacon timer is the firmware's internal SMAC/TBTT latch: per
the R6a verdict ([../wifi-tbtt-timer/README.md](../wifi-tbtt-timer/README.md))
no H2D key maps to a beacon/timer worker, the band/MAC start writes no timer
word, and the live latch `0x401015CC` reads **0 (never started)**.  The
remaining work is the firmware-side SMAC-start decode (`fw-smac-start`), not
another host frame.  BA stays fail-closed (`ba_enable=0`, no peer).  Record the
probes; a page/mask/H2D anomaly: power-cycle, capture, no retry.

---

## Acceptance summary

| check | pass criterion | required when |
| --- | --- | --- |
| A0 boot identity | 6.18.60 banner on read-only UART; SSH/ping at 192.168.1.1; pair b | always |
| R1 identity | kernelb `1794a531`; cmdline `ubi.mtd=rootfsb` + `luofu_switch`; ubi0=mtd14; apk pin `6f995278`; pair b/0x11; writable overlay | always - **installed** |
| R2 userspace | module `a717c86d` 358,544 B/64 params; cfg80211 `bc4d9c73`; hostapd `fef44f3b` v2.12 | always - **installed** |
| R3 policy | all gates 0; autoload 0; ap.enabled 0; module not loaded | always - **installed** |
| R4 regdomain | country US; 5 GHz channel usable; no regdb/pkcs7 error | always - **installed** |
| R5 firmware | PD4 `cfe24bfb` + block `0b4ef918` on the box | before any load |
| R6 load/page | C2 ok=2/0/0; page golden; state 1; mask 0; no exception | per rung |
| R7 HE AP | HE IEs on air + BSS Color 25; AP-ENABLED; no "Driver does not support HE" | per rung (2.4 GHz) |
| R8 5 GHz RF | gates 1 1 1 1; mode 3; R+0x3A8/0x3AC exact; band-1 bound; **no beacon** | per rung (5 GHz) |

If any required receipt fails: **STOP**, keep the read-only UART capture, and
follow [ROLLBACK.md](ROLLBACK.md) for install-level failures only.  A rung
failure is not a rollback trigger - power-cycle and re-read the rung recipe.
