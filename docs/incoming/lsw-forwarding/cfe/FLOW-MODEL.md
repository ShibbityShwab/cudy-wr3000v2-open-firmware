# CFE / switch full-reversal: static model (before the carveout dump)

Status 2026-11-22. **The stock runtime tables are CAPTURED**: the vendor 5.10 kernel blocks
/dev/mem read() but mmap() works, so a static ARM physdump was brokered onto the stock boot and the
real vendor state was dumped (STOCK-DUMP-ANALYSIS.md). The static module decode below remains the
map/reference; the stock capture validates the boot state and shows which tables the vendor actually
populates in the plain-LAN configuration (only tagact + IFC defaults + NAPT config; no l3intf/nhp/
car/userll and no NAPT flow entries, even after ICMP traffic). The flow-table DDR partition is
solved and stock==W22 byte-identical (section 4).

This is the host-side static decode of the vendor CFE/switch tables from the pinned hi_* modules (hashes identical between /lib/hisilicon/ko, the recovery-extract tree and the
older fixtures), the stock procfs dumps (hsan-stock/hsan2) and the known register maps. The
physical carveout dump from a consistent stock boot (see DUMP-REQUEST.md) is the missing input
that turns the flow-table layout from hypothesis into bytes.

## 1. Module and call graph (what installs the FFU/CFE state)

- hi_lsw_pfe.ko: PFE table managers + hardware writers (ifc, l3intf, tagact, nhp, car, mcc,
  napt flow counters, l3/nhp/ifc counters). Static inner functions decoded with symbols.
- hi_lsw_dp.ko: DP queue/EQM/ EPS; queue tables in the queue DDR window through the EQM indirect
  engine (already decoded in W7; not CFE flow state).
- hi_lsw_pub.ko: generic table manager plus the MMIO table banks (tab128 writers at pub+0x53..5c).
- hi_kcfe_pub.ko: CFE framework (hooks, rootif/subif/FDB/tag API, proc entries under /proc/hsan).
- hi_kcara.ko: ADP adapter; registers cfe-adp callbacks (hi_cfe_adp_lsw/port/mc/ddos_register) and
  implements the acceleration/flow bridge between the CFE framework and the PFE table managers
  (hi_adp_flow_add/del/get_age, hi_cfe_acc_* surface in the CFE docs). This is the module that
  turns CFE userspace/learn requests into hi_tab_mgr_alloc_* + set_data calls.
- hi_kcfe_lrn_*.ko: learning libraries (L2/L3/L4/MC/VXLAN/PPP/PPTP/DSLite), feeding the same ADP
  API. Their learning outputs are flow entries, not raw L2 rows (confirmed earlier).
- hi_swa.ko: switchdev/rootif glue and DFX; no table writers relevant here.
- /proc/hsan writers: hi_basic.ko owns the generic proc/DFX framework; the table nodes print the
  software table-manager shadow only (which is why the stock capture shows empty ifc/l3intf/flows
  while the hardware counters show traffic).

## 2. Table managers (from the stock /proc/hsan/lsw/table headers)

The proc headers print the manager "type" enum, key/data sizes, slots and allocation counters.
The software shadow being empty while counters are non-zero proves the CFE path writes hardware
directly (ADP -> hardware writers / DDR engine), not through a shadowed table. The table below is
the complete stock set; hardware location is decoded from the writer functions.

| proc name | type | key | data | total | hardware location (decoded) |
| --- | --- | --- | --- | --- | --- |
| ifc | 1807 | 20B | 24B | 119 | ifc+0x50000 tab128 (16B key/mask words) + ifc+0x120 engine (5-word action descriptors, write-only); ifc+0x60000 counters |
| ifc_cnt | 1797 | 0 | 16B | 63 | ifc+0x60000 (16B entries) |
| l3intf | 1799 | 16B | 16B | 31 | pub+0x5b000 key bank, pub+0x5c000 data bank (16B per id) READABLE MMIO |
| l3_cnt | 1795 | 0 | 16B | 31 | eps+0x50000 (16B entries) |
| nhp | 1798 | 16B | 16B | 127 | pub+0x59000 (16B per id) READABLE |
| nhp_cnt | 1796 | 0 | 16B | 255 | eps+0x4d000 (16B entries) |
| tagact | 1800 | 12B | 12B | 63 | pub+0x54000 (16B per id) READABLE |
| car | 1809 | 0 | 4B | 63 | pub+0x55000 (16B per id) |
| car_profile | 6 | 16B | 16B | 31 | pub+0x53000 (16B per id) |
| ipv4_flow | 1804 | 16B | 28B | 8224 | flow DDR window (engine-managed; napt+0x130 base A) |
| ipv6_flow | 1805 | 40B | 52B | 8208 | flow DDR window (engine-managed; napt+0x130..13c regions) |
| flow_cnt | 1794 | 0 | 16B | 255 | napt+0x40000 tab128 (16B counters per flow id) READABLE |
| userll | 1810 | 4B | 4B | 511 | mcc+0x44000 (4B word per id) READABLE |
| userll_ext | 2048 | 12B | 12B | 63 | mcc window, location TBD (ext table; dump will show) |
| shaping/shaping_up/shaping_down | 1801/1802/1803 | 0 | 4/4/8B | 32/16/16 | DP/EQM blocks (not CFE flow state) |
| glb_attr | 1792 | 0 | 4B | 2 | pfe_glb+0x2c (attr 0 = up_port), napt+0x54 bit8 (flow_age_en) |
| cnt | 1793 | 0 | 4B | 255 | counter manager (ids 1/2 used by cfe mac_stats) |
| eqm_sht | 0 | 4B | 4B | 63 | EQM queue tables (W7 decode) |

Manager-id notes: the PFE/ADP code creates table managers with ids in the 0x700..0x712 range and
those ids are internal; the "type" numbers above are the proc/DFX type enum. Earlier working notes
that equated 0x708 with tagact etc. are consistent for the PFE-created managers, but hi_kcara also
creates managers (ADP) whose ids must be read from its create calls; the hardware locations above
are taken from the writer functions, not from the ids.

## 3. Readable hardware table banks (validated with devmem on our image)

The following banks are plain MMIO inside the LSW windows and are readable on both kernels once
the block is powered; the stock dump request reads them with dd.

- tagact: pub+0x54000, 16-byte entries. Decoded writer tag_write@0x2eb0 (see earlier lane):
  word0 bits11:0 svlan, bit18 0x40000 add-tag flag, bits12..14 priority/class, word1 byte1 =
  0x80 | (selector<<4) with selector = add_type (del_type==0) or del_type+2; double-tag variant
  carries cvlan/tpid2 fields. The stock capture has exactly one entry: id=1, w0=0, w1=0xc000
  (0x80|0x40 -> selector 4 -> del_type 2), w2/w3=0.
- l3intf: key bank pub+0x5b000+id*16; data bank pub+0x5c000+id*16 (both 16B).
  Decoded from l3intf_write@0x2bec and l3intf_dump@0x2854. Logical 16-byte software entry (the
  blob the CFE/ADP layer passes, field map from the vendor dump function):
    +0..3   sip (IPv4, network order) - only for an IPv4 interface
    +4..9   smac (6 bytes) - only for an L2/MAC interface
    +10..11 session id (printed as session when nonzero)
    +12..13 mtu in bits 0..13; the same word bits14..19 carry the 6-bit tagact_id
    +14     bit4 = smac-interface flag, bit5 = sip-interface flag,
            bits7:6 = ppp mode (00 none, 01 add, 10 del, 11 rep)
    +15     bit0 = ttl_dec; bits6:1 = statis_idx
  Hardware encoding (from the writer; byte-swapped MAC, MTU/ppp/ttl packed into the data bank):
    key bank:  +0..3 sip; +4..9 mac (reversed 4 bytes at +6..9 plus rev16 of +4..5);
               +10 l3 interface flags; rest zero
    data bank: byte+0 word: bits0..10 mtu, bits16..17 ppp mode, bit18 ttl_dec, bit19 always 1;
               tagact_id in the word at +0 bits24..29 (verify against dump);
               statis_idx at +8 word bits0..4 with bit8 set
  The tagact_id reference for the stock rootif/L3 entries is expected to appear here; the entry
  can therefore be decoded entirely from the MMIO dump (no DDR needed).
- nhp: pub+0x59000 (16B entries) - next-hop table; writer nhp_write@0x2dd0.
- car / car_profile: pub+0x55000 / pub+0x53000 (16B entries).
- mcc userll: mcc+0x44000 + id*4 (4B words; writer pfe_mcc_write_user_ll@0x4764).
  Writer packs: bits0..8 = 9-bit port/alias field from software hword+0, bits10..13 = 4-bit
  vport from (byte1>>1)&0xf, bit25 = 0x2000000 flag, bit14 = "id was zero" marker (clz trick),
  bits16..21 = (byte2 & 0x3f) when nonzero, bits22..? from 0xfec0 mask. The dump-side software
  blob (userll_ext) prints port/vport/ext_port from +8h/+6b/+7b, mac at +2..5, and a mac flag at
  +10 bit0 - i.e. the word is a port/vport (or MAC) mapping entry for the multicast user link
  list. Exact bit semantics [I]; the dump validates.
- ifc: tab128 key/mask words at ifc+0x50000 (16B per id; the 8 default ids read back in W10);
  the action descriptors are 5 write-only engine words (ifc+0x120 command, +0x138..+0x148 data).
- flow counters: napt+0x40000 (16B per flow id, tab128, writer hi_tab_write_flow_cnt@0x37d8).
- l3 counters: eps+0x50000; nhp counters: eps+0x4d000 (16B entries).

## 3b. Real vendor write set (forwarded LAN->WAN NAT stock phase)

The first populated hardware tables from stock are now decoded (FWD-PHASE-ANALYSIS.md):
- l3intf key/data entries 1,2: sip 0 and 192.168.50.115, MACs d4:0d:ab:64:1c:73/:74, tagact_id 2,
  mtu 1500, data word 0x000c0ddc;
- nhp entries 1,2: next-hop IP/MAC pairs for the LAN host and the WAN peer/gateway;
- tagact entry 2 (w0=0, w1=0x0000b000, selector 3), referenced by both l3intf rows.
These validate the static key/data layouts of the readable tables. The flow arenas remained empty.

## 4. Flow table path (v4/v6): what is already known and what the dump must settle

- The flow entries are not written through a CPU-readable tab128. The NAPT engine inserts/deletes
  them via command registers (pfe_napt_v4_del_flow writes napt+0xa8/0xac/0xb0/0x180/0xb4..0xd0 and
  napt+0x240/0x244/0x248; the insert path builds a 68-byte descriptor from the software data blob
  and drives the same register block), so the entry bytes land in the flow DDR window managed by
  the engine. The logical fields are known from the vendor dump callback
  pfe_napt_v4_dump_flow@0x4028: dip +0, sip +4, sport +8, dport +10, proto +12, igr_vport +13
  (low 4 bits), plus flags in +14/+15, and the 16-byte counter block appended at +16 (pkts/bytes/
  age printed by hi_tab_dump_flow_cnt). v6 uses 40B key / 52B data.
- **Flow-window geometry is SOLVED** from the our-side W22 dump (OUR-SIDE-VALIDATION.md section 2):
  the 1 MiB window is partitioned exactly as v4 bucket array 0x8000 bytes (8192 x 4B heads) at
  napt+0x130=0x80400000, v4 entry arena 0x40000 bytes (8192 x 32B slots) at napt+0x138=0x80408000,
  v6 bucket array 0x8000 bytes at napt+0x134=0x80448000, v6 entry arena 0xB0000 bytes (8192 x 88B
  slots) at napt+0x13c=0x80450000; 32K+256K+32K+704K = 1 MiB exactly. W22's two clear commands
  wrote the empty-bucket marker 0x017be000 into the two bucket arrays (8192 words each) and left
  the entry arenas at the DDR poison baseline (0x66cc66cc); the marker encoding is still [U]. v4
  total 8224 = 8192+32 reserved, v6 8208 = 8192+16 reserved. **Stock p0/p2 and our W22 dump are
  byte-identical over the whole flow window**, so W22 init reproduces the vendor state exactly; 200
  ICMP pings on stock created no flow entry (p0->p2 flow diff = 0 bytes), so the entry-arena byte
  layout is still [I]. It needs a stock phase with real forwarding/offload (STOCK-DUMP-ANALYSIS.md
  section 7) or a controlled insert probe.
- The same applies to the queue DDR window (4 MiB): W7 decoded the EQM table ids and formulas but
  the in-window layout is confirmed only by the dump.

## 5. Status classes (PIE RX completion)

Observed on hardware [V]:
- status 0: normal CPU-copy path (mirror copies, port 0).
- status 0x25 (37): IFC exception/punt class; carried the ARP/broadcast frames during the mirror
  era (E0-a). The vendor's completion decode sets the "no-error" summary bit only for status 0,
  our driver derived err from status != 0.
- status 0x4d (77): the NAPT/flow-engine up-direction punt class after the W22 NAPT init; this is
  the class that delivers native unicast to the CPU on the proof image.
The two non-zero classes are therefore different CPU-delivery paths (IFC exception vs NAPT punt),
not frame errors. Whether the hardware emits further classes (e.g. multicast cap_tocpu, WoC, IPv6)
is one of the things the stock dump + traffic matrix should reveal; the dump request includes the
PIE window and the counters so classes can be correlated with table hits.

## 6. Remaining open items (after the our-side dump; stock still inaccessible)

1. Flow-entry arena byte layout/field offsets (v4 32B and v6 88B slots were measured; the field
   packing inside a slot is still [I]). The forwarded LAN->WAN NAT phase still left the arenas empty
   (FWD-PHASE-ANALYSIS.md); the stock CFE appears not to use the DDR flow cache, so M2-A is not
   pursued. Geometry/bases are solved (section 4).
2. l3intf hardware field encoding (partially decoded; dump of pub+0x5b000/0x5c000 with known
   entries validates every bit).
3. tagact_id references: which l3intf entry references tagact 1 and what action it describes.
4. MCC userll / userll_ext layout and multicast CPU-copy wiring (mcc+0x44000 and the ext table).
5. Queue-window layout under CPU-punt traffic (CPU port 4 queue) and any CFE queue config beyond
   the W7 EQM init.
6. The userspace side of the CFE: recovery-extract has the vendor rootfs but no obvious CFE
   userspace with symbol strings; the kernel modules are the primary source. If the lead finds the
   CFE userspace binary (e.g. a Hisilicon/cfm daemon not present in the extracted rootfs), it
   would confirm the netlink attribute names for the ADP flow add/del API.
