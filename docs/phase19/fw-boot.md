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

### Test record

_(filled in below after the observation boot)_

## Part C - the outcome

_(filled in below)_
