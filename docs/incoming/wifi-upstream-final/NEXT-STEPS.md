# Next steps (2026-11-27)

Ordered actions after this finalization package. "Host" = no device; "Attended" = human at the
bench, recovery door armed, read-only UART capture; "Owner" = project owner/Lead decision.
Nothing below was executed by this lane.

## P0 - submission artefact (host-only, except one boot)

| # | action | kind | gate / evidence |
| --- | --- | --- | --- |
| 1 | Make the v2 fold content changes in the L0 patches: 013a -> 0013 as DT `reset-gpios` (+ binding), folds 027/038/041/044/048/051/053/066/067, and the four new RFC entries from 031/032/036/043/046/059/060 | host | [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) section 5; fold decisions in [../wifi-upstream-target/PATCH-CLASSIFICATION.md](../wifi-upstream-target/PATCH-CLASSIFICATION.md) |
| 2 | Add the PCIe endpoint `reset-gpios` to the single-source board DTS and regenerate DTS-0001 | host | [DTS-0001-FINAL.md](DTS-0001-FINAL.md) section 4 |
| 3 | Add 013a to L1 (`release-line/INCLUDED.txt` + `manifest.tsv`, 55 -> 56) and re-run `tools/check-release-line.sh` on pristine `linux-6.18.60` | host | release-line validator PASS 56/56 |
| 4 | Regenerate the v2 review form (30 + DTS-0001), re-run the full host validation and refresh `validation/sha256sums.txt` + `submission/MANIFEST.sha256` + the submission package docs | host | acceptance checklist in [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) section 6 |
| 5 | **One attended boot** of the `reset-gpios` DTS change (2 CPUs, both PCIe endpoints, NAND/UBI, serial) | attended | project rule: active-DTS changes need a boot test |
| 6 | Post the **platform** series (kernel group only; Wi-Fi explicitly out of scope) | owner | go/no-go gates in [UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md) section 9; statement in [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) section 5 |

## P1 - image / install closure (attended)

| # | action | kind | gate / evidence |
| --- | --- | --- | --- |
| 7 | Execute the **kernel+rootfs-from-one-build flash** | attended | [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md); ABI receipt `6.18.60~6f995278...-r1` |
| 8 | T0 full-set kmod deploy after the ABI receipt (cfg80211 + driver kmods) | attended | the fail-closed install now passes; [../wifi-image-deploy/BENCH-VALIDATION.md](../wifi-image-deploy/BENCH-VALIDATION.md) |
| 9 | Rebuild the image with hostapd + `wifi-scripts` and a persistent AP bench setup (rootfs-only change) | host build + attended flash | [../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md); [../wifi-ap-start/image/wifi-scripts-ucode.md](../wifi-ap-start/image/wifi-scripts-ucode.md) |
| 10 | Run the target `platform_do_upgrade` path end to end on a recovered unit (target-PR gate) | attended | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) section 4d |
| 11 | Target-PR config cleanup (remove `CONFIG_CMDLINE_FORCE` lab cmdline/gates, `CONFIG_DEVMEM`, decide `ARCH_HISI` set, wifi package metadata) | host | [../wifi-upstream-target/CONFIG-AUDIT.md](../wifi-upstream-target/CONFIG-AUDIT.md) |

## P2 - Wi-Fi research closure (bench research, not submission)

| # | action | kind | gate / evidence |
| --- | --- | --- | --- |
| 12 | Prove over-the-air beaconing (beacon D2H events or air capture) | attended | [AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) section 7 |
| 13 | Radio-up data-plane completion/RX on the started VAP (reuse dp-air merged module + F-AIR ladder) | attended | [../wifi-dp-air/LADDER-F-AIR.md](../wifi-dp-air/LADDER-F-AIR.md) |
| 14 | Close WPA2 (`add_key`/key install, M3) so the AP fixture is not open-only | driver research | [../wifi-scan-ap/](../wifi-scan-ap/README.md) |
| 15 | Full 13-channel scan with a sufficient window (`scan_timeout_ms`) | attended | [../wifi-scan-complete/](../wifi-scan-complete/README.md) |
| 16 | Optional: the host-clean boot-path experiment (spare inbound iATU viewport onto CA `0x40160000` + one-shot event 0x4C raise) - new device-write class, own attended design/recovery card | attended research | [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md); [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) section 4 (Option B) |
| 17 | Keep the umbrella RE report at v4 (AP milestone) and fix the one cross-lane hash drift in its verifier | host | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) 2.3 |

## P3 - publication (owner)

| # | action | kind | gate / evidence |
| --- | --- | --- | --- |
| 18 | Choose the publication path: fresh curated repos (recommended) or legacy history purge | owner | `../../PUBLISH-PREP.md` C0-C4; `../../PUBLISH-PURGE-RUNBOOK.md` |
| 19 | Close checklist items 1-9: licenses/DCO/provenance, Wi-Fi+LAN-switch statement, single DTS, links, checker false positive | owner+host | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) section 4 |
| 20 | Re-run `publish-prep-check.sh --strict` on a clean full clone and record the release log entry | owner+host | same |
| 21 | GPL request: leave as the dated no-reply record, or escalate with a stored sent copy + follow-up | owner | [../../GPL-REQUEST.md](../../GPL-REQUEST.md); [GPL-PD4-STATUS.md](GPL-PD4-STATUS.md) section 1 |

## P4 - parent-repository bookkeeping (Lead)

Append the 2026-11-27 ledger entry + regenerate `mem-entries-INDEX.md`; add the AP milestone and
this finalization to `STATUS.md` and regenerate it; update `EVIDENCE-INDEX.md`; run
`tools/check-repo.sh`; commit the parent repo (submodule pointer + ledger/STATUS). Decide the
untracked `opensource/docs/incoming/wifi-ready-emit/` lane output. See
[PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) section 5.
