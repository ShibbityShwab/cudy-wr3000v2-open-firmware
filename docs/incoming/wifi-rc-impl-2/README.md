# wifi-rc-impl-2 - the rate_ctl SET rate_mode auto retry on the current base (RC-2)

**Lane:** rc-impl (shared task **task-200**), 2026-11-28. **HOST-ONLY**: this
lane builds, host-tests and documents; it never contacts the router. **The Lead
fires.** Same safety rules as the rest of the project (no flash, no UART
transmit, never CA 0x400392f0, never RC misc 0x10161000, one message per boot,
never rmmod after the CPU release).

## The one-paragraph result

The 2026-11-27 RC R1 fire was an honest negative: the rate_ctl module built the
EXACT decoded 39-B \`rate_mode auto\` frame but the announce stalled at
\`out[0]=0x8\` and the chip faulted. The fw-h2d-stall lane (task-181) decoded that
signature: \`out[0]=0x8\` is the *unconsumed SR-announce bit*, cleared *before*
dispatch - so the stall was the **previous** dispatch faulting, and the
bench-proven fault of exactly that class is a TXQ-sync worker dereferencing a
zero \`ctx+0x190\` (PC 0x81040, mask 0x10). The old RC recipe wrote
\`h2d_ctx_create=3\` (its own text said "type 1 = the bench default"); the
calibration ladder that was proven clean used **type 1** and read back
**\`ctx4+0x190 != 0\`** before \`h2d_txq_sync72\`. This lane therefore:

1. re-implements the rate-control SET on the **current base** (the IMAGE #3
   merged driver source tree; \`evidence/base-pin.txt\`) with the **corrected C2
   precondition enforced in the driver**: the ladder now refuses a TXQ frame
   unless the context-create was **type 1** AND its read-only attestation passed
   (ctx4+0x190 != 0, ctx[0] low byte = module, the type-1 second allocation
   ctx+0x434 != 0). A read-only \`h2d_ctx_attest\` attribute exposes the same
   attestation for the Lead's readback.
2. keeps the decoded alg_cfg surface (SET rate_mode auto = the 39-B g0 id2 /
   key 0x0101 frame carrying the 11-byte blob \`91 01 00 0d 00 00 00 00 01 00 00\`)
   and the D2H **g1 id3 / syn 0x0101** config-sync capture (the rate-selection
   observable).
3. gates every rate_ctl write on the corrected precondition (fail-closed:
   no type-1 create -> no attestation -> no TXQ -> no rate SET).
4. ships the host tests (byte-exact frame, C2 order against the patched gate,
   fail-closed matrix), a clean cross build, and the corrected fire recipe.

## Directory

    README.md          this file
    REPORT.md          the corrected-C2 analysis, the code change, the evidence grading
    BUILD-NOTES.md     the base pin, the patch chain, the build + artifact identity
    FIRE-RECIPE.md     the corrected attended ladder (Lead fires)
    patches/           patches/0001-rate-ctl-2.patch (deterministic, generated)
    src/               the new portable + kernel sources added by the patch
    tools/             make-patch.py, run-host-tests.sh, build-rc2.sh, selftests
    evidence/          base pin + host-test outputs + build artifact record (text only)

## Base

The current base is the **IMAGE #3 merged driver source tree** (64 params):
F2 5 GHz chain + RX-watermark + BA skeleton + HE caps (see
\`../wifi-image-3/build-evidence/module-build/source-chain.txt\`). Its source pins
are in \`evidence/base-pin.txt\`; \`tools/make-patch.py\` and
\`tools/build-rc2.sh\` refuse a drifted base. The base tree itself is local
scratch (the project's .ko artifacts are never committed): stage it per the
IMAGE #3 module-build note, set \`BASE_TREE\` if it is not
\`/tmp/wifi-image-3-module\`.

## Reproduce

    python3 tools/make-patch.py --verify     # regenerate the patch (deterministic)
    ./tools/run-host-tests.sh                # 3 host suites (frame/C2/matrix)
    ./tools/build-rc2.sh                     # stage base + patch + tests + cross build

No device contact, no image, no flash. The .ko is local evidence only.

## The fire

See \`FIRE-RECIPE.md\`: load with \`rate_ctl=1\` + the 2.4 GHz AP ladder, then
\`h2d_ctx_create=1\` -> \`cat h2d_ctx_attest\` (valid=1) -> \`h2d_txq_sync72=1\` ->
\`echo auto > rc_rate_mode\` -> the \`rc_status\` rsp readback (g1 id3 syn 0x0101,
cfg 401, dev_ret 0). One idea per boot; a stall means capture + power-cycle,
never retry in place.
