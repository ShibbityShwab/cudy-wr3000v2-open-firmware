# wifi-rx-woc-class - the vendor RX buffer memory class (task-167)

**Lane:** `wifi-rx-woc-class`, shared task **task-167** (host-only; candidate 1 of the final
RX-capture hunt). **Status:** closeout report, written after the lane's artifacts were committed
and the bench fires had run. **Scope:** host-side decode + a fail-closed driver option + the fire
record; the Lead fired, this lane never contacted the bench.

**Result in one line:** the vendor's RX buffers are **not** a reserved/carved-out memory class -
they are ordinary cacheable skb slab memory with one held streaming `DMA_FROM_DEVICE` mapping each
(the "WOC" allocator); the parameterised reproduction of that class (`rx_buf_class=1`) **loads and
runs on the bench but does not change the RX outcome** (status goes to the sticky `0x582` bit 10,
`frames=0`). The buffer memory class is therefore **not** the fix.

This lane's committed artifacts are the portable model (`src/hi5622_rx_woc.{h,c}`) and two kernel
patches (`patches/0001-rx-woc-class.patch`, `patches/0002-rx-woc-class-stacked-on-rx-format.patch`).
The lane died before writing this report and before committing a build script/test harness; the
Lead built the module from the lane's (then untracked) patches and fired it. The honest gaps are
listed in section 8.

---

## 1. The question this lane was given

By the 2026-11-25/26 RX-hunt state the host-side RX static aspects were already converging on
"vendor-correct": the MAC ring/buffer format decoded (rx-format lane), the CRX event path decoded
with no missing host enable (rx-crx-gating lane), the vendor's 12-write/free-ring program
replicated byte-for-byte (rx-vendor-diff lane), and the device-owned live-bank state reading the
vendor stock mirror (R0 parity). Yet the MAC still never wrote a frame into host memory
(`frames=0`) and the receive path showed the bit-10 status class.

The remaining suspicion this lane owns: **the vendor allocates its RX buffers from a dedicated
("WOC") allocator inside `hi_kpie.ko`, not from `dma_alloc_coherent()` pools**; the RX write
master may require that memory class (or the vendor's reserved-DRAM carve-out hinted at by the
`0xC0000004 + 0x600000` host-CA bias). Task: decode the allocator, reproduce the class behind a
fail-closed parameter, and fire the existing R2 observe-only capture with the new class.

The lane brief's working hypothesis was a *reserved DRAM carve-out* with special attributes. **The
decode refuted the carve-out** (section 2.4): the class is "cacheable slab + streaming map", and
`0x600000` is simply the vendor kernel's `PHYS_OFFSET` (0x80600000) relative to the DDR base
(0x80000000). The experiment was kept anyway because the executable question - "does the RX write
master need this class?" - is answered only on the bench, not by the decode.

## 2. The decode

All facts below were derived by the lane from the local vendor host stack (the full module set at
`/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/`: `hi_kpie.ko` and
`hi5622v100_wifi.ko`; local-only artifacts, never committed). Addresses are module-relative;
"kpie" = `hi_kpie.ko`, "wifi" = `hi5622v100_wifi.ko`.

| # | fact | evidence |
| ---: | --- | --- |
| 1 | `hi_net_woc_alloc_skb` (kpie `0x73a8`): `__netdev_alloc_skb(NULL, buflen + 3, GFP_ATOMIC = 0xa20)`, then aligns `skb->data` to 4, then maps the payload once with `dma_map_page_attrs(dev, page(data), offset, buflen, DMA_FROM_DEVICE, attrs = 0)`; the returned DMA address is the direct-map CPU physical address | [V] |
| 2 | `hi_net_woc_skb_free` (kpie `0x6aec`): does **not** unmap the DMA mapping; the buffer is recycled through WOC per-CPU lists (the mapping is held for the buffer's lifetime) | [V] |
| 3 | `shuangta_host_rx_add_buff` (wifi `0x374d0`), the vendor's per-frame RX publication: pre-write `*data = skb` (the host-owned word at buffer+0), **zero the 36-byte device header**, cache-maintain the header span (map `TO_DEVICE` then `FROM_DEVICE`), then publish `data+4` through `pcie_if_hostca_to_devva` (identity window) | [V] |
| 4 | the `0x600000` in the vendor RX bias is the vendor kernel `PHYS_OFFSET` (`0x80600000` - DDR base `0x80000000`), i.e. a plain kernel-VA -> CPU-physical conversion, **not** a reserved carve-out; no vendor RX reserved region exists in the decode | [V]/[I] |
| 5 | therefore the class difference vs our driver is: coherent pool memory (our default) vs **cacheable skb slab memory + a held streaming `DMA_FROM_DEVICE` mapping per buffer + explicit per-publish cache maintenance** (the vendor class) | [V]/[I] |

The first two facts are the allocator; fact 3 is the publication contract the earlier rx-format
lane had already reproduced functionally (host word + zeroed 36-byte header + ring entry =
device VA of `buffer+4`); fact 4 closes the carve-out question.

### 2.1 Why this could have mattered

A cacheable (non-coherent) buffer class means the device's DMA writes are visible to the CPU only
through correct ownership handover: the host must flush/invalidate the relevant lines around the
publication and the consume. The vendor does this per publication (fact 3). A receiver that
requires the vendor's write-master attributes (or a specific memory aperture) would fail against
coherent pool memory even though every register is right. This lane made that difference
reproducible and measurable.

## 3. The committed model (`src/hi5622_rx_woc.{h,c}`)

The lane left a device-free, kernel-header-free C model so the arithmetic and the fail-closed
decisions can be pinned on any host. It is original code; every constant is a derived fact.

| item | value / behaviour |
| --- | --- |
| buffer classes | `RXWOC_CLASS_COHERENT = 0` (default), `RXWOC_CLASS_WOC = 1`; any other requested value is refused and latched to the safe value |
| geometry | host word 4 B at buffer+0, 36-byte device header at buffer+4 (`0x24`), normal device-writable `0x63c` (stride `0x640`), small `0xb0` (stride `0xb4`) |
| allocation size | `size + 8` bytes (alignment margin for the up-to-3-byte 4-align shift the vendor does) |
| alignment | `skb->data` aligned to 4 (vendor `add_buff` rule) |
| device window | DMA address must be inside `0x80000000..0x8fffffff` (the identity host-CA window the device can reach) |
| ring entry | `dma + 4` (device VA of the 36-byte header) |
| per-publish preparation | write the host bookkeeping word (`0x574f4301`, "WOC"+class tag, host-only), zero the 36-byte header; everything from buffer+0x28 on is device-owned |
| cache span | one `DMA_BIDIRECTIONAL` sync over the allocation (the single-call equivalent of the vendor's flush-then-invalidate around `add_buff`) |
| witness | read-only check that the host word survived and whether the device already wrote the header |

The model compiles clean as a standalone translation unit:

```
$ cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -c src/hi5622_rx_woc.c
(no output)
```

**Honest gap:** the header comment named `tools/rx_woc_selftest.c` as the intended harness, but
that file was never committed by the lane (see section 8). The model is therefore compile-checked,
not test-pinned, in this directory.

## 4. The kernel implementation (the two patches)

Both patches implement the same mechanism on the two different base trees the lane had available
(the RX work had several in-flight stacks; the lane committed one patch per base rather than
guessing):

| patch | base | note |
| --- | --- | --- |
| `patches/0001-rx-woc-class.patch` | the lane's primary RX source tree (its `hi5622_dp.c` pre-image index `447a2c3`) | full class implementation, including the per-publish prepare/flush inside the driver's buffer-preparation path |
| `patches/0002-rx-woc-class-stacked-on-rx-format.patch` | the **rx-format** lane's tree (pre-image `b061cdb`) | same mechanism, refactored so the rx-format tree's `dptx_rx_publish_one` can call a separate `hi5622_dp_rx_buf_flush()` between the header scrub and the ring-entry store |

Mechanism (both patches, `hi5622_dp.c` / `hi5622_dp.h` / `hi5622_dp_txrx.c`):

- **module parameter** `rx_buf_class` (uint, default `0` = coherent, the exact old behaviour).
  `1` = vendor WOC class. Any other value makes `dp_setup` **refuse before any allocation or
  device write**.
- **pools:** the normal and small RX pools gain a parallel `struct sk_buff *` array. For class 1:
  `__netdev_alloc_skb(NULL, size + 8, GFP_KERNEL)`, 4-align `skb->data` via `skb_reserve`,
  one held `dma_map_page_attrs(..., DMA_FROM_DEVICE, 0)` mapping; device-window check; pre-write
  the host word + zeroed 36-byte header; store cpu/dma/skb. Teardown unmaps and frees.
- **publication path:** for class 1 the per-buffer preparation writes the host word and zeroes the
  header, then runs one `dma_sync_single_for_device(..., DMA_BIDIRECTIONAL)` (patch 0002 exposes
  this as `hi5622_dp_rx_buf_flush()`) before the ring entry becomes visible; the consume path
  invalidates before the CPU parses the 36-byte header. The coherent class keeps the old
  behaviour exactly (no-op).
- **counters/verdict:** `rx_woc_prepares`, `rx_woc_sync_cpu`; the status string reports
  `RX buffer class N (vendor WOC (cacheable skb slab + streaming DMA_FROM_DEVICE))`; refill/wm
  paths report the class.
- **fail-closed inventory:** unsupported class refused at `dp_alloc_rings`; DMA outside the
  device window refused (allocation freed); align/size validation; no vendor byte is copied
  (only addresses/layouts are derived facts).

The Lead's fire confirms the parameter path worked as designed: the module loaded and reported
`DP setup: OK - RX buffer class 1 (vendor WOC)`.

## 5. The bench fire (the result)

The lane's own committed artifacts at commit time had **no build script**: the Lead built the
module from the lane's then-untracked patches and fired it. Record (RX-hunt ledger entry "WOC CLASS
FIRE + PD4 DECODE", 13:00):

| item | value |
| --- | --- |
| artifact | built by the Lead from this lane's patches; **365,332 B** stripped, sha256 `3363903a...`, **60 parameters** (the 59-param observe-only vendor-diff base + `rx_buf_class`) |
| load | `rx_buf_class=1`; dmesg `DP setup: OK - RX buffer class 1 (vendor WOC)` |
| boot/ladder | fresh boot, PD4 firmware, AP-ENABLED; page `0x86807FEF/7/2/0xABCD` **golden:1**, no exception at setup |
| traffic | the laptop sent 3 management authentication frames to the AP (its own kernel log proves the TX attempts) |
| capture | **status `0x00000582`** (bit 10 set; base status had been `0x00000182`), **`frames=0`**, no completion pending |
| verdict | **the WOC buffer class is NOT the fix** |

Two readings matter here:

1. **The class machinery worked** - the pools were allocated from the vendor class and the ladder
   reached AP-ENABLED with a golden page. This was not an artefact of a failed setup.
2. **The outcome was identical to the coherent class**: the MAC/device side still did not write a
   frame to host memory. Since the buffers in this run were exactly the vendor's memory class, the
   missing link cannot be "the RX write master requires slab/streaming memory instead of coherent
   memory" in the sense this lane tested.

## 6. What the result rules out - and what it does not

**Ruled out (for this experiment):** the buffer allocation class alone explains the zero-host-write
behaviour. The vendor's buffer class is reproduced and the device still writes nothing with an
armed ring and a received frame.

**Not ruled out (and not claimed by this lane):**

- the class interaction with *cache maintenance ordering* beyond the tested sync points (the vendor
  does the maintenance around `add_buff`; our sync is equivalent in shape but the fire did not
  measure line state);
- the RX write path being configured / initialised on the firmware side (the F1 hypothesis: the
  firmware RX ring/PPDU state is never (re)initialised by our minimal host message set);
- the completion-ring consumer phase/ownership (the sibling candidate lane
  `wifi-rx-completion-phase` fired the phase-1 prepublish; it ticked status bit 2 but
  `frames=0` too);
- a firmware-mediated DMAC fault on the arriving frame (the R2 result: with the host rings absent
  the same frames are harmless; with our rings armed the status moves to `0x582` and the page can
  be lost intermittently).

**The converged state after both candidate fires:** the device-side RX state matches vendor stock
exactly, every host-side static aspect (enable/format/device state/address transform/WOC class) is
vendor-correct, the MAC counts a received frame (the `0x582` bit-10 class) but writes **nothing**
to host memory (`frames=0`), and the completion-consumer phase can tick the RX-complete bit
without the frame landing. The leading remaining cause is the **firmware-side RX configuration /
init** (F1), which is the next lane's target (`wifi-init-decode`; the vendor module symbol table
and its generic config-event messages).

**Interactions to keep in mind for the next fire:** the page golden-loss during RX fires is
intermittent (a race/timing component); the module only ever changes behaviour when
`rx_buf_class=1` is set at load; loaded default (0) is the exact old behaviour.

## 7. Evidence map

| claim | artifact / record |
| --- | --- |
| the vendor-class protocol (alloc/free/publication/host-CA) | this report section 2; derived from the local vendor modules; the committed patch comments carry the same decode |
| the model constants/behaviour | `src/hi5622_rx_woc.h` (constants + API docs), `src/hi5622_rx_woc.c` |
| the kernel mechanism and fail-closed gates | `patches/0001-rx-woc-class.patch`, `patches/0002-rx-woc-class-stacked-on-rx-format.patch` |
| the artifacts were committed despite the lane dying | opensource commit `04c0521` ("Commit the rx-completion-phase + rx-woc-class + wifi-ready-emit lane artifacts ...") |
| the bench fire and verdict | superproject `mem-entries.md` entry "2026-11-26 13:00 - WOC-CLASS FIRE + PD4 DECODE" |
| the surrounding RX state and the R0/R2 evidence | `wifi-rx-vendor-diff/REPORT.md`, `wifi-rx-capture-plan/REPORT.md`, `wifi-rx-format/REPORT.md`; `wifi-re-report` v7 sections 6.26-6.29 |
| the sibling candidate fire (completion phase) | `wifi-rx-completion-phase/REPORT.md`; same ledger entry chain (12:00 pre-publish fire, 13:00 WOC fire) |

## 8. Honest limitations / what remains

1. **No committed selftest or build script.** The model is compile-verified only; the kernel
   patches were built by the Lead, not by a lane script. A future cleanup should add
   `tools/rx_woc_selftest.c` + `tools/run-host-tests.sh` + a build script (the
   `wifi-rx-completion-phase` lane's tooling is the pattern to port) and record the artifact
   identity in this report.
2. **Exact fired base-tree chain not recorded.** The two patches target two RX-stack bases; the
   Lead's 60-parameter module implies the observe-only vendor-diff base + `rx_buf_class`. The
   precise patch/base pair used for the fire is not in the committed artifacts [U].
3. **Buffer-class variants beyond class 1 are untested** (e.g. coherent buffers with the vendor's
   ownership sync, or cacheable memory without the streaming map). Class 1 is the vendor-exact
   class and it did not change the outcome.
4. **No cache-line/coherence measurement** was taken around the publication (the fire measured the
   RX outcome, not line state).
5. The lane did not open the vendor artifacts to commit any vendor-derived byte: only module
   addresses, sizes and layout facts appear in the patch comments/model (project rule).

## 9. Reproduce / reuse

- Model compile (host-only): `cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -c src/hi5622_rx_woc.c`.
- Kernel patch: apply the variant matching the base tree (
  `patch -p1 --dry-run < patches/0001-rx-woc-class.patch` or `.../0002-...patch`), build the
  module on the lab kernel tree, load with `rx_buf_class=1`; the outcome to reproduce is the
  13:00 bench row above (class selected, ap enabled, golden page, `frames=0`).
- The parameter is fail-closed: omit it (or 0) and the driver behaves exactly as before this lane.
- **Safety:** the RX ladder itself is governed by the project's fire rules and the Lead's recovery
  door; nothing in this document authorises a device write. Never write CA `0x400392F0`.
