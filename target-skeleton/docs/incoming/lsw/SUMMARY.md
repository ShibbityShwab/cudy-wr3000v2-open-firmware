# incoming/lsw - internal LSW switch read-only D1b baseline probe (patch 021)

**Lane:** `lsw-init`, shared task `task-47`, 2026-11-18.
**Status:** HOST-ONLY, READ-ONLY DRAFT. Nothing has run on the board; no boot, image,
flash or bench claim. Publication remains NOT READY.

This directory is an **incoming** deliverable: it carries no direct edit to a shared
repository file. The kernel patch itself is already in
`target-skeleton/target/linux/hisilicon/patches-6.18/` (direct lane write scope); apply
`shared.patch` for the DTS/doc changes, then move this SUMMARY to
`target-skeleton/docs/port-notes/lsw/SUMMARY.md` (replacing the patch-`016` D1a note,
which this document supersedes and extends).

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/021-net-lsw-init-luofu.patch` | kernel patch: Kconfig text, new `luofu-switch-regs.h`, extended `luofu-switch.c`, extended binding YAML | `de69296d80f2419b574d3af4ec4a8b83c877a711d278fc71350d14a231fe307e` |
| `target-skeleton/docs/incoming/lsw/shared.patch` | board DTS switch node (17 windows + CRG + port bitmap), `target-skeleton/README.md`, `docs/SKELETON-SMOKE.md`, `docs/TARGET-DESIGN.md`, `docs/LSW-SPEC.md` | `dd071001b1778b47556fe598a49d1f0f07b67d94d35b5b8d3f9e6e91c993aa84` |
| `target-skeleton/docs/incoming/lsw/SUMMARY.md` | this file (moved to `port-notes/lsw/SUMMARY.md` on integration) | - |

No `config-6.18` change is needed: patch `021` adds no Kconfig symbol, and the kernel
config delta is unchanged and idempotent (`CONFIG_LUOFU_SWITCH=y` already exists).

## 2. Lineage: patch 016 -> patch 021

- **`016`** (D1a, task-26) added the `hisilicon,luofu-switch` binding and a probe skeleton
  that validated 15 MMIO windows, two clocks, four resets and the `ethernet-ports` model,
  then refused with `-EOPNOTSUPP`. No register was read or written, and the vendor
  `queue`/`flow` DDR regions and the `port_bitmap` were deliberately not modelled
  (their live values and semantics were then unmeasured).
- **`021`** (this patch, D1b read-only) uses the measured 2026-11-18 baseline
  ([INVENTORY-2026-11-18.md](../../../../docs/INVENTORY-2026-11-18.md) section 3) to turn the
  skeleton into a **read-only baseline probe**. It:
  1. extends the binding/DTS to **17 windows** - the measured live DDR windows
     `queue = 0x80000000/4 MiB` and `flow = 0x80400000/1 MiB` plus the 15 register
     pages - and to the `hisilicon,crg` syscon phandle and the measured
     `hisilicon,port-bitmap = <0xf01 0x1ff0>` cells;
  2. adds `luofu-switch-regs.h`: the measured register map with an evidence grade on every
     offset/value, and the decoded qid-table write geometry documented as the D1b W2 write
     set (not executed);
  3. adds a **powered-state gate**: the probe checks CRG gate word `0x20` bits 0/1
     (read-only through the CRG syscon) and the four reset lines (through the reset API,
     active-low per patch `014`) and refuses **without touching the block** unless the
     measured powered state holds;
  4. reads and validates **30 measured baseline words** (`dp`/`eps`/`eqm`/`fam`/`ips`
     probe-time configuration + PIE channel map) with exact-match/masked expectations;
  5. re-derives both **qid maps** from the port model and compares **384 words**
     (128 table-A + 256 table-B), validating the D1b W2 model read-only;
  6. resolves each `ethernet-ports` child's owning platform device for the switchdev port
     model, but registers **no netdev and no notifier** (the GEMAC port netdev only exists
     behind patch `020`'s attended arm gate, and switchdev registration needs a real port
     netdev);
  7. adds static checks and the full measured-baseline documentation; the Kconfig help text
     now says "read-only probe".
- The driver still contains **no `writel()`**, enables no clock, drives no reset and always
  exits probe with `-EOPNOTSUPP`. The node stays `disabled`.

## 3. Evidence map (each embedded number -> measured artifact)

All values are [V] for the single stock vendor boot of 2026-11-18, read-only inventory run
`20261118T130512Z-inv1-run` (600/600 reads ok, all static reads identical in two passes).
Artifact paths are in the parent repository (gitignored, local-only):
`build/bench-inventory/20261118T130512Z-inv1-run/` (`inventory.json`, `.allow-addresses`,
`followup/`) and the analysis [INVENTORY-2026-11-18.md](../../../../docs/INVENTORY-2026-11-18.md).

| claim in patch 021 | value | evidence |
| --- | --- | --- |
| 15 MMIO windows, size 512 KiB each | `0x11000000`..`0x12480000` | vendor DT + live post-overlay `lsw_dp`/`lsw_pfe` `reg` readback (INV 3.1) |
| DDR `queue` base/size | `0x80000000` / `0x400000` | live post-overlay DT bytes (INV 3.1; base DT says `0x87b00000`, U-Boot overlay rebases) |
| DDR `flow` base/size | `0x80400000` / `0x100000` | same |
| `dp+0x1c/0x64/0x68` | `0x18000` / `1` / `0` | inventory entries 1-3 |
| `eps+0x1008` | `0` | inventory entry 4 |
| `eps+0xB00/B80` ch0-7 channel map | 16 values | inventory entries 5-20 |
| `eqm+0x00/0x18/0x38/0x40/0x48/0xa4` | `1`/`0x80001cc0`/`0x7f0`/`0x80000000`/`0x41100012`/`0x1fc0` | inventory entries 35-41 (idle bits 31/5 checked) |
| `fam+0x504` / `ips+0x00/0x08/0x10` | `0x2000` / `0`/`0x2a`/`0x790011` (bit 16) | inventory entries 59-62 |
| qid table A content | ports 4-12 x 8 slots, qids `0x101..0x148` (0x100 flag) | inventory entries 93-220, 128 words |
| qid table B content | ports 40-43 x 8 slots x 4 words, qids `0x149..0x168` | inventory entries 221-476, 256 words |
| `port_bitmap` | `<0xf01 0x1ff0>` | live DT `followup/port_bitmap.bin`, base DT unchanged (INV 3.3 / LSW-SPEC 6.3) |
| CRG gate word `0x20` bits 0/1 = `lsw_dp_clk0`/`lsw_pfe_clk0` | both set | live CRG readback `0x02f8300b` (INV 4.1; MISC-SOC-BLOCKS 5.2/5.6) |
| CRG reset word `0x30` bits 0/1/3/4 released (active-low) | all set | live `0xfe7fe05b`; polarity per patch `014` (MISC-SOC-BLOCKS 5.6) |
| qid-table write shape (table A word index, table B 16-byte entry, 0x100 flag) | - | vendor code shape ([V] disassembly, LSW-SPEC 7.3); semantics [I] |
| VLAN/L2/classifier/EQM formats | not modelled | [U] by design |

## 4. Evidence grades: proven vs [I] vs [U]

### 4.1 [V] host-validated in this session (isolated clone + pristine tree, no device)

- **Series 001-021 applies** to pristine `linux-6.18.60` in order: **21 patches, 82 files
  patched, 0 failed hunks, 0 `.rej`/`.orig`, RC=0**. Patch `020` (parallel PIE lane) was
  included at its snapshot sha256 `806a113c9dc581ccb3d9248f1298f4cf8a48e1645228b525b1a4853f70bfa6e8`;
  patch `021` applies with a small line offset in `drivers/net/ethernet/hisilicon/Kconfig`
  (020 changes the same file) and no rejects.
- `make target/linux/prepare` RC=0 (1,215 patching lines, 0 rejects) and
  `make target/linux/compile V=s -j10` **RC=0 with CONFIG_WERROR=y, 0 errors and
  0 warnings** in 121 s; zImage 3,279,096 B sha256
  `96b927d35e2e6c6be9980c7683de20c20a288a5df58ddf08ed7fbbe9bb1e92d3`; the new file
  compiles as `CC drivers/net/ethernet/hisilicon/luofu-switch.o`; `System.map`
  carries `luofu_switch_probe`; `CONFIG_LUOFU_SWITCH=y` and `CONFIG_PHYLINK=y`.
- `_text = 0xc0608000`, `swapper_pg_dir = 0xc0604000`; the compressed early-boot code
  materializes `movw #0x8000` / `movt #0x60` (0x00608000) at both head.S sites, so
  the `TEXT_OFFSET` contract is unchanged.
- the kernel config delta is **symbol-identical** to the committed delta after a
  canonical `make kernel_oldconfig` (0 symbol diff; the regenerated body differs only in
  the hand-maintained provenance header) and two consecutive runs emit a byte-identical
  file (sha256 `76aaafc8b51fe94f7989c99572da583467b1ec922e56a82f357c12b83737bc7f`);
  patch `021` adds no Kconfig symbol.
- `dt_binding_check` **RC=0** for the binding (dtschema 2026.9; `yamllint` not installed,
  the tree's optional YAML-lint step is skipped - the same limitation as every previous
  session on this host).
- board DTS compiles with the patched kernel's own dtc to **11,516 B**
  (sha256 `43c1e94bef3e3e35a639523aab57b7d2618f0f02ef26c60747be89e674c24638`) with
  **zero warnings**; `dt-validate` against the generated processed schema produces **no
  diagnostic for the switch node** (10 pre-existing diagnostics remain on unrelated TODO
  nodes; a full-draft `dtbs_check` is still not claimed clean).
- `checkpatch.pl --no-tree -f`: **0 errors / 0 warnings / 0 checks** on the Kconfig hunk,
  the 931-line driver, the 214-line register header and the 223-line YAML.
- **inventory cross-check**: every baseline word, the 16 PIE channel-map words and all
  384 qid-map words were re-derived from the measured `inventory.json` and compared with the
  values the driver embeds - **0 mismatches** (`inventory-crosscheck.log`);
- static checks: no `writel()`/`writew()`/`writeb()` outside the comment that says so,
  no `regmap_write`/`regmap_update_bits`, no `clk_enable`/`clk_prepare_enable`, no
  `reset_control_assert`/`reset_control_deassert`, no netdev/switchdev registration; the
  two DDR windows are `continue`d before the ioremap call; the register read set is a
  subset of the inventory's read set.

### 4.2 [V] but only true of the one measured boot

The 30 baseline values + 384 qid-map words are measured facts for the 2026-11-18 stock
boot. The code uses them as the W1 baseline, which is exactly why the checks are
fail-closed: another boot/unit/configuration that does not reproduce them is refused, not
"handled".

### 4.3 [I] interpretations embedded in the model (not measured)

- the table-A/B port rows belong to LSW port ids 4..12 / 40..43, and the row order is the
  allocator order - the *contents* are [V], the port identity meaning is [I]/[U];
- the contiguous qid allocation ("from qid 1, 8 per port") is the simplest model that
  reproduces the measured contents; the vendor allocator's policy is [I];
- "clock gates first, then deassert resets" ordering comes from the vendor probe shape, not
  from a measured write sequence; the polarity is [V] from the measured final state + patch
  `014`;
- the switchdev-per-port-netdev model and the choice to resolve, but not register, port
  netdevs (the GEMAC port netdev is behind patch `020`'s attended arm gate);
- the DDR regions are engine-consumed memory, not register pages (multiple independent
  observations, LSW-SPEC 5.4); their base/size are [V].

### 4.4 [U]: not claimed, not written

- whether any of the W2 writes has the intended effect (W3-W6, attended);
- EQM indirect command word / queue-table entry layout / classifier (IFC), L2, VLAN table
  formats, CAR/mirror/NAPT - **deliberately excluded from the write set**;
- qid-map class for ports >47 (write shape only partially recovered);
- the meaning of the 40..47 port class and the LSW-vs-MAC port-id mapping;
- the layout inside the queue/flow DDR regions;
- `port_bitmap` semantics (the driver only checks the measured value);
- **whether the block is safe to read in a state other than the measured vendor one** -
  the powered-state gate checks CRG clock/reset state, not the block's internal readiness;
  this is a residual risk for the first attended run and is why the node stays disabled;
- whether our future own-initialised block needs different baseline values (W3 readback).

## 5. Attended checks still owed (the reason for fail-closed)

1. **W3** - first own-kernel write: enable the two CRG gates, deassert the four reset lines
   with the corrected active-low polarity, no LSW register writes; read back the state and
   capture the pre-write register baseline (this is the data the current baseline check
   refuses without).
2. **W4** - one qid-map table-A write (one host queue + one MAC-port row), TX one frame,
   verify the model; power-cycle rollback armed.
3. **W5** - remaining populated rows (table A ports 4..12, table B ports 40..43), default
   forwarding test.
4. **W6** - learning-off/forward-all behaviour and the unknown-unicast default.
5. Read-only inventory of a **second boot/unit** to test whether the W1 baseline values and
   the qid-map contents are stable across boots (the current evidence is one boot).

## 6. Validation transcript (host-only)

Isolated clone: `/home/shibbityshwab/openwrt-lab-lswinit` (copy of the clk-series clone;
the shared clone `/home/shibbityshwab/openwrt-lab` was never written). Pristine tree:
fresh `linux-6.18.60` extraction from the OpenWrt `dl/` tarball. dtschema 2026.9 in
`/tmp/dtsv`; toolchain = the OpenWrt GCC 14.4.0 toolchain already built in the clone.

Evidence (local-only, gitignored, parent repo):
`build/linux-kbuild/waves/2026-11-18-lsw-init/`:
`final-series-validation.log`, `final-series-patches/` (21-patch snapshot + sha256),
`prepare-final-0021.log`, `compile-001-021.log`, `board-dts-final.dtb`,
`dt-validate-final.out`, `dt-validate-switch-node.out`, `apply-series.log`,
`inventory-crosscheck.log`, `static-checks.log`, `config-check.log` and
`021-net-lsw-init-luofu.patch` (same bytes as delivered).

## 7. Safety and publication constraints honored

No device was touched: no UART, SSH, TFTP, flash, bench tool or network access; the only
build activity is host-side kernel/config compile in a clone. The patch contains no vendor
binaries, blobs, extracted DTBs, calibration data or credentials, and no lab/bench
breadcrumbs; the cited values are the structural facts already published in
`INVENTORY-2026-11-18.md`/LSW-SPEC. The driver has no register write of any kind. The
delivered DTS node remains `disabled`, so a normal kernel boot never reaches the read-only
probe either.

## 8. Integrator notes

- Patch `021` **depends on `016`** (same driver/YAML files) and was validated in series
  order after `019` and after the parallel `020` snapshot; it applies with a 6-line offset
  in the hisilicon ethernet Kconfig because `020` edits the same file first.
- `shared.patch` applies to the `opensource/` root (`git apply` / `patch -p1`); it
  touches only the board DTS and docs - no kernel config change.
- Move this `SUMMARY.md` to `target-skeleton/docs/port-notes/lsw/SUMMARY.md`; it replaces
  the patch-`016` note.
- No boot/bench/flash claim of any kind is made here.
