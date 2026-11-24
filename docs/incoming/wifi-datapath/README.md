# wifi-datapath

Data-plane reverse engineering for the Hi5622V100 clean-room host: the hdpp/alg/wal message
family, the MAC host-interface frame rings, and the host skb<->frame architecture.

**Lane:** wifi-datapath (shared task task-105), host-only static RE + compile-verified
builders. 2026-11-24.

| file | contents |
| --- | --- |
| REPORT.md | main report: the two data planes, MAC slot-5 register map, firmware config-key tables, vendor senders, TX/RX descriptor formats, direct-read implications, host architecture, message map, minimum viable sequence, evidence index |
| FRAME-SPECS.md | byte-level specs: 12-byte HCC header, g0 id1/id2 config event, create/0xd8/0x320 vectors, 8-byte TX MSDU entry, packed ring index, MAC register program, RX buffer layout |
| BENCH-LADDER.md | attended bring-up stages B0-B5 with read-only packs, decision matrix and hard stop rules |
| BUILD-NOTES.md | build/test evidence and what was not done |
| src/dp_frames.h/.c | pure-C99 builders/validators (HCC frames, TX entries, ring indices, message catalogue) |
| src/dp_selftest.c | host-only known-answer + refusal suite (77 checks) |
| tools/build.sh | compile -Werror -pedantic + run the selftest |
| tools/fw_dp_tables.py | decode/verify the firmware group-0 id-1 key tables (35 checks), emit the markdown table |
| tables/firmware-config-keys.md | generated decoded key tables A/B (217+88 slots, decoded constants) |

Verification:

    ./tools/build.sh                                   # 77 passed, 0 failed
    .provision-venv/bin/python tools/fw_dp_tables.py --selftest   # 35 passed, 0 failed

Read-first: ../wifi-ctx-create/REPORT.md, ../wifi-h2d-catalog/REPORT.md,
../wifi-vendor-userspace/TABLES.md, ../../phase20/tx-path.md, ../../phase43/THE-CHANNEL-MAP.md.

No vendor binaries, firmware bytes, calibration data or per-unit values are committed; the
vendor artefacts are read in place locally and only decoded addresses/constants and original
code/text are published here. All C code is GPL-2.0 (kernel-linking license); docs are
original writing.
