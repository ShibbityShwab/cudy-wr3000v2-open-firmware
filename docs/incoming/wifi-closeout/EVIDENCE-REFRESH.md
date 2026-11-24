# EVIDENCE REFRESH - residual EV rows after report v4 (final state)

**Lane:** wifi-closeout (task-131), host-only, 2026-11-27.
**State of play:** the umbrella report index
[`../wifi-re-report/EVIDENCE-INDEX.md`](../wifi-re-report/EVIDENCE-INDEX.md) already carries the
final-state milestones: report **v4** (opensource commits `e96a207` + `0df7de0`) extended it
to **EV-245..EV-277** and the coverage is gap-free (see `0). The close-out lane therefore
**does not re-propose that block** - it (a) verifies it, and (b) delivers the residual rows for
the state that landed *after* v4 froze: the combined AP+DP fire, the self-contained final image,
and the close-out deliverables themselves. The residual block starts at **EV-278**.

> **Merge note.** The v4 report lane (re-report4, task-129) owns
> `../wifi-re-report/EVIDENCE-INDEX.md`. Append the EV-278.. block below (keep it contiguous
> after the current max; if another lane adds rows first, renumber). The parent repo's root
> `EVIDENCE-INDEX.md` is the Lead's file: the rows to append are in `2 and no file here
> touches it.

## 0. Current-index verification (re-run at close-out)

```sh
# coverage: EV ids contiguous from EV-001 to the newest row?
grep -o 'EV-[0-9]\{3\}' opensource/docs/incoming/wifi-re-report/EVIDENCE-INDEX.md | sort -u |
  awk -F- '{n=$2+0; if(n>max)max=n; a[n]=1} END {miss=0; for(i=1;i<=max;i++) if(!a[i]){printf "missing %d ",i; miss=1}
  if(!miss) printf "EV-001..EV-%03d contiguous, %d ids\n", max, max}'
```

Result at close-out: **EV-001..EV-277 contiguous, 277 ids** (report v4 state; commits
`e96a207`/`0df7de0`). The block below extends it as **EV-278..EV-281** without gaps.

Coverage check of the v4 block against the final state (grep of the index): the v4 rows cover
the AP milestone (EV-246/253-262), real scan results/BSS decode (EV-247/257/258/264), the
all-PASS matrix (EV-264), the ABI image flash + AP on the flashed image (EV-250/262/272), the
AP persistence + the new `g1 id0x23` family (EV-275/276), the H1/PD4 and honest limits
(EV-269/271/276) and the upstream-final package (EV-277). **Not covered by v4** (hence the
residual block): the 10:30 combined AP+DP fire outcome, the 12:30 self-contained final image
(hashes/contents/flash decision), and the close-out deliverables/verifier suite.

## 1. Suggested residual rows for `wifi-re-report/EVIDENCE-INDEX.md`

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-278 | 6.12 | **combined AP+DP module fire (2026-11-27 10:30)**: module **242,572 B sha256 44c6cc12...** (ap-start v7 feature set + the wifi-dp-air dp-txrx merge, committed opensource `bacd3c1`); one boot runs C2 clean -> iftype `__ap` -> hostapd **AP-ENABLED** (ssid `cudy-ap-test`) with the four AP-start frames consumed -> `dp_probe` OK (0x20) -> `dp_setup` OK (rings 256/512/4095, TXQ table devva, 14 MAC regs, MSDU table) -> key-0x0320 TXQ sync OK (ok=3) -> `dp_tx_frame` published (wptr 0->1, table wptr=1 rptr=0, completion_pending=1) = **POST-ONLY** -> `dp_rx_poll` 0 captures; page 0x86807FEF golden, mask 0, staging 0. **Conclusion (not a regression):** the host publishes correctly; the MAC device consumer does not advance rptr - the MAC TX engine needs configuration/start beyond the current lanes (vendor TX path likely needs MAC/PIE/EQM port setup or a firmware-side TX-queue start) - **[U]** | [V*] | `wifi-ap-start/REPORT.md` (combined-module section), `wifi-dp-air/LADDER-F-AIR.md`, TABLES v4 T31 | ledger 2026-11-27 10:30 | - |
| EV-279 | 6.13 | **self-contained final bench image built (2026-11-27 12:30; host-only, nothing flashed)**: rootfs-only rebuild, kernel slice UNCHANGED - container **6,922,559 B sha256 ae5e28bf...**, kernel 3,372,638 B sha256 13e81214... (slot 1794a531..., verifier-asserted; kernel .config sha 08b6e595 unchanged), rootfs UBI **3,538,944 B sha256 ff9f3e1c...**, squashfs sha 1dbd0d19..., **manifest 83 packages**; contents: iw 6.17, ucode iwinfo, wifi-scripts (incl. hostap UCs), hostapd-mini v2.12 + hostapd-common (504,333 B sha 13cb3a43), ucode-mod-nl80211/rtnl, wireless-regdb, cfg80211.ko and the **combined dp-air+AP-start v7 hi5622_wifi.ko** (artifact 242,572 B sha 44c6cc12; installed stripped 231,132 B sha 51608fd5, AP+dp attrs verified) - a fresh flash needs NO /tmp deployment; T0 full-set is no longer needed. Flash decision: the stock-telnet transport supports rootfs-only in practice (kernel bytes identical); strict rootfs-only needs a small `--rootfs-only` flag or the manual DEPLOY.md sequence; overlay wiped; rollback = previous rootfs UBI 62344e80 or stock pair A | [V] | `wifi-image-integration/README.md` + BUILD-NOTES, `wifi-image-deploy/DEPLOY.md`; local `/home/shibbityshwab/wifi-image-integration-artifacts/SHA256SUMS` | ledger 2026-11-27 12:30 (commit 78e30e8; host-only) | lane selftest 72/72 |
| EV-280 | 12 | **the close-out lane delivered the consolidated project map**: `wifi-closeout/LANE-INDEX.md` (every Wi-Fi + platform lane -> task/commit/deliverable/evidence/status, incl. the upstream-final and driver/platform lines), `EVIDENCE-REFRESH.md` (this file: v4 coverage verification + EV-278.. block + root-index rows + v3/v4 corrections), `OVERVIEW-README.md` (proposed `opensource/README.md`), `TEST-MATRIX.md` (host-only verifier sweep), `PUBLICATION-NOTES.md` (publish surface NOT clean: history purge + 9 fails, citing task-130's PUBLICATION-CHECKS.md), `STATUS-NEXT-ACTION.md` (proposed STATUS.md sec 3m/4 + ledger entry); every delivered file is text, no vendor bytes, no device contact | [V] | `wifi-closeout/` (this directory) | - | **yes** (`tools/verify-closeout.py`) |
| EV-281 | 9.2 | **the host-only verifier sweep is green except three known, explained non-PASS rows**: 52-entry suite (see TEST-MATRIX.md) - (a) `wifi-bench-session` selftest + artifact checker FAIL because its committed `artifacts.env` still pins the pre-revision merged module (`ec74fccd`, 884,576 B) and the pre-v7 AP module (`11511980`, 770,304 B) while the committed artifacts are `ca32aec4`/`fd8e1439` (and now the combined `44c6cc12`); action = re-pin before firing; (b) `wifi-cfg80211` build's host checks pass 242/242 but its provider-symbols baseline no longer matches the current kernel tree `Module.symvers` (tree-state drift, needs `--update`); (c) the `upstream-series.release-line` validator is external-prerequisite (needs the pristine `linux-6.18.60.tar.xz`; SKIP by default, `--external` to run). Everything else passes with the exact counts recorded in TEST-MATRIX.md | [V] | `wifi-closeout/TEST-MATRIX.md`, `build-evidence/host-suite/` | - | **yes** (suite run) |

## 2. Suggested rows for the parent repo's `EVIDENCE-INDEX.md`

The root index's Wi-Fi section ends at the 2026-11-22 transport rows (B1/B2). Append these rows
(claim | artifact | status | regenerate), newest state included:

| claim | artifact | status | regenerate |
| --- | --- | --- | --- |
| **First live Wi-Fi AP on our clean-room stack** - hostapd `wlan0: AP-ENABLED`, SSID `cudy-ap-test`; the four AP-start frames (vap_add 48 B / vap_start 40 B / ap_ssid 64 B / ap_cfg 184 B) consumed in 0 polls; VAP pointer `ctx0+0x190`=0x01010F18; live for minutes after start, with the boot-absent `g1 id0x23` 24 B D2H family appearing only after the AP start | `opensource/docs/incoming/wifi-ap-start/{REPORT.md,HOSTAPD-BRINGUP.md}`; ledger 2026-11-27 04:30, 06:30, 09:00; `wifi-re-report/EVIDENCE-INDEX.md` EV-246/253/261/275/276 | [V*] hardware, 2026-11-27 | deploy the v7/combo module (206,708 B sha fd8e1439... / combo 242,572 B sha 44c6cc12...) + the hostapd package set on the ABI image and follow `HOSTAPD-BRINGUP.md` sec 12; no on-air beacon/association proof without an external client |
| One-command full bring-up matrix: boot/C2/D1/D2/E1 **ALL PASS** (C2 54/54, D1 21/21, D2 real scan results, E1 37/37) | `build/wifi-bench-phase-matrix/20261124T094716Z/{PHASE-MATRIX.md,phase-matrix.json}` (local); `opensource/docs/incoming/wifi-bench-tooling/PHASES.md`; report EV-264 | [V*] hardware, 2026-11-26 | `cd opensource/docs/incoming/wifi-bench-tooling && python3 tools/phase_matrix.py --phase all --yes-really --attended --reboot-between` |
| Firmware scan returns real results decoded into cfg80211 BSS (6 channel + 6 beacon frames first; 12 BSS entries in the first full decode; 13/13 coverage is a long-window item) | ledger 2026-11-26 14:00/16:30; `opensource/docs/incoming/{wifi-vap-start,wifi-scan-d2h,wifi-scan-complete}/`; report EV-247/257/258 | [V*] hardware, 2026-11-26 | fire the D2 ladder (or bench-session phase S with `scan_timeout_ms=60000`) and read `iw dev wlan0 scan` + the `scan DONE (results=.. bss=..)` line |
| First MAC data-plane frame published to the MAC TX ring - POST-ONLY on both the dp-air module and the combined AP+DP module; the MAC consumer does not advance rptr (TX-engine config/start remains [U]) | `opensource/docs/incoming/wifi-dp-air/LADDER-F-AIR.md`; ledger 2026-11-27 02:30 and 10:30; report EV-263/278 | [V*] hardware, 2026-11-27 | `bringup-f-air-txrx.json` through `bench_session.py`; expect POST-ONLY / NO-RX verdicts |
| Bench image reproduced byte-for-byte (container df6a78c3..., kernel slice 13e81214, slot 1794a531, rootfs 62344e80) with the ABI-correct rootfs (kernel pin 6f995278) and the attended flash that closed the kmod-ABI gap (AP then re-run on the flashed image) | `opensource/docs/incoming/wifi-abi-image/{REPORT.md,FLASH-RUNBOOK.md}`; ledger 2026-11-27 06:30; report EV-250/262/272 | [V]/[V*] 2026-11-27 | `cd opensource/docs/incoming/wifi-abi-image && ./tools/build-abi-image.sh` then `./tools/verify-abi-image.sh`; flash per FLASH-RUNBOOK.md (attended) |
| Self-contained final bench image built (combined dp-air+AP-start v7 kmod + hostapd + ucode/wifi-scripts, kernel slice unchanged), awaiting the attended flash; a fresh flash would need no /tmp deployment | `opensource/docs/incoming/wifi-image-integration/` (commit 78e30e8) + `wifi-image-deploy/DEPLOY.md`; ledger 2026-11-27 12:30; report EV-279 | [V] host-only, 2026-11-27 | inspect the image manifest/verifier in `wifi-image-integration/build-evidence/`; flash attended per DEPLOY.md (rootfs-only decision) |
| Hostapd integration is a verified bench package and the AP-start ladder runs on the image (4 apk, no `kernel=` dep, T0 zero-flash, AP-ENABLED on the flashed image) | `opensource/docs/incoming/wifi-hostapd-deploy/` (README, DEPLOY.md, build-evidence/); report EV-254/259/261 | [V] + [V*] AP fire | `./deploy-hostapd.sh --check --evidence /tmp/hostapd-check.txt`; attended `--apply` needs bench-link PASS + recovery READY |
| Publication/upstream state: v1 review form stale, v2 (30+DTS-0001) specified but not generated, Wi-Fi documented as research-only, GPL unanswered since 2026-10-01; publish surface NOT clean (history purge + 9 fails) | `opensource/docs/incoming/wifi-upstream-final/`; report EV-277; `opensource/docs/incoming/wifi-closeout/PUBLICATION-NOTES.md` | [V] 2026-11-27 | re-run `tools/check-repo.sh`, `opensource/tools/publish-prep-check.sh`; read `PUBLICATION-CHECKS.md` |
| Host-only lane-verifier suite green apart from three explained rows | `opensource/docs/incoming/wifi-closeout/TEST-MATRIX.md` + `build-evidence/host-suite/`; report EV-281 | [V] host-only, 2026-11-27 | `cd opensource/docs/incoming/wifi-closeout && ./tools/run-host-suite.sh` |

## 3. Corrections / supersessions to carry forward

| earlier statement | final-state correction | evidence |
| --- | --- | --- |
| v3: "scan returns no results; D2 BLOCKED-DRIVER is the honest pending outcome" (EV-008/118/181/238/240) | already corrected by report v4 (EV-247/257/258/264): firmware scan results are real; D2 is a TRUE PASS | report v4 |
| v3: "the per-radio VAP start family is still [U] - the single remaining blocker" | already corrected by v4 (EV-253/256/260): mode-1 VAP base implemented and consumed; the VAP object exists | report v4 |
| v4: "the air-TX completion (POST-ONLY) needs a module combining AP-start and dp-txrx" | the combined module **exists and was fired** (2026-11-27 10:30, 242,572 B sha 44c6cc12, commit bacd3c1): AP+DP coexist, TX still **POST-ONLY**; the missing piece is the MAC TX-engine consumer/start config, not the module combination **[U]** | ledger 10:30; EV-278 |
| v4: image state = the flashed ABI image df6a78c3 (EV-250/272) | still true for the **flashed** image, but superseded as the bench target by the **self-contained final image** (container ae5e28bf, rootfs ff9f3e1c, combined driver packaged; built 12:30, **not flashed yet**) | ledger 12:30; EV-279 |
| publication lane: "flash runbook ready, nothing executed" | superseded: the flash was executed 2026-11-27 06:30 (kmod ABI closed) and the AP ran on the flashed image; the upstream-final lane recorded the update block | ledger 06:30 |

## 4. Re-verification commands (host-only)

```sh
# EV coverage (must stay gap-free) and collision check against the suggested block
grep -o 'EV-[0-9]\{3\}' opensource/docs/incoming/wifi-re-report/EVIDENCE-INDEX.md | sort -u | wc -l
cd opensource/docs/incoming/wifi-closeout && ./tools/verify-closeout.py

# AP milestone evidence is quoted in the committed lane docs:
sed -n '/## 12/,$p' opensource/docs/incoming/wifi-ap-start/REPORT.md   # v7 root cause + frames
grep -n 'AP-ENABLED' opensource/docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md

# matrix truth: local evidence must say all five phases PASS
grep -E '^\| (boot|C2|D1|D2|E1) ' build/wifi-bench-phase-matrix/20261124T094716Z/PHASE-MATRIX.md

# self-contained image truth (host-only): verifier + manifest in the lane
cd opensource/docs/incoming/wifi-image-integration && ./tools/selftest.sh

# lane-verifier sweep (this lane; results in TEST-MATRIX.md)
./tools/run-host-suite.sh
```

**Honesty rules for this refresh:** [V*] rows cite a ledger date (a bench session the project
actually ran); [V] rows cite a committed artifact whose verifier re-runs host-only; no row
cites a /tmp path as evidence; no vendor byte is reproduced - only sizes, hashes, addresses and
already-published frame field values. The bench-session stale pins are recorded as an action
item, not papered over.
