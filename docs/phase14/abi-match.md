# Struct-ABI match to the vendor kernel: the `CONFIG_PM` delta (phase 14 follow-up, 2026-10-01)

**Result: the phase-14 `struct wiphy` divergence is the `#ifdef CONFIG_PM` wowlan pointer pair.
Building the external module with `CONFIG_PM=n` in the prepared headers moves `wiphy->bands[]` from
offset 212 to offset 204 - exactly the 8-byte (two ARMv7 pointers) shift the vendor kernel showed -
and the skeleton's 2.4 GHz band then registers as `Band 1` instead of `Band 3`. The fix is a header
post-process on the vanilla tree, wired into the CI workflow; no vendor source is needed.**

This closes the phase-14 `wifiskel` finding
([wifiskel.md](wifiskel.md)): the vendor's `bands[]` really is 8 bytes below the vanilla
`multi_v7_defconfig` offset, and the difference is reproducible, not an unexplained vendor patch.

## The delta, measured

`lab/abiprobe` prints the offsets it was compiled with. The same source was built twice in one CI
run - once against the default `multi_v7_defconfig` headers (`CONFIG_PM=y`) and once after the
prepared headers were forced to `CONFIG_PM=n` - then loaded on the router.

    omo-abiprobe: CONFIG_PM=y CONFIG_PM_SLEEP=y
    omo-abiprobe: wiphy: sizeof=832 interface_modes=32 bands=212 perm_addr=0 hw_version=160
    omo-abiprobe: net_device: sizeof=1344 name=0 state=40 ifindex=168 netdev_ops=288

    omo-abiprobe: CONFIG_PM=n CONFIG_PM_SLEEP=n
    omo-abiprobe: wiphy: sizeof=640 interface_modes=32 bands=204 perm_addr=0 hw_version=160
    omo-abiprobe: net_device: sizeof=1216 name=0 state=40 ifindex=168 netdev_ops=288

| field | PM=y (vanilla) | PM=n (vendor-like) | delta |
| --- | ---: | ---: | ---: |
| `offsetof(struct wiphy, perm_addr)` | 0 | 0 | 0 |
| `offsetof(struct wiphy, interface_modes)` | 32 | 32 | 0 |
| `offsetof(struct wiphy, hw_version)` | 160 | 160 | 0 |
| `offsetof(struct wiphy, bands)` | **212** | **204** | **-8** |
| `sizeof(struct wiphy)` | 832 | 640 | -192 |
| `offsetof(struct net_device, name)` | 0 | 0 | 0 |
| `offsetof(struct net_device, state)` | 40 | 40 | 0 |
| `offsetof(struct net_device, ifindex)` | 168 | 168 | 0 |
| `offsetof(struct net_device, netdev_ops)` | 288 | 288 | 0 |
| `sizeof(struct net_device)` | 1344 | 1216 | -128 |

Everything up to `hw_version` is identical under both configs, `bands[]` moves by exactly 8 bytes,
and `interface_modes` does not move. That is the signature of the single conditional field group in
that span of `include/net/cfg80211.h` (5.10.201):

    char fw_version[ETHTOOL_FWVERS_LEN];
    u32 hw_version;

    #ifdef CONFIG_PM
        const struct wiphy_wowlan_support *wowlan;
        struct cfg80211_wowlan *wowlan_config;
    #endif

    u16 max_remain_on_channel_duration;
    ...
    struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];

Two 4-byte pointers = 8 bytes. With the vendor kernel behaving like `CONFIG_PM=n`, its `bands[]`
base sits 8 bytes below ours, so our `bands[0]` write was read as the vendor's `bands[2]` - the
60 GHz slot that `iw` printed as `Band 3`.

`sizeof(struct wiphy)` falls by 192 bytes, not 8, because `struct wiphy` also embeds
`struct device dev` (which contains a `CONFIG_PM`-gated `struct dev_pm_info`); that drop is
*downstream* of `bands[]` and does not affect the band pointer. The kernel allocates the wiphy with
its own (vendor) layout, so our `sizeof` is cosmetic; only the offsets we write matter.

### The exact config symbols

- **`CONFIG_PM`** defines the two-pointer `wowlan` / `wowlan_config` group in
  `include/net/cfg80211.h`; this is the symbol whose state changes `offsetof(struct wiphy, bands)`.
- **`CONFIG_PM_SLEEP`** is the companion switch (comment both to present a consistent `PM=n`).
- The vendor kernel's user-visible behaviour matches `PM=n`: no `/sys/power`, no per-device
  `power/` directory, no `pm_runtime_*` in `/proc/kallsyms`.

`scripts/config --disable PM` is **not enough**: `multi_v7_defconfig` sets `CONFIG_ARCH_ROCKCHIP=y`
and `CONFIG_ARCH_TEGRA=y`, and both `select PM` (`arch/arm/mach-rockchip/Kconfig:21`,
`arch/arm/mach-tegra/Kconfig:13`), so `make olddefconfig` turns `CONFIG_PM` back on. The CI log shows
exactly that, then the fallback:

    == CONFIG_PM state before (autoconf.h) ==
    #define CONFIG_PM 1
    #define CONFIG_PM_SLEEP 1
    == try scripts/config --disable PM + make olddefconfig ==
    CONFIG_PM stayed =y in .config (ARCH_ROCKCHIP / ARCH_TEGRA 'select PM')
    == forcing it off in include/generated/autoconf.h instead ==
    == CONFIG_PM state after (autoconf.h) ==
    661:/* #undef CONFIG_PM */
    1575:/* #undef CONFIG_PM_SLEEP */

## Build recipe (wired into the workflow)

`lab/abi-pm-off.sh <kernel-tree>` runs after the tree is configured and `modules_prepare`d, and
before the external module is built:

1. print the `CONFIG_PM*` state in `include/generated/autoconf.h`,
2. run `scripts/config --disable PM --disable PM_SLEEP` and `make olddefconfig`,
3. if `CONFIG_PM` is still `=y` (normal for `multi_v7_defconfig`), comment `CONFIG_PM` and
   `CONFIG_PM_SLEEP` out of `include/generated/autoconf.h` and `touch` it so the external-module
   build cannot regenerate it,
4. fail if `CONFIG_PM` is still active.

Only the headers matter: the kernel is not rebuilt, and the module links against the *vendor's*
exported symbols at load time. `.github/workflows/build-load-test-module.yml` was extended with:

- `build wifiskel + abiprobe (PM on)` - stages `wifiskel-pm-on.ko`, `abiprobe-pm-on.ko`,
- `force CONFIG_PM off in prepared headers (match vendor)` - runs `lab/abi-pm-off.sh`,
- `build wifiskel + abiprobe (PM off)` - rebuilds and stages the `-pm-off` binaries; the
  `wifiskel-ko` artifact therefore now carries the fixed (PM-off) module,
- an `abi-artifacts` artifact with all four `.ko` files.

Vermagic is unchanged and still matches the vendor: `5.10.201 SMP mod_unload ARMv7 `.

## CI

- Workflow `build-load-test-module.yml`, run **`36841917105`** (green) -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36841917105
  - commit `31d59a9` ("phase14: abiprobe + PM-off recipe - force CONFIG_PM=n so struct wiphy
    matches the vendor")
  - artifact `abi-artifacts`:
    - `abiprobe-pm-on.ko` md5 `6a13e71c6f9e44bf790d4c46b40b800c`
    - `abiprobe-pm-off.ko` md5 `ecef855e6fc7b7de5fa8fc68daf8ce77`
    - `wifiskel-pm-on.ko` md5 `e4ee154cc1f60199a43af5ae7eb3e97f`
    - `wifiskel-pm-off.ko` md5 `e70fed5a0393fd642a759c24b0d11550`
  - artifact `wifiskel-ko` md5 `e70fed5a0393fd642a759c24b0d11550` (identical to `wifiskel-pm-off.ko`)
- For comparison, the original phase-14 run was `36840547817` (artifact md5 `a4c0a14f...`), which
  produced the `Band 3` result.
- Source: `lab/abiprobe/{abiprobe.c,Makefile}`, `lab/abi-pm-off.sh`, `lab/wifiskel/wifiskel.c`.

## Device evidence (WR3000 V2.0, `root@192.168.10.1`)

Raw captures: `build/register-dumps/wifiskel-pmoff/` (`00_abiprobe.txt`, `10_pmon.txt`,
`20_pmoff.txt`, `30_after_health.txt`). No vendor phy was touched; only our modules were
`insmod`/`rmmod`'d.

### Before - PM-on build, `iw phy omo-skel info`

    Wiphy omo-skel
        wiphy index: 4
        ...
        Band 3:
            ...
            Frequencies:
                * 2412 MHz [1] (20.0 dBm)
                ...
                * 2472 MHz [13] (disabled)

`Band 3` = nl80211 band index 2 = `NL80211_BAND_60GHZ`: the same mis-indexing the original report
found. The frequencies are ours (2412..2472 MHz).

### After - PM-off build, `iw phy omo-skel info`

    Wiphy omo-skel
        wiphy index: 5
        ...
        Supported interface modes:
             * managed
        Band 1:
            Capabilities: 0x820
                HT20
                ...
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

`Band 1` is the 2.4 GHz label the vendor's own radio shows (`iw phy phy0 info` prints exactly one
`Band 1:` for its 2.4 GHz band), and all 13 of our channels are present. The module dmesg confirms
the offset it was compiled with:

    [43945.026067] omo-skel: init: ... offsetof(wiphy.interface_modes)=32 offsetof(wiphy.bands)=204
    [43945.050025] omo-skel: wiphy_new_nm(...) rc=0
    [43945.075417] omo-skel: wiphy_register() rc=0

`iw phy` while loaded shows the new phy alongside the vendor's, and `rmmod` cleans up:

    Wiphy omo-skel
    Wiphy phy1
    Wiphy phy0
    ...
    rmmod-rc=0
    Wiphy phy1
    Wiphy phy0

### After-health

- `iw phy`: `Wiphy phy1`, `Wiphy phy0` only; `iw dev` shows the vendor `vap*` interfaces as before.
- Vendor modules still loaded: `hi5622v100_wifi` (3387392, ref 1), `hi5622v100_plat`, `hi_kpie`,
  `ksecurec`.
- Taint `4097` (pre-existing: proprietary + out-of-tree vendor modules; no oops/warn bit).
- `dmesg | grep -iE "oops|panic|WARNING:|BUG:|Call trace"`: nothing. No reboot was needed.
  (The `wiphy index` climbs across load/unload cycles; that is a cosmetic cfg80211 counter, not
  state left behind.)

## Limits, stated plainly

- **Only `struct wiphy` (and `struct net_device`'s base fields) is shown to match.** The PM fix
  realigns `bands[]` because PM is the only conditional group in that span, but it does not prove
  the vendor has no *other* config differences. Fields *after* `bands[]` in `struct wiphy` -
  `regd`, the embedded `struct device dev`, `wdev_list`, `_net`, vendor/coalesce pointers, and
  everything past them - are not exercised by an offset write and remain unverified. The
  192-byte `sizeof` drop is dominated by `struct device`/`dev_pm_info`, so a driver that reads
  `wiphy->dev` offsets must not assume PM-off vanilla equals the vendor either.
- **`struct net_device` is not proven to match.** Its base fields (`name`, `state`, `ifindex`,
  `netdev_ops`) happen to sit before any PM-gated member and are identical under PM on/off, but
  `net_device` carries dozens of `CONFIG_*`/`IS_ENABLED` fields further down and an external module
  that pokes them is exposed exactly as `struct pci_dev` was in phase 11.
- **The method generalises; the vendor's own SDK headers are the real answer.** To find further
  deltas, extend `lab/abiprobe` with more `offsetof`/`sizeof` lines for any struct a driver touches
  (`net_device`, `sk_buff`, `pci_dev`, `cfg80211_registered_device`, ...), build it under candidate
  configs (PM off, and later whatever else the vendor config reveals), and diff the dmesg lines
  against the device's behaviour. A stronger check is to attach `pahole` to a `-g` built module.
  None of this replaces building against the vendor's exported headers, which we do not have.
- **No hardware.** `abiprobe` prints compile-time constants and returns; `wifiskel` only calls
  cfg80211. No PCI/MMIO, no firmware, no netdev, no data path. The vendor radios were live and
  unchanged throughout.
- **Only our modules were loaded/unloaded.** Nothing was written to flash, calibration, or the
  vendor phys.

## Reproduce

    # build + download
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download 36841917105 -n abi-artifacts -D /tmp/abi-artifacts

    # device: probe both configs, then the acceptance test
    scp -O /tmp/abi-artifacts/*.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/abiprobe-pm-on.ko;  dmesg | grep omo-abiprobe; rmmod abiprobe; \
      insmod /tmp/abiprobe-pm-off.ko; dmesg | grep omo-abiprobe; rmmod abiprobe; \
      insmod /tmp/wifiskel-pm-on.ko;  iw phy omo-skel info | grep "Band"; rmmod wifiskel; \
      insmod /tmp/wifiskel-pm-off.ko; iw phy | grep Wiphy; iw phy omo-skel info; rmmod wifiskel; \
      iw phy | grep Wiphy'

Note: the root SSH password on this test device is set locally (see the phase-13 reports for the
`SSH_ASKPASS` helper); it is not stored in this repository.
