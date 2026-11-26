# REPORT - the dfs_set +0x4A0 relax (the register-only arm)

**Lane:** wifi-5g-dfs-relax (shared task **task-230**, 2026-11-26).
**HOST-ONLY.** Input: `../wifi-5g-cac-context` REPORT/TABLES (task-229, commit
0c76fd8) and the R2 fire refusal of `../wifi-5g-dfs-order`. The host changes
are [I] (clean-room logic); the firmware facts are [V] (pinned image decode)
and the refusal/pre-state are [B] (bench).

## 1. The defect this lane fixes

The R2 fire (`wifi-5g-dfs-order`) refused the 0x07E7 ARM at the host pre-gate:

    cfg80211: dfs_set pre: mctx=0x........ ... b4a0=0x0104fd90 ...
                (u32[mctx+0x4A0] gate is nonzero - the worker would return without setting the flag)
    cfg80211: dfs_set REFUSED (-16): ...

The pre-gate read `u32[mctx+0x4A0]` and returned `-EBUSY` when it was
non-zero, assuming the worker would refuse. The decode says the opposite: the
worker's six-register write happens BEFORE the +0x4A0 load; a non-zero word
takes the silent-success branch 0xB590C (`movs r0,#0`) and the worker returns
0. The bench's +0x4A0 is the CAC context CA 0x5F450 (called from the 0x0065
VAP-start worker at 0x464B6) - i.e. **the normal state after the 0x0065**. The
ARM frame therefore never reached the firmware, and the six band-1 SMAC
sub-base bits stayed 0 on our boot.

## 2. The two arm kinds (firmware model, [V] wifi-5g-cac-context T5)

| | full arm | register-only arm |
| --- | --- | --- |
| precondition | `u32[mctx+0x4A0] == 0` (no CAC context) | `u32[mctx+0x4A0] != 0` (the 0x0065 allocated the CAC context) |
| the six band-1 SMAC sub-base bit0 writes CA 0x4010E000/+4/+8/+A0/+A4/+A8 | **yes** (0xB59C6..0xB5A18, pattern {1,0,1,1,0,1} = 0x2D) | **yes** (same instructions, unconditional) |
| CA 0xF593C / CA 0x5F450 helpers | run | **skipped** |
| `u8[mctx+0x4A5]` CAC flag | set 1 | **left 0** (the worker returns 0) |
| worker return | 0 | 0 (silent success) |

The CLEAR branch (0xB5A34) is unchanged: it needs `b4a0 != 0` **and** the flag
== 1, then runs CA 0xF58D8/CA 0x5E95C and clears the flag. On an
ARM-with-context boot the flag is 0, so the CLEAR path is a silent no-op / the
host CLEAR gates refuse.

## 3. The host change (exact spec from `../wifi-5g-cac-context/FIRE-RECIPE.md` sec. 2)

| # | spec | implementation |
| ---: | --- | --- |
| 1 | ARM/ARM_CLEAR: when `p->b4a0 != 0` do not return `-EBUSY`; set `ctx_present = 1` and keep going; replace the verdict string | `hi5622_dfs_set_precheck()` is now `(struct hi5622_dfs_set_probe *p, u8 mode)`, sets `p->ctx_present = p->b4a0 != 0u ? 1u : 0u`, keeps the mctx/helper/band/state/band-up/CAC-flag gates. Verdict: "the 0x0065 already allocated the CAC context (b4a0 != 0) - the ARM is the register-only arm (...)". Probe fill also derives `ctx_present` so the `dfs_set pre:` line prints it before the precheck call. |
| 2 | `postcheck`: accept `regs_pattern == 0x2D`; `cac == 1` only when `!ctx_present`; `cac == 0` with `ctx_present`; print `ctx_present=`/`arm_kind=register-only\|full` | `hi5622_dfs_set_postcheck(const struct hi5622_dfs_set_probe *p, u8 mode)`: ARM/ARM_CLEAR -> require `hi5622_dfs_set_armed(p->regs_pattern)` then `ctx_present ? cac==0 : cac==1`; CLEAR -> `cac==0`. New `hi5622_dfs_set_arm_kind()` returns "register-only"/"full"/"?". The `dfs_set arm post:` dmesg line prints both. |
| 3 | probe pack + log: add `b49c`/`b4a4`/`b4a8`; one line before the vap_start_g0 post: `dfs_ctx pre0x0065: b49c=.. b4a0=.. b4a4=.. b4a8=..` | the probe reads the four words (+ `b4ac`/`cac`); the `dfs_set pre:` line prints them + `ctx_present=`; a new read-only `dfs_ctx pre0x0065:` line runs immediately before the 0x0065 frame post (E1). |
| 4 | no new params, no frame byte change, no new position; CLEAR stays fail-closed | patch touches 3 files, no `module_param` added (66 params), the 29-B KAT bytes are unchanged, the ladder order `0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242` is unchanged; CLEAR/ARM_CLEAR gates unchanged (ARM_CLEAR now refuses the CLEAR *after* a passing register-only ARM because the flag is 0 - by design, use `dfs_set=1`). |

## 4. The postcheck contract (what the host grades)

| mode | `regs_pattern` | `ctx_present` | `cac` | verdict |
| --- | --- | ---: | ---: | --- |
| ARM / ARM_CLEAR | 0x2D | 0 | 1 | 0 (full arm PASS) |
| ARM / ARM_CLEAR | 0x2D | 1 | 0 | 0 (register-only arm PASS) |
| ARM / ARM_CLEAR | != 0x2D | any | any | -EIO (the worker did not write; fail-closed) |
| ARM / ARM_CLEAR | 0x2D | 0 | 0 | -EIO (full arm should have set the flag) |
| ARM / ARM_CLEAR | 0x2D | 1 | 1 | -EIO (register-only arm should not set the flag) |
| CLEAR | any | any | 0 | 0 |
| CLEAR | any | any | 1 | -EIO |
| any | - | - | - | -EINVAL for NULL probe / mode 0 / mode > 3 |

## 5. Host test coverage (host-only, no device)

* [src/dfs_set_selftest.c](src/dfs_set_selftest.c) - 101 C contract checks:
  plan/frame KATs unchanged; the ARM is accepted with `b4a0 != 0` and
  `ctx_present` recorded; every other gate still refuses; the verdict string
  swap; all postcheck rows of sec. 4; `arm_kind` strings; status line.
* [tools/dfs_relax_test.py](tools/dfs_relax_test.py) - 55 independent Python
  checks on the patched tree: contract intact (KATs re-derived from the
  `#define`s, 66-param count, no new `module_param`), relax contract, probe
  pack/format strings, the pre-0x0065 line position, the two postcheck call
  sites, the ladder order, fail-closed CLEAR/ARM_CLEAR text.
* the sibling `../wifi-5g-dfs-order/tools/dfs_order_test.py` still passes
  **46/46** on the patched tree (order regression).

## 6. Limits / honesty

* Host-only: nothing here was fired. The A-rung is the Lead's fire
  ([FIRE-RECIPE.md](FIRE-RECIPE.md)); its PASS is unmeasured.
* The "silent success" and the six-register write are [V] static decode plus
  the R2 bench pre-state; the register-only arm's **actual** register effect is
  what E3/E4 of the fire recipe measure.
* The C-NOG0 control behaves differently from the cac-context sketch: the
  fail-closed pre-gate refuses the ARM **before the wire** on
  `mctx[0x0E] == 0` (-EAGAIN), so the worker's gate-3 no-op is not reached
  from this module. The control still proves the fail-closed path (no frame,
  registers stay 0). Documented in the fire recipe.
* `u32[mctx+0x4A8]` has no writer in the image ([U], bench 1); the probe only
  records it. The +0x4A4 high half is the 0x00A3-class config length.
* No vendor binaries, no firmware bytes, no secrets: the module lives in
  `/tmp` only; the repo carries the patch, tests and docs.
