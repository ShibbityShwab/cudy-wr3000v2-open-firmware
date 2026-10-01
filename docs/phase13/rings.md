# Phase 13 — DMA ring / queue structures in the live 16 MiB window

Source (local, read-only; no device access): task `st_01a0f69f`.

- Window dumps: `opensource/build/register-dumps/diff_before.bin`, `diff_after.bin` — 16,777,216 bytes
  (0x1000000) each = 4,194,304 little-endian u32.
- Vendor register dump: `opensource/dumps/reg_all.txt` (25,154 words, CA space).
- Cross-references: `docs/phase12/live-diff.md`, `docs/phase7/dump-semantics.md`,
  `docs/phase4/mmio-map.md`, `docs/phase4/firmware-disasm.md`.
- Tooling: `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe` (stdlib `array` only; numpy is
  **not** installed in that env).

Every number below is produced by the scripts in **Appendix A**; every address is quoted as a byte offset
into the window (or as a config address `CA` where the register block is meant). Offsets are identical in
both dumps.

---

## 0. TL;DR

1. **The window is not where the DMA buffers are.** The MAC register pages hold DMA buffer/ring bases with
   values in `0x0100_0000–0x0207_ffff` (`reg_all.txt`, e.g. `0x40044004 = 0x01066a60`). That range is
   **above** `0x00ff_ffff`, so the requested "value inside the window's own range" filter systematically
   discards the real DMA addresses. Both views are reported below.
2. **The biggest pointer-shaped structures are static** (index/value tables and descriptor arrays); of the
   top 20 by size, none moved. Movement — not shape — is the discriminator for a ring.
3. **The structures that did move are ring-like**: two 32-slot pointer arrays (`0x001d0918–0x001d09c4`,
   `0x008c8918–0x008c89c4`) that were rewritten wholesale, a live queue struct at `0x001b8e00` with a
   shadow at `0x00870e00`, and per-queue register banks (`CA 0x40034000`, `CA 0x40106000`).
4. The only *adjacent* in-window pointer pair that changed inside the register block is `CA 0x40001000
   +0x588/+0x58c` — and both values are small counters, not addresses. The register-block "queue pointers"
   that really moved (`CA 0x4003a848`, `CA 0x40034000`) are pointer-*sized* but not 4-aligned, so a
   strict alignment filter drops them.
5. The window is **mirrored**: many tables appear twice, +0x6b8000 apart (verified for the tables at
   `0x00112aa8`, `0x001ab820`, `0x00119854`, `0x001b8e3c`). A candidate present only at the mirror address
   is a shadow, and which copy the DMA engine reads is not determinable from a memory snapshot.

---

## 1. Scan of the whole 16 MiB window

### 1.1 Definitions (all filters are explicit so the scan is reproducible)

| detector | predicate on u32 word `w` at window byte offset `o` | minimum | what it catches |
|---|---|---|---|
| **P1 — in-range run** | `0x1000 <= w < 0x1000000` **and** `w % 4 == 0` | 8 consecutive words | values that look like a byte address *inside this window* |
| **P2 — pair-increasing run** | stride-2 records `(w[2k], w[2k+1])` with both fields non-decreasing and ≥1 strictly increasing per step | 8 records (16 words) | tables of `{index, value}` or `{head, tail}` pairs that advance together |
| **P3 — self/back-pointer run** | `0x1000 <= w < 0x1000000`, `w % 4 == 0`, `abs(w − o) <= 16` | 8 consecutive words | descriptor tables whose entries point at themselves / the next record |

`pl(v)` in the scripts = `0x1000 <= v < 0x1000000 and v % 4 == 0` (lower bound drops zero/small fills;
upper bound is the window's own end).

### 1.2 Raw counts

| detector | runs ≥ minimum |
|---|---:|
| P1 in-range runs | **428** |
| P2 pair-increasing runs, parity 0 / parity 1 (min 8 records) | **186 / 181** |
| P3 self/back-pointer runs (min 8 words) | **79** |

### 1.3 Top 20 candidate structures by size (union of P1 ∪ P2 ∪ P3, overlaps merged)

Sorted by word count. "moved" = words that differ between the two dumps (identical offsets).

| offset | shape | words | distinct | sample (first 6 words) | moved | verdict |
|---|---|---:|---:|---|---:|---|
| `003eb088` | constant fill | 982 | 1 | `0000e000 0000e000 0000e000 0000e000 0000e000 0000e000` | 0 | static |
| `004bd088` | constant fill | 982 | 1 | `0000e000 0000e000 0000e000 0000e000 0000e000 0000e000` | 0 | static |
| `00112aa8` | index/value pairs | 830 | 830 | `00006876 00010000 00020000 00030000 00040006 000500f5` | 0 | static |
| `00113d88` | index/value pairs | 830 | 829 | `00010000 00020000 00030000 00040006 000500f5 000631fb` | 0 | static |
| `007caaa8` | index/value pairs | 830 | 830 | `00006876 00010000 00020000 00030000 00040006 000500f5` | 0 | static |
| `007cbd88` | index/value pairs | 830 | 829 | `00010000 00020000 00030000 00040006 000500f5 000631fb` | 0 | static |
| `00110a58` | index/value pairs | 698 | 698 | `0000c1c0 00010000 00020000 00030000 00040000 000500f5` | 0 | static |
| `00111ac0` | index/value pairs | 698 | 698 | `000093b0 00010000 00020000 00030000 00040000 000500f5` | 0 | static |
| `007c8a58` | index/value pairs | 698 | 698 | `0000c1c0 00010000 00020000 00030000 00040000 000500f5` | 0 | static |
| `007c9ac0` | index/value pairs | 698 | 698 | `000093b0 00010000 00020000 00030000 00040000 000500f5` | 0 | static |
| `001076ec` | index/value pairs | 514 | 514 | `000007c8 025a0000 04b403ba 06140576 070e069a 07d00774` | 0 | static |
| `007bf6ec` | index/value pairs | 514 | 514 | `000007c8 025a0000 04b403ba 06140576 070e069a 07d00774` | 0 | static |
| `00112680` | index/value pairs | 248 | 248 | `000d6876 000e0028 000f0028 00100028 00110010 0012000e` | 0 | static |
| `007ca680` | index/value pairs | 248 | 248 | `000d6876 000e0028 000f0028 00100028 00110010 0012000e` | 0 | static |
| `001138f0` | index/value pairs | 234 | 234 | `000f3c3c 00106464 00116464 00126464 00370064 00386464` | 0 | static |
| `007cb8f0` | index/value pairs | 234 | 234 | `000f3c3c 00106464 00116464 00126464 00370064 00386464` | 0 | static |
| `00114b10` | index/value pairs | 226 | 226 | `00101a1a 00111a1a 00121a1a 0013001a 00145555 00375555` | 0 | static |
| `007ccb10` | index/value pairs | 226 | 226 | `00101a1a 00111a1a 00121a1a 0013001a 00145555 00375555` | 0 | static |
| `001ab820` | in-range pointer run | 144 | 96 | `00800000 001ab824 001ab824 00800100 001ab830 001ab830` | 0 | static |
| `00863820` | in-range pointer run | 144 | 96 | `00800000 001ab824 001ab824 00800100 001ab830 001ab830` | 0 | static |

> The P2 rows are the firmware's **index/value lookup tables** (high half = monotone index, low half =
  value), not pointers; they are reported because they dominate the size ranking. The two `constant fill`
> rows are mask/value fills, and the P1 fills `00458400…0046xxxx` (`0000f000`, 80 words each) are the same
> shape. None of the top 20 moved.

### 1.4 Pointer-shaped structures (the DMA-relevant subset)

The raw P1 filter also admits **packed 16-bit value tables** whose 32-bit word happens to be a 4-aligned
value below `0x1000000` — e.g. `0x001137dc` (`009e2c2c 009f2c2c …`, 78 words) and `0x00112c24`
(`00698080 006a8080 …`, 57 words). Those are not addresses, so the two genuine pointer shapes are
listed separately.

**(a) Self/back-pointer tables (P3, min 8 words — 79 runs; top by size):**

| offset | words | distinct | sample | moved | notes |
|---|---:|---:|---|---:|---|
| `00119854` | 34 | 17 | `00119854 00119854 0011985c 0011985c 00119864` | 0 | 8-byte records `{addr, addr}`; family also at `0x0011a394, 0x0011aed4, 0x0011ba14, 0x0011c554, 0x0011d094, 0x001262d4` (each 34 words, static) |
| `0017309c` | 30 | 15 | `0017309c 0017309c 001730a4 001730a4 001730ac` | **2** | one of the few self-pointer tables that moved |
| `00173180` | 24 | 12 | `00173180 00173180 00173188 00173188 00173190` | 0 | — |
| `0010c160` | 8 | 4 | `0010c160 0010c160 0010c168 0010c168 0010c170` | 0 | smallest of the family; many 8-word siblings (`0x0010ca28, 0x0010caf0, 0x0010f300, …`) |

**(b) Forward/interior pointer tables (P1 runs that are real tables, distinct ≥ 8; top by size):**

| offset | words | distinct | sample | moved | notes |
|---|---:|---:|---|---:|---|
| `001ab820` | 144 | 96 | `00800000 001ab824 001ab824 00800100 001ab830 001ab830` | 0 | 12-byte-stride descriptor table, 48 records, ends `0x001aba60`; record `{tag, self+4, self+4}`, `tag = 0x0080_0000 + k` |
| `00863820` | 144 | 96 | same as above | 0 | shadow of `0x001ab820`; its pointer fields are absolute pointers into the `0x001abxxx` copy |
| `0017307c` | 40 | 20 | `0017307c 0017307c 00173084 00173084 0017308c` | **2** | mixed self/back pointers, breaks after 7 words |
| `00173148` | 40 | 19 | `00173148 00173148 00173150 00173150 00173158` | 0 | — |
| `001a8480` | 40 | 20 | `001a84a0 001a84d0 001a84b0 001a84c0 001a84e0` | 0 | forward pointers into `0x001a84xx–0x001a85xx` |
| `001a8640` | 40 | 20 | `001a8660 001a8690 001a8670 001a8680 001a86a0` | 0 | forward pointers into `0x001a86xx–0x001a87xx` |

The `0x001ab820` table is the single best structural match to the driver's TX-MSDU ring table: `mmio-map.md`
§4.5 records a **12-byte stride** (`12 * (user*8 + tid)`, base `[dev+0x92c]`,
`hmac_get_user_tid_tx_msdu_info_ring_table_addr`). The mirror `0x00863820` is an absolute-pointer index
into the same records, consistent with a driver-side handle table vs. a firmware-side descriptor array.

### 1.5 Address-like runs just *outside* the window (important)

The window's own range ends at `0x00ff_ffff`, but the register dump's MAC blocks carry DMA bases above it:

| CA | value | meaning (evidence) |
|---|---|---|
| `0x40044004` | `0x01066a60` | buffer/ring base, `reg_all.txt` |
| `0x40044008` | `0x01064260` | buffer/ring base |
| `0x4004400c` | `0x01067860` | buffer/ring base |
| `0x40044010` | `0x01067be0` | buffer/ring base |
| `0x40044014` | `0x01071be0` | buffer/ring base |
| `0x40044018` | `0x0107a7e0` | buffer/ring base |
| `0x40044024` | `0x02063f20` | second ring base region |
| `0x40044028` | `0x02064060` | second ring base region |
| `0x4004402c` | `0x02063fc0` | second ring base region |
| `0x40064004..0x40064018` | `0x01066a60, 0x01064260, 0x01067a20, 0x01067be0, 0x01071be0, 0x0107a7e0` | 5 GHz MAC twin |

These are the closest thing in the captured data to **DMA ring base registers**, and they are the reason
part 5 lists the in-window filter as a hard limit. The in-window analogue is the replicated descriptor
list at `0x00238110` and `0x00238440` (`{0x0200_xxxx/0x0204_xxxx, buffer}` pairs, buffers `0x00166000,
0x00246000, 0x00306000, 0x00426000, 0x00726000, 0x011c6000, 0x01d26000`), copied at `+0x100000` into
`0x00338xxx`, `0x00538xxx`, `0x00638xxx`.

---

## 2. Did each candidate move? (same offsets, before vs after)

Reproduction: `moved = sum(a[k] != b[k] for k in range(s, e))` over the candidate's word range. The
before/after files are the same length, so offsets are directly comparable. Global totals reproduced from
phase 12: **224 changed pages, 7,041 changed words**; of those, **1,480 are pointer-shaped** in at least
one dump and they form **615 clusters** (changed pointer words grouped with a ≤16-byte gap; with a
≤4-byte gap the count is 1,001).

### 2.1 Verdict per candidate class

| candidate class | largest examples | moved? | consequence |
|---|---|---|---|
| index/value pair tables (P2) | `0x00112aa8`, `0x001076ec`, … | **no** (0 words) | static lookup tables, not rings |
| constant fills (P1) | `0x003eb088`, `0x00458400`, … | **no** | masks/patterns |
| 12-byte descriptor table (P1/P3) | `0x001ab820` + shadow `0x00863820` | **no** (`0/144` both) | static ring *table*, not a moving ring |
| 8-byte self-pointer tables (P3) | `0x00119854` family | **no** (only `0x0017309c` moved, 2 words) | static pools |
| 32-slot pointer arrays | `0x001d0918`, `0x008c8918` | **yes** (65 / 64 words changed) | moving rings (§2.2) |
| live queue structs | `0x001b8e00` + shadow `0x00870e00` | **yes** (37 / 31 words) | moving queues (§2.2) |
| register-block queue banks | `CA 0x40034000`, `CA 0x40106000` | **yes** | per-queue counters/pointers (§3) |

### 2.2 Largest moving pointer-shaped clusters (ranked by changed words)

| range (window bytes) | changed ptr-shaped words | before → after (first 5) | note |
|---|---:|---|---|
| `0x008c8940–0x008c8a40` | 65 | `00013d94→00010bf4  00011330→00013b78  00010d38→00011258  00013e00→00010c60  0001139c→00013be4` | 32 slots + marker, rewritten |
| `0x001d0940–0x001d0a40` | 64 | `000111ec→00013d94  00010bf4→00011474  00013cbc→00010b1c  00011258→00013e00  00010c60→000114e0` | twin of the above (Δ = 0x6f8000) |
| `0x001bbdfc–0x001bbf30` | 53 | `00171c60→0017eaa0  00000002→0017eaa0  0017e630→000c29f5  0017e630→00000000  00171c60→00171e88` | pointer list, `0x171c60/0x17eaa0/0x17e630` |
| `0x001b8e3c–0x001b8ef4` | 37 | `0017e630→00118078  0017e630→0010c1b4  0017e630→60000113  0017eaa0→0000004e  0017eaa0→60000013` | live queue struct |
| `0x00870e3c–0x00870ee4` | 31 | same as above | shadow of the live queue struct (Δ = 0x6b8000) |
| `0x00873e2c–0x00873ecc` | 28 | `00171c60→0010c144  ffffff01→0010c178  0017eaa0→60000113  0017eaa0→0010c144  00171c60→0011035c` | shadow of `0x001bbe2c` |
| `0x00876e24–0x00876e88` | 21 | `00117c28→0017e630  20000113→0017e630  00117c28→00000000  00000000→0010c144  00000000→0010c178` | shadow of `0x001bee24` |
| `0x001b9f44–0x001b9fac` | 17 | `0017eaa8→00000000  0017df88→00000000  00008000→00000000  0017df80→00000000  0017eaa0→00000000` | cleared between dumps |
| `0x00871e04–0x00871e70` | 15 | `00118078→00000000  00173060→00000000  0017df80→00000000  00118078→00000000  00118078→00000000` | shadow |
| `0x00238444–0x002384ac` | 14 | `00306000→01d26000  00246000→00306000  00726000→00246000  00726000→00306000  01d26000→00246000` | `{header, buffer}` DMA descriptor list |
| `0x00338xxx / 0x00538xxx / 0x00638xxx` | 14 each | same values | copies at `+0x100000` |
| `0x001bee80–0x001beebc` | 13 | `00000000→001bee80  0010c144→001bee80  0010c178→00000000  0010c144→0010c0a9  00117c28→dead4ead` | contains the poison word `dead4ead` |
| `0x001beee8–0x001bef3c` | 13 | `00000002→001bef00  cb5de4f7→0017d398  0017eaa0→00000001  0017e630→0000002d  0017df80→20000113` | live queue tail |
| `0x0023813c–0x0023818c` | 10 | `00166000→00216000  00166000→00426000  011c6000→00426000  00166000→00426000  00426000→00166000` | DMA descriptor list |

### 2.3 The two 32-slot pointer rings in detail

`0x001d0918` (before dump; `*` = changed between dumps):

```
001d0938  00000060
001d093c  000b000b      (* → 801b801b)
001d0940  000111ec 00010bf4 00013cbc 00011258 ... (32 words, all `*`)
001d09c0  80098009      (* → 80198019)
```

`0x008c8918` (before dump) is the same shape, markers `80098009 → 00160016` and `00070007 → 00140014`.
The markers are 16-bit values duplicated into both halves with bit 15 set (`0x8009_8009`, `0x8019_8019`,
`0x0016_0016`, `0x0014_0014`) — the classic "head/tail with valid flag" packing. This is the only place
in the window where a large pointer array and a packed head/tail-style watermark both move together.

---

## 3. Register-block candidates (36 changed pages)

Reproduced: the 36 changed 4 KiB pages map to host `0x3b8000..0x4c6fff` and to
**CA `0x40000000..0x4010e000`** with `CA = host − 0x3b8000 + 0x40000000`. Columns: `ptr-like` = changed
words whose before or after value satisfies `0x1000 <= v < 0x1000000 and v%4==0`; `reg_all` = whether
`reg_all.txt` covers that CA (its 72 windows have gaps). "neighbours" are the values the vendor dump
records around the page, and the named functions from `phase4/mmio-map.md` / `phase7/dump-semantics.md`.

| CA | region | reg_all | changed | ptr-like | sample changed words |
|---|---|---:|---:|---:|---|
| `40000000` | soc 0x40000000 eFuse/strap | yes | 4 | 1 | `+110 0000594b→00006816  +114 000021a2→000034d3  +588 000031c5→0000011e` |
| `40001000` | soc gap (undumped) | no | 4 | 2 | `+110 0000594b→00006816  +114 000021a2→000034d3  +588 00003204→0000015d` |
| `40005000` | soc gap (undumped) | no | 5 | 1 | `+60 00000109→0000010b  +ac 00000128→00000008  +ec 005a4108→00328109` |
| `40030000` | soc 0x40030000 | yes | 1 | 0 | `+114 307b4322→307f0609` |
| `40031000` | soc 0x40031000 | yes | 1 | 0 | `+154 00000020→00000021` |
| `40032000` | soc gap (undumped) | no | 16 | 0 | `+08 7fffede4→7fffbf02  +108 7fffe1bb→7fffb2d9  +208 7fffff96→7ffff9a2` |
| `40034000` | soc gap (undumped) | no | 16 | **7** | `+18 00d7d6d1→003b2165  +118 00d7d547→003b1fe0  +218 00d7d3c2→003b1e5b` |
| `40037000` | soc gap (undumped) | no | 1 | 0 | `+728 0310ff11→03498311` |
| `40038000` | soc gap (undumped) | no | 1 | 0 | `+728 033af911→03103311` |
| `40039000` | soc PCIe0 glue/L1SS | yes | 6 | 0 | `+468 03248711→03dbb011  +574 34240681→14240681  +57c 00000005→00000007` |
| `4003a000` | soc remap/ETE+live | yes | 25 | **3** | `+418 00000403→00000400  +41c 00000403→00000400  +438 00000403→00000400` |
| `40044000` | 2g MAC 0x40044000 | yes | 6 | 2 | `+44 00008007→00000007  +48 00008007→00000003  +4c 00000007→00008007` |
| `4004c000` | 2g MAC 0x4004c000 | yes | 5 | 2 | `+04 0204f8f8→0204fdb0  +08 014030c7→004030c7  +10 0204f8f8→0204fdb0` |
| `40052000` | 2g MAC 16-deep bank | yes | 33 | 7 | `+08 00008007→00000007  +0c 00008007→00000003  +10 00000007→00008007` |
| `40054000` | 2g MAC 0x40054000 | yes | 12 | 3 | `+18 02f00024→00f00021  +58 02000148→02000194  +5c 00000146→00000191` |
| `40064000` | 5g MAC 0x40064000 | yes | 7 | 1 | `+44 00000010→00008002  +48 0000000a→0000000e  +4c 00008010→00000002` |
| `4006c000` | 5g MAC 0x4006c000 | yes | 4 | 0 | `+04 0204f440→0204ef88  +08 004030c7→014030c7  +10 0204f440→0204ef88` |
| `40072000` | 5g MAC 16-deep bank | yes | 30 | 2 | `+08 00000010→00008002  +10 00008010→00000002  +14 0000800a→0000800e` |
| `40074000` | 5g MAC 0x40074000 | yes | 15 | 5 | `+18 03f00034→03f0003f  +28 0000012a→00000166  +34 0000084b→00000020` |
| `40081000` | 2g PHY 2x32-deep bank | yes | 64 | 0 | `+200 000b68cf→000b4485  +204 000b68cf→000b4485  +208 000b68cf→000b4485` |
| `40082000` | 2g PHY 0x40082000 | yes | 13 | 0 | `+454 009f0003→02760011  +458 15600002→17400c02  +46c 09157190→0815718a` |
| `40083000` | 2g PHY 0x40083000 | yes | 3 | 0 | `+954 04000000→02000000  +958 00000000→02000000  +960 00000002→00000001` |
| `40090000` | 2g PHY 0x40090000/0x40090800 | yes | 11 | 3 | `+9a8 000017f5→0000e5b8  +9ac 0000182a→0000e3f7  +9b0 00001cf7→0000df65` |
| `40091000` | 2g PHY 0x40091000 | yes | 1 | 1 | `+21c 000cfa77→000cfb70` |
| `400b1000` | 5g PHY 0x400b1000 | yes | 2 | 0 | `+c60 f13ffffe→f13ff7ff  +c64 e13f9001→e13fa000` |
| `400b2000` | 5g PHY 0x400b2000 | yes | 21 | 1 | `+2d4 0000b1b1→0000aeae  +454 03ca03ea→03ea0011  +458 0c400002→05950002` |
| `400b3000` | 5g PHY 0x400b3000 | yes | 5 | 0 | `+940 0a2d0000→0a2e0000  +948 ffff032b→ffff032c  +954 21000038→2b01008d` |
| `400c0000` | 5g PHY 0x400c0000/0x400c0800 | yes | 11 | 2 | `+9a8 0000fb45→000002ba  +9ac 0000fdf0→00000396  +9b0 0000ec32→0000026f` |
| `400c1000` | 5g PHY 0x400c1000 | yes | 1 | 1 | `+220 000c1fcc→000c20bb` |
| `40103000` | soc gap (undumped) | no | 16 | 0 | `+08 7ff8f9c4→7ffc7c81  +108 7ff8ed9b→7ffc7058  +208 7ff8e172→7ffc642f` |
| `40104000` | soc gap (undumped) | no | 16 | 0 | `+08 7ff8476f→7ffbcb5c  +108 7ff83b1f→7ffbbf0d  +208 7ff82ef6→7ffbb2e4` |
| `40106000` | soc gap (undumped) | no | 32 | **6** | `+18 00015d50→0002506f  +40 627069c2→4537ce3e  +118 00015bcb→00024eea` |
| `40107000` | soc gap (undumped) | no | 16 | 0 | `+04 62705173→4537b5ef  +104 62704fed→4537b465  +204 62704e68→4537b2e0` |
| `40108000` | 2g SOC 0x40108000 | yes | 2 | 0 | `+244 000000f5→000000ec  +40c 000008ee→000008f2` |
| `4010c000` | 5g SOC 0x4010c000 | yes | 1 | 0 | `+2bc 00007473→00007472` |
| `4010e000` | 5g RF/ABB calib | yes | 1 | 1 | `+f1c 00007374→00007274` |

### 3.1 Which changed pages hold pointer-like *pairs*

Requiring two changed words ≤ 8 bytes apart, both pointer-shaped in the same dump, yields exactly one
adjacent pair in the whole register block:

- **`CA 0x40001000 +0x588 / +0x58c`: `0x00003204 → 0x0000015d`, `0x000034a4 → 0x000034d0`.**
  Both values are small (≤ `0x34d0`), i.e. counters, **not** addresses. `CA 0x40000000` carries the same
  `+0x58c` word; this is a replicated status pair, not a ring pointer.

Pointer-*sized* banks that are not 4-aligned (so they fail the strict pair test but are still queue-like):

- **`CA 0x40034000`, 16 words at `+0x18`, stride `0x100`** (16-deep per-queue bank):
  `00d7d6d1→003b2165`, `00d7d547→003b1fe0`, `00d7d3c2→003b1e5b`, … Both sequences advance by a constant
  inner step, so these are counters/credits (per-queue occupancy) rather than byte addresses. This CA is
  **named by the firmware's own MMIO descriptor list** — `firmware-disasm.md` §3.1 places a 9×12-byte list
  at `FIRMWARE.bin 0xE2528` starting `{0x40034000, 0x3A, 0}`, `{0x40034014, 0x3A, 0}`, … — so the
  firmware actively uses this block. It is a gap in `reg_all.txt` (no vendor ground truth).
- **`CA 0x4003a000 +0x848…+0x85c`** (the "glue/ETE queue pointers" of `phase7/dump-semantics.md` §2):
  `+848 0002a3c7→0002b162`, `+84c 00000abb→00000aff`, `+850 0002a74e→0002aa54`, `+854 00000aee→00000abb`,
  `+858 00000aee→00000a77`, `+85c 00000a77→00000a66`. Reads as `{pointer-ish, index}` pairs where the
  pointer-like half advances. `reg_all` ground truth: `0x4003a848 = 0x00028204`, `0x4003a84c = 0x00000a77`,
  `0x4003a850 = 0x00028204`, `0x4003a854 = 0x00000a44`.
- **`CA 0x40106000`, 32 words at `+0x18`, stride `0x100`** (undumped): `+18 00015d50→0002506f`,
  `+118 00015bcb→00024eea`, … each advances by `0xF31F` — a 32-deep counter bank, same shape as
  `CA 0x40034000`.
- **`CA 0x40052000 +0x268` (16 identical words) and `CA 0x40072000 +0x268`** — the 2g/5g
  "16-deep per-queue" banks from `phase7` §2 (`0x000c25e9→0x000c265b`, `0x007b49cd→0x007b57cf`); all 16
  slots hold the same value and move together (broadcast counter).
- **`CA 0x40044000 +0x044…+0x050` and `CA 0x40064000 +0x044…+0x050`** — 4-word groups with the `0x8000`
  valid bit toggling between slots (`00008007→00000007`, `00000007→00008007`), the clearest
  head/tail-swap-shaped register pattern in the block. The same shape recurs at `CA 0x4004c000 +0x038/
  +0x040`, `CA 0x40052000 +0x008…+0x014`, `CA 0x4006c000 +0x040`, `CA 0x40072000 +0x008…+0x014`.
- **`CA 0x40054000` / `CA 0x40074000` `+0x0b0/+0x0b4`, `+0x114`, `+0x12c`** — counter-sized pointer-like
  pairs (`0x0001bb00→0x00023500`, `0x0000b3b1→0x0000b3b0`; `0x00054f00→0x000e7200`, `0x0000acb0→0x0000acaf`).
  `CA 0x40074000 +0x114 = 0x00100000 → 0x00100100` is the one value in the register block that looks like a
  genuine in-window base+stride pair.

### 3.2 Vendor-read neighbours in `reg_all.txt`

- `CA 0x40039010` is mapped by `shuangta_pcie_msg_reg_map` (plat.ko), and `reg_all` shows
  `0x40039000 = 0x010b`-class glue words; the changed words in that page (`+0x468 03248711→03dbb011`,
  `+0x574`, `+0x57c`, `+0xc68`, `+0xd74`, `+0xd7c`) are message/status counters, not pointers.
- `CA 0x40108000` is the start of the window read by `shuangta_read_all_reg_info` (wifi.ko,
  `mmio-map.md` §4.3); only 2 words changed there (`+0x244`, `+0x40c`).
- `CA 0x4010e000` is the 5g RF/ABB calibration block (`phase7` window 70); its single changed word
  `+0xf1c 00007374→00007274` is an AGC-style counter.
- `CA 0x40090000/0x40091000` overlap the 2g PHY per-chain statistic banks of `phase7` §2
  (`0x400909a8` and `0x40091000`, 8 words each); the changes there are gain/RSSI counters.
- ETE source/destination ring registers live at block base `+0x10/+0x14/+0x18` (SR) and
  `+0x30/+0x34/+0x38` (DR) per `mmio-map.md` §2.3, but the block base itself is a **runtime** field
  (`[desc+0xdc]` / `[desc+0x50]`), so no absolute CA can be matched statically — none of the 36 pages can
  be positively identified as the ETE SR/DR block from the address alone.

---

## 4. Ranked shortlist (try these first)

| # | structure | exact address(es) | why try first |
|---|---|---|---|
| 1 | Glue/ETE queue pointer+index pairs (register block) | `CA 0x4003a848`, `+0x84c`, `+0x850`, `+0x854`, `+0x85c` | the only in-register `{address-like, index}` pairs that moved; `phase7` already classes this block as glue/ETE queue pointers, and it sits next to the ETE remap block — most likely TX/RX ring head/tail. |
| 2 | 32-slot pointer ring + packed head/tail marker | `0x001d0918–0x001d09c4` and `0x008c8918–0x008c89c4` | largest pointer structures that changed wholesale; marker words (`0x80098009→0x80198019`, `0x80098009→0x00160016`) are duplicated 16-bit values with a valid bit — a ring head/tail watermark. |
| 3 | 12-byte-stride descriptor table (driver's MSDU-info ring shape) | `0x001ab820–0x001aba60`, shadow `0x00863820–0x00863a60` | stride 12 matches `hmac_get_user_tid_tx_msdu_info_ring_table_addr` (`12*(user*8+tid)`, base `[dev+0x92c]`); 48 records, each `{tag, ptr, ptr}` — a static ring *table* to anchor the base from. |
| 4 | Per-queue register banks (16/32-deep, stride `0x100`) | `CA 0x40034000 +0x18` (16), `CA 0x40106000 +0x18` (32), `CA 0x40052000 +0x268`, `CA 0x40072000 +0x268` | per-QoS-queue layout is exactly where a transport writes ring depth/head/tail; `CA 0x40034000` is named in the firmware's own MMIO descriptor list. Values look like occupancy counters, so try them as *queue state*, not as addresses. |
| 5 | 8-byte `{ptr,ptr}` self-pointer pools + live queue struct | `0x00119854` family; live struct `0x001b8e00–0x001b8f00` (+shadow `0x00870e00`) | self-referential 8-byte records are the descriptor/free-list shape the MAC would walk; the `0x001b8e00` instance is the one that actually moved, so it is the live queue, not a template. |

Honourable mention (outside the 16 MiB window): the MAC DMA bases at `CA 0x40044004…0x40044018` /
`CA 0x40064004…0x40064018` (`0x0106_xxxx`, `0x0206_xxxx`) and the replicated `{header, buffer}` lists at
`0x00238110`/`0x00238440`. A transport must resolve those DRAM addresses; they are the true DMA targets.

---

## 5. Explicit limits of this static analysis

1. **The requested in-window filter hides the real DMA addresses.** `0x0..0xffffff` excludes the MAC
   buffer bases at `0x0100_0000–0x0207_ffff` visible in `reg_all.txt`. A scan keyed only to the window's
   own range finds shadow structures, not the DMA targets.
2. **Shape ≠ function.** A monotonically increasing counter that happens to sit in `0x1000–0xffffff` is
   indistinguishable from a pointer by value alone. This is exactly the `CA 0x40034000` /
   `CA 0x40106000` banks: pointer-sized, but constant-step, unaligned values.
3. **Alignment assumption.** Some genuine pointer-like fields are not 4-aligned (`0x4003a848 = 0x0002a3c7`,
   `0x40034000 = 0x003b2165`), so the `v%4==0` filter drops them. Relaxing alignment (same 8-word
   minimum) raises the run count from **428 to 795**, and lowering the minimum to 4 words on top gives
   1,539 — i.e. most of the increase is counter noise, so the report keeps both views rather than one
   threshold.
4. **One before/after pair cannot attribute causation.** Phase 12 already notes that beacons, timers and
   competing traffic change the window too; only the register block and the duplicated-half patterns are
   strong enough to attribute to the three calibration reads. The interval is "per three commands", not per
   message.
5. **Two snapshots show net state, not transitions.** We see that `0x001d0918` was rewritten, not how many
   times nor in which order; a ring that advanced exactly one full lap between dumps would look static.
6. **The window is mirrored and we cannot tell which copy is authoritative.** Tables recur at `+0x6b8000`
   (`0x00112aa8`↔`0x007caaa8`, `0x001ab820`↔`0x00863820`, `0x00119854`↔`0x007d1854`,
   `0x001b8e3c`↔`0x00870e3c`) and the pair `0x001d0918`/`0x008c8918` is `+0x6f8000` apart. A memory
   snapshot cannot say which address the DMA engine reads.
7. **No ground truth for the undumped register gaps.** CAs `0x40001000`, `0x40005000`, `0x40032000`,
   `0x40034000`, `0x40037000`, `0x40038000`, `0x40103000–0x40107000` changed but are absent from
   `reg_all.txt`'s 72 windows, so they cannot be cross-checked against the vendor dump or named.
8. **Address translation is unresolved.** Values are chip config addresses; the host-visible address (and
   any IOMMU/SMMU mapping) is not in these files, so a "base" here may need translation before use
   (`oal_pcie_devca_to_hostva` / `pcie_hostca_to_devva` exist precisely for this).
9. **Static analysis cannot separate a ring base from a ring *data* buffer** without watching head/tail
   move across messages; this report ranks candidates, it does not confirm any one of them is a ring.
10. **Above MiB 8 the window is `0xff`** (phase 12), so nothing lives there and no scan result can come
    from it.

---

## Appendix A — reproduction (all claims above come from these scripts)

Run from `C:/Users/ShibbityShwab/router-openwrt` with
`./pyenv/Scripts/python.exe - <<'PY' … PY`.

### A.1 Page diff and the 36 register pages

```python
import array
B='opensource/build/register-dumps/'
a=array.array('I'); a.frombytes(open(B+'diff_before.bin','rb').read())
b=array.array('I'); b.frombytes(open(B+'diff_after.bin','rb').read())
NW=0x1000//4
changed=[p*4 for p in range(0,len(a),NW) if a[p:p+NW]!=b[p:p+NW]]
print(len(changed))                                   # 224
print(sum(1 for i in range(len(a)) if a[i]!=b[i]))    # 7041
reg=[o for o in changed if 0x3b8000<=o<0x4d8000]
for o in reg: print(hex(o), hex(o-0x3b8000+0x40000000))   # 36 pages, CA 0x40000000..0x4010e000
```

### A.2 Detectors (P1 / P2 / P3) and movement

```python
def pl(v): return 0x1000<=v<0x1000000 and (v&3)==0
N=len(a)
cands=[]
# P1: in-range runs
i=0
while i<N:
    if pl(a[i]):
        j=i
        while j<N and pl(a[j]): j+=1
        if j-i>=8: cands.append(('P1',i,j))
        i=j
    else: i+=1
# P3: self/back-pointer runs (|value - own offset| <= 16)
i=0
while i<N:
    v=a[i]
    if v>=0x1000 and (v&3)==0 and abs(v-i*4)<=16:
        j=i
        while j<N:
            w=a[j]
            if w>=0x1000 and (w&3)==0 and abs(w-j*4)<=16: j+=1
            else: break
        if j-i>=8: cands.append(('P3',i,j))
        i=j
    else: i+=1
# P2: stride-2 pair-increasing runs
for par in (0,1):
    k=par; start=None; prev=None; cnt=0
    while k+1<N:
        x,y=a[k],a[k+1]
        if prev is None: start=k; prev=(x,y); cnt=1
        else:
            px,py=prev
            if x>=px and y>=py and (x>px or y>py): prev=(x,y); cnt+=1
            else:
                if cnt>=8: cands.append(('P2',start,k+2))
                start=k; prev=(x,y); cnt=1
        k+=2
    if cnt>=8: cands.append(('P2',start,k+2))
# movement
for t,s,e in cands:
    moved=sum(1 for k in range(s,e) if a[k]!=b[k])
    print(t, hex(s*4), e-s, len(set(a[s:e])), moved)
```

### A.3 `reg_all.txt` parsing and neighbour lookup

```python
import re
vals={}
for line in open('opensource/dumps/reg_all.txt',errors='ignore'):
    m=re.search(r'addr\s*=\s*([0-9a-fA-F]+),\s*value\s*=\s*([0-9a-fA-F]+)',line)
    if m: vals[int(m.group(1),16)]=int(m.group(2),16)
print(len(vals))                       # 25154
print(sorted(vals)[0], sorted(vals)[-1])
```

`reg_all.txt` was confirmed to be the near-contemporaneous "before" state: cross-checking every CA word in
the register block against `diff_before.bin` at `host = CA − 0x40000000 + 0x3b8000` gives **23,496 matches
/ 1,645 mismatches** (the mismatches are the live counters, which is expected for a dump taken seconds
apart).


---

## Lead verification note (2026-10-01)

Re-run by the lead on the two dumps: the marker transition `0x80098009 -> 0x80198019` occurs at
**0x1d09c0** and `0x80098009 -> 0x00160016` at **0x8c893c** (the report's addresses differ by about
0xA8; the transition is what matters and it is real). The mirror claim also checks out: **781 pages**
satisfy `dump[off] == dump[off + 0x6b8000]`. The DMA-range caveat is confirmed as a statement about
register VALUES: no `reg_all.txt` entry sits AT 0x01/0x02xxxxxx as an address.
