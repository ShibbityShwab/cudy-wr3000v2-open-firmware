# S1 identity and the deterministic condition: who writes handshake +8=2, and the CPU0->CPU1 TCM handoff gate

**Lane:** wifi-s1-cpu1 (shared task 'task-85'), **host-only**, 2026-11-23. The bench router
was never contacted, no image was built, no device was opened, and no vendor
byte/binary/firmware content is committed here - only derived addresses, constants, structure
layouts, disassembly facts and a read/write bench plan.

**Question.** The CA 0x1BF000 ROM<->image page carries the inter-core handshake
(+8 = status / +C = magic 0xABCD). Entry A writes {0, 0xABCD} and rings the inter-core wake
CA 0x40161F00 = 0x20000 (target core 1), then polls +8 == 2 forever. **Who writes 2, what is
S1, and what deterministic condition makes it run?** Bench context: GP1 (in-probe endpoint
reset pulse, DTS hog kept) sometimes completes S1 (2 of 4 warm boots; page 0x86807FEF / 2 /
0xABCD) and otherwise falls back to the pre-S1 signature (0x17DF44 / 0x1BD000 / 0x1BD0D8);
GP2 (hog removed, driver owns the pins, one clean pulse) never completes S1 but improves
mask 0x0A -> 0x00 (page 0x86803FEF / 0 / 0xABCD).

**Answer in one line.** The writer of 2 is mask-ROM stage S1, but S1 is not reached because the
image itself never manages to hand the secondary-core firmware to the secondary core: entry A
copies the CPU1 ITCM/DTCM images into TCM windows from a **platform-custom block at CPU
0x1B2800** whose section bounds must be supplied by the host; our transport never supplies
them, so the copies fail and the secondary core never runs. The host action is to pre-seed that
block (or preload the TCM windows) before release - GP3 below.

---

## 0. TL;DR - the answers

**(a) Who writes +8 = 2.** **Mask-ROM stage S1, not the image.** Verified negatives:
the only image store to +8/+C is entry A's strd {0,0xABCD} at CPU 0x473D4; the only literal
homes of CPU 0x1BF008 in the whole 928,920-byte image are 0x47510 (the handshake argument load)
and 0x47620 (the wait-path cache-range base) [V]; the secondary-core image shipped inside
FIRMWARE.bin (below) never references the page, the magic or the doorbell [V]. The ROM keeps
FIQ for itself (live FIQ vector target = 0x7C, ROM code) while IRQ is vectored into the image
trampoline 0x40064 [V*], and its handler-slot table points at the page area (CA 0x1F4 =
0x001BF110, CA 0x1F8 = 0x001BEF00) [V*] - it is the only agent that both owns the page contract
and survives the handoff to entry A. The +8 = 2 reply is emitted by the ROM's secondary-core
release/service path after entry A's wake doorbell. [I, high]

**(b) The deterministic condition found in this lane.** FIRMWARE.bin is a container of
**two standalone ARM images**: the CPU0 image at CPU 0x40000.. and a secondary-core image
**ITCM blob at CPU 0x117BC4 (len 0xA968) + DTCM blob at CPU 0x12252C (len 0x76C)** that ends
exactly at EOF; the ITCM blob starts with its own ARM exception vector table (linked at base
0) [V, and the local device INI states the same four bounds]. Entry A (CPU 0x46970) does not
just wait for S1 - **it first hands that image to the secondary core**: at CPU 0x46D3A..0x46D84
it reads four words from the platform-custom config block at **CPU 0x1B2800**
(BAR0 = CPU+0x6B8000 = 0x86A800), range-checks them against the page (0x1BF000), then calls
a bounds-checked copy (0xC1E44 -> memcpy 0x1036AC):
safe_copy(TCM 0x400000, cap 0xC000, custom_plat[+4], custom_plat[+8]) (ITCM) and
safe_copy(TCM 0x410000, cap 0x8000, custom_plat[+0xC], custom_plat[+0x10]) (DTCM), then
memset(0x416400,0,0x80) and *(u32*)0x416400 = custom_plat[+0] [V].
Those TCM windows are the vendor memmap regions excp_wram {0x400000,0x18000} and
excp_smac_dtcm {0x410000,0x8000} [V from plat.ko data] and are host-readable/writable through
the **region-1 window (BAR0 0x1C0000 / 0x1D0000)**. If the host never wrote the custom block
our transport does not - the bounds are stale/garbage, both safe copies fail, TCM stays empty,
the secondary core cannot run, and nothing ever writes 2. The deterministic condition is
therefore: **TCM payload loaded correctly + secondary-core release**. The first half is
host-controlled (GP3); the second is ROM-owned but exercised by the same entry-A path.

**(c) Why GP1 is flaky and GP2 never.** GP1 lets the chip's ROM run twice: once at SoC
power-on when the DTS hog releases the pins, and again after the driver's in-probe pulse. The
second (warm) ROM run can take a path that never boots the image at all - that is the observed
pre-S1 fallback page {0x17DF44, 0, 0x1BD000, 0x1BD0D8, 0x23232323}, which carries **no entry-A
progress bits and no {0,0xABCD}** - i.e. entry A never ran. The successful GP1 boots are the
warm runs that booted the image **and** still had a valid platform-custom block left in chip
WRAM by the earlier stock-vendor boots (WRAM survives chip/SoC resets while power stays on);
after one or two successful boots that stale block had been consumed/overwritten, and every
later boot (including all GP2 runs) failed. GP2 itself is the cleaner base: one single reset,
the ROM always boots the image (entry A runs; page 0x86803FEF/0/0xABCD) but S1 still cannot
complete because the custom block is invalid. So GP1's "sometimes" is *stale configuration
residue plus warm-run variance*, not a property of the pulse. [V for the code mechanism;
[I] for the residue explanation]

**(d) The exact deterministic host action (GP3).** Keep the GP2 configuration (hog removed,
driver owns the pins -> single clean reset) and add, in the transport before release:
1. write the four firmware section bounds into the platform-custom block
(BAR0 0x86A804/08/0C/10 = 0x00117BC4 / 0x0000A968 / 0x0012252C / 0x0000076C), read back; and
2. (fallback, or same run - identical bytes) preload the TCM windows from the local
FIRMWARE.bin: ITCM file 0xD7BC4..0xE252C -> host BAR0 0x1C0000 and DTCM
file 0xE252C..0xE2C98 -> host BAR0 0x1D0000, read back the first words (0xE59FF018,
0x40034000).
Then release 0x5A5A and watch. Expected: page 0x86807FEF / 0x3 / 0x00000002 / 0x0000ABCD,
TCM word0 = 0xE59FF018 (ITCM) and 0x40034000 (DTCM), state CA 0x118B60 = 1, mask = 0, then
the boot task/scheduler and the transport ready path progress. Full plan + decision matrix:
section 4.

**(e) If it still fails, the closing evidence list** is in section 6: stock golden reads of the
custom block, page and TCM windows (all host-readable, no writes), plus the ROM FIQ vector page
and the TCM coreword. There is no need for a ROM dump for the TCM gate itself; a ROM dump would
only be needed to read the exact S1 reply code, which is not on the critical path any more.

---

## 1. Method and local inputs

| artifact | identity | role |
| --- | --- | --- |
| /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin | 928,920 B, sha256 7fc87e20...ef311b, **local evidence only - never copied** | image + secondary-core blobs |
| /tmp/rootfs2524b/lib/firmware/hi_wifi/cfg_device_hisi.ini (same file in the 2.4.15 recovery-extract rootfs) | 799 B, local, gitignored | the four firmware section bounds + custom-block addresses |
| /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko | 364,660 B, 2.5.24, local | vendor memmap table and custom-config machinery |
| prior lanes: wifi-rom-s01, wifi-rom-flow, wifi-event-router, wifi-ete-engine, wifi-chip-reset, wifi-boot-kick, wifi-abort-rca, wifi-fw-link-event | committed reports | handshake page, ROM vectors, per-core event controllers, GP1/GP2 bench data |
| GP1/GP2 bench data (this swarm, cited as [V*]) | earlier attended lanes | page/state/mask time series |

Tool: tools/s1_cpu1.py (new, host-only) reads the firmware and (optionally) the local device
INI in place and prints derived facts. selftest = 46/46 checks, all PASS with the INI present,
and it skips (not fails) the INI cross-check when the INI is absent.
HI5622_FW / HI5622_INI override the paths.

Evidence grades: **[V]** verified here from the local artifact by tools/s1_cpu1.py;
**[V\*]** device-measured by an earlier attended lane and cited; **[I]** inference from code;
**[U]** unknown without a device read.

Address conventions (project-wide): image **CPU** address = file + 0x40000; host BAR0 region 5
(ACP-fw) = **CPU + 0x6B8000**; host BAR0 region 1 (WRAM/TCM) = 0x1C0000 + (CA - 0x400000);
host BAR0 device IO = 0x3B8000 + (CA - 0x40000000); the 0x1BF000 page is reachable at
BAR0 = 0x877000 (page CPU 0x1BF000 + 0x6B8000). phys = 0x40000000 + BAR0.

---

## 2. The image is a two-image container: CPU0 image + secondary-core ITCM/DTCM images [V]

The device INI carries a "boundary" block (its own comment: "changes in real time, written by a
python script") that is a complete description of the firmware file:

    firmware_itcm_src_addr = 0x117BC4   firmware_itcm_len = 0xA968
    firmware_dtcm_src_addr = 0x12252C   firmware_dtcm_len = 0x76C
    firmware_plat_custom_src_addr = 0x1B2800   firmware_plat_custom_len = 0x60
    firmware_wifi_custom_src_addr = 0x1B2860   firmware_wifi_custom_len = 0x9A8

Verified against the file [V]:

* CPU 0x117BC4 + 0xA968 = 0x12252C and 0x12252C + 0x76C = 0x122C98 = exactly EOF. The two blobs
  tile the tail of the file exactly.
* The ITCM blob starts with a **standalone ARM exception vector table**:
  +00..+10 ldr pc,[pc,#0x18] x5, +14 NOP, +18/+1C ldr pc,[pc,#0x14], with target words
  0xA3EC, 0xA324, 0xA34C, 0xA374, 0xA39C and IRQ target 0xA4B0 - all inside the blob. The code
  is linked as a **base-0 ARM image**: the second core executes it from its ITCM once loaded.
* The DTCM blob starts with a table of device-address words (0x40034000, 0x3A, 0, 0x40034014,
  ...), i.e. initial data, not code.
* The CPU1-region code at CPU 0x120A6C (rom-s01's "CPU1 bootstrap") is inside the ITCM blob at
  ITCM offset 0x8EA8, i.e. it is a routine of the secondary-core image, not a reset entry.

The vendor memmap in hi5622v100_plat.ko (data table at .data+0x26B8) names the two copy
destinations [V]:

| region | device CA | size | vendor name |
| --- | ---: | ---: | --- |
| 1 | 0x00400000 | 0x18000 | excp_wram   (ITCM image lands at 0x400000) |
| 2 | 0x01000000 | 0xE0000 | excp_pktram |
| 3 | 0x00410000 | 0x8000  | excp_smac_dtcm (DTCM image lands at 0x410000) |

Host windows: CA 0x400000 -> BAR0 0x1C0000, CA 0x410000 -> BAR0 0x1D0000 (region 1, 0x18000).

**Correction to the straightforward reading of the page:** the page words +0x10..+0x2C are
*not* a TCM descriptor. The handshake's cache range is [+8,+0x10) (entry A sets r6 = r4+8 =
0x1BF010 before the wait loop), and the only reader of +0x10..+0x20 is the handshake/cache
code. The TCM descriptor lives in the **platform-custom block**, section 3.

---

## 3. The CPU0 -> CPU1 handoff gate: entry A copies ITCM/DTCM from the custom block [V]

Entry A (CPU 0x46970) loads r6 = 0x001B2800 at CPU 0x469AC (literal 0x46A78) and uses it
twice:

**(1) Boot config.** 0x469E0..0x46A18 read custom_plat[+0x24]..[+0x54] (13 words, mode + 12
config words) and store them into the low-RAM boot object at CPU 0x10C080
(r4 = 0x10C0C0 - 0x40). custom_plat[+0x24] is a mode word compared with 1 and 3; the vendor
normal boot uses one of those. [V]

**(2) Secondary-core TCM handoff.** CPU 0x46D3A..0x46D84 [V]:

    r2  = custom_plat[+0x04]      ; r3 = custom_plat[+0x08]
    r8  = custom_plat[+0x0C]      ; sb = custom_plat[+0x10]
    if (r8 + sb) > 0x001BF000: skip the whole handoff
    safe_copy(0x00400000, 0xC000, custom_plat[+0x04], custom_plat[+0x08])   ; ITCM
    safe_copy(0x00410000, 0x8000, custom_plat[+0x0C], custom_plat[+0x10])   ; DTCM
    memset(0x00416400, 0, 0x80);  *(u32*)0x00416400 = custom_plat[+0x00]

0xC1E44 is a bounds-checked copy wrapper around memcpy at CPU 0x1036AC; 0xC1E0C is a
bounds-checked memset wrapper around CPU 0x1031A4. The two blobs are the ITCM/DTCM images of
section 2 - so **the image's own CPU0 boot path loads the secondary core, using bounds supplied
by the host**. Only after this handoff does entry A reach the page handshake
({0,0xABCD}, wake core 1, poll +8==2).

Consequences:

* With a valid custom block, entry A copies the secondary-core image into TCM at 0x400000 /
  0x410000 and then waits for the ROM to release that core; the ROM's release/service path is
  the S1 partner that writes +8 = 2.
* With an invalid custom block (our transport's state) both copies fail early; TCM stays empty;
  the core either cannot start or faults immediately; S1 never answers; entry A spins forever
  and the scheduler never runs - exactly the GP2 signature (0x86803FEF with no bit14, mask 0,
  state 0), and also the GP1 pre-S1 fallback when the ROM does not boot the image at all.
* The four bounds are exactly the device-INI values, and the whole custom-config machinery
  exists in the vendor stack: plat.ko parses the firmware_itcm_*/firmware_dtcm_* keys
  (its string table and .data config-descriptor array), builds the platform/wifi custom
  blocks and sends them to the device as part of platform init (dev_custom_send ->
  bal_write, sizes 0x60/0x9A8) before the firmware release. The image's other consumer of
  CPU 0x1B2860 (the wifi-custom block) is the PHY/radio config code (many references in
  0x72xxx..0xA4xxx) [V]. Our transport uploads only the flat FIRMWARE.bin and never writes
  either custom block.

Residual unknowns: the exact S1 reply code (ROM) and the exact core-release register/sequence
inside the ROM are [U]; they are not needed for GP3 because the same release path is exercised
by the image's own handoff + doorbell, and the only missing host input is the TCM payload.

---

## 4. GP1 vs GP2, and the deterministic GP3 action

### 4.1 The GP1/GP2 observations [V*]

| boot class | page +0 | page +8 | page +C | other | meaning |
| --- | --- | --- | --- | --- | --- |
| pre-GP1 / GP1 fallback | 0x17DF44 | 0x1BD000 | 0x1BD0D8 | +4=0, +10=0x23232323 | ROM fallback; **entry A never ran** |
| GP2 (hog removed) | 0x86803FEF | 0 | 0xABCD | mask=0, state=0, +4=3 | entry A ran + wrote its pair; no S1 |
| GP1 contract / vendor | 0x86807FEF | 2 | 0xABCD | state=1 | S1 replied; boot task + scheduler run |

### 4.2 Mechanism

1. **GP1 runs the chip's ROM twice.** The DTS hog releases the chip at early SoC boot (ROM run
   #1); the in-probe pulse then resets it (ROM run #2). The second run is a *warm* reset of a
   chip whose SRAM still holds ROM state; on some boots the ROM takes a path that does not boot
   the image at all -> the 0x17DF44 fallback page (no entry-A writes). That is the flaky half.
2. **Stale WRAM made early GP1 boots succeed.** The platform/wifi custom blocks live in
   firmware RAM (CPU 0x1B2800/0x1B2860, ACP-fw region 5). They are written by the vendor host on
   a stock boot and *persist* in chip RAM across resets while the board stays powered. The first
   GP1 boots after the stock vendor boots still had valid bounds, so entry A's TCM copies
   worked and S1 ran. Once those blocks had been consumed/overwritten (they are not part of any
   of our uploads, and the successful boot's own runtime clobbers them), every later boot -
   GP1 or GP2 - lost S1. This fits the observed order (two successes immediately after the
   stock/vendor sessions, then none) better than any pulse-shape explanation.
3. **GP2 is the clean base.** One single reset at probe; the ROM consistently boots the image
   (entry A progress 0x86803FEF, mask 0), but the TCM bounds are invalid -> no CPU1 -> no
   +8=2. GP2's mask=0 vs GP1's 0x0A is a consequence of the same story (entry A spins in a
   4-instruction loop with no exception instead of proceeding into faulting init code).

So the flakiness is *configuration residue + warm-run variance*; the deterministic base is
GP2 + a valid TCM payload.

### 4.3 GP3 - exact host action

**Preconditions:** GP2 kernel (wifi-pcie-hog removed; the driver owns GPIO0 20/21/28 and
performs the single in-probe assert/release pulse before clk/LTSSM), transport
prepare/upload/verify, gate 0xcece service, recovery door armed.

**Step A (vendor-faithful, primary).** Before writing the release word, pre-seed the
platform-custom block bounds (device CPU 0x1B2800 -> host BAR0 0x86A800, phys 0x4086A800),
32-bit writes, then read back:

| BAR0 | value | meaning |
| ---: | ---: | --- |
| 0x86A804 | 0x00117BC4 | firmware_itcm_src_addr |
| 0x86A808 | 0x0000A968 | firmware_itcm_len (cap 0xC000) |
| 0x86A80C | 0x0012252C | firmware_dtcm_src_addr |
| 0x86A810 | 0x0000076C | firmware_dtcm_len (cap 0x8000) |

Also log the whole block 0x86A800..0x86A85F (mode word at 0x86A824 is expected 1 or 3; do not
write it unless a run shows otherwise).

**Step B (fallback, safe in the same run).** Preload the TCM windows with the same bytes from
the local FIRMWARE.bin, then read back:

| host BAR0 | source (file offset) | len | readback expectation |
| ---: | ---: | ---: | --- |
| 0x1C0000 | 0xD7BC4 | 0xA968 | word0 = 0xE59FF018, word4 = 0xE320F000 |
| 0x1D0000 | 0xE252C | 0x76C | word0 = 0x40034000 |

Then write release CA 0x40000108 = 0x5A5A and watch for 20 s.

**Expected observables (one boot):** page CA 0x1BF000 (BAR0 0x877000) at ~0.5/2/5/20 s:
+0 = 0x86807FEF (bit14 set; GP2 baseline 0x86803FEF lacks it), +4 = 3,
**+8 = 0x00000002**, +C = 0x0000ABCD; TCM BAR0 0x1C0000/0x1D0000 as above; state CA 0x118B60
= 1; exception mask CA 0x40008 = 0; boot task/scheduler armed (event layer, CA 0x40161100 bit13);
then the normal transport watch (out[1] bit2, g4 ready id1/id2, ETE DR fills).

**Decision matrix.**

| observation | meaning | next |
| --- | --- | --- |
| TCM loaded + 2/0xABCD + state 1 | S1 deterministic; handoff gate confirmed | keep GP3; continue ready/ETE path |
| TCM loaded + +8 still 0 | CPU1 release (ROM) is the remaining gate | add the vendor wifi-custom block at CPU 0x1B2860 (BAR0 0x86E060) and read ROM status/TCM coreword 0x10938 |
| TCM empty, page shows no entry-A bits | image never booted (warm-run path) | keep the GP2 single-reset config; do not poke RAM |
| TCM empty, page shows entry-A bits | descriptor write did not land/ordered wrong | re-read before release, retry with Step B only |
| any exception/UR | stop, power-cycle, snapshot dmesg | no retry in session |

**Safety.** 4-6 32-bit writes into firmware RAM before release (image not executing), no device
IO register, no ROM page word, no handler slot, no CA 0x400392F0 / 0x40160110. Attended fire,
recovery armed, stop rule as in the swarm runbook.

### 4.4 Read-only pack that should be taken on the same boot anyway (cheap, decisive)

| # | read | BAR0 | expected / discriminator |
| --- | --- | ---: | --- |
| G1 | platform-custom block, 0x18 words | 0x86A800 | +4/+8/+0xC/+0x10 = the four bounds on a working boot; +0x24 mode 1/3 |
| G2 | page time series +0/+4/+8/+C | 0x877000 | states as in 4.1 |
| G3 | TCM ITCM window first 0x10 words | 0x1C0000 | 0xE59FF018 x5 / 0xE320F000 / 0xE59FF014 x2 when loaded |
| G4 | TCM DTCM window first 4 words | 0x1D0000 | 0x40034000, 0x3A, 0, 0x40034014 |
| G5 | TCM coreword | 0x1C0938 | nonzero (0x20/0x40/0x60) if the CPU1 init ran |
| G6 | state / mask | 0x7D0B60 / 0x6F8008 | state 1, mask 0 = success |
| G7 | ROM FIQ/IRQ vector words | 0x6B8020..0x6B803C, 0x6B81E0..0x6B81FC | ROM FIQ=0x7C, IRQ=0x40064 (record on stock too) |

The same pack on a **stock vendor boot** (the vendor stack writes the custom blocks) gives the
golden custom-block/TCM values: this is the decisive differential if GP3 is ambiguous.

---

## 5. Evidence table (new claims of this lane)

| # | claim | method | grade |
| ---: | --- | --- | --- |
| S1 | the only image store to page +8/+C is entry A strd {0,0xABCD} @0x473D4; no image writer of 2; literal homes of 0x1BF008 = {0x47510,0x47620} | whole-image literal/movw/movt census + straight-line store taint | [V] |
| S2 | the wait path 0x47610 has no timeout: cache-maint then b 0x473F0 | disassembly | [V] |
| S3 | FIRMWARE.bin is a container: secondary-core ITCM image at CPU 0x117BC4 (0xA968) + DTCM image at 0x12252C (0x76C) tiling EOF; ITCM has its own base-0 ARM vector table; local device INI states the same bounds | file layout + INI read + disassembly | [V] |
| S4 | entry A loads custom_plat = CPU 0x1B2800 (literal 0x46A78) and uses +0x24..+0x54 as boot config | disassembly 0x469AC..0x46A18 | [V] |
| S5 | entry A copies custom_plat[+4]/[+8] -> 0x400000 (cap 0xC000) and custom_plat[+0xC]/[+0x10] -> 0x410000 (cap 0x8000) via bounds-checked copy 0xC1E44, after a <=0x1BF000 range check; then memset 0x416400 0x80 and stores custom_plat[+0] there | disassembly 0x46D3A..0x46D84; 0xC1E44 body calls memcpy 0x1036AC; 0xC1E0C calls memset 0x1031A4 | [V] |
| S6 | those destinations are the vendor memmap excp_wram {0x400000,0x18000} and excp_smac_dtcm {0x410000,0x8000}; host region-1 window BAR0 0x1C0000 / 0x1D0000 | plat.ko .data table + .rel.data decode; transport/fwboot region map [V*] | [V] |
| S7 | the vendor stack carries the same bounds/addresses as INI keys and a custom-config send path (plat.ko parses them, dev_custom_send writes 0x60/0x9A8 blocks with bal_write before release); our transport does not | plat.ko string/descriptor/function disassembly | [V] |
| S8 | the secondary-core image never references the handshake page, magic or doorbell; the CPU1 tail at 0x120A6C is an ITCM-image routine that registers ROM low-RAM Thumb handlers 0x67d/0x691/0x6d9 on events 0x39/0x3a/0x3b and initialises its per-core controller 0x4018xxxx | literal scan + CFG walk + disassembly | [V] |
| S9 | ROM vectors: IRQ -> image trampoline 0x40064, FIQ -> ROM code 0x7C; handler-slot table CA 0x1E0..0x1FC contains image header slots + page-area pointers 0x1BF110/0x1BEF00 | prior live reads [V*] + arithmetic; image header mapping | [V*] |
| S10 | CA 0x40161F00 has exactly four image writers (entry A 0x20000; notify helper (1<<core)<<16 OR 1; exception 0x10002/0x20002) and no reader: it is a hardware inter-core wake/trigger whose consumer is the ROM/target core | whole-image doorbell census | [V] |
| S11 | S1's +8=2 writer is mask-ROM code, not image code | S1/S8/S9/S10 + golden vendor contract 2/0xABCD | [I, high] |
| S12 | the deterministic gate is the TCM payload handoff: invalid custom-block bounds -> safe copies fail -> CPU1 cannot run -> no S1; valid payload + ROM release -> S1 | S4/S5/S6/S7 + GP1/GP2 [V*] | [I, high] |
| S13 | GP1 flakiness = warm ROM-run variance (pre-S1 fallback page = entry A never ran) plus stale custom-block residue from earlier stock boots; GP2 = single clean reset, image boots, gate missing | GP1/GP2 time series [V*] + S12 | [I] |

---

## 6. If GP3 is still negative: what would close S1 definitively

1. **Golden stock differential (read-only, one stock boot).** With the vendor 2.5.24 stack up,
   read the platform-custom block (BAR0 0x86A800, 0x60 B), the wifi-custom header
   (BAR0 0x86E060, first 0x40 B), the handshake page (BAR0 0x877000), the TCM windows
   (BAR0 0x1C0000/0x1D0000) and the ROM vector/slot words. That names the exact working
   values and confirms the INI-derived bounds. (No writes; all inside the six inbound
   regions.)
2. **Kprobe/stock instrumentation** (attended): probe the vendor driver's bal_write/
   dev_custom_send calls to record exactly what it writes to 0x1B2800/0x1B2860 and when,
   relative to the release write. That makes the GP3 block byte-exact without guessing the
   mode/config words.
3. **TCM/coreword readback after GP3** (rows G3-G5): if TCM contains the ITCM vector table but
   the coreword 0x10938 stays 0 and +8 stays 0, the gate is the ROM's core-release path itself,
   and only a ROM-side trace (vendor debug or a mask-ROM dump) can name the condition. That is
   the definitive "ROM-internal, not host-triggerable" statement - but note it would be after
   the host has supplied the TCM payload, which is a much stronger negative than the pre-GP3
   state.

---

## 7. Negative results / do-not-chase

* No image site writes the value 2 to the handshake page (or to +8/+C at all beyond the strd);
  the CPU1/ITCM image does not reference the page, the magic or the doorbell [V].
* The page words +0x10..+0x2C are not a TCM descriptor (correction in section 2); the descriptor
  is in the platform-custom block [V].
* A third strd-style writer or a hidden pointer to the page does not exist: the literal census
  of 0x1BF000/0x1BF004/0x1BF008 is complete and small [V].
* Do not write the mode word (+0x24) or the wifi-custom blob blindly: GP3 starts with the four
  bounds only; everything else is a follow-up after a run.
* Do not repeat the pre-GP1 "no pulse" configuration to hunt S1: the ROM does not boot the
  image there at all; GP2 (single clean reset) is the correct base.

## 8. Reproduction (host-only; local blobs read in place; nothing vendor committed)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-s1-cpu1/tools/s1_cpu1.py

    $PY $T selftest     # 46/46 PASS - every host-side fact in this report
    $PY $T handshake    # page layout/writers + the two observed state signatures
    $PY $T tcm          # custom-block contract + ITCM/DTCM blobs + windows
    $PY $T cpu1         # secondary-core image, tail 0x120A6C, ROM handler ABI
    $PY $T rom          # ROM vectors/slots, doorbell census, writer argument
    $PY $T gp3          # the GP3 plan with exact BAR0 addresses and observables

The tool reads HI5622_FW and (optionally) HI5622_INI in place and prints derived
annotations only. No device contact by this lane.

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
contents are derived addresses, constants, structure layouts and disassembly facts. Device
untouched by this lane.*
