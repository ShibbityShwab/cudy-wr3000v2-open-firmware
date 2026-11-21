# M1a probe session runbook (host-prepared, attended, zero-unattended-writes)

Status 2026-11-21. This directory holds the reviewed M1a write proposal after E0-b.
Decode and justification: [../E0B-ANALYSIS.md](../E0B-ANALYSIS.md). Nothing here is executed
automatically; every write needs a human at the bench, the recovery door armed, UART capture
running and the two environment flags below.

What is proposed (details in the decode section of ../E0B-ANALYSIS.md):

- probe A: replay stock tag action entry 1 - 4 words at 0x124d4010..0x124d401c
  (stock content 0x00000000, 0x0000c000, 0x00000000, 0x00000000);
- probe B: replay stock pfe_glb+0x2c = 0x10000034 (up_port high16 0x1000) - 1 word.

Expected evidence: probe A is the only stock-only classification/tag table entry; it may be
referenced by vendor L3-interface/multicast entries that our image lacks, so a **null result is
the most likely outcome** and is a useful negative. Probe B is a flow-engine uplink attribute;
it is not expected to enable CPU delivery and is included only as a decoded-delta control.
The positive signal for M1a is an **extra native 98-byte RX delivery on eth2 while the mirror
is still on**, visible as an eth2 rx_packets delta of about 2x the sent ICMP count. A positive
signal is then proven by the mirror-off boot (cpu_mirror=0 image variant, the E0-a build recipe).

## 0. Preconditions (do not proceed if any fails)

- Human at the bench; recovery door armed; UART capture running:
  tools/recovery-card.sh (arm) and tools/uart-capture.sh start --label m1a.
- Bench link up; box on the adopted baseline image (E0-b ours: image 6e9c54d0 family, 6.18.60,
  2 CPUs, canonical command line with cpu_mirror=1 mirror_dst=4 accepterr=1 cpucopy_deliver=1
  cpucopy_port=11). Check the running image and command line:
  ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'uname -a; cat /proc/cmdline'
- SSH to the box works and the host can ping it with the mirror on (verified baseline).
- Capture directory on the host: build/evidence-bench-gates/e0b-m1a/ (gitignored).

## 1. Copy the artifacts to the box

From the repository root:

    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'mkdir -p /tmp/m1a'
    tools/rscp.sh m1a/observe-m1a.sh m1a/apply-probe-a-tagact.sh m1a/apply-probe-b-glbattr.sh root@192.168.1.1:/tmp/m1a/
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'cd /tmp/m1a && ls -la && sha256sum * && sh observe-m1a.sh | head'

Record the scripts' sha256 in the evidence directory.

## 2. Baseline measurement (read-only; no writes)

1. Snapshot: ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/observe-m1a.sh' > build/evidence-bench-gates/e0b-m1a/baseline-before.txt
2. Send a controlled ping (500 frames, 20 ms interval): ping -i 0.02 -c 500 192.168.1.1 > build/evidence-bench-gates/e0b-m1a/baseline-ping.txt
3. Snapshot again to baseline-after.txt.
4. Record before/after/delta for eth2_rx_packets, eth2_rx_dropped and l2_rx_total. The baseline
   delta of eth2 rx_packets is the mirror-copy count for 500 requests (plus the few ARP frames).

## 3. Probe A (tagact entry 1)

1. Review the plan (no writes): ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/apply-probe-a-tagact.sh'
2. Pre-arm the automatic revert (fire-and-forget; it reverts after 90 s even if SSH dies):
   ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "nohup sh -c 'sleep 90; cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --revert' >/tmp/m1a/auto-revert-a.log 2>&1 &"
3. Apply: ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --apply"
4. Immediately re-read the 4 words and the mirror registers with observe-m1a.sh; the script
   itself does the readback verification, record its output.
5. Send the controlled ping again and take the after snapshot.
6. Revert explicitly (do not wait for the timer if everything is fine):
   ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --revert"
7. Compare deltas vs baseline. Interpretation: a delta about double the baseline for
   eth2_rx_packets (same ping count) means a native duplicate delivery appeared - the M1a
   candidate signal; an unchanged delta means null.

## 4. Probe B (pfe_glb up_port attribute)

Repeat step 3 with apply-probe-b-glbattr.sh (one write). Expected: no change in the ping
counters. Record the same evidence set. Revert.

## 5. Decision

- If any probe shows a native duplicate delivery: build the cpu_mirror=0 image variant
  (canonical release-line tree, command line only; same recipe as the E0-a image 990c5cab),
  boot it attended and run the ping/ICMP-unicast test. That is the M1a proof attempt.
- If both probes are null: stop the write experiments. The next step is read-only: capture the
  stock procfs decoded tables (tagact, l3intf, ifc, glb_attr, tocpu_cnt, drop_cnt, br_dump,
  flow/mac tables) and decode the IFC/L3 write paths host-side. Do not write L2, NAPT, EQM or
  PIE words.

## 6. Abort and rollback

- Abort immediately on: SSH loss, ping loss to the box while the mirror registers still read the
  canonical values, dmesg Oops/BUG/panic, or any unexpected eth2 counter jump.
- The auto-revert armed before the apply restores the words after 90 s. Manual revert is the
  command in step 3.6. If SSH is gone, wait for the timer, then check SSH; if still gone, power
  cycle (human) and reinstall the baseline image; the recovery door is armed throughout.
- Every rollback word was captured by the generated script before the first write; the files are
  /tmp/m1a/apply-probe-a-tagact.sh.rollback.TIMESTAMP on the box.

## Closeout 2026-11-21: both probes were NULL - the write experiments are closed

The session above was run on hardware:

- probe A (tagact entry 1): the write at 0x124d4014 was **silently ignored** - wrote 0xc000,
  read back 0x0; the script restored the other words.
- probe B (pfe_glb+0x2c): the write was accepted and read back, but eth2 rx delta stayed 531 =
  mirror copies only (baseline 531).
- Box healthy, all words reverted, selector 0x11.

Conclusion: the tagact table has no usable CPU write path through the mapped window, and up_port
is not a delivery enabler. The CPU-punt mechanism was resolved instead by the consistent-stock
decoded procfs capture: IFC defaults punt broadcast to the CPU (ifc_tocpu=104) and route unicast
to the NAPT/flow engine (ifc_napt=170 -> napt up_tocpu=129). Our image has the same IFC defaults
but NAPT uninitialized, so unicast dies in the engine.

The next hardware step is therefore **not** another IFC/tagact replay: it is the NAPT init replay
prepared in [napt-init/](napt-init/) (read the proposal first):

- read-only confirmation on the current image: napt-init/check-napt-path.sh before/after a ping
  (the IFC napt-class counter should rise while up_tocpu stays 0);
- then, if approved: napt-init/apply-napt-init.sh (12 register writes + 2 polled clear commands,
  fail-closed with rollback and revert), mirror kept on, pre-armed auto-revert;
- expected: eth2 rx_packets delta about 2x the ping count and napt up_tocpu incrementing, then the
  cpu_mirror=0 proof boot.

Do not re-run the m1a/ probe scripts - they are kept only as the record of the NULL result.

## 7. What remains untested after this session

- Whether the tag action entry is referenced at all by the engine on our image (no readable
  reference in the captured state; the vendor reference is expected in L3-interface or
  multicast user-link entries, neither present on our image).
- Whether the real unicast-to-CPU state is the vendor CFE flow table (DDR carveout) - it cannot
  be read safely on our image and is not replayed.
- The IFC/L3 action encoding needed to program our own catch-all CPU punt (host-side decode
  still to do).
