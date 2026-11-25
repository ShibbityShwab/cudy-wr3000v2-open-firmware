# wifi-final-image-2 (task-159) - FINAL IMAGE #2

**Status: BUILT + VERIFIED host-side** (verify 82/82 PASS; selftest 49 passed /
0 failed / 1 skipped - only the optional ARM hostapd binary validator) -
**not flashed, not booted** by this lane. Kernel slice **UNCHANGED**; rootfs-only
successor of the SAE-hostapd image B (`8944740e`).

This lane bakes the bench image that carries:

* the **corrected final association module** as `kmod-hi5622-wifi` -
  344,372 B sha256 `ab9e18a4...`, **56 params**: final all-patch tree +
  wpa3-sae + ap-mgmt + sta-ops + data-tx + the **corrected mgmt-TX carrier**
  (raw **HCC g0 id6** control message: 12-B HCC header + 48-B device TX control
  + inline frame; completion DMAC `0x01F3` -> `g2 id13`; the superseded WAL
  `0x01F2` carrier is gone). Hardware-accepted carrier: 86 B posted, consumed
  0 polls. Association not yet fired as a combined build.
* the **WPA2/WPA3 hostapd fixtures** (open-proven, open, WPA2, WPA2+PMF,
  WPA2/WPA3 transition, WPA3-SAE MFP-required/BIP-CMAC-256);
* the **hi5622 loader/init** with `autoload=0` and the association gates
  (mgmt_tx/mgmt_rx/sta_ops/eapol_rx/sta_user_del/data_tx) **fail-closed**;
* the unchanged userspace stack: SAE hostapd `451dd49f...`, `iw`,
  ucode wifi-scripts/iwinfo, wireless-regdb, `cfg80211.ko`.

## Headline identities

    container   7,188,799 B  sha256 0d9429ec4681d43966bd2f23dc3fbe872f232961eaf142c16f565c67be5b5a15
    kernel      3,372,638 B  sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED; slot 1794a531...)
    rootfs UBI  3,801,088 B  sha256 44df3a63cfd9b9811a4ad99d8f49ae917a49c4408719d0df4582ed50785faedb
    squashfs    3,316,684 B  sha256 f0f970e20d9ea412f6f535c835cadbb57655824d3236a0f63243f394a7dc7a94
    module art.   344,372 B  sha256 ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e  (56 params)
    module inst.  325,932 B  sha256 4f1b45c187e9dfc61783a458f6a60056763f1da037bebec239b5b571f4ff4de1  (.text identical to artifact)
    manifest     83 packages

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: module + corrected carrier, WPA2/WPA3 fixtures, loader policy, verification, reproducibility, association fire recipe, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build record, inputs/pins, command sequence, three-run reproducibility, tree state, module rebuild/re-bake recipes |
| [HASHES.txt](HASHES.txt) | authoritative container/rootfs/kernel/module/payload identities |
| [VERIFY.txt](VERIFY.txt) | verifier output over the built bytes (82/82) |
| [FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) | rootfs-only flash re-check (kernelb/mtd12 needs no write) + manual sequence |
| `payload/` | text rootfs overlay: loader/init/uci/modprobe policy + 6 WPA2/WPA3 fixtures |
| `tools/bake-final-image-2.sh` | serialized shared-tree rootfs-only bake + verify + evidence record |
| `tools/verify-final-image-2.sh` | verifier over a built container (kernel, userspace, module, payload, manifest, policy, flash geometry) |
| `tools/selftest.sh` | host-only payload/policy/loader/uci/module checks |
| `build-evidence/` | committed text evidence (logs, run history, driver identity, payload manifest, verify output) |

## Reproduce / re-verify (host only)

    ./tools/selftest.sh
    ./tools/bake-final-image-2.sh
    ./tools/verify-final-image-2.sh --container /home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin \
        --module-artifact /tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko \
        --module-artifact-sha ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e \
        --module-artifact-size 344372 --module-params 56

Expected tails: `verify-final-image-2: 82 passed, 0 failed` and
`selftest: 49 passed, 0 failed, 1 skipped`.

Deploying this image is an **attended Lead decision**: see
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) and
`../wifi-image-integration/DEPLOY.md` (kernelb needs no write).

## Policy

Text only in git: the module/image/UBI/squashfs are local artefacts
(`/home/shibbityshwab/wifi-final-image-2-artifacts/`) and never committed. The
vendor PD4 firmware and the Wi-Fi custom block are **never packaged** - manual
pushes after flash (see `payload/etc/hi5622/FIRMWARE-POLICY.txt`). The
fixtures' `cudy-ap-test`/`test-passphrase-121` and
`CHANGEME-lab-only` are the project's throwaway test identities, not secrets.
