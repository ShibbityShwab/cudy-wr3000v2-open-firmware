# Live-devmem experiment plans (2026-11-20, qm-decode lane)

Box: DEVMEM image c6ede30c (patches 001-058 + CONFIG_DEVMEM=y), SSH up, stock pair on A,
physical recovery armed. All addresses are physical; use the static /usr/bin/devmem
on the box (stock side: telnet devmem). Every step below is gated: capture first,
write one thing, read back, observe, and keep the rollback word printed by the
capture step.

## TASK 1 - native CFE L2/VLAN/rootif forwarding (replace the CPU-copy mirror)

Known-good fallback: the mirror workaround (OPP d8=0x800 dc=0x6400000 c0=0x40) is
re-applied by the driver every 2 s, so the box cannot be left without a working
data path by these experiments.

### Phase A - read-only comparison, BOTH images, with the host pinging continuously

Run the same read loop on ours (SSH) and on stock (telnet devmem) while
"ping 192.168.1.1" runs from the host. Save each to a file, then diff.

1. IFC flow table (16-byte entries, index*16 from 0x50000):
   for i in 0 1 2 3 ... 31: devmem 0x12100000+0x50000+i*16 (4 words each)
   -> also 0x12100000+0x60000..0x60800 (second table bank).
2. L2 window (0x12180000): read 0x0-0x400, 0x8000-0x8400, 0x10000-0x10400,
   0x18000-0x18400, 0x20000-0x20400, 0x40000-0x40400, 0x60000-0x60400,
   0x70000-0x70400.
3. PA (0x12080000): 0x0-0x400 and 0x1000-0x1400.  NAPT (0x12280000): 0x0-0x400.
   MCC (0x12380000): 0x0-0x400.  PFE_GLB (0x12000000): 0x0-0x100.
4. PE (0x11180000), EQM (0x11200000), EPS (0x11280000), FAM (0x11100000):
   0x0-0x100 each, plus the already-known 0x1000/0x2000/0x6000 pages.
5. GEMAC windows 0x14380000/0x14400000/0x14480000/0x14500000: 0x0-0x2ff
   (MAC/VLAN state and the counter words; ours is already captured).

Deliverable of Phase A: the list of words/pages that are non-zero on stock with
traffic and zero (or different) on ours. Counters are known (W14/W15: the
0x1b6/0x1d1/0x1f1/0xcb/0xf9 class + dfx-only offsets) and are ignored. Any MAC
bytes (00:e0:4c:63:17:58 host, box MAC) or port/VLAN shaped words are the target.

### Phase B - candidate writes on ours (mirror stays on; rollback = restore captured words)

Each experiment: capture the full entry/page first (print rollback command), write
one entry, read back, ping + check RXDIAG, revert if no effect.

B1. Static L2 entry: locate in the stock L2 dump the 4-8 word entry containing the
    host or box MAC (MAC bytes appear as 0x00e04c63/0x17580000 style pairs). Write
    the same words at the same offset in our L2 window:
      devmem <l2base+offset> 32 <word>   (repeat for each word of the entry)
    Expected: host ping keeps replying, and additionally the console shows
    RXDIAG normal-path completions (status=0) whose port is the CPU port tag
    (likely 4) or 11 - i.e. frames now arrive through the native path, not only
    the mirror (port=0) copies. If the completion port is 4, the current
    cpucopy_deliver gate only maps port 0, so patch 059 is needed to map that
    port to eth2 before ping can use the native path.

B2. IFC flow entries: if the stock IFC table has entries beyond the 8 written by
    W10 (or different words in entries 9..31/0x60000 bank), write them one at a
    time with devmem to 0x12100000+0x50000+idx*16, then read back and ping.
    (The engine reads the table memory; no doorbell was seen for entry writes.)

B3. Port-VLAN membership: if Phase A shows port/VLAN words in L2/PA that are
    non-zero on stock with traffic, replay them the same way, lowest word first.

Rollback: restore the words captured immediately before each write; the mirror
fallback stays active (driver re-latches it). If any write makes ping worse,
restore and reboot.

Expected final evidence: dmesg/console RXDIAG lines "len=98 status=0 port=4/11"
for host frames while the mirror is present; then, with a small driver patch
(map the native CPU-port tag), ping replies via the native path and no port=0
copies needed.

## TASK 2 - release CPU1 from userspace (SMP)

Facts from the DEVMEM image: pen SYSCTRL+0xc00 = 0x81295000 (stale; kernel
secondary_startup physical = 0x8070b000), CRG 0x14880038 = 0x00200000, SCU
0x10180000/04/08 = 0x3/0x511/0x03030000, GICD 0x10181000 = 0x1, GICC CPU0
0x10180100 PMR = 0xF8, GIC CPU1 interface 0x10180200 to check.

### Step 0 - read-only capture (no risk)

  devmem 0x10100c00 32          # pen (rollback value)
  devmem 0x14880038 32          # CRG resume word (rollback value)
  devmem 0x10180000 32; devmem 0x10180004 32; devmem 0x10180008 32
  devmem 0x10181000 32; devmem 0x10181f00 32   # GICD ctlr / SGIR
  devmem 0x10180100 32; devmem 0x10180200 32   # GICC CPU0/CPU1
  cat /proc/cpuinfo | grep -c processor
  dmesg | grep -i 'luofu-smp\|CPU1\|smp' | tail -20

SCU status note: byte pairs at bits 17:16 (CPU0) and 25:24 (CPU1) with value 3
mean the core is powered / in SMP mode per the A9 SCU; our driver logs the same
word. If CPU1 status is NORMAL already and pen is stale, the core is parked with
an old pen and only a CRG reset/re-release makes it re-read the new pen.

### Step 1 - correct pen (safe, no immediate effect expected)

  devmem 0x10100c00 32 0x8070b000
  devmem 0x10100c00 32          # verify readback

### Step 2 - wake IPI (low risk; only works if the core re-reads the pen)

  devmem 0x10181f00 32 0x00020000   # target list CPU1, SGI 0 (raw GICD_SGIR)
Read-only checks: devmem 0x10180008 (CPU1 status bits 25:24), /proc/cpuinfo,
dmesg for "CPU1: thread" / our luofu-smp messages.

### Step 3 - CRG re-release (HANG RISK: touches reset/power; recovery must be armed)

Variant 3a (set the early-probe release mask) - capture the word first:
  OLD=$(devmem 0x14880038 32); devmem 0x14880038 32 $((OLD | 0x02800000))
Variant 3b (clear the production driver's four resume bits 0x16..0x19):
  devmem 0x14880038 32 $((OLD & ~0x03C00000))
Variant 3c: repeat Step 2 SGIR after 3a/3b.
After each write, read back, check SCU status and /proc/cpuinfo/dmesg. On any
hang, power cycle via the stock pair (physical recovery).

Rollback: devmem 0x10100c00 32 <pen rollback>; devmem 0x14880038 32 <resume
rollback>. The pen write alone is harmless; the CRG writes are the risk and must
be done only with the UART captured and recovery armed.

### Interpreting success

- Kernel-side success: /proc/cpuinfo lists 2 processors, dmesg shows CPU1 coming
  online (our driver prints per-CPU luofu-smp lines).
- If CPU1 consumes the pen but fails in the kernel stub, the UART shows our
  early-probe 'E'/marker output (that path is in patches 037/039 and needs the
  early-probe gate; the devmem image has them).
- If SCU CPU1 status stays NORMAL and nothing changes, the core is parked and
  needs the CRG reset path (Step 3), not another SGIR.

## Safety summary

- TASK 1: read-only first; every write one word/entry with captured rollback; the
  mirror fallback keeps the box reachable.
- TASK 2: Steps 1-2 safe; Step 3 CRG writes can hang the box - recovery armed,
  UART captured, one variant at a time.

## Addendum (2026-11-20): NAPT block decode - Phase A result explained

`hi_pfe_napt_init` in hi_lsw_pfe (0x4314) is the only writer of the eight differing NAPT words.
It runs once at module init (persistent config, not runtime status):

  NAPT+0x70 = (old & 0xFFCCFCC0) | 0x00110138     (RMW)
  NAPT+0x64 = 1                                  (enable)
  NAPT+0x54 = 0x100                              (hash geometry)
  NAPT+0x68 = list/flags word
  NAPT+0x50 = (table-bytes / 0xb71) | 0xF00000   (hash memory config)
  NAPT+0x130 = [sp+4]                            (memory-info base)
  NAPT+0x134 = [sp+4] + 0x48000                  (one table)
  NAPT+0x138 = [sp+4] + 0x8000                   (another table)
  NAPT+0x13c = [sp+4] + 0x50000                  (another table)

The base comes from hi_system_mem_info_get(2, ...): it is the vendor **reserved DDR
carveout** base (stock value 0x80400000, i.e. a platform-reserved region).  The
carveout holds the flow-offload tables; hi_pfe_napt_init also kmallocs software
bucket/aging arrays (vendor-only bookkeeping).  0x54/0x60/0x64/0x170/0x70 are the
flow engine geometry/enables; 0x130..0x13c point hardware at the DDR tables.

Implications:
- The NAPT block is the flow-offload engine, not the L2/bridge forwarding state.
  Everything else in the sampled windows matches stock, so the stock "native" path
  for host traffic is the CFE software punt/exception path (its runtime bridge/flow
  state lives in the vendor DDR carveout and in the CFE modules, which cannot load
  on 6.18).  Replaying NAPT alone will not produce native forwarding.
- On our image 0x80400000+ is certainly not the vendor carveout anymore: pointing
  the NAPT engine at it would DMA into whatever our 6.18 kernel has there.  Check
  System.map _end / /proc/iomem and do not write 0x130..0x13c unless that physical
  range is verified free.

Safe Phase B micro-experiment (no DDR, rollback = captured words):
  capture: devmem 0x12280054 / 0x60 / 0x64 / 0x68 / 0x70 / 0x170
  write:   0x54=0x100; 0x60=0x80000; 0x170=1; then 0x64=1; optionally
           0x70=(old & 0xFFCCFCC0) | 0x00110138
  expected: ping/RXDIAG unchanged (idle engine) - confirms it is harmless init
  config; it does not create a native CPU delivery path.

Patch 059 (CPU-port-tag mapping, port 4 -> eth2) is therefore not worth preparing
now: there is no native delivery path to map.  Keep it as a ready recipe for the
case where a future experiment surfaces normal-path completions with port 4.

