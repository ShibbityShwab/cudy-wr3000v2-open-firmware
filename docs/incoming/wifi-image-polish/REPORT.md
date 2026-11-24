# REPORT - wifi-image-polish (task-135): module defaults, init, AP configs, packaging

**Lane:** wifi-image-polish (shared task task-135).
**Date:** 2026-11-27.  **Mode:** host-only - no device contact, no image
build, no flash, no vendor bytes.  Everything below was verified on the host
against the flashed-image artifacts, the 6.18.60 kernel tree and the
tree-source userspace.

## 1. Scope

The project's Wi-Fi line reached its documented bench ceiling: the
self-contained image boots the clean-room driver and hostapd reaches a live
AP-ENABLED state, but the image had **no boot policy, no loader, no AP
configuration policy, compiler-level defaults were the fail-closed bench
values, and the AP-start diagnostic dumped a stack into dmesg on every
hostapd start**.  This lane closes the "usable image" gap without touching the
hardware-validated binary:

1. module parameter defaults as runtime-baked policy + a polished source build;
2. init/boot integration with an explicit bench-vs-production switch;
3. hardened hostapd configs with an honest driver capability matrix;
4. kmod/firmware packaging policy and a versioned ABI note;
5. the one driver change requested by the bench (`mgmt_diag` gating) plus the
   image defaults compiled in, compile-verified on the 6.18.60 tree.

Lead tuning input taken as authoritative for defaults (2026-11-27):
`fw_path=hi_wifi/FIRMWARE.PD4.bin`, `wifi_block_path=hi_wifi/wifi_block.bin`,
`allow_frames=16`, `frame_gap_ms=100`, `h2d_send_budget=4`,
`vap_start_first=1`, `vap_start_mode=1`, `vap_start_module=0`,
`scan_dwell_ms=60`, `hcc_poll_ms=5`, and the `mgmt_diag=1` (default 0) gate
for the `dump_stack()` in the one-shot `.mgmt_tx` path.

## 2. Baseline: what the flashed image is

| artifact | size | sha256 |
| --- | --- | --- |
| self-contained image container | 6,922,559 B | `ae5e28bf...` |
| kernel slice (`Linux-6.18.60`, load/entry `0x80608000`) | 3,372,638 B | `13e81214...` |
| rootfs UBI | 3,538,944 B | `ff9f3e1c...` |
| rootfs squashfs | 3,077,036 B | `1dbd0d19...` |
| installed driver | 231,132 B | `51608fd5...` |
| image `cfg80211.ko` | 406,028 B | `11a5e2b5...` |
| image hostapd (`/usr/sbin/hostapd`, 2.12) | 504,333 B | `13cb3a43...` |

The driver artifact staged into the image is the hardware-fired combined
AP+DP module of 2026-11-27: **242,572 B sha `44c6cc12...`** (debug-stripped;
unstripped 925,192 B `21d05161...`), `vermagic=6.18.60 SMP mod_unload ARMv7
p2v8`, `depends=cfg80211`, **19 module parameters**, hardware-validated with
hostapd AP-ENABLED + MAC data-plane setup (POST-ONLY TX).  The installed
231,132 B copy is OpenWrt's stripped form of the same artifact.  Full driver identities (authoritative pins in
[refs/artifact-identity.txt](refs/artifact-identity.txt)):

* **installed image module**: 231,132 B sha256 `51608fd556fa474d2da0dd88975c2a8b610301fb6b8a22d4b6e0589ade247a78`
* **hardware-validated prebuilt**: 242,572 B sha256 `44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b`
* **polished host build (NOT fired)**: 242,920 B sha256 `d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6`

## 3. Module parameter defaults

### 3.1 Two layers, on purpose

* **Runtime-baked layer (shipped, zero risk):** the validated module stays
  byte-identical (`44c6cc12` / installed `51608fd5`) and the packaged
  defaults are passed explicitly by `/usr/sbin/hi5622-load` - which is the
  proven path, because **modprobe on this image does not pass module
  parameters** (bench-confirmed 2026-11-27) while explicit `insmod` does.
  `/etc/modprobe.d/hi5622-wifi.conf` mirrors the same values for manual
  `modprobe` users; `/etc/config/hi5622` is the single policy source.
* **Source-baked layer (polished build, not fired):**
  `patches/0001-polish-module-defaults-and-mgmt-diag.patch` changes the
  compiled-in defaults so even a bare `insmod hi5622_wifi.ko` is correct, and
  gates the `.mgmt_tx` stack dump behind `mgmt_diag` (default 0).  This
  changes the artifact, so it needs one attended fire before deployment; the
  shipped image does **not** use it.

Authoritative table (image defaults, source defaults, perms, rationale):
[refs/module-params.txt](refs/module-params.txt) and [PARAMS.md](PARAMS.md).
The exact working incantation is in [PARAMS.md](PARAMS.md).

### 3.2 The polished build and its verification

`tools/build-polished-module.sh` is a real compile/identity gate, not just a
build:

1. It reproduces the **proven** combined module first (wifi-dp-air/src +
   wifi-ap-start provider patch v7 + AP frames) in the canonical workdir and
   asserts the result is exactly **242,572 B sha `44c6cc12...`** - i.e. the
   tree/toolchain still produce the hardware-fired artifact.
2. It then applies the polish patch and builds again: **242,920 B sha
   `d145a205...`** (unstripped 926,120 B sha `512b4734...`), 20 parameters
   (19 + `mgmt_diag`), same vermagic/depends, zero warnings/errors, stripped
   `.text` byte-identical to the unstripped build.
3. It asserts every changed default in the staged tree
   (fw_path/allow_frames/frame_gap_ms/h2d_send_budget/vap_start_first/
   vap_start_mode/scan_dwell_ms) and the `mgmt_diag` gate.

Artifact identities and the build logs are frozen in
[refs/artifact-identity.txt](refs/artifact-identity.txt) and
[build-evidence/](build-evidence/).  The .ko is local evidence only and is
never committed (lane `.gitignore`).

Caveat recorded honestly: the toolchain build-id hashes debug info (which
embeds the absolute build path), so the same source in a different directory
differs **only** in the 20-byte build-id; the build script pins the canonical
workdir for the identity assert.  The kernel ignores build-id when loading.

## 4. Init integration

Design, boot chain, failure modes: [INIT.md](INIT.md).  Summary:

* `/usr/sbin/hi5622-load` (one command, no arguments) does the whole
  bring-up: firmware preflight -> cfg80211 -> driver with explicit parameters
  -> parameter read-back -> wiphy wait -> C2 ladder (`h2d_ctx_create=3`,
  `h2d_txq_sync72=1`, `h2d_status` gate) -> optional country/VAP/hostapd with
  AP-ENABLED wait.
* `/etc/init.d/hi5622-wifi` (rc.common, START=96, STOP=20) is the boot
  switch; it delegates to the loader and never unloads the module (no rmmod
  after a CPU release).
* `/etc/config/hi5622` is the policy: **autoload=0 and ap.enabled=0 by
  default** (bench).  Production-style: `autoload=1` +
  `/etc/init.d/hi5622-wifi enable`; AP start is a second switch.
* Boot cost of autoload=1 is ~35-45 s (module's fixed boot ladder + 30 s ready
  wait) - acceptable at S96, documented, and off by default for that reason.
* It starts hostapd directly with a static config; OpenWrt's wifi
  scripts/netifd are not involved (the driver is a research full-MAC driver).

The loader is dry-run testable on any host (`HI5622_DRY_RUN=1`), which the
lane selftest uses to prove the packaged defaults and the insmod incantation.

## 5. hostapd configs and what the driver really supports

Capability matrix and per-fixture status: [HOSTAPD.md](HOSTAPD.md).  Key
points:

* **Open AP is the only usable security class** (needs no key install).
  `hostapd-open.proven.conf` is the hardware-fired baseline;
  `hostapd-open.conf` is the hardened deployment shape (HT20, max_num_sta=16)
  and is parser-validated only.
* **HT20 is advertised** (SGI20 + AMSDU, MCS0-7, 1 stream) and used by the
  hardened configs.  **HT40 is not advertised** (HT20-only skeleton) - the
  fixtures deliberately do not enable it, with a comment explaining why.
* Channels: 2.4 GHz 1-13 advertised; 12/13 regdomain-dependent.  The image's
  unsigned world regdb is NO_IR, so a country is required for a real AP
  (proven flow used `iw reg set US`); the loader has `hi5622.ap.country`.
  5 GHz: only the non-DFS 36/40/44/48 subset is even conceivable after
  `band_force=1`, no DFS/CAC engine - not validated.
* **WPA2 is blocked on M3** (no `add_key`/`set_key` in
  `struct cfg80211_ops`): the config is ready and hardened, hostapd will stop
  at key installation with -EOPNOTSUPP after the start_ap sequence.
* **WPA2+WPA3 transition is blocked on M3 + M4** (`SAE` needs the
  management-frame auth path; `.mgmt_tx` is fail-closed, no mgmt RX): the
  config is a future-ready target, explicitly **not** working security.
* beacon interval/DTIM are hostapd parameters; the firmware beacon path is not
  host-observable, so the fixtures make no on-air claim.

Config parse gate: all four fixtures parse cleanly with hostapd v2.12 built
from the same tree source, **with CONFIG_SAE enabled** (the image
hostapd-mini supports SAE: its binary contains the
`sae_require_mfp`/`sae_pwe` config strings), plus a deliberate broken
negative control.  The stock host-build from the deploy lane lacks CONFIG_SAE
and cannot parse the WPA3 fixture, which is why this lane builds its own
validator (`tools/build-hostapd-validator.sh`).

## 6. Packaging policy

Full detail: [PACKAGING.md](PACKAGING.md).  Summary:

* kmod packages already correct in the image: `kmod-hi5622-cfg80211`
  (target-tree cfg80211 only; never together with mac80211 backports) and
  `kmod-hi5622-wifi` (prebuilt hardware-validated module or source build;
  **no AUTOLOAD** - the init policy owns the load decision).
* This lane adds the userspace policy as a rootfs `files/` overlay
  (`tools/install-into-image.sh`, dry-run default, refuses binaries, creates
  the rc.d symlink) or as package `FILES` additions.
* **Firmware policy:** PD4 (`cfe24bfb...`, 928,920 B) and the wifi_block are
  vendor-derived and are **never packaged or committed**; they are pushed
  manually to `/lib/firmware/hi_wifi/` in an attended session.  The driver
  fails closed without them.  `fw_path`'s PD4 default is bench-specific; an
  upstreamable driver would use a clean-room path and a redistributable
  firmware package (documented, not implemented).
* **Versioned ABI:** kernel `6.18.60` / kernel ABI `6.18.60~6f995278` /
  kernel slice `13e81214...` / vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`
  / `depends=cfg80211`.  A module rebuild is a versioned event (sha, size,
  `PKG_RELEASE`, fresh fire); parameter-surface changes are part of that
  inventory.

## 7. Corrections and operational findings

1. **`h2d_ctx_create_t1` does not exist.**  Some lane notes spell the C2
   attribute `h2d_ctx_create_t1`; the real sysfs attribute in the fired module
   is `h2d_ctx_create` (WO; write value 1|2|3 selects the t1/t2/t3 grammar).
   The loader and docs use the real name; the wrong name in the older note
   should not be copied into scripts.
2. **modprobe does not pass parameters on this image**; explicit `insmod` is
   the proven path.  The loader therefore uses insmod and verifies the
   parameters from sysfs; the modprobe.d file is only a mirror for manual use.
3. **Two module identities must not be confused:** staged prebuilt 242,572 B
   `44c6cc12...` vs installed 231,132 B `51608fd5...` (OpenWrt's stripped
   form).  Both are the same code; ABI checks compare vermagic/params, the
   image check uses the installed hash.
4. **Host build of hostapd without CONFIG_SAE cannot validate a WPA3 config**
   ("invalid key_mgmt 'SAE'"); the target hostapd-mini does support SAE.  This
   lane builds an SAE-enabled validator for the parse gate.
5. **The image regdomain is the unsigned world db (NO_IR)** - an AP needs a
   country; the proven flow set one explicitly.  The loader exposes it as uci
   instead of hardcoding a country.
6. **Wiphy index is not stable** across module load/unload cycles; scripts
   must enumerate `/sys/class/ieee80211/` (the loader does).
7. **HT40 requested in the task cannot be enabled**: the wiphy advertises an
   HT20-only skeleton; configs document HT20 and explicitly omit HT40 rather
   than emitting a config the driver cannot honour.

## 8. Verification evidence (host-only, all run by this lane)

| gate | result |
| --- | --- |
| `tools/build-polished-module.sh` | stack1 reproduces `44c6cc12` (242,572 B) exactly; polished `d145a205` (242,920 B); 0 warnings/errors; params 19 -> 20; vermagic/depends OK. Logs: [build-evidence/](build-evidence/) |
| `tools/validate.py` (static) | **162 checks PASS** (payload, uci, modprobe/loader/init consistency, hostapd semantics, refs, no-binary/no-credential policy) |
| `tools/build-hostapd-validator.sh` + parse gate | hostapd v2.12 (CONFIG_SAE) parses all 4 fixtures cleanly; broken-config negative control is refused |
| `tools/selftest.sh` | **179 checks PASS**, 0 skipped (static 162 + 17 lane checks: loader dry-runs, compile evidence pins, patch sanity) |
| loader dry-run | prints the exact packaged insmod incantation (fw_path=PD4, allow_frames=16, vap_start_first=1, ...), never rmmods |
| `tools/install-into-image.sh` | dry-run stages 9 files + the rc.d symlink into an OpenWrt tree; refuses binary/firmware payloads |

Everything was run with no network access to the router and no writes outside
`/tmp` build directories; the image tree was only read.

## 9. Honest limits

* **No on-air evidence.**  hostapd AP-ENABLED is host-side; beacons,
  association, WPA2 and DHCP need an external client (the project's
  external-observer task).  Nothing here changes that.
* **WPA2/WPA3 are not functional.**  They need M3 key install and (for SAE) M4
  management-frame auth; the configs are hardened targets, not security.
* **The polished module is not fired.**  It is compile-verified and
  identity-recorded; deploying it requires one attended fire.  The shipped
  config deliberately works with the already-validated `44c6cc12` module.
* **HT40 / 5 GHz AP / DFS** are driver-capability gaps, documented rather than
  papered over.
* **Beacon interval/DTIM effect on the firmware** is unobservable with the
  current tooling; the configs set them but make no on-air claim.
* **The image still is not turnkey**: PD4 + wifi_block are manual pushes by
  policy (vendor-derived), and the driver remains research-only / non-upstream.

## 10. Reproduction

    cd opensource/docs/incoming/wifi-image-polish
    ./tools/selftest.sh                  # 179 checks
    ./tools/build-polished-module.sh     # compile gate + artifact identities
    ./tools/install-into-image.sh --tree <openwrt-tree>          # dry run
    ./tools/install-into-image.sh --tree <openwrt-tree> --apply  # stage

The payload is [rootfs/](rootfs/); the driver delta is
[patches/](patches/); all pinned hashes are in
[refs/artifact-identity.txt](refs/artifact-identity.txt).

## 11. Next steps (priority order)

1. External observer on the AP: beacon on air, association, then WPA2 once M3
   lands (the difference between "AP-ENABLED" and "working AP").
2. One attended fire of the polished module (`d145a205...`) to close the
   dmesg-noise item and validate the compiled-in defaults; keep the validated
   `44c6cc12` as rollback.
3. M3: `add_key`/`set_key` ops -> turns the WPA2 fixture positive; then M4
   management-frame auth for SAE (WPA2+WPA3 transition).
4. Optional: non-DFS 5 GHz AP (`band_force=1`) as a second endpoint
   experiment - capability-limited, not a product path.
