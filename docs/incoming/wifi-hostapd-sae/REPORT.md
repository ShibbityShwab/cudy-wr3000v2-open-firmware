# REPORT - wifi-hostapd-sae (task-148)

**Lane:** wifi-hostapd-sae, host-only.  **Date:** 2026-11-24 (bench-wave docs
date the hardware-validation session 2026-11-27).  **Base:** the FINAL
all-patch module (opensource `008aca5`; stripped 308,080 B sha `5b587949...`)
and the task-147 final bench image (container `0efcd1f1...`).
**Nothing was flashed, booted or written to the device by this lane.**

## 0. TL;DR

* The bench image's `hostapd-mini` v2.12 is SAE-less - proof: its built
  `hostapd/.config` has no `CONFIG_SAE` and the shipped binary refuses
  `wpa_key_mgmt=SAE` with `invalid key_mgmt 'SAE'` (reproduced in this lane
  as the negative control).
* Built the OpenWrt **full** hostapd variant `hostapd-mbedtls` for
  arm_cortex-a9 (musl): its package Makefile adds `CONFIG_SAE=y` and
  `CONFIG_TLS=mbedtls`; `libmbedtls21` was already in the image, so no new
  library package.  Installed image binary: 1,032,951 B sha
  `451dd49f...` (old mini: 504,333 B `13cb3a43...`).
* **Proof the build really has SAE:** 151 `-DCONFIG_SAE` compile lines,
  `src/common/sae.o` built, 13 `SAE:` code strings in the ARM binary,
  DT_NEEDED on the mbedtls trio.  (hostapd v2.12 `-v` prints only the version
  banner - there is no build-config list to grep; the functional gate below is
  the stronger proof.)
* **Functional gate on the shipped binary:** extracted it from the built
  container, ran it under qemu-arm against its own musl rootfs; it parses all
  three fixtures (WPA3-SAE only, WPA2/WPA3 transition, WPA2-hardened PMF),
  stops only at the absent-nl80211 pre-driver boundary, refuses a broken
  config, and the old mini binary refuses SAE (14/14 checks, evidence/
  qemu-config-gate-A.txt / -B.txt).
* **Re-baked the image** rootfs-only; kernel slice byte-identical
  (13e81214...).  Two variants are delivered (image A = strict drop-in on the
  task-147 image; image B = A + the wpa3-sae PMF/IGTK module, the recommended
  WPA3 bench combination).  Both pass 37/37 byte-level image checks.
* **Still not a usable WPA3 AP:** SAE association needs the AP management
  TX/RX path (wifi-ap-mgmt), `.add_station` (sta-ops) and RX/EAPOL.  This lane
  fixes the userspace parser blocker only - it does not fake association.

## 1. The problem, exactly

The task-147 image ships `hostapd-mini` from the shared OpenWrt release line
(tree `/home/shibbityshwab/openwrt-lab-selectorfix-img`, HEAD `27663a0`,
package `2026.08.07~831364bf-r7`).  Two independent facts prove it cannot
parse WPA3:

1. static: the built variant config
   `build_dir/target-arm_cortex-a9_musl_eabi/hostapd-mini/hostapd-.../hostapd/.config`
   is the tree's `files/hostapd-mini.config` and has no `CONFIG_SAE`; the
   package Makefile only adds `CONFIG_SAE=y` for the openssl/wolfssl/mbedtls
   variants (`ifeq ($(SSL_VARIANT),...)` blocks), never for internal/mini;
2. functional: the shipped 504,333 B `13cb3a43...` binary returns
   `Line 44: invalid key_mgmt 'SAE'` on `configs/hostapd-wpa3-sae.conf`
   (captured under qemu-arm in evidence/qemu-config-gate-A.txt).

## 2. Variant choice and why it works

This hostapd source version requires an external crypto backend for SAE
(`crypto_ec_*` exists only in crypto_openssl.c / crypto_wolfssl.c /
crypto_mbedtls.c; the internal-TLS variants cannot link SAE), so forcing
CONFIG_SAE into the internal `hostapd-mini` build is not possible.  The
correct drop-in is the **full mbedtls variant**:

| item | value |
| --- | --- |
| package name | `hostapd-mbedtls` (OpenWrt full variant; package Makefile VARIANT=full-mbedtls) |
| build dir variant | `hostapd-full-mbedtls` (fresh compile, log evidence in evidence/package-build-log.txt) |
| build flags added by the package | `CONFIG_SAE=y CONFIG_TLS=mbedtls CONFIG_OWE=y CONFIG_SUITEB192=y CONFIG_AP=y CONFIG_MESH=y CONFIG_EAP_PWD=y CONFIG_DPP=y CONFIG_DPP2=y` for the full variant |
| additional image packages | none: `libmbedtls21 3.6.8-r1` is already in the bench image (baseline `CONFIG_PACKAGE_libmbedtls=y`) |
| IEEE 802.11w | in this hostap source version PMF support is **unconditional** (no `CONFIG_IEEE80211W` symbol exists anywhere in the 2026-08-07 source); the stale commented line in the tree's variant config is ignored by the compiler |
| image size growth | rootfs squashfs 3,303,800 B vs 3,108,256 B before (+195,544 B); rootfs UBI 3,801,088 B vs 3,538,944 B (+262,144 B); container 7,188,799 B vs 6,922,559 B |

The package builds reproducibly through the tree's GCC 14.4 arm_cortex-a9
musl toolchain; the full compile log is curated in evidence/.

## 3. Package build evidence (target artifact)

``
binary (unstripped, build tree) 1,214,784 B sha256 3baf5aa15e3786c625a88ffa3a53fc9f265d4c7dcd6ae3cfb9511f5f70fff614
apk  hostapd-mbedtls-2026.08.07~831364bf-r7  554,612 B sha256 b3c95eb6cffcc8de11723125fba3b54f1b51c485a68af4970dbcb463baaa5a6f
apk  hostapd-common   (unchanged)  1,922 B sha256 e064cd3f52859ce3bd11fcc6d863524821c471fe9a440f9c0e0769ccee70ef3f
apk  libmbedtls21 3.6.8-r1 (baseline)  289,811 B sha256 7e0fdef54b2535d15f5b62ad1806b9014c1ff2339ba29f383b1874559ea67f42
image-installed (rstrip.sh stripped)  1,032,951 B sha256 451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469
``

SAE build proof (from evidence/package-build.txt + build log):
151 `-DCONFIG_SAE` occurrences, `src/common/sae.o` compiled twice (hostapd +
hostapd_cli link), 13 distinct `SAE: ` strings in the binary,
DT_NEEDED `libmbedcrypto.so.16 libmbedx509.so.7 libmbedtls.so.21`.
`hostapd -v` does not print a build-config list in v2.12; see section 4 for
the functional proof that replaces it.

## 4. Functional config gate (shipped ARM binary under qemu-arm)

Tool: `tools/validate-wpa3-configs.sh`.  It extracts the rootfs from the
built container, runs the **shipped** `/usr/sbin/hostapd` under qemu-arm
v7.2.0 with `-L` pointing at that same musl rootfs, and exercises the three
fixtures staged in `/etc/hi5622/`:

``
ok  shipped hostapd is ELF32 ARM
ok  shipped hostapd links libmbedcrypto (SAE EC backend)
ok  shipped hostapd -v reports v2.12 under emulation
ok  hostapd-wpa3-sae.conf:   configuration read; no parse/semantic errors; accepted (pre-driver boundary)
ok  hostapd-wpa2-wpa3.conf:  configuration read; no parse/semantic errors; accepted (pre-driver boundary)
ok  hostapd-wpa2-hardened.conf: configuration read; no parse/semantic errors; accepted (pre-driver boundary)
ok  negative control: invalid config refused (parser is live)
ok  negative control: old hostapd-mini (504,333 B 13cb3a43...) refuses key_mgmt=SAE
== validate-wpa3-configs: 14 passed, 0 failed ==
``

The "accepted" boundary is `Failed to initialize driver 'nl80211'` (the host
has no cfg80211 generic netlink) - i.e. the whole config file and semantic
checks ran before any driver call, which is exactly the surface that was
broken.

Emulation aids (verification only, image bytes untouched): a local static
qemu-arm 7.2.0 binary and `/lib -> ../usr/lib` symlinks for libraries that
qemu's `-L` path fallback would otherwise resolve to host x86_64 libraries.
On the device musl resolves those paths from `/usr/lib` normally.

## 5. Image re-bake

Tool: `tools/bake-image-with-hostapd-sae.sh`.  It holds the project world-build
lock, stages the task-147 payload + this lane's fixtures, re-stages the pinned
prebuilt module, appends the bench delta with `hostapd-mini` replaced by
`hostapd-mbedtls`, runs a world build (kernel compile skipped by pin), and
refuses to certify the image if the kernel slice changed.

| item | image A | image B |
| --- | --- | --- |
| driver module | FINAL all-patch 308,080 B `5b587949...` (installed 291,292 B `f07dd505...`) | wpa3-sae PMF/IGTK 311,108 B `08832e75...` (installed 294,168 B `c7124bb9...`) |
| container | 7,188,799 B `a31bc405...` | 7,188,799 B `8944740e...` |
| kernel slice | 3,372,638 B `13e81214...` UNCHANGED | same, UNCHANGED |
| rootfs UBI | 3,801,088 B `2ce5c191...` | 3,801,088 B `7817f090...` |
| rootfs squashfs | 3,303,800 B `f01a5124...` | 3,304,908 B `3664ce2a...` |
| verify | 37/37 PASS | 37/37 PASS |
| kernel .config pre/post | 08b6e595... / 08b6e595... | 08b6e595... / 08b6e595... |

The two bakes also reproduce the image deterministically: image A was baked
twice and produced the same container sha `a31bc405...` both times.  The
only rootfs delta vs the task-147 image is the hostapd package (plus the three
fixtures and the radius/full-variant helper files that the full hostapd variant
installs); the hostapd-common scripts are byte-identical.

Which image to use for the bench attempt: **image B** - it combines the SAE
userspace with the driver module that carries the IGTK /
`set_default_mgmt_key` fix the WPA3 AP-start key path needs (both components
were hardware-proven separately).  Image A is the strict "task-147 final image
+ SAE hostapd" drop-in if the driver lineup must stay exactly as task-147.

Flash geometry: the rootfs grows to 3,801,088 B inside a 24,379,392 B
rootfsb/mtd14 (15.6 % used, ~20.5 MB free) - still a rootfs-only flash, kernelb
needs no write (kernel slice unchanged, full-slot hash as in task-147:
`1794a531...` re-derived from the container kernel member by the verifier).
Deploy decisions stay with the Lead: see ../wifi-final-image/FLASH-FEASIBILITY.md.

## 6. Driver-side note (READY, per the handover)

The wpa3-sae driver module (311,108 B sha `08832e75...`) advertises AKM
PSK+SAE, `mgmt_stypes tx=0x3c2a` and the IGTK /
`.set_default_mgmt_key` fix; the WPA2-hardened PMF AP-ENABLED milestone
(GTK+IGTK installs) is hardware-proven.  This lane does not rebuild or alter
that module; image B packages it unchanged (its stripped installed form is
294,168 B sha `c7124bb9...`, verified from the built rootfs).

## 7. What this does NOT prove / limits

* Not flashed or booted; no device contact in this lane.  The built image is
  host-verified only.
* The qemu gate proves config parsing semantics of the shipped binary; it
  does not exercise nl80211, so it says nothing about the driver's AP-start or
  key-install behavior (that is the bench ladder in ../wifi-wpa3-sae/LADDER.md).
* A usable WPA3-SAE AP still needs: AP management TX/RX (wifi-ap-mgmt lane),
  `.add_station`/`.del_station` + per-STA firmware context (sta-ops lane),
  MAC RX/EAPOL data path.  This lane removes the userspace parser blocker; it
  does not claim an association.
* Strict SAE H2E (`sae_pwe=1`) still needs RSNXE transport in the AP-config
  message (the fixtures use `sae_pwe=2` for that documented reason - see
  ../wifi-wpa3-sae/TABLES.md).
* The full hostapd variant installs EAP/RADIUS/WPS/DPP support and the
  (disabled) radius service; it is bigger than mini.  This is a bench image,
  not a release-size optimisation.

## 8. Reproduction

    ./tools/build-hostapd-sae-package.sh                   # package + evidence
    ./tools/bake-image-with-hostapd-sae.sh --variant A     # image A + verify
    ./tools/bake-image-with-hostapd-sae.sh --variant B     # image B + verify
    ./tools/validate-wpa3-configs.sh --container <container>
    ./tools/verify-image-with-hostapd-sae.sh --variant A|B --container <container> ...

All scripts refuse to run when the shared tree is busy, snapshot/restore the
tree (.config and built kernel .config byte-identical, files/ overlay removed,
staged driver package restored to the task-147 artifact), and never touch a
device.  See BUILD-NOTES.md for the exact recorded commands, pins and tree
state.

## 9. Policy

No vendor binary/firmware/calibration byte, no credential and no secret in
this tree: only derived constants, original scripts/docs and the project's
throw-away placeholder passphrase are committed.  Binaries (.apk/.arm/.bin/
UBI) stay local under `/home/shibbityshwab/wifi-hostapd-sae-artifacts/` and
are recorded only as hashes.  The vendor PD4 firmware and the Wi-Fi custom
block are never packaged (image policy unchanged from task-147).
