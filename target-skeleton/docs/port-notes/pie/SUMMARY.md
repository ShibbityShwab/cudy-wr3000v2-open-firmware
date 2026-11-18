> Integration note: this directory is an **incoming** deliverable. Apply
> `shared.patch` (below) to the current repository and commit it with
> `patches-6.18/020-net-pie-datapath-luofu.patch`; the SUMMARY is the durable
> record of the lane.

# incoming/pie - PIE datapath + GEMAC glue (patch 020)

**Task:** task-46 (parallel port-lane, agent `pie-datapath`). **Date:** 2026-11-18.
**Scope:** implement the PIE packet-engine datapath and the GEMAC glue from the measured
2026-11-18 baseline: coherent DMA descriptor rings for the two active RX/TX channels,
channel/IRQ-mask sequencing from the measured values, RX enable via `0x3200` bit 0, a
TX/poll draft, DT ring tables, a fail-closed arm-gated probe, and the GEMAC netdev/phylink
front end that forwards transmit to the engine. Validate the `001`-`020` series
host-side in an isolated `openwrt main` clone. **No device was touched** (no router, UART,
TFTP, flash or bench tool); no PIE register has ever been written and nothing was booted.
Host-only, evidence-graded.

---

## 1. Deliverables

### 1.1 New kernel patch (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/020-net-pie-datapath-luofu.patch` | `806a113c9dc581ccb3d9248f1298f4cf8a48e1645228b525b1a4853f70bfa6e8` |

Bare unified diff (`--- a/...` / `+++ b/...`, no `diff --git`), same style as the
accepted `001`-`019`; applies with `patch -p1` and `git apply --check` in the
OpenWrt platform-patch order. It modifies five files and adds one:

- `drivers/net/ethernet/hisilicon/luofu-pie.c` - ring engine (rewritten/expanded from
  the patch-`015` scaffolding);
- `drivers/net/ethernet/hisilicon/luofu-pie.h` (new) - internal front-end/engine
  backend interface;
- `drivers/net/ethernet/hisilicon/luofu-gmac.c` - phylink/netdev front end over PIE;
- `drivers/net/ethernet/hisilicon/Kconfig` - `LUOFU_GMAC depends on LUOFU_PIE` +
  `select PHYLINK`; both help texts updated;
- `Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml` - required
  `hisilicon,rx-ring`/`hisilicon,tx-ring` ring tables, draft/arm-gate description;
- `Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml` - description
  updated to the front-end/engine split.

### 1.2 `shared.patch` (apply to the current repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/incoming/pie/shared.patch` | `11ac62043da090132aa7356a966d2df3a9c27f3dd53d62982b902bd126a451b2` |

- **SHARED_LINES_JSON lines, six files, git-style** (`diff --git a/... b/...`).
- Verified: `patch -p1 --dry-run` and `patch -p1` RC=0 against a byte-copy of the
  current repository files; applying it in a throwaway copy reproduces all six files
  byte-for-byte (sha256 comparison PASS, see section 4.8).
- Files and what they carry:
  - `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: `CONFIG_PHYLINK`
    resolves `m` -> `y` (patch `020` makes the built-in GEMAC `select PHYLINK`);
    [N] provenance comments updated for `004`-`020`, the GEMAC and PIE [N] blocks
    rewritten to the patch-`020` status, and the REMOVED/DOWNGRADED note adjusted. No
    new symbol is introduced by the patch itself.
  - `target-skeleton/dts/luofu-r116.dts`: the PIE node gains the measured ring tables
    `hisilicon,rx-ring = <0x8 0x600 0x8 0x600>` and
    `hisilicon,tx-ring = <0xc 0x2>` plus the corrected status comment; the GEMAC node
    comment describes the patch-`020` front end. Both nodes stay `disabled`.
  - `target-skeleton/README.md`: new status paragraph, a patch-`020` table row, the
    config-`6.18` and DTS rows updated.
  - `docs/SKELETON-SMOKE.md`: new "PIE datapath session" section with the exact
    commands/results and the proven-vs-unverified split.
  - `docs/TARGET-DESIGN.md`: the 6.2 GEMAC row, the 7.1 GEMAC row, the internal-switch
    row and the "explicitly not met" paragraph updated to the arm-gated engine + front
    end.
  - `docs/PIE-DATAPATH.md`: section 11 status block, the P2/P4 plan rows and TL;DR
    item 8 updated to the implemented-but-arm-gated state (RX decode still [U]).
- Post-apply hashes (sanity check for the lead):

  | file | sha256 |
  | --- | --- |
  | `target-skeleton/dts/luofu-r116.dts` | `b68a35bd27ad045f5732dd0b8591edc3f34d532ca60a2aa27d293b2d6015c114` |
  | `target-skeleton/target/linux/hisilicon/luofu/config-6.18` | `64227116886297a89f60ae685cf4fd734656983d436f4225a4cdab3b9e357bdf` |
  | `target-skeleton/README.md` | `872ac91f69be4a3d03bc216c3461141fb9d5bf13c94eacff12cde05a93067c95` |
  | `docs/SKELETON-SMOKE.md` | `1567b5d0763e1d29f772c239a31d7c0020e1c3c710429570847d84df3dc5eefb` |
  | `docs/TARGET-DESIGN.md` | `5f39c5da647bfcc18a3c403b159fd57dc10f7e2d63353ff333569c63140ac771` |
  | `docs/PIE-DATAPATH.md` | `9824c794b9ca46629017193f21448a8b3cd420a92987663f9e5fb5380a183066` |

- Shared files were **not** edited in the working tree; only the new patch file and this
  incoming directory are untracked additions.

---

## 2. Design

### 2.1 Arm gate (the safety property)

PIE is the block whose premature access has hung the AHB bus on this board. The patch
therefore keeps two independent gates on the data path:

1. the board DTS keeps the PIE and GEMAC nodes `disabled`; and
2. the PIE driver's probe **always** validates the DT/resources/ring tables and then
   refuses with `-EOPNOTSUPP` before the first register write unless
   `luofu_pie.arm=1` is on the kernel command line.

The armed path is the only code in the tree that writes PIE MMIO, enables the PIE clock,
releases its CRG reset, requests the four IRQ vectors or registers NAPI. After programming
it reads every programmed register back and calls `luofu_pie_halt()` (mask all, disable
RX, stop NAPI, free rings, assert reset, gate clock) if anything does not match. The GEMAC
front end registers a netdev only when the PIE backend is armed; otherwise it refuses
(`luofu_pie_probed()` true) or defers (`probed()` false, PIE not yet probed).

### 2.2 Ring engine

- Geometry comes from the new binding tables, using the measured live board values:
  **2 RX channels** of log2(256) 32-byte descriptors with 1536-byte buffers and
  **2 TX channels** of log2(4096) descriptors. These are the post-U-Boot-overlay values
  the stock vendor driver parsed (INVENTORY-2026-11-18 section 2); the base-DT 4+4
  configuration is not used.
- Rings are `dma_alloc_attrs()` coherent allocations (32-byte descriptors, no cache
  maintenance), RX buffers are coherently mapped skbs with the vendor headroom
  (`netdev_alloc_skb_ip_align(size + 128)`), and the DMA mask is 32-bit.
- Global/per-channel init follows the recovered v250 `reg_init` sequence; per-channel
  ring programming writes base/config and the RX size word (`0x3504 = buffer >> 7`,
  [I] decode, [V] readback correlation). All eight IRQ masks are set first (masked), then
  only the active wired channels are unmasked, then the global RX enable bit is set.
- RX: the four IRQ vectors use the recovered `chN,M` channel pairing; the hard handler
  masks, write-1-clears and schedules NAPI. The NAPI poll consumes completions by the
  RX read pointer, re-arms the descriptors and counts the completions as drops - it does
  **not** decode the still-[U] status/length fields, so no frame is handed to the stack.
- TX: `ndo_start_xmit` -> `luofu_pie_xmit()` linearizes the skb, maps it coherently,
  writes only the recovered length/address/owner descriptor fields (the offload words stay
  zero), advances a free-running write pointer and rings the per-channel doorbell. TX
  completion is poll-only (the vendor has no TX IRQ) and is reclaimed at the next
  transmit. The channel is picked from the current CPU modulo the TX channel count ([I]
  mirror of the vendor per-CPU choice).
- No WoC channels/rings, no GRO ring, no ACP programming, no per-port context table yet.

### 2.3 GEMAC glue

The MAC owns the net_device and a 6.18 phylink front end (the vendor layering,
PIE-DATAPATH section 2.1). `probe()` validates reg/clocks/resets/phy-mode and the PIE
backend gate, enables the MAC gate clock, releases the two CRG resets, creates phylink and
registers the netdev. `ndo_start_xmit` forwards to the PIE backend; `ndo_open/stop`
start/stop phylink and connect/disconnect the PHY. The only MAC register writes are the
measured 1 Gbps link-up words (PORT_MODE `0x010c0008` bracketed by MODE_CHANGE_EN
`0x01b4` bit 0 + MAC enable) and link-down; for other speeds the driver logs that no
measured encoding exists rather than guessing. `mac_config()` is a deliberate no-op (the
lab driver measured an AHB hang when touching the block from phylink's config path).

---

## 3. Evidence map (writes: what is backed, what is inferred)

| item | evidence |
| --- | --- |
| 2 active RX/TX channels; TX config 0xc, RX config 0x8; live ring tables `<0x8 0x600 x2>` / `<0xc 0x2>` | [V] INVENTORY-2026-11-18 section 2 (read-only inv1 run + DT follow-up) |
| register offsets/blocks (0x2000/0x3000 ring blocks, 0x0300/0x0400 IRQ status/mask, 0x3200 enable, 0x3504 size, per-channel 0x0180/0x0200/0x0380, 0x3210/0x3290/0x3310, 0x3204/0x3208, 0x3404/0x3408) | [V] vendor `hi_kpie.ko` v250 ops table/reg_init disassembly (PIE-DATAPATH sections 3-4); the measured post-boot values are [V] INVENTORY section 2 |
| descriptor shape: 32 bytes; RX addr +4 / owner byte+3 bit2; TX len +0 / addr +4 / owner byte+3 bit2; every other field zero | [V] PIE-DATAPATH section 5.2 (normal non-TSO branch only) |
| global RX enable `0x3200` bit 0; mask 1 = masked, status W1C bit 0 | [V] recovered functions + [V] measured post-boot state |
| 0x1000 bit 0 clear / bits 6-8 set; per-channel defaults 1/1/1 and 3/2/8; 0x3204=0, 0x3208=0x7d | [V] recovered reg_init + [V] measured on all eight slots |
| 0x3504 = buffer size / 128 | [I] decode (measured 0xc == live size word 0x600 >> 7; not the descriptor-ring bytes) |
| descriptor count = 1 << shift; second RX ring word = buffer size | [I] allocation-arithmetic inference, live values [V] |
| free-running 16/32-bit doorbell counters; initial full-ring push; owner bit cleared by engine on completion | [I] recovered code shape + readback correlation; not observed under traffic |
| RX completion status/length decode | [U] - **not implemented**; poll recycles buffers and counts drops |
| 0x3404 per-channel field, 0x3700 GRO size, 0x2200 DFX, WoC rings/channels, ACP attributes | [U] - deliberately not programmed (0x3700 left untouched because the board has no GRO ring) |
| GEMAC PORT_MODE 0x010c0008 at 1 Gbps, MODE_CHANGE_EN 0x01b4 bit 0 bracket, MAC enable bit; link-down CTRL=0 | [V] measured on the live vendor system, executed by the out-of-tree lab driver (`lab/luofu-gmac/luofu-gmac.c`, ETHERNET-BRINGUP-FRAMING.md) |
| all other MAC registers | [U] - not written |

---

## 4. Validation (isolated clone, host-only)

Clone: `/home/shibbityshwab/openwrt-lab-pie-datapath` created with
`cp -a --reflink=auto /home/shibbityshwab/openwrt-lab ...`; the shared clone was not
modified. Target tree re-synced to the committed `001`-`020` state; the old kernel build
directory was removed so `make target/linux/prepare` re-extracts 6.18.60 and re-applies the
whole series from scratch.

| gate | result |
| --- | --- |
| pristine 6.18.60 chain apply 001-020 | RC=0; 0 `.rej`; 2 `.orig` from patch `008`'s pre-existing pristine-tree hunk offsets (none from `020`); applied driver files byte-identical to the generated sources |
| `make target/linux/prepare V=s` | RC=0 (1,211 patching-file lines, patches `001`-`020`), no `.rej`/`.orig` in the extracted tree |
| `make target/linux/compile V=s -j16` | **RC=0**, 0 warnings / 0 errors in the whole log under `CONFIG_WERROR=y`; `luofu-pie.o` 76,740 B, `luofu-gmac.o` 45,492 B |
| zImage | 3,276,656 B sha256 `dfc7b753d34ad443a76ed3ffe60e77d7e63a28b771546ba0dccc4a40a9515a5a` (build-time dependent) |
| PIE/GEMAC built-in + linked | `luofu_pie_probe`, `luofu_pie_driver`, `luofu_pie_xmit`, `luofu_pie_armed`, `luofu_pie_probed`, `luofu_pie_rx_poll`, `luofu_gmac_probe`, `luofu_gmac_driver`, `phylink_create`, `phylink_of_phy_connect` in `System.map` |
| TEXT_OFFSET contract | unchanged: both `.head.o.cmd` files carry `-DTEXT_OFFSET=0x00608000`; `_text = stext = 0xc0608000`, `swapper_pg_dir = 0xc0604000` (load/entry `0x80608000`) |
| resolved config | `CONFIG_LUOFU_PIE=y`, `CONFIG_LUOFU_GMAC=y`, `CONFIG_PHYLINK=y` (was `m`), `CONFIG_NET_VENDOR_HISILICON=y`, `CONFIG_WERROR=y` |
| config canonical re-derivation (`make kernel_oldconfig`) | two runs RC=0, emitted delta byte-idempotent (`76aaafc8...`, 300 symbols), symbol set identical to the committed annotated delta (`SYMBOL_SETS_IDENTICAL`), annotated delta merges identically through `scripts/kconfig.pl`; only change vs 001-019 is `CONFIG_PHYLINK` m -> y |
| `dt_binding_check` for the two changed YAMLs | RC=0, both examples `DTEX`/`DTC [C]` clean; yamllint only missing (pre-existing tree skip) |
| board DTS -> DTB with the patched kernel's dtc | cpp RC=0, dtc RC=0 with **stderr 0 bytes**; DTB 11,430 B sha256 `d3a9dd86...`; pie rings `<0x8 0x600 0x8 0x600>` / `<0xc 0x2>`, both nodes `disabled` |
| `dt-validate` of the integrated DTB | RC=0 with only the same five pre-existing findings as earlier lanes; none names PIE/GEMAC |
| checkpatch (`--no-tree -f`) | 0 errors / 0 warnings / 0 checks: pie driver (1,346 lines), header (22), gmac driver (372), PIE YAML (128), GEMAC YAML (100) |
| static safety checks | no RC-misc `0x10161000` / forbidden CA `0x400392f0` / ACP-gate cells; no vendor-only DT properties; no lab/breadcrumbs; the arm path is the only PIE MMIO write path and it is unreachable without `luofu_pie.arm=1` |
| `shared.patch` reproduction | `patch -p1 --dry-run` and apply RC=0 against a byte-copy of the current repository files; all six files reproduce byte-for-byte (sha256 match) |

---

## 5. Proven vs unverified

**Proven (host-only):** the patch applies to pristine 6.18.60 and in the OpenWrt platform
order; the `001`-`020` series prepares and compiles with zero warnings under
`CONFIG_WERROR=y`; the load/entry `0x80608000` contract is unchanged; the bindings pass
`dt_binding_check` and the board DTS compiles with zero dtc warnings; the config delta is
canonically re-derived and idempotent; checkpatch is clean. The non-arm probe path cannot
touch the engine (no MMIO/clock/reset/IRQ/NAPI), and the arm path is unreachable without
the command-line opt-in.

**Not proven - any hardware behaviour:** no PIE or GEMAC register write has been executed
by this code; the engine has never been armed; every register value/offset is recovered or
measured on the vendor system, not confirmed for our init; the RX completion decode, the
TX offload words (kept zero), the doorbell counter semantics, the 0x3504 decode, the
ch4-7 mask question, ACP coherency and behaviour under traffic are open; the GEMAC netdev
has no RX delivery and no frame can be claimed to reach a wire (the internal-switch egress
map is missing). Nothing has booted, no image and no install path are involved.

---

## 6. Attended checks needed (before any traffic claim)

1. Recovery-armed bench boot with the nodes enabled and `luofu_pie.arm=1`: the first PIE
   MMIO write the project makes. Capture the arm log, the read-back verification result and
   a register dump; confirm no AHB hang and that the pointer words move.
2. Reconcile the RX completion decode against known frames; then implement delivery.
3. Internal-switch egress port/qid map: the long pole for any frame leaving a MAC port.
4. ACP coherency question for 0x250 under traffic (PIE-DATAPATH section 8).
5. MAC link bring-up: the only MAC writes are the measured 1 Gbps words; other speeds and
   the PHY/MDIO/CRG reset polarities are still unverified on hardware.

---

## 7. Notes for the lead (apply/commit)

- Apply: `cd opensource && git apply target-skeleton/docs/incoming/pie/shared.patch`
  (verified against a byte-copy of the current files), then move this SUMMARY to
  `target-skeleton/docs/port-notes/pie/SUMMARY.md` (merging with the patch-`015`
  record already there) and delete the incoming directory, and commit the new
  `patches-6.18/020-*.patch` with the shared-file changes.
- Do **not** apply the patch twice; the shared.patch is a plain git-style diff against the
  current HEAD state of the six listed files.
- No kernel-source overlap with the parallel LSW lane (task-47 owns patch `021`); both
  lanes only share the doc files (`README.md`, `SKELETON-SMOKE.md`, `TARGET-DESIGN.md`),
  where a union of the added sections/rows is the expected conflict resolution.
- The isolated clone `openwrt-lab-pie-datapath` was used for this lane; the shared clone
  `/home/shibbityshwab/openwrt-lab` and the other lane clones were not modified.
- No shared repository file was edited in place; `git status` in `opensource/` shows only
  the new patch file and this incoming directory as untracked additions.
