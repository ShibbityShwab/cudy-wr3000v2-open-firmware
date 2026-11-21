# SMP release diagnosis lane - FINAL record (task-62, agent smp-decode)

Revision 11, 2026-11-21. FINAL. SMP is resolved: CPU1 comes up on the 6.18 kernel
(canonical bench image 974e6ca6: 2 CPUs, CPU1 thread mpidr 80000001, ping 20/20,
SSH, rw overlay, soak running). The root cause was the bootloader handoff devicetree
contract, not any kernel SMP code; the fix is DTS-only.

Design/evidence doc: opensource/docs/SMP-RE.md. This revision's shared.patch is
CUMULATIVE: it appends sections 21-24 on top of the currently integrated sections
14-20 (sections 21-23 were delivered earlier as separate patches and were not yet
integrated; do not apply the older shared.patch versions on top).

## 1. Root cause [V]

U-Boot's bootfip handoff staging aborts when the kernel/board devicetree lacks two
nodes that the stock vendor base devicetree carries:

1. /aliases/console, resolved to the console UART node (stock base DTB:
   console = "/uart1@0x1010f000");
2. /reserved-memory/flashinfo@80600000, compatible hsan,flashinfo_reserved,
   reg = <0x80600000 0x4000>, atag-offset = <0x1000> (bootfip reads atag-offset and
   computes atag_addr = 0x80601000).

Without them our boot printed the vendor warning "alias console not found" and the
staging lines (alias console=..., serialnr->high=0x8250, serialnr->low=0x1010f000,
atag_addr=0x80601000) never appeared: CPU1 was never brought into the U-Boot start.S
pen-poll loop, so the kernel's correctly-decoded release (pen write, CRG resume
clear, wakeup IPI) had no consumer. That is what patches 033..042 measured and what
the 060/061 sentinel probes proved (CPU1 did not enter the stubs at decompressor
entry, at stext, or at late time).

With the two DT nodes present: stock staging lines print, CPU1 consumes the real
secondary_startup pen at the kernel release, and the system reports
"smp: Brought up 1 node, 2 CPUs", "CPU1: thread -1, cpu 1, socket 0, mpidr
80000001", /proc/cpuinfo = 2, online 0-1, CPU0/CPU1 interrupts and SCU CONFIG bit 5
set. The 060/061 sentinels stayed untouched, the clean contrast proving the loader
handoff staging was the missing piece.

[I] The internal bootfip mechanism (FIP staging / warm-reset / firmware BL2/BL31
path) is consistent with the decoded strings and code but is not instruction-level
proven; it is no longer needed for the fix.

## 2. The fix: DTS-only

Commit in target-skeleton/dts/luofu-r116.dts and mirror in the upstream submission
DTS (exact nodes are in SMP-RE.md section 24.2):

    aliases { serial0 = &uart1; serial1 = &uart0; console = &uart1; };

    reserved-memory {
        #address-cells = <1>; #size-cells = <1>; ranges;
        flashinfo@80600000 {
            compatible = "hsan,flashinfo_reserved";
            reg = <0x80600000 0x4000>;
            atag-offset = <0x1000>;
        };
    };

No kernel code change is required for SMP functionality: the vendor-faithful release
and the hisilicon,luofu-smp enable-method implementation already in the series are
correct; the missing piece was the loader handoff devicetree contract.

## 3. Diagnostic patch disposition

* 060 (handoff probes) and 061 (sentinel protocol + re-poll trampoline): bench-only
  diagnostics; keep in the bench set or drop; not needed for the fix.
* 033/034/035/037/039/042/059: bench instrumentation (dumps, probe stubs, late
  add_cpu trigger, bookkeeping); not needed for a release series. A release series
  should carry the minimal functional SMP driver plus the DTS fix and should be
  verified with no luofu_smp_* command-line tokens and no probe stubs (canonical
  image 974e6ca6 is that evidence).

## 4. Diagnostic patch hashes

| patch | sha256 |
| --- | --- |
| 033 | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| 034 | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| 035 | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| 037 | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| 039 | 37b7932b0c240d612582090ed17c5c91241f8bdfed074bdd0e7bafb13dd43a32 |
| 042 | dc8511edb95e4b788fc1c7d7934a23c6eb9e9160d4b538ef40333db550fa673a |
| 060 | e1d9dc5f6c1e64dcd473c26b6824e7c60822e18e424781388a03b82bcfa53938 |
| 061 | 5e254fe4cea0b64d47aa0fc3c4ddf7c82f935b76d92fd8f311ba9b219b52ddd8 |
| shared.patch v12 (cumulative sections 21-24) | 6fd85a2e30d98f67732b442b5cb200040c110d41dc937bf8725e50dbde25043b |

## 5. Corrections to earlier conclusions

* The "2-CPU image hung after 23 minutes" was a HOST bench-link outage (USB
  Ethernet carrier/IPs lost overnight). UART showed the box alive with no Oops/
  panic/BUG/WARNING (only the literal panic=0 cmdline); after power-cycle/link
  restore the same image is stable. No SMP stability bug. Bench lesson: check host
  link carrier before attributing a hang to the kernel; the UART is the authority.
* The W9 traffic crash (SMP-RE section 19) remains a net-lane item; it was not SMP.
* The W9 "2 CPUs" sighting was correctly re-attributed to the stock 5.10 fallback;
  the 060/061 sentinels then proved our kernel never had CPU1 in the poll loop
  before the DT fix.
* Loader-side leftovers (U-Boot staging copy content at 0x80000000, hboot overwritten
  in DRAM, unreadable 0x1017 block) are normal handoff artifacts, closed.

## 6. Integration

1. Apply the CUMULATIVE documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends sections 21-24 to docs/SMP-RE.md (current integrated state 14-20).
2. Commit the two DT nodes in the target DTS (and upstream submission DTS).
3. Release series: functional SMP driver + DTS fix; bench diagnostics out or gated.
   Verify a clean 2-CPU boot without any luofu_smp_* tokens.
4. Ledger/bench: record the host bench-link outage that caused the false hang
   attribution.