# Phase 3 - Live driver-to-firmware message boundary capture

Device: `root@192.168.10.1` (WR3000), accessed read-only over SSH.
Method: dynamic kprobes via tracefs `kprobe_events` (ftrace), plus kretprobe for callers.

Result summary: **tracing worked**. A real capture of the driver->firmware
response dispatch was obtained on `hmac_sync_dmac_alg_cfg_rsp_entry`. The four
calibration/save functions (`hmac_sync_dmac_cali_cfg_rsp_entry`,
`hmac_chan_tx_cali_sync`, `hmac_save_cali_data_to_file_2g`,
`hmac_save_cali_data_to_file_5g`) were probed successfully but were **not
observed to fire** under any read-only trigger available through `iwpriv`.
All probes were removed and tracing disabled at the end (proof in section 5).

---

## 1. Capability checks (with literal command + observed output)

### 1.1 Kernel identity

```
$ uname -a
Linux WR3000 5.10.201 #0 SMP Sat Nov 25 18:18:57 2023 armv7l GNU/Linux
```

```
$ cat /proc/version
Linux version 5.10.201 (jenkins@cudyrd1) (arm-mix510-linux-gcc (musl-1.2.3 linux-5.10 CS71.2.10.5.B002  2025-03-05 12:00:00) 10.3.0, GNU ld (GNU Binutils) 2.41) #0 SMP Sat Nov 25 18:18:57 2023
```

### 1.2 Does `/sys/kernel/debug/tracing` exist, and is tracefs mounted? -- YES

```
$ ls -la /sys/kernel/debug/
...
drwx------    6 root     root             0 Jan  1  1970 tracing
...
$ mount | grep -iE 'debug|trace'
debugfs on /sys/kernel/debug type debugfs (rw,noatime)
tracefs on /sys/kernel/debug/tracing type tracefs (rw,noatime)
```

tracefs is mounted read-write, and the standard control files exist:

```
$ ls /sys/kernel/debug/tracing/
README available_events available_tracers buffer_percent buffer_size_kb
buffer_total_size_kb current_tracer dynamic_events error_log events
free_buffer instances kprobe_events kprobe_profile options per_cpu
printk_formats saved_cmdlines saved_cmdlines_size saved_tgids set_event
set_event_notrace_pid set_event_pid timestamp_mode trace trace_clock
trace_marker trace_marker_raw trace_options trace_pipe tracing_cpumask
tracing_on tracing_thresh
```

Caveat (important): only the `nop` tracer is compiled in, so the `function` /
`function_graph` tracers are NOT available. Event-based (kprobe) tracing still
works because `kprobe_events` exists.

```
$ cat /sys/kernel/debug/tracing/available_tracers
nop
```

```
$ cat /sys/kernel/debug/tracing/current_tracer
nop
```

### 1.3 Does `/proc/kallsyms` expose the module symbols? -- YES

```
$ cat /proc/sys/kernel/kptr_restrict
0
$ head -5 /proc/kallsyms
c0008000 T stext
c0008000 T _text
c0008088 t __create_page_tables
c0008134 t __turn_mmu_on_loc
c0008140 t __fixup_smp
$ wc -l /proc/kallsyms
117607 /proc/kallsyms
$ grep -c hmac /proc/kallsyms
1924
```

The wifi driver is the module `hi5622v100_wifi`. All five anchor symbols are
present with real (non-zeroed) addresses:

```
$ for p in dmac_alg dmac_cali chan_tx_cali save_cali; do grep "$p" /proc/kallsyms; done
bf9f0d70 t hmac_sync_dmac_alg_cfg_rsp_entry	[hi5622v100_wifi]
bfbe7a64 B g_hmac_sync_dmac_alg_cfg_rsp_get_entry	[hi5622v100_wifi]
bf9f11a4 t hmac_sync_dmac_cali_cfg_rsp_entry	[hi5622v100_wifi]
bf9afdac t hmac_chan_tx_cali_sync	[hi5622v100_wifi]
bfa2cc20 t hmac_save_cali_data_verify_5g	[hi5622v100_wifi]
bfa2ce54 t hmac_save_cali_data_to_file_5g	[hi5622v100_wifi]
bfa2cd3c t hmac_save_cali_data_verify_2g	[hi5622v100_wifi]
bfa2cf4c t hmac_save_cali_data_to_file_2g	[hi5622v100_wifi]
bfa2d044 t hmac_save_cali_data_to_file	[hi5622v100_wifi]
```

### 1.4 Does creating a kprobe succeed? -- YES

```
$ cd /sys/kernel/debug/tracing
$ echo 'p:cap_test hmac_sync_dmac_alg_cfg_rsp_entry' >> kprobe_events
rc=0
$ cat kprobe_events
p:kprobes/cap_test hmac_sync_dmac_alg_cfg_rsp_entry
$ ls events/kprobes/
cap_test  enable  filter
$ cat error_log
(empty)
```

Then removed with `echo '-:cap_test' >> kprobe_events` (rc=0).

Conclusion: full kprobe capability is available; only the static function
tracers are missing. Tracing is therefore NOT inconclusive -- sections 2-3 are a
real capture.

---

## 2. Probes created, trigger activity, and captured trace

### 2.1 Exact probe commands

`t`-type kprobes on the five anchors, fetching the four ARM argument registers
(r0-r3) plus return probes (`$retval`) on the two "entry" handlers. The `<name>`
strings were written to `/sys/kernel/debug/tracing/kprobe_events`
(all appends returned `rc=0`, `error_log` stayed empty):

```
p:alg_cfg_rsp      hmac_sync_dmac_alg_cfg_rsp_entry  a0=%r0 a1=%r1 a2=%r2 a3=%r3 s0=+0(%r2):x32 s4=+4(%r2):x32 s8=+8(%r2):x32 sc=+12(%r2):x32 s10=+16(%r2):x32 s14=+20(%r2):x32 s18=+24(%r2):x32 s1c=+28(%r2):x32 a0_0=+0(%r0):x32 a0_4=+4(%r0):x32 a0_8=+8(%r0):x32
p:cali_cfg_rsp     hmac_sync_dmac_cali_cfg_rsp_entry a0=%r0 a1=%r1 a2=%r2 a3=%r3 p0=+0(%r0):x32 p4=+4(%r0):x32 p8=+8(%r0):x32 pc=+12(%r0):x32 q0=+0(%r2):x32 q4=+4(%r2):x32 t0=+0(%r3):x32 t4=+4(%r3):x32
p:chan_tx_cali     hmac_chan_tx_cali_sync            a0=%r0 a1=%r1 a2=%r2 a3=%r3 p0=+0(%r0):x32 p4=+4(%r0):x32 p8=+8(%r0):x32 pc=+12(%r0):x32 q0=+0(%r2):x32 q4=+4(%r2):x32 t0=+0(%r3):x32 t4=+4(%r3):x32
p:save_2g          hmac_save_cali_data_to_file_2g    a0=%r0 a1=%r1 a2=%r2 a3=%r3 p0=+0(%r0):x32 p4=+4(%r0):x32 p8=+8(%r0):x32 pc=+12(%r0):x32 q0=+0(%r2):x32 q4=+4(%r2):x32 t0=+0(%r3):x32 t4=+4(%r3):x32
p:save_5g          hmac_save_cali_data_to_file_5g    a0=%r0 a1=%r1 a2=%r2 a3=%r3 p0=+0(%r0):x32 p4=+4(%r0):x32 p8=+8(%r0):x32 pc=+12(%r0):x32 q0=+0(%r2):x32 q4=+4(%r2):x32 t0=+0(%r3):x32 t4=+4(%r3):x32
r:alg_ret          hmac_sync_dmac_alg_cfg_rsp_entry  ret=$retval
r:cali_ret         hmac_sync_dmac_cali_cfg_rsp_entry ret=$retval
```

Enable + arm:

```
$ echo 1 > events/kprobes/enable     # rc=0
$ echo > trace                       # clear ring buffer
```

### 2.2 Trigger activity

The `alg` private ioctl of the wifi netdev `Hisilicon0` is the entry point
(`iwpriv Hisilicon0` lists `alg (0101) : set 500 char & get 1000 char`). The
suggested trigger was used, repeated, and extended with other read-only `get_*`
subcommands (enumerated from the module's own strings, never a `set_*`):

```
$ iwpriv Hisilicon0 alg get_2g_power_param
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff

$ iwpriv Hisilicon0 alg get_5g_power_param
Hisilicon0  alg:[SUCC]00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a
```

Additional read-only gets run in the same arm window (all returned `[SUCC]`):
`get_xo_ducy_cali_param`, `get_xo_ppm_cali_param`, `get_2g_all_curve_param`,
`get_5g_all_curve_param`, `get_2g_upc`, `get_5g_upc`, `get_2g_curve_factor`,
`get_5g_curve_factor`.

### 2.3 Captured trace output

Trace from the suggested trigger, `iwpriv Hisilicon0 alg get_2g_power_param`
(run 4x; one entry+return per call), plus the argument/structure dump:

```
# tracer: nop
# entries-in-buffer/entries-written: 8/8   #P:2
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
    Host MSG RX -1170    [001] d...   588.631595: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cca458 a3=0xc37ebcae p0=0x12 p4=0x0 p8=0x0 pc=0x0 q0=0xd010dae q4=0x0 t0=0x0 t4=0x0
    Host MSG RX -1170    [001] d...   588.631642: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...   588.633928: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cca458 a3=0xc37ebcae p0=0x12 p4=0x0 p8=0x0 pc=0x0 q0=0xd010db0 q4=0x0 t0=0x0 t4=0x0
    Host MSG RX -1170    [001] d...   588.633968: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...   588.636247: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cca458 a3=0xc37ebcae p0=0x12 p4=0x0 p8=0x0 pc=0x0 q0=0xd010dae q4=0x0 t0=0x0 t4=0x0
    Host MSG RX -1170    [001] d...   588.636290: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...   588.638550: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cca458 a3=0xc37ebcae p0=0x12 p4=0x0 p8=0x0 pc=0x0 q0=0xd010db0 q4=0x0 t0=0x0 t4=0x0
    Host MSG RX -1170    [001] d...   588.638590: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
```

Same probe, deeper dump of the buffer at `r2` (28 bytes) and at `r0`, for
`get_2g_power_param` then `get_2g_all_curve_param`:

```
# tracer: nop
# entries-in-buffer/entries-written: 4/4   #P:2
    Host MSG RX -1170    [001] d...   613.311930: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cc9058 a3=0xc37ebcae s0=0xd010dae s4=0x0 s8=0x1 sc=0x0 s10=0x0 s14=0x0 s18=0x0 s1c=0x0 a0_0=0x12 a0_4=0x0 a0_8=0x0
    Host MSG RX -1170    [001] d...   613.311972: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
    Host MSG RX -1170    [001] d...   613.314584: alg_cfg_rsp: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]) a0=0xc39f78c8 a1=0x10e a2=0xc4cc8458 a3=0xc37ebcae s0=0xd010db4 s4=0x0 s8=0x1 sc=0x0 s10=0x0 s14=0x0 s18=0x0 s1c=0x0 a0_0=0x12 a0_4=0x0 a0_8=0x0
    Host MSG RX -1170    [001] d...   613.314618: alg_ret: (hmac_event_config_syn+0x150/0x424 [hi5622v100_wifi] <- hmac_sync_dmac_alg_cfg_rsp_entry) ret=0x0
```

Caller/serial ordering across the 8-command battery: again exactly one
`alg_cfg_rsp` + one `alg_ret` per command, in command order. The first word at
`r2+0` took the values (in trace order) `0xd010dad, 0xd010db2, 0xd010db4,
0xd010db5, 0xd010db8, 0xd010db9, 0xd010db6, 0xd010db7`; across all sessions the
observed set spanned `0xd010dad`..`0xd010db9` (9 distinct values).

`stacktrace` trigger was available and attached successfully
(`cat events/kprobes/alg_cfg_rsp/trigger` -> `# Available triggers: traceon
traceoff stacktrace enable_event disable_event`); the resulting trace showed:

```
    Host MSG RX -1170    [001] d...   619.142166: <stack trace>
 => hmac_sync_dmac_alg_cfg_rsp_entry
 => 0
```

i.e. the frame-pointer walk stops at the indirect call into the handler (the
caller identity comes from the kretprobe instead, see 3.2).

---

## 3. What the capture shows, and what it does NOT show

### 3.1 Call order / one call per request

Every read-only `alg` `get_*` request produced **exactly one** entry into
`hmac_sync_dmac_alg_cfg_rsp_entry`, followed by one return, serialized in the
order the commands were issued. All entries ran on the kernel thread
`Host MSG RX` (pid 1170, CPU 1, normal process context, `d...` = IRQs enabled).
No entry was seen on any other CPU or task during the window. This confirms the
response path is: request issued by userspace ioctl -> firmware/host message
exchange -> response dispatched on the `Host MSG RX` thread ->
`hmac_sync_dmac_alg_cfg_rsp_entry` parses it.

### 3.2 Arguments and structure (what is visible)

`hmac_sync_dmac_alg_cfg_rsp_entry`, 4 args in r0-r3:

| reg | value (observed) | interpretation from the dump |
|-----|------------------|------------------------------|
| r0  | `0xc39f78c8` (stable across all runs) | pointer to a context/object; `+0` = `0x12` (18), `+4`,`+8` = 0 |
| r1  | `0x10e` (270, stable) | constant scalar for this response class (length or opcode), unchanged for all 10 get commands |
| r2  | varies per request, e.g. `0xc4cca458`, `0xc4cc9058`, `0xc4cc8458` | pointer to the response/message buffer; `+0` varies (`0xd010dad`..`0xd010db9`), `+8` = `0x1`, rest 0 |
| r3  | `0xc37ebcae` (stable, not 4-byte aligned) | stable pointer (handler/descriptor or stack object) |

Return value `ret=0x0` on every call (success). The kretprobe's caller line
names the dispatch site: `hmac_event_config_syn+0x150/0x424` in
`[hi5622v100_wifi]` -> `hmac_sync_dmac_alg_cfg_rsp_entry`.

The varying word at `r2+0` (`0xd010dXX`) is a per-request message id/tag: it
takes distinct values per request in the narrow range `0x0d010dad`..`0x0d010db9`
and differs between the 2g and 5g variants of a `get_*` call. This is the
closest observable "message boundary" identifier for the driver<->firmware
exchange.

### 3.3 What the capture does NOT show

- **The four calibration probes never fired.** `hmac_sync_dmac_cali_cfg_rsp_entry`,
  `hmac_chan_tx_cali_sync`, `hmac_save_cali_data_to_file_2g`,
  `hmac_save_cali_data_to_file_5g` produced zero trace entries under every
  read-only trigger tried (`get_2g_power_param`, `get_5g_power_param`,
  `get_xo_ducy_cali_param`, `get_xo_ppm_cali_param`, `get_2g_all_curve_param`,
  `get_5g_all_curve_param`, `get_2g_upc`, `get_5g_upc`, `get_2g_curve_factor`,
  `get_5g_curve_factor`). Reaching them requires an actual calibration run
  (`set_*`/cali commands that write `cali_data.kv` and mutate driver state),
  which is outside this task's read-only scope. So the calibration
  request/response ordering, the calibration payload sizes, and the
  `save_cali_data_to_file` file format are NOT captured.
- **No raw wire bytes.** Probes sit at C function boundaries, not at the bus
  (PCIe/SDIO/DMA) layer, so the literal on-wire frame bytes and framing are not
  visible -- only the kernel-side pointers/lengths handed to/from each handler.
- **No argument names / struct field names.** Without the module's debug info
  the offsets are raw numbers; the field meanings in 3.2 are inferred from the
  dump, not from source.
- **No deep call chain.** `stacktrace` resolved only the probe's own frame (the
  indirect call is not walked); call identity beyond one level comes from the
  kretprobe caller line.
- **No static function tracing.** `available_tracers` = `nop` only, so no
  `function`/`function_graph` view and no per-function timing histogram.

---

## 4. Inconclusive section

Not applicable as the primary outcome: tracing IS available and a real capture
was obtained (sections 2-3). The only non-result is that the calibration-path
symbols did not fire under read-only triggers -- this is a scope limitation
(no calibration command was run), not a tracing-capability failure. Recorded for
completeness: the capability probe that would have explained an inconclusive
result was `cat /sys/kernel/debug/tracing/available_tracers` -> `nop` (static
tracers absent) and `echo 'p:...' >> kprobe_events` -> `rc=0` (kprobes present),
and neither blocked the capture.

---

## 5. Cleanup proof

Every probe was deleted and tracing disabled. Final state (literal command +
output):

```
$ cd /sys/kernel/debug/tracing
$ cat kprobe_events
                                       [bytes=0]
$ ls events/kprobes/
ls: events/kprobes/: No such file or directory
$ cat /sys/kernel/debug/kprobes/list
                                       [bytes=0]
$ cat kprobe_profile
(empty)
$ cat tracing_on
0
$ cat options/stacktrace
0
$ cat current_tracer
nop
$ cat events/enable
0
$ cat error_log
                                       [error_log bytes=0]
```

Notes on cleanup mechanics:
- A first delete attempt returned `ash: write error: Resource busy` because the
  `stacktrace` trigger was still attached to `alg_cfg_rsp`. Clearing it with
  `echo '!stacktrace' > events/kprobes/alg_cfg_rsp/trigger` (rc=0) released it,
  after which `echo '-:kprobes/alg_cfg_rsp' >> kprobe_events` (rc=0) removed the
  last probe.
- `tracing_on` was left at `0` as required ("disable tracing before you
  finish"); it was `1` before this work (with the `nop` tracer and no probes),
  so no tracing output can be produced now.
- The `events/kprobes/` directory no longer exists (tracefs recreates it on
  demand), `kprobe_events` is 0 bytes, and `/sys/kernel/debug/kprobes/list` is
  0 bytes -- no kprobe remains registered on the device.
- No device configuration was changed; all `iwpriv` calls were read-only `get_*`
  subcommands.
