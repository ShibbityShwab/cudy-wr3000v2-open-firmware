# Phase-42 verification (lead-run after the lost-child recovery, 2026-10-04)

The run's two lane children died in the host restart after writing their files; the lead
re-derived every load-bearing claim before writing this.

| report | verdict | basis |
| --- | --- | --- |
| block-structure.md | **PASS** | lead re-checked: all five marker words match the snapshot files exactly; 24 block starts of 0x200 present 8/8 per window; 0x3f100c = 0x00100000 |
| word-semantics.md | **PASS** | lead re-disassembled the fill: 0x17884 bfi r1,r3,#0x10,#0x10 (len<<16), 0x178a0 orr #0x4000, 0x178ac orr #0x2000, 0x178b8 bfi r2,r1,#0,#0xd with r1=0xd2b (0x1789c) - all exact; the (len<<16)|flags node format matches phase-25's live read and the phase-37 verified map |
| ete-registers-reconciled.md | **FAIL (one claim)** | its DR-base 0x8490b000, described as "live in three dumps", appears in NO artifact anywhere in the repo (grep of all docs + dumps: zero hits). Its DIRECTION is right and is supported elsewhere: phase25/live-vendor-ring-ground-truth.md reads SR ch0 base=0x848F6000 with 8-byte nodes (buf 0x82483840 len=72), and build/register-dumps/reg_all.txt holds 0x4004a004 = 0x84a90000. The corrected DR base is 0x84a90000 (register at device CA 0x4004a004), pending a fresh live read to confirm |
