# wifi-rx-h2d-window - why the first stock H2D trace saw no ring, and the corrected retry

**Lane:** `rx-h2d-window` (shared task **task-194**, the retry of **task-176**'s stock H2D trace).
**Date:** 2026-11-28. **Scope:** host-only static decode of committed artifacts; **no device contact, no
fire, no vendor binary/firmware byte committed** (addresses, sizes, hashes and decoded text only).

## One-line result

The first trace's `ring not seen within 120 s` was **not a window and not a timing problem**:
the v1 sampler reads the ETE registers with `pread()` on `/dev/mem`, and on this 32-bit ARM
kernel **`read()` of any physical address below RAM start is rejected by
`valid_phys_addr_range()`** (`arch/arm/mm/mmap.c`) with **-EFAULT** - an error the sampler
silently ignores - so **no `-w` value in the whole PCIe window range can ever return a
register**. The retry frame is also corrected on the stock's own terms: the vendor's H2D/ETE
path is **EP1**, whose region-3 window is **`0x583b8000`** and whose ETE window is
**`0x583f2000`** (the `0x403f2000` assumption is EP0 - *our* PD4 image's endpoint; both windows
alias the same on-chip registers). Timing is settled from the stock UART log: the SR ring is
programmed **during plat.ko's probe, ~14.18 s after boot**, while the S08 aid starts
**~9.8-10.5 s** - a `-W` of 120 s already has ~115 s of margin; `-W 180` is belt-and-braces.
The retry therefore needs a **v2 sampler with mmap() register access** (delivered here) plus
`-w 0x583f2000`; the RUNBOOK delta is below.

## Deliverables

| file | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: why v1 failed (root cause), the stock's own window (EP1), the boot-handshake/ring timing from the UART log, honest limits |
| [TABLES-window.md](TABLES-window.md) | endpoint/window/register address tables (EP0 vs EP1), the read()-vs-mmap() rules, the exact probe commands |
| [RUNBOOK-DELTA.md](RUNBOOK-DELTA.md) | the exact delta to `docs/incoming/wifi-rx-h2d-trace/RUNBOOK.md` (new T2.5 window gate, T4 arguments, PASS/FAIL, STOP rules) |
| [tools/omo_h2dsampler-mmap.c](tools/omo_h2dsampler-mmap.c) | v2 sampler: mmap() for the ETE window, pread() kept for DRAM nodes, ring-register validation, `-p` read-only probe mode, error logging |
| [tools/build-sampler-mmap.sh](tools/build-sampler-mmap.sh) | cross-build of v2 (static ARM/musl, same toolchain as v1) |
| [evidence/](evidence/) | host build/test output, the UART-log timeline extraction, the kernel-source citations, the v1-v2 diff |

## Status (what is proven vs. inferred)

* **[V] the failure mode**: the v1 code path (`pread` on `/dev/mem`), the ARM rejection rule
  (`addr < PHYS_OFFSET` -> `-EFAULT`) and the observed sampler log
  (`module_loaded=1` + `ring-not-seen wait_sec=120` at `ts_ms=125851`) all agree; no register
  was ever read. Per-boot PHYS_OFFSET = `0x80600000` is printed by the stock kernel itself
  (`Ignoring memory range 0x80500000 - 0x80600000`).
* **[V] the stock's own window**: EP1 = `0001:00:00.0` = `chip->dev[0]` (phy_devid 0),
  BAR0 `0x58000000`, region-3 host base `0x583b8000` (committed live-vendor dumps in
  `phase21/sibling-ep.md`, `phase32/doorbell-endpoint-path.md`); ETE window `0x583f2000`.
  EP0 = `0000:00:00.0` region-3 `0x403b8000` -> `0x403f2000` (the v1 assumption / our port).
  Both windows alias the same on-chip registers, including SR ch0 base [V, phase21 A.4].
* **[V] the timing**: the UART log of the capture boot (the last stock boot of
  `build/uart-logs/20261125T113518Z-uart-h2d-stock.log`) timestamps the whole bring-up; the
  vendor disassembly shows the SR ring registers are written by `pcie_main_init -> pcie_ete_init
  -> pcie_ete_sr_init -> pcie_ete_sr_reg_init` during probe (14.10-14.21 s), i.e. **before**
  the firmware download (14.23-14.54 s), and `module_loaded` only becomes visible after that
  same probe, so the v1 wait loop would have detected a live ring on its first poll.
* **[I] the on-device confirmation**: the v1 `pread` returning `-EFAULT` is inferred from the
  kernel code path + the symptom; the v2 `-p` probe mode makes it direct on the retry
  (`pread_control=EFAULT`), which is the first line of the retry evidence.

## The retry in one paragraph

Build v2 (`tools/build-sampler-mmap.sh`), install it in the same payload slot as v1
(`/usr/bin/omo_h2dsampler`, same aid), and on the stock root shell **before** the capture boot
run the read-only gate `/usr/bin/omo_h2dsampler -p -w 0x583f2000` - it must print a non-zero
SR0 base in DRAM (`0x8xxxxxxx`), depth-1 `0x1f`, and `pread_control=EFAULT` (the v1 defect,
now measured). The capture aid then runs `-w 0x583f2000 -W 180 -t 240`; `-w 0x403f2000` is
the documented equivalent (same on-chip register, EP0's window). PASS is unchanged from task-176:
a valid `M`-line list decoded by `tools/decode_h2d.py`; this lane changes the instrument, not
the deliverable.
