# Phase 42-decode: the night the decode run closed its own questions

Night of 2026-10-09 (UTC). Eight producer lanes, one independent verifier. No device action, no
kernel module, no git commit anywhere in the run. This report is the close-out record: what the
platform turned out to be, where the guard boundary sits, and which lane results survived
re-execution.

Verification of record: `build/tmp/bsp-notes/verify/VERDICT.md` (verifier `st_01a11ced`, which
re-ran each lane's core command or recomputed its value independently; producer summaries were not
trusted). Raw re-execution logs sit beside it as `build/tmp/bsp-notes/verify/reexec-*.out.txt`.

---

## 1. The platform discovery

Our Cudy WR3000 v2.0 belongs to a **Triductor/HiSilicon-family silicon world, TR6560-class**. Its
own device tree calls the SoC `hsan-luofu` (a HiSilicon-style `hsan,` compatible prefix) and the
Banana Pi BPI-Wifi6 **THG6500-TAX2** BSP calls the same class `triductor,tr6560` (a `tri,` prefix).
Two vendors, two names, one memory map.

The BSP is the cousin-chip open-source stack, cloned at `build/tmp/bsp` from
`https://github.com/BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP.git` at commit `daeb1eab` ("modify led
gpio pin"), an OpenWrt 22.03 tree for `target/linux/tr6560` with `DEVICE_THG6500-TAX2` selected in
its committed `.config`. It carries the sibling boards `THG6500-TAX2.dts` and `THG6400-TAC2.dts`
plus the SoC file `triductor-tr6560.dtsi`.

### The register-level match list

From `build/tmp/bsp-notes/dtscmp/delta.md` (lane `st_01a11ce5`), which decompiled our base DTB
(`build/tmp/uboot-art/vendor_base.dtb`, sha256 `947ec62d...`, 176 nodes, root `compatible =
"hsan-luofu"`) and diffed it node by node against the BSP trees. Every SoC-identity node lands on
the **same MMIO address in both trees**:

| SoC block | node kind | address (ours = BSP) |
|---|---|---|
| GIC (Cortex-A9) | `interrupt-controller` | `0x10181000`, CPU iface `0x10180100` |
| SCU | `scu` | `0x10180000` |
| L2 cache | `l2-cache` (pl310) | `0x7f000000` |
| TWD local timer | `local_timer` | `0x10180600` |
| SP804 timer | `timer` | `0x10104000` |
| UART0 / UART1 | `uart0` / `uart1` | `0x1010e000` / `0x1010f000` |
| FMC flash controller | `fmc` | `0x10a20000` + `0x1c000000` |
| PCIe RC/EP | `pcie` x2 | `0x10160000`, `0x10164000` (config window `0x10161000`) |
| GPIO | `gpio0` / `gpio1` | `0x10106000` / `0x10107000` |
| system controller | `system-controller` (sysctrl) | `0x10100000` |
| clock/reset generator | `crg` | `0x14880000` |
| iomux | `iomux` | `0x14900000` |
| TV sensor | `tvsensor` | `0x14900500` |
| boot-params reserve | `flashinfo` | `0x80600000` (16 KB) |

The Cudy blob describes a **superset** of the SoC: 176 nodes against the BSP's 49 (dtsi) + 16
(board), and it adds blocks the BSP never models (the five `hsan,mac` gemac controllers, the
`lsw_dp`/`lsw_pfe` switch front ends, `ledpwm`, `jent-rng`, the efuse sub-node family). The
differences sit in the board layer and in how much of the SoC each vendor bothered to describe,
never in the memory map. Same `crg` block, same GIC, same PCIe windows, same flash controller.

### The Wi-Fi stack is the same family, rebuilt

From `build/tmp/bsp-notes/drivdiff/map.md` (lane `st_01a11ce3`). Our stock Wi-Fi pair
`hi5622v100_wifi.ko` + `hi5622v100_plat.ko` and the BSP's `peanut_wifi.ko` + `peanut_plat.ko` are
the **same HiSilicon/Triductor HCC MAC driver family** split into a platform shim plus a MAC
module, rebuilt by two vendors for two SoCs. The shared code spine, by symbol-prefix count: wifi
`hmac_*` 1403, `wal_*` 677, `mac_*` 304, `alg_*` 162, `hdpp_*` 131, `hal_*` 77, `oal_*` 71; plat
`oal_*` 155, `hcc_*` 73, `pcie_*` 72, `hwifi_*` 47. The two platform shims are near-identical in
surface (784 shared symbols of 1,053 / 1,026). The modules differ in kernel identity
(`vermagic=5.10.201` ours vs `5.10.138` theirs, plus `preempt`), so neither set loads on the other
kernel without a rebuild, and the shared module contract survives: `g_en_rx_packet_4096_length`
on the wifi module, `g_ini_file_name` on the platform module.

The finding that matters for the downstream port: **`credit`, `mbox`, `mailbox` and `doorbell` are
zero in all four `.ko`** (rawscan, and an independent case-insensitive regex scan agrees at
0/0/0/0). The `hcc`/`pcie`/`ring` vocabulary is present on both sides; the credit/mailbox terms
the port expected are absent from the vendor binaries entirely.

### The boot chain has a stage we can name

From `build/tmp/bsp-notes/bootblob/analysis.md` (lane `st_01a11ce2`). The BSP ships
`TR6560-bootimage_nodtb.bin`, a 69,625-byte ARMv7 bootram at base `0xc0030000`. It carries an LZMA1
stream at file offset `0x19c8` that unpacks to exactly 148,000 bytes (sha256 `c4e4aeb4...`), and
that second stage carries the whole SDK command set (`load_boot`, `load_image`, `tftp`, `rcs`/`ccs`
for the A/B boot flag, `reg_write`, `Setup_boot_atags`, `tri_bootm_linux`). The bootram stages the
next stage at `0x80600000`, the same address the DTS reserves as the `flashinfo` boot-params block
and the same region our live `/dev/mem` probe read in section 2 below.

---

## 2. The guard boundary

State carried forward from the boot-matrix night, on our own device's kernel image
(`build/tmp/kboot/mtd12-pre-2415.bin`, mtd12 "kernelb", 8,650,752 bytes, sha256
`64ae2ffa...`):

- The zImage payload's first region is **free**: offsets `z[0x00-0x1f]` can be modified without
  the box refusing to boot.
- The region from **`z[0x20]` onward is guarded**: modifications there stop the boot.
- **Content verification is real.** The refusal is not a size or header sanity check; the box is
  inspecting bytes, and it acts on the content.
- **The mechanism is unresolved.** Nothing in this run names the code that performs the check.

Two lanes converged on the u-boot side of that question and both came back with a *negative* that
is itself load-bearing.

From `build/tmp/bsp-notes/ubootguard/hunt.md` (lane `st_01a11ce4`). The gate that actually refuses
a modified image is the **DTBO/overlay container check**, not a checksum. The boot command
`bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` reaches `do_bootfip` =
`FUN_800735a4` and loader `FUN_800736dc`, and the kernel entry at `0x800738xx` is taken *only*
when the overlay applier `FUN_800743ec` returns 0. That function reads the 4 bytes at the end of
the main DTB (`fdt_end`) and requires the magic `0xd7b7ab1e` (literal at file `0x34668`). The gate
is position-sensitive by construction: any change to `ih_size`, to the main DTB size, or to the
bytes where the container lives moves `fdt_end`, the applier fails, and the loader prints
"boot apply overlay fail" and never jumps.

The per-image container table makes the pattern unambiguous. Only the three vendor-content images
carry `0xd7b7ab1e` at that offset; every candidate the parent fired that did not boot carries its
own plain FDT magic `0xd00dfeed` (or `past-end`) there instead:

```
mtd12-pre-2415.bin    dtb@0x418610 totalsize=0x79f4  overlay-container@0x420004 magic=0xd7b7ab1e
vendmarked.bin        dtb@0x418610 totalsize=0x79f4  overlay-container@0x420004 magic=0xd7b7ab1e
kernelb-2.5.24-full.img  dtb@0x417e30 totalsize=0x7a44  overlay-container@0x41f874 magic=0xd7b7ab1e
u32test.bin / dcrc20.bin / be_e20.bin / onebit.bin / piggy.bin / ... overlay-container@0x420004 magic=0xd00dfeed
luofu-slice.bin       dtb@0x370988 totalsize=0x2a37  overlay-container@0x3733bf magic=past-end
```

Every verify-shaped primitive in the image is **off the boot path**. The uImage header/data CRC
verifier (`FUN_80042830`, `image_check_hcrc` / `image_check_dcrc`) is present, and so are two SHA-1
engines, an RSA-verify routine, three CRC-32 tables and a FIT hash verifier. The Ghidra call-graph
pass (`ghidra-reach.txt`) resolves every one of them as NOT reachable from `FUN_800736dc`, and the
corroborating byte evidence agrees: the deliberately-wrong-`dcrc` fixture `dcrc20.bin` still
carries the plain `0xd00dfeed` at the container offset, so a data-CRC check cannot be what refused
it. A stable CRC-32 table sits at `0x40570` and a second at `0x40a18` (both `e0=0x0, e1=0x77073096,
e128=0xedb88320, e255=0x2d02ef8d`), and the SHA-256 constant `0x6a09e667` and the AES S-box are
absent from the image.

The tail region compounds the puzzle. From `build/tmp/bsp-notes/tailblob/decode.md` (lane
`st_01a11ce1`): a 628-byte (`0x274`) obfuscated structure at file offset `0x42258c`, immediately
after the base DTB and two overlay FDTs with no gap, followed by 4,315,136 bytes of `0xFF` to the
partition end. Every byte belongs to a two-family marker alphabet: bytes at even offsets are `0xCC`
with 1-3 bits flipped, odd offsets are `0x66` with 1-3 bits flipped, so the transform is to first
order a 2-byte-repeating XOR key. Across 8 decode models and every plausible covered range, the
scripts found **zero** fields matching any crc32/crc16/sum/md5/sha1/sha256 of any range, while the
same code does find the uImage's own `hcrc`/`dcrc`/`size` fields byte-for-byte. So the tail is not
a plaintext integrity record with a checksum we can read off.

From `build/tmp/bsp-notes/flashinfo/dump.md` (lane `st_01a11ce6`) came the live cross-check, and it
came back **unrelated**. A read-only 16 KB capture of physical `0x80600000`
(`live-80600000-16k.bin`, sha256 `823631c5...`) shares **zero** 8-byte and **zero** 16-byte
sequences with the mtd12 tail slice. The live region is ARM `.text` (14,374 of 16,384 bytes
nonzero, 0 strings, cond-field distribution `e` on 225/226/227/229, `movw`/`str`/`bl` idioms),
while the mtd12 tail is a low-entropy scrambled field (98 distinct byte values). The scrambled
store at `0x80600000` does not even contain the plaintext word `devmem` returned at the same
address (`cc66cc76` is absent from the captured stream), so the two regions are not the same class
of read. If a match was expected (the tail struct mirrored into flash-info SRAM), the dump disproves
it at the byte level.

**Where that leaves the boundary.** It is real and it inspects content, but the check that fires is
the DTBO overlay-container magic at `fdt_end`, and the obfuscated tail sits unread by any checksum
we can reconstruct. The z[0x20..] refusal and the tail structure are both still open; the
mechanism is not among the verify primitives on the u-boot boot path.

---

## 3. Lane results

All eight lanes were re-executed by an independent verifier. **Every lane below is PASS.** The
quoted lines are the verifier's own re-execution, not the producer's.

### tailblob - PASS

Decisive claim: the 628-byte obfuscated trailer at `0x42258c..0x422800` in
`build/tmp/kboot/mtd12-pre-2415.bin`, with the positive control that the same code recomputes the
uImage header CRCs.

```
sha256 64ae2ffaec1e925f0f701c0a049c9e827ccc102691d6d681156250ea0fde87cb size 8650752
magic 27051956 hcrc 112cee61 size 004185d0 dcrc cf4ae686
hcrc recompute 112cee61 MATCH True
dcrc recompute cf4ae686 MATCH True
blob len 628 all nonzero True first byte dc last 66
0xFF run from 0x422800: True
distinct blob bytes 98
```

```
=== decode raw                  | target-valued windows: 0 ===
=== decode parity:66,cc         | target-valued windows: 0 ===
=== decode bitrev8              | target-valued windows: 0 ===
=== decode and7f                | target-valued windows: 0 ===
```

The U-Boot 2022.07 `uImage` naming the Cudy Luofu target ships in this image. The zero-digest-match
result reproduces across all 8 decode models.

### bootblob - PASS

Decisive claim: the 69,625-byte `TR6560-bootimage_nodtb.bin`; its LZMA1 stream at `0x19c8` unpacks
to exactly 148,000 bytes; header words at `0x30`/`0x34` declare base `0xc0030000`.

```
bootram size 69625
lzma-from-0x19c8 decompressed len 148000
sha256(decompressed) c4e4aeb40ab347462f61696cf0d3b03df5436ea68567214e1d315690a3765896
EQUAL to stage2.bin True
hdr word 0x30 (=48): 0xc0030000
```

```
bootram [\x20-\x7e]{4,} matches: 761
stage2 matches: 983 (unique 923)
b'load_boot' 123892   b'setup_boot_atags' 120134
```

### drivdiff - PASS

Decisive claim: the cross-map numbers (modinfo key counts, symbol/string counts, shared counts) and
that `credit`/`mbox`/`mailbox`/`doorbell` are zero in all four `.ko`.

```
hi5622v100_wifi.ko    depends=1 import_ns=211 license=4 name=1 parmtype=1 vermagic=1
hi5622v100_plat.ko    depends=1 import_ns=46  license=8 name=1 parmtype=1 vermagic=1
peanut_wifi.ko        depends=1 license=4 name=1 parmtype=1 srcversion=1 vermagic=1
peanut_plat.ko        depends=1 license=7 name=1 parmtype=1 srcversion=1 vermagic=1
```

```
term        hi5622v100_wifi.ko hi5622v100_plat.ko     peanut_wifi.ko     peanut_plat.ko
credit                       0                  0                  0                  0
mbox                         0                  0                  0                  0
mailbox                      0                  0                  0                  0
doorbell                     0                  0                  0                  0
ring                       305                 33                323                 22
hcc                        103                206                 88                182
pcie                        25                472                 14                432
```

The verifier also ran its own zero-term scan (`credit/mbox/mailbox/doorbell = 0/0/0/0`) and it
agrees.

### ubootguard - PASS

Decisive claim: the pre-entry gate is the DTBO/overlay container check in `FUN_800743ec` (file
offset `0x343ec`), which requires the magic `0xd7b7ab1e` (literal at file `0x34668`) at `fdt_end`;
and that the uImage CRC verifier and the SHA-1/CRC32/RSA code are not on the boot path.

```
file 0x34668 magic 1eabb7d7          # = d7b7ab1e LE
mtd12-pre-2415.bin    dtb@0x418610 magic=0xd00dfeed totalsize=0x79f4  overlay-container@0x420004 magic=0xd7b7ab1e
u32test.bin           dtb@0x418610 totalsize=0x79f4  overlay-container@0x420004 magic=0xd00dfeed
dcrc20.bin            hcrc=ok dcrc=BAD  overlay-container@0x420004 magic=0xd00dfeed
```

```
CRC32 table @0x40570 e0/e1/e128/e255: 0x0 0x77073096 0xedb88320 0x2d02ef8d
SHA1 constA @0x29618: 0123456789abcdeffedcba9876543210
sha256 6a09e667 present: -1 (LE) / -1 (BE);  AES sbox 637c777b present: -1
cmd table @0x571e8 = 8008a01e 00000010 80053980 800735a4 8008a026   # bootfip / 0x10 / common hook / do_bootfip
```

The U-Boot 2022.07 banner for the Luofu target sits in this image. Every byte-level decisive
evidence reproduces, and the uImage hcrc/dcrc/size triple verifies.

### dtscmp - PASS

Decisive claim: `vendor_base.dtb` decompiles to 176 nodes; the BSP trees carry 49/16 nodes; the DTB
sha256 is `947ec62d...` (md5-identical to `dt-spec/{uboota_main,kernela_main}.dtb`).

```
sha256 947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3 size 31220
node count 176
compatible b'hsan-luofu\x00'
cudy.dts re-parsed nodes 176
triductor-tr6560.dtsi size 7573  nodes 49
THG6500-TAX2.dts     size 3304  nodes 16
```

### flashinfo - PASS (documentation deviations)

Decisive claim: the saved 16 KB capture `live-80600000-16k.bin` and the verdict that the live
region is **unrelated** to the mtd12 tail struct (0 shared 8/16-byte sequences; ARM code; no
strings). Verified from the saved local artifacts only, no ssh.

```
823631c534ab09338f026da7ed627916662e94019d82cf2b73c1dcf6c9cbfe22  live-80600000-16k.bin (16384)
bin size 16384 gz-dec size 16384 gz==bin True
first 16 bytes eb2145e3922383e00120a0e3a332a0e1    # ARM .text
nonzero 14374 / 16384
strings -a -n 4 matches 0
shared 8-byte 0   shared 16-byte 0
```

Deviations are in `dump.md` prose only, not the artifact or verdict: the report says the reference
is "6144 bytes of 0xFF to the EOF at 0x840000" where the measurement is a 4,315,136-byte
(`0x41d800`) `0xFF` run from `0x422800`; it names `0x66` as the byte at `+0x26f` where that byte is
`0x6e` (`0x66` is the last byte of the full 628-byte run at `+0x273`); and it records the gz length
as unstable (8472 vs 8457) where the saved stream is authoritative at 8457. None of these touch the
bytes or the "unrelated" verdict. The node-name/`reg` mismatch it flags (`flashinfo@0x80c00000`
with `reg = <0x80600000 0x4000>`) is a stale node name; the address that matters is `0x80600000`.

### GPL letter - PASS (4 deviations)

Decisive claim: the rewritten `JETON-GPL-EMAIL.txt` names real components, in particular the vendor
`/lib/hisilicon/ko` modules. The task's specific deviation hunt (a module name in the letter absent
from the actual rootfs listing) does not occur.

```
ls rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/ | wc -l
58
PRESENT hi5622v100_wifi.ko   PRESENT hi5622v100_plat.ko   PRESENT hi_basic.ko   PRESENT hi_crg.ko
PRESENT hi_clk.ko            PRESENT hi_efuse.ko          PRESENT hi_flash.ko   PRESENT hi_acp.ko
PRESENT hi_kipc.ko
hi_kcfe_* count: 14
```

```
hi5622v100_wifi.ko .modinfo license entries: GPL, GPL, GPL, GPL          # no "GPL v2"
hi5622v100_plat.ko .modinfo license entries: GPL x7, "GPL v2" x1         # has "GPL v2"
```

All 9 named modules plus the `hi_kcfe_*` family are present in the actual listing. The letter
carries no credentials, no non-ASCII bytes, and no em or en dashes. Deviations are in the letter's
changelog, not the letter body: the changelog claims the unpacked rootfs is not present in this
checkout (it is, at `rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/`); it claims every module name
in the letter appears in tracked text (false for `hi_basic.ko`, `hi_crg.ko`, `hi_efuse.ko`,
`hi_flash.ko`, `hi_acp.ko`, `hi_kipc.ko`, each with 0 tracked occurrences, since the rootfs listing
is gitignored); it claims both Wi-Fi modules carry `license=GPL v2` where only
`hi5622v100_plat.ko` carries that literal; and the updated 4905-byte letter was not mirrored into
the repo-root copy at the time of verification.

### BSP CI - PASS

Decisive claim: `opensource/.github/workflows/bsp-tr6560-build.yml` parses as YAML, is
`workflow_dispatch`-only, uses the stated deps/clone/overlay/artifact steps, and the BSP it builds
provides the `.config` it assumes.

```
bytes 2916 sha256 faba03c7eabef824abd1c6f33cf2b771a714c56fcbce247211c1887c89857c3c
has workflow_dispatch True
runs-on ubuntu-22.04 timeout 360    steps 9
grep -c "DEVICE_THG6500-TAX2" build/tmp/bsp/.config  ->  2
build/tmp/bsp/target/linux/tr6560/  -> Makefile base-files files-5.10 generic image patches-5.10
```

The clone is pinned to `BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP`, the local clone is at `daeb1eab`,
and the BSP commits the 284,002-byte `.config` the build step enforces.

---

## 4. The GPL letter update

`JETON-GPL-EMAIL.txt` was rewritten this run (4905 bytes; tracked, and currently modified in the
worktree at the repo root). The old draft asked for "the U-Boot sources" and "the other GPL
packages", which let a reply stop at upstream U-Boot and upstream OpenWrt. The new letter names
what the firmware actually ships:

- the vendor kernel modules in `/lib/hisilicon/ko` as an explicit item, with the Wi-Fi stack
  `hi5622v100_wifi.ko` and `hi5622v100_plat.ko` named, plus the platform/clock/reset/efuse/flash/
  acp/kipc/kcfe families by category;
- the `/lib/firmware/hi_wifi` files as an item, with the caveat that a binary blob may not be GPL
  and the sender should say so rather than send nothing;
- the boot chain split into esbc plus U-Boot, with the two U-Boot slots from the mtd table named so
  a reply cannot quietly cover one;
- OpenWrt/LuCI split out of the catch-all, so the private `luofu_V200` target profile, the device
  tree and the LuCI controllers are asked for by name.

The structure matches `CUSTOM-FIRMWARE-PLAN.md` section 4, the GPLv2 3(a)/3(b) framing is kept, and
the tone, greeting, closing and the `hisi_trunk`/`opal22` flow identifiers are unchanged.

The verifier re-listed the real rootfs and confirmed every named module exists (58 modules in
`/lib/hisilicon/ko`, 14 of them `hi_kcfe_*`). The letter's own changelog carries the false caveat
that the unpacked rootfs is missing from the checkout, the false over-claim that all names appear
in tracked text, and the Wi-Fi license over-claim; those are changelog defects to fix before
sending, not letter defects. The `hi5622v100_plat.ko` module does carry both `GPL` and `GPL v2`
strings, which is what the license claim should say.

---

## 5. Open items (UNVERIFIED or FAIL)

Nothing in this run is UNVERIFIED or FAIL at the lane level: the verifier re-derived every decisive
claim. These are the open threads it left, stated so nobody reads them as results.

1. **The guard mechanism.** The z[0x20..] refusal is real and content-based, but the check that
   fires on a modified image is the DTBO overlay-container magic, and the obfuscated tail is not a
   checksum we can reconstruct. The code path that inspects `z[0x20..]` is still unnamed. The
   runner-up is the kernel-side self-extractor / gzip CRC inside the payload, which is out of scope
   for a u-boot hunt.
2. **The obfuscated tail's meaning.** The 628-byte structure's transform is a 2-byte-repeating XOR
   key to first order, but no field maps to any digest of any covered range across 8 decode models.
   What it encodes is open.
3. **The `0x80600000` relationship.** The live region was proven *unrelated* to the mtd12 tail, so
   the expected mirror does not exist. Whether the tail lives anywhere in SRAM/flash-info is
   unanswered; the `devmem` path returns scrambled content, not the plaintext word, so the two are
   not even the same class of read.
4. **Ghidra call-graph results not re-derived.** The "NOT reachable from boot path" verdicts in
   `ghidra-reach.txt` and the bootblob "615 functions" count were not re-executed with Ghidra (the
   verifier did not run it). They are corroborated by byte evidence but not independent.
5. **The second u-boot build.** A different u-boot build exists at
   `C:/Users/ShibbityShwab/AppData/Local/Temp/uboota.bin` (md5 `3d942555...`, 288,982 bytes
   different from the flash image, first diff at `0x18`). Its guard code was not inspected.
6. **The esbc partition.** mtd0 (`build/tmp/uboot-art/esbc`, 16,245 bytes gzipped) was not
   disassembled. It is a first-stage/secure stage, and the one place a hardware trust chain could
   still hide.
7. **The device-side experiment logs.** The live witness-register readings and the
   `RETURN_AT_DELTA` outcomes for the fired candidates were not written into the repo (the scripts
   print to console only), so the accepted/refused mapping is inferred from the task brief and the
   script names. This is the one link in the chain a file cannot confirm.
8. **The letter changelog defects** in section 4 above, to fix before the letter is sent.

---

## 6. Next actions

Named, in order:

1. **Find the z[0x20..] check.** Continue on the kernel side: the zImage self-extractor and its
   gzip CRC are the natural place, since the u-boot verify primitives are all off the boot path.
   Reproduce with `build/tmp/bsp-notes/ubootguard/checksum.py` and the container table in
   `containers.py` against a fresh candidate.
2. **Disassemble the esbc blob** (`build/tmp/uboot-art/esbc`, mtd0) before declaring the u-boot
   side exhausted, since a hardware trust chain there would explain a content check that has no
   boot-path caller.
3. **Read the second u-boot build** at `C:/Users/ShibbityShwab/AppData/Local/Temp/uboota.bin` for
   guard code, or label it out of scope explicitly.
4. **Fix the GPL letter changelog**: drop the false "rootfs unavailable" caveat, correct the
   tracked-text claim for the six modules with 0 occurrences, and state the license accurately
   (`hi5622v100_plat.ko` carries `GPL v2`, `hi5622v100_wifi.ko` carries `GPL` only). Mirror the
   updated letter into the repo-root copy.
5. **Run the BSP CI** (`bsp-tr6560-build.yml`) once with the dtscmp lane's `cudy.dts` placed under
   `opensource/lab/cudy-wr3000v2/` before the overlay step, to get a real TR6560-class build as
   the port's reference.
6. **Correct the flashinfo prose** (`build/tmp/bsp-notes/flashinfo/dump.md`): the `0xFF` run is
   4,315,136 bytes, the last byte of the 628-byte run is `0x66` at `+0x273`, and the saved gz is
   authoritative at 8457 bytes.
7. **Re-derive the Ghidra reachability verdicts** in an independent pass, or record them as
   corroboration-only in the ledger.

---

## Provenance

| lane | task | deliverable | verdict |
|---|---|---|---|
| tailblob | `st_01a11ce1` | `build/tmp/bsp-notes/tailblob/decode.md` | PASS |
| bootblob | `st_01a11ce2` | `build/tmp/bsp-notes/bootblob/analysis.md` | PASS |
| drivdiff | `st_01a11ce3` | `build/tmp/bsp-notes/drivdiff/map.md` | PASS |
| ubootguard | `st_01a11ce4` | `build/tmp/bsp-notes/ubootguard/hunt.md` | PASS |
| dtscmp | `st_01a11ce5` | `build/tmp/bsp-notes/dtscmp/delta.md` | PASS |
| flashinfo | `st_01a11ce6` | `build/tmp/bsp-notes/flashinfo/dump.md` (worktree `ta818b4ec4d`) | PASS (doc deviations) |
| GPL letter | - | `JETON-GPL-EMAIL.txt` (worktree `tdb6629b512`) | PASS (4 deviations) |
| BSP CI | - | `opensource/.github/workflows/bsp-tr6560-build.yml` | PASS |

Independent verifier: `st_01a11ced` -> `build/tmp/bsp-notes/verify/VERDICT.md` with
`reexec-*.out.txt` beside it. No device action, no kernel module, no commit in this run.
Timestamps are UTC. No credential values appear anywhere in this file.
