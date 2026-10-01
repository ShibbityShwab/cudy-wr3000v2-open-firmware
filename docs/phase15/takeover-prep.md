# takeover-prep: unbinding one vendor Wi-Fi endpoint, and what the chip looks like unowned (phase 15, 2026-10-01)

**Result: the two radios are PCI endpoints `0000:00:00.0` and `0001:00:00.0` (`59e7:0005`), both
bound to the PCI driver `rox_pci0`, which is owned by the module `hi5622v100_plat`. Unbinding the
5 GHz endpoint `0001:00:00.0` from userspace works and, for a fraction of a second (~0.2 s) before
the vendor's own PCIe worker thread faults, the chip is genuinely unowned and still fully readable: our new read-only module `lab/takeover` reads the same identity
words `0x101 0x110 0x2`, the same firmware header `71690400 2d740c00`, a live PCI config space and a
`pci_dev` reference that is still present. Then the vendor's own PCIe worker thread
(`pcie_thread`, in `hi5622v100_plat`) dereferences state that its remove path freed and the box
panics (`panic_on_oops`), the watchdog reboots it, and the vendor stack re-probes both endpoints -
**both radios serve again**. `bind` back from userspace fails (`ENODEV`): this handover is **not
reversible without a reboot**, and the vendor stack is not hot-unbind-safe.**

The 2.4 GHz endpoint `0000:00:00.0` was never unbound.

## 1. Who owns the endpoints, and the dependency chain that must survive

`readlink /sys/bus/pci/devices/<bdf>/driver`, on the device:

    0000:00:00.0 -> ../../../../../bus/pci/drivers/rox_pci0
    0001:00:00.0 -> ../../../../../bus/pci/drivers/rox_pci0

The PCI driver's owning module:

    readlink -f /sys/bus/pci/drivers/rox_pci0/module
    /sys/module/hi5622v100_plat

So the `<drv>` for `unbind`/`bind` is **`rox_pci0`**, and the module that must stay loaded is
**`hi5622v100_plat`**. `hi_pcie` is *not* the endpoint driver here: it is loaded but has use count 0
(it is the SoC-side PCIe host-controller module; `hi_basic` lists it as a user). It was never
touched.

The relevant `lsmod` chain (full capture in `build/register-dumps/takeover/00_before.txt`):

    hi_pcie                20480  0
    hi5622v100_plat       323584  3 hi5622v100_wifi
    hi5622v100_wifi      3387392  1
    hi_kpie               102400 10 hi5622v100_wifi,hi5622v100_plat,...
    ksecurec               28672 25 hi_xt_fwd,hi_kmsgcenter,hi5622v100_wifi,hi5622v100_plat,...

`hi5622v100_wifi` depends on `hi5622v100_plat` (the `3` in `hi5622v100_plat`'s user column), and both
sit on `hi_kpie`/`hi_basic`/`ksecurec`. Nothing was `rmmod`ed at any point - only `unbind`/`bind`
were used, plus our own module.

## 2. The module: `lab/takeover`

Source: `lab/takeover/{takeover.c,Makefile}` (`obj-m := takeover.o`). Built by the existing
`.github/workflows/build-load-test-module.yml` job (a `build takeover module` step, a vermagic line,
and a `takeover-ko` artifact were added). CI run **`36844949128`**, conclusion `success`; artifact
`takeover.ko`, md5 **`83ab50fcc2f9ae2f512c5806f5c73753`** (device md5 identical);
`vermagic=5.10.201 SMP mod_unload ARMv7`.

It is the hwprobe/ringwatch pattern aimed at a *selectable* endpoint:

- module parameter `domain=N` picks `N:00:00.0`, because the two radios are the same bus/dev/func and
  differ only in PCI domain;
- it does **not** claim the device: no `pci_request_region`, no `pci_enable_device`, no reset, no
  config write, no `iowrite32`. BAR0's base is read with `pci_read_config_dword()` (a vendor-kernel
  accessor, so no `struct pci_dev` layout risk) and four small windows are `ioremap()`ed;
- bounded debugfs reads under `/sys/kernel/debug/takeover/`:
  - `identity` - 4 words at `BAR0+0x3b8000` (the register-block anchor),
  - `fwheader` - 8 words at `BAR0+0x40000` (the firmware-image header),
  - `rings` - the two live cursor markers `0x1d093c`/`0x1d09c0` plus their `0x8c89xx` aliases,
    sampled fresh on every read (so two reads are two samples),
  - `refcheck` - PCI config vendor/device/class/rev plus a `pci_dev_get()`/`pci_dev_put()` round
    trip, to show the device object is still on the bus;
- everything is logged to dmesg at load as well.

It never unbinds or rebinds anything; the handover is driven entirely from userspace.

## 3. The handover cycle

### (a) Before

`build/register-dumps/takeover/00_before.txt`. Both endpoints bound to `rox_pci0`; `iw dev` shows
`phy1` = `vap8`,`vap9`,`vap11` (5 GHz) and `phy0` = `vap0`,`vap1`,`vap3` (2.4 GHz); `iw phy` shows
`Wiphy phy1 (Band 2)` and `Wiphy phy0 (Band 1)`; `vap0`/`vap3`/`vap8`/`vap11` are UP in `br-lan`.

### (b) Unbind endpoint `0001:00:00.0`

    echo 0001:00:00.0 > /sys/bus/pci/drivers/rox_pci0/unbind ; echo unbind-rc=$?
    unbind-rc=0
    readlink /sys/bus/pci/devices/0001:00:00.0/driver        -> (none)
    readlink /sys/bus/pci/devices/0000:00:00.0/driver        -> ../../../../../bus/pci/drivers/rox_pci0

The 5 GHz endpoint is unbound; the 2.4 GHz endpoint keeps its driver.

### (c) While unbound - the chip's window

Control first (module against the still-bound endpoint, `01_module_ep0_bound.txt` and
`04_module_ep1_bound.txt`): `identity = 00000101 00000110 00000002 00000000`,
`fwheader = 00046971 000c742d 00000000 ...`, `refcheck present=yes vendor=59e7 device=0005`.
On endpoint 1 while bound the cursors were seen to move (`0x1d093c` `80018001` -> `80048004`).

While endpoint 1 is unbound, the module's already-mapped windows still read
(`05_unbind_unowned_capture.txt`, `06_full_cycle_rebind.txt`):

    === readlink 0001 (unbound) ===   (none)
    === identity ===  BAR0+0x3b8000: 00000101 00000110 00000002 00000000
    === fwheader ===  BAR0+0x40000:  00046971 000c742d 00000000 00000000 00000000 00000000 00000000 00000000
    === rings s1 ===  0x1d093c 00160016   0x1d09c0 001a001a   0x8c893c 00160016   0x8c89c0 001a001a
    === rings s2 ===  0x1d093c 00190019   0x1d09c0 001a001a   0x8c893c 00190019   0x8c89c0 001a001a
    === refcheck ===  present=yes  config vendor=59e7 device=0005 class=0x028000 rev=0x00
                      pci_dev_get returned the device (pci_dev_put done)

So, unowned: the identity words and firmware header are unchanged and not `0xff`; the cursor markers
are live values and were observed to advance (`00160016` -> `00190019`) between two samples; PCI
config space answers and the `pci_dev` object is still present. The vendor logged its own removal:

    [PCIEL]pcie driver remove 0x5, name:0001:00:00.0
    [PCIE][INFO] [oal_pcie_regions_exit:785]oal_pcie_regions_exit

Note it tears down the PCIe regions but does **not** unregister `phy1`: within the window `iw dev`
still listed `phy1`/`vap8`/`vap9`/`vap11`. Those interfaces are not serving the radio, though - the
chip is unowned and the crash below follows within a fraction of a second.

### (d) rmmod the takeover module

Reached in the first unowned run: `rmmod takeover` -> `rmmod-rc=0`; the dmesg line
`omo-takeover: debugfs removed, windows unmapped, device released` and the debugfs directory is gone
afterwards (`01_module_ep0_bound.txt`).

### (e) Rebind - fails

    echo 0001:00:00.0 > /sys/bus/pci/drivers/rox_pci0/bind ; echo bind-rc=$?
    ash: write error: No such device
    bind-rc=1
    readlink /sys/bus/pci/devices/0001:00:00.0/driver   -> (none)

`bind` returns `ENODEV`. The PCI device object is still enumerable (our `refcheck` just used it), so
this is the vendor driver refusing/failing to re-attach to a device whose state its remove path
already destroyed - not the device being absent from the bus.

### (f) What breaks, and recovery

Immediately after the remove the vendor's PCIe worker thread faults. Verbatim from
`build/register-dumps/takeover/pstore_attempt3/dmesg-pstore_blk-0` (the full previous-boot dmesg):

    8<--- cut here ---
    Unable to handle kernel paging request at virtual address c9c604ec
    Internal error: Oops: 7 [#1] SMP ARM
    CPU: 1 PID: 1174 Comm: pcie_thread ... 5.10.201 #0
    PC is at shuangta_ete_dr_get_sr_dscr_flag+0x8/0x14 [hi5622v100_plat]
    LR is at pcie_ete_sending_trigger+0xc8/0x5d8 [hi5622v100_plat]
    ...
    (shuangta_ete_dr_get_sr_dscr_flag) from (pcie_ete_sending_trigger+0xc8/0x5d8)
    (pcie_ete_sending_trigger) from (pcie_thread_handle+0x90/0x3fc)
    (pcie_thread_handle) from (pcie_process_thread+0x38/0x1bc)
    (pcie_process_thread) from (kthread+0x14c/0x150)
    ---[ end trace 79ba66e367bc36f8 ]---
    Kernel panic - not syncing: Fatal exception

This is deterministic: it reproduced on both unbind attempts (`pstore/` and `pstore_attempt3/`; the
second crash's PC is inside `pcie_ete_sending_trigger+0x4a0`). `hi_linux`'s `panic_on_oops` turns it
into a panic; the 30 s hardware watchdog then reboots the box. **The panic is a property of the
vendor stack's remove path, not of our module** - `takeover` is loaded, read-only, and unloaded
again in the first run before the fault, and the faulting thread is vendor code in
`hi5622v100_plat`.

Recovery is automatic. After the reboot, both endpoints re-probe and both radios serve
(`07_after_recovery_final.txt`):

    === FINAL links ===
    0000:00:00.0 -> ../../../../../bus/pci/drivers/rox_pci0
    0001:00:00.0 -> ../../../../../bus/pci/drivers/rox_pci0
    === FINAL iw phy summary ===   Wiphy phy1 (Band 2) ; Wiphy phy0 (Band 1)
    === FINAL iw dev ===           phy1: vap8, vap9, vap11 ; phy0: vap0, vap1, vap3
    === FINAL vap link state ===   vap0,vap3,vap8,vap11 UP master br-lan
    === FINAL dmesg ===            [PCIEL]rox_pci0 PCIe driver register succ
                                   firmware_download success
                                   enable radio0/1 ... enable radio2/3

`iw phy phy1 info` answers (`Wiphy phy1`, wiphy index 1, managed+AP, `Band 2`). **Acceptance state
reached: both radios serving.** The vendor's `Could not find PHY for device 'radio2'/'radio3'` lines
are its own config entries that have no matching phy; they are present on a stock boot too and do
not correspond to either of the two active radios.

The 2.4 GHz endpoint was never unbound, but in this cycle it did not spare the box: the fault is in
the 5 GHz endpoint's own worker, and the panic reboots everything. Over the wired LAN the SSH path
stayed up through the whole exercise (the PC reaches `192.168.10.1` over Ethernet `192.168.10.28`,
not Wi-Fi), so both the unowned samples and the recovery were captured live.

## 4. Limits, stated plainly

- **A real takeover cannot be done by `unbind` alone.** Detaching `0001:00:00.0` from `rox_pci0`
  leaves the vendor's `pcie_process_thread` running against freed state; it oopses and the box
  panics. To own the device for real, the whole vendor Wi-Fi stack has to be out of the way - a full
  unload of `hi5622v100_wifi` + `hi5622v100_plat` (in that order, after the traffic paths are down),
  or a reboot into a kernel where they never load - not a per-endpoint `unbind`. This milestone
  proves only the *userspace* handover step and the read-only inspection, both reversible only
  through a reboot.- **`bind` is not a working "give it back".** It returns `ENODEV`; the documented way back is the
  reboot the panic already forces, after which the vendor stack re-initialises cleanly.
- **What the unowned window proves, and does not.** The chip's BAR0 memory and register window are
  readable while nothing owns the endpoint, the identity/firmware-header values survive, and a
  `pci_dev` reference round trip works - all captured before the vendor thread faulted. It does not
  prove that a *sustained* unowned state is safe, because it is not: the window is only ~0.2 s
  (measured: remove at 50.548898 s, fault at 50.789925 s in `pstore_attempt3/dmesg-pstore_blk-1`;
  and 75.986835 s -> 76.203397 s in `dmesg-pstore_blk-0`).
- **The 2.4 GHz radio is not a safety net for the box, only for the LAN path.** It stayed bound and
  `phy0`/its vaps stayed listed throughout, and it kept the wired/LAN control path alive; but the 5
  GHz worker's panic reboots the whole SoC regardless, taking 2.4 GHz down with it for the reboot.
  Anything that must survive the takeover (SSH recovery, watchdog) must not depend on either radio -
  here it did not.
- **Read-only, single radio.** `takeover` only reads; no writes to the BAR or config space. Only
  endpoint `0001:00:00.0` was unbound; endpoint `0000:00:00.0` never was.
- **`build/` artifacts are not committed** (the repo `.gitignore`s `build/`). The raw captures live
  on disk under `build/register-dumps/takeover/` (see `00_before.txt`, `01_module_ep0_bound.txt`,
  `02_unbind_0001.txt`, `03_after_reboot_health.txt`, `04_module_ep1_bound.txt`,
  `05_unbind_unowned_capture.txt`, `06_full_cycle_rebind.txt`, `07_after_recovery_final.txt`,
  `08_pstore_list.txt`, `pstore/`, `pstore_attempt3/`).

## 5. Reproduce

    # build + CI
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download <run-id> -n takeover-ko -D /tmp/takeover-ko

    # device: control read against the still-bound 2.4 GHz endpoint
    scp -O /tmp/takeover-ko/takeover.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/takeover.ko domain=0; \
      cat /sys/kernel/debug/takeover/{identity,fwheader,rings,refcheck}; rmmod takeover'

    # the handover (this panics and reboots the box; only endpoint 0001)
    ssh root@192.168.10.1 'insmod /tmp/takeover.ko domain=1; \
      cat /sys/kernel/debug/takeover/rings; cat /sys/kernel/debug/takeover/rings; \
      echo 0001:00:00.0 > /sys/bus/pci/drivers/rox_pci0/unbind; \
      cat /sys/kernel/debug/takeover/{identity,fwheader,rings,refcheck}; \
      echo 0001:00:00.0 > /sys/bus/pci/drivers/rox_pci0/bind; \
      rmmod takeover'

    # wait for the watchdog reboot, then verify both radios
    ssh root@192.168.10.1 'iw phy; iw dev; for d in 0000:00:00.0 0001:00:00.0; do \
      readlink /sys/bus/pci/devices/$d/driver; done; dmesg | grep -E "pcie driver register|firmware_download"'
