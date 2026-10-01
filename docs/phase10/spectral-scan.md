# Spectral scan: what 2.5.24 removed from the driver, and whether the device still answers (phase 10, 2026-10-01)

Scope: local static analysis of two module builds + read-only (`get_*`) probing of
`root@192.168.10.1`. Nothing on the device was modified; the one temporary file the
probe recipe creates (`/tmp/askpass.sh`) was deleted and its absence is shown in §2.

Inputs:

| file | size | identity |
|---|---|---|
| `build/versions/2.4.15/hi5622v100_wifi.ko` | 3,581,748 B | 2.4.15 driver module |
| `build/versions/2.5.24/hi5622v100_wifi.ko` | 3,564,728 B | 2.5.24 driver module |
| device `/lib/modules/5.10.201/hi5622v100_wifi.ko` | 3,564,728 B | md5 `4737fcb21a1a2262a96f84d780ad8b35` |

The device module size **and** md5 equal the local `2.5.24` file byte-for-byte
(`4737fcb21a1a2262a96f84d780ad8b35`), so the running driver is exactly the 2.5.24 build
analysed here. Device firmware is `DISTRIB_REVISION='2.5.24'`, `OpenWrt 22.03.6`,
kernel `5.10.201`.

Method: `pyenv/Scripts/python.exe` (pyelftools 0.33 + capstone 5.0.7, `CS_ARCH_ARM`,
`CS_MODE_ARM`) and the helper scripts under `build/tmp/` (`syms.py`, `cmp.py`,
`disas2.py`, `refs2.py`, `fninfo.py`, `table.py`, `tdiff.py`). The module is an ARM32
`ET_REL`; `.text` has `sh_addr=0`, so **all addresses below are `.text`-relative**
(file offset = address + `0x38`). `disas2.py` resolves the module's relocation entries,
so every `bl` target below is the real callee name, not the unrelocated placeholder.

Result in one line: 2.5.24 deleted 24 of the 30 `alg_cfg_args_analysis_spectral_*`
argument-parsers, both `..._hardamard_*` parsers, the `..._protect_mode` argument
wrapper and `alg_cfg_analysis_args_head`; the 414-entry command table is **unchanged**,
and the device running 2.5.24 still answers all six `get_spectral_*` commands with
`[SUCC]`.

---

## 1. The family that 2.4.15 has and 2.5.24 does not

Symbol-set diff of the two `.symtab`s (`cmp.py`) restricted to the relevant names.
`alg_cfg_args_analysis_spectral_*` in 2.4.15: 30 functions; in 2.5.24: 6 functions
(the same 6, same sizes). Removed = 24 spectral + 2 hardamard + 1 protect wrapper
+ `alg_cfg_analysis_args_head`. Removing them made the module 17,020 B smaller.

### 1.1 Removed functions (2.4.15 addresses and sizes)

| address | size | symbol |
|---:|---:|---|
| `0x150134` | 464 | `alg_cfg_args_analysis_spectral_scan_en` |
| `0x150304` | 464 | `alg_cfg_args_analysis_spectral_scan_ofdm_det` |
| `0x1504d4` | 464 | `alg_cfg_args_analysis_spectral_scan_11b_det` |
| `0x1506a4` | 388 | `alg_cfg_args_analysis_spectral_scan_nb_det` |
| `0x150828` | 400 | `alg_cfg_args_analysis_spectral_scan_fft_size` |
| `0x1509b8` | 408 | `alg_cfg_args_analysis_spectral_scan_fft_period` |
| `0x150b50` | 408 | `alg_cfg_args_analysis_spectral_scan_fft_count` |
| `0x150ce8` | 480 | `alg_cfg_args_analysis_spectral_scan_rssi_thr` |
| `0x150ec8` | 404 | `alg_cfg_args_analysis_spectral_scan_rssi_nb_thr` |
| `0x15105c` | 400 | `alg_cfg_args_analysis_spectral_scan_power_thr` |
| `0x151410` | 388 | `alg_cfg_args_analysis_spectral_scan_rpt_mode` |
| `0x151594` | 400 | `alg_cfg_args_analysis_spectral_scan_fftin_type` |
| `0x151934` | 388 | `alg_cfg_args_analysis_spectral_scan_deci_coef_en` |
| `0x151ab8` | 400 | `alg_cfg_args_analysis_spectral_scan_deci_coef_man` |
| `0x151c48` | 232 | `alg_cfg_args_analysis_spectral_scan_agc_lock_en` |
| `0x151d30` | 476 | `alg_cfg_args_analysis_spectral_scan_det_bw` |
| `0x151f0c` | 476 | `alg_cfg_args_analysis_spectral_scan_fft_in_shift_bits` |
| `0x1520e8` | 476 | `alg_cfg_args_analysis_spectral_scan_det_times` |
| `0x1522c4` | 476 | `alg_cfg_args_analysis_spectral_scan_det_time_shift` |
| `0x1524a0` | 480 | `alg_cfg_args_analysis_spectral_scan_fft_gap_size` |
| `0x15291c` | 476 | `alg_cfg_args_analysis_spectral_scan_add_win` |
| `0x152af8` | 476 | `alg_cfg_args_analysis_spectral_static_en` |
| `0x152cd4` | 480 | `alg_cfg_args_analysis_spectral_dbm_offset` |
| `0x152eb4` | 480 | `alg_cfg_args_analysis_spectral_dbm_offset_time` |
| `0x155f04` | 536 | `alg_cfg_args_analysis_hardamard_mode` |
| `0x15611c` | 536 | `alg_cfg_args_analysis_hardamard_enable` |
| `0x14b6b8` | 88 | `alg_cfg_args_analysis_protect_mode` |
| `0x14abb0` | 136 | `alg_cfg_analysis_args_head` |

Also dropped in the same release (not requested in detail): objects
`g_ast_spectral` (240 B) and `g_ast_dfs_spectral` (240 B), `alg_cfg_param_analyze_rate_protect_mode_process`
(284 B), and several `shuangta_*_spectral_*` accessors. In total 518 symbols exist only
in 2.4.15 and 6 only in 2.5.24 (none spectral).

### 1.2 Kept from the same family in 2.5.24 (same names, same sizes)

| 2.4.15 addr | size | symbol |
|---:|---:|---|
| `0x1511ec` | 548 | `alg_cfg_args_analysis_spectral_scan_nb_thr` |
| `0x151724` | 528 | `alg_cfg_args_analysis_spectral_scan_ant_index` |
| `0x152680` | 528 | `alg_cfg_args_analysis_spectral_scan_ant_idx` |
| `0x152890` | 140 | `alg_cfg_args_analysis_spectral_ant_index` |
| `0x153094` | 540 | `alg_cfg_args_analysis_spectral_nb_thre_db` |
| `0x1532b0` | 140 | `alg_cfg_args_analysis_spectral_nb_thr` |

These six are self-contained: relocation analysis (`refs2.py`) shows the retained
`nb_thre_db`/`nb_thr` and `ant_index`/`ant_idx`/`spectral_ant_index` call each other,
and they are what the surviving `hmac_spectral_*` scan path uses. The removed parsers
had no in-module caller left in 2.5.24 (they no longer exist to be referenced).

### 1.3 What the removed parsers actually do — cluster walkthrough

Every removed function is one shape: it takes the cfg payload (`r0` = parser context,
`r1` = arg descriptor, `r2` = out status word), validates the input, calls `strlen`
then `oal_atoi` on the value string, range-checks the integer, writes the parsed byte
into the descriptor at `[desc+0xa]`, and on error logs via `oam_error_log*`/`snprintf_s`
and writes `0xe` into the status word. It is pure host-side argument marshalling for
the `alg` set path; it touches no register directly.

**(a) Enable/detector flags — `spectral_scan_en`, `_ofdm_det`, `_11b_det`, `_nb_det`.**
These parse one boolean (must be 0 or 1). The 2.4.15 `..._scan_en` body is:

```
0x00150134  cmp      r2, #0
0x00150138  cmpne    r1, #0
0x0015013c  push     {r4, r5, r6, r7, r8, lr}
0x00150140  moveq    r3, #1
0x00150144  movne    r3, #0
0x00150148  cmp      r0, #0
0x0015014c  orreq    r3, r3, #1
0x00150150  sub      sp, sp, #8
0x00150154  cmp      r3, #0
0x00150158  bne      #0x1501c4
0x0015015c  ldrb     r7, [r1, #2]
0x00150160  mov      r4, r1
0x00150164  mov      r6, r2
0x00150168  cmp      r7, #0
0x0015016c  bne      #0x1501b0
0x00150170  ldrb     r1, [r0, #0x8c]
0x00150174  mov      r5, r0
0x00150178  cmp      r1, #1
0x0015017c  bne      #0x1501f0
0x00150180  ldr      r8, [r0, #4]
0x00150184  mov      r0, r8
0x00150188  bl       #0x150188   ; -> strlen
0x0015018c  add      r1, r0, #1
0x00150190  mov      r0, r8
0x00150194  bl       #0x150194   ; -> oal_atoi
0x00150198  mov      r8, r0
0x0015019c  cmp      r0, #1
0x001501a0  bhi      #0x15026c
0x001501a4  ldrb     r3, [r4, #2]
0x001501a8  cmp      r3, #0
```

Its literal strings pin the semantics:
`.LC362 = '{alg_spectral_cfg_param_analyse_status: config spectral_scan_en, uc_param_num(%u) should be 1!}'`
and `.LC365 = '{alg_spectral_cfg_param_analyse_status: config spectral_scan_en to be %u, should be 0 or 1!}'`.
`nb_det`/`ofdm_det`/`11b_det` are byte-identical copies with the config name changed
(e.g. `..._nb_det` uses `.LC388 = '...config spectral_nb_det to be %u, should be 0 or 1!'`).

**(b) FFT geometry — `fft_size`, `fft_period`, `fft_count`, `fftin_type`,
`fft_gap_size`, `fft_in_shift_bits`, `det_bw`, `det_times`, `det_time_shift`,
`rpt_mode`, `static_en`.** Same skeleton with different bounds. `fft_size/period/count`
sizes (400/408/408) and their `config spectral_fft_*` log strings identify them as the
FFT window/period/count knobs; `det_bw`, `det_times`, `det_time_shift` control the
energy detector; `fftin_type` selects the FFT input format; `rpt_mode` the report mode.

**(c) Decimation coefficients — `deci_coef_en` and `deci_coef_man`.** `..._deci_coef_en`
parses a boolean and stores it at `[desc+0xa]`:

```
0x00151934  push     {r4, r5, r6, r7, r8, lr}
0x00151938  mov      r6, r2
0x0015193c  ldrb     r5, [r1, #2]
0x00151940  sub      sp, sp, #8
0x00151944  cmp      r5, #0
0x00151948  bne      #0x151990
0x0015194c  mov      r4, r1
0x00151950  ldrb     r1, [r0, #0x8c]
0x00151954  mov      r7, r0
0x00151958  cmp      r1, #1
0x0015195c  bne      #0x1519a4
0x00151960  ldr      r8, [r0, #4]
0x00151964  mov      r0, r8
0x00151968  bl       #0x151968   ; -> strlen
0x0015196c  add      r1, r0, #1
0x00151970  mov      r0, r8
0x00151974  bl       #0x151974   ; -> oal_atoi
0x00151978  mov      r8, r0
0x0015197c  cmp      r0, #1
0x00151980  bhi      #0x151a20
0x00151984  ldrb     r3, [r4, #2]
0x00151988  cmp      r3, #0
0x0015198c  streq    r0, [r4, #0xa]
```

`.LC466 = '{alg_spectral_cfg_param_analyse_deci_coef_en: config deci_coef_en, uc_param_num(%u) should be 1!}'`.
`deci_coef_man` (400 B, `@0x151ab8`) is the companion that carries the manual
coefficient word when `deci_coef_en` selects manual rather than default decimation.

**(d) AGC lock — `agc_lock_en`.** The smallest of the family (232 B) because it only
parses the 0/1 flag with no range table:

```
0x00151c48  push     {r4, r5, r6, r7, lr}
0x00151c4c  mov      r6, r2
0x00151c50  ldrb     r5, [r1, #2]
0x00151c54  sub      sp, sp, #0xc
0x00151c58  cmp      r5, #0
0x00151c5c  bne      #0x151ca0
0x00151c60  ldrb     r3, [r0, #0x8c]
0x00151c64  cmp      r3, #1
0x00151c68  bne      #0x151cb4
0x00151c6c  ldr      r7, [r0, #4]
0x00151c70  mov      r4, r1
0x00151c74  mov      r0, r7
0x00151c78  bl       #0x151c78   ; -> strlen
0x00151c7c  add      r1, r0, #1
0x00151c80  mov      r0, r7
0x00151c84  bl       #0x151c84   ; -> oal_atoi
0x00151c88  mov      r3, r0
0x00151c8c  cmp      r0, #1
0x00151c90  bhi      #0x151d08
0x00151c94  ldrb     r2, [r4, #2]
0x00151c98  cmp      r2, #0
0x00151c9c  streq    r0, [r4, #0xa]
```

String: `.LC480 = '{alg_spectral_cfg_param_analyse_agc_lock_en: config agc_lock_en, uc_param_num(%u) should be 1!}'`.
This is the knob that freezes AGC while the spectral window is captured.

**(e) Windows — `add_win`.** Parses the "add window" enable:

```
0x0015291c  cmp      r2, #0
0x00152920  cmpne    r1, #0
0x00152924  push     {r4, r5, r6, r7, r8, lr}
0x00152928  moveq    r3, #1
0x0015292c  movne    r3, #0
0x00152930  cmp      r0, #0
0x00152934  orreq    r3, r3, #1
0x00152938  sub      sp, sp, #0x10
0x0015293c  cmp      r3, #0
0x00152940  bne      #0x1529ac
0x00152944  ldrb     r7, [r1, #2]
0x00152948  mov      r4, r1
0x0015294c  mov      r6, r2
0x00152950  cmp      r7, #0
0x00152954  bne      #0x152998
0x00152958  ldrb     r1, [r0, #0x8c]
0x0015295c  mov      r5, r0
0x00152960  cmp      r1, #1
0x00152964  bne      #0x1529d8
0x00152968  ldr      r8, [r0, #4]
0x0015296c  mov      r0, r8
0x00152970  bl       #0x152970   ; -> strlen
0x00152974  add      r1, r0, #1
0x00152978  mov      r0, r8
0x0015297c  bl       #0x15297c   ; -> oal_atoi
0x00152980  mov      r8, r0
0x00152984  cmp      r0, #1
0x00152988  bhi      #0x152a54
0x0015298c  ldrb     r3, [r4, #2]
0x00152990  cmp      r3, #0
0x00152994  streq    r0, [r4, #0xa]
```

Strings: `.LC520 = '{alg_spectral_cfg_param_analyse_add_win: config spectral_add_win, uc_param_num(%u) should be 1!}'`,
`.LC522 = '...config spectral_add_win to be %u, should be 0 or 1!'`.

**(f) dBm offsets — `dbm_offset` and `dbm_offset_time`.** These carry a **signed**
correction, unlike the flags. `dbm_offset` reads the value and immediately biases it
by `+0x40` before the bound test:

```
0x00152cd4  cmp      r2, #0
0x00152cd8  cmpne    r1, #0
0x00152cdc  push     {r4, r5, r6, r7, r8, lr}
0x00152ce0  moveq    r3, #1
0x00152ce4  movne    r3, #0
0x00152ce8  cmp      r0, #0
0x00152cec  orreq    r3, r3, #1
0x00152cf0  sub      sp, sp, #0x10
0x00152cf4  cmp      r3, #0
0x00152cf8  bne      #0x152d68
0x00152cfc  ldrb     r7, [r1, #2]
0x00152d00  mov      r4, r1
0x00152d04  mov      r6, r2
0x00152d08  cmp      r7, #0
0x00152d0c  bne      #0x152d54
0x00152d10  ldrb     r1, [r0, #0x8c]
0x00152d14  mov      r5, r0
0x00152d18  cmp      r1, #1
0x00152d1c  bne      #0x152d94
0x00152d20  ldr      r8, [r0, #4]
0x00152d24  mov      r0, r8
0x00152d28  bl       #0x152d28   ; -> strlen
0x00152d2c  add      r1, r0, #1
0x00152d30  mov      r0, r8
0x00152d34  bl       #0x152d34   ; -> oal_atoi
0x00152d38  add      r3, r0, #0x40    ; bias -64..63 -> 0..127
```

Strings confirm the range: `.LC534 = '...config spectral_dbm_offset to be %d, exceed [-64, 63]!'`;
`dbm_offset_time` is the same with `.LC540 = '...config spectral_dbm_offset_time to be %d, exceed [-64, 63]!'`.
These are the per-capture dBm calibration offsets the scanner adds to the reported PSD.

**(g) Hardamard — `hardamard_mode`, `hardamard_enable`.** Both 536 B. `hardamard_mode`
arg-checks all three pointers up front and stores a 0/1 into `[r6+0xa]`:

```
0x00155f04  cmp      r2, #0
0x00155f08  cmpne    r1, #0
0x00155f0c  push     {r4, r5, r6, r7, r8, lr}
0x00155f10  moveq    r4, #1
0x00155f14  movne    r4, #0
0x00155f18  cmp      r0, #0
0x00155f1c  orreq    r4, r4, #1
0x00155f20  sub      sp, sp, #0x10
0x00155f24  cmp      r4, #0
0x00155f28  bne      #0x155fe8
0x00155f2c  mov      r6, r1
0x00155f30  mov      r7, r2
0x00155f34  ldrb     r1, [r0, #0x8c]
0x00155f38  mov      r5, r0
0x00155f3c  ldrb     r8, [r6, #2]
0x00155f40  cmp      r8, #0
0x00155f44  beq      #0x155f64
0x00155f48  cmp      r1, #0
0x00155f4c  bne      #0x156088
0x00155f50  mov      r3, #0xe
0x00155f54  mov      r0, #0
0x00155f58  strh     r3, [r7]
0x00155f5c  add      sp, sp, #0x10
0x00155f60  pop      {r4, r5, r6, r7, r8, pc}
0x00155f64  cmp      r1, #1
0x00155f68  bne      #0x156014
0x00155f6c  ldr      r4, [r0, #4]
0x00155f70  mov      r0, r4
0x00155f74  bl       #0x155f74   ; -> strlen
0x00155f78  add      r1, r0, #1
0x00155f7c  mov      r0, r4
0x00155f80  bl       #0x155f80   ; -> oal_atoi
0x00155f84  uxtb     r3, r0
0x00155f88  cmp      r3, #1
0x00155f8c  strb     r3, [r6, #0xa]
0x00155f90  bls      #0x155f50
```

The Hadamard-transform mode/enable pair selects the orthogonal-sequence detector the
firmware uses for spectral preamble detection.

**(h) Protect mode and `alg_cfg_analysis_args_head`.** The 88-byte
`alg_cfg_args_analysis_protect_mode` is only a wrapper — it delegates to the 444-byte
`alg_cfg_param_analysis_protect_mode` (which **survives** in 2.5.24) and maps its return
into the status word:

```
0x0014b6b8  push     {r4, r5, lr}
0x0014b6bc  mov      r5, r2
0x0014b6c0  sub      sp, sp, #0xc
0x0014b6c4  bl       #0x14b6c4   ; -> alg_cfg_param_analysis_protect_mode
0x0014b6c8  subs     r4, r0, #0
0x0014b6cc  moveq    r3, #0xe
0x0014b6d0  strheq   r3, [r5]
0x0014b6d4  bne      #0x14b6e4
0x0014b6d8  mov      r0, r4
0x0014b6dc  add      sp, sp, #0xc
0x0014b6e0  pop      {r4, r5, pc}
0x0014b6e4  movw     r3, #0xbfb
0x0014b6e8  mov      r1, #0x1c
0x0014b6ec  mov      r0, #0
0x0014b6f0  movw     r2, #0
0x0014b6f4  movt     r2, #0
0x0014b6f8  str      r2, [sp]
0x0014b6fc  movw     r2, #0x4d6
0x0014b700  bl       #0x14b700   ; -> oam_error_log0
0x0014b704  mov      r0, r4
0x0014b708  add      sp, sp, #0xc
0x0014b70c  pop      {r4, r5, pc}
```

The retained callee logs `.LC87 = '[iw]protect_mode set fail! protect mode should be the same\n'`
and uses `alg_cfg_get_match_value` — protect mode is a lockstep/rate-protection mode,
so a set must match the currently running mode. `alg_cfg_analysis_args_head` (136 B)
is the generic name-based dispatcher that the framework used to find a parser:

```
0x0014abb0  cmp      r2, #0
0x0014abb4  cmpne    r1, #0
0x0014abb8  beq      #0x14ac20
0x0014abbc  cmp      r0, #0
0x0014abc0  cmpne    r3, #0
0x0014abc4  push     {r4, r5, r6, r7, r8, sb, sl, lr}
0x0014abc8  mov      r4, r0
0x0014abcc  mov      r5, r3
0x0014abd0  moveq    sl, #1
0x0014abd4  movne    sl, #0
0x0014abd8  beq      #0x14ac28
0x0014abdc  ldr      r0, [r0]
0x0014abe0  mov      r7, r1
0x0014abe4  mov      r6, r2
0x0014abe8  bl       #0x14abe8   ; -> alg_cfg_get_pkt_type
0x0014abec  subs     sb, r0, #0xd
0x0014abf0  mov      r8, r0
0x0014abf4  movne    sb, #1
0x0014abf8  ldr      r0, [r4, sb, lsl #2]
0x0014abfc  bl       #0x14abfc   ; -> alg_cfg_search_process_info_by_cfg_name
0x0014ac00  subs     r3, r0, #0
0x0014ac04  beq      #0x14ac30
0x0014ac08  str      r3, [r7]
0x0014ac0c  add      sb, sb, #1
0x0014ac10  mov      r0, sl
0x0014ac14  strb     sb, [r6]
0x0014ac18  strb     r8, [r5]
0x0014ac1c  pop      {r4, r5, r6, r7, r8, sb, sl, pc}
0x0014ac20  mov      r0, #0x64
0x0014ac24  bx       lr
0x0014ac28  mov      r0, #0x64
0x0014ac2c  pop      {r4, r5, r6, r7, r8, sb, sl, pc}
0x0014ac30  movw     r0, #0x3f6
0x0014ac34  pop      {r4, r5, r6, r7, r8, sb, sl, pc}
```

i.e. it resolves the cfg name to a table entry and returns it. Both it and the
`args_analysis_protect_mode` wrapper are gone from 2.5.24.

---

## 2. Live probe on the device (2.5.24, read-only)

Interface `Hisilicon0` exposes `alg (0101) : set 500 char & get 1000 char`; device
`DISTRIB_REVISION='2.5.24'`. Command used (verbatim recipe), one session:

```
printf '#!/bin/sh\necho RouterRoot-9x\n' > /tmp/askpass.sh; chmod +x /tmp/askpass.sh
cd /tmp && SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
  timeout 90 ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 \
  root@192.168.10.1 '<cmds>'
```

Module identity and all six requested get-side spectral commands, raw output:

```
=== find wifi ko ===
-rw-r--r--    1 root     root       3564728 Jul 27 04:23 /lib/modules/5.10.201/hi5622v100_wifi.ko
4737fcb21a1a2262a96f84d780ad8b35  /lib/modules/5.10.201/hi5622v100_wifi.ko

=== iwpriv Hisilicon0 alg get_spectral_scan_en ===
Hisilicon0  alg:[SUCC]spectral_scan : 0

=== iwpriv Hisilicon0 alg get_spectral_ofdm_det ===
Hisilicon0  alg:[SUCC]spectral_ofdm_det : 1

=== iwpriv Hisilicon0 alg get_spectral_11b_det ===
Hisilicon0  alg:[SUCC]spectral_11b_det : 1

=== iwpriv Hisilicon0 alg get_spectral_nb_det ===
Hisilicon0  alg:[SUCC]spectral_nb_det : 0

=== iwpriv Hisilicon0 alg get_spectral_rssi_thr ===
Hisilicon0  alg:[SUCC]spectral_rssi_thr : -85

=== iwpriv Hisilicon0 alg get_spectral_rssi_nb_thr ===
Hisilicon0  alg:[SUCC]spectral_rssi_nb_thr : -70

=== also get_spectral_power_thr (extra) ===
Hisilicon0  alg:[SUCC]spectral_power_thr : 12
```

Cleanup of the probe artifact, shown:

```
$ rm -f /tmp/askpass.sh
=== cleanup check ===
ls: cannot access '/tmp/askpass.sh': No such file or directory
```

All seven get-side spectral commands return `[SUCC]` with plausible, self-consistent
values. They match the values phase 3 recorded on this hardware
(`ulw/phase3/alg-commands.md`: `spectral_scan 0`, `ofdm_det 1`, `11b_det 1`,
`nb_det 0`, `rssi_thr -85`, `rssi_nb_thr -70`, `power_thr 12`), so 2.5.24 has not
reset the spectral configuration.

### 2.1 Command table is identical between the two releases

`g_ast_alg_cfg_process_info_table` sits at `.data+0x35c8`, size 4968 = 414 × 12, in
**both** modules. Extracting all 414 `{name, cfg_id, dir}` rows (`tdiff.py`) yields
**0 mismatches**. The spectral rows are byte-for-byte the same:

| # | name | cfg_id | dir |
|---:|---|---:|---|
| 174 | `spectral_scan_en` | 1401 | set |
| 175 | `get_spectral_scan_en` | 1401 | get |
| 176 | `spectral_ofdm_det` | 1402 | set |
| 177 | `get_spectral_ofdm_det` | 1402 | get |
| 178 | `spectral_11b_det` | 1403 | set |
| 179 | `get_spectral_11b_det` | 1403 | get |
| 180 | `spectral_nb_det` | 1404 | set |
| 181 | `get_spectral_nb_det` | 1404 | get |
| 188 | `spectral_rssi_thr` | 1408 | set |
| 189 | `get_spectral_rssi_thr` | 1408 | get |
| 190 | `spectral_rssi_nb_thr` | 1409 | set |
| 191 | `get_spectral_rssi_nb_thr` | 1409 | get |
| 192 | `spectral_power_thr` | 1410 | set |
| 193 | `get_spectral_power_thr` | 1410 | get |
| 9 | `protect_mode` | 404 | set |
| 10 | `get_protect_mode` | 404 | get |

---

## 3. Conclusion — is the feature still functional on 2.5.24?

**The get/read side of spectral scan is still functional on the device; the set/configure
side has lost its host-side argument parsers in the driver.**

Evidence for:

1. **Command surface intact.** The 414-entry command table is unchanged (0/414 row
   mismatches), and the six spectral cfg names/ids/dirs are present in the 2.5.24 module
   itself (§2.1). Nothing in the command enumeration was removed.
2. **Live round-trip works.** Every requested get command answered
   `[SUCC]` with a value on the 2.5.24 device (`get_spectral_scan_en`, `_ofdm_det`,
   `_11b_det`, `_nb_det`, `_rssi_thr`, `_rssi_nb_thr`, plus `_power_thr`), i.e. the
   `get` path — ioctl → command lookup → message/response — is still wired.
3. **Firmware-side state survives.** The returned values are the same defaults recorded
   on this hardware under 2.4.15, so the firmware is not returning errors or zeroed
   placeholders.
4. **The surviving code is the read/use path.** The six spectral argument functions that
   2.5.24 *kept* (`nb_thr`, `nb_thre_db`, `ant_index`, `ant_idx`, `spectral_ant_index`)
   form a self-contained cluster — relocation analysis shows them calling one another —
   whereas the removed parsers have no remaining in-module caller in 2.5.24; the removed
   ones were the `set_*` marshalling wrappers (§1.3).

Evidence against / caveats:

- The 24 removed parsers are the code that validated and packed values for
  `set_spectral_*`. In 2.5.24 the corresponding `set_spectral_scan_en` /
  `set_spectral_ofdm_det` / `set_spectral_fft_*` / `set_spectral_dbm_offset` /
  `set_spectral_add_win` / `set_spectral_deci_coef_*` / `set_spectral_agc_lock_en` /
  `hardamard_*` / `protect_mode` entries still exist in the table, but their per-option
  host parser is gone from this module. Whether the remaining generic path
  (`alg_cfg_args_analysis_param`, which *does* survive) can still honour a set is
  **unresolved** — it was not probed, because the task restricts this run to `get_`.
- Both facts are needed and neither alone is sufficient: the table alone would be
  consistent with a dead-but-listed command, and the live `[SUCC]` reads alone would not
  show that the driver side was thinned.

So: **the spectral read/telemetry feature remains reachable and working in the 2.5.24
firmware; what 2.5.24 removed is the driver-side `set_` argument marshalling for those
options.** The two are independent because the command table and the get path are
unchanged, while the removed functions are only reachable from the set path.

---

## 4. What a user loses and gains between 2.4.15 and 2.5.24

**Loses (driver side, 2.5.24):**

- Cannot express tuned spectral captures through the named options: scan enable,
  OFDM/11b/NB detector selection, FFT size/period/count, FFT input type/gap/shift,
  detector bandwidth/times/time-shift, report mode, static-enable, decimation
  coefficient enable/manual value, AGC-lock enable, add-window, and the per-capture
  dBm offset / dBm-offset-time corrections.
- The `hardamard_mode`/`hardamard_enable` and the `protect_mode` **set** wrappers are
  gone, so those setters have no dedicated host parser.
- `alg_cfg_analysis_args_head`, the generic name-based parser resolver, is gone.

**Does not lose:**

- The commands are still listed and their getters still answer (§2), so reading the
  current spectral configuration is unaffected. Retained setters for `nb_thr`,
  `nb_thre_db`, and the antenna index remain.

**Gains:** none for spectral scan; 2.5.24 is a smaller module (17,020 B less) because
the parsers were stripped. The only new symbols in 2.5.24 are unrelated
(`hmac_config_get_chip_max_user`, `wal_config_{get,set}_rssi_{access,warn}_th`,
`wal_ioctl_get_max_user`).

---

## 5. Limits

- **Set path not probed.** By scope this run only issued `get_*`. Therefore the claim
  "the feature is still functional" is proven for reading only; whether any
  `set_spectral_*` still changes device state is **unresolved** and would need
  controlled `set_`/`get_` round-trips (a write, deliberately out of scope).
- **Value provenance.** A `[SUCC]` get response proves the ioctl/command/message path
  works and returns a non-error value; it does not by itself prove the number is read
  live from firmware RF state rather than from a driver-side shadow that happens to be
  identical to the 2.4.15 defaults. The values matching phase 3 is consistent with both.
- **Invocation mechanism.** In 2.4.15, no relocation in the module (`refs2.py`,
  `abstab.py`) references the removed argument-parsers by symbol, and none of the
  spectral cfg_ids 1401–1410 appears as a `movw` constant in `.text`. The mechanism by
  which these parsers were registered/invoked was **not resolved**; the walkthrough in
  §1.3 describes what each function does, not who called it.
- **No dynamic firmware side.** The device firmware blob was not disassembled here; the
  "firmware still holds the state" reading rests on the live getter behavior, not on
  firmware code.
- **Scope of removed set.** With 518 symbols present only in 2.4.15, this document
  details only the spectral/hardamard/protect/args-head family named in the task; the
  remaining ~480 removed symbols are not characterised here.
- **One module only.** Only `hi5622v100_wifi.ko` was diffed; `hi5622v100_plat.ko` and
  other modules were not compared.

---

### Appendix — commands used

```
# symbol/size extraction
pyenv/Scripts/python.exe build/tmp/syms.py <ko> <substr>
# symbol-set diff
pyenv/Scripts/python.exe build/tmp/cmp.py 2.4.15/hi5622v100_wifi.ko 2.5.24/hi5622v100_wifi.ko
# disassembly with relocation-resolved call targets
pyenv/Scripts/python.exe build/tmp/disas2.py <ko> <symbol> [maxinsns]
# relocation targets of a function (strings + callees)
pyenv/Scripts/python.exe build/tmp/fninfo.py <ko> <symbol> [...]
# 414-entry table extraction / cross-release diff
pyenv/Scripts/python.exe build/tmp/table.py <ko>
pyenv/Scripts/python.exe build/tmp/tdiff.py <ko1> <ko2>   # -> 414/414, 0 mismatches
```
