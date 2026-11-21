# W22 release-integration drafts (2026-11-21)

Status: draft patches prepared after M1a was proven on hardware (proof image 5800c9ab). Nothing
here has been applied to the release series or the device by this session.

| file | content |
| --- | --- |
| 0001-net-lsw-napt-init.patch | luofu-switch.c/.h/regs.h: W22 NAPT/flow engine init stage (vendor order after W8, before W10; always-on with luofu_switch.naptinit default 1; carveout validation, idempotent guard, polled v4/v6 clears, snapshot restore) |
| 0002-net-pie-exception-delivery.patch | luofu-pie.c: deliver the native exception/CPU-delivery status class by default and retire the luofu_pie.accepterr gate |
| W22-DESIGN.md | design rationale, gate/ordering/idempotence/carveout contract, mirror-retirement decision, remaining unknowns |
| RELEASE-INTEGRATION.md | exact patch placement, manifest lines/hashes, target config command-line diff, validator/compile/image validation steps, fallback image, rollback |

Validation already done host-side: both patches apply to the release driver tree, both objects
compile in a configured linux-6.18.60 tree (RC=0), checkpatch 0 errors/0 warnings/0 checks.
sha256: 0001 = cfd7debc3908e0b52785afd5ffac979c8c5e03538b3f98dccbf54738fa98152f,
0002 = 8c748061fe0577cea471534f209e68258bc707d45746c25d4f40409f6eeb1bd3.

Not done: hardware run of the kernel implementation, release-series staging, manifest update,
release-line validator, full target build, image install. Those are the steps in
RELEASE-INTEGRATION.md (lead-run).
