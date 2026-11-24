# LANE INDEX - the complete project lane ledger (final state, 2026-11-27)

This is the close-out index of every research/implementation lane that produced the final
state. One row per lane: what it delivered, the commit that carries it, where the evidence
lives, and how it is verified. It is the map you read before touching anything - the
per-lane README/REPORT files remain the authority for the details.

**Why this file exists.** After wave 6 the project has ~70 lane directories under
`opensource/docs/incoming/` plus the platform lines under `opensource/target-skeleton/` and
`opensource/upstream/`. A reader cannot tell from a directory listing which lanes are
hardware-validated, which are compile-verified only, which are static RE, and which are
superseded intermediate steps. This index answers that, and it is the file the final
`STATUS.md` next-action and the publication track are written against.

**Final state in one paragraph (2026-11-27).** Our clean-room stack boots the Hi5622V100
firmware to the vendor-equivalent golden state through our own PCIe/ETE transport and HCC
duplex, runs the six-message calibration, registers a cfg80211 wiphy + VAP, receives real
scan results from the firmware decoded into cfg80211 BSS entries, and - on the flashed
ABI-correct image with native kmods/iw/hostapd - brings up **the first live Wi-Fi AP on our
clean-room stack** (`wlan0: AP-ENABLED`, SSID `cudy-ap-test`, persistent, with a boot-absent
firmware D2H event family appearing after the AP start). The MAC data plane is set up and the
first frame is published to the TX ring (POST-ONLY; the MAC consumer does not advance rptr -
the TX-engine start/config is open); a combined AP-start + data-plane module proves both host
stacks coexist in one boot. A **self-contained final bench image** (combined driver + hostapd
+ ucode/wifi-scripts, kernel slice unchanged) is built host-only and is the next flash target.
The Wi-Fi path requires the **PD4-patched research firmware** - the vendor blob's mailbox
dispatch cannot be programmed host-side - so the driver is a research artifact, not an
upstreamable driver. The platform line (6.18 kernel with SMP, PCIe, LSW/PIE/thermal drivers,
FIP image/trailer tooling) is the material for the OpenWrt target, and the review-form series
(001-026) is the upstream candidate.

## How to read a row

| column | meaning |
| --- | --- |
| **lane** | directory under `opensource/docs/incoming/` unless the path says otherwise |
| **task** | shared-task id where one exists; `-` for pre-board / platform work |
| **deliverable** | what is committed in the lane (one sentence) |
| **commit** | newest commit touching the lane directory (short sha) |
| **status** | `[V*]` hardware-validated, `[V]` host-verified committed artifact, `[I]` inference, `[U]` open |
| **evidence** | committed evidence path, or `local:` for gitignored bench artifacts (state page: `STATUS.md`, log: `mem-entries.md`) |

Every lane directory also has its own `README.md` (index), `REPORT.md` (analysis), and often
`BUILD-NOTES.md` / `TABLES.md`. A committed artifact is not a fresh-clone receipt when it is
marked `local:`; regenerate it with the lane's tool. The bench evidence convention is in the
parent repo's `EVIDENCE-INDEX.md`; the Wi-Fi claim-by-claim index is
[`../wifi-re-report/EVIDENCE-INDEX.md`](../wifi-re-report/EVIDENCE-INDEX.md) (EV-001..EV-277
at report v4; the residual close-out rows EV-278..EV-281 are in
[EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md)).

---

## A. Wi-Fi program - foundations (host-only static RE; no device contact)

These lanes established what the closed firmware/host stack is. They are read-only research:
their claims are `[V]` static-reversal facts or `[I]` inferences, refreshed by their own tools.

| lane | task | deliverable | commit | status | evidence |
| --- | --- | --- | --- | --- | --- |
| wifi-firmware-re | - | external-interface black-box characterization of the closed Hi5622V100 firmware/ROM | 52101ac | [V] | lane REPORT.md |
| wifi-driver-re | - | host-driver interface contract: cfg80211 ops map, H2D/D2H messages, ABI deltas | 53297c1 | [V] | lane REPORT.md / SUMMARY.md |
| wifi-hcc-re | 63 | HCC post-transport-boot contract (77-handler map, ids/rings) | 07b9201 | [V] | lane REPORT.md |
| wifi-source-hunt | - | source census for the HiSilicon HCC hmac/wal_cfg80211 family (GPL-2.0 hi3881 covers 23.9%/28.5%) | b0b330d | [V] | lane REPORT.md |
| wifi-shim-port | - | binary-port feasibility (vendor 5.10 ko on a 6.18 image) - negative result | c07203a | [V] | lane REPORT.md |
| wifi-framework-port | - | HiSilicon framework skeleton vs the open WLAN framework - scope decision | 04cccc5 | [V] | lane REPORT.md / SUMMARY.md |
| wifi-datapath-slice | - | hdpp/alg clean-room mapping + scaffolding (mapping CSVs, headers) | 9f0bb41 | [V] | lane REPORT.md |
| wifi-algcfg-slice | - | alg_cfg codec + wal config-command port to 6.18 (compile-clean codec) | 3d1ad9b | [V] | codec/, harness/ |
| wifi-wal-glue | - | 6.18 wal config-command front end wiring the alg_cfg codec | 422c8c9 | [V] | glue/, LICENSE-NOTES.md |
| wifi-transport-port | - | clean-room PCIe/ETE transport for 6.18 (`hi5622_pciet.ko`), B1/B2/B3 bench instruments | 656a928 | [V*] W26a boot | lane REPORT.md, BENCH-PLAN |
| wifi-hcc-link | 75 | host-side HCC link/event handshake determination (negative: no vendor H2D link/event frame) + observability ladder | 4d24913 | [V] | lane SPEC/REPORT |
| wifi-hcc-core | 92 | full post-ready HCC protocol state machine (compile-ready, 528-check selftest) | a8a69d4 | [V] | tests/hcc_core_test.c |
| wifi-hcc-service | 87 | host HCC service counterpart on the corrected boot protocol: vendor-order D2H loop, boot-guarded CECE policy, DR fill/ready receive path; compile-ready hi5622_pciet_hcc.ko + 83/83 frame/ring selftest | 03ad691 | [V] | lane REPORT.md/BENCH-PROCEDURE.md |
| wifi-d2h-direct | 89 | direct device group-A ring D2H receive service (`rd_*` attrs) + bench procedure | 7a8e542 | [V] | lane REPORT.md |
| wifi-h2d-catalog | 74 | firmware H2D dispatch tables + id->handler catalog | f20b506 | [V] | lane REPORT.md |
| wifi-h2d-frames | 93 | byte-exact H2D catalogue/builders/validators (233-check selftest) | 98ce465 | [V] | src/h2d_selftest.c |
| wifi-h2d-inject | 73 | validated userspace HCC H2D frame injector (63-case selftest, base+B3 patches) | f64956f | [V] | files/h2d_selftest.c |
| wifi-host-replica | 72 | vendor post-release host behaviour + minimal counterpart spec/skeleton | 8978bd1 | [V] | lane REPORT.md |
| wifi-ete-frames | 79 | device->host ready-path frames g4 id1/id2 decoded (no host H2D in the release window) | e88fcba | [V] | tools/verify-frames.py |
| wifi-ete-init | 81 | vendor ETE init words + B3 patch (intr mask, per-channel base/depth/ctrl) | 7170dea | [V] | lane REPORT.md |
| wifi-ete-engine | 82 | ETE start condition = firmware-sent trigger gated on hardware status; no host-visible wake | 03705ff | [V] | tools/ete_engine.py (150/150) |
| wifi-ete-wake | 106 | ETE DR A->B copy trigger chain + host-enabler delta + R0/E1/E2 ladder | e562366 | [V] | tools/ete_wake.py (81/81) |
| wifi-event-router | 78 | device event-controller map and the delivery gap (per-core controllers, timer, ETE) | f30d653 | [V] | lane REPORT.md |
| wifi-boot-gate | 77 | boot-task post-send chain + the exact 0x48820 bind gate (no host action) | 72b9385 | [V] | lane REPORT.md |
| wifi-fw-link-event | 76 | boot-region 0x474B8 is entry A's literal pool; event pump 0x46848; ROM IRQ dispatch chain | 0780671 | [V] | lane REPORT.md |
| wifi-rom-flow | 71 | image-only boot flow + ROM/dispatcher contract reconstruction | 26aa57a | [V] | lane REPORT.md |
| wifi-rom-dump | 86 | chip mask ROM reversed (S0/S1, the +8=2 writer, the 0xCEED gate, host action) | 734696f | [V] | tools/romdis.py (86/86) |
| wifi-rom-s01 | 83 | ROM S0/S1 + CA 0x1BF000 handshake page + ETE engine-global gap | f601889 | [V] | tools/rom_s01.py (70/70) |
| wifi-s1-cpu1 | 85 | who writes handshake +8=2; CPU0->CPU1 TCM handoff gate | 6f82a88 | [V] | tools/s1_cpu1.py (46/46) |
| wifi-chip-reset | 84 | vendor power/reset = hi_pcie.ko PERST/power GPIO pulse; the one host-controllable difference | 3bf67ab | [V] | tools/chip_power.py (13/13) |
| wifi-fw-postboot | 88 | firmware post-handshake continuation; the scheduler "fingerprint" probe is an instrument artifact | 9de74e8 | [V] | lane REPORT.md |
| wifi-fw-ready-gate | 67 | what gates the g4 ready send (HCC group-4 id1/id2): cold-boot hypothesis disproven, kick is firmware-internal | 195aff3 | [V] | lane REPORT.md sec 14 |
| wifi-boot-kick | 69 | vendor post-release boot-kick RE: PHY table fully applied; abort is firmware-internal | 195aff3 | [V] | lane REPORT.md |
| wifi-abort-rca | 70 | firmware DATA ABORT in the descriptor-fill chain: frame decode, object layout | 864d1bb | [V] | lane REPORT.md |
| wifi-irq-unmask | 90, 91 | firmware IRQ-unmask transition + H2D mailbox delivery gap; PD/PD2/PD3/PD3B patch candidates (PD3 bench-validated; PD4 is the working research patch, audited by wifi-fw-audit) | e9ff025 | [V*] PD3/PD4 | lane REPORT-PD2/PD3.md, recipes |
| wifi-fw-audit | 115 | PD4 minimally/safety audit + H1 (consume-only stub, empty live handler table) + vendor-clean alternatives | b094cf9 | [V*] H1 | tools/verify_pd4.py (14/14+14/14) |
| wifi-vendor-userspace | 96 | vendor userspace post-DEVICE-READY interface scope: the ~39-op cfg80211 surface a clean driver must expose | 861eb72 | [V] | lane REPORT.md |
| wifi-thermal-re | - | Wi-Fi/thermal feasibility + W23 thermal driver plan (thermal closed later: 066/067 validated) | abd257c | [V] | lane REPORT.md |

**Milestone these lanes produced:** the boot protocol is known and reproducible (boot ROM S1
handshake -> image boot task -> golden page), and the only way to service the firmware's H2D
mailbox on our host is the PD4 research patch (H1: the stub substitutes an empty handler
table, the duplex runs through the non-mailbox ETE/polled path). That is the honest reason the
Wi-Fi driver cannot be upstreamed.

---

## B. Wi-Fi program - bring-up implementation (compile-verified, then hardware-validated)

| lane | task | deliverable | commit | status | evidence |
| --- | --- | --- | --- | --- | --- |
| wifi-driver-clean | 94 | self-contained clean-room transport+HCC driver tree with the corrected protocol baked in | 1f2be1e | [V] | lane REPORT.md, BUILD-NOTES.md |
| wifi-driver-integration | 64 | host-driver integration skeleton: transport<->hdpp interface + hdpp/alg core (clean-room) | ba237e7 | [V] | lane REPORT.md |
| wifi-openwrt-pkg | 95 | OpenWrt kmod package for the clean-room driver, build-verified in the lab tree | e187ca0 | [V] | build-summary.txt |
| wifi-h2d-send | 98 | first real post-ready H2D message path (g0 id1 TXQ config sync) + validated senders; 72-byte live-node variant | 4218d28 | [V*] | lane REPORT.md sec 11-12 |
| wifi-ctx-create | 101 | the g0 id1 / key 0x64 context-create message (synthesized from the firmware parser) | ad43d81 | [V*] | lane REPORT.md |
| wifi-proto-seq | 103 | decode of the new g1 id3/id0x12 D2H answers + calibration alg set_* messages | 42fd884 | [V*] | lane REPORT.md |
| wifi-cfg80211 | 104 | cfg80211 full-MAC skeleton M0-M2: wiphy + VAP lifecycle over the working HCC protocol | 5fff5c6 | [V*] D1 | lane BUILD-NOTES |
| wifi-datapath | 105 | data-plane RE: hdpp/alg/wal/scan/AP/STA message map, MAC frame-ring formats, host skb<->frame architecture | 7f64314 | [V] | lane REPORT.md |
| wifi-datapath-bringup | 111 | MAC data-plane host side: TXQ-table address sync (g0 id1 key 0x0320) + TX/RX rings + one-shot dp_setup/probe | 7bf6e2a | [V*] E1 37/37 | lane BUILD-NOTES, benches |
| wifi-vap-scan | 110 | HCC user/VAP-add (g0 id7/id8) + scan request (g0 id0xC) byte-exact builders + bench ladder | f1c636b | [V] 137/137 | src/vapscan_selftest.c |
| wifi-vap-start | 117 | VAP-start family: WAL g2 cmds 0x64/0x65, g0 id1 keys 0x64/0x65, 0xF/0x10; provider patch | 231a39d | [V*] | lane BUILD-NOTES |
| wifi-scan-ap | 113 | cfg80211 scan/AP/VAP-message wiring + provider contract + fail-closed post gate | edc80ba | [V*] | lane REPORT.md |
| wifi-scan-complete | 123 | all-13-channel scan window + coverage completion + beacon IE decode into full BSS entries | ac84688 | [V] | lane BUILD-NOTES, TEST-LADDER.md |
| wifi-scan-d2h | 118 | scan result D2H RE + tolerant cfg80211 BSS decoder (g1 15/16/17, g2 2) | 469b193 | [V] | tools/selftest.sh |
| wifi-dp-txrx | 122 | first MAC data-plane TX/RX objects: 12-byte MSDU table, TX publish + completion read, RX descriptor parser (254/254) | 7f63b27 | [V*] POST-ONLY | lane REPORT.md |
| wifi-dp-air | 126 | merged provider+data-plane module (886,864 B sha ca32aec4) + first air-TX/RX ladder (30 steps/83 checks) | a615a8c, 7bf6e2a | [V*] POST-ONLY fire | lane LADDER-F-AIR.md |
| wifi-ap-start | 121 | AP start RE + builders: WAL AP-config cmd 0x0127 (156-byte body), 4-message AP sequence, mode-1 VAP base; v7 provider (sha fd8e1439) reached **hostapd AP-ENABLED**; the follow-up combined AP+DP module (242,572 B sha 44c6cc12) fired AP+data-plane in one boot | 9b180f3, bacd3c1 | [V*] **hostapd AP-ENABLED** | lane REPORT.md sec 11-12, HOSTAPD-BRINGUP.md |
| wifi-bench-tooling | 97, 108, 114, 124 | one-command bench sessions/phase matrix + patched-firmware builder/verifier + fail-closed gates (563/563 selftests) | 7bf6e2a | [V*] matrix | tools/selftest.py, benches |
| wifi-bench-session | 127 | attended command package: 13-channel scan + AP start + first TX/RX (dry-run verified; Lead fires). **Known pre-fire action:** its committed `artifacts.env` pins the pre-revision merged module (ec74fccd) and the pre-v7 AP module (11511980); the committed artifacts are ca32aec4 / fd8e1439 / the combined 44c6cc12 - re-pin before firing (its own selftest currently FAILs on this, by design) | 5c528de | [V] package; pins stale | lane SESSION-PLAN, GATES; TEST-MATRIX.md |
| wifi-hostapd-deploy | 125 | hostapd-mini v2.12 package set + T0 zero-flash deploy + AP-start ladder (deploy --check 88/88) | a32f028 | [V*] AP fire | build-evidence/ |
| wifi-image-integration | 112 (+ follow-up) | bench image with native iw 6.17 / libnl-tiny / iwinfo / wireless-regdb / clean-room driver packages; follow-up delivered the **self-contained final image** (container 6,922,559 B sha ae5e28bf, rootfs UBI ff9f3e1c, manifest 83 packages, combined 44c6cc12 driver + hostapd/ucode/wifi-scripts, kernel slice unchanged; host-only, next flash target) | 99c87cb, 78e30e8 | [V*] first image flashed; final image [V] built | lane README/BUILD-NOTES, build-evidence/ |
| wifi-image-deploy | 119 | T0 zero-flash apk deploy (userspace-only mode verified on hardware; kmod ABI path closed by abi-image) | 87083f7 | [V*] userspace deploy | deploy-t0.sh |
| wifi-abi-image | 128 | byte-for-byte image rebuild + ABI receipt + attended flash runbook; the runbook was executed by the Lead 2026-11-27 06:30 (kmod ABI closed) | 224be72 | [V*] flash executed | FLASH-RUNBOOK.md, build/abi-image-artifacts/ (local) |
| wifi-upstream-target | 107 | OpenWrt target + kernel-series readiness review (what is / is not submission-grade) | 9740946 | [V] | lane REPORT.md |

**Hardware-validated milestones (newest wins; ledger dates):**
1. 2026-11-24: transport B1/B2/B3 - firmware booted by our host, ETE rings, H2D smoke.
2. 2026-11-24: full-duplex HCC, golden page + ready pair, first real H2D message.
3. 2026-11-25: C2 calibration (54/54) and D1 cfg80211 VAP lifecycle (21/21) in the automation.
4. 2026-11-26: real VAP/user_add + notify + first scan request consumed; first real scan
   results (6 channels + 6 beacon frames, g1 id15/id17); D2 TRUE PASS; one-command matrix all
   five phases PASS (local evidence `build/wifi-bench-phase-matrix/20261124T094716Z/`).
5. 2026-11-27: first AP on the clean-room stack (`hostapd: wlan0: AP-ENABLED`, SSID
   `cudy-ap-test`, four AP-start frames consumed, VAP pointer nonzero); first data-plane TX
   frame published to the MAC TX ring (POST-ONLY); ABI-correct image flashed and the AP
   re-run on the flashed image with native kmods (`cfg80211.ko`/`hi5622_wifi.ko` from
   `/lib/modules/6.18.60`), `apk query kernel` = the kmod pin `6f995278`.
6. 2026-11-27 (later): AP persistence verified (hostapd alive for minutes; boot-absent
   `g1 id0x23` 24 B D2H family appears only after the AP start - likely AP/beacon-period
   activity, [I], not an on-air proof); the **combined AP+DP module fired** (AP-ENABLED +
   POST-ONLY TX in one boot; the MAC consumer/start remains [U]); the **self-contained final
   image** was built host-only (combined driver + hostapd packaged, kernel unchanged; not yet
   flashed).

---

## C. Audit / report / close-out lanes

| lane | task | deliverable | commit | status | evidence |
| --- | --- | --- | --- | --- | --- |
| wifi-re-report | 109, 116, 120, 129 | publication-grade umbrella RE report: v4 final at **2421 lines** + TABLES T1-T31 + **EV-001..EV-277** + verifier 172/172, with the AP milestone, scan/BSS decode, all-PASS matrix, air-TX POST-ONLY, flash/AP-on-image, persistence + honest limits | f59d6de, e96a207, 0df7de0 | [V] | tools/verify_report.py |
| wifi-upstream-final | 130 | upstream submission finalization: v1 review form stale -> v2 (30+DTS-0001) specified but not generated, DTS truth, honest AP/GPL/PD4 pages, PUBLICATION-CHECKS, NEXT-STEPS/EVIDENCE; dated 2026-11-27 blocks in ROADMAP/PUBLISH-READINESS/SUBMISSION-STATUS | 75c8ed4 | [V] | lane PUBLICATION-CHECKS.md |
| wifi-bench-session | 127 | (see B) package for the next attended sitting | 5c528de | [V] | lane SESSION-PLAN.md |
| wifi-closeout | 131 | this index + evidence refresh + final overview + host verifier suite | (this commit) | [V] | EVIDENCE-REFRESH.md, TEST-MATRIX.md, OVERVIEW-README.md |

---

## D. Platform lines (non-Wi-Fi)

### D1. Boot / loader / SoC reversal and early bring-up

| lane / artifact | task | deliverable | commit | status | evidence |
| --- | --- | --- | --- | --- | --- |
| loader-re | - | loader, recovery and upgrade formats (hboot1tag, hi_upgrade + RSA recovery gate, FIP hybrid) | 628c4b1 | [V] | lane REPORT.md |
| cudy-backup-root | 68 | stock backup crypt reversed (AES-128-CBC, key=MD5(hostname+"@"+bdinfo secret)) -> root file write PoC + hardware-validated restore | 827a99b | [V*] | lane REPORT.md |
| pcie-seq | - | vendor PCIe RC bring-up register sequence + 013 fix-list/addenda (PHY verdict, ACP polarity) | b2871ce | [V] | lane README.md |
| multi-port-probe | - | multi-port source-port probe patch + prepared experiment image (benched, not conclusive) | 4640631 | [V*] | lane files |
| opaque-binaries | - | hi_ipc ABI, HSLINK/HyFi, crypt KDF, bdinfo, TR-069 constant decoding | b488630 | [V] | lane REPORT.md |
| vendor-userspace | - | LuCI/cmagent/cmsd/bdinfo/hdb/upgrade static RE (stock 2.4.15/2.5.24) | 21cd936 | [V] | lane REPORT.md |
| upstream-blockers | - | upstream blocker survey; flashinfo compatible-first binding resolved, final "ZERO upstream blockers" for the platform line | 123ca0a | [V] | lane 00-INDEX.md |
| lsw-forwarding | - | LSW native-forwarding plan + artifacts (NAPT init, W22 hardware validation, M2 offload CLOSED - flow cache never populated) | a0aa6b1 | [V*] W22 | lane README, w22/ |

### D2. Target skeleton, drivers, image recipe (the OpenWrt track)

The platform driver work is committed as one review-form patch series plus the target
skeleton. It is **not** the Wi-Fi driver: it is the SoC/platform support (clocks, pinctrl,
watchdog, NAND, MDIO/PHY/GMAC, PCIe, PIE, LSW, SMP, selector, MAC nvmem, thermal).

| artifact | deliverable | count / validation | evidence |
| --- | --- | --- | --- |
| `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/` | platform driver patch series (compiles against linux-6.18.60 + OpenWrt GCC 14.4.0) | 67 patch files on disk (review form 001-026 + bench/experimental extensions; release line manifest 72 entries) | `opensource/docs/SKELETON-SMOKE.md` |
| `opensource/target-skeleton/` | OpenWrt target skeleton: DTS, target config, image recipe (`make world` RC=0 host-only), A/B install path, boot-pair recorder, bdinfo MAC provisioning | target layout accepted by `make defconfig`/`make target/linux/prepare`; image build byte-reproducible | port notes in `target-skeleton/{docs,tools}`, `opensource/docs/TARGET-SKELETON.md` |
| `opensource/upstream/series-6.18/` | review-form submission series (26 patches 001-026) + release line (51 patches) + DTS series + validation | `patch(1)` 26/26, `git am` 26/26 identical trees; check-release-line validator; cover/mapping/rationale | `04-validation.md`, `validation/release-line/` |

Hardware-validated platform milestones (evidence in the parent repo's `EVIDENCE-INDEX.md` and
`mem-entries.md`): first own-kernel boot on the board, persistent UBIFS overlay, SMP 2 CPUs
with the DTS loader hand-off fix, both PCIe RCs train, LSW/PIE write gates accepted,
first real Ethernet ARP/ICMP traffic from the PIE path, thermal sensor + cooling validated
(patches 066/067), bdinfo MAC provisioning.

### D3. Bench / publication tooling

| artifact | deliverable | status |
| --- | --- | --- |
| parent `tools/` (root repo) | bench session, UART read-only capture, fire preflight/device, recovery card, inventory, ledger generator | [V] tool tests in `tools/tests/` |
| parent `EVIDENCE-INDEX.md` | claim -> artifact -> regeneration index for the platform/bench line | [V] (Lead scope; refresh rows proposed in [EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md)) |
| `opensource/tools/publish-prep-check.sh` | publication checks; current status is recorded by the submission-finalization lane | see `../wifi-upstream-final/PUBLICATION-CHECKS.md` |
| this lane's `tools/run-host-suite.sh` | one command re-runs every host-only lane verifier (see [TEST-MATRIX.md](TEST-MATRIX.md)) | [V] |

---

## E. Evidence roots

- Committed lane evidence: `opensource/docs/incoming/<lane>/` (REPORT/README/TABLES/BUILD-NOTES,
  selftest sources, `build-evidence/` where recorded).
- Local (gitignored, never published): `build/` - bench logs, phase-matrix evidence
  (`build/wifi-bench-phase-matrix/`, `build/evidence-bench-gates/`), image artifacts
  (`build/abi-image-artifacts/`), kernel trees, UART logs.
- Ledger: parent `mem-entries.md` (append-only, newest wins); index `mem-entries-INDEX.md`.
- State page: parent `STATUS.md`; runner brief `tools/session-brief.sh`.
- Claim-by-claim Wi-Fi index: [`../wifi-re-report/EVIDENCE-INDEX.md`](../wifi-re-report/EVIDENCE-INDEX.md).

## F. Cross-cutting honest limits (repeat them, they matter)

1. **The Wi-Fi path requires the PD4-patched research firmware.** The vendor blob's mailbox
   dispatch is not programmable host-side; the patched stub is consume-only. It is a research
   instrument and must never ship.
2. **No upstreamable Wi-Fi driver exists yet.** The clean-room driver is a research artifact;
   the GPL source request to the vendor is unanswered. Upstream target work is platform-only.
3. **The AP milestone is a host-side AP-ENABLED.** Beacon TX/association needs an external
   observer and has not been verified (the post-AP firmware event family is indirect evidence
   only); data-plane air-TX is POST-ONLY on both the dp-air and the combined module - the host
   publishes to the ring but the MAC consumer does not advance rptr, and the vendor TX-engine
   start/config is still [U].
4. **Publication policy is binding:** no vendor binaries/blobs/firmware/ROM/calibration bytes in
   tracked text, curated repos, append-only ledger. The `opensource/` half is curated; the
   root notebook is public research material. See `PUBLISH.md` and `NOTICE.md`.
5. **Nothing here is a release.** Read `STATUS.md` for the live state and the real next action
   before acting; this index is a map, not a status page.
