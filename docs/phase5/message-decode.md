# Phase 5 — Decoding the driver->firmware message structures with tracefs kprobe fetchargs

Device: `root@192.168.10.1` (WR3000, `hi5622v100_wifi`), accessed read-only over SSH.
Target blob for the structural comparison: `build/tmp/hi5622v100_wifi.ko`
(`ET_REL`, section-relative addresses; the module is loaded at
`0xbf969000`, so `runtime = 0xbf969000 + section_offset`).

Method: dynamic kprobes in tracefs `kprobe_events`, with fetchargs that
dereference the message buffer(s) at fixed offsets, plus a kretprobe for the
return path and `trace_marker` labels to bind each capture to a command.
Everything below is a literal command and its raw output. Inference that is not
directly measured is labelled **HYPOTHESIS**.

Result summary: the wire format is no longer a black box. Both ends of the
`alg` get/read exchange were captured live:

- **Request** is built by `hmac_config_alg_send_event` (`.text+0x60c74`), the
  payload source pointer and the fully written header were read.
- **Response** is dispatched by `hmac_sync_dmac_alg_cfg_rsp_entry`
  (`.text+0x87d70`) from `hmac_event_config_syn+0x150`, and the RX message
  object was read at `hmac_event_config_syn+0x50`.
- The request payload pointer and the response payload pointer are the **same
  address**, so request and response share one buffer; the firmware overwrites
  the payload in place.
- The 32-bit **id word** at payload+0 is `0x0D01_<cfg_id>`: low 16 bits equal
  the driver's `cfg_id` from `g_ast_alg_cfg_process_info_table`, high 16 bits are
  the constant `0x0d01`. Verified for 10 `get_*` commands.

All probes were removed and tracing disabled at the end (proof in section 6).

---

## 0. Capability notes that shaped the capture

The lead already verified tracefs/kprobes/kallsyms. Two extra facts were
measured in this session and change the syntax that must be used.

### 0.1 `$argN` does NOT exist on this 5.10 ARM kernel — use `%rN`

```
$ echo 'p:argtest hmac_sync_dmac_alg_cfg_rsp_entry a0=$arg1 a1=$arg2 a2=$arg3 a3=$arg4' >> kprobe_events
sh: write error: Invalid argument
$ cat error_log
[ 2340.968841] trace_kprobe: error: Invalid $-valiable specified
  Command: p:argtest hmac_sync_dmac_alg_cfg_rsp_entry a0=$arg1 a1=$arg2 a2=$arg3 a3=$arg4
                                                         ^
```

So the lead's "tracefs supports `$argN`" is **not true here**; the ARM register
fetchargs `%r0`..`%r3` were used instead. Memory dereference
`+OFF(%rN):x8/x16/x32` works, including **negative offsets** (used to read the
message header backwards from a pointer into the middle of the buffer).

### 0.2 Probes persist across SSH sessions; deleting needs the event disabled

An earlier attempt to delete probes while `events/kprobes/enable` was `1`
returned `sh: write error: Resource busy` and left the probes registered. The
reliable order is: `echo 0 > events/kprobes/enable`, then
`echo '-:kprobes/<name>' >> kprobe_events`. This is used in the final cleanup.

### 0.3 Anchors

```
$ grep -E ' (hmac_config_alg_send_event|hmac_event_config_syn|hmac_sync_dmac_alg_cfg_rsp_entry)$' /proc/kallsyms
bf9c9c74 t hmac_config_alg_send_event	[hi5622v100_wifi]
...
```

(Recorded for reproducibility; the runtime addresses matched
`0xbf969000 + section_offset` for every anchor, i.e. the local `.ko` is the
running build.)

---

## 1. Capture method

### 1.1 Exact kprobe definitions used for the primary request/response capture

All four lines were appended to `/sys/kernel/debug/tracing/kprobe_events`
(each returned `rc=0`, `error_log` stayed free of new errors):

```
p:req hmac_config_alg_send_event+0x7c a0=%r0 a1=%r1 a2=%r2 a3=%r3 r5=%r5 r7=%r7 r8=%r8 base0=-12(%r0):x32 base4=-8(%r0):x32 base8=-4(%r0):x32 basec=+0(%r0):x32 base10=+4(%r0):x32 base14=+8(%r0):x32 base18=+12(%r0):x32 ps0=+0(%r2):x32 ps4=+4(%r2):x32 ps8=+8(%r2):x32 psc=+12(%r2):x32 ps10=+16(%r2):x32 ps14=+20(%r2):x32
p:rsp hmac_sync_dmac_alg_cfg_rsp_entry a0=%r0 a1=%r1 a2=%r2 a3=%r3 d0=+0(%r2):x32 d4=+4(%r2):x32 d8=+8(%r2):x32 dc=+12(%r2):x32 d10=+16(%r2):x32 d14=+20(%r2):x32 d18=+24(%r2):x32 d1c=+28(%r2):x32 d20=+32(%r2):x32 d24=+36(%r2):x32 d28=+40(%r2):x32 d2c=+44(%r2):x32 d30=+48(%r2):x32 d34=+52(%r2):x32 d38=+56(%r2):x32 d3c=+60(%r2):x32
r:rsp_ret hmac_sync_dmac_alg_cfg_rsp_entry ret=$retval
p:ev hmac_event_config_syn+0x50 r4=%r4 r5=%r5 m0=+0(%r5):x32 m4=+4(%r5):x32 m8=+8(%r5):x32 mc=+12(%r5):x32 m10=+16(%r5):x32 m14=+20(%r5):x32 m18=+24(%r5):x32 m1c=+28(%r5):x32 m20=+32(%r5):x32
```

Verified listing:

```
$ cat kprobe_events
p:kprobes/req hmac_config_alg_send_event+124 a0=%r0 a1=%r1 a2=%r2 a3=%r3 r5=%r5 r7=%r7 r8=%r8 base0=-12(%r0):x32 base4=-8(%r0):x32 base8=-4(%r0):x32 basec=+0(%r0):x32 base10=+4(%r0):x32 base14=+8(%r0):x32 base18=+12(%r0):x32 ps0=+0(%r2):x32 ps4=+4(%r2):x32 ps8=+8(%r2):x32 psc=+12(%r2):x32 ps10=+16(%r2):x32 ps14=+20(%r2):x32
p:kprobes/rsp hmac_sync_dmac_alg_cfg_rsp_entry a0=%r0 a1=%r1 a2=%r2 a3=%r3 d0=+0(%r2):x32 d4=+4(%r2):x32 d8=+8(%r2):x32 dc=+12(%r2):x32 d10=+16(%r2):x32 d14=+20(%r2):x32 d18=+24(%r2):x32 d1c=+28(%r2):x32 d20=+32(%r2):x32 d24=+36(%r2):x32 d28=+40(%r2):x32 d2c=+44(%r2):x32 d30=+48(%r2):x32 d34=+52(%r2):x32 d38=+56(%r2):x32 d3c=+60(%r2):x32
r2:kprobes/rsp_ret hmac_sync_dmac_alg_cfg_rsp_entry ret=$retval
p:kprobes/ev hmac_event_config_syn+80 r4=%r4 r5=%r5 m0=+0(%r5):x32 m4=+4(%r5):x32 m8=+8(%r5):x32 mc=+12(%r5):x32 m10=+16(%r5):x32 m14=+20(%r5):x32 m18=+24(%r5):x32 m1c=+28(%r5):x32 m20=+32(%r5):x32
```

Why the offsets:
- `hmac_config_alg_send_event+0x7c` is the instruction `bl memcpy_s`; at that
  point `r0 = msg_base + 0xc` (destination), `r2 = source payload pointer`,
  `r5 = r1 = r3 = payload length`, and the header fields `+8/+0xa` have already
  been written. `-12(%r0)` therefore reads `msg_base+0`.
- `hmac_event_config_syn+0x50` is the `cmp r5,#0` right after
  `ldr r5,[r4,#0x118]`, so `r5` is the received message object.
- `%r2` of `hmac_sync_dmac_alg_cfg_rsp_entry` is the response payload pointer.

Enable + arm:

```
$ echo 1 > events/kprobes/enable
$ echo 1 > tracing_on
$ echo > trace
```

### 1.2 Trigger commands

```
$ iwpriv Hisilicon0 alg get_2g_power_param
$ iwpriv Hisilicon0 alg get_5g_power_param
```

`Hisilicon0` is the netdev that exports the `alg (0101) : set 500 char & get
1000 char` private ioctl (`iwpriv Hisilicon0` output). All triggers are
read-only `get_*` subcommands.

### 1.3 Raw trace — request, RX header, response (one 2g + one 5g)

```
$ cat trace
# tracer: nop
#
# entries-in-buffer/entries-written: 14/14   #P:2
#
#                                _-----=> irqs-off
#                               / _----=> need-resched
#                              | / _---=> hardirq/softirq
#                              || / _--=> preempt-depth
#                              ||| /     delay
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
#              | |         |   ||||      |         |
    Host MSG RX -1170    [001] d...  2435.376471: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a0=0xc39d3858 a1=0x10e a2=0xc541e060 a3=0x10e r5=0x10e r7=0x101 r8=0xc541e060 base0=0xc4c27938 base4=0x0 base8=0x10e0101 basec=0x0 base10=0x0 base14=0x0 base18=0x0 ps0=0xd010dae ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0
    Host MSG RX -1170    [001] d...  2435.376699: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83080 r5=0xc39d3840 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc4c27938 m10=0x0 m14=0x10e0101 m18=0xd010dae m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2435.376714: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc39d3858 a3=0xc37ebcae d0=0xd010dae d4=0x0 d8=0x1 dc=0x0 d10=0x0 d14=0x0 d18=0x0 d1c=0x0 d20=0x0 d24=0x0 d28=0x0 d2c=0x0 d30=0x0 d34=0x0 d38=0x0 d3c=0x0
    Host MSG RX -1170    [001] d...  2435.376753: rsp_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...  2435.925073: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83800 r5=0xc39d3040 m0=0x1000101 m4=0x301d2 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1b602ef m18=0x81818181 m1c=0x81818181 m20=0x81818181
    Host MSG RX -1170    [001] d...  2435.926056: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e82a80 r5=0xc6f13640 m0=0x4000101 m4=0x30030 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1404ce m18=0x250025 m1c=0x290029 m20=0xafaca9a5
    Host MSG RX -1170    [001] d...  2436.177671: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83200 r5=0xc6f13040 m0=0x1000101 m4=0x30030 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1404ce m18=0x790043 m1c=0x790079 m20=0x9c9cadaa
    Host MSG RX -1170    [001] d...  2436.381335: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a0=0xc39d1c58 a1=0x10e a2=0xc541f060 a3=0x10e r5=0x10e r7=0x101 r8=0xc541f060 base0=0xc1d65938 base4=0x0 base8=0x10e0101 basec=0x0 base10=0x0 base14=0x0 base18=0x0 ps0=0xd010db0 ps4=0x0 ps8=0x1 psc=0x0 ps10=0x0 ps14=0x0
    Host MSG RX -1170    [001] d...  2436.381591: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83200 r5=0xc39d1c40 m0=0x12000101 m4=0x3012a m8=0x5a5a0000 mc=0xc1d65938 m10=0x0 m14=0x10e0101 m18=0xd010db0 m1c=0x0 m20=0x1
    Host MSG RX -1170    [001] d...  2436.381606: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc39d1c58 a3=0xc37ebcae d0=0xd010db0 d4=0x0 d8=0x1 dc=0x0 d10=0x0 d14=0x0 d18=0x0 d1c=0x0 d20=0x0 d24=0x0 d28=0x0 d2c=0x0 d30=0x0 d34=0x0 d38=0x0 d3c=0x0
    Host MSG RX -1170    [001] d...  2436.381641: rsp_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...  2436.931477: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e82480 r5=0xc39d0040 m0=0x1000101 m4=0x301d2 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1b602ef m18=0x81818181 m1c=0x81818181 m20=0x81818181
    Host MSG RX -1170    [001] d...  2436.932465: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83800 r5=0xc4b0aa40 m0=0x4000101 m4=0x30030 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1404ce m18=0x350035 m1c=0xe200e2 m20=0xafaca9a5
    Host MSG RX -1170    [001] d...  2437.184086: ev: (hmac_event_config_syn+0x50/0x424 [hi5622v100_wifi]) r4=0xc1e83080 r5=0xc4b0a640 m0=0x1000101 m4=0x30030 m8=0x5a5a0000 mc=0x0 m10=0x0 m14=0x1404ce m18=0x790043 m1c=0x790079 m20=0x9c9cadaa
```

(The interspersed `ev` lines with `m18=0x81818181`, `m14=0x1b602ef` and
`m0=0x1000101` are unrelated wifi events that happened to land in the same
window; the `m0=0x12000101` / `m14=0x10e0101` lines at `.376699` and `.381591`
are the `alg` responses. The `0x81818181` fill is the allocator poison in the
not-yet-written payload.)

### 1.4 Raw trace — full 270-byte response payload and the one-level pointer follow

Second capture with two extra probes (the `rsp`/`rsp2` pair dumps payload offsets
`0x00..0x10c`; `reqh` is `hmac_config_alg_send_event+0x60`, the `strd` that
stores the 8-byte header token, where `r0` holds the token value):

```
p:rsp  hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 a1=%r1 r0=+0(%r2):x32 ... r124=+124(%r2):x32
p:rsp2 hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 r128=+128(%r2):x32 ... r268=+268(%r2):x32
p:reqh hmac_config_alg_send_event+0x60 a0=%r0 a1=%r1 a3=%r3 tok0=+0(%r0):x32 tok4=+4(%r0):x32 tok8=+8(%r0):x32 tokc=+12(%r0):x32 tok10=+16(%r0):x32 tok14=+20(%r0):x32
```

Raw output:

```
$ cat trace
# tracer: nop
#
# entries-in-buffer/entries-written: 3/3   #P:2
#
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
    Host MSG RX -1170    [001] d...  2457.849939: reqh: (hmac_config_alg_send_event+0x60/0x174 [hi5622v100_wifi]) a0=0xc53dd938 a1=0x0 a3=0xc39d1c4c tok0=0xbfc0301c tok4=0xbfc0301c tok8=0xc53dd938 tokc=0x0 tok10=0x0 tok14=0x0
    Host MSG RX -1170    [001] d...  2457.850159: rsp2: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1c58 r128=0x0 r132=0x0 r136=0x16050000 r140=0x16051716 r144=0x16051716 r148=0x15051716 r152=0x15051716 r156=0x14011716 r160=0x14011516 r164=0x14011516 r168=0x14011516 r172=0x14001516 r176=0x12001516 r180=0x12000814 r184=0x12000814 r188=0x11030814 r192=0x11030c11 r196=0x11030c11 r200=0x10020c11 r204=0x10020b10 r208=0x10020b10 r212=0x10020b10 r216=0x10010b10 r220=0xf010b10 r224=0xf010a0f r228=0x6ff0a0f r232=0x6ff0a06 r236=0x6ff0a06 r240=0xa06 r244=0x0 r248=0x0 r252=0x0 r256=0x0 r260=0x0 r264=0x1a0000 r268=0x0
    Host MSG RX -1170    [001] d...  2457.850176: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1c58 a1=0x10e r0=0xd010dae r4=0x0 r8=0x1 r12=0x0 r16=0x0 r20=0x0 r24=0x0 r28=0x0 r32=0x0 r36=0x0 r40=0x0 r44=0x0 r48=0x0 r52=0x0 r56=0x0 r60=0x0 r64=0x0 r68=0x0 r72=0x0 r76=0x0 r80=0x0 r84=0x0 r88=0x0 r92=0x0 r96=0x0 r100=0x0 r104=0x0 r108=0x0 r112=0x0 r116=0x0 r120=0x0 r124=0x0
```

### 1.5 Raw trace — id word across 10 commands (with `trace_marker` labels)

To bind each id to its command unambiguously, `echo "MARK <name>" > trace_marker`
was written before each `iwpriv`:

```
p:rsp hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 id=+0(%r2):x32 h0=+0(%r2):x16 h2=+2(%r2):x16 s8=+8(%r2):x32
p:req hmac_config_alg_send_event+0x7c a2=%r2 r7=%r7 base8=-4(%r0):x32 ps0=+0(%r2):x32 ps4=+4(%r2):x32 ps8=+8(%r2):x32
```

```
$ for c in get_2g_power_param get_5g_power_param get_xo_ducy_cali_param get_xo_ppm_cali_param \
           get_2g_all_curve_param get_5g_all_curve_param get_2g_curve_factor get_5g_curve_factor \
           get_2g_upc get_5g_upc; do
    echo "MARK $c" > trace_marker; iwpriv Hisilicon0 alg $c >/dev/null 2>&1; sleep 1
  done
$ cat trace
# tracer: nop
#
# entries-in-buffer/entries-written: 30/30   #P:2
#
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
              sh-32351   [000] ....  2476.753229: tracing_mark_write: MARK get_2g_power_param
    Host MSG RX -1170    [001] d...  2476.755052: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc39d3860 r7=0x101 base8=0x10e0101 ps0=0xd010dae ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2476.755305: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1458 id=0xd010dae h0=0xdae h2=0xd01 s8=0x1
              sh-32351   [000] ....  2477.758014: tracing_mark_write: MARK get_5g_power_param
    Host MSG RX -1170    [001] d...  2477.759758: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc39d1c60 r7=0x101 base8=0x10e0101 ps0=0xd010db0 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2477.759972: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d3058 id=0xd010db0 h0=0xdb0 h2=0xd01 s8=0x1
              sh-32351   [000] ....  2478.762606: tracing_mark_write: MARK get_xo_ducy_cali_param
    Host MSG RX -1170    [001] d...  2478.764248: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc39d1460 r7=0x101 base8=0x10e0101 ps0=0xd010dad ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2478.764455: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d3858 id=0xd010dad h0=0xdad h2=0xd01 s8=0x1
              sh-32351   [000] ....  2479.767172: tracing_mark_write: MARK get_xo_ppm_cali_param
    Host MSG RX -1170    [001] d...  2479.768878: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc39d1060 r7=0x101 base8=0x10e0101 ps0=0xd010db2 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2479.769086: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d0058 id=0xd010db2 h0=0xdb2 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2480.771796: tracing_mark_write: MARK get_2g_all_curve_param
    Host MSG RX -1170    [001] d...  2480.773491: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541dc60 r7=0x101 base8=0x10e0101 ps0=0xd010db4 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2480.773720: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1458 id=0xd010db4 h0=0xdb4 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2481.776340: tracing_mark_write: MARK get_5g_all_curve_param
    Host MSG RX -1170    [001] d...  2481.778010: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541f860 r7=0x101 base8=0x10e0101 ps0=0xd010db5 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2481.778270: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d3858 id=0xd010db5 h0=0xdb5 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2482.781037: tracing_mark_write: MARK get_2g_curve_factor
    Host MSG RX -1170    [001] d...  2482.782710: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541d460 r7=0x101 base8=0x10e0101 ps0=0xd010db6 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2482.782931: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1458 id=0xd010db6 h0=0xdb6 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2483.785586: tracing_mark_write: MARK get_5g_curve_factor
    Host MSG RX -1170    [001] d...  2483.787252: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541f060 r7=0x101 base8=0x10e0101 ps0=0xd010db7 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2483.787478: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d3858 id=0xd010db7 h0=0xdb7 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2484.790214: tracing_mark_write: MARK get_2g_upc
    Host MSG RX -1170    [001] d...  2484.791985: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541e060 r7=0x101 base8=0x10e0101 ps0=0xd010db8 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2484.792223: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d1458 id=0xd010db8 h0=0xdb8 h2=0xd01 s8=0x1
              sh-32351   [001] ....  2485.795005: tracing_mark_write: MARK get_5g_upc
    Host MSG RX -1170    [001] d...  2485.796795: req: (hmac_config_alg_send_event+0x7c/0x174 [hi5622v100_wifi]) a2=0xc541ec60 r7=0x101 base8=0x10e0101 ps0=0xd010db9 ps4=0x0 ps8=0x1
    Host MSG RX -1170    [001] d...  2485.797070: rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a2=0xc39d3c58 id=0xd010db9 h0=0xdb9 h2=0xd01 s8=0x1
```

---

## 2. Observed request/response shapes

### 2.1 Layering: the same buffer is the request and the response

Direct equality from section 1.3:
- `req a0 = 0xc39d3858` (the destination of the request `memcpy_s`, i.e. the
  payload area), and `rsp a2 = 0xc39d3858` — **identical**.
- The RX object is `r5 = 0xc39d3840`, and `r5 + 0x18 = 0xc39d3858`.

So one allocation holds everything, and the offsets are:

| region | address | contents |
|---|---|---|
| RX message object base | `r5` | hcc/RX header |
| driver "user message" base | `r5 + 0xc` | header written by `hmac_config_alg_send_event` |
| payload | `r5 + 0x18` (= user base + 0xc) | id word + data; both request and response |

The response is therefore not a new allocation: the firmware round-trip
overwrites the payload in place, and the `Host MSG RX` thread hands the same
pointer back to `hmac_sync_dmac_alg_cfg_rsp_entry`.

### 2.2 The id word at the start of each buffer

`id = *(u32*)(payload+0)` = `0x0D01_<cfg_id>`:

- low 16 bits (`h0`) = the `cfg_id` from the driver's own
  `g_ast_alg_cfg_process_info_table` (phase 2);
- high 16 bits (`h2`) = the constant `0x0d01` for every one of the 10 commands;
- the same word appears in the request payload (`ps0`) and in the response
  (`id`), bit-for-bit.

| command | `cfg_id` (phase 2) | request `ps0` | response `id` | `h0` | `h2` |
|---|---|---|---|---|---|
| `get_2g_power_param` | `0x0dae` | `0xd010dae` | `0xd010dae` | `0x0dae` | `0x0d01` |
| `get_5g_power_param` | `0x0db0` | `0xd010db0` | `0xd010db0` | `0x0db0` | `0x0d01` |
| `get_xo_ducy_cali_param` | `0x0dad` | `0xd010dad` | `0xd010dad` | `0x0dad` | `0x0d01` |
| `get_xo_ppm_cali_param` | `0x0db2` | `0xd010db2` | `0xd010db2` | `0x0db2` | `0x0d01` |
| `get_2g_all_curve_param` | `0x0db4` | `0xd010db4` | `0xd010db4` | `0x0db4` | `0x0d01` |
| `get_5g_all_curve_param` | `0x0db5` | `0xd010db5` | `0xd010db5` | `0x0db5` | `0x0d01` |
| `get_2g_curve_factor` | `0x0db6` | `0xd010db6` | `0xd010db6` | `0x0db6` | `0x0d01` |
| `get_5g_curve_factor` | `0x0db7` | `0xd010db7` | `0xd010db7` | `0x0db7` | `0x0d01` |
| `get_2g_upc` | `0x0db8` | `0xd010db8` | `0xd010db8` | `0x0db8` | `0x0d01` |
| `get_5g_upc` | `0x0db9` | `0xd010db9` | `0xd010db9` | `0x0db9` | `0x0d01` |

This resolves the earlier phase-3 observation exactly: the "varying word"
`0x0d010dad..0x0d010db9` is the low-16 `cfg_id` with the fixed `0x0d01` tag in
the high half. The earlier note that running `get_2g_power_param` four times
alternated `0x…dae`/`0x…db0` is explained by the same table: those are the
`get_2g`/`get_5g` cfg_ids; a single 2g command produces only `0xd010dae` (seen
unambiguously in section 1.5).

### 2.3 Request message shape (driver -> firmware)

From `hmac_config_alg_send_event+0x7c` (section 1.3), with `M = r5 + 0xc` the
driver message base:

| offset in `M` | size | observed (`get_2g_power_param`) | evidence |
|---|---|---|---|
| `+0x00` | 8 | `0xc4c27938` + `0x00000000` | `base0`, `base4`; written by `strd r0,r1,[r3]` from the caller's stack arg |
| `+0x08` | u16 | `0x0101` | `base8` low half; `r7` register = `0x101`; `strh r7,[r3,#8]` |
| `+0x0a` | u16 | `0x010e` (270) | `base8` high half; `r5=r1=r3=0x10e`; `strh r5,[r3,#0xa]` |
| `+0x0c` | 270 | payload, `ps0=0xd010dae`, `ps4=0`, `ps8=1`, then zeros | `memcpy_s(M+0xc, len, src, len)`; `a2` = `src` pointer, dumped as `ps*` |

Field sizes: `+0x08` and `+0x0a` are 16-bit (the writes are `strh`), the 8-byte
`+0x00` unit is one `strd`, and the payload is copied with the same `0x10e`
length that is stored at `+0x0a`.

**HYPOTHESIS (field meaning):** `+0x08 = 0x0101` is the `alg` private-ioctl
command id (phase 2 showed `wal_algcmd_char_extra_adapt` forces cmd `0x0101`,
and it is `0x101` on every one of the 10 commands); `+0x0a = 0x010e` is the
payload length. The 8-byte `+0x00` unit behaves as an opaque token/handle (see
section 3).

### 2.4 Response message shape (firmware -> driver)

The RX payload is the same `M + 0xc` region. Full 270-byte dump (sections 1.4
and 1.5) decoded as 32-bit words:

| payload offset | size | `get_2g_power_param` | `get_5g_power_param` |
|---|---|---|---|
| `+0x00` | u16 | `0x0dae` (cfg_id) | `0x0db0` (cfg_id) |
| `+0x02` | u16 | `0x0d01` | `0x0d01` |
| `+0x04` | u32 | `0x00000000` | `0x00000000` |
| `+0x08` | u32 | `0x00000001` | `0x00000001` |
| `+0x0c..0x89` | — | zero | zero |
| `+0x8a` | 26 x u32 (104 B) | calibration table (see below) | (not dumped) |

The RX object header (`r5`, from `hmac_event_config_syn`), for the 2g response:

| offset | value (2g) | value (5g) |
|---|---|---|
| `+0x00` | `0x01200101` | `0x01200101` |
| `+0x04` | `0x0003012a` | `0x0003012a` |
| `+0x08` | `0x5a5a0000` | `0x5a5a0000` |
| `+0x0c` | `0xc4c27938` (= the request `+0x00` token) | `0xc1d65938` |
| `+0x10` | `0x00000000` | `0x00000000` |
| `+0x14` | `0x010e0101` (u16 `0x0101` cmd, u16 `0x010e` len) | `0x010e0101` |
| `+0x18` | `0xd010dae` (payload) | `0xd010db0` |
| `+0x1c` | `0x00000000` | `0x00000000` |
| `+0x20` | `0x00000001` | `0x00000001` |

Note `r5+0x14` duplicates the driver's `+0x08/+0x0a` cmd/len fields, and
`r5+0x0c` duplicates the request's 8-byte token.

### 2.5 The returned data reproduces the `iwpriv` output

The 104 bytes at payload `+0x8a` are the 26 words `iwpriv` prints. Decoding the
raw `rsp2` dump little-endian, starting at byte `0x8a`:

- bytes `0x8a..0x8d` = `05 16 16 17` -> `0x17161605` = first printed tuple;
- bytes `0x9a..0x9d` come from the high halves of `r152`/`r156`
  (`0x15051716`, `0x14011716`) -> `05 15 16 17` -> `0x17161505` (4th printed tuple);
- bytes `0xee..0xf1` = `ff 06 06 0a` -> `0x0a0606ff` = last printed tuple.

`iwpriv` for 2g returned exactly:

```
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

(the words after it, `r244`..`r260`, are zero; `r264=0x001a0000` is a
separate trailing field beyond the table.) The response also confirms `get_2g`
returns 26 words while the 5g result is 18 words (phase 2/3), i.e. the amount
of valid data differs but the 270-byte buffer envelope is identical.

---

## 3. Pointers followed one level deep

All pointer follows are single-level (tracefs fetchargs cannot double-deref, so
the second level was reached only where a register already held the pointer).

1. **`rsp %r0` = `0xc39f78c8`** (response handler arg 0, stable across every
   run). One-level read (section 1.3 via `d`/`c0..c8` in the first session):
   `+0(%r0)=0x12`, `+4=0`, `+8=0`. Interpretation **HYPOTHESIS**: a small
   context/descriptor whose first field is `0x12` (18). It is re-passed as the
   first argument to the per-cfg output handler (section 4).
2. **`rsp %r3` = `0xc37ebcae`** (2-byte aligned, process stack). This is the
   `sp+0x1e` out-slot that `hmac_event_config_syn` reads back after the call
   (`ldrh r8,[sp,#0x1e]`). Dumped `+0(%r3):x16`/`+2(%r3):x16` were `0` at
   entry — it is an **out** parameter, written by the callee, not an input.
3. **`%r2` (payload pointer)** — the main follow (sections 2.2/2.4).
4. **Request 8-byte token**: `reqh` (section 1.4) caught `r0 = 0xc53dd938`
   (the token value that is stored at message `+0x00`). Reading one level into
   it:
   ```
   tok0=0xbfc0301c tok4=0xbfc0301c tok8=0xc53dd938 tokc=0x0 tok10=0x0 tok14=0x0
   ```
   So the token points at a self-referential kernel object:
   `[+0x00]=0xbfc0301c`, `[+0x04]=0xbfc0301c` (two identical module-range
   pointers), `[+0x08]=0xc53dd938` (pointer to itself), then zeros.
   `0xbfc0301c - 0xbf969000 = 0x29a01c`, inside `hi5622v100_wifi`.
   **HYPOTHESIS**: a TX control block / work item; the two `0xbfc0301c` slots
   look like function pointers. No symbol was resolved for `0xbfc0301c`, so the
   interpretation is not confirmed.
5. **Request payload source pointer** `a2/%r8` (`0xc541e060`): read directly as
   `ps0/ps4/ps8` — this is where the id word originates before the `memcpy_s`.

---

## 4. Comparison against the driver's own structures (disassembly)

Reproduce with the local `.ko` (ARM/A32 — the module code decodes as ARM, not
Thumb). The helper used:

```
$ cd C:/Users/ShibbityShwab/router-openwrt
$ ./pyenv/Scripts/python.exe - <<'PY'
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'))
d=e.get_section_by_name('.text').data()
S={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()
   if s['st_info']['type']=='STT_FUNC'}
md=Cs(CS_ARCH_ARM,CS_MODE_ARM)
for n in ['hmac_sync_dmac_alg_cfg_rsp_entry','hmac_config_alg_send_event','hmac_event_config_syn']:
    v,sz=S[n]; print('##',n,hex(v),sz)
    for i in md.disasm(d[v:v+sz],v): print('  0x%06x  %-8s %s'%(i.address,i.mnemonic,i.op_str))
PY
```

Symbol addresses/sizes:

```
hmac_sync_dmac_alg_cfg_rsp_entry              0x087d70 size=352
hmac_event_config_syn                         0x06b19c size=1060
hmac_config_alg_send_event                    0x060c74 size=372
hmac_config_send_event                        0x057238 size=388
alg_eqment_config_param_output_entry          0x087b38 size=476
alg_cfg_args_param_analyse_equipment_param    0x1572c0 size=764
wal_config_alg_cfg_param_host_entry           0x0fc48c size=1168
```

### 4.1 `hmac_sync_dmac_alg_cfg_rsp_entry` reads exactly the observed cfg_id

Key instructions (ARM):

```
0x087d84  subs     r4, r2, #0        ; r4 = r2 = payload pointer (3rd arg)
0x087d8c  ldr      r6, [sp, #0x30]   ; arg7 -> passed as r2 to the per-cfg handler
0x087da8  ldrh     r0, [r4]          ; r0 = *(u16*)payload  <-- the cfg_id
0x087db0  ldrh     lr, [r1]          ; table entry .min
0x087dbc  ldrh     lr, [r1, #2]      ; table entry .max
0x087dc8  add      ip, ip, #1        ; 32-entry loop
0x087dcc  add      r1, r1, #8        ; 8-byte entries
0x087dd0  cmp      ip, #0x20
0x087dd4  bne      #0x87db0
0x087e00  ldr      r8, [r3, #4]      ; handler = entry+4
0x087e0c  mov      r0, r5            ; orig arg0
0x087e10  mov      r3, r7            ; orig arg3 (the sp+0x1e out slot)
0x087e14  mov      r2, r6            ; arg7
0x087e18  mov      r1, r4            ; payload pointer
0x087e1c  blx      r8                ; per-cfg output handler
```

This matches the live capture exactly: `ldrh [r2]` = `0x0dae` (`h0`), and the
handler is chosen by a **16-bit range search**. The fallback when the id is not
in the 32-entry table loads the global
`g_hmac_sync_dmac_alg_cfg_rsp_get_entry` (`.LANCHOR5`, relocated at
`0x87dd8/0x87ddc`) and calls it.

The table itself is at `.data+0x99c` (the `ldr r3,[pc,#0x124]` literal at
`0x87ecc` is an `R_ARM_ABS32` relocation against `.data`). Parsing the 32
8-byte entries (`u16 min; u16 max; u32 handler`), with handler names resolved
from `.rel.data`:

```
idx  off   min    max    handler (relocation target in .rel.data)
  0 0x000 0x0191 0x01a6 alg_autorate_config_param_output_entry
 ...
 20 0x0a0 0x0dad 0x0dc5 alg_eqment_config_param_output_entry     <-- our range
 ...
 31 0x0f8 0x1130 0x1132 alg_trigger_ack_config_param_output_entry
```

Entry 20 (`min=0x0dad`, `max=0x0dc5`) contains every id observed in section 2.2
(`0x0dae`, `0x0db0`, `0x0dad`, `0x0db2`, `0x0db4`, `0x0db5`, `0x0db6`,
`0x0db7`, `0x0db8`, `0x0db9`), and its handler is
`alg_eqment_config_param_output_entry` (`0x87b38`, which sits immediately
before the response handler). So **the wire id word's low half is literally the
table search key** in the driver's own dispatch code.

### 4.2 `hmac_config_alg_send_event` writes exactly the observed request header

```
0x060cc8  ldr      r3, [sp, #0x10]   ; r3 = msg buffer
0x060cd0  ldrd     r0, r1, [sp, #0x30]  ; 8-byte token from caller stack arg
0x060cd4  strd     r0, r1, [r3]      ; msg+0x00 = token      (base0/base4)
0x060ce0  strh     r7, [r3, #8]      ; msg+0x08 = r7 (0x0101)
0x060ce4  strh     r5, [r3, #0xa]    ; msg+0x0a = r5 (len 0x10e)
0x060ce8  add      r0, r0, #0xc      ; r0 = msg+0xc (dest)
0x060cf0  bl       memcpy_s          ; memcpy_s(msg+0xc, len, src, len)
0x060d00  bl       hcc_msg_tx         ; hand the message to the host channel
```

Relocations confirm the named callees: `+0x7c -> memcpy_s`, `+0x8c ->
hcc_msg_tx`, `+0xe8`/`+0x164 -> hcc_msg_free` (from `.rel.text`). Its caller is
`wal_config_alg_cfg_param_host_entry` (`0xfc48c`): `.rel.text` has
`hmac_config_alg_send_event` referenced at `.text+0xfc658`, inside that
function. So the path is
`wal_config_alg_cfg_param_host_entry -> hmac_config_alg_send_event ->
hcc_msg_tx`, which is exactly the "driver -> firmware" boundary the capture
sits on.

### 4.3 `hmac_event_config_syn` dispatches the RX object to that handler

```
0x06b1e8  ldr      r5, [r4, #0x118]  ; r5 = device->0x118 = RX message object
0x06b1ec  cmp      r5, #0
0x06b1f0  beq      #0x6b3c0
0x06b1fc  ldrh     r3, [r5, #0x14]
0x06b200  ldr      r6, [r5, #0xc]    ; r6 = [r5+0xc]
0x06b208  ldr      r7, [r5, #0x10]   ; r7 = [r5+0x10]
0x06b210  ldrb     r4, [r5, #3]      ; index into the cfg handler LUT
0x06b2cc  add      r1, r1, ip, lsl #3
0x06b2d0  add      r3, sp, #0x24     ; stack arg value
0x06b2d4  add      r2, r5, #0x18     ; r2 = payload pointer
0x06b2d8  ldr      r4, [r1, #4]      ; handler pointer
0x06b2dc  ldrh     r1, [r5, #0x16]   ; r1 = u16 @r5+0x16 = 0x10e
0x06b2e0  str      r3, [sp]          ; stack arg = sp+0x24
0x06b2e4  add      r3, sp, #0x1e     ; r3 = sp+0x1e (the out slot)
0x06b2e8  blx      r4                ; -> hmac_sync_dmac_alg_cfg_rsp_entry
```

`0x6b2e8 + 4 = 0x6b2ec = hmac_event_config_syn+0x150`, which is precisely the
caller line the kretprobe prints (`rsp_ret` in section 1.3). The register
mapping is a one-to-one match:

| register at `blx` | value in trace | meaning |
|---|---|---|
| `r0` | `0xc39f78c8` | context (arg0) |
| `r1` = `[r5+0x16]` | `0x10e` | length-like (matches the request len) |
| `r2` = `r5+0x18` | `0xc39d3858` | payload pointer |
| `r3` = `sp+0x1e` | `0xc37ebcae` | 2-byte-aligned stack out slot |
| `[sp]` = `sp+0x24` | — | 4th arg (arg7 of the response handler) |

### 4.4 Earlier phase-3 symbol table

The response handler and its sibling remain as the calibration boundary:
`hmac_sync_dmac_alg_cfg_rsp_entry` (`0x87d70`, 352 B) and
`hmac_sync_dmac_cali_cfg_rsp_entry` (`0x881a4`, 148 B). This session went one
level past them: the `alg` dispatch target for the power/calibration cfg_id
range is `alg_eqment_config_param_output_entry` (`0x87b38`), and the request
builder is `hmac_config_alg_send_event`.

---

## 5. Explicit limits

1. **`$argN` is unsupported on this kernel** (`Invalid $-valiable specified`),
   so every capture used `%r0`..`%r3`. There is no portable `$argN` form to
   report.
2. **No double dereference.** tracefs fetchargs dereference exactly once, so
   "pointer-to-pointer" chains can only be followed where a register already
   holds the second pointer. Consequently:
   - the request `+0x00` token could be followed one level (section 3.4) but
     its two `0xbfc0301c` fields could **not** be resolved to symbols;
   - `hmac_event_config_syn`'s `r6=[r5+0xc]` / `r7=[r5+0x10]` (the two extra
     arguments the response handler forwards to the per-cfg handler) were read
     as values (`0xc4c27938` / `0x0`) but not dereferenced.
3. **No function-call tracing.** `available_tracers = nop` only, so the
   per-cfg handler `alg_eqment_config_param_output_entry` could not be traced
   as a call graph; its identity comes from the static `.rel.data` relocation,
   not from a live probe at the time of this task.
4. **Field names are inferred.** The module has no DWARF (`hi5622v100_wifi.ko`
   has `.symtab` but no `.debug_info`), so offsets are exact and names are not:
   the 8-byte `+0x00` unit, `0x0101`, `0x010e` and `0x0d01` are described by
   size/value, with their roles marked HYPOTHESIS.
5. **`hmac_sync_dmac_cali_cfg_rsp_entry`, `hmac_chan_tx_cali_sync`,
   `hmac_save_cali_data_to_file_2g/5g` were not re-triggered.** Reaching them
   needs an actual calibration run (write path), which is outside read-only
   scope; this document covers the `alg` get/read path only.
6. **Payload tail not fully mapped.** Words beyond the 26-word table are zero
   for 2g in this capture; whether other cfg_ids use `+0x8a` as the fixed data
   base or place data elsewhere was not tested for each of the 10 commands
   (only the 2g table was decoded byte-for-byte).
7. **Historic `error_log` entries remain.** The `$argN` rejection and the
   earlier failed probe redefinitions are still in `error_log` (1655 bytes);
   they are not active probes and cannot be cleared without writing to that
   node, which was left alone.

---

## 6. Cleanup proof

```
$ cd /sys/kernel/debug/tracing
$ echo 0 > tracing_on ; echo "tracing_on rc=$?"
tracing_on rc=0
$ echo 0 > events/kprobes/enable ; echo "events/kprobes/enable rc=$?"
events/kprobes/enable rc=0
$ cat kprobe_events
p:kprobes/rsp hmac_sync_dmac_alg_cfg_rsp_entry a2=%r2 id=+0(%r2):x32 h0=+0(%r2):x16 h2=+2(%r2):x16 s8=+8(%r2):x32
p:kprobes/req hmac_config_alg_send_event+124 a2=%r2 r7=%r7 base8=-4(%r0):x32 ps0=+0(%r2):x32 ps4=+4(%r2):x32 ps8=+8(%r2):x32
$ for p in $(sed 's#^[^:]*:kprobes/##; s/ .*//' kprobe_events); do echo "-:kprobes/$p" >> kprobe_events; echo "del $p rc=$?"; done
del rsp rc=0
del req rc=0
$ echo -n "kprobe_events bytes: "; wc -c < kprobe_events
kprobe_events bytes: 0
$ echo -n "kprobes/list bytes: "; wc -c < /sys/kernel/debug/kprobes/list
kprobes/list bytes: 0
$ echo -n "tracing_on: "; cat tracing_on
tracing_on: 0
$ echo -n "current_tracer: "; cat current_tracer
current_tracer: nop
$ echo -n "events/kprobes exists: "; ls -d events/kprobes 2>&1
events/kprobes exists: ls: events/kprobes: No such file or directory
$ echo -n "kprobe_profile bytes: "; wc -c < kprobe_profile
kprobe_profile bytes: 0
```

Re-confirmed in a final independent session:

```
$ echo -n "kprobe_events bytes: "; wc -c < /sys/kernel/debug/tracing/kprobe_events
kprobe_events bytes: 0
$ echo -n "kprobes/list bytes: "; wc -c < /sys/kernel/debug/kprobes/list
kprobes/list bytes: 0
$ echo -n "tracing_on: "; cat /sys/kernel/debug/tracing/tracing_on
tracing_on: 0
```

No device configuration was changed; all triggers were read-only `get_*`
subcommands and `trace_marker` writes. Only this file was written.
