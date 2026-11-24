# wifi-hcc-core lane (task-92)

The complete post-ready host HCC protocol state machine for the Hi5622V100
clean-room PCIe/ETE transport, on the corrected GP8 boot protocol - as a
portable, device-free C library with a deterministic selftest and a 6.18.60
kernel compile check.

**Status:** design + implementation + compile verification complete (host-only;
the attended run of the integrated module is the Lead's session).  No vendor
binary, firmware byte, calibration value or secret is committed here.

| file | what |
| --- | --- |
| REPORT.md | protocol model, evidence, the policy fix list, verification, limits |
| INTEGRATION.md | exact map from the current hi5622_pciet_hcc fragments to the core + the code-level fixes |
| BUILD-NOTES.md | build result, toolchain, hashes |
| src/hcc_core.h | contract header: address maps, mailbox/boot/frame/ready/PM constants, types, API |
| src/hcc_core.c | the state machine: boot latch -> ready pair -> TXQ sync -> active; D2H mailbox, H2D coalescing, CECE policy, PM, heartbeat, rings, frames |
| src/hcc_core_kernel.h/.c | reference kernel adapter (readl/writel, jiffies clock, dev_info events) |
| src/kmod/ | device-free kernel compile-check module |
| tests/hcc_core_test.c | deterministic fake-BAR0 selftest (528 checks) |
| tools/build.sh | host build + host selftest + 6.18.60 kernel compile check |

Build:

    ./tools/build.sh          # host selftest PASS (528/528) + kernel compile check PASS
    ./tools/build.sh --no-kernel

One-line result: the whole post-release protocol sequence and its policy
corrections are now one tested state machine; host-exact semantics are pinned
by the selftest, and the library compiles clean inside a 6.18.60 module so it is
ready to replace the hand-rolled fragments in the in-tree hcc service.
