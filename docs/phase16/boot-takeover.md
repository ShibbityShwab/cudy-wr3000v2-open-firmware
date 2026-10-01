# boot-takeover: our module owns the endpoint from a clean boot (phase 16, 2026-10-01)

**Result: a boot-time takeover works where runtime takeover is blocked.** Runtime unload is
impossible (`../phase15/teardown.md`: `hi5622v100_plat`'s exit leaves its PCI driver `rox_pci0`,
its kthreads and its hrtimers registered; the box faults ~1.8 s after `rmmod`). A boot-time
takeover needs none of that teardown: hiding the two vendor Wi-Fi `.ko` files makes
`hi5622v100_wifi`/`hi5622v100_plat` never load, so `rox_pci0` is never registered, the two
`59e7:0005` endpoints are enumerated but unbound, and our `bringup.ko` claims one of them the
normal way. Verified on a real boot: vendor stack absent from `lsmod`, `bringup` loaded and
claiming `0000:00:00.0` with `pci_request_mem_regions rc=0`, the ROM vector page and firmware
header logged, both endpoints with **no driver** bound, LAN + SSH up, Wi-Fi down by design.
Recovery restored the normal vendor state (both radios, calibration answering). One real
limitation fell out: the module's full 928,920-byte firmware write **soft-locks the endpoint
bus**; a bounded write completes but does not verify (section 3.1/3.3).

This document records: (1) exactly how the vendor stack is loaded at boot, (2) the reversible
override, (3) the recovery command (recorded before the test reboot), (4) the test-boot evidence,
(5) the recovery-boot evidence, (6) the limits.

---

## 0. Recovery command - written down BEFORE the test reboot (required)

Installed on the device at `/root/recover-boot-takeover.sh` and run as:

    sh /root/recover-boot-takeover.sh

It renames the hidden vendor modules back, removes `/etc/rc.d/S99omo-bringup` and
`/etc/init.d/omo-bringup`, removes the staged `/lib/modules/5.10.201/bringup.ko`, `sync`, `reboot`.
The pristine vendor originals always exist in the read-only lower (`/rom`), so it is a no-op for
anything already restored. Exact script in section 2.3.

The one-shot loader **deletes its own rc.d symlink before its `insmod`** (section 2.2). This is the
watchdog safety: the first test boot's full-image write hung the box (section 3.1) and the
watchdog reset it; because the symlink was already gone, the next boot came up with the vendor
modules still hidden but *without* our module - reachable, so the recovery command could run.
If the box had not come back at all, the fallback is U-Boot slot A (stock).

---

## 1. How the vendor modules are loaded at boot (the exact mechanism)

The chain is **`/etc/init.d/hsan_start` (rc.d `S15`) -> `/usr/bin/wifi_init.sh` -> `insmod`**.

### 1.1 The boot entry

`/etc/rc.d/S15hsan_start -> ../init.d/hsan_start`; `hsan_start` ends its `start()` with:

    /etc/init.d/hsan_start:43    #WIFI ko insmod
    /etc/init.d/hsan_start:45    /usr/bin/wifi_init.sh

(`/etc/init.d/hsan_start`, `START=15`.)

### 1.2 The vendor script

`hi_cfm get board.wifi_vender` is empty and `sysinfo.factory` is `0`, so `/usr/bin/wifi_init.sh`
falls through `wuyi`/`tiangong` to the `else` "shuangta" branch:

    /usr/bin/wifi_init.sh:76    echo "wifi_version shuangta loading"
    /usr/bin/wifi_init.sh:77    insmod /lib/hisilicon/ko/hi_kwificlk.ko
    /usr/bin/wifi_init.sh:78    insmod /lib/hisilicon/ko/hi_pcie.ko
    /usr/bin/wifi_init.sh:79    insmod /lib/hisilicon/ko/hi5622v100_plat.ko
    /usr/bin/wifi_init.sh:80    insmod /lib/hisilicon/ko/hi5622v100_wifi.ko
    /usr/bin/wifi_init.sh:81    hi5622v100_cal_init.sh

Evidence the branch executes: `/usr/bin/hi5622v100_cal_init.sh` (called at line 81, only ever
called from this line) writes its calibration lines to `/var/log/hi5622v100.log`, and
`/usr/bin/wifi_init.sh:90` touches `/tmp/wifi_done`, both present at boot.

### 1.3 Where the `.ko` files really live, and why the path above still works

There is **no** `hi5622v100_*.ko` under `/lib/hisilicon/ko/` on this firmware (`hi_pcie.ko` is
there; `hi_kwificlk.ko` is not; the two Wi-Fi modules live only in `/lib/modules/5.10.201/`):

    /lib/modules/5.10.201/hi5622v100_wifi.ko   md5 e21629d226ec7de9a860a8955952d311  (overlay; vendor patch #1)
    /lib/modules/5.10.201/hi5622v100_plat.ko   md5 23660bc285393e678d5cade1c36c194b  (lower /rom, stock)

`/sbin/insmod` is a symlink to OpenWrt's `/sbin/kmodloader`, which resolves a module **by
basename** into `/lib/modules/$(uname -r)/` when the given path does not exist. Proven on the
device with an unloaded, standalone, stock module:

    # insmod /nonexistent/dir/sch_hfsc.ko ; echo rc=$?
    rc=0
    # lsmod | grep sch_hfsc
    sch_hfsc   28672   0

So `insmod /lib/hisilicon/ko/hi5622v100_plat.ko` really loads
`/lib/modules/5.10.201/hi5622v100_plat.ko`. (`hi_pcie` is loaded from its real
`/lib/hisilicon/ko/hi_pcie.ko` path in the same script.) A second confirmation: with the module
already loaded, `insmod /lib/hisilicon/ko/hi5622v100_plat.ko` reports
`module is already loaded - hi5622v100_plat` even though that path does not exist - the basename
is what is matched.

### 1.4 What is NOT involved

- `/etc/modules.d/` has no vendor entry (only stock OpenWrt crypto/netfilter entries); it is
  consumed by `/sbin/kmodloader` from `/etc/init.d/boot` (`S10boot`).
- `/etc/modules-boot.d/` holds five symlinks to crypto/gpio entries only.
- `/lib/preinit/*` does not load any of this.
- `modprobe -r` does not exist (`modprobe` == `kmodloader`, which rejects `-r`); the load is
  `insmod` by full path as above. Note `/lib/hisilicon/ko/hi_kwificlk.ko` does not exist either,
  so line 77 always fails; it is harmless.

### 1.5 Boot order (`dmesg` of the baseline boot)

    [    3.4] kmodloader: loading kernel modules from /etc/modules-boot.d/*
    [   11.0] kmodloader: loading kernel modules from /etc/modules.d/*
    [   13.0] hi_pcie ... host bridge /pcie@0x10160000          <- hsan_start -> wifi_init.sh:78
    [   13.1] pci 0000:00:00.0 [59e7:0005] ...                  <- endpoints enumerated
    [   15.3] [PCIEL]rox_pci0 PCIe driver register succ         <- hi5622v100_plat
    [   15.4] rox_pci0 0000:00:00.0 / 0001:00:00.0 enabling device

The endpoints do not exist on the PCI bus until `hi_pcie` probes at ~13 s, *after* the
`/etc/modules.d` pass at ~11 s. This is the constraint on where our module can load (section 2.2).

---

## 2. The override (staged in the writable overlay; reversible)

### 2.1 Hide the vendor modules (the choke point)

`insmod` resolves by basename into `/lib/modules/5.10.201/`, so renaming the two files there
neutralises every load path at once:

    mv /lib/modules/5.10.201/hi5622v100_wifi.ko  /lib/modules/5.10.201/hi5622v100_wifi.ko.omo-off
    mv /lib/modules/5.10.201/hi5622v100_plat.ko  /lib/modules/5.10.201/hi5622v100_plat.ko.omo-off

`hi_pcie` is deliberately left loadable and is still loaded by `wifi_init.sh:78`, so the PCIe host
bridges come up and both endpoints are enumerated (and left unbound, because `rox_pci0` only
exists inside `hi5622v100_plat`).

### 2.2 Load our module once, after the bus is up

Staged module: `/lib/modules/5.10.201/bringup.ko` (md5 `a1d9e5b8dae928a5ccd5b6a342b4c7cd`,
`vermagic=5.10.201`, name `bringup`, no dependencies), built from `lab/bringup/bringup.c`.

A `/etc/modules.d/99-omo-bringup` entry was considered and **rejected**: that pass runs at ~11 s,
before `hi_pcie` at ~13 s, so `bringup.ko` would fail `-ENODEV`. Instead the loader is a one-shot
init script at `S99` (after `network`/`dropbear`), which removes its own rc.d symlink first:

`/etc/init.d/omo-bringup`:

    #!/bin/sh /etc/rc.common
    START=99
    start() {
            rm -f /etc/rc.d/S99omo-bringup
            insmod /lib/modules/$(uname -r)/bringup.ko fwpath=/root/FIRMWARE-768k.bin
    }

enabled with `ln -sf ../init.d/omo-bringup /etc/rc.d/S99omo-bringup`. (`fwpath` bounds the write;
see section 3.1/3.3 - the first attempt used the default full image and hung.)

### 2.3 Recovery script installed on the device (`/root/recover-boot-takeover.sh`)

    #!/bin/sh
    set -x
    cd /lib/modules/5.10.201 || exit 1
    [ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
    [ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
    rm -f /etc/rc.d/S99omo-bringup
    rm -f /etc/init.d/omo-bringup
    rm -f /lib/modules/5.10.201/bringup.ko
    sync
    reboot

Staging was verified before the first reboot (`build/register-dumps/boot-takeover/010_staging.txt`):
both vendor modules renamed with their md5s unchanged, `bringup.ko` present, symlink and scripts in
place, both scripts pass `sh -n`.

---

## 3. Test-boot evidence

The test boot was run twice. Everything the brief asks for (a-d) is in **attempt 2**, the clean
boot; attempt 1 is kept because it produced the full-image write hang.

### 3.1 Attempt 1 - claim works, the full 928,920-byte write hangs the bus

First boot of the override (default `fwpath` = the full `FIRMWARE.bin`). It came up, served SSH,
and `bringup` loaded at ~39 s. It claimed the endpoint and logged the ROM page + header, then the
**4th write chunk never returned**; the watchdog declared a soft lockup and panicked ~25 s later,
and the box rebooted. Record pulled from the new pstore entry
(`build/register-dumps/boot-takeover/dmesg-pstore_blk-0.txt`):

    [   39.001998] omo-bringup: ep0 pci_enable_device rc=0
    [   39.006895] omo-bringup: ep0 pci_request_mem_regions rc=0 (MEM BARs claimed)
    [   39.014004] omo-bringup: ep0 BAR0 base=0x40000000 (config-space read; pci_resource_start() is ABI-unsafe here)
    [   39.024037] omo-bringup: ROM vector page BAR0+0x0: 00000101 00000110 00000002 00000000 00000000 00000000 00000000 00000000
    [   39.318599] omo-bringup: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
    [   39.326995] omo-bringup: firmware header BEFORE BAR0+0x40000: 00000020 00000000 00000000 00000000 00000000 00000000 00000000 00b40640
    [   39.373977] omo-bringup: wrote 262144/928920 bytes @ BAR0+0x40000
    [   39.520504] omo-bringup: wrote 524288/928920 bytes @ BAR0+0x80000
    [   39.594646] omo-bringup: wrote 786432/928920 bytes @ BAR0+0xc0000
    [   64.152320] watchdog: BUG: soft lockup - CPU#0 stuck for 22s! [insmod:7366]
    [   64.405005] PC is at memcpy+0x54/0x330
    [   64.579438] [<c0324ef4>] (memcpy) from [<bf94d360>] (omo_bringup_init+0x360/0x1000 [bringup])
    [   64.647670] Kernel panic - not syncing: softlockup: hung tasks

The three chunks up to `BAR0+0x40000+0xc0000` complete (each ~0.15 s); the 4th chunk (starting at
`BAR0+0x100000`, 142,488 bytes) stalls in the MMIO `memcpy` forever. The one-shot loader had
already deleted its symlink, so the watchdog reboot produced a **stable, reachable** boot with the
vendor modules still hidden and no `bringup` (captured in
`030_post_attempt1_stable.txt`). This is a genuine hardware boundary, not a module bug we can
ignore: the endpoint must not be written past ~`BAR0+0x100000` with raw `memcpy_toio`.

### 3.2 The fix for one boot - bound the write

For the clean test boot the loader passed `fwpath=/root/FIRMWARE-768k.bin` - the first 768 KiB
(786,432 bytes = exactly the three chunks that completed on attempt 1) of the vendor image. No
module rebuild was needed; `fwpath` is an existing module parameter.

### 3.3 Attempt 2 - clean test boot, everything the brief asks for (capture: `040_testboot2_evidence.txt`)

    --- uptime ---
     10:43:02 up 1 min

    --- (a) vendor stack absent, ours present ---
    [vendor]:  (none: hi5622v100_wifi + hi5622v100_plat + rox_pci0 absent)
    [hi_pcie]: hi_pcie  20480  0
    [bringup]: bringup  16384  0

    --- (b) our claim, ROM page and firmware header ---
    [   39.012436] omo-bringup: ep0 pci_enable_device rc=0
    [   39.017339] omo-bringup: ep0 pci_request_mem_regions rc=0 (MEM BARs claimed)
    [   39.024399] omo-bringup: ep0 BAR0 base=0x40000000 (config-space read; pci_resource_start() is ABI-unsafe here)
    [   39.034384] omo-bringup: ROM vector page BAR0+0x0: 00000101 00000110 00000002 00000000 ...
    [   39.182218] omo-bringup: firmware file /root/FIRMWARE-768k.bin size=786432 bytes
    [   39.189693] omo-bringup: firmware header BEFORE BAR0+0x40000: 00000020 00000000 ... 00b40640
    [   39.215719] omo-bringup: wrote 262144/786432 bytes @ BAR0+0x40000
    [   39.362244] omo-bringup: wrote 524288/786432 bytes @ BAR0+0x80000
    [   39.436372] omo-bringup: wrote 786432/786432 bytes @ BAR0+0xc0000
    [   39.597834] omo-bringup: write verdict: file=786432 bytes wrote_crc32=273593f6 readback_crc32=ed3b619f diffs=763394 first_diff=0x2 match=NO
    [   39.610412] omo-bringup: firmware write did NOT verify
    [   39.615596] omo-bringup: /sys/kernel/debug/bringup/readback = the 786432 bytes read back from the chip
    [   39.624902] omo-bringup: ep0 claimed, ROM+FW logged, image written (no reset, no CPU start)

    --- (d) what binds the endpoints ---
    0000:00:00.0 -> /sys/bus/pci/devices/0000:00:00.0/driver: No such file or directory
    0001:00:00.0 -> /sys/bus/pci/devices/0001:00:00.0/driver: No such file or directory
    rox_pci0 dir: No such file or directory
    0000:00:00.0 enable = 1        (our pci_enable_device)
    0001:00:00.0 enable = 0        (untouched)

    --- (c) reachability: LAN + SSH up, Wi-Fi down by design ---
    inet 192.168.10.1/24 ... br-lan
    [radios]:  (no Wiphy - wifi down by design)   [wlan ifaces]: 0
    br-lan: eth0, eth1, eth2
    wifi_done: /tmp/wifi_done (present)
    PC ping 192.168.10.1: 2/2, 0% loss

    --- stability ---
    pstore: no new record from this boot (blk-0 is attempt 1's); uptime keeps climbing
    debugfs readback: 786432 bytes

Reading:

- **(a)** `lsmod` has neither `hi5622v100_wifi` nor `hi5622v100_plat`, and `rox_pci0` does not
  exist; `hi_pcie` (the host-controller module) is still loaded, so the bus is enumerated. The
  overlay rename neutralised the boot load completely.
- **(b)** Our module owns `0000:00:00.0`: `pci_enable_device rc=0`, then
  `pci_request_mem_regions rc=0` - the claim the vendor stack would have refused with `-EBUSY`.
  It logs the ROM exception-vector page (`00000101 00000110 00000002`, the same identity words as
  phase 11/15) and the firmware header at `BAR0+0x40000`.
- **(d)** **Nothing binds the endpoints.** Our module is not a `pci_driver` - it claims the device
  with `pci_enable_device` + `pci_request_mem_regions` and holds it, so `/sys/.../driver` has no
  entry for either endpoint and `rox_pci0` is gone. `0000:00:00.0` shows `enable=1` (we enabled
  it); `0001:00:00.0` is `0` (untouched) - a clean demonstration that only our module owns one.
- **(c)** The wired LAN (switch, `br-lan` = eth0/eth1/eth2) is independent of the Wi-Fi stack and
  stayed up through both the test boot and the recovery; SSH answered throughout. There are zero
  `Wiphy` devices and zero wlan interfaces - Wi-Fi is down by design in this boot.

### 3.4 The write does not verify in the unowned state - and why

`match=NO` with 763,394 of 786,432 bytes differing (`first_diff=0x2`) means BAR0+0x40000 read
back is essentially not what was written. Two observations pin this down:

- The header *before* the write is `00000020 00000000 ... 00b40640`, whereas phase 15 saw
  `00046971 000c742d ...` while the vendor stack had loaded its firmware. So with the vendor stack
  absent, BAR0+0x40000 is in a reset/ROM state, not the firmware image window the vendor ends up
  with.
- The full-image write hung at ~`BAR0+0x100000`, i.e. the region past the file's 835,788-byte
  shared prefix (phase 11 noted the tail is a loader/fixup table, not file data).

So a naive `memcpy_toio` of the firmware blob is **not** the vendor download path. The chip
needs its download protocol (reset/CPU release, DMA rings, message interface) before BAR0 behaves
as the download window. That protocol is not implemented here (section 5).

---

## 4. Recovery-boot evidence (capture: `060_recovery_evidence.txt`)

`sh /root/recover-boot-takeover.sh` (`050_recovery_run.txt`) renamed the modules back
(`hi5622v100_wifi.ko` md5 `e21629d2...`, `hi5622v100_plat.ko` md5 `23660bc2...`, both equal to
the pre-test state), removed the entry and `bringup.ko`, and rebooted. The recovered boot:

    --- modules ---
    hi5622v100_plat       323584  3 hi5622v100_wifi
    hi5622v100_wifi      3387392  1
    hi_pcie                20480  0
    [bringup]: (absent)
    --- our leftovers (all "No such file") ---
    /lib/modules/5.10.201/bringup.ko, /lib/modules/5.10.201/hi5622v100_*.omo-off,
    /etc/rc.d/S99omo-bringup, /etc/init.d/omo-bringup
    --- endpoints ---
    0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    --- radios / interfaces ---
    Wiphy phy0, Wiphy phy1   (iw dev: 6 interfaces; vap0/vap3/vap8/vap11 AP, vap1/vap9)
    br-lan up, inet 192.168.10.1/24; brctl shows the eth ports and VAPs (vap8, vap11, eth2 ...)
    --- calibration ---
    Hisilicon0 alg:[SUCC]17161605 17161605 ...        (2g)
    Hisilicon0 alg:[SUCC]00000000 0004ff00 ...        (5g)
    --- pstore: no new record; PC ping 192.168.10.1: 0% loss ---

**Recovery verified: the normal state returns** - both vendor modules `lsmod`-present with the
same use counts as baseline, `rox_pci0` re-registered, both endpoints re-bound, both radios up
(`Wiphy phy0`/`phy1`, all six interfaces named as at baseline), `br-lan` serving on the wired
ports with the AP VAPs, and both calibration commands answering `[SUCC]`. On-device test
artifacts were then removed; the final state has no leftovers (`070_final_state.txt`).

---

## 5. Limits, stated plainly

- **One boot, Wi-Fi intentionally down.** During the test boot there is no `hi5622v100_wifi`,
  no `hi5622v100_plat`, no `rox_pci0`, no `Hisilicon0`, no `Wiphy`, no wlan interface, and the
  calibration commands cannot run. That is by design; the LAN/SSH path never dropped.
- **The test boot's default write hung the box (attempt 1).** Writing the full 928,920-byte
  `FIRMWARE.bin` to `BAR0+0x40000` soft-locks the endpoint bus in the 4th chunk (past
  `BAR0+0x100000`). The clean test boot therefore wrote only a 768 KiB prefix. Passing the full
  image through this module will hang the box; the module needs a bounded/tail-aware write (or the
  vendor download protocol) before the full image is safe.
- **The firmware write does not verify.** Even bounded, BAR0+0x40000 reads back something other
  than what was written (`match=NO`, 763,394/786,432 differing), and the unowned
  `BAR0+0x40000` header (`00000020...`) differs from the vendor-loaded one (`00046971...`). The
  chip is in a reset/ROM state; a raw `memcpy_toio` is not the vendor download path.
- **What our module does with the endpoint:** `pci_get_domain_bus_and_slot`, `pci_enable_device`,
  `pci_request_mem_regions`, config-space BAR0 read, `ioremap` of the ROM page and the firmware
  window, an MMIO write of a firmware file, an MMIO read-back + CRC32, and a debugfs readback file.
  It registers **no** `pci_driver`, installs **no** interrupt handler, sets up **no** DMA rings,
  runs **no** host<->chip message protocol, and never resets the chip or starts its CPU.
- **What it does *not* do:** it cannot make the radio work. There is no DMA ring setup, no message
  protocol, no interrupt/vector setup, and no CPU release - so this boot owns the endpoint
  mechanically but the radio stays down. "Owns the endpoint" here means the PCI memory claim only.
- **One endpoint, not two.** `bringup.ko` has a single device slot and a `domain` parameter
  (default 0); the test claimed `0000:00:00.0` (2.4 GHz). `0001:00:00.0` was left unbound and
  untouched (`enable=0`).
- **Reverting is a reboot.** As in phase 15, `rox_pci0`/`hi5622v100_plat` is not hot-reversible;
  recovery is the rename-back + reboot above. The vendor stack re-probes both endpoints on any
  clean boot, which is what makes this override safe.
- **The "one-shot" loader is a safety device, not a cheat.** It only guarantees a watchdog reboot
  after a hang cannot loop the box; normal use staged it, booted it once, and then recovery
  removed it.

---

## 6. Artifacts (`build/register-dumps/boot-takeover/`, not committed - `build/` is gitignored)

| file | what it is |
| --- | --- |
| `000_baseline.txt` | pre-change device state (md5s, refcounts, endpoints, iw, calibration) |
| `010_staging.txt` | the override staged and verified (`mv` .ko aside, `bringup.ko`, entry, `sh -n`) |
| `021_attempt1_partial_capture.txt` | attempt 1 capture as the box went down |
| `dmesg-pstore_blk-0.txt` | attempt 1's pstore record (44.7 KB), full crash |
| `020_attempt1_hang_excerpt.txt` | attempt 1 excerpt: claim -> 3 chunks -> soft lockup -> panic |
| `030_post_attempt1_stable.txt` | the watchdog reboot after attempt 1: vendor hidden, no bringup, reachable |
| `040_testboot2_evidence.txt` | the clean test boot (a-d as quoted in section 3.3) |
| `050_recovery_run.txt` | the recovery script's own output (rename-back, removals, reboot) |
| `060_recovery_evidence.txt` | the recovered boot: vendor back, both radios, calibration `[SUCC]` |
| `070_final_state.txt` | post-cleanup final state (no leftovers, normal) |

Related: `../phase15/teardown.md` (why runtime unload is impossible), `../phase15/bringup.md`
(the module and its refusal guard), `../phase15/takeover-prep.md` (unbind, unowned chip),
`../phase11/` (BAR0 map and the file/loader boundary).
