# W22: kernel-side NAPT init + PIE exception delivery (release-integration design)

Status 2026-11-21. M1a is **PROVEN on hardware**: proof image 5800c9ab (canonical adopted
release tree + command-line change only) booted without the CPU mirror, ping 200/200 0% loss,
working SSH, and UART RXDIAG showed native port=11 deliveries (status 77 class); the E0-a
status=37 error frames are gone. Evidence: m1a/napt-init/proof-image/RESULT.md and
build/evidence-bench-gates/m1a-proof/ (gitignored). The userspace proof script
(m1a/napt-init/proof-image/luofu-napt-init.sh) ran the decoded vendor hi_pfe_napt_init sequence
from an init.d hook. W22 moves that sequence into the driver and makes the native exception
class deliverable by default.

Patch drafts in this directory (validated host-side, not yet in the release series):

| draft | content | state |
| --- | --- | --- |
| 0001-net-lsw-napt-init.patch | luofu-switch.c/.h/regs.h: W22 NAPT init stage | applies to the release tree; compiled in a configured 6.18.60 tree (luofu-switch.o builds); checkpatch 0 errors/0 warnings |
| 0002-net-pie-exception-delivery.patch | luofu-pie.c: deliver the native exception class by default and remove the accepterr gate | applies, luofu-pie.o builds, checkpatch clean |

sha256: 0001 = cfd7debc3908e0b52785afd5ffac979c8c5e03538b3f98dccbf54738fa98152f,
0002 = 8c748061fe0577cea471534f209e68258bc707d45746c25d4f40409f6eeb1bd3.

Host validation performed (details at the end): both patches apply with patch(1) to the release
tree, both objects compile in a configured 6.18 tree, checkpatch clean. They are NOT yet run on
hardware as driver code; the proof used the userspace replay of the same sequence.

## 1. Where the code goes, and why there

- Stage function luofu_switch_napt_program() lives in luofu-switch.c next to the W8/W10 stages,
  with its register constants in luofu-switch-regs.h, and runs in the armed probe path
  **after W8 (PFE base init) and before W10 (IFC defaults)**. That is exactly the vendor order
  decoded in hi_pfe_probe: hi_pfe_init -> hi_pfe_pub_init -> hi_pfe_opp_init ->
  hi_pfe_mcc_init -> **hi_pfe_napt_init** -> **hi_pfe_ifc_init** -> hi_pfe_dfx_init. The W10
  classifier is what routes unicast into NAPT, so the engine must be configured first.
- The userspace proof ran after full boot (all stages done) and worked; the vendor-faithful
  order above is the release form.
- PIE/GEMAC ordering does not change: the GEMAC netdevs only register after
  luofu_switch_ready(), and the switch driver reaches READY only after every stage including W22
  verified. PIE is an independent platform device; its rings are armed by its own gates.

## 2. Gate semantics and idempotence

- New module parameter luofu_switch.naptinit (bool, 0444), **default 1** (on). The sequence is
  bench-proven, so the canonical image does not need a new attended token; the parameter exists
  so a comparison boot can skip the stage (naptinit=0) and as a fail-closed switch if a future
  regression appears.
- The armed chain refuses to arm the data path when the stage is disabled (naptinit=0), because
  the IFC defaults would route unicast into an unconfigured engine and the class would be dropped
  (exactly the E0-a symptom). A skipped stage is a refusal, not a silent half-path.
- Idempotent guard: before writing, the function checks the decoded stock signature
  (napt+0x70 == 0x00110138, napt+0x64 == 1 and the four table bases equal to the derived values).
  If it already holds (driver re-probe, initrd replay, or a userspace replay earlier) the stage
  logs "already initialized" and does nothing - it never reissues the table-clear commands on an
  initialized engine.
- Failure handling: any readback mismatch, unstable pre-read, clear-command timeout (vendor bound
  0x3d40 polls, bit0 busy) or snapshot restore clears the enable word back to its pre value and
  the probe fails closed. Because W22 runs inside the armed chain, a failure restores the
  later/earlier stage snapshots in the existing reverse order (ext -> ifc -> napt -> pfe -> ...)
  and luofu_switch_ready() stays false, so no netdev is exposed on a half-configured switch.

## 3. Carveout safety (the reason the planner refused NAPT before)

- The flow table bases are derived from the variant's measured flow window (variant->flow_base =
  0x80400000, size 1 MiB), which luofu_switch_check_windows() already validates against the
  devicetree switch-node resource. The derived bases are flow_base + {0, 0x8000, 0x48000,
  0x50000}, matching the decoded vendor layout for this flow window size and the stock-captured
  words 0x80400000/0x80408000/0x80448000/0x80450000.
- The stage refuses unless: flow_base equals the measured loader-handoff value; the whole
  base range stays inside the flow window; the flow window does not overlap the queue window;
  and flow_base + flow_size <= 0x80500000, the measured Linux RAM start (devicetree memory node
  starts at 0x80500000, so the switch carveout 0x80000000..0x80500000 is outside Linux memory by
  construction). The CPU never writes into the DDR carveout: the engine initializes its own
  tables through the two napt+0x60 clear commands, exactly like the vendor code.
- Up_port bits 31..16 of pfe_glb+0x2c are written from variant data (napt_up_port = BIT(12), the
  measured stock value for the flow uplink/WAN port), not hardcoded in the stage; the low 16 bits
  are preserved with the existing mask.

## 4. PIE side (patch 0002) and retirement of the attended tokens

- The native deliveries arrive in the completion status class that the mirror-era code treated
  as an error and dropped unless the attended luofu_pie.accepterr gate was set. W22b removes the
  gate and delivers the class to the net_device registered for the decoded port (the same
  vendor-exception/CPU-delivery class that carried status 37 in E0-a and status 77 on the proof
  boot). It logs the first deliveries as RXEXC; no command-line token is needed.
- The CPU-copy delivery code (luofu_pie.cpucopy_deliver/cpucopy_port) stays, default off, for the
  mirror fallback; it is not removed in this release line.

## 5. Should W22 always run, and can the mirror patches be retired?

Decision: **W22 always runs in the release line; the CPU-copy mirror patches stay as a documented
fallback, not as the default path.**

- W22 runs on every armed boot (naptinit default 1) and is required to reach READY.
- Mirror patches 054-057 (OPP mirror programming, post-link re-latch, PIE port-0 CPU-copy
  delivery) stay in the release line unchanged. They are default-off and are only reached with
  the fallback command line below. Retiring the code now would churn the release line for no
  functional gain and remove the fallback that made the bring-up possible; a later cleanup patch
  can drop them after a soak of W22-only images.
- The canonical command line loses the mirror/accepterr/cpucopy tokens and gains nothing new
  (the new stage is always on; naptinit=1 may be explicit for documentation). Fallback variant:
  add luofu_switch.naptinit=0 plus luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=4
  luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11 (accepterr is gone).
- Release impact list: see RELEASE-INTEGRATION.md (release-line manifest/counts, target config
  command line, README/RELEASE-NOTES/docs truth updates, validator, image rebuild and bench test,
  curated sync).

## 6. Remaining unknowns for W22

- Whether the NAPT config must be re-applied after link events/reboots (the proof image survived
  its boot with link up well after the replay, but M1b should re-check after cable re-plug and a
  reboot; the mirror's post-link behaviour is the cautionary precedent).
- The exact status-class envelope: status 77 is observed; whether any other status value can be
  produced by the native punt path (e.g. on other ports/classes) should be captured in M1b via
  RXDIAG or the debug counters. If a second class appears, W22b's default delivery already
  handles it (all non-zero status with a valid decoded port is delivered); the log will name it.
- The 0x160/0x170 MCC words and the multicast class are out of scope for W22 (they belong to
  hi_pfe_mcc_init/M1b multicast handling).
