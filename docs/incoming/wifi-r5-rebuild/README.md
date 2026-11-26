# wifi-r5-rebuild - the R5 module chain rebuilt from the committed scripts

**Lane:** `wifi-r5-rebuild` (shared task **task-225**) - reboot of the
`wifi-band1-start-send-5` (task-213) deliverable after the host `/tmp` chain
was cleaned (that lane's `build-r5-order.sh` refuses without
`/tmp/r4border-build`). **HOST-ONLY:** no device, no UART, no flash, no vendor
binary in this directory; the rebuilt `.ko` is local evidence only.

## The result in one paragraph

The whole lineage was re-run from the committed build scripts, in order:
E2 base -> beacon5g gate -> r1-band -> desc-table -> dfs-mode -> **F2 field-B
patch** -> 0x0242 band start -> send-2 VAP index -> 0x0065 VAP start (R4) ->
R4b order -> G9/G11 gate inversion -> R5 order. Every stage reproduced its
pinned artifact **byte-exactly**, and the final module is the installed form:

    /tmp/r5-rebuilt/hi5622_wifi.ko
    383,240 B (stripped)   sha256 24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c
    65 params              vermagic 6.18.60 SMP mod_unload ARMv7 p2v8
    .text sha256           a727c339714b8826ea069194a50d376622175ec5adf8e7b537442edc7731d11d

The deployed gate-inverted intermediate (`9bf1d846...`, 382,376 B, 65 params)
was reproduced too, and all five earlier intermediates match their lane pins
(see `CHAIN.md` for the full table, the patch shas and the timing: 129 s for
the whole chain on this host). **No drift, no pin update needed.** The R5
lane's host suites passed again inside the last stage: 295 C + 110 Python
checks, 0 failures. No fire recipe delta: the module is byte-identical to the
one the recipe was written for.

## Deliverables

    /tmp/r5-rebuilt/hi5622_wifi.ko             the rebuilt stripped module (383,240 B, 24f7221b...)
    /tmp/r5-rebuilt/hi5622_wifi.ko.unstripped  the unstripped form (1,424,680 B, 233df986...)
    CHAIN.md                                   the exact script order + every stage's pins + timing
    evidence/                                  the reproduction run, per-stage logs, independent verification

## Read in this order

1. `CHAIN.md` - the chain, the pins, the timing, the limits.
2. `evidence/chain-run.log` - the committed run: stage timestamps, artifact
   pins, final identity (`equal to the installed-form 24f7221b: MATCH`).
3. `evidence/dev-verify.txt` - independent re-verification of the deliverable.
4. `tools/reproduce.sh` - reproduces the whole chain and fails closed on drift.

## Reproduce

    ./tools/reproduce.sh                    # from the present base trees (~130 s)
    ./tools/reproduce.sh --with-e2-rebuild  # + rebuild the stage-1 base tree

The scripts' canonical `/tmp` work paths must not be overridden for the
byte-exact check (the whole-file sha is path-sensitive through build metadata;
`.text` and params are stable). The `.ko` is never committed.

## Honesty / limits

* Host-only: nothing was fired or read from the device; the fire belongs to
  the Lead and follows `../wifi-band1-start-send-5/FIRE-RECIPE.md` unchanged.
* The F2 bench-fire artifact itself was absent locally, so the F2 step enforces
  the six pinned **tree** shas, not that artifact's sha.
* Pre-beacon lineage below stage 1 is not rebuilt by this lane; each sibling
  script pins its own base artifact and fails closed on drift.
