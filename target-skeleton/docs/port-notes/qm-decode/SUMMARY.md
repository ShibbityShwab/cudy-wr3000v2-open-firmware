# incoming/qm-decode - round 4q: post-link CPU-copy re-apply + dwell sweep (patch 056)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 22).  Prior: `032`...`055`.
This package delivers **patch 056/W19**: re-apply the CPU-copy mirror after every MAC link-up,
re-latch it every 2 s, and add a dwelling destination sweep for one-shot identification.
No device access.

## 1. Static W18 result: the write timing is the bug

Image 851b3721 (static cpu_mirror dst=8, programmed at 3.245 s) gave 100% loss (50/50), while the
earlier W17 sweep (writes at 5.2/7.2/9.3/11.3 s, around eth2 link-up at 7.7 s / 35.0 s) produced a
burst of 4 ICMP replies.  RXSTROKE showed only the single accepted ARP exception completion
(ch0 hw_rptr=1 = sw_rptr=1), mask/status 0, fsm=0x2001, ch1 idle.  Conclusion: the OPP mirror /
CPU-copy registers are reset or ignored when the switch port comes up, so a boot-time write is lost;
writes that land around link-up survive.

## 2. Patch 056 (W19)

- **Post-link re-apply**: the GEMAC link-up path now calls
  `luofu_switch_cpu_mirror_link_up()@BT@ (new export in luofu-switch.h), which re-snapshots the OPP
  registers and schedules the mirror write 200 ms after carrier-up (logged as
  `W19_CPU_MIRROR: MAC link-up seen -> scheduling CPU-copy reapply in 200 ms@BT@).
- **Periodic re-latch**: in static mode the configuration is re-applied every 2 s (logged with pre
  and post register values, so the reset hypothesis is visible in the console).
- **Dwell sweep**: `luofu_switch.mirror_dwell=1@BT@ (with `mirror_dwell_ms@BT@, default 20000) programs
  each encodable destination 0..15 for the dwell time in turn, driven by a workqueue that survives
  link-up; one attended boot with the host pinging identifies the destination that sustains traffic.
  Destinations >15 are rejected with an explicit log (the decoded field is 4 bits, so 40..43 are not
  encodable mirror destinations).
- Snapshot/readback/restore discipline kept; work cancelled and snapshot restored on remove.

## 3. Expected console lines

```
W19_CPU_MIRROR: mirror machinery started (static mode, dst=8, re-latch every 2 s, post-link reapply enabled)
W19_CPU_MIRROR: MAC link-up seen -> scheduling CPU-copy reapply in 200 ms (dst=8)
W19_CPU_MIRROR: static CPU-copy applied dst=8 (pre d8=0x0 dc=0x0 c0=0x0; post 0xd8=0x800 0xdc=0x6800000 0xc0=0x40)
W19_CPU_MIRROR: static CPU-copy applied dst=8 (pre d8=0x800 dc=0x6800000 c0=0x40; post ...)   [every 2 s]
...
W19_CPU_MIRROR: dwell step 7/16 dst=6 pre d8=... dc=... c0=... ret=0; now hold 20000 ms for the host ping
```
If the pre values change after link-up, the reset hypothesis is confirmed on the console.

## 4. Run matrix

1. **Static post-link test**: full W3-W16 + `luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=8
   luofu_pie.rxdiag=1 luofu_pie.accepterr=1 luofu_pie.rxstroke=1@BT@; ping for 60 s.  Expect re-apply lines
   right after both link-ups and, if dst 8 is the CPU port, sustained replies with RXDIAG normal-path
   completions; RXSTROKE then shows whether hw_rptr advances during ping.
2. **Dwell identification**: same image with `luofu_switch.mirror_dwell=1@BT@ (optionally
   `luofu_switch.mirror=1@BT@); start the ping and note which step produces replies (each step logged
   with its destination and 20 s dwell).  The dwell loop is driven by a workqueue and re-applies after
   link-up, so it no longer races the port-up reset.
3. Once the destination is known, keep the static `cpu_mirror=1@BT@ configuration for sustained runs.

## 5. Deliverables / validation

- Patch `056-net-lsw-mirror-postlink-w19.patch` (`547ea498...@BT@): all patches apply (57/57, 0 rejects);
  luofu-switch/luofu-gmac compile clean under WERROR; checkpatch 0 errors (2 cosmetic blank-line
  checks).
- This SUMMARY is the lane record; docs stack update is batched for the next handover.
