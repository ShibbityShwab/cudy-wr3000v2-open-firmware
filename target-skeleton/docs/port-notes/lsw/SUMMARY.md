# incoming/lsw - internal LSW switch D1a probe skeleton (patch 016)

**Lane:** `driver-lsw`, shared task `task-26`, 2026-11-18.
**Status:** HOST-ONLY DRAFT. Nothing has run on the board; no boot, image, flash or bench
claim. Publication remains NOT READY.

This directory is an **incoming** deliverable: it carries no direct edit to a shared
repository file. The kernel patch itself is already in
`target-skeleton/target/linux/hisilicon/patches-6.18/` (direct lane write scope);
apply `shared.patch` for the DTS/config/doc changes, then move this SUMMARY to
`target-skeleton/docs/port-notes/lsw/SUMMARY.md` (the shared.patch's port-notes
README hunk expects that directory).

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/016-net-lsw-luofu.patch` | kernel patch: Kconfig + Makefile + `drivers/net/ethernet/hisilicon/luofu-switch.c` + `Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml` | `c15aba7c52b671211334423b919d5ca209906a53b339ff0ccef5259f22a0b68a` |
| `target-skeleton/docs/incoming/lsw/shared.patch` | DTS switch node (disabled), `CONFIG_LUOFU_SWITCH=y` `[N]` delta, and the README / SKELETON-SMOKE / TARGET-DESIGN / REMAINING-DRIVERS / port-notes edits | `db698a100c7548121b2f2baa7195f4a8665ade16db645ffb31534aa16067f87f` |

The patch adds:

- `drivers/net/ethernet/hisilicon/luofu-switch.c` (447 lines): platform driver for
  `hisilicon,luofu-switch` matching the v250 generation;
- `Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml` (179 lines);
- `CONFIG_LUOFU_SWITCH` (tristate, `depends on OF`) in
  `drivers/net/ethernet/hisilicon/Kconfig` and its Makefile object.

`shared.patch` also replaces the two placeholder LSW nodes in
`target-skeleton/dts/luofu-r116.dts` with one disabled `ethernet-switch@11000000`
node (15 windows + clocks + resets + `ethernet-ports`) and adds the annotated
`CONFIG_LUOFU_SWITCH=y` `[N]` entry to `luofu/config-6.18`.

## 2. What the driver does, in order

1. variant: `hisilicon,luofu-switch` -> v250 match data (`hwver = 0x250`, the value
   the vendor devicetree carries on both switch nodes); an unknown generation fails closed.
   The vendor `hwver` devicetree property is not reproduced.
2. windows: looks up 15 named 512 KiB MMIO windows (`dp ips fam pe eqm eps prbs
   pfe_glb pa ifc l2 napt mcc opp pub`), checks every size, claims and ioremaps each
   with `devm_platform_ioremap_resource_byname()`. Mapping does not touch the block.
3. clocks: acquires the two named CRG gate clocks (`lsw_dp_clk`, `lsw_pfe_clk`);
   **does not enable them**.
4. resets: acquires the four named CRG reset lines (`dp_logic`, `dp_srst`,
   `pfe_logic`, `pfe_srst`); **does not assert/deassert them**.
5. port model: parses the `ethernet-ports` child nodes - each with a `reg` port index,
   bounded by the recovered 46-entry port table and unique per node, plus an
   `ethernet` phandle to the node that provides the port's netdev; rejects duplicate
   indices/phandles, a missing `ports` node and a port list that names the switch
   itself. The phandle references are released before probe returns.
6. logs a one-line summary and returns `-EOPNOTSUPP` with
   "switch programming not verified: LSW register/queue/port-id model has never been
   measured; refusing to program the switch". No register is read or written (there is no
   `readl()`/`writel()` in the file at all), no clock is enabled, no reset is toggled, no
   interrupt is requested, no netdev is registered and no switchdev notifier is installed.
   The DTS node is `status = "disabled"`, so a normal boot never reaches probe.

## 3. Design decisions (and why)

- **One switch node, 15 MMIO windows** instead of the vendor split (`lsw_dp` 7 windows +
  `lsw_pfe` 10 windows). The vendor split follows its module layering
  (`hi_lsw_dp`/`hi_lsw_pfe`), not a hardware boundary: it is one switch in the data
  path (gemac -> LSW -> PHY -> RJ45), and the switchdev model wants one switch core with
  per-port netdevs. A single binding is also the smallest possible ABI to review before any
  register is ever programmed; a parser subdevice can still be split out later if D2 shows an
  independent driver role.
- **The vendor `queue`/`flow` windows are deliberately not modelled.** They are DDR memory
  the engine consumes (4 MiB / 1 MiB), not MMIO register pages, and which base is live after
  the U-Boot overlay (`0x87b00000` vs rebased `0x80000000`) is one of the explicit [U]
  items in LSW-SPEC.md 5.4. Putting DDR under a platform device's `reg` would be wrong and
  would freeze an unmeasured address.
- **switchdev, not DSA** (LSW-SPEC.md 9.3): there is no tag/CPU-port framing anywhere in the
  recovered model, each wired port already has its own GEMAC netdev, and the vendor's own
  integration is switchdev (with stubbed object handlers). The binding therefore describes
  `ethernet-ports` children, each naming the node that owns that port's netdev
  (`ethernet = <&gmacN>`); the switch driver will react to those netdevs through the
  standard switchdev contract in D2, it does not create netdevs or register ports in D1a.
- **The port index in the binding is a devicetree ordering index, not a hardware port id.**
  The mapping between the MAC port numbers (8..12 in the vendor devicetree/PIE descriptor) and
  the switch's internal port ids is [U] (LSW-SPEC.md 6.2/13.2 item 2), so the binding says so
  and the driver only validates ordering/uniqueness, never programs an id. The 46-port bound
  comes from the recovered queue-manager port table and is a sanity bound on the software
  model, not a hardware claim.
- **Clocks/resets acquired but not driven**, and **no register access at all** - stricter than
  "no writes": even a read of an uninventoried window is a hardware step the project's bench
  rules gate. Probe can therefore not leave the block in a different state than it found it.
- **No switchdev notifier / no netdev in D1a.** There is no port netdev to attach to today
  (the GEMAC nodes are disabled) and a notifier that ignored every object would reproduce
  exactly the vendor's shipped stub behaviour that LSW-SPEC.md 4.7 calls out. The port-model
  parsing is the D1a part of the switchdev scaffolding.
- **Compatible/name choice:** `hisilicon,luofu-switch` (LSW-SPEC.md 9.4 proposed
  `hisilicon,luofu-lsw` with "name TBD"; REMAINING-DRIVERS.md 5.3 and this lane use
  `-switch`). File `luofu-switch.c`, Kconfig `CONFIG_LUOFU_SWITCH`.
- **Variant by match data** (v250), like the PIE draft: the vendor `hwver` DT property is
  not an ABI for us; an unknown generation fails closed.
- **Config symbol delivered as `=y`** in the target config delta, consistent with the
  series convention (all Luofu drafts are built-in; runtime is gated by the disabled DTS node
  and the probe refusal). LSW-SPEC.md 11 D1a phrases the image state as "=n until proven"; the
  effective safety property is identical here because nothing can probe the node and the
  driver would refuse if it did. If the lead wants the literal `=n`, it is a one-line
  change in the delivered shared.patch with no other effect.
- **WoE is untouched:** `lsw_woe` has five SPI interrupts, no driver and no software
  evidence in either rootfs; it stays out of scope (LSW-SPEC.md 5.5).

## 4. Validation (isolated clone, host-only)

Clone: `/home/shibbityshwab/openwrt-lab-lsw` created with
`cp -a --reflink=auto /home/shibbityshwab/openwrt-lab ...`; the shared clone was not
modified. The target tree was re-synced to the committed 001-015 state (`git archive HEAD`)
plus patch 016 and the delivered DTS/config edits; the stale kernel build directory was
removed so `make target/linux/prepare` re-extracted 6.18.60 and re-applied the whole
series from scratch.

| gate | result |
| --- | --- |
| pristine 6.18.60 dry-run of 016 alone | RC=0 (Kconfig hunk applied with offset -23 - cosmetic, its context is common pre-001 text; Makefile hunk exact; new files created) |
| chained apply pristine -> 001..016 | RC=0, 60 `patching file` lines, 0 `.rej`/`.orig` |
| `make target/linux/prepare V=s` | RC=0, 1,193 patching lines, 0 `.rej`/`.orig`, the 016 hunks applied without offsets |
| `make target/linux/compile V=s -j16` | RC=0, **0 warnings** under `CONFIG_WERROR=y`; `CC drivers/net/ethernet/hisilicon/luofu-switch.o` (23,860 B) |
| zImage | 3,263,624 B sha256 `201ba86863d8ee6c14d865702ee2339cd3e373c30fbb4b94983afcc2d72df83f` |
| driver built-in | System.map: `luofu_lsw_probe`, `luofu_lsw_driver`, `luofu_lsw_of_match`, `luofu_lsw_v250`, `luofu_lsw_window_names`, `luofu_lsw_reset_names` |
| TEXT_OFFSET contract | unchanged: `_text = stext = 0xc0608000`, `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`, `DTEXT_OFFSET=0x00608000` |
| resolved config | `CONFIG_LUOFU_SWITCH=y`, `CONFIG_LUOFU_PIE=y`, `CONFIG_NET_VENDOR_HISILICON=y`, `CONFIG_ARCH_LUOFU=y`, `CONFIG_WERROR=y` |
| `dt_binding_check` for the new YAML | RC=0 (DTEX + DTC [C] for the example; dtschema 2026.9; `yamllint` unavailable - that one lint step skipped) |
| board DTS, pre-shared.patch | RC=0, **zero warnings**, DTB 10,193 B sha256 `4e4a03043e7c01448c5a559bc9fb055df0cd7f5d71703a4a0905a4600b8e37a0` |
| board DTS, post-shared.patch | RC=0, **zero warnings**, DTB 10,538 B sha256 `ff34bc7f7e744b61bbc0ca7bd0934317e46d837c87d024592fc08335ed5ce4a0` |
| `dt-validate` of the integrated DTB | RC=0, no diagnostic names the switch node (pre-existing unrelated diagnostics for other TODO nodes: sp804 compatible, uart clock-names) |
| fdtget switch node | `hisilicon,luofu-switch`, 15 reg tuples, `reg-names` dp..pub, clocks `<4 8><4 9>`, resets `<4 0x30 0><4 0x30 1><4 0x30 3><4 0x30 4>`, `status = "disabled"`, 4 `ethernet-port@N` children |
| config canonical re-derivation (`make kernel_oldconfig` twice) | RC=0 both, byte-identical sha256 `27ce568ea062aad81d5d1c96c848399fe2a7d7a56e614af712d22794c6550025`; 308-symbol set identical to the delivered annotated delta |
| checkpatch (`--no-tree -f`) | driver 0 errors / 0 warnings / 0 checks (447 lines); YAML 0/0/0 (179 lines) |
| `shared.patch` | `patch -p1 --dry-run` RC=0, apply RC=0, byte-identical to the delivered files; `git apply --check` RC=0 against the current repository; the repository files are still the pre-apply state the patch targets |
| static checks | no MMIO accessor anywhere in code (C comments stripped: 0 sites); no clock enable/reset toggle/IRQ request/NAPI/netdev/switchdev registration/`EXPORT_SYMBOL`; no lab/bench/local-path breadcrumbs; no vendor-only devicetree properties or `hsan` strings; no forbidden CA/IAR/misc-window addresses; DTS node disabled |

Evidence is local-only under
`build/linux-kbuild/waves/2026-11-18-lsw-driver/` (gitignored): logs, config
re-derivation runs, DTBs/preprocessed DTS, patch copies, `SHA256SUMS`.

## 5. Proven vs unverified

**Proven (host-only):** the patch applies to pristine 6.18.60 and in the OpenWrt platform-patch
order; the full kernel compiles with the delivered config under `CONFIG_WERROR=y` with
zero warnings; the binding passes `dt_binding_check`; the board DTS compiles with zero
warnings before and after the node change and the node survives `dt-validate`; the config
delta is re-derivation-identical; the driver cannot touch the hardware (no MMIO accessor, no
clock/reset operation, no IRQ, no netdev) and fails closed with `-EOPNOTSUPP`.

**Not proven / not claimed - any hardware behaviour:** the register/queue/port-id model is
recovered offline from the vendor modules, never measured on this board. `port_bitmap`
semantics, the LSW-vs-MAC port-id mapping, the EQM command word, the qid-map high port classes,
the classifier/VLAN/L2 table formats, the queue/flow DDR base and the reset-order requirement
all remain [U] (LSW-SPEC.md 13.2). The driver has never run on ARM, never mapped the real
windows at runtime and never registered a port; no boot, no image, no install path, no bench
run.

## 6. Remaining blockers / next steps

1. **D1b write set** - attended, read-only inventory of the switch windows on the stock vendor
   kernel (LSW-SPEC.md 12.3 W1/W2), then a reviewed write set: clock enable, reset deassert,
   qid maps for the host + four MAC ports, EQM queue entries. This is the gate before the DTS
   node may be enabled.
2. **D1c host queue path** - bind the PIE host queues to the LSW (after the PIE 015 inventory)
   and prove one frame from a port netdev to a MAC port; **D1d bridge** - Linux bridge over
   eth0..eth3, no offload.
3. **Blocked by no bootable 6.18 target** ([X], TARGET-DESIGN.md): every hardware step above is
   attended-bench work and waits on the target image/install path; the switch D1a lane itself
   is host-only complete.
4. **GPL source** (GPL-REQUEST.md items 2-4) would convert sections 5-8 of the RE from [U] to
   port data and shorten D1b - optional fast path, not a gate.

## 7. Notes for the lead (apply/commit)

- The kernel patch is already installed (untracked):
  `target-skeleton/target/linux/hisilicon/patches-6.18/016-net-lsw-luofu.patch`.
- Apply the shared files: `cd opensource && git apply target-skeleton/docs/incoming/lsw/shared.patch`
  (verified `git apply --check` RC=0 against the current repository; verified applied content is
  byte-identical to the delivered files). It touches **only** the 7 files listed in section 1;
  it does not touch the target `Makefile`, `image/` or `tools/` files that the parallel
  image-recipe lane is editing today.
- Then move this SUMMARY to `target-skeleton/docs/port-notes/lsw/SUMMARY.md` and commit
  the four new things together (patch 016, the incoming record, the shared-file changes, the
  port-notes README line).
- Possible conflict: if the parallel image-recipe lane also delivers a `shared.patch` that edits
  `target-skeleton/README.md`, `docs/SKELETON-SMOKE.md` or `docs/TARGET-DESIGN.md`, apply one
  first and use `git apply -3` (or a union of the additive hunks) for the other; this lane's
  hunks are anchored on the current HEAD text and only add rows/sections.
- `CONFIG_LUOFU_SWITCH=y` vs LSW-SPEC's "=n in the image until proven": see section 3; a
  one-line flip in the shared.patch if preferred.
- The shared clone `/home/shibbityshwab/openwrt-lab` was not modified; all validation used
  `/home/shibbityshwab/openwrt-lab-lsw`.
