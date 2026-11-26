# SPECTRAL-S1 - the PSD bank pointer chain (S-1 half-closed)

**Lane:** monitor2 (task-246). Host-only static decode of the device-version module
(2.5.24) via the local disassembly corpus (`/tmp/2524.dis`, symbol table
`/tmp/nm-2524.txt`, accessor dump `/tmp/psd-acc.txt`). Grades: [V] verified here;
[V\*] committed earlier lane; [I] inference; [U] open.

## 0. Result

The three PSD registers (+0x430/+0x434/+0x438) and the LMI event registers
(+0x528..+0x53c, verified here: `hal_set_cfg_lmi_ctrl2_cfg_lmi_psd_en` @0x3291c is
`bfi [r1,#0x52c], bit 9, width 1`) live in **one per-band mapped CA window**. The pointer
the accessors receive is:

```
state_of_band[+0x150]  ->  64-byte object allocated by oal_mem_alloc(size=0x40, type=3)
                           in shuangta_host_initialize_phy @0x3abfc
object[+0x10]          ->  host VA of the **3rd word (index 2)** of a per-band 10-word
                           CA table, mapped with oal_pcie_devca_to_hostva(band, CA)
```

The mapping helper is the anonymous routine at **0x3a934** called from the init helper
0x3aa68. It maps a 10-word table and stores the VAs at object offsets
`+0x00,+0x04,+0x10,+0x14,+0x18,+0x1c,+0x20,+0x24,+0x28,+0x2c` **in table order** (word 0
-> +0x00 ... word 9 -> +0x2c) - this is what makes `[obj+0x10]` = table word 2.

A second helper at 0x3a830 maps a **6-word** table into the same object at
`+0x08,+0x0c,+0x30,+0x34,+0x38,+0x3c`; together the two helpers fill exactly the 64-byte
object (16 fields, no free slots). The earlier `shuangta_init_spectral_param_defaults`
claim `[state+0x150] -> [+0x10]` is therefore **correct** [V] - it was missing the two
helpers and the table shape.

## 1. Init order (why this is the boot-time default bank)

`shuangta_host_initialize_phy` [V]:
1. `oal_mem_alloc(0x40, type 3)` -> `[dev+0x150]` (NULL -> -ENOMEM, and the error path
   frees/zeroes the pointer);
2. reads `dev[0]` (chip index) and `dev[2]` (band) and calls 0x3aa68(obj, chip, band);
3. 0x3aa68 copies a **32-word .rodata run T** to the stack - layout 10 + 6 + 10 + 6 words
   - and calls `0x3a934(obj, band, T-a)` plus `0x3a830(obj, band, T-b)` with the
   per-band sub-arrays (band 0: stack+0x4c / stack+0x1c; band 1: stack+0x74 /
   stack+0x34).
This runs at host-PHY init, before `hmac_spectral_init` / the first PSD default write.
The defaults that then land in the bank are decoded [V]:
`fft_size=2, fft_period=40, count=2, bottom_noise=-85, rssi_nb=-70, the_power_nb=12,
the_num_nb=12, rpt_mode=1, agc_lock_en=1, fftin_len_type=1, ofdm_det_en=1, 11b_det_en=1,
nb_det_en=0, psd_en=0, ant_index=0, p2dbm_offset=22` (`shuangta_init_spectral_param_defaults`
@0x3b4e4; `psd_en` default **0** - the block starts off).

## 2. The two candidate table families (the last check)

The 10-word per-band table is one of the two committed-decoded CA lists for the MAC blocks
[V\* `phase43/channel-array-map.md` §1/§3.2, `.rodata+0xb60` and `.rodata+0xbb0/0xc00`]:

| candidate | band 0 word 2 | band 1 word 2 | source table |
| --- | --- | --- | --- |
| mapping table (.rodata+0xb60) | **0x40046000** | **0x40066000** | 2g: 42000,44000,**46000**,48000,4a000,40000,4c000,52000,50000,54000 (5g = +0x20000) |
| sorted dump list (.rodata+0xbb0/0xc00) | **0x40044000** | **0x40064000** | 2g: 40000,42000,**44000**,46000,48000,4a000,4c000,52000,50000,54000 (5g = +0x20000) |

Both candidate windows are far larger than 0x540 (the register offsets in use), so **the
LMI/PSD block is consistent with either** and this decode cannot yet pick between them by
size. The exact check (one host-only step, no device):
1. from the 2.5.24 `hi5622v100_wifi.ko` (or the local 2.4.15 module, same code family),
   read the relocations `readelf -r` for the literal site in the init helper equivalent of
   0x3aa6c/0x3aa70 (`movw/movt ip`) and the `ldr [pc,#376]` at 0x3aa78;
2. resolve the addend -> .rodata offset of T, dump T's 32 words;
3. pick T[0..9] (band 0) / T[16..25] (band 1) and confirm their word 2.

Until that runs: **no PSD/LMI register write is allowed** (an unverified CA write is a bench
risk with no host-side recovery). S1 (read-only probe) also waits for it.

## 3. What was additionally verified here

* `hal_set_psd_cfg2_cfg_psd_en` = `bfi [r1,#0x438], bit 8, width 1` [V] (matches TABLES T6);
* `hal_set_cfg_lmi_ctrl2_cfg_lmi_psd_en` = `bfi [r1,#0x52c], bit 9, width 1` [V];
* accessor population **21 set + 16 get = 37** (the task-241 "19+17" is wrong - VERIFY §3);
* the PSD writes are reachable from two places only: `shuangta_init_spectral_param_defaults`
  (boot/band re-init defaults) and `hmac_spectral_set_scan_param_proc_SHUANGTA`
  (`fft_size` @0x8ae38, `fft_period` @0x8aea0, `count` @0x8ae78 - a 0..9 switch over the
  stock spectral parameter index); the other 17-ish setters have no other in-module caller
  [V], i.e. the stock feature is **init defaults + a 3-parameter runtime knob**, which is
  exactly the clean-room surface our fail-closed `spectral` implementation should expose
  (after S-1 closes).
