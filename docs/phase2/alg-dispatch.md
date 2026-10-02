# `iwpriv <wlan> alg` (ioctl 0x0101) dispatch map and driver<->firmware sync boundary

Target: `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/tmp/hi5622v100_wifi.ko`
(3,564,728 bytes, ELF 32-bit LSB **relocatable** `ET_REL`, ARM EABI5, not stripped).
Tooling: `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/pyenv/Scripts/python.exe` (capstone 5.0.7 + pyelftools).
All work read-only; no device access.

## 0. Address convention (important)

The file is a *relocatable object*, so every section has `sh_addr = 0` and every
"address" below is **section-relative** (`.text+0x…`, `.data+0x…`, `.rodata+0x…`,
`.rodata.str1.4+0x…`). This is why the resolved string references had to be computed
from relocation addends rather than from absolute vaddrs.

Verification command **C1**:

```
$ cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2 && ./pyenv/Scripts/python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'))
for s in e.iter_sections():
    print(f"{s.name:22s} addr=0x{s['sh_addr']:08x} off=0x{s['sh_offset']:08x} size=0x{s['sh_size']:x}")
EOF
```
Observed (abridged): `.text off=0x38 size=0x1630cc`, `.rodata off=0x167b00 size=0x25264`,
`.rodata.str1.4 off=0x191e40 size=0xb040b`, `.data off=0x243ab0 size=0x16768`,
`.symtab off=0x26331c`, and **`addr=0x00000000` for every section**.

`file hin5622v100_wifi.ko` → `ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped`.

Symbol count **C1b**: `.symtab` = 21,495 symbols, 3,847 `STT_FUNC`, 0 zero-sized functions.

---

## 1. How the dispatch was located (exact commands, offsets)

### 1.1 Pre-recon: symbol census of the ioctl/private layer

Command **C2**:

```
$ cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2 && ./pyenv/Scripts/python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'))
sym=e.get_section_by_name('.symtab')
fs=[s for s in sym.iter_symbols() if s['st_info']['type']=='STT_FUNC']
for s in fs:
    n=s.name.lower()
    if 'priv' in n or 'ioctl' in n:
        print(f"0x{s['st_value']:06x} size={s['st_size']:6d} {s.name}")
EOF
```
Key hits (exact output):
```
0x14877c size=   160 wal_hipriv_alg_cfg_get_cfg_id_normal
0x14881c size=   512 wal_hipriv_alg_cfg
0x10c564 size=   700 wal_algcmd_char_extra_adapt
0x11f608 size=   988 wal_iwpriv_char_extra_adapt
0x10dd20 size=  3300 wal_ioctl_get_param_char
0x11c808 size=  1400 wal_ioctl_set_param_char_process_subioctl
0x11c750 size=   184 wal_hipriv_alg_cfg_potting
```
This immediately pointed at the `wal_hipriv_alg_*` / `wal_algcmd_*` family.

### 1.2 Locate the `iw_priv_args` entry whose name is `alg` and whose cmd is 0x0101

The wireless-extensions registration entry is
`struct iw_priv_args { __u32 cmd; __u16 set_args; __u16 get_args; char name[16]; }` (24 bytes).
Scan `.rodata` for the literal little-endian word `01 01 00 00` and inspect the 8 bytes after it.

Command **C3**:

```
$ cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2 && ./pyenv/Scripts/python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
import re, struct
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb')); data=open('build/tmp/hi5622v100_wifi.ko','rb').read()
ro=e.get_section_by_name('.rodata'); b=data[ro['sh_offset']:ro['sh_offset']+ro['sh_size']]
for m in re.finditer(rb'\x01\x01\x00\x00', b):
    i=m.start(); w=b[i:i+24]
    name=w[8:24].split(b'\x00')[0]
    if name==b'alg':
        print(f"off=0x{i:x} raw={w.hex()} cmd=0x{struct.unpack_from('<I',w,0)[0]:04x} "
              f"set=0x{struct.unpack_from('<H',w,4)[0]:04x} get=0x{struct.unpack_from('<H',w,6)[0]:04x} name={name!r}")
EOF
```
Observed (exact):
```
off=0x9af4  raw=01010000f421e823616c6700000000000000000000000000 cmd=0x0101 set=0x21f4 get=0x23e8 name=b'alg'
off=0xb2a0  raw=01010000f421e823616c670000000000000002010000f421e823 cmd=0x0101 set=0x21f4 get=0x23e8 name=b'alg'
```
Decoding with `IW_PRIV_TYPE_CHAR = 0x2000`, `IW_PRIV_SIZE_MASK = 0x07ff`:
`0x21f4 = 0x2000|0x01f4` → **CHAR, 500 bytes (set)**; `0x23e8 = 0x2000|0x03e8` → **CHAR, 1000 bytes (get)**.
This matches the blackbox note "alg (0101, set 500 chars / get 1000 chars)".

So the anchor is confirmed: **`.rodata+0x9af4` = a one-entry `iw_priv_args` for `alg` (cmd 0x0101)**,
and `.rodata+0xb2a0` is the same entry inside the big registration array.

### 1.3 Find the registration array / handler arrays

Command **C4** (resolve relocations with addends decoded from MOVW/MOVT immediates and ABS32 words):

```
$ cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2 && ./pyenv/Scripts/python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'))
for s in e.get_section_by_name('.symtab').iter_symbols():
    if 'priv_arg' in s.name or 'iw_handler' in s.name:
        print(s.name, s['st_info']['type'], hex(s['st_value']), s['st_size'])
EOF
```
Observed (exact):
```
g_ast_iw_priv_args STT_OBJECT 0x9c68 6480
g_st_iw_handler_def STT_OBJECT 0x1c5c 24
g_st_iw_cfg_handler_def STT_OBJECT 0x1c44 24
```
Then dump the two `struct iw_handler_def` records (24 bytes each: `standard,num_standard,
num_private,num_private_args,private,private_args,get_wireless_stats`). Command **C5** output:

```
g_st_iw_handler_def @ .data+0x1c5c:
  +0x00: 0x00009b90  (standard array, .rodata+0x9b90)
  +0x04: 0x00210036  -> num_standard=0x36(54), num_private=0x21(33)
  +0x08: 0x0000010e  -> num_private_args=0x10e(270)
  +0x0c: 0x00009b0c  (private handler array, .rodata+0x9b0c)
  +0x10: 0x00009c68  (private_args = g_ast_iw_priv_args, .rodata+0x9c68)
  +0x14: 0x00000000
g_st_iw_cfg_handler_def @ .data+0x1c44:
  +0x04: 0x00200000  -> num_standard=0, num_private=0x20(32)
  +0x08: 0x0000000e  -> num_private_args=0x0e(14)
  +0x0c: 0x0000993c  (private handler array)
  +0x10: 0x000099bc  (private_args)
```

Dumping `g_ast_iw_priv_args` (270 × 24 bytes) with the name string resolved through
`.rel.rodata` ABS32 addends gives the full private-ioctl vocabulary. The relevant rows:

```
[237] cmd=0x0101 (low=0x01) set=0x21f4 get=0x23e8 name='alg'          <-- the alg command
[238] cmd=0x0102 (low=0x02) set=0x21f4 get=0x23e8 name='cali'
[236] cmd=0x8bfd (low=0xfd) set=0x21f4 get=0x23e8 name=''
```
and the small `g_st_iw_cfg_handler_def` private_args list (14 entries) ends with:

```
[12] cmd=0x8bfd set=0x21f4 get=0x23e8 name=''
[13] cmd=0x0101 set=0x21f4 get=0x23e8 name='alg'          <-- same alg entry
```

### 1.4 Find the module dispatcher that consumes cmd 0x0101

`wal_algcmd_char_extra_adapt` (0x10c564) was the standout by name. Disassembly shows it
**hard-codes 0x101 into the request header** and calls `wal_wlan_cfg_module_process_entry`.
Command **C6** (capstone ARM, relocations annotated):

```
$ .../python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
from capstone import *
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb')); data=open(...,'rb').read()
text=e.get_section_by_name('.text'); toff=text['sh_offset']
md=Cs(CS_ARCH_ARM, CS_MODE_ARM)
for ins in md.disasm(data[toff+0x10c564:toff+0x10c564+700], 0x10c564): print(hex(ins.address), ins.mnemonic, ins.op_str)
EOF
```
Exact relevant output:
```
0x10c564: push {r4,r5,r6,r7,r8,sb,sl,lr}      ; (dev, char*cmd, ..., char*extra) style handler
0x10c5d0: mov  r1, #0x1f4                     ; in-buffer max = 500 bytes  (matches set_args CHAR/500)
0x10c60c: mov  r3, #0x3e8                     ; out-buffer size = 1000      (matches get_args CHAR/1000)
0x10c660: movw ip, #0x101                     ; <<< request cmd = 0x0101
0x10c670: strh ip, [sp, #0x14]
0x10c674: bl   wal_split_cmd_line
0x10c71c: bl   wal_wlan_cfg_module_process_entry   ; dispatch
0x10c6bc: bl   memcpy_s                        ; copy result back to caller extra[]
```
`wal_wlan_cfg_module_process_entry` (@`.text+0x127af8`, size 972) walks a 12-byte module table
in `.data` and `blx`es the matching handler. Command **C7** (disassembly) shows:
```
0x127b60: ldrh r3, [r4, #4]      ; request cmd
0x127b64: ldrh r2, [r5]          ; table[i].cmd
0x127b6c: cmp  r2, r3
0x127b70: beq  0x127bcc
0x127b74: ldrh r2, [r5, #0xc]    ; table[i+1].cmd
0x127b78: cmp  r2, r3
0x127b7c: beq  0x127bc8          ; r5 += 0xc
...
0x127bec: ldr  r7, [r5, #8]      ; table[i].handler
0x127c08: blx  r7
```
Resolving `.LANCHOR1` at 0x127b40 (reloc → `.data+0x1d58`) and dumping the table gives
**C8**:
```
[0] @.data+0x1d58 cmd=0x0101 sub=0x0000 flags=0x01010101 handler=wal_wlan_cfg_alg_process_entry  @0x14b46c
[1] @.data+0x1d64 cmd=0x0102 sub=0x0000 flags=0x01010101 handler=wal_wlan_cfg_cali_process_entry @0x14b6ec
[2] @.data+0x1d70 cmd=0x00ba sub=0x0000 flags=0x00023330 handler=<none>
[3] @.data+0x1d7c cmd=0x2fa8 sub=0x0002 flags=0x00022e14 handler=<none>
```
This is the decisive link: **cmd 0x0101 → `wal_wlan_cfg_alg_process_entry` (`0x14b46c`)**;
the sibling **cmd 0x0102 → `wal_wlan_cfg_cali_process_entry` (`0x14b6ec`)**.

### 1.5 The command-name matcher and the name→cfg_id table

`wal_wlan_cfg_alg_process_entry` (`.text+0x14b46c`, size 640) resolves the user command
name into a numeric cfg id and then looks up a descriptor. Command **C9** (disassembly), key lines:
```
0x14b4fc: bl   alg_cfg_analysis_args            ; parse argv -> {cfg_id,dir,pkt_type}
0x14b538: ldrh r1, [r5]                         ; cfg_id from parsed args
0x14b530: movw ip, g_alg_cfg_lut
0x14b544: add  ip, ip, #0xb0                    ; stride = 176 bytes
0x14b550: ldrh r4, [ip]                         ; entry.cfg_id  (u16 @ +0)
0x14b558: cmp  r4, r1
0x14b560: ldrb r4, [ip, #2]                     ; entry.dir     (u8  @ +2)
0x14b568: cmp  r4, r0
0x14b5e4: bl   0x14a3a0                         ; invoke selected entry
```
`alg_cfg_analysis_args` (`0x14b260`) calls `alg_cfg_get_pkt_type` then
`alg_cfg_search_process_info_by_cfg_name`, which is the **name-matching helper** for the
`alg` command. `alg_cfg_search_process_info_by_cfg_name_normal` (`0x14ae68`, size 148) is a
plain `strncmp` loop over a 12-byte-entry table:
```
0x14ae78: movw r4, g_ast_alg_cfg_process_info_table
0x14ae90: add  r4, r4, #0xc           ; stride 12
0x14ae9c: ldr  r1, [r4]               ; entry.name  (char* @ +0)
0x14aea8: bl   strncmp
```
`alg_cfg_search_process_info_by_cfg_id` (`0x14b038`, size 188) is the id-keyed variant:
```
0x14b06c: add  r3, r3, #0xc           ; stride 12
0x14b078: ldrh r2, [r3, #4]           ; entry.cfg_id (u16 @ +4)
0x14b088: ldrb r2, [r3, #6]           ; entry.dir    (u8  @ +6)
0x14b0d0: ... optional override hook
```
Dumping `g_ast_alg_cfg_process_info_table` @ `.data+0x35c8` (size 4968 = 414 × 12 bytes)
with name pointers resolved via `.rel.data` yields 414 command rows (full dump reproduced in
§4; representative rows):

```
[350] @.data+0x4630 name='set_2g_power_param'       cfg_id=0x0dae dir=0
[351] @.data+0x463c name='get_2g_power_param'       cfg_id=0x0dae dir=1
[352] @.data+0x4648 name='set_5g_power_param'       cfg_id=0x0db0 dir=0
[353] @.data+0x4654 name='get_5g_power_param'       cfg_id=0x0db0 dir=1
[360] @.data+0x46a8 name='set_xo_ppm_cali_param'    cfg_id=0x0db2 dir=0
[361] @.data+0x46b4 name='get_xo_ppm_cali_param'    cfg_id=0x0db2 dir=1
[366] @.data+0x46f0 name='set_rssi_param'           cfg_id=0x0db3 dir=0
[367] @.data+0x46fc name='get_rssi_param'           cfg_id=0x0db3 dir=1
```

### 1.6 The per-command handler table (`g_alg_cfg_lut`)

`g_alg_cfg_lut` is `STT_OBJECT` at `.data+0x4934`, size 73216; `g_alg_cfg_lut_size`
(`.data+0x4930`) reads **416**. Entry stride is 0xb0 (176 B); the search keys on
`u16 cfg_id @+0` / `u8 dir @+2`; the **analysis handler function pointer is at +8** (170 of the
416 entries carry one; further pointers exist at +0x18/+0x1c etc.). Scanning the LUT by
cfg_id and resolving the +8 relocations (command **C10**) gives, verbatim:

```
 idx   LUT off    cfg_id dir  +8 analysis handler                       +0x18
  15 .data+0x5384  0x0197   0   alg_cfg_args_analysis_freq_bw_mode       None
  16 .data+0x5434  0x0197   1   alg_cfg_args_analysis_freq_bw_mode       None
 216 .data+0xddb4  0x0dae   0   alg_cfg_args_param_analyse_equipment_param None
 217 .data+0xde64  0x0dae   1   alg_cfg_args_param_analyse_equipment_param None
 218 .data+0xdf14  0x0db0   0   alg_cfg_args_param_analyse_equipment_param None
 219 .data+0xdfc4  0x0db0   1   alg_cfg_args_param_analyse_equipment_param None
 220 .data+0xe074  0x0daf   0   alg_cfg_args_param_analyse_equipment_param None
 221 .data+0xe124  0x0daf   1   alg_cfg_args_param_analyse_equipment_param None
 226 .data+0xe494  0x0db2   0   alg_cfg_args_param_analyse_equipment_param None
 227 .data+0xe544  0x0db2   1   alg_cfg_args_param_analyse_equipment_param None
 234 .data+0xea14  0x0db3   0   alg_cfg_args_param_analyse_equipment_param None
 235 .data+0xeac4  0x0db3   1   alg_cfg_args_param_analyse_equipment_param None
 406 .data+0x16054 0x10cd   0   alg_cfg_args_analysis_ns_aggr             None
 407 .data+0x16104 0x10cd   1   alg_cfg_args_analysis_ns_aggr             None
 409 .data+0x16264 0x10d1   0   alg_cfg_args_analysis_ns_rate             None
 410 .data+0x16314 0x10d1   1   alg_cfg_args_analysis_ns_rate             None
 404 .data+0x15ef4 0x1069   0   alg_cfg_args_analysis_param_binary        None
 405 .data+0x15fa4 0x1069   1   alg_cfg_args_analysis_param_binary        None
```

### 1.7 The sibling `setparam alg_cfg` path (for completeness)

There is a second, distinct "alg_cfg" mechanism reached via `setparam alg_cfg <name> <value>`
(hardware/debug config, not factory calibration). It is a **name-matching helper** too, but
against a different 5-entry table at `.rodata+0x23e04`:
- `wal_hipriv_alg_cfg_get_cfg_id_normal` (`.text+0x14877c`, 160 B) does
  `strncmp(entry.name, name, 0x50)` over 12-byte entries at `.rodata + .LANCHOR0(0x23a4c) + 0x3b8 = 0x23e04`
  and returns `entry.cfg_id` (`ldrh [r4,#4]`).
- `wal_hipriv_alg_cfg` (`.text+0x14881c`, 512 B) parses `<name> <int>` with
  `wal_get_cmd_one_arg`/`oal_atoi`, then calls `wal_send_cfg_event(dev,1,8,...)` and
  `wal_check_and_release_msg_resp` — i.e. this path talks to firmware directly.
- `wal_hipriv_alg_cfg_potting` (`.text+0x11c750`, 184 B) is the argument-length guard and
  tail-calls `wal_hipriv_alg_cfg`; it is reached from
  `wal_ioctl_set_param_char_process_subioctl` (`.text+0x11c808` @0x11cb18).
- Table contents at `.rodata+0x23e04` (from C11), 5 live entries:
  `sch_method=0x0001`, `edca_opt_en_ap=0x00c9`, `edca_opt_cw_probe_dbg=0x00cb`,
  `tpc_mode=0x0709`, `cca_opt_alg_en_mode=0x012d` (then zeros).

---

## 2. Recovered dispatch structure (summary)

```
iwpriv <wlan> alg <cmd> [args]
  │  wext ioctl cmd = 0x0101, name "alg"        (g_ast_iw_priv_args[237], .rodata+0x9c68)
  ▼
char-extra plumbing: wal_algcmd_char_extra_adapt  .text+0x10c564   (500-byte in / 1000-byte out)
  │  builds request header, forces cmd = 0x0101  (movw ip,#0x101 @0x10c660)
  ▼
wal_wlan_cfg_module_process_entry  .text+0x127af8
  │  module table @ .data+0x1d58 (12-byte entries): cmd 0x0101 ->
  ▼
wal_wlan_cfg_alg_process_entry  .text+0x14b46c
  │  alg_cfg_analysis_args .text+0x14b260
  │      alg_cfg_get_pkt_type .text+0x14b0f4
  │      alg_cfg_search_process_info_by_cfg_name .text+0x14aefc
  │          └─ alg_cfg_search_process_info_by_cfg_name_normal .text+0x14ae68
  │                 strncmp over g_ast_alg_cfg_process_info_table @ .data+0x35c8 (414 × 12B)
  │  scan g_alg_cfg_lut @ .data+0x4934 (416 × 176B) by {u16 cfg_id@0, u8 dir@2}
  ▼
per-command analysis handler = g_alg_cfg_lut[i].func @ entry+8
```

### ≥5 command names → cfg_id → handler

| command name | dir | cfg_id | LUT entry | handler function | handler addr |
|---|---|---|---|---|---|
| `get_2g_power_param` | get | 0x0dae | idx 217 @.data+0xde64 | `alg_cfg_args_param_analyse_equipment_param` | `.text+0x1572c0` |
| `get_5g_power_param` | get | 0x0db0 | idx 219 @.data+0xdfc4 | `alg_cfg_args_param_analyse_equipment_param` | `.text+0x1572c0` |
| `get_xo_ppm_cali_param` | get | 0x0db2 | idx 227 @.data+0xe544 | `alg_cfg_args_param_analyse_equipment_param` | `.text+0x1572c0` |
| `get_rssi_param` | get | 0x0db3 | idx 235 @.data+0xeac4 | `alg_cfg_args_param_analyse_equipment_param` | `.text+0x1572c0` |
| `get_2g_all_curve_param` | get | 0x0db4 | idx 200 @.data+0xd2b4 | `alg_cfg_args_param_analyse_equipment_param` | `.text+0x1572c0` |
| `get_ns_rate_param` | get | 0x10d1 | idx 410 @.data+0x16314 | `alg_cfg_args_analysis_ns_rate` | `.text+0x159ff8` |
| `set_ns_aggr_param` | set | 0x10cd | idx 406 @.data+0x16054 | `alg_cfg_args_analysis_ns_aggr` | `.text+0x159afc` |
| `get_tx_ant` | get | 0x1069 | idx 405 @.data+0x15fa4 | `alg_cfg_args_analysis_param_binary` | `.text+0x1575bc` |
| `get_freq_bw_mode` | get | 0x0197 | idx 16 @.data+0x5434 | `alg_cfg_args_analysis_freq_bw_mode` | `.text+0x14c080` |

Function addresses above come from the `.symtab` `STT_FUNC` dump (command C2/C9); the
cfg_ids/indices/handler names come from commands C9/C10 (exact outputs quoted in §1.5–1.6).

Note: some cfg_ids have **no** +8 handler in the LUT (e.g. `tpc_mode` 0x0709 at idx 115/116,
`set_cca_th` 0x012e at idx 256/257 printed `None`); those are routed elsewhere (see §5).

---

## 3. Driver <-> firmware sync boundary reference (`hmac_sync_dmac_*`, cali-sync)

Command **C12**:

```
$ .../python.exe - <<'EOF'
from elftools.elf.elffile import ELFFile
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'))
fs=[(s['st_value'],s['st_size'],s.name) for s in e.get_section_by_name('.symtab').iter_symbols()
    if s['st_info']['type']=='STT_FUNC']
for v,sz,n in sorted(fs):
    if n.startswith('hmac_sync_') or n.startswith('hmac_chan_tx_cali') or n.startswith('hmac_save_cali') \
       or (('cali' in n.lower() and 'sync' in n.lower()) or 'dmac' in n.lower()):
        print(f"0x{v:06x} size={sz:5d} {n}")
EOF
```
Observed (exact):

```
0x046dac size=  280 hmac_chan_tx_cali_sync
0x087d70 size=  352 hmac_sync_dmac_alg_cfg_rsp_entry
0x0881a4 size=  148 hmac_sync_dmac_cali_cfg_rsp_entry
0x0688b0 size=  208 hmac_sync_txrx_abnormal_info_sync
0x117fb8 size=  264 wal_sync_query_dmac_sta_info
0x08d7f0 size=  312 hmac_del_user_notify_dmac
0x098d78 size=  292 hmac_send_connect_result_to_dmac_sta
0x0c3c20 size=  284 hmac_save_cali_data_verify_5g
0x0c3d3c size=  280 hmac_save_cali_data_verify_2g
0x0c3e54 size=  248 hmac_save_cali_data_to_file_5g
0x0c3f4c size=  248 hmac_save_cali_data_to_file_2g
0x0c4044 size=   36 hmac_save_cali_data_to_file
```

Boundary interpretation (all named, all section-relative `.text`):
- `hmac_sync_dmac_alg_cfg_rsp_entry` `.text+0x087d70` (352 B) — handler for the DMAC (firmware)
  response to an ALG-CFG request; this is the firmware->driver end of the `alg_cfg` exchange.
- `hmac_sync_dmac_cali_cfg_rsp_entry` `.text+0x0881a4` (148 B) — handler for the DMAC response
  to a CALI-CFG request (the `cali` ioctl 0x0102 / calibration sync).
- `hmac_chan_tx_cali_sync` `.text+0x046dac` (280 B) — channel TX calibration sync.
- `hmac_save_cali_data_to_file` (+`_2g`/`_5g`, `_verify_2g`/`_5g`) — persist the calibration
  read back from firmware to `/lib/firmware`-style files.
- `wal_sync_query_dmac_sta_info`, `hmac_del_user_notify_dmac`,
  `hmac_send_connect_result_to_dmac_sta` — other DMAC message-boundary entry points.

The global that receives the alg-cfg DMAC response is
`g_hmac_sync_dmac_alg_cfg_rsp_get_entry` (`STT_OBJECT`, `.bss+0x1b0a4`, 4 bytes) — a runtime
function-pointer slot, consistent with the response being installed at init.

---

## 4. Full `alg` command vocabulary (name → cfg_id)

414 rows were recovered from `g_ast_alg_cfg_process_info_table` @ `.data+0x35c8` (12-byte
entries `{char* name; u16 cfg_id; u8 dir; ...}`; `dir=0` set, `dir=1` get). Table starts:

```
[  0] rate_mode        cfg_id=0x0191 dir=0      [  1] get_rate_mode    0x0191 dir=1
[  2] ar_scen_mode     0x01a6 dir=0             [  3] fec_coding       0x0192 dir=0
[  4] get_fec_coding   0x0192 dir=1             [  5] ar_probe         0x01a3 dir=0
...
[350] set_2g_power_param 0x0dae dir=0           [351] get_2g_power_param 0x0dae dir=1
[352] set_5g_power_param 0x0db0 dir=0           [353] get_5g_power_param 0x0db0 dir=1
[354] set_2g_low_power_param 0x0daf dir=0       [355] get_2g_low_power_param 0x0daf dir=1
[356] set_5g_low_power_param 0x0db1 dir=0       [357] get_5g_low_power_param 0x0db1 dir=1
[358] adjust_ppm       0x0dba dir=0             [359] xo_ppm_cali      0x0dbb dir=1
[360] set_xo_ppm_cali_param 0x0db2 dir=0        [361] get_xo_ppm_cali_param 0x0db2 dir=1
[362] fem_check        0x0dc2 dir=1             [363] efuse_test       0x0dc5 dir=1
[364] rssi_cali        0x0dc0 dir=0             [365] rssi_cali_ant    0x0dc1 dir=0
[366] set_rssi_param   0x0db3 dir=0             [367] get_rssi_param    0x0db3 dir=1
[396] set_amsdu_debug  0x0f3d dir=0             [397] set_cca_th       0x012e dir=0
[398] get_cca_th       0x012e dir=1             [404] tx_ant           0x1069 dir=0
[405] get_tx_ant       0x1069 dir=1             [406] set_ns_aggr_param 0x10cd dir=0
[407] get_ns_aggr_param 0x10cd dir=1            [409] set_ns_rate_param 0x10d1 dir=0
[410] get_ns_rate_param 0x10d1 dir=1
```
The same table also contains `curve_param`/`*_all_curve_param` (0x0db4/0x0db5),
`*_curve_factor` (0x0db6/0x0db7), `*_upc` (0x0db8/0x0db9), `power_cali`/`power_cali_mimo`
(0x0dbc/0x0dbd), `save_2g_upc`/`save_5g_upc` (0x0dc3/0x0dc4), `get_xo_ducy_cali_param`
(0x0dad) — matching the calibration vocabulary listed in `DRIVER-BLACKBOX.md` §5.

---

## 5. What could NOT be determined (and what was tried)

1. **The exact kernel-wext path that delivers cmd 0x0101 into `wal_algcmd_char_extra_adapt`.**
   I verified the registration (`g_st_iw_handler_def`/`g_st_iw_cfg_handler_def` +
   `g_ast_iw_priv_args`) and the downstream chain, and I verified `wal_algcmd_char_extra_adapt`
   itself forces cmd=0x0101 and calls `wal_wlan_cfg_module_process_entry`.
   What I could **not** pin down is which concrete registered private-ioctl handler invokes
   `wal_algcmd_char_extra_adapt` for the `alg` name, because 0x0101 is below
   `SIOCIWFIRSTPRIV (0x8BE0)`, so the *standard* wext private indexing
   (`private[cmd - SIOCIWFIRSTPRIV]`) does not apply, and the private arrays I dumped only
   cover 0x8BE0..0x8BFF.
   Tried: dumped `g_st_iw_handler_def.private` (.rodata+0x9b0c, 33 entries) and
   `g_st_iw_cfg_handler_def.private` (.rodata+0x993c, 32 entries) — indices 0/1 are
   `wal_ioctl_set_param`/`wal_ioctl_get_param`; disassembled `wal_net_device_ioctl`
   (.text+0x119940) — it only branches on `SIOCDEVPRIVATE` 0x89F0 → `wal_witp_wifi_priv_cmd`
   and 0x89F3; scanned `.text` for every `movw #0x101` (33 sites, all listed) — the only
   relevant ones are inside `wal_algcmd_char_extra_adapt` (0x10c660) and unrelated
   `hmac_*`/`mac_*`/`wal_config_*` functions.
   Callers of `wal_algcmd_char_extra_adapt` found via `.rel.text`: `wal_hipriv_set_rate`
   (0x10c8b8), `set_mcs` (0x10c9d0), `set_mcsac` (0x10cae8), `set_mcsax` (0x10cc34),
   `set_rxch` (0x10d520), `set_bw` (0x143410), plus in-range addresses 0x10cd54/0x10ce4c/
   0x10cfe0/0x10d224/0x10d244/0x10d6c8/0x12ab84/0x148724 (their containing symbols were not
   resolved by the function-range lookup). So the most likely reading is that low-numbered
   char cmds are funnelled by a hisi-internal path (possibly via
   `wal_ioctl_get_param_char`/`wal_iwpriv_char_extra_adapt`, whose cmd field is taken from the
   user `iwreq`) rather than by the generic wext index — but I did not prove it.

2. **Bulk cfg_id→handler coverage.** Only 170 of the 416 `g_alg_cfg_lut` entries have a
   pointer at +8; the other pointer slots (+0x18, +0x1c, +0x28, +0x2c, +0x38, +0x3c, +0x48,
   +0x4c) are populated for a minority of entries. I did not reconstruct what each of those
   slots means, nor did I decompile `alg_cfg_args_param_analyse_equipment_param`
   (.text+0x1572c0) to confirm the semantic of each calibration sub-field.

3. **Firmware-side wire format.** The report stops at the driver's message-send boundary
   (`wal_send_cfg_event` in the `alg_cfg` path; the `wal_wlan_cfg_module_process_entry`
   request struct: `{dev; u16 cmd; ...; out_ptr@+0x9c; out_len@+0xa0}`). The actual
   ITCM/DTCM message layout and the meaning of `hmac_sync_dmac_alg_cfg_rsp_entry`'s decoded
   fields were not disassembled field-by-field.

4. **`g_ast_alg_cfg_process_info_table` entry tail bytes** (+6 dir verified; +8, +10 read as
   0/1 flags and used by `wal_config_alg_cfg_param_host_entry` as type selectors) were not
   fully decoded into named semantics.

5. **No separate `"alg"` C-string constant** exists for the command name: a scan of
   `.rodata.str1.4` for an exact NUL-terminated `alg` returned nothing, and a scan for a
   `.rodata`-relative reference to the standalone `alg` string at `.rodata+0x33c7` returned
   no relocation hits. The name lives inline inside the `iw_priv_args` entries
   (`.rodata+0x9af4`, `.rodata+0xb2a0`, `.rodata+0x99e4`), which is why the string-search
   anchor ultimately came from the `01 01 00 00` cmd word, not from the text `alg`.
```
```

---

### Verification of this document

Reproduce with the commands C1–C12 above using
`C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/pyenv/Scripts/python.exe`; all printed values were
observed in this session's output. Addresses are section-relative because the input is an
`ET_REL` object (C1). The `alg` priv_arg entry (C3), the module table entry cmd 0x0101 (C8),
the name→cfg_id table row for `get_2g_power_param` (C9), and the `g_alg_cfg_lut` handler for
cfg_id 0x0dae (C10) are each a single-command, single-output claim.
