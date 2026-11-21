# Blocker 2 - upstream binding for the loader hand-off window

## Measured contract (from SMP-RE.md section 24)

The stock Cudy/Hisilicon U-Boot `bootfip` refuses to stage the kernel unless
the base DTB carries:

1. `/aliases/console` - resolved to the console UART node;
2. `/reserved-memory/flashinfo@80600000` - compatible
   `"hsan,flashinfo_reserved"`, `reg = <0x80600000 0x4000>`,
   `atag-offset = <0x1000>`.  The loader reads `atag-offset` and computes
   `atag_addr = 0x80601000`.

Without them staging bails ("alias console not found") and CPU1 never enters the
loader pen-poll loop; with them the target image boots 2 CPUs.  This is the
measured release-critical contract.

## Why a binding is needed

An unreviewed vendor compatible/property set inside `reserved-memory` cannot be
merged.  Upstream already has reserved-memory child bindings (ramoops,
shared-dma-pool, vendor SRAM windows), so a SoC-specific child binding that
documents a loader hand-off window is a normal shape.

## Options (recommended order)

- **A (recommended, needs loader check):** add the documented
  `"hisilicon,luofu-flashinfo"` string first and keep the vendor string as a
  fallback: `compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved";`.
  The binding (`hisilicon,luofu-flashinfo.yaml`) documents both forms.  This is
  only safe if the loader's node match still succeeds with the vendor string in
  second position - the decoded loader path is not instruction-level proven, so
  **one boot-test is required** before adopting the DTS delta.
- **B (no DTS change, weakest):** keep the vendor compatible as-is and register
  `hsan` in `Documentation/devicetree/bindings/vendor-prefixes.yaml` with a
  rationale ("HiSilicon luofu vendor loader prefix").  Upstream may reject an
  undocumented vendor prefix or require the vendor to document it.
- **C (DTS change with new compatible only):** drop the vendor string entirely -
  not recommended: the loader contract is measured with the vendor string and
  would need a full hardware re-qualification.

## Draft artifacts

- `hisilicon,luofu-flashinfo.yaml` - proposed
  `Documentation/devicetree/bindings/reserved-memory/` schema.
- `dts-flashinfo-compatible.diff` - option-A DTS delta (compatible list
  change only; `reg`/`atag-offset` unchanged).

## Questions for maintainers

1. Is a SoC-specific reserved-memory child binding acceptable for a
   bootloader-consumed hand-off window, or should this be modelled as a
   `memory-region` consumed by a small "loader hand-off" driver?
2. Is `hsan` acceptable as a registered vendor prefix, or must the documented
   `hisilicon` string be first?
3. Should the binding live under `bindings/reserved-memory/` or under
   `bindings/arm/hisilicon/`?

## Remaining work

- Maintainer direction on shape/prefix.
- If option A is chosen: one hardware boot-test (loader staging must still
  complete); otherwise no DTS change.
- Add the chosen compatible to `07-dts-upstream-bindings.md` and the submission
  package once confirmed.
