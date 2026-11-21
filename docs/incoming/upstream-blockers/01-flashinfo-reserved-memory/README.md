# Blocker 2 (last remaining) - upstream binding for the loader hand-off window

**STATUS: draft binding + final host-side option; ONE loader boot-test still
required before adoption.**  No hardware claim is made here.

## Measured contract (SMP-RE.md section 24)

The stock Cudy/Hisilicon U-Boot `bootfip` refuses to stage the kernel unless
the base DTB carries:

1. `/aliases/console` - resolved to the console UART node;
2. `/reserved-memory/flashinfo@80600000` - compatible
   `"hsan,flashinfo_reserved"`, `reg = <0x80600000 0x4000>`,
   `atag-offset = <0x1000>`.  The loader reads `atag-offset` and computes
   `atag_addr = 0x80601000`.

Without them staging bails ("alias console not found") and CPU1 never enters the
loader pen-poll loop.  This is measured and release-critical.

## Final recommended option (host-side decision, not hardware-tested)

Use a documented `hisilicon` compatible first and keep the vendor string as a
fallback:

    flashinfo@80600000 {
        compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved";
        reg = <0x80600000 0x4000>;
        atag-offset = <0x1000>;
    };

The proposed binding is `hisilicon,luofu-flashinfo.yaml` (draft under
`Documentation/devicetree/bindings/reserved-memory/`); its `compatible`
schema accepts the new string, the vendor string, and the pair in that order.
The vendor string stays in the DTS as the loader-compatibility fallback; it is
retained only because the stock loader's match path has not been re-tested with
the new string first.

Fallback plan if the loader test fails: keep the vendor compatible first/only
and register `hsan` in `Documentation/devicetree/bindings/vendor-prefixes.yaml`
(or ask the maintainers whether the vendor-only reserved-memory child can be
documented as the loader contract); do not rename the vendor string in the DTS.

## Exact hardware test required (one variable, not yet run)

1. Start from the current final board DTS (681 lines, sha256 `9d96ac31...`;
   image baseline c3ef2a50 lineage).  Change ONLY the flashinfo `compatible`
   list to the recommended pair; no other active DTS delta.
2. Build the same target image (the normal image recipe, including
   `Build/luofu-pad-dtb`), record its sha256 and the DTB size/sha.
3. Install on the unit with the verified path (pair b, selector 0x11 is the
   current known-good state) and capture UART read-only (no typing).
4. **PASS criteria:** the stock `bootfip` prints its staging lines
   (`alias console=...`, `serialnr->high/low=...`,
   `atag_addr=0x80601000`), the kernel boots, CPU1 comes up (2 CPUs), no
   data abort, no fallback to stock pair A.
5. **FAIL criterion:** staging bails/falls back to stock; then revert to the
   vendor-first/only compatible and pursue the fallback plan rather than
   guessing.

This tests the loader's compatible match behavior, not the kernel.  It is the
only hardware item left for the blocker list.

## Draft artifacts

- `hisilicon,luofu-flashinfo.yaml` - proposed reserved-memory child binding.
- `dts-flashinfo-compatible.diff` - the compatible-list delta (the only
  change in the hardware test).

## Maintainer questions

1. Is a SoC-specific reserved-memory child binding acceptable for a
   bootloader-consumed hand-off window?
2. Is the `hisilicon` compatible first acceptable with the vendor string kept
   as fallback, or should the vendor-only form be documented instead?
3. Where should the binding live (`bindings/reserved-memory/` or
   `bindings/arm/hisilicon/`)?
