# Evidence map - claims in this package

Project evidence markers: **[V]** verified on hardware or from a named committed artifact,
**[V\*]** verified in a bench session with read-back, **[I]** inferred, **[U]** unmeasured,
**[X]** blocked/excluded. The authoritative live record is the parent-repo ledger
`mem-entries.md` (newest dated entry wins) and `STATUS.md`; the umbrella narrative is
[../wifi-re-report/REPORT.md](../wifi-re-report/REPORT.md) (v4 in flight, task-129).
This package cites the owning lanes and the ledger directly so it does not depend on that
revision.

## 1. Final-state claims (2026-11-27)

| # | claim | marker | primary evidence |
| --- | --- | --- | --- |
| E1 | First live Wi-Fi AP on the clean-room stack: hostapd `AP-ENABLED`, SSID `cudy-ap-test`, 4 AP-start messages consumed in 0 polls, VAP ptr ctx0+0x190=`0x01010F18`, ctx0+0x18C=`0x70094810`, page `0x86807FEF` golden, mask 0 | [V\*] | parent ledger **2026-11-27 04:30**; [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) |
| E2 | AP-start provider module v7 `fd8e1439...` (206,708 B) from base `b42ff4ec...`; commit `9b180f3` | [V] | [../wifi-ap-start/README.md](../wifi-ap-start/README.md), [../wifi-ap-start/BUILD-NOTES.md](../wifi-ap-start/BUILD-NOTES.md); lane selftest 128/128 re-run in [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |
| E3 | First data-plane frame published to the MAC TX ring = POST-ONLY (wptr 1, rptr 0, completion_pending=1) | [V\*] | parent ledger **2026-11-27 02:30**; [../wifi-dp-air/LADDER-F-AIR.md](../wifi-dp-air/LADDER-F-AIR.md) |
| E4 | One-command bring-up matrix: boot/C2/D1/D2/E1 all PASS | [V\*] | parent ledger **2026-11-26 17:00**; evidence `build/wifi-bench-phase-matrix/20261124T094716Z/` (local, gitignored); [../wifi-bench-tooling/](../wifi-bench-tooling/) |
| E5 | 13-channel scan: 12 BSS entries decoded, host-timeout at ~3.5 s/channel; scan `results` proven earlier (6/13) | [V\*] | parent ledger **2026-11-26 14:00/16:30**; [../wifi-scan-complete/](../wifi-scan-complete/README.md) |
| E6 | Bench image reproduced byte-for-byte: container `df6a78c3` 6,533,439 B, kernel slice `13e81214` 3,372,638 B, slot `1794a531`, rootfs UBI `62344e80`, ABI-correct rootfs kernel= `6f995278...` | [V] | [../wifi-abi-image/REPORT.md](../wifi-abi-image/REPORT.md); parent ledger **2026-11-26 18:00** |
| E7 | T0 userspace zero-flash deploy PASS (49/49 gates; native iw 6.17/iwinfo/regdb persistent) | [V] | parent ledger **2026-11-26 12:00**; [../wifi-image-deploy/BENCH-VALIDATION.md](../wifi-image-deploy/BENCH-VALIDATION.md) |
| E8 | hostapd package set host-verified (88/88 + 14/14 + 107/107); zero-flash deploy path; not in the flashed image; ucode workaround | [V] host | [../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md); re-run in [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |
| E9 | The AP milestone requires the PD4-patched firmware; no vendor-clean boot path proven | [V]/[I] | [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md); parent ledger 2026-11-25 21:30; [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) |
| E10 | GPLv2 request sent 2026-10-01, no reply held, no receipt copy | [V] record | [../../GPL-REQUEST.md](../../GPL-REQUEST.md) status header; `NOTICE.md` GPL note |
| E11 | Platform series: L0 67 / L1 55 (needs 56) / L2 v1 26+DTS-0001 frozen 2026-11-22; v2 = 30+DTS-0001 planned | [V] | [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md); [../wifi-upstream-target/PATCH-CLASSIFICATION.md](../wifi-upstream-target/PATCH-CLASSIFICATION.md) |
| E12 | v1 is stale: 013a committed and load-bearing; DTS-0001 still carries the retired hog; L1 lacks 013a | [V] | hashes/greps re-run 2026-11-27, records in [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) section 3 and [DTS-0001-FINAL.md](DTS-0001-FINAL.md) |
| E13 | Target tree build-verified 2026-11-24 (defconfig/kernel/world RC=0) | [V] host | [../wifi-upstream-target/BUILD-VERIFICATION.md](../wifi-upstream-target/BUILD-VERIFICATION.md) |
| E14 | `platform_do_upgrade` never ran end to end; kernel+rootfs flash runbook not executed | [U] | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md); [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md) |
| E15 | Publication NOT READY: history vendor blobs, licenses/DCO, statements, DTS copies, links | [V] checker | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md); `../../PUBLISH-READINESS.md`, `../../PUBLISH-PREP.md` |
| E16 | `gen-series.py --check` clean (26 patches) re-run this session | [V] host | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) section 1 |
| E17 | PD4 audit: byte-identical rebuild, minimal two segments, cave safety, H1 consume-only, ranked variants, alternatives | [V] | [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md); harness 14/14+14/14 re-run |
| E18 | Calibration/C2, cfg80211 (wiphy/VAP), real management frames, scan: earlier milestones | [V\*] | parent ledger 2026-11-25/26 entries; [../wifi-re-report/REPORT.md](../wifi-re-report/REPORT.md) sections 6.1-6.8 |

## 2. Parent-ledger entries cited (dates only; the ledger is authoritative)

* **2026-11-25** 05:10/16:30/18:30/19:30/21:30 - duplex loop, six-calibration boot, VAP/scan
  frames, data-plane host side, PD4 H1 consume-only.
* **2026-11-26** 14:00/15:00/16:30/17:00/18:00/12:00/20:30 - first scan results, D2 TRUE PASS,
  13-channel scan/BSS decode, one-command matrix ALL PASS, ABI image reproduction, T0 userspace
  PASS, first hostapd run.
* **2026-11-27** 00:30/01:30/02:30/03:30/04:30 - air-TX POST-ONLY, AP capability fixes (v4/v5),
  first TX publish, v6 start_ap + firmware crash, **v7 AP-ENABLED milestone**.

## 3. Commits (opensource submodule) relevant to this package

| commit | content |
| --- | --- |
| `9b180f3` | wifi-ap-start v7 - the AP milestone module/patches |
| `59f0c19` / `910b242` / `41fde23` / `a7771ae` / `0372648` | AP capability fix iterations (v6/v5/v4/v3/v2) |
| `a32f028` | hostapd package + zero-flash deploy + AP-start ladder |
| `224be72` | ABI image rebuild + flash plan |
| `7bf6e2a` / `a615a8c` | data-plane merge + probe fix |
| `9740946` (recorded in lane report) | upstream-target: target fixes, build verification, patch classification |

## 4. Local (gitignored) evidence paths

`build/abi-image-artifacts/` (image members + SHA256SUMS), `build/evidence-t0-deploy/`,
`build/wifi-bench-phase-matrix/`, `build/evidence-bench-gates/`, lane `build-evidence/`
directories. These are local evidence only and are never committed.
