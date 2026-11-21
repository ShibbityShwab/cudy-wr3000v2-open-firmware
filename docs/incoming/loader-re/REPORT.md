
# Loader, recovery and upgrade formats - HiSilicon luofu (Cudy WR3000 v2.0)

**Status: host-only, read-only reverse engineering. No device, UART, TFTP, flash or boot was used for this document. It is a format and semantics record, not a flash instruction and not a release claim.**

- Lane: loader/recovery/upgrade reverse engineering, 2026-11-21.
- Scope: the stock loader (esbc + U-Boot), the vendor recovery/sysupgrade container, the vendor web/serial upgrade verification paths, and what all of that means for a robust install/recovery procedure on this board.
- Evidence: local dumps in `dumps/router-20261108/`, `tftp/recovery.bin`, the vendor images in `firmware/`, the vendor rootfs trees `rootfs-2.4.15/` and `rootfs-2.5.24/`, and the project docs `opensource/docs/SELECTOR-0x21.md`, `opensource/docs/FIRST-INSTALL.md`, `opensource/target-skeleton/docs/TRAILER-FORMAT.md`.
- Method: raw file parsing plus ARM/Thumb disassembly with the repo toolchain (`build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump`), all offline.
- Markers: **[V]** verified against a named local artifact or a reproducible host-side check; **[I]** inferred from [V] evidence; **[U]** unknown/unmeasured; **[X]** a previous claim contradicted by the evidence here.

No vendor binaries or full firmware blobs are reproduced in this file. Sizes, offsets, hashes and format constants are factual structure; the only vendor byte sequences quoted are format constants (magic words, descriptor words) the analysis needs.

---

## 0. Findings in one page

1. **The vendor recovery image and the vendor web-upgrade image are the same file.** `tftp/recovery.bin` is byte-identical to `firmware/WR3000V2-R116-2.4.15-20251030-114751-sysupgrade.bin` (sha256 `014d6f60...`). "TFTP recovery" is the on-device U-Boot consuming a normal vendor sysupgrade package; it is not a recovery-only format. [V]
2. **The package is a signed container with three layers**: a 512 KiB U-Boot section (`hboot1tag` header + U-Boot + `OEM_BOOT` version marker), a 256-byte RSA-2048 signature block, and a FIT image (`flat_dt`) whose `/images/ubi` data property is a HiSilicon `hi_upgrade` package containing the kernel slice and the rootfs UBI image; fwtool metadata and a `FWx0` trailer are appended in two places. [V]
3. **The recovery gate is RSA and it is real.** U-Boot verifies RSA-2048 PKCS#1 v1.5 SHA-1 over `image[0, 0x80000)` concatenated with `image[0x80800, EOF)`; the signature is the 256 bytes at `0x80000`. The scheme and the key were reproduced from the binary and both vendor images verify; the same public key is embedded in U-Boot and in the userspace `oem-check` tool. [V] **A custom `recovery.bin` is therefore not possible without the vendor private key.**
4. **The recovery path writes both A/B pairs and erases `rootfs_data`.** Full recovery capture: `mtd erase/write kernela`, `kernelb`, `rootfsa`, `rootfsb` (same image to each), then `mtd erase rootfs_data`, then reset. It never writes U-Boot, esbc, env, bdinfo, fac, cfg, log or pstore, and never writes the selector cell. [V]
5. **Pair selection is env `bootflag` + SYSCTRL `boot_reg` as a failure record** (re-confirmed in the loader code and the recovery capture). The pair is chosen by `bootcmd=mtd read kernel${bootflag}`; `bootfip` writes the *opposite* pair's low byte into the cell on every boot, and a successful boot needs a userspace writer (vendor `hsan_appm` -> `boot_done`; project `luofu_record_pair`) to re-record the running pair. [V]
6. **The FIP/DTB handoff contract is position-sensitive.** The gate word `0xd7b7ab1e` is a 32-bit BE load at `base_dtb + FDT-totalsize`; an unaligned base-DTB totalsize data-aborts in U-Boot before the gate check, so the totalsize must be 4-byte aligned (project pads to 8 as a superset; `scripts/pad-dtb.py`). After a successful overlay apply, bootfip stages an ATAG list in the `flashinfo_reserved` window (DT node base + `atag-offset` = `0x80600000 + 0x1000`), including a custom flash-info ATAG `0x5441000a`, then enters the kernel with `r0=0`, `r1=machid` (env `machid`, else a default) and `r2=patched DTB`. [V]
7. **The vendor update format and our image format are deliberately incompatible.** Stock U-Boot recovery, stock `oem-check` and the stock web upgrade accept only the hboot1tag+FIT+RSA container; our target's `platform_check_image` accepts only the project's OpenWrt tar container and explicitly refuses vendor images. The install vehicle is therefore the stock userspace telnet installer (or equivalent), and the recovery door is the stock signed TFTP image. [V]/[I]
8. **esbc (mtd0) does not cryptographically authenticate U-Boot** in the code we have: it checks the `hboot1tag` magic and the region-size/code-length fields, then jumps. No SHA/RSA/AES code or constants exist in the esbc blob. Whether the SoC eFuse secure-boot gate is fused on this unit is [U]; if it is not, a custom U-Boot written from stock root is *in principle* loadable (high brick risk, not attempted; it does not make the stock recovery image signable). [V]/[I]/[U]

---

## 1. Evidence inventory

| artifact | size | sha256 | role |
| --- | --- | --- | --- |
| `dumps/router-20261108/uboota.bin` (= `full/mtd1-uboota.bin`) | 1,048,576 | `36d94da9757da5b1fb15101cd457087ca99c0895de1fc64e58ef8210a7433132` | on-device U-Boot (U-Boot 2022.07, 2023-11-25), A slot |
| `dumps/router-20261108/ubootb.bin` (= `full/mtd2-ubootb.bin`) | 1,048,576 | `9736ab57d1bce93cf626048a9e949aad6d4bdbca599e62f058c86128c727db48` | A/B copy; identical except 2,042 bytes at file `0xA0000` [U] |
| `dumps/router-20261108/full/mtd0-esbc.bin` | 262,144 | `6574d747b40716585410ce21a377c5cf1cc33a17c7f6be8340859b470dedac1f` | esbc first stage (two copies) |
| `dumps/router-20261108/enva.bin` / `bdinfo.bin` | 262,144 each | `d1529faf...` / `49432e0f...` | env A and board-info evidence (referenced, not re-decoded here) |
| `tftp/recovery.bin` | 20,975,775 | `014d6f6062bd118e35ae7ee2fbc913859e81ccd1bf727d12c96875986a6f0d75` | vendor recovery image = vendor 2.4.15 sysupgrade image |
| `firmware/WR3000V2-R116-2.4.15-20251030-114751-sysupgrade.bin` | 20,975,775 | same as recovery.bin | vendor 2.4.15 image |
| `firmware/WR3000V2-R116-2.5.24-20260727-122111-sysupgrade.bin` | 22,024,351 | `ded2d835ed22a4fb26a305c999603631ea846dc5ef8f512aa006d72dd31d2f72` | vendor 2.5.24 image (second layout data point) |
| `dumps/router-20261108/full/mtd11-kernela.bin` | 8,650,752 | `6c7c0784fded8c48996f4a20e81a6907511223d47d0f532480abd78d70adb5d8` | stock kernel slot A (2.4.15, padded) |
| `dumps/router-20261108/full/mtd12-kernelb.bin` | 8,650,752 | `dffd16fc8dfb2c5edcbef95a328585267fc4362c95e2c6bc27ec2adfd8fbf904` | kernel slot B at dump time |
| `dumps/router-20261108/full/proc_mtd.txt` | - | - | partition table (sizes/offsets) |
| `dumps/router-20261108/uboot_capture6.log`, `uboot_capture7.log`, `serial_shell.log` | - | - | U-Boot banners, `multiupg detect`, `bootflag`/`bootreg` lines |
| `build/uart-logs/20261120T061118Z-uart-dump-run.log` (local, gitignored) | - | - | full TFTP-recovery transcript (quoted excerpts only) |
| `rootfs-2.5.24/sbin/oem-check` | 18,172 | - | stock image verifier (ELF ARM, stripped) |
| `rootfs-2.5.24/usr/lib/libhi_upgrade.so` | - | - | stock upgrade engine |
| `rootfs-2.5.24/lib/upgrade/platform.sh`, `lib/hsan.sh` | - | - | stock sysupgrade/upgrade shell path |
| `rootfs-2.5.24/usr/bin/fwtool` | - | - | vendor-patched fwtool (`FWx0` trailer) |

The RSA key (public, in U-Boot and oem-check) has modulus SHA-256 `f3e22478159283f0c3dc79e6c393bb215f60a40a09f9a64bb07bea84591b639d`, n0inv `0xf60d6ea5`, exponent 65537. It is the same key in the 2.4.15 build, the 2.5.24 build and oem-check; both vendor images verify under it. [V]

Partition table (`proc_mtd.txt`; all sizes hex, erase block 0x20000):
`esbc 0x0/0x40000`, `uboota 0x40000/0x100000`, `ubootb 0x140000/0x100000`, `enva 0x240000/0x40000`, `envb 0x280000/0x40000`, `fac 0x2C0000/0x200000`, `bdinfo 0x4C0000/0x40000`, `cfga 0x500000/0x200000`, `cfgb 0x700000/0x200000`, `log 0x900000/0x440000`, `pstore 0xD40000/0x40000`, `kernela 0xD80000/0x840000`, `kernelb 0x15C0000/0x840000`, `rootfsa 0x1E00000/0x1740000`, `rootfsb 0x3540000/0x1740000`, `rootfs_data 0x4C80000/0x1600000`, `upgrade 0x6280000/0x1D80000`. [V]

---

## 2. Boot chain, stage by stage

Observed console order (normal boot; `uboot_capture7.log`, `20261120T062950Z-uart-w12-run.log`):

    enter esbc main
     nand probe start......  nand probe succ ......
    nand read oob: offset 0x40000 ; nand read: size 0x100
     flash read uboot_region_size [100000]......
     This boot is main region area......
    nand read oob: offset 0x40000 ; nand read: size 0x100
     flash read uboot code len [6910c]......
    nand read: offset 0x40000 size 0x6910c
    enter uboot
    U-Boot 2022.07 (Nov 25 2023 - 18:18:57 +0000) for luofu
    DRAM: 123 MiB / NAND: 128 MiB / Loading Environment from NAND... OK
    Reset Info: Linux reboot cmd to reset
    bootflag a bootreg 10
    multiupg detect
    bootdelay is 3000
    ... mtd read kernel${bootflag} ...

### 2.1 esbc (mtd0, 256 KiB, two copies)

- The blob at `dumps/router-20261108/full/mtd0-esbc.bin` is two copies of a ~16 KiB Thumb-2 program (string/literal tables duplicated at +0x20000); the code literal pool uses `0x8001xxxx` addresses, i.e. it is linked/loaded at `0x80010000`. Entry at offset 0 is an ARM `b 0x18` that switches into the Thumb-2 body. [V]
- It initialises NAND/FMC, reads a raw (OOB-inclusive) 0x100-byte record at flash offset `0x40000` (= start of `uboota`), decides main vs backup U-Boot area ("This boot is main region area"), then reads the region header and U-Boot code length from that header. [V capture + code at `0x80012364`]
- The header fields it consumes are read by function `0x8001243c` after a `strcmp(x, "hboot1tag")` (`0x8001177c` helper): `+0x64` = U-Boot region size (0x100000), `+0x5c` = U-Boot code length. The 32-byte SHA-256 at `+0x18` (section 3) is **not** checked here. No SHA, AES or RSA code/constants/strings exist in the esbc blob. [V]
- It loads the U-Boot code to `0x81000000` (argument of the NAND-init/read helper at `0x80011840`) and jumps through a small dispatch table at `0x800136f0`; the "enter uboot" print is produced on that path. [V]
- **Consequence**: the esbc stage is a structural loader, not an authenticator of U-Boot in this build. U-Boot authentication would have to come from SoC secure boot (eFuse `sec_boot_en`) if fused; the state on this unit is [U]. This is relevant to "custom recovery" in section 8.3 but is **not** a recommendation to write U-Boot.

### 2.2 On-flash U-Boot region layout (`uboota`/`ubootb`, 1 MiB each)

| offset | size | content |
| --- | --- | --- |
| `0x00000` | 0x100 | `hboot1tag` header (section 3) |
| `0x00100` | code_len (0x6910C current) | U-Boot image (ARM vector table first) |
| `0x6918C` | zero pad | 0x00 filler to the version marker |
| `0x7FC00` | 18 | `OEM_BOOT %08u\n` (U-Boot version marker) |
| `0x7FC12` | 0x3EE | 0x00 filler |
| `0x80000` | 0x80000 | 0xFF erased on `uboota`; `ubootb` carries an unexplained 2,042-byte blob at `0xA0000` [U] |

- The `OEM_BOOT` marker is read by vendor `lib/hsan.sh` with `ubootver-offset` from the device tree (512 KiB; reads at offset 511 KiB): 2.4.15/recovery image = `OEM_BOOT 00000005`, current on-device U-Boot and the 2.5.24 image = `OEM_BOOT 00000007`. Stock `platform_do_upgrade_uboot` only flashes U-Boot when the image marker is numerically greater than the running one, so the 2.4.15 recovery package does not downgrade/update U-Boot (and the TFTP path never touches U-Boot at all, section 8). [V]
- The two U-Boot dumps differ only in the `0xA0000` blob; code/header/version marker are identical. [V]

---

## 3. The `hboot1tag` header (on-flash U-Boot image header)

256 bytes, little-endian unless stated:

| off | type | value | meaning | evidence |
| --- | --- | --- | --- | --- |
| 0x00 | char[16] | `hboot1tag` NUL padded | magic | [V] all four copies |
| 0x10 | u32 | 1 | format version | [V] |
| 0x14 | u32 | 0 | reserved | [V] |
| 0x18 | u8[32] | SHA-256 | SHA-256 of the code bytes `[code_off, code_off+code_len)` | [V] recomputed for uboota, ubootb, the 2.5.24 image and the 2.4.15/recovery build |
| 0x38..0x57 | - | 0 | reserved | [V] |
| 0x58 | u32 | 0x100 | code offset (payload starts right after this header) | [V] |
| 0x5C | u32 | varying | U-Boot code length: 0x6910C (2023-11-25 build), 0x6908C (older 2.4.15 build) | [V] matches the esbc print `flash read uboot code len [6910c]` and the image sizes |
| 0x60 | u32 | 0x3CB0 | **unknown constant** in every build; not read by esbc; not a tested checksum of the header; likely a format/tag constant | [V] value; [U] meaning |
| 0x64 | u32 | 0x100000 | U-Boot region size (as printed by esbc) | [V] |

Notes:
- The header is exactly a flashable U-Boot partition image: the vendor userspace `dd`s the first 512 KiB of the sysupgrade container (this header + U-Boot) into `uboot${flag}`. [V]
- The SHA-256 is present and correct in every artifact, but no consumer was found in esbc or U-Boot; it is presumably used by vendor factory/upgrade tooling [U].
- When the header sits inside a vendor sysupgrade image, it is covered by the package RSA signature (sections 6/7), so the image's U-Boot section is authenticated by the package signature even though esbc does not check the hash. [V]

---

## 4. U-Boot 2022.07: image, disassembly base and custom surface

- Banner: `U-Boot 2022.07 (Nov 25 2023 - 18:18:57 +0000) for luofu`; ARMv7, `DRAM: 123 MiB`, `NAND: 128 MiB`, watchdog 30 s. [V]
- Mapping used here for `dumps/router-20261108/uboota.bin` (and the first 512 KiB of the 2.5.24 image, which is byte-identical): **VA = file offset + 0x8003FF00**. The header is at file 0x0; the U-Boot vector table is at file 0x100 = VA 0x80040000. [V] (same mapping as `opensource/target-skeleton/docs/TRAILER-FORMAT.md`).
- The 2.4.15/recovery build inside `recovery.bin` is an older link: code length 0x80 bytes shorter, every string/table offset 0x30 lower than the current build (e.g. `bootfip` string file 0x4A0EE vs 0x4A11E), same instruction flow modulo the shift. Addresses below are for the **current/2.5.24 build** unless stated. [V]
- Custom commands relevant here, from the command table at file 0x57200-0x57500: `bootfip` (handler VA 0x80073BB8), `hi_upgrade` (handler VA 0x80048200, usage "upgrade cmd for hisilicon"), `imxtract`, `custombtn`, `ledblink`, `fdt`, `go`, `md`, `mtd`, `nand`, env/`saveenv`, network `tftpboot`, etc. [V]
- Default env strings embedded in the image (boot-failure fallbacks, not the live env): `bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}`, `esbcupdate_eth`, `ubootupdate_eth` (writes uboota and ubootb), `fipupdate_eth` (writes kernela and kernelb), `squashfsupdate_eth`/`rootfsupdate_eth` (writes rootfsa and rootfsb), `bdinfoupdate_eth`, and bootmenu entries 0-6 ("update rootfs.rw", "update rootfs.ro", "update kernel.images", "update uboot", "update esbc", "update bdinfo", "reboot uboot"). These are the serial rescue commands; the uboot/kernel/rootfs update ones write **both** slots, which is why the project never uses them. [V]

---

## 5. Pair selection: env `bootflag` and SYSCTRL `boot_reg` (re-confirmed)

This re-derives the contract already reconstructed in `opensource/docs/SELECTOR-0x21.md` and confirms it in both U-Boot builds; it is repeated because the recovery/upgrade formats depend on it.

- `bootcmd` reads `kernel${bootflag}` (`bootflag=a|b`), so the env variable is the selector. [V]
- The SYSCTRL cell at `0x10100C08` (`hsan,sysenv` `bootreg-offset = 0xc08`) is the loader's **failure record**: on every boot `bootfip` does a low-byte read-modify-write storing the *opposite* pair of the env value (`a -> 0x11`, `b -> 0x10`). If the cell contradicts the env on the next boot, U-Boot prints "boot from kernelX failed, change to kernelY", `setenv`/`saveenv`s the new `bootflag` and boots that pair. [V] (function VA 0x8004E1B8 and the bootfip store at VA 0x80073D48-0x80073DA4 in this build; same as the already-recorded decode.)
- A boot is stable only if userspace re-records the running pair (`0x10`/`0x11`) after it is up: vendor `/etc/init.d/hsan_appm` writes `/sys/devices/platform/sysenv/boot_done`; the project image has `/etc/init.d/luofu_record_pair` (START=96). Without that writer the next reboot follows the failover path. [V] vendor side / [I] project side design.
- `0x21` is the project's bench handover value, not a vendor value; the loader discards the 0x20 bit in the same boot (low-byte RMW). [V]
- True power-on clears the cell to 0; U-Boot then initialises it from the env (`bootreg init, set 10/11`). A failed handover cannot block the physical recovery door for that reason. [V]
- Vendor stock upgrade paths write only `0x10`/`0x11` (section 9). [V]

---

## 6. The FIP/DTB handoff contract (`bootfip`)

`bootcmd` reads the kernel slot with `mtd read` to `loadaddr = 0x83200000` and calls `bootfip ${loadaddr}`. The command handler is VA 0x80073BB8; the uImage loader is VA 0x800736DC; the overlay applier is VA 0x800743EC; the match helper is VA 0x800741B4. The full structural decode is in `opensource/target-skeleton/docs/TRAILER-FORMAT.md` and `tools/fip-slice.py`; this section restates the contract and adds the handoff/ATAG details not previously recorded. [V]

### 6.1 What is validated before the kernel is entered

1. uImage magic `0x27051956`; `ih_size` non-zero; base DTB at `image + 0x40 + ih_size` with FDT magic `d00dfeed`. [V]
2. Gate word `be32(base_dtb + base_dtb_totalsize) == 0xd7b7ab1e`. This is a 32-bit BE access at that address: an FDT totalsize that is not 4-byte aligned makes the load unaligned and U-Boot data-aborts (measured; `TRAILER-FORMAT.md` section 11). The vendor base DTB totalsize (0x79F4) is 4-byte but not 8-byte aligned and boots; the project pads to 8 bytes as a superset (`scripts/pad-dtb.py`). [V]
3. Descriptor: container total `<= 8 MiB`, entry count `<= 64`, `+8 == +12 == +20 == 32`; per entry `offset = be32(entry+4)` relative to the gate, overlay FDT valid with `totalsize <= 128 KiB`, family/`vendor_id`/`board_id` match (`luofu`, `hsan`, board strap 3 on R116), first match applied with libfdt to the base DTB. A bare uImage (no trailer), a wrong gate or no matching overlay makes bootfip print "boot apply overlay fail" and **not** enter the kernel; the loader then falls back to the other slot (silent fallback). [V]
4. uImage header/data CRCs and `ih_load`/`ih_ep` are **not** verified on this path (documented correction in TRAILER-FORMAT; reconfirmed). [V]

### 6.2 Handoff to the kernel (new detail: ATAG / flashinfo staging)

After the overlay apply succeeds, the loader at VA 0x800738E8-0x80073B60:

1. Calls a helper (VA 0x8007361C) with the patched base DTB. The helper finds the DT node `hsan,flashinfo_reserved`, reads its `atag-offset` property (vendor base DTB: `reg = <0x80600000 0x4000>`, `atag-offset = 0x1000`), and computes the ATAG staging address `0x80601000`. Errors print "error: atag-offset not set" / "error: no atag-addr exists". [V]
2. Builds an ATAG list at that address (`ATAG_CORE` = `0x54410001` inline at VA 0x80073A54; memory/command-line tags via `0x800409CC`; a custom flash-info tag builder at VA 0x80074B30; terminator written at the end). The custom tag value `0x5441000a` is at VA 0x80074BBC; the tag payload is platform flash geometry for the vendor kernel flash-info consumer (the node/`atag-offset` pair is the vendor `hi_flash` contract). [V code; consumer [I]]
3. If the U-Boot `machid` env variable exists it is parsed (hex) and used as `r1`; otherwise the default board value loaded from state is used. [V]
4. Disables caches/MMU, then:

       r0 = 0
       r1 = machid
       r2 = patched DTB address
       pc = kernel entry (derived by the kernel payload decompressor; the uImage
            load/entry fields are informational only on this path)

   i.e. the standard ARM DT-boot ABI. [V]

**Consequence for our images**: our kernel slice must keep the vendor contract bootfip enforces: base DTB totalsize 4-byte aligned minimum (the project pads to 8), a valid descriptor with at least one overlay matching (`luofu` family, `vendor_id = hsan`, board id 3), and it must not reuse the `0x80600000` 16 KiB reserved window if vendor userspace/kernels are ever mixed in (the vendor passes flash info there). The vendor base DT's `/chosen/bootargs` is `noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2 additional_cpus=1`. [V]

---

## 7. The vendor recovery/sysupgrade container (`hboot1tag` package)

### 7.1 Top-level byte map (both vendor images)

The file has a fixed skeleton; only sizes change between releases. Numbers are for 2.4.15 (= `recovery.bin`) with 2.5.24 in parentheses. All offsets are absolute file offsets.

| offset | size (2.4.15 / 2.5.24) | content |
| --- | --- | --- |
| `0x000000` | 0x100 | `hboot1tag` header, section 3 (`code_len 0x6908C / 0x6910C`) |
| `0x000100` | code_len | U-Boot image (older/current build as described) |
| `0x06918C` | pad to 0x7FC00 | zero padding |
| `0x07FC00` | 18 | `OEM_BOOT 00000005\n` (`...07` for 2.5.24) |
| `0x080000` | 0x100 | RSA-2048 signature (256 bytes, big-endian) |
| `0x080100` | 0x700 | zero padding; **not covered by the signature** (7.2) |
| `0x080800` | 0x13807AC / 0x14807AC | FIT image (`d00dfeed`), end `0x1400FAC` / `0x1500FAC` |
| after FIT | 8 zeros + 219-byte JSON + 16-byte `FWx0` trailer | fwtool metadata (7.5) |
| EOF | total 0x140109F / 0x150109F | both files end 0x...109F; the second is the first + 1 MiB |

The vendor image is padded so the FIT/metadata region ends at a fixed 1 MiB boundary: total sizes differ by exactly 0x100000 between the two releases. [V]

### 7.2 Signature block and hash coverage (verified end-to-end)

- `image[0x80000 .. 0x80100)` = RSA-2048 signature; exponent 65537.
- Signed data = SHA-1 over `image[0 .. 0x80000)` **concatenated with** `image[0x80800 .. EOF)`. That is: the whole 512 KiB U-Boot section (header, code, `OEM_BOOT` marker and zero padding), plus everything from the FIT onwards, including the FIT, the inner hi-upgrade package, the appended fwtool metadata and the final trailer. [V]
- Excluded: only `[0x80100, 0x80800)` (0x700 bytes of zero padding). The loader reads exactly the signature at 0x80000 and the FIT at 0x80800, so the hole has no consumer and is not a useful modification surface. [V]
- Verification reproduced in Python with `pow(sig, 65537, n)` and the key extracted from the image itself: both vendor images produce the exact PKCS#1 v1.5 block `00 01 FF...FF 00 30 21 30 09 06 05 2b 0e 03 02 1a 05 00 04 14 || SHA1`. Digest values: 2.4.15 `5d11a3e33750290562cc3df0247fdd38bbb6d550`, 2.5.24 `897fcb9e40c2a18e6eabfeb007623f1ad4b852d9`. [V]
- Key location in the U-Boot image: struct at file 0x54FD8 (older 2.4.15 build) and 0x55008 (current 2.5.24 build): `{u32 len_words = 64; u32 n0inv; u32 modulus[64] (LE); u32 rr[64]}` (n0inv and RR verified consistent; exponent 65537 in the verify code). The same modulus/n0inv pair is embedded in `oem-check` at file offset 0x4004. [V]
- Low-level print `rsa verify ok` and recovery-path print `firmware rsa verify ok` are both in the U-Boot image; failure prints `rsa verify error` / `firmware rsa verify error` and the recovery path refuses the image ("image not support" / "image recovery error"). [V]

### 7.3 FIT layer

- Standard flat device tree (`d00dfeed`, version 17) at `0x80800`. Root properties: `timestamp`, `description = "R116"`. `/images/ubi` node: `description = "kernel-rootfs image"`, `type = "firmware"`, `arch = "arm"`, `compression = "none"`, `hash@1 { value; algo = "crc32" }`, plus the big inline `data` property. [V]
- The `data` property starts at file `0x808A4` (FIT offset 0xA4) and has length 0x1380279 (2.4.15) / 0x1480279 (2.5.24), ending at `0x1400B1D` / `0x1500B1D`. It contains, in order: the hi_upgrade package (7.4), zero padding, and a second fwtool metadata + `FWx0` trailer at its very end (7.5). [V]
- The FIT is the object stock `fdt_dumpimage -T flat_dt -p 0 -w 1048576` extracts on device: subimage "ubi" = the hi_upgrade package. [V from stock `lib/upgrade/platform.sh`]

### 7.4 hi_upgrade package (the actual kernel+rootfs payload)

Header 0x5B8 bytes (1464), little-endian except where noted; this is the structure stock `platform_do_mtd_upgrade` and U-Boot's `hi_upgrade` command parse:

| off | type | 2.4.15 | 2.5.24 | meaning |
| --- | --- | --- | --- | --- |
| 0x000 | u32 (LE value) | 0x76543210 | same | package magic (`10 32 54 76` bytes) |
| 0x004 | char[20] | `wrt_ax3000_lite` | same | product string |
| 0x044 | char[16] | `22.03.06` | same | software version |
| 0x084 | char[16] | `1.0` | same | package version |
| 0x094 | char[16] | `luofu` | same | chip/platform id (U-Boot refuses a mismatch: "chip mismatch") |
| 0x0A4 | char[16] | `ctc` | same | vendor/build id |
| 0x0EC | u32 BE | 2 | 2 | number of sub-files |
| 0x0F0 | layer[0] u32[3] BE | type 0x00020002, offset 0x5B8, length 0x42258C | 0x00020002, 0x5B8, 0x421E38 | kernel sub-file: type, file offset, file length; char[16] version at +0xC |
| 0x13C | layer[1] u32[3] BE | type 0x00020003, offset 0x422B44, length 0xF00000 | 0x00020003, 0x4223F0, 0xFC0000 | rootfs sub-file: same layout |
| 0x5B0 | u32 BE | 0x01322CC4 | 0x013E2570 | `auc_pkg_total_length` printed by U-Boot; = header + kernel + rootfs + 0x180 |
| 0x5B4 | u32 BE | 0x96702C93 | 0xC423AA02 | header CRC-32 over header bytes `[0, 0x5B4)`, stored **without** the final XOR (raw CRC-32, init 0xFFFFFFFF); verified for both images |

Layer struct size is 76 bytes (0x4C); the two files are contiguous: kernel at `0x808A4+0x5B8 = 0x80E5C`, rootfs at `0x80E5C+kernel_len`. The kernel sub-file is the full FIP slice (legacy uImage + base DTB + gate/descriptor + overlays); the rootfs sub-file is the raw UBI image. [V]

Kernel slice inside the package (2.4.15 numbers, 2.5.24 in parentheses):

- uImage at 0x80E5C: magic `0x27051956`, `ih_size` 0x4185D0 (0x417DF0), load/entry `0x80608000`, OS=5 (Linux), arch=2 (ARM), type=2 (kernel), comp=0, name `Linux-5.10.201`. Payload ends at 0x49946C (0x498C8C). [V]
- Base DTB immediately after the payload (0x49946C / 0x498C8C), totalsize 0x79F4 (0x7A44), gate at DTB+totalsize = 0x4A0E60 (0x4A06D0). [V]
- Gate: `d7b7ab1e`; container total 0x2588 (0x25C4); two overlays (4,592/4,920 and 4,604/4,968 bytes) at gate+0x60 and gate+0x1250 (0x125C); container end = rootfs start = 0x4A33E8 (0x4A2C94). Exactly the `TRAILER-FORMAT.md` shape. [V]
- Rootfs UBI image = 0xF00000 (0xFC0000) bytes = 120 (126) PEBs of 0x20000; the `UBI#` EC magic is at the start of every 0x20000 block. [V]

After the rootfs sub-file, still inside the package total, is a 384-byte (0x180) high-entropy block. It is not addressed by any layer length and U-Boot's `hi_upgrade` command does not read it; it is most likely a per-package authentication/checksum trailer for vendor userspace (`ui_crc`/`ui_head_crc` strings exist in `libhi_upgrade.so` and oem-check). No MD5/SHA-1/SHA-256/SHA-512 of the header/kernel/rootfs/fip-slice was found in it under the tested models. [V presence/size; [U] exact fields. It is inside the RSA-signed region.]

### 7.5 fwtool metadata + `FWx0` trailer (two copies)

The vendor-patched `/usr/bin/fwtool` appends metadata and a signature chunk to a firmware file; its magic word is `FWx0` (bytes 46 57 78 30 = LE 0x30785746) instead of upstream OpenWrt's marker. In both vendor images the metadata JSON and a 16-byte trailer appear twice:

| copy | JSON start | JSON length | trailer bytes (hex) |
| --- | --- | --- | --- |
| outer (after FIT) | FIT end + 8 | 219 | `46 57 78 30 32 23 3d d8 01 00 00 00 00 00 00 f3` |
| inner (end of FIT data property) | 0x1400A28 (2.4.15) / 0x1500A28 (2.5.24) | 229 | `46 57 78 30 79 6f 87 0a 01 00 00 00 00 00 00 fd` |

Decoded trailer layout from the two copies: magic `FWx0`; a 4-byte unknown value (not one of the tested CRCs; candidate per-chunk id/CRC [U]); `version 1` (u8 plus 3 reserved zero bytes); a **big-endian u32 whose last byte equals payload length + 24** (outer 219+24 = 243 = 0xF3; inner 229+24 = 253 = 0xFD; the same relation holds for the 2.5.24 copies: 0xE3/0xF3 and 0xE5/0xFD). The +24 is consistent with an 8-byte chunk header (zero bytes immediately before the JSON) plus the 16-byte trailer. [V fields/relation; [U] the 4-byte unknown and the exact 8-byte header semantics.]

The JSON is standard OpenWrt `supported_devices` metadata: outer copy `supported_devices: ["R116"]` plus `version.dist/version/revision/target/board`; inner copy `supported_devices: ["ax3000_lite"]`. `fwtool` also contains the standard extract/append switches (`-I/-S/-i/-s`) and reads the FIT `description` property, so the vendor userspace can inspect this metadata; the loader paths in sections 7.2 and 8 do not. [V]

---

## 8. The TFTP recovery path (what actually happens)

### 8.1 Trigger and control flow

- Physical entry: power off, hold RESET, power on. During U-Boot init a button poll (function VA 0x80047E9C, GPIO framework) prints `RESET button is pressed for:  0 second(s) 1 second(s) 2 second(s)` while held, and after 2 s prints `Resetting...`, runs `ledblink 24 500` and sets a global "recovery requested" flag. This does **not** reset the SoC; it is a flag the later boot stage reads. [V]
- The `multiupg detect` stage prints its line and the configured bootdelay (normally `bootdelay is 3000` = 3 s x1000; one capture shows 48000, i.e. a persisted 48 s window, writer [U]); the normal autoboot then either continues `bootcmd` or, if the recovery flag is set, the recovery decision function (VA 0x8004E9BC, "recovery button press" string at VA 0x8008718B) jumps into the TFTP/flash loop at VA 0x8004E818. [V]
- The loop (VA 0x8004E818) forever:
  1. `run_command("tftpboot recovery.bin")` (server `192.168.1.88`, device `192.168.1.1`, load address `0x83200000`, the normal U-Boot TFTP path); [V]
  2. checks the FIT at `0x83280800` (`fdt_check_header`) and compares the FIT root `description` with `R116` ("firmware format error" / "firmware name error(%s %s)"); [V]
  3. RSA-verifies the loaded image exactly as in 7.2 (`rsa verify ok` / `firmware rsa verify ok`; on failure `firmware rsa verify error` and refuse); [V]
  4. calls the flash function at VA 0x8004E550 ("flash image"). [V]

### 8.2 What the flash function writes (measured, full capture)

The flash function (VA 0x8004E550 current build) first runs `imxtract 0x83280800 ubi` (extract the FIT `ubi` subimage; U-Boot copies it to the `loadaddr` area `0x85600000`), then runs `hi_upgrade <fileaddr> <filesize>`, which parses the package (7.4) and prints `auc_pkg_total_length`, `kernel len/offset`, `rootfs len/offset`, `upgrade soft version`, and exports `kernel_addr/kernel_len/rootfs_addr/rootfs_len` into the environment. The flash function then (`memcpy` to the `0x85600000` staging buffer; `mtd erase` + `mtd write`) writes:

    kernela  <- kernel slice (0x42258C bytes; page-rounded to 0x422800 on the write)
    kernelb  <- the same kernel slice
    rootfsa  <- rootfs UBI (0xF00000 bytes)
    rootfsb  <- the same rootfs UBI
    mtd erase rootfs_data

then prints "resetting ...", executes the U-Boot reset and the board reboots. Excerpt from the attended capture (`build/uart-logs/20261120T061118Z-uart-dump-run.log`, local):

    Bytes transferred = 20975775 (140109f hex)
    rsa verify ok
    firmware rsa verify ok
    flash image
    command buf: imxtract 83280800 ubi
    ## Copying 'ubi' subimage from FIT image at 83280800 ...
    crc32+ hi-upgrade: fileaddr = 832808a4, filesize = 1380279
    command buf: hi_upgrade 0x832808a4 0x1380279
    hi-img: addr = 0x832808a4, size = 0x1380279
    auc_pkg_total_length = 20065476
    kernel len 0x42258c offset 0x5b8
    rootfs len 0xf00000 offset 0x422b44
    upgrade soft version: 22.03.06
    Flashing kernel...
    command buf: mtd erase kernela;mtd write kernela 0x85600000 0x0 0x42258c
    Writing 4335616 byte(s) (2117 page(s)) at offset 0x00000000
    command buf: mtd erase kernelb;mtd write kernelb 0x85600000 0x0 0x42258c
    ...
    command buf: mtd erase rootfsa;mtd write rootfsa 0x85600000 0x0 0xf00000
    Writing 15728640 byte(s) (7680 page(s)) at offset 0x00000000
    ...
    Erasing rootfs_data...
    resetting ...
    reset_cpu -> enter esbc main

Conclusions from the capture (this closes a previously-open question):
- Recovery writes **both** A and B kernel/rootfs slots with the same image and erases the shared `rootfs_data`; it never touches `uboota`/`ubootb`, `esbc`, the env blocks, `bdinfo`, `fac`/`cfga`/`cfgb`, `log` or `pstore`, and never writes the SYSCTRL selector cell. [V]
- Recovery therefore destroys an A/B install (both pairs replaced) and the persistent configuration (rootfs_data erased), but preserves loader/env/board data and cannot be blocked by a bad selector value. [V]
- `hi_upgrade` rounds the kernel write length up to the NAND page (0x42258C -> 0x422800 on the write line); the slot stays erased beyond the written bytes. Kernel slot size 0x840000, rootfs slot 0x1740000. [V]
- The recovery image's own embedded U-Boot section is **not** used by this path: recovery is executed by the already-running on-device U-Boot. The embedded U-Boot would only be used by the stock web/sysupgrade path (9.1) when the `OEM_BOOT` version increases. [V]

### 8.3 Can a custom recovery image be made?

No, not as a TFTP recovery image:
- The U-Boot recovery path verifies RSA-2048/PKCS#1v1.5-SHA1 over the whole image except the signature block (7.2). Any modified byte in the signed range fails. The excluded 0x700-byte hole has no consumer: the FIT is read at 0x80800 and the signature at 0x80000, both fixed.
- The recovery image is byte-identical to a stock vendor sysupgrade image, so its U-Boot section is signed too; there is no unsigned recovery-only section.
- The vendor private key is not available, and the same public key is in every build and in `oem-check`, so a substitute key cannot be introduced either.
- Practical recovery artifacts are therefore (a) a stock vendor image (the known 2.4.15 `recovery.bin`, or a newer signed release if one appears) and (b) the stock userspace install/upgrade tools, which are the actual install vehicle.

Possible-but-not-recommended loader path: esbc checks only `hboot1tag` magic and region/code sizes (2.1) and contains no crypto; if the SoC secure-boot eFuse is not fused, a custom U-Boot image written from stock root *could* be loaded by esbc (and a custom U-Boot could then run unsigned installs). This is [I]/[U], high brick risk (a bad `uboota`/`ubootb` pair has no recovery path that does not depend on U-Boot; the RESET+TFTP door itself lives in U-Boot), and it does not change the conclusion above. It is recorded as an option for a future attended loader experiment, not as a recovery design.

---

## 9. Vendor sysupgrade / web / serial verification paths

### 9.1 Stock sysupgrade CLI path (stock OpenWrt 22.03.6)

From `rootfs-2.5.24/lib/upgrade/platform.sh` and `lib/hsan.sh`:

1. `platform_check_image` calls `oem-check -b <board> -o <bootloader bytes hex> -f <image>` and then `image_demux`. [V]
2. `oem-check` (ELF ARM, stripped; strings: "firmware format error", "firmware name error(%s %s)", "firmware header crc error", "firmware data crc error", "firmware verify ok", "In RSAVerify(): Padding check failed!", "rsa verify failed") re-implements the container checks in userspace: format at the start of the file (FIT header checks plus a uImage fallback scan), FIT name against the board argument, then the same RSA verification; its embedded public key matches U-Boot's (same modulus/n0inv). [V]
3. `image_demux` splits the file with `dd bs=2k`: `uboot_skip` comes from the device-tree `ubootver-offset` (512 KiB on this board); it keeps `count=uboot_skip/2 = 256` 2 KiB blocks (512 KiB) as the bootloader image and `skip=uboot_skip/2+1 = 257` blocks (0x80800) as the FIT, then extracts subimage `ubi` from the FIT with `fdt_dumpimage` and replaces the image with it. These constants are the empirical proof of the container skeleton in 7.1. [V]
4. `platform_do_upgrade_uboot` compares the image `OEM_BOOT` version (read at `ubootver-offset-1` KiB of the 512 KiB bootloader image) with the running version and, only if greater, does `mtd erase`/`mtd write` the first `uboot_skip/2` KiB into `uboot<upgrade_flag>` (and, when the sysenv node exists, also into `uboot<boot_flag>`). [V]
5. `platform_do_mtd_upgrade` parses the hi_upgrade header (7.4) with `dd` at fixed offsets: kernel length at `240+8 = 248` (BE), kernel data at 1464, rootfs length at `240+76+8 = 324` (BE), rootfs data after the kernel; it writes `kernel<upgrade_flag>` and `rootfs<upgrade_flag>` (the pair opposite to the boot cell), then `platform_set_boot_reg <flag>` writes `0x10` (pair a) or `0x11` (pair b) to `/sys/devices/platform/sysenv/boot_reg`. [V]
6. The stock upgrade therefore writes the *inactive* pair and moves the selector; the loader's mismatch/failover path re-stamps env `bootflag` on the next boot (section 5). [V]
7. `fw_setenv` is not usable on the running image (the project recorded `/tmp/fw_setenv -> /bin/fw_printenv` as a print-only trap); the env is owned by the loader. [V parent evidence; restated because it matters for install]

### 9.2 Stock web UI path

`libhi_upgrade.so` is the web/autoupgrade engine (strings: `hi_upgrade_pkg_*`, `hi_upgrade_mtd_fip_onetrack_write`, `find uboot_web_upg_img`, `--pst_header->auc_head_crc`, `--board chip is %s, firmware chip is %s`, `signature error`, `hi_hi_encrypt_rsa_de_call`, `hi_hi_encrypt_sha256_image`, `upgrade flag:%u bootreg:%x`, `echo 0x$x > /sys/devices/platform/sysenv/boot_reg`). The exact web packaging (`.zip` with the same `.bin` plus `md5.txt`) and the exact order of its checks were not fully decoded. What is [V]:
- the web package contains the same container as recovery (`recovery.bin` is that container);
- the stock userspace verifies it with `oem-check` in the sysupgrade path and with `libhi_upgrade` in the web path, both using the vendor RSA key;
- after writing, the stock path writes `0x10`/`0x11` to the sysenv cell and (`platform_do_upgrade_uboot`) may update U-Boot when the version increases;
- no stock path writes a signed image to *both* pairs except the TFTP recovery path (8.2) and the default-env rescue commands (4). [V]

### 9.3 Stock recovery image properties that matter

- Load address `0x83200000`; file name exactly `recovery.bin`; server `192.168.1.88`; device `192.168.1.1`. [V]
- Size 20,975,775 bytes for 2.4.15; a newer release would change size and version but must carry a valid vendor signature (the loader checks only signature/format, not a release string). [V scheme; [I] newer-image acceptance]
- The stock recovery does not downgrade U-Boot (2.4.15 package has version 5; current U-Boot is 7; the TFTP path never writes U-Boot at all). [V]

---

## 10. Our target image vs the vendor container (compatibility matrix)

| property | vendor package (`recovery.bin` / web `.bin`) | our target container (`luofu-image.py`) |
| --- | --- | --- |
| outer shape | hboot1tag U-Boot section + RSA sig + FIT | deterministic ustar tar (`sysupgrade-<board>/` dir) |
| members | U-Boot 512 KiB, kernel slice, rootfs UBI inside FIT `ubi` | `CONTROL`, `kernel` (uImage+trailer), `root` (UBI), `fip-manifest.txt`, `INSTALL-STATUS.txt` |
| check | oem-check / U-Boot RSA-2048 SHA-1 vendor key | project `platform_check_image` re-derives every byte; refuses foreign/vendor images |
| install | stock `platform_do_mtd_upgrade` / web lib | our `platform_do_upgrade`: inactive pair only, selector last, no env write, no rootfs_data erase |
| loader acceptance | U-Boot `bootfip` gate `0xd7b7ab1e` etc. | same gate/trailer contract (the FIP slice is byte-compatible); rootfs is a plain UBI image |
| signed? | yes, vendor RSA | no signature; integrity is the project's byte-level check + sha256 manifest |

Consequences:
- Stock U-Boot recovery, stock `sysupgrade` and the stock web UI **cannot install** our image (no vendor RSA signature / wrong format); our `platform_check_image` **refuses** vendor images. The two upgrade worlds are disjoint by design. [V]/[I]
- The install path used on the bench is the stock userspace telnet installer (`tools/stock-telnet-install.sh`): write the inactive pair from stock root, set env `bootflag` and move the selector consistently, reboot. It is also the only path because (a) UART TX is dead (U-Boot console not usable) and (b) the vendor recovery write path cannot be repurposed. [V parent docs]
- The project's A/B upgrades run from **our** image (`platform_do_upgrade` in `opensource/target-skeleton/target/linux/hisilicon/base-files/lib/upgrade/platform.sh`): tar check -> inactive pair write -> read-back -> selector last. It never writes env or `rootfs_data`. Its requirements and risks are in section 11. [V]
- Our kernel slice must remain a valid FIP slice: full uImage + base DTB (4-byte-aligned totalsize minimum, project uses 8) + descriptor with a matching overlay; a bare uImage or truncated slice is refused by bootfip and silently falls back to the other pair. [V]

---

## 11. What a robust install / recovery design needs

### 11.1 Loader env re-stamp (stock side)

- The env is the pair selector; the loader owns it. There is no working userspace `fw_setenv` on the stock image. Two safe mechanisms:
  1. **Mismatch path (preferred)**: write the *opposite* recorded-pair value (`0x10` when running B, `0x11` when running A) to the SYSCTRL selector after the pair has been written; the loader re-stamps env `bootflag` on the next boot itself. This is what our installer and the stock `platform.sh` rely on, and what the captures show. [V]
  2. **Raw env block write (fallback only, high risk)**: env blocks are `crc32-le(4) | flags(1) | data(131067)` in a 128 KiB block at `mtd3`/`mtd4`; a hand-written block must fix the CRC and both copies. Avoid unless the mismatch path is unavailable; a corrupt env costs a recovery cycle.
- A true power-on clears the selector cell, so a bad cell state can never block the physical recovery door. [V]

### 11.2 Per-slot trailer handling

- Never write kernel slots with anything less than the complete FIP slice: uImage `(0x40 + ih_size)` + base DTB (totalsize at least 4-byte aligned; the project pads to 8) + 32-byte gate (`d7b7ab1e`, total, 32, 32, count, 32, ...) + 32-byte entries + overlays. Keep the slot's erased tail as `0xFF`; bootfip does not read past the container.
- Validate on the host exactly as bootfip does (`fip-slice.py check` / `luofu-image.py check-container`) and on the device via the project's `platform_check_image` before writing. Both already do this. [V]
- The 628-byte post-container region found in stock kernel slots is not part of the format and not needed by bootfip (documented in `TRAILER-FORMAT.md`; unchanged). [V]

### 11.3 `platform_do_upgrade` correctness review (project image)

The current project implementation is host-tested only. Points the format evidence supports or flags:

- Correct: writes only the inactive pair; kernel-slice validation mirrors bootfip; rootfs validation checks UBI geometry; selector written **last**; never writes env; never erases `rootfs_data`; refuses `0x00`/`0x21`/unknown selector states; two-witness check (selector vs booted UBI mtd number). [V code]
- Dependency: the two-witness check and the "next boot boots the written pair" model require the boot-time pair recorder (`luofu_record_pair`) to run on every successful boot; without it the selector reads the loader's opposite-pair marker and the next reboot follows the failover path. The recorder's behaviour on hardware is [U] until an attended A/B validation. [V mechanism; [U] end-to-end]
- Assumption to verify on hardware: after writing the inactive pair and the selector, the loader's mismatch path re-stamps env `bootflag` and saves it (decoded from the loader and observed for the bench `0x21` recipe, but not yet for this exact installer sequence). [V mechanism; [U] end-to-end]
- Missing/needed for full robustness: an attended A/B install rehearsal with a deliberate failure of the new pair (to prove failover back to the old pair), and a recovery rehearsal from a deliberately broken pair using the stock TFTP image. [I]
- Do not add a bootloader write to `platform_do_upgrade`. U-Boot updates are version-gated in the stock path and need the signed vendor container; our target has no signed U-Boot image and no way to produce one. [V]

### 11.4 Recovery fallback

1. Keep a stock signed recovery image and its hash pinned. It is the only TFTP recovery vehicle; it writes both pairs and erases `rootfs_data` (8.2), so after using it the box is on the recovery image's release (2.4.15 for `tftp/recovery.bin`) with a clean config, and must be re-installed via the stock telnet installer. [V]
2. Recovery is physical: RESET held through the counter, host at `192.168.1.88/24` serving `recovery.bin`, device at `192.168.1.1`; no agent-only path exists (UART TX dead; no network command reaches the recovery loop). [V]
3. A failed/broken selector value cannot block recovery (POR clears the cell). [V]
4. If both kernel slots are bad but U-Boot is intact, recovery still boots because it is U-Boot (from flash) that TFTPs, verifies and flashes. Only corrupted U-Boot or esbc is unrecoverable over this door; that is why U-Boot must never be written by our tools. [V]
5. After recovery, run the stock telnet installer (pair-b flow) and verify env `bootflag` plus selector agreement before reboot; keep the old pair intact. [V parent procedure; [U] hardware rehearsal record]

### 11.5 Recovery image bookkeeping

- Pin: recovery.bin sha256 `014d6f60...` / md5 `421114c8...`; size 20,975,775; the file is the 2.4.15 vendor sysupgrade image. Do not edit it (signature).
- The 2.5.24 image is the same format and is also signed; either can be served, but the loader will only flash kernel/rootfs (not U-Boot), so the running loader version stays whatever is already in `uboota`/`ubootb`.

---

## 12. Tooling recommendations

Host-side, read-only (all commands run from the repo root):

1. **Structural container dump / CI gate** - `opensource/docs/incoming/loader-re/decode-vendor-image.py` (this directory) parses the outer header, signature block, FIT, hi_upgrade header, layers, fwtool metadata/trailers and verifies the RSA signature by extracting the public key from a supplied U-Boot image. Use it to classify any new vendor release before touching the bench, and in CI on image artifacts.
2. **FIP slice validation** - existing `fip-slice.py check` (trailer/gate/overlay rules) and `luofu-image.py check-container` (tar/CONTROL/kernel/rootfs) remain the canonical checks; this report does not replace them.
3. **U-Boot disassembly helper** - `arm-none-linux-gnueabihf-objdump -D -b binary -m arm --adjust-vma=0x8003ff00` for the current build (and `0x8003FED0` for the older 2.4.15 build when disassembling a vendor image file directly). For esbc use `-M force-thumb --adjust-vma=0x80010000`.
4. **Recovery expectation check** - before any attended recovery, confirm the served file hash equals the pinned value and the FIT description is `R116` (the loader checks both; a wrong file fails with "firmware name error" / "firmware rsa verify error").
5. **Never** add a signed-image bypass, a custom `recovery.bin`, or a loader write to the installer on the strength of this document. The only non-vendor loader option (custom U-Boot) is explicitly [I]/[U] and needs its own attended plan with a full dump and a recovery door that does not depend on U-Boot.

---

## 13. Disassembly / code reference table (current build unless noted)

| role | VA | file offset | note |
| --- | --- | --- | --- |
| U-Boot vector table start | 0x80040000 | 0x100 | mapping VA = offset + 0x8003FF00 |
| `bootfip` handler | 0x80073BB8 | 0x33CB8 | |
| uImage loader | 0x800736DC | 0x337DC | gate/overlay validation |
| overlay applier | 0x800743EC | 0x344EC | gate magic literal at VA 0x80074668 |
| match helper / table | 0x800741B4 / 0x800952E8 | 0x342B4 / 0x553E8 | 11 rows x 24 B |
| ATAG/flashinfo staging helper | 0x8007361C | 0x3371C | reads `hsan,flashinfo_reserved` + `atag-offset` |
| flash-info ATAG builder | 0x80074B30 | 0x34C30 | tag 0x5441000a literal at VA 0x80074BBC |
| kernel-entry path | 0x800739xx-0x80073B60 | - | r0=0, r1=machid, r2=DTB, `blx` entry |
| `hi_upgrade` command handler | 0x80048200 | 0x8300 | parse header/layers, export env |
| FIT name check | 0x800134D0 / 0x800134B4 | 0x35D0 / 0x35B4 | FIT `description` vs "R116" |
| RSA verify | 0x80069E88 | 0x29F88 | SHA-1 + RSA-2048 PKCS#1 v1.5 |
| recovery decision | 0x8004E9BC | 0xEA8C | prints "recovery button press" |
| recovery TFTP/flash loop | 0x8004E818 | 0xE918 | `tftpboot recovery.bin` + verify |
| recovery flash function | 0x8004E550 | 0xE650 | `imxtract` -> `hi_upgrade` -> mtd writes |
| button poll / recovery flag | 0x80047E9C | 0x7F9C | "RESET button is pressed for" / ledblink / flag |
| pair selection fn | 0x8004E1B8 | 0xE2B8 | env/`bootreg` mismatch path |
| `bootfip` cell store | 0x80073D48 | 0x33E48 | opposite-pair low-byte RMW |
| `multiupg detect` | 0x8007E2E8 | 0x3E3E8 | reads/prints `bootdelay` |
| esbc loader main | 0x80012364 (Thumb) | - | `-M force-thumb --adjust-vma=0x80010000` |
| esbc U-Boot magic/size | 0x8001243C | - | reads header +0x5C/+0x64 |
| esbc enter-uboot | 0x80012244 | - | loads at 0x81000000 and jumps |

The 2.4.15 build inside `recovery.bin` has these addresses 0x30 lower (e.g. recovery loop 0x8004E7E8, flash function 0x8004E520, RSA key struct file 0x54FD8), same instruction flow. [V]

---

## 14. Unknowns / gaps (honest list)

| # | unknown | today's reading | how to close |
| --- | --- | --- | --- |
| U1 | meaning of `hboot1tag +0x60 = 0x3CB0` | constant across all builds; not read by esbc; not a tested checksum | vendor U-Boot source/tooling; not needed for install |
| U2 | esbc main/backup region selection and OOB record format | main observed every boot; code reads a raw 0x100-byte record at 0x40000 and has a main/backup branch | further esbc RE (Thumb functions around 0x800121xx/0x80012278) plus one read-only OOB dump |
| U3 | the 384-byte high-entropy trailer after the rootfs inside the hi_upgrade package | inside the signed region, not read by U-Boot's hi_upgrade; likely userspace signature/CRC trailer (`ui_crc` strings) | `libhi_upgrade.so` RE with the vendor kernel crypto path |
| U4 | fwtool `FWx0` trailer field at +4 and the 8-byte chunk header | 16-byte trailer decoded except one 4-byte word; size field = payload+24 | vendor fwtool RE (or vendor build logs) |
| U5 | who consumes `hboot1tag +0x18` SHA-256 | correct in all builds; no consumer in esbc/U-Boot found | vendor factory tooling |
| U6 | the `ubootb` 2,042-byte blob at file 0xA0000 | content unidentified; A/B copies otherwise identical | needs context/another dump |
| U7 | 628-byte post-container region in stock kernel slots | not part of the format, not required by bootfip | unchanged from `TRAILER-FORMAT.md`; not needed |
| U8 | SoC secure boot (eFuse `sec_boot_en`) state on this unit | unknown; if un-fused, a custom U-Boot is loadable in principle | read-only efuse inspection on hardware (attended) |
| U9 | exact web-UI/libhi_upgrade check order and the `FWx0`/signature chunk role | same key and container as oem-check/U-Boot; not fully decoded | `libhi_upgrade.so` RE |
| U10 | writer of the observed `bootdelay is 48000` (48 s) window | recovery-adjacent bench state; not needed for install | trace the setenv if it ever matters |
| U11 | after a stock recovery, why the next boot often shows `bootflag b`/`bootreg 11` | env/cell untouched by recovery (8.2); the preceding state explains it | read-only env/cell capture post-recovery |

---

## 15. Reproduction commands (host-only, no device)

    # 1. outer header / signature / FIT / hi_upgrade / fwtool dump of a vendor image
    python3 opensource/docs/incoming/loader-re/decode-vendor-image.py \
        --image tftp/recovery.bin --uboot dumps/router-20261108/uboota.bin

    # 2. FIT walk (either the script above or fdtdump on the extracted FIT)
    dd if=tftp/recovery.bin of=/tmp/fit.bin bs=1 skip=$((0x80800)) count=$((0x13807ac)) 2>/dev/null
    fdtdump /tmp/fit.bin | head -40

    # 3. hi_upgrade header CRC (raw CRC-32, no final XOR, over bytes [0,0x5B4))
    python3 - <<'PY'
    import struct,zlib
    d=open('tftp/recovery.bin','rb').read(); h=d[0x808a4:0x808a4+0x5b8]
    stored=struct.unpack_from('>I',h,0x5b4)[0]
    calc=(~zlib.crc32(h[:0x5b4]))&0xffffffff
    print(hex(stored),hex(calc),stored==calc)
    PY

    # 4. U-Boot disassembly (current build / older build / esbc)
    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump \
      -D -b binary -m arm --adjust-vma=0x8003ff00 \
      --start-address=0x80073bb8 --stop-address=0x80074690 \
      dumps/router-20261108/uboota.bin
    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump \
      -D -b binary -m arm -M force-thumb --adjust-vma=0x80010000 \
      --start-address=0x80012300 --stop-address=0x800124c0 \
      dumps/router-20261108/full/mtd0-esbc.bin

---

## 16. Cross-references

- `opensource/target-skeleton/docs/TRAILER-FORMAT.md` - FIP trailer/descriptor/overlay match rule, gate semantics and the 8-byte alignment finding (this report adds the ATAG/flashinfo staging step and the recovery-path use of the same gate).
- `opensource/docs/SELECTOR-0x21.md` - complete selector reconstruction; this report re-confirms the loader code and adds the recovery/upgrade consequences.
- `opensource/docs/FIRST-INSTALL.md` - install design; this report adds the recovery image write-set (both pairs + rootfs_data) and the "no custom recovery image" conclusion.
- `opensource/docs/incoming/upstream-blockers/` (flashinfo reserved-memory window) - section 6.2 gives the loader side of that contract.

*End of report. Host-only; no device was touched, no binary or full firmware blob was committed.*
