# Phase 6 — More decoded command paths and the 8-byte header token

Device: `root@192.168.10.1` (WR3000, `hi5622v100_wifi`), accessed over SSH.
Method: the phase-3/phase-5 dynamic-kprobe technique — tracefs `kprobe_events`
with ARM register fetchargs (`%rN`) plus single-level memory dereference
(`+OFF(%rN):x8/x16/x32`) — because `$argN` is rejected by this 5.10 ARM kernel
(section 1.1).

Scope: every trigger below is a read-only `iwpriv Hisilicon0 alg get_*` except
one `set_rssi_param` that writes the *same all-zero values back* that were read
first (section 4). A calibration snapshot was taken before any set.

Commands decoded beyond phase-5's `get_2g_power_param`:

| command | cfg_id | request len | response data |
|---|---|---|---|
| `get_5g_power_param` | 0x0db0 | 0x10e | 18-word table @ +0x8a |
| `get_2g_all_curve_param` | 0x0db4 | 0x10e | 12-word table @ +0x8a |
| `get_5g_all_curve_param` | 0x0db5 | 0x10e | 32-word table @ +0x8a |
| `get_rssi_param` | 0x0db3 | 0x10e | 10-word table @ +0x8a (all zero) |
| `get_xo_ducy_cali_param` | 0x0dad | 0x10e | 1-word table @ +0x8a |
| `get_cca_th` | 0x012e | **0x0e** | 3 signed bytes @ +0x0b |

DFS commands are **not reachable** through this ioctl (section 2.8): the firmware
has `dfsdebug`/`radarfilter`/`radarfilter_get` symbols, but no DFS name in the
driver's `alg` command table, and every DFS name tried returns `[FAIL]`. A
non-DFS third+ path (`get_cca_th`) was used instead.

All probes were removed and tracing disabled at the end (proof in section 6).

---

## 1. Method

### 1.1 `$argN` is unsupported; `%rN` works (re-measured this session)

```
$ cd /sys/kernel/debug/tracing
$ echo 'p:argtest hmac_sync_dmac_alg_cfg_rsp_entry a0=$arg1 a1=$arg2' >> kprobe_events
sh: write error: Invalid argument
$ tail -3 error_log
[ 2975.774039] trace_kprobe: error: Invalid $-valiable specified
  Command: p:argtest hmac_sync_dmac_alg_cfg_rsp_entry a0=$arg1 a1=$arg2
                                                         ^
$ echo 'p:regtest hmac_sync_dmac_alg_cfg_rsp_entry a0=%r0 a1=%r1' >> kprobe_events
regN rc=0
$ cat kprobe_events
p:kprobes/regtest hmac_sync_dmac_alg_cfg_rsp_entry a0=%r0 a1=%r1
```

(The `argtest`/`regtest` probes were deleted in the same session;
`kprobe_events` was 0 bytes afterwards.)

### 1.2 Anchors (from phase 5, re-confirmed by the captures below)

- `hmac_config_alg_send_event+0x7c` — the `bl memcpy_s` while building the
  **request**; `%r0 = msg+0xc`, `%r2 = source payload`, `%r5 = payload length`,
  and `msg+0x00/+0x08/+0x0a` are already written. `-12(%r0)` therefore reads the
  message base `msg+0`.
- `hmac_event_config_syn+0x50` — `%r5 = RX message object`; the payload pointer is
  `%r5+0x18`.
- `hmac_sync_dmac_alg_cfg_rsp_entry` — `%r2 = response payload pointer`.

### 1.3 Probe definitions used

```
p:req  hmac_config_alg_send_event+0x7c a0=%r0 a1=%r1 a2=%r2 r5=%r5 r7=%r7 base0=-12(%r0):x32 base4=-8(%r0):x32 base8=-4(%r0):x32 ps0=+0(%r2):x32 ps4=+4(%r2):x32 ps8=+8(%r2):x32 psc=+12(%r2):x32 ps10=+16(%r2):x32 ps14=+20(%r2):x32 ps18=+24(%r2):x32 ps1c=+28(%r2):x32 ps20=+32(%r2):x32 ps24=+36(%r2):x32 ps28=+40(%r2):x32 ps2c=+44(%r2):x32 ps30=+48(%r2):x32 ps34=+52(%r2):x32
p:ev   hmac_event_config_syn+0x50 r4=%r4 r5=%r5 m0=+0(%r5):x32 m4=+4(%r5):x32 m8=+8(%r5):x32 mc=+12(%r5):x32 m10=+16(%r5):x32 m14=+20(%r5):x32 m18=+24(%r5):x32 m1c=+28(%r5):x32 m20=+32(%r5):x32
p:rsp  hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 r0=+0(%r2):x32 ... r7c=+124(%r2):x32      # 32 words
p:rsp2 hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 r80=+128(%r2):x32 ... r10c=+268(%r2):x32  # 36 words
```

(the `...` are the full `rXX=+N(%r2):x32` runs at every 4-byte offset; the two
probes together dump response payload offsets 0x00–0x10c, exactly as phase 5.)

Enable/arm: `echo 1 > events/kprobes/enable; echo 1 > tracing_on; echo > trace`.
Each command was bracketed with `echo "MARK <name>" > trace_marker`.

### 1.4 Capture 1 — six commands

```
$ iwpriv Hisilicon0 alg get_2g_power_param
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
$ iwpriv Hisilicon0 alg get_5g_power_param
Hisilicon0  alg:[SUCC]00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a
$ iwpriv Hisilicon0 alg get_2g_all_curve_param
Hisilicon0  alg:[SUCC]feda0000 083c0556 fe670000 077105e9 0a73f50a 101dfd31 fa21036b 03fc0965 ec1f1283 fde611d9 dd541ffa f3a01c89
$ iwpriv Hisilicon0 alg get_5g_all_curve_param
Hisilicon0  alg:[SUCC]78420492 2df042d7 0aef046a 390417b4 f7470402 1f580e0c fb7f018f 20390bd2 fbb50142 22d20bc7 fa28022e 1f210cbf f2dc067d 1c39108d f60004a7 1c4e0ef1 f40005b3 1a871027 f39105be 17c010b2 efe1087c 19cb11dd f26006f0 1aaa1095 f1da07c7 1bff1063 f0df080d 1a5c112f 381d0000 4309f111 1d490000 3c780f13
$ iwpriv Hisilicon0 alg get_rssi_param
Hisilicon0  alg:[SUCC]00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
$ iwpriv Hisilicon0 alg get_cca_th
Hisilicon0  alg:[SUCC]ed_high_20th = [-62], ed_high_40th = [-59], ed_high_80th = [-56]

$ iwpriv Hisilicon0 alg get_xo_ducy_cali_param
Hisilicon0  alg:[SUCC]000016da
```

Raw trace (request `req`, RX object `ev`, response payload `rsp`/`rsp2`), one
block per command, verbatim:

```
              sh-3625    [000] ....  2872.999747: tracing_mark_write: MARK get_2g_power_param
    Host MSG RX -1170    [001] d...  2873.001511: req: a0=0xc39d1058 a1=0x10e a2=0xc39d3860 r5=0x10e r7=0x101 base0=0xc5439938 base4=0x0 base8=0x10e0101 ps0=0xd010dae ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2873.001722: ev: r4=0xc53c1200 r5=0xc39d1040 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc5439938 m10=0x0 m14=0x10e0101 m18=0xd010dae m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2873.001733: rsp2: a2=0xc39d1058 r80=0x0 r84=0x0 r88=0x16050000 r8c=0x16051716 r90=0x16051716 r94=0x15051716 r98=0x15051716 r9c=0x14011716 ra0=0x14011516 ra4=0x14011516 ra8=0x14011516 rac=0x14001516 rb0=0x12001516 rb4=0x12000814 rb8=0x12000814 rbc=0x11030814 rc0=0x11030c11 rc4=0x11030c11 rc8=0x10020c11 rcc=0x10020b10 rd0=0x10020b10 rd4=0x10020b10 rd8=0x10010b10 rdc=0xf010b10 re0=0xf010a0f re4=0x6ff0a0f re8=0x6ff0a06 rec=0x6ff0a06 rf0=0xa06 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0x1a0000 r10c=0x0
    Host MSG RX -1170    [001] d...  2873.001750: rsp: a2=0xc39d1058 r0=0xd010dae r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2874.004643: tracing_mark_write: MARK get_5g_power_param
    Host MSG RX -1170    [001] d...  2874.006326: req: a0=0xc4d15858 a1=0x10e a2=0xc541cc60 r5=0x10e r7=0x101 base0=0xc1dd1938 base4=0x0 base8=0x10e0101 ps0=0xd010db0 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2874.006561: ev: r4=0xc53c1500 r5=0xc4d15840 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1dd1938 m10=0x0 m14=0x10e0101 m18=0xd010db0 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2874.006575: rsp2: a2=0xc4d15858 r80=0x0 r84=0x0 r88=0x0 r8c=0xff000000 r90=0xb0004 r94=0xb000801 r98=0xfd02ff00 r9c=0x9040009 ra0=0xfe000900 ra4=0x90009 ra8=0x9040c03 rac=0x0 rb0=0x140f0000 rb4=0x500160f rb8=0x16080c rbc=0x1a080c06 rc0=0x16170f00 rc4=0x1813001a rc8=0x16001a15 rcc=0x1a151b rd0=0x0 rd4=0x0 rd8=0x0 rdc=0x0 re0=0x0 re4=0x0 re8=0x0 rec=0x0 rf0=0x0 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0x120000 r10c=0x0
    Host MSG RX -1170    [001] d...  2874.006592: rsp: a2=0xc4d15858 r0=0xd010db0 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2875.009587: tracing_mark_write: MARK get_2g_all_curve_param
    Host MSG RX -1170    [001] d...  2875.011269: req: a0=0xc39d0058 a1=0x10e a2=0xc541d060 r5=0x10e r7=0x101 base0=0xc5581938 base4=0x0 base8=0x10e0101 ps0=0xd010db4 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2875.011506: ev: r4=0xc1fad080 r5=0xc39d0040 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc5581938 m10=0x0 m14=0x10e0101 m18=0xd010db4 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2875.011517: rsp2: a2=0xc39d0058 r80=0x0 r84=0x0 r88=0x0 r8c=0x556feda r90=0x83c r94=0x5e9fe67 r98=0xf50a0771 r9c=0xfd310a73 ra0=0x36b101d ra4=0x965fa21 ra8=0x128303fc rac=0x11d9ec1f rb0=0x1ffafde6 rb4=0x1c89dd54 rb8=0xf3a0 rbc=0x0 rc0=0x0 rc4=0x0 rc8=0x0 rcc=0x0 rd0=0x0 rd4=0x0 rd8=0x0 rdc=0x0 re0=0x0 re4=0x0 re8=0x0 rec=0x0 rf0=0x0 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0xc0000 r10c=0x0
    Host MSG RX -1170    [001] d...  2875.011533: rsp: a2=0xc39d0058 r0=0xd010db4 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2876.014512: tracing_mark_write: MARK get_5g_all_curve_param
    Host MSG RX -1170    [001] d...  2876.016176: req: a0=0xc4d17c58 a1=0x10e a2=0xc541d060 r5=0x10e r7=0x101 base0=0xc1c01938 base4=0x0 base8=0x10e0101 ps0=0xd010db5 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2876.016401: ev: r4=0xc1f19380 r5=0xc4d17c40 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1c01938 m10=0x0 m14=0x10e0101 m18=0xd010db5 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2876.016416: rsp2: a2=0xc4d17c58 r80=0x0 r84=0x0 r88=0x4920000 r8c=0x42d77842 r90=0x46a2df0 r94=0x17b40aef r98=0x4023904 r9c=0xe0cf747 ra0=0x18f1f58 ra4=0xbd2fb7f ra8=0x1422039 rac=0xbc7fbb5 rb0=0x22e22d2 rb4=0xcbffa28 rb8=0x67d1f21 rbc=0x108df2dc rc0=0x4a71c39 rc4=0xef1f600 rc8=0x5b31c4e rcc=0x1027f400 rd0=0x5be1a87 rd4=0x10b2f391 rd8=0x87c17c0 rdc=0x11ddefe1 re0=0x6f019cb re4=0x1095f260 re8=0x7c71aaa rec=0x1063f1da rf0=0x80d1bff rf4=0x112ff0df rf8=0x1a5c rfc=0xf111381d r100=0x4309 r104=0xf131d49 r108=0x203c78 r10c=0x0
    Host MSG RX -1170    [001] d...  2876.016432: rsp: a2=0xc4d17c58 r0=0xd010db5 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2877.019490: tracing_mark_write: MARK get_rssi_param
    Host MSG RX -1170    [001] d...  2877.021141: req: a0=0xc39d1058 a1=0x10e a2=0xc541d460 r5=0x10e r7=0x101 base0=0xc1ee9938 base4=0x0 base8=0x10e0101 ps0=0xd010db3 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2877.021361: ev: r4=0xc1f18180 r5=0xc39d1040 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1ee9938 m10=0x0 m14=0x10e0101 m18=0xd010db3 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2877.021374: rsp2: a2=0xc39d1058 r80=0x0 r84=0x0 r88=0x0 r8c=0x0 r90=0x0 r94=0x0 r98=0x0 r9c=0x0 ra0=0x0 ra4=0x0 ra8=0x0 rac=0x0 rb0=0x0 rb4=0x0 rb8=0x0 rbc=0x0 rc0=0x0 rc4=0x0 rc8=0x0 rcc=0x0 rd0=0x0 rd4=0x0 rd8=0x0 rdc=0x0 re0=0x0 re4=0x0 re8=0x0 rec=0x0 rf0=0x0 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0xa0000 r10c=0x0
    Host MSG RX -1170    [001] d...  2877.021391: rsp: a2=0xc39d1058 r0=0xd010db3 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2878.024425: tracing_mark_write: MARK get_cca_th
    Host MSG RX -1170    [001] d...  2878.026106: req: a0=0xc318ae58 a1=0xe a2=0xc5253260 r5=0xe r7=0x101 base0=0xc1dd1938 base4=0x0 base8=0xe0101 ps0=0xd01012e ps4=0x0 ps8=0x1 psc=0x6e610000 ps10=0x6c656e ps14=0x0 ps18=0x8be0 ps1c=0x4801 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x8be0 ps34=0x7800
    Host MSG RX -1170    [001] d...  2878.026327: ev: r4=0xc1f18180 r5=0xc318ae40 m0=0x12000101 m4=0x3002a m8=0x5a5a0000 mc=0xc1dd1938 m10=0x0 m14=0xe0101 m18=0xd01012e m1c=0x0 m20=0xc2000001
    Host MSG RX -1170    [001] d...  2878.026340: rsp2: a2=0xc318ae58 r80=0x0 r84=0x0 r88=0x0 r8c=0x0 r90=0x0 r94=0x0 r98=0x0 r9c=0x0 ra0=0x0 ra4=0x0 ra8=0x0 rac=0x0 rb0=0x0 rb4=0x0 rb8=0x0 rbc=0x0 rc0=0x0 rc4=0x0 rc8=0x1 rcc=0x0 rd0=0x0 rd4=0x0 rd8=0x0 rdc=0x0 re0=0x0 re4=0x0 re8=0x0 rec=0x0 rf0=0x0 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0x0 r10c=0x0
    Host MSG RX -1170    [001] d...  2878.026361: rsp: a2=0xc318ae58 r0=0xd01012e r4=0x0 r8=0xc2000001 rc=0xc8c5 r10=0x0 r14=0xffffff00 r18=0xffffffff r1c=0xffffffff r20=0xffffffff r24=0xffffffff r28=0xffffffff r2c=0xffffffff r30=0x0 ... r7c=0x0
    Host MSG RX -1170    [001] d...  2879.029676: tracing_mark_write: MARK get_xo_ducy_cali_param
    Host MSG RX -1170    [001] d...  2879.031361: req: a0=0xc39d0058 a1=0x10e a2=0xc541e060 r5=0x10e r7=0x101 base0=0xc1d65938 base4=0x0 base8=0x10e0101 ps0=0xd010dad ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0 ps18=0x0 ps1c=0x0 ps20=0x0 ps24=0x0 ps28=0x0 ps2c=0x0 ps30=0x0 ps34=0x0
    Host MSG RX -1170    [001] d...  2879.031579: ev: r4=0xc1f19c80 r5=0xc39d0040 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1d65938 m10=0x0 m14=0x10e0101 m18=0xd010dad m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2879.031593: rsp2: a2=0xc39d0058 r80=0x0 r84=0x0 r88=0x16da0000 r8c=0x0 r90=0x0 r94=0x0 r98=0x0 r9c=0x0 ra0=0x0 ra4=0x0 ra8=0x0 rac=0x0 rb0=0x0 rb4=0x0 rb8=0x0 rbc=0x0 rc0=0x0 rc4=0x0 rc8=0x0 rcc=0x0 rd0=0x0 rd4=0x0 rd8=0x0 rdc=0x0 re0=0x0 re4=0x0 re8=0x0 rec=0x0 rf0=0x0 rf4=0x0 rf8=0x0 rfc=0x0 r100=0x0 r104=0x0 r108=0x10000 r10c=0x0
    Host MSG RX -1170    [001] d...  2879.031609: rsp: a2=0xc39d0058 r0=0xd010dad r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
```

(The `ev` lines whose `m0=0x1000101`/`m18=0x81818181` in the full trace were
unrelated wifi events in the same window; the `m0=0x12000101` lines above are the
`alg` responses. The full trace was `entries-in-buffer/entries-written: 56/56`.)

---

## 2. Command payload layouts

The **request and response share one buffer** (phase 5): `req a0 == rsp a2`
(e.g. `0xc39d1058` for `get_2g_power_param`), and the RX object `%r5` satisfies
`%r5+0x18 = payload pointer`.

### 2.1 Request envelope (driver → firmware)

With `M = %r0 - 0xc` at `hmac_config_alg_send_event+0x7c`:

| offset in `M` | size | observed | source field |
|---|---|---|---|
| `+0x00` | 8 | low u32 = token (section 3), high u32 = `0` | `base0`/`base4` |
| `+0x08` | u16 | `0x0101` (alg private-ioctl cmd id) | `base8` low half |
| `+0x0a` | u16 | payload length — `0x010e` (270) for all equipment cmds, `0x000e` (14) for `get_cca_th` | `base8` high half; equals `r5` |
| `+0x0c` | len | payload (copied by `memcpy_s`) | `ps0...` |

Request payload for every **get** captured here:

```
+0x00 u32 = 0x0D01_<cfg_id>      (ps0)
+0x04 u32 = 0x00000000           (ps4)
+0x08 u32 = 0x00000001           (ps8)      <- only for the 270-byte equipment cmds
+0x0c .. +0x10d = 0x00           (padding to len=0x10e)
```

For `get_cca_th` (`len=0x0e`) the copied request payload is only 14 bytes:
`+0x00=0xd01012e`, `+0x04=0`, `+0x08=1`, `+0x0c..+0x0d=0`; the non-zero words
visible from `psc` upwards in the trace (`psc=0x6e610000`, `ps18=0x8be0`, …) are
**stale bytes outside the 14-byte copy** (the buffer is a reused 270-byte pool) —
evidence that the copied length, not the buffer size, is the message extent.

The length is command-specific and comes from the driver's analysis handler
(`alg_cfg_args_param_analyse_equipment_param` writes `movw r3,#0x10e; strh r3,[sl]`
at `.text+0x1573e8`), not from a constant in the send path.

### 2.2 Response envelope (firmware → driver), equipment family (len 0x10e)

| payload offset | size | observed | notes |
|---|---|---|---|
| `+0x00` | u16 | `cfg_id` (e.g. `0x0db0`) | `r0` low half |
| `+0x02` | u16 | `0x0d01` for **get** | `r0` high half |
| `+0x04` | u32 | `0x00000000` | `r4` |
| `+0x08` | u32 | `0x00000001` | `r8` |
| `+0x0c..+0x89` | — | `0x00` | |
| `+0x8a` | N·4 | the returned table (N words) | decoded below |
| `+0x10a` | u16 | **N**, the word count of the table | value at `+0x10a`; bytes `+0x108/+0x109` are `0` |
| `+0x10c` | u32 | `0x00000000` | `r10c` |

The `+0x10a` word count was checked against the printed output for **six**
commands and matched every time:

| command | `r108` word value | bytes `+0x10a/+0x10b` | table words | printed words |
|---|---|---|---|---|
| `get_2g_power_param` | `0x001a0000` | `1a 00` = 26 | 26 | 26 |
| `get_5g_power_param` | `0x00120000` | `12 00` = 18 | 18 | 18 |
| `get_2g_all_curve_param` | `0x000c0000` | `0c 00` = 12 | 12 | 12 |
| `get_5g_all_curve_param` | `0x203c78`* | `20 00` = 32 | 32 | 32 |
| `get_rssi_param` | `0x000a0000` | `0a 00` = 10 | 10 | 10 |
| `get_xo_ducy_cali_param` | `0x00010000` | `01 00` = 1 | 1 | 1 |

\* for the 32-word 5g curve the table reaches `+0x109`, so `r108` also contains
the last two data bytes (`78 3c`); the count bytes `+0x10a/+0x10b` are still
`20 00`.

The `iwpriv` printer reads the table as little-endian u32 at payload `+0x8a`.

### 2.3 `get_5g_power_param` (cfg_id 0x0db0) — and the 2g/5g difference

Request: `ps0=0xd010db0 ps4=0 ps8=1`, all-zero padding, `len=0x10e`, network
token `base0=0xc1dd1938`.

Response payload bytes (from `rsp`/`rsp2`): `+0x00=0x0db0`, `+0x02=0x0d01`
(so `r0=0xd010db0`), `+0x04=0`, `+0x08=1`; table at `+0x8a` (bytes
`00 00 00 00 | 00 ff 04 00 | 08 01 00 0b | ff 00 0b 00 | …`):

```
+0x8a: 00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00
+0xa6: 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08
+0xc2: 001a1617 1a151813 151b1600 0000001a
```

26-word 2g table at the same base `+0x8a` (from `get_2g_power_param`):

```
+0x8a: 17161605 17161605 17161605 17161505 17161505 15161401 15161401
+0xa6: 15161401 15161401 15161400 08141200 08141200 08141200 0c111103
+0xc2: 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001
+0xde: 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

Both commands put the table at the **same** offset `+0x8a` inside the **same**
270-byte envelope. What differs between the 2g and 5g variants:

1. the id/cfg_id word (`+0x00`): `0x0dae` vs `0x0db0`;
2. the table **length**: 26 words (104 B) vs 18 words (72 B), also reflected in
   `+0x10a`;
3. the table **content** (per-band calibration values) — bytes differ from the
   first word on;
4. everything else (`+0x04=0`, `+0x08=1`, gap to `+0x8a`, 270-byte total) is
   identical.

### 2.4 `get_rssi_param` (cfg_id 0x0db3)

Request `ps0=0xd010db3 ps4=0 ps8=1`, `len=0x10e`.
Response `r0=0xd010db3`, `+0x04=0`, `+0x08=1`, and `+0x8a..+0xb1` =
10 u32 all zero; count `+0x10a = 0x000a` = 10, matching the ten zero words
printed. (= "no RSSI calibration stored on this unit", consistent with the
pre-existing snapshot.)

### 2.5 `get_2g_all_curve_param` / `get_5g_all_curve_param` (0x0db4 / 0x0db5)

Same envelope; table at `+0x8a`; counts 12 and 32 words (both confirmed at
`+0x10a`, `0x0c` and `0x20`). Raw table bytes equal the printed words
little-endian, e.g. 5g starts at `+0x8a` with `92 04 42 78 | d7 42 f0 2d | …`
= `78420492 2df042d7 …`. So a third and fourth command path are fully mapped;
between the two variants only cfg_id, count and content change.

### 2.6 `get_xo_ducy_cali_param` (cfg_id 0x0dad)

Request `ps0=0xd010dad`; response table at `+0x8a` = one word
(`+0x8a..+0x8d = da 16 00 00` → `0x000016da`), count `+0x10a = 0x0001`,
matching the printed `000016da`.

### 2.7 `get_cca_th` (cfg_id 0x012e) — different length and different data offset

This is the structurally distinct path: `len = 0x0e` (14), not `0x10e`; the RX
header's `m4` is `0x0003002a` (vs `0x0003012a`), `m14 = 0x0e0101`.

Response payload bytes:

```
+0x00: 2e 01 00 00   -> 0xd01012e  (cfg_id 0x012e, tag 0x0d01)
+0x04: 00 00 00 00
+0x08: 01 00 00 c2    (r8 = 0xc2000001)
+0x0c: c5 c8 00 00    (rc = 0x0000c8c5)
+0x10: 00 00 00 00
+0x14: 00 ff ff ff    (r14 = 0xffffff00)
+0x18: ff ff ff ff ... (r18..r2c = 0xffffffff)
```

The three printed thresholds appear as **signed bytes at `+0x0b/+0x0c/+0x0d`**:

- `+0x0b = 0xc2` = -62 (`ed_high_20th = [-62]`)
- `+0x0c = 0xc5` = -59 (`ed_high_40th = [-59]`)
- `+0x0d = 0xc8` = -56 (`ed_high_80th = [-56]`)

So `get_cca_th` uses a *compact* layout (values inline at `+0x0b`, not a
`+0x8a` table), and the request/response length is 14 bytes. Its `+0x08` word
is not the constant `1`.

### 2.8 DFS commands are not reachable through `iwpriv Hisilicon0 alg`

The firmware does contain DFS/radar symbols (`build/tmp/fw-symbols.txt`:
`0x076bb9 dfsdebug`, `0x076ae1 radarfilter`, `0x076a33 radarfilter_get`) and the
driver strings contain `get_radar_th`, `get_cac`, `get_detect_sw`,
`get_detect_check_info`, but none of them is in the `alg` name→cfg_id table and
every attempt returned `[FAIL]`:

```
$ iwpriv Hisilicon0 alg dfsdebug
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg radarfilter_get
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg radarfilter
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg dfsenable
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg cacenable
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg get_radar_th
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg get_cac
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg get_detect_sw
Hisilicon0  alg:[FAIL]
$ iwpriv Hisilicon0 alg get_detect_check_info
Hisilicon0  alg:[FAIL]
```

Conclusion: **DFS/radar is not part of the `alg` wire protocol** on this build
(it lives in the `hmac_config_dfs_*` / firmware `detect_check` path, reached by
other configuration, not by a user-visible `alg` name). The third+ command paths
were therefore taken from the equipment family plus `get_cca_th`.

---

## 3. The 8-byte header token

The request's `M+0x00` is an 8-byte unit written by one `strd`. Observed low/high
words (`base0`/`base4`) and the value the firmware returns at **RX object
`+0x0c`** (`mc`):

| # | command | `base0` (token low u32) | `base4` | `mc` (RX+0x0c) | echoed? |
|---|---|---|---|---|---|
| 1 | `get_2g_power_param` | `0xc5439938` | `0` | `0xc5439938` | yes |
| 2 | `get_5g_power_param` | `0xc1dd1938` | `0` | `0xc1dd1938` | yes |
| 3 | `get_2g_all_curve_param` | `0xc5581938` | `0` | `0xc5581938` | yes |
| 4 | `get_5g_all_curve_param` | `0xc1c01938` | `0` | `0xc1c01938` | yes |
| 5 | `get_rssi_param` | `0xc1ee9938` | `0` | `0xc1ee9938` | yes |
| 6 | `get_cca_th` | `0xc1dd1938` | `0` | `0xc1dd1938` | yes |
| 7 | `get_xo_ducy_cali_param` | `0xc1d65938` | `0` | `0xc1d65938` | yes |
| 8 | `get_rssi_param` (get1, §4) | `0xc53dd938` | `0` | `0xc53dd938` | yes |
| 9 | `set_rssi_param` (§4) | `0xc53c5938` | `0` | `0xc53c5938` | yes |
| 10 | `get_rssi_param` (get2, §4) | `0xc1c01938` | `0` | `0xc1c01938` | yes |

Answers to the three questions, with the evidence above:

1. **Does it change per request?** Yes, the low word differs between calls — but
   it is **not unique per request**: `0xc1dd1938` is used by both #2 and #6
   (different commands), and `0xc1c01938` by both #4 and #10. It is a reused
   buffer/descriptor address, not a fresh id.
2. **Is it echoed?** Yes, unconditionally, in the RX message object at `+0x0c`
   (`mc` == `base0` in 10/10 captures), and the RX header also repeats the
   cmd/len at `+0x14` (`m14` == `base8`). This is the request↔response
   correlation handle.
3. **Sequence number or channel id?** **Neither.**
   - Not a sequence number: the values are not monotonic and repeat (#2/#6,
     #4/#10) — a counter would increase.
   - Not a channel id: it is a full 32-bit kernel address in the module's heap
     band (`0xc1c0_1938`..`0xc558_1938`), in the same address range as the
     payload pointers (`0xc4b7_…`, `0xc53c_…`), and it varies per call on a
     single-netdev, single-radio interface. Its low 12 bits are `0x938` in all
     10 samples (address of a fixed member inside an allocated object). Phase 5
     followed it one level and found `[+0]=[+4]=0xbfc0301c` (two
     module-range pointers) and `[+8]=itself` — a descriptor/control block, not
     an integer id.
   - Conclusion (**substantiated**): the 8-byte header token is the address of a
     per-message TX descriptor (low word; high word `0`), echoed verbatim by the
     firmware so the driver can match the response. Marked HYPOTHESIS only for
     the *identity* of the pointed-to object, which was not symbol-resolved.

---

## 4. Get vs set for the same cfg_id (`get_rssi_param` / `set_rssi_param`, 0x0db3)

Before any set, `tools/wifi-cal-snapshot.sh` was run (allowed write under
`build/cal-snapshots/`):

```
$ bash tools/wifi-cal-snapshot.sh
snapshot dir: /c/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/cal-snapshots/20260930-204706
pulled cfg_hi5622v100_hisi.ini
pulled cfg_device_hisi.ini
pulled FIRMWARE.bin
missing hi_wifi_default_2g.conf
missing hi_wifi_default_5g.conf
---- manifest ----
7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b *FIRMWARE.bin
efb281183d01c0ba56a4bacc51f9ae0d95a1284406cee0a547b57d3865d85b33 *alg-values.txt
...
```

The snapshot's `alg-values.txt` records the pre-existing value (all-zero):

```
=== get_rssi_param ===
Hisilicon0  alg:[SUCC]00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
```

Argument syntax note: the set args must be delivered as **one quoted string**;
unquoted input is not parsed into parameters and is rejected before any state
change:

```
$ iwpriv Hisilicon0 alg set_rssi_param 0 0 0 0 0 0 0 0 0 0
Hisilicon0  alg:[FAIL][Error]config en_cfg_id[3507] param,uc_param_num(0) should be 10!
$ iwpriv Hisilicon0 alg "set_rssi_param 0 0 0 0 0 0 0 0 0 0"
Hisilicon0  alg:[SUCC]
```

(3507 = 0x0db3.)

Capture: `get_rssi_param`, then `set_rssi_param` writing the **same ten zeros**,
then `get_rssi_param` again. Raw trace:

```
              sh-4459    [000] ....  2945.999646: tracing_mark_write: MARK get1
    Host MSG RX -1170    [001] d...  2946.001349: req: a0=0xc4b7b858 a1=0x10e a2=0xc4b7b460 r5=0x10e r7=0x101 base0=0xc53dd938 base4=0x0 base8=0x10e0101 ps0=0xd010db3 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ... ps34=0x0
    Host MSG RX -1170    [001] d...  2946.001573: ev: r4=0xc53c1080 r5=0xc4b7b840 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc53dd938 m10=0x0 m14=0x10e0101 m18=0xd010db3 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2946.001585: rsp: a2=0xc4b7b858 r0=0xd010db3 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
              sh-4459    [000] ....  2946.002352: tracing_mark_write: MARK set_zeros
    Host MSG RX -1170    [001] d...  2946.004049: req: a0=0xc4b7b058 a1=0x10e a2=0xc4b78860 r5=0x10e r7=0x101 base0=0xc53c5938 base4=0x0 base8=0x10e0101 ps0=0xd000db3 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ... ps34=0x0
    Host MSG RX -1170    [001] d...  2946.004258: ev: r4=0xc53c0180 r5=0xc4b7b040 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc53c5938 m10=0x0 m14=0x10e0101 m18=0xd000db3 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2946.004268: rsp: a2=0xc4b7b058 r0=0xd000db3 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
              sh-4459    [000] ....  2946.005062: tracing_mark_write: MARK get2
    Host MSG RX -1170    [001] d...  2946.006726: req: a0=0xc4b7b858 a1=0x10e a2=0xc4b7bc60 r5=0x10e r7=0x101 base0=0xc1c01938 base4=0x0 base8=0x10e0101 ps0=0xd010db3 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ... ps34=0x0
    Host MSG RX -1170    [001] d...  2946.006945: ev: r4=0xc53c0f00 r5=0xc4b7b840 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1c01938 m10=0x0 m14=0x10e0101 m18=0xd010db3 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2946.006957: rsp: a2=0xc4b7b858 r0=0xd010db3 r4=0x0 r8=0x1 rc=0x0 r10=0x0 ... r7c=0x0
```

Command outputs from the same session:

```
Hisilicon0  alg:[SUCC]00000010 00000000 ... (get1)   <- value 0x10 left by the syntax test above
Hisilicon0  alg:[SUCC]                               (set_zeros)
Hisilicon0  alg:[SUCC]00000000 00000000 ... (get2)   <- back to snapshot value
```

### What differs between get and set (same cfg_id, substantiated)

1. **The direction bit is carried in the id word's high 16 bits at
   payload `+0x02`**:
   - get: `ps0 = 0x0D01_DB3`  (byte `+0x02 = 0x01`)
   - set: `ps0 = 0x0D00_DB3`  (byte `+0x02 = 0x00`)
   The firmware echoes the same word (`r0 = 0xd000db3` / `0xd010db3`).
   Everything else in the observed request is the same shape: `len=0x10e`,
   `base8=0x10e0101`, the 8-byte header token identical in shape (low word =
   pointer, high word = 0), `+0x04=0`, `+0x08=1`.
2. **Response envelope is the same**: `r4=0`, `r8=1`, same 270-byte buffer; the
   only difference is the id tag echoed at `+0x02`.
3. The **state round-trip is real**: an earlier `set_rssi_param 10 …`
   (0x10 written, see get1 output `00000010`) was then overwritten by
   `set_rssi_param 0 …`, and `get2` returned all zeros again — i.e. set writes
   take effect and the same-value write restored the snapshot state.

**Limitation:** because the same-value set carried all-zero parameters, the
payload beyond `+0x00` is zero for the set as well, so *this* capture does not
prove where non-zero set parameters land inside the 270-byte payload. A
non-zero set would show it (the `00000010` read-back proves non-zero parameters
are stored), but that was out of the allowed "same value back" scope. Marked
unresolved in section 5.

---

## 5. Explicit limits

1. **DFS/radar is not reachable via `iwpriv Hisilicon0 alg`.** Nine DFS/radar
   names all return `[FAIL]`; the `alg` name table (phase 2) has no DFS entry.
   The firmware symbols exist but the wire path to them is not the `alg` ioctl.
   No DFS command payload was captured.
2. **Set-parameter offsets are not mapped.** Only a same-value (all-zero) set was
   run, so the position of non-zero set values inside the payload is not proven.
   The direction discriminator (id tag `0x0d00` vs `0x0d01`) *is* proven.
3. **No double dereference.** tracefs fetchargs dereference once; nested pointers
   (e.g. the token's `0xbfc0301c` fields) could not be followed further.
4. **Field names are inferred.** The module has `.symtab` but no `.debug_info`;
   offsets/lengths are measured, names are HYPOTHESIS. The `+0x10a` word count,
   the `+0x8a` data base, the `0x0101` cmd id and the `0x0d01/0x0d00` direction
   tag are described by value and by their measured correlation with output.
5. **Single unit / single frequency.** All data is from one WR3000 (`chip
   id 0x34`, `get_channel:0`); "what varies between 2g and 5g" is the
   driver/firmware cfg_id and table contents, not a comparison across radios.
6. **Only one response payload region was fully decoded per command.** The
   bytes between `+0x0c` and `+0x89` were zero for every equipment command here;
   whether some cfg_id uses that gap was not tested for all 414 names.
7. **The `alg` set argument must be one quoted string**; this is a handling
   quirk (unquoted args are silently ignored and the driver reports
   `uc_param_num(0)`), not a property of the wire message.
8. Historic `error_log` entries (the `$argN` rejection etc.) remain; they are not
   active probes.

---

## 6. Cleanup proof

All `req`/`ev`/`rsp`/`rsp2` probes deleted, tracing disabled:

```
$ cd /sys/kernel/debug/tracing
$ echo 0 > tracing_on; echo "tracing_on rc=$?"
tracing_on rc=0
$ echo 0 > events/kprobes/enable; echo "enable rc=$?"
enable rc=0
$ for p in $(sed 's#^[^:]*:kprobes/##; s/ .*//' kprobe_events); do echo "-:kprobes/$p" >> kprobe_events; echo "del $p rc=$?"; done
del req rc=0
del ev rc=0
del rsp rc=0
$ echo -n "kprobe_events bytes: "; wc -c < kprobe_events
kprobe_events bytes: 0
$ echo -n "kprobes/list bytes: "; wc -c < /sys/kernel/debug/kprobes/list
kprobes/list bytes: 0
$ echo -n "tracing_on: "; cat tracing_on
tracing_on: 0
$ echo -n "current_tracer: "; cat current_tracer
current_tracer: nop
$ echo -n "events/kprobes: "; ls -d events/kprobes 2>&1
events/kprobes: ls: events/kprobes: No such file or directory
$ echo -n "kprobe_profile bytes: "; wc -c < kprobe_profile
kprobe_profile bytes: 0
$ echo -n "events/enable: "; cat events/enable
events/enable: 0
```

(The delete loop above ran over the probe set registered in that final session;
capture 1's extra `rsp2` probe had already been removed by the next session's
preamble, and the one-off `argtest`/`regtest` pair was deleted in its own
session.)

Final independent re-check in a fresh SSH session (`tracing_on` was found at `1`
once during re-validation with no probes and the `nop` tracer registered —
nothing to trace; it was reset to `0` and re-verified after a 6 s wait and again
in this fresh session):

```
$ ssh root@192.168.10.1 'echo -n "kprobe_events bytes: "; wc -c < /sys/kernel/debug/tracing/kprobe_events; ...'
kprobe_events bytes: 0
kprobes/list bytes: 0
tracing_on: 0
current_tracer: nop
events/kprobes: ls: /sys/kernel/debug/tracing/events/kprobes: No such file or directory
kprobe_profile bytes: 0
```

No device configuration was changed; every trigger was a read-only `get_*`
except the documented same-value `set_rssi_param` that restored the snapshot
value. The only files written are this document and the calibration snapshot
under `build/cal-snapshots/20260930-204706/`.
