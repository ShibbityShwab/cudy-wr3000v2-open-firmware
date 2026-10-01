# tx-path: the host->device SR/HCC transmit path (phase 20f, 2026-10-01)

Task `st_01a0f897`. Direct sequel to `docs/phase20/rx-loop.md`, which proved the complete, correct
DR receive loop and the producer-index commit, then measured a clean negative: the released firmware
never deposits a payload, writes only mailbox id 6 and id 2, and never reaches payload sending. That
report named the remaining blocker as the **host SR/HCC command path** - `pcie_msg_send`, the SR
descriptor fill, the ALG command table, and the id-1 "Device plat ready!" completion the vendor's
`multi_chip_loading` waits for.

**Headline.** The host->device SR path is recovered from `plat.ko` instruction by instruction and is
now built and exercised on the takeover: the vendor's own `shuangta_ete_sr_dscr_fill` node format
(`word0` = message-buffer device address, `word1 = (len<<16)|0x6000|0xd2b`), the producer-index
commit at `SR+0x18`, and the `pcie_msg_send(chip,3)` doorbell (`out[0]` mask `0x40039010`,
`out[2] |= 1` `0x400392d4`). On **both** takeover boots the endpoint's SR engine **consumed the
posted descriptors** (`SR ch0` device index `0x10 -> 0x400`, i.e. all 32 nodes) and the H2D mask
`out[0]` read back `0x08` (bit 3) - the first time a host->device message has been delivered into the
chip in this project. The **first host->device frame** is the vendor's 72-byte HCC **id-1** frame,
recovered byte-exact from a fresh vendor boot and byte-identical to a runtime `hcc_msg_tx` frame.

**The firmware still does not answer.** `out[1]` still shows only `0x40` (id 6) then `0x04` (id 2),
never `0x01` (the id-1 "Device plat ready!"), no SR/DR payload byte changes, no HCC message in our
SR buffers, and `irq_taken = 0` - identical to `rx-loop`. The new, decisive observation is that the
device **never clears the H2D mask**: the vendor's `pcie_msg_send_irq` waits for `out[0]` to read 0
(`pcie_msg_wait_for_clr` @ `0x173dc`), and in the takeover `out[0]` stays `0x08` forever. So the ETE
engine consumed the descriptor nodes, but the firmware's HCC message service never took the message -
the gate is now downstream of the descriptor fill, in the device-side HCC dispatcher / group binding.

- **Part A** (this report): the transmit path in full, with disassembly and a live cross-check.
- **Part B**: `lab/txpath/txpath.c` - rxloop plus the SR post, the producer commit and the doorbell;
  two takeover boots.
- **Part C**: what the firmware did, what arrived, the handshake state, and the named next blocker.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a value
the device printed/measured) or **[inferred]**.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_msg_send pcie_msg_send_irq shuangta_ete_sr_dscr_fill \
    pcie_ete_sr_reg_init shuangta_ete_sr_get_nodesize shuangta_ete_sr_get_dscr_addr \
    shuangta_ete_sr_get_dscr_len shuangta_ete_sr_get_dscr_flag \
    pcie_ete_ring_ptr_plus hcc_msg_alloc hcc_msg_tx hcc_msg_tx_to_core \
    hcc_queue_add_msg hcc_queue_tx_process hcc_msg_process multi_chip_loading \
    device_plat_ready_msg_process host_ready_msg_process \
    > build/tmp/txpath/msg_sr.txt
$PY lab/ko_disasm.py $KO hcc_msg_tx hcc_msg_tx_to_core hcc_msg_alloc hcc_msg_free \
    hcc_msg_module_init hcc_msg_process hcc_msg_get_msg_map hcc_msg_register_tab_chip \
    hcc_msg_register_tab_core > build/tmp/txpath/hcc.txt
$PY lab/ko_disasm.py $KO hcc_queue_tx_process hcc_queue_add_msg hcc_queue_get_by_msg \
    pcie_ete_init_src_ring shuangta_ete_sr_node_init_handle pcie_ete_tx_done_handle \
    pcie_ete_ring_ptr_plus > build/tmp/txpath/sr2.txt
```

Live cross-check (read-only `devmem` through endpoint 0's BAR0, plus tracefs kprobes) is in
`build/register-dumps/txpath/001_live_sr_trace.txt`.

---

## Part A - the vendor's host->device transmit path

### A.0 The pieces, in dependency order

The ETE channels `0..2` are the **SR** (source/send) rings at blocks `0x400/0x450/0x4a0` of the ETE
window (BAR0 `0x403f2000`), depth 32, stride `0x6c` in the software channel array and `0x50` in the
register block (`pcie_ete_get_chn_cfg` @ `0x15f00`, `.rodata+0x101c`; phase-20e A.1). A host->device
message is one 8-byte SR node pointing at a host buffer that already holds the HCC frame; the node is
committed to the ring, and the doorbell is rung. The whole sequence is two functions:

```
shuangta_ete_sr_dscr_fill @0x17858   ->  fills the node, commits the index, rings the doorbell
pcie_msg_send             @0x160f4   ->  the doorbell itself (H2D mask + out[2] bit 0)
```

and the frames themselves are built by the HCC layer (`hcc_msg_alloc` @ `0x11d9c`,
`hcc_msg_tx` @ `0x11bf8`, drained by `hcc_queue_tx_process` @ `0x1128c`).

### A.1 `pcie_msg_send` - the doorbell **[proven]**

```
===== pcie_msg_send @ 0x160f4 (main path) =====
  0x0160f8: mov  r5, r0                 ; r5 = arg0 (the chip/bi arg, must be 0)
  0x0160fc: mov  r4, r1                 ; r4 = id
  0x016138: cmp  r4, #9
  0x01613c: bhi  error                  ; ids 0..9 only
  0x016140: cmp  r5, #0
  0x016144: bne  error                  ; arg0 must be NULL
  0x016148: movw r3, .LANCHOR0
  0x016150: ldr  r6, [r3, #4]           ; r6 = the global comm
  0x01615c: add  r8, r6, #0x50          ; spinlock
  0x016168: ldr  r3, [r6, #0x48]        ; outstanding shadow
  0x016170: lsr  r2, r3, r4
  0x016174: tst  r2, #1
  0x016178: bne  already-pending        ; this id is already outstanding
  0x01617c: ldr  r2, [r6, #0x44]        ; previous mask
  0x016184: orr  r3, r3, r1, lsl r4     ; set bit id
  0x016188: str  r3, [r6, #0x48]
  0x01618c: cmp  r2, #0
  0x016190: bne  unlock                 ; coalesce while one is in flight
  0x016194: ldr  r2, [r6, #0x2c]        ; out[0]  CA 0x40039010
  0x016198: str  r3, [r6, #0x44]
  0x01619c: str  r3, [r2]               ; *out[0] = pending bitmap (1<<id)
  0x0161a0: str  r5, [r6, #0x48]        ; r5 == 0 : clear the outstanding shadow
  0x0161a4: ldr  r2, [r6, #0x34]        ; out[2]  CA 0x400392d4
  0x0161a8: ldr  r3, [r2]
  0x0161ac: orr  r3, r3, r1             ; r1 = 1
  0x0161b0: str  r3, [r2]              ; *out[2] |= 1   (ring the doorbell)
```

`pcie_msg_send_irq` @ `0x174a8` is the synchronous variant: it first writes `8` to `out[5]`
(CA `0x400392f0`) and then spins in `pcie_msg_wait_for_clr` @ `0x173dc` until `*out[0]` reads back 0
before flushing the same pending mask and doorbell:

```
  0x0174f8: ldr  r3, [r4, #0x40]        ; out[5] CA 0x400392f0
  0x017504: str  r2(=8), [r3]           ; *out[5] = 8
  0x017508: bl   pcie_msg_wait_for_clr  ; wait for the device to clear out[0]
  0x01753c: str  r3, [r2]               ; *out[0] = pending
  0x017550: str  r3, [r2]               ; *out[2] |= 1
```

`pcie_msg_send` has exactly **two callers** in the whole module (relocation scan):

| caller | id | when |
| ------ | -- | ---- |
| `shuangta_ete_sr_dscr_fill` @ `0x178f8` | **3** | immediately after an SR node is filled |
| `pcie_ete_rcv_buff_check` @ `0x15144` | **5** | after DR receive buffers are reclaimed |

So the host->device message ids are **3 = "SR descriptor posted"** and **5 = "DR buffers
reclaimed"**; the same numeric namespace is used device->host with different handlers
(`pcie_msg_init` @ `0xb6e4` registers device->host ids 1/3/6/7 - phase-20d A.1). **[proven]**

### A.2 The SR node - `word0 = buffer, word1 = (len<<16)|0x6d2b` **[proven]**

`shuangta_ete_sr_dscr_fill(r0, r1 = chanctx, r2 = buffer device address, r3 = len, stack = flag)`
@ `0x17858`; the data branch:

```
  0x01789c: movw r1, #0xd2b             ; the host-fill magic
  0x0178a0: orr  r2, r2, #0x4000        ; owner/valid bit 14
  0x0178ac: orr  r2, r2, #0x2000        ; owner/valid bit 13
  0x0178b8: bfi  r2, r1, #0, #0xd       ; word1[12:0] = 0xd2b
  0x0178cc: str  r1(=addr), [r3, r2, lsl #3]   ; node[idx].word0 = buffer device address
  0x0178e0: str  r2, [r3, #4]                  ; node[idx].word1 = (len<<16)|0x6000|0xd2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus        ; index[9:0]|phase[10], wrap at depth
  0x0178f8: bl   pcie_msg_send                 ; doorbell id 3
```

with `len` placed into `word1[31:16]` by `bfi r1, r3, #0x10, #0x10` at `0x17884`. So a filled SR node
is `{word0 = message-buffer device address, word1 = (len<<16) | 0x6d2b}`. The node width is fixed at
8 bytes and the declared length is read back the same way:

```
===== shuangta_ete_sr_get_nodesize @ 0x17654 =====     mov r0, #8 ; bx lr
===== shuangta_ete_sr_get_dscr_addr @ 0x17670 =====    ldr r2,[r0] ; ldr r0,[r2, r1, lsl #3]  ; word0
===== shuangta_ete_sr_get_dscr_len  @ 0x17684 =====    ldr r0,[node+4] ; lsr r0, r0, #0x10   ; word1>>16
===== shuangta_ete_sr_get_dscr_flag @ 0x17698 =====    ldr r0,[node+4] ; ubfx r0, r0, #0, #0xd; word1[12:0]
```

The ring position is the packed 11-bit index `index[9:0] | phase[10]`
(`pcie_ete_ring_ptr_plus` @ `0x13ef8`, phase-20e A.4). **[proven]**

### A.3 The index commit register `SR+0x18` **[proven]**

`pcie_ete_sr_reg_init` @ `0x14a48` programs one SR channel (`r4` = channel instance,
`r3 = [r4+0xdc]` = the mapped SR register block):

```
  0x014a98: ldr  r1, [r6, #0x84]        ; chip resource
  0x014aac: bl   pcie_hostca_to_devva   ; host VA -> device VA
  0x014ab0: str  r0, [r5, #0x10]        ; SR+0x10 = node-array device address
  0x014ac4: ... bfi depth-1             ; SR+0x14 = depth-1 (0x1f)
  0x014ad4: ldr  r3, [r4, #0xdc]
  0x014ad8: ldr  r2, [r4, #0xc]         ; the channel's packed producer index
  0x014adc: str  r2, [r3, #0x18]        ; SR+0x18 = committed producer index   <-- THE COMMIT
  0x014ae8: ldr  r2, [r3, #8]
  0x014aec: ldrb r1, [r1, #5]           ; cfg[5] (1 for the three SR channels)
  0x014af0: bfi  r2, r1, #0, #3
  0x014af4: str  r2, [r3, #8]           ; SR+0x08[2:0] = cfg[5]
```

So the SR control set is `{+0x10 base = node array devva, +0x14 depth-1, +0x18 producer index,
+0x08[2:0] = 1}`, and the second index register `+0x1c` is the device's read index - exactly
mirroring the DR pair (`+0x30/+0x34/+0x38`, consumer `+0x3c`) recovered in phase 20e. **[proven]**

### A.4 The HCC frame and the command table **[proven structure + measured]**

Frames are allocated by `hcc_msg_alloc` @ `0x11d9c` (skb of `len+0xf`, `strh (len+0xc)` into
`buffer+4`, owner nibble set in `buffer[1]`) and queued by `hcc_msg_tx` @ `0x11bf8` ->
`hcc_queue_add_msg`, which the HCC TX thread drains in `hcc_queue_tx_process` @ `0x1128c`
(`bal_port_start_xfer`). A live kprobe pair proves the runtime chain and the exact arguments
(`build/register-dumps/txpath/001_live_sr_trace.txt`):

```
   HCC TX Thread-1169  hcc_queue_tx_process   r0=0xbf943b24 r1=0xbf943794
     pcie_thread-1177  shuangta_ete_sr_dscr_fill r2=0x8532bc40 r3=0x12a  (flag=1)
     pcie_thread-1177  pcie_msg_send           r0=0x0 r1=0x3
```

Capturing `hcc_msg_tx+0x20` (`r3` = `skb->data`, the frame about to be sent) gives the byte layout
live:

```
  id-1 frames (periodic control, 72 B):
    l=0x30 id=0x1 mg=0x5a5a w0=0x01000100 w8=0x5a5a0200 wc=<token> w14=0x001400d8 w18=0x1
    l=0x30 id=0x1 mg=0x5a5a w0=0x04000100 w8=0x5a5a0000 wc=0x0      w14=0x001400d8 w18=0x1
  alg get_2g_power_param frame (id 3, 298 B):
    l=0x12a id=0x3 mg=0x5a5a w0=0x01200101 w8=0x5a5a0000 wc=<token> w14=0x010e0101 w18=0xd010dae w20=0x1
```

so the wire frame is

| offset | size | meaning |
| ------ | ---- | ------- |
| `+0x00` | u32 | protocol/group (`byte[0]&0xf` is the HCC group; `hcc_msg_process` @ `0x1204c`) |
| `+0x04` | u16 | total length; `+0x06` = **HCC message id** (`ldrh r2,[r2,#6]` @ `0x1209c`) |
| `+0x08` | u16/u16 | reserved, then `0x5a5a` at `+0x0a` - the magic `pcie_ete_rcv_buff_check` tests |
| `+0x0c` | 8 | opaque token (echo handle; the firmware returns it in the response) |
| `+0x14` | u16 cmd, u16 len | the ALG command id and payload length |
| `+0x18` | len | payload; for ALG commands the first word is `0x0D01_<cfg_id>` |

The payload's `cfg_id` is looked up in the driver's **414-entry command table**
(`g_ast_alg_cfg_process_info_table`; `lab/out/2.4.15/ko_algtable.csv` - 225 setters / 189 getters,
`get_2g_power_param = 0x0dae`, `get_5g_power_param = 0x0db0`, ...). The phase-5/6 kprobe captures fix
the request/response envelope and the id word (`0x0D01_<cfg_id>`); `hcc_msg_process` @ `0x1204c`
dispatches received frames by group (`byte[0]&0xf`) and id (`u16@+6`) into the per-core HCC tables
registered by `hcc_msg_register_tab_chip`/`_core`. **[proven]** (the header offsets and the runtime
call chain are measured; the `+0x00` field names are **[inferred]**).

### A.5 The vendor's FIRST host->device message **[measured, byte-exact]**

A fresh vendor boot (`rox_pci0` bound, radios up, no probes) leaves the first SR ch0 node pointing at
a 72-byte frame. Reading the node array through `/dev/mem` (SR ch0 `+0x10` = `0x84543000` at the
moment of the read; the outbound window is an identity map so device address == host physical) gives
the node list and the slot-0 buffer:

```
node[00] w0=0x85257C40 w1=0x00486000   <-- slot 0, len 0x48
node[01] w0=0x85256C40 w1=0x00486000
...

buffer 0x85257C40 (72 B):
  +00 = 0x04000100   +04 = 0x00010030   +08 = 0x5A5A0000   +0c = 0x00000000
  +10 = 0x00000001   +14 = 0x001400D8   +18 = 0x00000001   +1c..+28 = 0
  +2c..+44 = 0xFFFFFFFF x7
```

i.e. group `0`, **HCC id 1**, total length `0x30`, magic `0x5a5a` at `+0x0a`, token `0`, then
`{1, 0x001400d8, 1, 0,0,0, 0xff...}`. This is byte-identical to the third live `hcc_msg_tx` id-1
frame above (`w0=0x04000100 w1=0x00010030 w2=0x5a5a0000 w3=0 w4=1 w5=0x001400d8 w6=1`), so it is a
genuine vendor host->device frame, and it is the first thing the vendor host posts on its SR ring
after the firmware is up (slot 0 of a fresh ring, `wptr` 0 -> 20 over the first minute). **[measured]**
(the 72 bytes are exact; that it is literally the first frame of the boot relies on slot 0 never
having been reused - `wptr` was 20 (< depth 32) at the read, so the slot index is reliable, while a
recycled *buffer* could in principle have been overwritten - it was not: the bytes match a runtime
id-1 frame).

### A.6 What the firmware is expected to answer **[proven, and not observed]**

The vendor's bring-up entry `multi_chip_loading` @ `0xf2ac` (called from wifi.ko
`host_module_init` @ `0x3e178`) waits on two completions:

```
  0x00f2fc: bl   wlan_pm_open                 ; firmware download + release + enable_irq
  0x00f324: add  r0, r4, #0x234
  0x00f328: mov  r1, #0xc8                    ; 200 jiffies
  0x00f32c: bl   wait_for_completion_timeout  ; WAIT #1
  0x00f338: ... .LC74 "multi_chip_loading::plat_ready timeout[0x%x]"
  0x00f358: orr  r3, r3, #1 ; str [r4,#0x218] ; mark "device seen"
  0x00f37c: add  r0, r4, #0x220
  0x00f380: mov  r1, #0x7d0                   ; 2000 jiffies
  0x00f384: bl   wait_for_completion_timeout  ; WAIT #2
  0x00f398: ... .LC77 "multi_chip_loading SUCC!"
```

The two completions are fired by two **device->host** HCC handlers in the chip table that
`plat_init_bal_hcc_excp` @ `0xe380` installs (`hcc_msg_register_tab_chip(chip=4, .data+0x2660,
count=5)`):

| id | handler | string | completes |
| -- | ------- | ------ | --------- |
| 1 | `device_plat_ready_msg_process` @ `0xeb78` | "Device plat ready! chip id : %d" | WAIT #1 (`+0x234`) |
| 2 | `host_ready_msg_process` @ `0xebd8` | "DEVICE READY" | WAIT #2 (`+0x220`) |

Both read the frame at `r0+0x118`, require source core `byte[1]>>4 == 0`, and id 1 needs no payload
while id 2 copies `len-0xc` bytes into `g_dmac_to_hmac_read_msg[core]` (`0x64`-byte records). So the
firmware is expected to answer with the **id-1 "Device plat ready!" frame** (which completes
`multi_chip_loading`'s first wait and lets the vendor host proceed to `hal_main_init` /
`hmac_main_init` / `wal_main_init`), and then the id-2 frame. **[proven]**

Note the direction: the ready frames are device->host. `multi_chip_loading` itself contains **no
host->device write** (phase-19/20d, re-confirmed here): the host's acts are `enable_irq`,
`dev_status_check` (reads) and the two waits. The host->device frames of the dialogue are the ones
`hcc_msg_tx` posts once the HCC layers are up - the first of which is the id-1 frame of A.5.

### A.7 Live cross-check of the SR control registers (fresh vendor boot) **[measured]**

Read-only `devmem` through endpoint 0's BAR0 (`ETE` window at `0x403f2000`), no probes, right after a
vendor reboot:

```
SR ch0 (block 0x400): +0x08=0  +0x10=0x84543000  +0x14=0x1f  +0x18=0x14  +0x1c=0x14  +0x30=0x01060750  +0x38=0x14  +0x3c=0x14
SR ch1 (block 0x450): +0x08=0  +0x10=0x84542000  +0x14=0x1f  +0x18=0x00  +0x1c=0x00  +0x30=0x01060650  +0x38=0x00  +0x3c=0x00
SR ch2 (block 0x4a0): +0x08=0  +0x10=0x84541000  +0x14=0x1f  +0x18=0x00  +0x1c=0x00  +0x30=0x01060550  +0x38=0x00  +0x3c=0x00
DR ch3 (block 0x590): +0x08=0  +0x10=0x01060440  +0x30=0x84540000  +0x38=0x41d  +0x3c=0x41d
DR ch6 (block 0x680): +0x08=0  +0x10=0x01060110  +0x30=0x8453d000  +0x38=0x404  +0x3c=0x404
```

and the slot-0 buffer of SR ch0 is the id-1 frame of A.5. This confirms the register map in the live
vendor: base `+0x10`, depth `+0x14 = 0x1f`, producer `+0x18`, consumer `+0x1c`, with the SR node array
in host RAM (`0x8454_xxxx`) and the `+0x30` side pointing at device SRAM. At runtime the vendor
leaves `SR+0x08 = 0` even though `pcie_ete_sr_reg_init` writes `cfg[5] = 1` there - the device clears
the nibble once the ring is live (the takeover's boot 2 wrote 1 and it read back 1 while the engine
was idle, then stayed 1). **[measured]**

### A.8 Proven vs inferred

| claim | status |
| ----- | ------ |
| `pcie_msg_send`: `out[0]` (CA `0x40039010`) = pending bitmap, `out[2]` (CA `0x400392d4`) `|= 1` | **proven** (`0x16194`..`0x161b0`) |
| `pcie_msg_send_irq` first writes `out[5] = 8` and waits for `out[0]` to clear | **proven** (`0x174f8`..`0x17550`, `pcie_msg_wait_for_clr` `0x173dc`) |
| `pcie_msg_send` is called only by `sr_dscr_fill` (id 3) and `rcv_buff_check` (id 5) | **proven** (relocation scan) |
| SR node = `{word0 = buffer device address, word1 = (len<<16)|0x6000|0xd2b}`, 8 bytes | **proven** (`0x17858`, `0x17654/70/84/98`) |
| packed index `index[9:0]|phase[10]`, wrap at depth | **proven** (`pcie_ete_ring_ptr_plus` `0x13ef8`) |
| SR program set `{+0x10 base, +0x14 depth-1, +0x18 producer, +0x08[2:0]=cfg[5]}` | **proven** (`pcie_ete_sr_reg_init` `0x14a48`) |
| SR control registers match a live vendor boot (base/depth/producer/consumer) | **measured** (A.7) |
| HCC frame: `+0x04` total length, `+0x06` message id, `+0x0a` magic, `+0x18` payload | **proven** (`hcc_msg_alloc`, `hcc_msg_process`; live `hcc_msg_tx`) |
| runtime H2D chain `hcc_queue_tx_process -> sr_dscr_fill -> pcie_msg_send(3)` | **measured** (kprobe) |
| the ALG command table (414 entries) maps names to `cfg_id`; payload word `0x0D01_<cfg_id>` | **proven** (`ko_algtable`, phase-5/6) |
| the vendor's first SR frame is the 72-byte HCC id-1 frame | **measured** (A.5; slot 0 of a fresh ring) |
| id 1 "Device plat ready!" completes `multi_chip_loading` wait #1; id 2 completes wait #2 | **proven** (phase-19 A.1/A.2, re-confirmed) |
| the ready dialogue contains no host->device write | **proven** (`multi_chip_loading`, `wlan_power_on`) |
| the device clears `out[0]` when it consumes the H2D message | **proven** intent (`pcie_msg_wait_for_clr`); **not observed** in the takeover (Part C) |
| the `+0x00` frame word's field names (group/protocol) | **inferred** |
| what the firmware does with the H2D id/group | **open** - the Part B/C experiment |

---

## Part B - `lab/txpath/txpath.c` and the test boots

`txpath` is `rxloop` (phase 20e, kept verbatim) plus the host->device transmit path. It keeps every
proven step: claim (`pci_enable_device` + `pci_request_mem_regions`), the six inbound iATU viewports,
the outbound (device->host) viewport, `PCI_COMMAND=7`, `FIRMWARE.bin` -> BAR0 `0x6f8000` with a
`diffs=0` read-back, the ETE SR/DR program registers and the `+0x2e8` RMW, the DR post and its
producer-index commit, the ack/clear/re-arm service, the real id-6 handler, the `0x5a5a` release and
the config line write. It changes exactly:

1. **SR node post and producer commit** (`omo_post_sr`). For each of the three SR channels
   (blocks `0x400/0x450/0x4a0`), all 32 8-byte nodes are filled with
   `{word0 = slot device address, word1 = (len<<16)|0x6d2b}` exactly as `shuangta_ete_sr_dscr_fill`
   @ `0x17858`, and the packed producer index (`0x400` = 32 entries, phase 0) is committed to
   `SR+0x18` exactly as `pcie_ete_sr_reg_init` @ `0x14a48`. Host memory only.
2. **The doorbell** (`omo_send_doorbell`). `pcie_msg_send(chip, 3)` @ `0x160f4`:
   `out[0] <= 1<<3` (CA `0x40039010`) and `out[2] |= 1` (CA `0x400392d4`), both read back. Rung once
   after the release and once at the first poll.
3. **The message.** Slot 0 carries the vendor's first SR frame, byte-exact from the live capture
   (A.5): the 72-byte HCC id-1 frame. Boot 2 additionally puts the live-captured 298-byte
   `alg get_2g_power_param` frame (HCC id 3, payload `0x0d010dae`) in slot 1.
4. **The SR consumer/log** (`omo_scan_sr`). Reads each SR channel's `+0x18/+0x1c`, logs
   node/payload changes, and decodes any device-written HCC frame in slot 0 with the vendor's test
   (`magic 0x5a5a` at `+0xa`, `id` at `+6`, group `byte[0]&0xf`).

Every device write is quoted from the disassembly. The only unproven-but-quoted write remains
`PCI_INTERRUPT_LINE = 0xcf` (207), the measured live vendor value. Boot 2 additionally writes
`SR+0x08 = 1`, which is the vendor's own `pcie_ete_sr_reg_init` value (`cfg[5] = 1`).

### B.1 Boot 1 - `txpath.ko` md5 `fd5260c583cacebc26e0cac51d5aec3d`

CI run `36904060091`, `vermagic=5.10.201 SMP mod_unload ARMv7`, 45156 bytes. Evidence:
`build/register-dumps/txpath/{030,031,040}_*`.

```
[   39.512550] omo-txpath:   SR ch0 ctrl=0x00000000 base=0x00000000 depth=0x00000000 wptr=0x00000000
[   39.569069] omo-txpath:   SR ch0 base            [0x410] <= 0x832b4000 readback=0x832b4000 match=YES
[   39.939616] omo-txpath: DR ch3 posted 32 nodes word0=0x83bc7000 (payload dma ... dr_dscr_fill @0x1765c)
[   39.950507] omo-txpath: DR ch3 commit DR+0x38 (wptr) <= 0x00000400 readback=0x00000400
[   41.754711] omo-txpath: SR ch0 first H2D message built (72 B): proto=0x04000100 len/id=0x00010030 magic=0x5a5a0000 tok=0x00000000/00000001 hdr14=0x001400d8 hdr18=0x00000001
[   41.772714] omo-txpath: SR ch0 posted 32 nodes word0=0x83a58000 word1=0x00486d2b; commit SR+0x18 <= 0x00000400 readback=0x00000400
[   41.788469] omo-txpath: SR ch1 posted 32 nodes word0=0x83bcc000 word1=0x00486d2b; commit SR+0x18 <= 0x00000400 readback=0x00000400
[   41.804342] omo-txpath: SR ch2 posted 32 nodes word0=0x83bd4000 word1=0x00486d2b; commit SR+0x18 <= 0x00000400 readback=0x00000400
[   43.109764] omo-txpath: [post0 +310ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
[   43.140250] omo-txpath: [send post0] out[2] CA=0x400392d4 0x00000000 -> 0x00000001 readback=0x00000000 (doorbell |= 1)
[   43.289366] omo-txpath: [poll +490ms] MBOX out[0] H2D mask    CA=0x40039010 0x00000000 -> 0x00000008
[   43.310830] omo-txpath: [poll +510ms] MBOX out[1] pending     CA=0x40039014 0x00000000 -> 0x00000040
[   43.999430] omo-txpath: [poll +1200ms] MBOX out[1] pending    CA=0x40039014 0x00000040 -> 0x00000004
[   44.199472] omo-txpath: [poll +1400ms] MBOX out[1] pending    CA=0x40039014 0x00000004 -> 0x00000000
[   68.933218] omo-txpath: done (release=1 rings=1 sr_posted=1 ... irq_taken=0 irq_handled=0 msgs=3 services=5 sendflag=1 dr_events=4 sr_events=1)
```

**What the transmit did:** the SR ch0 device index moved `0x10 -> 0x400` at `+310 ms` after the
commit - the endpoint's SR engine read every posted descriptor. `out[0]` read back `0x08` (bit 3 =
the SR "message posted" id) and stayed there; `out[2]` self-cleared. **What it did not do:** zero
`payload+` lines, no SR node change after the post, no `HCC MESSAGE` decode, no mailbox id 1; the
mailbox still showed only `0x40` (id 6) then `0x04` (id 2).

### B.2 Boot 2 - `txpath.ko` md5 `b3ee1822697a78258baeec2c93dc07da`

CI run `36904711206`, 45716 bytes. Evidence: `build/register-dumps/txpath/{032,033,041}_*`. This boot
sets `SR+0x08 = 1` (the vendor's own `cfg[5]` value) and adds the live-captured ALG frame in slot 1.

```
[   39.246425] omo-txpath:   SR ch0 ctrl            [0x408] <= 0x00000001 readback=0x00000001 match=YES
[   41.406553] omo-txpath: SR ch0 first H2D message built (72 B): proto=0x04000100 len/id=0x00010030 magic=0x5a5a0000 ...
[   41.424634] omo-txpath: SR ch0 slot1 alg frame built (298 B): w0=0x01200101 len/id=0x0003012a magic=0x5a5a0000 cmd/len=0x010e0101 payload=0x0d010dae
[   41.439880] omo-txpath: SR ch0 posted 32 nodes word0=0x84c04000 word1=0x00486d2b; commit SR+0x18 <= 0x00000400 readback=0x00000400
[   42.794807] omo-txpath: [post0 +310ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
[   42.811849] omo-txpath: [send post0] pcie_msg_send(chip,3): out[0] CA=0x40039010 0x00000000 -> 0x00000008 readback=0x00000008
[   42.825272] omo-txpath: [send post0] out[2] CA=0x400392d4 0x00000000 -> 0x00000001 readback=0x00000000
[   42.990761] omo-txpath: [poll +510ms] MBOX out[1] pending     CA=0x40039014 0x00000000 -> 0x00000040
[   43.669671] omo-txpath: [poll +1190ms] MBOX out[1] pending    CA=0x40039014 0x00000040 -> 0x00000004
[   43.869972] omo-txpath: [poll +1390ms] MBOX out[1] pending    CA=0x40039014 0x00000004 -> 0x00000000
[   68.782657] omo-txpath: done (release=1 rings=1 sr_posted=1 ... irq_taken=0 irq_handled=0 msgs=3 services=5 sendflag=1 dr_events=4 sr_events=1)
```

Boot 2 reproduces boot 1 exactly: SR engine consumed the descriptors, `out[0] = 0x08`, the id-6
wake twice and the id-2 word once, `sr_events=1`, zero payload changes, zero HCC-message decodes,
`irq_taken=0`. The `SR+0x08 = 1` write read back `1` (the register is *not* self-clearing at init) and
did not change the firmware's behaviour.

### B.3 Live takeover state after boot 2 (`041_testboot2_state.txt`)

```
SR ch0 block 0x400: +08=1 +10=0x8361F000 +18=0x400 +1c=0x400   (engine read all 32)
SR ch1 block 0x450: +08=1 +10=0x83C7C000 +18=0x400 +1c=0x010   (engine read 16)
SR ch2 block 0x4a0: +08=1 +10=0x83C0B000 +18=0x400 +1c=0x010   (engine read 16)
DR ch3..6:          +30=host buffers +38=0x400 +3c=0x010
mailbox: out[0]=0x00000008  out[1]=0  out[2]=0  out[3]=0  out[4]=0  out[5]=0
```

The H2D mask `out[0]` **stays `0x08`**: the endpoint consumed the descriptor nodes but never cleared
the host message register, which is exactly the register the vendor's synchronous sender spins on.

---

## Part C - outcome

**Did the firmware answer?** No. Over both 20 s runs, with the SR path fully posted (32 nodes on
each of the three SR channels), the producer index committed, the descriptor nodes consumed by the
SR engine, and the `pcie_msg_send(chip,3)` doorbell written and observed (`out[0] = 0x08`), the
firmware's visible output is **unchanged from phase 20e**: `out[1] = 0x40` (id 6) then `0x04`
(id 2), then silence. There is no `out[1] = 0x01` (the id-1 "Device plat ready!"), no SR/DR payload
byte change, no device-written HCC frame in our SR buffers, and `irq_taken = 0`.

**What arrived / changed.** The new, measured facts are:

1. The endpoint's SR engine **consumed** the posted descriptors - `SR ch0` device index
   `0x10 -> 0x400` on both boots (`sr_events = 1`), and channels 1/2 partially (`0x10`). So the SR
   node format, the base/depth programming and the producer-index commit are all correct and the
   host->device ring is real and live.
2. The doorbell works: `out[0]` (CA `0x40039010`) read `0x08` after the send - the exact value
   `pcie_msg_send(chip, 3)` writes - while `out[2]` self-cleared (the device took the trigger bit).
3. The H2D mask is **never cleared by the device** (`out[0] = 0x08` at the end of the run). The
   vendor's `pcie_msg_send_irq` waits for that clear (`pcie_msg_wait_for_clr` @ `0x173dc`); a mask
   that never clears means the **firmware's HCC message service never took the message** even though
   the ETE engine read the descriptors.

**Handshake state.** The same as phase 20e, plus the transmit half now proven live: the host can
build an SR node, commit it, and doorbell the device, and the device's ETE reads it. The dialogue
still stops at the device's announce (`out[1]` id 6 then id 2); the id-1 "Device plat ready!" frame
never crosses.

**Named next blocker.** The gate is now **device-side, downstream of the descriptor fill**: the
firmware does not service the posted H2D message (`out[0]` is not cleared), so it never emits the
id-1/id-2 ready frames. The two concrete candidates, in dependency order:

1. **The device-side engine/dispatch enable.** The `SR+0x08` nibble is the only SR control bit we
   now write (`cfg[5] = 1`, matching `pcie_ete_sr_reg_init`); boot 2 shows it is not sufficient. The
   vendor also read-modify-writes each channel's `+0x2e8` with `0xfffffc20` (`pcie_ete_chn_res`
   @ `0x7490`) - which the takeover does - but the live boot additionally has the ETE *block IRQ*
   cleared by `pcie_ete_intr_init` @ `0x7528` (`str r2,[r3]` with `& 0xffe0f8f8`) and a device-side
   source configured outside `plat.ko`. Any device-side register that makes the firmware poll the SR
   ring is not written by any string we have recovered.
2. **The group/core binding of the frame.** `hcc_msg_process` dispatches on `byte[0]&0xf` (group) and
   `u16@+6` (id). Our posts reproduce a live group-0 id-1 frame exactly, so the *frame* is right; what
   is unproven is which SR channel/group the firmware associates with the message core before the
   ready dialogue - i.e. whether a pre-ready H2D frame is even accepted, or whether the id-1 frame is
   only ever sent *after* the vendor's own `hal/hmac/wal` initialisation has bound the core queues.
   Testing that needs either the firmware's dispatch table or a device-side trace, not more host
   writes.

Until one of those is identified, the mailbox announce (`id 6`, `id 2`) is still the whole reachable
dialogue - but the host half of the transmit path is now built and demonstrably delivered.

---

## Test record / Recovery

Raw evidence: `build/register-dumps/txpath/` + `stage/`. **Two takeover boots + one recovery boot.**

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live vendor boot before staging: modules/md5, drivers, irq 207/209, config, radios |
| `001_live_sr_trace.txt` | read-only live kprobe + devmem cross-check (SR path call chain, H2D frames, SR regs) |
| `010_staging.txt`, `011_staging2.txt` | vendor modules hidden, `txpath.ko` + loader + recovery installed, md5, syntax |
| `020_testboot_cmd.txt`, `021_testboot2_cmd.txt` | the takeover reboots |
| `030_..._dmesg_full.txt`, `031_testboot_evidence.txt` | boot 1 (md5 `fd5260c5...`) |
| `032_..._dmesg_full.txt`, `033_testboot2_evidence.txt` | boot 2 (md5 `b3ee1822...`) |
| `040_testboot_state.txt`, `041_testboot2_state.txt` | takeover state after each test: SR/DR regs, mailbox, config, pstore |
| `060_recovery_run.txt` | the recovery script run from the device |
| `070_recovery_evidence.txt` | recovered boot: modules/radios/IRQs/power params/web, leftovers, pstore |
| `stage/` | `txpath.ko`, `omo-txpath`, `recover-txpath.sh`, `stage-txpath.sh` |
| `../tmp/txpath/*.txt` | the Part-A disassembly dumps |

### Baseline (live vendor, `000_baseline.txt`)

```
hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3 ; md5 wifi e21629d2... plat 23660bc2...
0000:00:00.0 irq=207 ; 0001:00:00.0 irq=209 ; both -> rox_pci0 ; hisi_pci_intx
config ep0: COMMAND=0x0006, LINE=0xcf ; 6 wlan ifaces
```

### Takeover boots

Both boots: six inbound + one outbound viewport `match=YES`; all seven SR/DR program registers and
the `+0x2e8` RMW `match=YES`; four DR channels posted (32 nodes each) with `DR+0x38 <= 0x400`; three
SR channels posted (32 nodes each) with `SR+0x18 <= 0x400`; firmware `diffs=0`; release `0x5a5a`;
config line `0xff -> 0xcf`; `request_irq(207)` `rc=0`. Boot 2 also `SR+0x08 <= 1`.
`done (... irq_taken=0 irq_handled=0 msgs=3 services=5 sendflag=1 dr_events=4 sr_events=1)`.
No panic in either boot; pstore unchanged (blk-0/1/2 mtimes `10:41`/`14:37`/`14:37`, all pre-test).

### Recovery (`070_recovery_evidence.txt`)

`sh /root/recover-txpath.sh` renamed the modules back and removed the module, loader, symlink, `/tmp`
copy and itself, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1 (md5 e21629d2.../23660bc2... = baseline)
both endpoints bound to rox_pci0 ; irq 207/209 hisi_pci_intx ; config LINE=0xcf, COMMAND=0x0006
chip id:0x34 ; 6 wlan ifaces ; br-lan UP ; web UI HTTP OK
2g/5g power params match baseline ; leftovers (module, .omo-off, loader, symlink, /tmp, /root script): absent
pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note

No panic in either takeover boot or the recovery boot; `pstore` gained no record. All measurements
were made through endpoint 0's own BAR0 (the ETE window at `0x403f2000`) or from the module; the RC
`misc` window (`0x10161000`) was never touched. One early state capture read the mailbox CAs at the
wrong host offset (`0x403b8000 + BAR0 offset` instead of `0x403b8000 + (CA-0x40000000)`); it
returned values and did not fault, and the correct addresses were used afterwards.

### Writes per takeover boot

Six inbound iATU viewports + one outbound viewport + `PCI_COMMAND=7` + the 928,920-byte firmware
(read back) + the seven SR/DR program registers + the `+0x2e8` RMW + the four DR base/depth/write
registers + the three SR base/depth/ctrl/write registers + the 0x5a5a release + the ack/re-arm pair
per message + the H2D mask/doorbell pair. All are quoted; the one unproven-but-quoted write remains
`PCI_INTERRUPT_LINE = 0xcf`. Boot 2 additionally writes `SR+0x08 = 1`, quoted from
`pcie_ete_sr_reg_init` @ `0x14ae8`.

### Regenerate

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
# Part A dumps (see the header) land in build/tmp/txpath/.
gh run download 36904060091 -n txpath-ko -D build/tmp/txpath-ko     # boot 1 (fd5260c5...)
gh run download 36904711206 -n txpath-ko -D build/tmp/txpath-ko2    # boot 2 (b3ee1822...)
# stage: scp build/register-dumps/txpath/stage/{txpath.ko,omo-txpath,recover-txpath.sh,stage-txpath.sh}
#        to /tmp, then sh /tmp/stage-txpath.sh; recovery: sh /root/recover-txpath.sh
```
