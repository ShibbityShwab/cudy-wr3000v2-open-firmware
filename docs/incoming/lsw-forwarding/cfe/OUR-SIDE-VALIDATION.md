# Our-side dump validation (cfe-ours, W22 image 6e9c54d0 family)

Status 2026-11-21. The stock runtime dump is **not possible**: the vendor 5.10 kernel has no usable
/dev/mem (dd on the carveout, MMIO windows and the control page all fail with "Bad address", no
bytes returned; there is no addable bootargs env, only bootcmd, so no iomem=relaxed either; there is
no vendor debug kernel). This is a real finding about the stock kernel, documented in
DUMP-REQUEST.md section 0. The vendor runtime CFE tables therefore remain inaccessible; the static
module decode stays the source of truth for vendor geometry.

What the lead provided instead is a full read-only dump from **our image** (W22/M1a proof family,
devmem/mmap works there). Inputs in build/evidence-bench-gates/cfe-ours/ (gitignored, hashes
below): ddr-carveout.bin (0x80000000, 5,242,880 bytes), windows/ (15 LSW windows of 0x80000 +
PIE window partial 16,375 bytes + empty control page), uart-classes.log/excerpt, windows.SHA256SUMS.

  sha256 ddr-carveout.bin = 8577a305a38899c2575e1c13cd3e18e219a2f94b5d8869f03ea1c06f43b42a23
  sha256(win15-0x12480000) = e4ffeae1fd8b910624361779939f0796fdde608874773905735d0e36b58e0f8a
  sha256(win16-0x10a70000) = c0e4a4f7bebc6d8587f1bcd985e42a94f97ae790f55510bd2a52eef94a16c38c
  (windows.SHA256SUMS covers all 17 entries; win17 control page is 0 bytes.)

Decoder: analyze-cfe-dump.py with the flat our-side layout:

  python3 analyze-cfe-dump.py --dir build/evidence-bench-gates/cfe-ours \
      --windows-dir build/evidence-bench-gates/cfe-ours/windows \
      --carveout build/evidence-bench-gates/cfe-ours/ddr-carveout.bin \
      --host-mac 00:e0:4c:63:17:58 --host-ip 192.168.1.88 --box-ip 192.168.1.1
  -> build/evidence-bench-gates/cfe-ours/cfe-analysis.md (7.7 KB) + cfe-analysis.json

## 1. What the dump validated

### 1.1 IFC defaults (W10) - PASS, bit exact
The readable ifc tab128 bank at ifc+0x50000 holds the W10 default entries:
  idx2 = 0x00100800, idx3..7 = 0x00001d01, idx8 = 0x00100800 (word0 of each 16B entry; all other
  words zero), exactly the payload table decoded for patch 043.
The write-only 5-word action engine retains its last descriptor at ifc+0x138..0x148:
  {0x00000000, 0x00000000, 0x00000000, 0xffff0000, 0x000f0008} = W10 entry 8 payload exactly.
So the ifc table address/stride decode and the W10 default program are confirmed on hardware, and
the engine data registers are readable (only the last-written descriptor; per-entry action cannot
be read back).

### 1.2 NAPT W22 state - PASS, bit exact
napt control reads back exactly the stock/decoded values:
  0x50=0x00f00b69 0x54=0x00000100 0x60=0x00080000 0x64=0x00000001 0x68=0x00020303
  0x70=0x00110138 0x130=0x80400000 0x134=0x80448000 0x138=0x80408000 0x13c=0x80450000.
0x60=0x00080000 is the post-clear command/status residue (busy bit clear); the four flow bases are
the decoded W22 values.

### 1.3 L2 configuration - PASS
l2+0x100 bank matches the decoded W6 defaults (0x104=0x01030000, 0x108..0x13c=0x01400008,
0x140=0xffff0000, 0x158/0x15c=0x01400008, 0x164=0x00010001, 0x180=0x12c, 0x184=0x0f4240,
0x188=0x60); the L2 counter page l2+0x40000 reads 0x25c2 in the same repeated-bank pattern observed
on stock (stock 0x414), confirming the earlier "counter values, not port maps" conclusion.

### 1.4 CFE value tables are empty on our image - expected, and a limitation
tagact (pub+0x54000), l3intf (pub+0x5b000/0x5c000), nhp (pub+0x59000), car (pub+0x55000),
car_profile (pub+0x53000), mcc userll (mcc+0x44000), eps l3/nhp counters and the napt flow
counter bank are all zero. Our driver never programs the vendor CFE tables, so the dump proves the
**readable table addresses and the decoder path** but cannot validate the vendor field semantics:
the stock runtime values would be needed for that, and stock is inaccessible. This is exactly the
gap the static decode covers, with the grades recorded in FLOW-MODEL.md.

## 2. Flow-table DDR geometry - SOLVED (exact 1 MiB partition)

The W22 engine clear wrote a deterministic pattern into the v4 and v6 bucket arrays; the rest of
the flow window retains the DDR poison pattern 0x66cc66cc. Measured on the dump:

| region | physical range | size | content |
| --- | --- | --- | --- |
| v4 bucket array | 0x80400000..0x80408000 | 0x8000 = 32 KiB | 8192 words, all 0x017be000 (empty-bucket marker) |
| v4 entry arena | 0x80408000..0x80448000 | 0x40000 = 256 KiB | poison baseline (0x66cc66cc) |
| v6 bucket array | 0x80448000..0x80450000 | 0x8000 = 32 KiB | 8192 words, all 0x017be000 |
| v6 entry arena | 0x80450000..0x80500000 | 0xB0000 = 704 KiB | poison baseline |

The partition is exact: 32K + 256K + 32K + 704K = 1 MiB, and the four bases match the decoded NAPT
registers one-to-one (0x130=v4 buckets, 0x138=v4 entries, 0x134=v6 buckets, 0x13c=v6 entries).
Entry-stride arithmetic fits the vendor table sizes:
  v4: 8192 slots x 32 B = 256 KiB exactly; total 8224 = 8192 + 32 extra (reserved).
  v6: 8192 slots x 88 B = 704 KiB exactly; total 8208 = 8192 + 16 extra (reserved).
So both hash tables use **8192 buckets** (4 B heads) and fixed-size entry arenas, v4 entry stride
32 B, v6 entry stride 88 B. The 0x017be000 marker is the engine's empty-bucket/head value (exact
encoding still [U]; it is written by both clear commands and is stable in this dump).

Inside the entry arenas the dump shows 0x66cc66cc poison with ~10.7 % of words carrying a
single-byte deviation (B: 7100/65536; D: 19213/180224; deviations spread uniformly over byte lanes
and offsets, not at entry stride). They are not readable table entries: no MAC/IP appears anywhere
in the flow window, and no structured runs appear at 32 B/88 B stride. This is consistent with the
entry arenas being untouched/lazily initialized on our image (and the deviations being a
write-strobe/poison artifact); a pre-init control dump (below) will confirm. The **bucket/arena
geometry and the entry strides are [V]** from this dump; the entry byte layout is still [I]
(static decode from pfe_napt_v4_write_flow/dump_flow gives key/data logical fields but the arena
encoding was never exercised on hardware).

## 3. Queue DDR window - live/stale cell arena, not a clean config dump

The 4 MiB queue window (0x80000000..0x80400000) is fully nonzero and is clearly a live cell/queue
arena plus **stale data from the previous stock session**:
- the first ~0x20000 is ARM-code-like bytes (stale kernel/boot image);
- repeated 0x400/0x100-stride blocks contain the **stock-session host IP 192.168.10.44**
  (0xc0a80a2c little endian inside block payloads) and the byte-reversed bench host MAC
  (00:e0:4c:63:17:58 -> 58:17:63:4c:e0:00) at +0x54 of a 0x100-ish block in 1023 pages
  (2730 occurrences);
- our traffic/counters are also present (ICMP-class native punt happened on this image).
This is not a static CFE table dump; it is a mix of live queue cells and traffic payloads.
**Correction 2026-11-22 (after the stock capture):** the retained-vendor-data reading was wrong.
The stock dump shows the queue window is rewritten by traffic every boot (stock p0 vs p2: 3.62 MB
of 4 MiB changed, 1024/1024 pages changed, about 7% equal words); our image queue window differs
from stock p0 almost entirely (0/1024 identical pages). It is a live cell/ring arena, not boot
residue, and can only be decoded by differential snapshots plus the cell-header format. The earlier hexdump probe values (0x80000000=0, 0x80400000=0x017be000,
0x80408000=0x66cc66cc) are consistent with this map.

## 4. Counters under the class traffic matrix (traffic was live)

napt counter block (napt+0x1000, decoded names from the earlier counter lane):
  0x1000=1380 tx_total, 0x100c=81 dn_tocpu, 0x1010=1285 up_tocpu, 0x103c=14 sip_chk_tocpu,
  0x104c=123 option_tocpu, 0x1050=36 option_drop, 0x1054=1 flag_tocpu, 0x1060=1380 rx_total,
  0x1070=1366 rx_napt, 0x1074=95 rx_mc.
ifc counter block (ifc+0x1000): 0x1000=9051 rx_total, 0x1008=9051 ifc_hit, 0x1014=116 ifc_tocpu,
  0x1024=1268 ifc_napt.
These quantify the native W22 path: ~1268-1285 frames took the NAPT up_tocpu class (status 77),
~116 the IFC exception/punt class, in this traffic matrix. UART classes observed: status 0 (normal/
mirror), 37 (IFC exception/ARP), 77 (NAPT native punt; ICMP len 98, port 11). No new class seen.

## 5. What still needs a read (our image, /tmp/physdump) - requested in DUMP-REQUEST.md section 5

1. Clean-baseline carveout dump on a fresh W22 boot with **no traffic** (current dump has traffic
   and stale cells), to separate static table regions from queue/counter churn.
2. Pre-init control: same image booted with luofu_switch.naptinit=0 (fallback command line), no
   traffic, carveout+windows; it should show no 0x017be000 in flow window (confirms the marker and
   the 32K/256K/32K/704K partition is written by the W22 clear commands).
3. Second-boot W22 dump to confirm the marker/partition is deterministic (same value 0x017be000,
   same extents).
4. Traffic-phase carveout + key windows: after p2 ICMP (stop, dump immediately) and after p3 TCP,
   plus a 25-30 s second dump; to look for any flow-entry allocation/aging and queue-cell layout.
5. Windows key subset at those phases (ifc/napt/l2/eps/pub/mcc) for counter/class correlation.
6. Alias check: dump CPU phys 0x00000000..0x1000 and 0x80000000..0x1000 and compare; confirms the
   carveout is not an alias of low DDR (the code-like stale bytes raise this question).
7. Optional: on stock, after TCP/HTTP traffic, cat /proc/hsan/lsw/table/ipv4_flow and /ipv6_flow
   (if the ADP shadow ever allocates, the vendor decoder would print real entries; likely empty,
   but it is a cheap check).
