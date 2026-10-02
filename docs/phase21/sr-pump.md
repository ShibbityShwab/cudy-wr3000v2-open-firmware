# sr-pump: the vendor SR producer pump, the phase-bit fix, and whether the chip takes our frame (phase 21, 2026-10-02)

Task `st_01a0fb7b`. Direct sequel to `docs/phase21/service-thread.md`, which stood the vendor
service worker up as a kthread and named the next blocker: *the thread only re-rang the id-3
doorbell; it never performed the vendor's SR descriptor fill + producer-index commit from the
thread.* This phase implements exactly that producer side (`lab/srpump`, commit `e128e7c`) and fixes
its one real defect — `pcie_ete_ring_ptr_plus` masking off the phase bit so a full-lap refill
committed the *same* index — in commit `ad5f9d4`, then boots the fixed module and measures whether
the chip now takes the frame.

**Headline.** The fix is real and observable: on the takeover boot the SR producer **advances across
the phase boundary** (`SR ch0 commit SR+0x18 <= 0x00000000`, readback `0x00000000`) where the
pre-fix build committed the identical `0x00000400` forever. **The chip still does not take the
frame.** Over the full 25 s service run `out[0]` (CA `0x40039010`, the H2D mask) was **never cleared
by the device** (`H2D MASK CLEARED BY DEVICE` count = 0), `out[1]` never carried bit 0 (the id-1
"Device plat ready!" reply), no `0x5a5a` HCC buffer appeared, no payload byte changed, and
`glue_clears = 0`. The SR engine read the first descriptor lap (ch0 device index `0x10 -> 0x400`,
all 32 nodes; ch1/ch2 at `0x10`, 16 nodes) and then **stopped consuming**; with all three rings full
(32 outstanding) the pump correctly had nothing to refill, so its one advancing commit is the whole
story. The gate is downstream of the descriptor fill and the phase fix does not move it.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a value
the device printed/measured) or **[inferred]**.

Artifacts (regenerable / captured):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_ete_sending_trigger pcie_ete_ring_ptr_plus \
    shuangta_ete_sr_dscr_fill pcie_ete_sr_reg_init pcie_msg_send \
    > build/tmp/phase21/srpump/sr_tx.txt
$PY lab/ko_disasm.py $KO pcie_intr_handle oal_pcie_transfer_done pcie_ete_transfer_done_handle \
    > build/tmp/phase21/srpump/glue.txt
```

Live run evidence (this task):

```
build/register-dumps/srpump/000_arm_recovery.txt     watchdog install (start-stop-daemon)
build/register-dumps/srpump/010_staging.txt          staging (fixed srpump.ko md5 b027d67a…)
build/register-dumps/srpump/020_testboot_cmd.txt     the loader as booted
build/register-dumps/srpump/030_testboot_evidence.txt
build/register-dumps/srpump/031_testboot_live.txt    dmesg | grep omo-srpump (828 lines)
build/register-dumps/srpump/032_testboot_full.txt    full dmesg
build/register-dumps/srpump/033_decisive.txt         the decisive lines, extracted
build/register-dumps/srpump/060_recovery_run.txt
build/register-dumps/srpump/070_recovery_evidence.txt
build/register-dumps/srpump/031_testboot_live_prefix.txt   (dead lane, PRE-fix build)
build/register-dumps/srpump/032_testboot_full_prefix.txt   (dead lane, PRE-fix build)
```

---

## Part A — the vendor SR transmit sequence, from `pcie_ete_sending_trigger` @`0x13f90`

### A.0 The pieces

`pcie_ete_sending_trigger` @`0x13f90` is the per-queue SR producer the vendor's service thread calls
(`pcie_thread_handle` @`0x16b8c`). It computes how many descriptors are outstanding, caps the fill at
the ring depth, and either drives the callback path (alloc skb, fill, send) or, when the ring is
already fed, walks the packed producer/consumer indices forward and commits them. The node format
itself is applied by `shuangta_ete_sr_dscr_fill` @`0x17858`, the descriptor-index arithmetic by
`pcie_ete_ring_ptr_plus` @`0x13ef8`, the register commit by `pcie_ete_sr_reg_init` @`0x14a48`, and
the doorbell by `pcie_msg_send` @`0x160f4`. **[proven]** for the calls/addresses; sizes from
`.symtab`.

### A.1 `pcie_ete_ring_ptr_plus` @`0x13ef8` — the packed index walk (the bug)

The ring index is 10 bits of index plus a phase bit at bit 10; the walk must **keep** the phase bit
while incrementing. The vendor:

```
  0x013ef8: ldr  r3, [r0]              ; r3 = *idx  (packed index|phase)
  0x013efc: add  r2, r3, #1            ; r2 = idx+1   (phase carry NOT masked)
  0x013f00: bfi  r3, r2, #0, #0xa      ; r3[9:0] = (idx+1)[9:0]   <-- phase bit kept
  0x013f04: ubfx r2, r3, #0, #0xa      ; index only
  0x013f08: cmp  r2, r1                ; depth?
  0x013f0c: bfceq r3, #0, #0xa         ; wrap index to 0
  0x013f10: ubfxeq r2, r3, #0xa, #1    ; phase
  0x013f14: eoreq r2, r2, #1           ; toggle
  0x013f18: bfieq r3, r2, #0xa, #1     ; put it back
  0x013f1c: str  r3, [r0]              ; *idx = packed result
  0x013f20: bx   lr
```

The previous implementation discarded the phase on the first step
(`next = (idx + 1) & 0x3ff`), so a full-lap refill starting from `0x400` returned `0x400` again and
the device saw no new nodes. The fixed implementation keeps `idx & ~0x3ff` through the increment, so
`0x400 + 32` wraps to index 0 with the phase toggled to **0** (`0x000`). **[proven]** — the boot in
Part B shows both values live. The same packed walk appears inline in
`pcie_ete_sending_trigger` at `0x014210..0x01423c` (producer) and `0x01424c..0x014274` (peer):

```
  0x014210: ldr  r3, [r4, #0x1c]       ; producer index (packed)
  0x014214: ldr  r2, [r4, #0x18]       ; ring object (depth at +4)
  0x014218: add  r1, r3, #1
  0x01421c: bfi  r3, r1, #0, #0xa      ; producer[9:0]++
  0x014220: ldrb r1, [r2, #4]          ; depth
  0x014224: ubfx r0, r3, #0, #0xa
  0x014228: cmp  r0, r1
  0x01422c: bfceq r3, #0, #0xa         ; wrap
  0x014230: ubfxeq r1, r3, #0xa, #1    ; phase
  0x014234: eoreq r1, r1, #1           ; toggle
  0x014238: bfieq r3, r1, #0xa, #1
  0x01423c: str  r3, [r4, #0x1c]       ; commit producer
  0x01424c: ldr  r3, [r4, #0x28]       ; peer/consumer index (packed), same walk
  ...
  0x014274: str  r3, [r4, #0x28]
```

### A.2 `pcie_ete_sending_trigger` @`0x13f90` — outstanding, fill, commit order, "fence", doorbell

**Outstanding.** The producer and consumer packed indices are XORed and the phase bit (`0x400`)
decides the wrap direction:

```
  0x014000: ldr  r3, [r4, #0x1c]       ; producer
  0x014004: eor  r3, r3, r8            ; r8 = consumer
  0x014008: tst  r3, #0x400            ; phase bits differ?
  0x01400c/10: ldrne/ldreq r3,[r4,#0x1c]
  0x01401c: ubfxne r3, r3, #0, #0xa
  0x014024: subne r3, r3, r1           ; (producer&0x3ff) - (consumer&0x3ff)
  0x01402c: addne r3, r3, r2           ;   + depth
  0x014030: subeq r3, r3, r2
```

so `outstanding = (producer - consumer) mod (2*depth)`, exactly what `omo_sr_outstanding()`
implements. **[proven]**

**Fill.** When `[r4+0x58] > outstanding` the function allocates an skb
(`__netdev_alloc_skb`, `skb_put`), stamps the outgoing header (`mvn r3,#0x5a; strb r3,[fp,#0xa]` and
`[fp,#0xb]` = `0xA5`, `0xA5` at `0x014110..0x014124`), and calls the
per-descriptor callbacks `[r5+0x18]+0x38` / `+0x3c` (get-dscr addr/len) and the fill callback. A
descriptor is recognised by the callback returning `0xd2b`:

```
  0x014058: movw r3, #0xd2b
  0x01405c: cmp  r0, r3
  0x014060: bne  #0x143c4              ; not a host descriptor -> stop/exit
```

**Commit register/order.** After the fill loop the function commits the indices into the channel
register block (`r5 = [r4+0x50]`) under the channel spinlock, **producer (+0x38) first, then the
peer (+0x18)**:

```
  0x014408: ldr  r5, [r4, #0x50]       ; channel register block
  0x014414: add  r5, r4, #0x4c         ; lock object
  0x01441c: bl   _raw_spin_lock_irqsave
  0x014420: ldr  r3, [r4, #0x50]
  0x014424: ldr  r2, [r4, #0x1c]       ; packed producer
  0x014430: str  r2, [r3, #0x38]       ; ctrl+0x38 = producer
  0x014434: ldr  r3, [r4, #0x50]
  0x014438: ldr  r2, [r4, #0x28]       ; packed peer/consumer
  0x01443c: str  r2, [r3, #0x18]       ; ctrl+0x18 = peer
  0x014448: b    _raw_spin_unlock_irqrestore
```

The register that the module quotes and commits is the SR write pointer `SR+0x18`
(`pcie_ete_sr_reg_init` @`0x14a48`, A.4). **[proven]** for the stores and their order.

**The "fence".** There is **no explicit `dsb`/`dmb`/`wmb`/`isb` anywhere in
`pcie_ete_sending_trigger`, `shuangta_ete_sr_dscr_fill`, `pcie_ete_sr_reg_init` or `pcie_msg_send`.**
The only ordering primitives are (a) the `_raw_spin_lock_irqsave` / `_raw_spin_unlock_irqrestore`
pair bracketing the two commit stores, whose unlock is a release barrier on ARM, and (b)
`iowrite32` in the takeover module (which carries its own barrier). The absence is a real finding:
whatever orders the node fill against the index commit is the driver's normal store ordering plus
the lock, not a `dsb`. **[proven]** by exhaustive opcode search of the four functions.

### A.3 `shuangta_ete_sr_dscr_fill` @`0x17858` — the node and the word1 flag bits

The function stores the buffer address in `word0` and builds `word1` on the stack, then either rings
the doorbell (data branch) or just walks the index (control branch). The branch is selected by the
**5th argument** (`ldr r2,[sp,#0x18]; cmp r2,#0; beq 0x17910`):

*Data / host-fill branch* (`arg != 0`, falls through at `0x17898`):

```
  0x01789c: movw r1, #0xd2b             ; low-13 magic
  0x0178a0: orr  r2, r2, #0x4000        ; owner bit 14
  0x0178ac: orr  r2, r2, #0x2000        ; owner bit 13
  0x0178b8: bfi  r2, r1, #0, #0xd       ; word1[12:0] = 0xd2b
  0x0178cc: str  r1, [r3, r2, lsl #3]   ; node.word0 = buffer device address
  0x0178e0: str  r2, [r3, #4]           ; node.word1 = (len<<16)|0x6000|0xd2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus ; advance the packed index
  0x0178f8: bl   pcie_msg_send          ; r1 = 3 -> the SR doorbell
```
`word1 = (len << 16) | 0x6d2b` (owner bits 13 and 14 set). The length is `uxth` of the 4th argument
(`0x17868`), and the pointer is the 3rd argument written as `word0`. **[proven]**

*Control branch* (`arg == 0`, `0x17910`): the same `0xd2b` magic but the owner bits are taken from
`arg` (zero) so both are **clear** — `word1 = (len<<16) | 0x0d2b` — and the function tail-calls
`pcie_ete_ring_ptr_plus` **without** ringing `pcie_msg_send`:

```
  0x017914: bfi  r1, r2, #0xe, #1       ; bit14 = arg bit0 (0)
  0x017920: bfi  r1, r2, #0xd, #1       ; bit13 = arg bit1 (0)
  0x017930: bfi  r2, r1, #0, #0xd       ; word1[12:0] = 0xd2b
  0x01796c: b    pcie_ete_ring_ptr_plus
```

So the "host-fill case" word1 is `(len<<16)|0x6d2b`; the SR engine recognises a host-filled node by
`word1[12:0] == 0xd2b` (`pcie_ete_sending_trigger` @`0x14058` tests exactly `0xd2b`). The takeover
module posts the `0x6d2b` form, matching the node format `docs/phase20/tx-path.md` and
`docs/phase20/rx-loop.md` recovered and measured live. **[proven]** for the instructions; **[inferred]**
which vendor caller passes `arg=0` vs non-zero (the callers are not needed for the takeover).

### A.4 `pcie_ete_sr_reg_init` @`0x14a48` — the register commit

```
  0x014aac: bl   pcie_hostca_to_devva     ; host buffer CA -> device VA
  0x014ab0: str  r0, [r5, #0x10]          ; SR+0x10 = base (device VA)
  0x014ac0: ldrb r3, [r3, #4]             ; depth
  0x014ac8: sub  r3, r3, #1
  0x014acc: bfi  r1, r3, #0, #0xa         ; depth-1
  0x014ad0: str  r1, [r2, #0x14]          ; SR+0x14 = depth-1
  0x014ad8: ldr  r2, [r4, #0xc]           ; instance write pointer (packed)
  0x014adc: str  r2, [r3, #0x18]          ; SR+0x18 = producer   <-- the commit
  0x014ae0: ldr  r3, [r4, #0xdc]
  0x014ae4: ldr  r1, [r4, #8]
  0x014ae8: ldr  r2, [r3, #8]             ; SR+0x08 = ctrl
  0x014aec: ldrb r1, [r1, #5]
  0x014af0: bfi  r2, r1, #0, #3
  0x014af4: str  r2, [r3, #8]
```

`SR+0x18 = [instance+0xc]`, the packed producer. This is the register the module writes and reads
back every pump. **[proven]**

### A.5 `pcie_msg_send` @`0x160f4` — the doorbell

```
  0x016194: ldr  r2, [r6, #0x2c]          ; out[0] CA 0x40039010
  0x01619c: str  r3, [r2]                 ; out[0] = pending bitmap (1<<id)
  0x0161a4: ldr  r2, [r6, #0x34]          ; out[2] CA 0x400392d4
  0x0161a8: ldr  r3, [r2]
  0x0161ac: orr  r3, r3, #1
  0x0161b0: str  r3, [r2]                 ; out[2] |= 1  (the doorbell)
```

It is an **asynchronous** send: unlike `pcie_msg_send_irq` @`0x174a8` it does **not** write
`out[5]` (`0x400392f0`) and does **not** wait for `out[0]` to clear. The SR doorbell is `id=3`
(`shuangta_ete_sr_dscr_fill` @`0x178f8`), the reclaim doorbell `id=5`
(`pcie_ete_rcv_buff_check` @`0x15140`). **[proven]**

### A.6 The ETE glue base question (still open)

`pcie_intr_handle` @`0x82e4` reads the glue status from `[[ctx+4]] + 0x2ec` and dispatches the bits
masked by `0x3d8`; `oal_pcie_transfer_done` @`0x83e4` clears them write-1-to-clear:

```
  0x008304: ldr  r4, [r3, #0x2ec]     ; glue status word
  0x008308: dsb  sy
  0x00830c: ands r4, r4, #0x3d8       ; bits 3,4,6,7,8,9
  0x008310: beq  ret
  0x008328: rbit r5, r4; clz r5, r5   ; lowest set bit
  0x00833c: ldr  r2, [r3, #0x48]      ; handler table at ctx+0x48
  0x00834c: blx  r2
```

The base (`[ctx+4]`, the ETE private/register block) is **[inferred]**. The two reachable candidates
behave differently and neither can be confirmed from the host in a takeover:

| candidate | reach | this run / live vendor |
| --- | --- | --- |
| ETE block `0x4003a000 + 0x2ec` (= BAR0+`0x3f22ec`) | region-3 viewport | reads **0** in the takeover and in the live vendor BAR0 dump |
| SSU/PCIe glue `0x400002ec` (= BAR0+`0x3b82ec`) | region-3 viewport | reads **`0x2292`** live (`& 0x3d8 = 0x90`) — the only ever-non-zero `+0x2ec` |

`omo_glue_service` reads the ETE-block candidate and W1Cs it only when `& 0x3d8` is non-zero; it also
does a read-only probe of `0x400002ec`. In this run the ETE word was 0 every iteration
(`glue_clears = 0`); the `0x400002ec` probe changed but was never written. **The exact base remains
unresolved**; the module's `dsb`-equivalent is `iowrite32`/`ioread32` ordering. **[proven]** for the
instruction and the two measured values; **[inferred]** for which is `[ctx+4]`.

---

## Part B — `lab/srpump`, the mandatory self-recovery, and the run

### B.1 The module

`lab/srpump/srpump.c` is `lab/svc/svc.c` (the phase-21 service-thread module, kept verbatim) plus the
in-thread SR producer pump `omo_pump_sr()`. Every 100 ms iteration it: runs `omo_msg_service`
(`pcie_msg_handle` @`0x171f8`: clear `out[1]`, ack `0x40101438`, re-arm `0x40101414`, dispatch), the
glue status service, the DR/SR scans, **and then the new pump**: compute
`outstanding = (producer - consumer) mod (2*depth)` per SR channel, refill the free nodes with the
vendor's 72-byte id-1 frame (slot 1 keeps the alg `get_2g_power_param` frame), advance the packed
producer with the **fixed** `omo_ring_ptr_plus`, commit `SR+0x18`, and ring `pcie_msg_send(chip,3)`.
Every 10 iterations it re-posts the DR buffers and rings `pcie_msg_send(chip,3)` + `(chip,5)`. All
policy from `svc` is retained: `enable=0` (never touch `out[5]`), the only unproven-but-quoted write
is `PCI_INTERRUPT_LINE = 0xcf`, and the RC misc window `0x10161000` is never touched. **[proven]**
for the module; the vendor instructions it mirrors are Part A.

Fixed build (commit `ad5f9d4`) = `build/tmp/phase21/srpump-ko/srpump.ko`, 55228 bytes, md5
**`b027d67a18a56c20a83db5c81deae1e0`**, `vermagic=5.10.201` (CI run `36914082768`, head
`ad5f9d4cbff7338757f65fbed810dc511c747478`).

### B.2 The mandatory device-side self-recovery (armed before staging)

The prior lane died with the vendor modules hidden and left the router without Wi-Fi for 12 hours, so
recovery was armed first, before any `.ko` was renamed or any loader installed:

1. `/root/recover-srpump.sh` written from `lab/srpump/recover-srpump.sh`
   (md5 `3e331e7799edbb258aea6c6ce1587157`): restores both `.omo-off` files, removes the loader,
   its `rc.d` symlink, the staged module and the `/tmp` copy and itself, `sync`s and `reboot`s.
2. An independent, session-surviving device-side watchdog started. `setsid` is **not** present on
   this box (busybox applet missing — the literal `nohup setsid …` would have silently failed), so
   the equivalent detach is `start-stop-daemon`:

   ```
   start-stop-daemon -S -b -m -p /tmp/omo-srpump.timer.pid -x /bin/sh -- /root/omo-srpump-watch.sh
   ```

   `/root/omo-srpump-watch.sh` sleeps 900 s, then runs `/root/recover-srpump.sh` unless
   `/tmp/omo-srpump.done` exists. Evidence: `000_arm_recovery.txt`
   (`pid=12025 ALIVE /bin/sh /root/omo-srpump-watch.sh`), re-verified alive in a **separate** SSH
   session — it survives the shell.
3. Because the watchdog is a process in the running boot, it cannot cross the reboot into the test
   boot; it was **re-armed immediately after the test boot answered** (`031_first_contact.txt`, pid
   `6322`, `watchdog armed Thu Oct 1 18:03:36 UTC 2026`), which covers the window that actually
   matters (test boot up, vendor modules hidden, Wi-Fi down).
4. Cancelled cleanly: `touch /tmp/omo-srpump.done` then `/root/recover-srpump.sh`
   (`060_recovery_run.txt`). The watchdog script I added (`/root/omo-srpump-watch.sh`) is not covered
   by the lab recovery script and was removed by hand in the health pass; `/tmp` is cleared by the
   reboot.

**Residual gap (stated, not fixed):** if the test boot had hung the box so hard that neither SSH nor
the shell came up, the pre-staging watchdog would already be dead (it died at the reboot) and only a
physical power cycle could recover. No boot-persistent hook (`/etc/rc.local`) exists on this
firmware, and adding one would alter the normal boot; I did not. The `enable=0` design (the `out[5]`
hang family is not touched) and the previous `svc` boot both argue the box will not hang.

### B.3 Staging (`010_staging.txt`)

```
b027d67a18a56c20a83db5c81deae1e0  /lib/modules/5.10.201/srpump.ko
-rwxr-xr-x /etc/init.d/omo-srpump ; S99omo-srpump -> ../init.d/omo-srpump
-rwxr-xr-x /root/recover-srpump.sh
-rw-r--r-- /lib/modules/5.10.201/hi5622v100_plat.ko.omo-off  (364660 B, vendor)
-rw-r--r-- /lib/modules/5.10.201/hi5622v100_wifi.ko.omo-off  (3564728 B, vendor)
loader syntax: OK ; recovery syntax: OK
```

### B.4 The test boot (`020_testboot_cmd.txt`, `030…033`)

The loader (`/etc/init.d/omo-srpump`) is the one-shot takeover boot: it claims/decodes the endpoint,
programs the six inbound + one outbound iATU viewports, loads `FIRMWARE.bin`, builds the message
context and the ETE SR/DR rings, posts DR buffers, posts the vendor's id-1 SR frame, releases the
chip (`0x5a5a` -> CA `0x40000108`) and runs the service thread with the pump for 25 s
(`enable=0 svc=1 svcdur=25000 svcms=100 svcdoorbell=10`). It boots and returns; **no hang, no
panic** — `service thread exit after 170 iters`, `freed irq 207`, pstore record set unchanged
(blk-0/1/2 mtimes still `10:41`/`14:37`/`14:37`).

### B.5 Recovery and health (`060`, `070`)

Recovery restored the vendor stack and rebooted. The recovered boot verifies:

```
2 wiphys: phy0 phy1
radios: vap0 Cudy-1C73 (2g), vap8 Cudy-1C73-5G (5g), ...
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
calibration: iwpriv Hisilicon0 alg get_2g_power_param -> alg:[SUCC] 17161605 ...
             iwpriv Hisilicon0 alg get_5g_power_param -> alg:[SUCC] 00000000 ...
chip id: 0x34 version:0x00 ; HTTP OK ; br-lan 192.168.10.1/24 UP
leftovers (srpump.ko, *.omo-off, init.d/omo-srpump, rc.d/S99omo-srpump,
           /root/recover-srpump.sh, /tmp/srpump.ko, /tmp/omo-srpump, watchdog): absent
pstore: no new record
```

**Boots this session: 2** — one takeover test boot (fixed module) + one recovery boot. The
pre-fix boot that the dead lane captured (`031_testboot_live_prefix.txt`) is *not* counted; it
predates this session and never ran the fix.

---

## Part C — outcome: the fix is proven, the chip still does not take the frame

**The phase-bit fix works.** On this boot the pump's very first refill advanced the producer across
the phase boundary — exactly the transition the pre-fix build could not make:

```
[pump svc +930ms] SR ch0 refilled 32 node(s) (wptr=0x00000400 rptr=0x00000400 outstanding=0)
                  commit SR+0x18 <= 0x00000000 readback=0x00000000   (FIXED)
[pump svc +950ms] SR ch1 refilled 16 node(s) (wptr=0x00000400 rptr=0x00000010 outstanding=16)
                  commit SR+0x18 <= 0x00000410 readback=0x00000410
[pump svc +960ms] SR ch2 refilled 16 node(s) (wptr=0x00000400 rptr=0x00000010 outstanding=16)
                  commit SR+0x18 <= 0x00000410 readback=0x00000410
```

The pre-fix capture committed `SR+0x18 <= 0x00000400` forever (`031_testboot_live_prefix.txt`); the
fixed capture commits `0x00000000` for ch0 — index 0, phase toggled. **[proven]**

**The chip still does not take it.** After that single advancing refill the three rings are full
(ch0 `wptr=0x00000000 rptr=0x00000400`, ch1/ch2 `wptr=0x00000410 rptr=0x00000010`, i.e. 32
outstanding each) and the SR engine **never consumes again** for the remaining 24 s; the pump
therefore has nothing to refill (only the 3 refill lines exist). Over the whole run:

* `out[0]` CA `0x40039010` was written only by the module's own doorbells
  (`0x08` = id-3, `0x20` = id-5) and **never cleared by the device** — `H2D MASK CLEARED BY DEVICE`
  count = **0**; `pcie_msg_wait_for_clr` would have waited forever;
* `out[1]` CA `0x40039014` carried only the device's id 6 (`0x40`) and id 2 (`0x04`), consumed and
  acked each time — **no bit 0, no id-1 "Device plat ready!" reply**;
* no `0x5a5a` buffer in any DR payload, no HCC message decode, no payload byte changed
  (`sr_events = 1`, `dr_events = 4`, all from the initial lap);
* `glue_clears = 0` — the ETE `+0x2ec` word read 0 every iteration, so no glue write fired.

Final line:

```
omo-srpump: done (release=1 rings=1 sr_posted=1 acpoff=0 svc=1 iters=170 glue_clears=0
             pollms=100 polldur=20000 irq=207 irq_taken=0 irq_handled=0 msgs=4 services=4
             sendflag=1 dr_events=4 sr_events=1 pumped=64)
```

**What this rules out.** The phase-bit producer bug is *not* the reason the chip ignores the frame.
The pump now posts a correct, advancing descriptor supply and the SR engine consumes the first lap,
yet the device-side HCC message service still refuses the host->device message: `out[0]` is never
taken down, exactly as in `docs/phase20/tx-path.md` and `docs/phase21/service-thread.md`. The
blocker is downstream of the descriptor fill and the producer commit.

**Named next blocker.** The remaining candidates are the device-side accept gate (the firmware's HCC
receive routine is reached only through the device PCIe glue ISR, and `pcie_intr_handle`'s base
`[ctx+4]+0x2ec` is unresolved — Part A.6) and the host-side interrupt source: in this takeover the
endpoint still has **no IRQ line** (`irq_taken = 0`, `PCI_INTERRUPT_LINE` set only by the
unproven-but-quoted `0xcf` write). Pinning the true ETE glue base (device-side trace, or resolving
`[ctx+4]`) and arming the real interrupt source are the two paths left.

### Writes per takeover boot

As `svc`/`fwaccept`: six inbound iATU viewports + one outbound viewport + `PCI_COMMAND=7` + the
928,920-byte firmware (read back) + the seven SR/DR program registers + the per-channel `+0x2e8` RMW
+ the DR base/depth/wptr + the SR base/depth/wptr/ctrl + the `0x5a5a` release + the host
`out[1]`/`out[3]`/`out[4]` service words + `out[0]`/`out[2]` for the id-3 and id-5 doorbells
(repeated every 10 iterations = 2 s). **New this phase:** the in-thread pump's `SR+0x18` producer
commits (3, one per channel — the rings then stay full) and the SR node arrays in host memory. ETE
`+0x2ec` W1C: 0 times (word read 0); `0x400002ec` probed read-only. The RC misc window
`0x10161000` was never touched; CA `0x400392f0` was never written (`enable=0`).

### Hazard note

No panic and no chip hang (`service thread exit after 170 iters`, clean `rmmod` on exit). All
measurements were made through endpoint 0's own BAR0/BAR2; the RC `misc` window was never read or
written.
