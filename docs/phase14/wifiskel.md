# wifiskel: registering a cfg80211 wiphy from our own module (phase 14, 2026-10-01)

**Result: the vendor kernel accepted our module's `wiphy_new_nm()` / `wiphy_register()`
calls (`rc=0`) and a phy called `omo-skel` appeared in `iw phy` while loaded and vanished on
`rmmod`. But it did not land where we put it: the 2.4 GHz band was registered at nl80211 band
index 2 (the 60 GHz slot). The cause is a struct-ABI divergence, not a bug in the module: the
vendor kernel's `struct wiphy` places `bands[]` 8 bytes (two ARMv7 pointers) *below* the vanilla
5.10.201 `multi_v7_defconfig` offset. Every field up to `interface_modes` lines up; the delta
begins somewhere before `bands[]`. It is not a config toggle we can reproduce - see the verdict
below.**

This is the cfg80211 counterpart to the phase-11 `struct pci_dev` finding: a module built from
public source against vanilla 5.10.201 headers does **not** share the vendor kernel's struct
layouts, and the deepest driver integration point (cfg80211) demonstrates it cleanly. No
hardware was touched, no oops occurred, and the router's own Wi-Fi stack was untouched.

## CI

- Workflow: `build-load-test-module.yml`, job `build`, step `build wifiskel module`.
- CI run (green): **`36840547817`** -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36840547817
  - commit `ee12ede`, artifact `wifiskel-ko`
  - module fingerprint from CI: `vermagic=5.10.201 SMP mod_unload ARMv7`
  - artifact md5: `a4c0a14f570cad369ff72e37564a4c36` (matches the `.ko` loaded on the device)
- Source: `lab/wifiskel/wifiskel.c` (+ `lab/wifiskel/Makefile`, `obj-m := wifiskel.o`).

## What the module does

`lab/wifiskel/wifiskel.c` is a plumbing-only wiphy skeleton. On load it:

1. logs the struct sizes it was compiled with,
2. `wiphy_new_nm(&omo_ops, 0, "omo-skel")` - one empty `cfg80211_ops` table, zero private data,
   requested name `omo-skel`,
3. sets `wiphy->bands[NL80211_BAND_2GHZ]` to a static 2.4 GHz band: channels 1..13 (2412..2472
   MHz), `max_power = 20` dBm, 12-bitrate CCK/OFDM list, and a minimal HT cap (`HT20`, `SGI_20`,
   `MAX_AMSDU`, MCS 0-7, no HT40),
4. sets `wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION)` so cfg80211's `WARN_ON(!ifmodes)`
   does not fire,
5. `wiphy_register()`.

On unload it calls `wiphy_unregister()` then `wiphy_free()`. Every step and its return code is
`pr_info`'d with the `omo-skel` prefix.

There is **no hardware path at all**: no PCI access, no MMIO, no firmware, no netdev, no data
path. The module never creates an interface; it only proves cfg80211 registration works across
the vendor ABI boundary (and reveals where it does not).

## Before / after, verbatim from the router

Raw captures are in `build/register-dumps/wifiskel/` (`01_before.txt` .. `06_after_health.txt`,
`wifiskel_dmesg.txt`).

### Before - `iw phy`

    Wiphy phy1
    Wiphy phy0

### insmod (rc=0) - dmesg

    [43178.889400] omo-skel: init: built against vanilla 5.10.201 headers: sizeof(struct wiphy)=832 sizeof(struct ieee80211_supported_band)=92 sizeof(struct ieee80211_channel)=56 sizeof(struct ieee80211_sta_ht_cap)=22
    [43178.908037] omo-skel: wiphy_new_nm(ops, sizeof_priv=0, name="omo-skel") rc=0 ptr=c47e61c0
    [43178.916213] omo-skel: set bands[2GHZ] rc=0 (n_channels=13 n_bitrates=12 ht_supported=1 first_ch=2412 max_power=20)
    [43178.926542] omo-skel: set interface_modes=0x4 rc=0 (no netdev is created)
    [43178.933647] omo-skel: wiphy_register() rc=0
    [43178.937827] omo-skel: registered wiphy "omo-skel": no netdev, no hardware, no data path
    insmod-rc=0

### While loaded - `iw phy` (the new phy is present, named by us)

    Wiphy omo-skel
    Wiphy phy1
    Wiphy phy0

and the sysfs class confirms it, with the vendor phys untouched:

    /sys/class/ieee80211/omo-skel -> ../../devices/virtual/ieee80211/omo-skel
    /sys/class/ieee80211/phy0     -> ../../devices/virtual/ieee80211/phy0
    /sys/class/ieee80211/phy1     -> ../../devices/virtual/ieee80211/phy1

### While loaded - `iw phy omo-skel info`

    Wiphy omo-skel
        wiphy index: 2
        max # scan SSIDs: 0
        max scan IEs length: 0 bytes
        max # sched scan SSIDs: 0
        max # match sets: 0
        Retry short limit: 7
        Retry long limit: 4
        Coverage class: 0 (up to 0m)
        Available Antennas: TX 0 RX 0
        Supported interface modes:
             * managed
        Band 3:
            Capabilities: 0x820
                HT20
                Static SM Power Save
                RX HT20 SGI
                No RX STBC
                Max AMSDU length: 7935 bytes
                No DSSS/CCK HT40
            Maximum RX AMPDU length 65535 bytes (exponent: 0x003)
            Minimum RX AMPDU time spacing: No restriction (0x00)
            HT TX/RX MCS rate indexes supported: 0-7
            Frequencies:
                * 2412 MHz [1] (20.0 dBm)
                * 2417 MHz [2] (20.0 dBm)
                * 2422 MHz [3] (20.0 dBm)
                * 2427 MHz [4] (20.0 dBm)
                * 2432 MHz [5] (20.0 dBm)
                * 2437 MHz [6] (20.0 dBm)
                * 2442 MHz [7] (20.0 dBm)
                * 2447 MHz [8] (20.0 dBm)
                * 2452 MHz [9] (20.0 dBm)
                * 2457 MHz [10] (20.0 dBm)
                * 2462 MHz [11] (20.0 dBm)
                * 2467 MHz [12] (disabled)
                * 2472 MHz [13] (disabled)
        interface combinations are not supported
        max # scan plans: 1
        max scan plan interval: -1
        max scan plan iterations: 0
        Supported extended features:

`iw dev` at this point still lists only the vendor radios' interfaces (`vap0`, `vap1`, `vap3`,
`vap8`, `vap9`, `vap11`); `omo-skel` created no netdev.

### rmmod (rc=0) - dmesg

    [43243.399369] omo-skel: wiphy_unregister() rc=0 (void)
    [43243.404327] omo-skel: wiphy_free() rc=0 (void)
    [43243.408748] omo-skel: unloaded, phy removed
    rmmod-rc=0

### After - `iw phy` and health

    Wiphy phy1
    Wiphy phy0

    -- interfaces --
        Interface vap11        type AP
        Interface vap8         type AP
        Interface vap9         type managed
        Interface vap3         type AP
        Interface vap0         type AP
        Interface vap1         type managed
    -- vendor modules --
    hi5622v100_wifi      3387392  1
    hi5622v100_plat       323584  3 hi5622v100_wifi
    -- wifiskel loaded? --
    (not loaded)
    -- taint -- 4097   (pre-existing: proprietary + out-of-tree vendor modules; no oops/warn bit)

No oops, no panic, no `WARNING:` from the module, and no reboot was needed; the vendor stack
came through unchanged.

## ABI verdict

**Partial. The kernel accepted the wiphy (`wiphy_new_nm` and `wiphy_register` both returned 0)
and the leading part of `struct wiphy` clearly matched, but `bands[]` does not: the vendor's
array base is 8 bytes lower, so our `bands[0]` write was read as the vendor's `bands[2]`.**

How the shift was measured:

1. `iw` labels bands by the nl80211 nested attribute type (`iw phy.c`:
   `printf("Band %d:\n", nl_band->nla_type + 1)`), and the kernel emits each band with its index
   as the attribute type (`nl80211.c`: `nla_nest_start_noflag(msg, band)` with `band` the loop
   index). So `Band 3` means **nl80211 band index 2**, which in 5.10.201 is
   `NL80211_BAND_60GHZ` - even though the frequencies listed are ours, 2412..2472 MHz. The
   vendor's own `phy0` prints `Band 1` for its 2.4 GHz band, confirming the label convention.
2. Our module wrote the pointer at `wiphy->bands[NL80211_BAND_2GHZ]` (index 0). The vendor read
   it at index 2, i.e. `vendor_offsetof(bands) = our_offsetof(bands) - 2*4`. On ARMv7 a wiphy
   band pointer is 4 bytes, so the vendor's `bands[]` base is **8 bytes below** the vanilla
   5.10.201 offset.
3. Everything *before* the divergence matched, so the delta is not at the start of the struct:
   `wiphy_new_nm` returned a valid wiphy, the requested name `omo-skel` was used, and our write
   to `wiphy->interface_modes` (`0x4`) was read back as `* managed`. `interface_modes` is well
   before `bands[]`, so the delta begins in the region between the two.
4. In vanilla `include/net/cfg80211.h` the only conditional field group anywhere between
   `interface_modes` and `bands[]` is `#ifdef CONFIG_PM` (`const struct wiphy_wowlan_support
   *wowlan;` + `struct cfg80211_wowlan *wowlan_config;` = two pointers = 8 bytes on ARMv7);
   every other field in that span is unconditional. An absent 8-byte wowlan pair is therefore
   the natural explanation of the delta, and the device is consistent with the vendor kernel
   running `CONFIG_PM=n`: there is no `/sys/power`, no per-device `power/` directory, and
   `pm_runtime_*` is absent from `/proc/kallsyms` (all of which are `CONFIG_PM` artifacts).
   (`pm_qos_*` appears but proves nothing - `kernel/power/qos.o` is `obj-y` - and
   `device_pm_move_to_tail` is in always-built `drivers/base/core.c`.) We could not read the
   vendor's exact config to confirm this, and our own `multi_v7_defconfig` build cannot turn
   `CONFIG_PM` off (`ARCH_ROCKCHIP` / `ARCH_TEGRA` `select PM`), so we report the divergence as
   the measured struct difference rather than as a single proven toggle.

Either way the verdict is a genuine `struct wiphy` layout divergence between the vendor's
cfg80211 and vanilla 5.10.201 - the same class of hazard phase 11 found for `struct pci_dev`,
now demonstrated on cfg80211.

Why the wiphy still registered: `wiphy_new_nm` allocates `struct cfg80211_registered_device`
using the *vendor's* layout and returns `&rdev->wiphy`, a pointer that is valid no matter what
our headers say. Only the interpretation of the object's *contents* is affected: our write to
`wiphy->bands[0]` landed where the vendor keeps `bands[2]`.

Practical consequence for a real driver: calls into exported cfg80211 entry points survive the
mismatch (they run entirely inside the vendor's code), but direct reads/writes of cfg80211 or
`net_device` struct fields are only safe when the module is compiled against the vendor's actual
cfg80211 headers/config (its SDK), not vanilla 5.10.201. The delta is consistent with the
vendor's `CONFIG_PM` state (which the stock `multi_v7_defconfig` cannot reproduce), and if it is
a vendor patch the same conclusion holds: an external module must be built against the vendor's
own cfg80211, not the vanilla tree.

## Limits, stated plainly

- **No hardware.** The module performs no PCI/MMIO access, no firmware load, no DMA; it only
  calls cfg80211. It cannot interfere with the chip.
- **No netdev, no interface, no data path.** `omo-skel` advertises one interface mode but never
  creates an interface; nothing can actually transmit. The phy exists only as a registration
  artifact.
- **Band mis-indexed by the ABI mismatch.** The frequencies, powers and HT caps are exactly
  ours (the kernel read our `ieee80211_supported_band` / `ieee80211_channel` / rate arrays
  correctly), but the kernel believes they are a 60 GHz band. Channels 12-13 show `disabled`
  because the device's regdomain is `US` (FCC), which is expected cfg80211 behaviour.
- **Only our module was loaded/unloaded.** The vendor `hi5622v100_wifi` / `hi5622v100_plat`
  and `phy0`/`phy1` were live throughout and are unchanged after the test.
- **No pstore record to report.** The ABI mismatch did not crash; there was no oops and no
  reboot.

## Reproduce

    # build + CI
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download 36840547817 -n wifiskel-ko -D /tmp/wifiskel-ko

    # device (insmod/rmmod of our module only; vendor radios untouched)
    md5sum /tmp/wifiskel-ko/wifiskel.ko          # a4c0a14f570cad369ff72e37564a4c36
    scp -O /tmp/wifiskel-ko/wifiskel.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'iw phy | grep Wiphy; insmod /tmp/wifiskel.ko; dmesg | grep omo-skel; \
      iw phy | grep Wiphy; iw phy omo-skel info; rmmod wifiskel; iw phy | grep Wiphy; iw dev'
