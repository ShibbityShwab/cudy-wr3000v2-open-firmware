# DTS-0001 - classification and final regeneration contract (2026-11-27)

**What this file is.** The final classification of the upstream review-form devicetree entry
**DTS-0001**, its relation to the kernel series, why the frozen artefact is stale, and the
exact contract for regenerating it. Host-only; no DTS was changed by this lane.

## 1. What DTS-0001 is

DTS-0001 is the **devicetree series entry** that ships with the kernel review form: it adds
`arch/arm/boot/dts/hisilicon/luofu-r116.dts` (the board DTS) and the `dtb-$(CONFIG_ARCH_LUOFU) +=
luofu-r116.dtb` Makefile entry, compiled against pristine `linux-6.18.60` after the kernel
series (it uses the CRG clock-ID header added by kernel patch 0004).

It is **not** optional packaging: two nodes are a loader contract consumed by the stock U-Boot
before the kernel runs, and SMP (kernel patch 0018) has no consumer without them:

* `/aliases/console = &uart1` (plus serial0/serial1 aliases) - the bootfip staging contract;
* `/reserved-memory/flashinfo@80600000` with `compatible = "hsan,flashinfo_reserved"`,
  `reg = <0x80600000 0x4000>`, `atag-offset = <0x1000>` - the CPU1 loader hand-off staging.

Without them the stock loader's hand-off aborts and CPU1 never enters the pen-poll loop. This
was the 2026-11-21 SMP root cause (parent-repo `docs/SMP-RE.md` section 24; recorded in
`upstream/series-6.18/06-dts-series-dependency.md`). **0018 and DTS-0001 ship together.**

## 2. Current artefact and its classification

| field | value |
| --- | --- |
| file | `upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` |
| sha256 (frozen 2026-11-22) | `33956380db9ca5c7b91404596d6078d2f35e4a349978e41e4a1c5d5b9b5155fa` |
| payload | 699-line filtered board DTS + Makefile entry, 712 patch lines |
| compiled DTB (recorded) | 12,697 B sha256 `288a5c3f...`; padded 12,704 B |
| derivation | single-source `target-skeleton/dts/luofu-r116.dts` -> `tools/dts-upstream-filter.py` (strips the release-line-only W23 thermal delta) -> `tools/gen-dts-series-patch.py` |
| status | **DRAFT DTS REVIEW ENTRY - STALE, MUST REGENERATE** (classification below) |

### Why it is stale (verified this session, host-only)

1. **It still carries the retired `wifi-pcie-hog`.** The frozen DTS-0001 contains one
   occurrence of `wifi-pcie-hog`; the current single-source DTS does not (grep re-run
   2026-11-27: no `wifi-pcie-hog`, no `reset-gpios`; `luofu-r116.dts` is 702 lines, sha256
   `df0d8e591329bf370727c1cc22b9adf08c388f193dd7cd9d60034534d7486557`). The hog was retired on
   2026-11-24 with the committed `013a` driver pulse (GP1 line): a hog that releases the
   endpoint lines before the driver asserts them defeats the reset pulse, so it is not just
   cosmetic - the current hardware-validated boot has no hog.
2. **The planned 0013 fold adds a new DT property.** v2 folds `013a` into 0013 as DT data
   (`reset-gpios` on the PCIe RC node or a board-level reset-controller child). The DTS and the
   PCIe binding must carry it; the frozen DTS-0001 and binding do not. This is a real
   active-DTS change, not a prose sync.
3. The 2026-11-22 form predates the 2026-11-24 target fixes generally (recorder, selector path,
   header truth); those are target files rather than DTS content, but the DTS series is
   regenerated from the same single source and must move with it.

**Classification: STALE - DO NOT SEND.** It is regenerable without new hardware **except** for
item 2, which needs one attended boot after the DTS change (project rule: no active-DTS change
is accepted from host checks alone - the 2026-11-21 active-DTS cleanup is the recorded
counter-example in `upstream/series-6.18/07-dts-upstream-bindings.md` section 1).

## 3. What must be in the regenerated entry

**Include** (from the single source):
* the loader hand-off contract nodes (above) - measured from the stock loader, release-critical;
* serial/UART, GMAC + MDIO + PHYs + internal switch + ethernet ports, FMC/SPI-NAND + the 17
  fixed partitions (with the read-only bdinfo partition), PCIe RC0/RC1 with the identity
  `dma-ranges`, pinctrl, thermal zone `cls0`, CRG syscon + gates + clock tree, watchdog,
  sysctrl, restart, boot-selector;
* the 2026-11-21 hardware-verified board features: gpio-keys-polled (reset/WPS) and six
  `oem:green:*` gpio-leds (ordinary mainline bindings, ride in the board DTS);
* the new `reset-gpios` property from the 0013 fold (once the fold content is decided).

**Exclude deliberately:**
* the release-line-only W23 thermal delta (filtered by `dts-upstream-filter.py`; the in-series
  thermal binding 0012 documents only the 0x100 window);
* Hi5622 Wi-Fi endpoint children: no binding and no upstream driver, PCI endpoints bind by ID.
  The proposal file [../wifi-upstream-target/dts-endpoint-proposal.dtsi](../wifi-upstream-target/dts-endpoint-proposal.dtsi)
  is documentation only and must not be added to the active DTS until the Wi-Fi decision;
* any vendor-extracted DTS content and any vendor-derived register table - none may appear;
* bench-only nodes/properties (attended gates, mirror workaround, etc.).

## 4. Regeneration contract (host-only, exact order)

```sh
cd opensource/upstream/series-6.18
python3 tools/dts-upstream-filter.py \
    ../../../target-skeleton/dts/luofu-r116.dts /tmp/luofu-r116.upstream.dts
python3 tools/gen-dts-series-patch.py /tmp/luofu-r116.upstream.dts \
    dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch
# compile + contract + pad checks (uses pristine linux-6.18.60 + the kernel series):
#   dtc -I dts -O dtb -W=1 ; grep the two nodes in the decompiled DTB ;
#   tools/pad-dtb.py ; dt_binding_check for the PCIe binding
python3 tools/gen-series.py --check          # kernel form must stay clean
sha256sum dts-series/0001-*.patch            # record the new hash
```

Then refresh, in the same pass: `MANIFEST.sha256`, `01-commit-messages.md` (the DTS commit
message and the 0013 message must describe the reset-gpios fold and the retired hog),
`06-dts-series-dependency.md` (new hash/payload), `release-line/manifest.tsv` +
`INCLUDED.txt` (add 013a, 55 -> 56) and `validation/sha256sums.txt`.

**Acceptance gates for the regenerated entry:**
- [ ] patch applies to pristine `linux-6.18.60` (after the kernel series) and compiles;
- [ ] DTB contains `/aliases/console` and `/reserved-memory/flashinfo@80600000` with the
      measured values (loader-hand-off contract check);
- [ ] dtc `W=1` clean; PCIe binding `dt_binding_check` RC=0 with the `reset-gpios` schema;
- [ ] one attended boot of the resulting DTS on the unit (recovery armed, 2 CPUs, PCIe both
      endpoints, NAND/UBI, serial console) - required because the hog retirement + reset-gpios
      are active changes;
- [ ] DTS-0001 hash and payload length updated in the submission package and manifest;
- [ ] no vendor-derived node, table or extracted DTS content in the patch.

## 5. What a reviewer sees (and what must not be claimed)

The entry is a normal "add board support" DTS for an ARM32 Hisilicon SoC, with two
board-specific contracts (loader hand-off nodes, PCIe endpoint reset lines) documented in the
commit message. The review rationale for the `hsan,` compatibles and the flashinfo reserved
memory is in `03-rationale-tree-placement.md` and `07-dts-upstream-bindings.md`.

Must not be claimed: that the DTS makes Wi-Fi work (it has no endpoint nodes and the series
has no Wi-Fi driver), that the internal switch/PIE behave as a supported network path (RFC,
gated), or that the board DTS is upstream-reviewed. The hardware record is a bench notebook
record, not a Tested-by ([../../../upstream/series-6.18/submission/TESTED-BY-POLICY.md](../../../upstream/series-6.18/submission/TESTED-BY-POLICY.md)).

## 6. Evidence

| claim | evidence |
| --- | --- |
| DTS-0001 = 712 lines, `33956380...`, contains the hog | `upstream/series-6.18/dts-series/0001-...patch`; grep re-run 2026-11-27 |
| current single source 702 lines, `df0d8e59...`, no hog/reset-gpios | `target-skeleton/dts/luofu-r116.dts`; `sha256sum`/grep re-run 2026-11-27 |
| loader hand-off nodes are release-critical for SMP | `upstream/series-6.18/06-dts-series-dependency.md`; parent-repo `docs/SMP-RE.md` section 24 |
| active-DTS changes need a boot test; host checks are not sufficient | `upstream/series-6.18/07-dts-upstream-bindings.md` section 1 (image `6f19c6d6` regression) |
| hog retired; 013a pulse is the validated path | parent-repo ledger 2026-11-24 (GP1), `STATUS.md` 3c; [../wifi-upstream-target/REPORT.md](../wifi-upstream-target/REPORT.md) 3.1 |
| L1 = 55, no 013a | `release-line/INCLUDED.txt`, `manifest.tsv`, grep re-run 2026-11-27 |
