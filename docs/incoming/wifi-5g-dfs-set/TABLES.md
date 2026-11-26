# TABLES - the 0x07E7 DFS SET: keys, worker, registers, frame, gates, order

**Lane:** wifi-5g-dfs-set (task-227), 2026-11-26. **HOST-ONLY** decode + build;
no device contact. Grades: **[V]** re-derived by `tools/fw_dfs_set_re.py --selftest`
(89/89) from the pinned image; **[V\*]** committed earlier lane / bench record;
**[I]** inference; **[U]** needs a device read.

## T1. Pinned artifacts

| artifact | identity |
| --- | --- |
| FIRMWARE.bin | 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`, **CA = file + 0x40000** |
| host mappers | device IO phys = CA + **0x3B8000**; firmware RAM phys = 0x40000000 + CA + **0x6B8000** |
| R5 base module (local) | `/tmp/r5-rebuilt/hi5622_wifi.ko` 383,240 B / `24f7221bfc4c...` (wifi-r5-rebuild CHAIN.md) |
| this lane's module (local) | `/tmp/dfs-set-artifact/hi5622_wifi.ko` **391,388 B / 1b47f0daff7e... / 66 params** |
| decode tool | `tools/fw_dfs_set_re.py` 89/89 PASS ([evidence/fw-decode-selftest.txt](evidence/fw-decode-selftest.txt)) |

## T2. The dispatcher and the worker [V]

The g0-id1 config-event dispatcher is CA **0x43D58** (committed wifi-5g-smac-keys
T1): key = u16 hdr+20, payload = hdr+24, worker from table A CA **0x10C23C**
(217 rows, `{u16 key; u16 pad; u32 worker|1}`).

| item | value |
| --- | --- |
| table A row | **94** |
| key | **0x07E7** |
| worker | **CA 0xB58FC** (Thumb) |
| vendor host emitters | `hmac_dfs_cac_start` (0xAD420, body {01}), `hmac_dfs_cac_stop` (0xACD2C) / CAC-timeout (0xAE688) (body {00}), `hmac_config_cac_sync_state` (0xAD1D4, generic 1-B sender) [V\*] |

## T3. The worker CA 0xB58FC: gates, body parse, writes [V]

**Gates before any write** (all three must pass):

| # | gate | site | effect if it fails |
| ---: | --- | --- | --- |
| 1 | `u8[hcc_ctx+17] == 1` | 0xB5900/04 | log 0x619, return 0 |
| 2 | `u32[hcc_ctx+0x4B0] != 0` (per-link) | 0xB5914 | log 0x619, return 0x64 |
| 3 | `CA 0xA4C0E(u32[perlink+0xE20]) != 0` - the **started-module scan**: >= 1 module ctx with `u8[ctx+0x0E]` in **1..3** | 0xB5936 | log 0x61F, return 0x64 |

**Body:** `body[0] = u8(hdr+24)`, 1 byte. **`0x01` = ARM, `0x00` = CLEAR**
(the branch is at 0xB5996: `cmp r4,#0 ; beq 0xB5A34`).

**ARM (body 0x01)** - writes bit0 of six u16 registers of the **band-1 SMAC
sub-base CA 0x4010E000** in this order; every write is `(old & ~1) | bit`:

| register CA | +OFF | bit0 | arm write site |
| ---: | ---: | ---: | ---: |
| 0x4010E000 | +0x00 | **1** | 0xB59E8 |
| 0x4010E004 | +0x04 | **0** | 0xB59D6 |
| 0x4010E008 | +0x08 | **1** | 0xB59C6 |
| 0x4010E0A0 | +0xA0 | **1** | 0xB5A18 |
| 0x4010E0A4 | +0xA4 | **0** | 0xB5A08 |
| 0x4010E0A8 | +0xA8 | **1** | 0xB59FA |

Arm pattern = **{1,0,1,1,0,1} = 0x2D** (bit i = REG[i] bit0). The pre-state is
logged (line 0x24FF) and then overwritten; the **non-bit0 bits are preserved**
(so our boot's `+8 = 0xFFFE` becomes `0xFFFF`, not 1 - see T7).

**CLEAR (body 0x00)** - reads the six back; only when they read the exact ARM
pattern does it write bit0 = 0 on all six (sites 0xB5A8E/+8, 0xB5A9E/+4,
0xB5AAC/+0, 0xB5ABA/+A8, 0xB5AC8/+A4, and the shared store 0xB5A18 for +A0);
otherwise the path is a diagnostic log (line 0x2514) with no write. So CLEAR
works on an armed sub-base, not on a random pre-state.

**Then the per-module context** (lookup CA 0x961AE(u8[hcc_ctx])):

| item | value |
| --- | --- |
| gate u32 | `u32[mctx+0x4A0]` - if nonzero the ARM returns without setting the flag (0xB5B04) |
| flag | `u8[mctx+0x4A5]` **CAC-active**: ARM sets 1 (0xB5B3C), CLEAR sets 0 (0xB5B42) |
| helper object | `u32[mctx+0x4AC]` |
| ARM helpers | `CA 0xF593C(helper_obj, u8[mctx+4])` then `CA 0x5F450(mctx)` (0xB5B20/0xB5B26) |
| CLEAR helpers | `CA 0xF58D8(helper_obj)` then `CA 0x5E95C(mctx)` (0xB5B5C/0xB5B62) |

The six registers are written **before** the mctx/flag logic, so even a
"already active" ARM normalises the register bit0s.

## T4. The frame our driver emits [V + host-built]

The same 29-B **g0-id1 config-event carrier** as the committed 0x00F7 / 0x01F7
frames (12-B HCC header + 8-B token + key u16 + len u16 + 1-B body + 4-B 0xFF
tail):

| offset | bytes (arm) | meaning |
| ---: | --- | --- |
| 0 | `00` | group 0 |
| 1 | `01` | alloc nibble 1, dst 0 |
| 2 | `00` | FA (inherited family byte) |
| 3 | `00` | **FB = field B** = the AP-start module context (the F2 fix) |
| 4..5 | `1d 00` | total = 29 |
| 6..7 | `01 00` | HCC id 1 |
| 8..9 | `00 00` | fc/dst |
| 10..11 | `5a 5a` | transport tag |
| 12..19 | `00 00 00 00 01 00 00 00` | token (word at +16 = 1) |
| 20..21 | `e7 07` | **key 0x07E7** |
| 22..23 | `01 00` | body length 1 |
| 24 | **`01`** (ARM) / **`00`** (CLEAR) | the only varying byte |
| 25..28 | `ff ff ff ff` | inherited tail |

Full arm KAT: `000100001d00010000005a5a0000000001000000e707010001ffffffff`
CLEAR KAT: `000100001d00010000005a5a0000000001000000e707010000ffffffff`

**There are no channel / width / CAC-time / radar fields on the wire.** The
1-byte body is the whole payload: the channel+width were already carried by the
preceding 0x0124 (and 0x01F7 mode) frames, the CAC duration is a **host-side**
constant (T6), and the radar detection itself is firmware-side (the six
registers).

## T5. Driver knob + fail-closed gates [V decode / design]

`dfs_set` module parameter (0444, default 0; `HI5622_DFS_SET_PARM_MAX = 3`):

| value | action |
| ---: | --- |
| 0 | no frame |
| 1 | ARM body {01} |
| 2 | ARM then CLEAR (both frames) |
| 3 | CLEAR only |
| >3 | plan refuses `-EINVAL` before any frame |

Live gates (pre-post probe, read-only; first failing gate aborts the ladder):

| gate | field | pass | fail |
| --- | --- | --- | --- |
| G1 | `mctx` resolved (slot0/base -> CA 0xA4C0E family) | nonzero | -ENODEV |
| G2 | `u32[mctx+0x4AC]` helper object | nonzero | -ENODEV |
| G3 | `u8[mctx+0x11]` band | 1 (5 GHz) | -EINVAL |
| G4 | `u8[mctx+0x0E]` VAP state | 1..3 | -EAGAIN (post the 0x0065 first) |
| G5 | `u8[bandobj+0xEC8]` band-up | 1 | -EAGAIN |
| G6 | `u32[mctx+0x4A0]` gate | 0 (ARM/ARM_CLEAR) | -EBUSY |
| G7 | `u8[mctx+0x4A5]` CAC flag | 0 (ARM/ARM_CLEAR) / 1 (CLEAR) | -EALREADY / -EPERM |
| G8 | six-register bit0 pattern | 0x2D (CLEAR only) | -EPERM |
| G9 | post-read flag | 1 after ARM; 0 after CLEAR | -EIO, sequence aborts |

The parameter also requires `beacon_static=1` (the only ladder with the
0x0065 step that opens G4) and a band-1 AP (band 0 is a clean no-post).

## T6. The vendor CAC timings (host module, not on the wire) [V\*]

From the committed `../wifi-5g-160-dfs/REPORT-DFS.md` (the same frame's host
state machine; **not implemented** by this lane):

| item | value |
| --- | --- |
| CAC time (UNII-2/2e) | **60,000 ms** (band+0xD38) |
| CAC time (5600..5650 MHz TDWR window) | **600,000 ms** (band+0xD3C) |
| non-occupancy period (NOL) | **1,800,000 ms** = 30 min (band+0xD58) |
| CAC tick | 60,000 ms direct path; 63,000 ms x max where max = cac/60000 |
| CAC completion | 0x07E7{00} + `hmac_start_all_bss_of_band` (beacons start) |
| stock 0x07E7 sends in the 104 s boot capture | **3** [V\* wifi-f1-from-capture] - the stock sends it on an ordinary ch48 AP start, not only on a DFS channel |

Our driver posts the SET/CLEAR **frame** only; the timer + NOL + the
start_all_bss completion stay a later rung (a full cfg80211 CAC state machine).

## T7. The bench datum this lane targets (2026-11-26 21:00, pair-A vs pair-B) [V\*]

The committed ledger entry: the band-1 sub-base `[0x4010E000]` reads
`0x404C6000 = 1` on the stock and `0` on our boot; the "0x6008" reads 1 vs
0xFFFE; the manual bar0write3 flip of +0 and +8 did **not** produce the beacon.
Reading the shorthand as **physical addresses** (the bar0read tool address
column): `0x404C60xx = CA 0x4010E0xx`:

| phys | CA | stock | ours (pre-fix) | 0x07E7 ARM writes | note |
| ---: | ---: | ---: | ---: | --- | --- |
| 0x404C6000 | +0x00 | **1** | 0 | bit0 := 1 | matches the arm |
| 0x404C6004 | +0x04 | 0 | 0 | bit0 := 0 | matches the arm |
| 0x404C6008 | +0x08 | **1** | **0xFFFE** | bit0 := 1 -> `0xFFFF` on our boot | non-bit0 bits preserved |
| 0x404C6010 | +0x10 | 0xF5 | 0xF5 | untouched | observation word |
| 0x404C6020 | +0x20 | 0x20 | 0x20 | untouched | observation word |
| 0x404C60A0 | +0xA0 | [U] | [U] | bit0 := 1 | the bench did not probe it |
| 0x404C60A4 | +0xA4 | [U] | [U] | bit0 := 0 | " |
| 0x404C60A8 | +0xA8 | [U] | [U] | bit0 := 1 | " |

Band-1 SMAC producer counter (the rx-smac-complete T5 observable): band1
object 0x175A18 + 0xCC0 = CA **0x1766D8** -> phys **0x4082E6D8**; the 2026-11-26
bench record has the stock's band-1 counter word = `0x0E0F0F` vs ours 0 (the
exact word interpretation is [U]; the committed u8 counter is the low byte).
The fire reads it before/after the ARM and records any movement.

So the **firmware's own ARM writer** is the proper path to the stock's state
(and it sets the three registers the manual flip never touched); the manual
bar0write3 is retired (see FIRE-RECIPE). On our boot expect bit0s
`{1,0,1,1,0,1}` after the ARM - and at +0x08 the raw value `0xFFFF`, not the
stock's `0x1` (the stock's other 15 bits were 0 at arm time; bit0 is the
semantic match).

## T8. Posting order [V decode / I placement]

Vendor host order (the 0x07E7 emitters are the CAC start/stop in the AP-start
path; the stock sends it before the BSS/MAC start):
`0x00F7 (country) -> 0x01F7 (DFS/RF mode) -> 0x0124 (channel) -> 0x07E7 (CAC
arm) -> [CAC timer] -> 0x0127/0x0065/0x0242 (BSS start)`.

Our R5 ladder order (the committed send-5 R5 recipe):
`vap_add -> desc_table(0x00F7) -> dfs_mode(0x01F7) -> ap_chan(0x0124) ->
ap_ssid -> ap_cfg(0x0127) -> vap_start_g0(0x0065) -> band_start(0x0242) ->
ap_vap_start`.

**This lane inserts the 0x07E7 between `vap_start_g0` and `band_start`:**
`vap_add -> desc_table -> dfs_mode -> ap_chan -> ap_ssid -> ap_cfg ->
vap_start_g0 -> **dfs_set(0x07E7)** -> band_start -> ap_vap_start`.

Why not earlier: the worker's gate 3 (the started-module scan CA 0xA4C0E)
needs a module context with state 1..3, and in **our** ladder the only writer
of `mctx[0x0E]` is the 0x0065 VAP-start worker (the vendor reaches that state
via the 0x0069 VAP FSM earlier in its boot, which is why the stock can arm CAC
before its own 0x0065). Why not later: the semantic of the rung is "CAC armed
before the MAC start", matching the vendor.

## T9. Base-relative file pins (this lane's patch) [V]

| item | sha256 |
| --- | --- |
| pristine R5 `hi5622_scan.c` | `86d6c57ecc98...` |
| pristine R5 `Kbuild` | `1bb510853b30...` |
| `patches/0001-dfs-set.patch` (33,313 B) | `5030f4076d03...` |
| patched `hi5622_scan.c` | `08d3aefecfac...` |
| patched `Kbuild` | `8f9766189837...` |
| new `hi5622_dfs_set.c` / `.h` | `f7631f7cbfcd...` / `18f951b90811...` |
| module `.ko` (391,388 B) | `1b47f0daff7e...` |
| module `.text` (159,268 B) | `a8b737a46610...` |
