# Stock CFE dump analysis (vendor 5.10, mmap physdump workaround) - 2026-11-22

The "stock inaccessible" conclusion is **overturned**: the vendor kernel blocks /dev/mem read()
("Bad address"), but mmap() of /dev/mem works, so a static ARM physdump binary was brokered onto
the stock boot (wget -> /tmp/physdump -> ./physdump addr len | nc host) and the runtime tables came
out. This is the real vendor state, finally available.

## 1. Inputs (build/evidence-bench-gates/cfe-stock/, gitignored, hashes in STOCK.SHA256SUMS)

| file | range | sha256 |
| --- | --- | --- |
| carveout-p0.bin | 0x80000000..0x80500000 (5 MiB, idle-ish) | 35310a0706a88fef3b4761a97dc11e24c30d84ee18a514b5aa015d1aeee9a4ab |
| p2-carveout.bin | same, immediately after 200 host->box ICMP pings | 103d7a798fdadd10193eea93dfd30c16e039ed86913096c4c2f97b2987c4e8d5 |
| win10-ifc.bin / p2-win10-ifc.bin | 0x12100000, 512 KiB | 66668fd7... / 33cea760... |
| win12-napt.bin / p2-win12-napt.bin | 0x12280000, 512 KiB | 57b224f1... / 85cee465... |
| win08-pfeglb, win11-l2, win13-mcc, win14-opp, win15-pub | 0x12000000/0x12180000/0x12380000/0x12400000/0x12480000, 512 KiB | in STOCK.SHA256SUMS |
| win16-pie.bin | 0x10a70000, 64 KiB (full window) | 5221d4cd... |
| ctrl-80600000.bin | 0x80600000, 16 KiB | 9dcc3f3f... |

p0 = captured with the box idle-ish after boot; p2 = same boot, captured immediately after the host
sent 200 ICMP pings to the box. This is the clean-room capture the previous phase could not obtain.

## 2. The vendor CFE tables - what is actually programmed on stock

Decoded from the MMIO windows (offets from the static model in FLOW-MODEL.md; commands at the end):

- **tagact (pub+0x54000): exactly one entry, id 1**: 16-byte hardware row
  00000000 00c00000 00000000 00000000 = w0=0, w1=0x0000c000 (valid bit 0x80; selector 4 =
  del_type 2, i.e. the "remove tag" action predicted from the tag_write decode). This is the only
  CFE value table row on the whole stock boot in this configuration, and the earlier partial read
  of entry word1=0x0000c000 is confirmed.
- **l3intf (pub+0x5b000 key / 0x5c000 data), nhp (pub+0x59000), car (pub+0x55000),
  car_profile (pub+0x53000), mcc userll (mcc+0x44000): ALL ZERO.** The vendor does not allocate
  these tables in this stock configuration (the stock /proc shadow agreed: only tagact alloc=1).
  So tagact entry 1 currently has **no userll/l3intf reference visible** in the readable tables; it
  is a boot-time default that is referenced only when the CFE installs a matching interface/flow
  (not done for plain LAN traffic). Do not assume the l3intf->tagact path from the static decode is
  exercised by stock here.
- **ifc (ifc+0x50000 tab128 + engine): identical to our W22 image, bit for bit** - the seven W10
  default entries (idx2=0x00100800, idx3..7=0x00001d01, idx8=0x00100800) and the last write-only
  engine descriptor at ifc+0x138 = {0,0,0,0xffff0000,0x000f0008}. The stock defaults are exactly
  what our W10 stage programs.
- **NAPT control (napt+0x50..0x70/0x130..0x13c): identical to our W22 image**:
  0x50=0x00f00b69, 0x54=0x00000100, 0x60=0x00080000, 0x64=1, 0x68=0x00020303, 0x70=0x00110138,
  bases 0x80400000/0x80448000/0x80408000/0x80450000. **The flow window (all 1 MiB) is byte-identical
  between stock p0, stock p2 and our W22 image**: bucket arrays at 0x80400000/0x80448000 are 8192
  words of 0x017be000, the v4/v6 entry arenas are the deterministic 0x66cc66cc-family empty-state
  image, and the four region extents are exactly 32K/256K/32K/704K. This is the strongest possible
  validation of W22: our NAPT init reproduces the vendor state byte for byte, including the
  empty-table encoding. No flow entry was allocated by the 200 ICMP pings (flow window p0 vs p2
  diff = 0 bytes), consistent with the counters showing the ICMP class taking the up_tocpu punt.
- **L2 config bank (l2+0x100) and pfe_glb (glb+0x2c=0x10000034, attr0 up_port) identical to ours.**
- **ctrl page 0x80600000 is readable on stock** (unlike our image): it holds the DDR poison
  0x66cc66cc plus a few bootloader-like snippets (e.g. 0x600d1a88 at +0x10); it is not switch
  state and it does not look like all-zero RAM.

## 3. Counters: p0 -> p2 (200 host->box ICMP pings)

ifc (ifc+0x1000): rx_total/ifc_hit 8231 -> 8340 (+109), ifc_tocpu 29 -> 35 (+6),
ifc_napt 3095 -> 3254 (+159).
napt (napt+0x1000): tx_total 3672 -> 3813 (+141), up_tocpu 3426 -> 3577 (+151),
rx_napt 3641 -> 3781 (+140), rx_total 3672 -> 3813, option_tocpu 655 -> 139 (-516!),
option_drop 42 -> 42, flag_tocpu 1 -> 1, sip_chk_tocpu 31 -> 32, rx_mc 246 -> 236 (-10),
mc_fail_tocpu 215 -> 204 (-11).
The up_tocpu/rx_napt/ifc_napt increments (~140-159 for 200 pings, some replies/requests already
counted before p0) confirm the native punt class under real stock traffic and match our W22 numbers
(ours: up_tocpu 1285, rx_napt 1366, ifc_napt 1268 in the our-side matrix). **Anomaly:** the
option_tocpu and rx_mc/mc_fail counters DECREASE between p0 and p2, i.e. at least some of the
"counter" words are not monotonic accumulators - the vendor CFE may rotate/clear or the registers
are banked gauges. Treat the counter block as volatile snapshot state, not as free-running totals,
for M2 work. The flow-counter table (napt+0x40000) stays empty (no flow entries).

## 4. Queue window: live cell/ring arena, not configuration

The 4 MiB queue window changes massively between p0 and p2: 3,621,287 bytes differ (of 4 MiB),
1022/1024 pages have >1 KiB changed, no page is stable, and only ~7% of words are equal across the
two snapshots (~54% in the first 64 KiB). Its first words look like a queue/cell header followed by
payload bytes, and the host IP/MAC appear inside packet-like data (host IP 192.168.10.44 LE at
0xcb4, host MAC at 0x19ec64 in p0). Conclusion: the queue DDR window holds the live LSW cell/ring
arena (packet buffers plus queue descriptors), not a static table image; it cannot be decoded from
a single snapshot, and the earlier "our-image queue contained stock boot residue" reading is
**superseded**: our image and stock queue windows differ almost entirely (0/1024 identical pages),
because the queue window is rewritten by traffic on every boot. Config-like layout still lives in
the MMIO queue/EQM tables (opp/eqm windows), not here.

## 5. PIE window (win16, 64 KiB)

Nonzero pages 0x0/0x1000/0x2000/0x3000/0x5000 contain ring/descriptor-like words (addresses such
as 0x8260xxxx, lengths, status words) and some counters. The status classes are already known from
the UART matrix (0 normal/mirror, 0x25=37 IFC exception/ARP, 0x4d=77 NAPT native punt, ICMP len 98
port 11); the PIE window adds the registers behind them but was not needed to decode the class
enumeration. Full decode remains [U].

## 6. What this changes in the model / M2

- W22 init equality with stock is now *byte-exact* over the whole flow window, not just the
  register readback: release confidence strengthened.
- The stock plain-LAN configuration does **not** populate l3intf/nhp/car/car_profile/userll and
  creates no NAPT flows, even after 200 ICMP. The vendor CFE in this configuration is using the
  same CPU-punt software path our D1 uses; hardware L2/VLAN/flow offload is simply not exercised.
- Therefore M2-A/M2-B feasibility cannot be judged from this capture: to see vendor writes we need
  a stock phase with **routed or LAN-to-LAN forwarding** (two hosts / WAN-side peer), which may
  populate l3intf/tagact/userll and NAPT flow entries. That phase is the next request.
- tagact alone (one delete-tag row) is not a forwarding table; the static tag/l3 decode remains
  unvalidated for actual references until such a phase exists.

## 7. Remaining reads requested (stock, read-only, via the same physdump/mmap path)

1. **Routed/LAN-to-LAN phase** (decisive for M2): with a second host/NIC or a WAN-side peer,
   generate traffic that is forwarded by the box (host A <-> host B through the stock bridge, or
   LAN <-> WAN routed/NATed traffic), then dump carveout + win10-ifc/win11-l2/win12-napt/
   win13-mcc/win15-pub/win14-opp; compare with p0. Look for the first non-empty l3intf/userll and
   any NAPT flow entry in the arenas (v4 bucket/arena bytes will finally show the entry encoding).
2. **Bridge-learning phase** (if two LAN hosts are possible): ping A<->B, dump pub+0x5b000/
   0x5c000 and mcc+0x44000; check whether l3intf/userll/userll_ext populate.
3. **Multicast phase**: send mDNS/IGMP; dump mcc/pub; check userll_ext/MC tables.
4. If a second host is impossible: at minimum a stock phase with **box-initiated** traffic
   (stock shell ping/wget to the bench host) and a **broadcast/ARP** burst, then dump the same
   windows; it may populate some tables even without routing.
5. Cheap: capture /proc/hsan/lsw/table/{l3intf,tagact,userll,userll_ext,ipv4_flow,ipv6_flow}
   text state at the same phase (the CFE shadow print may decode allocated rows if any appear).

Until such a phase exists, the completed model is: boot CFE state = IFC defaults + one tagact
delete-tag row + NAPT config/empty tables + queues/counters; everything else static [I].

## 8. Regeneration

  python3 cfe/decode-stock-dump.py --dir build/evidence-bench-gates/cfe-stock
writes cfe-stock-analysis.md next to the dumps (the ad-hoc probes used for this document are
build/linux-kbuild/waves/2026-11-21-lsw-fwd/stock_probe*.py, gitignored).
