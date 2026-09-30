# FIRMWARE.bin — forensic structure analysis

Scope: local files, read-only. No device access.
Target: `C:/Users/ShibbityShwab/router-openwrt/build/tmp/FIRMWARE.bin`
Cross-check input: `C:/Users/ShibbityShwab/router-openwrt/build/tmp/cfg_wifi.ini`
Tool: `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe`

Every number below is followed by the command that produced it and the observed output.
Statements that are inference rather than measurement are explicitly labelled **HYPOTHESIS**.

---

## 0. Sample identity

`cmd: ls -la build/tmp/FIRMWARE.bin; sha256sum build/tmp/FIRMWARE.bin rootfs-2.4.15/squashfs-root/lib/firmware/hi_wifi/FIRMWARE.bin build/cal-snapshots/20260930-195439/FIRMWARE.bin`

```
-rw-r--r-- 1 ShibbityShwab 197121 928920 Oct  1 02:46 build/tmp/FIRMWARE.bin
7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b *build/tmp/FIRMWARE.bin
7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b *build/cal-snapshots/20260930-195439/FIRMWARE.bin
7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b *rootfs-2.4.15/squashfs-root/lib/firmware/hi_wifi/FIRMWARE.bin
```

- Size = **928,920 bytes = 0xE2C98**.
- `md5 0e530b976d5a20e87358671f1a577695`, `sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`.
- The `build/tmp` copy is byte-identical to the vendor rootfs copy and to the calibration-snapshot copy.

`cmd: sha256sum build/tmp/cfg_wifi.ini; file build/tmp/FIRMWARE.bin`

```
a7ac5bcd9adf8c00fd004a113827ad85ae592da5738ca3c94afa30768ef44297 *build/tmp/cfg_wifi.ini
build/tmp/FIRMWARE.bin: data
```

- `build/tmp/cfg_wifi.ini` is byte-identical to `cfg_hi5622v100_hisi.ini` in the rootfs and in the snapshot (same sha256 `a7ac5bcd…`).
- `file` sees no recognized container (not ELF, not gzip/LZMA/zlib framing) — just `data`.

---

## 1. Header analysis — first 64 bytes

### 1.1 The words

`cmd: python -c "import struct; d=open('build/tmp/FIRMWARE.bin','rb').read(); [print('%04x: %08x  %d'%(i,struct.unpack_from('<I',d,i)[0],struct.unpack_from('<I',d,i)[0])) for i in range(0,64,4)]"`

```
0000: 00046971  289137
0004: 000c742d  816173
0008: 00000000  0
000c: 00000000  0
0010: 00000000  0
0014: 00000000  0
0018: 00000000  0
001c: 00000000  0
0020: 00000000  0
0024: 00000000  0
0028: 00000000  0
002c: 00000000  0
0030: 00000000  0
0034: 00000000  0
0038: 00000000  0
003c: 00000000  0
```

Raw bytes 0x00–0x0F: `71 69 04 00 2d 74 0c 00 00 00 00 00 00 00 00 00`
(`cmd: xxd -l 128 build/tmp/FIRMWARE.bin`).

So the 64-byte header is: **two non-zero 32-bit words, then 56 zero bytes**, and the real code/pointer material starts at 0x50 (see below).

`cmd: xxd -l 128 build/tmp/FIRMWARE.bin`

```
00000000: 7169 0400 2d74 0c00 0000 0000 0000 0000  qi..-t..........
00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000050: 4031 1000 1431 1000 e830 1000 bc30 1000  @1...1...0...0..
00000060: 9030 1000 04e0 4ee2 1305 6df9 1300 02f1  .0....N...m.....
00000070: ff1f 2de9 0410 0de2 01d0 4de0 0240 2de9  ..-.......M...@-.
```

### 1.2 Hypotheses for the two header words

Both values are **odd** (bit 0 set), which is the ARM **Thumb function-pointer** convention — and both are smaller than the file size:

- `w0 = 0x00046971` → masked to 0x00046970 (Thumb bit cleared).
- `w1 = 0x000c742d` → masked to 0x000c742c.

Verification that those offsets land on plausible content (`cmd: python -c "... print(hex, d[off:off+16].hex())"`):

```
off 0x46970: 03 f0 70 63 40 f2 f2 41 18 43 bc f7 55 f9 d4 e7   <- Thumb-2 instructions
off 0xc742c: 04 ff ff ff fe 00 00 00 05 01 ff 00 03 00 01 00   <- small signed-byte data table
```

- **HYPOTHESIS A (preferred):** a boot/entry descriptor — word0 = Thumb entry point (code at 0x46970), word1 = Thumb pointer to an initialisation data table (at 0xc742c).
- **HYPOTHESIS B:** a (magic, version) pair. As ASCII, word0 = `71 69 04 00` = `'q' 'i' 0x04 0x00` and word1 = `2d 74 0c 00` = `'-' 't' 0x0c 0x00`. The `'qi'` magic is not a known container signature, and the file has no matching trailer magic, so this is weak.
- **HYPOTHESIS C (rejected):** a (length, offset) or (offset, length) pair. Falsified by the arithmetic below — no combination yields the file size.

### 1.3 Arithmetic check against the real file size (928,920 = 0xE2C98)

`cmd: python -c "... w0,w1=struct.unpack_from('<II',d,0); print(w0+w1, w1-w0, N, N-8) ..."`

```
w0=0x00046971=289137  w1=0x000c742d=816173  size=928920=0xe2c98
sum w0+w1 = 1105310 diff to size = 176390
w1-w0 = 527036
body len (after 8) = 928912 0xe2c90
sum of all bytes = 101367726   (0x60abfae)
sum bytes[8:] = 101367331      (0x60abe23)
crc32(all) =0x2f3bf1c7
crc32(body8)=0x0a555041
adler32(all)=0x29591a54
w0 matches crc32(body)? False    w1 matches crc32(body)? False
w0==N? False   w1==N? False
```

Results:

| check | value | equals size 0xE2C98? |
|---|---|---|
| w0 | 0x00046971 = 289,137 | no |
| w1 | 0x000C742D = 816,173 | no |
| w0 + w1 | 0x0010DD9E = 1,105,310 | no (size + 176,390) |
| w1 − w0 | 0x000808BC = 527,036 | no |
| w0 × w1 mod 2³² | 0xF1D4F79D = 4,057,578,717 | no |
| sum of all bytes | 101,367,726 (0x60ABFAE) | no |
| crc32(all) | 0x2F3BF1C7 | no |
| crc32(bytes[8:]) | 0x0A555041 | no |
| adler32(all) | 0x29591A54 | no |

**Conclusion:** the header contains **no length field equal to the file size**, and no simple checksum of the body reproduces either header word. The two words behave like in-file pointers, not descriptive fields.

### 1.4 The 5-pointer table at 0x50

`cmd: xxd -l 128 build/tmp/FIRMWARE.bin` (lines 0x50, 0x60 above), decoded:

```
0x50: 0x00103140
0x54: 0x00103114
0x58: 0x001030E8
0x5C: 0x001030BC
0x60: 0x00103090
```

- 5 consecutive 32-bit values, stride **0x2C = 44 bytes**.
- All are **> file size (0xE2C98)**, so they cannot be file offsets — they are addresses in some other space.
- **HYPOTHESIS:** an array of 5 handler/descriptor pointers with 44-byte spacing, relocated at load time (see §2). If the load base were 0x100000 they would map to file offsets 0x3140, 0x3114, 0x30E8, 0x30BC, 0x3090, all of which contain Thumb-2 code (`cmd: python -c "... print(d[0x3140:0x3150].hex(' '))"` → `27 ff 4f f0 00 08 a7 e7 90 f8 e0 76 47 b9 03 78`), but the entries are not function prologues, so this base hypothesis is unconfirmed.

---

## 2. INI load-map cross-check

### 2.1 The declared map

From `build/tmp/cfg_wifi.ini` (= `cfg_hi5622v100_hisi.ini`, sha256 `a7ac5bcd…`), section `[HOST_WIFI_NORMAL]`:

```
firmware_itcm_src_addr=0xf009c   firmware_itcm_len=0xadd0
firmware_dtcm_src_addr=0xfae6c   firmware_dtcm_len=0x1098
firmware_custom_src_addr=0x1b2800 firmware_custom_src_len=0x10
```

### 2.2 They are NOT file offsets

`cmd: python -c "N=928920; [print(name, hex(v), v, v>=N) for name,v in ...]"`

```
itcm_src   0x0f009c =   983196   >=size? True
dtcm_src   0x0fae6c =  1027692   >=size? True
custom_src 0x1b2800 =  1779712   >=size? True
file size  0x0e2c98 =   928920
```

All three addresses exceed the 928,920-byte file (by 54,276 / 98,772 / 850,792 bytes respectively). **They cannot be offsets into this file.**

### 2.3 Structural proof that they are a load map, not file layout

`cmd: python -c "print(0xf009c+0xadd0==0xfae6c); print(0x117bc4+0xa968==0x12252c)"`

```
itcm end = 0xfae6c  == dtcm_src_addr 0xfae6c ? True
0x117bc4+0xa968=0x12252c == 0x12252c ? True
```

- ITCM and DTCM are **exactly contiguous** in the address space: `itcm_src + itcm_len == dtcm_src` (0xF009C + 0xADD0 = 0xFAE6C). This is a property of a memory map, not of a file (there is no marker in the file at any such boundary).
- The **same** `FIRMWARE.bin` (identical sha256) is paired with **different** ITCM/DTCM addresses and lengths in a second INI from the same snapshot — `build/cal-snapshots/20260930-195439/cfg_device_hisi.ini`:

```
firmware_itcm_src_addr=0x117bc4   firmware_itcm_len=0xa968
firmware_dtcm_src_addr=0x12252c   firmware_dtcm_len=0x76c
firmware_plat_custom_src_addr=0x1b2800  firmware_plat_custom_len=0x60
firmware_wifi_custom_src_addr=0x1b2860  firmware_wifi_custom_len=0x9a8
```

That file annotates the block `#实时变化,由python脚本写入` ("changes in real time, written by a python script"). So the ITCM/DTCM addresses are **generated per build/device**, which is incompatible with their being fixed offsets into an immutable file.

### 2.4 What they could be instead

- **HYPOTHESIS 1 (strongest):** absolute addresses in the Wi-Fi CPU's own memory/load map (the driver copies ITCM/DTCM/custom segments *from* those addresses, e.g. a chip-side SRAM window or a driver-relocated image buffer). Supported by: exact contiguity of itcm+len==dtcm (§2.3), the in-file pointer table at 0x50 holding addresses > file size (§1.4), and the per-build regeneration note (§2.3).
- **HYPOTHESIS 2:** offsets into a **larger combined firmware image** (≥ `custom_src + custom_len = 0x1b2810` = 1,779,728 bytes) of which `FIRMWARE.bin` (928,920 B) is only the code-bearing slice. Consistent with the numeric sizes but no such larger image exists in this workspace, so unconfirmed.
- **HYPOTHESIS 3:** runtime relocation offsets computed by the vendor's calibration Python script (explicitly documented in `cfg_device_hisi.ini`), i.e. they may point into a DRAM buffer the driver allocates rather than into any file at all.

### 2.5 Does any region of the file match the ITCM/DTCM sizes?

`cmd: python -c "... for val in (0xadd0, 0x1098, 0xbe68, 0xa968, 0x76c, 0xb0d4, 0x10): search LE and BE ..."`

```
0x0add0 itcm_len A      -> 0 hits
0x01098 dtcm_len A      -> 0 hits
0x0be68 itcm+dtcm A     -> 0 hits
0x0a968 itcm_len B      -> 0 hits
0x0076c dtcm_len B      -> 2 hits [(844676, '<I'), (844720, '<I')]   <- coincidence in code
0x0b0d4 itcm+dtcm B     -> 0 hits
```

- The file contains **no little- or big-endian encoding of 0xADD0, 0x1098, 0xBE68, 0xA968 or 0xB0D4** anywhere. It does not self-describe the ITCM/DTCM segment sizes, and there is no length-prefixed section table naming them.
- There is therefore **no positive evidence** that any contiguous region of the file corresponds to the ITCM or DTCM image; a candidate region cannot even be delimited from the file's own content.
- Sanity check: the file size is not a simple combination of the declared lengths — `0xE2C98 / (0xADD0+0x1098) = 0xE2C98 / 0xBE68 ≈ 19.0`; `0xE2C98 − 0xBE68 = 0xD6E30`, which is not a section boundary visible in the entropy map.

---

## 3. Entropy map (4 KB blocks)

`cmd: python -c "... for each 4096-byte block: Shannon entropy bits/byte ..."`

```
blocks 227 blocksize 4096 last block size 3224
min 0.6456 max 7.2561 mean 6.8134
```

Histogram (rounded to 0.1 bits/byte):

```
6.0:1  6.1:3  6.2:1  6.3:4  6.4:4  6.5:2  6.6:2  6.7:5  6.8:9  6.9:18
7.0:38 7.1:90 7.2:34 7.3:1
```

**Key finding:** the maximum block entropy is **7.2561 bits/byte**, far below the ~7.9–8.0 that compressed data (LZMA/zlib) or encryption produces. Combined with the `file` result, this means **FIRMWARE.bin is not compressed** — it is a plain code+data image.

### 3.1 High-entropy regions (≥ 6.5 bits/byte) — 815,104 bytes

`cmd: python -c "... classify blocks by threshold, merge contiguous ..."`

```
high 0x00000..0x49fff   303104 bytes (max 7.23)
high 0x4c000..0x94fff   299008 bytes (max 7.21)
high 0x96000..0xbefff   167936 bytes (max 7.26)
high 0xc1000..0xc2fff     8192 bytes (max 6.98)
high 0xd8000..0xd8fff     4096 bytes (max 6.97)
high 0xda000..0xe1fff    32768 bytes (max 6.96)
```

Interpretation:
- **0x00000–0xBEFFF (≈ 770 KB) is the main code body.** Confirmed by byte statistics (`cmd: python -c "Counter(d[0x1000:0xc3000])"`): top bytes `f8 f0 46 03 01 f7 20` are exactly the Thumb-2 instruction-byte bias, plus 71 occurrences of the ARM `bx lr` word `1e ff 2f e1` and 797 occurrences of the Thumb `bx lr` halfword `70 47` (`cmd: python -c "d.count(b'\x70\x47')"`).
- **0xD8000–0xE1FFF** is a second code/veneer block (see §5, ARM veneers at 0xE24FC).

### 3.2 Low-entropy regions (< 6.0 bits/byte) — 60,568 bytes

```
low 0xc3000..0xc5fff   12288 bytes (min 5.18)
low 0xc8000..0xd0fff   36864 bytes (min 0.65)   <- min at block 0xCF000
low 0xd5000..0xd6fff    8192 bytes (min 4.48)
low 0xe2000..0xe2c97    3224 bytes (min 5.66)
```

Interpretation:
- **0xC3000–0xC8000** — mixed pointer tables and the firmware's own string/symbol table (this block holds the lock/thread/param name strings, §4). Byte histogram: `00:4750, 01:703, ff:620, 03:553 …` (many small integers and NULs).
- **0xC8000–0xD8000** — 65,536-byte constant/structure region, very sparse: `zeros 28084, 0xFF 778, bytes<0x10 40113` (`cmd: python -c "seg=d[0xc8000:0xd8000]; print(seg.count(0),seg.count(0xff),sum(1 for x in seg if x<0x10))"`). Contains the highly repetitive blocks (min 0.65 at 0xCF000) — register/config tables, §5.
- **0xE2000–0xE2C98** — tail: ARM code + veneer tables + the MMIO/enum tables and the `DEADBEEF` marker (§5).
- **Mid band 6.0–6.5 — 53,248 bytes** in [0x4A000–0x4BFFF], [0x95000–0x95FFF], [0xBF000–0xC0FFF], [0xC6000–0xC7FFF], [0xD1000–0xD4FFF], [0xD7000], [0xD9000] — transition zones between code and tables.

---

## 4. String census

### 4.1 Method and totals

`cmd: python -c "import re,collections; runs=[(m.start(),m.group()) for m in re.finditer(rb'[\x20-\x7e]{8,}',d)]; ..."`

```
runs>=8: 414 unique: 391
unique containing 'F': 154
unique len>=16: 135
```

- **414 printable runs ≥ 8 chars, 391 unique.**
- **154 of the unique strings contain an ASCII `F`** and are false positives caused by Thumb-2 byte pairing (e.g. `';F2F)F F'`, `'SFBF)F8F'`, `'|c"xfxSCO'`). This is itself forensics: it confirms the bulk of the file is 16-bit-aligned instruction data, not text.

### 4.2 Version-like

```
0x0cd2ec  'ChenTangV100R001C20T13'
```

`cmd: python -c "print(d[0xcd2ec-16:0xcd2ec+48])"`

```
b'\x1d\xaa\x07\x00\xf1\xa4\x07\x00d\x00\x00\x04\x00\x00\x00\x00ChenTangV100R001C20T13\x00\x00...'
```

One real firmware version banner, NUL-terminated, near the tail of the symbol table. It matches the INI's board tag (`...CHENTANG-normal for V600...`).

### 4.3 Test-pattern / state names

`cmd: python -c "print(d[0xc44c0:0xc4500])"`

```
0x0c44c0: b'SE\x00SI\x00SK\x00TN\x00TR\x00VN\x00INIT\x00NORMAL\x00VERIFY20M\x00VERIFY40M\x00INVALID\x00ACTIVE'
VERIFY20M at 0xc44de
VERIFY40M follows at 0xc44e8
```

- `VERIFY20M` / `VERIFY40M` sit in a run of short state/enum tokens (`INIT`, `NORMAL`, `INVALID`, `ACTIVE`) — consistent with calibration `VERIFY 20 MHz / 40 MHz` states.

### 4.4 Path-like

`cmd: python -c "... filter strings containing '/' or '\\\\' or '.bin' ..."`

```
path-like strings: 13, all of which are either code artifacts or
ASCII-ramp constant tables, e.g.
' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHI'
'ddeeffgg...yyzz{{||}}~~'
```

There are **no genuine filesystem paths** (no `/lib/firmware`, `/usr/local/factory`, `.cal` or `.ko` names) in the blob. The apparent "path" hits are the `/` character inside monotonically increasing constant tables.

### 4.5 The genuine symbol/log strings (the valuable census)

All are single occurrences (x1). Grouped counts:

| category | count | examples (offset, string) |
|---|---|---|
| `*_isr` (interrupt handlers) | 32 | `0x0e266c smac_coex_rx_abort_end_5g_isr`, `0x0e29e8 smac_tbtt_5g_isr`, `0x0e2a6c smac_backoff_timeout_5g_isr` |
| lock/spinlock names (debug tokens) | 37 | `0x0c419e &g_irq_controller_lock`, `0x0c4220 &g_gpio_lock`, `0x0c6d0d &pst_device->st_reset_hw_info.st_reset_spinlock` |
| rate-control / bandwidth-probe params | 21 | `0x0c4a0e sudden_good_delta_gdpt_ratio`, `0x0c4b61 tx_rate_aging_time`, `0x0c4c6f gi1_with_2xltf_en` |
| DFS / radar params | 14 | `0x0c5d4a dfsenable`, `0x0c5d7a radarfilter`, `0x0c5e95 detect_check` |
| threads / workqueues | 7 | `0x0ccfc8 'HCC TX Thread'`, `0x0ccff8 'HCC RX Thread'`, `0x0cd028 'HCC OAM Thread'`, `0x0c42df pm_core_wq`, `0x0c43de pcie_thread`, `0x0c60b8 hirt_thread_stat_suspend` |
| enable/switch flags | 17 | `0x0c5e59 cac_silent_enable`, `0x0c5e82 octo_filter_enable` |
| misc anchors | — | `0x0c43a5 'DEVICE EXCP_INFO'`, `0x0c6328 'HEARTBEAT'`, `0x0c51ba 'BROADLINK'`, `0x0c4396 hcc_factory`, `0x0c434b offline_para` |

`cmd: python -c "print(d[0xc6328:0xc6368])"` → `b'HEARTBEAT\x00&pst_timer->st_timer_lock\x00idle0\x00idle1\x00...'`

**Layout:** the symbol/log/name table spans roughly **0xC417F–0xC6D20**; the `smac_*_isr` handler-name table spans **0xE266C–0xE2B90**. These two tables are the `smac`/`hcc` firmware's debug and TRAP-print strings — they are the richest structural evidence that this is a HiSilicon Wi-Fi MAC firmware (matches the `hi5622v100` driver and `Hisilicon0` netdev in `DRIVER-BLACKBOX.md`).

---

## 5. Substantiated embedded structures

### 5.1 64-byte boot header — offsets 0x00–0x63

Two Thumb pointers + 0x48 reserved zeros + a 5-entry pointer array at 0x50 (stride 0x2C). Evidence: §1.1, §1.4. Command: `xxd -l 128 build/tmp/FIRMWARE.bin`.

### 5.2 ARM veneer clusters

`cmd: python -c "d.find(b'\x04\xf0\x1f\xe5') ..."`

```
ARM veneer 'ldr pc,[pc,#-4]' (04f01fe5) occurrences: 11
  veneer clusters: [('0xc3b40','0xc3b50'), ('0xc3b68','0xc3b70'), ('0xe24fc','0xe2524')]
ARM 'bx lr' (1eff2fe1) occurrences: 71  (first 0xc2c58 ... last 0xe24f8)
Thumb 'bx lr' (7047) count: 797
```

- The image mixes **ARM (A32)** and **Thumb-2** code: dedicated ARM stubs at ~0xC2C00–0xC2D00 and ~0xE2300–0xE24FC, with `ldr pc,[pc,#-4]` veneer tables at 0xC3B40–0xC3B70 and 0xE24FC–0xE2524 (each veneer is `04 f0 1f e5` followed by a target address), bridging to Thumb.
- `cmd: python -c "print(d[0xe24fc:0xe2528].hex(' '))"` → `04 f0 1f e5 31 8e 00 00 04 f0 1f e5 81 00 00 00 04 f0 1f e5 15 8e 00 00 04 f0 1f e5 3d 8e 00 00 ...` — six 8-byte `{ldr pc,[pc,#-4]; target}` veneers.

### 5.3 MMIO register table + magic marker — 0xE2528–0xE259C

`cmd: python -c "print(d[0xe2528:0xe25a0].hex(' '))"`

```
0e2528: 00 40 03 40 3a 00 00 00 00 00 00 00
0e2534: 14 40 03 40 3a 00 00 00 00 00 00 00
0e2540: 28 40 03 40 3a 00 00 00 00 00 00 00
0e254c: 3c 40 03 40 3a 00 00 00 00 00 00 00
0e2558: 00 50 03 40 3b 00 00 00 00 00 00 00
0e2564: 14 50 03 40 3b 00 00 00 00 00 00 00
0e2570: 28 50 03 40 3b 00 00 00 00 00 00 00
0e257c: 3c 50 03 40 3b 00 00 00 00 00 00 00
0e2588: 00 70 10 40 3a 00 00 00 00 00 00 00
0e2594: 00 00 00 00 0f 00 00 00 ef be ad de
```

- **12-byte structs `{u32 addr, u32 id, u32 zero}`**: addresses `0x40034000, 0x40034014, 0x40034028, 0x4003403C, 0x40035000, 0x40035014, 0x40035028, 0x4003503C, 0x40107000` all lie in the **0x40000000 peripheral/MMIO window**, with ids 0x3A/0x3B. **HYPOTHESIS:** an init/read-back list of radio registers (base+0x14 stride).
- Immediately after the table, bytes 0xE2594–0xE259F form a 12-byte trailer struct `{0x00000000, 0x0000000F, 0xDEADBEEF}` (`cmd: python -c "print(d[0xe2594:0xe25a0].hex(' '))"` → `00 00 00 00 0f 00 00 00 ef be ad de`), i.e. a count/length field **0x0F** followed by the magic **`ef be ad de` (0xDEADBEEF)**. The magic occurs exactly once in the whole file (`cmd: python -c "print(hex(d.find(b'\xef\xbe\xad\xde')))"` → `0xe259c`). **HYPOTHESIS:** a table descriptor/trailer (`count=15, magic=DEADBEEF`) for the preceding block; the following 15-track-looking values (0x00008000, 0x00000224, 0x0000023C, 0x00000414 …) begin at 0xE25A0.

### 5.4 Address / jump table — 0xC3EE0–0xC3FB4 (53 words)

`cmd: python -c "... maximal runs of consecutive words in [0x10000,0x200000) ..."`

```
0xc3ee0: 3f 44 10 00 42 44 10 00 49 6d 10 00 2f 63 10 00
0xc3ef0: 45 44 10 00 48 44 10 00 4b 44 10 00 4e 44 10 00
...
0xc5b84..0xc5c6c  58 words   <- longest such run in the file
0xc3ee0..0xc3fb4  53 words
0xc5ecc..0xc5f9c  52 words
```

- A 53-word run of 32-bit values in the 0x00104xxx range, almost all ascending by +3 (`0x0010443F, 0x00104442, …`). **HYPOTHESIS:** an address/jump table (the +3 pattern would fit 3-byte-indexed entries or a table of Thumb addresses), sitting in the low-entropy 0xC3000 region.
- The same scan shows the longest pointer-like runs in the whole file cluster in the 0xC3000–0xD4000 table zone (58, 53, 52, 48, 41, 33, 32, 31 … words), i.e. the low-entropy region is dominated by address tables, not code.

### 5.5 Constant / lookup tables — 0xC65C0–0xC6790

`cmd: python -c "print(d[0xc65c0:0xc66c0].hex(' '))"`

```
0c65c0: 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16      <- linear ramp
0c6610: 1e 21 23 25 27 2a 2c 2e 30 33 35 38 3a 3d 3f 41      <- ~2.5 step ramp
0c66d0: 01 01 01 01 01 02 02 02 02 02 02 02 02 02 00 04      <- 2^n bit-mask runs
0c6740: 20 40 40 80 80 01 02 04 08 10 20 40 80 03 0c 30      <- shifts/masks
0c6750: c0 0f f0 ff 4a 4a 4b 4b 7a 4c 4c 4d 4d 4e 4e 4f
```

- Classic PHY/DSP constant pools: monotone ramps, `2^n` bit-mask tables and shift tables. **HYPOTHESIS:** rate/`log2`/scaling helpers for the calibration and rate-control engines named in §4.5.

### 5.6 Exponential table — 0xC7F20

`cmd: python -c "print(d[0xc7f20:0xc7f40].hex(' '))"`

```
0c7f20: 66 00 b3 00 65 01 c9 02 8f 05 17 0b 22 16 28 2c
```

16-bit values `0x0066, 0x00B3, 0x0165, 0x02C9, 0x058F, 0x0B17, 0x1622, 0x2C28` — a smooth geometric (~×1.97) progression. **HYPOTHESIS:** a power/amplitude (dB→linear) table. The following block (`0c7f40: ff 03 fe 03 fd 03 …`) holds values clustered near 0x03FF/0x0400 (10-bit full scale).

### 5.7 Register/config tables — 0xCF000

`cmd: python -c "print(d[0xcf000:0xcf070].hex(' '))"`

```
0cf000: 04 00 00 00 04 00 00 00 1e 00 00 00 07 00 00 00
0cf010: 1a 00 00 00 3a 02 00 00 3b 02 00 00 03 00 00 00
0cf020: 04 00 00 00 04 00 00 00 00 00 00 00 03 40 00 00
0cf040: f5 00 00 00 f5 00 00 00 00 00 00 00 03 40 00 00
0cf050: 00 00 00 00 00 03 00 00 ...
0cf0b0: 44 00 00 00 80 00 00 00 02 03 00 00 10 00 00 00
```

- Sparse 32-bit entries with recurring constants `0x00004003`, `0x00005003`, `0x00000300`, `0x000000F5`, `0x000001E0`. **HYPOTHESIS:** per-mode/per-channel register-configuration rows. This is what drives the block entropy down to 0.65 at 0xCF000.

### 5.8 (index, value) tables — 0xD0940 and 0xD2AAC

`cmd: python -c "print(d[0xd0940:0xd0990].hex(' ')); print(d[0xd2aac:0xd2b00].hex(' '))"`

```
0d0940: 00 00 0a 00 65 08 0b 00 44 f7 01 00 00 90 02 00
0d0950: 6f 14 03 00 71 1c 04 00 80 1f 05 00 00 00 06 00
...
0d2aac: 00 00 01 00 00 00 02 00 00 00 03 00 06 00 04 00
0d2abc: f5 00 05 00 fb 30 06 00 f2 03 07 00 03 40 08 00
```

- Both are 4-byte entries where the **high 16 bits are a monotonically increasing index** (0x0A,0x0B,0x0C… and 0x01,0x02,0x03…) and the low 16 bits carry a per-index value (mixed ranges, e.g. 0x0865, 0x9000, 0x1F80 at 0xD0940). **HYPOTHESIS:** indexed register/value or gain-indexed lookup lists.

### 5.9 Numeric growth table at end of file — 0xE2C60–0xE2C98

`cmd: xxd -s 0xe2c58 -l 64 build/tmp/FIRMWARE.bin`

```
000e2c58: 0000 0000 0000 0000 0000 0000 0100 0000
000e2c68: 0300 0000 0600 0000 0d00 0000 1a00 0000
000e2c78: 3400 0000 6800 0000 d100 0000 a301 0000
000e2c88: 4603 0000 8d06 0000 1b0d 0000 361a 0000
```

`cmd: python -c "... struct.unpack_from('<I',d,o) for o in range(0xe2c58,0xe2c98,4)"`

```
0e2c60: 00000000  0      0e2c78: 00000034  52     0e2c90: 00000d1b  3355
0e2c64: 00000001  1      0e2c7c: 00000068  104    0e2c94: 00001a36  6710
0e2c68: 00000003  3      0e2c80: 000000d1  209
0e2c6c: 00000006  6      0e2c84: 000001a3  419
0e2c70: 0000000d  13     0e2c88: 00000346  838
0e2c74: 0000001a  26     0e2c8c: 0000068d  1677
```

Values `0, 1, 3, 6, 13, 26, 52, 104, 209, 419, 838, 1677, 3355, 6710` (all 32-bit LE) — an almost-doubling sequence occupying the final 0x38 bytes. **HYPOTHESIS:** a timeout/retry or backoff-duration progression (the file simply ends inside this table; there is no trailer or checksum).

### 5.10 Other repeated values (code fingerprint)

`cmd: python -c "Counter of 32-bit LE words"`

```
0x6370f003 x475
0xf003061b x433
0x4ff0e92d x275   <- Thumb-2 'push.w {r4-r11,lr}'
0x302df648 x237
0x46204629 x172
0x8ff0e8bd x171   <- Thumb-2 'pop.w {r4-r11,pc}'
0x47702000 x143   <- 'movs r0,#0; bx lr'
```

The most frequent words are Thumb-2 instruction encodings, confirming the main body is compiled code. Repeated data constants such as `0x00001110` (x6) and `0xFFFFFFFF` (x73) appear almost exclusively in the table regions (0xC4xxx onwards).

---

## 6. Explicitly unresolved

1. **Address space / load base of the INI values.** Whether `0xf009c`/`0x1b2800` are addresses in the chip's memory map, offsets into a larger combined image, or driver-relocated buffer offsets. Only the *negatives* are certain: not offsets into this 928,920-byte file; ITCM and DTCM are contiguous in that space; the values are regenerated per build.
2. **Meaning of header words 0x00046971 / 0x000C742D.** Both look like Thumb pointers (odd, in-file), but no length/checksum hypothesis survived the arithmetic check. Whether 0x0–0x4F is reserved, and the semantic of the 5 pointers at 0x50, is unproven.
3. **Whether the INI ITCM/DTCM segments exist anywhere in this file.** No length constant and no marker for 0xADD0 / 0x1098 / 0xBE68 was found; a candidate region cannot be delimited.
4. **No section table.** The file has no ELF/section header, no self-describing segment directory, and no trailer; region boundaries in §3 are inferred from entropy only.
5. **Purpose of the 0xC3000–0xD8000 tables.** The address/jump table (0xC3EE0), the constant ramps (0xC65C0) and the register tables (0xCF000) are structurally clear but their field semantics are not decoded.
6. **`0x0000000F` + `0xDEADBEEF` at 0xE2594/0xE259C.** Whether 0x0F is a count for the preceding MMIO table, a length, or unrelated.
7. **Missing BSS/clear regions.** The image contains no large zero run outside the low-entropy table zone (whole-file zero bytes: 61,663 = 6.6 %), so it appears to be a single loaded segment; the runtime layout after the driver relocates it is unknown.
8. **No integrity field.** No header/trailer checksum equals any standard digest of the body (crc32, adler32, byte-sum all checked, §1.3), so the file carries no verifiable self-checksum.

---

### Reproduce

```bash
cd /c/Users/ShibbityShwab/router-openwrt
ls -la build/tmp/FIRMWARE.bin
sha256sum build/tmp/FIRMWARE.bin build/tmp/cfg_wifi.ini
xxd -l 512 build/tmp/FIRMWARE.bin
./pyenv/Scripts/python.exe - <<'PY'
# sections 1-5: header words, INI arithmetic, 4 KB entropy map, string census, tables
import struct, math, re, collections, zlib
d = open('build/tmp/FIRMWARE.bin','rb').read(); N = len(d)
print('size', N, hex(N))
print([('%04x' % i, '%08x' % struct.unpack_from('<I', d, i)[0]) for i in range(0, 64, 4)])
print('itcm+len==dtcm:', 0xf009c + 0xadd0 == 0xfae6c)
E = []
for i in range(0, N, 4096):
    b = d[i:i+4096]; c = collections.Counter(b); n = len(b)
    E.append(-sum((v/n) * math.log2(v/n) for v in c.values()))
print('entropy min/max/mean: %.4f %.4f %.4f' % (min(E), max(E), sum(E)/len(E)))
print('runs>=8:', len(re.findall(rb'[\x20-\x7e]{8,}', d)))
PY
```
