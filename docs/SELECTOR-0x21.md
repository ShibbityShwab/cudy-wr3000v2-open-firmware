# SELECTOR-0x21 - the SYSCTRL bootreg 0x21 handover, reconstructed from local evidence

**Status: host-only reconstruction. No device action, no UART/TFTP/flash, no boot or
flash claim for our own target.** Every hardware observation below is re-read from
artifacts already in the parent (private) repository: the 2026-11-08 partition dumps,
the 2026-11-07..17 read-only console captures, the vendor rootfs trees, and the recorded
ledger. Nothing here validates our driver or installer on hardware.

- **Task:** shared task-30, 2026-11-18. Deliverable: this document (new file; no shared edits).
- **Question:** what exactly is the SYSCTRL bootreg value `0x21` (manual B / recovery
  handover), when is it written, how does the stock loader treat it on the next boot
  (does it normalise to `0x10`/`0x11`, does it persist, what does it print), and what
  does that mean for the selector driver (017), the installer (`platform_do_upgrade`)
  and the recovery procedure?
- **Evidence base:** named in section 10; each claim carries a marker.

**Markers used in this document**

| marker | meaning |
| --- | --- |
| **[V]** | verified by a named local artifact or a reproducible host-side check (dumps, capture lines, disassembly addresses, ledger entries) |
| **[I]** | inferred: the conclusion follows from [V] evidence, but the composite behaviour was not itself directly observed |
| **[U]** | unknown / not measured locally; the statement depends on a fact nobody has closed yet |
| **[X]** | contradicted by local evidence (a claim found in existing docs that the artifacts do not support) |

## 0. Short answer

1. **`0x21` is not a magic "manual B" opcode in the loader.** [V] In every image we have
   (U-Boot `uboota`/`ubootb`, the ESBC stage, the vendor kernel driver, the vendor
   userspace) the only thing that reads the cell interprets **the low byte**, and the only
   thing that *writes* it from the loader writes `0x10` or `0x11`. The `0x20` bit of
   `0x21` is overwritten (the loader's write is a low-byte read-modify-write) before Linux
   starts, and no local vendor binary was found that originates `0x21`.
2. **The boot pair is selected by the env `bootflag`, not by the cell.** [V] `bootcmd` is
   `mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` (env dump). The cell is
   the loader's **failure record**: on every boot `bootfip` sets the cell's low byte to the
   *opposite* pair of the one the env names; if the boot attempt fails before userspace
   records success, the next boot sees the mismatch and re-stamps the env to the other
   pair ("boot from kernelX failed, change to kernelY"). A boot that succeeds must leave
   the cell matching the env or the *next* reboot flips slots.
3. **`0x21` is the bench-recipe handover value**, [V] written by the project's own
   `FLASH-PLAN.md` slot-switch recipe and `tools/fire-device.sh` (`echo 0x21 >
   /sys/devices/platform/sysenv/boot_reg`) together with `bootflag=b` in both env copies.
   The vendor's own sysupgrade paths write only `0x10`/`0x11` (`libhi_upgrade.so`,
   2.5.24 `platform.sh`). The sysenv kernel interface will accept `0x21` because it stores
   any low byte the caller gives it - that is not evidence that the vendor chooses it.
   [X] the claim in patch 017 / the AB-install summary that "the vendor writes 0x21".
4. **On the next boot the loader normalises it.** [V] Console: `bootflag b bootreg 21` ->
   `bootreg offset: c08` / `boot regval: 21` -> boot pair B (`kernelb`/`rootfsb`); the
   bootfip write then stores `0x10` (opposite of b). If the image boots to userspace, the
   vendor init re-writes the cell to the running pair (`0x11`); if it does not, the cell
   stays `0x10` and the next boot fails over to A. **`0x21` never persists.** A true
   power-on reset clears the cell to `0` in every capture; a warm reset keeps whatever the
   last writer left.
5. **Implication:** for our own pair the "handover" is the same mechanism for `0x11` and
   `0x21`; what makes an A/B install *stable* is a boot-time writer that re-records the
   running pair (the vendor's `boot_done`). Without it the loader's opposite-pair marker
   survives and the *second* boot silently rolls back. The installer must therefore ship
   (or depend on) a "boot done" step, and until an attended hardware validation exists the
   install path stays [U].

---

## 1. Where the selector lives: SYSCTRL cell, not the env block

### 1.1 The cell

- The board's vendor DTS describes a `hsan,sysenv` node with `bootreg-offset = 0xc08`;
  the SYSCTRL page is `0x10100000`, so the selector is the 32-bit cell at
  **`0x10100C08`**. [V] `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko` string table
  (`bootreg-offset`, `hsan,sysenv`); disassembly of the same module (section 5.2);
  `parent:BENCH-FACTS.md` sec.5; ledger phase "the env is not the selector" (2026-10-12).
- The vendor kernel driver `hi_ksysenv.ko` exposes it as
  `/sys/devices/platform/sysenv/boot_reg`. The driver's **show** prints only the low byte;
  its **store** is a read-modify-write that replaces **only the low byte** and preserves
  the upper 24 bits; its separate **`boot_done`** store toggles the low byte between
  `0x10` and `0x11` (see 5.2). [V] disassembly in section 5.2.
- The same `.ko` is byte-identical in the 2.4.15 and 2.5.24 rootfs trees
  (md5 `e325b525b9f57978ea9801d7af6ea75b`). [V]
- Values actually observed in local artifacts: `0x00`, `0x10`, `0x11`, `0x21` (full
  32-bit values; upper bytes zero in every observation). [V] console `boot regval:` lines
  and sysfs prints in the captures listed in section 4/10. No other value is invented or
  observed.

### 1.2 The env blocks `enva`/`envb` (mtd3/mtd4)

- Partitions: `mtd3 enva` and `mtd4 envb`, `0x40000` bytes each. [V]
  `parent:dumps/router-20261108/full/proc_mtd.txt`.
- Each holds one **128 KiB U-Boot environment block** with the layout
  `crc32-le(4) | flags(1) | data(131067)` and `0xFF` fill after the 128 KiB block. [V]
  verified on both dumps: the stored little-endian dword equals the standard CRC-32 of
  `bytes[5:131072]` (mtd3 `0x233a84dd`, mtd4 `0x6bcefd87`; see appendix B). The `crc32-le |
  flags | data` claim in the older notes is therefore correct, and the env is *not* where
  the selector lives.
- The env data holds the boot selector variables, including
  `bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` and
  `bootflag=a`. [V] both dumps at dump offset 197 (`bootflag=a`) and offset 0x82
  (`bootcmd=`).
- The loader saves a changed env on the failover path: captures show
  `Saving Environment to NAND... Erasing redundant NAND... Erasing at 0x280000...` or
  `Erasing NAND... Erasing at 0x240000...`. [V] `parent:build/uart-logs/20261116T075623Z-3dh-fire.log`
  lines 284-287 and 1470-1473. `mtd4` starts at NAND `0x280000` and `mtd3` at `0x240000`
  (sizes from `proc_mtd.txt`), so those are the primary/redundant env copies; which copy
  is written alternates with the redundant-env scheme. [I]

### 1.3 How the two interact (the decoded contract)

The loader's rule, reconstructed in section 3:

- **`bootflag` (env) names the pair the loader will actually read**: `a` -> `kernela`,
  `b` -> `kernelb`. [V] `bootcmd` + captures.
- **The cell low byte records the other pair** while the boot attempt is in flight, and
  after userspace has run it should record **the running pair**. [V] disassembly 3.3 +
  captures 4.2-4.6.
- **Mismatch = failure report.** If env names `a` but the cell says `0x11` (or env `b`
  and cell `0x10`), the loader concludes the previous attempt of the env pair failed: it
  prints `boot from kernelX failed, change to kernelY`, writes `bootflag=Y` into the env,
  and boots Y. [V] disassembly 3.2 and captures 4.4.
- Therefore the pair is switchable by *either* writing the env (changes what bootcmd
  reads) *or* writing a cell value that contradicts the env (the loader flips the env).
  The supported/installed path is the second one, because the vendor offers no usable
  env writer: `fw_setenv` is absent on the running image and the project's record that
  `/tmp/fw_setenv -> /bin/fw_printenv` "answers as the env writer" is a trap - it prints
  but does not write. [V] `parent:BENCH-FACTS.md` sec.5; ledger entries 2026-10-11/12.

### 1.4 Claims in earlier project docs that this reconstruction corrects

| earlier claim | status | why |
| --- | --- | --- |
| "0x21 boots slot B" (`FLASH-PLAN.md` recipe) | [V] true on *this bench*, but for the pair (`bootflag=b` in env) + (`0x21` in cell), not from the cell alone | cell alone with env `a` does **not** boot B: no mismatch fires and `bootcmd` reads `kernela` (section 3.2) |
| "the env `bootflag` is an output the loader re-stamps from the cell" (AB-install summary, `platform.sh` header) | [X] as a general statement | the env is the selector `bootcmd` uses; the loader re-stamps it **only on the mismatch/failover path** (3.2), and it writes the **cell** from the env on every boot (3.3) |
| "0x21 is written by the vendor sysenv manual/recovery handover" (patch 017 comment, ABI doc, AB-install summary) | [X] | vendor userspace writes only `0x10`/`0x11` (5.1); `0x21` originates in our bench recipe (5.4) |
| "0x20 (manual A) not established" (`BENCH-FACTS.md` sec.5) | [V] correct | no local artifact writes or observes `0x20`; loader logic makes the bit ineffective (2.3) |

---

## 2. What `0x21` actually is

### 2.1 Byte decomposition

- Low byte `0x21` = binary `0010_0001`: bit 0 set, bit 5 set, bit 4 clear.
- The recorded-pair values are `0x10` (A) and `0x11` (B): bit 4 is set in both, bit 0
  selects the pair. [V] 4.1/4.2.
- **The loader's only cell write clears the whole low byte and then ORs in `0x10` or
  `0x11`, from the env `bootflag`.** [V] disassembly 3.3 (`bic r3, r3, #0xff` then
  `orr #0x11` / `orr #0x10`). Consequence: bit 5 never survives the next boot, and there
  is no branch anywhere in the loader that tests bit 5. [V] the only compares on the cell
  low byte are against `0x00`, `0x10`, `0x11` (3.2).
- So `0x21` is best read as **"B pair trial value, encoded as `0x11` plus a marker bit
  that our recipe uses and the loader ignores"**. The *behaviour* of `0x21` and `0x11`
  under the stock loader is identical when the env `bootflag` is `b`; the difference is
  only the console print (`bootreg 21`) and the fact that `0x21` does not accidentally
  satisfy the `a`-vs-`0x11` mismatch test (which `0x11` does).

### 2.2 Who writes it

- [V] **The bench recipe writes it**, not the vendor: `FLASH-PLAN.md`
  "Slot switch recipe" lines 193-202 prints the value map and the switch commands;
  `parent:tools/fire-device.sh` lines 15-35 default `BOOT_REG=0x21` for `kernelb`;
  `parent:tools/fire.sh` line 136 passes `BOOT_REG=0x21` for slot B. [V] The `show/store
  boot_reg` console lines at the head of every fire capture are that write:
  `[ 231.542012] store boot_reg: 21` / `[ 231.573582] show boot_reg: 21`
  (`parent:build/uart-logs/20261116T052813Z-fire7-3df.log` lines 1-2), repeated in every
  subsequent fire capture (section 10 lists them).
- [V] **The vendor userspace writes the cell too, but only `0x10`/`0x11`:**
  - `rootfs-2.4.15/usr/lib/libhi_upgrade.so` (identical md5 `a5f018ea2a8b21e8aa600370592516bc`
    in both rootfs trees) contains the literal
    `echo 0x$x > /sys/devices/platform/sysenv/boot_reg` and, immediately after the
    upgrade-flag IPC call, formats `0x10 | flag` and runs it (disassembly of the function
    at file offset `0x6118`-`0x6164`: `movne r7,#17` / `moveq r7,#16`; prints
    `upgrade flag:$u bootreg:$x`). `flag` is 0/1 - the `/home/cmd/upgrade/hi_upgrade_flag_set`
    parameter file declares `range<0x0,0x1>`. [V]
  - `rootfs-2.5.24/lib/upgrade/platform.sh` `platform_set_boot_reg()`: prints
    `echo "0x10"` for partition `a` else `"0x11"` (lines 74-82), called after writing the
    kernel and rootfs of the **inactive** pair (line 122). [V] The 2.4.15 variant of the
    file does not touch `boot_reg` at all; it delegates to `hi_ipc`/`libhi_upgrade.so`. [V]
- [V] **The loader itself writes the cell on every boot**: `bootreg init` at power-on when
  it reads 0, and the opposite-pair marker in `bootfip` (3.3).
- [U] No local artifact shows the vendor's TFTP recovery image writing `0x21`; the
  disassembled loader recovery/flash function (section 6) contains **no** store to
  `sysctrl+0xc08`. Whether the recovery image's userspace writes the cell is not
  observable from the local evidence.

### 2.3 What it is not

- **Not a second selector.** `bootcmd` uses the env variable; the cell selects nothing by
  itself. [V]
- **Not preserved.** [V] sections 3.4 and 4.5.
- **Not symmetric with `0x20`.** No artifact has `0x20`; and since the loader's decision
  code never looks at bit 5, "manual A" (if written as `0x20`) would behave exactly like
  `0x11`'s opposite marker: with env `a` it would be overwritten to `0x11` and the boot
  would proceed on A; with env `b` it would be overwritten to `0x10` and the boot would
  proceed on B. [I] from [V] disassembly. Do not use `0x20` for anything.

---

## 3. The stock loader's next-boot treatment (read-only disassembly)

### 3.1 Mapping the dumped loader image

- `parent:dumps/router-20261108/uboota.bin` is byte-identical to
  `parent:dumps/router-20261108/full/mtd1-uboota.bin`
  (md5 `e7396d3d4fa41b8bdd4cb5ac17ff226f`); `ubootb`/mtd2 is a different dump
  (md5 `bad76c5789f8b416a256fa3b8b42faf6`; identical up to file offset `0xA0000`, where
  `uboota` is `0xFF` fill and `ubootb` carries a 2,042-byte binary blob - content
  unidentified, **not** the env and not the selector; [U] what it is). [V]
- The partition begins with a 0x100-byte `hboot1tag` header; the U-Boot code's ARM vector
  table is at file offset 0x100 and its literal pool holds `0x80040000`-based handler
  pointers. The correct raw-disassembly mapping is therefore
  `file offset f -> VA 0x8003FF00 + f` (i.e. `--adjust-vma=0x8003ff00`), which makes the
  vectors, handlers and the code in 3.2/3.3 decode cleanly. [V] appendix A command.
- This is the same image the ESBC stage prints as `flash read uboot code len [6910c]`
  and loads before `enter uboot`. [V] any capture's pre-U-Boot section (e.g.
  `20261116T052326Z-recovery-boot.log` lines 108-124).

### 3.2 Function A - the bootflag/bootreg decision (VA `0x8004E188`)

Strings it uses (file offsets in `uboota.bin`): `hsan,sysctrl` (0x46EF4),
`hsan,sysenv` (0x46F01), `no sysenv_node exists, later change bootflag only` (0x46F0D),
`bootreg-offset` (0x46F3F), `bootflag` (0x46F4E), `bootreg init, set $x` (0x46F57),
`boot from kernela failed, change to kernelb` (0x46F6D),
`boot from kernelb failed, change to kernela` (0x46F9A). [V] strings table; appendix A.

Reconstructed behaviour [V] (disassembly `0x8004E188`-`0x8004E32C`; the sequence below is
a faithful summary, not a decompiled listing):

1. Find the `hsan,sysctrl` and `hsan,sysenv` DT nodes. If `hsan,sysenv` is missing, print
   `no sysenv_node exists, later change bootflag only` and skip the cell checks; the
   matching no-sysenv branch in `bootfip` (3.3) then **toggles `bootflag` every boot**
   (the no-sysenv fallback); this board has the node, so that path is not taken here. [V]
2. Read `bootreg-offset`, read the 32-bit cell at that offset, read env `bootflag`, and
   print `bootflag <value> bootreg <low-byte-hex>`. [V]
3. If the low byte is **0**: initialise it from the env - `0x10` if `bootflag=a`, else
   `0x11` - write it back, read it back, print `bootreg init, set $x`. [V]
4. Else if `bootflag == a` **and** low byte `== 0x11`: print
   `boot from kernela failed, change to kernelb`, `setenv bootflag b`, `saveenv`. [V]
5. Else if `bootflag == b` **and** low byte `== 0x10`: print
   `boot from kernelb failed, change to kernela`, `setenv bootflag a`, `saveenv`. [V]
6. Continue to the normal boot path (`multiupg detect`, bootdelay, `bootcmd`). The only
   comparison values in this function are `0x00`, `0x10`, `0x11`; no bit-5 test exists.
   [V]

### 3.3 Function B - `bootfip`'s cell write (VA around `0x80073BB8`-`0x80073DB0`)

`bootcmd` runs `bootfip` after reading the partition. The function prints, in this
console order, `bootreg offset: $x` and `boot regval: $x` (strings at file 0x4A241 /
0x4A255, i.e. VA `0x8008A141` / `0x8008A155`), then: [V]

    r3 = *(sysctrl + bootreg_offset)      ; the whole 32-bit cell
    if (env bootflag != 0) {
            print "bootreg offset: $x", offset
            print "boot regval: $x", cell
            r3 = (r3 & ~0xff) | (bootflag=="a" ? 0x11 : 0x10)
            *(sysctrl + bootreg_offset) = r3
    }

The write stores the **opposite pair of the env `bootflag`**:
`bootflag=a` -> cell `0x11`; `bootflag=b` -> cell `0x10`. [V] disassembly; appendix A.
The write is unconditional on the normal `bootcmd` path whenever `bootflag` is set; the
only branch that skips it is the missing-`hsan,sysenv` fallback (`no sysenv_node exists,
change bootflag only`, file 0x4A215). [V]

### 3.4 The resulting state machine (next boot after a manual cell write)

Let `pair(env)` be `kernela/rootfsa` for `a`, `kernelb/rootfsb` for `b`. [V]

| current state | next boot | result |
| --- | --- | --- |
| cell `0` (true POR) | env read; cell := `0x10`/`0x11` from env; boots `pair(env)` | `bootreg init, set 10/11` |
| env=a, cell=0x10 | no mismatch; bootfip sets cell `0x11`; boots A | normal A boot |
| env=b, cell=0x11 | no mismatch; bootfip sets cell `0x10`; boots B | normal B boot |
| env=a, cell=0x11 | mismatch -> env := b, saveenv; bootfip sets cell `0x10`; boots B | failover to B, then `boot from kernela failed...` printed **before** the env save |
| env=b, cell=0x10 | mismatch -> env := a, saveenv; bootfip sets cell `0x11`; boots A | failover to A |
| env=b, cell=`0x21` | no mismatch (cell is neither 0x11 nor 0x10); bootfip sets cell `0x10`; boots B | **0x21 boots B; the 0x20 bit is discarded** |
| env=a, cell=`0x21` | no mismatch; bootfip sets cell `0x11`; boots A | **0x21 alone does not boot B** |

The consequences [V]/[I]:

- `0x21` is **consumed in the same boot**. [V]
- If the boot succeeds and reachable userspace re-records the running pair, the cell ends
  at `0x11` (B). [V] 4.3.
- If the boot fails before that, the cell stays at `0x10`; the next boot sees the
  mismatch and falls back to A. [V] 4.4.
- A cell write with env `a` can never boot B by itself; the env must be `b` or the write
  must be `0x11` (which triggers the mismatch) - see 7.2. [V]

### 3.5 What it prints (the complete relevant vocabulary)

| stage | exact print | evidence |
| --- | --- | --- |
| U-Boot init, every boot | `bootflag <a/b> bootreg <hex low byte>` | [V] captures |
| U-Boot init, true POR with cell 0 | `bootreg init, set 10` / `bootreg init, set 11` | [V] captures |
| U-Boot init, mismatch/failover | `boot from kernela failed, change to kernelb` / `boot from kernelb failed, change to kernela` | [V] captures |
| bootfip, every normal boot | `bootreg offset: c08` then `boot regval: <hex>` | [V] captures |
| vendor userspace reads the sysfs shadow | `show boot_reg: <hex>` (kernel `printk` from `hi_ksysenv.ko`) | [V] captures |
| vendor userspace writes it | `store boot_reg: <hex>` | [V] captures |
| absent sysenv node (not this board) | `no sysenv_node exists, ...` | [V] string only |
| boolean, bit 5, "manual", "recovery handover" | **no such print exists** in the loader or the kernel driver | [V] strings of both binaries |

---

## 4. Measured evidence (existing read-only captures)

All files below are under `parent:build/uart-logs/` unless stated. The captures are
read-only UART records; they are cited here, not re-run.

### 4.1 True power-on: cell 0, env decides, `init` records the pair

`20261107T191003Z-poweron-uboot.log` [V]:

    173: Hard Power-on reset for internal POR circuit timing
    176: bootflag a bootreg 0
    177: bootreg init, set 10
    180: bootreg offset: c08
    181: boot regval: 10
    344: [    1.285683] show boot_reg: 11
    1005:[   16.240012] store boot_reg: 10

Reading: POR cleared the cell; U-Boot recorded `0x10` from env `a`; `bootfip` then wrote
the opposite-pair marker `0x11` (visible at 1.28 s); the vendor init re-recorded the
running pair `0x10` at 16.24 s. A capture with env `b` shows the symmetric sequence:
`20261117T124213Z-uart-recovA.log` `bootreg init, set 11` (line 10728), `boot regval: 11`
(10888), `show boot_reg: 10` at 1.284 s (11051), `store boot_reg: 11` at 30.4 s (12869).
[V]

**Every** true-POR capture in the set prints `bootreg init, set` only after a banner with
`bootreg 0`: 43 POR init occurrences across 23 captures ([V] counted with
`grep -c 'bootreg init, set' parent:build/uart-logs/*.log`; each init is the cell==0
branch of 3.2). This is the recovery-relevant fact: a power cycle clears a `0x21`/`0x11` cell.

### 4.2 A `0x21` fire boot

`20261116T052813Z-fire7-3df.log` lines 1-2 and 232-241 [V]:

    [  231.542012] store boot_reg: 21      <- the fire recipe wrote 0x21 on the previous boot
    [  231.573582] show boot_reg: 21
    ...
    bootflag b bootreg 21
    multiupg detect
    bootdelay is 3000
    input <abort key> to stop autoboot in  1s
    Reading 8650752 byte(s) (4224 page(s)) at offset 0x00000000
    bootreg offset: c08
    boot regval: 21
       Image Name:   Linux-5.10.201

The same banner appears in every `0x21` fire capture (section 10). [V]

### 4.3 Successful handover -> cell `0x11` on later boots

`20261117T041013Z-uart-3dq3.log` [V]:

    233: bootflag b bootreg 21        <- the fire boot
    ...
    1907:bootflag b bootreg 11        <- a later boot after the fire boot reached userspace

and `20261117T051800Z-uart-3dq4.log` lines 236 / 1877 (21 then repeated 11). [V]
Interpretation: after a successful B boot the cell is `0x11`; the exact writer is either
the vendor init's `boot_done` (stock userspace, 5.1) or, in the hybrid bench images, the
private 5.10 lab kernel's early fallback flip (5.5) - both write the running pair, so both
explain the observation; the cell value is the same. [V]/[I]

### 4.4 Failed handover -> cell `0x10`, env saved, failover to A

`20261116T075623Z-3dh-fire.log` [V]:

    223: bootflag b bootreg 21        <- the fire boot; the image never produced console
    ...
    282: bootflag b bootreg 10        <- next boot
    283: boot from kernelb failed, change to kernela
    284: Saving Environment to NAND... Erasing redundant NAND...
    285: Erasing at 0x280000 --  50% complete.
    291: Reading 8650752 byte(s) ... (this read is kernela)
    293: boot regval: 10

Also `20261117T115050Z-uart-3dq13.log` lines 304-315 (same sequence), and the repeated A/B
cycles of the 3dh capture (lines 1468-1479, 1532-1543, ...) show the same rule from the B
side: `bootflag a bootreg 11` -> `boot from kernela failed, change to kernelb` -> env saved
-> boot B. [V]

### 4.5 Persistence

| reset kind | cell after | evidence |
| --- | --- | --- |
| true power-on | `0x00` | [V] 43/43 POR init sequences |
| warm reset while userspace recorded success | running pair (`0x10`/`0x11`) | [V] 4.1, 4.3 |
| warm reset after a failed attempt, before userspace recorded success | opposite-pair marker (`0x10` for a B attempt) | [V] 4.4 |
| `0x21` written, then any boot | never `0x21` again | [V] 3.3/3.4 + 4.2-4.4 |

**`0x21` does not persist.** It is normalised in the same boot that consumes it, and a
power cycle erases it anyway. [V]

### 4.6 The userspace "boot done" write that makes it stable

- The vendor init script `/etc/init.d/hsan_appm` (START=16) ends its `appm_boot()` with
  `echo > /sys/devices/platform/sysenv/boot_done` [V]
  `parent:rootfs-2.5.24/etc/init.d/hsan_appm` line 103, and the 2.4.15 file line 99. [V]
- The kernel side of that write toggles the cell to the **running pair** (see 5.2). [V]
- Kernel console proof, stock A boot: `show boot_reg: 11` at 1.28 s (the bootfip marker)
  then `store boot_reg: 10` at 16.24 s (the boot-done write) [V] 4.1 - and the symmetric
  B boot (`show 10` -> `store 11`) in `20261117T124213Z-uart-recovA.log` lines 11051 /
  12869. [V]
- This write is what makes a healthy stock system stable across reboots; without it the
  loader's opposite-pair marker would flip the pair on the following boot. [V]/[I] 3.4.

---

## 5. Who writes the cell (complete list found locally)

### 5.1 Vendor userspace (stock firmware)

| writer | value | when |
| --- | --- | --- |
| `libhi_upgrade.so` (`hi_upgrade_image_write_file` completion) | `0x10` or `0x11` (`0x10|flag`) | after a vendor image write, before reboot |
| `rootfs-2.5.24/lib/upgrade/platform.sh` `platform_set_boot_reg` | `0x10` or `0x11` | after writing kernel+rootfs of the inactive pair |
| `/etc/init.d/hsan_appm` -> `/sys/.../boot_done` | running pair (`0x10`/`0x11`) | once per successful boot, ~16-30 s |

[V] 2.2/4.6. **No vendor writer of `0x21` exists in the local rootfs trees.** [V]
(`grep -rla 'boot_reg'` over both trees finds only `hi_ksysenv.ko`, `libhi_upgrade.so`,
`lib/hsan.sh`, `lib/upgrade/platform.sh`; `boot_done` only `hsan_appm`.)

### 5.2 `hi_ksysenv.ko` (the sysfs shim)

Disassembly of the identical module in both rootfs trees [V]
(`parent:rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko`, md5
`e325b525b9f57978ea9801d7af6ea75b`; symbol table contains `boot_reg_show`,
`boot_reg_store`, `boot_done_store`):

- `boot_reg_show`: `regmap_read(bootreg-offset)`; prints only the low byte;
  `printk("show boot_reg: $x")`. [V]
- `boot_reg_store`: `simple_strtol()`; `regmap_read`; `val = (val & ~0xff) | (input &
  0xff)`; `regmap_write`; `printk("store boot_reg: $lx")`. **It accepts any low byte**,
  including `0x21`; it does not validate or normalise. [V]
- `boot_done_store`: reads the cell; **if low byte == `0x10` writes `0x11`, else writes
  `0x10`** (i.e. it records the running pair when the bootfip marker is the opposite
  pair); `printk("store boot_reg: $lx")` (same format string). [V] The disassembly at
  module offsets `0x320`-`0x3E8`; appendix A.

### 5.3 The loader

- Power-on cell 0 -> `bootreg init, set 10/11` from env. [V] 3.2.
- Every `bootfip` -> low-byte RMW to the opposite pair of the env. [V] 3.3.
- On mismatch -> `setenv`+`saveenv` of `bootflag` (env write, not a cell write). [V] 3.2.

### 5.4 The project's fire recipe (the origin of `0x21`)

`FLASH-PLAN.md` lines 193-202 ("Slot switch recipe") and
`parent:tools/fire-device.sh` lines 15-35, 53-56: erase/write the target kernel slot,
write an env block with `bootflag=b` to mtd3/mtd4, `echo 0x21 >
/sys/devices/platform/sysenv/boot_reg`, then `reboot`. The recipe also writes
`devmem 0x10100C94 32 0xDEAD0000`; the meaning of that word is outside this document
[U] (it is not the selector cell and no loader code read by this reconstruction tests it).
[V] file contents; the `0x21` write is visible in every fire capture's first two lines.
[V]

### 5.5 The private 5.10 lab kernel (bench only, not the upstream target)

The bench-reversal kernel tree writes the same cell in two places, which matters when
reading old captures [V] `parent:build/linux-kbuild/linux-5.10.201/arch/arm/mach-luofu/luofu.c`:

- `luofu_restart()` writes `0x10` before any software reset (added 2026-11-17) - "a reset
  that leaves the boot_reg cell at B makes U-Boot boot the test slice again, which is how
  failed fires became loops". Lines 154-172.
- `luofu_early_crumb()` (early_initcall) reads the cell and, if the low byte is `0x10` or
  `0x11`, writes `val ^ 1` - the self-recovering "fallback arm". Lines 285-310; ledger
  phase 73 (`parent:mem-entries.md` lines 15301-15325).

Consequences for interpretation: a hybrid-image fire that panics goes through
`luofu_restart` and therefore leaves `0x10`; one that hangs and is reset by the SoC
watchdog leaves whatever the early flip wrote (`0x11`). Both are visible in the captures
(3dq13's post-`#257` boot shows `10` after a panic; 3dq4's repeating `#245` boots show
`11`). [V] This is **bench instrumentation, not vendor behaviour**, and the upstream
6.18 target does not contain either write. [V] patch 017 probe performs no register
access.

---

## 6. The vendor recovery / TFTP flow

- **Entry is physical only.** Power off, hold RESET, power on holding; at a true POR the
  loader prints `RESET button is pressed for: 0/1/2 second(s)`, `Resetting...`, then after
  the boot counter `recovery button press`, then `tftpboot recovery.bin` from
  `192.168.1.88` (filename `recovery.bin`, load address `0x83200000`). [V]
  `parent:build/uart-logs/20261116T052326Z-recovery-boot.log` lines 138-151; `BENCH-FACTS.md`
  sec.4.
- **The cell is 0 at that point**, so the recovery boot starts from the same clean state
  as any POR: the init prints `bootflag <env> bootreg 0` and `bootreg init, set 10/11`
  before entering the recovery path. [V] 4.1, and the recovery captures show line 47
  `bootreg init, set 11` for env b (`20261116T055749Z-plt-recovery.log` lines 46-47).
  A failed `0x21` handover therefore **cannot block physical recovery**; a power cycle
  erases it. [V]
- **The loader's recovery code** (strings `tftpboot recovery.bin`, `firmware rsa verify
  ok`, `firmware rsa verify error`, `hi-upgrade: fileaddr = ...`, `Flashing kernel...`,
  `Flashing rootfs...`, `Erasing rootfs_data...`, `image recovery error`; function around
  VA `0x8004E520`-`0x8004E818`) flashes a kernel/rootfs pair via `mtd erase`/`mtd write`
  and resets. It contains **no store to the selector cell**: the only two cell writes in
  the loader are the two decoded in 3.2/3.3. [V] disassembly; [U] which pair the
  recovery image flashes and what its userspace does to the env/cell - that is inside the
  closed recovery image and is not observable from the local evidence.
- **Observed outcome after recovery:** the box comes back on stock firmware; the recovery
  captures show the next normal boot with `bootflag a bootreg 10` (A pair)
  (`20261116T052326Z-recovery-boot.log` lines 296-302; `20261117T124213Z-uart-recovA.log`).
  [V] The exact mechanism that leaves env/cell on A is not resolved. [U]
- **The recovery image is a vendor sysupgrade container** (the `hboot1tag` magic is what
  the loader's recovery path expects; `tftp/recovery.bin` md5
  `421114c8ea999c925b7aa79cc548af81`, `BENCH-FACTS.md` sec.4). Our own target's
  `platform_check_image` deliberately refuses vendor recovery images. [V] platform.sh
  header.

---

## 7. Implications

### 7.1 Selector driver 017 (`drivers/soc/hisilicon/luofu-selector.c`)

The shipped patch (`../target-skeleton/target/linux/hisilicon/patches-6.18/017-soc-luofu-boot-selector.patch`,
sha256 `c639642a...`) is directionally right - syscon regmap, no register access at
probe, verified store, manual/uninitialised values refused by the installer - but four
things must change before it is treated as evidence-backed:

1. **Comment/ABI claim about `0x21` is wrong.** [X] Lines 49-52 say `0x21` is "written by
   the stock sysenv userspace interface when a slot is flashed by hand". The stock
   interface writes whatever value userspace gives it; the vendor userspace never gives
   it `0x21` (5.1); the project's fire recipe does (5.4). Rewrite as: "`0x21` is the
   project's measured *bench* handover value; the stock loader treats it as the B pair
   and discards the `0x20` bit on the next boot; it is not produced by vendor userspace."
   The ABI doc inside the same patch (lines 359-362) carries the same sentence and must be
   corrected the same way.
2. **The store writes the full dword, the vendor writes only the low byte.** [V] patch
   line 187 `regmap_write(sel->sysctrl, sel->offset, target)` writes `0x00000010` /
   `0x00000011`; the vendor driver RMWs the low byte and preserves the upper 24 bits
   (5.2); the loader's own write is also a low-byte RMW (3.3). Every observed value has
   zero upper bytes, so no damage is known to have occurred, but the semantics are not
   the vendor's. **Change the store to a low-byte RMW** (or refuse when the upper 24 bits
   are nonzero) before the interface is called a faithful model. [I]/[U] upper bits
   meaning: unmeasured.
3. **`current_pair` cannot answer "which pair will boot".** [V] Because `bootcmd` reads
   the env, the cell alone is a *record*, not the selector; a stale env can disagree with
   the cell (the loader resolves that by flipping the env, 3.2). The installer's
   two-witness check (selector + booted rootfs) is the right mitigation; keep it. Do not
   expose a "next boot pair" semantic in the driver.
4. **Do not add a `0x21`/manual store path.** [V] the `manual-b` decode is useful
   diagnostics; the value must stay unreachable (patch lines 162-173 already do this).
   With 2.3 it would not buy any behaviour the loader honours.

Status of the driver itself: probe/store/decode logic is host-validated only; nothing
about it is hardware-validated. [U]

### 7.2 Installer (`platform_do_upgrade`)

The delivered installer
(`../target-skeleton/target/linux/hisilicon/base-files/lib/upgrade/platform.sh`,
sha256 `612a65c5...`) writes the inactive pair, then the selector (`next_pair`), and never
writes the env. Re-read against 3.2-3.4:

- **The handover mechanism is the mismatch path, and it is correct.** [V]/[I] With the
  running pair `active` and the selector moved to `other` (`0x11` when running A /
  `0x10` when running B), the next boot sees env=`active` + cell=`other` -> mismatch ->
  env re-stamped to `other`, saveenv, boot the written pair. The installer's
  "write selector last" ordering is right: before the store the box still boots `active`;
  after the store it boots `other` once, and if `other` fails before userspace records
  success the cell stays at the opposite marker and the next boot fails back to `active`.
  [I] (mechanism fully disassembled [V]; the end-to-end install has not run [U]).
- **The two-witness check is necessary but has a dependency.** [V] `_luofu_active_pair`
  requires the selector's pair to equal the booted rootfs pair. On a *stock* boot that
  holds (boot-done at 16 s re-records the running pair). On **our own target** it holds
  only if our userspace writes the running pair on every boot. **Without such a step the
  installer refuses on our own system** (selector would read the opposite-pair marker)
  and, worse, the first reboot after any normal boot would silently change pairs. [V]
  from 3.3/3.4 + 4.6.
- **This is the missing piece: a boot-time "record the running pair" service.**
  - Requirement: on every successful boot of our target, once the system is considered
    up (vendor does it at ~16-30 s, after `hsan_appm`), write the running pair
    (`0x11` for a B boot, `0x10` for an A boot) through `next_pair`.
  - Same semantics as the vendor `boot_done` toggle (5.2), but expressed as "write the
    pair that `/sys/class/ubi/ubi0/mtd_num` + `/proc/mtd` says is running" so it is
    idempotent, not a toggle.
  - It must NOT run so early that a failing boot loses the loader's fallback window; the
    vendor's own window is ~16 s, which is the only measured value ([V] 4.6).
  - The machine-restart driver (patch 010) write-sames the cell it reads; that is only
    correct *after* the boot-done writer has run, otherwise it preserves the
    opposite-pair marker and causes the next boot to flip. [V] 3.3/3.4, patch 010 code.
  - [V] this dependency is not stated in the AB-install summary's TODO list; add it.
- **Do not start writing the env from the installer.** [V] no usable env writer exists on
  the target image, and the loader-owns-the-env model is unnecessary because the mismatch
  path performs the stamp. Keep "never writes the env copies" (platform.sh header lines
  48-51) and close it with the attended A/B validation.
- **Refusals are correct.** `0x21` -> `manual-b` -> refuse; `0x00` -> refuse;
  selector/rootfs disagreement -> refuse. [V] platform.sh lines 426-449, 436. Keep them;
  the normalisation procedure for a `manual-b` box is [U] and must not be guessed (see 8).

### 7.3 Recovery procedure

- **A failed or unwanted `0x21` handover cannot strand the box**: the recovery path is a
  true power-on, and every true POR clears the cell to 0 before the loader acts (4.1),
  then initialises it from the env. [V] The recovery entry (RESET counter + TFTP) is
  reached before `bootcmd`, so a bad pair on either slot cannot prevent it. [V] 6.
- **The recovery door remains the only way out if both pairs are bad or the loader
  cannot boot.** [V] `BENCH-FACTS.md` sec.4. The cell is not part of that path.
- **After recovery, verify the pair state from the console, not from a remembered cell
  value**: read `bootflag`/`bootreg` lines at the next boot (3.5) and, in userspace,
  `cat /sys/devices/platform/sysenv/boot_reg` + `/sys/class/ubi/ubi0/mtd_num` +
  `fw_printenv bootflag`. [I]
- **Do not "repair" a manual/recovery cell with a blind write.** [V]/[U] `0x21` is not
  produced by the vendor; if a box is seen with `manual-b` in-boot it was written after
  the bootfip pass by userspace and the next boot's behaviour is exactly the ordinary
  mismatch rule - but which value to write to reach a desired state depends on the env
  state, which the kernel cannot read. This is exactly the class of action that needs a
  written, attended experiment, not an installer fallback.

### 7.4 Bench fire path (`0x21`)

- The `0x21` + `bootflag=b` recipe remains **valid as the project's measured bench
  handover** (dozens of captures; the value is only necessary because it is what the
  recipe writes - `0x11` + `bootflag=b` is the same handover and matches the recorded-pair
  encoding). [V]/[I]
- It is **not the vendor path** and must not be copied into the target installer (7.2).
  [V]
- The `devmem 0x10100C94 32 0xDEAD0000` step in `tools/fire-device.sh` is unexplained by
  the loader code read here. [U] Do not treat it as a selector mechanism without its own
  evidence.
- Never rely on `fw_setenv` on the running image. [V] `BENCH-FACTS.md` sec.5.

### 7.5 Risk register (0x21 / selector / install / recovery)

| # | risk | evidence | severity |
| --- | --- | --- | --- |
| R1 | Installer handover is not stable without a boot-time running-pair writer: first boot of the new pair succeeds, the next reboot silently fails back to the old pair | [V] 3.3/3.4 + 4.6; [U] until an init step exists | high for the target |
| R2 | Driver claims about `0x21` provenance are unsupported and would mislead a future reader/auditor | [X] 7.1(1) | medium (documentation correctness) |
| R3 | Driver full-dword store vs vendor/loader low-byte RMW: upper 24 bits and exact semantics differ | [V] 7.1(2); [U] whether upper bits are used | medium |
| R4 | Two-witness refusal on a target whose boot-done writer is missing - install path cannot run at all | [V] 7.2 | high for bring-up |
| R5 | `0x21`/manual-b box + automated installer: nothing to do safely without an attended normalisation procedure | [U] 7.3 | low (fails closed today) |
| R6 | Env corruption by a hand-written env block (the older recipe) - the loader saves env on failover; primary/redundant copies differ | [V] 1.2; [U] exact flag semantics | low if env is never hand-written |
| R7 | Recovery assumptions: physical only; a box that cannot reach the RESET-counter path (or both pairs bad) still needs hands | [V] BENCH-FACTS sec.4 | inherent |
| R8 | `0x20`/"manual A" invented by someone to "go back to A" - bit is ignored and env decides | [V] 2.3 | low (education) |

---

## 8. Exact attended read-only experiments that would close the unknowns

These are **read-only**: no cell write, no env write, no mtd write, no flash, no TFTP
write, no UART transmit. They need a human at the bench only for the physical power
cycle (experiment 2). Any experiment that writes the cell or env is deliberately **not**
listed here; those require their own written plan, a gate/preflight and the project's
fire rules, and are never blind writes.

1. **Current boot state, stock image (1 ssh probe + 1 serial capture).**
   Read `/sys/devices/platform/sysenv/boot_reg`, `/sys/class/ubi/ubi0/mtd_num`,
   `/proc/mtd`, `fw_printenv bootflag`, and `dmesg | grep -E 'show boot_reg|store boot_reg'`
   (the driver prints both, 3.5). Closes: that the in-boot cell equals the running pair on
   the current stock image, and the 16-30 s boot-done timestamp on this specific firmware.
   Evidence file: a fresh capture under `parent:build/uart-logs/` (the ssh read is
   host-only).
2. **One attended true power-on, UART capture only.**
   Human powers off, then on (no RESET hold, no fire). The read-only capture should show:
   `Hard Power-on reset ...`; `bootflag <env> bootreg 0`; `bootreg init, set 10/11`;
   `bootreg offset: c08`; `boot regval: 10/11`; in userspace `show boot_reg: <opposite>`
   at ~1.3 s and `store boot_reg: <running>` at ~16-30 s. Closes: POR cell clearing and
   the full normalisation chain on today's firmware, with no write by us.
3. **Read the raw cell and the sysfs shadow side by side (devmem read only).**
   `devmem 0x10100C08 32` and `cat /sys/devices/platform/sysenv/boot_reg` in the same
   boot, at >60 s uptime. Closes: whether the upper 24 bits are ever nonzero in the
   running system (R3) and that the sysfs shadow is the same cell.
4. **`ls -l /sys/devices/platform/sysenv/`** on a stock boot. Closes: that `boot_done`,
   `boot_reg`, `chip_type`, `rf_slave_type` are all present in the running firmware (the
   ABI our target should mirror).
5. **Read the current env copies from the live system** (read-only `dd` of mtd3/mtd4 to
   `/tmp`, hash + string compare, no writes): closes whether the two copies currently
   agree on `bootflag` and what the flags bytes are - useful before any future env
   reasoning. (Host-side, existing dumps already satisfy this; this is the live check.)
6. **Re-derive the loader's two decision functions from the dump** with the command in
   appendix A and compare the instruction sequence against section 3 - a future auditor
   can repeat this with no device at all. Closes: independent audit of this document's
   central mechanism.
7. **Read the stock recovery image's own metadata** (host-side `binwalk`/`strings` on
   `parent:tftp/recovery.bin`, read-only): closes which vendor release the recovery image
   carries and whether its loader wrapper is the same ESBC/U-Boot pair. It does **not**
   close what the recovery image writes at runtime (that needs the closed path and is
   [U]).

What these cannot close (and why): the exact runtime behaviour of the closed vendor
recovery image; the meaning of the `ubootb` spare-area blob; upper-bit semantics of the
cell if they are never nonzero; and any statement about **our** driver/installer on
hardware - that requires the attended A/B validation, which is a write experiment and is
out of scope here.

---

## 9. Unknowns summary

| # | unknown | today's best reading | closing experiment |
| --- | --- | --- | --- |
| U1 | Does `0x21` ever occur on a stock, vendor-driven box? | No local evidence; vendor writes only `0x10`/`0x11`. `0x21` is our recipe. | docs audit (2.2) + experiment 2 (watch every stock boot's cell in console) |
| U2 | Meaning of bit 5 (`0x20`) | ignored by the loader; overwritten before Linux | none needed for the target; do not use |
| U3 | Upper 24 bits of the cell | zero in all observations; vendor writes low byte only | experiment 3 |
| U4 | Exact flags semantics of the two env copies / which is primary | loader saves "redundant NAND" and "NAND" alternately; both dumps were `bootflag=a` | experiments 5, 6 |
| U5 | What the recovery image writes at runtime | post-recovery state observed on A; mechanism not observable | none locally read-only; [U] |
| U6 | End-to-end install: does the loader re-stamp env on a real install? | mechanism decoded [V]; behaviour unmeasured | attended A/B validation (write plan, not this doc) |
| U7 | Boot-done requirement for our target boot stability | [V] mechanism implies it is required | build the init step, then the A/B validation |
| U8 | `devmem 0x10100C94 = 0xDEAD0000` in the fire recipe | unexplained; not the selector | ledger/spec audit; not needed for the target |
| U9 | `ubootb` 0xA0000-blob content | not env, not selector; unknown | host-side RE if it ever matters |

---

## 10. Evidence index and regeneration

All parent paths are reported with their checksums where they are files. Regeneration
commands are host-only and read-only.

| artifact | checksum / location | what it proves |
| --- | --- | --- |
| U-Boot slot A dump | `parent:dumps/router-20261108/uboota.bin` = `full/mtd1-uboota.bin`, md5 `e7396d3d4fa41b8bdd4cb5ac17ff226f` | loader code for 3.2/3.3 |
| U-Boot slot B dump | `parent:dumps/router-20261108/full/mtd2-ubootb.bin`, md5 `bad76c5789f8b416a256fa3b8b42faf6` | same code + one unexplained spare-area blob |
| env copies | `parent:dumps/router-20261108/full/mtd3-enva.bin`, `mtd4-envb.bin` | env format (`bootcmd`, `bootflag=a`) |
| sysenv driver | `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko` (and 2.5.24 copy), md5 `e325b525b9f57978ea9801d7af6ea75b` | 5.2 |
| vendor upgrade lib | `parent:rootfs-2.4.15/usr/lib/libhi_upgrade.so` (and 2.5.24 copy), md5 `a5f018ea2a8b21e8aa600370592516bc` | 2.2 (writes `0x10`/`0x11`) |
| vendor platform.sh (2.5.24) | `parent:rootfs-2.5.24/lib/upgrade/platform.sh` | 2.2 |
| vendor init | `parent:rootfs-2.5.24/etc/init.d/hsan_appm` line 103 | 4.6 |
| selector driver | `../target-skeleton/target/linux/hisilicon/patches-6.18/017-soc-luofu-boot-selector.patch`, sha256 `c639642a7648bf4f4c16390560f81afb496ff7afd16e7a3b0b08e6d9bfef524b` | 7.1 |
| installer | `../target-skeleton/target/linux/hisilicon/base-files/lib/upgrade/platform.sh`, sha256 `612a65c506aed6e52aae0f7b936bbc89a1ebae392227450a0c99eaa828d70900` | 7.2 |
| AB-install record | `../target-skeleton/docs/port-notes/abinstall/SUMMARY.md` | prior claims corrected here |
| bench recipe | `FLASH-PLAN.md` lines 193-202; `parent:tools/fire-device.sh`; `parent:tools/fire.sh` | 2.2/5.4 |
| lab kernel writers | `parent:build/linux-kbuild/linux-5.10.201/arch/arm/mach-luofu/luofu.c` lines 154-172, 285-310 | 5.5 |
| console capture, POR | `parent:build/uart-logs/20261107T191003Z-poweron-uboot.log` lines 173-181, 344, 1005 | 4.1 |
| console capture, 0x21 fire boot | `parent:build/uart-logs/20261116T052813Z-fire7-3df.log` lines 1-2, 232-241 | 4.2 |
| console capture, success | `parent:build/uart-logs/20261117T041013Z-uart-3dq3.log` lines 233, 1907; `20261117T051800Z-uart-3dq4.log` lines 236, 1877 | 4.3 |
| console capture, failure/failover | `parent:build/uart-logs/20261116T075623Z-3dh-fire.log` lines 223, 282-293, 1468-1479; `20261117T115050Z-uart-3dq13.log` lines 304-315 | 4.4 |
| console capture, B boot `boot_done` | `parent:build/uart-logs/20261117T124213Z-uart-recovA.log` lines 10728, 10888, 11051, 12869 | 4.1/4.6 |
| recovery capture | `parent:build/uart-logs/20261116T052326Z-recovery-boot.log` lines 138-151, 296-302 | 6 |

Additional `0x21` fire captures carrying the same banner and the same normalisation
sequence [V]: `20261116T064641Z-ssh-fire2.log`, `20261116T073113Z-3dg2.log`,
`20261117T010004Z-uart-3dm.log`, `20261117T012608Z-uart-3dn.log`,
`20261117T024055Z-uart-3dp.log`, `20261117T031025Z-uart-3dq.log`,
`20261117T034235Z-uart-recov3.log`, `20261117T041013Z-uart-3dq3.log`,
`20261117T051800Z-uart-3dq4.log`, `20261117T083801Z-uart-recov5.log`,
`20261117T093619Z-uart-3dq7.log`, `20261117T102221Z-uart-3dq9.log`,
`20261117T103046Z-uart-3dq10.log`, `20261117T104319Z-uart-3dq10b.log`,
`20261117T115050Z-uart-3dq13.log`, `20261117T233458Z-uart-v17.log`.

Regeneration commands (host-only, read-only):

    # U-Boot disassembly (mapping: file offset 0x100 -> VA 0x80040000)
    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump \
        -D -b binary -m arm --adjust-vma=0x8003ff00 \
        dumps/router-20261108/uboota.bin > build/uboota.dis
    # strings with offsets (file offsets used above)
    strings -a -t x dumps/router-20261108/uboota.bin | less
    # sysenv module (symbol table + boot_reg/boot_done functions)
    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump -d \
        rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko
    # env block validation (appendix B)
    python3 -c "import struct,zlib; d=open('dumps/router-20261108/full/mtd3-enva.bin','rb').read(); print(hex(struct.unpack_from('<I',d)[0]), hex(d[4]), hex(zlib.crc32(d[5:131072])))"

---

## Appendix A - disassembly excerpts (host-only)

Mapping and tool: section 10. Addresses are VAs under the mapping above.

**A.1 Decision function A (`0x8004E188`-`0x8004E32C`), condensed**

    8004e188  push {r0,r1,r7,lr}
    8004e190  ldr  r1,=0x80086ef4        ; "hsan,sysctrl"
    8004e198  bl   0x80056da4            ; find DT node
    8004e1a0  ldr  r1,=0x80086f01        ; "hsan,sysenv"
    8004e1ac  bl   0x80056da4
    8004e1b4  bge  0x8004e220            ; node exists -> main path
    8004e1b8  ldr  r0,=0x80086f0d        ; "no sysenv_node exists, later change bootflag only"
    ...
    8004e220  ldr  r1,=0x80086f3f        ; "bootreg-offset"
    8004e228  bl   0x80056828            ; of_property_read_u32
    8004e22c  ldr  r0,=0x80086f4e        ; "bootflag"
    8004e230  bl   0x8006418c            ; env_get
    8004e23c  ldr  r4,[r6,r3]            ; r4 = *(sysctrl + bootreg_offset)
    8004e244  uxtb r7,r4                 ; r7 = low byte
    8004e254  bl   0x8006f2b4            ; printf("bootflag %s bootreg %x", ...)
    8004e25c  bne  0x8004e2a0            ; cell != 0
    8004e26c  bic  r4,r4,#0xff
    8004e274  orreq r4,r4,#16            ; bootflag=="a" -> 0x10
    8004e278  orrne r4,r4,#17            ; else           -> 0x11
    8004e284  str  r4,[r6,r3]            ; WRITE cell (init)
    8004e298  bl   0x8006f2b4            ; printf("bootreg init, set %x")
    8004e2a0  ... strcmp(bootflag,"a"); cmp r7,#17; bne ...
    8004e2b4  bl   0x8006f2b4            ; "boot from kernela failed, change to kernelb"
    8004e2c0  bl   0x8004e0f8            ; setenv("bootflag","b")
    8004e2c4  bl   0x80064a8c            ; saveenv
    8004e2cc  ... strcmp(bootflag,"b"); cmp r7,#16; bne ...
    8004e2e8  bl   0x8006f2b4            ; "boot from kernelb failed, change to kernela"
    8004e2ec  ldr  r1,=0x80084b98        ; "a"
    8004e2f0  b    0x8004e2bc            ; setenv + saveenv

**A.2 `bootfip` cell read/write (`0x80073BB8`-`0x80073DB0`), condensed**

    80073bb8  push {r0,r1,r4,...,lr}
    80073bbc  ldr  r1,=0x80086df4        ; "hsan,sysctrl"
    80073bc8  bl   0x80056da4
    80073bcc  bl   0x80056c70            ; node -> sysctrl base (r5)
    80073bd0  ldr  r1,=0x80086e01        ; "hsan,sysenv"
    80073bdc  bl   0x80056da4
    80073be4  bge  0x80073d48            ; node exists -> selector path
    ...                                  ; no-sysenv path: toggles env bootflag each boot
    80073d48  add  r2,sp,#4
    80073d4c  ldr  r1,=0x80086e3f        ; "bootreg-offset" (pool 0x80074154)
    80073d50  bl   0x80056828            ; of_property_read_u32
    80073d58  ldr  r0,=0x8008a141        ; "bootreg offset: %x"
    80073d5c  bl   0x8006f2b4
    80073d64  ldr  r0,=0x80086e4e        ; "bootflag"
    80073d68  ldr  r8,[r5,r6]            ; r8 = *(sysctrl + offset)
    80073d6c  bl   0x8006418c            ; env_get("bootflag")
    80073d74  beq  0x80073c28            ; empty -> skip
    80073d78  mov  r1,r8
    80073d7c  ldr  r0,=0x8008a155        ; "boot regval: %x"
    80073d80  bl   0x8006f2b4
    80073d84  mov  r0,r7                 ; bootflag string
    80073d88  ldr  r1,=0x80084b98        ; "a"
    80073d8c  bl   0x8006de88            ; strcmp
    80073d90  ldr  r3,[r5,r6]
    80073d98  bic  r3,r3,#0xff           ; keep upper 24 bits
    80073d9c  orreq r3,r3,#17            ; bootflag=="a" -> 0x11
    80073da0  orrne r3,r3,#16            ; else           -> 0x10
    80073da4  str  r3,[r5,r6]            ; WRITE cell (opposite pair)

**A.3 `hi_ksysenv.ko` `boot_done_store` (module offset `0x320`-`0x3E8`), condensed**

    320  push {r4,r5,lr}
    36c  bl   regmap_read             ; val = cell
    378  bic  r2,r3,#0xff             ; keep upper bits
    37c  uxtb r3,r3                   ; low byte
    380  cmp  r3,#16                  ; == 0x10 ?
    388  moveq r5,#17                 ; yes -> 0x11
    38c  movne r5,#16                 ; no  -> 0x10
    390  orr  r2,r2,r5
    398  bl   regmap_write            ; WRITE cell (running pair)
    3a8  bl   printk                  ; "store boot_reg: %lx"

---

## Appendix B - env block verification (reproducible)

The two 2026-11-08 dumps satisfy the layout `crc32-le(4) | flags(1) | data(131067)`
inside a 128 KiB block (rest of the 256 KiB partition `0xFF`):

| file | stored LE dword | flags | CRC-32 of `bytes[5:131072]` |
| --- | --- | --- | --- |
| `mtd3-enva.bin` | `0x233a84dd` | `0x4a` | `0x233a84dd` |
| `mtd4-envb.bin` | `0x6bcefd87` | `0x49` | `0x6bcefd87` |

Both data areas carry `bootflag=a` and
`bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}`. The flags byte
differs between the copies (0x4a vs 0x49); its semantics are not established here [U].
The env is **not** where the selector cell lives; this appendix exists to keep the two
stores separate and to close the "cell vs env" confusion.

---

---

## 11. Resolution (2026-11-18, shared task-31) - what was changed from this reconstruction

This section records what the project did with the reconstruction. It is a
host-only follow-up: no device, UART, TFTP, flash or boot was involved, and
nothing below is a hardware claim.

1. **Patch 017 corrected.** [V] The driver's top comment, the Kconfig help, the
   YAML binding and the ABI doc no longer claim a vendor `0x21` provenance:
   they describe `0x21` as the project's measured bench handover value, state
   that the loader selects the pair from the env `bootflag` and uses the cell
   as its opposite-pair/failed-boot record, and record that vendor userspace
   writes only `0x10`/`0x11`. The `next_pair` store is now a **low-byte
   read-modify-write** that preserves the upper 24 bits (as the loader's own
   `bootfip` store and the vendor sysenv driver do) and verifies the whole
   cell read-back; probe still performs no register access. The decode is
   unchanged and deliberately conservative: a cell whose upper 24 bits are
   non-zero reads as `unknown`, because their meaning is [U] (2.3, U3).
   Corrected source patch sha256
   `c991aaa50419f4b6ba0c9a37018a682c0a7a5afc66d43c611c947af25cce0f1d`;
   series file sha256
   `1bac7bdd1aad268ab4cb354485e9e565d0a03322e7f3d2d56dc1a452e0682c7f`.

2. **The missing boot-time writer now exists in the target image.** [V] (host
   only) `target-skeleton/target/linux/hisilicon/base-files/etc/init.d/
   luofu_record_pair` records the running pair after a successful boot:
   START=96 (after `/etc/init.d/done`), derives the booted rootfs pair from
   `/sys/class/ubi/ubi0/mtd_num` plus its `/proc/mtd` label (with an
   optional `/proc/cmdline` `ubi.mtd=` cross-check), waits bounded for the
   luofu-selector interface, and writes the running pair to `next_pair` only
   when the recorded value is one of the two established pairs and differs
   (idempotent). It never writes `manual-b`/`uninitialised`/`unknown`, never
   touches the env blocks and never blocks the boot indefinitely; the image's
   `prepare_rootfs` enables it automatically (`S96luofu_record_pair`). This
   is the equivalent of the vendor `boot_done` (4.6) and it is the step that
   makes R1/R4 go away in the design - its hardware behaviour remains [U].

3. **Installer contract corrected.** [V] `base-files/lib/upgrade/platform.sh`
   now documents the corrected contract: the two-witness check (selector record
   vs booted rootfs slot) is valid **because** the recorder runs on every boot;
   the handover is the loader's env/record mismatch path (the env is never
   written); the refusals are unchanged (`0x21`/`0x00`/unknown/missing/mismatch).
   A warning (not a refusal) is emitted when the running system predates the
   recorder.

4. **Tests extended and re-run.** [V] `tools/test-platform-abinstall.sh` now
   also runs the real recorder against a mocked sysfs/proc fixture and covers
   the corrected store/record semantics: 94/94 fixture assertions and 100/100
   with the real built container (2026-11-18).

5. **No `0x20`/"manual A" path was added** (2.3/7.1(4)); the handover still only
   accepts the two recorded-pair values (7.1(3), 7.2).

Risk-register effect (7.5): R2 fixed [V]; R3 mitigated by the low-byte RMW
store - the upper 24 bits are preserved but their hardware meaning is still
[U]; R1 and R4 have a designed and host-tested fix (the recorder) whose
hardware behaviour is [U]; R5-R8 unchanged.

Still open, and unchanged by this work: the attended checks in section 8
(in-boot cell vs running pair - now also: confirm the recorder leaves the cell
equal to the booted rootfs pair - the POR clearing chain, and a raw read of the
cell with non-zero upper bits to close U3), the env-block re-stamp, the first
boot of a written pair and the full A/B install. Host-side validation evidence
for the corrected patch/recorder/installer is in
`target-skeleton/docs/incoming/selectorfix/SUMMARY.md`.

*End of document. Host-only; no device was touched for this reconstruction. Any future
write experiment on the selector or the env requires its own written plan, a gate, and a
human at the bench - never a blind write.*
