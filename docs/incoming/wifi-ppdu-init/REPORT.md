# REPORT - the PPDU descriptor-list INIT (CA 0xC016C / CA 0xD3BAC): the chain, the preconditions, and the object the probe should have read

**Lane:** `wifi-ppdu-init` (task-219).  HOST-ONLY static decode of the pinned
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; CA = file
offset + 0x40000).  Grades: **[V]** re-derived by
[tools/fw_ppdu_init_re.py](tools/fw_ppdu_init_re.py) (`--selftest` 36/36 PASS);
**[V*]** committed earlier lane / dock dump; **[I]** inference; **[U]** needs a
device read.  Everything below is read-only; nothing here authorises a write.

---

## 0. Verdict

1. **Trigger: firmware boot, not an H2D message.** The image entry word
   (CA 0x40000) is the Thumb pointer 0x46971 -> fn **0x46970**.  In its boot-init
   body it takes the function pointer 0x47625 (fn **0x47624**) and hands it to
   **CA 0xC745C** (registration; the descriptor is assembled on the stack at
   0x47412-0x47420 *with the entry in the descriptor's first word*).  That
   registered body later calls the device init **0xD1B68** from **0x48314**.
   **[V]** - no BL targets 0x46970 or 0x47624, i.e. both are table/task-installed,
   which is exactly what the registration call does.
2. **The device init 0xD1B68** (called with no argument; all state is static)
   first `memset(0x174BE0, 0, 0x1C68)`, then applies its **gate**
   `u8[0x17FCA8+0xB68]` = u8[0x180810]:
   * 0 -> return 0 (skip, silent, the list stays uninitialised),
   * 1 -> run (= write the device array and loop the bands),
   * >1 -> log and return 0x65 (error; the caller aborts the whole wifi attach).
   It writes the device array header (`u8[0x174BEE]=2`, `u8[0x174BEC]=0x1152`,
   `u16[0x174BE8]=0x100`) and then loops band = 0..count-1 with
   `r4 = 0x174C30 + band*0xE28`, `r5 = r4-0x40 = get_device(band)`
   (= CA **0xF4F48**: `0x174BF0 + band*0xE28` for band 0..1, NULL otherwise).  **[V]**
3. **The init CA 0xC016C receives that per-band object in r0** (`mov r0,r5` at
   0xD3BAA, `bl 0xC016C` at 0xD3BAC - its only BL caller).  It sets the PPDU
   descriptor list up **inside the object**: head self-pointer at obj+0x66C
   (= 0x0017525C for band 0), prev self-pointer at +0x670, count 0 at +0x674,
   the debug-name pointer **0x001060F7** (`&pst_device->st_device_alloced_ppdu_dscr_list.st_spin_lock`)
   at obj+0x67C, then **pre-allocates 0x1A0-byte descriptors** with allocator
   CA 0xBF128 and appends them to the list (0xC043A..0xC0450).  Return 0 =
   success, 0x64 = failure.  **[V]**
4. **The bench datum probed a different structure.** The f1-thread probe used
   `ctx = u32[0x171458] + u16[0x171504]`.  That is the resolver's
   **module context id 0 = 0x119758**: the resolver (CA 0x961AE -> 0xFE578) is
   `u32[manager + 4*(0x18C+id)] + u16[manager+0x6DC]`, manager 0x170E28, and
   the 19 x 0xB40 context array is built from the literal **0x119758** (CA
   0x49330) into manager+0x630.  The PPDU list, however, lives in the **device**
   object 0x174BF0+0xE28*band.  0x119758 != 0x174BF0, so the reported
   `u32[0x119758+0x67C] = 0` is **expected in every boot** and **does not
   prove** the list was never initialised.  **[V] addresses, [V] consequence.**
5. **Corrected self-check:** `u32[0x174BF0+0x67C]` must be 0x001060F7
   (phys 0x4082D26C) and head/prev 0x0017525C (phys 0x4082D25C/0x4082D260);
   band 1 object is 0x175A18 (name at 0x00176094).  See PROBES P2-P7.
6. **If the corrected probe still shows the list absent**, the differences to
   chase are firmware-internal and ranked H1 gate byte, H2 the body never
   reaching 0x48314 (earlier failure), H3 boot registration order - not a
   missing H2D message (REPORT sec. 6).  **[I]**

---

## 1. Inputs and method

* image `rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin` (2.4.15 == 2.5.24
  payload), 928,920 B, sha256 `7fc87e20...c3ef311b`; CA = file+0x40000 **[V]**;
* committed lanes as prior art: `../wifi-rx-f1-thread` (PPDU list located, the
  probe formula), `../wifi-f1-from-capture` (captured families),
  `../wifi-smac-start` (20 stock-only g0-id1 keys), `../wifi-rx-*`;
* method: capstone 5.0.7 Thumb for exploration, then every quoted fact is
  re-derived by the committed stdlib tool (near-range Thumb-2 BL decoder,
  PC-relative literal resolver, fixed word reads).  A recursive-descent walker
  over the image's function starts was used to locate containing functions;
  the tool only asserts the final facts (36 checks).

## 2. The call chain (all **[V]**)

| # | site (CA) | what it does | evidence |
| ---: | --- | --- | --- |
| 1 | **0x40000** | image word0 = 0x46971 -> boot entry fn **0x46970** (Thumb) | A1; no BL caller of 0x46970 (B3) |
| 2 | **0x47416-0x47424** | entry fn loads 0x47625 (fn 0x47624) from pool 0x4751C, builds a descriptor at sp+0x18 (`str r1,[sp,#0x18]`), calls **CA 0xC745C** with r0 = &descriptor | B1, B2, B6 |
| 3 | **0x47624** | registered body entry (two literal loads then `push.w`); not a BL target (B4/B5) | |
| 4 | **0x482E6-0x48312** | preceding init steps, then a bounded wait loop (r4=0xB; `bl 0xC2E3C(0x32)` sleep; reads `u32[0x18179C]` and `[ptr+0x50]`) | decoded |
| 5 | **0x48314** | `bl 0xD1B68` - the only BL caller of the device init (C1/C2) | |
| 6 | **0xD1B68** | `memset(0x174BE0,0,0x1C68)`; gate u8[0x180810]; device array header; band loop | D1-D3, E1-E3, F1-F3 |
| 7 | **0xD3BAA/0xD3BAC** | `mov r0,r5; bl 0xC016C` (r5 = 0x174BF0+band*0xE28) | F4 |
| 8 | **0xC016C** | PPDU descriptor-list create/init + prealloc (only BL caller 0xD3BAC) | H1-H8 |

The two registered/table-installed roots are consistent with the previous
lane's finding that the `HCC RX Thread` entries have no BL callers (TABLES
T8): this firmware installs its init/task entries through tables, so a BL-only
caller scan misses them.

## 3. The device init 0xD1B68 (what it needs to run)

Entry bytes: `movw r3,#0x1c68` before the push (the literal-load setup is part
of the entry, hence the registered pointer is 0x47625/0xD1B69-style Thumb
addresses are always odd).

Order of operations **[V]**:

1. **clear the per-device array**: `memset(0x174BE0, 0, 0x1C68)` (CA 0xC1E0C;
   observed call convention `(dst, len, fill)`).  This also zeroes the count
   byte the loop later reads - and the same function writes it back (step 3).
2. **gate**: `r3 = u32[0x17FCA8]` (module context, literal pool 0xD1CE8);
   `ldrb.w r3,[r3,#0xB68]`; `cmp r3,#1`; `bls.w 0xD3F9E`.
   * flag 0 -> 0xD3F9E -> 0xD3F86 -> return 0 (no init, no error);
   * flag 1 -> 0xD1B9E -> run the init;
   * flag >1 -> log at 0xD1B8A-0xD1B90 then return 0x65 (caller aborts).
3. **device array header**: writes `u16[0x174BEC]=0x1152`, `u8[0x174BEE]=2`
   (the **device count** the loop later reads), `u16[0x174BE8]=0x100`; several
   device-IO registers are programmed (0x40101900/04, 0x40002260 checks...).
4. **band loop**: `r4 = 0x174C30` (literal pool 0xD1D18), `r7 = 0`; head at
   0xD3F40: `r2 = 0x174BE0` (loop pool 0xD3FC4), `count = u8[r2+0xE]`,
   `cmp count, band`; body 0xD1CB6..0xD3F3C; tail `r4 += 0xE28; band++`.
5. In the body: `r5 = r4 - 0x40` (0xD1D92), i.e. `0x174BF0+band*0xE28`;
   a large per-band init (queues, register windows, function pointers, spinlocks
   named `&pst_hal_device->st_spinlock` / `&pst_device->st_reset_hw_info.st_reset_spinlock`);
   then `bl 0xC016C(r5)` at 0xD3BAC; on failure returns 0x64.
6. **getter CA 0xF4F48**: `if (chip) return 0; if (band > 1) return 0; return 0x174BF0 + 0xE28*band`
   (literal 0x174BF0 at 0xF4F60, movw 0xE28 at 0xF4F4E, bound `cmp r1,#1`).

**Preconditions, consolidated:** the boot entry has run (registration done); the
registered body reaches 0x48314 (its preceding init steps return 0 - the two
abort branches are 0x482E2 and 0x482F0 -> 0x47D5E); `u8[0x180810] == 1`; a
device array at 0x174BE0/0x174BF0 that this function itself sizes (2 devices).
There is no evidence of a *host-writable* field among these: they are all
firmware-RAM state written by this same boot sequence.

## 4. The list init CA 0xC016C (what it builds)

* object = r0 = the per-band device object (0x174BF0+0xE28*band) **[V]**;
* sets head = obj+0x66C and prev = obj+0x66C (**same** self-pointer, i.e. both
  store `sb = r4+0x66C`, 0xC02B2/0xC02B6), count = 0 (0xC02D0), name
  0x001060F7 at obj+0x67C (0xC02C2, pool 0xC0484) **[V]**;
* then, still inside the same function, a loop allocates 0x1A0-byte descriptors
  with CA 0xBF128 (0xC0402; the size 0x1A0 is set at 0xC03F4) and **appends**
  them (0xC043A: `ldr r3,[obj+0x670]; str r5,[obj+0x670]`; 0xC0450: count++),
  until a count returned through 0xD1A6C is reached **[V] loop shape, [I] count
  source**;
* returns 0 (success) / 0x64 (failure, logged at 0xC0412).  The caller treats 0
  as success and proceeds; 0x65 from the gate aborts the whole attach.

Consequence for F1: the list is **not** host-populated - the host never writes
obj+0x66C..+0x67C.  A present list proves the firmware ran this init; an absent
one means the firmware did not (or ran it on another band object).

## 5. The correction to the earlier probe (important)

`wifi-rx-f1-thread` PROBES P4 used the resolver formula as the object of the
PPDU list.  That is the *module-context* array, not the device array:

| item | module contexts (resolver) | device objects (the list) |
| --- | --- | --- |
| base | **0x119758** (literal CA 0x49330) | **0x174BF0** (literal CA 0xF4F60, getter 0xF4F48) |
| stride / count | 0xB40 x 19 (id 0..18) | 0xE28 x 2 (band 0..1) |
| init site | manager init 0x49204..0x49264 (manager 0x170E28, table at +0x630) | 0xD1B68 / 0xC016C |
| PPDU list field | +0x66C in the *device* struct only | obj+0x66C..obj+0x67C |

The probe printed `a = 0x119758, b = 0` -> ctx 0x119758 = context id 0.  Its
`ctx+0x67C` (CA 0x119DD4, phys 0x407D1DD4) is 0x50 before the band-0 device
object starts and has no relation to the PPDU list.  **Therefore the bench
datum `ctx+0x67C == 0` must not be read as "the PPDU descriptor list was never
initialized".**  The corrected read-only check is P2/P3 in PROBES.md.

(Why the formula exists at all: the resolver is used all over the RX/TX code to
get per-module contexts; several of those contexts have their own fields at +0x66C
etc., which is why an offset-only scan is ambiguous.  The PPDU list is
identified by its debug-name literal and its init site, not by an offset.)

## 6. Why it does not run on our host-only boot - candidates

The init is not message-driven, so the difference from stock cannot be "a
missing H2D message".  Ranked candidates (all **[I]** until probed):

* **H1 - gate byte differs.** `u8[0x180810]` is firmware-RAM state from the
  boot init; on our boot it may be 0 (skip) or >1 (error aborted the attach).
  This is the single cheapest discriminator (PROBES P1).
* **H2 - the registered body never reaches 0x48314.**  That body is long; the
  call is behind several `cmp r0,#0` / `bne.w 0x47D5E` abort branches.  If an
  earlier step fails on our boot, the device init never runs.
* **H3 - boot-registration/order difference.**  The registration call 0xC745C
  is in the image-entry function 0x46970; if a boot-environment difference (host
  cfg / firmware section upload / a prior init returning early) changes that
  function's path before 0x47416, the body is never installed.
* **H4 - the init did run, on the other band object.**  The loop covers bands
  0..count-1; a wrong assumption about which object the RX consumer uses would
  show "absent" on the probed object only.  P6 covers band 1.

Cross-checks used: the captured stock H2D families (`wifi-f1-from-capture`)
and the 20 stock-only g0-id1 keys (`wifi-smac-start`) are runtime H2D
handling; none of them appears in this chain (no message dispatch in 0xD1B68 /
0xC016C; the body 0x47624 is a registered init task, not the H2D RX thread).
Phase43's D2H preconditions (gate 0x4000010C == 0xCECE, out[1] drained, id<=10)
are satisfied on our boot but are about the *publish* side; they do not gate
this init.

## 7. Limits

* The semantics of CA 0xC745C are [I] "register/create a task or function entry"
  (it copies a descriptor: entry at +0, a stack size word 0x1000 at +0x10, and
  calls 0xC7218).  The chain facts do not depend on it: the pointer 0x47625 is
  unambiguous and fn 0x47624 is not reachable by BL.
* The exact meaning of the gate byte 0x180810 is [U] (needs the corrected probe
  and, ideally, a stock read of the same byte).  We only prove the comparison
  and its three outcomes.
* Runtime ordering "before/after the host ladder" is [I]: the chain is
  boot-phase, but the exact scheduling instant is RTOS state we cannot see
  statically.
* No device was touched.  Every probe in PROBES.md is a read; the fire recipe is
  the Lead's attended run.
* No vendor binary bytes are committed: the tool reads the local image and the
  docs quote decoded addresses/words only.
