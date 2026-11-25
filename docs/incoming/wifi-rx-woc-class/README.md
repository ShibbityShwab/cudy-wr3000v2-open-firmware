# wifi-rx-woc-class - the vendor RX buffer memory class (task-167)

**Lane:** wifi-rx-woc-class (shared task `task-167`), host-only. **Status:** closed; artifacts
committed, the bench fire ran, the report is [REPORT.md](REPORT.md). The lane died before writing
its docs; this page + the report were written at the RX-hunt closeout.

**Question:** the vendor's RX buffers come from a dedicated allocator (`hi_net_woc_alloc_skb` in
`hi_kpie.ko`). Does the RX write master require that memory class (a reserved carve-out with special
attributes) instead of our `dma_alloc_coherent()` pools?

**Answer:** no carve-out exists - the "WOC" class is cacheable skb slab memory + one held streaming
`DMA_FROM_DEVICE` mapping per buffer, and the `0x600000` bias is the vendor kernel PHYS_OFFSET.
The class was reproduced behind the fail-closed `rx_buf_class=1` parameter and fired: the module
loaded and reported the vendor class, the AP came up golden, the laptop sent authentication frames,
but the MAC still wrote **nothing** to host memory (`frames=0`, status `0x582`). **The buffer
memory class is not the fix.**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode (alloc/free/publication/host-CA), the model, the kernel patches, the bench fire and verdict, the honest gaps |
| [src/hi5622_rx_woc.h](src/hi5622_rx_woc.h), [src/hi5622_rx_woc.c](src/hi5622_rx_woc.c) | portable host-testable model of the class protocol and the fail-closed decisions |
| [patches/0001-rx-woc-class.patch](patches/0001-rx-woc-class.patch) | the class implementation on the lane's primary RX source tree |
| [patches/0002-rx-woc-class-stacked-on-rx-format.patch](patches/0002-rx-woc-class-stacked-on-rx-format.patch) | the same mechanism stacked on the rx-format lane's tree |

**Ground rules:** host-only; no device/bench contact from the lane (the Lead fires); no vendor
binary, object code, firmware byte or per-unit identifier is committed; only derived offsets,
sizes and layout facts appear. The fired module (365,332 B sha256 `3363903a...`, 60 params) was
built by the Lead from these patches and is local-only evidence.

**Known gaps:** no committed host-test harness/build script (the model is compile-verified; the
fired module was built by the Lead), and the exact fired patch/base pair was not recorded - see
REPORT.md section 8.
