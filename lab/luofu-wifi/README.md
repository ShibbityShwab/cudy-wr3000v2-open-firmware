# luofu-wifi

The **Wi-Fi service skeleton** for the Hi5671Y "luofu" endpoint (Cudy WR3000
v2.0, HiSilicon luofu / OpenWrt 22.03.6, kernel 5.10.201) - the first piece of
*our own* `wifidrv` code that states the device's service as a structure
instead of a one-shot probe.

The arc (phase20 -> phase49) localized the whole path end to end; the port
never wrote it down as a driver. This module maps the windows the arc named,
runs the announce/handshake state machine, decodes the glue/mailbox and the
SR/DR ring bookkeeping, and reports the take-related status - **read-only,
zero MMIO stores**.

## What it does

`hw=1` claims EP0 (`pci_enable_device`, `pci_request_mem_regions` - it REFUSES
rather than fight if the vendor stack owns the BARs) and maps four pages of the
endpoint's region-3 inbound window (docs/phase18/inbound-map.md row 3: host
`0x403b8000` -> dev CA `0x40000000`, so CA `0x400XXXXX` sits at BAR0
`0x3b8000 + (CA - 0x40000000)`). Every read goes through `lw_read()`, the
aligned-dword accessor: one 4-byte-aligned `readl()` of the dword that contains
the field, then shift/mask. The window answers only 4-byte-aligned 32-bit
accesses (a 2-mod-4 read external-aborts whatever its width - `rcfix.md`,
`pciskel-smoke.md`).

| window | BAR0 | device CAs | what the skeleton reads |
| --- | --- | --- | --- |
| handshake | `0x3b8000` | `0x40000108` / `0x4000010c` | the release word and the park gate |
| glue/mailbox | `0x3f1000` | `0x40039010`..`0x40039aec` | out[0]/out[1], the doorbell, raw/mask/status on copy A **and the twin copy B** |
| rings | `0x3f2000` | `0x4003a400`..`0x4003a6bc` | SR ch0-2 and DR ch0-3 program registers |
| D2H | `0x4b9000` | `0x40101414`/`34`/`38` | the device-local D2H assert and the msg-map pair |

## The three decodes

**1. The announce/handshake state machine** (`lw_decode_announce()`,
`h2d2.md` sec 3). The device's ETE bring-up zeroes both low halves of CA
`0x40000108`/`0x4000010c`, posts `out[1] = 4` (announce id 2), rings the
device-local D2H assert (`0x40101434`), and **parks** on
`*(CA 0x4000010c) == 0x0000cece`; the host completes the handshake (writing
`0xcece`), the release runs and the CPU mask can lift. The decode reports
`IDLE` (gate 0), `PARKED` (gate is something else) or `RELEASED`
(gate == `0xcece`), and expands `out[1]` into the posted message ids.

**2. The glue/mailbox** (`lw_decode_mailbox()`, `twin.md`, `h2d2.md` sec 1).
`out[0]` is the H2D pending bitmap the host publishes; the dispatcher's own
gate is "out[0] != 0" and it CONSUMES by zeroing `out[0]`. `0x400392d4` is the
self-clearing HOST2DEVICE_INTR_SET doorbell, `0x400392e4`/`ec` the RAW and
post-mask status, and byte `0x400392e8` the mask (0 = unmask). Bit 3 is
`device2host_rx_intr` - the D2H-RX source; bit 4 is `device2host_intr`. The twin
copy B (`0x40039800`, the same file at +0x800) is read too: its masked level is
the discriminator (a copy-A ring with a still twin is device-originated).

**3. The rings and the credit bookkeeping** (`lw_decode_rings()`,
`credit2.md`). SR ch0-2 at `+{0x400,0x450,0x4a0}` and DR ch0-3 at
`+{0x590,0x5e0,0x630,0x680}` - a **0x50 stride**, the register blocks the
vendor's own `.rodata` table names (ko file `0x20e5c`), re-derived here from
`pcie_ete_{sr,dr}_reg_init` (`0x14a48`/`0x1483c`). Per channel: DR+`0x30` the
node-array device VA, DR+`0x34[9:0]` depth-1, DR+`0x38` the committed producer
index (the host's GRANT), DR+`0x3c` the device's 16-bit consumer index; SR+`0x08`
ctrl, +`0x10` base, +`0x14` depth, +`0x18` wptr, +`0x1c` rptr. Credit =
`(producer - consumer) mod (2*depth)`, phase-aware on bit 10.

> **Stride correction (load-bearing).** `lab/wifidrv1/wifidrv1.c` indexes these
> channels with `0x114`/`0x6c`; `credit2.md` sec 5 retracts exactly that ("those
> are host *struct* strides"). The register blocks are `0x50` apart - the
> `.rodata` table at ko file `0x20e5c` lists `0x400,0x450,0x4a0` / `0x590,0x5e0,
> 0x630,0x680`. This skeleton uses the register stride; a reader comparing the
> two files is looking at the corrected value here.

**The DR completion test** (`lw_dr_hdr_ok()`): `pcie_ete_rcv_buff_check`
(`0x14d74`) accepts a slot iff the *buffer's* 16-bit field at `+0x0a` reads
`0x5a5a` and its 16-bit length at `+0x04` is non-zero. That buffer is host DRAM
the vendor owns, so the predicate is carried as code and only ever applied to a
node array that falls **inside the mapped ETE ring page** (a range guard, never
an arbitrary device read - the misc-window panic is the precedent).

**4. The take status** (`lw_sample_take()`, `realchain.md` sec 5). The take is
the device CPU accepting SPI `0x4c`; its host-side witnesses are `out[0] -> 0`
(the dispatcher's own consume store) and a rising copy-A raw/status bit 3 (the
device rang D2H). **The module stores nothing, so any rise it observes across
its bounded sample window is device-originated by construction** - the arc's
decision rule with its "host bit-3 store" leg removed. The device-side per-entry
id sensor (`V2_ID`) is firmware instrumentation and is deliberately out of
scope. The ack IAR `0x4016010c` / AIAR `0x40160120` are **never read**; the
compile-time `static_assert`s prove neither lies inside a mapped window.

## The read-only pledge

* No MMIO store token exists in the source: no `iowrite*`, no `write[bwlq]`, no
  `memcpy_toio`/`memset_io`; `readl()` is the module's only MMIO op, and the
  register tables hold offsets only, so a store cannot be expressed through them.
* CA `0x400392f0` (the W1C HOST_INTR_CLR) is not even mapped, and neither is the
  RC misc window `0x10161000`.
* The only non-MMIO side effect is the kernel's own `pci_enable_device()`, which
  sets the endpoint's command register so a memory BAR can be claimed - the same
  call `lab/eteprobe` and `lab/wifidrv1` make.

## Parameters

| param | default | meaning |
| --- | --- | --- |
| `hw` | 0 | 0 = registration/ABI only (no PCI access); 1 = claim + read-only decode |
| `domain` | 0 | the RC domain of the `59e7:0005` endpoint to bind |
| `samples` | 1 | bounded take-status samples (1..64) |
| `delay_ms` | 20 | gap between samples (0..1000) |

## Build / smoke

Compiles in the CI cross-build against the vanilla 5.10.201 arm headers via
`.github/workflows/lab-module-build.yml` (matrix target `lab/luofu-wifi`,
triggered on `omo/**`) and the `build-load-test-module.yml` lane; the resulting
`luofu-wifi.ko` is uploaded as the `luofu-wifi-ko` artifact.

Smoke on the router: `insmod luofu-wifi.ko` (default `hw=0`) proves
vermagic/ABI only - no probe, no BAR claim. With `hw=1 domain=0` the claim is
read-mostly and refuses on conflict; the router's 2 wiphys / 6 interfaces /
calibration are untouched because nothing is written.

## Staged next (the rung toward the real driver)

The skeleton deliberately stops at the observation side. The next rung, in
dependency order (`stage2.md` sec 2, "5 -> 6"):

1. **The glue**: register the `pci_driver`'s INTx virq and an ISR that dispatches
   on the glue status mask `0x3d8`, with the mandatory in-ISR bound
   (`disable_irq_nosync` after a small K, never re-enabled after a trip).
2. **The service**: the H2D send (`out[0] |= 1<<id`, then ring the doorbell
   `0x400392d4` bit 0) and the D2H ack/consume path (`out[1]` -> ack
   `0x40101438` -> clear `out[1]` -> re-arm `0x40101414`).
3. **The announce**: complete the `0xcece` handshake in the bring-up order
   (id 3 -> 6 -> 5 -> 2), which is the precondition for the take at all.
4. **The rings**: allocate the SR/DR node arrays, program `DR+0x30/0x34` and the
   committed producer index, and drive `pcie_msg_send(5)`'s reclaim loop.
   Only then does the `0x5a5a` header test above start returning real deposits.
