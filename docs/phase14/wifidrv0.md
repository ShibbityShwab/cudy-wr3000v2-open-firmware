# wifidrv0: a cfg80211 wiphy **and** a real, bringable wireless netdev (phase 14, 2026-10-01)

**Result: the module registers the wiphy `omo-drv0` (2.4 GHz, `Band 1`) and creates one real
managed netdev, `omowl0`. On the device `iw dev` lists it, `ip link set omowl0 up`/`down`
succeed, `iw dev omowl0 info` reports `type managed`, `iw dev omowl0 del` removes it,
`iw phy omo-drv0 interface add omowl0 type managed` re-creates it through the nl80211 path, and
`rmmod` removes phy + interface. The vendor `phy0`/`phy1` and all `vap*` interfaces are
unchanged throughout.**

The first attempt oopsed when `omowl0` was brought up (IPv6 MLD hit a missing
`ndo_start_xmit`); the watchdog rebooted the box, pstore captured the oops, and the vendor stack
came back healthy. A drop stub fixed it. Both runs are documented below.

This closes the phase-14 `wifiskel` limit ("no netdev, no interface"): the same wiphy now has a
netdev that the network stack can actually open and close.

## The trap this task hid: `struct net_device` does **not** match our headers

The task asked to follow the kernel's usual cfg80211 example pattern (`netdev_priv = wdev`,
`wdev->wiphy`, `wdev->iftype`, `dev->ieee80211_ptr = wdev`, `dev->netdev_ops = ...`). That pattern
assumes our compiled `struct net_device` offsets equal the vendor kernel's. They do not.

The running device (WR3000 V2.0, **fw 2.5.24**) has a *backported* cfg80211:

    # modinfo cfg80211 | head -3
    version:        backported from Linux (v5.15.92-0-ge515b9902f5f) using backports v5.15.92-1-0-gdfe0f60c

and its `struct net_device` carries the `CONFIG_WIRELESS_EXT` pair, so several offsets differ from
our `multi_v7_defconfig` (PM-off) build. Measured from the **device's own** `abiprobe-pm-off.ko`
and from the **device's own** `cfg80211.ko` / `mac80211.ko`:

| field | our PM-off 5.10.201 | vendor (fw 2.5.24) | delta |
| --- | ---: | ---: | ---: |
| `offsetof(net_device, netdev_ops)` | 288 (`0x120`) | **296** (`0x128`) | +8 |
| `offsetof(net_device, ieee80211_ptr)` | 452 (`0x1c4`) | **496** (`0x1f0`) | +44 |
| `ALIGN(sizeof(net_device), 32)` (= `netdev_priv`) | 1216 (`0x4c0`) | **1344** (`0x540`) | +128 |
| `offsetof(wiphy, interface_modes)` | 32 | 32 (`iw` reads `0x4`) | 0 |
| `offsetof(wiphy, bands)` | 204 | 204 (`Band 1`) | 0 |

The +8 on `netdev_ops` is `CONFIG_WIRELESS_EXT`; the extra +36 up to `ieee80211_ptr` is further
config divergence (so simply forcing `WIRELESS_EXT=y` in the prepared headers would **not** have
been enough). `netdev_priv` differs by 128 bytes, which is why `netdev_priv()` cannot be used.

Writing `dev->netdev_ops` through the compiled offset (288) would leave the vendor's real
`netdev_ops` (296) NULL, and `register_netdevice()` dereferences it immediately
(`net/core/dev.c: if (dev->netdev_ops->ndo_init)`), i.e. an oops on every load.

### How the vendor offsets were measured (not guessed)

`lab/wifidrv0` addresses exactly the three fields it owns through the vendor's measured offsets:

- `mac80211.ko` `ieee80211_if_setup+0x28`: `str r1,[r4,#0x128]` — `dev->netdev_ops = &ieee80211_dataif_ops` (relocation at `0x1af58`→`.LANCHOR0`); also `strb r0,[r4,#0x31c]` (`needs_free_netdev`) and `str r2,[r4,#0x320]` (`priv_destructor`).
- `mac80211.ko` `ieee80211_if_add` right after `alloc_netdev_mqs`: `add r4,r5,#0x540` (`sdata = netdev_priv(ndev)`) then `add r3,r4,#8; str r3,[r5,#0x1f0]` (`ndev->ieee80211_ptr = &sdata->wdev`).
- `cfg80211.ko` `cfg80211_netdev_notifier_call+0x1c`: `ldr r4,[r8,#0x1f0]` — the notifier reads `dev->ieee80211_ptr` at `0x1f0`.

The three constants live at the top of `lab/wifidrv0/wifidrv0.c` and are logged against the
compiled values at init. The module keeps the `wireless_dev` inside the netdev private area (at
the vendor's `netdev_priv` base, `0x540`) and sizes the private area at 4096 bytes so the vendor's
(v5.15.92, larger) `wireless_dev` fits; it writes no other `net_device` field by offset — everything
else goes through vendor code (`ether_setup`, `eth_mac_addr`, `register_netdevice`,
`unregister_netdevice`, `free_netdev`).

The disassembly source modules were pulled from the device into
`build/register-dumps/wifidrv0/dev-modules/` (`cfg80211.ko` md5 `afa0c8b3…`, `mac80211.ko` md5
`381c61a2…`, `hi5622v100_wifi.ko` md5 `4737fcb2…`; these are the exact files the running kernel
loaded).

## CI

Workflow `build-load-test-module.yml`, two green runs:

- **`36843947828`** (the tested, fixed module) — commit `fef5cf2`
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36843947828
  - step `build wifidrv0 (PM off)` after `lab/abi-pm-off.sh` (`#undef CONFIG_PM` /
    `#undef CONFIG_PM_SLEEP` in `include/generated/autoconf.h`)
  - artifact `wifidrv0-ko`: `wifidrv0.ko` md5 **`acb6108dfe0d7c6412d7e2d9350566e7`**
  - artifact `abi-artifacts`: `wifidrv0-pm-off.ko` md5 `acb6108dfe0d7c6412d7e2d9350566e7` (identical),
    `abiprobe-pm-off.ko` md5 `5adc6f94971907961fb0aebd97b06723`
  - vermagic: `vermagic=5.10.201 SMP mod_unload ARMv7 `
- **`36843547740`** (first build, module without the `ndo_start_xmit` stub) — commit `18115c8`, green;
  `wifidrv0.ko` md5 `4c5a21e30407f35b72dc75e1d3a76aef`. This is the binary that oopsed; kept for
  the record. See "First attempt" below.

Source: `lab/wifidrv0/{wifidrv0.c,Makefile}` (`obj-m := wifidrv0.o`). `lab/abiprobe/abiprobe.c`
was extended to print the extra `net_device` offsets used in the table above.

## What the module does

On load:

1. logs the compiled vs vendor `net_device` offsets and the compiled `wiphy` offsets,
2. `wiphy_new_nm(&omo_ops, 0, "omo-drv0")`,
3. fills `wiphy->bands[NL80211_BAND_2GHZ]` with channels 1..13 (2412..2472 MHz), 12 rates, minimal
   HT20 cap (same band as `wifiskel`), sets `interface_modes = BIT(NL80211_IFTYPE_STATION)` and a
   fixed locally-administered `perm_addr` (`02:00:6f:6d:6f:30`),
4. `wiphy_register()`,
5. (with RTNL held) calls its own `add_virtual_intf` for `"omowl0"`, so one managed interface exists
   automatically at load.

`cfg80211_ops` implements `add_virtual_intf` (alloc netdev, place `wireless_dev` at the vendor's
`netdev_priv` offset, `wiphy`/`iftype`, set `ieee80211_ptr`, set a MAC via `eth_mac_addr`,
`register_netdevice`), `del_virtual_intf` (`unregister_netdevice` + `free_netdev`),
`change_virtual_intf` (accept only a same-type no-op, else `-EOPNOTSUPP`), plus `ndo_open`/
`ndo_stop` logging stubs and a `ndo_start_xmit` drop stub. Every step logs its return code.

On unload: delete the interface first (so `wiphy_unregister()`'s
`WARN_ON(!list_empty(&wiphy->wdev_list))` cannot fire), then `wiphy_unregister()` and
`wiphy_free()`.

## Device lifecycle evidence (fw 2.5.24, `root@192.168.10.1`)

Raw captures: `build/register-dumps/wifidrv0/` (`00_before.txt`, `10_insmod2.txt`,
`11_up2.txt`, `12_down_del2.txt`, `13_iw_add.txt`, `14_change_type.txt`, `15_rmmod_after.txt`,
`16_band1_info.txt`, `17_after_health.txt`, `18_abiprobe_pmoff.txt`; first-attempt files
`02_insmod.txt`..`06_after_crash_health.txt`; `pstore/`).

### Before - `iw phy`, `iw dev`

    Wiphy phy1
    Wiphy phy0
    -- interfaces --
        vap11 (phy#1, AP, 5240), vap8 (phy#1, AP), vap9 (phy#1, managed),
        vap3 (phy#0, AP, 2442), vap0 (phy#0, AP, 2442), vap1 (phy#0, managed)
    taint 4097 (pre-existing: proprietary + out-of-tree vendor modules)

### insmod (rc=0)

    [  255.857179] omo-drv0: init: compiled offsets: netdev_ops=288 ieee80211_ptr=452 ALIGN(sizeof(net_device),32)=1216; vendor offsets: 296/496/1344
    [  255.869965] omo-drv0: init: offsetof(wiphy.interface_modes)=32 offsetof(wiphy.bands)=204
    [  255.878039] omo-drv0: wiphy_new_nm(ops, sizeof_priv=0, name="omo-drv0") rc=0 ptr=c47e89c0
    [  255.886216] omo-drv0: set bands[2GHZ] rc=0 (n_channels=13 n_bitrates=12 ht_supported=1 first_ch=2412 max_power=20)
    [  255.896577] omo-drv0: set interface_modes=0x4 perm_addr=02:00:6f:6d:6f:30 rc=0
    [  255.903971] omo-drv0: wiphy_register() rc=0
    [  255.908160] omo-drv0: eth_mac_addr(rc=0)
    [  255.923836] omo-drv0: add_virtual_intf name=omowl0 type=2 ifindex=20 rc=0
    [  255.930664] omo-drv0: ready: phy "omo-drv0", interface "omowl0" (no hardware, no data path)
    insmod-rc=0

The init line is the ABI proof: our compiled `ieee80211_ptr` is 452 but the vendor's is 496.

### While loaded - `iw dev`

    phy#2
        Interface omowl0
            ifindex 20
            wdev 0x200000001
            addr 02:00:6f:6d:6f:30
            type managed
    phy#1 ... (vendor vap11/vap8/vap9)
    phy#0 ... (vendor vap3/vap0/vap1)

    # iw dev omowl0 info
    Interface omowl0
        ifindex 20
        wdev 0x200000001
        addr 02:00:6f:6d:6f:30
        type managed
        wiphy 2

### While loaded - `iw phy omo-drv0 info`

    Wiphy omo-drv0
        wiphy index: 3
        ...
        Supported interface modes:
             * managed
        Band 1:
            Capabilities: 0x820
                HT20
                ...
            Frequencies:
                * 2412 MHz [1] (20.0 dBm)
                ...
                * 2462 MHz [11] (20.0 dBm)
                * 2467 MHz [12] (disabled)
                * 2472 MHz [13] (disabled)

`Band 1`, managed-only, 13 channels (12-13 `disabled` because the device regdomain is US/FCC).
The interface itself does not report a channel because it is not associated/AP; `iw dev` shows the
type (`managed`) as required.

### up / info / down

    # ip link set omowl0 up ; echo up-rc=$?
    up-rc=0
    [  260.610953] omo-drv0: ndo_open omowl0 rc=0 (no data path)
    20: omowl0: <BROADCAST,MULTICAST,UP,LOWER_UP> ... state UNKNOWN ... link/ether 02:00:6f:6d:6f:30 ...

    # iw dev omowl0 info   (type still managed, wiphy 3 in this capture)

    # ip link set omowl0 down ; echo down-rc=$?
    down-rc=0
    [  266.302598] omo-drv0: ndo_stop omowl0 rc=0 (no data path)

### remove via `iw` (rc=0), then re-add via `iw` (rc=0)

    # iw dev omowl0 del ; echo del-rc=$?
    del-rc=0
    [  267.328212] omo-drv0: del_virtual_intf name=omowl0
    [  267.374360] omo-drv0: del_virtual_intf rc=0 (netdev removed and freed)
    # iw dev | grep -E 'phy#|Interface'
    phy#1 ... vap11/vap8/vap9
    phy#0 ... vap3/vap0/vap1
    # iw phy | grep Wiphy            # our phy still registered
    Wiphy omo-drv0
    Wiphy phy1
    Wiphy phy0

    # iw phy omo-drv0 interface add omowl0 type managed ; echo add-rc=$?
    add-rc=0
    [  271.751229] omo-drv0: add_virtual_intf name=omowl0 type=2 ifindex=21 rc=0
    # iw dev omowl0 info
    Interface omowl0
        ifindex 21
        addr 02:00:6f:6d:6f:30
        type managed
        wiphy 2

### change_virtual_intf

    # iw dev omowl0 set type managed ; echo rc=$?
    rc=0                      # nl80211 short-circuits a same-type change, so our callback is not called
    # iw dev omowl0 set type monitor ; echo rc=$?
    command failed: Not supported (-95)
                              # cfg80211 rejects 'monitor' before the driver because interface_modes
                              # advertises managed only; our callback's non-no-op check is a second guard

`change_virtual_intf` is implemented (accept `type == wdev->iftype`, else `-EOPNOTSUPP`) but is not
reachable through `iw` with a single allowed interface type: nl80211 skips it for a no-op and
cfg80211 filters disallowed types first. It remains a guard against a future type set.

### rmmod (rc=0) and after

    # iw dev omowl0 del ; rmmod wifidrv0
    [  283.143313] omo-drv0: del_virtual_intf name=omowl0
    [  283.194018] omo-drv0: del_virtual_intf rc=0 (netdev removed and freed)
    [  284.279429] omo-drv0: wiphy_unregister() rc=0 (void)
    [  284.284399] omo-drv0: wiphy_free() rc=0 (void)
    [  284.288891] omo-drv0: unloaded, phy and interface removed
    rmmod-rc=0
    # iw phy | grep Wiphy
    Wiphy phy1
    Wiphy phy0
    # iw dev | grep -E 'phy#|Interface'
    phy#1 ... vap11/vap8/vap9
    phy#0 ... vap3/vap0/vap1
    # lsmod | grep wifidrv0
    (not loaded)
    # cat /proc/sys/kernel/tainted
    4097
    # dmesg | grep -iE 'oops|panic|WARNING:|BUG:|Call trace|Unable to handle|Internal error'
    (none)

The vendor radio (`hi5622v100_wifi` 3387392, ref 1; `hi5622v100_plat`, `hi_kpie`, `ksecurec`) and
`phy0`/`phy1` are live and unchanged. No oops in the fixed run; no reboot.

## First attempt: oops + recovery (the missing `ndo_start_xmit`)

The first binary (`4c5a21e3…`) registered the phy and created `omowl0`, and `ip link set omowl0 up`
returned 0, but ~40 ms later the box oopsed and the watchdog rebooted it. pstore captured it
(`build/register-dumps/wifidrv0/pstore/dmesg-pstore_blk-3`, md5 `c2c4bb46…`):

    [44890.121202] omo-drv0: ndo_open omowl0 rc=0 (no data path)
    [44890.126796] swa_netdevice_event 215:event:1, name:omowl0
    [44890.159345] 8<--- cut here ---
    [44890.162399] Unable to handle kernel NULL pointer dereference at virtual address 00000000
    [44890.176738] Internal error: Oops: 80000005 [#1] SMP ARM
    [44890.435659] LR is at dev_hard_start_xmit+0xf8/0x254
    [44890.433131] PC is at 0x0
    ...
    [44890.754370] (__dev_queue_xmit) from (ip6_finish_output2+0x1c8/0x8c8)
    [44890.771179] (mld_sendpack) from (mld_ifc_timer_expire+0x1e8/0x470)

Cause: bringing the netdev up made the stack add an IPv6 link-local address (`addrgenmode eui64`),
and the MLD timer queued a multicast report; `dev_hard_start_xmit()` then called our NULL
`ndo_start_xmit`. It was not a layout bug — `ndo_open` had already been called through the correct
vendor offset.

Recovery: the device came back on its own after ~20 s; `iw phy`, `iw dev`, `lsmod`, taint `4097`
and the vendor modules were all verified healthy (`06_after_crash_health.txt`). Fix:
`ndo_start_xmit` drops every packet (`kfree_skb`, `NETDEV_TX_OK`), no per-packet logging. All
evidence above is from the rebuilt, fixed module.

## Limits, stated plainly

- **No hardware, no data path.** No PCI/MMIO/DMA/firmware. `ndo_start_xmit` drops every packet;
  nothing is transmitted. This is a registration/lifecycle test, not a radio.
- **No scan.** `max # scan SSIDs: 0`; the module implements no scan ops.
- **`struct net_device` is not config-matched, it is offset-matched.** The module is built against
  vanilla 5.10.201 PM-off headers and hardcodes the three `net_device` offsets measured from the
  *device's own* modules (fw 2.5.24). A different firmware/build could move them; the init log
  prints both sets so a mismatch is visible. Only the fields the module owns are addressed this
  way; `struct wiphy` is handled by the PM-off recipe (verified `Band 1`).
- **Single interface, managed only, fixed MAC.** `add_virtual_intf` allows one `STATION` interface
  and returns `-EBUSY` for a second; the MAC is a fixed locally-administered address.
- **`change_virtual_intf` is a guard, not a feature.** With one advertised interface type it cannot
  be reached through `iw` (see above).
- **Channels 12-13 show `disabled`.** The device regdomain is US (FCC); that is expected cfg80211
  behaviour, not a module limit.
- **One oops happened during development** (first attempt only). It is captured in pstore and the
  vendor stack was verified afterwards. The fixed binary ran the full lifecycle with no oops and no
  reboot.

## Reproduce

    # build + download the tested module
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download 36843947828 -n wifidrv0-ko -D /tmp/wifidrv0-ko
    md5sum /tmp/wifidrv0-ko/wifidrv0.ko        # acb6108dfe0d7c6412d7e2d9350566e7

    # device: load, exercise, remove (our module only; vendor stack untouched)
    scp -O /tmp/wifidrv0-ko/wifidrv0.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'md5sum /tmp/wifidrv0.ko; insmod /tmp/wifidrv0.ko; iw phy | grep Wiphy; \
      iw dev; iw phy omo-drv0 info; ip link set omowl0 up; iw dev omowl0 info; \
      ip link set omowl0 down; iw dev omowl0 del; iw phy omo-drv0 interface add omowl0 type managed; \
      iw dev omowl0 del; rmmod wifidrv0; iw phy | grep Wiphy; iw dev; lsmod | grep wifidrv0'

Note: the root SSH password on this test device is set locally (see the phase-13 reports for the
`SSH_ASKPASS` helper); it is not stored in this repository.

## Provenance of the vendor offset table

The offset values come from disassembling the device's own modules (pulled to
`build/register-dumps/wifidrv0/dev-modules/` and disassembled with `pyelftools` + `capstone`, ARM
mode):

    cfg80211.ko  cfg80211_netdev_notifier_call   ldr r4,[r8,#0x1f0]      -> ieee80211_ptr = 0x1f0
    mac80211.ko  ieee80211_if_setup              str r1,[r4,#0x128]       -> netdev_ops    = 0x128
    mac80211.ko  ieee80211_if_add                add r4,r5,#0x540         -> netdev_priv   = 0x540
                                                 str r3(=r4+8),[r5,#0x1f0]

and our side from the extended `abiprobe-pm-off.ko` run on the device
(`18_abiprobe_pmoff.txt`):

    omo-abiprobe: net_device: sizeof=1216 name=0 state=40 ifindex=168 netdev_ops=288
    omo-abiprobe: net_device: ieee80211_ptr=452 dev_addr=460 addr_len=375 ALIGN(sizeof,NETDEV_ALIGN)=1216 sizeof(wireless_dev)=280
