# Phase 50 - NAND/FMC Port Specification: the Triductor TR6560 Flash Memory Controller at 0x10a20000

> **HEADLINE.** The `luofu`/TR6560 SoC's flash block is a single **FMC** (Flash Memory Controller) at MMIO `0x10a20000` (4 KB register space) with a 1 MB memory-mapped data window at `0x1c000000`, driving an **ESMT F50L1G41LC SPI-NAND** (1 Gbit / 128 MB, 2048-byte pages, 64-byte OOB, 128 KB blocks, on-die 1-bit/512B ECC, quad-I/O) on chip-select 1. The controller is programmed through a small command-engine register set (`cmd/addrh/addrl/op_cfg/data_num/op`) for byte-level commands and a second DMA engine (`op_ctrl/saddr_d0/d1/oob/dma_len` + a 0x2200-byte contiguous buffer split as 8 KB data + 512 B OOB) for full-page transfers; completion is polled (`op` bit0 for commands, `op_ctrl` bit0 for DMA), interrupts (mask `0x38` = bits 3-5) are only used for post-op error detection, and command-mode data returns through the `0x1c000000` window. Every register, bitfield, command sequence and the full Linux MTD glue were recovered from three independent compiled artifacts - the Triductor BSP objects (`tri_fmc.o`, `tri_nand.o`, `tri_spi_nand_drv.o`, `tri_nand_mtd.o`, `tri_nand_bbt.o`, `tri_mtd_parts.o`, `ofpart_triductor.o`, ...), the vendor's `hi_flash.ko` from the live box (same driver, "HSAN FMC Controller Device Driver, Version 100"), and the vendor U-Boot `stage2` blob - plus the vendor boot log and a live register dump; the decoded fields are consistent across all three code bases and with the measured idle register values, which the U-Boot/kernel init paths explain except for five low-order bits whose semantics stay UNKNOWN (section (g)).

---

## (a) Register map (offset from 0x10a20000)

The authoritative layout is the DWARF `struct tri_fmc_reg_s` (size 176 = 0xb0) in
`build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/tri_fmc.o`
(extracted to `build/tmp/nand-spec/dwarf/tri_fmc.o.dwarf`). Member offsets below are
that struct's `DW_AT_data_member_location` values; `tri_fmc_reg_dump`
(`tri_fmc.o!tri_fmc_reg_dump`, loops `+0x00..+0xaf`, step 4, "%08x") confirms the
window. Three additional registers live beyond the struct: `+0xc0`/`+0xc4` (ECC error
counters, `hi_flash.ko!hi_fmc_get_ecc_err_num`), `+0x10c` (U-Boot mode bit) and
`+0x70`/`+0x100` (U-Boot DMA-descriptor fields). Live values were measured on the
running vendor box (`+0x00=0x00001823, +0x04=0x000800C0, +0x08=0x0000006F,
+0x0C=0x00000333, +0x10=0x00088880, +0x20=0x00000000, +0x100=0x00000000`).

| Off | Name (from `tri_fmc_reg_s`) | Bits / fields (decoded) | Status |
|---|---|---|---|
| 0x00 | `fmc_reg_cfg` | **[0]** operation mode "normal": both vendor (`hi_flash.ko!hi_fmc_opmode_set_normal`: `cfg \|= 1`) and BSP (`tri_fmc.o!tri_fmc_opmode_set`) write it; BSP *also* polls it as the post-DMA busy bit (`tri_fmc.o!tri_fmc_status_check`, used from `tri_spi_nand_drv.o!tri_spi_nand_drv_dma_transfer@0xac`), while the vendor polls `op_ctrl` bit0 for DMA instead (`hi_flash.ko!hi_fmc_status_check_dma_status`) - a BSP-vs-vendor discrepancy, see (g). **[1]** UNKNOWN (live = 1). **[2]** flash type: 1 = raw/parallel NAND, 0 = SPI flash - `tri_fmc.o!tri_fmc_is_nand_flash_type` (`ubfx cfg,#2,#1`) selects `tri_nand_get_ops` vs `tri_spi_nand_get_ops` in `tri_nand.o!tri_nand_probe@0x90`; live = 0 with SPI-NAND attached, consistent. **[3:4]** UNKNOWN (live = 0). **[7:5]** ECC type - `tri_fmc.o!tri_fmc_ecctype_get` (`ubfx cfg,#5,#3`) / `tri_fmc_ecctype_set` (`(v&0xff)<<5` into `cfg&~0xe0`); live = 1 (meaning UNKNOWN, see (g)). **[8:10]** UNKNOWN (live = 0). **[11:12]** UNKNOWN (live = 1,1). | MEASURED (fields above marked UNKNOWN per-bit) |
| 0x04 | `fmc_reg_global_cfg` | **[2]** data randomizer enable - `hi_flash.ko!hi_fmc_set_randomizer` (`bfi global_cfg, r1, #2, #1`); live = 0 (off). **[6]** write-protect enable - `tri_fmc.o!tri_fmc_wp_en_set` (`(v<<6)&0x40`), `hi_flash.ko!hi_fmc_wp_en_set` (`bfi ...,#6,#1`), `hi_fmc_wp_en_check` reads it; U-Boot sets it in its dying-gasp path (`stage2.bin@0xc003b518-24`: `global_cfg \|= 0x40`); live = 1 (WP on at idle). **[7]** UNKNOWN (live = 1). **[19]** (0x80000) UNKNOWN (live = 1). rest UNKNOWN (live = 0). | MEASURED |
| 0x08 | `fmc_reg_timint_spi_cfg` (= "timing_spi_cfg") | SPI bus timing. U-Boot writes **0x6f** during FMC init (`stage2.bin@0xc003ed00-04`: `mov r2,#0x6f; str r2,[r3,#8]` with r3 = 0x10a20000); vendor SFC path writes the same constant (`hi_flash.ko!hi_sfc_hw_init@0x18`). Field split UNKNOWN. | MEASURED (value), fields UNKNOWN |
| 0x0c | `fmc_reg_pnd_pwidth_cfg` | Pulse/period width configuration ("pnd" = pos/neg-dly, pwidth). Live = **0x333**. Vendor writes 0x375 when configuring the SFC path (`hi_flash.ko!hi_fmc_set_pwidth`: `movw r3,#0x375; str r3,[r0,#0xc]`) - the live 0x333 therefore comes from U-Boot, not this call. Field split UNKNOWN. | MEASURED (value), fields UNKNOWN |
| 0x10 | `fmc_reg_pnd_opidle_cfg` | Operation-idle delays. Live = **0x88880** (nibble pattern 0x8 per field - symmetric timing). No writer found in kernel or U-Boot code paths inspected (bootloader origin; see (g)). Field split UNKNOWN. | MEASURED (value), fields UNKNOWN |
| 0x14 | `fmc_reg_reserved1` | - | UNKNOWN |
| 0x18 | `fmc_reg_fmc_int` | Raw interrupt status. Read+mask: `tri_fmc.o!tri_fmc_int_status_get` (`[+0x18] & mask`); vendor checks bits 3-5 (`hi_flash.ko!hi_fmc_int_status_get`: `tst r3,#0x38`). | INFERRED |
| 0x1c | `fmc_reg_fmc_int_en` | Interrupt enable. `tri_fmc.o!tri_fmc_int_en_set(mask, en)` sets/clears `[+0x1c]`; vendor enables bits 3,4,5 (`hi_flash.ko!hi_fmc_en_interrupt`: `bfi` into bits 3, 4 and 5). Only mask **0x38 (bits 3-5)** is ever used by either driver. | INFERRED |
| 0x20 | `fmc_reg_fmc_int_clr` | Interrupt clear (write-1-to-clear). `tri_fmc.o!tri_fmc_int_clr_set(mask)` writes `[+0x20]`; vendor writes 0x38 (`hi_flash.ko!hi_fmc_clr_interrupt`). Live = 0 (W1C reads back 0). | MEASURED |
| 0x24 | `fmc_reg_cmd` | Command byte(s) for command-engine ops. Holds one byte for SPI (0x06 WREN, 0xff RESET, 0xd8 BLOCK ERASE, 0x9f RDID, 0x0f GET FEATURES, 0x1f SET FEATURES, 0x90 raw-NAND READ ID, 0x05 raw status) and packs **two** raw-NAND command cycles in one 32-bit word (0xd060 = erase-setup 0x60 << 8 \| 0xd0, `tri_nand_drv_yyxxxx.o!tri_nand_drv_erase@0x1c`). | INFERRED (strong: three code bases agree) |
| 0x28 | `fmc_reg_addrh` | Row address, high half. Written as `row >> 16` in every DMA page op (`tri_spi_nand_drv.o!tri_spi_nand_drv_dma_transfer@0x18-24`). | INFERRED |
| 0x2c | `fmc_reg_addrl` | Row address, low half: DMA ops write `row << 16` (column fixed at 0) (`tri_spi_nand_drv.o!tri_spi_nand_drv_dma_transfer@0x20-28`); command ops write the feature address (0xc0 status, 0xb0 config) or 0 (`..._check_status@0x48`, `..._read_id`). | INFERRED |
| 0x30 | `fmc_reg_op_cfg` | **[3:0]** dummy cycles after cmd (SPI only; e.g. 1 for RDID, per-spec `read_dummy_num` for page reads) · **[6:4]** address cycles (0=none, 1=get/set-feature+raw ID, 3=SPI erase/write/read, 5=raw-NAND column+row) · **[9:7]** SPI interface type (`read_if_type`/`write_if_type` from the flash spec; =0 for raw NAND; quad when enabled) · **[12:11]** chip select (value from DT `spi_cs` << 11; BSP and vendor use `cs<<11`, U-Boot hardcodes 0x800 = CS1, `stage2.bin@0xc003d174-0x178`). | INFERRED (very strong) |
| 0x34 | `fmc_reg_reserved2` | - | UNKNOWN |
| 0x38 | `fmc_reg_data_num` | Byte count for command-engine data phase (1 = get feature, 5 = RDID) (`tri_spi_nand_drv.o!tri_spi_nand_drv_read_id@0x5c`, `..._check_status@0x54`). | INFERRED |
| 0x3c | `fmc_reg_op` | Command-engine trigger/status. Observed encodings (all three code bases): **0x81** = cmd only (WREN, RESET), **0x85** = cmd+data-in (raw status read), **0x89** = raw-NAND reset, **0x8b** = raw-NAND init reset, **0xc1** = cmd+addr (SPI erase), **0xc5** = cmd+addr+data-in (GET FEATURES, raw ID), **0xdb** = raw-NAND erase, **0x185** = cmd+dummy+data-in (RDID). Field hypothesis (INFERRED): bit0 = send-cmd (also the **busy** bit - polled until 0 by `tri_fmc_status_check`/`hi_fmc_status_check`), bit1 = data-write direction, bit2 = data-read phase, bit3 = wait device-ready, bit4 = second cmd byte, bit6 = address phase, bit7 = START (present in all), bit8 = dummy phase. | INFERRED |
| 0x40 | `fmc_reg_dma_len` | DMA transfer length (vendor SFC DMA paths write it, `hi_flash.ko!hi_sfc_hw_dma_write@0xc0`; BSP declares but doesn't write it - length is implied by `op_ctrl`+page geometry). | INFERRED |
| 0x44 | `fmc_reg_reserved3` | Second DMA length/count word (written by U-Boot's DMA-descriptor setup, `stage2.bin@0xc003f4d4` writes `+0x48`; U-Boot also writes `+0x70`/`+0x100` for OOB reads). | UNKNOWN |
| 0x48 | `fmc_reg_reserved4` | DMA op register: U-Boot writes `op_cfg-like << 16 \| count[15:0]` (`stage2.bin@0xc003f4bc-d4`, `str r3,[r2,#0x48]`). | INFERRED |
| 0x4c | `fmc_reg_saddr_d0` | DMA bus address of data half 0 (`dma_handle`, set in both `tri_spi_nand_drv_dma_init`/`tri_nand_drv_dma_init` and `hi_flash.ko!hi_spi_nand_drv_init`). | INFERRED |
| 0x50 | `fmc_reg_saddr_d1` | DMA bus address of data half 1 (`handle+0x1000`). | INFERRED |
| 0x54 | `fmc_reg_reserved5` (8 bytes) | Unused by all code inspected (max page 0x2000 needs only d0+d1). | UNKNOWN |
| 0x5c | `fmc_reg_saddr_oob` | DMA bus address of the OOB area (`handle+0x2000`, 0x200 bytes). | INFERRED |
| 0x60 | `fmc_reg_reserved6` (8 bytes) | Written by U-Boot's DMA-descriptor setup from a 4-word descriptor struct (`stage2.bin@0xc003f47c-94`: stores descriptor words to +0x5c, +0x60, +0x64, +0x68). | UNKNOWN |
| 0x68 | `fmc_reg_op_ctrl` | DMA-engine trigger/status. **[0]** busy/done - vendor polls it (`hi_flash.ko!hi_fmc_status_check_dma_status`, up to 0x186a0 iterations); **[1:0]** op type: 1 = page READ, 3 = page WRITE, 0x11 = READ OOB (bit4) · **[15:8]** SPI write command (PROG LOAD etc.) · **[23:16]** SPI read command (READ FROM CACHE / page read). Written last, after `dsb st` + `arm_heavy_mb`, which starts the transfer (`tri_spi_nand_drv.o!tri_spi_nand_drv_dma_transfer@0x88-0xa8`, vendor `hi_spi_nand_hw_*`, U-Boot `stage2.bin@0xc003d180-0x188`: `(read_cmd<<16)|0x11` for OOB reads). Raw-NAND mode omits the cmd bytes (controller issues standard NAND sequences from op type). | INFERRED |
| 0x6c..0xab | reserved (64 bytes) | - | UNKNOWN |
| 0xac | `fmc_reg_flash_info` | Read-only device/controller status. Raw-NAND paths poll: busy = bit0; success = bits[6:5] == 0b11 (init/reset) or bits[7:5] == 0b111 (erase) (`tri_nand_drv_yyxxxx.o!tri_nand_drv_init@0x98-0xc0`, `..._erase@0x64-0x84`, vendor identical in `hi_nand_probe` erase/init wait loops). | INFERRED |
| 0xb0..0xbf | (past struct) | - | UNKNOWN |
| 0xc0, 0xc4 | (unnamed) | ECC error counters, read by `hi_flash.ko!hi_fmc_get_ecc_err_num` (`ldr r0,[r0,#0xc0]; ldr r1,[r3,#0xc4]`). | INFERRED |
| 0x10c | (unnamed) | U-Boot init ORs 0xc into it (`stage2.bin@0xc003ecf0-98`: `[+0x10c] \|= 0xc`). Live dump did not cover it. | UNKNOWN |
| 0x70, 0x100 | (unnamed) | Written by U-Boot's DMA descriptor setup only for OOB reads (`stage2.bin@0xc003f4a4-0xb4`). Live `+0x100 = 0` (idle). | UNKNOWN |

Notes: all MMIO accesses are plain 32-bit loads/stores through an `ioremap` of the 0x10a20000 window (`tri_fmc.o!tri_fmc_init@0x38-0x58` stores the base in `g_fmc_reg`; vendor `hi_flash.ko!init_module@0x7c-0x90`). `arm_heavy_mb` barriers bracket DMA starts. The controller does **not** use the GIC on this board - neither DT (`pinned.dts` fmc node has no `interrupts`) nor any driver registers an IRQ; completion is always polled.

## (b) Command / issue path

Two independent engines share one base:

**1. Command engine** (byte-level ops: WREN, RESET, ERASE, RDID, GET/SET FEATURES, raw-NAND cmds):
1. Program `cmd` (0x24), `addrh`/`addrl` (0x28/0x2c), `op_cfg` (0x30: addr cycles, dummy, if-type, CS), `data_num` (0x38).
2. Write `op` (0x3c) with START(bit7) plus phase bits - the write starts the operation.
3. Poll `op` bit0 until 0 (bounded: `tri_fmc_status_check` / `hi_fmc_status_check`, cap 0x186a0 iterations, `arm_delay_ops->const_udelay` between polls; BSP timeout prints **"FMC: controller operation time out"** and returns -0xf, vendor returns 0x70000000).
4. Data written by the device lands in the **0x1c000000 window** (mapped by `tri_fmc.o!tri_fmc_init` into `g_fmc_membuf`, size `g_fmc_membuf_size`; raw-NAND `tri_nand_drv_pre_init` keeps it at `chip+0x2c` and reads ID bytes straight from it, `tri_nand_drv_yyxxxx.o!tri_nand_drv_read_id@0x84`); data written by the host for SET FEATURES is placed in the window first (`hi_flash.ko!hi_spi_nand_drv_write_feature@0xbb`).

Exact recipes (SPI-NAND, CS=1), all from `tri_spi_nand_drv.o` / `hi_flash.ko`:
| Operation | cmd | addr | op_cfg | data_num | op |
|---|---|---|---|---|---|
| WREN (write-reg enable) | 0x06 | - | cs<<11 | - | 0x81 |
| RESET | 0xff | - | cs<<11 | - | 0x81 |
| GET FEATURES (status) | 0x0f | addrl=0xc0 | (cs<<11)\|0x10 (1 addr cyc) | 1 | 0xc5 |
| GET FEATURES (config) | 0x0f | addrl=0xb0 | (cs<<11)\|0x10 | 1 | 0xc5 |
| SET FEATURES (config) | 0x1f | addrl=0xb0 | (cs<<11)\|0x10 | 1 | 0xc5 (window holds byte) |
| RDID | 0x9f | - | (cs<<11)\|1 (1 dummy) | 5 | 0x185 |
| BLOCK ERASE | 0xd8 | addrl=block, addrh=0 | (cs<<11)\|0x30 (3 addr cyc) | - | 0xc1 |
| raw-NAND READ ID | 0x90 | addrl=0 | (cs<<11)\|0x10 | 5 | 0xc5 |
| raw-NAND RESET | 0xff | - | cs<<11 | - | 0x89 |
| raw-NAND init reset | 0xff | - | cs<<11 | - | 0x8b |
| raw-NAND ERASE | 0xd060 (2 cyc) | addrl=block | (cs<<11)\|0x30 | 1 | 0xdb |

**2. DMA engine** (full page read/write, OOB):
1. `saddr_d0/d1/oob` (0x4c/0x50/0x5c) point at one `dma_alloc_attrs` buffer of 0x2200 bytes = data[0x1000] + data[0x1000] + oob[0x200] (`tri_spi_nand_drv.o!tri_spi_nand_drv_dma_init`; vendor identical). Max transfer = 0x2000 data + 0x200 OOB = 8 KB page + 512 B.
2. `addrh/addrl` = row<<16 (no column), `op_cfg` from spec (addr cycles, if-type, dummy, CS).
3. `op_ctrl` (0x68) = (read_cmd<<16) | (write_cmd<<8 for writes) | 1 (read) / 3 (write) / 0x11 (read OOB); `dsb st` + `arm_heavy_mb` + store = start.
4. Poll: vendor = `op_ctrl` bit0; BSP = `tri_fmc_status_check(base)` (polls **cfg bit0** - see (g) discrepancy).
5. Completion: copy data from buffer+0x0000/0x1000 and OOB from buffer+0x2000 (`..._dma_read@0x40-0x60`).
6. Error path in BSP raw-NAND read: after the op, `tri_fmc_int_status_get(0x38)` - if any of bits 3-5 set → `printk("3[err] read page_addr: ...")` + `tri_fmc_reg_dump()` (`tri_nand_drv_yyxxxx.o!tri_nand_drv_read@0x6c-0xac`).

**Status/timeout discipline:** WREN path: `tri_fmc_write_reg_en` = save `cfg[7:5]`, clear ECC (BSP: `bic cfg,#0xe0`; vendor: `bfc r3,#5,#3`), issue 0x06/0x81, poll `op` bit0, restore ECC on success ("FMC: controller operation time out" on expiry, returns -0xf / 0x70000000). Device-side waits: SPI = GET FEATURES 0xc0 loop, mask 1 (OIP) for reset, 7 for erase (`tri_spi_nand_drv_check_status`: fail if `status & ~mask & 0xd`... decoded: `tst r6,#0xd` where r6 = status & mask → fails if bits 0/2/3 of the masked status set); raw = poll `flash_info` (+0xac) for `!bit0 && bits[6:5]==0b11` (or `bits[7:5]==0b111` after erase), cap 0x7d0 iterations. Poll delays are `arm_delay_ops->const_udelay(0x20c498)` in the vendor and BSP (a suspiciously large constant - 2,147,480 µs if taken literally - likely a scaled timeout or vendor quirk; the iteration caps bound it).

## (c) NAND device bring-up

1. **Platform/bus attach** (BSP): `tri_nand_init` registers `platform_driver g_nand_pltdrv` matching `of_device_id none_fmc_of_match = {"tri,fmc"}` (`tri_nand.o` .rodata dump, string at +0x40). `tri_nand_probe` (752 B): kmalloc host (0xa20), embed `mtd_info` at host+0x100 and `tri_nand_chip` at host+0x30 (struct layout in `tri_nand.o` DWARF: spec@0, name@0x34, oob_poi@0x38, en_state@0x3c, controller@0x40, hwcontrol@0x44 (spinlock+active+wq), page_buf@0x58, ecc@0x5c (72 B), ops@0xa4, buffers@0xc4, bbt@0xc8, priv@0xcc).
2. **FMC init**: `tri_nand_drv_pre_init` → `tri_fmc_init`: `of_find_compatible_node("tri,fmc")`, ioremap res0 (0x10a20000→`g_fmc_reg`), ioremap res1 (0x1c000000→`g_fmc_membuf`, size 0x100000), `memset(window,0xff,size)`, read DT prop `spi_cs` → `g_fmc_cs` (BSP dtsi `spi_cs=<1>`; vendor reads the same prop plus `partition_offset`, resets `sfc_rst` = crg 0x2c bit0 and clock `sfc_clk` via devm).
3. **Mode select**: `tri_nand_drv_is_nand_flash_type()` = cfg bit2. On our board bit2=0 → SPI path: `g_nand_drv_ops = tri_spi_nand_get_ops()` (= `g_spi_nand_ops`, 8 function pointers, order from `.data.g_spi_nand_ops` relocations: `{init, write, read, erase, read_id, read_oob, write_oob, reset}`).
4. **Flash spec from bootloader ATAG**: `tri_fmc_atags_parse()` finds node "tri,flashinfo_reserved" (vendor: "hsan,flashinfo_reserved", live at 0x80800000) and walks the ATAG list for tag **0x5441000a** (vendor `hi_flash.ko!init_module@0x264-0x29c` uses the same magic; prior decode notes `build/tmp/bsp-notes/bootblob/gen2.py` name it "custom flash_info tag"). The payload holds: byte index → 0x18-byte records → the 52-byte `tri_nand_spec` copied into `chip->spec` (`tri_nand.o!tri_nand_probe@0xc0-0x108`, 3×16+4 words), plus a 0xa20-byte blob at +0x34 (RAM BBT + part table, `probe@0x110-0x134`). `tri_nand_spec` fields (DWARF): options, tri_size, block_size, page_size, oob_size, page_shift, erase_shift, bbt_erase_shift, bbt_len, ecc_type, read/write/erase_addr_cycle (u8), and `tri_spi_nand_rw_cfg_s` {read_if_type, read_cmd, read_dummy_num, write_if_type, write_cmd, write_dummy_num, rsv[2]}. Page-size gate: 0x800/0x1000/0x2000 → enum {2KB=1,4KB=2,8KB=3}, else "!!do not support pagesize" (`probe@0x138-0x16c`).
5. **Driver init (SPI)**: `tri_spi_nand_drv_init(chip)`: `wp_en_set(0)` (WP off while operating), register transfer fns via `tri_hal_nand_transfer_reg({dma_transfer, bus_transfer=stub})`, `dma_init` (0x2200 buffer + saddr regs). Vendor additionally does the **quad-mode enable**: GET FEATURES 0x0f addr **0xb0** (SPI-NAND configuration register) → `val |= spec_byte` → SET FEATURES 0x1f addr 0xb0 (with ECC cleared to 0 and WP off around it, restored after) (`hi_flash.ko!hi_spi_nand_drv_init@0x342c-0x3468`; write path in `hi_spi_nand_drv_write_feature`). This is what the DTS boolean `enable-quad-mode` turns into; the OR'd byte comes from the U-Boot spec table (standard SPI-NAND QE bit0 = 0x01).
6. **Reset**: cmd 0xff, op 0x81, then GET FEATURES status (mask OIP) up to 0x7d0 polls ("FMC: reset spi nand flash fail" / "device reset time out"). Vendor identical (`hi_spi_nand_drv_reset`).
7. **ID read**: ECC→0, cmd 0x9f, data_num 5, op_cfg (cs<<11)|1, op 0x185, poll, `memcpy(dst, window, len)`, ECC restored. Our chip answers **0x8C 0x2C** (+ 32-hex unique ID via the UID path) → ESMT F50L1G41LC (mainline `drivers/mtd/nand/spi/esmt.c`, `esmt_8c_spinand_table`: NAND_MEMORG(1, 2048, 64, 64, 1024, 20, ...) = 1 target, 2048 B page, 64 pages/block, 64 B OOB, 1024 blocks/lun; ECC 1 bit/512 B on-die). Confirmed independently by the live vendor log (`hccaccept-dmesg.txt`: "Flash UID: 8C2C-...", UBI "PEB size: 131072", "min./max. I/O unit sizes: 2048/2048") and the 0x8000000 (128 MB) partition total.
8. **ECC config**: `cfg[7:5]` = ecctype; every ID/feature/status op zeroes it and restores it after (BSP `tri_fmc_ecctype_get/set`, vendor identical); live idle value = 1. Exact enum→strength mapping is not recoverable (see (g)).
9. **Timing/mode config**: done by the bootloader (timing 0x6f, pwidth 0x333, opidle 0x88880 - U-Boot writes timing, see (a)); kernel only touches cfg bit0 (normal mode), cfg[7:5] (ECC) and global_cfg bit6 (WP) / bit2 (randomizer, vendor).
10. **OOB layout**: OOB is 64 B (spec.oob_size); controller DMA places it at buffer+0x2000; BBM convention = OOB bytes [0:2] (marker 0x00 = bad) - the mainline ESMT driver documents the 4×16 B layout (free bytes 2:3,18:19,34:35,50:51; ECC at 8:15 etc.; BBM 0:1) which the port should reuse (`esmt.c` comment block).
11. **BBT handling**: BSP gets the RAM BBT from the bootloader ATAG (0xa20 blob copied to `chip->bbt`, `tri_nand.o!tri_nand_probe@0x118-0x134`): `tri_nand_bbt_block_isbad` = 2 bits/block (`ldrb [bbt, blk>>3]; (v>>(blk&6))&3`), out-of-range → -EINVAL; `tri_nand_bbt_block_markbad` = update bitmap + erase block (3 retries via ops->erase) + `write_oob` with a 0x200 buffer of 0x00 (memset 0 on success, 1 after retries exhausted). The vendor instead scans at boot ("nand_scan_bbt: Out of memory", "nand_bbt: Can't scan flash and build the RAM-based BBT" strings in `hi_flash.ko`). MTD layer wraps markbad with `wp_disable`/`wp_enable` (`tri_nand_mtd.o!tri_nand_mtd_block_markbad`).

## (d) MTD integration surface

`tri_nand_mtd_contact(mtd)` (`tri_nand_mtd.o`, called from `tri_nand_probe@0x230`) fills `mtd_info` (embedded at host+0x100):

- geometry: `type=4 (NAND)`, `flags=0x400 (WRITEABLE)`, `size=spec.tri_size` (0x8000000), `erasesize=spec.block_size` (0x20000), `writesize=spec.page_size` (0x800), `writebufsize=page_size`, `oobsize=spec.oob_size` (0x40), `oobavail` from `ecc->oobregion` (+4), `subpage...` n/a.
- hooks: `_erase@0x5c`, `_read@0x68`, `_write@0x6c`, `_read_oob@0x74`, `_write_oob@0x78`, `_block_isbad@0xac`, `_block_markbad@0xb0` (0x60/0x64/0x9c/0xa0 left NULL).
- entry points take the old-API shape `(mtd, loff_t from/to, size_t len, size_t *retlen, u_char *buf)` - read/write validate `from+len <= size` (-EINVAL otherwise), then `tri_nand_mtd_get_device` (spinlock on `chip->hwcontrol.lock`, `active` state + waitqueue `&chip->controller->wq`, states TRI_FL_READING/WRITING/ERASING, `__wake_up` on release).
- `tri_nand_mtd_read_ops(mtd, page, off, ops)` / `_write_ops(...)` iterate page-by-page through `mtd_oob_ops` (mode AUTO=1/PLACE=2/RAW=0 semantics, retlen/oobretlen accounting), calling `g_nand_drv_ops->read/write` (→ `tri_hal_nand_drv_rw` → registered `dma_transfer`). Partial pages: RMW through `chip->buffers->databuf` (memset 0xff for fresh pages; page cache in `chip->page_buf`, invalidated as -1). OOB transferred by `tri_nand_mtd_transfer_oob` per `mtd_oob_region` list with prepad/postpad.
- `tri_nand_mtd_erase(mtd, erase_info*)`: alignment checks (`-EINVAL`), `wp_check` (enabled → -EIO; vendor prints "nand_mtd_erase: Device is write protected!!!"), per-block `tri_nand_block_isbad` check ("nand_erase: attempt to erase a bad block at page 0x%08x"), `wp_disable` → `g_nand_drv_ops->erase` (which does WREN+0xd8+status-wait) → `wp_enable`.
- `tri_nand_mtd_write_oob`/`read_oob`: reject mode > 2 (-0x20c in both drivers; vendor also prints "not support mtd oob mode"); the BSP handles MTD_OPS_AUTO/PLACE/RAW via `tri_nand_mtd_transfer_oob` region walk, the vendor only the PLACE path.
- read failure of a page returns -EBADMSG (-74) after drv_read error; drv_write oob memset 0xff padding.
- After probe, `mtd_device_parse_register(mtd, {"cmdlinepart","ofpart",NULL}, NULL, NULL, 0)` (`tri_nand.o!tri_nand_probe@0x238-0x260`; vendor: same parser list, `hi_flash.ko` strings "cmdlinepart"/"ofpart", platform driver name **hi_nfc** - the `hi_flash.ko` string at +0x5dc - matching `/sys/bus/platform/drivers/hi_nfc/10a20000.fmc` on the live box - and imports `hi_nand_init_mtd_partitions_extern` from its companion module).

The vendor module is the same design (hi_nand_probe 1544 B, same host/chip/mtd embedding, same `{init,write,read,erase,read_id,read_oob,write_oob,reset}` ops tables in `.data`, same hook set) and its dmesg on the live box confirms the geometry the port must reproduce.

## (e) Partition parsing

1. **DT fixed-partitions** (both boards): the 17-partition vendor table (pinned `fmc@10a20000`; `opensource/docs/soc/luofu-r116-pinned.dts:781-871`) and the BSP's 11-partition table (`triductor-tr6560.dtsi:210-282`) are plain `fixed-partitions` nodes consumed by the standard `ofpart` parser - the vendor dmesg prints "17 fixed-partitions partitions found on MTD device 10a20000.fmc". No custom parser is needed for either table.
2. **ofpart_triductor** (`build/tmp/bsp/.../drivers/mtd/parsers/ofpart_triductor.{h,o}`): `tr6560_partitions_post_parse(mtd, parts, nr_parts)` (500 B) is a *post-parse rename* for the BSP's A/B scheme: it reads the upg flag (flash-tag payload byte [1], logged "i_upg_flag: %d") and rewrites partition `name` pointers - flag 0: `kernelA→kernel`, `firmwareA→firmware`, `rootfsA→rootfs`; flag 1: `kernelB→kernel`, `rootfsB→rootfs`, `firmwareB→firmware` (strings `.LC1..LC10` = kernelA/kernel/rootfsA/firmwareA/firmware/kernelB/rootfsB/firmwareB). Header gated by `CONFIG_MTD_TRIDUCTOR_NAND`.
3. **tri_mtd_parts** (`tri_mtd_parts.o!tri_setup_mtd_partitions`, 200 B): decodes the bootloader flash-info partition table (first byte = count, then 0x18-byte records) into `g_nand_parts` (20 × 64-byte `mtd_partition`) and `g_mtd_name` (512 B), printing "mtd partition tables[%d]:". It is the ATAG-table path used when neither cmdlinepart nor DT partitions match.

## (f) Port plan to a mainline Linux MTD driver

**Recommended architecture: reuse the mainline SPI-NAND stack via a thin spi-mem controller driver.**

- The device itself is fully supported upstream: `esmt_8c_spinand_table` already contains **F50L1G41LC** (`SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x2C)`, `NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1)`, `NAND_ECCREQ(1, 512)`, quad read-cache variants 1S-1S-4S/2S/1S, ESMT OOB layout) - zero chip work needed.
- What must be written: `drivers/spi/spi-luofu-fmc.c` (or `drivers/mtd/nand/spi/`-adjacent) exposing `struct spi_controller` + `struct spi_mem_ops` whose `exec_op` translates `struct spi_mem_op` into the FMC register protocol from sections (a)/(b). The op templates the spinand core uses map 1:1:
  - reset (0xff) → cmd/op=0x81; wr_en (0x06) → 0x06/0x81 (with the ECC save/clear/restore dance);
  - get_feature (0x0f, addr, 1 B in) / set_feature (0x1f, addr, 1 B out) → `op_cfg=(cs<<11)|0x10`, `data_num=1`, `op=0xc5`, data through the 0x1c000000 window;
  - readid (0x9f, 0 addr, 1 dummy, 5 B) → `op=0x185`, window read;
  - blk_erase (0xd8, 3 addr) → `op=0xc1` + WREN + GET-FEATURES status wait (mask 0x0d fail bits);
  - page_read (0x13) / prog_exec (0x10) → `op_ctrl` DMA engine: saddr d0/d1/oob, `op_ctrl[23:16]=read_cmd`, `[15:8]=write_cmd`, type bits 1/3, `op_cfg` if-type field = bus width (1-1-4 → quad value from the U-Boot spec table; read_dummy_num = 1 for ESMT quad), poll `op_ctrl` bit0;
  - read_cache (0x03/0x0b/0x6b) / write_cache (0x02/0x32) → same DMA engine (data phase only, no row address).
- What must change vs the vendor/BSP code: drop the bootloader-ATAG flash-spec dependency (spec comes from the mainline spinand table + nanddev geometry); use `spinand`'s on-die ECC engine instead of the FMC `cfg[7:5]` ecctype (keep the field at a fixed safe value, or 0, since on-die ECC makes host ECC unnecessary); replace the vendor's waitqueue/active-state serializer with the spinand core's mutex; implement interrupts (mask 0x38) only as optional error reporting; implement `nand-randomizer` via `global_cfg` bit2 if a board needs it.
- Smallest bring-up that reads the ID and then a partition:
  1. `ioremap(0x10a20000, 0x1000)`; if the bootloader did not run, write timing 0x6f / pwidth 0x333 / opidle 0x88880, `cfg` bit0=1, `cfg` bit2=0 (SPI mode), `global_cfg` bit6=0 (WP off).
  2. RESET (0xff/0x81) + GET FEATURES 0xc0 wait (OIP).
  3. RDID (0x9f, dummy 1, 5 B, window) → expect 0x8C 0x2C.
  4. Quad enable: GET FEATURES 0xb0, OR 0x01, SET FEATURES 0xb0 (ECC cleared around it).
  5. Page read of page 0: saddr setup, `addrh/addrl=0`, `op_cfg=(1<<11)|(4<<7)|(1<<4)|1` (CS1, quad if-type, 3 addr cyc, 1 dummy), `op_ctrl=(0x13<<16)|1`, poll bit0, copy 0x800 B data + 0x40 B OOB from the DMA buffer.
  6. Wrap as spi-mem exec_op → the rest (partitions, UBI, sysupgrade) is mainline.

**DTS node for our board** (address 0x10a20000, window 0x1c000000, CS 1, quad, sfc_rst, sfc_clk - the 17-partition table verbatim from the pinned DTS):

```dts
fmc: spi-nand@10a20000 {
    compatible = "hisilicon,luofu-fmc";   /* new binding; vendor: "hsan,fmc" */
    reg = <0x10a20000 0x1000>,           /* controller          */
          <0x1c000000 0x100000>;         /* memory-mapped window */
    spi_cs = <1>;
    enable-quad-mode;
    clocks = <&crg LUOFU_CLK_SFC>;
    clock-names = "sfc_clk";
    resets = <&crg 0x2c 0x0>;            /* sfc_rst: crg reg 0x2c bit 0 */
    reset-names = "sfc_rst";
    #address-cells = <1>;
    #size-cells = <1>;
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;
        /* the 17 vendor partitions, offsets/sizes as pinned: */
        partition@0       { label = "esbc";        reg = <0x0 0x40000>; };
        partition@40000   { label = "uboota";      reg = <0x40000 0x100000>; };
        partition@140000  { label = "ubootb";      reg = <0x140000 0x100000>; };
        partition@240000  { label = "enva";        reg = <0x240000 0x40000>; };
        partition@280000  { label = "envb";        reg = <0x280000 0x40000>; };
        partition@2c0000  { label = "fac";         reg = <0x2c0000 0x200000>; };
        partition@4c0000  { label = "bdinfo";      reg = <0x4c0000 0x40000>; };
        partition@500000  { label = "cfga";        reg = <0x500000 0x200000>; };
        partition@700000  { label = "cfgb";        reg = <0x700000 0x200000>; };
        partition@900000  { label = "log";         reg = <0x900000 0x440000>; };
        partition@d40000  { label = "pstore";      reg = <0xd40000 0x40000>; };
        partition@d80000  { label = "kernela";     reg = <0xd80000 0x840000>; };
        partition@15c0000 { label = "kernelb";     reg = <0x15c0000 0x840000>; };
        partition@1e00000 { label = "rootfsa";     reg = <0x1e00000 0x1740000>; };
        partition@3540000 { label = "rootfsb";     reg = <0x3540000 0x1740000>; };
        partition@4c80000 { label = "rootfs_data"; reg = <0x4c80000 0x1600000>; };
        partition@6280000 { label = "upgrade";     reg = <0x6280000 0x1d80000>; };
    };
};
```

(Total 0x8000000 = 128 MB = 1024 blocks × 128 KB. The skeleton already exists in `opensource/docs/soc/luofu-r116.dts:358-421`; `LUOFU_CLK_SFC` / CRG binding per `opensource/docs/soc/vendor-dt-notes.md` sec. 9. The FMC has no GIC interrupt wiring in either DT - the driver must poll.)

## (g) Honest limits - what the objects do NOT tell us

1. **cfg bit0 semantics conflict** (BSP vs vendor): BSP `tri_fmc_status_check` polls cfg bit0 as the DMA busy bit; vendor polls `op_ctrl` bit0 and writes cfg bit0 as a mode bit. On the live box cfg bit0 = 1 at idle, so the BSP's poll would hang there. Resolution: observe cfg bit0 during an actual page read on the live box (devmem in a loop while a read runs) - if it never toggles, the BSP code is a quirk/bug and `op_ctrl` bit0 is the one true DMA busy flag.
2. **cfg bits 1, 8-12 and global_cfg bits 7, 19** (live = 1,1 for 11:12; 1,1 for 7/19): no writer or reader found in any artifact. Resolution: dump cfg/global_cfg from U-Boot console at its `nand` prompt, or `devmem` right after each boot stage to bisect the writer.
3. **pwidth (0x333) and opidle (0x88880) provenance + field split**: no kernel or U-Boot store found; the values must be written by an earlier boot stage (bootram/stage1) or be power-on defaults. Resolution: same stage-bisection; a logic analyzer on the SPI pins settles the actual timing fields.
4. **ecctype enum semantics** (cfg[7:5], live = 1): which value is NONE/1bit/4bit/8bit/24bit and the HW ECC algorithm (BCH? Hamming? polynomial) are in closed SDK headers. Resolution: with SPI-NAND on-die ECC the field is vestigial - safe to leave at 1; a raw-NAND board would need experiments (write known patterns, flip bits, read the 0xc0/0xc4 error counters).
5. **op register bits 3/4 exact meaning** (raw-NAND reset 0x89 vs 0x81, erase 0xdb vs 0xc1) - hypothesis (wait-ready / 2nd cmd) is from pattern-matching only. Resolution: raw-NAND logic capture or SDK header.
6. **Registers 0x44/0x48/0x54/0x58/0x60/0x64/0x70/0x100/0x10c**: only U-Boot writes are seen (0x48 op/count, 0x5c-0x68 descriptor blob, 0x70/0x100 OOB extras, 0x10c |= 0xc); semantics UNKNOWN. Resolution: devmem watch during U-Boot `nand read` operations.
7. **The quad-enable OR byte** comes from the U-Boot flash-spec table (vendor `hi_flash.ko!hi_spi_nand_drv_init` ORs `chip->spec[...]`), not from any kernel constant - its value is not recoverable from these artifacts (almost certainly 0x01 = QE). Resolution: GET FEATURES 0xb0 dump on the live box after boot.
8. **ATAG payload format beyond the 52-byte spec** (the 0xa20 blob: exact BBT bit layout, partition-record details) and the exact `flashinfo_reserved` ATAG writer logic live in U-Boot's `setup_boot_atags` (symbol seen in `stage2` strings); only its consumption side is decoded here. Resolution: disassemble `stage2`'s `setup_boot_atags`/`tri_nand_chip_init`, or dump 0x80600000 live (a live capture exists at `build/tmp/bsp-notes/flashinfo/` - it reads back ARM code, not a parameter block, so the mapping is itself an open question).
9. **Interrupt bit meanings** within mask 0x38 (bits 3-5: which is cmd-done / dma-done / ecc-error) - both drivers treat the set as a generic error. Resolution: trigger each op class and read 0x18/0x1c live.
10. **HiSilicon/vendor closed source**: the vendor `hi_flash.ko`/`hi_basic.ko` and U-Boot are the only complete implementations; no GPL source was found in the BSP tree (no `tri_fmc.c` - only `.o`), so anything in this list that those artifacts don't reveal needs the observations named above.

## Evidence

Files read (repo root `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2`, all paths relative):

All analyzed objects and the vendor module are **ARM32 (not Thumb)**: `file` reports "ELF 32-bit LSB relocatable, ARM, EABI5", every STT_FUNC symbol has an even `st_value` (no Thumb bit - checked with `awk` over the extracted symtabs, command below), and capstone decodes cleanly in `CS_MODE_ARM` throughout; the stage2 U-Boot blob is likewise pure ARM mode code. This matches the Cudy kernel (5.10.201 SMP ARMv7).

- `build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/Makefile`, `.../nfc_tr6560/Makefile`, `.../drivers/mtd/parsers/ofpart_triductor.h`
- The 10 objects (ELF 32-bit LSB ARM EABI5, with debug_info, not stripped - verified with `file`):
  `tri_fmc.o`, `tri_mtd_parts.o`, `nfc_tr6560/{tri_hal_nand,tri_nand,tri_nand_bbt,tri_nand_check,tri_nand_drv_yyxxxx,tri_nand_mtd,tri_spi_nand_drv}.o`, `parsers/ofpart_triductor.o`
- `build/tmp/bsp/target/linux/tr6560/files-5.10/arch/arm/boot/dts/triductor-tr6560.dtsi` (lines 185-305; fmc node at 210-282)
- `opensource/docs/soc/luofu-r116-pinned.dts` (lines 770-900; fmc node 781-871), `opensource/docs/soc/luofu-r116.dts` (lines 345-435; skeleton node 358-421), `opensource/docs/soc/vendor-dt-notes.md` (sec. 6, 9, 10)
- `opensource/lab/ko_disasm.py`, `opensource/lab/ko_mmio.py` (method templates)
- `rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_flash.ko` (vendor module, same driver family)
- `build/tmp/bsp-notes/bootblob/stage2.bin`, `strings_stage1.txt`, `strings_stage2.txt` (vendor U-Boot stage2 + strings; FMC code at 0xc003b4xx/0xc003ecxx-0xc003f4xx)
- `build/tmp/bsp-notes/flashinfo/dump.md`, `build/tmp/bsp-notes/dtscmp/delta.md` (prior decode tasks' notes)
- `build/register-dumps/detached/hccaccept-dmesg.txt` (lines 147-169: hi_nand_init banner, Flash UID, 17 partitions, UBI geometry)
- Mainline kernel (web): `drivers/mtd/nand/spi/core.c` (spinand framework + esmt_8c manufacturer), `drivers/mtd/nand/spi/esmt.c` (F50L1G41LC table + OOB layout)

Commands actually run (bash via `bash -lc`, Windows host; scratch under `build/tmp/nand-spec/` only):

```
file build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/*.o \
     build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/nfc_tr6560/*.o
cat build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/Makefile \
    build/tmp/bsp/target/linux/tr6560/files-5.10/drivers/mtd/triductor/nfc_tr6560/Makefile
./pyenv/Scripts/python.exe -c "import capstone; print(capstone.__version__); import elftools; print('pyelftools ok')"
./pyenv/Scripts/python.exe build/tmp/nand-spec/extract.py <all 10 .o files>          # symtab/strings/DWARF -> build/tmp/nand-spec/{symbols,strings,dwarf}/
./pyenv/Scripts/python.exe build/tmp/nand-spec/ko_disasm_dump.py <each .o>           # capstone ARM disasm + reloc/data decode -> build/tmp/nand-spec/disasm/*.asm
./pyenv/Scripts/python.exe build/tmp/nand-spec/extract.py rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_flash.ko
./pyenv/Scripts/python.exe build/tmp/nand-spec/ko_disasm_dump.py rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_flash.ko
./pyenv/Scripts/python.exe build/tmp/nand-spec/uboot_fmc.py build/tmp/bsp-notes/bootblob/stage2.bin
./pyenv/Scripts/python.exe build/tmp/nand-spec/uboot_full.py                       # full stage2 disasm (skipdata) -> build/tmp/nand-spec/stage2.dis
grep -n '1823\|0x333\|88880\|800c0\|0x6f\|10a20000' build/tmp/nand-spec/stage2.dis
grep -rn '10a20000' build/tmp/bsp-notes opensource/docs/phase42-decode
grep -n -i -E 'nand|fmc|mtd|flash|hi_nand' build/register-dumps/detached/hccaccept-dmesg.txt
find build/tmp/bsp -name 'objdump' -o -name '*objdump*'                            # none present -> pyenv capstone used
awk '$3=="FUNC" && $5!="UND" && and(strtonum("0x"$1),1)' build/tmp/nand-spec/symbols/*.syms   # Thumb check: empty = all ARM mode
```

Key evidence paths per claim (offsets are instruction offsets inside the named function):
- Register struct: `tri_fmc.o` DWARF `tri_fmc_reg_s` (`build/tmp/nand-spec/dwarf/tri_fmc.o.dwarf`)
- cfg/wp/int accessors: `build/tmp/nand-spec/disasm/tri_fmc.o.asm` (`tri_fmc_opmode_set`, `tri_fmc_ecctype_get/set`, `tri_fmc_wp_en_set/check`, `tri_fmc_int_status_get/en_set/clr_set`, `tri_fmc_reg_dump`)
- Command recipes: `build/tmp/nand-spec/disasm/tri_spi_nand_drv.o.asm` (`tri_spi_nand_drv_reset`, `_read_id`, `_erase`, `_check_status`, `_dma_transfer`, `_dma_read/write`, `_dma_read_oob`, `_write_oob`, `_dma_init`); raw-NAND mirrors in `tri_nand_drv_yyxxxx.o.asm`
- Vendor confirmation + extras: `build/tmp/nand-spec/disasm/hi_flash.ko.asm` (`hi_fmc_status_check*`, `hi_fmc_write_reg_en`, `hi_fmc_set_pwidth`, `hi_fmc_opmode_set_normal`, `hi_fmc_en_interrupt/clr_interrupt`, `hi_fmc_set_randomizer`, `hi_fmc_get_ecc_err_num`, `hi_spi_nand_check_status`, `hi_spi_nand_hw_init`, `hi_sfc_hw_init`, `hi_spi_nand_drv_init` (quad enable), `hi_spi_nand_drv_read/write_feature`, `hi_nand_drv_read_id`, `init_module`, `hi_nand_probe`)
- U-Boot confirmation: `build/tmp/nand-spec/stage2.dis` @ `0xc003ec4c`-`0xc003ed10` (timing=0x6f, `+0x10c |= 0xc`), `0xc003f2c8`-`0xc003f3f0` (opmode/ecctype/wren/wp/int accessors identical to tri_fmc.o), `0xc003f434`-`0xc003f4d4` (DMA descriptor: 0x5c/0x60/0x64/0x68/0x70/0x100, `+0x48` op/count), `0xc003b504`-`0xc003b53c` (dying-gasp sets global_cfg bit6)
- Platform/geometry/MTD: `build/tmp/nand-spec/disasm/tri_nand.o.asm` (`tri_nand_probe`, `none_fmc_of_match` data, `tri_nand_init`), `tri_nand_mtd.o.asm` (`tri_nand_mtd_contact` and all hooks), `tri_nand_bbt.o.asm`, `tri_mtd_parts.o.asm`, `ofpart_triductor.o.asm`; DWARF type dumps `tri_nand.o.dwarf`, `tri_nand_drv_yyxxxx.o.dwarf`, `tri_nand_mtd.o.dwarf`, `tri_hal_nand.o.dwarf`
- Chip identity: live `hccaccept-dmesg.txt` line 148 "Flash UID: 8C2C-..." + UBI geometry lines 171-176; mainline `esmt.c` `esmt_8c_spinand_table` (ID method OPCODE_ADDR 0x2C, memorg 2048/64/64/1024)
- Live register dump: task input (`+0x00=0x1823, +0x04=0x800C0, +0x08=0x6F, +0x0C=0x333, +0x10=0x88880, +0x20=0x0, +0x100=0x0`)
