# ringwatch: the two 32-slot rings seen moving in real time (phase 13, 2026-10-01)

**Result: an open, read-only kernel module (`lab/ringwatch`) maps BAR0 and, on a debugfs write,
snapshots three regions of the live chip memory into a timestamped ring buffer we can read back. It
watched the two pointer rings `0x1d0900` and `0x8c8900` plus the live queue struct `0x1b8e00` for
~10 s of live traffic. The packed markers are free-running 5-bit index counters (modulo 32, wrapping,
all 32 values observed); the 32-slot array they bracket is 128 bytes, i.e. a **4-byte slot stride**;
and across the five `get_2g_power_param` commands the live queue struct at `0x1b8e00` flipped exactly
once, from `{000c6903, 01042de8, 0010f1f0}` to `{0017e630, 00000002, 0017e630}`.**

`insmod`/`rmmod` both returned 0, the debugfs node was gone after unload, the device never rebooted
(uptime `11:42` before / `11:44` after), and nothing was written to the chip or its register space.

Source (read-only toward the chip): `lab/ringwatch/ringwatch.c` + `lab/ringwatch/Makefile`.
This module is built only for reading: there is no `iowrite32`, no config-space write, no
`pci_request_region`, no `pci_enable_device` and no reset anywhere in it. The only write it accepts is
userspace writing into **our** debugfs node, which selects "take a snapshot".

---

## The module

- On load it finds endpoint `0000:00:00.0` (`59e7:0005`) with `pci_get_domain_bus_and_slot()` and reads
  the BAR0 base from PCI config space with `pci_read_config_dword()` (read-only vendor-kernel accessor,
  so no `struct pci_dev` layout risk).
- It `ioremap()`s each configured region without claiming anything, then `ioread32()`s it.
- Module parameters (both overridable with `insmod ringwatch.ko ...`):
  - `regions="start:len,start:len,..."` — BAR0 byte ranges. Default:
    `0x1d0900:0x100,0x8c8900:0x100,0x1b8e00:0x40` (the three phase-13 regions).
  - `nsnapshots=N` — ring depth, default **32**.
- Debugfs nodes under `/sys/kernel/debug/ringwatch/`:
  - `sample` (0200, write-only): any write takes one timestamped snapshot of every region into the
    in-kernel ring (last N kept, oldest overwritten). Writing `clear` empties the ring instead.
  - `log` (0444): every kept snapshot as text, one line per word — `offset value timestamp_ns`.
  - `regions` (0444): the one-shot full dump of the same three regions, captured once at load.
- The default snapshot is 64 + 64 + 16 = **144 words** (576 bytes) of chip memory per sample, with
  `ktime_get_ns()` as the timestamp.

## CI

- Workflow: `.github/workflows/build-load-test-module.yml` — added a `build ringwatch module` step, a
  vermagic line, and a `ringwatch-ko` artifact (the change is `230fad6`).
- Run: **`36838694040`** —
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36838694040
  - commit `230fad6`, conclusion **`success`** (the `build ringwatch module` step is green).
  - artifact `ringwatch-ko` = `ringwatch.ko`, **14088 bytes**, md5 **`1a849ed17c5f4daa78a42236842e4b06`**,
    vermagic **`5.10.201 SMP mod_unload ARMv7`** (matches the vendor kernel).
- The artifact was `scp`'d to `/tmp/ringwatch.ko`; the device md5 is the same
  `1a849ed17c5f4daa78a42236842e4b06` (verified with `md5sum /tmp/ringwatch.ko` on the router).

## On-device run

    ===BEFORE===   Thu Oct  1 09:35:56 UTC 2026, up 11:42
    ===INSMOD===   insmod_rc=0
    ===DEBUGFS===  -r--r--r-- log ; -r--r--r-- regions ; --w------- sample
    ===RMMOD===    rmmod_rc=0 ; /sys/kernel/debug/ringwatch/ gone
    ===AFTER===    Thu Oct  1 09:38:44 UTC 2026, up 11:44

The `get_2g_power_param` answer read during the run is the same 26-word table phase 12 recorded
(`17161605 … 0a0606ff`), i.e. the calibration surface was undisturbed.

## Experiment protocol

1. **Prescribed run** (`build/register-dumps/ringwatch_log.txt`): `echo 1 > sample` three times, 1 s
   apart (idle); then `iwpriv Hisilicon0 alg get_2g_power_param` five times; then `echo 1 > sample`
   three more times. Six snapshots, span 5.117 s.
2. **Controlled run** (`ringwatch_pair_log.txt`): `clear`, then five times
   {take snapshot → one `get_2g_power_param` → take snapshot}. Ten snapshots, each pair ~15–19 ms
   apart, so the message is the dominant thing that can move between a pre and its post (background
   traffic still lands in the window, which is why the idle pairs are a useful control).
3. **Rapid run** (`ringwatch_rapid_log.txt`): reloaded with `nsnapshots=400`, then 400 snapshots taken
   back to back (4.614 s, 86.7 snapshots/s) to watch the cursor wrap.

No `set_*` command was issued **on purpose**: the task's hazard list requires strictly read-only
behaviour toward the chip and its register space, and a vendor `set_2g_power_param` writes to the
chip even when it writes the same value back. The safe branch ("otherwise stay with get") was taken.

## Raw dmesg (`build/register-dumps/ringwatch_dmesg.txt`, trimmed)

Load (run 1), verbatim:

    [42127.374673] omo-ringwatch: BAR0 base=0x40000000, 3 regions, 144 words/snapshot, ring=32, read-only, no claim
    [42127.384518] omo-ringwatch: region 0 BAR0+0x1d0900 len=0x100 first=000033b9
    [42127.391703] omo-ringwatch: region 1 BAR0+0x8c8900 len=0x100 first=000033b9
    [42127.398547] omo-ringwatch: region 2 BAR0+0x1b8e00 len=0x40 first=00000000
    [42127.405316] omo-ringwatch: load-time dump ts_ns=42127375374050 ready

The six prescribed snapshots, verbatim:

    [42130.835094] omo-ringwatch: snapshot 1 ts_ns=42130835669730 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000
    [42131.851623] omo-ringwatch: snapshot 2 ts_ns=42131852195550 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000
    [42132.868049] omo-ringwatch: snapshot 3 ts_ns=42132868621390 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000
    [42133.919868] omo-ringwatch: snapshot 4 ts_ns=42133920442610 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000
    [42134.936294] omo-ringwatch: snapshot 5 ts_ns=42134936866620 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000
    [42135.952363] omo-ringwatch: snapshot 6 ts_ns=42135952935820 r0@0x1d0900=000033b9 r1@0x8c8900=000033b9 r2@0x1b8e00=00000000

(The dmesg line prints each region's first word, not the marker; the markers are in `log`.)

## Snapshot excerpts

### The two packed markers over the prescribed run (`log`, low 15 bits)

| snapshot | phase | `0x1d093c` (raw) | idx | `0x1d09c0` (raw) | idx |
| ---: | --- | --- | ---: | --- | ---: |
| 1 | idle | `80198019` | 25 | `00010001` | 1 |
| 2 | idle | `801b801b` | 27 | `801f801f` | 31 |
| 3 | idle | `80148014` | 20 | `000d000d` | 13 |
| 4 | after 5× `get` | `80158015` | 21 | `000c000c` | 12 |
| 5 | after | `80118011` | 17 | `00080008` | 8 |
| 6 | after | `800d800d` | 13 | `00040004` | 4 |

`0x8c893c` / `0x8c89c0` are **bit-for-bit identical** to `0x1d093c` / `0x1d09c0` in every snapshot,
as is the 32-word data array (see below). The marker advanced across the five-command burst:
`0x1d093c` 20 → 21 between snapshots 3 (last idle) and 4 (first after).

### The live queue struct `0x1b8e00` before/after the five gets

| offset | idle (snapshots 1–3) | after (snapshots 4–6) |
| --- | --- | --- |
| `0x1b8e34` | `000c6903` | `0017e630` |
| `0x1b8e38` | `01042de8` | `00000002` |
| `0x1b8e3c` | `0010f1f0` | `0017e630` |

These three words are **the only words in the `0x1b8e00` region that changed**, and they changed
exactly at the boundary between the idle snapshots and the snapshots taken after the five commands.
Neighbours that stayed constant: `0x1b8e30 = 0010f1f0`, `0x1b8e2c = 40000113`,
`0x1b8e28 = 01058cb8`, `0x1b8e24 = 0011038c`, `0x1b8e40` onward.

### The 32-word array and the slot stride (controlled run)

`sn02 → sn03` (dt 14 ms): the ring array at `0x1d0940` changed at **exactly slots 3, 4, 5**, and the
write cursor `0x1d093c` went `3 → 6`. `sn05 → sn06` (dt 18 ms): slots **6, 7, 8**, cursor `6 → 9`.
Every change is a contiguous run starting at the cursor's old value and the cursor advances by the run
length. The array is 32 words = 128 bytes = `0x80`, exactly from `0x1d0940` to the second marker at
`0x1d09c0`.

### Rapid run: the cursor is a 5-bit wrapping counter

400 snapshots in 4.614 s (86.7/s): `0x1d093c` covers **all 32 values 0..31 and wraps 9 times**;
`0x1d09c0` covers all 32 values and wraps 8 times. Per-snapshot delta histogram (mod 32) for
`0x1d093c`: `{0:279, 1:27, 2:27, 3:64, 7:1, 8:1}`.

---

## Analysis

### Region 1 — `0x1d0900..0x1d0a00` (the first ring)

Layout, from the load-time `regions` dump and the snapshots:

| offset | role |
| --- | --- |
| `0x1d0900..0x1d0934` | 14 words, constant over the whole run: three `{counter, counter}` pairs (`2965/2949`, `3479/3461`, `32e9/32d1`) plus small/zero words. Not this ring's state. |
| `0x1d0938` | `00000060` — a constant just before the cursor (see limits). |
| `0x1d093c` | **cursor A**, packed: low 16 bits = high 16 bits = index, bit 15 set. |
| `0x1d0940..0x1d09bc` | **32 words = 128 bytes of ring data** (4-byte slots). |
| `0x1d09c0` | **cursor B**, packed the same way (bit 15 set only in snapshot 2 of the prescribed run). |
| `0x1d09c4..` | a *different* pointer array; it changed over seconds but not within one message (see limits). |

Which words change: only the data words the cursor walks through change (49 of the 64 captured words
changed over the 5 s run, because the cursor laps the 32 slots). Words `0x1d0900..0x1d0938` never
changed.

Do the markers advance monotonically? **Yes, modulo 32.** They are free-running counters that only
ever step forward and wrap (`… 29, 30, 31, 0, 1 …`); the raw low-15-bit value can look like it went
"down" (e.g. 27 → 20) only because it wrapped inside the ~1 s between prescribed snapshots. The rapid
run proves this: all 32 residues appear and the sequence wraps 9 times in 4.6 s. The two cursors are
best read as the write/publish cursor (`0x1d093c`) and a second (read) watermark (`0x1d09c0`) of the
same 32-slot ring; their separation varies (17 slots in the controlled run, 2–3 in the rapid run), so
it is occupancy, not a fixed offset. The 16-bit index is duplicated into both halves; bit 15 of each
half is a flag that is set on all cursor-A words in these runs and *toggles* on cursor B
(`0001`, `801f`, `000d`, …), so it is a valid/wrap flag rather than part of the index (the index in
the tables above is the low 15 bits).

Slot stride: **4 bytes.** The bracketed array is 32 words for 32 slots (128 bytes). The strongest
direct evidence is the controlled run's granularity: the cursor advances by 1 and by 2 slots between
snapshots as well as by 3 (rapid-run histogram), so its unit is one 4-byte slot; the 3-slot runs are
bursts of three consecutive single-slot writes at `[old_cursor .. new_cursor-1]`, not a 12-byte
element. A 12-byte element would force the cursor to advance only in multiples of 3, which the data
contradicts.

Adjacent words: immediately before cursor A is the constant `00000060`; immediately after cursor B the
values are more pointers (`0x1d09c4 = 00011844 …`), i.e. the next structure, not part of this ring.
The slot values themselves are 4-byte-aligned pointers into the `0x00010xxx–0x00014xxx` pool
(e.g. `00010ccc`, `00013c50`), changing slot by slot as the cursor overwrites them.

### Region 2 — `0x8c8900..0x8c8a00` (the mirror)

This region is **not merely similar, it is identical**: in all six prescribed snapshots the 32-word
array at `0x8c8940` equals the array at `0x1d0940` word for word, and both packed markers
`0x8c893c`/`0x8c89c0` equal `0x1d093c`/`0x1d09c0`. The second ring is the `+0x6f8000` twin of the
first (the queue-struct mirror is `+0x6b8000`), and a snapshot cannot say which copy the engine reads
(rings.md limit 6). Inside a single 15 ms message window both copies still matched exactly.

### Region 3 — `0x1b8e00..0x1b8e40` (the live queue struct)

Only three words move, and they moved **because of the message**: `0x1b8e34/38/3c` went
`{000c6903, 01042de8, 0010f1f0}` (three idle snapshots) → `{0017e630, 00000002, 0017e630}` (three
snapshots taken right after the five `get_2g_power_param` commands). The new triple is
`{pointer, small-int, same-pointer}`; the old middle word `01042de8` looks like a handle/token and was
replaced by `00000002`. Note `0x1b8e30` held `0010f1f0`, the pre-value of `0x1b8e3c`, so the struct
duplicates a pointer at `+0x30` and `+0x3c` (relative to the region base `0x1b8e00`).

Limits 4 and 5 of `rings.md` are the right frame here: beacons, timers and competing traffic also
write these structures, so one 5-command interval proves *the message touched this struct* but not
*the exact instruction count*.

### Does a `get_2g_power_param` move the ring cursor?

In the controlled run, three of the five pre→post pairs showed **no** cursor movement at all; pair 3
showed `+3` and pair 5 showed `+1`. Background traffic advances the cursor ~62 slots/s on its own
(288 increments in the 4.614 s rapid run), so a single calibration read is not distinguishable from
the background at this sampling rate. The
message-attributable signal in this experiment is the `0x1b8e00` struct change, not the cursor.

---

## Explicit limits

1. **Read-only, no claim.** No config-space write, no `iowrite32`, no `pci_request_region`,
   `pci_enable_device` or reset. No vendor module was loaded or unloaded by us. The only writes were
   to our own debugfs `sample` node.
2. **One sample rate.** 86.7 snapshots/s is far faster than the ~62 cursor steps/s we measured, but a
   step can still be missed; the pre/post pairing mitigates this and the wrap count is exact.
3. **Attribution.** The five-command burst moved `0x1b8e00` exactly once, but beacons/timers also run;
   only the before/after boundary, not per-command causality, is shown.
4. **Which mirror is live is unknown.** `0x1d0900`/`0x8c8900` (`+0x6f8000`) are identical in every
   sample; a memory snapshot cannot say which address the DMA engine reads.
5. **The constant `00000060` at `0x1d0938`, the constant 14-word prefix, and the pointer array after
   `0x1d09c4` are not interpreted here.** They are reported as observed neighbours, not as fields.
6. **The slot values are read as 4-byte pointers but not dereferenced.** `00010ccc`–`00014xxx` is the
   descriptor-pool-looking range from `rings.md`; the module reads only these three regions and does
   not follow the pointers.
7. **No `set_*` run.** The safe branch of the task was taken; whether a value-identical
   `set_2g_power_param` moves the ring more strongly than `get` is untested.
8. **Timing resolution.** The module's timestamp is `ktime_get_ns()` in the write handler; the
   latency between the debugfs write and the `ioread32()`s of the three regions is not measured, and
   the three regions are read sequentially, not atomically.

## Pulled artifacts (under `build/register-dumps/`)

| file | what it is |
| --- | --- |
| `ringwatch_log.txt` | prescribed run: six snapshots, 871 lines |
| `ringwatch_session.txt` | prescribed run: device shell transcript + iwpriv answers |
| `ringwatch_pair_log.txt` | controlled run: ten snapshots (five pre/post pairs) |
| `ringwatch_pair_session.txt` | controlled run transcript |
| `ringwatch_rapid_log.txt` | rapid run: 400 snapshots, 58,001 lines |
| `ringwatch_regions.txt` | load-time one-shot dump of the three regions |
| `ringwatch_dmesg.txt` | dmesg, all omo-ringwatch lines (load + every snapshot) |
| `ringwatch_analyze.py` | the analysis script; every number above comes from it |
| `ringwatch_analysis.txt` | its output |

## Reproduce

    # CI + artifact (run 36838694040, commit 230fad6)
    gh run download 36838694040 -n ringwatch-ko -D /tmp/rw-ko

    # device (read-only apart from insmod/rmmod of our own module)
    scp -O /tmp/rw-ko/ringwatch.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/ringwatch.ko; \
        cat /sys/kernel/debug/ringwatch/regions; \
        echo 1 > /sys/kernel/debug/ringwatch/sample; \
        iwpriv Hisilicon0 alg get_2g_power_param; \
        echo 1 > /sys/kernel/debug/ringwatch/sample; \
        cat /sys/kernel/debug/ringwatch/log; rmmod ringwatch'

    # analysis (any Python 3)
    cd build/register-dumps && python3 ringwatch_analyze.py
