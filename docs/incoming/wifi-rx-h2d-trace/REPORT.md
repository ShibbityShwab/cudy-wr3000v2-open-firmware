# REPORT - the stock H2D trace, the decoded vendor debug modules, and the F1 narrowing

**Lane:** `rx-h2d-trace` (shared task **task-176**; FEATURE-BACKLOG items **RX-1** and **RX-2**),
2026-11-27. **Host-only**: static analysis of the local vendor artifacts + a bench procedure for the
Lead. No device contact was made by this lane; no vendor binary or firmware byte is committed.

Evidence markers: **[V]** verified by this lane from a named local artifact or a committed lane;
**[V\*]** decoded by an earlier committed lane and cited; **[M]** measured on the bench by an
earlier committed lane; **[I]** inference; **[U]** unknown.

Companions: [README.md](README.md), [RUNBOOK.md](RUNBOOK.md),
[TABLES-debug-interfaces.md](TABLES-debug-interfaces.md), [H2D-FORMATS.md](H2D-FORMATS.md),
[tools/](tools/), [evidence/](evidence/).

---

## 0. Verdict

The RX gate is still the one open bug, and until this lane the "stock H2D trace" was a wish with
no instrument. It is now a **concrete, read-only, previously-proven instrument plus a decoded wire
format**:

1. The vendor's H2D messages are not hidden in firmware or in an opaque mailbox. They are **HCC
   frames in host DRAM**, posted to the device as **8-byte SR descriptor nodes** in a PCIe-visible
   ring. The live ring, its node array and every message buffer are readable read-only from the
   stock userspace through the endpoint's BAR0 window + `/dev/mem` - this was already proven on a
   normal vendor boot (phase 25 read the vendor's own SR ch0: base `0x848F6000`, nodes
   `{buf, len}`, and the buffers' HCC headers) **[M]**.
2. The vendor debug modules (`wifi_debug.ko`, `plat_debug.ko`) were decoded: **neither has an H2D
   message log**. Their H2D-relevant facility is `save_hostmem` (host-physical DDR dump, useful as
   a bulk snapshot/second instrument); their `hcc_queue_info_dump` export is a **no-op stub** in
   this build **[V]**. The H2D trace therefore does not depend on the debug modules at all (but the
   bulk-dump path uses them).
3. The **kprobe** instrument is proven available on the stock 5.10.201 (tracefs mounted, dynamic
   kprobes fully functional, only the `nop` static tracer; `kptr_restrict=0`, module symbols in
   `/proc/kallsyms`) **[M]**, and the one-shot S08 init-aid that arms 26 probes inside the
   module-load window is a proven technique on this board **[M]**. kprobe on `hcc_msg_tx` with a
   nested-dereference fetch of the header pointer at the message object's `+0x118` gives the
   ordered id/list; the SR-ring sampler gives the authoritative bytes.
4. **F1 narrowing (static):** the vendor init chain itself sends nothing **[V\***], the g0-id1
   config-key surface is fully enumerated with exactly **one unaccounted firmware-live key
   (`0x0004`)** **[V\***], and the direct-id space of the vendor's 77 HCC builders is fully
   enumerated **[V\*]**. This lane adds the missing dimension: the vendor also speaks
   `hcc_msg_tx_to_core` (band-core messages) - a message class **our driver's H2D path has never
   exercised** - and none of the three spaces is contradicted by the RX evidence. The stock trace is
   the discriminator; the offline diff in §5 turns the capture into a ranked F1 candidate list.

---

## 1. Inputs and method

| item | value |
| --- | --- |
| vendor module (2.5.24 pair-A stack) | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` - 3,581,748 B, sha256 `22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2`, unstripped **[V]** (this is the 2.4.15 rootfs copy of the same vendor module family; the runbook re-hashes the pair-A copies on the device) |
| debug module wifi_debug | 437,500 B, sha256 `b5111503289a2732b24382aeb7253122abb68e9c364d39366c3258c290ef1995`, ELF32 ARM EABI5 relocatable, not stripped, 1,976 `nm` lines; `.modinfo`: `license=GPL`, `name=wifi_debug`, `depends=hi5622v100_plat,hi5622v100_wifi`, `vermagic=5.10.201 SMP mod_unload ARMv7 ` **[V]** |
| debug module plat_debug | 99,304 B, sha256 `583b8d8a071c4834e2baeaa462573c26d656fabd2101a008bc494a3be7b453f6`; `.modinfo`: `license=GPL`, `name=plat_debug`, `depends=hi5622v100_plat`, `parmtype=power:bool`, same vermagic **[V]** |
| platform module | `hi5622v100_plat.ko` - contains the HCC/BAL/PCIe transport: `hcc_msg_alloc` 0x11d9c, `hcc_msg_tx` 0x11bf8, `hcc_msg_tx_to_core` 0x11b2c, `hcc_queue_*`, `shuangta_ete_sr_dscr_fill`, `pcie_msg_send` - 1,958 `nm` lines **[V]** |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`; CPU address = file offset + `0x40000` **[V\***] |
| disassembly | project toolchain `/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump -dr`; capstone 5.0.7 for the firmware **[V]** |
| local analysis artifacts | `build/wifi-rx-h2d-trace/` (gitignored): `wifi_debug.dis`, `hi5622v100_plat.dis`, `.nm`/`.strings` dumps, `fw_smac_strings.txt` - regenerate with the commands in [evidence/DECODE-LOG.md](evidence/DECODE-LOG.md) **[V]** |

---

## 2. Why a read-only H2D trace is possible (the vendor path, re-cited)

The complete vendor host->device chain is decoded in the committed phase docs; the parts that
matter for tracing are:

```
builder (hmac_*/hdpp_*) -> hcc_msg_alloc(len)         plat.ko 0x11d9c  ; skb + 12-byte HCC header
   header at *(msg_obj + 0x118); hdr[4]=total=len+12 ; hdr[1] low nibble = 1
 -> hcc_msg_tx(msg)                                    plat.ko 0x11bf8  ; hdr[9] = hdr[1]>>4; queue add
 -> hcc_queue_tx_process / tx thread                   plat.ko 0x1128c  ; drains the queue
 -> shuangta_ete_sr_dscr_fill(chan, buf_devva, len)    plat.ko 0x17858  ; node = {buf, (len<<16)|0x6d2b}
 -> SR+0x18 producer commit + pcie_msg_send(chip, 3)   plat.ko 0x160f4  ; doorbell out[0]=1<<3, out[2]|=1
```

(All four function addresses/offsets are **[V\***] from phase 20/tx-path, phase 24/hcc-command-layer,
phase 25/live-vendor-ring-ground-truth and this lane's `nm`/relocation re-check of `plat.ko`.)

The consequence that makes the trace possible:

* the **message bytes stay in host DRAM** (the SR node only points at them: `word0 = buffer
  address`, `word1 = (len<<16)|0x6d2b`) - they are not copied into a device mailbox **[V\***];
* the **ring and its registers are host-visible**: ETE window at device CA `0x4003a000` is mapped
  into BAR0's region-3 viewport at host `0x403f2000` (= `0x403b8000 + (CA - 0x40000000)`), and the
  SR channel blocks are at `+0x400` (ch0) / `+0x450` (ch1) / `+0x4a0` (ch2) with
  `+0x08` ctrl, `+0x10` base (host physical), `+0x14` depth-1, `+0x18` producer,
  `+0x1c` consumer **[V\*** / phase-20 A.7 measured on a fresh vendor boot] **[M]**;
* the base register's value and the node buffers are **host physical addresses in the DDR range**
  (measured values `0x84543000` / `0x8248xxxx`; the vendor's inbound window is identity for these
  ranges) and the stock vendor kernel allows `/dev/mem` reads of them - phase 25 read them
  read-only from userspace on a normal vendor boot **[M]**.

---

## 3. The vendor debug modules, decoded (full table in TABLES-debug-interfaces.md)

**Load facts.** Neither module autoloads: `/usr/bin/wifi_init.sh` loads `wifi_debug.ko` only if
`/etc/hi_version` contains "turbo", which it does not **[V\*** phase 15]. They are loaded by hand
from the stock root shell (`insmod /lib/hisilicon/ko/wifi_debug.ko`, rc=0 measured in phase 7
**[M]**). **Never `rmmod`**: unloading `wifi_debug` panics the router (**HAZARDS.md §1 [M]**); the
aid/restore removes the module by *reboot*.

**`wifi_debug.ko` surface (decoded this lane from the unstripped module [V]).** Its init creates
four writable sysfs nodes under `/sys/hisys/plat/` (measured in phase 7 **[M]**: `pcie`,
`hcc_test`, `hcc_test_cfg`, `plat_test`) and extends `/sys/hisys/hipriv` from 100 to 181
commands **[M]**:

| surface | what it is | H2D trace use |
| --- | --- | --- |
| `/sys/hisys/plat/pcie` | command dispatcher (`oal_pcie_set_debug_info`); `cat` lists 30 commands, `echo help` dumps the grammar to dmesg. Contains **read commands** `readmem`, `savemem`, `save_hostmem`, `readconfigmem`, `read32/read16`, `host_read32`, `dump_all_regions{,_mem,_info}` and **write/dangerous commands** `write32/write16`, `host_write32`, `loadfile`, `performance_*`, `wlan_power_on/off`, `pcie_relink_test`, `pcie_ete_test`, `testcase`, `try_to_sleep`, `host_wakeup_dev` | **`save_hostmem` only** (read-only host-DDR dump; exact decode below). Never the write commands. |
| `/sys/hisys/plat/hcc_test` + `hcc_test_cfg` | HCC test transmitter: modes `HCC_TX_MODE`/`HCC_RX_MODE`/`HCC_LOOP_MODE`, a "verify 0/1" mode, a workqueue and its own D2H handler registration | **not a trace**: it sends *its own* test messages and verifies loopback; it does not log the driver's H2D. Its sysfs group also failed to register (-ENOMEM) in one measured stock boot **[M]** - optional/uncertain, not used. |
| `/sys/hisys/plat/plat_test` | platform test commands: `plat_excp_relink`, `plat_device_bugon`, `plat_heartbt_start/stop_test`, `wlan_power_on/off/loop`, `pcie_access_test`, `plat_pcie_loop_test`, `plat_memory_test`, `pcie_l1ss_*`, `ipc_msg_test`, and the **OAM log switches** `device_oam_log_switch_cfg`/`host_oam_log_switch_cfg`/`oam_log_switch_cfg` | none needed for H2D. The OAM-log switches enable *firmware-side* logging (a future RX-2 aid), not the H2D list. |
| `/sys/hisys/hipriv` | 81 extra commands incl. `get_all_regs`, `reginfo`, `dump_rx_dscr`, `dump_tx_dscr`, `dump_all_dscr`, `dump_ba_bitmap`, `stat_dump` | firmware-mediated dump requests (they *send* H2D; they do not capture H2D). |
| imported `hcc_queue_info_dump` | expected queue dump | **no-op stub** in this build: `plat.ko 0x124a8: mov r0,#0; bx lr` **[V]** - the facility does not exist here. |

**The one useful decode: `save_hostmem` [V].** Function `oal_pcie_debug_save_hostmem` @
`wifi_debug.ko .text+0xf04`: parses **three** arguments (`sscanf_s` `.LC34`), `ioremap()`s the
first as a **host physical address** with the length rounded up to a word, `vmalloc()`s a bounce
buffer, opens the third argument as a file, copies 4 bytes at a time (`ldr`/`str` with
`dsb`/`arm_heavy_mb`), then writes it out. It does **not** call the device
`oal_pcie_devca_to_hostva` transform - so it is a host-RAM dumper, exactly what a bulk H2D snapshot
needs. Command grammar (with the dispatcher's chip-id quirk measured in phase 7 **[M]**):
`echo 'save_hostmem 0 0x<host-phys> <decimal-len> <path>' > /sys/hisys/plat/pcie`.
Danger note: the node also accepts writes; our procedure only ever uses read commands.

**`plat_debug.ko` is the same code family for the platform-only dependency set** (it carries its
own `oal_pcie_debug_*` + `hcc_test_*` implementations and depends on `hi5622v100_plat` only) - it
duplicates the sysfs surfaces, so **loading both modules would collide**; the correct module for the
combined stack is `wifi_debug.ko`, and phase 7's "plat_debug sysfs init faulted" observation is
consistent with the duplicate-group collision **[I]**. Do not load both.

**The base platform debug node is a stub [V].** `hi5622v100_plat.ko` creates
`/sys/hisys/plat/debug`  (the read-only node that already exists before `wifi_debug.ko` loads):
its get handler `plat_debug_sysfs_get_info` 0x1a55c only calls `oam_warning_log0`; its set handler
`plat_debug_sysfs_set_info` 0x1a554 is `mov r0,#0; bx lr`. No trace data there.

**Conclusion:** the debug modules are **not** the H2D trace instrument. The trace instrument is the
live SR ring (and, for ordering/attribution, tracefs kprobes); `save_hostmem` is a useful second
instrument for a bulk host-DRAM snapshot of the same data.

---

## 4. The stock-image H2D trace plan (RX-1)

The full command-level procedure is [RUNBOOK.md](RUNBOOK.md); this section is the design and the
evidence that each step is grounded.

### 4.1 What is captured and why this is the vendor's real sequence

Boot the **vendor pair-A image** (stock 2.5.24: vendor kernel 5.10.201 + vendor rootfs + the vendor
wifi stack loads normally; the box otherwise runs our pair-B image). The vendor driver then performs
its own full bring-up: `wifi_init.sh` -> `hi5622v100_plat` -> `hi5622v100_wifi` -> the vendor
userspace (softapd/app_acs/app_nlc/wpad) brings up its AP. Every message of that sequence is available
in the SR ring as it is posted; a sampler started before the wifi module loads records the boot H2D
list. This is exactly RX-1's evaluation: "a captured H2D message list from the vendor's working
boot, diffed against ours".

### 4.2 Capture method A (primary): live SR-ring sampler, read-only

A small static ARM binary (`tools/omo_h2dsampler.c`) run by a one-shot `S08` init aid on the
stock image:

1. waits for `/sys/module/hi5622v100_plat` (the aid is at S08; the module load is later - the same
   ordering that let the phase-21 aid arm probes in the ~300 ms window **[M]**);
2. opens `/dev/mem` (stock vendor kernel allows the reads; phase 25 proved host-DDR reads **[M]**)
   and maps/loops over the ETE window at host `0x403f2000`;
3. every ~20 ms reads SR ch0..2 `+0x08/+0x10/+0x14/+0x18/+0x1c`; on producer advance, reads the
   new 8-byte nodes from the base and every new buffer's header+payload;
4. writes one tab-separated record per message to `/overlay/h2d-trace.log` (with a raw hex body
   field), plus periodic ring-state lines; stops after `-t` seconds or on a stop-file.
5. the aid (not the sampler) is removed after collection; the module set is never `rmmod`ed.

Why it is safe: **zero writes of any kind** - reads of host RAM, reads of a BAR0 window, and a file
append. It never touches the forbidden CA `0x400392f0`, never writes flash, never reloads modules.
The ring may be sampled imperfectly (recycling, as phase 25 noted) - the decoder marks torn pairs
and the kprobe complement closes ordering gaps; the capture is re-runnable on a fresh power-cycle.

### 4.3 Capture method B (complement): kprobe trace on `hcc_msg_tx`

The same aid arms a kprobe set **before** the wifi module loads (one single write of all definitions
to `kprobe_events`, the phase-21 arming trick), then enables it:

```
p:h2d_tx hcc_msg_tx msg=%r0 hdr=+0x118(%r0) \
    w0=+0(+0x118(%r0)):x32  w1=+4(+0x118(%r0)):x32  w2=+8(+0x118(%r0)):x32 \
    w3=+12(+0x118(%r0)):x32 w4=+16(+0x118(%r0)):x32 w5=+20(+0x118(%r0)):x32 \
    w6=+24(+0x118(%r0)):x32 w7=+28(+0x118(%r0)):x32 w8=+32(+0x118(%r0)):x32 \
    w9=+36(+0x118(%r0)):x32 w10=+40(+0x118(%r0)):x32 w11=+44(+0x118(%r0)):x32 \
    w12=+48(+0x118(%r0)):x32 w13=+52(+0x118(%r0)):x32 w14=+56(+0x118(%r0)):x32 \
    w15=+60(+0x118(%r0)):x32
p:h2d_send pcie_msg_send chip=%r0 id=%r1
```

with `trace_pipe` redirected to a file for the same window. This is grounded in: tracefs + dynamic
kprobes work on this kernel **[M]**; recursive dereference (`+offs(FETCHARG)`) is a documented 5.10
kprobe-events feature (checked in the local 5.10.201 tree's `Documentation/trace/kprobetrace.rst`
and `kernel/trace/trace_probe.c` recursion) **[V]**; `hcc_msg_tx` is a FUNC symbol in
`hi5622v100_plat.ko` and module symbols are visible because `kptr_restrict=0` **[M]**; the message
object stores the header pointer at `+0x118` (phase 3/24/25 decode) **[V\*]**.
Caveat: the `+0x118` offset is the vendor's sk_buff-ABI-dependent header pointer slot, verified from
the vendor disassembly (`hcc_msg_tx` itself loads it) - if the probe's fetch reads a null header for
a malformed object the trace event shows zeros; it cannot fault the box (kprobe fetch is fault-safe).
Method B gives **order + call-site identity**; method A gives the authoritative bytes. Run both in
the same boot.

### 4.4 Capture method C (bulk snapshot): `wifi_debug.ko` `save_hostmem`

Optional. After the stock stack is fully up (and after the aid has captured the boot), load
`wifi_debug.ko`, read the SR base register, then dump the ring node array (e.g. 0x1000 B at the
base) and a window of the message buffers with
`echo 'save_hostmem 0 0x<host-phys> <len> /overlay/h2d-bulk.bin' > /sys/hisys/plat/pcie`.
This is the fallback for a kernel where `/dev/mem` RAM access is restricted, and the tool for a
"one-shot snapshot" of the ring state. It requires the debug module load (never unload: reboot only)
and writes only /overlay files.

### 4.5 What the capture is compared against (the "ours" side)

Our driver's committed H2D set (the fired association/mgmt-carrier tree), assembled from the
committed lanes:

* **g0 id1 config events**: key `0x0064` (context create, 20 B data; also the VAP-add carrier),
  key `0x00D8` (TXQ MIB config, 20 B - the captured vendor first post-ready frame),
  key `0x0320` (TXQ address sync, 4 B), key `0x0065` (VAP start);
* **g0 direct ids**: id 6 (mgmt TX), id 7/8 (user add / user add notify - the 5 GHz order fix posts
  these after the VAP add);
* **WAL group-2 AP-start family** (cmd `0x0124` -> `0x0005` -> `0x006D` -> `0x0127` -> `0x0065`
  with the band byte).

Nothing in our set is a band-**core** message (`hcc_msg_tx_to_core`), and our set never contains
key `0x0004`. Both facts are integral to the F1 narrowing (§6).

---

## 5. The offline analysis plan (what the capture turns into)

Formats and validation rules are in [H2D-FORMATS.md](H2D-FORMATS.md); the host tool is
`tools/decode_h2d.py`. Pipeline:

1. **Normalise.** Each sampler record -> `{ts_ms, ch, idx, node_len, buf_phys, hdr_total, group,
   bid, vap, chip, id, dst, magic_ok, payload_hex}`; kprobe records -> `{ts_us, id, group, total,
   hex64}`. A message is **VALID** iff
   `magic == 0x5a5a`, `12 <= node_len <= 0x674+12`, `hdr[4] == node_len` (the vendor's own
   invariant, phase 25 **[M]**) and `group <= 3`, `id <= 0x3f`. Torn/recycled records are
   **quarantined**, never silently dropped.
2. **Classify.** (a) group 0 / id 1 -> config event: key = u16 @ +20, sub-len = u16 @ +22, data @
   +24, the 4-byte `0xff` tail inside total; (b) other group/id -> direct message, dump the first
   52 payload bytes; (c) group 1/2 -> band-core message (the dimension we never exercise).
3. **Order + dedupe.** Sort by timestamp; collapse the same (group,id,key,len,first-16-bytes) seen
   twice (ring re-sampling) into one entry with a count; keep the first/last timestamps.
4. **Diff vs ours.** The decoder compares every valid message against the "our set" table (the §4.5
   list, encoded in `tools/ours_h2d_set.txt`) and emits three ranked buckets:
   * **MISSING-IN-OURS** - vendor sends it, we never do. This is the RX-2/F1 candidate bucket;
   * **WE-SEND-UNCERTAIN** - matches a key/id we send but with a different length or leading 32
     payload bytes (format drift, e.g. the 5 GHz band-field class);
   * **UNMATCHED-FAMILY** - a group/id/key the committed tables do not know at all (new decode).
5. **Decode the candidate's firmware handler** (closes RX-2's "decoded message + firmware handler
   address" criterion):
   * g0 id1 messages -> key table A dispatch already decoded (CA `0x43D58` handler, key table A CA
     `0x10C23C`, committed `wifi-ctx-create/tools/ctx_create_re.py --tables`);
   * direct ids -> the firmware H2D top-level dispatch is still [U]; document the captured
     (group,id) pair with its bytes and hand it to a firmware lane with the firmware's message
     handlers named by the committed phase-25 tables. The *id itself* is the narrowing; the
     handler lookup is a follow-up decode, not a bench item.
6. **Report.** `decode_h2d.py --diff` prints the ordered list, the three buckets, and the
   PASS/FAIL line for RX-1: `CAPTURE VALID: N messages, M families, T seconds`.

**Decision table (what the trace settles):**

| capture outcome | meaning for RX / F1 |
| --- | --- |
| vendor boot sends the same families we send (+ nothing else except the known txq/user/mgmt set) | F1 as "a missing host message" is **refuted**; the fault is in the ring/PPDU descriptor contract or a firmware-internal state, and RX-4/F4 or a deeper firmware decode is the path |
| vendor sends a g0 id1 key we never send (e.g. `0x0004`) | that key is the F1 candidate; fire the existing fail-closed patch (task-170, `wifi-rx-init-msg/`) and/or a trace-matched frame |
| vendor sends a **core** (group 1/2) message we never send | new F1 candidate class; decode its id/bytes, add the to_core carrier, fire |
| vendor sends an id/key at a different length or with different leading bytes than ours | our frame is format-wrong for that family; patch the byte-level delta and fire |
| the aid/capture cannot run (no stock root channel / no /dev/mem) | fall back to method C (`save_hostmem`) or method B (kprobes); if neither, the line stays blocked on the Lead's bench decision - not on a guess |

---

## 6. F1 narrowing: the static candidate inventory (RX-2)

The F1 hypothesis at the bench: "the firmware's RX ring/PPDU-descriptor state is never
(re)initialised by our minimal host message set; the vendor sends a pre-RX request
(`smac_msg_proc_rx_ring_reset_req`/`enable_trx_req` class) that our driver never sends". The static
picture is now complete enough to bound the candidate space exactly:

**Space 1 - g0 id1 config events [V\*].** The firmware's handler (CA `0x43D58`) dispatches on the
u16 key at payload+8 through key table A (CA `0x10C23C`, 217 entries); exactly **8 keys are live**:
`0x0004, 0x0064, 0x0065, 0x0066, 0x0067, 0x0069, 0x00D8, 0x0320`. The vendor module's 266
config-event call sites cover 7 of them; **key `0x0004` has no sender anywhere in the shipped
module** (exhaustive negative), and its firmware worker (CA `0x811CD`, decoded in the committed
lane) is a one-byte write into the created context block (`ctx[+0x190][0x52] = data[0]`, clean
refusal without the context). That is candidate **F1-a**, already delivered as a fail-closed patch
+ fire recipe by task-170 (`../wifi-rx-init-msg/`); **not proven** to be the RX fix.

**Space 2 - direct ids [V\*].** The vendor's 77 HCC builders and the ids they write at header+6 are
enumerated in `../wifi-rx-init-msg/TABLES-h2d-builders.md`. This lane re-read that table for an
RX-init-class id: **there is none** - the direct-id space is call/response (scan, auth/assoc,
user add, BA, TX completion, mgmt TX, acs/dfs/spectral/11v reports), and nothing named or shaped
like RX ring reset / trx enable. So F1 is **not** a direct-id message in the shipped module.

**Space 3 - the band-core dimension (new, this lane) [V].** `plat.ko` exports
`hcc_msg_tx_to_core` (0x11b2c) and the vendor has **11 builders** that use it
(`hmac_tx_post_event` id 1, `shuangta_irq_host_mac_isr` id 2, `hmac_rx_data_send_continue_msg`
id 3, `hmac_tx_complete_notify_other_core` id 4, `shuangta_irq_host_ba_info_ring_overrun_isr`
id 6, `shuangta_always_rx_output_format` ids 0/7, `hmac_ba_timeout_fn` id 8). Our driver's
committed H2D path is a **chip-group (group 0)** carrier; it has never sent a core-group message
(the `hcc.c` debug grammar can be told `group=2`, but no driver code path emits one). This is the
only vendor H2D class completely outside our replication. It is a real candidate class for F1-b:
"the firmware core's RX path expects a core-directed message the host never sends". Note the two
honest limits: (i) the builders are runtime events/flow control, not an init sequence, so a *boot*
trace may show none of them before the AP starts - the sampler window must cover the AP start; (ii)
the firmware's core-side handler table (chd/core dispatch) is only partly decoded (12 named slots in
the committed phase-25 tables), so a captured core id must be matched to a handler as a follow-up
decode.

**Space 4 - the specific names are unreachable statically [V].** The firmware's 45-name
`smac_*` tail name pool (containing `smac_msg_proc_rx_ring_reset_req` @ file `0xE28BC` and
`smac_msg_proc_enable_trx_req` @ file `0xE29A8`) is **referenced by no pointer, no index array and
no computed base** under every tested bias - proven exhaustively in phase 8 (this lane re-ran the
quick checks: no u32/u16 literal references to those string addresses in the firmware **[V]**). So
the `rx_ring_reset_req`/`enable_trx_req` handlers **cannot be found by name**; the remaining static
route to them is to map a captured (group,id) / config key to the firmware dispatch and follow it
(phase 8 §4's three SMAC opcode dispatchers at file `0xDA0FC/0xDB5EC/0xDC378` are the record of
that open route; this lane's disassembly of them shows they are SMAC internal opcode dispatchers,
not the H2D mailbox dispatch, so this route remains [U]).

**The F1 conclusion is therefore a bound, not a message:** any true F1 fix must be one of
(a) config key `0x0004`; (b) a core-group (to_core) message; (c) a runtime message produced by
vendor code paths that our host never enters (which the trace will show as an UNMATCHED or
MISSING family). The stock trace cannot be replaced by more static analysis: the three spaces are
already exhaustively enumerated on the static side. That is why RX-1 is the critical path.

---

## 7. Honest limits and risks

* **The trace needs the stock root channel.** The procedure uses the documented no-broker install
  channel (crafted config-backup restore, lane `cudy-backup-root`) to drop the aid + sampler as
  persistent files on the stock rootfs, then one attended reboot. If the channel is not usable, the
  fallback is method B/F with a temporary telnet root session only if one exists - otherwise RX-1
  waits for the Lead's bench decision.
* **Stock pair A is the control image**; the restores write only the stock *overlay* (persistent
  files), never flash, never pair B, never the selector. The runbook's T7 removes the aid and
  returns to pair B; the recovery door stays armed for the whole sitting.
* **No /dev/mem = no method A.** The T2 read-only preflight checks that `/dev/mem` can read one
  known-good physical word range; if it cannot, use method C (`save_hostmem`) and/or method B.
* **Ring churn.** The vendor's SR ring recycles fast (phase 25's own caution). The sampler catches
  what it can; the diff marks repeats; the kprobe stream is the ordering cross-check. A second
  power-cycled capture at a higher sample rate is the fallback for any gap.
* **The debug module is not needed for the primary method and must never be unloaded** (HAZARDS §1);
  method C loads it once and ends with a reboot. `plat_debug.ko` must not be loaded alongside it.
* **No claim is made that the trace closes the RX bug by itself.** It closes RX-1 (the reference
  list) and produces the RX-2 candidate with bytes; the fire of that candidate is the Lead's call.
* **Firmware handler lookup for a captured direct/core id remains a decode task** (Space 4); the
  trace's ids are the input, not the answer.

---

## 8. Reproduction

```
# decode dumps / tables (host; local artifacts are never committed)
$ .provision-venv/bin/python opensource/docs/incoming/wifi-rx-h2d-trace/tools/decode_h2d.py --selftest
$ opensource/docs/incoming/wifi-rx-h2d-trace/tools/run-host-tests.sh
# device sampler build (host-side cross-compile; the binary is a local artifact)
$ opensource/docs/incoming/wifi-rx-h2d-trace/tools/build-sampler.sh
```

The exact commands/hashes used for every decode in this report are in
[evidence/DECODE-LOG.md](evidence/DECODE-LOG.md); the host-test transcript is in
[evidence/host-tests.txt](evidence/host-tests.txt).
