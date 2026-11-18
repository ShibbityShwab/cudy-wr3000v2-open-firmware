# PIE packet engine draft - patch 015

**Lane:** `driver-pie`, shared task `task-23`, 2026-11-18.
**Status:** HOST-ONLY DRAFT, data path deliberately disabled. Nothing has run on
the board; no boot, image, flash or bench claim. Publication remains NOT READY.

## What is delivered

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/015-net-pie-luofu.patch` | the kernel patch (Kconfig + Makefile + driver + binding), applies on top of 001-014 | `fac5ab382964c4696ba1a305ec4401068edded8372980f0af25bd62ed01446d2` |
| `target-skeleton/docs/incoming/pie/shared.patch` | DTS PIE node + `CONFIG_LUOFU_PIE=y` `[N]` entry for the lead to apply | `d38348f3404faca7c901bebbb13803e253838bfd927efe8b2a3798ae76b61bb4` |

The patch adds:

- `drivers/net/ethernet/hisilicon/luofu-pie.c` (597 lines) - platform driver
  for `hisilicon,luofu-pie`, matching the v250 generation via match data;
- `Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml` (96 lines);
- `CONFIG_LUOFU_PIE` (tristate, `depends on OF`) in
  `drivers/net/ethernet/hisilicon/Kconfig` and its `Makefile` object;
- `shared.patch` replaces the `pie@10a70000` placeholder with the final node
  (four named interrupts, clocks, reset, dma-coherent, still `disabled`) and
  adds the annotated `CONFIG_LUOFU_PIE=y` delta entry.

**No shared repository file was edited by this lane.** The two shared files
(target-skeleton/dts/luofu-r116.dts and luofu/config-6.18) are still the
pre-apply state; `shared.patch` was dry-run, applied and byte-compared against
a copy of exactly that state (see validation below).

## What the driver does (and why it stops where it does)

Probe sequence, in order:

1. variant match data: `hisilicon,luofu-pie` -> v250 generation
   (`hwver = 0x250`, threshold-recovered generation). A variant outside
   `0x200..0x2ff` fails closed (no recovered register model). The vendor
   `hwver` devicetree property is **not** reproduced - the SoC variant is
   match data in the binding-compatible driver, as PIE-DATAPATH.md 11.1
   recommends.
2. resource validation: 64 KiB register window (size checked), `devm` ioremap
   (mapping only - no MMIO access), one unnamed CRG gate clock, one CRG reset
   line, four interrupts looked up **by name** (`ch0-7`, `ch1-6`, `ch2-5`,
   `ch3-4`), `dma-coherent` required, 32-bit DMA mask.
3. coherent ring bookkeeping: 4 RX rings of 1024 descriptors (32 KiB each) and
   4 TX rings of 4096 descriptors (128 KiB each), allocated with
   `dma_alloc_attrs(dev, size, &dma, GFP_KERNEL, 0)` - the vendor's coherent
   model, no cache maintenance. The rings are logged and freed again; no
   descriptor is armed.
4. **refusal**: `-EOPNOTSUPP` with the message
   `data path not started: PIE register/channel inventory is not captured;
   refusing to arm the engine`. No register is read or written, the clock is
   not enabled, the reset line is not toggled, no interrupt is requested and no
   NAPI instance is registered.

The header comment documents the exact next steps: (1) attended read-only
inventory of the PIE window; (2) reconcile and settle the inferred items
(ring-pair word semantics by reading back 0x2004/0x3004/0x3504, RX descriptor
status/length bits, per-vector channel pairing); (3) implement global and
per-channel register init from those values, then RX arming/NAPI/IRQs;
(4) implement the TX producer only after the RX path exists and a
zeroed-offload descriptor is proven to transmit; (5) the switch egress
port/qid map is a separate block needed before any frame can leave a port.

Scaffolding that is compiled but deliberately not wired (marked in the file):
the recovered v250 register offsets (TX/RX ring blocks 0x2000/0x3000 + ch*0x10,
doorbells and completion pointers, RX interrupt status/mask, global RX enable,
RX ring-size config), the 32-byte descriptor field offsets/accessors (RX
address+owner, TX length+address+owner; everything unrecovered stays zero), the
doorbell/kick/enable accessors, and a hard-IRQ handler that encodes the
recorded schedule/mask/ack order and the channel pairing. A poll callback is
deliberately absent: the RX completion decode is not recovered.

## Deliberate decisions

- **Ring geometry is driver data, not DT ABI.** The vendor `rx_ring`/`tx_ring`
  properties are not an ABI and their pair-word decoding ("log2 descriptor
  count" / buffer size / TX channel count) is inferred from the allocation
  arithmetic, not measured. The driver therefore uses documented board
  constants (RX shift 10, TX shift 12, buffer 2048, 4+4 channels) and says so;
  the inventory settles the decoding by reading back the programmed registers.
- **Clock/reset are acquired but not driven in probe.** Enabling the CRG gate
  or toggling the reset is a bring-up operation (and both were only
  host-compiled in patches 004/014), so it belongs to the stage after the
  inventory, together with the first register write.
- **No WoC / no netdev / no exported API.** The upper channel of each vector
  pair is the Wi-Fi offload half and is not initialized or serviced; the
  per-port context table and the in-kernel PIE API are the next stage - an
  export with no consumer would freeze an ABI on unverified behaviour.
- **Coherent-DMA only.** The binding requires `dma-coherent`; the driver
  refuses a node without it. The ACP attribute question (whether the v250 path
  needs software ACP programming) stays open and is not modelled.

## Validation (isolated clone, host-only)

Clone: `/home/shibbityshwab/openwrt-lab-pie` created with
`cp -a --reflink=auto /home/shibbityshwab/openwrt-lab ...`; the shared clone was
not modified. Target tree re-synced to the committed 001-014 state; the old
kernel build directory was removed so `make target/linux/prepare` re-extracted
6.18.60 and re-applied the whole series 001-015 from scratch.

| gate | result |
| --- | --- |
| pristine 6.18.60 dry-run (001-013 + 015 individually; 014 is a dependent follow-up to 004 and is covered by the chained apply) | RC=0 |
| chained apply pristine -> 001..015 | RC=0, 56 patching lines, 0 `.rej`/`.orig` |
| `make target/linux/prepare V=s` | RC=0, 1189 patching lines, 0 `.rej`/`.orig`, patch 015 hunks exact (no offsets) |
| `make target/linux/compile V=s -j16` | RC=0 in 1m50s, **0 warnings** under `CONFIG_WERROR=y`; `CC drivers/net/ethernet/hisilicon/luofu-pie.o` |
| zImage | 3,262,336 B sha256 `5231214b13345a673d0da690a168ae0819ff92dcc3f9c1964732e2605816868d` |
| driver built-in | `System.map`: `luofu_pie_probe`, `luofu_pie_driver`, `luofu_pie_v250`, `luofu_pie_irq_names` |
| TEXT_OFFSET contract | unchanged: `_text = stext = 0xc0608000`, `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`, `DTEXT_OFFSET=0x00608000` |
| resolved config | `CONFIG_LUOFU_PIE=y`, `CONFIG_NET_VENDOR_HISILICON=y`, `CONFIG_WERROR=y` |
| `dt_binding_check` for the new YAML | RC=0 (dtschema 2026.9; yamllint unavailable, that one lint step skipped) |
| board DTS -> DTB with the patched kernel's dtc (pre- and post-shared.patch) | RC=0, **zero warnings** both; integrated DTB 10,193 B sha256 `4e4a03043e7c01448c5a559bc9fb055df0cd7f5d71703a4a0905a4600b8e37a0` |
| `dt-validate` of the integrated DTB | RC=0, no diagnostic names the PIE node |
| config canonical re-derivation (`yes '' \| make kernel_oldconfig` twice) | RC=0 both, byte-identical emissions, symbol set identical to the delivered delta (307 symbols incl. `CONFIG_LUOFU_PIE=y`) |
| checkpatch (`--no-tree -f`) | driver 0 errors/0 warnings/0 checks (597 lines); YAML 0/0/0 (96 lines) |
| `shared.patch` | dry-run RC=0, apply RC=0, produces the delivered files byte-for-byte |
| static checks | no MMIO/DMA-sync in the probe path; no `request_irq`, no `register_netdev`, no `EXPORT_SYMBOL`; no lab/bench/local-path breadcrumbs; no vendor-only DT properties (`rx_ring`, `tx_ring`, `hwver`, `acp-*`, `hsan,*`); no forbidden CA/IAR/misc-window addresses |

Evidence is local-only under
`build/linux-kbuild/waves/2026-11-18-pie/` (gitignored): logs, config
re-derivation runs, DTB/DTS artefacts, patch copies, `SHA256SUMS`.

## Proven vs unverified

**Proven (host-only):** the driver is mainline-API code (platform driver, DMA
API, reset/clk/IRQ frameworks, NAPI scaffolding) that applies to pristine
6.18.60, applies in the OpenWrt clone after 001-014, builds as a built-in with
zero warnings, produces a dt_binding_check-clean binding and a zero-warning
board DTS with the PIE node disabled, and the config delta is
re-derivation-identical. The probe path cannot touch the hardware: it has no
MMIO, no clock/reset operation, no IRQ request and no NAPI registration, and it
frees the rings before returning `-EOPNOTSUPP`.

**Not proven / not claimed - any hardware behaviour:**
- every register offset/field is recovered from a vendor module, not measured on
  this board; the engine's boot state is unknown;
- ring-pair word decoding, descriptor decode, per-channel init words, interrupt
  status/mask semantics and the ACP coherency question are still open;
- the driver has never been instantiated on ARM, never mapped the real window at
  run time and never armed a ring;
- no boot, no image, no install path, no bench run. Enabling the node before
  the attended inventory would only log the refusal - but it must not be
  enabled because even the ioremap of this block is a hardware step the
  project's bench rules gate.

## Next steps (hardware, attended, recovery armed)

1. Read-only inventory of the PIE window as listed in the driver header and
   PIE-DATAPATH.md section 11.2 P3; capture raw values into a wave directory.
2. Reconcile: ring-pair words (read back 0x2004/0x3004/0x3504), RX descriptor
   status/length, the [U] per-channel words, and the per-vector channel pairing.
3. Then (lead decision) enable the node and implement init/arming/NAPI/IRQ from
   the measured values, with the TX producer last and the LSW port/qid map as
   the separate long pole for any traffic claim.
