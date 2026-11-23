# SPEC - host counterpart for the Hi5622V100 HCC link/event bring-up

**Lane:** wifi-hcc-link (task-75). **Status:** compile-ready, bench-unvalidated.
**Companion evidence:** REPORT.md (negative proof and firmware cross-reference).

---

## 1. Scope and the finding this spec encodes

The lane asked for the host-side HCC link/event bring-up handshake. The static
reversal answer (REPORT.md sec 2-5) is: **there is no such H2D handshake in the
vendor host.** Both vendor module versions' complete H2D sender census shows only
runtime config/mgmt/data-path messages; the host HCC framework (link/event/queue
objects) is host-RAM-local; init paths send nothing except a post-DEVICE-READY
TX-queue config sync. On the firmware side, no H2D receive handler reaches the
event dispatcher, its per-link callbacks, or the link-create worker; those are
firmware-internal boot code.

This spec therefore specifies the **honest host counterpart**: a bounded
observability + consumer-liveness ladder that (a) reads the exact firmware
objects involved in the S10 abort, (b) measures whether the firmware can consume
H2D traffic at all, and (c) classifies the link/event state and the next lane. It
deliberately contains **no fabricated link-create frame, no device-RAM write and
no payload-carrying speculative frame.**

Non-goals: replicating the vendor driver's data path, sending config messages
(their payload structs are not reconstructed), or "repairing" firmware RAM.

---

## 2. Interfaces (sysfs attributes on the PCI device)

The extension registers its own attribute group from
`pciet_hcc_link_probe()` (same pattern as the h2d injector). All attributes are
on the endpoint's device directory (`/sys/bus/pci/devices/<BDF>/`).

### 2.1 `hcc_link_status` (read-only)

Prints one snapshot, all reads, no writes:

| block | fields |
| --- | --- |
| boot | `fw_loaded cpu_released gate_announced windows_programmed` |
| mailbox | `out[0] out[1] out[2] ack(out3) rearm(out4)` |
| mailbox (read-only checkpoints) | `out[5]` (never written), `d2h_doorbell`, `ete_intr` |
| firmware boot words | `exc_mask` (CPU 0x40008), `state` (0x118B60), `boot_slot` (0x17D42C), `g_ptr` (0x172130) |
| g object (when plausible) | `g+0x9C` (out[1] pointer), `g+0xB4` (armed), `g+0xB8` (pending), `g+0x2CC` |
| HCC instance (0x10C190) | `+0x00 +0x04 +0x190 +0x2B8 +0x2CC +0x4B0` |
| registry core-0 record (0x171FB4) | 8 words |
| per-link +0x4B0 words | 4 x (`+0x4B0`, `+0x4B4`) at 0x172020/0x1722CC/0x172404/0x172458 |
| ETE | SR0-2 consumer, DR0-3 fill |
| wake probe state | attempts/consumed/timeouts + last probe details |
| VERDICT | `g=valid/invalid registry=populated/unexpected link_ctx=n/4 SET/UNSET h2d_consumer=ALIVE/DEAD/UNKNOWN` + next-step line |

Classification rules (deliberately loose, printed raw values are authoritative):
* `g` valid: `*(0x172130)` in `[0x40000, 0x198000)` (readable CPU window);
* registry populated: record`[1]` in `[0x80000,0x120000)` **and** record`[4]` in
  `[0x170000,0x173000)` (live cold-boot shape from boot-kick sec 18.2);
* `link_ctx` SET when all four `+0x4B0` words are nonzero;
* `h2d_consumer` from the last `hcc_link_wake` result (ALIVE only if the id-1 bit
  was cleared); UNKNOWN before the first wake.

If the iATU windows are not programmed the attribute reports the boot block with
`windows=0` and says "prepare first" (region-5/region-3 reads are only
meaningful after `prepare`).

### 2.2 `hcc_link_wake` (write "1")

One bounded, vendor-attested, payload-free H2D consumer-liveness probe.

Preconditions (each refusal is logged, no write happens):
1. `windows_programmed` = 1 (endpoint iATU viewports programmed);
2. `cpu_released` = 1 (a live firmware; the vendor posts this wake only against
   a running device);
3. `out[0]` reads 0 (nothing else in flight; the probe will not clear a foreign
   pending word);
4. fewer than 3 attempts this module load.

Sequence (all writes through the transport's single gated writer):
1. `out[0] = BIT(1)` (CA 0x40039010) - H2D mailbox id 1, the payload-free global
   message wake (firmware id-1 handler just posts its own message object);
2. `out[2] |= 1` (CA 0x400392D4) - doorbell, vendor `pcie_msg_send` order;
3. poll `out[0]` up to 100 ms (2000 x 50 us) for bit 1 to clear;
4. read-only checkpoints: `out[2]` (the device dispatcher writes 8 on a real
   consume), `out[5]` (device ack; **never written**), `out[1]` (D2H bitmap);
5. record/print; return `count` on consume, `-ETIMEDOUT` on timeout.

A "consumed" result therefore means: the firmware H2D dispatch path is executing;
a timeout means it is parked before/inside its message layer. That decision is
the point of this attribute.

### 2.3 `hcc_link_plan` (read-only)

The attended ladder (L0-L3) and the stop rules as text, so the operator does not
need the paper document at the bench.

---

## 3. Address/bounds contract (must not change without new evidence)

| item | rule | source |
| --- | --- | --- |
| CPU-RAM window | `BAR0 = CPU + 0x6B8000`, readable CPU < `0x198000` (viewport 5: BAR0 0x6B8000..0x8CFFFF) | abort-rca sec 0; transport region table |
| chip-IO window | `BAR0 = 0x3B8000 + (CA & 0xFFFFF)` | transport header |
| out[5] | CA `0x400392F0`: **read-only**; build asserts prevent any write | project hard rule |
| mailbox ids | only id 1 is posted (BIT(1)); id 3 is the h2d injector's announce; **id 5 is never posted** | hcc-re/h2d-catalog |
| ETE | SR0-2 + DR0-3 read-only in status; firmware-owned enable/ctrl/fill/consumer never written | transport/B3 contract |
| RC misc window | `0x10161000` never touched | project hard rule |

## 4. Integration

* New files: `files/pciet_hcc_link.c`, `files/pciet_hcc_link.h`.
* Patch `patches/0002-hi5622-pciet-hcc-link-b3.patch` (a **0002 add-on**, applies
  after wifi-h2d-inject's `0001` on `../wifi-transport-port/b3`):
  * `hi5622_pciet.h`: add the probe/remove prototypes;
  * `pciet_main.c`: call `pciet_hcc_link_probe(ctx)` after the h2d probe in
    probe(), and `pciet_hcc_link_remove(ctx)` in remove();
  * `Kbuild`: add `pciet_hcc_link.o`.
* Rebuild with `tools/build.sh` (applies 0001 + 0002 on a copy of the B3 tree and
  compiles against the existing 6.18.60 kernel tree; no image build).
* Module-level preconditions: same as B3 - `insmod hi5622_pciet_b3.ko poll_ms=0`
  for the P0/P1-style attended probes (the base poll thread would race the D2H
  contract).

## 5. Decision table after the ladder

| L0/L2 observation | L1 wake | meaning | next lane |
| --- | --- | --- | --- |
| any | **not consumed** | firmware parked before/inside its H2D message layer; no H2D frame can be the missing input | firmware-internal link/event bring-up (REPORT sec 7 lane 1) |
| `link_ctx=0/4` | consumed | H2D consumer alive but per-link contexts never bound; the missing stage is firmware-internal | same as above; do **not** fabricate RAM or payload frames |
| `link_ctx=4/4` | consumed | per-link/event contexts exist; host/event layer is live | driver-replica lane (post-ready H2D config events) |
| `registry=unexpected` | any | boot chain did not reach the same state as the recorded cold boot; stop and re-baseline | boot procedure re-run; no H2D experiment |

## 6. Acceptance criteria for the first bench run

Success = a complete `hcc_link_status` snapshot before and after one
`hcc_link_wake`, with a unambiguous verdict line and no anomaly:
* no staged-abort frame change beyond the recorded `mask`,
* no new `out[1]` bits beyond the recorded platform-up announce,
* no kernel oops, no device hang; `out[0]` ends 0 (consumed) or unchanged
  (timeout) and the run stops either way.

Failure (stop rules): any of the above anomalies -> power-cycle; do not retry in
the session; report the last `hcc_link_status` snapshot and the dmesg line.

## 7. Open items carried by this spec

* The boot-region pointer table at CPU `0x474B8` (holding `0x46849`) has no
  located consumer yet - REPORT sec 5.4/7 lane 1.
* The four per-link `+0x4B0` words are read but their setter for the *event
  dispatcher ctx* (vs the 5 static store sites) is not identified.
* `out[5]` readback semantics (device ack) remain [U]; the probe records it but
  does not depend on it.

