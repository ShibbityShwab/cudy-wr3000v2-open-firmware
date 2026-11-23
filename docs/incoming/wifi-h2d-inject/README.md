# wifi-h2d-inject — host-side H2D message injector (Hi5622V100 / WR3000 v2.0)

**Lane:** wifi-h2d-inject (task-73, host-only). **Read [REPORT.md](REPORT.md) first** — it
holds the contract, the sysfs interface, the build/test evidence and the benchmark procedure
for the Lead (section 7).

Quick start (host only; no device, no image build):

    ./tools/build.sh --selftest-only            # userspace frame-model selftest (63 cases)
    ./tools/build.sh --module-only              # base transport + injector -> hi5622_pciet.ko
    ./tools/build.sh --module-only --tree b3    # B3 instrument + injector -> hi5622_pciet_b3.ko

Layout:

| path | what |
| --- | --- |
| <code>files/pciet_h2d_frame.h</code> | pure-C HCC request parser / frame builder / validator / shared self-test |
| <code>files/pciet_h2d.c</code> | kernel injector (sysfs attrs, SR0 post, contract announce, drain-poll) |
| <code>files/h2d_selftest.c</code> | userspace selftest + frame CLI (<code>build</code>, <code>validate</code>) |
| <code>patches/</code> | compile-ready patches for the two reference trees |
| <code>tools/</code> | patch generator, deterministic edit script, build driver |

Hard rules encoded: payload <= 0x674 / total <= 0x680, HCC message id 5 refused everywhere,
mailbox announce hard-wired to id 3, never write CA 0x400392f0 (out[5]). See REPORT.md.

Status: host-side implementation complete and compile-verified; the bench run is the Lead's
next attended session (REPORT.md section 7). This lane never contacted the router.
