# Roadmap: from a working box to a complete open-source OpenWrt submission

> **Status 2026-11-18: this is a phase plan, not current state.** The bench lab-kernel line
> reached its documented ceiling (v17: it boots the vendor userspace, the full module stack
> loads, `eth2` comes up and `br-lan` is created, but the vendor HYFI/bridge userspace contract
> needs a multi-step port - see the parent `STATUS.md` section 3 and the parent ledger entry
> 2026-11-18 06:50). There is **no OpenWrt target, board DTS, image recipe or driver series**
> yet; that is recorded in the parent `EVIDENCE-INDEX.md` ("Upstream submission is not
> started") and the ledger entry 2026-11-17 00:20. Bench gates are not upstream progress.
>
> The concrete design is now **[TARGET-DESIGN.md](TARGET-DESIGN.md)** (target/subtarget layout,
> DTS conversion, FIP/dual-slot image recipe, sysupgrade metadata, per-driver upstream
> readiness, acceptance criteria, staged plan). This roadmap keeps the phase view and the
> history; where a phase claim below is superseded, the status note says so and points at the
> ledger. The live state is the parent repository's `STATUS.md`; the newest dated entry in the
> parent `mem-entries.md` wins over every claim here.
>
> Publication of this submodule is **NOT READY** (`PUBLISH-READINESS.md`: vendor `.o` files
> remain in history; the publish checklist is open). Do not present the published half as a
> submission-ready repository.
>
> Related pages: [TARGET-DESIGN.md](TARGET-DESIGN.md) (current design),
> [VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) (the separate vendor
> sk_buff/bridge/HYFI port that the v17 ceiling needs), [PORT-SURFACE.md](PORT-SURFACE.md)
> (measured Wi-Fi port cost), [PORT-STATUS.md](PORT-STATUS.md) (history of what was attempted),
> [PUBLISH-READINESS.md](PUBLISH-READINESS.md), [GPL-REQUEST.md](GPL-REQUEST.md),
> [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) (the working vendor-stack
> recipe, bench instrument only).

## Where the plan stands on 2026-11-18

**Bench line (reversal track, not upstream).** The 2026-11-14 hybrid - a vanilla 5.10.201
kernel running the patched vendor driver stack, `eth2` up, `br-lan` 192.168.10.1, ping 3/3 -
was superseded by a longer series of fires. The final one (v17) boots our kernel with the
vendor userspace, loads all 19 preloaded vendor modules with `ret=0`, brings `eth2` up at
1 Gbps, and creates `br-lan`; then HSLINK creates and destroys `br-lan` in a loop and
userspace goes silent. The RE verdict is that the vendor userspace needs the vendor
`sk_buff` ABI (`+0x74`/`+0x78`) and the vendor bridge patches - a multi-step port, not a
single fire. Bench fires are stopped; the box needs physical RESET+TFTP recovery. Evidence:
parent `STATUS.md` section 3, parent `EVIDENCE-INDEX.md` "Bench kernel line 2026-11-17/18",
parent ledger 2026-11-18 06:50 (and 2026-11-17 20:15/20:40 for the ABI findings).

**Upstream track.** Nothing above changes this: there is no target/DTS/image recipe/driver
series, no install path, and no WiFi answer. The measured blockers stand:

- Wi-Fi: the vendor module is not a mac80211 driver (0 mac80211 symbols, a vendor MAC inside
  the module, 211 symbols imported through a vendor `HW_RTOS_NS` namespace) over a closed
  928,920-byte firmware blob; the GPLv2 source request was sent 2026-10-01 with no reply held
  (`PORT-SURFACE.md`, `GPL-REQUEST.md`).
- Internal LSW switch: no driver source exists; only reverse-engineered vendor modules.
- PCIe host: the lab PCIe driver is a read-only frame plus an unvalidated, compile-gated
  bring-up; it is not a host controller.
- A/B `sysupgrade` and the stock loader's FIP container: the trailer is currently copied
  from a vendor-derived artifact and cannot yet be regenerated from published material; the
  install path is unproven (see TARGET-DESIGN.md sections 3, 5).

**What this plan is for.** The phases below describe the work shape and the history of how it
was approached. Use them with [TARGET-DESIGN.md](TARGET-DESIGN.md), which is the current,
concrete statement of what to build. Milestone status is in the milestone table at the end.

---

## Phase 0 - freeze the working state (2026-11-14) - **HISTORY, SUPERSEDED**

> **Status 2026-11-18: history.** The deliverables exist, but the "working state" was the
> hybrid bench line that stopped at v17, and the picture is not reproducible from the
> published half: the kernel delta series and the FIP-trailer recipe are unpublished
> (`PUBLISH-READINESS.md` unchecked items). Do not read this phase as current or as
> upstream progress.

Deliverables, all in the parent (private) repository at the time:

- `lab/vendor-stack/init-vendorroot.c` - the initramfs init: mounts the vendor rootfs_data,
  writes the overlay shadows, arms the watchdog keeper, loads the vendor module stack in
  dependency order. (Now tracked in the parent mirror as `tools/lab-src/kbuild/init-vendorroot.c`.)
- `tools/patch-vendor-modules.sh` - reproducible `.plt` patching for a module directory.
- `VENDOR-STACK-ON-MAINLINE.md` - the loader recipe and the two non-obvious blockers
  (`.plt` sections, `finit_module` syscall number).
- The kernel tree, DTS and build lane in the parent private half (`tools/lab-src`, parent
  `RUNBOOK.md`).

Evidence then: G4 green (parent `STATUS.md` gate table), ping 3/3, `Hi-GEMAC ... eth2` and
`br-lan port 1(eth2)` in dmesg. Note the G4 caveat that still stands: the bound PHY driver
was the vendor's `hi_kphy`, so M2 ("no vendor network modules") was never met.

---

## Phase 1 - observation platform (the reversal multiplier) - **HISTORY / PARTIAL**

> **Status 2026-11-18: history/partial.** The premise - "the vendor stack now runs on our
> kernel, which turns every vendor driver into a live oracle" - held during the 2026-11
> bench line but that line is stopped (v17 ceiling, above). Much of what this phase describes
> was delivered in other forms across the phases: the register maps and BAR maps
> (`phase4/mmio-map.md`, `phase11/barmap.md`, `phase43/*`), the ring instruments
> (`lab/ringwatch`, `phase42/THE-RING-MAP.md`), the vendor dmesg/sysfs reads that fixed the
> PHY mapping, and `ETHERNET-BRINGUP-FRAMING.md` (the species of bring-up bugs). Read those
> rather than treating 1.1-1.3 as open tasks.

- **1.1** A register snapshot harness for the vendor modules while they run. Delivered in
  spirit by the read-only devmem/BAR instruments and the lab modules; the record is
  `phase11/barmap.md`, `phase4/mmio-map.md`, `phase43/THE-CHANNEL-MAP.md`.
- **1.2** A trace of the vendor's own MDIO transactions during probe. The MDIO protocol was
  decoded from `hi_kmdio.ko` and the live vendor dmesg; see
  `lab/luofu-mdio/luofu-mdio.c` and `ETHERNET-BRINGUP-FRAMING.md` section 3.
- **1.3** The switch bring-up trace. Not delivered as a sequence; the switch remains
  reverse-engineered only and has no driver (`TARGET-DESIGN.md` section 8.1).

Historical deliverable: `dumps/live/<boot>/` - local only, not a published receipt.

---

## Phase 2 - clean-room drivers, block by block

> **Status 2026-11-18:** the table below is the **2026-11-14 estimate**, kept as the plan's
> original shape. It is not current state. The current per-driver statement (live source
> paths, what is verified, what is needed) is [TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.
> In short: FMC/NAND read/write is proven on the bench (G1/G2); CRG/WDT/MDIO/PHY/GEMAC have
> lab drivers with real work done; pinctrl is a mapping-table driver with stubs; LSW has no
> driver; PCIe is a read-only frame; Wi-Fi is blocked. M2 is still **NOT MET**.

Each block is closed by running our driver with the vendor modules **removed** and the box
still passing its smoke test. Order is by dependency, which is also the order the vendor
loads them.

| block | current (2026-11-14 estimate) | method | closes when |
| --- | --- | --- | --- |
| FMC/NAND | 95% | finish BCH ECC and bad-block handling from `hi_flash`'s tables | the vendor rootfs mounts with our controller only, read and write verified |
| CRG/clocks | 90% | enumerate remaining gates from the live clock tree | every consumer resolves with no vendor module |
| WDT | 100% | done | - |
| pinctrl | 10% | replay `hi_kpinctrl`'s group table from Phase 1 dumps into pinctrl-mmio | UART/ETH/MDIO pins configure from our driver |
| MDIO | 60% | implement the four-phase handshake against the traced transactions | `mdio-tool` read/write works with `hi_kmdio` unloaded |
| KPHY | 0% | the Genius PHY init sequence from the 1.2 trace | our phy driver binds `14000000.mdio0:04` |
| GEMAC | 70% | already probes; attach to our PHY once MDIO/KPHY are ours | `eth2` up with no vendor module |
| LSW switch | 15% | `hi_lsw_pfe`/`hi_lsw_dp` disassembly plus the switch bring-up trace: VLAN table, MAC table, port config | the LAN ports forward through our switch driver |
| PCIe | 40% | host controller init from the phase 19-22 work, plus the vendor's link sequence | the WiFi endpoint enumerates under our host driver |
| WiFi | 30% | see the decision below | a wiphy exists without the vendor's Wi-Fi modules |

The switch and the WiFi are the two large efforts. Everything above them is weeks, not
quarters. (The Wi-Fi 30% figure was an estimate; `PORT-SURFACE.md` is the measured surface
and `TARGET-DESIGN.md` section 8.3 is the current position.)

---

## Phase 3 - the WiFi decision (a real fork in the road)

> **Status 2026-11-18:** not decided, and the 2026-11-18 evidence narrows the near-term path.
> The bench line's remaining blocker is the vendor HYFI/bridge userspace contract
> (`VENDOR-SKB-BRIDGE-PORT.md`), which is relevant to the hybrid track, not to an upstream
> Wi-Fi driver. For upstream, `PORT-SURFACE.md` is the measured position: the driver is not
> mac80211, imports 211 symbols through the vendor `HW_RTOS_NS`, and the firmware is closed.
> The GPLv2 source request was sent 2026-10-01 and no reply is held (`GPL-REQUEST.md`).

The Wi-Fi stack is `hi5622v100_plat`, `hi5622v100_wifi` and a **928,920-byte firmware blob**.
The driver side is tractable in the sense that the ABI delta to the vendor kernel was
identified for a CI-built module (`phase14/abi-match.md`), the HCC service map is mapped, and
the mailbox protocol is partially reversed (the superseded `PORT-STATUS.md` is the history of
that work). The firmware is not: without source it cannot be made open, and no amount of
driver work changes that.

Three options, in order of preference:

1. **Request the GPL'd source from Cudy.** The kernel modules are GPL-declared works and the
   vendor must provide corresponding source. This is the only path to a genuinely complete
   open-source submission, and it is a legal request, not a technical one. Status and the
   follow-up/escalation plan are in `GPL-REQUEST.md`; the Jeton same-SoC letter is a separate
   draft in the parent repo.
2. **Ship the blob, documented.** A port that loads the vendor Wi-Fi firmware is not
   open-source in the strict sense, but it is what most dual-licence router ports do. The
   device page states it plainly. This still requires a driver whose source lives in-tree,
   which today does not exist.
3. **Replace the stack.** A softmac rewrite on the Hi5622 MAC is a multi-quarter project with
   no guarantee.

Do not let the Wi-Fi decision block Phases 1, 2, 4 and 5. The wired box is fully portable
without it - which is the plan in [TARGET-DESIGN.md](TARGET-DESIGN.md) (wired-first target).

---

## Phase 4 - userspace

> **Status 2026-11-18: not started.** The vendor rootfs work (phases 26 and the custom
> 0.1-0.3 builds) established what the vendor base depends on and that an upstream
> `arm_cortex-a9` userspace runs on the vendor kernel; it is input to the port, not a
> deliverable of it. The upstream target will use standard OpenWrt packages, so the vendor
> `hi_*` app layer and LuCI pages are to be replaced, not ported.

- Replace the vendor's `hsan_*` rc scripts and `hi_appenv`/`hi_*env` blobs with device tree
  data and standard OpenWrt init.
- Reimplement the vendor LuCI pages (`patched/*.lua` in the parent private half) as standard
  packages; the RPC injection surface documented in the writeup disappears with them.
- Port the LED and button maps, the dual-image/sysupgrade logic, and the MAC address handling.
  (The DTS has LED/key nodes in the recovered runtime tree; the MAC address source is still
  [U] - see TARGET-DESIGN.md section 12.)

Deliverable: a rootfs that boots with no vendor userspace component.

---

## Phase 5 - the OpenWrt submission

> **Status 2026-11-18: design written, no submission.** The concrete target/subtarget
> recommendation, DTS conversion plan, image recipe (FIP trailer, dual kernel slots,
> squashfs+UBI rootfs), sysupgrade metadata and acceptance criteria are in
> [TARGET-DESIGN.md](TARGET-DESIGN.md). Two corrections to the older text below:
> (a) the target/subtarget choice and the image constraints are no longer open prose, they
> are in TARGET-DESIGN.md sections 4-5; (b) the older claim that "a failed boot leaves a
> status word that makes `bootfip` reject the slot" is **not supported** by the 2026-11-16
> captures - do not build a rollback plan on it (parent `BENCH-FACTS.md` section 5).

- **Target**: [TARGET-DESIGN.md](TARGET-DESIGN.md) section 4 proposes
  `target/linux/hisilicon` with subtarget `luofu` and explains why (the vendor firmware's own
  target string is `hisilicon/luofu`; the board DT root is `hsan-luofu`; the alternative
  `triductor/tr6560` is a different board family with a `tri,*` binding namespace). The
  private lab tree compiles a 5.10.201 DTS into its slices, but it is a stage-1 skeleton with vendor
  `hsan,*` compatibles, a lab partition and no bindings; it is not a submittable DTS.
- **Drivers**: send the drivers upstream as separate patch series with devicetree bindings.
  The inventory and the per-driver "what is needed" table are in TARGET-DESIGN.md section 7;
  the short order is WDT, CRG, thermal, pinctrl, FMC, MDIO/PHY/GEMAC, switch, PCIe. Upstream
  review is the gate; land them in `drivers/` before the target.
- **Target and image recipe**: PR to `openwrt/openwrt` with the target, the image recipe and
  the device profile. The recipe must encode the measured boot constraints, not just
  "uImage + dual-image":
  - the loader (`bootcmd=mtd read kernel${bootflag}; bootfip`) accepts only a FIP package;
  - the 8,650,752-byte kernel slot holds `uImage + the fixed 40,732-byte FIP/DTBO trailer`,
    whose bytes follow the gate layout in TARGET-DESIGN.md section 3.2 (vendor base DTB +
    96-byte metadata descriptor with magic `0xd7b7ab1e` + two overlays);
  - the uImage load/entry address (0x80608000 in the working lane) must be derived from the
    kernel's own `TEXT_OFFSET`/staging base, not copied blind;
  - the trailer is currently vendor-derived and **cannot yet be regenerated from published
    material** - TARGET-DESIGN.md section 5.2 is the plan to make it a build product;
  - A/B `sysupgrade` writes only the inactive kernel/rootfs pair and switches the measured
    selector/env mechanism; the install path is still open (TARGET-DESIGN.md section 5.5).
- **Compliance**: the GPL request from Phase 3 (`GPL-REQUEST.md`), plus the repository-root
  `NOTICE.md` and `PUBLISH-READINESS.md` as the evidence trail. Track the vendor's response as part of the
  submission, not as an afterthought.
- **Device page**: the OpenWrt wiki entry with the hardware identity warning from
  `HARDWARE-IDENTITY.md` (the enclosure label says v1; the PCB is the v2/luofu design).

---

## Milestones

> All milestones were written around the bench-hybrid era. Status column added 2026-11-18.
> Bench gates G1-G6 in the parent `STATUS.md` are **not** these milestones.

| # | milestone | gate | status 2026-11-18 |
| --- | --- | --- | --- |
| M1 | hybrid image reproducible from a clean checkout | `tools/lab-src` + the parent RUNBOOK build lane + the patch script + the smoke test | **SUPERSEDED / NOT VERIFIED**: the private build lane exists, but the kernel delta series and trailer recipe are unpublished (`PUBLISH-READINESS.md`) and the hybrid bench line stopped at v17. Not an upstream artifact. |
| M2 | `eth2` up with **no vendor network modules** | our MDIO + PHY + GEMAC, vendor stack removed | **NOT MET**: the bound PHY driver is the vendor's `hi_kphy` (parent `STATUS.md` G4 caveat, parent `EVIDENCE-INDEX.md` G4 row) |
| M3 | LAN ports forward through our switch driver | our LSW, `hi_lsw_*` removed | **NOT STARTED**: no switch driver source exists (TARGET-DESIGN.md section 8.1) |
| M4 | WiFi decision executed | source received, or the blob documented, or the rewrite scoped | **NOT STARTED**: GPL request sent 2026-10-01, no reply held; no decision recorded. The v17 HYFI/bridge work is a separate vendor-userspace port (`VENDOR-SKB-BRIDGE-PORT.md`), not this milestone. |
| M5 | rootfs with no vendor userspace | standard OpenWrt init + packages | **NOT STARTED** (upstream userspace work is Phase 4) |
| M6 | drivers accepted upstream | patch series merged in `drivers/` | **NOT STARTED**: no series submitted; drivers are lab-only (TARGET-DESIGN.md section 7) |
| M7 | target accepted in OpenWrt | PR merged, device page live | **NOT STARTED**: no target/DTS/image recipe exists (parent `EVIDENCE-INDEX.md` upstream row) |

**Current next real targets** (replacing the old "M2 is the next real target" line): the
host-side S0 work in [TARGET-DESIGN.md](TARGET-DESIGN.md) section 10 - one DTS source with the
stock partition map, binding/YAML drafts, and an offline FIP-trailer generator that fails the
build unless the loader gate is satisfied. Bench-side, S1 needs a written single-variable
plan, an armed recovery door and a human at the bench; bench fires are stopped until then.

---

## References

**Current design and measured surfaces**

- [TARGET-DESIGN.md](TARGET-DESIGN.md) - target/subtarget layout, DTS conversion, FIP/dual-slot
  image recipe, sysupgrade metadata, driver readiness, acceptance criteria, staged plan (2026-11-18).
- [VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) - the vendor `sk_buff`/`bridge`/HYFI
  port that the v17 bench ceiling needs (2026-11-18).
- [PORT-SURFACE.md](PORT-SURFACE.md) - the measured Wi-Fi port cost (211 namespaced symbols,
  0 mac80211 symbols, closed firmware).
- [GPL-REQUEST.md](GPL-REQUEST.md) - the GPLv2 source request and its status (sent 2026-10-01,
  no reply held).
- [PUBLISH-READINESS.md](PUBLISH-READINESS.md) - why this half is not publishable yet.
- `docs/phase50/bsp-build-status.md` - the TR6560 BSP as a target prior art
  (different board, `tri,*` bindings; not a drop-in source).
- `docs/ETHERNET-BRINGUP-FRAMING.md` - the Ethernet bring-up lessons and the
  measured PHY mapping.
- `docs/phase42-decode/report.md` - the loader gate decode.

**History (marked superseded in their own headers; keep as evidence of what was tried)**

- [PORT-STATUS.md](PORT-STATUS.md), `PORT-PLAN.md`, `DEPLOYMENT-DECISION.md`,
  `HYBRID-IMAGE-PLAN.md`, `UPSTREAM-PORT-PLAN.md` (its inventory is still useful; its
  target/DTS/image sections are superseded by TARGET-DESIGN.md).

**Parent (private) repository**

- `STATUS.md` - the only live status page (section 3 = the bench ceiling).
- `BENCH-FACTS.md` - physical invariants (loader, slots, UART read-only, recovery).
- `EVIDENCE-INDEX.md` - claim/artifact/regeneration table; the "Upstream submission is not
  started" row is the authoritative statement.
- `mem-entries.md` - the ledger; the newest entry (2026-11-18 06:50) wins over every claim
  in this file. Relevant dates for this roadmap: 2026-10-01 (GPL request), 2026-11-14
  (G4/hybrid), 2026-11-17 00:20 (harness/publish/upstream audit), 2026-11-17 20:15/20:40
  (HYFI/sk_buff verdict), 2026-11-18 06:50 (v17 ceiling).

---

## Status reconciliation: claims that are superseded by the 2026-11-18 record

These are claims from the pre-2026-11-18 version of this document (kept quoted so a reader with
an old copy can find them).

| older claim | now | where the current statement lives |
| --- | --- | --- |
| "Phase 0 - freeze the working state (done, 2026-11-14)" | history; the state stopped at v17 and is not reproducible from the published half | parent `STATUS.md` section 3; `PUBLISH-READINESS.md`; TARGET-DESIGN.md |
| "the vendor stack runs on our kernel, so every vendor driver is a live oracle" | true only during the stopped bench line; do not treat as current | parent `STATUS.md` section 3; ledger 2026-11-18 06:50 |
| "a failed boot leaves a status word that makes `bootfip` reject the slot" | **not supported** by the 2026-11-16 captures; do not use for rollback | parent `BENCH-FACTS.md` section 5 |
| "five modules build with matching vermagic" (Wi-Fi driver side) | an ABI delta was identified for a CI-built module; the measured port surface is the real statement | `phase14/abi-match.md` (history); `PORT-SURFACE.md` |
| M1 "hybrid image reproducible from a clean checkout" | not verifiable from published material; the build lane is private and the kernel/trailer deltas are unpublished | `PUBLISH-READINESS.md` unchecked items |
| M2 "next real target" | M2 is NOT MET (vendor `hi_kphy` bound); the next real target is the host-side S0 work in TARGET-DESIGN.md section 10 | parent `STATUS.md` G4; parent `EVIDENCE-INDEX.md` G4 row |
| "the trailer is vendor-derived and cannot currently be regenerated from published material" | still true; the regeneration design is now written | TARGET-DESIGN.md sections 3.2 and 5.2 |
| any "aspirational plan, not current state" framing | replaced by the 2026-11-18 status block at the top and the milestone status column | this file |
