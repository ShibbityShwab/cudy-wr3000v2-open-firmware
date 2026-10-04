# channel-register-offsets: what one wifi.ko "channel" block's words hold (phase 43, 2026-10-04)

Task `st_01a10726` (parent `01a0fc5c`, root `01a0fc5c`). **Static, read-only**: the two vendor modules and
the device-side register dump only. No device access, no register write, nothing outside this file written.

Sources (hashes as given in the task):

| file | identity | use |
| --- | --- | --- |
| `opensource/build/tmp/hi5622v100_wifi.ko` | 3,564,728 B, md5 `4737fcb21a1a2262a96f84d780ad8b35` | the module that owns the channel base array and every accessor quoted below |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | 364,660 B, md5 `23660bc285393e678d5cade1c36c194b` | ETE block owner (contrast only; no channel-block accessor found) |
| `build/register-dumps/reg_all.txt` | 25,154 words, device CAs | the live values cross-checked per offset |

Conventions: `.ko` addresses are **`.text` offsets**, `.text` at file offset `0x38`, code is **ARM**
(`CS_MODE_ARM`); quoted bytes are the raw little-endian instruction words at that offset. `.rodata` is at
file offset `0x167b00` with `sh_addr = 0`, so `rodata+0xNNN` in this file is both the section offset and
the value a literal pool holds for it. `reg_all.txt` values are quoted as `<CA> = <value>`. Every
instruction cited was re-disassembled for this report (Appendix A); every dump value was read out of
`reg_all.txt` (Appendix B).

---

## 0. The answer in one block

The block the task names, device CA **`0x4004a000`**, is not a message/descriptor ring and has no
wptr/rptr. It is the **CSI (channel-state-information) output-buffer block** of the 2.4 GHz MAC, and its
first four words are:

| offset | meaning | vendor writes? | vendor reads? | `reg_all.txt` |
| --- | --- | --- | --- | --- |
| `+0x00` | **CSI param / enable word** | **yes** -- `shuangta_to_hmac_csi_set_param` (`str r1,[r0]`), called from `hmac_csi_enable` and `hmac_csi_disable` | yes -- `shuangta_to_hmac_csi_get_param` (`ldr r3,[r0]`) | `0x4004a000 = 0x00000000` |
| `+0x04` | **CSI buffer device address** (host DRAM buffer, `pcie_if_hostca_to_devva` of its host CA) | **yes** -- `shuangta_to_hmac_csi_set_buf_addr` (`str r1,[r0,#4]`), from `hmac_csi_init` | yes -- getter @`0x350e4` (`ldr r1,[r0,#4]` then `oal_pcie_devca_to_hostva`) | `0x4004a004 = 0x84a90000` |
| `+0x08` | **CSI buffer size in bytes** | **yes** -- `shuangta_to_hmac_csi_set_buf_size` (`str r1,[r0,#8]`) | yes -- `shuangta_to_hmac_csi_get_buf_size` (`ldr r3,[r0,#8]`) | `0x4004a008 = 0x00006140` |
| `+0x0c` | **CSI whitelist table base** (32 entries x 0x20 B; entry words at `+0x0c/+0x10/+0x14` of each entry) | **yes** -- `shuangta_to_hmac_csi_set_whitelist` (`str lr,[r2,#0xc]`, `r2 = block + i*0x20`) | yes, as a pointer -- `shuangta_to_hmac_csi_get_whitelist` (`addne r3,r0,#0xc`) | `0x4004a00c = 0x00000000` |

**`0x4004a004` / `0x4004a008` are therefore not a ring base and an index.** `+0x04` is a host-DRAM
*buffer* address (as a device VA) and `+0x08` is that buffer's **size**, and the value `0x6140` is the
vendor's own hard-coded CSI buffer size constant -- `hmac_csi_init` loads literally `movw r7, #0x6140`
(`0xbae38`) and passes it to `shuangta_to_hmac_csi_set_buf_size` (`0xbae54`). The whole 0xe60-byte
window of `0x4004a000` is zero except `+0x04`, `+0x08` and one unexplained word at `+0x414`
(`0x4004a414 = 0x00002710`).

Where the *ring* registers actually live in this same CA family is S5: role index 5, CA
**`0x40040000`** (2 GHz) / `0x40060000` (5 GHz), at `+0x20..+0x54` (`addr / size / wptr / rptr` for the
three RX rings, `tx_ba_info` at `+0x00/+0x04`, `msdu_info_ring_ptr_table_base` at `+0x10`,
`host_mac_int_mask` at `+0x48`). That block's `+0x48` reads `0xfffffcc1` in the dump, and the module
writes exactly that constant -- a one-word unit test that the mapping below is right.

---

## 1. What the `0x40042000`-style table really is (it is not 12 channels)

`wifi.ko`'s `.rodata` holds, at `rodata+0xb60`, a **10-entry** array of device CAs; 40 bytes later at
`rodata+0xb88` the **5 GHz twin**:

```
rodata+b60 = 40042000 40044000 40046000 40048000 4004a000 40040000 4004c000 40052000 40050000 40054000   <- 2 GHz
rodata+b88 = 40062000 40064000 40066000 40068000 4006a000 40060000 4006c000 40072000 40070000 40074000   <- 5 GHz
```

(raw `.rodata` words at `rodata+0xb60` and `rodata+0xb88`, read for this report; the 12 words the
phase-42 record called a "12-entry channel-base
array at 0xb60" are `b60..b8c`, i.e. the 10-entry 2 GHz array **plus the first two words of the 5 GHz
array** at `b88/b8c` -- the array is 10 long, not 12.)

These two arrays are consumed by **`shuangta_host_initialize_machw` @`0x3491c`**, which converts each CA
to a host VA once and stores the resulting 10-word VA array in the per-band device object at `+0x14c`:

```
0x034954  feffffeb  bl oal_mem_alloc; (reloc R_ARM_CALL -> oal_mem_alloc)
0x034960  4c0186e5  str r0, [r6, #0x14c]; <-- [dev+0x14c] = the 10-entry VA array
0x034968  6cc19fe5  ldr ip, [pc, #0x16c]; ip = rodata+0xb60 (literal @0x34adc,
                                                           ;     R_ARM_ABS32 addend 0xb60)
0x034980..0x034998   ldm ip!,{r0-r3} / stm r5!,{r0-r3} x2 + ldm ip,{r0,r1} / stm r5,{r0,r1}
                                                           ; 10 CAs copied to sp+0x14 (band 0)
0x034a04  14208de2  add r2, sp, #0x14; band 0 -> the copy at sp+0x14
0x0349c4  3c208de2  add r2, sp, #0x3c; band 1 -> the copy at sp+0x3c (b88)
0x0349d0  7dfdffeb  bl #0x33fcc; -> converts the 10 CAs, in order
```

`0x33fcc` is the converter loop: it walks the 10 CA words (`ldr r1, [r6, #4]!` @`0x33ff8`), calls
`oal_pcie_devca_to_hostva` for each (`bl` @`0x3400c`, reloc symbol `oal_pcie_devca_to_hostva`) and stores
the results into the 10 consecutive words of the array (`str r2, [r8, #0]` ... `[r8, #0x24]`,
dispatch table @`0x34030`). So **array word N = the host VA of the N-th CA of `rodata+0xb60`
(band 0) or `rodata+0xb88` (band 1)** -- and every later accessor reaches a channel block by loading
`[dev+0x14c]` and then a fixed array word.

`rodata+0xbb0` is a *second*, differently ordered pair used only by the vendor's register dumper
(`shuangta_read_all_reg_info` @`0x35600`, which copies `rodata+0xbb0` at `0x3592c`: `ldr ip,[pc,#0x840]`
-> literal @`0x36174`, R_ARM_ABS32 addend `0xbb0`): the ascending CA list plus, at `rodata+0xbd8`, the
**matching 10 window lengths**. Those ten lengths reproduce `reg_all.txt`'s captured run lengths
byte-for-byte, which is what fixes the block boundaries used in this report:

| CA (ascending list) | length word | `reg_all.txt` run | matches |
| --- | --- | --- | --- |
| 0x40040000 | `rodata+bd8 = 0x000000b0` | `40040000 .. 400400ac` = 0xb0 | yes |
| 0x40042000 | `rodata+bdc = 0x0000092c` | `40042000 .. 40042928` = 0x92c | yes |
| 0x40044000 | `rodata+be0 = 0x00000ac0` | `40044000 .. 40044abc` = 0xac0 | yes |
| 0x40046000 | `rodata+be4 = 0x00001000` | `40046000 .. 40046ffc` = 0x1000 | yes |
| 0x40048000 | `rodata+be8 = 0x00000e50` | `40048000 .. 40048e4c` = 0xe50 | yes |
| 0x4004a000 | `rodata+bec = 0x00000e60` | `4004a000 .. 4004ae5c` = 0xe60 | yes |
| 0x4004c000 | `rodata+bf0 = 0x00000924` | `4004c000 .. 4004c920` = 0x924 | yes |
| 0x40050000 | `rodata+bf4 = 0x00000044` | `40050000 .. 40050040` = 0x44 | yes |
| 0x40052000 | `rodata+bf8 = 0x000002d4` | `40052000 .. 400522d0` = 0x2d4 | yes |
| 0x40054000 | `rodata+bfc = 0x00000190` | `40054000 .. 4005418c` = 0x190 | yes |

So `reg_all.txt` is this dumper's output, and the CAs are **ten distinct sub-blocks of one band's MAC**,
not ten identical channels: the 0x2000 stride is a slot spacing with the 8th slot (`0x4004e000`) absent,
and each block has its own length and its own register layout. The role assignment of each array word is
fixed by the accessors that use it:

| array word (byte off) | CA, band 0 | CA, band 1 | role | proof |
| --- | --- | --- | --- | --- |
| `+0x00` (0) | 0x40042000 | 0x40062000 | `fcs_error_inj` reg at `+0x924`; test-mode reg at `+0xa8` | S4 |
| `+0x0c` (3) | 0x40048000 | 0x40068000 | ht-matrix buffer (`+0x10/+0x14/+0x18`), `cca_nav_bypass_cfg` at `+0x618` | S4 |
| `+0x10` (4) | **0x4004a000** | 0x4006a000 | **CSI block** (`+0x00/+0x04/+0x08` + whitelist at `+0x0c`) | S2, S3 |
| `+0x14` (5) | 0x40040000 | 0x40060000 | RX rings + tx_ba_info + msdu ptr table + host MAC int status/mask | S5 |
| `+0x1c` (7) | 0x40052000 | 0x40072000 | a register at `+0x2ac` holding a device CA | S4 |
| `+0x20` (8) | 0x40050000 | 0x40070000 | host MAC int status read at `+0x00` | S4 |

The mapping is anchored by three independent dump/test matches (all in S4): index 3 (`+0x10 = 0x834c4000`,
`+0x14 = 0x4b0`, `+0x18 = 3` = pointer/step/num of the ht-matrix buffer), index 5 (`+0x48 = 0xfffffcc1` =
the mask constant the module writes), index 0 (`+0x924` fits only inside the 0x92c-byte block at
0x40042000, not inside 0x40040000's 0xb0 bytes).

---

## 2. The target block: CA 0x4004a000 (role index 4, the CSI block)

### 2.1 Offset table

| offset | meaning | width / format | vendor WRITES | vendor READS |
| --- | --- | --- | --- | --- |
| `+0x00` | **CSI param / enable word** (the CSI feature's config word) | full word | `shuangta_to_hmac_csi_set_param` | `shuangta_to_hmac_csi_get_param` |
| `+0x04` | **CSI buffer address** (device VA of a host DRAM buffer) | full word | `shuangta_to_hmac_csi_set_buf_addr` | unnamed getter @`0x350e4` |
| `+0x08` | **CSI buffer size in bytes** | full word | `shuangta_to_hmac_csi_set_buf_size` | `shuangta_to_hmac_csi_get_buf_size` |
| `+0x0c` | **CSI whitelist entry 0, word 0** = whitelist table base | word, entry stride `0x20`, 32 entries, words at `+0x0c/+0x10/+0x14` | `shuangta_to_hmac_csi_set_whitelist` | `shuangta_to_hmac_csi_get_whitelist` (returns `block+0xc`) |
| `+0x10` | whitelist entry 0, word 1 | word | `shuangta_to_hmac_csi_set_whitelist` | -- |
| `+0x14` | whitelist entry 0, word 2 | word | `shuangta_to_hmac_csi_set_whitelist` | -- |
| `+0x0c + i*0x20 (+0xc/+0x10/+0x14)`, i = 0..31 | whitelist entry i | 3 words per entry | `shuangta_to_hmac_csi_set_whitelist` | `shuangta_to_hmac_csi_get_whitelist` hands the base to the caller |
| `+0x414` | **[unknown]** -- no accessor and no reader found in either module | word | -- | -- |

There is **no wptr, no rptr, no depth and no credit** anywhere in this block: none of the four
accessors touches any other offset, and the whole 0xe60-byte window is zero apart from `+0x04`, `+0x08`
and `+0x414` (Appendix B).

### 2.2 The four words, one by one

**(a) `+0x00` -- CSI param.**

```
shuangta_to_hmac_csi_set_param @0x34f6c  (STT_FUNC, 0x7c B)
0x034f7c  4c3190e5  ldr r3, [r0, #0x14c]; r3 = the 10-word channel VA array
0x034f88  100093e5  ldr r0, [r3, #0x10]; r0 = array word 4 = VA of CA 0x4004a000  (band 0)
0x034f90  00108015  strne r1, [r0]; *(+0x00) = arg1   <-- WRITE
```
The caller is named, which fixes the meaning: `hmac_csi_enable` @`0xb9d84` sets bit 0 in its software
shadow (`ldrb r2,[r3]; orr r2,r2,#1; strb r2,[r3]` @`0xb9de4..0xb9dec`) and then passes the whole word --
`0x0b9df4  001093e5  ldr r1, [r3]` / `0x0b9df8  bl -> shuangta_to_hmac_csi_set_param` -- so `+0x00` is
the CSI *enable/config* word, and `hmac_csi_disable` @`0xb9e98` writes it disabled.

Read side:
```
shuangta_to_hmac_csi_get_param @0x34fe8
0x034ff8  4c3190e5  ldr r3, [r0, #0x14c]
0x035004  100093e5  ldr r0, [r3, #0x10]
0x03500c  00309015  ldrne r3, [r0]; r3 = *(+0x00)   <-- READ
```

**(b) `+0x04` -- CSI buffer address.**

```
shuangta_to_hmac_csi_set_buf_addr @0x35068
0x035078  4c3190e5  ldr r3, [r0, #0x14c]
0x035084  100093e5  ldr r0, [r3, #0x10]
0x03508c  04108015  strne r1, [r0, #4]; *(+0x04) = arg1   <-- WRITE
```
The caller `hmac_csi_init` shows what the value is -- a host buffer converted to a device address:
```
hmac_csi_init @0xbae2c
0x0bae1c  0000a0e3  mov r0, #0
0x0bae20  feffffeb  bl pcie_if_hostca_to_devva   ; r0 = device VA of the host buffer
0x0bae2c  040085e5  str r0, [r5, #4]; (the ring/desc object's own copy)
0x0bae34  0010a0e1  mov r1, r0
0x0bae38  407106e3  movw r7, #0x6140; r7 = 24896 = the CSI buffer size
0x0bae48  feffffeb  bl shuangta_to_hmac_csi_set_buf_addr
0x0bae4c  0710a0e1  mov r1, r7
0x0bae54  feffffeb  bl shuangta_to_hmac_csi_set_buf_size
```
Read side (unnamed getter between `csi_get_buf_size` and `csi_set_whitelist`, `@0x350e4`):
```
0x035104  4c3194e5  ldr r3, [r4, #0x14c]
0x035110  100093e5  ldr r0, [r3, #0x10]
0x03512c  041090e5  ldr r1, [r0, #4]; r1 = *(+0x04)
0x035138  feffffeb  bl oal_pcie_devca_to_hostva; -> host VA, returned to the caller
```

**(c) `+0x08` -- CSI buffer size.**

```
shuangta_to_hmac_csi_set_buf_size @0x351f0
0x035200  4c3190e5  ldr r3, [r0, #0x14c]
0x03520c  100093e5  ldr r0, [r3, #0x10]
0x035214  08108015  strne r1, [r0, #8]; *(+0x08) = arg1   <-- WRITE
shuangta_to_hmac_csi_get_buf_size @0x3526c
0x03527c  4c3190e5  ldr r3, [r0, #0x14c]
0x035288  100093e5  ldr r0, [r3, #0x10]
0x035290  08309015  ldrne r3, [r0, #8]; r3 = *(+0x08)   <-- READ
```
Both callers are named `..._buf_size`, and the value written is the `movw r7, #0x6140` constant of
`hmac_csi_init` (`0xbae38`) -- so `+0x08` is a byte size, and the dump's `0x6140` is that size.

**(d) `+0x0c` -- CSI whitelist table base (32 x 0x20 bytes).**

```
shuangta_to_hmac_csi_set_whitelist @0x3547c
0x035498  10209ce5  ldr r2, [ip, #0x10]; r2 = VA of CA 0x4004a000
0x0354b8  8102a0e1  lsl r0, r1, #5; r0 = i * 0x20
0x0354cc  200051e3  cmp r1, #0x20; 32 entries
0x0354d0  0ce082e5  str lr, [r2, #0xc]; entry i, word 0  -> +0xc + i*0x20
0x0354d4  10209ce5  ldr r2, [ip, #0x10]
0x0354e0  ...       str  lr, [r2, #0x10]       ; entry i, word 1
0x0354f0  ...       str  lr, [r2, #0x14]       ; entry i, word 2
```
Read side returns the table *base* to the caller, confirming that `+0x0c` starts a contiguous table:
```
shuangta_to_hmac_csi_get_whitelist @0x35574
0x035584  4c3190e5  ldr r3, [r0, #0x14c]
0x035590  100093e5  ldr r0, [r3, #0x10]
0x035598  0c308012  addne r3, r0, #0xc; returns block + 0xc
0x03559c  ...       strne r3, [r1]
```
Callers: `hmac_add_csi_whitelist` @`0xbb15c` and `hmac_del_csi_whitelist` @`0xbb3dc` -- i.e. the words
`+0x0c .. +0x400` are address/ID filters, not an index pair.

### 2.3 Dump values for this block

```
0x4004a000 = 00000000     ; +0x00 CSI param: CSI disabled in this capture
0x4004a004 = 84a90000     ; +0x04 CSI buffer device address (host DRAM, same address class as the
                          ;        phase-25 live SR ring base 0x848F6000)
0x4004a008 = 00006140     ; +0x08 CSI buffer size = 24896 bytes = the module's movw r7,#0x6140
0x4004a00c = 00000000     ; +0x0c whitelist[0].word0
0x4004a010 = 00000000     ; +0x10 whitelist[0].word1
0x4004a014 = 00000000     ; +0x14 whitelist[0].word2
0x4004a414 = 00002710     ; +0x414 = 10000, [unknown] - no accessor found
```
All other words in `0x4004a000..0x4004ae5f` read zero.

---

## 3. Which offsets the vendor WRITES vs only reads (summary for the target block)

| offset | written by | read by |
| --- | --- | --- |
| `+0x00` | `shuangta_to_hmac_csi_set_param` (`hmac_csi_enable`, `hmac_csi_disable`) | `shuangta_to_hmac_csi_get_param` |
| `+0x04` | `shuangta_to_hmac_csi_set_buf_addr` (`hmac_csi_init`) | unnamed getter @`0x350e4` |
| `+0x08` | `shuangta_to_hmac_csi_set_buf_size` (`hmac_csi_init`) | `shuangta_to_hmac_csi_get_buf_size` (`hmac_csi_complete`) |
| `+0x0c .. +0x0ff` | `shuangta_to_hmac_csi_set_whitelist` | `shuangta_to_hmac_csi_get_whitelist` (hands out the base) |
| `+0x100 .. +0x40f` | idem (the same 32-entry loop, `i` up to 31) | idem |
| `+0x410 .. +0xe5f` | **nothing found** | **nothing found** |

No offset in this block is read-only: every register that has a reader also has a writer, and vice
versa.

---

## 4. The other array words (for completeness; each cited dump value is a cross-check)

**word 0 (`+0x00`) = CA 0x40042000 / 0x40062000** -- the FCS-error-injection / test-mode block:
```
shuangta_to_hmac_set_fcs_error_inj @0x34ee8
0x034ef4  4c0190e5  ldr r0, [r0, #0x14c]
0x034f00  000090e5  ldr r0, [r0]; word 0 = VA of CA 0x40042000
0x034f28  242980e5  str r2, [r0, #0x924]; *(0x40042000+0x924) = the injection word
hal_set_test_mode_cfg_tx_fcs_err_inj_en @0x32454
0x032454  a83091e5  ldr r3, [r1, #0xa8]
0x032460  9231c3e7  bfi r3, r2, #3, #1          ; +0xa8 bit 3
0x032464  a83081e5  str r3, [r1, #0xa8]        ; -> 0x40042000 + 0xa8
```
Dump: `0x400420924`-range -- `0x40042924 = 0x00000000`, `0x40042928 = 0x00000064`; `0x400420a8 = 0x00000006`.
`+0x924` lies inside the 0x92c-byte window of `0x40042000`, and **outside** the 0xb0-byte window of
`0x40040000` -- which is how word 0 is pinned to 0x40042000.

**word 3 (`+0x0c`) = CA 0x40048000 / 0x40068000** -- ht-matrix buffer + CCA/Nav bypass:
```
hal_set_ht_matrix_buffer_pointer_cfg_ht_matrix_buffer_pointer @0x324e4
0x0324ec  103081e5  str r3, [r1, #0x10]
hal_set_ht_matrix_buffer_step_cfg_ht_matrix_buffer_step @0x3253c
0x03254c  143081e5  str r3, [r1, #0x14]
hal_set_ht_matrix_buffer_num_cfg_ht_matrix_buffer_num @0x32484
0x032494  183081e5  str r3, [r1, #0x18]
hal_set_cca_nav_bypass_cfg_pri_20m_cca_bypass_en @0x324cc (and 6 siblings)
0x0324dc  183681e5  str r3, [r1, #0x618]; bits 0..7 of +0x618
```
with the base from `shuangta_set_cca_nav_bypass_cfg_*_proc`:
```
0x036ed4  4c3191e5  ldr r3, [r1, #0x14c]
0x036ed8  ...       ldr  r1, [r3, #0xc]     ; word 3 = VA of CA 0x40048000
```
Dump: `0x40048010 = 0x834c4000` (pointer), `0x40048014 = 0x000004b0` (0x4b0 = 1200, the step),
`0x40048018 = 0x00000003` (num), `0x40048618 = 0x00000000` (bypass, disabled). Three named registers
matching three consecutive dump words is the strongest single confirmation of the index mapping.

**word 5 (`+0x14`) = CA 0x40040000 / 0x40060000** -- see S5.

**word 7 (`+0x1c`) = CA 0x40052000 / 0x40072000** -- a register at `+0x2ac` that *holds a device CA*
(the counterpart get/set pair converts it through the CA/VA translators):
```
unnamed getter @0x352ec                      unnamed setter @0x35400
0x035328  1c0093e5  ldr r0, [r3, #0x1c]       0x03541c  1c0093e5  ldr r0, [r3, #0x1c]
0x035334  ac1290e5  ldr r1, [r0, #0x2ac]      0x035424  ac128015  strne r1, [r0, #0x2ac]
0x035348  bl        oal_pcie_devca_to_hostva
```
Dump: `0x400522ac = 0x00000000`.

**word 8 (`+0x20`) = CA 0x40050000 / 0x40070000** -- host-MAC interrupt status read:
```
shuangta_get_host_mac_int_status @0x34ae0
0x034af0  4c3190e5  ldr r3, [r0, #0x14c]
0x034afc  200093e5  ldr r0, [r3, #0x20]; word 8 = VA of CA 0x40050000
0x034b04  00309015  ldrne r3, [r0]; r3 = *(+0x00)
```
Dump: `0x40050000 = 0x00000400`.

---

## 5. Where the *ring* registers live: word 5 = CA 0x40040000 / 0x40060000

This is the block that carries base/depth/wptr/rptr, and it is reached exclusively as array word 5
(`+0x14`) -- never as the CA the task named.

### 5.1 Offset table (evidence: the `hal_set_*` accessors + the callers that use them)

| offset | meaning | format | evidence |
| --- | --- | --- | --- |
| `+0x00` | `tx_ba_info_buff_depth` | bits 15:0 | `hal_set_tx_ba_info_buf_depth_cfg_tx_ba_info_buff_depth` @`0x326ac`: `0x0326b8  1230cfe7  bfi r3, r2, #0, #0x10` / `0x0326bc  003081e5  str r3, [r1]` |
| `+0x04` | `tx_ba_info_buf_addr` (device VA of a host buffer) | word | `hal_set_tx_ba_info_buf_addr_cfg_tx_ba_info_buf_addr` @`0x325e4`: `0x0325ec  043081e5  str r3, [r1, #4]`; caller `shuangta_ba_info_res_alloc` @`0x37e4c` passes `[array+0x14]` |
| `+0x10` | `msdu_info_ring_ptr_table_base` | word | `hal_set_msdu_info_ring_ptr_table_base_cfg_msdu_info_ring_ptr_table_base` @`0x326c4`: `0x0326cc  103081e5  str r3, [r1, #0x10]` |
| `+0x1c` | `rx_norm_buff_len` (bits 15:0) **and** `rx_small_buff_len` (bits 31:16) | two u16 | `hal_set_rx_data_buff_len_cfg_rx_norm_buff_len` @`0x326d4`: `0x0326e0  1230cfe7  bfi r3, r2, #0, #0x10` / `str r3,[r1,#0x1c]`; `..._cfg_rx_small_buff_len` @`0x3256c`: `0x032578 ... bfi r3,r2,#0x10,#0x10` / `0x03257c  1c3081e5  str r3, [r1, #0x1c]` |
| `+0x20` | `rx_norm_data_free_ring_addr` | word | `..._cfg_rx_norm_data_free_ring_addr` @`0x326ec`: `0x0326f4  203081e5  str r3, [r1, #0x20]` |
| `+0x24` | `rx_small_data_free_ring_addr` | word | `..._cfg_rx_small_data_free_ring_addr` @`0x32714`: `0x03271c  243081e5  str r3, [r1, #0x24]` |
| `+0x28` | `rx_data_cmp_ring_addr` | word | `..._cfg_rx_data_cmp_ring_addr` @`0x3263c`: `0x032644  283081e5  str r3, [r1, #0x28]` |
| `+0x2c` | `rx_norm_data_free_ring_size` | bits 11:0 | `..._cfg_rx_norm_data_free_ring_size` @`0x3264c`: `0x032658  1230cbe7  bfi r3, r2, #0, #0xc` / `0x03265c  2c3081e5  str r3, [r1, #0x2c]` |
| `+0x30` | `rx_small_data_free_ring_size` | bits 11:0 | `..._cfg_rx_small_data_free_ring_size` @`0x325a8`: `0x0325b4  1230cbe7  bfi r3, r2, #0, #0xc` / `0x0325b8  303081e5  str r3, [r1, #0x30]` |
| `+0x34` | `rx_data_cmp_ring_size` | bits 11:0 | `..._cfg_rx_data_cmp_ring_size` @`0x326fc`: `0x032708  1230cbe7  bfi r3, r2, #0, #0xc` / `0x03270c  343081e5  str r3, [r1, #0x34]` |
| `+0x38` | `rx_norm_data_free_ring_wptr` | bits 14:0 index + bit 15 phase | `hal_set_rx_norm_data_free_ring_wptr_...` @`0x325c0`: `0x0325c0  383092e5  ldr r3, [r2, #0x38]` ... `0x0325d8  1c30cfe7  bfi r3, ip, #0, #0x10` / `0x0325dc  383082e5  str r3, [r2, #0x38]` |
| `+0x3c` | `rx_small_data_free_ring_wptr` | idem | `..._cfg_rx_small_data_free_ring_wptr` @`0x32618`: `0x032618  3c3092e5  ldr r3, [r2, #0x3c]` / `0x032634  3c3082e5  str r3, [r2, #0x3c]` |
| `+0x40` | `rx_data_cmp_ring_rptr` | idem | `..._cfg_rx_data_cmp_ring_rptr` @`0x325f4`: `0x0325f4  403092e5  ldr r3, [r2, #0x40]` / `0x032610  403082e5  str r3, [r2, #0x40]` |
| `+0x44` | host MAC interrupt **status** (write 1 to clear) | word | `shuangta_clear_host_mac_int_status` @`0x34be8`: `0x034c04  140093e5  ldr r0, [r3, #0x14]` / `0x034c0c  44108015  strne r1, [r0, #0x44]` |
| `+0x48` | host MAC interrupt **mask** | word | get: `shuangta_get_host_mac_int_mask` @`0x34b60`: `0x034b8c  48309015  ldrne r3, [r0, #0x48]`; write: `shuangta_host_mac_irq_unmask` @`0x34c64` `0x034c98  481080e5  str r1, [r0, #0x48]` (RMW with `bic`), `shuangta_host_mac_irq_mask` @`0x34cec` (RMW with `orr`), and `hal_set_host_intr_mask_cfg_host_intr_mask` @`0x32724` `0x03272c  483081e5  str r3, [r1, #0x48]` |
| `+0x4c` | `rx_data_cmp_ring_wptr` | bits 14:0 + phase | `..._cfg_rx_data_cmp_ring_wptr` @`0x32584`: `0x032584  4c3092e5  ldr r3, [r2, #0x4c]` / `0x0325a0  4c3082e5  str r3, [r2, #0x4c]` |
| `+0x50` | `rx_norm_data_free_ring_rptr` | idem | `..._cfg_rx_norm_data_free_ring_rptr` @`0x32664`: `0x032664  503092e5  ldr r3, [r2, #0x50]` / `0x032680  503082e5  str r3, [r2, #0x50]` |
| `+0x54` | `rx_small_data_free_ring_rptr` | idem | `..._cfg_rx_small_data_free_ring_rptr` @`0x32688`: `0x032688  543092e5  ldr r3, [r2, #0x54]` / `0x0326a4  543082e5  str r3, [r2, #0x54]` |

The three wptr/rptr setters are read-modify-write -- they preserve the register's other bits and write
only `bits 15:0`, where `[14:0]` is the index and `bit 15` the phase/toggle (the same packing plat.ko
uses for the ETE ring indices, `phase21/sr-pump.md` SA.1). That is why the live `+0x38`/`+0x3c` read
`0x8000` -- index 0 with the phase bit set.

### 5.2 The callers bind every register to a named ring

`shuangta_rx_host_init_dscr_queue` @`0x37898` (r5 = `[dev+0x14c]`, `0x0379bc  4c5194e5  ldr r5, [r4, #0x14c]`):

| call | argument base | register written |
| --- | --- | --- |
| `0x037984 bl hal_set_rx_data_buff_len_cfg_rx_norm_buff_len` | `0x037980 ldr r1,[r5,#0x14]` | `+0x1c[15:0]` |
| `0x03799c bl ..._rx_norm_data_free_ring_addr` | `0x037998 ldr r1,[r5,#0x14]` | `+0x20` |
| `0x0379a8 bl ..._rx_norm_data_free_ring_size` | `0x0379a4 ldrh r0,[r4,#0x94]` (the depth) | `+0x2c` |
| `0x037a58 bl ..._rx_small_buff_len` | `0x037a54 ldr r1,[r5,#0x14]` | `+0x1c[31:16]` |
| `0x037a70 bl ..._rx_small_data_free_ring_addr` | `0x037a6c ldr r1,[r5,#0x14]` | `+0x24` |
| `0x037a7c bl ..._rx_small_data_free_ring_size` | `0x037a78 ldrh r0,[r4,#0xe4]` | `+0x30` |
| `0x037b20 bl ..._rx_data_cmp_ring_addr` | `0x037b1c ldr r1,[r5,#0x14]` | `+0x28` |
| `0x037b2c bl ..._rx_data_cmp_ring_size` | `0x037b28 ldrh r0,[r4,#0xbc]` | `+0x34` |

and each address argument is the output of `pcie_if_hostca_to_devva` (`0x037994`, `0x037a68`,
`0x037b18`), i.e. these are host DRAM ring buffers seen as device addresses -- the same class as the
port's SR ring base. The corresponding host-side ring objects keep the *register pointers*:
`0x037968  143095e5  ldr r3, [r5, #0x14]` / `0x03796c  2c3083e2  add r3, r3, #0x2c` -> ring struct `+0xa8`, and
`0x037980/0x0379a8` etc. for the rest; so the vendor's `hal_ring_*` helpers (`0x31e2c..0x323cc`) read and
write wptr/rptr through exactly these pointers.

`shuangta_ba_info_res_alloc` @`0x37d28` binds the BA-info buffer:
```
0x037dc4  4c5194e5  ldr r5, [r4, #0x14c]
0x037e28  143095e5  ldr r3, [r5, #0x14]; word 5 base
0x037e30  1c3184e5  str r3, [r4, #0x11c]; +0x00 (depth+addr pair kept as ring ptrs)
0x037e3c  143095e5  ldr r3, [r5, #0x14]
0x037e44  243184e5  str r3, [r4, #0x124]
0x037e4c  141095e5  ldr r1, [r5, #0x14]
0x037e50  bl        hal_set_tx_ba_info_buf_addr_cfg_tx_ba_info_buf_addr
0x037e58  b400dbe1  ldrh r0, [fp, #4]; depth (u16)
0x037e5c  bl        hal_set_tx_ba_info_buf_depth_cfg_tx_ba_info_buff_depth
```
`shuangta_set_msdu_info_ring_ptr_table_base` @`0x38ebc`:
```
0x038ecc  4c3193e5  ldr r3, [r3, #0x14c]
0x038ed8  141093e5  ldr r1, [r3, #0x14]
0x038ee8  bl        hal_set_msdu_info_ring_ptr_table_base_cfg_msdu_info_ring_ptr_table_base
```
`shuangta_host_chip_irq_init` @`0x3a6dc` initialises the mask (the constant is the dump's value):
```
0x03a784  141093e5  ldr r1, [r3, #0x14]; word 5 base
0x03a7a0  1c1095e5  ldr r1, [r5, #0x1c]; the mask register pointer
0x03a7a4  1f00a0e3  mov r0, #0x1f
0x03a7a8  bl        hal_set_cfg_intr_mask_host_cfg_intr_mask_host
0x03a7bc  c10c0fe3  movw r0, #0xfcc1
                          movt r0, #0xffff        ; 0xfffffcc1 -> hal_set_host_intr_mask
```

### 5.3 Dump values for word 5 (CA 0x40040000)

```
0x40040000 = 00000100   tx_ba_info_buff_depth = 256
0x40040004 = 83fd7000   tx_ba_info_buf_addr   (device VA of a host buffer)
0x40040010 = 02060850   msdu_info_ring_ptr_table_base
0x4004001c = 00b0063c   rx_norm_buff_len = 0x063c (1596), rx_small_buff_len = 0x00b0 (176)
0x40040020 = 844c7000   rx_norm_data_free_ring  addr
0x40040024 = 844c8000   rx_small_data_free_ring addr
0x40040028 = 83734000   rx_data_cmp_ring        addr
0x4004002c = 00000100   rx_norm_data_free_ring  size = 256
0x40040030 = 00000200   rx_small_data_free_ring size = 512
0x40040034 = 00000fff   rx_data_cmp_ring        size = 4095
0x40040038 = 00008000   rx_norm_data_free_ring  wptr  (index 0, phase 1)
0x4004003c = 00008000   rx_small_data_free_ring wptr  (index 0, phase 1)
0x40040040 = 00000000   rx_data_cmp_ring        rptr
0x40040044 = 00000000   host_mac_int_status (clear)
0x40040048 = fffffcc1   host_mac_int_mask  == the module's own movw #0xfcc1 / movt #0xffff
0x4004004c = 00000000   rx_data_cmp_ring wptr
0x40040050 = 00000000   rx_norm_data_free_ring rptr
0x40040054 = 00000000   rx_small_data_free_ring rptr
```
The mask word renders `0xfffffcc1` for both the module constant (`0x3a7bc`) and the live register
(`0x40040048`) -- an independent, one-word confirmation that word 5 is CA `0x40040000`.

---

## 6. What this means for the deposit question, and what stays unproven

1. **The task's two named dump words are not a ring base and an index.** `0x4004a004` is the CSI
   buffer address and `0x4004a008` its **size** (`0x6140`, the vendor's own constant in
   `hmac_csi_init`). The phase-42 note "channel 5's base+4 = a host-DRAM ring base, base+8 = an index"
   is corrected: it is a host-DRAM *buffer* (yes) but the neighbour is a size, not an index, and the
   block's role is CSI, not message transfer. A sibling lane of this same phase (`vendor-port-dr-diff.md`)
   reads `+0x08` as a "packed index (low 10 bits 0x140, phase bit 10)"; that reading is contradicted by
   the two named `..._buf_size` accessors and by `hmac_csi_init`'s own constant: the same `movw r7, #0x6140`
   (`0xbae38`) is passed to `shuangta_to_hmac_csi_set_buf_size` (`0xbae54`), and a 24896-byte CSI
   buffer is a size, not a 15-bit ring index. The two lanes agree that `0x4004a000` is the CSI block and
   that it is not the DR ring.
2. **The deposit-path ring registers are in a different array word of the same CA family** -- word 5,
   CA `0x40040000` (2 GHz) / `0x40060000` (5 GHz), offsets `+0x20..+0x54` (S5). If the question is
   "which channel register does the vendor set that the port never does", the concrete candidates are
   `+0x20/+0x24/+0x28` (the three RX ring **addr** registers, written with `pcie_if_hostca_to_devva`
   output), `+0x2c/+0x30/+0x34` (sizes), `+0x38/+0x3c/+0x40/+0x4c/+0x50/+0x54` (wptr/rptr) and
   `+0x48 = 0xfffffcc1` (host MAC interrupt mask). The port (`lab/wifidrv1/wifidrv1.c`) touches only the
   ETE block (`ETE_SR_BASEREG 0x010`, `ETE_SR_WPTR 0x018`, `ETE_SR_RPTR 0x01c`, `ETE_DR_BASEREG 0x030`
   inside CA `0x4003a000`) and the message block (`0x40039000`); it has no code path to CA
   `0x4004xxxx` at all.
3. **Reachability caution (unsettled, not tested here).** The port's own comment says its region-3
   viewport is `host 0x403b8000 -> dev CA 0x40000000, size 0x120000` (which would put CA `0x4004a000`
   at BAR0 `0x402a000`), while the task states a host read of BAR0 `0x402a004` bus-errors. This report
   proposes no CA-readable address for any of these registers; the divergence between the declared
   region size and the measured fault is a device-side question, recorded here rather than resolved.
4. **Not settled:** the meaning of `+0x414 = 0x2710` in the CSI block (no accessor, no reader in either
   module); whether the whitelist table really spans the full 32 entries at runtime (the dump is all
   zero inside it, and `0x4004a414` sits just past entry 31's last word `+0x400`); and the roles of the
   remaining array words 1, 2, 6 and 9 (no accessor found that names them -- only word 0, 3, 4, 5, 7 and
   8 are named by the module's own symbols).

---

## Appendix A -- reproduction (every quoted instruction)

```
cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2
./pyenv/Scripts/python.exe - <<'PY'
from elftools.elf.elffile import ELFFile
from capstone import *
e = ELFFile(open('opensource/build/tmp/hi5622v100_wifi.ko','rb'))
tx = e.get_section_by_name('.text'); b = tx.data()
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)          # .text sh_addr = 0, sh_offset = 0x38
for a in (0x34f7c, 0x34f88, 0x34f90, 0x35078, 0x35084, 0x3508c, 0x35104, 0x35110, 0x3512c,
          0x35200, 0x3520c, 0x35214, 0x3527c, 0x35288, 0x35290, 0x35498, 0x354cc, 0x354d0,
          0x35584, 0x35590, 0x35598, 0x34af0, 0x34b8c, 0x34c0c, 0x34c98, 0x32584, 0x325c0,
          0x325f4, 0x32618, 0x32664, 0x32688, 0x326ac, 0x326c4, 0x326e4, 0x326ec, 0x32714,
          0x324e4, 0x3253c, 0x32484, 0x34ee8, 0x37e50, 0x37e5c, 0x38ee8, 0x3a7a8, 0x33ff8,
          0x3400c, 0x34960, 0x34968, 0x349c4, 0x34a04):
    for i in md.disasm(b[a:a+4], a):
        print('0x%06x  %s  %s %s' % (i.address, ''.join('%02x' % x for x in i.bytes),
                                     i.mnemonic, i.op_str))
PY
```
Byte counts in the output are the raw ARM words; `bl` sites are printed with their target because the
call symbol comes from `.rel.text` (e.g. `0x03400c -> oal_pcie_devca_to_hostva`,
`0x037e50 -> hal_set_tx_ba_info_buf_addr_cfg_tx_ba_info_buf_addr`). Array and table contents come from
`rodata` at file offset `0x167b00 + off` (the arrays verified for this report: `rodata+b60/b88`,
`rodata+bb0/bd8`, `rodata+c00`).

## Appendix B -- every `reg_all.txt` value quoted

Read from `build/register-dumps/reg_all.txt` (`addr = HEX, value = HEX`, 25,154 words; contiguous
runs of 4 bytes):

```
40040000=00000100  40040004=83fd7000  40040010=02060850  4004001c=00b0063c
40040020=844c7000  40040024=844c8000  40040028=83734000  4004002c=00000100
40040030=00000200  40040034=00000fff  40040038=00008000  4004003c=00008000
40040040=00000000  40040044=00000000  40040048=fffffcc1  4004004c=00000000
40040050=00000000  40040054=00000000
40048010=834c4000  40048014=000004b0  40048018=00000003  40048618=00000000
400420a8=00000006  40042924=00000000  40042928=00000064
4004a000=00000000  4004a004=84a90000  4004a008=00006140  4004a00c=00000000
4004a010=00000000  4004a014=00000000  4004a414=00002710
40050000=00000400  40050004=00000000  40050008=00000020  4005000c=00000020  40050010=00000000
400522ac=00000000
```
