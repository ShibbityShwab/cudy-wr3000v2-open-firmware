# FIRE-RECIPE - wifi-5g-cac-context: the ARM-with-context rung (+ the corrected baseline)

**Delta of** `../wifi-5g-dfs-order/FIRE-RECIPE.md` (R2). **HOST-ONLY lane:
nothing here is fired by this lane; the Lead fires.** One rung per fresh
power-cycled boot; never re-post a consumed frame. Written against the
`wifi-5g-dfs-order` module contract (66 params, `dfs_set` 0..3) plus the
small host change below.

## 1. Why a new rung

The R2 refusal was client-side: the dfs_set pre-gate refused on
`u32[mctx+0x4A0] != 0`. The decode
([REPORT.md](REPORT.md) sec. 3, [TABLES.md](TABLES.md) T5) shows the firmware
ARM with a present context is **silent success after writing the six band-1
SMAC sub-base registers**. The rung posts the same 29-B frame with the
pre-gate relaxed and grades the six registers, not the CAC flag.

## 2. The host change (the implementing lane applies it; not this lane)

In the dfs_set module (files `hi5622_dfs_set.c/.h` + the `hi5622_scan.c`
post site, per `../wifi-5g-dfs-set/patches/0001-dfs-set.patch`):

1. `hi5622_dfs_set_precheck(&p, mode)`: for ARM / ARM_CLEAR, when
   `p->b4a0 != 0` **do not return -EBUSY**; set a new probe field
   `ctx_present = 1` and keep going. Replace the verdict string
   "u32[mctx+0x4A0] gate is nonzero - the worker would return without setting
   the flag" with "the 0x0065 already allocated the CAC context (b4a0 != 0) -
   the ARM is the register-only arm (six SMAC regs; the CAC flag is skipped by
   the worker and the worker returns 0)".
2. `hi5622_dfs_set_postcheck` (the `arm post:` reader): accept
   `regs_pattern == 0x2D`; require `cac == 1` only when `!ctx_present`;
   with `ctx_present` require `cac == 0` (the flag is NOT set) and PASS.
   Also print `ctx_present=` and `arm_kind=register-only|full` in the dmesg line.
3. Extend the probe pack/log with `b49c` (+0x49C), `b4a4` (+0x4A4) and `b4a8`
   (+0x4A8) next to the existing `b4a0`/`b4ac`/`cac`. Add one line **before** the
   vap_start_g0 post: `dfs_ctx pre0x0065: b49c=%08x b4a0=%08x b4a4=%08x b4a8=%08x`.
4. No new params, no frame byte change, no new position. CLEAR stays
   fail-closed as implemented (on this rung it is a silent no-op: flag 0).

## 3. The A-rung (ARM-with-context), fresh power-cycled boot

Load the R5 ladder + `beacon_static=1 vap_start_g0=1 dfs_mode=1 dfs_set=1 band_start=1`
(the dfs-order R2 insmod line) with the changed module. Expected dmesg order:

    dfs_ctx pre0x0065: b49c=00000000 b4a0=00000000 b4a4=00000000 b4a8=00000000
    frame post 'vap_start_g0' ... (40 B, consumed)
    dfs_set pre: ... b49c=0x0104fexx b4a0=0x0104fdxx b4a4=0x00a3xxxx b4a8=0x... cac=0
                 ctx_present=1 (...register-only arm...)
    frame post 'dfs_set_arm' ... (29 B, key 0x07E7, body 01, consumed)
    dfs_set arm post: regs_pattern=0x2d cac=0 ctx_present=1 arm_kind=register-only (PASS)
    dfs_mode posted -> band_start ...

Probes:

| id | probe | expect | pass rule |
| ---: | --- | --- | --- |
| E1 | the four words BEFORE the 0x0065 (new line) | all 0 | hard: else the pre-state model is wrong |
| E2 | the four words after the 0x0065 (`dfs_set pre`) | b4a0 = the 0x0104FD90 class; b49c = b4a0+0x118; b4a4 = 0x00A3xxxx class; b4a8 record | hard: b4a0/b49c relationship |
| E3 | `dfs_set arm post` | **regs_pattern = 0x2D, cac = 0** | hard PASS |
| E4 | phys `0x404C6000/04/08/A0/A4/A8` (the six) | bit0 {1,0,1,1,0,1}, other bits preserved; record raw | hard PASS |
| E5 | `0x404C4220` band-1 MAC enable | 0x1039 (unchanged) | record |
| E6 | RF mode `P+0xF04` / R image (dfs-order D7/D8) | 3 / 0x01800060,0x03E6138A | record (late 0x01F7) |
| E7 | page golden `$D/boot` | 0x86807FEF, state 1, mask 0 | hard ABORT if not |
| E8 | `iw dev OBS scan freq 5200` | ch40 AP | optional jackpot, not PASS-required |

**PASS** iff E1 is zero, E2 shows the bench-class context, E3 shows 0x2D with
cac=0, E4 shows the 0x2D bit0 pattern (the firmware writer fired), and E7 is
golden. **FAIL** if E3 regs != 0x2D (record E2/E3; do not hand-write the
registers). **ABORT/power-cycle** on any page change, UART exception, failed
frame consumption or an unexpected AP-start abort.

## 4. Corrected baseline / negative controls

* **C-BASE (replaces the retired R1a zero-baseline):** same module,
  `dfs_set=0`, `vap_start_g0=1`. The four words after the 0x0065 are
  **NONZERO** (the bench values). The clean zero state is E1 only. Rationale:
  [TABLES.md](TABLES.md) T7 - the 0x0065 AP branch calls CA 0x5F450.
* **C-NOG0 (gate-3 negative):** `vap_start_g0=0 dfs_set=1`. The 0x07E7
  worker's gate 3 (CA 0xA4C0E, `mctx[0x0E]=0`) returns 100; the six
  registers stay 0 and the module's postcheck must FAIL-closed (no band start
  on an unverified writer). One fresh boot; not required for the A-rung PASS.

## 5. The stock-side read-only rung (S-rung; later session, stock pair A)

Resolves the [U] of REPORT sec. 7 (why the stock reads zero at AP-up). Read
only via devmem on the stock boot (`mctx` from the hcc ctx slot as the
driver does): the four words at (a) 2.4 GHz AP up, (b) 5 GHz AP up,
(c) immediately after a `hostapd restart`/`wifi reload` of the 5 GHz interface,
(d) 1 s and 10 s after (c). Also read the band-1 sub-base bit0 each time.
Expected if the g0 0x0065 ran for the 5 GHz VAP: nonzero after (c); the
observed all-zero at (b) then needs a CLEAR/stop explanation. No writes, no
frames - pure devmem reads; keep the stock recovery door armed as usual.

## 6. Safety / invariants

* One rung per fresh power-cycled boot; a consumed frame is never re-posted.
* No manual register writes (the manual sub-base flip is retired - 21:00 bench:
  it aligned the words but no beacon).
* The Page-Golden gate runs before any probe interpretation; a page change =
  ABORT + power cycle.
* The 29-B frame is byte-identical to `../wifi-5g-dfs-set` KATs; only the host
  gate policy and the expected post-state change.
* The beacon (E8) is not guaranteed by this rung: the decode says the six
  sub-base bits are the radar/detect enable the firmware writes; the 5 GHz
  TBTT/SMAC init is a separate step (`../wifi-fw-tbtt-arm` lineage).
