# fw-boot: the vendor's post-download sequence, and whether the chip leaves ROM state (phase 19, 2026-10-01)

Task `st_01a0f76e`. Phase 18 (`docs/phase18/inbound-map.md`) closed the first complete open path
from image to silicon: our module decoded the inbound window (BAR2 `iatu_bar1`, `0x104+0x200*i`),
wrote `FIRMWARE.bin` to `BAR0+0x6f8000` (device CA `0x01240000`) in 524288-byte chunks, and verified
the read-back byte-for-byte (`file=928920 bytes diffs=0 match=YES`). The image is in the chip. This
phase asks the next question: **what does the vendor do after the transfer to make the chip leave
ROM state and run that image, and can we observe any sign of it?**

- **Part A** - the sequence that follows the firmware transfer in the vendor's bring-up, recovered
  from the disassembly of `hi5622v100_plat.ko` (full `.symtab`, `build/register-dumps/teardown/`,
  md5 `23660bc285393e678d5cade1c36c194b`) and `hi5622v100_wifi.ko` (md5
  `4737fcb21a1a2262a96f84d780ad8b35`). Every claim marked **[proven]** (a constant, relocation or
  control-flow edge in the instruction stream) or **[inferred]** (semantics derived from structure,
  not a literal).
- **Part B** - `lab/fwboot/fwboot.c`: claim the endpoint, decode the window, write the firmware, then
  OBSERVE the state the disassembly says should change if the firmware runs. **No release write is
  performed, because Part A finds none that is proven.**
- **Test record** - the staging, the observation boot, the recovery.
- **Part C** - the outcome: whether the chip showed any sign of running our image, the exact
  observations, what remains, and the risk notes.

Raw disassembly: `build/tmp/phase19/` (`ready.txt`, `irq.txt`, `msg.txt`, `fwdl.txt`, `maininit.txt`,
`pm.txt`, `wifi_init.txt`, `cas.txt`).

---

## Part A - the vendor's post-download sequence

### A.0 Headline: **there is no proven CPU-release register write**

The task asked for "the CPU release/reset writes". The honest answer from the instructions is that
the vendor's `hi5622v100_plat.ko` and `hi5622v100_wifi.ko` contain **no register write that can be
proven to release or reset the chip CPU**. What follows the transfer is a *wake-up-and-listen*
sequence, not a *kick*:

1. the image is placed (phase 18);
2. the endpoint's host IRQ is enabled - `bal_irq_enable(0)`;
3. the driver waits for the firmware to announce itself over the HCC mailbox, with two bounded
   waits (200 jiffies, then 2000 jiffies), completed by two HCC message handlers.

There is exactly one boot-looking device write in the whole vendor download function - a 4-byte
`0x00005a5a` to device CA `0x40000108` - and it sits on a branch the vendor **does not take** for
this chip (A.4). Whether it is the chip's boot register is **[inferred]** and untested; it is
deliberately **not** written by the test boot.

### A.1 The call chain **[proven]** (all `bl`/relocation citations)

```
multi_chip_loading              @0xf2ac   ; exported; no caller inside plat.ko -> called by hi5622v100_wifi.ko
  0xf2fc: bl -> wlan_pm_open
  0xf32c: bl -> wait_for_completion_timeout(LANCHOR0+0x234, 0xc8)   ; 200 jiffies
  0xf384: bl -> wait_for_completion_timeout(LANCHOR0+0x220, 0x7d0)  ; 2000 jiffies

wlan_pm_open                    @0xe4c
  0xeac:  bl -> wlan_power_on

wlan_power_on                   @0xe46c
  0xe494: bl -> wlan_bal_init_process       ; dev_power_ctl + bal_reinit -> pcie_reinit stub (no HW)
  0xe4b0: bl -> hwifi_rf_cali_file_load
  0xe4d0: bl -> firmware_download_function  ; the transfer (phase 17/18)
  0xe5f8: bl -> bal_irq_enable              ; <-- the FIRST call after a successful download

firmware_download_function      @0xf9f8
  0xfabc: bl -> firmware_download
firmware_download               @0xf834
  0xf860: bl -> firmware_check_version
  0xf8dc: bl -> firmware_mem_try_alloc(0x80000, 0x1000, &out)
  0xf930: bl -> firmware_file_send          ; -> bal_write -> pcie_write -> memcpy_s to CA 0x01240000
```

`wlan_power_on` (disasm, `build/tmp/phase19/fwdl.txt` and `pm.txt`):

```
  0x00e4d0: feffffeb bl       #0xe4d0   ; -> firmware_download_function [R_ARM_CALL]
  0x00e4d4: 004050e2 subs     r4, r0, #0
  0x00e4d8: 0f00000a beq      #0xe51c          ; success -> 0xe51c
  ...
  0x00e51c: feffffeb bl       #0xe51c   ; -> ktime_get            (timing only)
  0x00e58c: ...                                       (stats bookkeeping)
  0x00e5f8: feffffeb bl       #0xe5f8   ; -> bal_irq_enable [R_ARM_CALL]
```

So on success the vendor's very next hardware-touching act is `bal_irq_enable`. Phase 17 already
established that the whole pre-download "power" chain (`dev_power_ctl`, `bal_reinit` -> `pcie_reinit`,
`pcie_via_gpio_*`) is a set of literal stubs (`mov r0,#0; bx lr`), so nothing else happens between
the image landing and the IRQ enable. **[proven]**

### A.2 The post-download step is host-side: `bal_irq_enable` **[proven]**

`bal_irq_enable` @0x10c78 is a bus dispatch exactly like `bal_write`:

```
  0x010c78: cmp   r0, #0                      ; r0 = port
  0x010c7c: bne   #0x10c9c                    ; non-zero -> -ENOSYS
  0x010c80: movw  r3, -> .LANCHOR1
  0x010c88: ldr   r3, [r3, #0x14]             ; port-0 bus context
  0x010c8c: ldr   r3, [r3, #0x30]             ; g_st_pcie_bus_driver+0x30 = pcie_irq_enable
  0x010c98: bx    r3
```

and `pcie_irq_enable` @0x7fb0 -> `oal_enable_pcie_irq` @0x7d5c:

```
  0x007df4: ldr   r3, [r4, #0x18]             ; pci_dev
  0x007df8: ldr   r0, [r3, #0x184]            ; pci_dev->irq
  0x007dfc: bl    -> enable_irq
```

This is a **host-side IRQ unmask** (it calls the kernel `enable_irq` on the endpoint's IRQ line). It
writes no device register. In the takeover boot there is no IRQ to enable, because we never ran the
vendor's `oal_pcie_probe_irq_init` / `do_request_irq` (phase 16 `endpoint-init.md` A.3). **[proven]**

### A.3 The boot handshake: two HCC "ready" messages, both bounded **[proven]**

`multi_chip_loading` @0xf2ac is the vendor bring-up entry that `hi5622v100_wifi.ko` calls (it has no
caller inside `plat.ko`, and it is an exported symbol). It calls `wlan_pm_open` (which downloads the
firmware and enables the IRQ) and then waits on two completions in its own `.LANCHOR0` state block
(`.LANCHOR0 = .bss+0x3a40`):

```
  0x00f2fc: bl    wlan_pm_open
  0x00f324: add   r0, r4, #0x234                 ; completion #1
  0x00f328: mov   r1, #0xc8                      ; 200 jiffies
  0x00f32c: bl    wait_for_completion_timeout
  0x00f330: cmp   r0, #0
  0x00f334: bne   #0xf358                         ; signalled -> continue
  0x00f338: ... printk(.LC74) ; bl dev_status_check ; return err   (timeout path)
  0x00f358: ldr   r3, [r4, #0x218] ; orr r3,r3,#1 ; str  ; "device seen"
  0x00f374: bl    dev_status_check
  0x00f37c: add   r0, r4, #0x220                 ; completion #2
  0x00f380: mov   r1, #0x7d0                     ; 2000 jiffies
  0x00f384: bl    wait_for_completion_timeout
  0x00f388: cmp   r0, #0 ; beq #0xf3a8             ; timeout -> printk(.LC76) ; return err
```

Those two completions are completed by two **HCC message handlers** - i.e. the handshake is a
message from the firmware, not a register the host polls:

```
device_plat_ready_msg_process   @0xeb78
  0x00eb7c: ldr   r3, [r0, #0x118]              ; skb->data
  0x00eb94: ldrb  r4, [r3, #1] ; lsrs r4, r4, #4 ; source-core nibble; must be 0
  0x00ebc8: ldr   r0, [pc, #4]  ; = .LANCHOR0+0x234
  0x00ebcc: bl    -> complete                    ; completes WAIT #1  (the 200-jiffy wait)

host_ready_msg_process          @0xebd8
  0x00ecb8: mov   r3, #0x1a8                      ; per-core record stride
  0x00ecc8: str   r4=1, [r6, r7]                  ; mark core ready
  0x00ece4: str   r1, [r6, #0x21c]                ; OR core bit into the ready bitmask
  0x00ed20: add   r0, r6, #0x220                  ; .LANCHOR0+0x220
  0x00ed24: bl    -> complete                     ; completes WAIT #2  (the 2000-jiffy wait)
```

The literal at `0xebcc` is `.LANCHOR0+0x234` (`.LANCHOR0 = 0x3a40`, `0x3a40+0x234 = 0x3c74`, which is
exactly the word at `0xebd4`), so wait #1 is the "device plat ready" message and wait #2 is the
"host ready" message. **[proven]**

The messages arrive over the HCC/ETE message path. `pcie_msg_handle` @0x171f8 walks the pending mask
and dispatches `handler[id]`; `pcie_msg_init` @0xb6e4 registers the handlers:

```
  0x00b818: movw  r2, -> pcie_dev_ready_msg_handle   ; id 1  (a 4-byte stub: `bx lr`)
  0x00b82c: bl    -> pcie_msg_register
  0x00b848: movw  r2, -> pcie_trigger_ete_sending_handle ; id 6
  0x00b878: movw  r2, -> pcie_trigger_ete_sending_handle ; id 7
  0x00b8a0: movw  r2, -> pcie_ete_transfer_done_handle  ; id 3
```

and the mailbox/doorbell registers are the six device CAs resolved by `shuangta_pcie_msg_reg_map`
@0x1b1a0 (`out[0..5]`), tabulated in `docs/phase17/ete-engine.md` A.7:

| slot | device CA | role (evidence) |
| --- | --- | --- |
| out[0] | `0x40039010` | H2D pending/message mask (`pcie_msg_send` writes it; `pcie_msg_init` zeroes it) |
| out[1] | `0x40039014` | message register 1 (zeroed by `pcie_msg_init`) |
| out[2] | `0x400392d4` | **doorbell / trigger** (`pcie_msg_send` ORs bit 0) |
| out[3] | `0x40101438` | MAC-side message register |
| out[4] | `0x40101414` | MAC-side message register |
| out[5] | `0x400392f0` | message register 5 (`pcie_msg_send_irq` writes 8) |

All six lie in region 3 (`SHUANGTA_REGION_IO`, host `0x403b8000..0x404d7fff` = BAR0+0x3b8000 ..
BAR0+0x4d7fff, device CA `0x40000000..0x4011ffff`), so through the phase-18 decode they are
`BAR0+0x3f1010`, `+0x3f1014`, `+0x3f12d4`, `+0x4b9438`, `+0x4b9414`, `+0x3f12f0`. **[proven]**

**So the "handshake" is: enable the endpoint IRQ, then wait up to 200 jiffies (2 s at HZ=100) for
the firmware's "device plat ready" message and up to 2000 jiffies (20 s at HZ=100) for its
"host ready" message.** The wall times are jiffies-dependent **[inferred]**; the jiffy counts are
**[proven]**.

### A.4 The one boot-looking device write in the download function, and why it is not taken **[proven]**

`firmware_download` @0xf834 has a second branch that writes a 4-byte magic to a device register. It
is only taken when the descriptor has no file table or a zero file count:

```
  0x00f86c: ldr   r3, [r4, #0x10]        ; desc+0x10 = file table pointer
  0x00f870: cmp   r3, #0
  0x00f874: beq   #0xf884                ; table == 0 -> the single-shot write
  0x00f878: ldr   r3, [r4, #0xc]         ; desc+0x0c = file count
  0x00f87c: cmp   r3, #0
  0x00f880: bne   #0xf8d0                ; count != 0 -> the chunk loop (our path)
  0x00f884: ...                          ; the single-shot write:
  0x00f8a0: ldr   r2, [r4, #0x18]        ; desc+0x18 = header descriptor {addr, buf, len}
  0x00f8a4: ldr   r0, [r4, #4]           ; chip handle
  0x00f8a8: ldr   r3, [r2, #8]           ; len
  0x00f8ac: ldr   r1, [r2], #4           ; addr
  0x00f8b0: bl    -> bal_write           ; bal_write(chip, addr, buf, len)
```

The descriptor is `.data+0x26a4` (`.LANCHOR1 = .data+0x269c`, `desc+0x18 = LANCHOR1 + 8 + type*0xc`),
and reading the ELF data + relocations gives:

```
  .data+0x26a4: 0x40000108      ; addr  = device CA
  .data+0x26a8: 0x00005a5a      ; buf   = the 2-byte magic, zero-extended
  .data+0x26ac: 0x00000004      ; len   = 4
```

i.e. `bal_write(chip, 0x40000108, &0x5a5a, 4)`. **But `firmware_download_function` always sets
`desc+0xc = 1` (`0xfa68: str r1=1, [r2, #0xc]`) and a non-null file table (`0xfa74`), so the vendor
takes the chunk path and never writes `0x40000108` for chip 0.** Whether `0x5a5a -> CA 0x40000108` is
the chip's boot/release register is **[inferred]**; it is the only boot-looking write in the object,
and this phase does **not** perform it (the phase-19 clause is "only the proven steps"; this one is
not proven, because the vendor does not take it).

### A.5 The negative result: a full device-CA scan of both objects **[proven]**

To be sure no release write was missed, every `movw`/`movt` immediate pair in every `STT_FUNC` of
both objects was scanned for a value in a device range (`0x40000000..0x4011ffff`,
`0x01200000..0x01417fff`, `0x02000000..0x021fffff`). `hi5622v100_plat.ko` yields exactly
(`build/tmp/phase19/cas.txt`):

```
exception_pcie1_link_down      @0x010324  CA=0x40000000
pcie_l1ss_dev_pinmux_set       @0x01b448  CA=0x40000554
pcie_l1ss_dev_pinmux_set       @0x01b498  CA=0x400005bc
pcie_main_init                 @0x000994  CA=0x40002210
shuangta_pcie_enable_remap     @0x01ae44  CA=0x40030000
shuangta_pcie_msg_reg_map      @0x01b1b4  CA=0x40039010
shuangta_pcie_msg_reg_map      @0x01b1ec  CA=0x40039014
shuangta_pcie_l1ss_{set,clear} ...        CA=0x40039220 / 0x400392d0 / 0x40039a20 / 0x40039ad0
shuangta_pcie_msg_reg_map      @0x01b210  CA=0x400392d4
shuangta_pcie_msg_reg_map      @0x01b27c  CA=0x400392f0
shuangta_pcie_msg_reg_map      @0x01b258  CA=0x40101414
shuangta_pcie_msg_reg_map      @0x01b234  CA=0x40101438
```

`hi5622v100_wifi.ko` yields only read/dump ranges (`shuangta_read_soc_to_file`, `read_all_reg_info`)
plus one MAC message field. **Neither object holds a CPU-release/boot register write.** This is a
proven negative, and it is the reason Part B does not write anything beyond the phase-18 set.

### A.6 Cross-reference of the earlier recovered init constants **[proven]**

The brief asked which of the earlier constants belong to this step:

| earlier constant | where it lives | when it runs | belongs to the post-download step? |
| --- | --- | --- | --- |
| `BAR0+0x2210 = (old & 0x3f) \| 0x180` | `pcie_main_init` @0x994, `oal_pcie_devca_to_hostva(0x40002210)` | **PCIe probe time** (`oal_pci_lres_init` -> `pcie_main_init`); a PHY voltage trim, printed as `[PCIEL]oal_pcie_set_voltage to 0.875V` | **No** - runs before the download, from the probe path our takeover does not replay |
| `BAR0+0x3a200 = 1` | `shuangta_pcie_enable_remap` @0x1ae3c, called from `pcie_main_init` @0x8dc only when the probe/device count `> 1` | **PCIe probe time**; enables the PCIe inbound address remap | **No** - same probe path, and gated on a chip count we do not have |

Both are probe-time PCIe init. They are prerequisites for the *decode* (phase 18 recovered the
equivalent membar programming and our module reproduces it), not part of the CPU release. **So
neither of the two briefed constants belongs to the handshake step; the handshake is the IRQ enable +
the two ready messages of A.3.**

### A.7 Proven vs inferred

| claim | status |
| --- | --- |
| the first call after a successful download is `bal_irq_enable(0)` | **proven** (0xe4d0..0xe5f8) |
| `bal_irq_enable` -> `pcie_irq_enable` -> `oal_enable_pcie_irq` -> `enable_irq(pci_dev->irq)` | **proven** |
| that step is host-side (no device register write) | **proven** |
| `multi_chip_loading` is the bring-up entry called by wifi.ko | **proven** (exported, no in-module caller) |
| the handshake is two bounded waits: 200 jiffies then 2000 jiffies | **proven** (0xf328/0xf380) |
| those waits are completed by the "device plat ready" and "host ready" HCC messages | **proven** (0xebcc complete = LANCHOR0+0x234; 0xed20 complete = LANCHOR0+0x220) |
| the message/doorbell CAs are `0x40039010/14`, `0x400392d4`, `0x400392f0`, `0x40101414`, `0x40101438` | **proven** (`shuangta_pcie_msg_reg_map` + `pcie_msg_send`) |
| there is **no** CPU-release/reset register write in either object | **proven negative** (full device-CA scan, A.5) |
| `0x5a5a -> CA 0x40000108` is a boot/release magic | **inferred** - and the vendor does not take that branch (A.4) |
| the wall-clock time of 200/2000 jiffies | **inferred** (HZ-dependent) |
| `BAR0+0x2210`, `BAR0+0x3a200` are probe-time, not post-download | **proven** |

**Part A conclusion.** The vendor's post-download bring-up, as recovered, is: *place the image (done
in phase 18) -> enable the endpoint IRQ -> wait for the firmware's two ready messages over the HCC
mailbox.* There is no proven CPU-release write. If the chip leaves ROM state, it is because the ROM
or a hardware boot sequencer starts it - not because the driver writes a release register.

---

## Part B - `lab/fwboot/fwboot.c` and the observation

Because Part A finds no proven release write, Part B performs **only the phase-18-proven writes** and
then observes:

1. **Claim** as `inbound.c`: `pci_get_domain_bus_and_slot`, id check, `pci_enable_device`,
   `pci_request_mem_regions`, BAR0 base from config space.
2. **Decode** the window: the six inbound viewports at `iatu_bar1 + 0x104 + 0x200*i` in the vendor's
   order, every write read back; then `PCI_COMMAND = 7`.
3. **Write** `FIRMWARE.bin` to `BAR0+0x6f8000` (device CA `0x01240000`) in 0x80000-byte chunks and
   verify the read-back - the exact path phase 18 verified byte-for-byte.
4. **Observe**: sample 30 registers `nobs` times (`obsdelay` ms apart) and report which words
   changed between samples. The set (BAR0 offset -> device CA) is:

| group | BAR0 offset | device CA | why |
| --- | --- | --- | --- |
| ROM/RAM code | `0x000000..0x00000c` | `0x0..0xc` | the ROM vector page; region 0 is BAR0+0 = CA 0 |
| region-5 alias | `0x6b8000` | `0x01200000` | the same ROM code as seen through the ACP window |
| our image | `0x6f8000`, `0x6f8004` | `0x01240000/4` | the firmware we placed (read-back proof) |
| firmware RAM | `0x7dac18..0x7dac24`, `0x8c7ff0` | `0x01322c18..`, `0x01417ff0` | BSS/stack/heap just past the 928920-byte image - a running CPU writes here |
| HCC mailbox | `0x3f1010`, `0x3f1014`, `0x3f12d4`, `0x3f12f0`, `0x4b9414`, `0x4b9438` | the six message CAs | the firmware writes these to signal ready (A.3) |
| `dev_status_check` set | `0x3b82a8`, `0x3b82d4`, `0x3bd00c`, `0x3bd05c`, `0x3bd060`, `0x3fc110`, `0x3fc208`, `0x3f1224`, `0x3f1220`, `0x4b9230`, `0x4b9234` | `efuse_chip_id`, `dcoldo_efuse`, `dcoldo_vset`, `pbank_code`, `abank_code`, `temp`, `lock_status`, `pcie0_status`, `pcie0 latch`, `tcxo_pll_mux_sel`, `tcxo_pll_status` | the 11 registers `dev_status_check` reads - chip status that changes once the chip is out of ROM |

Parameters: `domain` (0), `program` (1), `mode` (1 = observe after writing the firmware, 0 = observe
without writing), `fwpath`, `target` (0x6f8000), `chunk` (0x80000), `maxlen` (0), `obsdelay` (3000 ms),
`nobs` (3). **No release write is performed** (the vendor's IRQ enable is host-side and there is no
IRQ registered in the takeover boot; the `0x5a5a` magic is on an untaken branch).

### B.3 The build

`fwboot.ko` built by the existing GitHub Actions workflow (`build fwboot module` step added),
CI run **36871468530** (commit `250732a`), artifact `fwboot-ko`; md5
`69a35629fe92695dbe7758117eda7e7a`, `vermagic=5.10.201 SMP mod_unload ARMv7`, 17404 bytes.

---

## Test record

Artifacts in `build/register-dumps/fwboot/` (gitignored): `000_baseline.txt`, `010_staging.txt`,
`020_testboot_evidence.txt` (captured mid-run), `021_testboot_evidence2.txt` (**the full
observation**), `030_recovery_run.txt`, `040_recovery_evidence.txt`, `041_recovery_radios.txt`,
plus `stage/` (`fwboot.ko`, `omo-fwboot`, `recover-fwboot.sh`).

**Boots: 1 observation boot + 1 recovery boot.** Per the lane directive, no second (release) boot
was run, because Part A finds **no proven** release write to perform; the observation boot is the
baseline the release step must change.

### 1. Baseline (live router, vendor stack loaded, no reboot)

```
hi5622v100_plat       323584  3 hi5622v100_wifi
hi5622v100_wifi      3387392  1
  40000000-40ffffff : 0000:00:00.0
    41800000-41803fff : iatu_bar1
pstore: blk-0/2/3 mtimes 10:41 / 10:26 / 10:34  (all pre-test)
6 wlan interfaces
```

### 2. Staging (vendor hidden, module + one-shot loader installed)

The vendor modules are renamed `.ko.omo-off` (the phase-16/17/18 recipe), `fwboot.ko` is installed
at `/lib/modules/5.10.201/fwboot.ko` (md5 `69a35629fe92695dbe7758117eda7e7a`, matches the artifact),
the one-shot loader `/etc/init.d/omo-fwboot` is symlinked `S99`, and `/root/recover-fwboot.sh` is
installed. Both shell scripts pass `sh -n`:

```
+ sh -n /tmp/omo-fwboot && echo loader-syntax-ok
loader-syntax-ok
+ sh -n /tmp/recover-fwboot.sh && echo recover-syntax-ok
recover-syntax-ok
+ md5sum /lib/modules/5.10.201/fwboot.ko
69a35629fe92695dbe7758117eda7e7a  /lib/modules/5.10.201/fwboot.ko
```

The loader deletes its own rc.d symlink before `insmod` (watchdog safety: a hang resets into a
reachable boot with the vendor still hidden and no `fwboot`), then reboots.

### 3. The observation boot (`021_testboot_evidence2.txt`)

Claim and decode first - the same proven path as phase 18, every iATU write read back `match=YES`:

```
omo-fwboot: endpoint 0000:00:00.0 id 59e7:0005
omo-fwboot: pci_enable_device rc=0 command 0x0140 -> 0x0142
omo-fwboot: pci_request_mem_regions rc=0 (MEM BARs claimed)
omo-fwboot: BAR0 base=0x40000000 (config space), BAR2=0x41800000 (iatu_bar1)
omo-fwboot: before BAR0+0x6f8000 = 0xffffffff
omo-fwboot:   iatu[0x104] <= 0x80000000 readback=0x80000000 match=YES  (r0 ctrl2=ena)
... (all six viewports, all match=YES) ...
omo-fwboot: programmed 6 viewports
omo-fwboot: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
omo-fwboot: decode verdict BAR0+0x6f8000: before=0xffffffff after=0x00000000 decoded=YES
```

The firmware write, verified end to end exactly as phase 18 (note the `after=0x00000000`: the window
reads as zero words here, then our image lands in it):

```
omo-fwboot: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
omo-fwboot: writability probe BAR0+0x6f8000: wrote 0xdeadbeef read 0xdeadbeef match=YES
omo-fwboot: download 928920 bytes -> BAR0+0x6f8000 (device CA 0x01240000) in 524288-byte chunks
omo-fwboot: wrote 524288/928920 bytes @ BAR0+0x6f8000
omo-fwboot: wrote 928920/928920 bytes @ BAR0+0x778000
omo-fwboot: verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES
omo-fwboot: observing 30 registers, 3 samples, 3000 ms apart (no release write)
```

Then the three samples. **No release write was performed.** Sample 1 (t=40.5 s), sample 2
(t=43.8 s) and sample 3 (t=47.1 s) - the full 30-register table is in the evidence file; the
distinguishing rows:

| register | CA | sample1 (40.5 s) | sample2 (43.8 s) | sample3 (47.1 s) |
| --- | --- | --- | --- | --- |
| ROM vector word0 | `0x00000000` | `0xe59ff018` | `0xe59ff018` | `0xe59ff018` |
| region5 alias of CA0 w0 | `0x01200000` | `0xe59ff018` | `0xe59ff018` | `0xe59ff018` |
| fw image word0 | `0x01240000` | `0x00046971` | `0x00046971` | `0x00046971` |
| fw image word1 | `0x01240004` | `0x000c742d` | `0x000c742d` | `0x000c742d` |
| fw BSS/stack +0x00 | `0x01322c18` | `0x40080000` | `0x40080000` | `0x40080000` |
| fw BSS/stack +0x04 | `0x01322c1c` | `0x00000025` | `0x00000025` | `0x00000025` |
| msg out[0] H2D mask | `0x40039010` | `0x00000000` | `0x00000000` | `0x00000000` |
| msg out[1] | `0x40039014` | `0x00000000` | `0x00000000` | `0x00000000` |
| msg out[2] doorbell | `0x400392d4` | `0x00000000` | `0x00000000` | `0x00000000` |
| msg out[5] | `0x400392f0` | `0x00000000` | `0x00000000` | `0x00000000` |
| msg out[4] MAC-side | `0x40101414` | `0x00000000` | `0x00000000` | `0x00000000` |
| msg out[3] MAC-side | `0x40101438` | `0x00000000` | `0x00000000` | `0x00000000` |
| pcie0 status latch | `0x400392d0` | `0x00000005` | `0x00000005` | `0x00000005` |
| pcie0_status | `0x40039224` | `0x2000a230` | `0x2000a230` | `0x2000a230` |
| efuse_chip_id | `0x400002a8` | `0x00000186` | `0x00000186` | `0x00000186` |
| dcoldo_efuse | `0x400002d4` | `0x00008108` | `0x00008108` | `0x00008108` |
| tcxo_pll_status | `0x40101234` | `0x00000001` | `0x00000001` | `0x00000001` |
| region5 top word | `0x01417ff0` | `0xdeadbeaf` | `0xdeadbeaf` | `0xdeadbeaf` |

```
omo-fwboot: change verdict over 3 samples: s1->s2 changed=0/30  s2->s3 changed=0/30
omo-fwboot: done (mode=1 program=1 nobs=3 obsdelay=3000)
```

No panic, no bus stall, no new pstore record; the box stayed reachable throughout.

### 4. Recovery (`030_recovery_run.txt`, `040_recovery_evidence.txt`, `041_recovery_radios.txt`)

`sh /root/recover-fwboot.sh` renamed the modules back, removed the loader, the module, the symlink
and the `/tmp` copy, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat       323584  3 hi5622v100_wifi
hi5622v100_wifi      3387392  1
md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
Wiphy phy0 (Band 1) + phy1 (Band 2), 6 wlan interfaces; hostapd + softapd running
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
br-lan 192.168.10.1/24 up
leftovers (module, .omo-off, loader, /etc/init.d, symlink, /tmp): all absent
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored, both radios answering.**
The inbound decode does **not** persist across a reboot: the loader's own log shows
`before iatu viewport0 [0x100] = 0x00000000` and `before BAR0+0x6f8000 = 0xffffffff` on the fresh
boot, i.e. the iATU must be re-programmed every boot (which the module does).

---

## Part C - the outcome

### C.1 Did the chip show signs of running our loaded firmware? **No.**

The image is in the chip and provably so - `verify target BAR0+0x6f8000: file=928920 bytes diffs=0
match=YES` - and the decode is live (`BAR0+0x000000 = 0xe59ff018` is the chip's ROM/RAM code, the
region-5 alias of CA 0 matches it). But **nothing changed over the 6 seconds of observation**: all
30 sampled words were byte-identical across the three samples (`changed=0/30`, twice).

Every register that the disassembly says would announce a running firmware is silent:

- the **HCC mailbox** - `out[0]` `0x40039010` (the H2D pending mask), `out[1]` `0x40039014`, the
  **doorbell** `out[2]` `0x400392d4`, `out[5]` `0x400392f0` and the two MAC-side registers
  `0x40101414` / `0x40101438` - read `0x00000000` in all three samples. The firmware never wrote
  the mailbox, so neither of `multi_chip_loading`'s two ready waits (A.3) would ever complete;
- the **RAM just past the image** (`CA 0x1322c18`, the BSS/stack/heap the firmware would initialise
  and use) holds fixed values (`0x40080000 0x00000025 0x00001110 0x00000008`) - not a running
  program's zeroed BSS and moving stack;
- the `dev_status_check` set (`efuse_chip_id` `0x186`, `dcoldo_efuse` `0x8108`, `pcie0_status`
  `0x2000a230`, `tcxo_pll_status` `1`, `temp` `0`, `lock_status` `0`) is frozen at the ROM-state
  values.

So the chip is in the same frozen state before and after our image lands. This is the plain
reading: **placing the image does not start the CPU.**

### C.2 The exact observations

1. The decode and the download are reproducible end to end on a fresh boot: all six iATU writes
   read back `match=YES`, `PCI_COMMAND=7` reads back `0x0006`, the window goes `0xffffffff` ->
   our data, and the read-back is `diffs=0` over all 928920 bytes.
2. The chip is **not** reset by the host reboot: `region5 top word` `CA 0x1417ff0` already held
   `0xdeadbeaf` before anything of ours wrote there (we wrote only `CA 0x1240000..0x1322c18`), and
   it survived. ACP SRAM retains state across the host reboot - useful for the next lane (a running
   firmware's BSS would persist too, so a post-release sample has a stable place to look).
3. The message/doorbell registers are readable through the decoded region 3 window
   (`BAR0+0x3f1010` etc.) and sit at zero. That is the single most direct place to see the firmware
   run, and it is now instrumented.
4. **One boot, one recovery, no panic, no pstore record, box reachable throughout.**

### C.3 What remains

The open problem is exactly one step: **nothing proven from the disassembly starts the chip CPU.**
The candidates, in order of how testable they are:

- **The message path.** The vendor's own handshake is message-driven (A.3): enable the endpoint IRQ,
  then wait for the firmware to write the mailbox. The takeover boot cannot enable that IRQ (there
  is no registered IRQ handler - `do_request_irq` is part of the vendor probe path we do not
  replay), and the chip is silent anyway, which is consistent: the firmware is not running, so there
  is nothing to answer. Any future step must first make the CPU run, then this handshake applies.
- **The `0x5a5a -> CA 0x40000108` magic (A.4).** The only boot-looking write in the vendor download
  function, on a branch the vendor does not take for chip 0. It is the cheapest next experiment
  (a single 4-byte write inside the already-decoded region 3, read back, with the full 30-register
  observation either side), but it is **inferred**, not proven, and was deliberately not performed.
- **The ROM/boot sequencer.** The chip may boot from ROM at CA 0 and need a separate hand-off we
  have not found in either `.ko`; the honest state is that neither object contains a proven release
  register, so the mechanism is either in the untaken branch, in the ROM itself, or in the
  `hi_pcie`/RC layer outside the two objects we scanned.

Radio bring-up is downstream of all of this and untouched: `hdpp_chip_init`/`hal_chip_init` in
`hi5622v100_wifi.ko` send HCC messages to a firmware that is not running, so they cannot succeed at
this stage.

### C.4 Risk notes for the writes taken

- **The six iATU viewport register writes (BAR2)** - the vendor's own probe-time programming, every
  write read back; the same class as phase 18, now on a fresh boot. Taken.
- **`PCI_COMMAND = 7`** - proven safe in phases 16/17/18 (reads back `0x0006`). Taken.
- **The writability probe (`0xdeadbeef` at `BAR0+0x6f8000`)** - immediately overwritten by the first
  firmware chunk; bounded and read back. Taken.
- **The full 928,920-byte firmware write to `BAR0+0x6f8000`** - the vendor's own download target
  through the decoded region-5 window; completed without a stall in both phase 18 and here. Taken.
- **The 30-register observation reads** - reads only (the whole decode path is read-safe: phase 18
  read the same windows with no stall); no write of any kind. Taken.
- **No release/reset write, no doorbell ring, no IRQ unmask, no write outside the documented set.**
  The `0x5a5a` magic and the vendor IRQ enable were deliberately not performed because they are not
  proven to belong to the taken path.
- **Recovery**: vendor modules restored and both radios verified after a single reboot; the pstore
  baseline is unchanged (no panic), and the iATU programming is known not to persist across the
  reboot.
