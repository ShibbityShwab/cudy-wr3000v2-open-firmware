# wifi-final-image-2 (task-159) - FINAL IMAGE #2

**Status: BUILT + VERIFIED host-side** (verify 84/84 PASS; selftest 50 passed /
0 failed / 1 skipped - only the optional ARM hostapd binary validator) -
**not flashed, not booted** by this lane. Kernel slice **UNCHANGED**; rootfs-only
successor of the SAE-hostapd image B (`8944740e`).

This lane bakes the bench image that carries:

* the **corrected final association module with the RX refill watermark** as
  `kmod-hi5622-wifi` - 347,388 B sha256 `d2c608e7...`, **57 params**: final
  association tree + wpa3-sae + ap-mgmt + sta-ops + data-tx + the **corrected
  mgmt-TX carrier** (raw **HCC g0 id6** control message: 12-B HCC header + 48-B
  device TX control + inline frame; completion DMAC `0x01F3` -> `g2 id13`;
  the superseded WAL `0x01F2` carrier is gone) + the **EAPOL/data RX refill
  watermark** (`rx_refill_wm`, default 75, fail-closed/inert at boot).
  Hardware-accepted carrier: 86 B posted, consumed 0 polls. Association not yet
  fired as a combined build.
* the **WPA2/WPA3 hostapd fixtures** (open-proven, open, WPA2, WPA2+PMF,
  WPA2/WPA3 transition, WPA3-SAE MFP-required/BIP-CMAC-256);
* the **hi5622 loader/init** with `autoload=0` and the association gates
  (mgmt_tx/mgmt_rx/sta_ops/eapol_rx/sta_user_del/data_tx) **fail-closed**;
* the unchanged userspace stack: SAE hostapd `451dd49f...`, `iw`,
  ucode wifi-scripts/iwinfo, wireless-regdb, `cfg80211.ko`.

## Headline identities

    container   7,188,799 B  sha256 8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8
    kernel      3,372,638 B  sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED; slot 1794a531...)
    rootfs UBI  3,801,088 B  sha256 31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d
    squashfs    3,318,696 B  sha256 cb42bfed25a12698770236b44bb8e73fafcf362d7571911f6470374090ce0578
    module art.   347,388 B  sha256 d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6  (57 params)
    module inst.  328,788 B  sha256 b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e  (.text identical to artifact)
    manifest     83 packages

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: module + corrected carrier + RX watermark, WPA2/WPA3 fixtures, loader policy, verification, reproducibility, association fire recipe, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build record, inputs/pins, command sequence, three-run reproducibility, tree state, module rebuild/re-bake recipes |
| [HASHES.txt](HASHES.txt) | authoritative container/rootfs/kernel/module/payload identities |
| [VERIFY.txt](VERIFY.txt) | verifier output over the built bytes (84/84) |
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
        --module-artifact /tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko \
        --module-artifact-sha d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6 \
        --module-artifact-size 347388 --module-params 57

Expected tails: `verify-final-image-2: 84 passed, 0 failed` and
`selftest: 50 passed, 0 failed, 1 skipped`.

Deploying this image is an **attended Lead decision**: see
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) and
`../wifi-image-integration/DEPLOY.md` (kernelb needs no write).

## Policy

Text only in git: the module/image/UBI/squashfs are local artefacts
(`/home/shibbityshwab/wifi-final-image-2-artifacts/`; module durable copy under
`build/wifi-rx-watermark/`) and never committed. The vendor PD4 firmware and
the Wi-Fi custom block are **never packaged** - manual pushes after flash (see
`payload/etc/hi5622/FIRMWARE-POLICY.txt`). The fixtures'
`cudy-ap-test`/`test-passphrase-121` and `CHANGEME-lab-only` are the
project's throwaway test identities, not secrets.
