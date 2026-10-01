# Live diff: what a single message moves inside the chip (phase 12, 2026-10-01)

Two full 16 MiB snapshots of the chip's memory window, taken around three calibration reads
(`iwpriv Hisilicon0 alg get_2g_power_param`), diffed page by page. Dumps:
`opensource/build/register-dumps/diff_before.bin` and `diff_after.bin` (16,777,216 bytes each, produced
by `lab/barmap`).

## Global numbers

| metric | value |
| --- | --- |
| changed pages (4 KiB) | **224 of 4096** |
| contiguous changed regions | 120 |
| changed 32-bit words | 7,041 |
| changed words that are small positive increments | 1,256 |
| changed pages inside the register block (`0x3b8000..0x4d7fff`) | **36** |

Distribution by megabyte of the window: 55 in MiB 1, 25 in 2, 28 in 3, 31 in 4, 22 in 5, 16 in 6,
10 in 7, 37 in 8; nothing above MiB 8 (that region is all `0xff`).

## The register block reacted

Mapping host to config address (`CA = host - 0x3b8000 + 0x40000000`), the 36 changed register pages
cover **CA `0x40000000` to `0x4010e000`**, including the blocks the vendor's own dump classes as MAC,
PCIe-message and queue areas: `0x40000000`, `0x40001000`, `0x40005000`, `0x40030000`, `0x40031000`,
`0x40032000`, `0x40034000`, `0x40037000`, `0x40038000`, `0x40039000`, `0x4003a000`, `0x40044000` and
more. A single command therefore leaves fingerprints in both the chip's code memory and its register
space, which is exactly the pair a transport implementation must drive.

## What the changed words look like

- **Counter-like:** 1,256 words are small positive increments over a three-command interval, often in
  duplicated pairs, e.g. `0x0000a18b -> 0x0000a19a` and `0x0000f301 -> 0x0000f30f`.
- **Per-chain duplicated halves:** many words carry the same 16-bit value twice (or two halves that move
  together), e.g. `0x1f8f1f8f -> 0x4fc54fc5`, `0x4dfb4dfb -> 0x4f804f80`. These look like per-antenna or
  per-chain entries rather than single scalars.
- **State transitions:** e.g. `0x00000413 -> 0x0000000f` (the same value repeated at two neighbouring
  words) reads like a status handshake rather than a counter.

## Limits

- A before/after pair cannot separate changes caused by the command from background activity (beacons,
  timers, other traffic); only the register block and the duplicated-half patterns are strong enough to
  attribute.
- The instruction count between snapshots is not exactly one (three identical reads were issued), so
  increments are "per three commands" and not a per-message rate.
- No attempt is made here to name individual ring structures; the region list is the input for that
  work, not the conclusion.
