# Phase 8 - internal structure of the `.kv` calibration payloads

Scope: local files only, read-only. Inputs: `build/tmp/wifi_cali_data.kv` (8,912 B) and
`build/tmp/wifi_cali_data_2g.kv` (2,336 B), the phase-8 container analysis
(`ulw/phase8/kv-format.md`), and the phase-7 live-RAM tables (`ulw/phase7/firmware-ram-dump.md`
§2.4). Tooling: `pyenv/Scripts/python.exe` only (no writes outside this file).

Verified container facts carried over from `kv-format.md` and re-checked here:

```
sha256 dual  8b55839288a88eccf69aaa4366913e11acb0d45c9bea4254742f5fa184b57bbd
sha256 2g    84dad372dd2e9c55ac268b6dae5e42bc5c23bfaa15eef60f466572519523221d
word[0]/word[-1] of the payload:  dual 0x9080 / 0x0c  ,  2g 0x8080 / 0x0f
0xfeedfeed occurrences: 0 in both (RAM magic fully overwritten before the write)
ZZZZ (0x5a5a5a5a): 1 in both;  0xa5a5a5a5: 1 in both
```

The payload starts at disk offset 4 (after `ZZZZ`) and ends before the 4-byte `a5a5a5a5` trailer, so
the struct is `[4 : len-4]`: **2,226 words** (dual) and **582 words** (2g). All word indices below are
payload-relative (word `i` is at disk byte `4 + 4*i`).

**Headline.** The "38-word stride" is real but it is the length of a **record whose field layout
repeats**, not a run of repeating values: within a record the *zero* positions repeat, the *values* do
not. The "0x24-byte sub-stride" (9 words) is the spacing of the record's `[4 zero + 5 data]`
sub-blocks. Both files decompose cleanly into a marker header, a 4-word-periodic data region, a
region of 38-word records with one fixed template, and a final truncated record.

---

## 1. Repetition / periodicity analysis

All numbers below come from this scan (run from `build/tmp/`):

```python
import struct
dw,gw = parse(open('wifi_cali_data.kv','rb').read()), parse(open('wifi_cali_data_2g.kv','rb').read())
# parse(b): body=b[4:len(b)-4]; unpack '<%dI' % (len(body)//4)
def ac(w,maxs):            # exact-word equality autocorrelation
    return [(s, m:=sum(1 for i in range(len(w)-s) if w[i]==w[i+s]), len(w)-s) for s in range(1,maxs+1)]
def cond(w,maxs):          # P(w[i+s]==w[i] | w[i]!=0)  -- content-only repetition
    ...
def maskac(w,maxs):        # nonzero-position agreement: both w[i],w[i+s] nonzero
    ...
```

### 1.1 Raw exact-word autocorrelation

| stride (words) | dual match | dual rate | 2g match | 2g rate |
|---:|---:|---:|---:|---:|
| **38** | 940/2188 | 0.430 | 235/544 | 0.432 |
| 76 | 922/2150 | 0.429 | 203/506 | 0.401 |
| 114 | 876/2112 | 0.415 | 178/468 | 0.380 |
| 152 | 852/2074 | 0.411 | 165/430 | 0.384 |
| 190 | 819/2036 | 0.402 | 149/392 | 0.380 |
| 228 | 799/1998 | 0.400 | 132/354 | 0.373 |
| 266 | 772/1960 | 0.394 | 113/316 | 0.358 |
| 8 | 717/2218 | 0.323 | 181/574 | 0.315 |
| 9 | 577/2217 | 0.260 | 156/573 | 0.272 |
| 5 | 87/2221 | 0.039 | 17/577 | 0.029 |

**The set of strides that repeat:** `38, 76, 114, 152, 190, 228, 266, ...` - i.e. every multiple of 38,
scoring highest at the fundamental 38 and decaying monotonically with stride length. Odd multiples are
not special. This matches `kv-format.md`'s observation.

### 1.2 The 38 is not a period of *values*

Restricting the comparison to positions where `w[i] != 0` (so free matches on zero-filled columns are
excluded) collapses the 38-stride:

| stride | dual `w[i+s]==w[i]` given `w[i]!=0` | 2g same |
|---:|---:|---:|
| 38 | 28/1238 = **0.023** | 17/308 = **0.055** |
| 4 | 88/1258 = 0.070 | 28/328 = 0.085 |
| 2 | 32/1258 = 0.025 | 8/328 = 0.024 |
| 8 | 53/1254 = 0.042 | 11/324 = 0.034 |

So at stride 38 only ~2-6 % of the nonzero words equal their 38-word-away counterpart. The 0.43 raw
rate comes from the *positions* of zeros, not from repeated content. Concretely: the body alternates
`data, 0, data, 0, ...`, and any even stride lands nonzero-on-nonzero; the zero runs also recur on a
38-lattice (below). Confirmed by the nonzero-mask autocorrelation:

| mask stride | dual | 2g |
|---:|---:|---:|
| 38 | 1198/2188 = **0.548** | 293/544 = **0.539** |
| 76 | 1171/2150 = 0.545 | 263/506 = 0.520 |
| 114 | 1144/2112 = 0.542 | 237/468 = 0.506 |
| 28 | 953/2198 = 0.434 | - |
| 8 | 952/2218 = 0.429 | 244/574 = 0.425 |

The mask peaks at 38 while the content does not: **38 is a record *layout* period (where the fields and
their zero-fill fall), not a value period.**

### 1.3 The true repetition unit: a 38-word record with a 9-word (`0x24` B) sub-stride

Counting maximal zero runs of length >= 4 words (`zerorun_starts`) and taking their offsets modulo 38
about the first body run:

```
DUAL base=485: zero-run offsets mod 38 = [0, 9, 18]   (142 runs total)
2G   base=133: zero-run offsets mod 38 = [0, 9, 18]   ( 38 runs total)
```

Every 4-zero run sits at record offset 0, 9, or 18. Spacings: 9, 9, 20, repeating - i.e. three 9-word
groups (`0x24` = 36 bytes apart) then the next 38-word record. **The `0x24`-byte sub-stride is the
spacing of `[4 zero + 5 data]` sub-blocks inside the record.** Aligning records and computing the
per-offset zero fraction over all records gives the record template (45 records dual, 11 records 2g -
full tables in §2):

```
record offsets 0-3, 9-12, 18-21, 27-28, 31-32  -> 100 % zero in every record, both files
record offsets 4-8, 13-17, 22-26, 33-37        ->  0 % zero (5 data words each), both files
record offsets 29,30                           ->  0 % zero, small positive integers, both files
```

### 1.4 Defensible block boundaries

| | dual (`wifi_cali_data.kv`) | 2g (`wifi_cali_data_2g.kv`) |
|---|---|---|
| payload words | 2,226 | 582 |
| header (marker arrays) | words `0 .. 91` (92 w) | words `0 .. 23` (24 w) |
| pre-record data region | words `92 .. 484` (393 w) | words `24 .. 132` (109 w) |
| 38-word records | words `485 .. 2194` (45 records) | words `133 .. 550` (11 records) |
| final partial record | words `2195 .. 2225` (31 w) | words `551 .. 581` (31 w) |

* The record base is where the first 4-word zero run appears (dual 485, 2g 133); no length>=4 zero run
  exists at `base-38*k` back to the header (the zero-run census lists `14,42,70,88` then `485,494,...`
  for dual), so the record region genuinely starts there.
* **Both files end with exactly 31 trailing words** (index 2226-485 = 1741 = 45*38 + 31; 582-133 = 449
  = 11*38 + 31). Those 31 words carry the record template's offsets `0..30` (zeros at 0-3,9-12,18-21,
  27-28; data at the three 5-word groups present (offsets 4-8, 13-17, 22-26); the scalar pair at 29,30
  - dual ends `...00000007 0000000c`,
  2g ends `...0000000b 0000000f`). That is a final record truncated at field 30 by the fixed struct
  size, not a separate object.

---

## 2. Candidate field template (a 38-word record)

Evidence = the per-offset census over 45 dual + 11 2g records (`col=[rec[r] for rec in recs]`;
zero % and distinct values per offset). Left half = dual, right = 2g.

| off | zero% | distinct | role (candidate) | example values (dual rec 0 / 2g rec 0) |
|---:|---:|---:|---|---|
| 0-3 | 100 | 1 | 4 reserved/zero words | `0 0 0 0` |
| 4-8 | 0 | 42-45 / 11 | 5 data words = 10 x 16-bit values | `1fde0014 00101ff6 001d1fe7 00121ff0 1fd90006` / `00161ff7 1ffc1fdf 1ff01fe1 1ffc1fe1 00141ff8` |
| 9-12 | 100 | 1 | 4 reserved/zero words | `0 0 0 0` |
| 13-17 | 0 | 44-45 / 11 | 5 data words | `1feb1ff3 1ff61fff 1ff40002 1ff61ffe 1fee1ff2` / `00060006 1ff40025 1ff80003 1ff80016 000c1ff3` |
| 18-21 | 100 | 1 | 4 reserved/zero words | `0 0 0 0` |
| 22-26 | 0 | 42-45 / 11 | 5 data words | `1fde000a 1ff11ff5 1ffb1fed 1ff61ff3 1fea0008` / `000a1ffb 00211ff4 00201ff7 1fed1ff2 0016000b` |
| 27-28 | 100 | 1 | 2 reserved/zero words | `0 0` |
| **29** | 0 | 8 (dual) / 3 (2g) | small positive integer | dual 5..12 (`0a`,`06`,`05`...); 2g 11..15 (`0f`,`0c`,`0b`...) |
| **30** | 0 | 6 (dual) / 1 (2g) | small positive integer | dual 10..15 (`0f`,`0c`,`0b`...); 2g **constant 15** |
| 31-32 | 100 | 1 | 2 reserved/zero words | `0 0` |
| 33-37 | 0 | 42-45 / 11 | 5 data words | `0036001a 1fd11ff5 1fb11fd9 1fcf1feb 0039000a` / `00060008 000b1ffb 000f0003 00150006 0011002a` |

Equivalent framings of the same 38 words:

* **three `[4 zero + 5 data]` groups (9 words = 0x24 B each) + one `[2 zero + 2 scalar + 2 zero + 5
  data]` (11 words)**; or
* **four 5-word data groups** at offsets 4, 13, 22, 33 (20 data words = 40 16-bit values), with 16
  zero words of reserved space and the 2-word scalar pair at 29-30.

### 2.1 Value evidence

* **Data words are pairs of 16-bit values.** Every data word's two halves carry small magnitudes; e.g.
  `0x1fde0014` = `(0x0014, 0x1fde)`, `0x0036001a` = `(0x001a, 0x0036)`.
* **Data halves are 13-bit, not full 16-bit.** Across all 1,800 (dual) / 440 (2g) data halves the
  observed range is `0x0000 .. 0x1fff` (max = 8191 = `0x1fff`; no half reaches `0x2000`). Read as
  two's-complement modulo `0x2000`, `x >= 0x1000` means `x - 0x2000`, giving a range of about
  **-80 .. +60** - plausible for calibration deltas/trim values. This is a decoding hypothesis backed
  by the range, not a proven signedness.
* **Scalars at 29/30** are the only other nonzero columns. They are small non-negative counts/indices
  that vary per record: dual `off29` = 5..12, `off30` = 10..15 (with `off30 - off29` in {3,5,6,7});
  2g `off29` = 11..15 and `off30` **always 0x0f = 15**. `0x9080`-run lengths of 7 and the 2g `0x8080`
  runs of 6 suggest the scalars are per-record indices/counts, but that is not established.
* **Zero columns are structural** (constant 0 in 100 % of records), i.e. reserved/padding slots, not
  data that happens to be zero on this unit.

### 2.2 Header (marker arrays) - words before the pre-region

```
DUAL words 0..91 : 9080 x7 | a080 x7 | 0 x14 | 9080 x7 | a080 x7 | 0 x14 |
                   9090 9080x4 9090x2 9080x3 | a080 x4 | 0 x14 | 9080x3 a080 | 0 x4
2G   words 0..23 : 8080 x6 | 0 x6 | 8080 x6 | 0 x6
```

Census: dual header = 24 x `0x9080`, 19 x `0xa080`, 3 x `0x9090`, 46 x `0`; 2g header = 12 x `0x8080`,
12 x `0`. The `0x9080`/`0xa080`/`0x8080`/`0x9090` values are stored as a u16 in a u32 slot (high half
zero) and form fixed-length runs (7 in the dual, 6 in 2g) followed by an equal-count zero run. They
behave like per-band/per-chain marker arrays; the run lengths numerically parallel the band counts but
this is an observation, not a decode.

### 2.3 Pre-record data region (words 92..484 dual, 24..132 2g)

Exact-word autocorrelation inside the region peaks at **stride 4 words (16 B)** - dual
254/389 = 0.65, 2g 69/105 = 0.66 - plus its multiples (8, 12, 16, 20, 24 ...). The layout is
`[value][0][value][0]` repeated (about 47 % of the words are zero in both files). Most halves are
small signed 16-bit values (2g example run: `ffee ffdc ... 0030 0032 ... ffe0 ffe2 ... 0040 001c`),
but the dual region also contains a stretch of larger/fixed-point-looking words near its end
(words ~460-477, e.g. `6d000e07 7a000c06 82000a05`), so this region is **not** a single uniform table.

---

## 3. Cross-check against the live 26-word 2g and 18-word 5g tables

Live tables (phase-7 `firmware-ram-dump.md` §2.4), searched as little-endian u32 arrays:

```
2g_power_param(26) @ 0x1b2f24 = 17161605 17161605 17161605 17161505 17161505 15161401 ...
5g_power_param(18) @ 0x1b30ea = 00000000 0004ff00 0801000b ff000b00 0009fd02 ...
```

Searches performed on both files (found byte offsets via `bytes.find` for all 4-byte phases and both
u16 phases):

| search | `wifi_cali_data.kv` | `wifi_cali_data_2g.kv` |
|---|---|---|
| 26-word 2g table as u32 array, all 4 byte phases | 0 hits | 0 hits |
| 18-word 5g table as u32 array, all 4 byte phases | 0 hits | 0 hits |
| either table flattened to u16, all 2 byte phases | 0 hits | 0 hits |
| 2g table's 17 distinct u16 halves present as values | 0/17 | 0/17 |
| 5g table's 26 distinct u16 halves present as values | 7/26 (all trivial small ints) | 6/26 |

The 5g "hits" are `0x0000`, `0x0004`, `0x0016`, `0x001a`, `0x000b`, `0x09/0x0900` etc. - values that
occur everywhere in a file full of small signed numbers - and the first table word is `0x00000000`, so
this is a coincidence of small integers, not a match. No whole table word (`0x0004ff00`,
`0x0c030009`, `0x17161605`, ...) occurs at any offset in any byte order.

**No mapping is supported.** The store records are 38-word structures whose payload is 13-bit-signed
trim values; the live tables are packed 4-byte words (`0x17161605`, `0x0004ff00`) that are the
firmware's post-decoding result. Neither a literal copy nor a simple affine/byte/word-order transform
connects them: no table appears verbatim, no table u16 half appears, and a signed reading of the table
halves has no counterpart in the store either. The only claim the data supports is the causal direction
already in phase 7: the store is the driver's *input* struct, the WRAM tables are the firmware's
*output*; the transformation between them is not recoverable from these files alone.

---

## 4. The 2g-only file vs the dual file's extra 6,576 bytes

Size difference = 8,912 - 2,336 = **6,576 B = 1,644 words**. Decomposing by the §1.4 section model:

| section | dual words | 2g words | extra words | extra bytes |
|---|---:|---:|---:|---:|
| marker header | 92 | 24 | 68 | 272 |
| pre-record region | 393 | 109 | 284 | 1,136 |
| 38-word records | 1,710 (45) | 418 (11) | 1,292 (34 records) | 5,168 |
| final partial record | 31 | 31 | 0 | 0 |
| **total** | **2,226** | **582** | **1,644** | **6,576** |

So **the extra 6,576 bytes are not one repeated block and not padding**: they are a longer marker
header (+68 w), a longer pre-record region (+284 w) and 34 additional 38-word records (+1,292 w), with
an identical 31-word truncated record at the end.

**What the extra region repeats:** the same units as the rest of the file - the same `0x9080`/`0xa080`
marker runs (7-wide, vs the 2g file's 6-wide `0x8080`), the same 4-word-periodic `[value][0]` data
region, and the identical 38-word record template (§2: all 16 zero columns and the 5-word data groups
line up the same way in both files). What it does **not** repeat is the 2g file's content:

```
all 45 dual records distinct; all 11 2g records distinct
2g records that also occur as a dual record: 0 of 11
common prefix of the two payloads: 5 bytes (magic + first payload byte); first payload word
  differs (dual 0x00009080 vs 2g 0x00008080)
```

So the dual struct is a distinct, larger object that happens to use the same field/record grammar - it
is neither a superset nor a suffix of the 2g struct. The extra records carry different values, not a
copy of the 2g calibration.

---

## 5. Explicit limits

1. **No proven field *semantics*.** The template in §2 is a statistical description of a struct copied
   opaquely by the driver; `plat.ko` ships no DWARF and no struct definition, so offset 4 is "5 words
   of paired small signed values", not "gain trim" or any named field. Roles marked "candidate" are
   inferences from ranges and constancy only.
2. **The 38-word "stride" must not be read as a value period.** It is a record-layout period
   (§1.2): at stride 38 only 2.3 % (dual) / 5.5 % (2g) of nonzero words match. Any future use of
   "38-word stride" should carry that caveat.
3. **13-bit signedness is a hypothesis.** The data halves never exceed `0x1fff`, which is consistent
   with (but does not prove) two's-complement modulo `0x2000` giving about -80..+60; the pre-record
   region instead contains full 16-bit negatives (`0xffe0`), so the two regions may use different
   encodings.
4. **Record count is not a clean integer.** The struct ends 31 words into a record in both files
   (45.82 / 11.82 records). Whether that is a deliberate trailing 31-word object or a truncation of the
   record array by a fixed struct size is not resolved; the two are indistinguishable from the bytes.
5. **No mapping to the live tables is established** (§3): neither table, nor its u16 halves under sign
   conversion, occurs anywhere in either file, so any "store -> table" transform is unverified.
6. **No device access.** The live-table contents were taken from the phase-7 report, not re-read here;
   this analysis only searches the two local `.kv` files.
7. Nothing in this report claims what any field *does*; it establishes repetition structure, section
   boundaries, the record template, and the two negatives (no value period at 38, no table mapping).
