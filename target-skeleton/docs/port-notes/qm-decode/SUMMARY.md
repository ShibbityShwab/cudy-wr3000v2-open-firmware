> **Update 2026-11-21 (native forwarding, W22):** the CPU-copy mirror described below
> is superseded as the canonical path by the W22 driver NAPT/flow-engine init (patch
> 059, gate luofu_switch.naptinit) plus PIE native exception-class delivery (patch
> 060).  The mirror patches 054-057 and their tokens remain only in the documented
> fallback image.  This page is retained as the historical decode record.

# incoming/qm-decode - round 4s: WORKING DATA PATH + canonical bench config (patch 058)

**Lane:** qm-decode, shared task task-61, 2026-11-20 (round 24). Prior: 032...057.
This package delivers **patch 058**: the canonical attended bench configuration for the now-working
data path, the CPU-copy workaround documentation, cosmetic checkpatch cleanup, the verification
matrix and the curated-repo resync plan. No device access in this lane.

## 1. SUCCESS: full data path on the OpenWrt 6.18 image (image 39424b94, patches 001-057)

- Host ping -c50: 50/50 received, 0% loss (avg 13.3 ms); second run -c25: 25/25, 0% loss (0.68 ms).
- AF_PACKET sniff: bidirectional ICMP and ARP on the wire.
- SSH works: "Linux OpenWrt 6.18.60 #0 SMP armv7l", br-lan 192.168.1.1/24 UP, 112 MB RAM.
- Persistent overlay: /dev/ubi0_1 on /overlay ubifs rw (14 MB), overlayfs rw.
- Console: W19_CPU_MIRROR static CPU-copy applied dst=4 after each MAC link-up and every ~2 s
  (pre==post d8=0x800 dc=0x6400000 c0=0x40); RXCPUCOPY port=0 -> port 11 len=98 status=0 lines;
  heartbeat continuous, no Oops.
- /proc/cpuinfo: 1 CPU online - SMP / second-CPU bring-up is a separate open issue.

## 2. Why it works (final chain)

1. W3-W16 arm the PIE/LSW/GEMAC path; W16 also fixed the MAC config words (0x1c=1, max-frame 1536).
2. The switch CPU-copy mirror (decoded hi_port_set_mirror: OPP+0xd8 masks, OPP+0xdc destination,
   OPP+0xc0 enable bit 6, OPP+0xb4 latch pulse) copies eth2/port-11 ingress to the CPU port.
   W19 dwell identified the CPU port as **mirror destination 4** (13 normal-path len=98 status=0
   port=0 completions inside the dst=4 window, none outside it).
3. The mirror registers do not survive switch port-up: W19 re-applies 200 ms after every MAC
   link-up and re-latches every 2 s (luofu_switch.cpu_mirror=1, mirror_dst=4).
4. CPU-copy completions carry port 0 (no net_device): W20 hands them to the mirror source port's
   netdev (luofu_pie.cpucopy_deliver=1, cpucopy_port=11), logged as RXCPUCOPY.
5. With those four pieces the full wire->PIE RX->CPU->kernel->PIE TX->wire loop works.

The mirror is a **bench workaround**; the clean long-term fix is native CFE L2/VLAN/rootif
forwarding (FDB + port VLAN membership + to-CPU action). That state is documented as D3/bench-only,
not an upstream design.

## 3. Canonical attended command line (also in target-skeleton/README.md)

    luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1 luofu_switch.eps=1
    luofu_switch.qminit=1 luofu_switch.pfe=1 luofu_switch.ifc=1 luofu_switch.ext=1
    luofu_pie.arm=1 luofu_pie.rxcfg=1 luofu_pie.accepterr=1
    luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11
    luofu_gmac.macregs=1
    luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=4
    (optional diagnostics: luofu_pie.rxdiag=1 luofu_pie.rxstroke=1)

## 4. Verification matrix (patch 058 proposes it; the first line is already proven)

1. ping -c50 192.168.1.1: 0% loss (proven); repeat -c25: 0% loss (proven).
2. SSH login, reboot, SSH again.
3. Overlay persistence: write a file under /overlay, reboot, file survives.
4. /proc/cpuinfo remains 1 CPU online; SMP bring-up is separate and must not be conflated.
5. Console: mirror apply lines after every link-up and every ~2 s (pre==post), RXCPUCOPY lines for
   host traffic, continuous heartbeat, no Oops.

## 5. What patch 058 changes

- Canonical bench config documented in target-skeleton/README.md (command line + verification
  matrix + resync plan) and in LSW-SPEC/PIE-DATAPATH.
- CPU-copy workaround documented with the W19 dwell evidence and the native-CFE long-term note.
- Cosmetic checkpatch cleanup: multiple blank lines / blank-line-after-declarations in the W20/W19
  code paths, plus canonical-config notes in the module parameter descriptions.
- Series numbering note (updated 2026-11-21): the duplicate 042 was resolved - the SMP diagnostics
  are 062-065 - and the release line is 51 patches of the L0 stack (001-058 + 062-065); see
  `opensource/upstream/series-6.18/05-release-line.md`.

## 6. Curated-repo / upstream series resync plan

(1) Freeze the series at 001-058 after review; (2) regenerate EVIDENCE-SHA256SUMS and the incoming
shared docs patch; (3) copy series + docs to the curated repo staging branch and run the series-apply
check on a pristine 6.18 tree there; (4) add the working-image note (image hash, canonical command
line, verification matrix) but keep the mirror workaround marked bench-only; (5) publish only after
the confidentiality/vendor-binary checks pass (no vendor .ko/.o or blobs in tracked text).

## 7. Deliverables / validation

- Patch: target/linux/hisilicon/patches-6.18/058-net-luofu-canonical-bench-config.patch
  (sha256 44b52458628b54f9ef86fd0ff1d3b5bd70c936e3d48801d6f47d21d099ac0e35).
- Series apply: 59/59 files, 0 failed hunks, 0 .rej/.orig; both drivers compile clean under WERROR;
  checkpatch 0 errors, 0 checks (only the expected plain-diff "Missing commit description" warning).
- Docs: target-skeleton/docs/incoming/qm-decode/shared.patch (sha256
  64ab0ec07fed5ae782d8befe226c339d434c22366f8ac0d02073983e2c453068) applies cleanly to the current
  opensource tree.
- This SUMMARY is also copied to target-skeleton/docs/port-notes/qm-decode/SUMMARY.md.
