# Phase 8 - `.kv` calibration-store layout, decoded from the driver's writer functions

Scope: local files only, read-only. Inputs: `build/tmp/hi5622v100_wifi.ko` (ARM32, full symtab),
`build/tmp/hi5622v100_plat.ko` (the module that actually defines the buffer and the file readers),
`build/tmp/wifi_cali_data.kv` (8,912 B), `build/tmp/wifi_cali_data_2g.kv` (2,336 B), and the phase-7
context (`ulw/phase7/firmware-ram-dump.md`, `ulw/phase7/calibration-store.md`).
Tooling: `pyenv/Scripts/python.exe` + pyelftools + capstone; helper script `build/tmp/phase8_dis.py`.

**Headline result.** The writers do **not** pack fields. Each one takes a raw pointer to a persistent
in-RAM calibration struct and issues **one** `oal_kernel_file_write` of the whole struct. The only
offset-specific logic is in the `verify_*` helpers, which check a magic `0xfeedfeed` at offset 0 and at
`len-4`, and *replace* both with `0x5a5a5a5a` / `0xa5a5a5a5` before the write. That substitution is the
entire origin of the on-disk `ZZZZ` ... `a5a5a5a5` container.

---

## 1. The writer functions and their helpers

All addresses are link-time offsets into `.text` (section vaddr 0, so symbol value == disassembly
address). Sizes are `st_size` from the symtab. All five functions live at a `$a` mapping symbol, i.e.
they are ARM (A32), not Thumb; capstone A32 decodes them cleanly.

| function | value | size | file |
|---|---:|---:|---|
| `hmac_save_cali_data_to_file` | `0xc4044` | 36 | wifi.ko |
| `hmac_save_cali_data_to_file_2g` | `0xc3f4c` | 248 | wifi.ko |
| `hmac_save_cali_data_to_file_5g` | `0xc3e54` | 248 | wifi.ko |
| `hmac_save_cali_data_verify_2g` | `0xc3d3c` | 280 | wifi.ko |
| `hmac_save_cali_data_verify_5g` | `0xc3c20` | 284 | wifi.ko |
| `hmac_rf_cali_host_addr` | `0x14d4` | 16 | plat.ko |
| `hmac_rf_cali_host_addr_2g` | `0x14e4` | 16 | plat.ko |
| `hmac_rf_cali_data_file_name` | `0x14f4` | 12 | plat.ko |
| `hmac_rf_cali_data_file_name_2g` | `0x1500` | 12 | plat.ko |
| `hwifi_rf_cali_file_load_5g` (reader, for cross-check) | `0x2e44` | 444 | plat.ko |

Callers (from `.rel.text` relocation sites):

* `hmac_save_cali_data_to_file` is called from `hmac_main_init` (`0x3f6c8`, call site `0x3f86c`).
* `hmac_save_cali_data_to_file_5g` and `_2g` are called from `alg_eqment_config_param_output_entry`
  (`0x87b38`, sites `0x87bc4` and `0x87d00`).

### 1.1 `hmac_save_cali_data_to_file` = `0xc4044` (36 B) - the dispatcher

```
000c4044: 10402de9   push {r4, lr}
000c4048: 0000a0e3   mov r0, #0
000c404c: feffffeb   bl  hmac_save_cali_data_to_file_2g      ; RELOC hmac_save_cali_data_to_file_2g
000c4050: 000000e3   movw r0, #0                            ; RELOC .LC15 = "saving cali_data.kv file \n"
000c4058: feffffeb   bl  printk
000c405c: 0000a0e3   mov r0, #0
000c4060: 1040bde8   pop {r4, lr}
000c4064: feffffea   b   hmac_save_cali_data_to_file_5g      ; RELOC hmac_save_cali_data_to_file_5g
```

It calls the 2g writer with argument `0`, prints `saving cali_data.kv file`, then tail-calls the 5g
writer with argument `0`. The argument is the "chip id / handle" that `verify_*` forwards to
`get_chip_type` (see 1.4).

### 1.2 `hmac_save_cali_data_to_file_5g` = `0xc3e54` (248 B)

```
000c3e54: f0412de9   push {r4, r5, r6, r7, r8, lr}
000c3e58: 0040a0e1   mov r4, r0                              ; r4 = arg
000c3e5c: feffffeb   bl  hmac_rf_cali_host_addr              ; r0 = *(void**)(.LANCHOR0+0x120)
000c3e60: 005050e2   subs r5, r0, #0
000c3e64: 2e00000a   beq 0xc3f24                             ; null -> printk .LC7 "pst_buf is null, vmalloc failed"
000c3e68: 0410a0e1   mov r1, r4
000c3e6c: feffffeb   bl  hmac_save_cali_data_verify_5g       ; verify(addr, arg) - mutates the magic
000c3e70: 004050e2   subs r4, r0, #0
000c3e74: f081bd18   popne {r4, r5, r6, r7, r8, pc}          ; verify != 0 -> abort
000c3e78: feffffeb   bl  hmac_rf_cali_data_file_name         ; r0 = "/usr/local/factory/wifi_cali_data.kv"
000c3e7c: 6d2fa0e3   mov r2, #0x1b4                          ; mode 0664
000c3e80: 4210a0e3   mov r1, #0x42                           ; O_RDWR|O_CREAT
000c3e84: 0070a0e1   mov r7, r0
000c3e88: feffffeb   bl  filp_open                           ; file = filp_open(name, 0x42, 0x1b4)
000c3e8c: 006050e2   subs r6, r0, #0
000c3e90: 2800000a   beq 0xc3f38                             ; NULL/ERR -> printk .LC10 "create file error,fp NULL, filename is [%s]"
000c3e94: 010a76e3   cmn r6, #0x1000
000c3e98: 2600008a   bhi 0xc3f38
...
000c3eb8: d02202e3   movw r2, #0x22d0                         ; COUNT = 8912
000c3ebc: 0510a0e1   mov r1, r5                              ; BUF  = cali host addr
000c3ec0: 0030a0e3   mov r3, #0                              ; POS  = 0
000c3ec4: feffffeb   bl  oal_kernel_file_write               ; write(file, buf, 0x22d0, 0)
000c3ec8: 000050e3   cmp r0, #0
000c3ecc: 040000aa   bge 0xc3ee4
000c3ed0: ... printk .LC8  "save cali_data.kv file err: len:%u save_path:%s done"
000c3ee4: ... printk .LC9  "save cali_data.kv file done"
000c3ef8: feffffeb   bl  vfs_fsync
000c3f1c: f041bde8   pop {r4, r5, r6, r7, r8, lr}
000c3f20: feffffea   b   filp_close
```

Write parameters, field by field:

| what | register | value |
|---|---|---|
| path | r0 | `hmac_rf_cali_data_file_name()` -> `/usr/local/factory/wifi_cali_data.kv` |
| flags | r1 | `0x42` (O_RDWR\|O_CREAT) |
| mode | r2 | `0x1b4` (0664) |
| **source buffer** | r1 of write | `hmac_rf_cali_host_addr()` - a pointer held in `.bss` |
| **count** | r2 of write | `0x22d0` = **8912** |
| file position | r3 | 0 |
| header field written by the writer itself | - | none; see 1.4 |

### 1.3 `hmac_save_cali_data_to_file_2g` = `0xc3f4c` (248 B)

Byte-for-byte the same shape, with the 2g accessors and the 2g count:

```
000c3f54: feffffeb   bl  hmac_rf_cali_host_addr_2g           ; r0 = *(void**)(.LANCHOR0+0x124)
000c3f64: feffffeb   bl  hmac_save_cali_data_verify_2g
000c3f70: feffffeb   bl  hmac_rf_cali_data_file_name_2g     ; "/usr/local/factory/wifi_cali_data_2g.kv"
000c3f74: 6d2fa0e3   mov r2, #0x1b4
000c3f78: 4210a0e3   mov r1, #0x42
000c3f80: feffffeb   bl  filp_open
...
000c3fb0: 922ea0e3   movw r2, #0x920                          ; COUNT = 2336
000c3fb4: 0510a0e1   mov r1, r5
000c3fb8: 0030a0e3   mov r3, #0
000c3fbc: feffffeb   bl  oal_kernel_file_write
...
000c3fe4: ... printk .LC13 "save cali_data_2g.kv file done"
000c3ff0: feffffeb   bl  vfs_fsync
000c4018: feffffea   b   filp_close
```

Error strings: `.LC11` (`"2g savemem pst_buf is null, vmalloc size %u failed!"`), `.LC12`
(`"2g save cali_data.kv file err: len:%u save_path:%s done"`), `.LC14`
(`"2g create file error,fp NULL, filename is [%s]"`).

**Accessor evidence** (plat.ko): `hmac_rf_cali_host_addr` is `movw/movt r3, .LANCHOR0+0x120; ldr r0,[r3]`
- i.e. it returns the *pointer stored* in a global slot. `hmac_rf_cali_host_addr_2g` reads the next word
(`ldr r0,[r3,#4]`). `hmac_rf_cali_data_dma_addr`/`_2g` read `+8`/`+0xc`. `hmac_rf_cali_data_file_name`
returns the address of the `.data` string at vaddr `0xac`
(`'/usr/local/factory/wifi_cali_data.kv'`); `_2g` returns `.data` vaddr `0x12c`
(`'/usr/local/factory/wifi_cali_data_2g.kv'`). The same `.LANCHOR0+0x120` slot is where
`hwifi_rf_cali_file_load_5g` stores the buffer it allocates when it reads the file (see 2.3), so the
write buffer and the read buffer are the same global.

### 1.4 The magic helpers `hmac_save_cali_data_verify_{2g,5g}` - where `ZZZZ`/`a5a5a5a5` come from

These are the only functions that touch a specific offset. **5g** (`0xc3c20`, 284 B):

```
000c3c5c: 023a84e2   add r3, r4, #0x2000            ; r4 = buffer
000c3c60: 001094e5   ldr r1, [r4]                   ; word at offset 0
000c3c64: ed2e0fe3   movw r2, #0xfeed
000c3c68: ed2e4fe3   movt r2, #0xfeed               ; r2 = 0xfeedfeed
000c3c6c: 020051e1   cmp r1, r2
000c3c70: cc2293e5   ldr r2, [r3, #0x2cc]           ; word at offset 0x2000+0x2cc = 0x22cc
000c3c74: 1000001a   bne 0xc3cbc
000c3c78: 010052e1   cmp r2, r1                     ; must also equal 0xfeedfeed
000c3c7c: 1200001a   bne 0xc3ccc
000c3c80: 5a1a05e3   movw r1, #0x5a5a
000c3c84: 5a1a45e3   movt r1, #0x5a5a               ; 0x5a5a5a5a
000c3c88: a5250ae3   movw r2, #0xa5a5
000c3c8c: a5254ae3   movt r2, #0xa5a5               ; 0xa5a5a5a5
000c3c90: 001084e5   str r1, [r4]                   ; buffer[0]        = 0x5a5a5a5a
000c3c94: cc2283e5   str r2, [r3, #0x2cc]           ; buffer[0x22cc]   = 0xa5a5a5a5
```

**2g** (`0xc3d3c`, 280 B) is identical except the tail offset is `0x91c`:

```
000c3d78: 001094e5   ldr r1, [r4]
000c3d7c: ed3e0fe3   movw r3, #0xfeed
000c3d80: ed3e4fe3   movt r3, #0xfeed               ; 0xfeedfeed
000c3d84: 1c2994e5   ldr r2, [r4, #0x91c]
000c3d88: 030051e1   cmp r1, r3
000c3d98: 5a2a05e3   movw r2, #0x5a5a
000c3d9c: 5a2a45e3   movt r2, #0x5a5a
000c3da0: a5350ae3   movw r3, #0xa5a5
000c3da4: a5354ae3   movt r3, #0xa5a5
000c3da8: 002084e5   str r2, [r4]                   ; buffer[0]      = 0x5a5a5a5a
000c3dac: 1c3984e5   str r3, [r4, #0x91c]           ; buffer[0x91c]  = 0xa5a5a5a5
```

So in **RAM** the struct is bracketed by `0xfeedfeed` at `+0` and `+len-4`; on **disk** those two words
become `0x5a5a5a5a` ("ZZZZ") and `0xa5a5a5a5`. If the buffer already holds `0x5a5a5a5a`/`0xa5a5a5a5`,
the function refuses with `-22` and prints `.LC1`/`.LC4` ("... succ") - i.e. each buffer can be saved
once per boot. `get_chip_type` failing (`!= 0`) refuses with `.LC0` ("... FAIL! chip_id = %d").

---

## 2. `.kv` on disk <-> the write, with offsets

The container is a *verbatim image* of the driver struct (`oal_kernel_file_write` count == file size,
buffer == struct base, position 0). Therefore the on-disk field offsets are exactly the struct offsets.

| disk offset | size | content | produced by |
|---|---|---|---|
| `+0x0000` | 4 | `0x5a5a5a5a` ("ZZZZ") | `verify_*` rewrites RAM `0xfeedfeed` at `+0` |
| `+0x0004` .. `len-8` | `len-8` | the driver's calibration struct payload, byte-for-byte as it sits in RAM | the single `oal_kernel_file_write` |
| `len-4` (dual `0x22cc`, 2g `0x91c`) | 4 | `0xa5a5a5a5` trailer | `verify_*` rewrites RAM `0xfeedfeed` at `len-4` |

* dual file `len` = `0x22d0` = 8912 -> payload `0x0004..0x22c7` (8900 B), trailer `0x22cc`.
* 2g file `len` = `0x920` = 2336 -> payload `0x0004..0x917` (2324 B), trailer `0x91c`.

Byte-level confirmation (read of the actual files):

```
dual head 5a5a5a5a 80900000 80900000 80900000 80900000 80900000 80900000 80900000
dual tail 00000000 07000000 0c000000 a5a5a5a5
2g   head 5a5a5a5a 80800000 80800000 80800000 80800000 80800000 80800000 00000000
2g   tail 00000000 0b000000 0f000000 a5a5a5a5
  -> word[0] = 0x5a5a5a5a and word[last] = 0xa5a5a5a5 in both files, exactly matching the writes.
  -> 0xfeedfeed occurs 0 times in either file (d.count(pack('<I',0xfeedfeed)) == 0): the RAM magic is
     fully overwritten before the write.
  -> byte-zero fraction: dual 0.611, 2g 0.604 (the "~60 % zeros" seen earlier);
     word-zero fraction: dual 0.434, 2g 0.432.
     dual = 2228 words (1262 nonzero), 2g = 584 words (332 nonzero).
```

### 2.1 Observed payload structure (not writer-driven - reported as observation)

The writer copies an opaque struct, so the payload layout comes from the struct, not from a packer.
What the bytes themselves show:

* dual: the first 15 words are `00009080` x7 then `0000a080` x7 (`+0x04..0x38`); the pattern repeats at
  `+0x74` (14 words), again at `+0xe4`, and a 4-word tail at `+0x154`. `0x9080`/`0xa080` are two
  little-endian u16s (`0x9080`, `0xa080`); they look like per-band/tag marker arrays.
* 2g: the same idea with `00008080` x6 at `+0x04..0x18` and again at `+0x34..0x48`, then a body of
  u16-pair values (`ffeeffee`, `00300030`, `1ff*` signed-looking values, ...).
* Both payloads are periodic. Autocorrelation of the nonzero-word mask peaks at a **38-word (152-byte)
  stride** for both files (dual 1199 matched pairs, 2g 293). The 2g body from `~0x1e4` to `~0x914`
  additionally shows a `0x24`-byte (9-word) sub-stride of 5-word groups; the dual body likewise has
  long stretches of 5-word groups.
* These offsets are *observations of periodicity*, not decoded field names; without the struct
  definition (the .ko ships no DWARF) the per-field semantics are not established - see 5.

### 2.2 The count

`0x22d0` (dual) and `0x920` (2g) are the literal counts in the write calls, and they equal the two file
sizes exactly. There is no separate length/header word in the file: the reader is compiled with the
same constant.

### 2.3 Reader cross-check (plat.ko `hwifi_rf_cali_file_load_5g`, `0x2e44`)

The reader independently confirms the same head/tail offsets:

```
00002e64: d00202e3   movw r0, #0x22d0                  ; alloc 0x22d0 bytes
00002e9c: d03202e3   movw r3, #0x22d0
00002ea4: bl memset_s
00002ecc: bl filp_open  ; .LANCHOR1 = "/usr/local/factory/wifi_cali_data.kv"
00002ef8: 0022d0 (movw r2,#0x22d0)
00002efc: bl kernel_read                           ; reads exactly 0x22d0 bytes
00002f00: d03202e3   movw r3, #0x22d0
00002f04: cmp r0, r3                               ; short read -> "oal_file_read fail"
00002f18: ldr r1, [r3]                             ; r3 = buffer
00002f1c: cmp r1, #0x5a5a5a5a                      ; [0]      must be ZZZZ
00002f28: ldr r2, [r3, #0x2cc]                     ; [0x22cc] must be a5a5a5a5
00002f34: cmp r2, #0xa5a5a5a5
00002f3c: ... printk .LC54 "file broken start =%x end =%x!"
```

The matching `hwifi_rf_cali_file_load_2g` (`0x2c94`) is the 0x920/0x91c twin.

---

## 3. The 2g file (2,336 B) vs the dual file (8,912 B)

| | 2g | dual | difference |
|---|---:|---:|---:|
| write count in code | `0x920` = 2336 | `0x22d0` = 8912 | `0x19b0` = **6576** |
| file size (verified) | 2336 | 8912 | 6576 |
| payload bytes (`len-8`) | 2328 | 8904 | 6576 |
| trailer offset | `0x91c` | `0x22cc` | - |
| buffer global | `.LANCHOR0+0x124` | `.LANCHOR0+0x120` | separate slots |

What the 6576-byte difference is made of: **the extra 5g/dual part of the calibration struct**. Each
writer writes its own whole struct from its own pointer (2g from the `+4` slot, dual from the `+0` slot),
so the difference is exactly the additional fields the dual-band struct carries, copied contiguously -
not padding, not a repeated block, and not a prefix relationship. Byte comparison proves the two images
are different objects rather than one nested in the other:

```
longest common prefix of 2g.kv and .kv : 5 bytes of 2336 (0.2 %)   # magic + first payload byte
first payload word: 2g = 0x00008080, dual = 0x00009080
2g file bytes found inside dual file (and vice versa): none
```

The 2g image is a *different layout*, not a truncation of the dual one; the 5g half of the dual payload
(roughly `+0x174` onward, where the dual's marker-array header ends) has no counterpart in the 2g file.

---

## 4. Do the 26-word 2g table and 18-word 5g table appear in the `.kv` bytes?

Phase-7 established where the tables sit in firmware WRAM: the 26-word `2g_power_param` at CPU
`0x1b2f24` and the 18-word `5g_power_param` at CPU `0x1b30ea` (phase-7 `firmware-ram-dump.md` §2.4).
Reconstructed as little-endian u32 arrays and searched:

| search | `wifi_cali_data_2g.kv` | `wifi_cali_data.kv` |
|---|---|---|
| 26-word 2g table, contiguous LE u32 (104 B) | **0 hits** | **0 hits** |
| 18-word 5g table, contiguous LE u32 (72 B) | **0 hits** | **0 hits** |
| each table word, all 4 byte rotations | **0 hits** | **0 hits** |
| the 2g 4-byte words `0x17161605`, `0x0a0606ff`, `0x0a0f0f01`, `0x08141200` as raw LE bytes | **absent** | **absent** |
| the 5g 4-byte words `0x0004ff00`, `0x1a151813`, `0x16140f14` as raw LE bytes | **absent** | **absent** |

**Explicit negative: neither table is in either `.kv` file, at any offset, in any byte order.** The only
"matches" found are individual u16 halves of small table words (`0x0009`, `0x0016`, `0x001a`, `0x0004`,
`0x000b`) that occur everywhere in the files; every one is a coincidence of a small integer, and no
whole table word (`0x0004ff00`, `0x0c030009`, ...) occurs.

Interpretation (consistent with the phase-7 chain): the `.kv` store is the driver's *input* calibration
struct, not the firmware's power tables. The 26/18-word tables are the firmware's post-load *result*
(in WRAM at `0x1b2f24`/`0x1b30ea`), read back via `alg get_2g_power_param` / `get_5g_power_param`; they
are derived from this struct by firmware code and are not round-tripped through the file.

Closest structural echoes (stated as observations, **not** established mappings): the dual payload's
marker runs are 7+7 words (`0x9080` x7 then `0xa080` x7) which numerically parallels the 7 five-GHz
bands, and the 2g payload's runs are 6 `0x8080` words; but no 26- or 18-element array of the table
values exists in the file.

---

## 5. Explicit limits

1. **No field-by-field semantic decode of the payload.** The writers copy an opaque struct; the `.ko`
   carries no DWARF (no `.debug_*` sections), so `+0x04` onward cannot be named from the binaries. Only
   the container offsets (`+0`, `len-4`), the counts (8912 / 2336) and observed periodicity (38-word
   stride; `0x24`-byte sub-stride in the 2g body) are established. The `0x9080`/`0xa080`/`0x8080`
   marker words and the `ffee`/`0030`/`1ff*` body values are reported as raw content, not decoded.
2. **The struct is not in `wifi.ko`.** The buffer is `.bss` in `plat.ko` (`.LANCHOR0+0x120`); wifi.ko
   imports the accessors as undefined globals. Any deeper field work must use plat.ko + the message
   envelope, not wifi.ko alone.
3. **One save per boot.** `verify_*` refuses when the RAM magic is already `0x5a5a5a5a`/`0xa5a5a5a5`, so
   after a successful save the in-RAM struct can only be re-written to disk again if the RAM magic is
   restored to `0xfeedfeed` (e.g. by re-loading the file). Not chased further.
4. **Which of the two writers deletes/truncates is not claimed** - `filp_open` with `0x42` creates the
   file if absent; no explicit truncate call was found, so a shorter overwrite on top of a longer old
   file is not ruled out. This did not affect the observed dumps (both files matched their write
   counts exactly).
5. **Table search scope.** Only the two `.kv` files were searched; the 13,487-byte text
   `hi5622v100.cal` was out of scope for this report.
