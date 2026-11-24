# EVIDENCE INDEX - claim by claim

Companion to [REPORT.md](REPORT.md): every material claim in the report, with its evidence marker,
the artifact that establishes it and the bench/ledger record behind it. "Re-verified here" records
what the consolidation pass actually re-ran (device-free only); "-" means the claim is quoted from
its owning lane's record and was not re-executed in this pass.

Marker legend: `[V]` verified from a named local artifact by the owning lane's committed tool;
`[V*]` bench-measured in an attended session (ledger date); `[I]` inference; `[U]` unknown.

Artifact paths are relative to `opensource/docs/incoming/` unless they start with `../` or are
quoted as a superproject file (`mem-entries.md`, `STATUS.md`). "Section" refers to the numbered
section of [REPORT.md](REPORT.md).

---

## Section 1 - executive summary

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-001 | 1.1 | the host can boot the endpoint firmware to the vendor-equivalent golden state | [V*] | `wifi-driver-clean/REPORT.md`, `wifi-rom-dump/REPORT.md` | ledger 2026-11-24 10:40 (GP8) | - |
| EV-002 | 1.1 | the ready handshake is received and decoded by a clean-room host | [V*] | `wifi-d2h-direct/REPORT.md` | ledger 2026-11-24 14:00 | selftest 92/92 |
| EV-003 | 1.1 | two ordered real H2D messages are consumed with zero exceptions and answered | [V*] | `wifi-h2d-send/REPORT.md` sec 11-12 | ledger 2026-11-25 05:10 | - |
| EV-004 | 1.1 | the firmware responses are g1 id3 (36 B) and g1 id0x12 (40 B) | [V*] | `wifi-h2d-send/REPORT.md`, this report TABLES T13 | ledger 2026-11-25 06:00 | frame bytes cross-checked |
| EV-005 | 1.2 | the route to that state requires the release-only boot protocol | [V]/[V*] | `wifi-rom-dump/REPORT.md` sec 6-7 | ledger 2026-11-24 10:00/10:40 | - |
| EV-006 | 1.2 | the firmware IRQ/thread layer is not proven alive; the mailbox is serviced through the PD3/PD4 research patch | [V*] | `wifi-irq-unmask/REPORT-PD3.md` | ledger 2026-11-24 22:10 | - |
| EV-007 | 1.2 | the ETE A->B copy is idle; the D2H path is the direct device-ring reader | [V*] | `wifi-d2h-direct/REPORT.md`, `wifi-ete-engine/REPORT.md` | ledger 2026-11-24 13:10 | - |
| EV-008 | 1.2 | the Wi-Fi *operational* interface is partially started: a cfg80211 management interface (wiphy + VAP netdev, M0/M1) and the mailbox-layer firmware VAP (g0 id7/id8) exist and the scan request (g0 id0xC) is consumed; scan **results**, association and any frame TX/RX are not started; upstream remains blocked by the closed firmware | [I]/[V*] | `wifi-vendor-userspace/REPORT.md`, `wifi-cfg80211/REPORT.md`, `wifi-scan-ap/REPORT.md` | ledger 2026-11-25 15:00 - 2026-11-26 06:30 | - |
| EV-009 | 1.3 | the eight-step breakthrough ladder and its dates | [V*] | this report sec 1.3; lanes named per row | ledger 2026-11-22..11-25 | - |
| EV-010 | 1.4 | firmware 928,920 B sha256 7fc87e20... ; ROM content 2,073 B sha256 fcfd464f... | [V] | `wifi-firmware-re/REPORT.md`, `wifi-rom-dump/REPORT.md` sec 1 | - | - |
| EV-011 | 1.4 | 60 H2D receive slots (41 group 0 incl. early 0x2F, 19 group 2) | [V] | `wifi-h2d-catalog/REPORT.md`, `wifi-h2d-frames/catalogue.md` | - | catalogue counts checked |

| EV-126 | 1.1 | the v2 chain extends to the calibration answer and the wiphy/VAP interface | [V*] | `wifi-proto-seq/REPORT.md`, `wifi-cfg80211/REPORT.md` | ledger 2026-11-25 12:30/15:00 | - |
| EV-127 | 1.2 | cfg80211 wiphy + VAP netdev lifecycle hardware-validated on our own image (M0/M1) | [V*] | `wifi-cfg80211/REPORT.md`, `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` | ledger 2026-11-25 15:00 | - |
| EV-128 | 1.2 | ladder C1: 298 B calibration request consumed and a 298 B g1 id3 answer counted; answer body not captured | [V*] | `wifi-proto-seq/REPORT.md` sec 4-7, `wifi-h2d-send/REPORT.md` sec 13 | ledger 2026-11-25 12:30 | - |
| EV-129 | 1.2 | the ETE DR host lever measured negative; the data plane is mapped **and host-implemented with its TXQ address sync bench-accepted (E1)**, but no frame has moved; upstream still blocked | [V*]/[I] | `wifi-ete-wake/REPORT.md`, `wifi-datapath/REPORT.md`, `wifi-datapath-bringup/REPORT.md` | ledger 2026-11-25 10:10, 2026-11-26 03:00 | - |
| EV-130 | 1.3 | the four additional 2026-11-25 breakthroughs of the v2 ladder (PD4 cave, ETE verdict, C1, wiphy/VAP) | [V*] | REPORT sec 1.3; lanes named per row | ledger 2026-11-25 02:10-15:00 | - |
| EV-131 | 1.4 | v2 key numbers: image/module hashes, channel plan, C1 lengths, MAC ring geometry | [V]/[V*] | TABLES T21-T24 and the lanes they cite | ledger 2026-11-25 | constants cross-checked |
| EV-196 | 1.1 | the v3 chain extends to the complete calibration boot, the real VAP/scan management frames, the data-plane E1 setup and the one-command matrix | [V*] | `wifi-proto-seq/REPORT.md`, `wifi-scan-ap/REPORT.md`, `wifi-datapath-bringup/REPORT.md`, TABLES T26-T28 | ledger 2026-11-25 16:30 - 2026-11-26 06:30 | - |
| EV-197 | 1.1 | the firmware receives and consumes our hand-built VAP user_add/user_add_notify and the first scan request, but returns no scan results | [V*] | `wifi-vap-scan/REPORT.md`, `wifi-scan-ap/REPORT.md`, TABLES T27 | ledger 2026-11-26 04:30/06:30 | - |
| EV-198 | 1.2 | firmware VAP/scan/AP control messages are partially proven: VAP add + notify + scan request delivered; per-radio VAP start, scan results and AP start remain open | [V*]/[U] | TABLES T27, `wifi-vap-scan/REPORT.md` sec 8 | ledger 2026-11-26 06:30 | - |
| EV-199 | 1.2 | the MAC data path is host-set-up and TXQ-table-synced (E1 PASS), but no frame has traversed the rings | [V]/[V*] | `wifi-datapath-bringup/REPORT.md`, TABLES T26 | ledger 2026-11-26 03:00 | v3 build 69/69 + 308/308 + 34/34 |
| EV-200 | 1.2 | one command runs the full matrix: boot/C2/D1/D2/E1 = PASS/PASS/PASS/BLOCKED-DRIVER/PASS | [V*] | `wifi-bench-tooling/PHASES.md`, TABLES T28 | ledger 2026-11-26 05:30 | - |
| EV-201 | 1.3 | the four new breakthrough-ladder rungs (C2, H1, one-command matrix, real frames) and their key numbers | [V*]/[V] | REPORT sec 1.3/1.4; TABLES T23/T25/T26/T27/T28 | ledger 2026-11-25/26 | constants cross-checked |

## Section 2 - system map

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-012 | 2.1 | endpoint enumerates as 59e7:0005 class 0x028000 behind each RC | [V*] | `wifi-transport-port/REPORT.md` | ledger 2026-11-22 (A run) | - |
| EV-013 | 2.1 | the transport and driver are original host code; no vendor host module is loaded in our stack | [V] | `wifi-driver-clean/REPORT.md` | - | - |
| EV-014 | 2.2 | IO-CA translation 0x3B8000+(CA-0x40000000) and CPU translation 0x6B8000+CPU | [V] | `wifi-transport-port/REPORT.md` sec 1, `wifi-hcc-core/src/hcc_core.h` | bench read-backs 2026-11-22 | constants cross-checked |
| EV-015 | 2.2 | host phys = 0x40000000 + BAR0 for endpoint 0 on the bench | [V*] | `wifi-rom-dump/REPORT.md` sec 1 | stock golden-read sessions | - |
| EV-016 | 2.2 | the 20-bit region-3 mask is wrong for CAs >= 0x40100000 (F4 bug) | [V] | `wifi-hcc-core/src/hcc_core.h` header note, `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 17:20/20:00 | - |
| EV-017 | 2.2 | CPU addresses 0x40008/0x4000C/0x1BF000 are firmware CPU, not device CAs | [V] | `wifi-rom-dump/REPORT.md` sec 1, `wifi-hcc-core/src/hcc_core.h` | - | conventions checked in report |
| EV-018 | 2.3 | the layer stack (userspace -> cfg80211 -> HCC -> ETE -> PCIe -> chip) | [I] | `wifi-vendor-userspace/REPORT.md`, `wifi-host-replica/REPORT.md` | - | - |
| EV-019 | 2.5 | the device-side address map rows (ROM, image, page, mailbox, ETE, rings, blocks) | [V]/[V*] | TABLES T1/T4/T14 and the lanes they cite | bench reads | constants cross-checked |
| EV-020 | 2.5 | three distinct firmware objects 0x10C0F4 / 0x10C190 / 0x10F1F0 | [V]/[U] | `wifi-h2d-catalog/REPORT.md`, `wifi-irq-unmask/REPORT-PD2.md`, `wifi-ete-engine/REPORT.md` | - | - |

| EV-132 | 2.2 | the MAC host-interface block CA 0x40040000/0x40060000 lies inside region 3 (BAR0 0x418000/0x438000); a MAC-window read once bus-errored, so ladder B2 reads before writing | [V]/[V*] | `wifi-datapath/REPORT.md` sec 2.1/9.4 | - | - |
| EV-133 | 2.5 | device map rows for the MAC slot-5 ring block and the firmware key tables A/B | [V] | `wifi-datapath/REPORT.md` sec 2/4 | - | - |

## Section 3 - boot chain

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-021 | 3.1 | the readable 16 KiB ROM window is the whole relevant mask ROM (2,073 B; rest RAZ) | [V] | `wifi-rom-dump/REPORT.md` sec 2, `wifi-rom-dump/tools/romdis.py` | ROM dump 2026-11-24 09:20 | lane tool quoted (86 checks) |
| EV-022 | 3.2 | S0 sets up cache/MPU/stacks and splits CPU0/CPU1 on MPIDR&3 | [V] | `wifi-rom-dump/REPORT.md` sec 3 | - | - |
| EV-023 | 3.2 | CPU0 cold fallback: 0xBAAD -> analog -> poll 0x5A5A -> write 0xCEED -> blx entry A 0x46970 | [V] | `wifi-rom-dump/REPORT.md` sec 4 | - | - |
| EV-024 | 3.2 | the firmware must be uploaded before the release (entry pointer re-read) | [V] | `wifi-rom-dump/REPORT.md` sec 4 | - | - |
| EV-025 | 3.3 | CPU1 S1 waits for page +C == 0xABCD and gate == 0xCEED, then writes page +8 = 1<<core = 2 and blx entry B 0xC742C | [V] | `wifi-rom-dump/REPORT.md` sec 5 | stock/GP8 golden reads | - |
| EV-026 | 3.3 | the image never writes +8 = 2; it only consumes the ACK | [V] | `wifi-rom-dump/REPORT.md` sec 5, `wifi-rom-s01/REPORT.md` | - | - |
| EV-027 | 3.4 | the handshake page layout and golden words 0x86807FEF/7/2/0xABCD | [V*] | `wifi-rom-s01/REPORT.md`, `wifi-driver-clean/REPORT.md` | stock golden read 2026-11-24 05:00; GP8 10:40 | constants cross-checked |
| EV-028 | 3.4 | state CPU 0x118B60 = 1 and exception mask CPU 0x40008 = 0 on a golden boot | [V*] | `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 21:10 | - |
| EV-029 | 3.5 | CA 0x4000010C is dual-use: ROM 0xCEED vs image 0xCECE | [V] | `wifi-rom-dump/REPORT.md` sec 6-7 | - | - |
| EV-030 | 3.5 | our 0xCECE writes during boot destroy the 0xCEED handshake and stall S1 | [V]/[V*] | `wifi-rom-dump/REPORT.md`, `wifi-s1-cpu1/REPORT.md` | GP2-GP6 vs GP8 differential | - |
| EV-031 | 3.5 | the vendor host never writes 0xCECE (0 hits in 60+ modules) | [V] | `wifi-host-replica/REPORT.md`, `wifi-boot-kick/tools/vendor_const_scan.py` | - | - |
| EV-032 | 3.6 | the vendor pre-release order: firmware chunks -> platform block -> wifi block -> release | [V*] | `wifi-rom-flow`/lane records; kprobe capture | ledger 2026-11-24 07:20 | - |
| EV-033 | 3.6 | the four TCM bounds live at platform-block words 1-4; the full 24-word block is required for byte-identical TCM | [V*] | `wifi-s1-cpu1/REPORT.md`, this report TABLES T6 | GP3/GP4 2026-11-24 | - |
| EV-034 | 3.6 | the ETE rings and D2H service must be armed before release | [V] | `wifi-transport-port/REPORT.md`, `wifi-ete-frames/REPORT.md` sec 2.5 | bench sessions | - |
| EV-035 | 3.7 | the golden state values incl. TCM 0xE59FF018/0x40034000/0x37C8F8D3 and boot slot 0x000C19D1 | [V*] | `wifi-driver-clean/REPORT.md`, `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 21:10, stock 05:00 | constants cross-checked |
| EV-036 | 3.7 | ready pair g4 id1 + g4 id2 with payload 0x01060850 inside the vendor bounds | [V*] | `wifi-d2h-direct/REPORT.md` sec 5, `wifi-ete-frames/REPORT.md` sec 2.3 | ledger 2026-11-24 14:00 | - |
| EV-037 | 3.8 | event 0x4C is never delivered on our boot; no H2D message creates it; vendor sends nothing pre-ready | [V]/[V*] | `wifi-irq-unmask/REPORT.md`, `wifi-fw-link-event/REPORT.md`, `wifi-hcc-link/REPORT.md` | ledger 2026-11-24 12:20/15:10 | - |
| EV-038 | 3.8 | the "scheduler fingerprint absent" reading was an instrument aliasing bug (correction C7) | [V] | `wifi-fw-postboot/REPORT.md` sec 2-3 | - | - |
| EV-039 | 3.8 | the PD3/PD4 patch calls the firmware H2D dispatcher from the boot-task epilogue | [V*] | `wifi-irq-unmask/REPORT-PD3.md`, `REPORT-PD4.md` | ledger 2026-11-24 22:10; 2026-11-25 02:10 | - |
| EV-040 | 3.9 | the boot decision ladder (vendor / pre-fix / post-fix paths) | [V]/[V*] | synthesis of the boot-chain evidence rows above | GP8 | - |

## Section 4 - critical findings

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-041 | 4 F1 | 0xCEED clobbering is the root cause of the S1 stall; the fix is release-only boot | [V]/[V*] | `wifi-rom-dump/REPORT.md` sec 6-7 | GP8 ledger 2026-11-24 10:40 | - |
| EV-042 | 4 F1 | an evidence-driven, bounded CECE service replaces the boot-time write | [V] | `wifi-hcc-core/src/hcc_core.h` CECE policy, `wifi-driver-clean/REPORT.md` | - | hcc_core selftest 528 |
| EV-043 | 4 F2 | the exception-staging routine writes its 17-word frame over CPU 0x4000C..0x4004B | [V*] | `wifi-irq-unmask/REPORT-PD4.md` sec 1 | ledger 2026-11-25 00:20 | - |
| EV-044 | 4 F2 | PD4 relocates the stub to CPU 0x108240 in a 637-byte gap with zero references | [V] | `wifi-irq-unmask/REPORT-PD4.md` sec 2 | cave read-back 2026-11-25 02:10 | hash cross-checked |
| EV-045 | 4 F3 | the dispatcher wrapper is 0x40294 -> dispatcher 0xC18AC, contract r0 = hcc instance 0x10C190 | [V] | `wifi-irq-unmask/REPORT-PD2.md` | - | - |
| EV-046 | 4 F3 | PD3/PD4 consumption: announce consumed in 0 polls, h2d_complete done=1, page intact | [V*] | `wifi-irq-unmask/REPORT-PD3.md` sec 5.4 | ledger 2026-11-24 22:10/22:45 | - |
| EV-047 | 4 F4 | device D2H nodes sit in PKTRAM CA 0x01060110/0x01060440 and bodies in ACP CA 0x02000000+ | [V*] | `wifi-d2h-direct/REPORT.md`, `wifi-ete-frames/REPORT.md` sec 2.5 | ledger 2026-11-24 13:10 | - |
| EV-048 | 4 F4 | the ETE engine copy is firmware-triggered and idle; no host write starts it | [V]/[V*] | `wifi-ete-engine/REPORT.md` (150-check selftest), `wifi-ete-frames/REPORT.md` | engine status reads 2026-11-24 | lane selftest quoted |
| EV-049 | 4 F4 | direct read delivers the ready pair and catalogue with zero overruns | [V*] | `wifi-d2h-direct/REPORT.md` sec 5 | ledger 2026-11-24 14:00 | selftest 92/92 |
| EV-050 | 4 F5 | the g0 id1 key-0xD8 handler dereferences ctx+0x190; only the key-0x64 create writes it | [V] | `wifi-ctx-create/REPORT.md`, `wifi-h2d-send/REPORT-G0ID1-HANDLER.md` | - | ctx selftests 40/40+26/26 |
| EV-051 | 4 F5 | context-create type 1 allocates ctx+0x190 (0x01011448) and makes the TXQ sync clean | [V*] | `wifi-h2d-send/REPORT.md` sec 11-12 | ledger 2026-11-25 05:10 | - |
| EV-052 | 4 F5 | the ordered lap produces firmware responses g1 id3/g1 id0x12 | [V*] | `wifi-h2d-send/REPORT.md` sec 12, TABLES T13 | ledger 2026-11-25 05:10/06:00 | frame bytes cross-checked |
| EV-053 | 4 F6 | the 20-bit region-3 mask mis-maps out[3]/out[4] by 1 MiB | [V] | `wifi-hcc-core/src/hcc_core.h`, `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 17:20 | - |
| EV-054 | 4 F6 | after the fix the deferred-notify flush retires (g+0xB4 = g+0xB8 = 0) | [V*] | `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 21:10 | - |

| EV-134 | 4 F7 | the calibration carrier is g0 id2 key 0x0101, 298 B, with six cfg ids recovered statically | [V] | `wifi-proto-seq/REPORT.md` sec 4-6 | - | proto-seq 66/66 |
| EV-135 | 4 F7 | C1 bench: frame consumed in 0 polls, 298 B g1 id3 answer counted, mask/page clean, body recycled | [V*] | `wifi-h2d-send/REPORT.md` sec 13 | ledger 2026-11-25 12:30 | - |
| EV-136 | 4 F8 | cfg80211 M0/M1 model with ready-gated wiphy registration (no MMIO, no H2D from the cfg layer) | [V] | `wifi-cfg80211/REPORT.md` sec 3-4 | - | cfg80211 logic 454/454 |
| EV-137 | 4 F8 | the cfg80211 image was flashed to kernelb, the wiphy registered, iw enumerates phy0 and a VAP netdev is created/deleted | [V*] | ledger 2026-11-25 15:00, `STATUS.md` 3k, T21/T22 | - | - |
| EV-138 | 4 F9 | the frame plane is the separate MAC slot-5 ring block; the vendor's next frame is the TXQ address sync g0 id1 key 0x0320 | [V] | `wifi-datapath/REPORT.md` sec 0/2/5 | - | datapath 77/77 |
| EV-139 | 4 F9 | the ready g4 id2 payload seeds the MSDU-info ring pointer table (ACP 0x02060850 -> MAC+0x10) | [V]/[V*] | `wifi-datapath/REPORT.md` sec 0.3 | ledger 2026-11-24 14:00 | - |
| EV-140 | 4 F10 | PD4 is a two-window patch (4-byte entry redirect + 64-byte stub in the dead CPU 0x108240 gap), image hash cfe24bfb..., length unchanged | [V]/[V*] | `wifi-irq-unmask/REPORT-PD4.md` sec 2-3, T25 | ledger 2026-11-25 02:10 | - |
| EV-141 | 4 F10 | no vendor-clean (unpatched-firmware) mailbox-delivery path has been demonstrated; the audit's only host-only candidate is a spare-iATU aperture onto the event controller + one-shot event-0x4C raise (untested) | [V]/[I]/[U] | `wifi-fw-audit/REPORT.md` sec 7 | - | - |

| EV-186 | 4 F10 | PD4 rebuilds byte-identically from the pinned blob: exactly 2 segments, 68 patched bytes, 59 differing byte positions, length unchanged 928,920 B | [V] | `wifi-fw-audit/REPORT.md` sec 2, `verify_pd4.py` (31 checks) | - | **yes** (harness re-run 31/31) |
| EV-187 | 4 F10 | the stub's 637-byte cave has zero references (exhaustive Thumb census + ARM filtered scan), sits outside both boot memsets and the exception-staging frame | [V] | `wifi-fw-audit/REPORT.md` sec 4 | - | **yes** (harness re-run) |
| EV-188 | 4 F10 | H1 confirmed by the 5-word read-only pack: sentinel field CPU `0x10C1B0` = static `0x00118D68`, live table CPU `0x118D68` all zero, ROM RAZ `0x2000` = 0 -> the dispatcher handlers never execute under PD4 | [V*] | `wifi-fw-audit/REPORT.md` sec 5, ledger 2026-11-25 21:30 | ledger 2026-11-25 21:30 | - |
| EV-189 | 4 F10 | diagnostic variants PD4-NOSENT (`0bc1b3e8...`) and PD4-ALWAYS0 (`84e9cd95...`) are built/hashed; PD4-MIN/PD4-LITE specified; one-shot epilogue call rejected | [V] | `wifi-fw-audit/REPORT.md` sec 3, `pd4_variants.py` | - | **yes** (14/14 selftest) |
| EV-202 | 4 F7 | C2 fired: six calibration messages (3502-3507) consumed in 0 polls and all six answered by 298 B g1 id3 (catalogue count 7, `last_total 0x012A`); automated C2 PASS 54/54 | [V*] | `wifi-proto-seq/REPORT.md` sec 7, `wifi-bench-tooling/PHASES.md`, TABLES T23 | ledger 2026-11-25 16:30/23:40 | - |
| EV-203 | 4 F8 | the cfg80211 management interface later carries real VAP messages and the scan request; the per-radio VAP start object is still absent | [V*]/[I] | `wifi-scan-ap/REPORT.md`, TABLES T22/T27 | ledger 2026-11-26 04:30/06:30 | - |
| EV-204 | 4 F9 | the frame plane is host-implemented (dp_probe/dp_setup/dp_rings/dp_txq_sync) and E1 PASS; no frame has been sent or received | [V]/[V*] | `wifi-datapath-bringup/REPORT.md`, TABLES T26 | ledger 2026-11-26 03:00 | v3 build re-run |
| EV-205 | 4 F10 | H1 confirmed: sentinel pointer keeps static `0x00118D68`, live handler table all zero, so PD4 dispatches through the ROM zero table and its handlers never execute; duplex is non-mailbox | [V*]/[I] | ledger 2026-11-25 21:30, `wifi-fw-audit/REPORT.md` sec 5, TABLES T25 | ledger 2026-11-25 21:30 | - |
| EV-206 | 4 F11 | the four management-frame builders recovered from firmware handlers: g0 id7 168->180 B, id8 168->180 B, id9 12->24 B, id0xC 508->520 B, with SR node word1s | [V] | `wifi-vap-scan/REPORT.md` sec 3, `recipes/frames.md`, TABLES T27 | - | v3 host build 137/137 |
| EV-207 | 4 F11 | provider module 676,180 B sha256 `4a1c3b9a...` with caps `0x1f`: id7 commits the VAP, id8/id0xC consumed in 0 polls, scan yields no results -> VAP-start wall | [V]/[V*] | `wifi-scan-ap/BUILD-NOTES.md`, `wifi-scan-ap/REPORT.md`, TABLES T27 | ledger 2026-11-26 04:30/06:30 | v3 source selftest 294/294 |

## Section 5 - protocol reference

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-055 | 5.1 | the 12-byte header: total = payload+12 at +4, id u16 at +6, tag 0x5A5A at +0x0A, group low nibble of byte 0 | [V] | `wifi-ete-frames/REPORT.md` sec 1.3, `wifi-h2d-frames/REPORT.md` | - | h2d-frames selftest 233 |
| EV-056 | 5.1 | a frame is valid iff tag 0x5A5A, total == node length, dst core matches | [V] | `wifi-h2d-frames/REPORT.md`, `wifi-d2h-direct/REPORT.md` | - | h2d-frames selftest 233 |
| EV-057 | 5.2 | group 0 (41 slots), group 2 (19), groups 1/3/4 direction and use | [V] | `wifi-h2d-catalog/REPORT.md` sec 4-6, `wifi-h2d-frames/catalogue.md` | - | catalogue counts checked |
| EV-058 | 5.2 | keys: 0x0064 = context-create, 0x00D8 = TXQ config sync; key at payload+8, len at +0xA, data at +0xC | [V] | `wifi-ctx-create/REPORT.md`, `wifi-h2d-frames/REPORT.md` | - | ctx selftests |
| EV-059 | 5.3 | H2D announce: OR 1<<id into out[0] (CA 0x40039010) then OR bit0 into out[2] (CA 0x400392D4); poll out[0] to 0 | [V] | `wifi-host-replica/REPORT.md` sec 3.1, `wifi-h2d-frames/REPORT.md` | bench 2026-11-24 15:10 | - |
| EV-060 | 5.3 | mailbox ids 1/3/5/6 semantics; 5 is a BUG trap; 0,2,4,7,8,9 dropped; never write CA 0x400392F0 | [V] | `wifi-h2d-catalog/REPORT.md` sec 3 | - | - |
| EV-061 | 5.4 | vendor D2H service order: bitmap -> ack out[3] -> drain -> re-arm out[4] -> dispatch lowest-first | [V] | `wifi-host-replica/REPORT.md` sec 4.2, `wifi-hcc-core/src/hcc_core.h` | - | hcc-core selftest 528 |
| EV-062 | 5.4 | handler table ids {1..7}; 0/8/9/10 dropped; id > 10 stops the word | [V] | `wifi-host-replica/REPORT.md` sec 4.3 | - | hcc-core selftest 528 |
| EV-063 | 5.5 | SR0-2 / DR0-3 channel map, 8-byte node format, packed index/depth 32 | [V]/[V*] | `wifi-transport-port/REPORT.md` sec 1, `wifi-ete-frames/REPORT.md` | bench A/B2 2026-11-22 | - |
| EV-064 | 5.6 | the boot register table (release/gate/page/mask/mailbox) | [V]/[V*] | TABLES T4/T9; lanes as cited | GP8, PD3 | constants cross-checked |

| EV-142 | 5.7 | Wi-Fi frames do not ride HCC/ETE; the TXQ-table address sync is g0 id1 key 0x0320 (4-byte device VA) | [V] | `wifi-datapath/REPORT.md` sec 2/5 | - | - |
| EV-143 | 5.7 | TX entry and RX ring/descriptor geometry (8-byte MSDU-info, 256/512 free rings, 4095 completion ring, 0x28 B descriptor) | [V] | `wifi-datapath/REPORT.md` sec 6-7 | - | - |

## Section 6 - the operational duplex loop

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-065 | 6 | the ordered lap session shape (S0-S11) is what the bench actually ran | [V*] | `wifi-h2d-send/BENCH-PROCEDURE.md` Ladder C, this report sec 6 | ledger 2026-11-25 05:10 | - |
| EV-066 | 6 | PACK BEFORE: ctx190 = 0, ctx0 = 0, table entry 0x0011C458, mask 0 | [V*] | `wifi-h2d-send/REPORT.md` sec 11 | ledger 2026-11-25 05:10 | - |
| EV-067 | 6 | type-1 create writes ctx+0x190 = 0x01011448, ctx[0]=4, ctx+0x10=0x00010001, ctx+0x434=0x0100478C | [V*] | `wifi-h2d-send/REPORT.md`, `wifi-ctx-create/README.md` | ledger 2026-11-25 05:10 | - |
| EV-068 | 6 | TXQ sync consumed with out0/out2/out5 = 0, mask 0, staging 0, h2d_status ok=2 timeout=0 | [V*] | `wifi-h2d-send/REPORT.md` sec 12 | ledger 2026-11-25 05:10 | - |
| EV-069 | 6 | the module used was 400,732 B sha256 774c4f1f... with 242/242 + 233/233 selftests | [V] | `wifi-h2d-send/BUILD-NOTES.md` | ledger 2026-11-25 05:10 | - |
| EV-070 | 6 | the hcc_core state machine IDLE->BOOT->WAIT_PLAT->WAIT_DEV->READY_TXQ_SYNC->ACTIVE | [V] | `wifi-hcc-core/src/hcc_core.h`, `INTEGRATION.md` | - | hcc-core selftest 528 |

| EV-144 | 6.1 | the ordered lap is deterministic on a fresh boot (identical context addresses/values) | [V*] | `wifi-h2d-send/REPORT.md` sec 11-12 | ledger 2026-11-25 07:40 | - |
| EV-145 | 6.2 | calibration carrier geometry: g0 id2, key 0x0101, 270 B blob, 298 B frame/node, field B = module | [V] | `wifi-proto-seq/REPORT.md` sec 5 | - | - |
| EV-146 | 6.2 | the six vendor calibration cfg ids and value sizes (3502-3507) | [V] | `wifi-proto-seq/REPORT.md` sec 5.3 | - | - |
| EV-147 | 6.2 | C1 fired set_rssi_param 3507: consumed in 0 polls; answer catalogue count=2, last 298 B; mask 0/page golden | [V*] | `wifi-h2d-send/REPORT.md` sec 13 | ledger 2026-11-25 12:30 | - |
| EV-148 | 6.2 | the C1 answer body was recycled before capture; g1 id3 is the generic config-sync channel (syn_id dispatch) | [V*]/[V] | `wifi-proto-seq/REPORT.md` sec 6, REPORT correction C20 | ledger 2026-11-25 12:30 | - |
| EV-149 | 6.3 | image build artifacts: cfg80211.ko 949393f9..., container 22b72852..., kernel slice 13e81214..., slot 1794a531... | [V] | `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5, T21 | - | - |
| EV-150 | 6.3 | the flash went through stock's mtd to kernelb with a full-slot verify and the documented handover; stock pair A preserved | [V*] | `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` sec 3, ledger 2026-11-25 15:00 | - | - |
| EV-151 | 6.3 | cfg80211.ko + the clean driver loaded; one wiphy per endpoint; registration gated on the ready pair | [V*] | ledger 2026-11-25 15:00, `wifi-cfg80211/REPORT.md` sec 3 | - | - |
| EV-152 | 6.3 | iw enumerates Wiphy phy0 and the nl80211 VAP wlan0 is created and deleted (/sys/class/ieee80211/phy0 exists) | [V*] | ledger 2026-11-25 15:00, `STATUS.md` 3k | - | - |
| EV-153 | 6.3 | M0/M1 limits at the v2 cut-off: no firmware VAP object, no scan/association/data path, cfg80211 config bench-only. v3 adds the mailbox-layer VAP and the accepted scan request; the per-radio VAP start and scan results remain open (v3 claims in section 6) | [V*]/[I] | `wifi-cfg80211/REPORT.md` sec 6/8, REPORT sec 10.1 | - | - |
| EV-154 | 6.4 | two-plane model: MAC host-interface register map, TX/RX ring formats and the 217/88-slot key tables | [V] | `wifi-datapath/REPORT.md` sec 2/4/6/7 | - | datapath 77/77 + 35/35 |
| EV-155 | 6.4 | data-plane bring-up order D0-D5 with a read-only B2 probe first; executed in v3 as B2a probe -> dp_setup -> TXQ address sync (E1 PASS) | [I]/[V*] | `wifi-datapath/REPORT.md` sec 9-10, `wifi-datapath-bringup/REPORT.md`, T26 | ledger 2026-11-26 03:00 | - |
| EV-156 | 6.5 | the trigger chain: event 0x2E status bits 24-28 -> sending trigger 0xC625A; no image or vendor-host writer of status/send/doors | [V] | `wifi-ete-wake/REPORT.md` sec 2-3 | - | ete-wake 81/81 |
| EV-157 | 6.5 | E1a/E1b negative on hardware: the +0x38 host lever left status/send/fills unmoved; direct read stays the supported RX path | [V*] | `wifi-ete-wake/REPORT.md` sec 8, ledger 2026-11-25 10:10 | - | - |
| EV-158 | 6.5 | the SR (H2D) A->B copy runs normally while the DR direction is gated (the asymmetry that keeps H2D working) | [V*] | `wifi-ete-wake/REPORT.md` sec 6 | ledger 2026-11-24 15:10/22:10 | - |

| EV-190 | 6.6 | the PD4 stub only reads out[0] CA `0x40039010` and calls the firmware wrapper `0x40294` -> dispatcher `0xC18AC`; every device write (ack/clear/doorbell/handler dispatch) is the firmware's own | [V] | `wifi-fw-audit/REPORT.md` sec 6 | - | - |
| EV-191 | 6.6 | audit verdict: patching is required for the closed vendor blob as delivered, conditional only on the untested event-controller aperture experiment; with H1 confirmed PD4 is a consume-only mailbox servicer (handlers never run), not fundamental to the hardware; PD4 research-only | [I]/[V*] | `wifi-fw-audit/REPORT.md` sec 7-10, ledger 2026-11-25 21:30 | ledger 2026-11-25 21:30 | - |
| EV-208 | 6.2 | C2: all six calibration messages consumed in 0 polls; six 298 B g1 id3 answers (count 7, last_total 0x012A, last_len 298); mask 0, staging 0, page golden, ok=8 timeout=0; automated 54/54 | [V*] | TABLES T23, `wifi-proto-seq/REPORT.md` sec 7, `wifi-bench-tooling/PHASES.md` | ledger 2026-11-25 16:30/23:40 | - |
| EV-209 | 6.4 | E1 PASS 37/37: dp_probe one RO read, dp_setup (rings + 8 MSDU entries + 14 MAC writes with read-back), dp_rings snapshots, TXQ sync accepted (`txq_addr_sent=1 ok=2 timeout=0`); module 462,952 B sha256 `8fc693ad...` | [V]/[V*] | `wifi-datapath-bringup/REPORT.md`, TABLES T26 | ledger 2026-11-26 03:00 | v3 kernel build + 69/69 + 34/34 |
| EV-210 | 6.6 | H1 pack detail: CPU `0x10C1B0` = `0x00118D68`, CPU `0x118D68` all zero at 2 s and 48 s, ROM RAZ `0x2000` = 0, sanity anchor CPU `0x40000` = `0x00046971`; PD4-ALWAYS0 should behave identically, PD4-NOSENT would enter the empty table | [V*] | ledger 2026-11-25 21:30, `wifi-fw-audit/REPORT.md` sec 5, TABLES T25 | ledger 2026-11-25 21:30 | - |
| EV-211 | 6.7 | real-frame operational record: user_add (slot 2, devva `0x8417c000`, producer 2->3, posted 1/8), user_add_notify (posted 2/8), scan_req (slot 4, devva `0x8371f000`, producer 4->5, posted 3/8), all consumed in 0 polls | [V*] | TABLES T27, ledger 2026-11-26 06:30 | ledger 2026-11-26 06:30 | - |
| EV-212 | 6.7 | the firmware scan handler CA `0x6494C` reads VAP id 0 and needs the per-radio VAP started; no D2H scan events arrived, the scan completed aborted (results=0, timeout=1) and `iw` said "scan aborted!" | [V]/[V*]/[U] | `wifi-vap-scan/REPORT.md` sec 3.4, TABLES T27, ledger 2026-11-26 06:30 | ledger 2026-11-26 06:30 | - |
| EV-213 | 6.8 | one-command matrix result: boot PASS, C2 PASS 54/54, D1 PASS 21/21, D2 BLOCKED-DRIVER, E1 PASS 37/37 with per-phase evidence and bounded reboots | [V*] | `wifi-bench-tooling/PHASES.md`, TABLES T28 | ledger 2026-11-26 05:30 | - |
| EV-214 | 6.8 | bounded reboot orchestration: immediate `/sbin/reboot -f` background, every transport call budget-clamped, `[reboot]` progress, observed back after 61.5 s (uptime 46 s, seen_down=1), killable phase process groups under `--phase-timeout` | [V*] | `wifi-bench-tooling/REPORT.md` sec 9.9, ledger 2026-11-26 05:30 | ledger 2026-11-26 05:30 | - |
| EV-215 | 6.8 | the bench-tooling device-free selftest is 503/503 in v3 (was 312 in v2) | [V] | `wifi-bench-tooling/tools/selftest.py` | - | **yes** (v3 re-run 503/503) |
| EV-216 | 6.7 | provider facts: module 676,180 B sha256 `4a1c3b9a...`, caps `0x1f`, `frame_gap_ms` 20, `allow_frames` default 0 (fail-closed), 0 compiler/modpost warnings | [V] | `wifi-scan-ap/BUILD-NOTES.md`, `wifi-scan-ap/REPORT.md` | - | v3 source selftest 294/294 |
| EV-217 | 6.7 | state after the real-frame session: golden page `0x86807FEF/7/2/0xABCD`, state 1, mask 0, staging 0, heartbeats continuing, no exception, stock pair preserved | [V*] | ledger 2026-11-26 06:30 | ledger 2026-11-26 06:30 | - |
| EV-218 | 6.4 | data-plane [U] remainders: no frame traversed, RX completion ring/descriptor format unconsumed, rings assume user index 0, TX entry encoder/20 B descriptor untested on hardware, band 1 untouched | [U]/[I] | `wifi-datapath-bringup/REPORT.md` sec 4, TABLES T26 | - | - |

## Section 7 - bench evidence chain

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-071 | 7 | 2026-11-22 transport A/B1/B2: six viewports, upload diffs=0, release, D2H bitmap, ETE rings | [V*] | `wifi-transport-port/SUMMARY.md` | ledger 2026-11-22 | - |
| EV-072 | 7 | 2026-11-23 recovery + install-from-stock; B3 cold-boot ready gate NEGATIVE | [V*] | `wifi-fw-ready-gate/REPORT.md` sec 14 | ledger 2026-11-23 | - |
| EV-073 | 7 | 2026-11-24 01:15 GP1: reset pulse makes ROM S0/S1 run | [V*] | `wifi-chip-reset/REPORT.md` | ledger 2026-11-24 01:15 | - |
| EV-074 | 7 | 2026-11-24 02:20 GP1 flakiness; pre-upload state identical across boots | [V*] | `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 02:20 | - |
| EV-075 | 7 | 2026-11-24 03:10 GP2: mask 0 but S1 never completes | [V*] | `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 03:10 | - |
| EV-076 | 7 | 2026-11-24 04:00 GP3: TCM bounds -> TCM copied, S1 still absent | [V*] | `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 04:00 | - |
| EV-077 | 7 | 2026-11-24 05:00/06:20 stock golden reads: page, TCM, platform block, wifi block | [V*] | `wifi-s1-cpu1/REPORT.md`, TABLES T6/T7 | ledger 2026-11-24 05:00/06:20 | - |
| EV-078 | 7 | 2026-11-24 05:40 GP4: TCM byte-identical incl. coreword; S1 still absent | [V*] | `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 05:40 | - |
| EV-079 | 7 | 2026-11-24 07:20 kprobe capture of the vendor pre-release order | [V*] | this report sec 3.6; lane records | ledger 2026-11-24 07:20 | - |
| EV-080 | 7 | 2026-11-24 08:00/08:45 GP5/GP6: both blocks and vendor-matched timing do not fix S1 | [V*] | `wifi-s1-cpu1/REPORT.md` | ledger 2026-11-24 08:00/08:45 | - |
| EV-081 | 7 | 2026-11-24 09:20 mask ROM dumped and host-readable | [V*] | `wifi-rom-dump/REPORT.md` | ledger 2026-11-24 09:20 | - |
| EV-082 | 7 | 2026-11-24 10:00 root cause found; 10:40 GP8 golden boot | [V]/[V*] | `wifi-rom-dump/REPORT.md`, `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 10:00/10:40 | - |
| EV-083 | 7 | 2026-11-24 11:10-12:20 GP9/GP10/hcc-service: boot golden, IRQ layer dark, mailbox dead | [V*] | `wifi-hcc-service/REPORT.md` | ledger 2026-11-24 11:10/11:40/12:20 | - |
| EV-084 | 7 | 2026-11-24 13:10-14:00 D2H frames decoded; complete ready handshake received | [V*] | `wifi-d2h-direct/REPORT.md` | ledger 2026-11-24 13:10/14:00 | - |
| EV-085 | 7 | 2026-11-24 15:10 first H2D frame delivered; mailbox not acked; no-newline grammar rule | [V*] | `wifi-h2d-inject/REPORT.md` | ledger 2026-11-24 15:10 | - |
| EV-086 | 7 | 2026-11-24 16:10 PD: exception path reached, page clobbered | [V*] | `wifi-irq-unmask/REPORT.md` | ledger 2026-11-24 16:10 | - |
| EV-087 | 7 | 2026-11-24 17:20 F4 bug found; h2d-frames + hcc-core land | [V] | `wifi-hcc-core/REPORT.md`, `wifi-h2d-frames/REPORT.md` | ledger 2026-11-24 17:20 | selftests 528/233 |
| EV-088 | 7 | 2026-11-24 18:25-19:00 PD2 analysis and PD2-A boot failure (live cave) | [V]/[V*] | `wifi-irq-unmask/REPORT-PD2.md` | ledger 2026-11-24 18:25/19:00 | - |
| EV-089 | 7 | 2026-11-24 20:00-21:10 clean driver lands; F4 verified on hardware (flush retires) | [V]/[V*] | `wifi-driver-clean/REPORT.md` | ledger 2026-11-24 20:00/21:10 | - |
| EV-090 | 7 | 2026-11-24 22:10-22:45 PD3 works and is recorded | [V*] | `wifi-irq-unmask/REPORT-PD3.md` sec 5.4 | ledger 2026-11-24 22:10/22:45 | - |
| EV-091 | 7 | 2026-11-24 23:40-2026-11-25 00:20 TXQ sync faults; exception record read | [V*] | `wifi-h2d-send/REPORT.md`, `wifi-irq-unmask/REPORT-PD4.md` | ledger 2026-11-24 23:40, 11-25 00:20 | - |
| EV-092 | 7 | 2026-11-25 01:10 g0 id0 control stub consumed with no exception | [V*] | `wifi-h2d-send/REPORT.md` | ledger 2026-11-25 01:10 | - |
| EV-093 | 7 | 2026-11-25 02:10 PD4 cave works; fault reproduces; handler cascades | [V*] | `wifi-irq-unmask/REPORT-PD4.md` | ledger 2026-11-25 02:10 | - |
| EV-094 | 7 | 2026-11-25 03:10-05:10 context-create RE; type-3 lap; type-1 breakthrough | [V]/[V*] | `wifi-ctx-create/REPORT.md`, `wifi-h2d-send/REPORT.md` | ledger 2026-11-25 03:10/04:10/05:10 | ctx selftests |
| EV-095 | 7 | 2026-11-25 06:00 response frames decoded byte-exactly | [V*] | this report TABLES T13, lane records | ledger 2026-11-25 06:00 | frame bytes cross-checked |

| EV-159 | 7 | 2026-11-25 07:40 - ordered lap reproduced deterministically across boots | [V*] | `wifi-h2d-send/REPORT.md` | ledger 2026-11-25 07:40 | - |
| EV-160 | 7 | 2026-11-25 08:40 - frame plane identified; first automated protocol-runner fire exposed host-side bugs | [V]/[V*] | `wifi-datapath/REPORT.md`, `wifi-bench-tooling/REPORT.md` | ledger 2026-11-25 08:40 | - |
| EV-161 | 7 | 2026-11-25 09:10 - cfg80211 lane lands host-side (wiphy/VAP M0-M2) | [V] | `wifi-cfg80211/REPORT.md` | ledger 2026-11-25 09:10 | - |
| EV-162 | 7 | 2026-11-25 10:10 - ETE-wake E1a/E1b negative; DR copy classified device/ROM-internal | [V*] | `wifi-ete-wake/REPORT.md` | ledger 2026-11-25 10:10 | - |
| EV-163 | 7 | 2026-11-25 11:00 - g1 id3/id0x12 decoded and the six calibration frames built | [V] | `wifi-proto-seq/REPORT.md` | ledger 2026-11-25 11:00 | - |
| EV-164 | 7 | 2026-11-25 11:40 - bench-auto protocol mode rebuilt on a raw-read transport helper (312/312) | [V] | `wifi-bench-tooling/REPORT.md` sec 8 | ledger 2026-11-25 11:40 | - |
| EV-165 | 7 | 2026-11-25 12:30 - ladder C1: the firmware answers the first calibration message | [V*] | `wifi-h2d-send/REPORT.md` sec 13 | ledger 2026-11-25 12:30 | - |
| EV-166 | 7 | 2026-11-25 13:30 - wave-2 lanes and the target build all delivered (image/slice hashes recorded) | [V] | `wifi-upstream-target/BUILD-VERIFICATION.md` | ledger 2026-11-25 13:30 | - |
| EV-167 | 7 | 2026-11-25 15:00 - first real Wi-Fi interface on our own image (wiphy + VAP, M0/M1) | [V*] | `wifi-cfg80211/REPORT.md`, T21/T22 | ledger 2026-11-25 15:00 | - |
| EV-219 | 7 | 2026-11-25 16:30 - ladder C2: all six calibration messages answered (six 298 B g1 id3), calibration boot complete | [V*] | `wifi-proto-seq/REPORT.md`, TABLES T23 | ledger 2026-11-25 16:30 | - |
| EV-220 | 7 | 2026-11-25 18:30/19:30 - scan-ap fail-closed module bench-validated; VAP-scan builders, data-plane host side, image integration and provider integration land host-side | [V]/[V*] | lane reports; ledger 2026-11-25 18:30/19:30 | ledger 2026-11-25 18:30/19:30 | - |
| EV-221 | 7 | 2026-11-25 21:30 - H1/H2 decisive read-only pack: PD4 consume-only, handlers never execute; duplex is non-mailbox | [V*] | ledger 2026-11-25 21:30, `wifi-fw-audit/REPORT.md` sec 5 | ledger 2026-11-25 21:30 | - |
| EV-222 | 7 | 2026-11-25 23:40 - first fully automated bench phase PASS (C2, 54/54, 7.02 s) | [V*] | `wifi-bench-tooling/REPORT.md`, ledger | ledger 2026-11-25 23:40 | - |
| EV-223 | 7 | 2026-11-26 03:00 - automated matrix per-phase: C2 PASS, D1 PASS, E1 PASS, D2 documented BLOCKED-DRIVER | [V*] | ledger 2026-11-26 03:00, `wifi-bench-tooling/PHASES.md` | ledger 2026-11-26 03:00 | - |
| EV-224 | 7 | 2026-11-26 04:30 - first real firmware management message (g0 id7 user_add 180 B consumed, VAP committed); gaps: id8 inflight refusal, scan caps 0x1d vs 0x3 | [V*] | ledger 2026-11-26 04:30, `wifi-scan-ap/REPORT.md` sec 3.1 | ledger 2026-11-26 04:30 | - |
| EV-225 | 7 | 2026-11-26 05:30 - one-command full matrix complete with bounded reboot orchestration | [V*] | ledger 2026-11-26 05:30, TABLES T28 | ledger 2026-11-26 05:30 | - |
| EV-226 | 7 | 2026-11-26 06:30 - three real frames (user_add, user_add_notify, first scan request) consumed in 0 polls; no scan results within 5 s -> scan aborted; VAP-start hypothesis | [V*] | ledger 2026-11-26 06:30, TABLES T27 | ledger 2026-11-26 06:30 | - |
| EV-227 | 7 | end-of-chain state 2026-11-26 06:30: cfg80211 bench image on pair B, boot/C2/D1/E1 PASS in one command, D2 BLOCKED-DRIVER, no pending writes, stock pair preserved | [V*] | ledger 2026-11-26 06:30 | ledger 2026-11-26 06:30 | - |

## Section 8 - corrections register

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-096 | 8 | C1: bitmap 0x4 is not DEVICE READY; the ready handshake is g4 id1+id2 | [V] | `wifi-host-replica/REPORT.md` | - | - |
| EV-097 | 8 | C2: release-only boot (no 0xCECE during boot) | [V]/[V*] | `wifi-rom-dump/REPORT.md` | GP8 | - |
| EV-098 | 8 | C3/C4: exception-bit map and header-order corrections | [V] | `wifi-rom-dump/REPORT.md` sec 9 | - | - |
| EV-099 | 8 | C5: "S1 not host-triggerable" was wrong; our own write was the blocker | [V*] | `wifi-rom-dump/REPORT.md` | GP6 vs GP8 | - |
| EV-100 | 8 | C6: the engine copy is not required for D2H receive | [V*] | `wifi-d2h-direct/REPORT.md` | 2026-11-24 13:10 | - |
| EV-101 | 8 | C7: the scheduler-fingerprint conclusion was an instrument aliasing bug | [V] | `wifi-fw-postboot/REPORT.md` | - | - |
| EV-102 | 8 | C8: out[3]/out[4] region-3 translation fix | [V]/[V*] | `wifi-driver-clean/REPORT.md` | 2026-11-24 21:10 | - |
| EV-103 | 8 | C9: the PD3 cave is not safe for first-fault analysis; PD4 moved it | [V*] | `wifi-irq-unmask/REPORT-PD4.md` | 2026-11-25 02:10 | - |
| EV-104 | 8 | C10: out[2]/out[5] are non-latching pulse observables | [V] | `wifi-irq-unmask/REPORT-PD3.md` sec 5.5 | 2026-11-24 22:45 | - |
| EV-105 | 8 | C11/C12: context-create precondition; type 1 allocates | [V*] | `wifi-ctx-create/REPORT.md`, `wifi-h2d-send/REPORT.md` | 2026-11-25 04:10/05:10 | - |
| EV-106 | 8 | C13: the PD2 cave is live on our boot | [V*] | `wifi-irq-unmask/REPORT-PD2.md` | 2026-11-24 19:00 | - |
| EV-107 | 8 | C14: the g0 id1 fault is handler-specific, not transport | [V*] | `wifi-h2d-send/REPORT.md` | 2026-11-25 01:10 | - |
| EV-108 | 8 | C15: sysfs grammar needs the no-newline write | [V*] | `wifi-h2d-send/BENCH-PROCEDURE.md` | 2026-11-24 15:10 | - |
| EV-109 | 8 | C16: GP1's pulse is necessary but not sufficient | [V*] | `wifi-s1-cpu1/REPORT.md` | 2026-11-24 06:20/10:40 | - |

| EV-168 | 8 | C17: a Wi-Fi management interface can exist before the full stack (wiphy+VAP validated) | [V*] | REPORT sec 8; bench 2026-11-25 15:00 | - | - |
| EV-169 | 8 | C18: CONFIG_CFG80211 is no longer a blocker (built, loaded, exercised) | [V] | `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5 | - | - |
| EV-170 | 8 | C19: the ETE DR copy is not wakeable from the host-owned published index | [V*] | `wifi-ete-wake/REPORT.md` sec 8 | ledger 2026-11-25 10:10 | - |
| EV-171 | 8 | C20: g1 id3 is a generic syn_id config-sync channel; the 36 B frame is a VAP-MIB event and g1 id0x12 is the ACS family | [V] | `wifi-proto-seq/REPORT.md` sec 2-3 | - | - |
| EV-172 | 8 | C21: Wi-Fi frames use the MAC slot-5 ring block, not HCC/ETE; the direct-read constraint is message-only | [V] | `wifi-datapath/REPORT.md` sec 2/8 | - | - |
| EV-173 | 8 | C22: the first TXQ-sync fault was the missing context precondition, not a 48-vs-72 node-length problem | [V*] | `wifi-h2d-send/REPORT.md` sec 12 | ledger 2026-11-25 05:10 | - |
| EV-174 | 8 | C23: empty bench step captures were the raw-read transport, not a sequence failure; bar0read fixed it | [V] | `wifi-bench-tooling/REPORT.md` sec 8 | ledger 2026-11-25 11:40 | - |
| EV-175 | 8 | C24: an unconsumed announce can be the exception cascade corrupting the PD3 poller | [V*] | `wifi-irq-unmask/REPORT-PD4.md`, `wifi-h2d-send/REPORT.md` | ledger 2026-11-25 01:10-05:10 | - |
| EV-176 | 8 | C25: the cfg80211 config belongs to the bench image, not the upstream kernel series | [V] | `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` sec 5 | - | - |
| EV-177 | 8 | C26: the domain->band wiphy mapping is an inference with a band_force override | [I] | `wifi-cfg80211/REPORT.md` sec 3/10 R4 | - | - |

| EV-192 | 8 | C27: the event-raise helper targets CA `0x40161188`/`0x40161288` for event 0x4C (not `0x401611A0`) | [V] | `wifi-fw-audit/REPORT.md` sec 13 C1, C27 in REPORT | - | - |
| EV-193 | 8 | C28: the vendor "twin/ack word" is simply the out[2] doorbell OR-1; no extra host register needs writing | [V] | `wifi-fw-audit/REPORT.md` sec 13 C2, C28 in REPORT | - | - |
| EV-228 | 8 | C29: the assumption that PD4 dispatches through the real handler table is refuted; H1 confirms the consume-only branch, so the duplex path is non-mailbox | [V*]/[I] | H1 pack ledger 2026-11-25 21:30, REPORT C29/T25 | ledger 2026-11-25 21:30 | - |
| EV-229 | 8 | C30: "C2 is the next open sequence" is superseded - C2 ran and all six messages were answered; automated C2 PASS 54/54 | [V*] | ledger 2026-11-25 16:30/23:40, TABLES T23 | ledger 2026-11-25 16:30/23:40 | - |
| EV-230 | 8 | C31: the payload structs are no longer an absolute wall - byte-exact builders were accepted by the firmware (id7 commit, id8/id0xC consumed); remaining [U] fields did not block acceptance | [V]/[V*] | `wifi-vap-scan/REPORT.md`, TABLES T27, ledger 2026-11-26 | ledger 2026-11-26 04:30/06:30 | - |
| EV-231 | 8 | C32: "data plane mapped only / TXQ sync not fired" is superseded - host implementation + E1 PASS, TXQ sync accepted; still no frame moved | [V]/[V*] | `wifi-datapath-bringup/REPORT.md`, TABLES T26 | ledger 2026-11-26 03:00 | v3 build re-run |
| EV-232 | 8 | C33: a delivered scan request alone does not produce results; the per-radio VAP must be started (scan handler reads VAP id 0) | [V]/[V*]/[U] | firmware handler decode + scan attempt, TABLES T27 | ledger 2026-11-26 06:30 | - |

## Section 9 - verification

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-110 | 9.1 | hcc-core selftest 528/528 PASS | [V] | `wifi-hcc-core/tools/build.sh`, `tests/hcc_core_test.c` | - | **yes** (this pass) |
| EV-111 | 9.1 | h2d-frames build/selftest 233/233 PASS | [V] | `wifi-h2d-frames/tools/build.sh` | - | **yes** (this pass) |
| EV-112 | 9.1 | d2h-direct selftest 92/92 PASS | [V] | `wifi-d2h-direct/tools/d2h_selftest.py` | - | **yes** (this pass) |
| EV-113 | 9.1 | ctx-create selftests 40/40 + 26/26 PASS | [V] | `wifi-ctx-create/tools/ctx_create_re.py`, `ctx_create_frame.py` | - | **yes** (this pass) |
| EV-114 | 9.1 | bench-tooling selftest 177/177 PASS | [V] | `wifi-bench-tooling/tools/selftest.py` | - | **yes** (this pass) |
| EV-115 | 9.2 | static claim reproduction commands are committed and read vendor artifacts in place | [V] | lane READMEs and `REPRODUCE.md` | - | **yes** (paths checked) |
| EV-116 | 9.3 | bench ladders are documented but must remain attended and recovery-gated | [V] | `wifi-driver-clean/BENCH-PROCEDURE.md`, `wifi-h2d-send/BENCH-PROCEDURE.md`, project `RUNBOOK.md` | - | - |
| EV-117 | 9.4 | this report's verifier checks cross-references, markers, claim IDs and key constants | [V] | `wifi-re-report/tools/verify_report.py` | - | **yes** (this pass) |

| EV-178 | 9.1 | the wave-2 device-free lane checks were re-run in this revision: 528, 233, 92, 40+26, 312, 77+35, 66+38+55, 81, 454+242 | [V] | the lanes' committed selftest entry points (see 9.1) | - | **yes** (this pass) |
| EV-179 | 9.1 | the cfg80211 source selftest reports 144/145; the one failure is the known provider-symvers redundancy documented by the target lane | [V] | `wifi-cfg80211/tools/selftest.py`, `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5 | - | **yes** (this pass) |
| EV-180 | 9.4 | the verifier was extended for the v2 tables (T21-T25), milestone vocabulary and constants | [V] | this report's `tools/verify_report.py` | - | **yes** (this pass) |

| EV-194 | 9.1 | the audit harness was re-run in this revision: verify_pd4 14/14 selftest + 31/31 checks; pd4_variants 14/14 | [V] | `wifi-fw-audit/tools/verify_pd4.py`, `pd4_variants.py` | - | **yes** (this pass) |
| EV-195 | 11 | the audit deliverable is a patch recipe + verifier + derived analysis; the patched image and vendor blob stay local (no vendor bytes committed) | [V] | `wifi-fw-audit/README.md` sec publication, `wifi-fw-audit/recipes/PD4.recipe.json` | - | - |
| EV-233 | 9.1 | v3 re-ran the host-only lane checks: bench-tooling 503/503, vap-scan 137/137, scan-ap source 294/294, fw-audit 14/14 + 14/14, datapath-bringup 69/69 + 308/308 + 34/34 | [V] | the lanes' committed selftest entry points | - | **yes** (v3) |
| EV-234 | 9.1 | v3 compile-verified the data-plane module against the project 6.18.60 tree: 462,952 B sha256 `8fc693ad...`, vermagic 6.18.60, 0 compiler/modpost warnings, 13 dp symbols | [V] | `wifi-datapath-bringup/tools/build.sh`, v3 build log | - | **yes** (v3 build) |
| EV-235 | 9.4 | the verifier was extended for v3: section 6.1-6.8, findings F1-F11, tables T1-T28, the v3 evidence range and the v3 milestone vocabulary/constants | [V] | `wifi-re-report/tools/verify_report.py` | - | **yes** (verifier PASS) |
| EV-236 | 9.4 | v3 constants cross-checked against the owning lanes and ledger: `4a1c3b9a`/caps 0x1f, 180/180/520 B, `8fc693ad`/key 0x0320, 503 checks, H1 pack values | [V] | the lanes named per check, superproject ledger | - | **yes** (verifier) |
| EV-237 | 9.3 | the bench-ladder map now includes the C2/D1/D2/E1 phases and the one-command matrix; every real run is attended and recovery-gated | [V*] | TABLES T28, `wifi-bench-tooling/README.md` | ledger 2026-11-26 05:30 | - |

## Sections 10-13 - limits, publication, lane map, conventions

| ID | section | claim | marker | artifact(s) | bench/ledger | re-verified here |
| --- | --- | --- | --- | --- | --- | --- |
| EV-118 | 10 | the result is message-level + management-level + real-request-level, not link-level: wiphy/VAP netdev lifecycle and mailbox-layer VAP frames work, but no per-radio VAP is started, no scan/association result exists and no frame has moved | [I]/[V*] | `wifi-scan-ap/REPORT.md`, `wifi-vap-scan/REPORT.md`, `wifi-cfg80211/REPORT.md` sec 6/8 | ledger 2026-11-25 15:00 - 2026-11-26 06:30 | - |
| EV-119 | 10 | the firmware IRQ/thread layer remains dormant; PD3 bypasses only the mailbox | [V]/[V*] | `wifi-fw-link-event/REPORT.md`, `wifi-irq-unmask/REPORT.md` | 2026-11-24 | - |
| EV-120 | 10 | the context-create frame is synthesized, not vendor-captured; key-0x64 sender unproven | [I] | `wifi-ctx-create/REPORT.md` sec 6/11 | - | - |
| EV-121 | 10 | open questions re-ranked for v2 (VAP/scan payload structs, MAC data-plane bring-up, calibration body capture, vendor-clean mailbox delivery, IRQ layer, CECE service, DR start condition) | [I]/[U] | this report sec 10.2 | - | - |
| EV-122 | 11 | no vendor binary/firmware/ROM/calibration/secret is committed by this lane; blobs remain local | [V] | this report sec 11, project `PUBLISH.md` | - | **yes** (verifier checks) |
| EV-123 | 11 | upstream Wi-Fi stack remains blocked by the closed firmware; GPL source is the route | [V] | `wifi-driver-re/SUMMARY.md`, `opensource/docs/GPL-REQUEST.md` | - | - |
| EV-124 | 12 | the lane documentation map matches the directories present under `opensource/docs/incoming/` | [V] | the directory listing itself | - | **yes** (verifier checks) |
| EV-125 | 13 | conventions and glossary terms as used throughout | [V] | this report sec 13 | - | - |
| EV-181 | 10 | v3 limits: real-request-level (not link-level); H1 consume-only PD4; data plane host-set-up and E1-PASS but frame-moving unproven; D2 BLOCKED-DRIVER; ETE DR closed; calibration bodies not captured | [V*]/[I] | REPORT sec 10.1, T26/T28 | - | - |
| EV-182 | 10.2 | v3 open questions re-ranked: (1) what starts the per-radio VAP, (2) first frame on the rings, (3) calibration-body/scan-field captures, (4) IRQ layer/vendor-clean delivery, then CECE/g2/join/D2H/DR/event-trigger | [I]/[U] | REPORT sec 10.2 | - | - |
| EV-183 | 11 | the cfg80211 bench image is a research instrument, not an upstream deliverable or release | [V] | `wifi-upstream-target/REPORT.md` sec 9/12 | - | - |
| EV-184 | 12 | the lane documentation map now covers the wave-2/3 lanes and the fw-audit lane | [V] | the directory listing under `opensource/docs/incoming/` | - | **yes** (verifier checks) |
| EV-185 | 13 | the glossary gained the cfg80211/MAC-plane/calibration vocabulary | [V] | REPORT sec 13 | - | - |
| EV-238 | 10 | v3 limits updated: real-request-level not link-level; H1 consume-only PD4; data plane host-set-up but no frame; D2 BLOCKED-DRIVER; no per-radio VAP start | [V*]/[I] | REPORT sec 10.1, TABLES T26/T28 | - | - |
| EV-239 | 10.2 | v3 open questions re-ranked: (1) what starts the per-radio VAP, (2) first frame on the rings, (3) calibration-body/scan-field capture, (4) IRQ layer/vendor-clean delivery, then CECE/g2/join/D2H/DR/event-trigger | [U]/[I] | REPORT sec 10.2 | - | - |
| EV-240 | 10.1 | D2 BLOCKED-DRIVER is an honest pending-feature outcome, never "scan works"; the row flips to PASS only when the scan path returns a real result | [V*] | TABLES T28, `wifi-bench-tooling/PHASES.md` | ledger 2026-11-26 05:30 | - |
| EV-241 | 11 | the cfg80211 bench image and the phase matrix are bench instruments, not releases; real frames and phase runs are attended-only | [V] | REPORT sec 11, `wifi-bench-tooling/README.md` | - | - |
| EV-242 | 11 | PD4 remains research-only: H1 confirms it is a consume-only servicer, no vendor-clean mailbox alternative is proven, and it must never ship | [V*]/[I] | REPORT sec 11, `wifi-fw-audit/REPORT.md` | ledger 2026-11-25 21:30 | - |
| EV-243 | 12 | the lane documentation map now covers wifi-vap-scan, wifi-scan-ap and wifi-datapath-bringup, and the updated bench-tooling/fw-audit roles | [V] | the directory listing under `opensource/docs/incoming/` | - | **yes** (verifier checks) |
| EV-244 | 13 | the glossary gained C2/provider-caps/user_add/scan_req/VAP-start/phase-matrix/dp_*/H1 terms | [V] | REPORT sec 13 | - | - |


---

**Coverage statement.** Every numbered section of [REPORT.md](REPORT.md) has at least one claim
row here; every claim row carries a marker and either an artifact path, a ledger date, or an
explicit [I]/[U] status. If a future edit adds a claim to the report without adding it here, the
lane verifier's section-coverage check and the project documentation audit should catch it.

