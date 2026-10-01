# release-attempts: the `0x5a5a` write to CA `0x40000108` releases the chip (phase 19b, 2026-10-01)

Task `st_01a0f7c0`. This is the direct sequel to `docs/phase19/fw-boot.md`, which proved the image
could be placed in the chip (phase 18 decode + write, read-back `diffs=0`) but observed **no** sign
of it running, and found **no proven CPU-release write** in either vendor module. This phase runs the
bounded release experiments, cheapest first, and stops at the first act that changes the chip's state.

**Headline: the very first act - the "inferred" untaken-branch write the previous lane flagged,
`bal_write(chip, 0x40000108, &0x5a5a, 4)` - changed the chip's state.** The CPU left ROM state: its
BSS was zeroed, the `dev_status_check` registers that were frozen at `0xffffffff` became live values,
`tcxo_pll_status` advanced, and the firmware wrote the HCC mailbox (`out[1]` `CA 0x40039014` `0`->`4`).
Per the lane directive the series **stops at act (a)**; no reset act was taken.

- **Part A** - experiment (a): the kick, its pre/post 30-register + ACP-SRAM observation, the verdict.
- **Part B** - the reset hunt in both vendor modules (recorded for completeness; not taken, because
  the series stopped at (a)). Quotes each candidate from the disassembly.
- **Part C** - the outcome and what it means for the vendor handshake.
- **Test record** - staging, boot A, the kick, recovery, radios.

Raw evidence: `opensource/build/register-dumps/release/` - `000_baseline.txt`, `010_staging.txt`,
`020_bootA_evidence.txt`, `021_bootA_full_dmesg.txt`, `030_kickA_observe.txt`,
`031_kickA_extended.txt`, `040_recovery_run.txt`, `041_recovery_evidence.txt`,
`042_recovery_radios.txt`. The module/scripts are `lab/fwboot/fwboot.c` (unchanged) and
`build/register-dumps/release/stage/omo-observe.sh` (the userspace observer/kick).

---

## Part A - experiment (a): the `0x5a5a` boot magic

### A.1 The act

`fw-boot.md` A.4 recovered the only boot-looking device write in `firmware_download`: a 4-byte
`0x00005a5a` to device CA `0x40000108`, on a branch the vendor does **not** take for chip 0 (the
descriptor always has a file table and count != 0, so the chunk loop runs instead). The previous lane
deliberately did not perform it.

This lane performs exactly that write. Device CA `0x40000108` lies in region 3 (`SHUANGTA_REGION_IO`),
whose host window is `BAR0+0x3b8000` = physical `0x403b8000`; CA `0x40000108 - 0x40000000 = 0x108`, so

```
host = 0x403b8000 + 0x108 = 0x403b8108
```

`bal_write(chip, addr, buf, len=4)` with `buf = 0x00005a5a` writes the four bytes `5a 5a 00 00`
little-endian. The module had already programmed that window (all six iATU viewports read back
`match=YES`) and written `FIRMWARE.bin` to `BAR0+0x6f8000` (`CA 0x01240000`, read-back `diffs=0`), so a
single userspace /dev/mem write of the same 32-bit word is byte-identical to the vendor's `bal_write`:

```
devmem 0x403b8108 32 0x5a5a
kick readback: 0x00005A5A
```

No module rebuild was needed: the existing `fwboot.ko` performs the proven claim/decode/write path, and
the one unproven 4-byte act is issued from userspace through the window that path programmed.

### A.2 Pre-observation (the chip is in ROM state)

The module's own three samples, 3 s apart, immediately after the firmware write:

```
omo-fwboot: verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES
omo-fwboot: observing 30 registers, 3 samples, 3000 ms apart (no release write)
omo-fwboot: change verdict over 3 samples: s1->s2 changed=0/30  s2->s3 changed=0/30
```

Sample S0, taken by `omo-observe.sh` immediately before the write (same 30 registers + the ACP
signature):

| register | CA | S0 (pre) |
| --- | --- | --- |
| fw BSS/stack +0x00 | `0x01322c18` | `0x40080000` |
| fw BSS/stack +0x04 | `0x01322c1c` | `0x00000025` |
| fw BSS/stack +0x08 | `0x01322c20` | `0x00001110` |
| fw BSS/stack +0x0c | `0x01322c24` | `0x00000008` |
| dcoldo_vset | `0x4000500c` | `0xffffffff` |
| pbank_code | `0x4000505c` | `0xffffffff` |
| abank_code | `0x40005060` | `0xffffffff` |
| tcxo_pll_mux_sel | `0x40101230` | `0x00000000` |
| tcxo_pll_status | `0x40101234` | `0x00000001` |
| msg out[1] | `0x40039014` | `0x00000000` |
| ACP SRAM signature (region-5 top) | `0x01417ff0` | `0xdeadbeaf` |
| ROM vector word0 / region-5 alias | `0x0` / `0x01200000` | `0xe59ff018` |
| fw image word0/word1 | `0x01240000`/`4` | `0x00046971` / `0x000c742d` |
| the six HCC mailbox regs | `0x40039010/14`, `0x400392d4`, `0x400392f0`, `0x40101414/38` | all `0x00000000` |
| pcie0_status / latch | `0x40039224` / `0x40039220` | `0x2000a230` / `0x00000008` |
| efuse_chip_id / dcoldo_efuse | `0x400002a8` / `0x400002d4` | `0x186` / `0x8108` |

The chip is frozen exactly as phase 19 recorded: BSS holds stale pre-load words, the analog/status
registers read all-ones or fixed ROM values, and every mailbox register is zero.

### A.3 Post-observation (the chip leaves ROM state)

S1 is taken immediately after the `devmem` write, S2 at +3 s, S3 at +6 s. The registers that changed:

| register | CA | S0 pre | S1 +0 s | S2 +3 s | S3 +6 s |
| --- | --- | --- | --- | --- | --- |
| fw BSS/stack +0x00 | `0x01322c18` | `0x40080000` | `0x00000000` | `0x00000000` | `0x00000000` |
| fw BSS/stack +0x04 | `0x01322c1c` | `0x00000025` | `0x00000000` | `0x00000000` | `0x00000000` |
| fw BSS/stack +0x08 | `0x01322c20` | `0x00001110` | `0x00000000` | `0x00000000` | `0x00000000` |
| fw BSS/stack +0x0c | `0x01322c24` | `0x00000008` | `0x00000000` | `0x00000000` | `0x00000000` |
| dcoldo_vset | `0x4000500c` | `0xffffffff` | `0x260d4184` | `0x260d4184` | `0x260d4184` |
| pbank_code | `0x4000505c` | `0xffffffff` | `0x00000312` | `0x00000312` | `0x00000312` |
| abank_code | `0x40005060` | `0xffffffff` | `0x0000010b` | `0x0000010b` | `0x0000010a` |
| tcxo_pll_mux_sel | `0x40101230` | `0x00000000` | `0x00000001` | `0x00000001` | `0x00000001` |
| tcxo_pll_status | `0x40101234` | `0x00000001` | `0x00000002` | `0x00000002` | `0x00000002` |
| msg out[1] | `0x40039014` | `0x00000000` | `0x00000000` | `0x00000004` | `0x00000004` |

**9 of 30 changed in the instant after the write; a 10th (the mailbox) changed within 3 s; and one
(`abank_code`) keeps moving.** An 8-sample, 4 s watch confirms the state is live and not settling:

```
t=76.6s ... msg1=0x00000004 vset=0x260D4184 pbank=0x00000312 abank=0x0000010D tcxo_mux=0x1 tcxo_st=0x2
t=80.7s ... msg1=0x00000004 vset=0x260D4184 pbank=0x00000312 abank=0x0000010B tcxo_mux=0x1 tcxo_st=0x2
t=88.8s ... msg1=0x00000004 vset=0x260D4184 pbank=0x00000312 abank=0x00000109 tcxo_mux=0x1 tcxo_st=0x2
t=92.8s ... msg1=0x00000004 vset=0x260D4184 pbank=0x00000312 abank=0x0000010A tcxo_mux=0x1 tcxo_st=0x2
t=104.9s ... msg1=0x00000004 vset=0x260D4184 pbank=0x00000312 abank=0x00000109 tcxo_mux=0x1 tcxo_st=0x2
```

Unchanged throughout: the ROM vector and its region-5 alias (`0xe59ff018` - it is ROM), the firmware
image words (`0x00046971`/`0x000c742d` - the image is intact), the ACP SRAM signature
(`CA 0x1417ff0 = 0xdeadbeaf` survives, as it did across host reboots), `efuse_chip_id`,
`dcoldo_efuse`, `pcie0_status`, `pcie0 latch`, `temp`, `lock_status`.

### A.4 Verdict

**State changed: YES.** The chip left ROM state the moment `0x5a5a` landed on `CA 0x40000108`, and the
signature is exactly the one the previous phase said to look for:

- the **firmware BSS was zeroed** (`CA 0x1322c18..c24` `0x40080000/0x25/0x1110/0x8` -> `0/0/0/0`) -
  a running image clearing its own BSS;
- the `dev_status_check` set that had been frozen at `0xffffffff` (analog bias `dcoldo_vset`,
  `pbank_code`, `abank_code`) now reads live, plausible values;
- `tcxo_pll_mux_sel` switched `0`->`1` and `tcxo_pll_status` `1`->`2` - the clock/PLL sequencer moved;
- the **HCC mailbox `out[1]` (`CA 0x40039014`) was written by the chip** (`0`->`4`), and one status
  word (`abank_code`) keeps changing - the chip is executing, not frozen.

No panic occurred, the box stayed reachable, and `/sys/fs/pstore` gained no new record (the three
files still carry their pre-test mtimes `10:41` / `10:26` / `10:34`).

**The series stops here, per the directive** ("stop the whole series at the first act that changes the
chip's state"). The reset hunt below is recorded as analysis, but no reset act was taken.

---

## Part B - the reset hunt (both vendor modules)

Because act (a) changed the state, no reset was executed. This section records the search in
`hi5622v100_plat.ko` (md5 `23660bc285393e678d5cade1c36c194b`) and `hi5622v100_wifi.ko`
(md5 `4737fcb21a1a2262a96f84d780ad8b35`) so the negative is on record for the next lane.

### B.1 PCIe bridge / secondary-bus reset (Bridge Control Register bit 6 at config `0x3e`)

**Not present.** Every `pci_write_config_word`/`pci_write_config_dword` call site in `plat.ko` was
enumerated with its register offset:

```
pcie_inbound_viewport_switch  pci_write_config_dword  off=0x900, 0x908
pcie_inbound_region_cfg       pci_write_config_dword  off=0x90c, 0x910, 0x914, 0x918, 0x91c
oal_pcie_set_inbound_by_viewport pci_write_config_word off=4
shuangta_pcie_l1ss_set        pci_write_config_dword  off=0x80, 0x158, 0x98
```

These are the recovered iATU viewport path (`0x900`/`0x908`/`0x90c..0x91c`, rev==1 twin of the membar
path), the `PCI_COMMAND=7` tail, and the L1SS probe-time programming. **Offset `0x3e` is never
written.** `hi5622v100_wifi.ko` imports **no** `pci_write_config_*` at all, so no config-space write of
any kind exists there:

```
hi5622v100_wifi.ko  import pci_write_config_word: no   pci_write_config_dword: no   pci_write_config_byte: no
```

### B.2 Device reset via config space (Function Level Reset)

**Not present.** No write sets the PCIe Device Control FLR bit (bit 15), and the only
`pci_find_capability` use in either object is a **read-only** link-capability check. `plat.ko` has one
relocation site (`.text` `0x808c`), inside an unnamed helper at `0x8064`:

```
0x00808c: bl       #0x808c   ; -> pci_find_capability
0x008090: cmp      r0, #0
0x008094: beq      #0x8114
0x008098: add      r1, r0, #4                 ; PCIe cap + 4 = Link Capabilities
0x00809c: add      r2, sp, #0xa
0x0080a0: mov      r0, r4
0x0080a4: bl       #0x80a4   ; -> pci_read_config_word
0x0080a8: subs     r4, r0, #0
0x0080ac: ldrheq   r0, [sp, #0xa]
0x0080b0: andeq    r0, r0, #7                 ; max-link-speed bits
```

It reads the link capability word and masks the speed; it writes nothing. No `pci_reset_function`,
`pci_reset_bus`, `pci_try_reset_function`, `pci_set_power_state`, `pci_save_state` or
`pci_restore_state` is imported by either module.

### B.3 GPIO / pinctrl toggles

**Not present.** The two GPIO reset entry points are literal stubs:

```
===== pcie_via_gpio_rst_device @ 0x107d4 size=8 (.text) =====
  0x0107d4: 0000a0e3 mov      r0, #0
  0x0107d8: 1eff2fe1 bx       lr
===== pcie_via_gpio_power_cfg @ 0x107f0 size=8 (.text) =====
  0x0107f0: 0000a0e3 mov      r0, #0
  0x0107f4: 1eff2fe1 bx       lr
```

`wlan_pm_h2d_gpio_level_set` is a printk only, `plat_product_gpio_test` returns the module id
`0x8b2d`, and neither module imports `gpiod_get`, `gpiod_set_value`, `devm_gpiod_get` or
`pinctrl_select_state`. `pcie_l1ss_dev_pinmux_set` writes the PCIe L1SS pinmux registers
(`CA 0x40000554` / `0x400005bc`) at probe time - a pin function select, not a chip reset.

### B.4 ACPI / PM / D-states

There is no ACPI on this ARM platform and no D-state API is imported. Power management is software:
`plat_pm_set_state` is a one-instruction `.data` store:

```
===== plat_pm_set_state @ 0xf108 size=16 (.text) =====
  0x00f108: movw     r3, #0   ; -> .LANCHOR0
  0x00f10c: movt     r3, #0
  0x00f110: str      r0, [r3, #8]
  0x00f114: bx       lr
```

`oal_pcie_shutdown` -> `dev_power_ctl` (stub) -> `plat_pm_set_state`; `wlan_power_off` is
`heartbeat_stop` + `bal_irq_disable` + `bal_close` + `dev_power_ctl`. `pcie_reinit` is a stub
(`mov r0,#0; bx lr`). No chip reset.

### B.5 A "reset message"

The only reset-looking mechanisms that exist are **host-to-firmware messages**, which by construction
cannot release a CPU that is still in ROM:

```
===== hmac_config_reset_hw @ 0x5740c (hi5622v100_wifi.ko) =====
  0x05741c: mov      r1, #0xf9
  0x057424: bl       #0x57424   ; -> hmac_config_send_event
...
===== hmac_config_reset_operate @ 0x88c54 (hi5622v100_wifi.ko) =====
  0x088e00: mov      r2, #4
  0x088e04: movw     r1, #0x1e1
  0x088e08: bl       #0x88e08   ; -> hmac_config_send_event
```

`hmac_config_reset_hw` sends config event `0xf9`; `hmac_config_reset_operate` parses three numeric
arguments and sends event `0x1e1`. Both go over HCC to a running firmware. `hcc_reset` @ `0x10da8`
is not a reset either - it returns the constant `0x8b2d` for `r0==0` (a module/status id).

### B.6 Recap of the device-CA scan (from `fw-boot.md` A.5)

Every `movw`/`movt` device-range constant in every `STT_FUNC` of both objects was already enumerated:
`plat.ko` yields only the known probe/IO/mailbox registers
(`0x40000000`, `0x40000554`, `0x400005bc`, `0x40002210`, `0x40030000`, `0x40039010`, `0x40039014`,
`0x40039220/d0`, `0x400392d4`, `0x400392f0`, `0x40039a20/ad0`, `0x40101414`, `0x40101438`); `wifi.ko`
yields only read/dump ranges. **There is no CPU-release/reset register write in either object.** The
only boot-looking write is the untaken-branch `0x5a5a` of A.4 - and it is now proven to be the
release.

### B.7 Reset-hunt conclusion

No vendor host-side chip-reset constant exists to test. The reset candidates the brief lists are all
absent or stubs; the only reset paths are firmware messages that presuppose a running CPU. The series
therefore has exactly one testable "release" act, `0x5a5a -> CA 0x40000108`, and it worked.

---

## Part C - the outcome

1. **The `0x5a5a` magic is the chip's release/boot trigger.** Writing `0x00005a5a` to device CA
   `0x40000108` after the firmware is placed makes the chip leave ROM state; the firmware BSS is
   zeroed, the analog/clock status registers go live, and the chip writes its first HCC mailbox word.
2. This is the write the vendor's `firmware_download` would issue on its "no file table / zero count"
   branch - the branch it does not take for chip 0. The previous lane's inference was correct.
3. **With the CPU released, the vendor handshake in `fw-boot.md` A.3 becomes applicable**: the next
   step after the download is `bal_irq_enable` (a host-side `enable_irq` that the takeover boot has no
   handler for) and then two bounded waits for the firmware's "device plat ready" and "host ready"
   messages. The mailbox `out[1] = 0x4` observed here is the first sign of the chip talking back.
4. The ACP SRAM signature `CA 0x1417ff0 = 0xdeadbeaf` survived the release and the whole session -
   confirming the earlier note that the chip's ACP SRAM persists and is a stable thing to watch.

---

## Test record

**Boots: 1 takeover/observation/kick boot + 1 recovery boot.** One unproven-but-quoted act per boot
(the `0x5a5a` write), as required.

### Baseline (live router, vendor stack loaded, no reboot - `000_baseline.txt`)

```
Thu Oct  1 13:31:04 UTC 2026   up 11 min
hi5622v100_plat  323584  3 hi5622v100_wifi
hi5622v100_wifi 3387392  1
md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
phy0/phy1; 6 wlan interfaces; chipid 0x34 version 0x00
pstore blk-0/2/3 mtimes 10:41 / 10:26 / 10:34 (all pre-test)
```

### Staging (`010_staging.txt`)

Vendor modules renamed `.ko.omo-off`; `fwboot.ko` (md5 `69a35629fe92695dbe7758117eda7e7a`, matches the
artifact) installed; one-shot loader `/etc/init.d/omo-fwboot` symlinked `S99`; `/root/recover-fwboot.sh`
and `/root/omo-observe.sh` installed. All shell scripts pass `sh -n`:

```
loader-syntax-ok / recover-syntax-ok / observe-syntax-ok
69a35629fe92695dbe7758117eda7e7a  /lib/modules/5.10.201/fwboot.ko
```

### Boot A - takeover, decode, firmware write (`020_bootA_evidence.txt`, `021_bootA_full_dmesg.txt`)

Claim + six iATU viewports all `match=YES`; `PCI_COMMAND=7` reads back `0x0006`; the window goes
`0xffffffff` -> our data; the firmware write verifies `file=928920 bytes diffs=0 match=YES`; the
module's 3 pre-kick samples report `changed=0/30` twice. No panic; pstore unchanged. Then
`omo-observe.sh kickA 0x40000108 0x5a5a` performed the act and the pre/post sampling
(`030_kickA_observe.txt`), followed by the extended watch (`031_kickA_extended.txt`).

### Recovery (`040_recovery_run.txt`, `041_recovery_evidence.txt`, `042_recovery_radios.txt`)

`sh /root/recover-fwboot.sh` renamed the modules back, removed the loader/module/symlink/tmp copies,
`sync`, `reboot`. Recovered boot:

```
hi5622v100_plat  323584  3 hi5622v100_wifi
hi5622v100_wifi 3387392  1
md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
0000:00:00.0 and 0001:00:00.0 bound to rox_pci0
phy0 Band 1 + phy1 Band 2; 6 wlan interfaces; hostapd + softapd running
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
br-lan 192.168.10.1/24 up
leftovers (fwboot.ko, .omo-off, loader, symlink, /root/omo-observe.sh, /tmp copies): all absent
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored, both radios answering.**

### Risk notes for the acts taken

- **The six iATU viewport writes + `PCI_COMMAND=7` + the 928,920-byte firmware write** - the
  phase-18-proven path, reproduced here; every write read back, `diffs=0`. Taken.
- **The single 4-byte `0x5a5a -> CA 0x40000108` write** - the one unproven-but-quoted act of the boot,
  inside the already-decoded region-3 window, read back `0x00005A5A`. It changed the chip's state; the
  box did not panic and the bus did not lock. Taken once.
- **The observation reads** - reads only, the same windows as phase 18/19. Taken.
- **No reset, no config-space write, no GPIO toggle.** The series stopped at act (a).
- **Recovery** - vendor modules restored and both radios verified after a single reboot; pstore
  baseline unchanged.
