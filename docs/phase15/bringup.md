# bringup: the first real bring-up attempt of one Wi-Fi endpoint, and where the vendor stack refuses to let go (phase 15, 2026-10-01)

**Result: the stack cannot be unloaded. `rmmod hi5622v100_wifi` - the first module in the
documented reverse load order - oopses inside the vendor module's own exit path
(`hmac_main_exit -> hdpp_main_exit -> hdpp_user_module_exit -> hdpp_user_del ->
hmac_softnp_user_clear_proc+0x80`, `PC=0x0`), and the 30 s watchdog reboots the box. That is
the minimal failing step: everything past it (plat, the rest of the chain, our endpoint claim)
is unreachable, so the firmware write was never attempted and no checksum verdict exists. The
bringup module is instead built and proven against the still-owned endpoint: it is refused the
BAR with `-EBUSY` and, by construction, logs that and writes nothing. Recovery is automatic and
verified: after the reboot both endpoints are bound to `rox_pci0`, both `Wiphy phy0`/`phy1` and
all AP VAPs are up in `br-lan`, and calibration answers `[SUCC]`.**

This is verification outcome **(b)** from the brief: the minimal failing step is documented with
its panic record, and the router ends healthy.

## What this does and does not cover

- **Does:** establishes the vendor's real load order and the reverse unload order from the
  device's own loader script; shows `modprobe -r` is not available on this device; identifies the
  userspace module reference and how to drop it; proves the stack's own module-exit path is not
  unload-safe; builds and device-loads an endpoint-owning firmware-download module; proves that
  module refuses to touch an owned endpoint.
- **Does not:** claim the endpoint, map BAR0 with our own claim, log the ROM/firmware header from
  that claim, or write the firmware image. There is **no DMA ring setup, no message protocol, no
  interrupt setup, and no CPU release** anywhere in this milestone - only the memory-download
  half of a bring-up was even designed, and that half was not reached. See "Limits".

## 1. The unload order, and why it cannot be executed

### 1.1 The vendor's own order (authoritative)

`/usr/bin/wifi_init.sh`, the "shuangta" branch for this chip, loads with `insmod`:

    insmod /lib/hisilicon/ko/hi_kwificlk.ko
    insmod /lib/hisilicon/ko/hi_pcie.ko
    insmod /lib/hisilicon/ko/hi5622v100_plat.ko
    insmod /lib/hisilicon/ko/hi5622v100_wifi.ko
    hi5622v100_cal_init.sh
    ... (wifi_debug.ko only if /etc/hi_version contains "turbo")

So the reverse (unload) order is `hi5622v100_wifi -> hi5622v100_plat -> hi_pcie ->
hi_kwificlk`. `lsmod` agrees on the dependency direction: `hi5622v100_plat ... 3
hi5622v100_wifi` (the `used by` column), i.e. wifi uses plat, so wifi must go first.
`hi_pcie` was loaded with use count 0. The debug modules (`wifi_debug.ko`, `plat_debug.ko`) were
never loaded and were never `rmmod`ed.

### 1.2 `modprobe -r` does not exist on this device

`/sbin/modprobe` is a symlink to OpenWrt's `kmodloader` (so are `insmod`/`rmmod`/`lsmod`), and
this build's `modprobe` mode rejects `-r`:

    # modprobe -r hi5622v100_wifi ; echo rc=$?
    modprobe: unrecognized option: r
    rc=255

(`kmodloader -h`, `modprobe --help` print nothing; `rmmod -h` treats `-h` as a module name.)
The exact-single-module equivalent is `rmmod <name>` (kmodloader's `rmmod` mode by argv[0]),
which is what was used. The brief's "never rmmod the debug modules" was honoured: no
`wifi_debug`/`plat_debug` was present or touched.

### 1.3 The module reference, and taking the traffic paths down

`rmmod hi5622v100_wifi` initially failed - use count 1, no *module* holder
(`/sys/module/hi5622v100_wifi/holders` empty):

    # busybox rmmod hi5622v100_wifi
    rmmod: can't unload module 'hi5622v100_wifi': Resource temporarily unavailable
    # cat /sys/module/hi5622v100_wifi/refcnt
    1

The reference is a userspace one: the hostapd/wpa_supplicant control sockets. The teardown that
dropped it to 0 was:

    wifi down                              # rc=0, removes every VAP
    /etc/init.d/softapd stop               # rc=0
    kill $(pgrep -x app_acs)                # rc=0
    kill $(pgrep -x app_nlc)                # rc=0
    /etc/init.d/wpad stop                   # rc=0  <-- refcnt 1 -> 0 here

(`hi5622v100_plat`'s own use count also fell 3 -> 1 over `wifi down`, the remaining 1 being
wifi itself.) After `/etc/init.d/wpad stop`, `cat /sys/module/hi5622v100_wifi/refcnt` returned
`0`. Only then was the module removable in principle.

### 1.4 The panic (minimal failing step)

With refcount 0, `rmmod hi5622v100_wifi` reset the SSH connection; the pstore record from the
next boot (`build/register-dumps/bringup/pstore_attempt_m0.txt`) is unambiguous:

    [HIWIFI]hi_wifi_tx_hook para func is null
    hmac_softnp_woe_hook_exit: succ
    swa_netdevice_event 215:event:6, name:Hisilicon0
    [HSLINK]dev(Hisilicon0) is destroy
    8<--- cut here ---
    Unable to handle kernel NULL pointer dereference at virtual address 00000000
    Internal error: Oops: 80000007 [#1] SMP ARM
    Modules linked in: ... hi5622v100_wifi(O-) hi5622v100_plat(O) ...
    CPU: 0 PID: 17934 Comm: rmmod ...
    PC is at 0x0
    LR is at hmac_softnp_user_clear_proc+0x80/0xc0 [hi5622v100_wifi]
    (hmac_softnp_user_clear_proc [hi5622v100_wifi]) from (hdpp_user_del+0x38/0x84 [hi5622v100_wifi])
    (hdpp_user_del [hi5622v100_wifi]) from (hdpp_user_module_exit+0x54/0xac [hi5622v100_wifi])
    (hdpp_user_module_exit [hi5622v100_wifi]) from (hdpp_main_exit+0x8/0x64 [hi5622v100_wifi])
    (hdpp_main_exit [hi5622v100_wifi]) from (hmac_main_exit+0x5c/0xd4 [hi5622v100_wifi])
    (hmac_main_exit [hi5622v100_wifi]) from (sys_delete_module+0x140/0x208)
    Kernel panic - not syncing: Fatal exception

`hi5622v100_wifi(O-)` is the kernel's own marker that the module was mid-removal. **The fault is
in the vendor module's `module_exit`, not in our code** - nothing of ours was loaded at that
point. The stack panics on unload, exactly as it panics on a per-endpoint `unbind` (phase 15
`takeover-prep`).

### 1.5 Why it is deterministic, not a state we left behind

The device's own `hi5622v100_wifi.ko` (pulled; md5 `4737fcb21a1a2262a96f84d780ad8b35`, identical
to `/lib/modules/5.10.201/hi5622v100_wifi.ko`) was disassembled (`pyelftools` + `capstone`).
`hmac_softnp_user_clear_proc` at `+0x7c` is:

    +0x74: ldr  r3, [r4, #0x10]      ; r4 = &(.LANCHOR0), a runtime-populated global
    +0x7c: blx  r3                   ; -> PC=0x0 when the slot is NULL; LR = +0x80
    +0x80: ldr  r3, [r4, #0x14]
    +0x90: bx   r3

and the relocation that fills `r4` targets `.LANCHOR0` (a local anchor in `.bss`), i.e. a
function-pointer slot that is zero until something registers it. The exit's own
`hmac_softnp_woe_hook_exit` runs immediately before `hdpp_user_del` in the same teardown, so the
slot is already cleared by the time it is called - the null call is structural, not a side
effect of `wifi down`. A second unload attempt in a different radio state would cost another
reboot and was not expected to differ.

## 2. The bringup module (`lab/bringup`)

Source: `lab/bringup/bringup.c` + `lab/bringup/Makefile` (`obj-m := bringup.o`). Built by
`.github/workflows/build-load-test-module.yml` (a `build bringup module` step, a vermagic line,
and a `bringup-ko` artifact were added).

- CI run (green): **`36846202589`** -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36846202589
  - artifact `bringup-ko` = `bringup.ko`, 11592 bytes, md5 **`a1d9e5b8dae928a5ccd5b6a342b4c7cd`**,
    sha256 `d542b0c3d3082a2fb38812e9e27b333131f83ffea34fec5248701387a9b5892b`,
    `vermagic=5.10.201 SMP mod_unload ARMv7` (matches the vendor kernel; device md5 identical).
- It targets one endpoint by PCI **domain** (`domain=N`, default 0 = `0000:00:00.0`) and takes the
  firmware file path as a parameter (`fwpath=P`, default `/lib/firmware/hi_wifi/FIRMWARE.bin`).
- Load sequence, all in `module_init`:
  1. `pci_get_domain_bus_and_slot()` -> `pci_enable_device()` -> `pci_request_mem_regions(dev,
     "omo-bringup")`. If the request fails (region busy) it logs and **aborts before mapping or
     writing** - the built-in guard against touching a vendor-owned chip.
  2. BAR0 base from **PCI config space** with `pci_read_config_dword()` (not
     `pci_resource_start()`: that returns 0 across the struct-`pci_dev` ABI boundary, phase 11
     `hwprobe`).
  3. `ioremap()` the ROM vector page (BAR0+0x0, 4 KiB) and the firmware window
     (BAR0+0x40000, `fwlen`); log 8 words of each.
  4. read the firmware file with `kernel_read()` until EOF (no `i_size_read`/`file_inode`, again
     to avoid struct-layout inlines), bounded to 2 MiB.
  5. `memcpy_toio()` the bytes to BAR0+0x40000 in 256 KiB chunks with a progress line each,
     `memcpy_fromio()` the same region back, then compare byte-for-byte and CRC32 and log
     `match=YES|NO`, `diffs`, `first_diff`.
  6. expose the read-back bytes at `/sys/kernel/debug/bringup/readback`.
- On unload: debugfs removed, windows `iounmap`ed, `pci_release_mem_regions` +
  `pci_disable_device` + `pci_dev_put`. No reset, no config-space write, no CPU start.

## 3. The on-device sequence - what was actually run

### 3.1 The unload attempt (section 1) - panicked, box rebooted

Sequence, one step per SSH invocation, each captured (files in section 7):

    rmmod hi5622v100_wifi                         # refcnt 1 -> EAGAIN, refused
    wifi down ; /etc/init.d/softapd stop ; kill app_acs app_nlc ; /etc/init.d/wpad stop
    # refcnt now 0
    rmmod hi5622v100_wifi                         # -> oops in hmac_main_exit, panic, watchdog reboot

The 2.4 GHz endpoint `0000:00:00.0` was never unbound; the 5 GHz endpoint was never unbound in
this milestone either. No per-endpoint `unbind`/`bind` was used.

### 3.2 `bringup.ko` against the still-owned endpoint (guard proof, no write)

Because the prerequisites could not be met (section 1), `insmod`ing `bringup.ko` while the
vendor still owns the endpoint is the only safe execution available - and it is the intended
failure path of the module. `/proc/iomem` shows the vendor holds the BAR
(`40000000-40ffffff : 0000:00:00.0`, child `SHUANGTA_REGION_ROM_WRAM`), so the claim must fail:

    # insmod /tmp/bringup.ko ; echo insmod_rc=$?
    insmod_rc=255
    # dmesg
    omo-bringup: ep0 pci_enable_device rc=0
    rox_pci0 0000:00:00.0: BAR 0: can't reserve [mem 0x40000000-0x40ffffff 64bit]
    omo-bringup: pci_request_mem_regions rc=-16 (region busy - vendor stack still loaded?) - refusing to write

The module did not stay loaded and did **not** map or write anything. Endpoints stayed bound and
calibration answered afterwards. This proves the module loads on the device (vermagic/symbols
OK) and that its guard prevents a write to an owned chip.

## 4. ROM / firmware findings

No new on-device read was possible in this run (the module refuses before mapping when the
endpoint is owned). The known, already-established values are:

- BAR0 `0x0`, the ARM exception-vector page: first bytes `18 f0 9f e5` repeated
  (`e59ff018` = `ldr pc, [pc, #24]`) - the ROM/boot vector page (phase 11 `barmap`).
- BAR0 `0x40000`, the firmware header: `00046971 000c742d 00000000 ...` (raw
  `71 69 04 00 2d 74 0c 00 ...`) - unchanged and non-`0xff` while unowned (phase 15
  `takeover-prep`).
- The image to be written: `/lib/firmware/hi_wifi/FIRMWARE.bin`, **928,920 bytes**, md5
  `0e530b976d5a20e87358671f1a577695`, sha256
  `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` (pulled to
  `build/register-dumps/bringup/FIRMWARE.bin`, device = local).
- Note for a future run: phase 11 found the file is byte-identical to BAR0 only for its first
  835,788 bytes; the file's last ~93 KiB are zeros while BAR0 `0x10c0cc` onward holds a
  loader/fixup table. Writing the file's full 928,920 bytes to BAR0+0x40000 (as specified) would
  therefore also zero BAR0 `0x10c0cc..0x11dfff`. The module bounds the write to the file length
  and to <=2 MiB; nothing outside `[0x40000, 0x40000+fwlen)` is touched.

## 5. Recovery evidence (post-panic reboot, verbatim)

Captured in `build/register-dumps/bringup/050_final_health.txt`:

    === uptime ===            10:27:22 up 2 min
    === endpoints ===         0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
                              0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    === iw phy ===            Wiphy phy1 ; Wiphy phy0
    === iw dev ===            phy1: vap11,vap8 (AP), vap9 ; phy0: vap3,vap0 (AP), vap1
    === vap link state ===    vap0,vap3,vap8,vap11 UP master br-lan
    === bridge members ===    br-lan: eth0, eth1, eth2, vap0, vap3, vap8, vap11
    === calibration 2g ===    Hisilicon0 alg:[SUCC]17161605 17161605 ...
    === calibration 5g ===    Hisilicon0 alg:[SUCC]00000000 0004ff00 ...
    === dmesg ===             rox_pci0 PCIe driver register succ
                              firmware_download success
                              enable radio0 / radio1 / radio2 / radio3
                              (no oops/panic in the running boot)

Both radios serve; the wired SSH path never went down. The `hi5622v100_*` modules are present
again with the same use counts as at baseline.

## 6. Limits, stated plainly

- **The headline bring-up was not achieved.** The brief's step 1 prerequisite ("unload the whole
  vendor wireless stack") is impossible on this firmware: the vendor module's own exit crashes.
  Therefore steps 2-3 (claim with our own code, ROM/header log from that claim, firmware write,
  checksum verdict) were not reachable, and there is **no write verdict**.
- **`modprobe -r` is not a thing here.** This device's `/sbin/modprobe` is `kmodloader` and
  rejects `-r`; `rmmod` was used for exact single-module removal, never on the debug modules.
- **The module reference is a userspace socket, not a module.** `wifi down` + stopping
  `wpad`/`softapd`/`app_nlc`/`app_acs` drives `hi5622v100_wifi`'s use count to 0; that is the
  "traffic paths down" state. The unload still panics.
- **One panic was spent** to establish the minimal failing step. The disassembly shows the
  faulting call is a null slot cleared by the same exit path, so the failure is treated as
  structural; no second unload attempt was made.
- **What a real bring-up would additionally need** (none of it attempted): DMA ring setup, the
  host<->chip message protocol, interrupt/vector setup, and releasing the chip CPU. Even the
  memory-download half that `bringup.c` implements was only exercised up to its refusal guard.
- **No hardware was written in this milestone.** `bringup.ko` never reached its write; the only
  device changes were `insmod`/`rmmod` of our own module, `wifi down`, and stopping vendor
  daemons - all undone by the panic reboot.
- **`build/` is not committed** (the repo `.gitignore`s `build/`). All raw captures live on disk
  at `build/register-dumps/bringup/`.

## 7. Artifacts (`build/register-dumps/bringup/`)

| file | what it is |
| --- | --- |
| `000_baseline.txt` | pre-work `lsmod`, endpoints, `iw dev`, firmware file md5 |
| `001_deps_dryrun.txt` | dependency chain, loader identification, `modprobe -n` unsupported |
| `010_unload_wifi.txt`, `011_unload_wifi.txt` | `modprobe -r` rejected; bare `rmmod` refused (EAGAIN) |
| `012_wifi_down_prepare.txt`, `013_after_drop.txt`, `014_state_after_prepare.txt` | `wifi down` / daemon stop, refcount |
| `015_daemon_stop.txt` | refcnt `1 -> 0` after `/etc/init.d/wpad stop` |
| `020_unload_chain.txt` | the armed `rmmod hi5622v100_wifi` (connection reset) |
| `021_pstore_panic.txt`, `pstore_attempt_m0.txt`, `pstore_attempt_m3.txt` | the panic record, pulled |
| `030_recovery_attempt1.txt`, `050_final_health.txt` | post-reboot radio/calibration recovery |
| `040_guard_test.txt` | `bringup.ko` `-EBUSY` refusal against the owned endpoint |
| `FIRMWARE.bin` | the 928,920-byte image the module would write (md5 `0e530b976d5a20e87358671f1a577695`) |

## 8. Reproduce

    # build + download the module
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download 36846202589 -n bringup-ko -D /tmp/bringup-ko
    md5sum /tmp/bringup-ko/bringup.ko          # a1d9e5b8dae928a5ccd5b6a342b4c7cd

    # device: drop the userspace module reference, then try to unload (this panics)
    ssh root@192.168.10.1 'wifi down; /etc/init.d/softapd stop; \
      kill $(pgrep -x app_acs) $(pgrep -x app_nlc); /etc/init.d/wpad stop; \
      cat /sys/module/hi5622v100_wifi/refcnt; rmmod hi5622v100_wifi'
    # watchdog reboots; pull the panic:
    scp root@192.168.10.1:/sys/fs/pstore/dmesg-pstore_blk-0 .

    # device: prove the refusal guard with the vendor stack up (no write)
    scp -O /tmp/bringup-ko/bringup.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/bringup.ko; dmesg | grep omo-bringup'
    # -> pci_request_mem_regions rc=-16 ... refusing to write

    # recovery
    ssh root@192.168.10.1 'for d in 0000:00:00.0 0001:00:00.0; do readlink -f \
      /sys/bus/pci/devices/$d/driver; done; iw phy; iw dev; \
      iwpriv Hisilicon0 alg get_2g_power_param'
