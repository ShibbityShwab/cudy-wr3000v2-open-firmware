# REPORT - wifi-final-image (task-147): FINAL bench image with the all-patch module

**Lane:** wifi-final-image (shared task **task-147**; the parent prompt named
`task-142`, which is the separate on-air-observer task and is owned by another
lane - the content matched task-147, which this lane claimed).
**Mode:** host-only build/verification. **No device contact, no flash, no boot,
no vendor binary in tracked text.**
**Host build:** 2026-11-24T14:43:46Z -> 14:44:02Z (16 s; local clock;
the inherited bench-wave docs date the hardware-validation session 2026-11-27).

## 0. TL;DR

The `wifi-image-integration` final image (container `ae5e28bf`) was rebuilt as
a **ROOTFS-ONLY successor** in the shared release-line OpenWrt tree with

1. the **FINAL ALL-PATCH clean-room module** baked as `kmod-hi5622-wifi`
   (308,080 B sha256 `5b587949...`, 47 parameters, hardware-validated in one
   boot: AP 5-message vendor order -> hostapd **AP-ENABLED**, **WPA2-AP GTK
   installed**, **TX COMPLETED**, **RX arm PASSED**);
2. the **wifi-image-polish loader/init policy retargeted to the 47-parameter
   module** with `autoload=0` (nothing touches the Wi-Fi chip at boot);
3. **WPA2 (+WPA3-transition) hostapd fixtures**, with the WPA2 fixture updated
   to the final module's real capability (M3 `.add_key/.set_key` exists and the
   GTK install is proven; client association remains the mgmt-path blocker).

| artefact | size | sha256 |
| --- | ---: | --- |
| **sysupgrade container** | **6,922,559 B** | **`0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada`** |
| **kernel slice** | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (**UNCHANGED**; slot `1794a531...`) |
| **rootfs UBI** | 3,538,944 B | **`6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e`** |
| rootfs squashfs | 3,108,256 B | `c6f5e779e8769c0379c6f69a6f5f40cb2a2ec2dbcda2d8133ea623c02c617f5f` |
| rootfs volume in UBI | 3,108,864 B | `f51a2e0d9bb9a7a9ecafbbe0186adad317108863241ccb3908cd94f8d5bbb263` |
| **module artifact / installed** | 308,080 / **291,292 B** | `5b587949...` / **`f07dd505f1d7ac5dd62a826c440414bf2bc1662fe2fecb504dc4cec5b5e0d412`** |
| kmod apk | 126,774 B | `aa6bdaed4fc2704129bbdbadb5827ae7ad5d458e3ee8161bee19fea915380793` |
| image manifest | **83 packages** | same package set as `ae5e28bf` |

* **Verification: PASS, 33/33 checks, 0 failed** - `tools/verify-final-image.sh`
  over the built bytes (see [VERIFY.txt](VERIFY.txt),
  [build-evidence/final-image-verify.txt](build-evidence/final-image-verify.txt)).
* **Two independent host builds were byte-identical** (same container / kernel /
  rootfs UBI / squashfs hashes) - see BUILD-NOTES.md section 4.
* **Rootfs-only flash feasibility re-checked: YES.** Kernelb needs no write
  (same `13e81214...` slice / `1794a531...` slot already on flash); the new
  rootfs UBI fits mtd14 (14.5 % used). The current root-repo installer still
  writes both members (no `--rootfs-only`), so choose as-is (safe, identical
  kernel bytes) or the documented manual rootfs-only sequence -
  [FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md).
* **WPA3-SAE is not ready**: the in-flight `wifi-wpa3-sae` lane (task-144) has
  no deliverable yet, so this bake ships the WPA2 fixture + the WPA2/WPA3
  transition fixture (explicitly **not functional**; SAE needs the mgmt-frame
  path). The drop-in point for the WPA3 configs is `/etc/hi5622/`.
* **Nothing was flashed or booted by this lane.** The exact all-patch artifact
  was not fired *as this combination* before (its constituent features were
  individually hardware-fired; the one-boot validation in the inherited
  evidence is the combined run). The next step is the Lead's attended install +
  boot receipt, then the external observer / mgmt-path work.

## 1. What changed vs the previous image (ae5e28bf)

| item | previous (`ae5e28bf`) | this image (`0efcd1f1`) |
| --- | --- | --- |
| kernel slice | `13e81214...` (3,372,638 B) | **identical** - no kernel write needed |
| rootfs UBI | 3,538,944 B `ff9f3e1c...` | 3,538,944 B **`6196232d...`** |
| driver module | combined dp-air + AP-start v7: 242,572 B `44c6cc12...`, installed 231,132 B `51608fd5...` | **FINAL ALL-PATCH: 308,080 B `5b587949...`, installed 291,292 B `f07dd505...`**, 47 params |
| driver features | AP start + data plane | **+ scan R4, stability/E2, RX consumer (rx_arm/drain), five-message AP order + beacon-static, WPA2 key ops (GTK), STA-join/HT/VHT/band** |
| init policy | none (bench loads by hand) | **`/etc/config/hi5622` autoload=0 + `/etc/init.d/hi5622-wifi` (START=96, no-op at boot) + `/usr/sbin/hi5622-load`** |
| hostapd fixtures | none in image (bench push) | **in image:** `/etc/hi5622/hostapd-open.proven.conf`, `hostapd-open.conf`, `hostapd-wpa2.conf`, `hostapd-wpa2-wpa3.conf` |
| packages | 83 | 83 (payload is a rootfs overlay; no package added) |

Byte-size notes: the container size is unchanged (6,922,559 B) because the
rootfs UBI still pads to the same total; the installed module grew +60,160 B
(291,292 vs 231,132) and the payload adds ~22 KB of text, which the squashfs
compresses to +31,220 B (3,108,256 vs 3,077,036 B).

## 2. The driver module baked into the image

| property | value |
| --- | --- |
| source | clean-room all-patch integration module, opensource tree **`008aca5`** (wifi-stability final integration; documented in `../wifi-stability/FINAL-INTEGRATION.md`) |
| artifact | `/tmp/wifi-allpatch-artifact/hi5622_wifi.ko` - **308,080 B sha256 `5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41`** (unstripped 1,138,024 B `a5068091...`) |
| packaging | **PREBUILT** - staged via `wifi-image-integration/tools/install-into-tree.sh --driver-ko`; no compile; the exact validated bytes are installed |
| tree package artifact | 308,080 B `5b587949...` (verifier asserts byte identity of both `package/hi5622-wifi/prebuilt/hi5622_wifi.ko` and the package build artefact) |
| installed form | `/lib/modules/6.18.60/hi5622_wifi.ko` - **291,292 B sha256 `f07dd505...`** (OpenWrt rstrip form of the same bytes) |
| vermagic / depends / license | `6.18.60 SMP mod_unload ARMv7 p2v8` / `cfg80211` / `GPL` |
| parameters | **47** (asserted); key policy params below |
| hardware validation (inherited) | one boot on the all-patch tree: AP 5-message vendor order -> hostapd **AP-ENABLED** (ssid `cudy-ap-test`, type AP); **WPA2-AP GTK installed** (key_add g0 id1 `0x0078` 96 B idx=1 multi cipher=`0xfac04` + set_default_key `0x0079` 68 B; firmware workers CA `0x0A5825`/`0x0A5C9D`); **TX COMPLETED** (TXQ publish, rptr==wptr); **RX ARM PASSED** (live bank CA `0x40050000` status=`0x182` mask=`0xfffffcc1`, rptr=`0x20`); page golden `0x86807FEF`, mask 0 |

**ABI note:** the module was compiled against the same linux-6.18.60 tree
family used for the image (the shared release-line tree's kernel .config sha
`08b6e595...` is unchanged before/after; `CONFIG_MODVERSIONS` is off, so
vermagic + header ABI is the contract - the same proven cross-tree pattern the
previous hardware-fired modules used). The verifier asserts vermagic, depends,
parameter count and the final-module feature strings **from the installed
bytes**, and asserts the package/build artefacts are the exact pinned artifact.

## 3. Boot/load policy: autoload=0 (bench) - nothing touches the chip at boot

Payload (all text, byte-identical in the image; hashes in [HASHES.txt](HASHES.txt)):

* `/etc/config/hi5622` - single policy source. **autoload=0** (bench
  default; the driver boot ladder writes the device on load, so loading stays
  attended). `ap.enabled=0`. `strict=1`. The **final-module single-boot
  recipe** is baked as policy: `allow_frames=32`, `h2d_send_budget=10`,
  `frame_gap_ms=100`, `vap_start_first=1`, `vap_start_mode=1`,
  `vap_start_module=0`, `scan_dwell_ms=60`, `hcc_poll_ms=5`,
  **`rx_enable=1`, `beacon_static=1`, `beacon_vap_start=1`, `wpa_keys=1`**,
  `wpa_key_module=0`, `sta_join=0`, `ht_mode=0`, `vht_cap=0`,
  `band_vendor=0`, `scan_band_cfg=0`, `hcc_trace=0`, `stop_ap_mode=0`,
  `mgmt_diag=0`, `txq_publish=1`.
  (These are the values the inherited one-boot validation used; the older
  19-param polish policy - `allow_frames=16`, `h2d_send_budget=4` - is NOT
  enough for the WPA2/RX rungs.)
* `/etc/init.d/hi5622-wifi` - rc.common service (START=96, STOP=20). With
  `autoload=0` it is a **no-op at boot**; `service start` delegates to the
  loader. `stop` only kills hostapd; **it never rmmods** (bench rule).
* `/etc/rc.d/S96hi5622-wifi` -> `../init.d/hi5622-wifi` symlink is staged so
  the service exists; the autoload switch is what keeps boot hands-off.
* `/usr/sbin/hi5622-load` - explicit-insmod loader: firmware preflight
  (PD4 + wifi_block must be pushed by hand), cfg80211 first, driver with 29
  explicit parameters (modprobe on this image does not pass module args),
  parameter read-back from `/sys/module/hi5622_wifi/parameters`, wiphy wait,
  C2 ladder (`h2d_ctx_create=3` -> `h2d_txq_sync72=1`, clean `ok=2`),
  optional country/VAP/hostapd. Host dry-run: `HI5622_DRY_RUN=1 hi5622-load`.
* `/etc/modprobe.d/hi5622-wifi.conf` - mirror of the packaged defaults for
  manual `modprobe` users (documented caveat: modprobe does not pass params on
  this image; the loader uses insmod).
* `/etc/hi5622/FIRMWARE-POLICY.txt` - the PD4/wifi_block manual-push policy
  (vendor-derived; never packaged).

## 4. hostapd fixtures and the real security status

The final module turns **M3** from "blocked" into "implemented and
hardware-proven": `struct cfg80211_ops` now carries
`.add_key/.del_key/.set_default_key` (`wpa_keys=1`), translating to the decoded
WAL g0 id1 commands `0x0078` (key_add), `0x0079` (set_default_key),
`0x007A` (key_del). The AP GTK install is proven in one boot.

| fixture (in `/etc/hi5622/`) | status on the FINAL module |
| --- | --- |
| `hostapd-open.proven.conf` | **hardware-fired baseline** (AP-ENABLED, AP-start frames consumed 0 polls); keep for bisecting |
| `hostapd-open.conf` | hardened open deployment shape (HT20 SGI20, max_num_sta=16); parser-validated with hostapd v2.12 (same source as the image's hostapd-mini) |
| `hostapd-wpa2.conf` | **protocol-ready to GTK**: ssid `cudy-ap-test`, WPA2-PSK/CCMP, HT20, rekey 600; start_ap reaches AP-ENABLED and the GTK is installed (proven). **Remaining blocker:** the 4-way PTK needs a real client **association**, i.e. the management-frame path (driver `.mgmt_tx` is fail-closed, no mgmt RX - M4, lane `wifi-ap-mgmt`). NOT a working secured AP yet. |
| `hostapd-wpa2-wpa3.conf` | WPA2+WPA3 transition (WPA-PSK + SAE, PMF required): **READY, NOT FUNCTIONAL** - SAE + PMF need the mgmt TX/RX path. Kept as the target shape; the dedicated WPA3 configs are the in-flight `wifi-wpa3-sae` lane's deliverable. |

Honest capability summary: **open AP is usable** (proven); **WPA2 is one
missing client-association path away** (key install proven); **WPA3-SAE and
PMF are blocked on the same mgmt path**; HT40 and 5 GHz AP are driver
capability gaps (HT20 only); the regdomain is the unsigned world db (NO_IR), so
a country must be set for a real on-air AP (`hi5622.ap.country`).

The WPA2 fixture carries the project's **throwaway test credential**
(`cudy-ap-test` / `test-passphrase-121`) so the external-observer harness
(task-142) can scan/associate against the documented test network. It is a lab
fixture, not a secret; a real deployment must replace it.

## 5. Verification performed (host-only, from the built bytes)

`tools/verify-final-image.sh` (full output in [VERIFY.txt](VERIFY.txt)):
**PASS 33 / FAIL 0**. Highlights:

1. **Kernel slice UNCHANGED**: 3,372,638 B `13e81214...` (the requirement),
   built kernel .config sha `08b6e595...` unchanged, and the **full 8,650,752 B
   flashable slot hash is still `1794a531...`** (the kernelb/mtd12 write unit).
2. **rootfs squashfs reassembled from the UBI** (per-PEB EC/VID parse) equals
   the build's `root.squashfs` (+608 B ubinize padding).
3. **all 16 required base-image files present**; known-good identities exact
   (iw, hostapd, ucode modules, regdb, cfg80211).
4. **all 9 payload files present byte-identically** (compare vs `payload/`),
   the `S96` enable symlink correct, the autoload=0 / 32-10 / feature-gate
   policy assertions pass, loader executable and carries the full parameter
   set, init service START=96.
5. **installed module is the FINAL all-patch build**: vermagic, depends,
   license, **47 parameters**, feature strings (dp/ap/rx/key/beacon/sta/scan);
   the tree package and build artefacts are **exactly** the pinned 308,080 B
   `5b587949...` bytes.
6. **manifest**: 83 packages, all required packages present.
7. **no vendor firmware/calibration binary packaged** (PD4/wifi_block are
   manual pushes; verified absent).

Static host checks: `tools/selftest.sh` - payload/policy/loader/uci
consistency, no binary/secret payload, **hostapd v2.12 config-parse gate for all
4 fixtures + negative control**, module identity, docs/hashes consistency.
Output: [build-evidence/selftest.txt](build-evidence/selftest.txt).

## 6. Rootfs-only flash feasibility (re-checked)

**Feasible.** Kernelb needs no write (identical `13e81214...` slice; the
`1794a531...` slot is already on flash). The new rootfs UBI
(`6196232d...`, 3,538,944 B) fits mtd14 (0x1740000 = 24,379,392 B; 14.5 %
used). The current root-repo installer has **no `--rootfs-only` flag**
(re-checked; sha `3a8fbaa0...`), so: (a) use it as-is - safe, because the
kernel write is content-identical and slot-verified - or (b) the documented
manual strict rootfs-only sequence (updated hashes) in
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) section 4. Prerequisites: stock
active on pair A, recovery armed, handover `bootflag=b` + selector `0x11`.
Rollback: stock pair A + the previous rootfs UBI `ff9f3e1c...`; kernelb is
never touched. Overlay loss is accepted (rootfs wipe).

## 7. Findings / corrections

1. **The 47-parameter module needs a different boot recipe than the 19-param
   polish build.** `allow_frames=16`/`h2d_send_budget=4` (polish defaults) are
   not enough for the WPA2/RX rungs; the validated single-boot recipe uses
   `32`/`10`. The baked `/etc/config/hi5622` + loader defaults were retargeted
   accordingly, and the selftest cross-checks uci vs loader vs modprobe mirror.
2. **The image-polish WPA2 fixture described the driver as having no
   `.add_key/.set_key`** - true for the old module, **false for the all-patch
   module**. The baked fixture's comments/status were corrected to the proven
   GTK-install state; the remaining blocker is client association (mgmt path),
   not key installation.
3. **The package build dir must be purged** before rebuilding: the OpenWrt
   package stamps from the previous image would otherwise keep the old
   `hi5622_wifi.ko` in the package build dir (same version string). The bake
   script removes `build_dir/.../linux-hisilicon_luofu/hi5622-wifi/` and the
   verifier proves the built artefact is the pinned bytes.
4. **The container size is unchanged** (6,922,559 B) and the rootfs UBI size is
   unchanged (3,538,944 B) vs `ae5e28bf` - the changes compress to a similar
   total; do not infer identity from sizes, use the hashes.
5. **Two host builds were byte-identical** - the image is reproducible from this
   tree/config (see BUILD-NOTES.md section 4).
6. **No vendor bytes, no secrets in tracked text** - the image carries our GPL
   clean-room module and text policy only; the firmware stays a manual push.

## 8. Limits (honest)

* Host-only: the image was **not flashed and not booted** by this lane; the
  all-patch module was **not fired as this exact combination before** (its
  features were individually hardware-fired; the inherited one-boot validation
  is the combined run). One attended install+boot is required to claim the
  image live.
* WPA2 is not an end-to-end working secured AP (client association/mgmt path
  missing); WPA3-SAE/PMF are not functional. Nothing here is a security claim.
* The image is **BENCH-ONLY**: the delta is not in the target image stanza;
  the driver is research code; PD4 + wifi_block are manual pushes.
* The regdomain (unsigned world db) means a country must be set for real on-air
  channels; HT40/5 GHz AP are driver gaps.
* If a later lane delivers a new module (e.g. mgmt TX/RX) or the WPA3 lane
  delivers configs, that is a **new bake**: re-run `tools/bake-final-image.sh
  --driver-ko <new artifact>` (pin sha/size/params in the script) and update
  the WPA3 fixtures under `payload/etc/hi5622/`.

## 9. Reproduction

    # host-only, shared tree serialized by /tmp/cudy-openwrt-world-build.lock
    ./tools/bake-final-image.sh                     # build + verify + evidence
    ./tools/verify-final-image.sh --owrt <tree>     # re-verify the newest container
    ./tools/selftest.sh                             # static/policy/parse checks
    ./tools/stage-payload.sh --tree <tree>          # dry-run payload staging

The bake script restores the shared tree (byte-identical `.config`, kernel
`.config`, rootfs staging tree, no leftover `files/` overlay) even on failure.

## 10. Provenance / policy

* Lane tools/payload/docs are text only; the module, image container, UBI and
  squashfs are **not committed** (local: `/home/shibbityshwab/wifi-final-image-artifacts/`,
  `SHA256SUMS` recorded in BUILD-NOTES.md).
* The module is our clean-room GPL code (not vendor-derived); it is packaged in
  the image because that is the deliverable. The vendor firmware, calibration
  and custom block are **never** packaged (policy verified by the verifier).
* No vendor binary, no private key, no real credential in tracked text.