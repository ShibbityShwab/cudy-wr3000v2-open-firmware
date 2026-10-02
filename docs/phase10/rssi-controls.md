# Phase 10 - Reversing the two features 2.5.24 added: per-chip user limits and RSSI thresholds

Scope: local disassembly of the vendor driver module plus READ-ONLY probing of the running router.
Every claim below carries the command or byte-level excerpt it came from.

## 0. Inputs and provenance

| item | value |
| --- | --- |
| driver analysed | `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/versions/2.5.24/hi5622v100_wifi.ko` |
| sha256 | `de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521` |
| previous release | `build/versions/2.4.15/hi5622v100_wifi.ko` sha256 `22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2` |
| disassembly | ELF `.text` (ARM, sh_addr 0), capstone ARM mode, relocations resolved from `.rel.text`/`.rel.rodata` |
| tooling | `pyenv/Scripts/python.exe` (pyelftools + capstone 5.0.7), scratch scripts in `/c/tmp` |
| device | `root@192.168.10.1`, `Linux WR3000 5.10.201 armv7l` |
| running module | `/lib/modules/5.10.201/hi5622v100_wifi.ko` sha256 `de78ec07...` -> **byte-identical to the 2.5.24 file analysed** |

Reproduce the device hash:

```
$ ssh root@192.168.10.1 'sha256sum /lib/modules/5.10.201/hi5622v100_wifi.ko'
de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521  /lib/modules/5.10.201/hi5622v100_wifi.ko
```

Presence check of the seven names in both releases (`STT_FUNC` lookup in `.symtab`):

```
2.4.15: hmac_config_get_chip_max_user no   wal_config_get_chip_max_user no   wal_ioctl_get_max_user no
        wal_config_get_rssi_access_th no  wal_config_get_rssi_warn_th no
        wal_config_set_rssi_access_th no  wal_config_set_rssi_warn_th no
2.5.24: all seven YES
```

Related names that exist in **both** releases (so they are not part of the delta):
`wal_config_get_max_user`, `wal_config_set_max_user`, `wal_config_set_chip_max_user`,
`hmac_config_set_chip_max_user`, `hmac_config_get_max_user`, `wal_hipriv_set_chip_max_user`,
`wal_set_ap_max_user`, `wal_ioctl_get_param_char`, `wal_ioctl_get_param_char_process`.

## 1. Disassembly walk (what each new function reads/writes)

Symbol table (addresses are `.text`-relative; sizes from `st_size`):

| function | addr | size |
| --- | --- | --- |
| `hmac_config_get_chip_max_user` | `0x758b0` | `0x15c` |
| `wal_config_get_chip_max_user` | `0x12e398` | `0x4` |
| `wal_ioctl_get_max_user` | `0x113f34` | `0x124` |
| `wal_config_get_rssi_warn_th` | `0x12e9cc` | `0x60` |
| `wal_config_set_rssi_warn_th` | `0x12c880` | `0x90` |
| `wal_config_get_rssi_access_th` | `0x12ea2c` | `0x60` |
| `wal_config_set_rssi_access_th` | `0x12c910` | `0x90` |

### 1.1 `wal_config_get_chip_max_user` - a 4-byte tail-call thunk

```
=== wal_config_get_chip_max_user @ 0x12e398 size 0x4 ===
  0x12e398: feffffea   b   #0x12e398   ; reloc -> hmac_config_get_chip_max_user
```

It passes its three arguments straight through. `.rel.text+0x12e398` is `R_ARM_JUMP24` to
`hmac_config_get_chip_max_user`; there is no other body.

### 1.2 `hmac_config_get_chip_max_user(r0 = mac handle, r1 = u16* out_count, r2 = u16 out[3])`

Full body (relocations resolved; `; reloc ->` shows the real callee):

```
0x0758b0: cmp r2,#0 ; cmpne r1,#0 ; push {r4,r5,r6,r7,r8,sb,lr} ; cmp r0,#0
          orreq r4,r4,#1 ; cmp r4,#0 ; bne 0x75998        ; arg-validity gate -> error, return 0x64
0x0758d8: mov r6,r0          ; r6 = mac handle
0x0758dc: ldrb r0,[r0,#2]    ; read mac id = *(u8*)(handle+2)
0x0758e0: mov r7,r1          ; r7 = out_count
0x0758e4: mov r5,r2          ; r5 = out buffer
0x0758e8: bl  mac_res_get_mac_chip        ; r8 = chip struct
0x0758f8: beq 0x759e4                     ; chip==0 -> oam_error_log0, return 0x64
0x0758fc: ldrb r0,[r8]       ; chip id = *(u8*)(chip+0)
0x075900: mov r1,r4          ; band index (0, then 1)
0x075904: bl  hmac_band_get_by_id         ; r0 = band struct
0x07590c: beq 0x759c4                     ; band NULL -> oam_warning_log1, next band
0x075910: add r3,r0,#0x1000
0x075914: ldrb r3,[r3,#0xd1] ; flag = *(u8*)(band+0x10d1)
0x075918: cmp r3,#0
0x07591c: bne 0x75988
0x075920: ldrh r3,[r0,#0x18] ; read u16 at band+0x18
0x075924: strh r3,[r5,#2]    ; out[1]
0x075928: cmp r4,#1 ; mov r4,#1 ; bne 0x758fc   ; loop once for band 1
0x075988: cmp r3,#1
0x07598c: ldrheq r3,[r0,#0x18]
0x075990: strheq r3,[r5,#4]  ; when flag==1: out[2] = u16 at band+0x18
0x075934: ldrh r2,[r8,#0xfc] ; u16 at chip+0xfc
0x07593c: strh r2,[r5]       ; out[0]
0x075938: mov r3,#8
0x075940: strh r3,[r7]       ; *out_count = 8
0x075944: ...
0x075978: bl  oam_warning_log3   ; line 0x12b, module 0x5d4; logs out[1], out[0], out[2]
0x07597c: mov r0,#0 ; pop ; return 0
```

Reads/writes:
- `*(u8*)(handle+2)` (mac id), `*(u8*)(chip+0)`, `*(u16*)(chip+0xfc)`.
- `*(u8*)(band+0x10d1)` - a one-byte selector: 0 -> value lands in `out[1]` (`+2`), 1 -> `out[2]` (`+4`).
- `*(u16*)(band+0x18)` - the per-band value.
- Writes `out[0..4]` (three `u16`) and `*out_count = 8`.
- Error paths return `0x64` (100); success returns 0.
- **No MMIO / register access.** All accesses are to driver software structures returned by
  `mac_res_get_mac_chip` / `hmac_band_get_by_id`.

Cross-reference to `opensource/docs/phase7/dump-semantics.md`: that report establishes that every one
of the 72 dumped windows is chip MMIO/BAR space reached through `oal_pcie_devca_to_hostva`, and that
driver code touches only 7 windows, all in the `soc_register`/glue group. None of those offsets appear
here - `+0x18`, `+0xfc`, `+0x10d1` are fields inside host `mac_chip`/`band` structures, not BAR
addresses. This function touches **0 register windows**.

### 1.3 `wal_ioctl_get_max_user` - firmware round-trip + text formatting

```
0x113f34: push {r4,r5,r6,r7,lr} ; sub sp,#0x1c
0x113f44: ldr r3,[pc,#0x108] ; ldr r3,[r3] ; str r3,[sp,#0x14]   ; stack canary
0x113f58: ldrh r7,[sp,#0x30]        ; 7th argument (u16) - the iwpriv reply-size cap
0x113f50: mov ip,#0xeb
0x113f70: mov r2,#2 ; add r3,sp,#0xe ; strh ip,[sp,#0xe]        ; request buffer: u16 id = 0xeb
0x113f6c: str r2,[sp]                                            ; arg 1 of wal_send_cfg_event = 2
0x113f74: str r3,[sp,#4] ; mov r2,#2 ; add r3,sp,#0x10           ; arg 3 = &rep
0x113f80: bl  wal_send_cfg_event                                 ; firmware request, id 0xeb
0x113f84: subs r4,r0,#0 ; bne 0x113ff8                           ; error path -> oam_error_log0, return -ret
0x113f8c: ldr ip,[sp,#0x10]                                      ; rep = reply pointer
0x113f90: cmp ip,#0 ; beq 0x113ff8
0x113f98: ldrh r2,[ip,#0x14]     ; reply field +0x14
0x113fa0: ldrh r3,[ip,#0x10]     ; reply field +0x10
0x113fac: ldrh r2,[ip,#0x12]     ; reply field +0x12
0x113fa8: str r2,[sp,#4] ; str r2,[sp]                          ; format args 2 and 3
0x113fbc: bl  sprintf_s                                          ; r0=r5 (out buf), r2=.LC103 (fmt), r3=+0x10, stack=+0x12,+0x14
0x113fc4: ldrhge r0,[r6,#4] ; addge r0,r0,r3 ; strhge r0,[r6,#4] ; wrqu->data.length += strlen(rep)
0x113fec: mov r0,r4 ; pop ; return 0
```

Format string (present in `.rodata.str1.4` at `+0x7e790`; matched byte-for-byte by the device output in
§4):

```
Max Assoc User: Chip[%u] 2.4G[%u] 5G[%u].\n
```

Reads: firmware reply struct fields `+0x10`, `+0x12`, `+0x14` (three `u16`). Writes: the caller's reply
buffer via `sprintf_s`, and `wrqu->data.length` (`[r6+4]`). `r7` (the 7th arg, a `u16` from the stack
at `sp+0x30`) is forwarded to `wal_send_cfg_event`. Error paths log `oam_error_log0` at module line
`0x4ef` and return `-ret`; the null-reply path returns `-1` (`mvn r4,#0`). No MMIO.

### 1.4 `wal_config_get_rssi_warn_th` / `wal_config_get_rssi_access_th` - read one byte

```
=== wal_config_get_rssi_warn_th @ 0x12e9cc size 0x60 ===
  0x12e9cc: str lr,[sp,#-4]! ; mov ip,r0        ; r0 = wal handle
  0x12e9d4: movw r3,#0 ; movt r3,#0  ; reloc -> .LC20
  0x12e9e0: ldr lr,[ip,#0x448]                  ; read 32-bit slot at handle+0x448
  0x12e9e4: mov r0,#4
  0x12e9ec: movw r3,#0x3e3
  0x12e9f0: strb lr,[r2]                        ; *out = low byte of slot
  0x12e9f4: strh r0,[r1]                        ; *out_len = 4
  0x12e9fc: ldr r1,[ip,#0x448]                  ; re-read for the log
  0x12e9e0..: strb/formats via oam_info_log3    ; fmt 'wal_config_get_rssi_warn_th: %d, %d, vap_id: %d'
  0x12ea20: mov r0,#0 ; pop {pc} ; return 0
```

`wal_config_get_rssi_access_th` is byte-for-byte the same shape, offset **`+0x44c`**, log string
`wal_config_get_rssi_access_th: %d, %d, vap_id: %d`, module line `0x3f6`.

Reads: `*(u32*)(handle+0x448)` (warn) / `*(u32*)(handle+0x44c)` (access). Writes: one byte to `*r2`,
`4` to `*r1`, and the log. Returns 0 unconditionally. No MMIO.

### 1.5 `wal_config_set_rssi_warn_th` / `wal_config_set_rssi_access_th` - write one byte

```
=== wal_config_set_rssi_warn_th @ 0x12c880 size 0x90 ===
  0x12c880: push {r4,lr}
  0x12c884: cmp r2,#0 ; cmpne r0,#0 ; mov r3,r0
  0x12c894: moveq r4,#1 ; movne r4,#0 ; beq 0x12c8dc   ; null check
  0x12c8a0: ldrb r0,[r2]             ; r0 = *(u8*)input  (zero-extended)
  0x12c8a4: movw r1,#0 ; reloc -> .LC31
  0x12c8ac: str r0,[r3,#0x448]       ; *(u32*)(handle+0x448) = value
  0x12c8b0: str r0,[sp,#8]
  0x12c8b8: ldrb r3,[r2]
  0x12c8c0: stm sp,{r1,r3}           ; fmt 'wal_config_set_rssi_warn_th: %d,  %d!'
  0x12c8cc: bl  oam_info_log2
  0x12c8d0: mov r0,r4 ; pop {r4,pc}  ; return 0
  0x12c8dc: ... oam_error_log2, return 0x64
```

`wal_config_set_rssi_access_th` is identical with offset **`+0x44c`** and log string
`wal_config_set_rssi_access_th: %d,  %d!`; null-arg log `wal_config_set_rssi_access_th: pst_mac_vap/puc_param is null ptr %d, %d!`.

Reads: `*(u8*)r2` (the value). Writes: `*(u32*)(handle+0x448)` / `+0x44c` (a 32-bit store whose value
is always 0..255), the log, and returns 0 (or `0x64`=100 on null args). No MMIO.

**Only consumer of `handle+0x448`/`+0x44c` in the whole module.** A full per-function scan for any
memory operand with displacement `0x448`/`0x44c` across all 3,847 `.text` functions finds exactly these
four functions (plus four unrelated PC-relative loads and one `mac_vap` field `[r4,#0x44c]` in
`mac_vap_init`, a different structure). So nothing else in the module reads or compares these fields.

## 2. Reachability from userspace

There are two independent paths, both in the wext (Wireless Extensions) private-ioctl namespace.

### 2.1 The interface table that names them

`g_st_iw_handler_def` (`.data` at `0x1c5c`, `struct iw_handler_def`) resolves to:

```
+0x00 standard        = .rodata 0x9b90   (+4 num_standard = 54)
+0x06 num_private     = 33
+0x08 num_private_args= 270
+0x0c private         = .rodata 0x9b0c
+0x10 private_args    = .rodata 0x9c68   (= symbol g_ast_iw_priv_args, size 0x1950 = 270*24)
```

`g_ast_iw_priv_args` is `struct iw_priv_args[270]` (`{u32 cmd; u16 set_args; u16 get_args; char name[16];}`).
The entries relevant here:

```
entry[ 88] cmd=0x021a set=0x4801 get=0x0000 name='usernum'
entry[ 89] cmd=0x021a set=0x0000 get=0x4801 name='get_usernum'
entry[ 90] cmd=0x00eb set=0x2032 get=0x0000 name='set_max_user'
entry[ 91] cmd=0x00eb set=0x0000 get=0x21f4 name='get_max_user'
entry[227] cmd=0x07ed set=0x4801 get=0x0000 name='set_rssi_warn'
entry[228] cmd=0x07ed set=0x0000 get=0x4801 name='get_rssi_warn'
entry[229] cmd=0x07ee set=0x4801 get=0x0000 name='set_rssi_acce'
entry[230] cmd=0x07ee set=0x0000 get=0x4801 name='get_rssi_acce'
```

`private` handler array (`.rodata 0x9b0c`, idx = `cmd - SIOCIWFIRSTPRIV`):

```
idx  1 (SIOCIWFIRSTPRIV+0x1 = 0x8be1) -> wal_ioctl_get_param
idx 11 (SIOCIWFIRSTPRIV+0xb = 0x8beb) -> wal_ioctl_get_param_char
idx 15 (SIOCIWFIRSTPRIV+0xf = 0x8bef) -> wal_ioctl_set_param_char
```

**So the userspace interface is the wext private ioctl family `SIOCIWFIRSTPRIV+…` (iwconfig/iwpriv),
with the command names above; `iw`/nl80211 is not involved.**

### 2.2 The `max_user` (`0xeb`) path

```
userspace iwpriv "get_max_user"
  -> ioctl SIOCIWFIRSTPRIV+0xb (0x8BEB)
  -> wal_ioctl_get_param_char (0x10dd20, handler idx 11)
  -> wal_ioctl_get_param_char_process (0x114058)     [.rel.text+0x10de04, R_ARM_CALL]
       id = wrqu->data.flags = *(u16*)(wrqu+6)  (0x10dd34: ldrh r4,[r2,#6])
       cmp ip,#0xeb ; beq 0x114214                (0x1140f8)
  -> b wal_ioctl_get_max_user                        [.rel.text+0x114244, R_ARM_JUMP24]
  -> wal_send_cfg_event(id=0xeb) -> firmware -> reply -> sprintf_s(...)
```

`wal_ioctl_get_max_user` is reached from exactly one site, `.rel.text+0x114244`, inside
`wal_ioctl_get_param_char_process`. `wal_ioctl_get_param_char_process` is called from exactly one site,
`.rel.text+0x10de04`, inside `wal_ioctl_get_param_char`.

`set_max_user` (`0xeb`) is the set-side of the same entry. The pre-existing `wal_hipriv_set_chip_max_user`
(`0x143498`) parses a text line with `wal_get_cmd_one_arg`/`oal_atoi` and is **verified** to send id `0xeb`
(`mov ip,#0xeb; strh ip,[sp,#0xc]; bl wal_send_cfg_event`). The ioctl writer is tentatively
`wal_ioctl_set_param_char` (idx 15, mirror of the get handler) but its `0xeb` handling was **not**
confirmed (its immediate scan shows no `0xeb` compare) and no write was performed on the device to
test it - mark as inferred, not verified.

### 2.3 The RSSI-threshold (`0x7ed`/`0x7ee`) and `wal_config_*` path

`wal_config_get_chip_max_user` (and the four `rssi_*_th` functions) are **not called by code at all**;
they are only stored as function pointers in a second `.rodata` table:

```
base 0x8618 ($d / .LANCHOR0, .rodata)
record = 12 bytes, stride 0xc: { u32 id; u32 get; u32 set; }  (the walker compares the low u16: ldrh [r2])
0x8dbc: id=0x021a get=wal_config_get_max_user            set=wal_config_set_max_user
0x8dc8: id=0x00eb get=wal_config_get_chip_max_user       set=wal_config_set_chip_max_user
0x8dd4: id=0x021b get=wal_config_get_sta_list            set=0
...
0x9188: id=0x07ed get=wal_config_get_rssi_warn_th        set=wal_config_set_rssi_warn_th
0x9194: id=0x07ee get=wal_config_get_rssi_access_th      set=wal_config_set_rssi_access_th
0x91a0: id=0x07ef get=0                                 set=wal_config_fbt_start_scan_batch
```

(`rel.rodata` gives `R_ARM_ABS32` at +0x8dc0/+0x8dc4, +0x8dcc/+0x8dd0, +0x918c/+0x9190, +0x9198/+0x919c`.)

The table is walked by `wal_config_process_entry` (`0xfc91c`): `ldrh r8,[r4]` reads the incoming id,
then a loop `ldrh r1,[r2]; cmp r1,r8; add r2,r2,#0xc` linearly scans records. Its caller chain is:

```
wal_config_process_entry   (0xfc91c, size 0x368)  [.rel.text+0xfd070]
  <- wal_config_process_pkt (0xfcd9c, size 0x8e0)  [.rel.text+0xfd958, +0xfd9a8]
  <- wal_recv_config_cmd    (0xfd80c, size 0x270)  registered as a hook by
                             wal_drv_cfg_func_hook_init (0xfb9f8) [.rel.text+0xfba00/+0xfba04, MOVW/MOVT]
```

So the `wal_config_*` handlers run on the **host config-command receive path**, i.e. after a config
event round-trips through the firmware (`wal_send_cfg_event` → firmware → `wal_recv_config_cmd`),
not directly from the ioctl. The ioctl side for the same ids is:

```
iwpriv "get_rssi_warn"/"get_rssi_acce"/"get_usernum"  -> ioctl 0x8BE1 -> wal_ioctl_get_param (idx 1)
iwpriv "set_rssi_warn"/"set_rssi_acce"                -> ioctl 0x8BE0 -> wal_ioctl_set_param (idx 0)
   wal_ioctl_get_param  (0x11cfa8) switches on the id and, for ids it does not special-case,
                        tail-calls wal_ioctl_get_param_no_wait (0x11b8f8)   [.rel.text+0x11d10c]
   wal_ioctl_get_param_no_wait: ldr r2,[r4]            ; id from extra[0]
                                strh r2,[sp,#0xe]      ; request {id}
                                bl wal_send_cfg_event  ; firmware round-trip
                                ...; bl memcpy_s       ; copies 4 bytes from reply+0x10 into extra
   wal_ioctl_set_param  (0x11cd80) calls wal_ioctl_set_param_check_result (0x11ba6c)
                        [.rel.text+0x11ce38] which validates ids 0x7ed/0x7ee
```

### 2.4 Interface a user would call (names)

```
iwpriv <iface> get_max_user      # ioctl 0x8BEB            (strace-verified)
iwpriv <iface> get_rssi_warn     # ioctl 0x8BE1 getparam   (strace-verified)
iwpriv <iface> get_rssi_acce     # ioctl 0x8BE1 getparam   (strace-verified)
iwpriv <iface> set_max_user <n>  # set side of the 0x00eb entry (not straced; no write performed)
iwpriv <iface> set_rssi_warn <n> # set side of the 0x07ed entry (not straced; no write performed)
iwpriv <iface> set_rssi_acce <n> # set side of the 0x07ee entry (not straced; no write performed)
```

The `get_*` rows are proven by `strace` in §4. The `set_*` names are present in the same
`SIOCGIWPRIV` listing (see §4) and the device-side set validator was located in code (§3), but the
actual write calls were deliberately not issued (READ-ONLY constraint), so their ioctl numbers are
inferred, not measured.

There is no matching `alg` command: the 414-entry `g_ast_alg_cfg_process_info_table` (`ko_algtable.csv`)
has no `max_user` entry, and its RSSI entries are unrelated (`edca_opt_rssi_th` 0x00d4,
`spectral_rssi_thr` 0x0580, `spectral_rssi_nb_thr` 0x0581, `sounding_rssi_limit` 0x096a, `rssi_cali`
0x0dc0). So these controls are **not** reachable through the `alg`/`cali` proc or iwpriv command; they
are their own wext private commands as listed above.

## 3. What the thresholds mean numerically

`rssi_warn` and `rssi_access` are stored/returned as a **single byte**; the driver performs no
scaling, negation or offset anywhere (the setters do `ldrb` then `str`, the getters do `ldr` then
`strb`). The only place a numeric domain appears is the set-side validator
`wal_ioctl_set_param_check_result` (`0x11ba6c`):

```
id 0x7ed (rssi_warn):                          id 0x7ee (rssi_access):
0x11bb74: sub r3,r5,#0x64   ; value-100        0x11bbd4: sub r3,r5,#0x64 ; value-100
0x11bb78: lsr r0,r5,#0x1f   ; sign bit         0x11bbd8: cmp r3,#0
0x11bb7c: cmp r3,#0                             0x11bbdc: ble 0x11bc18     ; value <= 100 -> non-neg check
0x11bb80: orrgt r0,r0,#1    ; value>100 -> bad 0x11bbe0: cmp r5,#0xff
0x11bb84: cmp r0,#0                             0x11bbe4: beq 0x11bc20     ; value == 255 -> accepted
0x11bb88: beq ok                                0x11bc18: cmp r5,#0
          (else) 0x11bbb0: mvn r0,#0x15 = -22 = -EINVAL         0x11bc1c: blt error
```

Explicit numeric limits:
- `set_rssi_warn`: accepted only for `0 <= value <= 100`, else `-EINVAL` (-22).
- `set_rssi_acce`: accepted for `0 <= value <= 100` **or `value == 255`** (0xFF), else `-EINVAL`.
  `255` is a sentinel (the value the getter returns by default), not a threshold.
- Both getters return the raw byte (0..255).

Observed defaults on the running device: `get_rssi_warn` = `0`, `get_rssi_acce` = `255` (= disabled).

Units: the code shows **no** dBm conversion and no comparison anywhere in the module (see the full
`0x448`/`0x44c` consumer scan in §1.5). So the stored quantity is a raw `0..100` level, or `0xFF` for
off. There is one format string suggesting the intent -
`{hmac_ap_rx_auth_check_rssi::auth_rssi[%d] is lower than rssi_access_th[%d]}` at
`.rodata.str1.4+0x20d01` - but **no relocation in the module references it** (searched `R_ARM_MOVW/MOVT`
and `R_ARM_ABS32` across every `.rel.*` section), so it is dead/orphaned text and I cannot cite a live
dBm comparison. Whether the 0..100 value is dBm, dB relative to some reference, or a
percentage/quality index is therefore **not determinable from this driver** - mark unresolved.

`max_user` (`0xeb`) is not a threshold: `get_max_user` returns three association-user counts. On the
device it returns `Chip[128] 2.4G[64] 5G[64]`, i.e. per-chip 128 and per-band 64.

## 4. Reachable on the running device? Yes - probe and output

All probes read-only (`get_*` only; nothing was set). The password was fed through a throwaway
askpass script that was deleted afterwards.

Probe (exact commands):

```bash
printf '#!/bin/sh\necho RouterRoot-9x\n' > /tmp/askpass.sh; chmod +x /tmp/askpass.sh
cd /tmp && SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0 timeout 60 \
  ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 root@192.168.10.1 \
  'iwpriv vap0 get_max_user; iwpriv vap0 get_rssi_warn; iwpriv vap0 get_rssi_acce; iwpriv vap0 get_usernum'
```

Output:

```
vap0      get_max_user:Max Assoc User: Chip[128] 2.4G[64] 5G[64].
vap0      get_rssi_warn:0
vap0      get_rssi_acce:255
vap0      get_usernum:64
```

Exit statuses: `get_max_user rc=0`, `get_rssi_warn rc=0`.

The driver exposes the names in its `SIOCGIWPRIV` table on the live device (full un-truncated list,
258 lines; relevant excerpt):

```
vap0      Available private ioctls :
          ...
          set_max_user     (00EB) : set  50 char  & get   0
          get_max_user     (00EB) : set   0       & get 500 char
          ...
          set_rssi_warn    (07ED) : set   1 int   & get   0
          get_rssi_warn    (07ED) : set   0       & get   1 int
          set_rssi_acce    (07EE) : set   1 int   & get   0
          get_rssi_acce    (07EE) : set   0       & get   1 int
```

`strace` of the actual syscalls (confirms the ioctl mapping in §2):

```
$ strace -f -e trace=ioctl iwpriv vap0 get_max_user | grep 0x8b
ioctl(3, _IOC(_IOC_NONE, 0x8b, 0xeb, 0), 0x...) = 0      # 0x8BEB = SIOCIWFIRSTPRIV+0xb
vap0      get_max_user:Max Assoc User: Chip[128] 2.4G[64] 5G[64].

$ strace -f -e trace=ioctl iwpriv vap0 get_rssi_warn | grep 0x8b
ioctl(3, _IOC(_IOC_NONE, 0x8b, 0xe1, 0), 0x...) = 0      # 0x8BE1 = SIOCIWFIRSTPRIV+0x1
vap0      get_rssi_warn:0

$ strace -f -e trace=ioctl iwpriv vap0 get_rssi_acce | grep 0x8b
ioctl(3, _IOC(_IOC_NONE, 0x8b, 0xe1, 0), 0x...) = 0
vap0      get_rssi_acce:255
```

(The `iwpriv` binary is a symlink to the stock `iwconfig`; each call first issues `SIOCGIWPRIV` to read
the table, then the private ioctl shown above.)

Cleanup, shown cleaned:

```
$ rm -f /tmp/askpass.sh; ls -la /tmp/askpass.sh
ls: cannot access '/tmp/askpass.sh': No such file or directory
```

No files were written on the device and no `set_*` command was ever issued.

## 5. Explicit limits (summary)

- `get_max_user` (`0xeb`): returns a formatted string; three `u16` association counts from the
  firmware reply (`Chip`, `2.4G`, `5G`); no driver-side clamping. Device values: `128/64/64`.
- `set_max_user` (`0xeb`): the set side of the `0x00eb` entry; the text path
  `wal_hipriv_set_chip_max_user` is verified to send id `0xeb` to `wal_send_cfg_event`; the ioctl set
  path was not exercised (read-only). The `alg` table has no cfg_id `0xeb` (grep of
  `ko_algtable.csv` for `,0x0*0?eb,` -> no rows), so this control is only on the wext/`hipriv` surface.
- `rssi_warn` set: `0..100` inclusive, else `-EINVAL`.
- `rssi_acce` set: `0..100` inclusive, or the single sentinel `255` (0xFF); else `-EINVAL`.
- Both RSSI getters return a single unsigned byte (`0..255`); the field is the low byte of a 32-bit
  slot at wal-handle `+0x448` (warn) / `+0x44c` (access).
- `hmac_config_get_chip_max_user` writes a 3-element `u16` array and sets `*out_count = 8`; it
  requires a non-NULL handle, out buffer and count pointer, and returns `100` on failure.
- Units of the RSSI values: raw `0..100` (plus `0xFF` off) per the code; no dBm conversion exists in
  the driver, so "dBm" cannot be asserted from the binary.

## 6. Unresolved / not claimed

1. **True unit of the RSSI thresholds (dBm vs raw index).** The only comparison-shaped evidence is the
   orphaned format string `{hmac_ap_rx_auth_check_rssi::auth_rssi[%d] is lower than rssi_access_th[%d]}`
   (`.rodata.str1.4+0x20d01`) with no relocation pointing at it; no live reader/compare of `+0x448`/
   `+0x44c` exists in this module. So the meaning of the number beyond "0..100 level, 255 = off" is not
   recoverable here (it may be enforced in the frozen firmware blob).
2. **The firmware-side handler for ids `0xeb`/`0x7ed`/`0x7ee`.** The host send path
   (`wal_send_cfg_event`) and the host receive path (`wal_recv_config_cmd` → table) are proven, but the
   blob itself is frozen across 2.4.15/2.5.24 (`docs/phase9/version-diff.md`) and was not re-analysed
   for these ids in this phase.
3. **The `hmac_config_get_chip_max_user` field semantics** (`band+0x18`, `chip+0xfc`, selector
   `band+0x10d1`) are described by offset only; no type/struct name is present in the symbol table.
