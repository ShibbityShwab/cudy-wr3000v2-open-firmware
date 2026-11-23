# wifi-hcc-service lane (task-87)

Host-side HCC service counterpart for the Hi5622V100 transport, running the
vendor's post-release algorithm on the **corrected boot protocol** (GP8):
release 0x5a5a only; never write 0xCECE during the ROM boot stage; write it
only after the handshake page CA 0x1bf000 +8 reads 2 and only on observed
handler-spin evidence.

**Status:** design + compile-ready module + build verification complete
(host-only; the bench run is the Lead's attended session). No vendor binary,
firmware byte or secret is committed here.

| file | what |
| --- | --- |
| REPORT.md | the five answers (D2H order, CECE policy, ETE announce/engine wake, ready exchange, IRQ-unmask determination), design, evidence ledger |
| BENCH-PROCEDURE.md | attended stages S0-S10, stop rules, expected observables |
| BUILD-NOTES.md | module metadata, build line, 6.18 API notes, self-test |
| src/ | compile-ready module: transport skeleton copies + hi5622_hcc_service.h + pciet_hcc_service.c + Kbuild/Makefile |
| tools/build.sh | host-only build driver (scratch copy, sha256/vermagic/attrs) |
| tools/frame_selftest.py | device-free frame model + ring arithmetic self-test (83 checks) |

Build: ./tools/build.sh  ->  hi5622_pciet_hcc.ko
(201,140 B, sha256 8fd9085431ee6b064f435f9550d9dfef5d74b0023020983682361af7746a48db,
vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, zero warnings).

Load ONLY this module (driver hi5622_pciet_hcc; it cannot coexist with
hi5622_pciet.ko / hi5622_pciet_b3.ko - one driver per endpoint).

One-line result: the host counterpart is now vendor-exact and build-verified;
the remaining post-GP8 stall is classified by the read-only hcc_irq_probe as
either image-scheduler/task-queue side or CPU/ROM IRQ-delivery side - both
firmware-internal, so the next lane is firmware-side, not more host writes.
