# wifi-ax-twt-impl - the TWT setup path (the TWT enable config event)

**Lane:** `wifi-ax-twt-impl` (shared task **task-199**), 2026-11-28.
**HOST-ONLY**: implementation + host build + host tests + the fire recipe;
no device contact, no flash, no fire. **The Lead fires.**  No vendor binary,
firmware byte, calibration value, MAC or secret is stored here; the only
bytes committed are *our own host-built protocol bytes* (the g0-id1 key
0x035D frame) - the same rule as the sibling lanes.

**Why this lane exists.**  The 802.11ax advanced lane (task-195) decoded the
vendor AX machinery and found **TWT the one 802.11ax feature the host can
enable end-to-end without an associated client**: the vendor user path
`wal_hipriv_twt_enable` accepts exactly `{1, 3}` and posts a **g0-id1
config event, key 0x035D, one-byte body**; the firmware worker (CA 0x41E5A)
writes the per-module context bits `ctx+0xDA.bit6` (TWT enable) and
`bit7` (TWT required).  This lane implements that path on the merged
64-param module base, fail-closed, with the field-B rule the F2 lane paid
for: **the message field B must be the AP-start module context, never a
hard-coded vendor-wire value**.

Read in this order:

1. **[REPORT.md](REPORT.md)** - the decoded path, what was implemented and
   why, the design decisions, the build, the dmesg contract, the honest
   limits (TWT *agreements* need the RX gate).
2. **[TABLES.md](TABLES.md)** - T1-T10: every address, frame byte, body
   layout, parameter matrix, field-B derivation, plan order, dmesg/probe
   pack, build identity, failure modes.
3. **[FIRE-RECIPE.md](FIRE-RECIPE.md)** - what the Lead does: load
   `twt=1 he_caps=1 beacon_static=1`, the 2.4 GHz HE AP, the `ctx+0xDA`
   read-only probe, the TWT client probe (conditional on the RX gate),
   PASS/FAIL/abort.
4. **[BUILD-NOTES.md](BUILD-NOTES.md)** - the merged-base chain, source and
   artifact pins, reproduce, parameter list.
5. `patches/0001-twt-enable.patch` - the whole change on the pinned merged
   base (6 files: 2 new, 4 edits); `patches/0000-merge-he-kbuild-fix.patch`
   fixes the one Kbuild hunk the HE patch cannot apply on the F2 tree (the
   IMAGE #3 lane did the same edit by hand).
6. `src/` - the portable model (`hi5622_ax_twt.c/h`) + two host tests;
   `tools/` - the build and host-test entry points; `evidence/` - the
   generated outputs.

## Bottom line (details in REPORT/TABLES)

1. **Frame [V]**: g0-id1, 12-B HCC header + 8-B token + {u16 key 0x035D;
   u16 len 1; body[0] = 1 or 3} + 4x 0xFF = **29 B**:
   `000100001d00010000005a5a00000000010000005d03010001ffffffff` (enable)
   and `...03010003ffffffff` (enable+required).  Body values are the vendor
   user-path validator's set: `{1, 3}` only; 2 is not a bit combination and
   is refused.
2. **Fail-closed parameter [V]**: `twt` uint, 0444, default **0** = no
   frame (byte-identical to the merged base).  **1** = TWT enable, **3** =
   enable+required; both require `beacon_static=1` (the vendor AP-start
   ladder), `beacon_vap_start=1`, `he_caps=1` (HE AP advertised) and the
   **2.4 GHz** band; 2 or >3 refused `-EINVAL` before any frame; a 5 GHz AP
   refused `-EOPNOTSUPP` (not bench-proven).
3. **Field B [V]**: derived at post time from the live AP-start module
   context CA (module table slot 0 recipe): `field_b = (mctx - 0x00119758) /
   0xB40`, accepted only for the 19 decoded boot contexts 0..18; CA 0
   (unbound) refuses `-EAGAIN`, any other CA refuses `-ERANGE` - the
   builder never guesses a module selector.
4. **Placement [V]**: the TWT post is the **last** AP-start step, after the
   VAP start (the plan logs it as step `twt`; host-tested order).  The
   post is followed by a bounded (100 ms) read-only read-back of
   `ctx+0xDA` bits 6/7, logged as `expected bits 6/7=0x40/0xc0`.
5. **Host tests [V]**: **128 checks** - model selftest 54/54 (byte-exact
   frames, field-B derivation, full fail-closed matrix, status line), AP-start
   plan order 12/12, independent Python re-derivation 62/62.
6. **Build [V]**: the lane's build script reconstructs the merged 64-param
   base from the committed sibling patches and proves it: base `.text` ==
   IMAGE #3 **494ccd42...**; the TWT module `.text` **162,196 B**
   `1d7d6dda...`, **65 params** (64 + `twt`), **0 warnings**, vermagic
   6.18.60; installed form **362,540 B** sha256
   `ed13145a...c01f93`; **0 global symbols lost**, 5 added.
7. **Not fired [U]**: whether the firmware accepts the frame and sets
   `ctx+0xDA` is decided by the Lead's fire + the read-only probe pack
   (CPU `0x119832` = ctx0+0xDA = **0x40** for twt=1, **0xC0** for twt=3).
   The per-user TWT **agreement** (firmware D2H TWT-sync event) needs an
   associated STA, i.e. the RX gate; the client probe in the fire recipe is
   explicitly conditional and the lane makes no on-air claim.

## Honest limits

* TWT **enabling** is implemented; TWT **agreements / broadcast TWT /
  TWT-required on-air advertisement** are not.  The firmware worker writes
  two per-module context bits; what the firmware then builds into beacons is
  not decoded (the sibling lane grades it [U]) and cannot be claimed here.
* No client can associate while the RX gate is closed (the H2D trace lane is
  decoding it), so the TWT client probe's evidence is the association gate,
  not this lane's frame.  The rung's host-side PASS stands on its own.
* Only the 2.4 GHz HE AP is allowed: it is the bench-proven HE AP shape; the
  5 GHz chain is not on air yet (the band-1 MAC start gap) and TWT there
  would confound two variables.
* The ext-TWT IE key 0x00E9 and the other nine AX config events are decoded
  by the sibling lane but are **not** implemented here (one feature per
  fire).

## Files

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | what/why, design decisions, build, dmesg, limits |
| [TABLES.md](TABLES.md) | T1-T10: addresses, frames, matrix, plan, probes, pins, failure modes |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the Lead's one-shot rung (load -> HE AP -> ctx probe -> client probe) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | merged-base chain, pins, reproduce |
| [patches/0001-twt-enable.patch](patches/0001-twt-enable.patch) | the change on the pinned merged base |
| `src/hi5622_ax_twt.{c,h}` | portable TWT-enable model (kernel/host compile the same code) |
| `src/twt_selftest.c` | host selftest: 54 checks |
| `src/plan_order_test.c` | host plan-order test against `hi5622_scan_logic.c`: 12 checks |
| `tools/build-twt-impl.sh` | reconstruct + patch + build + pin the artifact |
| `tools/run-host-tests.sh` | the no-device test entry point |
| `tools/twt_frame_xcheck.py` | independent Python re-derivation: 62 checks |
| `evidence/` | host-tests, build, module identity, frames, dmesg strings, diff stat |
