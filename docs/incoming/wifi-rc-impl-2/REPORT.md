# REPORT - wifi-rc-impl-2: the rate_ctl SET rate_mode auto retry with the corrected C2 (RC-2)

**Lane:** rc-impl (shared task **task-200**), 2026-11-28. **HOST-ONLY**: this lane
built, host-tested and documented; it never contacted the router. **The Lead
fires.** All inherited safety rules apply (recovery door armed; UART read-only;
no flash; never CA 0x400392f0; never read RC misc 0x10161000; one idea per boot;
never rmmod after the CPU release).

## 0. Result in one paragraph

The 2026-11-27 RC R1 fire built the EXACT decoded 39-B `rate_mode auto` frame but
its announce stalled at `out[0]=0x8` and the chip faulted (ledger 07:00). The
fw-h2d-stall lane (task-181) decoded that signature: `out[0]=0x8` is the
**unconsumed SR-announce bit** and the dispatcher clears it *before* dispatch, so
the stall was the **previous** dispatch faulting; the bench-proven fault of
exactly that class is a TXQ worker dereferencing a zero `ctx+0x190` (PC
0x81040, mask 0x10). The old RC recipe wrote `h2d_ctx_create=3` (its own
comment said "type 1 = the bench default"); the clean calibration ladder used
**type 1** and read back **`ctx4+0x190 != 0`** before `h2d_txq_sync72`. RC-2
therefore (a) re-implements the rate-control SET on the **current base** - the
IMAGE #3 merged driver source tree, 64 params - with the **corrected C2
precondition enforced in the driver**, (b) keeps the decoded alg_cfg frame and
the D2H g1 id3/syn 0x0101 capture, and (c) ship the three host suites, a clean
reproducible cross build and the corrected fire recipe. **Host gates: frame KAT
37/37, C2 order 25/25, matrix 76/76, 0 warnings/errors, build reproducible.**

## 1. The corrected C2 precondition (why the old fire stalled)

Chain of evidence:

| # | fact | source |
| --- | --- | --- |
| 1 | The RC R1 fire wrote `echo 3 > h2d_ctx_create`; its comment claimed "type 1 = the bench default". | `../wifi-rate-ctl/FIRE-RECIPE.md` R1 |
| 2 | `out[0]=0x8` is the unconsumed announce bit; the dispatcher clears it **before** the handler runs, so the stalled frame was never dispatched - a previous dispatch faulted. | `../wifi-fw-h2d-stall/REPORT.md` sec 1.3 |
| 3 | The known fault of that class: a TXQ frame whose field-B context exists but `ctx+0x190 == 0` -> `ldr r3,[ctx+0x190]` (=0), `strb r1,[r3,#0x4c]` -> data abort PC 0x81040, fault addr 0x4C, mask 0x10. | `../wifi-irq-unmask/REPORT-PD4.md` sec 6.4 |
| 4 | The clean calibration ladder: `h2d_ctx_create` **type 1** -> read back `ctx4+0x190 != 0` -> `h2d_txq_sync72` -> the alg frames. | `../wifi-proto-seq/recipes/cal-alg-ladder.json` P0-P3; `../wifi-bench-tooling/tools/sequences/ladder-c-ordered.json` |
| 5 | The post-create PACK the bench validated: `ctx190` (BAR0 0x7D45E8) nonzero, `ctx[0]` low byte = 4, the **type-1 second allocation** `ctx+0x434` (BAR0 0x7D488C) nonzero. | `../wifi-bench-tooling/tools/sequences/ladder-c-ordered.json` post-create-pack |

So the "corrected C2" is: **type-1 context-create -> a read-only attestation of
the type-1 signature -> the ordered TXQ sync -> then (and only then) the
rate/alg config events.**

## 2. The driver-enforced attestation (new code, current base)

`h2d_send.c` now records the create type and performs a **read-only**
attestation before any TXQ frame, using the existing BAR0 region-5 firmware-RAM
window (`hi5622_fw_ram_read`; no register write, no new MMIO):

    module-4 context CA  0x11C458  (= boot table 0x119758 + 4*0xB40)
    ctx+0x190            BAR0 0x7D45E8   != 0   (the context-create allocation)
    ctx+0x434            BAR0 0x7D488C   != 0   (the TYPE-1 second allocation)
    ctx[0] low byte      0x00000004             (the created module id)

`ok = ctx190 != 0 && ctx0_lo == 4 && ctx434 != 0` - the bench PACK's exact
type-1 signature.  The result is carried into the portable gate
(`struct h2d_live.ctx_create_type/ctx_attested`) and enforced there:

* `h2d_gate_check()` (portable) now refuses a TXQ frame with:
  * `ctx_create_required` - no context-create this load;
  * `ctx_create_type1_required` - the create was type 2 or 3 (the old fire's type 3);
  * `ctx190_attest_required` - the attestation did not hold;
  before the existing `txq_phase_not_ready` / `txq_already_sent` gates.
* a new RO attribute `h2d_ctx_attest` exposes the attestation for the Lead's
  ladder readback:
  `corrected-C2 attestation: valid=1 rc=0 ctx_create_type=1 ctx4=0x0011c458 ctx0_lo=4 ctx190=0x... ctx434=0x... reads=N expected=...`
* `h2d_status` gains a `corrected-C2:` line (recorded type / attestation /
  values / read count); a non-type-1 create logs a warning at write time.

The rate_ctl surface (`hi5622_rate_ctl.c`) refuses **before any device write**
unless the recorded facts are all true (see sec 5), and logs a fresh
read-only attestation at send time as bench evidence.

**Finding for the image lanes:** the IMAGE #3 payload loader still writes the
old value - `payload/usr/sbin/hi5622-load` line ~364: `sysfs_write "$D/h2d_ctx_create" "3"`
(and `payload/etc/config/hi5622` documents "h2d_ctx_create=3").  With RC-2's
gate that load would refuse the TXQ sync (fail-closed) and therefore refuse the
rate SET.  **Any image that should carry the RC retry must pass 1, not 3.**

## 3. The rate_ctl SET rate_mode auto frame (byte-exact)

The decoded command (wifi-rate-ctl lane): alg_cfg SET, cfg id **401**
(rate_mode), value[0] = **0** (auto), blob
`91 01 00 0d 00 00 00 00 01 00 00` (11 B), on the bench-proven HCC group 0 /
id 2 / key 0x0101 config-event carrier.  The complete 39-B wire:

    00 01 00 04 27 00 02 00 00 00 5a 5a    12-byte HCC header
    00 00 00 00 00 00 00 00                8-byte token (zero)
    01 01                                  key 0x0101
    0b 00                                  blob length 11
    91 01 00 0d 00 00 00 00 01 00 00      the alg_cfg blob
    00 00 00 00                            inherited 4-byte zero tail

Carrier fields: group 0, alloc/dst `0x01`, field A 0, **field B = 4** (the
module context the type-1 create made), total 0x27, id 2, field C 0, dst 0,
tag 0x5a5a.  The wire is built by the new portable
`hi5622_rate_ctl_wire.c` (using the base's exact-frame library
`h2d_frames` and its transport/firmware/handler validation) and posted
through the base's single fail-closed wire path `hi5622_frame_post()`
(`allow_frames` budget, inter-frame gap, out[0] consumption poll, no retry) -
the same path the F2 5 GHz/DFS frames use.  The 39-B frame decodes clean on the
current base: group 0 / id 2, handler CA 0xd6069 present.

## 4. D2H g1 id3 / syn 0x0101 capture

`hi5622_hcc_deliver()` now calls `hi5622_rate_ctl_rx()` for every received
frame; it filters group 1 / id 3 and parses the decoded config-sync layout
(syn_id 0x0101 at payload+8, body length at +10, body = request blob with
`dev_ret` updated for SET / `value` filled for GET).  The answer is recorded in
the read-only `rc_status`/`rc_log` attributes:

    last_rsp syn=0x0101 cfg=401 type=SET pkt=13 dev_ret=0 need_w4=1 body_len=11 value_len=1 value=00
    last_wire_len=39 last_wire=<the 39 bytes> out0_after=0x00000000
    last_rsp_body=<the request blob with dev_ret updated>

The parser (portable, `hi5622_rate_ctl_logic.c`) refuses malformed answers
(wrong group/id/syn, short body, length mismatch) - host-tested.

## 5. The rate_ctl fail-closed matrix (portable, host-tested)

`rc_check()` order: disabled -> **corrected-C2 precondition** -> budget -> wire
shape -> per-command allowlist.  Decisions:

| condition | decision |
| --- | --- |
| `rate_ctl=0` (default) | `disabled` |
| no TYPE-1 create this load | `ctx_type1_required` |
| create type-1 but `ctx190` attestation absent/failed | `ctx190_attest_required` |
| attested but the ordered `txq_sync72` not committed | `txq_sync72_required` |
| frame budget exhausted (`allow_frames`, capped at 16) | `budget` |
| blob length != 10+value / > 270 | `bad_len` |
| cfg id/type not on the decoded allowlist | `id_not_allowed` |
| value outside the decoded enum | `bad_value` |
| raw probe without `rate_ctl_arbitrary=1` | `arbitrary_off` |

The module parameters remain fail-closed: `rate_ctl=0` (surface off),
`rate_ctl_fb=4` (module context), `rate_ctl_arbitrary=0` (no raw probes).
No register write is added anywhere; the only device effect is the decoded
config-event frame through `hi5622_frame_post()`.

## 6. Host tests (recorded in evidence/)

    ./tools/run-host-tests.sh
    == rc2_frame_selftest    37 passed, 0 failed (the 39-B byte-exact KAT,
                                carrier fields, base validator, 7 refusals)
    == rc2_c2_order_selftest 25 passed, 0 failed (the REAL patched
                                h2d_gate_check + rc_precond_check: every
                                order/type/attestation branch, DUP/BUDGET/OUT0)
    == rc2_matrix_selftest   76 passed, 0 failed (blob KATs incl. the 11-B
                                SET auto blob, builder refusals, g1-id3 parser,
                                the fail-closed matrix incl. the precond, names)

The frame test compiles the patched tree's `h2d_frames.c`/`h2d_send_logic.c`
directly, so it tests the real gate code, not a model.  The C2-order test's
`h2d_live` vectors are the kernel's own inputs (the attestation is a kernel
read-only read whose RESULT is the vector; the read itself is not re-executed
on the host).

## 7. Build (evidence/build-module.txt)

    base: IMAGE #3 merged driver source tree (64 params) - evidence/base-pin.txt
    + patches/0001-rate-ctl-2.patch (71,453 B, deterministic)
    = module 1,519,716 B unstripped (.text sha256 ba81e4b3...), activity form
      401,884 B sha256 3d43a3cd..., 67 params (64 + rate_ctl/fb/arbitrary),
      vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211, license GPL,
      0 warnings, 0 errors, 0 conflict markers; two consecutive builds produced
      identical identities.

The artifact is LOCAL EVIDENCE ONLY, never committed.  It is NOT stripped into
the image-bake installed form here (the image lane owns that transformation).

## 8. Evidence grading

* **[V]** the bench-proven contract facts cited in sec 1 (type-1 ladder,
  ctx190/ctx434/ctx0 signature, 39-B carrier, g1 id3 sync shape) - each has a
  committed lane source above; **[V]** the byte-exact wire KAT and the base
  validator acceptance (host, deterministic); **[R]** the D2H answer fields
  (decoded layout, not captured here).
* **[I]** the ground-truth firmware-side effects: that a bench boot with the
  corrected C2 makes the 39-B frame consumed and answered; the ordering above
  is the decoded + bench-proven one, but RC-2 itself has not fired.
* **[U]** none added by this lane.
* Limits inherited: only the 270-byte calibration blobs are bench-proven on
  this carrier; the short 11-B blob remains inferred (the fw-h2d-stall decode
  exonerates the framing - the stall was the preceding C2).  The R1 fire is the
  discriminator (FIRE-RECIPE.md).

## 9. Reproduce (host-only)

    cd opensource/docs/incoming/wifi-rc-impl-2
    python3 tools/make-patch.py --verify     # base pin + deterministic patch
    ./tools/run-host-tests.sh                # 3 suites (frame/C2/matrix)
    ./tools/build-rc2.sh                     # + cross build + artifact record

Base tree: the IMAGE #3 merged driver source tree (see BUILD-NOTES.md); the
base is local scratch, its identity is pinned in evidence/base-pin.txt.

## 10. Files

    patches/0001-rate-ctl-2.patch   8 base edits + 6 new files (71,453 B)
    src/hi5622_rate_ctl.{c,h}       kernel glue: sysfs, precond, D2H capture
    src/hi5622_rate_ctl_logic.{c,h} portable: blobs, parser, matrix, precond
    src/hi5622_rate_ctl_wire.{c,h}  portable: the exact 39-B wire builder
    tools/make-patch.py             base pin + patch apply/verify (+ --regen)
    tools/run-host-tests.sh         3 suites
    tools/build-rc2.sh              patch + tests + cross build + artifact record
    tools/rc2_frame_selftest.c      byte-exact frame KAT
    tools/rc2_c2_order_selftest.c   the corrected C2 order on the real gate
    tools/rc2_matrix_selftest.c     fail-closed matrix (from the RC-1 selftest)
    evidence/                       base pin, patch apply, test + build records
