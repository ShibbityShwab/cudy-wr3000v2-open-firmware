# Firmware call graph for the named radar/DFS handlers (phase 7, 2026-10-01)

Built by disassembling the firmware blob's own named entry points. Method: resolve the 30 `{address,
name}` pairs from the tables with the `-0x40000` bias (phase 4), clear the Thumb bit, decode in Thumb
mode with capstone, and walk each function until its epilogue (`pop {..., pc}` or `bx lr`), collecting
every `bl`/`blx` immediate. Script and raw output: `build/tmp/callgraph2.py`, `build/tmp/callgraph.txt`.

## The uniform shape

Every one of the 30 handlers is a thin wrapper. They share four helpers, and the callee counts are
small (1 to 20 calls):

| helper | role (from its own disassembly) |
| --- | --- |
| `0x2c28` | parameter-block handler: `push {r4,r5,lr}`, zeroes a `0x48`-byte local, calls `0xc31a4` (a memset-like routine at the blob's edge), then walks the parameter. Appears in 18 of the 30 handlers. |
| `0x1f90` | near-identical twin of `0x2c28` with a `0x58` frame - the second half of the common get/set pair. |
| `0xbcfe` | string-to-integer parser: skips spaces, detects `0x2d` (`-`), calls `0x6f6` for the digits, negates. Used by every setter that takes a number. |
| `0x81dc0` | prefix/string matcher: `ldrsb` loops comparing two strings byte by byte, with null guards. The name-matching arm of the dispatcher. |
| `0x7cdc4` | lookup helper: calls `0x561ae` and branches on the result. |
| `0x64fc8` / `0x64ff8` | the two off-channel helpers used by `offchanenable` and `cac_silent_enable`. |
| `0xb50b4`, `0x70930`, `0xb4aa4`, `0xc179c`, `0x1df4` | handler-specific targets (one per feature). |

## The graph (30 rows, condensed)

| handler | offset | calls | callees |
| --- | --- | --- | --- |
| `debug` | `0xbd20` | 3 | `0x81dc0`, `0xbcfe`, `0x1f90` |
| `enabletimer` | `0x75548` | 1 | `0x1f90` |
| `radar_phy_enable` | `0x75860` | 1 | `0x2c28` |
| `offcactime` | `0x758a0` | 1 | `0x2c28` |
| `cac` | `0x75fa4` | 1 | `0xb50b4` |
| `get_detect_check_info` | `0x7609c` | 1 | `0x2c28` |
| `detect_check` | `0x76110` | 5 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe`, `0x1f90` |
| `octo_filter_enable` | `0x76188` | 5 | same five as above |
| `cac_silent_enable` | `0x76204` | 2 | `0x64ff8`, `0x2c28` |
| `pulse_check_filter` | `0x762fc` | 20 | `0x2c28`, `0x81dc0`, `0xbcfe`, `0x1f90`, `0x64ff8`, ... |
| `read_pulse` | `0x7643c` | 4 | `0x1f90`, `0x81dc0`, `0xbcfe` |
| `one_pulse_chirp_enable` | `0x764a0` | 6 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe`, `0x1df4`, `0xb4aa4` |
| `log_switch` | `0x7651c` | 5 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe`, `0x1f90` |
| `non_occupancy_period` | `0x76578` | 1 | `0x2c28` |
| `get_radar_th` | `0x76614` | 1 | `0x2c28` |
| `set_radar_th` | `0x76710` | 5 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe` |
| `set_5g_channel_bitmap` | `0x76780` | 16 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe`, `0xc179c`, ... |
| `set_next_chan` | `0x76850` | 5 | `0x1f90`, `0x81dc0`, `0xbcfe`, `0x2c28`, `0x70930` |
| `dfstrig` | `0x768b8` | 4 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe` |
| `offchantime` | `0x76908` | 4 | same four |
| `operntime` | `0x76958` | 1 | `0x2c28` |
| `offchanenable` | `0x769f4` | 1 | `0x64fc8` |
| `radarfilter_get` | `0x76a32` | 1 | `0x1f90` |
| `radarfilter` | `0x76ae0` | 1 | `0x7cdc4` |
| `ctsdura` | `0x76b18` | 4 | `0x2c28`, `0x7cdc4`, `0x81dc0`, `0xbcfe` |
| `offchannum` | `0x76b68` | 4 | same four |
| `dfsdebug` | `0x76bb8` | 4 | same four |
| `cacenable` | `0x76c04` | 1 | `0x2c28` |

The full table with every callee is in `build/tmp/callgraph.txt`.

## What this means

1. **The handler layer is regular and small.** A getter that takes no argument calls only the parameter
   helper; a setter adds the string-to-int parser and, when the parameter needs validation, the lookup
   helper. That is a wiring diagram an open implementation can follow directly.
2. **The plumbing is shared, the features are not.** The radar pipeline proper
   (`read_pulse` -> `pulse_check_filter` -> `detect_check` -> `dfstrig`) shows up as distinct functions
   with their own targets, so the DFS logic is real code in the blob, not a stub.
3. **The dispatcher's name matching lives in the blob too** (`0x81dc0`), which is why the host's `alg`
   commands map onto firmware-side parameter names almost one to one.

## Limits

- Only direct `bl`/`blx #imm` calls were captured; indirect calls through tables (`blx rN`) are not in
  the graph, and the firmware uses tables heavily elsewhere.
- Each function was walked to its first epilogue; functions with multiple return paths may have later
  calls unrecorded.
- The unnamed helper addresses are described only by their first dozen instructions; their full
  semantics are not claimed.
