# sr-trigger: the device-side SR consume trigger — the vendor's ETE interrupt/notification enable, the field-by-field live diff, and the chip that still will not take the frame (phase 21, 2026-10-02)

Task `st_01a0fbc2`. Direct sequel to `docs/phase21/sr-sibling.md`, which made the sibling's SR
rings and pump correct (`SR+0x18 <= 0x00000410` on all three channels, `pumped=96`,
`irq_taken=23813/10275`) yet saw the chip still refuse the frame: `out[0]` (CA `0x40039010`) never
cleared, `SR+0x1c` never advanced, no id-1 reply. That report named the blocker as the
**device-side H2D SR consume/accept trigger**. This phase is Part A (find what tells the chip's
firmware a new SR descriptor is waiting, from `hi5622v100_plat.ko` and `FIRMWARE.bin`), Part B
(`lab/srt/srt.c` = sr2 + the candidate enable + a full field-by-field register dump, two boots),
and Part C (whether the chip consumed).

Every claim is **[proven]** (a constant/relocation/control-flow in an instruction stream, a value
the device printed, or a live read) or **[inferred]**.

---

## Headline

**Part A: no settable per-channel "notify"/"kick" register distinguishes the vendor from this
takeover.** The vendor's own boot binding writes and the firmware's own ETE bring-up are fully
enumerated (A.1/A.2), and the live field-by-field sweep (A.3) finds only three differences on the
three SR channels and the ETE interrupt block:

| register | live vendor | our post-init (boot 1) | verdict |
| --- | --- | --- | --- |
| SR `+0x08` ctrl (all 3 ch) | `0x00000000` | `0x00000001` | ours deviated from `pcie_ete_sr_reg_init` (`cfg[5]=0`); **tested in boot 2, no effect** |
| ETE intr `0x40039510` (+0x08) | `0x00000000` | `0x00000100` | firmware-set; write ignored |
| ETE intr `0x40039514` (+0x0c) | `0x09001000` | `0x00000100` (= instance-1 reset) | **write-protected** (writes read back `0x100`); a status word, not an enable |

Every other SR/DR channel field matches the vendor once the dynamic indices are allowed for:
`+0x00=1`, `+0x28=0xffff`, `+0x30` device ring base, `+0x34=0x1f`, `+0x48=1`, and the ETE
interrupt block `+0x00` (`0x40039508`) both read exactly `0x3f201818`. The `0xffe0f8f8` mask is
**not** a difference: `pcie_ete_intr_init` applies it in the vendor and in this takeover, and the
residual is identical (`0x3f3f1f1f & 0xffe0f8f8 = 0x3f201818`). **[proven]**

**Part B: two boots, neither consumes.** Boot 1 (`srctrl=1`, post-release ETE re-assert) proved the
re-assert is a **no-op** — the firmware never touches `0x40039508` (`[post0] ETE intr
0x40039508=0x3f201818`, identical to `[pre]`), so the boot-order hypothesis in A.4 is **disproven**.
Boot 2 (`srctrl=0`, the vendor's ctrl value) wrote `+0x08=0` on all three SR channels and still saw
`SR+0x1c` frozen at `0x10` for the whole 25 s run. Both boots: `pumped=96`, `out[0]` never cleared,
no id-1, `sr_events=1`. **[proven]**

**Part C: the chip did not consume. Named next blocker: the sibling's ETE SR engine never fetches
the descriptors at all.** On the sibling `SR+0x1c` stays `0x10` after the pump commits `0x410`,
whereas phase 20f on endpoint 0 saw the engine advance it (`0x10 -> 0x400`). The register windows,
the pump, the interrupt path and the firmware's own bring-up are all live and match the vendor;
what is missing is downstream of the register file — the device-side ETE descriptor fetch /
firmware interrupt context — and the only live status word that differs (`0x40039514 = 0x09001000`
vs our reset `0x100`) is **read-only**, so it cannot be the host-writable trigger. **[proven
observation, inferred cause]**

---

## Part A — what tells the firmware a new SR descriptor is waiting

### A.0 Sources

Static (all under `build/register-dumps/teardown/` unless noted):

| file | md5 | use |
| --- | --- | --- |
| `hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | host-side ETE/interrupt code |
| `build/register-dumps/bringup/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` | device firmware (runtime = file + 0x40000) |

Disassembly is this repo's `lab/ko_disasm.py` (capstone ARM) and a Thumb-2 linear sweep of the
blob. Live (normal vendor boot, read-only): `build/register-dumps/srt/001_live_vendor_regs.txt`
(devmem through the sibling's own BAR0 region-3 viewport, `host = 0x583b8000 + CA - 0x40000000`),
plus `build/register-dumps/livebind/021_live_state_vendor.txt` and `dumps/reg_all.txt` for
cross-check. Our takeover values are the `[pre]`/`[post0]` field dumps the new module emits.

### A.1 The ETE channel block, and who writes each field

Each of the seven channels is a `0x50`-byte block (`0x400/0x450/0x4a0` SR, `0x590/0x5e0/0x630/0x680`
DR). The live vendor block reads (SR0 = CA `0x4003a400`):

```
+00=00000001 +04=00000000 +08=00000000 +0c=00000000
+10=8380A000 +14=0000001F +18=00000418 +1c=00000418
+20=00000000 +24=00000000 +28=0000FFFF +2c=00000000
+30=01060750 +34=0000001F +38=00000418 +3c=00000418
+40=00000000 +44=00000000 +48=00000001 +4c=00000000
```

The host programs the `+0x10` group for SR (`pcie_ete_sr_reg_init` @`0x14a48`) and the `+0x30`
group for DR (`pcie_ete_dr_reg_init` @`0x1483c`); the firmware programs the counterpart group and
the channel enable. The two candidates in the block are:

* **`+0x00`** — the firmware sets it to 1 itself (A.2). `[post0]` reads `+00=1`.
* **`+0x08`** — the host writes it. `pcie_ete_sr_reg_init` @`0x14ae4`:

```
0x014ae4: ldr  r1, [r4, #8]        ; channel cfg entry (12 bytes, .rodata+0x101c)
0x014ae8: ldr  r2, [r3, #8]        ; old SR+0x08
0x014aec: ldrb r1, [r1, #5]        ; cfg[+5]
0x014af0: bfi  r2, r1, #0, #3      ; SR+0x08[2:0] = cfg[+5]
0x014af4: str  r2, [r3, #8]
```

The channel-cfg table `.rodata+0x101c` (also `dump_chn_cfg_table.txt`) holds `{block, 0x06800020, 1}`
per entry, so byte `+5` is `0x00` — **the vendor programs ctrl = 0**, and the live vendor reads 0.
`lab/sr2` wrote 1 (a deviation). This is the single clean per-channel field difference and is the
one Part B boot 2 tests. **[proven]**

### A.2 The vendor's boot binding and the firmware's own ETE bring-up

The only device-side writes the vendor host makes are the two kprobe-captured binding writes
(`build/register-dumps/livebind/011_bootprobe_trace.txt`): ETE interrupt block CA `0x40039508`
`&= 0xffe0f8f8` (`pcie_ete_intr_init` @`0x7528`) and glue channel-resource CA `0x400392e8`
`&= 0xfffffc20` (`pcie_ete_chn_res` @`0x7490`). Both are already reproduced by sr2/srt and match
the live vendor (`0x3f201818` / `0x00000020`). the two binding writes and the SR/DR ring-program writes (`lb_sr_base/depth/wptr/ctrl`,
`lb_dr_base/depth/prod`). There is no *additional* vendor write to the ETE/channel windows. **[proven]**

The firmware's own `pcie_msg_init` (file `0x9334`) then builds the device's ETE state. Its SR loop
(file `0x937e..0x94a4`) programs `+0x10/+0x14/+0x18/+0x08` and **enables the channel itself**:

```
0x0948a: ldr.w r3, [r3, #0x31c]    ; channel register block VA
0x0948e: cbz   r3, #0x9498
0x09490: ldr   r2, [r3]
0x09492: orr   r2, r2, #1
0x09496: str   r2, [r3]            ; SR+0x00 |= 1
```

and the DR loop does the same at `0x9560..0x9568`. Later in the same function the firmware also
reconfigures the ETE interrupt block (`[r4+0xc]`, the field `pcie_ete_intr_init` stores the
`0x40039508` VA in on the host side):

```
0x096b0: ldr  r3, [r4, #0xc]
0x096b4: ldr  r2, [r3]
0x096b8: bfi  r2, r7, #0xc, #1     ; clear bit 12
0x096bc: str  r2, [r3]
0x096c0: ...                        ; clear bit 29
0x096f6: ldr  r3, [pc, #0xd0]      ; -> file 0x97c8 = 0xe0e0f8f8
0x096f8: ldr  r1, [r2]
0x096fa: ands r3, r1
0x096fc: str  r3, [r2]              ; *ETE_intr &= 0xe0e0f8f8
```

So the firmware's own mask is `0xe0e0f8f8`, not the host's `0xffe0f8f8`. But the live vendor reads
`0x3f201818`, which still has bits 24-28 set — impossible for a last-word `& 0xe0e0f8f8` (it would
clear them to `0x00200818`). Therefore the host's write is the last one in the vendor, i.e. the
firmware does **not** leave its mask value in `0x40039508` on a working boot. **[proven]** Boot 1
confirms the same on our takeover (A.4). The firmware's H2D dispatcher that would clear `out[0]`
is at file `0x818a8` (`str r1(=0), [r2]` with `r2 = ctx+4 = out[0]`); it is present and correct but
never entered. **[proven, from `docs/phase20/fw-accept.md` A.2]**

### A.3 The live field-by-field comparison — the difference is the answer

Full vendor sweep: `build/register-dumps/srt/001_live_vendor_regs.txt`. Our post-init sweep:
`build/register-dumps/srt/043_boot1_decisive.txt` (`[post0] blk …`, one line per channel, 20
words each). Diff on the three SR channels (boot 1, `srctrl=1`), field by field:

| field | vendor ch0/ch1/ch2 | ours ch0/ch1/ch2 (boot 1) | note |
| --- | --- | --- | --- |
| `+00` enable | `1`/`1`/`1` | `1`/`1`/`1` | firmware-set, matches |
| `+04` | `0` | `0` | matches |
| **`+08` ctrl** | `0`/`0`/`0` | **`1`/`1`/`1`** | **the only clean difference**; boot 2 sets 0 |
| `+0c` | `0` | `0` | matches |
| `+10` base | host ring devva | host ring devva | matches (different boot: different DMA) |
| `+14` depth-1 | `0x1f` | `0x1f` | matches |
| `+18`/`+1c` | index pair | index pair | dynamic; vendor idle `==`, ours committed |
| `+20`/`+24` | `0` | `0` | matches |
| `+28` | `0xffff` | `0xffff` | matches |
| `+2c` | `0` | `0` | matches |
| `+30` device ring base | `01060750`/`01060650`/`01060550` | same | firmware-set, matches |
| `+34` | `0x1f` | `0x1f` | matches |
| `+38`/`+3c` | index pair | index pair | dynamic |
| `+40`..`+4c` | `0`,`0`,`1`,`0` | `0`,`0`,`1`,`0` | matches |

DR channels (`0x590..0x680`) match on every field including `+08=0`. The ETE interrupt block:

| CA | vendor | ours post (boot 1) | instance-1 reset (`0x40039d08+`) | note |
| --- | --- | --- | --- | --- |
| `0x40039508` (+0x00) | `0x3f201818` | `0x3f201818` | `0x3f3f1f1f` | matches; host `& 0xffe0f8f8` applied |
| `0x4003950c` (+0x04) | `0x00000000` | `0x00000000` | `0` | matches |
| `0x40039510` (+0x08) | `0x00000000` | `0x00000100` | `0` | firmware-set; write ignored |
| `0x40039514` (+0x0c) | `0x09001000` | `0x00000100` | `0x100` | **write-protected status**, not a settable enable |

`0x40039514` was probed live in the takeover (`053_boot2_live_e14_test.txt`): writing
`0x09001000` read back `0x00000100`, and a following id-3 doorbell (`out[0]|=8`, `out[2]=1`) left
`out[0]` at `0x28` and `SR+0x1c` at `0x10`. It is a read-only status word. **[proven]** Therefore,
of the three differences, only SR `+0x08` is a host-writable per-channel field — and Part B boot 2
shows writing the vendor value there does not move the gate.

### A.4 The boot-order hypothesis, tested and disproven

The vendor boots the firmware before its host driver's `pcie_ete_init` (`dmesg`: `wlan_power_on`
`13.0828`, `pcie_main_init` `13.0864`), so a natural hypothesis was that the host's `0x40039508`
write lands last only in the vendor, and that this takeover's post-release firmware boot
overwrites it. Boot 1 tested exactly that with a post-release re-assert of the quoted value:

```
[post0 +450ms] ETE intr 0x40039508 post-release pre=0x3f201818 mode=1 <= 0x3f201818
[post0] ETE intr 0x40039508=0x3f201818 ... (vendor target 0x3f201818)
```

The pre value was already the vendor value, so the firmware does not touch the register in this
takeover either. The hypothesis is **disproven**, and `0x40039508` (including the `0xffe0f8f8`
mask) is not the missing trigger. **[proven]**

### A.5 Proven vs inferred

| claim | status |
| --- | --- |
| SR ctrl `+0x08` is host-written from `cfg[+5]`, which is 0 in `.rodata+0x101c`; vendor reads 0 | **proven** (`0x14ae4..0x14af4`, table dump, live read) |
| firmware sets channel `+0x00 |= 1` and the `+0x48=1` state itself | **proven** (file `0x9490`, `0x9562`; live `[post0]`) |
| firmware's own ETE-intr mask is `0xe0e0f8f8` (literal file `0x97c8`), differing from the host's `0xffe0f8f8` | **proven** |
| the firmware does not leave its mask value in `0x40039508` (host write last) | **proven** (live `0x3f201818`; boot-1 pre==vendor) |
| `0x40039514` is write-protected (write ignored) | **proven** (live write/readback) |
| no vendor host write to the ETE/channel windows beyond the two binding writes and the ring programs | **proven** (kprobe capture, phase-21 live-binding) |
| the firmware H2D dispatcher at file `0x818a8` clears `out[0]` but is never entered | **proven** routine; entry path (`docs/phase20/fw-accept.md` A.4) **inferred** |
| the missing trigger is downstream of the exposed register file (device ETE fetch / firmware interrupt context) | **inferred** |

---

## Part B — `lab/srt/srt.c`, recovery, staging, two boots

### B.1 The module and the one new element

`lab/srt/srt.c` is `lab/sr2/sr2.c` plus:

1. `omo_dump_full(tag)` — a read-only, one-line-per-channel dump of all seven channel blocks
   (`+00`..`+4c`) and the ETE interrupt block words (`0x40039508/+0c/+10/+14`), emitted at
   `[pre]` (end of `omo_ete_program`) and `[post0]` (after release). This is the field-by-field
   evidence for A.3.
2. `omo_ete_intr_reassert(tag)` — the Part-A candidate (post-release ETE-intr write; `intr` mode
   0=read-only, 1=vendor target `0x3f201818`, 2=OR, 3=raw reset).
3. `omo_srctrl` — the SR `+0x08` low-3 value, applied in both `omo_ete_program` and
   `omo_ete_resync_sr` (default 1 = sr2 behaviour; boot 2 passes 0 = the vendor value).

`enable=0` is retained: the module never writes CA `0x400392f0` (`out[5]`, the phase-20 hang
family) and never touches the RC misc window `0x10161000`. The only unproven-but-quoted write is
`PCI_INTERRUPT_LINE = 209` (unchanged from sr2). **[proven]**

Built by CI (`build-load-test-module.yml`, `workflow_dispatch`, new `lab/srt` step + `srt-ko`
artifact), branch `omo/phase21-srt`:

| boot | commit | `srt.ko` md5 | size | vermagic |
| --- | --- | --- | --- | --- |
| 1 | `735c058` (run `36985840051`) | `0b791cdde8e2c53a75ef20b93fda9b1f` | 58240 | `5.10.201 SMP mod_unload ARMv7` |
| 2 | `17fd8fa` (run `36986574217`) | `15feb85fdc3e17b94e36db4dfa34ce01` | 58592 | `5.10.201 SMP mod_unload ARMv7` |

### B.2 Device-side self-recovery (armed before staging, re-armed inside each takeover boot)

`/root/recover-srt.sh` (md5 `fb362e967073dbdfdb440aac889dc348`) restores both `.omo-off` modules,
removes the loader, its `rc.d` symlink, the staged module, the `/tmp` copies and itself, then
reboots. It was armed in the vendor boot with
`start-stop-daemon -S -b -m -p /tmp/omo-srt.timer.pid -x /bin/sh -- /root/recover-srt.sh --watch`
(pid `15454`, `ALIVE`; `000_arm_recovery.txt`), and the loader now re-arms the same watchdog
**inside** the takeover boot before `insmod` — verified alive both boots (pid `7535` boot 1,
`7501` boot 2; `040_first_contact.txt`, `051_boot2_decisive.txt`). A second, shorter
`WATCH=300` watchdog (pid `10667`) was armed before the live `0x40039514` probe. Cancelled with
`touch /tmp/omo-srt.done` followed by `/root/recover-srt.sh` (`060_recovery_run.txt`). The
recovery plan was recorded in `000_recovery_plan.txt` before the reboot. **[proven]**

### B.3 Staging

`030_staging.txt` / `050_staging_boot2.txt`: `srt.ko` staged in `/lib/modules/5.10.201/`,
`/etc/init.d/omo-srt` + `S99omo-srt`, `/root/recover-srt.sh`, both vendor modules hidden as
`.omo-off` (364660 B / 3564728 B); loader and recovery `sh -n` OK.

### B.4 Boot 1 — `srctrl=1`, `intr=1` (post-release ETE re-assert)

`042_boot1_full_dmesg.txt` (1317 lines), decisive in `043_boot1_decisive.txt`:

```
[pre]  ETE intr 0x40039508=0x3f201818 +0c=0x00000000 +10=0x00000000 +14=0x00000000
[post0+450ms] ETE intr 0x40039508 post-release pre=0x3f201818 mode=1 <= 0x3f201818
[post0] ETE intr 0x40039508=0x3f201818 +0c=0x00000000 +10=0x00000100 +14=0x00000100
[post0] blk 0x400 +00=1 +04=0 +08=1 +0c=0 +10=8370b000 +14=1f +18=4 +1c=10 +20=0 +24=0
                  +28=ffff +2c=0 +30=01060750 +34=1f +38=4 +3c=4 +40=0 +44=0 +48=1 +4c=0
[pump svc] SR ch0 refilled 32 node(s) ... commit SR+0x18 <= 0x00000410 readback=0x00000410
[svc] SR ch0 wptr(+0x18)=0x00000410 rptr(+0x1c)=0x00000010 base(+0x10)=0x8370b000
done (... irq_taken=12565 irq_handled=2 msgs=4 services=4 sendflag=1 dr_events=4 sr_events=1 pumped=96)
```

`out[0]` was never cleared by the device (`H2D MASK CLEARED BY DEVICE` count = 0), `SR+0x1c`
stayed `0x10` for the remaining 24 s, no id-1 device reply, no `0x5a5a` payload. The ETE
re-assert was a value-identical no-op. **[proven]**

### B.5 Boot 2 — `srctrl=0`, `intr=0` (the vendor SR ctrl value)

`051_boot2_decisive.txt`:

```
[post0] blk 0x400 +00=1 +04=0 +08=0 +0c=0 +10=8377c000 +14=1f +18=4 +1c=10 +20=0 +24=0
                  +28=ffff +2c=0 +30=01060750 +34=1f +38=4 +3c=4 +40=0 +44=0 +48=1 +4c=0
[post0+790ms] ETE intr 0x40039508 mode=0 read-only pre=0x3f201818
[pump svc] SR ch0/ch1/ch2 refilled 32 node(s) ... commit SR+0x18 <= 0x00000410
[svc] SR ch0 wptr(+0x18)=0x00000410 rptr(+0x1c)=0x00000010
done (... irq=209 irq_taken=23133 irq_handled=2 msgs=3 services=3 sendflag=1 dr_events=4 sr_events=1 pumped=96)
```

SR `+0x08` now matches the vendor (`0`), the pump still commits the full lap on all three
channels, and the chip still does not fetch (`SR+0x1c` stays `0x10`). **[proven]**

### B.6 Live `0x40039514` probe (no reboot)

`053_boot2_live_e14_test.txt` (userspace `devmem`, sibling BAR0): write `0x40039514 = 0x09001000`
→ readback `0x00000100` (ignored); then `out[0] |= 8` and `out[2] = 1` → `out[0] = 0x28`,
`SR+0x1c = 0x10` for 9 s. The remaining difference is read-only. **[proven]**

---

## Part C — did the chip consume? named next blocker

**No.** Across both takeover boots (and the live probe) the chip never takes the frame:

* `out[0]` (H2D mask, CA `0x40039010`) is written only by the module's own id-3/id-5 doorbells and
  is **never cleared by the device** (`H2D MASK CLEARED BY DEVICE` count = 0 on both boots);
* `SR+0x1c` never advances past `0x10` after the pump commits `SR+0x18 = 0x00000410` — the SR
  engine does not fetch the 32 fresh descriptors on any channel;
* `out[1]` never carries bit 0 (id-1); the `ID-1 READY!` line in each log is our own posted frame
  decoded before the resync, not a device reply; no `0x5a5a` buffer appears; `sendflag=1`,
  `sr_events=1`, `dr_events=4` (the initial lap only).

**What this phase rules out.** The named candidates from the task are all now measured:

1. **Per-channel control bits in the SR channel block** — `+0x00` is firmware-set and matches; the
   only host-writable difference was `+0x08`, and boot 2 wrote the vendor value `0` with **no
   effect**. The vendor's boot binding makes no other channel write.
2. **The ETE interrupt block `0x40039508` and the `0xffe0f8f8` mask** — the vendor and this takeover
   both land on exactly `0x3f201818`; the mask is not the difference, and boot 1 proved the
   firmware does not rewrite the register after release.
3. **A per-channel notify/kick register** — none exists in the exposed channel blocks: every field
   matches the vendor (modulo the dynamic indices) or is read-only.
4. **Whether the firmware's ETE interrupt enable must be written after firmware boot** — boot 1
   disproves the ordering hypothesis for `0x40039508`; `0x40039514` is write-protected.

**Named next blocker: the sibling's ETE SR engine does not fetch the descriptors at all.** The
contrast with phase 20f is decisive: on endpoint 0 the same pump moved the device consumer
(`SR+0x1c 0x10 -> 0x400 = SR engine read`, `docs/phase20/tx-path.md`), while on the sibling
`SR+0x1c` is frozen at `0x10` even though the block, depth, base, ctrl, the packed producer commit
and the doorbell all match the vendor. Since the register file is identical and the only differing
status word is read-only, the gate is downstream of the exposed registers — the device-side ETE
descriptor fetch / the firmware's interrupt context. The concrete next experiments are: (a) a
device-side trace (kprobe on the firmware is unavailable; JTAG/ROM-monitor, or a firmware-side
probe) over the ETE descriptor fetch, and (b) re-checking whether the ETE engine's outbound fetch
is bound to function 0's iATU (where the vendor's writes land) while the sibling claims function 1 —
i.e. whether the sibling's dual-function model is valid for the *data* path, not just for the
interrupt line. `enable=1` (`out[5]`, the phase-20 hang family) was **not** touched.

---

## Recovery and health

Recovery cancelled with `/tmp/omo-srt.done` and run by hand (`060_recovery_run.txt`); the
recovered boot verifies (`080_final_health.txt`):

```
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
chip id: 0x34 version:0x00
calibration: get_2g_power_param -> [SUCC]17161605... ; get_5g_power_param -> [SUCC]...
6 interfaces: vap0 Cudy-1C73 (2g) / vap8 Cudy-1C73-5G (5g) + guest vaps
irq 209 hisi_pci_intx live (4501) ; irq 207 = 0
br-lan 192.168.10.1/24 UP
leftovers: no *.omo-off, no srt.ko, no init.d/omo-srt, no S99omo-srt, no /root/recover-srt.sh,
           no /tmp/srt.ko, no /tmp/omo-srt
pstore: no new record (blk-0 10:41, blk-1/2 14:37 unchanged)
```

Pre-existing (not from this task): the dangling `/etc/rc.d/S99omo-rtmsg` symlink and the
`/root/recover-*.sh` scripts left by earlier phase lanes are still present; they predate this
phase and were not touched.

Boots this session: two `srt` takeover boots (both real module boots, both captured) plus one
recovery boot. No panic, no chip hang; the `0x40039514` userspace probe did not disturb the chip.

## Writes per takeover boot

Boot 1: six inbound iATU viewports + one outbound + `PCI_COMMAND=7` + the 928,920-byte firmware
(read back) + the SR/DR program registers + the glue `+0x2e8` RMW + the pre-release ETE-intr RMW
`0x40039508 &= 0xffe0f8f8` + the post-release ETE re-assert (value-identical) + the post-release
SR re-assert (base/depth/wptr/ctrl=1) + the `0x5a5a` release + the host message service words +
the SR producer commits (96 nodes) + the single unproven-but-quoted `PCI_INTERRUPT_LINE <= 209`.
Boot 2: the same, with `intr=0` (read-only) and SR `+0x08 <= 0` (the vendor value) in both the
program and the resync. Neither boot wrote CA `0x400392f0`; the RC misc window `0x10161000` was
never touched.

## Artifacts

```
lab/srt/srt.c                            the module (sr2 + full field dump + intr/srctrl triggers)
lab/srt/Makefile, omo-srt                build + one-shot loader (re-arms recovery in-boot)
lab/srt/stage-srt.sh, recover-srt.sh     staging + self-recovery/watchdog
build/tmp/phase21/srt-ko/srt.ko          boot 1 module (md5 0b791cdde8e2c53a75ef20b93fda9b1f)
build/tmp/phase21/srt-ko2/srt.ko         boot 2 module (md5 15feb85fdc3e17b94e36db4dfa34ce01)
build/register-dumps/srt/000_baseline.txt        vendor baseline before staging
build/register-dumps/srt/000_arm_recovery.txt    watchdog install (start-stop-daemon)
build/register-dumps/srt/000_recovery_plan.txt   recovery recorded before the reboot
build/register-dumps/srt/001_live_vendor_regs.txt  full live vendor field sweep (Part A)
build/register-dumps/srt/020_build.txt           CI runs + md5s
build/register-dumps/srt/030_staging.txt, 050_staging_boot2.txt
build/register-dumps/srt/042_boot1_full_dmesg.txt, 043_boot1_decisive.txt
build/register-dumps/srt/051_boot2_decisive.txt, 052_boot2_inst1_probe.txt,
build/register-dumps/srt/053_boot2_live_e14_test.txt
build/register-dumps/srt/060_recovery_run.txt, 080_final_health.txt
```
