# fw-handshake: the firmware's wake-up messages (phase 19c, 2026-10-01)

Task `st_01a0f7cc`. Direct sequel to `docs/phase19/release-attempts.md`, which proved that writing
`0x5a5a` to device CA `0x40000108` releases the chip: firmware BSS zeroes, analog/status registers go
live, and within 3 s the chip writes its first HCC mailbox word (`out[1]` CA `0x40039014` = `0x4`).
This phase asks what that word and the vendor's post-download handshake actually are, and runs a
boot that releases the chip and **logs its first messages**.

- **Part A** - the two "ready" messages recovered from `hi5622v100_plat.ko` disassembly: their ids,
  payload format, what the firmware sends, what the host does/answers, and the `0x4` mailbox word.
- **Part B** - `lab/fwhs/fwhs.c`: claim, decode, load firmware, release (`0x5a5a`), then **read and
  log** the six mailbox CAs + a firmware-RAM scan window for 15 s. No handshake write, because the
  disassembly proves none exists in that path.
- **Part C** - what the firmware said, how many messages arrived, whether the handshake advanced,
  what remains for a full dialogue.
- **Test record / Recovery** - staging, two test boots, recovery, radios.

Disassembly artifacts: `build/tmp/phase19/ready.txt`, `msg.txt`, `pm.txt` (regenerate with
`lab/ko_disasm.py build/register-dumps/teardown/hi5622v100_plat.ko <func>`). The module is
`lab/fwhs/fwhs.c` (CI run `36876384013`, artifact `fwhs-ko`, md5 `be37eed8224a05dbd29a62aba803ca0e`).
Raw evidence: `build/register-dumps/fwhs/`.

Vendor objects: `hi5622v100_plat.ko` md5 `23660bc285393e678d5cade1c36c194b` (the object scanned here),
`hi5622v100_wifi.ko` md5 `e21629d226ec7de9a860a8955952d311`.

---

## Part A - the two ready messages

### A.1 Where they are registered **[proven]**

`multi_chip_loading` @ `0xf2ac` is the bring-up entry `hi5622v100_wifi.ko` calls (there is no caller
inside `plat.ko`; `host_module_init` in wifi.ko calls it at `.text+0x3e178`, then `hal_main_init`,
`hmac_main_init`, `wal_main_init`). The call chain is:

```
wifi.ko host_module_init
  oal_net_dev_res_init; oam_wifi_init
  0x03e178: bl   multi_chip_loading          ; wait for the two ready messages
  0x03e184: bl   hal_main_init
  0x03e190: bl   hmac_main_init
  0x03e19c: bl   wal_main_init
```

`multi_chip_loading` calls `wlan_pm_open` -> `wlan_power_on`, which downloads the firmware and then
calls `bal_irq_enable` (host-side `enable_irq`, no device register - phase 19 `fw-boot.md` A.1/A.2),
and then waits on two completions:

```
===== multi_chip_loading @ 0xf2ac =====
  0x00f2fc: bl   wlan_pm_open
  0x00f324: add  r0, r4, #0x234            ; completion #1
  0x00f328: mov  r1, #0xc8                 ; 200 jiffies
  0x00f32c: bl   wait_for_completion_timeout
  0x00f334: bne  #0xf358                   ; signalled -> continue
  0x00f338: ... .LC74 "multi_chip_loading::plat_ready timeout[0x%x]" ; dev_status_check ; return err
  0x00f358: ldr  r3,[r4,#0x218]; orr r3,#1; str r3,[r4,#0x218]   ; mark "device seen"
  0x00f374: bl   dev_status_check
  0x00f37c: add  r0, r4, #0x220            ; completion #2
  0x00f380: mov  r1, #0x7d0                ; 2000 jiffies
  0x00f384: bl   wait_for_completion_timeout
  0x00f38c: beq  #0xf3a8                   ; timeout -> .LC76 "device_ready timeout ..."
  0x00f390: bl   dev_status_check
  0x00f398: ... .LC77 "multi_chip_loading SUCC!"
```

The two completions are completed by two **HCC message handlers**, registered not through
`pcie_msg_register` (that is the PCIe-level ISR table) but through the plat.ko **HCC chip message
table**. `plat_init_bal_hcc_excp` @ `0xe380` installs it:

```
===== plat_init_bal_hcc_excp @ 0xe380 =====
  0x00e3a8: mov  r2, #5                    ; count = 5 entries
  0x00e3ac: movw r1, .LANCHOR0             ; -> .data+0x2660, the handler table
  0x00e3b0: movt r1, .LANCHOR0
  0x00e3b4: mov  r0, #4                    ; chip/group id 4
  0x00e3b8: bl   hcc_msg_register_tab_chip
```

`hcc_msg_register_tab_chip` -> `hcc_msg_register_tab_customise`, which walks an array of 12-byte
entries `{ u16 id; u16 pad; u32 handler; u32 pad }` (`ldrh r2,[entry+0]`, `ldr r2,[entry+4]`,
`ip += 0xc`). Resolving the `.data+0x2660` table with its relocations gives:

```
entry  id   handler
  0     0   (null)
  1     1   device_plat_ready_msg_process      ; reloc @ .data+0x2670
  2     2   host_ready_msg_process             ; reloc @ .data+0x267c
  3     3   (null)
  4     4   (null)
```

So the two ready messages are **HCC messages with chip-table ids 1 and 2**. **[proven]**

### A.2 What the two messages are **[proven]**

Both handlers receive the message at `*(r0+0x118)` (the RX message object / `skb->data`). The HCC
header layout the handlers read is: `byte[1]` high nibble = source core, `u16 at +4` = length,
`byte[9]` = core index, payload at `+0xc`.

**Message #1 - `device_plat_ready_msg_process` @ `0xeb78`, string `.LC52` = "Device plat ready! chip id : %d"**

```
  0x00eb7c: ldr  r3, [r0, #0x118]        ; msg object
  0x00eb80: cmp  r3, #0
  0x00eb84: movweq r4, #0x8b2d           ; (error)
  0x00eb94: ldrb r4, [r3, #1]
  0x00eb98: lsrs r4, r4, #4              ; source core = byte[1] >> 4
  0x00eba0: beq  #0xebbc                 ; core must be 0
  0x00eba4: ... .LC51 "device_plat_ready_msg_process chip_id ERROR: %d, CHIP_NUM_BUTT: %d" ; return
  0x00ebbc: ... .LC52 "Device plat ready! chip id : %d"
  0x00ebc4: bl   printk
  0x00ebc8: ldr  r0, [pc, #4]            ; = .LANCHOR0+0x234
  0x00ebcc: bl   complete                ; <-- completes WAIT #1 (the 200-jiffy wait)
```

**Message #2 - `host_ready_msg_process` @ `0xebd8`, string `.LC54` = "DEVICE READY\nhost_ready_msg_process:l_msg_len:%d,sizeof(g_dmac_to_hmac_read_msg):%d. chip_id:%u"**

```
  0x00ebf8: ldrh r6, [r5, #4]            ; l_msg_len  (u16 at +4)
  0x00ebfc: ldrb r1, [r5, #1]
  0x00ec00: sub  r8, r6, #0xc            ; payload length = len - 0xc
  0x00ec04: tst  r1, #0xf0               ; source core must be 0
  0x00ec28: ldrb r7, [r5, #9]            ; core index
  0x00ec30: sub  r6, r6, #0xd
  0x00ec48: cmp  r6, #0x62               ; payload <= 0x62 bytes
  0x00ec4c: bhi  #0xec9c                 ; .LC56 "invalid l_msg_len..."
  0x00ec60: add  r0, r6(anchor), #0x1a8  ; g_dmac_to_hmac_read_msg[core]
  0x00ec64: mov  ip, #0x64               ; per-core record stride 0x64
  0x00ec68: lsr  r3, r3, #4               ; core = byte[1] >> 4
  0x00ec6c: add  r2, r2, #0xc            ; src = msg + 0xc
  0x00ec74: mla  r0, ip, r3, r0          ; dest = record + 0x64*core
  0x00ec7c: bl   memcpy_s                ; copy the payload
  0x00ecb8: mov  r3, #0x1a8
  0x00ecc8: str  r4=1, [r6, r7]          ; mark this core ready
  0x00ecdc: lsr  r3, r3, #4
  0x00ece0: orr  r1, r1, r4, lsl r3      ; OR core bit into ready bitmask
  0x00ece4: str  r1, [r6, #0x21c]        ; LANCHOR0+0x21c = device_ready bitmap
  0x00ecf0: ... .LC57 "host_ready_msg_process::device_ready map %#x"
  0x00ed20: add  r0, r6, #0x220
  0x00ed24: bl   complete                ; <-- completes WAIT #2 (the 2000-jiffy wait)
```

So both are **device -> host** messages:

| # | id | handler string | payload | host completion |
| - | -- | -------------- | ------- | --------------- |
| 1 | 1 | "Device plat ready! chip id : %d" | header only (source core 0) | `complete(LANCHOR0+0x234)` - the 200-jiffy wait |
| 2 | 2 | "DEVICE READY" | `len-0xc` bytes (<= 0x62) copied to `g_dmac_to_hmac_read_msg[core]` (0x64-byte record) | `complete(LANCHOR0+0x220)` - the 2000-jiffy wait |

The firmware sends **#1 first** (the 200-jiffy wait is the shorter, entered first), then **#2**. The
message-content id (chip-table id 1/2) and the wire header offsets are **[proven]**; that the wire
selector is `byte[0]&0xf` / `u16@+6` per `hcc_msg_process` @ `0x1204c` is **[inferred]** (the exact
byte the firmware stamps is not exercised by the host-side tables alone).

### A.3 What the host does after each - and what it "answers" **[proven]**

**After message #1:** `multi_chip_loading` sets bit 0 in its state word (`LANCHOR0+0x218`), prints
`wlan_pm_open SUCC chip_id[%d]`, and calls `dev_status_check`. Then it enters wait #2.

**After message #2:** nothing but `dev_status_check` and `printk("multi_chip_loading SUCC!")`;
control returns to wifi.ko `host_module_init`, which brings up `hal_main_init` / `hmac_main_init` /
`wal_main_init`.

`dev_status_check` @ `0xf18c` is **read-only**: it walks an 11-entry `{name, CA}` table, resolves
each CA with `oal_pcie_devca_to_hostva`, `ioread32`s it, and prints `"%s=0x%x"` (with bitfield
extraction for `dcoldo_efuse` / `dcoldo_vset`). Every `bl` in it is `memcpy`/`strcmp`/`printk`/
`oal_pcie_devca_to_hostva`; there is no `bal_write` / `iowrite`.

**What the host writes:** the order the vendor exercises is
`enable_irq` (`bal_irq_enable`, host-side only) -> wait for #1 -> `dev_status_check` (reads) -> wait
for #2 -> `dev_status_check` (reads) -> HAL/HMAC init. **There is no host->device message in the
ready-handshake path.** The host's implicit "answer" to a received message is the PCIe message ISR
service, `pcie_msg_handle` @ `0x171f8`: it writes `1` to the ack reg (`[ctx+0xc]`), **reads then
clears the pending mask** (`r5 = *[ctx+4]; *[ctx+4] = 0`), dispatches `handler[lowest set bit]`, and
writes `1` to a re-arm reg (`[ctx+0x10]`). That path requires the vendor's `oal_pcie_probe_irq_init`
/ `do_request_irq` and the HCC/msg context, none of which exist in the boot-time takeover. So the
takeover can **observe** the mailbox but cannot service or ack it, and cannot send a handshake
message.

### A.4 The `0x4` first mailbox word **[proven convention + inferred identity]**

The six message registers are mapped by `shuangta_pcie_msg_reg_map` @ `0x1b1a0`:

| slot | device CA | role |
| ---- | --------- | ---- |
| out[0] | `0x40039010` | H2D pending/message mask (`pcie_msg_send` writes it) |
| out[1] | `0x40039014` | message register 1 - the register the chip wrote |
| out[2] | `0x400392d4` | **doorbell** (`pcie_msg_send`/`_irq` OR bit 0) |
| out[3] | `0x40101438` | MAC-side message register |
| out[4] | `0x40101414` | MAC-side message register |
| out[5] | `0x400392f0` | message register 5 |

`pcie_msg_init` @ `0xb6e4` **zeroes** `out[0]` and `out[1]` at init (`str r8,[r3]` @ `0xb738`/`0xb740`).
The register convention is a **one-bit-per-message bitmap**: `pcie_msg_handle` @ `0x172b4` finds the
lowest set bit (`rbit`/`clz`) and calls `handler[bit]`, and `pcie_msg_send_irq` @ `0x174a8` writes the
literal `8` to `out[5]`:

```
  0x0174f8: ldr  r3, [r4, #0x40]   ; out[5]
  0x0174fc: mov  r2, #8
  0x017504: str  r2, [r3]          ; 8 = bit 3 -> pcie_msg_register id 3 = pcie_ete_transfer_done_handle
```

**[proven]** bit N of a message register = message id N. Therefore **`out[1] = 0x4 = bit 2`** means
"message id 2 pending" in the vendor's numbering. **[inferred]** The chip-table id 2 is
`host_ready_msg_process` ("DEVICE READY"), so the first word the released firmware writes most likely
announces the id-2 ready message (or, less likely, is a group/ready code `4` = the chip-4 HCC table
that holds both ready handlers; or a count). The boot log below shows this is the **only** mailbox
word the firmware ever produces in the takeover, and it is written once ~1.85 s after release.

### A.5 Proven vs inferred

| claim | status |
| ----- | ------ |
| `multi_chip_loading` waits 200 then 2000 jiffies on `.LANCHOR0+0x234` / `+0x220` | **proven** (0xf324..0xf384) |
| those completions are the id-1 `device_plat_ready_msg_process` and id-2 `host_ready_msg_process` | **proven** (table `.data+0x2660` keys 1/2 + relocs + `complete` sites) |
| message #1 string "Device plat ready! chip id : %d", message #2 string "DEVICE READY" | **proven** (.LC52 / .LC54) |
| #2 copies `len-0xc` (<=0x62) bytes from `msg+0xc` into a 0x64-stride per-core record | **proven** (0xebf8..0xed24) |
| both messages require source core 0 (`byte[1]>>4 == 0`) | **proven** |
| the host does **not** send a message in the ready handshake; it enables the IRQ and reads status | **proven** (wifi.ko call order; dev_status_check is read-only; no device write in the path) |
| message registers are one-bit-per-id bitmaps (bit N = id N) | **proven** (`pcie_msg_send_irq` writes 8 for id 3; `pcie_msg_handle` dispatches lowest set bit) |
| `out[1] = 0x40039014` is the register the chip wrote and `0x4` = bit 2 | observed (`release-attempts.md`, this boot) / **proven** convention |
| `0x4` = the id-2 `host_ready` message (vs. group id 4 / a count) | **inferred** |

**Part A conclusion.** The vendor handshake is a two-message device->host exchange (`Device plat
ready!` then `DEVICE READY`), completed into two bounded waits; the host's only acts are the
host-side `bal_irq_enable` and read-only `dev_status_check` calls. There is **no host mailbox write**
to answer the first step with, so the takeover boot performs none - it can only read the chip's
mailbox.

---

## Part B - `lab/fwhs/fwhs.c`

The module reuses the phase-18/19 proven path and then does the read/log:

1. **Claim** the endpoint (`pci_enable_device`, `pci_request_mem_regions`, BAR0 base from config
   space); refuses if the vendor stack is loaded.
2. **Decode** the six inbound iATU viewports via BAR2 (`0x104 + 0x200*i`), every write read back;
   `PCI_COMMAND = 7`.
3. **Load** `FIRMWARE.bin` to `BAR0+0x6f8000` (CA `0x01240000`) in 0x80000-byte chunks, verify
   read-back.
4. **Release**: `iowrite32(0x5a5a, BAR0+0x3b8108)` (CA `0x40000108`), read back. This is the only
   device write beyond the phase-18 set; it is the phase-19b-proven release, not a new unproven act.
5. **Baseline**, then **read and log** for 15 s (500 ms polls):

   | what | CAs / BAR0 |
   | ---- | ---------- |
   | six mailbox regs | `0x40039010/14`, `0x400392d4`, `0x400392f0`, `0x40101414`, `0x40101438` |
   | 12 status regs (`dev_status_check` set + fw BSS/image) | `0x400002a8`, `0x4000500c/5c/60`, `0x40039224/20`, `0x40101230/34`, `0x01322c18/1c`, `0x01417ff0`, `0x01240000` |
   | firmware-RAM scan window | BAR0 `0x7d8000..0x838000` (CA `0x01320000..0x01380000`, 384 KiB), word-diffed against a pre-release snapshot |

   Every change is logged with a jiffy-derived `+ms` timestamp; each mailbox word is decoded as a
   pending bitmap. No handshake write is performed (Part A proves there is none).

Parameters: `domain=0 program=1 release=1 pollms=500 polldur=15000 scanbase=0x7d8000
scanlen=0x60000`. CI run `36876384013`, artifact `fwhs-ko`, md5
`be37eed8224a05dbd29a62aba803ca0e`, `vermagic=5.10.201 SMP mod_unload ARMv7`, 19872 bytes.

### B.1 The message ring

`fw-boot.md` A.3 / phase-17 `ete-engine.md` A.7 show the HCC RX path: the device DMA-writes the
message into **host-allocated memory** (an skb), and the ETE ring/queue and message context are
created by the vendor's `pcie_ete_init`/`pcie_msg_init`/`hcc_init` from a per-chip resource. With the
vendor stack hidden (boot-time takeover) there is no host message buffer, no ETE ring base, no
mapped message block and no message thread - so there is **no device-side message ring we can
read**. The only device-side handshake surface is the six mailbox registers; the module polls those
plus the firmware RAM (in case the firmware deposits a message there). That is the honest reachable
set.

---

## Part C - what the firmware said

**Boot:** one takeover/release/read boot (`fwhs.ko` at `S99`), one recovery boot. The release write
(the phase-19b-proven act) was performed once; no other unproven device write was made. No panic;
`/sys/fs/pstore` gained no record (blk-0/2/3 mtimes still `10:41`/`10:26`/`10:34`).

### C.1 The mailbox log (from `040_testboot_evidence.txt`)

Pre-release, all six mailbox registers read 0. After the release write (`0x5a5a`, readback
`0x00005a5a`):

```
[   40.713728] omo-fwhs: [post0 +360ms] STAT dcoldo_vset        CA=0x4000500c 0xffffffff -> 0x260d4184
[   40.722797] omo-fwhs: [post0 +370ms] STAT pbank_code         CA=0x4000505c 0xffffffff -> 0x00000313
[   40.731875] omo-fwhs: [post0 +380ms] STAT abank_code         CA=0x40005060 0xffffffff -> 0x000000ea
[   40.740956] omo-fwhs: [post0 +390ms] STAT tcxo_pll_mux_sel   CA=0x40101230 0x00000000 -> 0x00000001
[   40.750033] omo-fwhs: [post0 +400ms] STAT tcxo_pll_status    CA=0x40101234 0x00000001 -> 0x00000002
[   40.759102] omo-fwhs: [post0 +400ms] STAT fw BSS +0x00       CA=0x01322c18 0x40080000 -> 0x00000000
[   40.768177] omo-fwhs: [post0 +410ms] STAT fw BSS +0x04       CA=0x01322c1c 0x00000025 -> 0x00000000
[   40.777251] omo-fwhs: [post0 +420ms] SCAN CA=0x01320000 BAR0+0x7d8000 0x022af894 -> 0x00000000
...
[   41.096472] omo-fwhs: [post0 +740ms] scan window changed 98278/98304 words (shown 24)
[   42.199423] omo-fwhs: [poll +1850ms] MBOX out[1]             CA=0x40039014 0x00000000 -> 0x00000004
[   42.208462] omo-fwhs:   out[1]             = 0x00000004  bits={2}
[   42.214863] omo-fwhs: [poll +1860ms] STAT abank_code         CA=0x40005060 0x000000ea -> 0x000000e9
...
[   62.245563] omo-fwhs: done (release=1 pollms=500 polldur=15000 scanlen=393216)
```

### C.2 What the firmware said

- **How many messages arrived: exactly one mailbox word.** `out[1]` (`CA 0x40039014`) went
  `0 -> 0x4` at **+1.85 s** after the release write, decoded as **bit 2**. It never changed again
  over the remaining ~13 s; the other five mailbox registers stayed `0`.
- **The firmware is running and cleared its BSS:** the 384 KiB firmware-RAM scan window changed
  `98278/98304` words (all to zero) and then stayed identical, and the `dev_status_check` set went
  live (`dcoldo_vset 0xffffffff -> 0x260d4184`, `pbank_code -> 0x313`, `abank_code` free-running,
  `tcxo_pll_*` advanced) - the same signature phase 19b recorded.
- **The handshake did not advance.** The vendor's wait #1 wants the id-1 "Device plat ready!"
  message completed into `LANCHOR0+0x234`; the chip's single word is bit 2 (id 2 by the vendor's
  bitmap convention, `host_ready_msg_process`/`DEVICE READY`). With no host IRQ registered and no
  HCC message service, nothing consumed or acked it, no message payload was deposited, and no
  second word appeared. We observed the chip's first handshake sign, not a completed dialogue.
- **What remains for a full dialogue:** (1) register and enable the endpoint IRQ (vendor
  `oal_pcie_probe_irq_init` + `do_request_irq` / `bal_irq_enable`); (2) stand up the PCIe message
  context and HCC/ETE engine (`pcie_msg_init`, `pcie_ete_init`, `hcc_init`) so a message reader/ack
  exists and host-side DMA buffers are allocated; (3) service `pcie_msg_handle` so the pending bit is
  read/cleared/acked; then the id-1/id-2 handlers can run, `g_dmac_to_hmac_read_msg[]` gets the
  device-ready payload, and `multi_chip_loading` can complete both waits. Only then does a
  host->device message make sense.

---

## Test record

Artifacts in `build/register-dumps/fwhs/` (gitignored) + `stage/`:

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live router, vendor stack loaded, no reboot |
| `010_staging.txt` | vendor modules hidden, `fwhs.ko` + loader + recovery installed, syntax/md5 (plus the re-stage after the first build) |
| `021_attempt1_unknown_symbol.txt` | **attempt 1 failed to load**: `fwhs: Unknown symbol __aeabi_uldivmod` (a 64-bit ns division); no release write, no panic |
| `020_reboot_cmd.txt`, `030_reboot_cmd.txt` | the two test reboots |
| `040_testboot_evidence.txt` | **the full `omo-fwhs` log (540 lines)** |
| `041_full_dmesg.txt` | the boot dmesg |
| `050_post_test_state.txt` | post-test: `fwhs` loaded, pstore unchanged, box reachable |
| `060_recovery_run.txt`, `070_recovery_evidence.txt`, `080_final_health.txt` | recovery script + verified healthy state |
| `stage/` | `fwhs.ko` (md5 `be37eed8...`), `omo-fwhs`, `recover-fwhs.sh` |

**Boots: attempt-1 boot (module load failure, recovered by re-stage), test boot, recovery boot.**
The only device write on the test boot was the phase-19b-proven `0x5a5a` release; no unproven write
was made (Part A proves there is no host answer to write).

### Baseline (live, vendor stack loaded)

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b
0000:00:00.0 -> rox_pci0 ; phy0+phy1 ; 6 wlan ifaces ; br-lan 192.168.10.1/24
pstore blk-0/2/3 mtimes 10:41 / 10:26 / 10:34 (all pre-test)
```

### Staging

Vendor modules renamed `.ko.omo-off`; `fwhs.ko` (md5 `be37eed8224a05dbd29a62aba803ca0e`) installed;
`/etc/init.d/omo-fwhs` symlinked `S99` (the loader deletes its own symlink before `insmod`, so a hang
watchdog-reboots into a reachable boot with no `fwhs`); `/root/recover-fwhs.sh` installed. Both
scripts pass `sh -n`; `md5sum /lib/modules/5.10.201/fwhs.ko` matches the artifact.

### Test boot

Claim + six iATU viewports `match=YES`; `PCI_COMMAND=7` reads back `0x0006`; firmware write verifies;
release write readback `0x00005a5a`; the mailbox log above. `fwhs` stayed loaded (`16384 0`), no
panic, pstore unchanged.

### Recovery

`sh /root/recover-fwhs.sh` (staged, run from the device) renamed the modules back and removed the
module, loader, symlink and `/tmp` copy, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b   (baseline)
0000:00:00.0 and 0001:00:00.0 both bound to rox_pci0
phy0 Band 1 + phy1 Band 2 ; 6 wlan ifaces (vap0/1/11/3/8/9) ; hostapd+softapd running
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
br-lan 192.168.10.1/24 up
leftovers (fwhs.ko, .omo-off, loader, symlink, /tmp copy, /root/recover-fwhs.sh): all absent
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Risk notes

- The six iATU viewport writes + `PCI_COMMAND=7` + the 928,920-byte firmware write - the
  phase-18-proven path; every write read back.
- The `0x5a5a -> CA 0x40000108` release - the phase-19b-proven act, taken once.
- All handshake work was **reads only**; the disassembly shows no host write to answer the first
  step, so none was attempted.
- The first attempt did not load (`__aeabi_uldivmod`, a 64-bit division in the poll timestamps);
  it was fixed to jiffies arithmetic and the module rebuilt before the test boot.
- Recovery - vendor modules restored and both radios verified after a single reboot; pstore unchanged.
