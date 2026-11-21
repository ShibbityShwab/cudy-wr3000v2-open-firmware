# CFE runtime dump - status 2026-11-22: stock captured; forwarded (through-box) phase needed

> **UPDATE 2:** a routed/WAN stock phase was captured (box-terminated traffic) and still produced
> zero flow-entry/table changes - see ROUTED-PHASE-ANALYSIS.md. The next read is a true
> forwarded-through-the-box phase (LAN host -> box -> WAN/LAN peer, netns recipe in that file
> section 3); that is the decisive one for the NAPT entry encoding and M2.

> **UPDATE 2026-11-22: the stock dump succeeded.** The vendor kernel blocks /dev/mem read() but
> mmap() works, so the lead brokered a static ARM physdump and captured the real stock state
> (build/evidence-bench-gates/cfe-stock/, hashes in STOCK.SHA256SUMS). Findings:
> STOCK-DUMP-ANALYSIS.md. Still needed: one stock phase with real forwarding/offload so the vendor
> populates l3intf/userll/NAPT flow entries - see STOCK-DUMP-ANALYSIS.md section 7 (routed/
> LAN-to-LAN with a second host, bridge learning, multicast, or box-initiated traffic; same
> read-only mmap/physdump path). The our-image phase requests below remain optional controls.
# CFE runtime dump - status 2026-11-21: stock path BLOCKED, our-side reads used

## 0a. Stock kernel has no usable /dev/mem (real finding - do not retry)

The consistent stock vendor boot (5.10.201) cannot be dumped: every /dev/mem read fails with
"Bad address" and returns no bytes - the carveout (0x80000000), the MMIO windows and the control
page all fail. The vendor kernel is built without a usable devmem path, the boot environment has no
edit-able bootargs (only bootcmd is set, so there is no iomem=relaxed to add), and there is no
vendor debug kernel. The stock runtime CFE tables are therefore **inaccessible**; the static module
decode (FLOW-MODEL.md) remains the source of truth for the vendor geometry, and the our-side dump
below is what executes/validates the readable parts. Do not repeat the stock /dev/mem attempts.

## 0b. What we dumped instead (our image, W22, devmem+mmap works)

build/evidence-bench-gates/cfe-ours/ (gitignored): ddr-carveout.bin 5 MiB from 0x80000000
(sha256 8577a305a38899c2575e1c13cd3e18e219a2f94b5d8869f03ea1c06f43b42a23), windows/ 15 LSW MMIO
windows of 0x80000 + PIE partial 16,375 B, windows.SHA256SUMS, uart-classes.log/excerpt, and the
empty control page. Decode result: OUR-SIDE-VALIDATION.md (flow geometry solved: 8192-bucket v4/v6
arrays + 8192-slot arenas, exact 1 MiB partition). The decoder is analyze-cfe-dump.py with
--windows-dir/--carveout.

## 0c. Remaining read requests (our image, /tmp/physdump, read-only)

These close the remaining [U]/[I] items; all are reads of our image, none touch the release series.

1. **Clean baseline, W22 boot, no traffic**: wait 30 s after boot with no ping/ARP, then dump
   carveout (5 MiB) + key windows (pub/mcc/ifc/napt/l2/eps). Separates static tables from the
   traffic/queue churn in the current dump.
2. **Pre-init control, same image with luofu_switch.naptinit=0** (fallback command line), no
   traffic: carveout + key windows. The flow window must show no 0x017be000 marker and no
   32K/256K/32K/704K partition - this proves the partition is written by the W22 clear commands.
3. **Second-boot W22 dump** (no traffic): confirms the marker value/extents are deterministic
   across boots.
4. **Traffic phases**: after p2 (200 ICMP pings, stop, wait 2 s) and after p3 (TCP e.g. telnet to
   the box), dump carveout + key windows immediately, then a second dump 25-30 s later. Purpose:
   any v4/v6 entry allocation/aging, queue-cell evolution, CPU-queue counters.
5. **Alias check**: dump CPU phys 0x00000000..0x1000 and 0x80000000..0x80001000 and compare; the
   code-like stale bytes in the queue window make an alias of low DDR worth ruling out (our
   /dev/mem read is at the CPU physical address; if 0x80000000 were an alias of 0x0 the whole
   "carveout" interpretation would change).
6. **Optional stock-side cheap check**: after TCP/HTTP traffic on stock, cat
   /proc/hsan/lsw/table/ipv4_flow (and ipv6_flow). If the ADP software shadow ever allocates, the
   vendor decoder would print real flow entries; likely still empty, but it costs one cat.

Exact dumper examples (lead's /tmp/physdump mmaps /dev/mem):
    /tmp/physdump 0x80000000 0x500000 > carveout-clean.bin
    /tmp/physdump 0x12480000 0x80000  > mmio-pub.bin       (same for 0x12380000 mcc,
                                                           0x12100000 ifc, 0x12280000 napt,
                                                           0x12180000 l2, 0x11280000 eps)
    /tmp/physdump 0x00000000 0x1000   > alias-low.bin
Then place files in a new phase dir and run analyze-cfe-dump.py (flat layout) as documented in
OUR-SIDE-VALIDATION.md.

---

# Original stock dump request (kept for the record; NOT executable on the vendor kernel)

The original design asked for, on a consistent stock 5.10 boot, read-only dd dumps of:
- the 5 MiB DDR carveout at 0x80000000 (queue 4 MiB + flow 1 MiB: dd bs=4096 skip=524288
  count=1280) plus the 0x80600000 control page;
- all 16 LSW MMIO windows (0x80000 each: dp/ips/fam/pe/eqm/eps/prbs/pfe_glb/pa/ifc/l2/napt/mcc/
  opp/pub and PIE 0x10a70000), covering the readable table banks tagact pub+0x54000, l3intf
  pub+0x5b000/0x5c000, nhp pub+0x59000, car pub+0x55000, car_profile pub+0x53000, mcc userll
  mcc+0x44000, ifc tab128 ifc+0x50000/0x60000, flow counters napt+0x40000, l3/nhp counters
  eps+0x50000/0x4d000;
- procfs text state from /proc/hsan (pfe/dp counters, all lsw/table/* dumps, cfe table/intf/lrn,
  pie/eth counters, ip -s link, brctl FDB);
- traffic phases p0 idle, p1 ARP, p2 200 ICMP + a second dump 25-30 s later, p3 TCP, p4 broadcast,
  p5 box-to-host, optional p6 LAN-to-LAN;
- exfil via tar.gz + sha256 + nc/base64, with collect-stock-dump.sh.
This is retained as the design record. It cannot run: the vendor kernel's /dev/mem returns "Bad
address" for all these ranges (section 0a), so the stock runtime tables stay unavailable until a
vendor debug kernel or another privileged read path exists.
