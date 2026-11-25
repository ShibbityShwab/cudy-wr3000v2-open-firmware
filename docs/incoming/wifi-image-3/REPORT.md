# REPORT - wifi-image-3: the IMAGE #3 bake (task-196)

**Lane:** wifi-image-3 (shared task task-196), 2026-11-28. **HOST-ONLY**: no
device contact, no flash, no fire, no network. **The Lead fires.**
**Status: container built + verified (129/129) + reproducible (3/3 identical
bakes); not flashed, not booted.**

## 1. The task

"The verified image #2 + the regdomain fix + the ax hostapd (the
wifi-image-update lane packaged the rootfs-only UBI). TASK: bake image #3 (the
full sysupgrade container) carrying: the fixed cfg80211, the ax hostapd, the
best driver modules from the lanes (the F2/beacon modules' lineage - decide the
most complete), update the hashes + the verification (84/84-style). Use the
wifi-final-image-2/wifi-image-update tooling."

Done: the full container (not only the rootfs UBI), the decided module, the
hash record and a 129-check verifier (the image #2 "84-style" verifier,
retargeted and extended).

## 2. The base (verified image + verified update)

| base | identity |
| --- | --- |
| image #2 container | 7,188,799 B `8ba1ad22...` (verified 84/84 by the image #2 lane; kernel 13e81214..., rootfs 3,801,088 B `31e7a39c...`) |
| wifi-image-update rootfs | root.squashfs 3,997,696 B `b461df0c...` + rootfs.ubi 4,456,448 B `d3c665f1...` (the bench-proven fixes: `cfg80211.ko` 2,317,952 B `bc4d9c73...` and `hostapd` 1,618,388 B `fef44f3b...`) |

The bake re-verifies the base before using it: the base squashfs sha, the base
UBI sha, the image #2 container sha/size and the base UBI vol0 == base squashfs
round-trip (so the two base artifacts cannot silently disagree), and the
kernel member extracted from the image #2 container still hashes 13e81214...

## 3. The module decision

The lane artifacts sit on three branches of the same association tree. Picking
the tip of one branch would drop another bench-proven feature:

| branch | tip artifact | params | bench proof |
| --- | --- | --- | --- |
| association + RX watermark (image #2) | 347,388 B `d2c608e7...` | 57 | image #2 accepted; MGMT carrier 86 B/0 polls; RX live bank armed |
| 5 GHz chain (F2) | /tmp/f2-module.ko 364,184 B `12427b86...` | 61 | **F2 fire 2026-11-27 23:30: the 5 GHz RF PROGRAMMED** (mode 3 band 1; R+0x3A8=0x01800060, R+0x3AC=0x03E6138A) |
| HE advertisement | 356,564 B `475975d3...` | 59 | **HE AP live (2026-11-27 04:00/09:00: HE caps + HE Operation IEs on air)** |
| BA skeleton | 359,992 B `78460841...` | 60 | fail-closed (no peer) |

Neither the F2 tip nor the image #2 tip nor the HE tip carries the others.
**IMAGE #3 carries the merge**: the F2 5 GHz chain tree + the image #2 RX
watermark patch + the HE patch + the BA patch. Full candidate inventory:
MODULE-INVENTORY.md.

### The merged module (the image's driver)

    activity  379,092 B  sha256 545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
    installed 358,544 B  sha256 a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed
    64 params = F2's 61 + rx_refill_wm + he_caps + ba_enable
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends cfg80211 ; license GPL

Built host-side from the committed patches against the project 6.18.60 KDIR,
**0 compiler/modpost warnings**. The merge touches only what the three features
touch (build-evidence/module-build/merged-source-delta.txt): 4 new files
(`hi5622_ba.c/.h`, `hi5622_he_caps.c/.h`) and 5 touched files
(`hi5622_cfg80211.c` for HE, `hi5622_dp_txrx.c` + `hi5622_rx_live.c/.h` for
T4, `Kbuild`); nothing from the F2 tree is removed.

### The module validation (why this is not a blind merge)

1. **Symbol preservation** (build-evidence/module-build/symbol-preservation.txt):
   `nm --print-size` over both unstripped modules:
   * 986 unique symbols with **identical sizes**,
   * **0 symbols exist only in the F2 module** (nothing is lost),
   * **182/182 5 GHz/beacon-chain symbols present with identical sizes**
     (`desc_table`, `dfs_mode`, `beacon5g`, `band5g`, `vapmsg5g`, the scan/
     AP/VAP/frame-post paths),
   * only 5 symbols changed size, all feature-status/user functions:
     `cfg80211_status_show`, `hi5622_cfg80211_init/remove` (he_caps policy +
     status), `hi5622_dp_rx_capture_show`, `hi5622_dp_rx_drain_store` (the T4
     watermark pass),
   * 50 new symbols = `ba_*`, `he_caps_*`, `rx_wm_*` and the three params.
2. **The constituent lanes' host tests pass on the merged sources**
   (build-evidence/module-build/host-tests-*.txt): rx-watermark 127 checks,
   HE-caps 109 + 22 checks, BA 183 checks - 0 failures.
3. **The fail-closed contract**: every added feature defaults to 0 (off) at
   load and is inert until enabled; `he_caps>1` is refused at init; the image
   loader refuses `desc_table/dfs_mode/vap5g` without `band5g=1`. The image
   ships all of them at 0.

**Honest limit:** the exact 64-param module is a new combination. The three
constituent code paths are individually bench-proven, but the combination has
not been fired. That is why the image ships it fail-closed and why the docs
name both the proven parents and the unproven combination.

## 4. The image #3 policy payload (text, committed)

| file | change |
| --- | --- |
| `payload/etc/config/hi5622` | image #3 header (module identity/chain); the 7 new gates `band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable`, all fail-closed; the F2 5 GHz and the 802.11ax recipes as comments |
| `payload/usr/sbin/hi5622-load` | records the image #3 module identity; reads, passes AND verifies the 7 new params; refuses the 5 GHz gates without `band5g=1`; refuses he_caps>1; keeps the no-rmmod policy |
| `payload/etc/modprobe.d/hi5622-wifi.conf` | mirrors the new defaults (the manual-modprobe record; the loader is the proven path) |
| `payload/etc/init.d/hi5622-wifi` | header retargeted to the 64-param module |
| `payload/etc/hi5622/hostapd-open-ax.conf` | 2.4 GHz open HE fixture (the proven HE AP class; SSID cudy-ap-test-he) |
| `payload/etc/hi5622/hostapd-open-5g40.conf` | the F2 fire's 5 GHz ch40 AP shape (SSID cudy-ap-test-5g40) |
| `payload/etc/hi5622/IMAGE-3-NOTES.txt` | the on-image operator recipes (2.4 GHz, AX, 5 GHz F2) + the firmware push policy |

The payload is byte-identical in the built rootfs (the verifier compares every
file against this lane). The six pre-existing WPA2/WPA3 fixtures are preserved.
The 3 new files take the rootfs from image #2's 521 files to 524.

## 5. The build (exact chain in build-evidence/build-summary.txt)

1. verify pinned inputs (base squashfs/UBI, image #2 container, module
   artifacts, kernel slice, ubinize cfg),
2. extract the base root.squashfs (sudo, faithful: the /dev/console node),
3. install the image #3 module in the image's strip-kmod form,
4. apply the text payload,
5. `mksquashfs -comp xz -b 262144 -Xbcj arm -noappend -mkfs-time 1794002400
   -all-time 1794002400` (the image's exact options + pinned times for
   byte-reproducibility),
6. `ubinize -Q 1003 -p 131072 -m 2048 -s 2048 -O 2048` with the checked-in
   `target/linux/hisilicon/image/luofu-ubinize.cfg` two-volume layout,
7. re-assemble the sysupgrade container around the UNCHANGED kernel member
   (fixed member mtime 2026-11-27 00:00:00 +0700, owner 0/0),
8. verify (129/129), record artifacts + evidence.

**Reproducibility:** the pinned squashfs times + the fixed UBI image sequence
number + the fixed tar mtime make the whole container byte-reproducible:
**three consecutive bakes produced the identical container sha
`4cd85b5d...`** (build-evidence/build-runs.txt).

## 6. Verification

`tools/verify-image-3.sh`: **129 passed, 0 failed** (VERIFY.txt). It verifies
from the built bytes, not from the build logs:

* container: 6 members, CONTROL board/target, fip-manifest slice hash, fixed
  mtime/owner, the kernel member sha (13e81214...) and size,
* rootfs: UBI EC magic, PEB alignment/count, vol0 -> squashfs round-trip,
  mtd14 capacity, squashfs magic/options,
* rootfs content: 524 files, the module identity/params/modinfo/64 params/the
  11 required feature strings/installed .text == activity .text, the fixed
  cfg80211 (sha/size/0 pkcs7 refs), the ax hostapd (sha/size/ELF/mbedtls/HE
  markers), the bench userspace (iw/iwinfo/ucode/init/regdb),
* policy payload: every file byte-identical to the lane payload, the rc.d
  link, `sh -n` on the loader, autoload=0, the 7 gates present + fail-closed,
  the uci labels, the 7 loader params, the 7 readback verifications, the
  band5g guard, the modprobe mirror,
* consistency: the 46 loader insmod parameters are a subset of the module's
  64; all 44 loader uci keys exist in the shipped uci,
* policy: no vendor firmware/PD4/wifi_block binary packaged, the firmware
  policy text present, the package db (83 pkgs incl. hostapd-mbedtls + both
  kmods), /dev/console char device, module 0644 root:root, hostapd 0755,
  all six WPA2/WPA3 fixtures.

`tools/selftest.sh`: **102 passed, 0 failed, 0 skipped** - the artifact-free
half (payload presence/syntax, uci/modprobe/loader consistency, fixtures,
notes, publication policy, stage-1 script syntax), so a fresh checkout can
validate the lane without the local artifacts.

## 7. Limits / not done by this lane

* **Not flashed, not booted**: the image is a host-built artifact; the Lead
  decides the fire (FLASH-FEASIBILITY.md).
* **The merged 64-param module is not bench-fired as a combination** (its three
  parents are); all new features ship off/fail-closed.
* **The 5 GHz TBTT/beacon TX** is still the open firmware piece
  (fw-tbtt-arm lane): IMAGE #3 carries the F2-proven RF chain, so when the
  TBTT arm fix lands the beacon can be re-fired without another image surgery
  (the module would then be re-baked with the fix; the 5 GHz gates already
  exist in the loader).
* **The 5 GHz VAP path is not proven end-to-end on air** (the RF is programmed;
  the beacon emission is the TBTT gap). The docs do not claim a 5 GHz beacon.
* The image remains a **bench image**: autoload=0, AP start disabled, the
  association/5 GHz/AX/BA gates 0. Nothing loads or transmits at boot.
* The vendor-derived PD4 firmware and the Wi-Fi custom block are **never
  packaged**; the image fails closed without the manual push.
* The apk database is not rewritten for the swapped files (same as the
  wifi-image-update base: `cfg80211.ko`, `hostapd` and now
  `hi5622_wifi.ko` are image-policy replacements). `apk audit` would flag
  them; this is a documented bench-image property, not a boot dependency.
