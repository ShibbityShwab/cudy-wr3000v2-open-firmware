# wifi-final-image (task-147) - FINAL bench image with the all-patch module

**Status:** BUILT + VERIFIED host-side (PASS 33/33) - **not flashed, not
booted** by this lane. Kernel slice UNCHANGED. Rootfs-only flash feasibility
re-checked: YES.

This lane bakes the FINAL bench image as a **rootfs-only successor** of the
wifi-image-integration image (container `ae5e28bf`):

* the **FINAL ALL-PATCH clean-room module** as `kmod-hi5622-wifi` -
  308,080 B sha256 `5b587949...`, 47 params, one-boot hardware-validated
  (AP 5-message order -> AP-ENABLED, WPA2-AP GTK, TX COMPLETED, RX arm);
* the **wifi-image-polish loader/init policy retargeted to the 47-param
  module** with `autoload=0` (nothing loads at boot);
* **hostapd fixtures in the image**: open-proven, open-hardened, **WPA2**
  (status: protocol-ready to GTK; association blocked on the mgmt path) and the
  WPA2/WPA3 transition target (not functional; WPA3-SAE lane not delivered).

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: what changed, module identity, policy, hostapd/security status, verification, findings, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build record, inputs, two-run reproducibility, tree restore state |
| [HASHES.txt](HASHES.txt) | authoritative new container/rootfs/squashfs/module identities + payload hashes |
| [VERIFY.txt](VERIFY.txt) | verifier output over the built bytes (PASS 33/33) |
| [FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) | rootfs-only flash re-check: kernelb needs no write, mtd14 fits, installer gap, manual sequence |
| `payload/` | the text rootfs overlay staged into the image (autoload=0 policy, loader/init, hostapd fixtures) |
| `tools/` | `bake-final-image.sh`, `verify-final-image.sh`, `stage-payload.sh`, `selftest.sh` |
| `build-evidence/` | committed text evidence; `build-evidence/work/` is local/gitignored |

## Headline identities

    container   6,922,559 B  sha256 0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada
    kernel      3,372,638 B  sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED, slot 1794a531...)
    rootfs UBI  3,538,944 B  sha256 6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e
    squashfs    3,108,256 B  sha256 c6f5e779e8769c0379c6f69a6f5f40cb2a2ec2dbcda2d8133ea623c02c617f5f
    module      308,080 B    sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41  (47 params; installed stripped 291,292 B f07dd505...)
    manifest    83 packages

## Reproduce / re-verify

    ./tools/bake-final-image.sh        # serialized shared-tree rootfs-only build + verify + evidence
    ./tools/verify-final-image.sh      # re-verify the newest image container from its bytes
    ./tools/selftest.sh                # static/policy/loader/uci/hostapd-parse checks
    ./tools/stage-payload.sh --tree <openwrt-tree>   # dry-run payload staging

Deploying this image is an **attended Lead decision**: see
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) and
`../wifi-image-integration/DEPLOY.md` (kernelb needs no write).

## Policy

Text only in git: the module/image/UBI are local artefacts
(`/home/shibbityshwab/wifi-final-image-artifacts/`), never committed. The
vendor PD4 firmware and the Wi-Fi custom block are **never packaged** - they
are manual pushes after flash (see `payload/etc/hi5622/FIRMWARE-POLICY.txt`).
The WPA2 fixture's `cudy-ap-test`/`test-passphrase-121` is the project's
throwaway test network identity, not a secret.