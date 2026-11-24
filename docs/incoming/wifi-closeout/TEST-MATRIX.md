# TEST MATRIX - host-only verifier sweep (close-out)

**Lane:** wifi-closeout (task-131). **Runs:** 2026-11-27, host-only - no device, no network,
no bench-router contact. The recorded run is in
[build-evidence/host-suite/](build-evidence/host-suite/) (`summary.tsv`, `RESULT.txt`, one log
per entry).

**Result:** **53 entries - 48 PASS, 4 FAIL, 1 SKIP.** Every FAIL is explained below; each is
either a stale evidence pin in a lane-owned file or a tree-state drift in a verifier baseline -
none is a regression of a delivered artifact, and none is hidden. (The run header timestamp
comes from the host clock, which is skewed in this environment; the project wall-clock state is
2026-11-27 per the ledger.)

## 1. How the suite works

```sh
# the whole sweep (~4-5 min)
cd opensource/docs/incoming/wifi-closeout && ./tools/run-host-suite.sh
# subsets
./tools/run-host-suite.sh --fast                 # skip the kernel-module builds
./tools/run-host-suite.sh --external             # also run the tarball release-line validator
./tools/run-host-suite.sh --only wifi-ap-start   # substring filter on the entry name
./tools/run-host-suite.sh --list                 # print entries
```

Runner sha256: `cc245164328b8ff8b0c154b458f11d23b314676aa56ff23d4a12f233adffdc81`. Each entry is a lane's own committed entry point, run from that lane's
directory with its own output captured; a non-zero exit is a FAIL unless the entry exits 77
(SKIP = missing local prerequisite). The runner is deliberately **not** a replacement for the
lane verifiers - it is the one-command way to exercise all of them at close-out.

## 2. Full table

| # | entry | command | result | pass count | note |
| --- | --- | --- | --- | --- | --- |
| 1 | `wifi-bench-tooling.selftest` | `./tools/run-selftest.sh` | **PASS** | 563/563 | - |
| 2 | `wifi-bench-session.selftest` | `./tools/selftest.sh` | **FAIL** | 20 passed | stale artifact pin (merged module `ec74fccd` wanted; committed/fired module is `ca32aec4`) - the package's own verifier correctly fails; re-pin `artifacts.env` before firing |
| 3 | `wifi-bench-session.artifacts` | `./tools/check-artifacts.sh` | **FAIL** | - | same stale pins + the AP-module pin (`11511980` wanted; committed v7 is `fd8e1439`, combined is `44c6cc12`) - re-pin before firing |
| 4 | `wifi-bench-tooling.fw-builder` | `$PY tools/fw_patch_builder.py --selftest` | **PASS** | 27/27 PASS | - |
| 5 | `wifi-fw-audit.pd4` | `$PY tools/verify_pd4.py --selftest` | **PASS** | 14/14 PASS | - |
| 6 | `wifi-fw-audit.variants` | `$PY tools/pd4_variants.py --selftest` | **PASS** | 14/14 PASS | - |
| 7 | `wifi-ete-engine.selftest` | `$PY ete_engine.py selftest` | **PASS** | 150/150 PASS | - |
| 8 | `wifi-ete-wake.selftest` | `$PY ete_wake.py selftest` | **PASS** | 81/81 | - |
| 9 | `wifi-rom-s01.selftest` | `$PY tools/rom_s01.py selftest` | **PASS** | 70/70 PASS | - |
| 10 | `wifi-s1-cpu1.selftest` | `$PY tools/s1_cpu1.py selftest` | **PASS** | 46/46 PASS | - |
| 11 | `wifi-rom-dump.selftest` | `$PY tools/romdis.py selftest` | **PASS** | 86/86 PASS | - |
| 12 | `wifi-chip-reset.selftest` | `$PY chip_power.py selftest` | **PASS** | 13/13 PASS | - |
| 13 | `wifi-hcc-service.selftest` | `$PY tools/frame_selftest.py` | **PASS** | - | - |
| 14 | `wifi-d2h-direct.selftest` | `$PY tools/d2h_selftest.py` | **PASS** | 92/92 | - |
| 15 | `wifi-ete-frames.verify` | `$PY tools/verify-frames.py` | **PASS** | - | - |
| 16 | `wifi-datapath.tables` | `$PY tools/fw_dp_tables.py --selftest` | **PASS** | 35 passed | - |
| 17 | `wifi-datapath.build` | `./tools/build.sh` | **PASS** | 77 passed | - |
| 18 | `wifi-h2d-frames.build` | `./tools/build.sh` | **PASS** | 233 passed | - |
| 19 | `wifi-h2d-inject.selftest` | `./tools/build.sh --selftest-only` | **PASS** | 63 passed | - |
| 20 | `wifi-hcc-core.host` | `./tools/build.sh --no-kernel` | **PASS** | - | - |
| 21 | `wifi-vap-scan.build` | `./tools/build.sh` | **PASS** | 137/137 | - |
| 22 | `wifi-vap-start.build` | `./tools/build.sh` | **PASS** | 61/61 | - |
| 23 | `wifi-ap-start.build` | `./tools/build.sh` | **PASS** | 128 passed | - |
| 24 | `wifi-ap-start.re` | `$PY tools/ap_start_re.py --selftest` | **PASS** | - | - |
| 25 | `wifi-scan-d2h.selftest` | `./tools/selftest.sh` | **PASS** | 85/85 | - |
| 26 | `wifi-proto-seq.selftest` | `./tools/selftest.sh` | **PASS** | 55/55 | - |
| 27 | `wifi-openwrt-pkg.selftest` | `./tools/selftest.sh` | **PASS** | 92/92 | - |
| 28 | `wifi-hostapd-deploy.check` | `./deploy-hostapd.sh --check --evidence /tmp/wifi-closeout-hostapd-check.txt` | **PASS** | 88 passed | - |
| 29 | `wifi-hostapd-deploy.selftest` | `./tools/selftest.sh --evidence /tmp/wifi-closeout-hostapd-selftest.txt` | **PASS** | 14 passed | - |
| 30 | `wifi-hostapd-deploy.ladder` | `./tools/check-ladder.py` | **PASS** | 107 passed | - |
| 31 | `wifi-image-deploy.selftest` | `./tools/selftest.sh` | **FAIL** | 80 passed | local T0 baseline changed underneath: `/home/shibbityshwab/wifi-image-integration-artifacts/` now holds the **self-contained final-image** set (combined kmod `59286d63`), so the old container `df6a78c3` and kmod `aac8f3fa` are absent - re-baseline after the final-image plan lands |
| 32 | `wifi-image-integration.selftest` | `./tools/selftest.sh` | **PASS** | - | - |
| 33 | `wifi-abi-image.selftest` | `./tools/selftest.sh` | **PASS** | 89 passed | - |
| 34 | `wifi-re-report.verify` | `python3 tools/verify_report.py` | **PASS** | 172/172 | - |
| 35 | `wifi-driver-clean.build` | `./tools/build.sh` | **PASS** | 133/133 | - |
| 36 | `wifi-h2d-send.build` | `./tools/build.sh` | **PASS** | 251/251 | - |
| 37 | `wifi-cfg80211.build` | `./tools/build.sh` | **FAIL** | 242/242 | host checks pass **242/242**; the FAIL is step 4, the provider-symbols baseline no longer matches the current kernel tree `Module.symvers` (6 symbols already exported) = tree-state drift, regenerate with `--update` |
| 38 | `wifi-scan-ap.build` | `./tools/build.sh` | **PASS** | 317/317 | - |
| 39 | `wifi-scan-complete.build` | `./tools/build.sh` | **PASS** | 373/373 | - |
| 40 | `wifi-datapath-bringup.build` | `./tools/build.sh` | **PASS** | 308/308 | - |
| 41 | `wifi-dp-txrx.build` | `./tools/build.sh` | **PASS** | 12 passed | - |
| 42 | `wifi-dp-air.build` | `./tools/build.sh` | **PASS** | 48/48 | - |
| 43 | `wifi-hcc-core.build` | `./tools/build.sh` | **PASS** | - | - |
| 44 | `wifi-vap-start.kernel` | `./tools/kernel-check.sh` | **PASS** | - | - |
| 45 | `wifi-h2d-frames.kernel` | `./tools/kernel-check.sh` | **PASS** | - | - |
| 46 | `wifi-ap-start.kernel` | `./tools/kernel-check.sh` | **PASS** | - | - |
| 47 | `target-skeleton.mac-provisioning` | `bash tools/test-mac-provisioning.sh` | **PASS** | - | - |
| 48 | `target-skeleton.mac-nvmem` | `bash tools/test-mac-nvmem.sh` | **PASS** | - | - |
| 49 | `upstream-series.gen-check` | `python3 tools/gen-series.py --check` | **PASS** | - | - |
| 50 | `upstream-series.release-line` | `./run-release-line-check.sh` | SKIP | local OpenWrt tarball prerequisite (run with --external) | SKIP by design (rc 77): needs the pristine `linux-6.18.60.tar.xz` (present as an OpenWrt DL input on this host, not a repo artifact); run `./tools/run-host-suite.sh --external` to execute the 51-patch release-line validator |
| 51 | `platform.check-lab-src` | `bash tools/check-lab-src.sh` | **PASS** | - | - |
| 52 | `platform.check-init-shadow` | `bash tools/check-init-shadow.sh` | **PASS** | - | - |
| 53 | `platform.bench-inventory-tests` | `bash tools/tests/bench-inventory.test.sh` | **PASS** | 145/145 assertions | - |

## 3. The four FAILs, exactly

1. **`wifi-bench-session.selftest`** (20 passed, 1 failed): the package's committed
   `artifacts.env` still pins the **pre-revision** merged module (`ec74fccd`, 884,576 B) while
   the committed artifact is the revision-2 merged module `ca32aec4` (886,864 B).
2. **`wifi-bench-session.artifacts`**: same stale pin, plus the **pre-v7** AP module
   (`11511980`, 770,304 B) while the committed v7 module is `fd8e1439` (206,708 B, debug
   stripped) and the combined AP+DP module is `44c6cc12` (242,572 B). This is a real pre-fire
   action item, not a verifier bug: the package correctly refuses to run against stale pins.
   **Action:** re-pin `artifacts.env` (and the lane's pin table) to the current artifacts.
3. **`wifi-image-deploy.selftest`** (80 passed, 25 failed): the lane verifies a live T0
   baseline from `/home/shibbityshwab/wifi-image-integration-artifacts/`; that directory was
   regenerated for the **self-contained final image** (container `ae5e28bf`, combined kmod
   `59286d63`), so the old baseline it wants (container `df6a78c3`, kmod `aac8f3fa`) is no
   longer present. The lane's selftest is telling the truth: the T0 zero-flash deployment path
   is superseded by the final image (no /tmp deployment needed). **Action:** re-baseline the
   lane against the final image (or keep the old artifacts locally pinned if T0 must be
   replayed on the df6a78c3 image).
4. **`wifi-cfg80211.build`**: the host-side checks pass **242/242**; the FAIL is the
   provider-symbols reproducibility step - the committed `cfg80211-provider.symvers` baseline
   no longer matches the current kernel tree's `Module.symvers` (6 cfg80211 symbols are now
   exported there). Tree-state drift in the verification environment, not a driver regression.
   **Action:** regenerate the baseline with the lane's `--update` against the current tree, or
   run the check against the frozen base tree.

## 4. The SKIP

- **`upstream-series.release-line`** exits 77 by design: the release-line validator needs a
  pristine `linux-6.18.60.tar.xz` as an input (it is an OpenWrt build input on this host, not a
  repository artifact). Run `./tools/run-host-suite.sh --external` to execute the 51-patch
  release-line validation (patch(1) apply/fuzz/rejects + the loader hand-off DTS contract).

## 5. What cannot be run host-only (attended-only, by design)

These are real bench actions and are *not* part of this suite: the one-command phase matrix
real fire (`phase_matrix.py --phase all ... --attended`), the AP-start/hostapd ladder, the
air-TX ladder, the T0/rootfs flash, and any module load on the router. Their evidence is the
ledger dates and the local (gitignored) bench artifacts referenced from
[LANE-INDEX.md](LANE-INDEX.md) and [EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md).

## 6. Reading the pass counts

Pass counts are the lane verifiers' own counts. They prove the **host-side artifacts** (builders,
decoders, compute logic, scripts, images, reports) re-verify from the committed tree; they are
**not** hardware evidence. Hardware claims are marked [V*] with a ledger date and live in
[LANE-INDEX.md](LANE-INDEX.md) / [EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md). A green matrix here
means "the close-out content is checkable", not "the radio is finished".
