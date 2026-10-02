# Message-block aliasing: what is eliminated, and the one test left (phase 23m, 2026-10-02)

The `out[0]` alias (`docs/phase23/window-disambiguation.md`) is narrowed to a single mechanism.
Everything that could be checked statically has been, and each candidate is now either proven or
excluded.

## The observation, restated exactly

Same driver, same viewport values, two states:

| offset | device CA | vendor boot | my takeover runs |
| --- | --- | --- | --- |
| BAR0 `0x3f1000` | `0x40039000` | `0x0000010B` | `0x000559e7` (config[0x00]) |
| BAR0 `0x3f1010` | `0x40039010` (out[0]) | `0x00000000` | `0x40000004` (config[0x10]) |
| BAR0 `0x3f12e8` | `0x400392e8` (glue chn_res) | `0x00000020` | `0x00000000` |
| BAR0 `0x3f1508` | `0x40039508` (ETE intr) | `0x3F201818` | (written OK after the fix) |
| BAR0 `0x3f2000` | `0x4003a000` (ETE block) | `0x0000010A` | `0x0000010a` **identical** |

Two decisive facts:

1. **The offsets are correct.** The vendor boot returns real register values at every one of them
   (`0x10B`, `0x20`, `0x3F201818`, `0x10A`) - matching phase 11/18/20's recorded values and phase 15's
   captured `chn_res = 0x20`. So neither the table nor my window arithmetic is wrong.
2. **The aliased values are exactly PCI config-space words.** `0x3f1000` returns config dword `0x00`
   (vendor `59e7` | device `0005`), and `0x3f1010` returns config dword `0x10` (the BAR0 register).
   The offset delta `0x10` in BAR0 maps to the offset delta `0x10` in config space.

## Candidates, resolved

| candidate | verdict |
| --- | --- |
| wrong window offset (`0x39000` vs `0x3f0000`) | **was** a real bug, fixed - and it is not this, because the corrected offset still aliases |
| undersized `ioremap` | **was** a real bug (the oops), fixed - not this |
| wrong viewport values | **eliminated** - all six read back byte-identical to the vendor (`write-path-executed.md`) |
| wrong BAR in ctrl2 | **eliminated** - the vendor uses BAR0 for every region (`ctrl2 = 0x80000000`) and its own live values match mine |
| translation arithmetic | **eliminated** - `dev + (host - base)` yields the correct CAs (`0x40039000`, `0x4003a000`) for both blocks |
| the same region serving both blocks | **the remaining suspicion** - both CAs fall inside region 3 (`0x403b8000..0x404d7fff` -> `0x40000000`), yet the part of the window carrying the message block resolves to config space while the ETE part does not |

## The one test left

Read **every 0x1000-aligned word from `0x3f1000` to `0x3f3000`** in a takeover boot after programming
the viewports, and the same range on a vendor boot, then diff. If the alias starts at a boundary
(config space reappearing from some offset onward), the boundary names the mechanism; if the whole
low half aliases and the ETE half does not, the two blocks are served by different region descriptors
and the region table needs a seventh entry - which would mean the vendor's own descriptor list
(`g_shuangta_region_types`, 6 x 0x50) is not the whole story for the IO area.

That is a read-only sweep: no writes, no descriptors, and it runs in the same detached harness with
the watchdog armed. It is also cheap - one boot, and the answer is a table diff rather than an
argument.

## Why this matters for the port (and why it can wait)

The message block is how the host and the released firmware talk (`out[0]`/out[1]` pending words,
the doorbell, the glue status). Reading it correctly is a prerequisite for the H2D dialogue that phase
22 identified as the real blocker. But it is **not** a prerequisite for the port's own structure: the
wiphy, the netdev, the endpoint claim, the viewport programming and now the ring ownership are all
independent of it, and those are what a driver build actually needs. The sweep is the next device
step; nothing else in the port is blocked on it.
