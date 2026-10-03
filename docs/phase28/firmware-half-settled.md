# The firmware half: the last unblocked step is a no-op, measured (phase 28a, 2026-10-03)

Phase 26 laid out three strategies for "a custom, current OpenWrt firmware" and concluded that a wholesale
modern rootfs would cost the vendor app layer, the Wi-Fi calibration and the device's own flashing path.
The one strategy that needed **no third party and no risky flash** was the curated in-place upgrade
("keep the vendor base, upgrade what is safe"). This phase tests whether that is actually possible.

## The measurement

```
installed packages:              433
upgradable within 22.03.6:         0
```

**There is nothing to upgrade in place.** The vendor's image is built from the OpenWrt 22.03.6 feeds
(`downloads.openwrt.org/releases/22.03.6/packages/arm_cortex-a9/*`, confirmed in phase 26a) and
**every installed package is already at the newest version those feeds carry.** `opkg list-upgradable`
returns zero, and that holds after a feed refresh.

So the curated-upgrade strategy is not "modest gain" - it is **no gain**. The only thing "newer" than the
installed set is a **different major release** (23.05/24.10/25.12), and reaching any of those means
replacing the base, which is the rootfs-level operation phase 26c showed is not a safe swap.

## What that leaves, cleanly

| strategy | status, by measurement |
| --- | --- |
| A. curated in-place upgrade | **closed** - nothing is upgradable; the base is already current |
| B. full modern rootfs on the vendor kernel | **permitted technically, and the user has declined it** (2026-10-03: "keep reversing the Wi-Fi") |
| C. from-source build | **needs the vendor's source** (`docs/phase25/OPEN-SOURCE-REQUEST.md`, written and ready) |

That is the complete map of the firmware half. **Every path is now either measured closed, declined, or
waiting on the vendor** - which is why the deliverable's firmware section can be called settled rather
than pending.

## A real defect found on the way (cosmetic, but worth recording)

`opkg` emits a long run of errors:

```
pkg_get_installed_files: Failed to open //usr/lib/opkg/info/libc.list: No such file or directory
   (and the same for libuci-lua, iwinfo, kmod-hi_flash, luci-app-arpbind, ... )
```

`/usr/lib/opkg/info/` holds 160 `.control` files, but the corresponding `.list` files (the per-package
file manifests) are **absent** for a subset of packages, and `libc` reports `Version: (null)-4`. This is a
footprint-reduction side effect of the vendor's image build - it does not affect running services, and the
deployed `omo-minimal-0.3` is unaffected.

**Why it matters anyway:** it makes `opkg` output noisy and unreliable for anything manifest-derived, so a
future reader should not mistake these errors for corruption. The router is healthy by every functional
check (2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands, the injected SSH lane serving).

