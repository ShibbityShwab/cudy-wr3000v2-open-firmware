# wifi-fw-audit lane (shared task task-115)

Audit of the **PD4** firmware patch (the boot-task-epilogue poll stub that makes the vendor
firmware's H2D mailbox consumer serviceable on our clean-room host) and of the vendor-clean
alternatives for the missing mailbox event 0x4C delivery.

Host-only. The vendor firmware blob and the mask-ROM dump are **local evidence only**; this
directory carries offsets, source-window hashes, OUR patch bytes, analysis and tools - never
vendor bytes. The patched image is built locally by the reader from their own device/rootfs and
is never committed.

## Deliverables

* [REPORT.md](REPORT.md) - the audit: PD4 identity/reproduction, minimality (segment/stub
  footprint, per-instruction purpose, variant ranking incl. the rejected one-shot and PD2/PD3
  locations), safety (cave proof, writer exclusion, reference census, entry timing, stub
  invariants, residual risks), the **H1/H2 open question** with the decisive 5-word read-only
  pack, what functionality the stub changes, the **vendor-clean alternatives A1-A6** (mailbox
  doorbell bits, ETE 0x2D/0x2E, spare-iATU event-controller aperture, ROM handlers, other
  wake paths, vendor trace), the formalised recipe/harness artifact, the publication and
  provenance rationale, recommendations and the corrections register.
* [recipes/PD4.recipe.json](recipes/PD4.recipe.json) - machine-readable recipe (extended
  `hi5622-fw-patch-recipe/1` schema; compatible with the owning lanes' builders). Pins the
  source sha256/length, each segment's source-window sha16 and our patched bytes, the patched
  image sha256, and the semantics/safety notes.
* [tools/verify_pd4.py](tools/verify_pd4.py) - independent verification harness: identity,
  full-byte delta, entry/loop decode, callee (wrapper/dispatcher) contract, writer-exclusion
  bounds, whole-image conservative reference census, stub safety invariants and the H1/H2
  observation. 31 checks + 14 selftest checks; reads the local blob in place; refuses to write
  inside the repository.
* [tools/pd4_variants.py](tools/pd4_variants.py) - behavioural variants built at 2-4 bytes from
  PD4 (force the live branch / force the consume-only branch), plus the spec-only
  `PD4-MIN`/`PD4-LITE` shapes and the rejected one-shot design; builds to a non-repo path with
  a JSON manifest. 14 selftest checks.

## Reproduce (host-only)

    PY=.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-fw-audit/tools

    $PY $T/verify_pd4.py --selftest                   # 14/14 PASS (no vendor blob needed)
    $PY $T/verify_pd4.py                              # 31 checks + census; expects the local blob
    $PY $T/verify_pd4.py --json | jq .                # machine-readable result
    $PY $T/verify_pd4.py --no-census                  # fast checks only
    $PY $T/pd4_variants.py --selftest                 # 14/14 PASS
    $PY $T/pd4_variants.py --table                    # ranked variant/option table
    $PY $T/pd4_variants.py --build /tmp/pd4-variants  # diagnostic images + manifest outside the repo

Firmware discovery: `--fw PATH`, else `$HI5622_FW`, else the known local
`rootfs-2.4.15`/`rootfs-2.5.24`/`recovery-extract` copies (walked up from the tool). There is no
network access and no device contact anywhere in these tools.

## Headline audit results (full argument in REPORT.md)

* PD4 rebuilds byte-identically: patched image sha256 `cfe24bfb...037b1`, exactly two segments
  (4-byte branch at file `0x0916A` + 64-byte poll stub at file `0x0C8240`), 68 patched bytes,
  length unchanged; nothing else differs.
* The stub cave (CPU `0x108197..0x108413`) is outside both boot memsets and the
  exception-staging frame; the exhaustive Thumb census finds zero references into the gap.
* The stub only reads mailbox out[0] CA `0x40039010` and calls the firmware's own event-0x4C
  wrapper CPU `0x40294` -> dispatcher CPU `0xC18AC`; every device write is the firmware's.
* **Open (H1/H2):** the stub's static-sentinel comparison may always take the consume-only
  branch (no image writer of CPU `0x10C1B0` was found). Section 5.2 of REPORT.md gives the
  decisive 5-word read-only pack (BAR0 `0x7C41B0`, `0x7D0D68`, `0x2000`) for the next
  attended session.
* **Vendor-clean alternatives:** no host-only path is proven; the only candidate is a spare
  inbound iATU viewport onto CA `0x40160000` plus a one-shot raise of event 0x4C
  (CA `0x40161188` = `0x1000`, mirror `0x40161288`), which is a new device-write class and has
  not been attempted. Until then, patching is required for the closed vendor blob as delivered
  (not fundamental to the hardware).

## Safety rules carried (not weakened here)

* The host never writes CA `0x400392F0`; PD4 does not change that (the firmware dispatcher owns
  that ack register and writes it itself).
* Never `0xCECE` during boot - release-only `0x5A5A`; release/announce discipline unchanged.
* Never read the RC misc window `0x10161000`; the event-controller block CA `0x40160000` is not
  host-visible today and the A3 experiment (if ever run) needs its own gate and recovery card.
* Any PD4/variant fire: standard GP8 golden boot, recovery armed, one frame per idea, no retries,
  read-only cave-integrity check (BAR0 `0x7C0240`) and, after a fault, the staging frame
  (BAR0 `0x6F800C`).
