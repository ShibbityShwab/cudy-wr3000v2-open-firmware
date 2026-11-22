# transport-delta - the marked transport-module change the driver needs

**Lane:** wifi-driver-integration (task-64), 2026-11-22. **Original text; no
vendor source or object code.** The delta below is the *only* change the
hdpp-style driver requires in the wifi-transport-port module. It is delivered
as a patch + two new files so the transport lane keeps ownership of its code:
nothing in this directory modifies the repository's transport sources.

## Why a delta is needed at all

The transport skeleton (`../wifi-transport-port/build/`) has no exported
symbols, owns the PCI device, all BAR mappings and every device write, and its
D2H path is a boot-only polled scan with internal handlers.  A driver that
lives in a *separate* module therefore needs three things the skeleton does not
provide:

1. a stable client/ops contract instead of raw `struct hi5622_pci` poking;
2. the phase37 1.2 D2H service routine (ack / read+clear / re-arm) and
   dispatch of the bitmap ids to the driver's handlers - currently the
   transport only reads, drains and dispatches its own boot handlers;
3. a way to hand completed DR-ring payloads (HCC frames) to the driver's HCC
   demux instead of logging them.

The delta adds exactly that, as **new file `pciet_iface.c` (+ the shared
header `hi5622_transport_iface.h`)** and **6 small marked hunks** in the
existing `pciet_main.c` / `Kbuild`.  Exports alone would not be enough
because the D2H service order and the RX fan-out live inside the scan/handler
code paths.

## Baseline

Verified against the submodule tree at commit `9700250` ("wifi-transport:
harden D2H poll (2s initial settle); B2 bench results recorded"):

| file | sha256 | touched by the patch |
| --- | --- | --- |
| `build/pciet_main.c` | `bf036931710fb8489b00353b21c765dbfe989ad31dde09e1c5e5ef93023f43fa` | yes (5 hunks) |
| `build/Kbuild` | `2c2b4815cb361de66610ebf209e54cea3260dccfbfb331a765bf486dabb41379` | yes (1 hunk) |
| `build/hi5622_pciet.h` | not touched | no |
| `build/pciet_ete.c` | not touched | no |

The patch applies with `git apply -p1` from the transport build directory;
if the transport lane changes `pciet_main.c` later, re-check the sha above
first (hunks 4/5 currently apply with a small line offset, which git accepts).

## The 6 marked hunks (all tagged `[driver-port integration delta]`)

| # | file / function | change |
| --- | --- | --- |
| 1 | `pciet_main.c` includes | `#include "hi5622_transport_iface.h"` |
| 2 | `pciet_d2h_payload()` | offer each DR payload to `pciet_iface_deliver_rx()` first; skip the legacy wire-log/hexdump when a driver consumed it |
| 3 | `pciet_d2h_scan()` | `if (pciet_iface_d2h_client_scan(ctx)) return;` - when a driver is attached **and** `driver_mode=1`, the iface performs ack/clear/re-arm + dispatch |
| 4 | `pciet_probe()` | `pciet_iface_transport_added(ctx)` publishes the endpoint to registered clients |
| 5 | `pciet_remove()` | `pciet_iface_transport_removed(ctx)` withdraws it first and waits for in-flight driver callbacks |
| 6 | `Kbuild` | `hi5622_pciet-y += pciet_iface.o` |

The two new files are copied into the transport build directory by
`../tools/build-modules.sh` from `../interface/` (so the transport lane's
tracked files stay untouched in the repo).  The patch itself only contains the
6 hunks; the header comment lists this contract.

## What the delta exports / what the driver binds to

Exported from `hi5622_pciet.ko` (`EXPORT_SYMBOL_GPL`, verified with
`nm -g --defined-only`):

* `hi5622_transport_register_client(struct hi5622_transport_client *)`
* `hi5622_transport_unregister_client(struct hi5622_transport_client *)`

The client struct/ops table (full documentation in
`../interface/hi5622_transport_iface.h`) covers:

* HCC H2D send: `ops->h2d_send()` = SR0 node + **mailbox announce id 3**
  (phase37 2.1).  `ops->h2d_kick()` accepts id 3 only - H2D id 5 is a
  firmware BUG() trap (phase37 1.1 step 3 [V]).
* D2H dispatch table: `ops->d2h_register(t, id 0..10, fn, arg)`; the iface
  snapshots the transport's own boot handlers at attach and runs them first,
  then the driver handlers. Dispatch order: bitmap ids lowest-first.
* TX/RX ETE ring service: `ops->ete_init()` (SR0 group A + DR0 group B
  register programming; bench-gated), `ops->ete_tx()`,
  `ops->ete_rx_service()`, `ops->ete_depth()/ete_buf_len()`.
* Firmware-ready/state: `ops->get_state()`, `ops->wait_ready()`,
  `ops->d2h_count()`, plus the client `fw_state()` callback and a
  transport handle with `name`, `dev`, `id`.
* Client `rx()` callback for each completed DR payload (HCC frame, 12-byte
  header + payload; valid only for the call duration).
* Lifecycle callbacks `attach()/detach()` (process context); `fw_state()`,
  `rx()` and D2H handlers may run in atomic context.

## Safety gate: `driver_mode`

The iface adds a transport module parameter:

```
/sys/module/hi5622_pciet/parameters/driver_mode    (bool, default 0)
```

With `driver_mode=0` the transport keeps its original write behaviour: the
D2H path is the legacy read/drain/dispatch, and DR payloads keep the legacy
logging.  An attached driver only observes (state, diagnostics).  With
`driver_mode=1` the phase37 1.2 service routine takes over, which adds exactly
two device writes per non-empty D2H bitmap event:

* ack = 1 -> CA 0x40101438 (out[3], BAR0+0x4b9438)
* re-arm = 1 -> CA 0x40101414 (out[4], BAR0+0x4b9414)

These are the phase37 1.2 contract words; the never-write register
CA 0x400392f0 (out[5]) is untouched and is asserted against at build time in
`pciet_iface.c`.  Enabling `driver_mode=1` is deliberately an **attended
bench step** (see ../REPORT.md section 10), so loading the driver by mistake
cannot change the transport's write footprint.

## Review notes for the transport lane

1. The delta does not move ownership: BAR ioremap, DMA ring allocation, the
   never-write gate and the sysfs attended steps all stay in the transport.
2. `pciet_iface.c` writes through `ctx->bar0` directly (the transport's
   `pciet_writel_checked()` is static); the two write offsets are compile-time
   checked against `HI5622_CA_NEVER_WRITE`.  Please keep the helper visible if
   you prefer a single write path - that is a cosmetic refactor, not a
   requirement.
3. With no client registered (or `driver_mode=0`) the patched transport is
   behaviourally identical to the unpatched one; the added hooks fast-return.
4. The iface keeps a snapshot of `ctx->d2h[i].func` taken at probe.  If the
   transport starts registering more boot handlers after probe, tell us and we
   will refresh the snapshot at attach instead.

## Applying / testing

```sh
# from the repository root (opensource submodule):
cd docs/incoming/wifi-transport-port/build
cp ../../wifi-driver-integration/interface/{hi5622_transport_iface.h,pciet_iface.c} .
git apply -p1 ../../wifi-driver-integration/transport-delta/0001-pciet-integration-iface.patch
# ... or simply run ../../wifi-driver-integration/tools/build-modules.sh,
# which does the copy + apply + 3-module build in an out-of-tree workdir.
```
## Required transport changes before B3 (documented, deliberately NOT in this delta)

The driver skeleton attaches today, but a real D2H data exchange (B3) needs
these transport-side corrections.  They are **transport-lane work**, found
while reconciling this lane's interface with the hcc-re contract
(`../wifi-hcc-re/REPORT.md` sections 4/9, task-63, submodule commit
07b9201); this lane does not patch transport ring logic (the transport owner
must, and the B2 module remains the validated baseline):

1. **DR consume must advance the host free-buffer index `DR+0x38`, not write
   `+0x3c`.**  `pciet_ete_dr_consume()` currently writes
   `HI5622_ETE_CH_B_CNS` (+0x3c), which is the device's service/fill index
   (hcc-re 4.2 [V]).  The host publishes reclaimed buffers at **+0x38** after
   dispatching each completed node.
2. **Index encoding is `(idx & 0x3ff) | (phase << 10)`** with the phase bit
   toggling when the low 10 bits wrap at depth (vendor `pcie_ete_ring_ptr_plus`
   @0x13ef8, hcc-re 4.2 [V]).  The current SR post writes the raw producer+1
   and masks `& 0x1f`; the DR consumer reads/writes a raw index.  B2's H2D
   smoke did not depend on phase wrap, but any multi-wrap transfer will.
3. **Arm all seven channels (P0):** SR0-2 + DR0-3, depth 32, pre-post DR node
   buffers (word0 = payload buffer device VA) and publish `DR+0x38`.  The probe
   currently allocates/programs only SR0 and DR0.  hcc-re flags=1 marks SR0-2 +
   DR0 as the control/HCC channels and DR1-3 as data [I]; B3 P2 pins which DR
   channel carries chip-group messages.
4. **SR ctrl `+0x08 = 0`** is part of the vendor SR init
   (`pcie_ete_sr_reg_init` @0x14ae0, hcc-re 4.2 [V]); `pciet_ete_ring_init()`
   does not write it today.  The channel `+0x00` enable is set by the firmware
   itself, not the host (transport already leaves it alone - keep it that way).
5. **ETE interrupt/mask block CA 0x40039508**: if the optional IRQ path is
   ever used, mask it with `&= 0xffe0f8f8` (hcc-re 4.4 [V]); the polled path
   needs nothing here.
6. **H2D doorbell write form [U]:** phase37/hcc-re describe the announce as
   `out[0] = 1<<3; out[2] |= 1` while the current transport writes the
   pending word to the doorbell and B2 measured mask 8 working.  Keep the B2
   form; capture `out[0]/out[2]` around the B3 first frame to arbitrate.

The iface ops table deliberately exposes only the SR0/DR0 service that exists
today (`ete_init/ete_tx/ete_rx_service`); when (1)-(4) land, extend the ops
with per-channel accessors or fold the full arming into `ete_init` and bump the
interface revision.

