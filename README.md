# Cudy WR3000 v2.0 - clean-room OpenWrt + Wi-Fi bring-up (curated public half)


This repository is the **curated public half** of a long-running reverse-engineering and
clean-room bring-up project for the **Cudy WR3000 v2.0** router (PCB `WR3000V2-R116`, SoC
HiSilicon **Hi5671YV200** "luofu", Wi-Fi **Hi5622V100** over PCIe, 128 MB NAND / 128 MB DDR3;
the retail label may say "v1" but the PCB is the luofu v2 design - read
[docs/INVENTORY-2026-11-18.md](docs/INVENTORY-2026-11-18.md) before any hardware work).

Two tracks are deliberately kept apart:

1. **Bench reversal + clean-room bring-up** - our kernel, our drivers, the vendor's closed
   Wi-Fi firmware. "It works on our stack" is a bench result, not a release.
2. **Upstream submission** - an OpenWrt target + kernel patch series. Today the platform
   series is a real review-form candidate; the **Wi-Fi driver is not upstreamable** because
   it needs the patched research firmware (see the honest limits below).

## Final state - what is hardware-validated (2026-11-27)

| area | state | evidence |
| --- | --- | --- |
| Platform | our 6.18.60 kernel boots the board from flash with 2 CPUs (DTS loader hand-off fix), persistent UBIFS overlay, both PCIe RCs train, thermal sensor + cooling, first real Ethernet traffic from the PIE/GEMAC path | parent notebook `EVIDENCE-INDEX.md`; [docs/SKELETON-SMOKE.md](docs/SKELETON-SMOKE.md), [docs/SMP-RE.md](docs/SMP-RE.md) |
| Wi-Fi transport + boot protocol | our clean-room PCIe/ETE host boots the Hi5622V100 firmware to the vendor-equivalent golden state, HCC duplex + ready handshake + six-message calibration | [docs/incoming/wifi-driver-clean/REPORT.md](docs/incoming/wifi-driver-clean/REPORT.md), [docs/incoming/wifi-irq-unmask/](docs/incoming/wifi-irq-unmask/) |
| cfg80211 + scan | wiphy + VAP lifecycle; firmware scan returns real results decoded into cfg80211 BSS entries (12 in the first full decode; 13/13 channel coverage needs the long-window session) | [docs/incoming/wifi-re-report/REPORT.md](docs/incoming/wifi-re-report/REPORT.md), [docs/incoming/wifi-scan-complete/README.md](docs/incoming/wifi-scan-complete/README.md) |
| **Live Wi-Fi AP** | **`wlan0: AP-ENABLED`**, SSID `cudy-ap-test`; the four-message AP-start sequence (vap_add 48 B -> vap_start 40 B -> ap_ssid 64 B -> ap_cfg 184 B) is consumed by the firmware; runs both on the /tmp-deployed module and on the flashed image with native kmods, and persists for minutes (a boot-absent firmware D2H family, `g1 id0x23` 24 B, appears only after the AP start - indirect evidence, not an on-air proof) | [docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md](docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md), [docs/incoming/wifi-hostapd-deploy/README.md](docs/incoming/wifi-hostapd-deploy/README.md) |
| MAC data plane | TXQ-table address sync + TX/RX rings set up; first frame published to the MAC TX ring (**POST-ONLY**) - also with the combined AP+DP module; the MAC consumer does not advance rptr, so TX completion/RX belongs to the open MAC-TX-engine work | [docs/incoming/wifi-dp-air/LADDER-F-AIR.md](docs/incoming/wifi-dp-air/LADDER-F-AIR.md) |
| Automation | **one command runs the whole bring-up matrix**: boot / C2 calibration / D1 VAP / D2 scan / E1 data-plane = **all PASS** (C2 54/54, D1 21/21, D2 real results, E1 37/37) | [docs/incoming/wifi-bench-tooling/README.md](docs/incoming/wifi-bench-tooling/README.md) |
| Image | bench image **reproduced byte-for-byte** (container `df6a78c3`, kernel slice `13e81214`, slot `1794a531`, rootfs UBI `62344e80`), ABI-correct rootfs (kernel pin `6f995278`), native `iw` 6.17 / `iwinfo` / `wireless-regdb` / hostapd package set, flashed attended and booted with native `modprobe` from `/lib/modules`; **self-contained final image** built host-only (container `ae5e28bf`, rootfs UBI `ff9f3e1c`, combined driver + hostapd + ucode/wifi-scripts, kernel slice unchanged) - the next flash target | [docs/incoming/wifi-abi-image/FLASH-RUNBOOK.md](docs/incoming/wifi-abi-image/FLASH-RUNBOOK.md), [docs/incoming/wifi-image-integration/README.md](docs/incoming/wifi-image-integration/README.md) |
| Publication/upstream | platform review-form series 001-026 validated (apply/compile/checkpatch); publication checklist is **not clean** (history purge pending, vendor objects in old history, link/license issues) | [docs/incoming/wifi-upstream-final/PUBLICATION-CHECKS.md](docs/incoming/wifi-upstream-final/PUBLICATION-CHECKS.md), [docs/PUBLISH-READINESS.md](docs/PUBLISH-READINESS.md), [upstream/series-6.18/README.md](upstream/series-6.18/README.md) |

Claim-by-claim evidence: the Wi-Fi umbrella report and its index
([REPORT.md](docs/incoming/wifi-re-report/REPORT.md),
[EVIDENCE-INDEX.md](docs/incoming/wifi-re-report/EVIDENCE-INDEX.md), EV-001..EV-277 at report
v4 + the residual EV-278..EV-281 rows in [EVIDENCE-REFRESH.md](docs/incoming/wifi-closeout/EVIDENCE-REFRESH.md)), the
lane map in [LANE-INDEX.md](docs/incoming/wifi-closeout/LANE-INDEX.md), and the parent
notebook's ledger (`mem-entries.md`, newest dated entry wins). The host-only verifier suite
summary is [TEST-MATRIX.md](docs/incoming/wifi-closeout/TEST-MATRIX.md).

## Honest limits (read these before quoting any success)

1. **The Wi-Fi path requires the PD4-patched research firmware.** The vendor blob's mailbox
   dispatch is not programmable host-side; the patched stub is consume-only (the live handler
   table is empty; the working duplex runs through the non-mailbox ETE/polled path). PD4 is a
   research instrument and must never ship. See
   [docs/incoming/wifi-fw-audit/REPORT.md](docs/incoming/wifi-fw-audit/REPORT.md).
2. **There is no upstreamable Wi-Fi driver yet.** The clean-room driver is a research artifact;
   the vendor GPL source request is unanswered
   ([docs/GPL-REQUEST.md](docs/GPL-REQUEST.md)). Do not present the Wi-Fi work as a submission.
3. **The AP milestone is a host-side "AP-ENABLED".** Beacon-on-air, client association and
   WPA2 have not been verified with an external client (no second device on the bench yet);
   the post-AP firmware event family is indirect evidence only. The MAC TX frame is POST-ONLY
   on both the data-plane module and the combined AP+DP module: the host publishes correctly
   but the MAC consumer does not advance rptr - the vendor TX-engine start/config (MAC/PIE/EQM
   port setup or a firmware TX-queue start) is [U].
4. **The bench is attended.** Flashing and any device action need hands on the box, a ready
   recovery door and the fail-closed tooling; there is no remote/automatic fire path. The LAN
   switch/PIE drivers belong to the target patch series, not to a release.
5. **Publication policy is binding:** no vendor binaries, firmware blobs, ROM/calibration bytes
   or secrets in tracked text; the ledger is append-only; the curated repos are the publish
   surface. The pre-2026-11-17 public history is **not clean** and still needs a purge
   ([docs/PUBLISH-READINESS.md](docs/PUBLISH-READINESS.md),
   [docs/PUBLISH-PURGE-RUNBOOK.md](docs/PUBLISH-PURGE-RUNBOOK.md)); see also
   [NOTICE.md](NOTICE.md).

## How to reproduce (host-only first, bench second)

**Prerequisites:** Linux host with the project's lab toolchain; a *local copy* of the vendor
dumps/firmware needed by the reversible steps (gitignored, never published); for the bench
itself the physical router, UART (read-only), the bench Ethernet link and a human.

1. **Reproduce the analysis and host-side builds.** Each lane directory is self-contained:
   `cd docs/incoming/<lane> && ./tools/build.sh` (or `./tools/selftest.sh`). Every lane's
   `README.md` has its exact command line and expected counts. The close-out lane runs all of
   them in one sweep: `docs/incoming/wifi-closeout/tools/run-host-suite.sh` (results in
   [TEST-MATRIX.md](docs/incoming/wifi-closeout/TEST-MATRIX.md)).
2. **Reproduce the umbrella analysis.** The Wi-Fi report is host-only: its verifier
   (`docs/incoming/wifi-re-report/tools/verify_report.py`) re-checks the claim/evidence
   structure; the report's `REPRODUCE.md` has the per-section details.
3. **Reproduce the image.** The bench image builds from the OpenWrt tree with the target
   skeleton: `docs/incoming/wifi-abi-image/tools/build-abi-image.sh` (serialized, restores the
   shared tree, verifies from bytes) and
   `docs/incoming/wifi-image-integration/tools/build-image.sh`. The self-contained final image
   is the `78e30e8` revision of the integration lane (combined driver + hostapd packaged).
   Expected hashes are in those lanes' BUILD-NOTES/REPORT and in the close-out evidence rows
   ([EVIDENCE-REFRESH.md](docs/incoming/wifi-closeout/EVIDENCE-REFRESH.md) EV-279).
4. **Run the bench matrix (attended only).**
   `cd docs/incoming/wifi-bench-tooling && python3 tools/phase_matrix.py --phase all --yes-really --attended --reboot-between`
   - it boots the patched firmware, runs the five phases with fail-closed gates, reboots
   between phases and writes per-phase evidence. Read
   [docs/incoming/wifi-bench-tooling/PHASES.md](docs/incoming/wifi-bench-tooling/PHASES.md)
   first; every write is gated on the bench link, recovery readiness and attendance.
5. **Run the AP.** The exact hostapd + module + PD4 firmware sequence and expected dmesg are
   in [docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md](docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md)
   (sec 8/12) with the package deploy in
   [docs/incoming/wifi-hostapd-deploy/DEPLOY.md](docs/incoming/wifi-hostapd-deploy/DEPLOY.md).
6. **Flash (optional, attended).** Only the ABI-correct image from step 3: read
   [docs/incoming/wifi-abi-image/FLASH-RUNBOOK.md](docs/incoming/wifi-abi-image/FLASH-RUNBOOK.md)
   end to end. Keep one slot stock; recovery is physical. The 2026-11-27 flash of
   `df6a78c3` is the reference run (ledger 2026-11-27 06:30).

**Never:** write host-to-router UART bytes (the TX line is dead), blind-flash a slot, replace
`/etc/rc.d/luofu` wholesale, or publish vendor bytes. Hazards:
[docs/HAZARDS.md](docs/HAZARDS.md).

## Repository layout

| path | what it is |
| --- | --- |
| `docs/` | project docs: target design, inventory, specs, roadmaps, publication policy |
| `docs/incoming/` | the lane deliverables (this is where the 2026-11 Wi-Fi work lives; see [LANE-INDEX.md](docs/incoming/wifi-closeout/LANE-INDEX.md)) |
| `target-skeleton/` | OpenWrt target skeleton: DTS, config, image recipe, A/B install path, port notes ([README](target-skeleton/README.md)) |
| `upstream/series-6.18/` | the kernel review-form submission series + release line + validation ([README](upstream/series-6.18/README.md)) |
| `lab/` | legacy lab drivers and analysis artifacts (kept for provenance) |
| `tools/` | host tools (calibration snapshot/restore, module patching, publish checks) |

## Legal and provenance

Tools and documentation are MIT licensed ([LICENSE](LICENSE)); kernel code is GPL-2.0
(each file carries `SPDX-License-Identifier: GPL-2.0`). Vendor firmware and modules are **not**
included and remain the vendor's; the vendor Wi-Fi module declares `license=GPL` while
shipping as a binary, and the GPL source request status is in
[docs/GPL-REQUEST.md](docs/GPL-REQUEST.md). Reverse-engineered work is derived from
GPL-licensed binaries by disassembly, and is published as GPL-2.0 - this is **not** a
"clean-room from documents" claim for the drivers. The Wi-Fi research instrument and the
PD4-patched firmware are never published as bytes. See [NOTICE.md](NOTICE.md).

## Reading order

1. [LANE-INDEX.md](docs/incoming/wifi-closeout/LANE-INDEX.md) - the map of every lane and its evidence.
2. [docs/incoming/wifi-re-report/REPORT.md](docs/incoming/wifi-re-report/REPORT.md) - the umbrella Wi-Fi story.
3. [docs/TARGET-DESIGN.md](docs/TARGET-DESIGN.md) + [docs/ROADMAP-OPENWRT.md](docs/ROADMAP-OPENWRT.md) - the upstream target.
4. [docs/SKELETON-SMOKE.md](docs/SKELETON-SMOKE.md) + [upstream/series-6.18/README.md](upstream/series-6.18/README.md) - the platform series.
5. [docs/incoming/wifi-upstream-final/README.md](docs/incoming/wifi-upstream-final/README.md) - the submission finalization package.
6. The parent notebook's `STATUS.md` + `mem-entries.md` - the live state and the full history.

## Glossary (the short version)

**golden boot/page** = the vendor-equivalent firmware boot state (`0x86807FEF`, state 1,
mask 0); **HCC** = the firmware/host message channel; **H2D/D2H** = host-to-device /
device-to-host frames; **g0/g1/g2 idN** = message group/id; **vap_add/vap_start** = the
firmware VAP object create/start messages; **mode-1 VAP base** = the g0 id1 key 0x0064/0x0065
pair; **ap_ssid/ap_cfg** = the AP config messages (keys 0x0005/0x0127); **PD4** = the
research firmware patch that services the firmware mailbox; **C2/D1/D2/E1** = the phase-matrix
calibration / VAP / scan / data-plane phases; **POST-ONLY** = a frame published to the MAC TX
ring but not yet consumed/completed by the radio; **kmod-ABI pin** = the kernel package version
the modules require (`6f995278`).
