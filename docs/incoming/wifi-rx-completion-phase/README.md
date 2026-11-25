# wifi-rx-completion-phase - the completion-ring consumer phase (task-168)

**Lane:** wifi-rx-completion-phase (shared task `task-168`), host-only. **Status:** closed;
artifacts committed, the bench fire ran, the report is [REPORT.md](REPORT.md). The lane died before
writing its docs; this page + the report were written at the RX-hunt closeout.

**Question:** the completion-ring consumer is the one host-written RX word never modelled as a
*phase* quantity. Does the device's DMAC need a non-zero completion-consumer phase before it
accepts its first completion write? The vendor ring pointer is `index[14:0] | phase<<15`, and the
vendor's initial completion consumer is `0x0000` (the free-ring producers publish `0x8000`).

**Answer:** the phase is real - the one-shot phase-1 prepublish (`0x8000`) moved the host-MAC
status word `0x182 -> 0x186` (**bit 2, RX-complete, first time seen**) but **no frame was written**
(`frames=0`, completion ring empty, page stayed golden). **The completion-consumer phase is not
the capture blocker.**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the vendor semantics, the model, the kernel rung, the bench fire, the verdict, the honest gaps |
| [src/hi5622_rx_cmp_phase.h](src/hi5622_rx_cmp_phase.h), [src/hi5622_rx_cmp_phase.c](src/hi5622_rx_cmp_phase.c) | device-free model: packed-pointer arithmetic, phase-aware pending, the fail-closed gate chain, the exact-width store |
| [patches/0001-rx-cmp-phase-prepublish.patch](patches/0001-rx-cmp-phase-prepublish.patch) | kernel glue: the `rx_cmp_prepublish` parameter + one-shot `dp_rx_cmp_prepublish` action (probe-and-rollback) |
| [tools/rx_cmp_phase_selftest.c](tools/rx_cmp_phase_selftest.c), [tools/run-host-tests.sh](tools/run-host-tests.sh) | host test: **102 checks, 0 failures, PASS** (re-run at closeout) |
| [tools/build-prepublish-module.sh](tools/build-prepublish-module.sh) | reproducible build + identity gates (60 parameters; rejects drift/warnings) |

**Ground rules:** host-only; the Lead fired; no vendor binary, object code, firmware byte or
per-unit identifier is committed. The fired module (`bc942e13...`, 60 params) is local-only
evidence.

**Known gaps:** the lane's build was run by the Lead, not the lane; the traffic rung was never run
with the prepublish accepted (the clean no-traffic run is the discriminator only); the exact
signal raising status bit 2 is inferred - see REPORT.md section 8.
