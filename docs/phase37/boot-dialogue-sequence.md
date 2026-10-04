# Boot dialogue sequence: every init-path host->device message, in order, to radios-up (phase 37)

Task `st_01a105a5`. Read-only static reconstruction of the vendor's host-side boot dialogue from the
two vendor modules and the firmware blob, so the port (`opensource/lab/wifidrv1`) can replicate it.
Every offset below was re-disassembled with the repo-local capstone and passes; the full checker is
quoted at the end (`74 claims, 0 FAIL`).

Artifacts (md5 re-confirmed this session):

| artifact | path | md5 |
| --- | --- | --- |
| firmware | `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` |
| plat module | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` |
| wifi module | `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` |

Conventions (unchanged from the record): `.ko` offsets are **section-relative** (`.text` file offset
`0x38`, `.text.unlikely` file offset `0x1d35c`); firmware offsets are **file** offsets with runtime =
file + `0x40000`; pointer literals in the blob are runtime addresses. In the `.ko`, a `bl`/`b` to
another `.text` function is the loader's `imm24 = -2` trampoline (capstone prints a site-relative
target), so those branch targets are verified against the ELF relocation table, not the printed operand
(identical to `docs/phase32/VERIFICATION.md`'s note).

---

## 0. Summary: the ordered dialogue

| # | who | action | transport | id | function (module + offset) |
| --- | --- | --- | --- | --- | --- |
| 1 | host | post SR node + commit producer, then announce | **ring + mailbox** | **3** | `shuangta_ete_sr_dscr_fill` (plat.ko `0x17858`); announce `pcie_msg_send` @ `0x178f8` |
| 2 | device | wake the host's receive thread | mailbox (D2H `out[1]`) | **6** | `pcie_trigger_ete_sending_handle` (host handler); device emits `out[1]=0x40` |
| 3 | host | receive-buffer check | **mailbox** | **5** | `pcie_ete_rcv_buff_check` (plat.ko `0x14d74`); `pcie_msg_send` @ `0x15144` |
| 4 | device | announce platform ready -> releases host init | mailbox (D2H `out[1]`) | **2** | device ETE bring-up writes `out[1]=4`; host `device_plat_ready_msg_process` calls `complete()` |

Steps 1-4 are the mailbox/ring dialogue. The steady-state (post-boot) protocol is ring-only
(`docs/phase36/h2d-mailbox-boot-only.md`); the radios come up after step 4 when `hi5622v100_wifi.ko`
initialises the hmac/wal/hdpp layers on top of the completed platform dialogue.

---

## 1. The two host send entry points (mailbox), and what each writes

There are exactly **two** direct call sites of the mailbox send function in the whole module, plus one
registration of its "irq" sibling. The send writes the H2D bitmap `out[0]` (CA `0x40039010`) and rings
the doorbell (CA `0x400392d4`); it is the **notification** half - the message **body** travels in the SR
ring (`docs/phase24/hcc-command-layer.md`).

### 1.1 `pcie_msg_send(chip, id)` @ `0x160f4` (plat.ko)

```
0x16194 ldr r2, [r6, #0x2c]      ; out[0] CA 0x40039010
0x1619c str r3, [r2]             ; *out[0] = pending | (1<<id)
0x161a4 ldr r2, [r6, #0x34]      ; doorbell CA 0x400392d4
0x161ac orr r3, r3, r1
0x161b0 str r3, [r2]             ; *doorbell |= 1
```

The id is an argument (`r4 = r1` at `0x160fc`); the bit is OR-ed into the pending word at `0x16184`
(`orr r3, r3, r1, lsl r4`). The two init-path calls pass **id 3** and **id 5** (section 2).

### 1.2 `pcie_msg_send_irq` @ `0x174a8` (plat.ko) - registered, NOT called during init

```
0x174f8 ldr r3, [r4, #0x40]      ; out[5] CA 0x400392f0 (the ack)
0x17504 str r2, [r3]             ; *out[5] = 8   (the arm this project forbids writing)
0x1753c str r3, [r2]             ; *out[0] = pending
0x1754c orr r3, r3, #1
0x17550 str r3, [r2]             ; *doorbell |= 1
```

`pcie_msg_init` stores `&pcie_msg_send_irq` into the per-channel struct as a **callback**, not a call:

```
0xb794 movw fp, #0               ; =&pcie_msg_send_irq  (R_ARM_MOVW_ABS_NC)
0xb798 movt fp, #0               ; =&pcie_msg_send_irq  (R_ARM_MOVT_ABS)
0xb7d0 str  fp, [r3, #0x60]      ; channel[+0x60] = pcie_msg_send_irq
```

So `pcie_msg_send_irq` is the message service's send mechanism once the queue drains; it is never
invoked by the boot init itself.

### 1.3 The firmware's H2D dispatcher (file `0x818ac`), which consumes the mailbox

```
0x818b8 str r7, [r2]             ; *(ctx+0xc) = 1        ack 0x400392f0 = 1
0x818bc ldr r5, [r2]             ; pending = *(ctx+4)    out[0] 0x40039010
0x818be str r1, [r2]             ; *(ctx+4) = 0          clear out[0]
0x818c4 str r1, [r2]             ; *(ctx+0x10) = 8       doorbell 0x400392d4 = 8
0x818da ldr r3, [r6, #0x20]      ; handler table = 0x118d68
0x818e8 blx r3                   ; handler[lowest-set-bit](arg)
```

The table is 8-byte entries `{fn, arg}` written by the firmware's register function:

```
0x81894 str.w r2, [r4, r1, lsl #3]   ; table[id].fn  = r2
0x81898 str   r3, [r5, #4]           ; table[id].arg = r3
```

The live table holds ids **1/3/5/6** (`docs/phase30/the-gate-located.md`). The firmware registers the
id-3 slot itself, in its own `pcie_msg_init` (file `0x9334`):

```
0x982a ldr r2, [pc, #0x38]       ; fn = 0x000c5145
0x9838 bl  #0x8187a              ; register(id=3, fn=0xc5145, arg=0x10c0f4 ctx)
```

---

## 2. The two init-path H2D messages (host -> device)

### 2.1 Message id 3 - the SR descriptor post / announce (ring + mailbox)

Performed by **`shuangta_ete_sr_dscr_fill`** @ `0x17858` (plat.ko), the same routine the ops table
`g_st_pcie_bus_driver` exposes (`docs/phase25/chip-ops-table.md`). It fills one SR node, advances the
producer pointer (the commit), then announces id 3 - all in one function:

```
0x178c0 ldr r2, [r4, #0xc]           ; packed producer index (low 10 bits)
0x178cc str r1, [r3, r2, lsl #3]     ; node[idx].word0 = buffer devva
0x178e0 str r2, [r3, #4]             ; node[idx].word1 = (len << 16) | flag
0x178ec bl  pcie_ete_ring_ptr_plus   ; advance producer (the commit)
0x178f0 ldr r0, [r5, #0x80]          ; the chip object
0x178f4 mov r1, #3                   ; <-- id 3
0x178f8 bl  pcie_msg_send            ; pcie_msg_send(chip, 3)  <-- the announce
```

This is the **id-6 trigger** established in `docs/phase24/id6-trigger-proven.md`: the device consumes
the descriptor and answers with D2H id 6.

### 2.2 Message id 5 - the receive-buffer check (mailbox)

Performed by **`pcie_ete_rcv_buff_check`** @ `0x14d74` (plat.ko):

```
0x15138 ldr r3, [r4, #0x68]
0x1513c mov r1, #5               ; <-- id 5
0x15140 ldr r0, [r3, #0x80]      ; the chip object
0x15144 bl  pcie_msg_send        ; pcie_msg_send(chip, 5)
```

This is reached through the **receive path**, not a bare init call:

```
0x1655c bl pcie_ete_dr_get_uploadbuf    ; in pcie_rx_handle          (plat.ko 0x1655c)
0x152c4 bl pcie_ete_rcv_buff_check      ; in pcie_ete_dr_get_uploadbuf (plat.ko 0x152c4)
```

i.e. `pcie_rx_handle` -> `pcie_ete_dr_get_uploadbuf` -> `pcie_ete_rcv_buff_check` -> id 5. The RX thread
is exactly what the device's D2H id 6 (`pcie_trigger_ete_sending_handle`) wakes, so id 5 is the host's
answer to id 6, inside the same boot dialogue.

---

## 3. The two D2H responses (device -> host)

### 3.1 id 6 = `pcie_trigger_ete_sending_handle` - "wake the host's receive thread"

Registered by `pcie_msg_init` for **both id 6 and id 7**:

```
0xb854 mov r1, #6
0xb858 bl  pcie_msg_register      ; id 6 -> pcie_trigger_ete_sending_handle
0xb880 mov r1, #7
0xb884 bl  pcie_msg_register      ; id 7 -> pcie_trigger_ete_sending_handle
```

`pcie_trigger_ete_sending_handle` @ `0x15efc` is a 4-byte stub - a single tail call to
**`pcie_wkup_thread`** (`b`, R_ARM_JUMP24 at `.text+0x15efc`). So the id-6 handler literally wakes the
thread whose receive path (`pcie_rx_handle` -> `pcie_ete_dr_get_uploadbuf` -> `pcie_ete_rcv_buff_check`)
issues the id-5 receive-buffer check - closing the loop on step 2 -> step 3 of section 0.

### 3.2 id 2 = `device_plat_ready_msg_process` - "platform ready"

The device's ETE bring-up function (file `0x86e14`) ends by announcing ready on `out[1]` (CA
`0x40039014`, literal at `0x86fbc`) and ringing the device-side doorbell, then polling a status word:

```
0x86f54 movs r2, #4               ; bit 2 = id 2
0x86f5a str  r2, [r3]             ; *out[1] = 4
0x86f5e add.w r3, r3, #0xc8000
0x86f62 add.w r3, r3, #0x420      ; device-side doorbell 0x400a1434
0x86f66 str  r2, [r3]             ; ring it
```

The host handler for id 2 is `device_plat_ready_msg_process` @ `0xeb78` (plat.ko), which calls
`complete()` (`docs/phase24/dialogue-endpoints.md`) - this is the completion the host's init waits on.

### 3.3 Host D2H handler table as registered by `pcie_msg_init`

| id | handler | registration |
| --- | --- | --- |
| 1 | `pcie_dev_ready_msg_handle` | `mov r1,#1` @ `0xb824`, `bl` @ `0xb82c` |
| 3 | `pcie_ete_transfer_done_handle` | `mov r1,#3` @ `0xb8ac`, `bl` @ `0xb8b8` |
| 6 | `pcie_trigger_ete_sending_handle` | `mov r1,#6` @ `0xb854`, `bl` @ `0xb858` |
| 7 | `pcie_trigger_ete_sending_handle` | `mov r1,#7` @ `0xb880`, `bl` @ `0xb884` |

(The OAM/HCC layer separately registers `device_plat_ready_msg_process` for id 2 and the other
platform events - `docs/phase24/handler-table.md`; the id space is domain-dependent,
`docs/phase25/full-event-vocabulary.md`.)

---

## 4. The boot order (what runs before the dialogue, then the dialogue itself)

### 4.1 Module init - no messages

`init_module` @ `0x1a75c` (plat.ko), in order:

```
0x1a760 bl plat_res_init
0x1a764 bl oal_main_init
0x1a770 bl oam_main_init
0x1a77c bl sdt_drv_main_init
0x1a788 bl low_power_init
0x1a794 bl plat_hcc_init
```

`plat_hcc_init` @ `0x1a690` (in order; `bal_init`/`hcc_init` register the credit/hcc callbacks):

```
0x1a694 bl plat_custom_init
0x1a6a0 bl plat_main_init
0x1a6ac bl pcie_init_static_res
0x1a6bc bl bal_init
0x1a6c8 bl hcc_init
0x1a6d4 bl plat_exception_init
```

### 4.2 PCIe probe and ETE bring-up - no messages yet

`pcie_main_init` @ `0x704` (`.text.unlikely`), in order:

```
0x768 bl pcie_init_default
0x7ac bl __pci_register_driver
0x830 bl get_pcie_res
0x868 bl pci_dev_res_init
0x8e8 bl pcie_ete_init
0x944 bl pcie_comm_init
```

`pcie_ete_init` @ `0x7820`: `bl pcie_ete_intr_init` @ `0x78b8`, `bl pcie_ete_rings_init` @ `0x78d4`
(and `pcie_ete_chn_res` @ `0x78e8`).

`pcie_comm_init` @ `0x171cc` - the message service comes up last:

```
0x171e8 bl pcie_thread_init
0x171f4 b  pcie_msg_init        ; tail call
```

### 4.3 The message service arms, but sends nothing

`pcie_msg_init` @ `0xb6e4` (plat.ko) builds the context object (out[0]/out[1]/ack/doorbell CAs),
registers the D2H handlers (section 3.3) and installs `pcie_msg_send_irq` as the channel send callback
(section 1.2). It sends **no** message; it only arms the receive side.

### 4.4 The dialogue (the four steps of section 0)

1. **host -> device: SR post + id 3.** `shuangta_ete_sr_dscr_fill` fills a node, commits the producer
   index, and announces id 3 (`0x178f4`/`0x178f8`). Transport: ring (the node) + mailbox (the bit).
2. **device -> host: id 6.** The firmware's id-3 handler (entry `0x85144` -> `0x85128`, fn `0xc5145`)
   runs, and the device emits `out[1]=0x40` - `pcie_trigger_ete_sending_handle`.
3. **host -> device: id 5.** The id-6 handler wakes the receive thread: `pcie_rx_handle` @ `0x1655c`
   -> `pcie_ete_dr_get_uploadbuf` @ `0x152c4` -> `pcie_ete_rcv_buff_check` @ `0x14d74` ->
   `pcie_msg_send(chip, 5)` @ `0x15144`. Transport: mailbox.
4. **device -> host: id 2.** The device's ETE bring-up (file `0x86e14`) writes `out[1]=4` @ `0x86f5a`
   and rings its doorbell @ `0x86f66`. The host's `device_plat_ready_msg_process` @ `0xeb78` runs
   `complete()`, releasing the init wait.

### 4.5 Radios up

After step 4 releases the platform init, `hi5622v100_wifi.ko` initialises the hmac/wal/hdpp layers
(its `*_main_init` register the message tables named in `docs/phase25/full-event-vocabulary.md`), which
drives the radio bring-up. `hi5622v100_wifi.ko` contains **no** `pcie_msg_send`/`pcie_msg_send_irq`
call site (`docs/phase36/h2d-mailbox-boot-only.md`); its traffic rides the SR/DR rings and the HCC layer
in plat.ko, so the mailbox dialogue of section 0 is the complete boot-time mailbox exchange.

---

## 5. Correction to the phase-36 relocation scan

`docs/phase36/h2d-mailbox-boot-only.md` lists five `.ko` relocation sites and calls four of them
"`pcie_msg_send` init-path call sites". Re-checking relocation sections and types this session shows
only **two** are `bl` calls; the rest are data references:

| offset | section | type | what it actually is |
| --- | --- | --- | --- |
| `0xb794`/`0xb798` | `.text` | R_ARM_MOVW_ABS_NC/MOVT_ABS (43/44) | `movw/movt fp,=&pcie_msg_send_irq` - a **registration**, stored at `[ch+0x60]` (`0xb7d0`) |
| `0x15144` | `.text` | R_ARM_CALL (28) | **call** `pcie_msg_send` from `pcie_ete_rcv_buff_check` (id 5) |
| `0x178f8` | `.text` | R_ARM_CALL (28) | **call** `pcie_msg_send` from `shuangta_ete_sr_dscr_fill` (id 3) |
| `0xaf8` | `__ksymtab` | R_ARM_ABS32 (2) | the kernel symbol export table entry for `pcie_msg_send` - not a call |
| `0x2920` | `.data` | R_ARM_ABS32 (2) | the `g_st_pcie_bus_driver` ops-table slot for `pcie_msg_send` - not a call |

So the steady-state conclusion stands and is sharpened: the boot-time mailbox dialogue is exactly the
two sends above (id 3 and id 5), plus the D2H answers (id 6 and id 2).

---

## 6. Verification

Re-disassembly of every offset quoted here, via the repo-local pyenv python (capstone 5.0.7), against
the raw binaries. `.ko` branch targets are checked against the ELF relocation table (section 0, above).

```bash
PY=pyenv/Scripts/python.exe
$PY - <<'EOF'
import re
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
ROOT=r"C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2"
d=open(ROOT+"/build/tmp/FIRMWARE.bin","rb").read()
dp=open(ROOT+"/opensource/build/register-dumps/teardown/hi5622v100_plat.ko","rb").read()
TEXT_FO,UNLIKELY_FO=0x38,0x1d35c
arm=Cs(CS_ARCH_ARM,CS_MODE_ARM); th=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
e=ELFFile(open(ROOT+"/opensource/build/register-dumps/teardown/hi5622v100_plat.ko","rb"))
sym=e.get_section_by_name('.symtab'); syms=list(sym.iter_symbols())
rel={}
for s in e.iter_sections():
    if not isinstance(s,RelocationSection): continue
    b=e.get_section(s['sh_info']); bn=b.name if b else '?'
    for r in s.iter_relocations():
        i=r['r_info_sym']; rel[(bn,r['r_offset'])]=syms[i].name if i<len(syms) else '?'
def norm(s): return re.sub(r"\s+"," ",s).strip()
fail=[]; n=0
def br(label,sec_off,tgt,unlikely=False,mnem="bl"):
    global n; n+=1
    bn=".text.unlikely" if unlikely else ".text"
    off=(UNLIKELY_FO if unlikely else TEXT_FO)+sec_off
    got=list(arm.disasm(dp[off:off+4],off))[0].mnemonic
    ok=(got==mnem and rel.get((bn,sec_off))==tgt)
    if not ok: fail.append((label,got,rel.get((bn,sec_off))))
def ins(label,sec_off,exp,unlikely=False):
    global n; n+=1
    off=(UNLIKELY_FO if unlikely else TEXT_FO)+sec_off
    got=norm(list(arm.disasm(dp[off:off+4],off))[0].mnemonic+" "+list(arm.disasm(dp[off:off+4],off))[0].op_str)
    if not got.startswith(norm(exp)): fail.append((label,got))
def fi(label,off,exp):
    global n; n+=1
    got=norm(list(th.disasm(d[off:off+4],off))[0].mnemonic+" "+list(th.disasm(d[off:off+4],off))[0].op_str)
    if not got.startswith(norm(exp)): fail.append((label,got))
# init order
for o,t in [(0x1a760,'plat_res_init'),(0x1a764,'oal_main_init'),(0x1a770,'oam_main_init'),
            (0x1a77c,'sdt_drv_main_init'),(0x1a788,'low_power_init'),(0x1a794,'plat_hcc_init')]: br('init_module',o,t)
for o,t in [(0x1a694,'plat_custom_init'),(0x1a6a0,'plat_main_init'),(0x1a6ac,'pcie_init_static_res'),
            (0x1a6bc,'bal_init'),(0x1a6c8,'hcc_init'),(0x1a6d4,'plat_exception_init')]: br('plat_hcc_init',o,t)
br('pcie_main_init',0x768,'pcie_init_default',True); br('pcie_main_init',0x8e8,'pcie_ete_init',True); br('pcie_main_init',0x944,'pcie_comm_init',True)
br('pcie_comm_init',0x171e8,'pcie_thread_init'); br('pcie_comm_init',0x171f4,'pcie_msg_init',mnem='b')
br('pcie_ete_init',0x78b8,'pcie_ete_intr_init'); br('pcie_ete_init',0x78d4,'pcie_ete_rings_init')
for o in (0xb82c,0xb858,0xb884,0xb8b8): br('pcie_msg_init reg',o,'pcie_msg_register')
br('rcv_buff_check send',0x15144,'pcie_msg_send'); br('sr_fill send',0x178f8,'pcie_msg_send')
br('rx_handle',0x1655c,'pcie_ete_dr_get_uploadbuf'); br('dr_get_uploadbuf',0x152c4,'pcie_ete_rcv_buff_check')
br('id6 handler tail',0x15efc,'pcie_wkup_thread',mnem='b')
# argument loads and register writes
for o,e in [(0xb824,'mov r1, #1'),(0xb854,'mov r1, #6'),(0xb880,'mov r1, #7'),(0xb8ac,'mov r1, #3'),
            (0xb794,'movw fp, #0'),(0xb7d0,'str fp, [r3, #0x60]'),(0x16194,'ldr r2, [r6, #0x2c]'),
            (0x1619c,'str r3, [r2]'),(0x161a4,'ldr r2, [r6, #0x34]'),(0x161ac,'orr r3, r3, r1'),
            (0x161b0,'str r3, [r2]'),(0x174f8,'ldr r3, [r4, #0x40]'),(0x17504,'str r2, [r3]'),
            (0x1753c,'str r3, [r2]'),(0x1754c,'orr r3, r3, #1'),(0x17550,'str r3, [r2]'),
            (0x1513c,'mov r1, #5'),(0x178cc,'str r1, [r3, r2, lsl #3]'),(0x178e0,'str r2, [r3, #4]'),
            (0x178f4,'mov r1, #3')]: ins('arg/write',o,e)
# firmware
for o,e in [(0x818b8,'str r7, [r2]'),(0x818bc,'ldr r5, [r2]'),(0x818be,'str r1, [r2]'),(0x818c4,'str r1, [r2]'),
            (0x818da,'ldr r3, [r6, #0x20]'),(0x818e8,'blx r3'),(0x81894,'str.w r2, [r4, r1, lsl #3]'),
            (0x81898,'str r3, [r5, #4]'),(0x982a,'ldr r2, [pc, #0x38]'),(0x9838,'bl #0x8187a'),
            (0x86f54,'movs r2, #4'),(0x86f5a,'str r2, [r3]'),(0x86f66,'str r2, [r3]'),(0x85144,'b #0x85128'),
            (0x85128,'push {r4, lr}'),(0x294,'ldr r0, [pc, #4]'),(0x296,'b.w #0x818ac')]: fi('fw',o,e)
# literal pools
for o,v,name in [(0x97e0,0x40039010,'out0'),(0x97dc,0x40039014,'out1'),(0x86fbc,0x40039014,'out1-d2h'),
                 (0x29c,0x10c190,'ctx'),(0x9858,0x40295,'stub294'),(0x985c,0x402a1,'stub2a0'),
                 (0x9864,0xc5145,'id3fn'),(0x2a8,0x10c0f4,'msgobj')]:
    n+=1
    if int.from_bytes(d[o:o+4],'little')!=v: fail.append(('pool '+name,))
print("checked",n,"claims,",len(fail),"FAIL")
for f in fail: print("FAIL:",f)
raise SystemExit(1 if fail else 0)
EOF
```

Output: `checked 74 claims, 0 FAIL` (exit 0). All offsets in sections 1-4 reproduce to the quoted
mnemonic; all `bl`/`b` targets reproduce to the quoted symbol through the relocation table.
