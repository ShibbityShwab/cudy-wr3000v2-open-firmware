# wifi-image-3 (task-196) - IMAGE #3: the 5 GHz chain + 802.11ax image

**Status: BUILT + VERIFIED host-side** (verifier **129 passed / 0 failed**;
selftest **102 passed / 0 failed**; **three consecutive bakes byte-identical**)
- **not flashed, not booted** by this lane. The Lead owns the fire.

IMAGE #3 is the full sysupgrade container: the **verified image #2** (kernel
slice **UNCHANGED**) + the bench-proven **wifi-image-update** userspace fixes
(the 5 GHz regdomain `cfg80211.ko` + the **802.11ax hostapd**) + **the IMAGE #3
driver module** (the 5 GHz F2 chain **+** `he_caps` **+** the BA skeleton **+**
the image #2 RX watermark) + the image #3 text policy payload (loader/uci/
modprobe/hostapd fixtures).

## Headline identities (see HASHES.txt)

    container   7,843,840 B  sha256 4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53
    kernel      3,372,638 B  sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED; slot 1794a531...)
    rootfs UBI  4,456,448 B  sha256 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3
    squashfs    4,009,984 B  sha256 ecbba4f97300607cd41614ab09c9aa6378bcea215e8de37f4c3097b860a0bfaf
    module inst.  358,544 B  sha256 a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed  (64 params)
    module act.   379,092 B  sha256 545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
    cfg80211.ko 2,317,952 B  sha256 bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91  (regdomain fix)
    hostapd     1,618,388 B  sha256 fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6  (802.11ax build)

## What changed vs image #2 (and vs the wifi-image-update rootfs-only UBI)

| item | image #2 / update | IMAGE #3 |
| --- | --- | --- |
| kernel slice | 13e81214..., slot 1794a531... | **UNCHANGED** |
| rootfs | image #2 rootfs -> update: cfg80211 + ax hostapd | + the image #3 module + the image #3 text policy + 2 hostapd fixtures + operator notes |
| driver module | image #2: 347,388 B d2c608e7..., 57 params (assoc tree + RX watermark) | **64 params**: image #2's assoc tree + RX watermark + the F2 5 GHz chain (`band5g`, `vap5g`, `ap_chan_wait_ms`, `desc_table`, `dfs_mode`, the field-B fix) + `he_caps` (HE/ax advertisement) + `ba_enable` (fail-closed BA skeleton) |
| loader/uci/modprobe/init | image #2 policy (57-param surface) | image #3 policy: the 7 new parameters are passed, **verified**, and refuse the 5 GHz gates without `band5g=1`; all new gates ship **0 (fail-closed)** |
| hostapd fixtures | 2.4 GHz only | + `hostapd-open-ax.conf` (2.4 GHz HE, proven class) + `hostapd-open-5g40.conf` (the F2 ch40 AP shape) |

The **kernel is not rebuilt**: only the rootfs member changes - a rootfs-only
flash writes mtd14 and leaves kernelb/mtd12 untouched (FLASH-FEASIBILITY.md).

## The module decision (why the merged 64-param module)

The newest lane artifacts are on three separate branches of the same
association tree; picking one alone would regress a bench-proven feature
(no 5 GHz chain, or no RX watermark, or no HE advertisement). IMAGE #3 therefore
carries the **merge of the three**: the F2 5 GHz chain tip + the image #2 RX
watermark + the `he_caps` HE advertisement + the BA skeleton. It was built
host-side from the committed patches, 0 warnings, and validated structurally:
**0 symbols from the F2 module are lost, 182/182 5 GHz/beacon symbols keep
identical sizes, 986 symbols identical**, 50 new symbols = the added features.
See MODULE-INVENTORY.md for the full candidate table and REPORT.md for the
evidence; BUILD-NOTES.md has the exact build chain.

**Honesty**: the F2 module (5 GHz RF programmed), the HE module (HE AP on air)
and the image #2 module (association/RX accepted) are each bench-proven; the
exact 64-param combination is **not** fired. Every added feature is
fail-closed at load and inert until a fire enables it.

## Verification (host-side)

    ./tools/selftest.sh        # 102 passed, 0 failed, 0 skipped (no artifacts needed)
    ./tools/bake-image-3.sh    # 3 identical bakes; builds + verifies
    ./tools/verify-image-3.sh --container /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin         --module-installed /home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.installed.ko         --module-activity /home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.activity.ko
    # expected tail: verify-image-3: 129 passed, 0 failed

The verifier checks the container members, the kernel slice (unchanged), the
UBI geometry + vol0 round-trip, the squashfs options, the module identity/
params/features/.text, the regdomain cfg80211, the ax hostapd, the full
payload byte-identity, the loader-uci-module consistency, the fail-closed
gates, the firmware policy, the package db, the device nodes and the
reproducible container metadata. Full output: VERIFY.txt.

## Recon / re-bake (host only)

    ./tools/selftest.sh
    ./tools/bake-image-3.sh                # writes /home/shibbityshwab/wifi-image-3-artifacts/
    ./tools/verify-image-3.sh --container ...   # (bake runs it already)

Inputs are pinned in the script; a drifted base/module/kernel aborts before
any work. The merged module itself is rebuilt by the recipe in
build-evidence/module-build/source-chain.txt (needs the project 6.18.60 KDIR
and the OpenWrt GCC 14.4 toolchain, local).

## Deploying this image is an attended Lead decision

See FLASH-FEASIBILITY.md (rootfs-only flash + full container) and the
project runbook. The firmware policy is unchanged: the PD4 research firmware
and the Wi-Fi custom block are **never packaged** - push them by hand, attended
(see payload/etc/hi5622/IMAGE-3-NOTES.txt on the image itself).

## Policy

Text only in git: the module/container/UBI/squashfs are local artifacts and
never committed. No vendor binary, firmware byte, calibration value or secret
is stored here; the only committed protocol bytes are the project's own text
policy and host-built frames (none new in this lane). The fixtures'
`cudy-ap-test*` identities are the project's throwaway test identities.
