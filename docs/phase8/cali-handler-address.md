# Recovering the `smac_msg_proc_set_cali_ppdu_tx_num_req` handler by matching the tail name pool (phase 8, 2026-09-30)

Scope: local files, read-only, no device access. Input: `build/tmp/FIRMWARE.bin`
(928,920 B = `0xE2C98`). Tooling: `pyenv/Scripts/python.exe`, capstone 5.0.7
(`CS_ARCH_ARM`, `CS_MODE_THUMB`). Prior art: `ulw/phase8/firmware-cali-parser.md`
(shortlist item 4), `ulw/phase6/firmware-symbols.md`, `ulw/phase4/firmware-disasm.md`,
`ulw/phase7/firmware-callgraph.md`.

**Bias note.** Addresses here are **file offsets into FIRMWARE.bin** unless a bias is named.
The phase-4 symboltable convention is `file_offset = table_word − 0x40000` (odd word ⇒ Thumb);
that convention is exercised explicitly below and is *not* silently applied to the pool.

**Result in one line.** The 45-name tail pool (`0xE266C-0xE2BA3`) is **not referenced by any
pointer, index array, or computed base in the image**. There is no table whose entries resolve to
the pool offsets (± any of the biases tested), the pool is not lexicographically sorted, and no
instruction loads the calibration name's offset (`0xE2894`, `0x122894`, or `0xA2894`). The
calibration handler therefore **cannot** be reached by the name-pool route, and its address remains
**unresolved**. The scans that establish this, and one adjacent weak candidate, are below.

---

## 1. Full tail name-pool layout

The pool is 45 NUL-terminated ASCII strings, 4-byte aligned, each padded with 0x00 bytes to the
next 4-byte boundary. It starts at `0xE266C` and the last string ends (NUL at) `0xE2BA3`. Ordinal
is 0-based in pool order.

Scan (reproducible): find every `b'smac_'` and read to the following NUL.

```python
d=open('build/tmp/FIRMWARE.bin','rb').read()
i=0; rows=[]
while (i:=d.find(b'smac_',i))>=0:
    j=d.find(b'\x00',i); rows.append((i,d[i:j].decode())); i=j
print(len(rows))          # 45
```

| ord | file offset | len | string |
|---:|---|---:|---|
| 0 | `0xE266C` | 29 | `smac_coex_rx_abort_end_5g_isr` |
| 1 | `0xE268C` | 29 | `smac_coex_rx_abort_end_2g_isr` |
| 2 | `0xE26AC` | 23 | `smac_rx_complete_5g_isr` |
| 3 | `0xE26C4` | 23 | `smac_rx_complete_2g_isr` |
| 4 | `0xE26DC` | 23 | `smac_tx_complete_5g_isr` |
| 5 | `0xE26F4` | 23 | `smac_tx_complete_2g_isr` |
| 6 | `0xE270C` | 24 | `smac_common_timer_5g_isr` |
| 7 | `0xE2728` | 24 | `smac_common_timer_2g_isr` |
| 8 | `0xE2744` | 37 | `smac_coex_pta_rx_abort_timeout_5g_isr` |
| 9 | `0xE276C` | 37 | `smac_coex_pta_rx_abort_timeout_2g_isr` |
| 10 | `0xE2794` | 29 | `smac_coex_tx_abort_end_5g_isr` |
| 11 | `0xE27B4` | 29 | `smac_coex_tx_abort_end_2g_isr` |
| 12 | `0xE27D4` | 37 | `smac_coex_pta_tx_abort_timeout_5g_isr` |
| 13 | `0xE27FC` | 37 | `smac_coex_pta_tx_abort_timeout_2g_isr` |
| 14 | `0xE2824` | 35 | `smac_msg_proc_set_mu_edca_timer_req` |
| 15 | `0xE2848` | 28 | `smac_msg_proc_ac_suspend_req` |
| 16 | `0xE2868` | 42 | `smac_msg_proc_set_cali_self_cts_tx_num_req` |
| **17** | **`0xE2894`** | **38** | **`smac_msg_proc_set_cali_ppdu_tx_num_req`** |
| 18 | `0xE28BC` | 31 | `smac_msg_proc_rx_ring_reset_req` |
| 19 | `0xE28DC` | 39 | `smac_msg_proc_update_tx_bypass_ctrl_req` |
| 20 | `0xE2904` | 34 | `smac_msg_proc_beacon_tx_resume_req` |
| 21 | `0xE2928` | 35 | `smac_msg_proc_beacon_tx_suspend_req` |
| 22 | `0xE294C` | 29 | `smac_msg_proc_clr_hw_fifo_req` |
| 23 | `0xE296C` | 27 | `smac_msg_proc_tx_resume_req` |
| 24 | `0xE2988` | 28 | `smac_msg_proc_tx_suspend_req` |
| 25 | `0xE29A8` | 28 | `smac_msg_proc_enable_trx_req` |
| 26 | `0xE29C8` | 29 | `smac_msg_proc_disable_trx_req` |
| 27 | `0xE29E8` | 16 | `smac_tbtt_5g_isr` |
| 28 | `0xE29FC` | 16 | `smac_tbtt_2g_isr` |
| 29 | `0xE2A10` | 22 | `smac_rx_timeout_5g_isr` |
| 30 | `0xE2A28` | 22 | `smac_rx_timeout_2g_isr` |
| 31 | `0xE2A44` | 18 | `smac_pre_tx_5g_isr` |
| 32 | `0xE2A58` | 18 | `smac_pre_tx_2g_isr` |
| 33 | `0xE2A6C` | 27 | `smac_backoff_timeout_5g_isr` |
| 34 | `0xE2A88` | 27 | `smac_backoff_timeout_2g_isr` |
| 35 | `0xE2AA4` | 28 | `smac_user_queue_empty_5g_isr` |
| 36 | `0xE2AC4` | 28 | `smac_user_queue_empty_2g_isr` |
| 37 | `0xE2AE4` | 19 | `smac_user_ps_5g_isr` |
| 38 | `0xE2AF8` | 19 | `smac_user_ps_2g_isr` |
| 39 | `0xE2B0C` | 26 | `smac_tx_abort_start_5g_isr` |
| 40 | `0xE2B28` | 26 | `smac_tx_abort_start_2g_isr` |
| 41 | `0xE2B44` | 27 | `smac_lifetime_expire_5g_isr` |
| 42 | `0xE2B60` | 27 | `smac_lifetime_expire_2g_isr` |
| 43 | `0xE2B7C` | 19 | `smac_tx_exit_5g_isr` |
| 44 | `0xE2B90` | 19 | `smac_tx_exit_2g_isr` |

Structure of the ordering:

- **ordinals 0-13**: ISR names (7 `5g`/`2g` pairs, `coex`/`complete`/`timer`).
- **ordinals 14-26**: 13 `smac_msg_proc_*_req` message-processor names (the calibration name is
  ordinal 17, the second of the two `cali_*_tx_num` names).
- **ordinals 27-44**: 18 more ISR names (9 `5g`/`2g` pairs).

The order is **not** lexicographic (it is ISR-block / msg_proc-block / ISR-block), so no
binary-search-on-name table can be feeding this pool; any linkage must be positional (ordinal).

The calibration string is unique in the image (`d.find` = `0xE2894`, next occurrence none;
the substring `cali_ppdu_tx_num` also occurs only at `0xE28A6` inside it).

**Irregularity inside the pool.** Between ordinal 30 and 31, the 4 bytes at `0xE2A40`
(`00 01 02 03`) are not the usual NUL padding: `smac_rx_timeout_2g_isr` ends (NUL) at `0xE2A3E`,
`0xE2A3F` is `00`, and `0xE2A40-0xE2A43` = `00 01 02 03` before `smac_pre_tx_5g_isr` at
`0xE2A44`. So the pool is contiguous *as text*, but at least one inter-string gap holds four
non-zero bytes. This is noted for completeness; it is too small to be an index block.

---

## 2. Candidate indexing structures for the pool

### 2.1 Direct pointer search (the primary hypothesis) — NEGATIVE

Covering 4-byte little-endian words at every byte position (aligned and unaligned), for each of the
45 string offsets `S`, for each bias `b ∈ {0, 0x10000, 0x20000, 0x40000, 0x80000, 0xC0000,
0xE0000}` (`0x40000` is the phase-4 symbol-table bias), tested `W − b == S`.

```python
import struct
d=open('build/tmp/FIRMWARE.bin','rb').read(); L=len(d)
starts={...}                       # the 45 offsets in §1
for o in range(0,L-3):
    w=struct.unpack_from('<I',d,o)[0]
    for b in (0,0x10000,0x20000,0x40000,0x80000,0xC0000,0xE0000):
        if w-b in starts: print(hex(o),hex(w),hex(b))
```

Result: **exactly one hit**, at file `0xD7EF9`, `W=0x001A2B0C`, bias `0xC0000` → `0xE2B0C`
(ordinal 39). `0xD7EF9` is **odd**, i.e. mid-stream bytes inside the second Thumb-2 code block, not
a table slot; the coincidence is an instruction-byte artifact. **No 4-byte word in the file is a
pointer to any pool string, raw or with the `+0x40000` bias.**

### 2.2 Pool-range pointer search under three biases — no name pointer

Tested every 4-byte word `W` for `W−b ∈ [0xE266C, 0xE2C98]`, `b ∈ {0, 0x40000, −0x40000}`.
Counts: bias `0` → 2 hits (aligned words `0x1BDE4 = 0xE2889` and `0x1BDF0 = 0xE27A5`, both
inside main-block code), bias `+0x40000` → 0, bias `−0x40000` → 6 hits (aligned
`0xD096C = 0xA2C00`, plus five **unaligned** mid-instruction byte sequences at
`0xC4687`, `0xC468F`, `0xC4697`, `0xC469F`, `0xC8C7B`). **None of the eight lands on a string
start**: `0xE2889` and `0xE27A5` fall *inside* string bodies (ordinals 16 and 10), `0xE2C00` falls
in the post-pool data after the last NUL, and the five unaligned ones are not table slots. So no
base+offset pointer resolves to a name — they are coincidental code/data bytes.

### 2.3 Literal words for the calibration offset and for the pool base — NEGATIVE

| word searched | meaning | occurrences |
|---|---|---|
| `0xE2894` | raw file offset of cali name | **0** |
| `0xA2894` | `0xE2894 − 0x40000` (task's suggested alternative) | **0** |
| `0x122894` | `0xE2894 + 0x40000` (firmware's own runtime bias) | **0** |
| `0xE2893` / `0xE2895` | Thumb-tagged variants | 0 / 0 |
| `0xE266C` / `0x12266C` / `0x2266C` | pool base, three biases | 0 / 0 / 0 |
| `0xE2000` / `0xE2B90` / `0x122B90` | tail start, pool end, `<end>+0x40000` | 0 / 0 / 0 |

### 2.4 Pool-relative delta arrays (index arrays) — NEGATIVE

If a table stored pool-relative offsets, it would contain the delta sequence
`0x0,0x20,0x40,0x58,0x70,0x88,0xA0,0xBC,0xD8,0x100,…,0x524`. Scanned for consecutive-delta runs
anywhere in the file: the longest u32 match of the delta prefix is **2 words**, at `0xCCE3C`
(`0x00000000, 0x00000020` — trivially common values), the third delta fails there; there is no
u16 run of the first three deltas (`0,0x20,0x40`); and no run of ≥5 u32 deltas exists. So there is
no pool offset/index array.

### 2.5 `movw`/`movt` construction of any pool address — NEGATIVE

Linear Thumb disassembly of both code regions `(0x0,0xC0000)` and `(0xD8000,0xE2000)`, tracking
`movw`+`movt` register pairs, found **no** pair building an address in `[0xE266C,0xE2C98]`
(pool), `[0x12266C,0x122C98]` (`+0x40000`), or `[0xA266C,0xA2C98]` (`−0x40000`). Likewise no
single `mov`/`movw` immediate equal to `0x228` (pool-relative cali offset), `0xA2894`, `0xE2894` or
`0xE266C`. So no code computes the pool base.

### 2.6 Adjacent structures actually present (evaluated, none is a name index)

These are the only structures found bordering or near the pool; each is described with the scan
that produced it (hex dump of the region + word/delta interpretation above).

1. **64-byte 0/1 flag array at `0xE262C`** (immediately before the pool; preceded by the single
   word `0x20` at `0xE2628`). Bytes `0xE262C-0xE266B` are all 0/1:
   `0101…01 0000 0001 0101 01 0000 0000 0101 0101 0001 0101 0000 0000 0100 0000 0101 0100 …`.
   Reading it as a 45-entry ordinal-aligned array (first 45 bytes, `0xE262C`+) gives 14 of the 16
   `5g`/`2g` adjacent pairs equal — but the file's runs of identical bits make that a weak signal,
   and, critically, **the array holds only 0/1 flags, never an address**. It cannot yield the
   handler. It is as consistent with a 64-slot interrupt-source bitmap as with pool ordinals.
2. **9-record × 16-byte table at `0xE2BAC`** (after the pool; the last string's padding runs to
   `0xE2BAC`). Records: `{0x3F,0x1008,1,0}`, `{0x1F,0x0508,2,0}`, `{0x1D,0x1108,3,0}`,
   `{0x8027,0x1108,4,0x02100000}`, `{0x8027,0x1108,5,0x40000000}`, `{0x23,0x1110,6,0x40040000}`,
   `{0x23,0x1110,7,0x40080000}`, `{0x25,0x1110,8,0x40100000}`, `{0x21,0x1110,9,0x40180000}`.
   The third word is a 1..9 sequence, the second is a small register offset (`0x1008/0x0508/0x1108/
   0x1110`), the fourth a `0x40000000`-based address — register/MMIO descriptors, **no name
   pointer and no pool-relative offset**.
3. **Byte array at `0xE2C3C`** (0x04/0x10/0x01 pattern) and the **14-word growth sequence at
   `0xE2C60`** `0,1,3,6,13,26,52,104,209,419,838,1677,3355,6710` (file end). Neither references
   the pool.
4. **Two 32-entry Thumb code-pointer tables elsewhere** (found by scanning for maximal runs of
   words that are Thumb pointers under the `−0x40000` bias):
   - `0xC5ECC` — 32 descending entries resolving to file `0x9595A,0x9593A,…` in the main block;
     it sits directly after the radar name strings (`octo_filter_enable`, `detect_check`,
     `get_detect_check_info`, `radar_phy_enable` at `0xC5E80-0xC5EC8`). A radar/DFS dispatch table.
   - `0xCB058` — 32 entries resolving to `0xA6D00,0xA6CBC,…` (plus `0xAB438`, `0xAB3B8`).
   Neither has any field equal to a pool offset or a `0x122xxx` value (a fortiori, §2.1/2.3), and
   their length (32) does not match the 45-name pool. They index *code*, not this name list; any
   relation to the pool could only be by an ordinal convention, which nothing in the image
   evidences.

### 2.7 Verdict on part 2

**No candidate indexing structure feeds the tail name pool.** The only pool-adjacent index-like
object is the `0xE262C` 0/1 flag array, which carries no addresses. The pool is unreferenced by the
firmware image under every pointer/bias/index scheme tested.

---

## 3. Table entry and handler address

**No table was found**, so the ordinal route yields **no address** for
`smac_msg_proc_set_cali_ppdu_tx_num_req` (ordinal 17). There is consequently no entry to quote and
no handler address to disassemble. This confirms and strengthens `firmware-cali-parser.md` §4.1:
the earlier pass showed no pointer to this one name; this pass shows there is no pointer to *any*
of the 45 pool names, and no index/offset array for the pool either, so the pool as a whole is
name-only.

---

## 4. Alternative route: loads of the string offset / a pool base + index

Per the task's fallback, the code was searched for loads of the offset itself and of a nearby pool
base plus an index.

- **Literal `0xA2894`** (`0xE2894 − 0x40000`, the exact value named in the task): **0** occurrences
  as a 4-byte word.
- **Literal `0xE2894`** (raw file offset): **0** occurrences.
- **Literal `0x122894`** (`+0x40000`): **0** occurrences.
- **`movw`/`movt`** building any address in `[0xA266C,0xA2C98]`, `[0xE266C,0xE2C98]`,
  `[0x12266C,0x122C98]`: **none** (§2.5).
- **Pool base word** `0xE266C`/`0x12266C`/`0x2266C`: **0** occurrences anywhere (§2.3).

So there is nothing to substantiate: **no instruction or table load uses the calibration string's
offset, the pool base, or a base-plus-index form of it.** The pool is not consumed by code in this
image through any direct or computed reference that the scans can see.

**Substantiated but inconclusive lead** (not a name-pool result): the second code block contains
three message-opcode dispatchers that read an opcode byte from a message buffer and branch —
`0xDA0FC` (`ldrb r6,[r4]; ldrb r5,[r4,#1]; cmp r5,#0x1E; cmpls r6,#0x31`),
`0xDB5EC` (`ldrb r2,[r0,#1]; cmp r2,#0x34`), and `0xDC378`
(`ldrb r5,[r0]; ldrb r2,[r0,#1]; cmp r2,#0x32 / #0x33`). These are genuine SMAC message-opcode
dispatches and are the most plausible place a cali-message handler is invoked. But there is no
mapping in the image from the name (or from a known cali opcode) to one of them, so this does
**not** yield the handler address; it is recorded as the next best place to look.

---

## 5. Explicit limits

1. **Negative, not proved unreachable.** The result is "no reference found by the scans below."
   References that do not manifest as a 4-byte literal, a table word, or a `movw`/`movt` pair —
   e.g. a base loaded at runtime from a pointer-capable register/MMIO and merely offset by a small
   immediate — would be invisible to these scans. Such a scheme was not found and is not claimed.
2. **Bias set is finite.** Pointer tests used biases `{0, ±0x40000, 0x10000, 0x20000, 0x80000,
   0xC0000, 0xE0000}`; a pointer could exist under some other bias. The most likely biases (file
   offset, and the firmware's own `+0x40000`) are covered and are empty.
3. **The `0xE262C` 0/1 array is a weak candidate.** Its size (64 bytes) does not equal the pool's 45
   entries, it contains no addresses, and its apparent alignment with the names rests on the
   file's long runs of identical bits. It is reported as an object of interest, not as an index.
4. **The 32-entry tables (§2.6.4) were not disassembled entry-by-entry.** Whether their order
   coincidentally matches any sub-list of the pool is not established; nothing links them to the
   names.
5. **No host-driver cross-check in this pass.** The driver module (`hi5622v100_wifi.ko`) and the
   wire-protocol docs were not consulted for a firmware command id that could be matched to an
   opcode in `0xDA0FC`/`0xDB5EC`/`0xDC378`; that is the recommended next step and is outside this
   task's stated inputs.
6. **Function/dispatch identification elsewhere is heuristic** (inherited from the prior passes):
   the three opcode dispatchers were identified by `cmp #0x1E..0x34` on `ldrb [rN,#1]`, which does
   not by itself prove they are the cali path.

---

## Verification

Every claim above is backed by a scan run in this pass (`pyenv/Scripts/python.exe`, file
`build/tmp/FIRMWARE.bin`):

* **§1 (pool layout).** `d.find(b'smac_')` walk → 45 strings; the table's offsets/lengths are the
  raw run boundaries. Cali string `d.find(...)` = `0xE2894` (single occurrence); text region ends
  `0xE2BA3`; the `0xE2A40` = `00 01 02 03` irregularity is a direct hex read.
* **§2.1.** All-byte-position word scan, 7 biases, `W − b ∈ starts` → 1 hit at unaligned `0xD7EF9`.
* **§2.2.** All-word `W − b ∈ [0xE266C,0xE2C98]`, `b ∈ {0, +0x40000, −0x40000}` → 2 / 0 / 6 hits
  respectively, each enumerated and shown not to land on a string start.
* **§2.3.** Exact word counts for `0xE2894`, `0xA2894`, `0x122894`, `0xE266C`, `0x12266C`,
  `0x2266C`, `0xE2000`, `0xE2B90`, `0x122B90`, and Thumb-tagged variants → all 0.
* **§2.4.** Consecutive-delta run search against the derived delta sequence → longest u32 prefix
  run = 2 (`0xCCE3C`), no u16 run of the first three deltas, no u32 run ≥5.
* **§2.5.** Thumb linear disassembly of both code regions with `movw`/`movt` pair tracking → no
  address in the three pool ranges; no `mov`/`movw` immediate in `{0x228, 0xA2894, 0xE2894,
  0xE266C}`.
* **§2.6.** Hex dumps of `0xE2610-0xE266C`, `0xE2B9C-0xE2BB4`, `0xC5E80-0xC5F80`, `0xCB000-0xCB180`;
  run scan for maximal Thumb-pointer runs (`−0x40000` bias) locating `0xC5ECC` (32) and `0xCB058`
  (32); the `0xE2C60` growth sequence and `0xE2BAC` 16-byte records read directly.
* **§4.** Same scans as §2.3/§2.5 plus the opcode-dispatcher excerpts quoted from
  `firmware-cali-parser.md` §3.2 (re-derived from the raw windows there).

**Bottom line.** The calibration-message handler address is **not recoverable** from the tail name
pool: the pool has no pointer table, no offset/index array, and no computed-base reference in the
image. The strongest remaining lead is to map a driver command id to one of the three firmware
message-opcode dispatchers (`0xDA0FC`, `0xDB5EC`, `0xDC378`), which was out of scope here.
