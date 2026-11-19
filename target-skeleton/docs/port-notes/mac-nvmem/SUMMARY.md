# incoming/mac-nvmem - factory MAC exposed to the kernel netdev layer + guarded LAN/WAN map

**Lane:** `mac-nvmem`, shared task `task-58`, 2026-11-19.
**Status:** HOST-ONLY, NOTHING BOOTED. No device, router, UART, network, TFTP, flash or bench
tool was touched; the box was not contacted and no MAC was read from a real unit. Publication
remains NOT READY.

This directory is the incoming deliverable for the factory-MAC nvmem lane. It carries the shared
**documentation** patch only (`shared.patch`); the code artifacts (board DTS, kernel patch
`026`, base-files, host test) are direct lane writes already present in
`target-skeleton/` and listed in section 1. Apply `shared.patch` from the `opensource/`
root, then move this `SUMMARY.md` to
`target-skeleton/docs/port-notes/mac-nvmem/SUMMARY.md` (same convention as the other lanes).

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/026-net-gmac-bdinfo-mac-nvmem.patch` | GEMAC MAC-source hardening: read before netdev allocation; honour `-EPROBE_DEFER` from a not-yet-registered MTD nvmem provider (defer, do not randomise); keep the logged random fallback for every other error; install `ndo_set_mac_address = eth_mac_addr` / `ndo_validate_addr = eth_validate_addr` so userspace `macaddr` still applies. Comments + binding description document the bdinfo nvmem source. No register/MTD write. **Refreshed against the post-024 context after 024 landed: applying 001-025 then this patch is a clean apply (no fuzz/offset) and the resulting files are byte-identical to the version the full build compiled.** | `f979ea9ba4ab0a6789514a3b373e66c909fc316993ec340e6d90a94d2db033b3` |
| `target-skeleton/dts/luofu-r116.dts` (modified) | `bdinfo` partition becomes `read-only` with a `nvmem-layout` (`fixed-layout`) holding one `mac-base` cell `mac@de00 { reg = <0xde00 0x6>; #nvmem-cell-cells = <1>; }`; gmac1..3 get `nvmem-cells = <&bdinfo_mac 0>` (LAN base), gmac4 `<&bdinfo_mac 1>` (WAN base+1), each with `nvmem-cell-names = "mac-address"`. Comments record the per-port-offset plan, the LSW port correlation and the backport dependency. **Direct lane write, not in `shared.patch`.** | `d4b523cce7644a3db55a6561079ac469e7200633d7078b66b349f0327f37e4d1` |
| `target-skeleton/target/linux/hisilicon/base-files/lib/functions/luofu-netdev.sh` (new) | Guarded LAN/WAN interface map (vendor R116 map WAN = eth3, LAN = eth2 eth1 eth0) that activates only when all four expected GEMAC netdevs exist; otherwise logs a placeholder naming the missing devices and configures nothing. Documents LSW MAC ports 9..12 / qid rows 4-12 and carries `TODO(netdevs)`. `LUOFU_NET_TEST_ROOT` host seam. | `376be25983759846034e583f770e2968a3833ed12ec62a88ffde49492e96f580` |
| `target-skeleton/target/linux/hisilicon/base-files/etc/board.d/02_network` (modified) | Sources/calls `luofu-netdev.sh` before the unchanged factory-MAC provisioning; header updated to the implemented state (guarded map + kernel-side nvmem path). **The existing `luofu-mac.sh` provisioning is kept intact** (still sourced, still `luofu_mac_setup`/`luofu_mac_apply`; its 103-assertion test still passes). | `c0f4ff475bdc22cfabb966c1c38f55692c409dafb4d02917f6c2a7a7200993cb` |
| `target-skeleton/tools/test-mac-nvmem.sh` (new, 0755) | Host test: statics; synthetic-bdinfo read-path mock; kernel mac-base arithmetic incl. wrap/erased/zero cases; per-node offsets from the DTS; `--kernel <tree>` cpp+dtc compile + decompiled-DTB phandle checks; mocked `/sys/class/net` interface-map scenarios; re-runs the pre-existing provisioner test. **96/96 assertions pass.** | `92826dc9531d9219e9b27cd570f36fb71983b9c284ccd0a68ad2701c6459fb96` |
| `target-skeleton/docs/incoming/mac-nvmem/shared.patch` | Documentation hunks only: `docs/MISC-SOC-BLOCKS.md` (new 1.4a + 1.5 update), `docs/SKELETON-SMOKE.md` (session record + one stale-line correction), `docs/TARGET-DESIGN.md` (open question 5), `target-skeleton/README.md`. Rebased on the `025` integration commit (`opensource` HEAD `4d965d6`); `git apply --check` RC=0. | `73b3ff26273926551e9bd9b52672cdd7b5e904a5505c0db3ada3b940bac9b99a` |
| `target-skeleton/docs/incoming/mac-nvmem/SUMMARY.md` | this file | - |

Evidence bundle (local-only, gitignored):
`build/linux-kbuild/waves/2026-11-19-mac-nvmem/` (logs, DTS/DTB, pristine-apply log,
checkpatch, host tests, hashes).

## 2. Item 1 - the kernel nvmem provider, and why it is not a new driver

### 2.1 What the built target already provides (with provenance)

| piece | where | provenance |
| --- | --- | --- |
| MTD nvmem provider for **every** MTD device/partition (read from `mtd_read()`, **read-only**, root-only) | `drivers/mtd/mtdcore.c` `mtd_nvmem_add()`: `config.reg_read = mtd_nvmem_reg_read`, `config.read_only = true`, `config.root_only = true` | vanilla `linux-6.18.60` [V] |
| OF node for a fixed partition (`child->dev.of_node = part->of_node`) | `drivers/mtd/mtdpart.c` | vanilla 6.18.60 [V] |
| `nvmem-layout` container parsed for the provider's OF node, `#nvmem-cell-cells` phandle arguments resolved | `drivers/nvmem/core.c` (`of_nvmem_cell_get()`, `nvmem_populate_layout()`) | vanilla 6.18.60 [V] |
| `mac-base` cell post-processing (raw 6-byte read -> `eth_addr_add(index)`; ASCII/HEX forms too) | `drivers/nvmem/layouts.c` `nvmem_layout_parse_mac_base()`, `nvmem_mac_base_raw_read()`; called from `nvmem_add_cells_from_dt()` | **OpenWrt carried backports** of upstream commits: `target/linux/generic/pending-6.18/804-nvmem-core-support-mac-base-fixed-layout-cells.patch` and `809-01-nvmem-core-generalize-mac-base-cells-handling.patch` [V]; **not in vanilla 6.18.60** - stated dependency |
| consumer path | `of_get_mac_address()` (`net/core/of_net.c`) -> `of_get_mac_address_nvmem()` -> `of_nvmem_cell_get(np, "mac-address")`; `nvmem_get_mac_address()` (`net/ethernet/eth.c`) requires exactly 6 bytes and `is_valid_ether_addr()` | vanilla 6.18.60 [V] (the `of_add_mac_address()` property write-back in the built tree is OpenWrt `pending-6.18/683-of_net-add-mac-address-to-of-tree.patch`, incidental to this path) |
| provider missing/late -> `-EPROBE_DEFER` | `drivers/nvmem/core.c` (`__nvmem_device_get()`) | vanilla 6.18.60 [V] |
| partition `read-only` -> `MTD_WRITEABLE` cleared | `drivers/mtd/parsers/ofpart_core.c` | vanilla 6.18.60 [V] |

So the "provider" is stock MTD core; the board DTS only declares the cell. This supersedes
`MISC-SOC-BLOCKS.md` section 1.4 option 2's "~100-line target-local provider" estimate (that
was written for the bench 5.10.201 tree, which indeed has neither the provider nor mac-base).

### 2.2 The DTS wiring and the per-port offset plan

```dts
partition@4c0000 {
        label = "bdinfo";
        reg = <0x4c0000 0x40000>;
        read-only;
        nvmem-layout {
                compatible = "fixed-layout";
                #address-cells = <1>;
                #size-cells = <1>;
                bdinfo_mac: mac@de00 {
                        compatible = "mac-base";
                        reg = <0xde00 0x6>;
                        #nvmem-cell-cells = <1>;
                };
        };
};
```

| node | role | cell reference | resulting address |
| --- | --- | --- | --- |
| `gmac1`/`gmac2`/`gmac3` | LAN (eth0/eth1/eth2; LSW MAC ports 9/10/11) | `nvmem-cells = <&bdinfo_mac 0>` | factory base MAC |
| `gmac4` | WAN (eth3; LSW MAC port 12) | `nvmem-cells = <&bdinfo_mac 1>` | base + 1 |
| `gmac0` | disabled, no MAC wiring | - | - |

Evidence for the rule: vendor 2.4.15 `board.d/02_network:25-29` (LAN =
`macaddr_canonicalize $(bdinfo mac)`, WAN = `macaddr_add $lan_mac 1`),
`99_fixwan:41` (2.5.24 LAN = `mtd_get_mac_binary bdinfo 0xde00`), and
`99_fixwan:17-52` (the vendor applies the **same** LAN base MAC to every LAN device, WAN =
base+1). There is **no per-jack offset rule** in any vendor script: the documented plan for
future per-port netdevs is to keep LAN = offset 0 for every LAN port and WAN = offset 1; only
an explicit new evidence/policy could justify distinct per-port addresses. LSW port numbers:
`hisilicon,port` = 9..12 for gmac1..4 (vendor DT; live set 9-12), qid-table rows cover switch
ports 4-12 of which 4-7 are the internal PIE host ports (LSW-SPEC.md 6.1/6.2/7.7).

Arithmetic note [V code]: the kernel mac-base post-processing is a **48-bit** add
(`eth_addr_add()`: `ether_addr_to_u64()` + offset, carry into the OUI possible), while the
vendor/OpenWrt userspace `macaddr_add()` masks to the low 24 bits. The two agree for every
base the provisioner accepts (it rejects `...:ff:ff:ff`, the only divergent case); the
userspace validator remains the stricter authority, and the host test covers both behaviours.

### 2.3 Read-only and fail-safe properties

- **Read-only, enforced twice:** mtdcore registers the nvmem provider with `read_only = true`
  and `root_only = true` (no `reg_write` callback is provided, so there is no write path at
  all), and the partition carries `read-only`, which clears `MTD_WRITEABLE` in the partition
  parser. Nothing in this lane writes factory data.
- **Fail-safe when the partition is absent / unreadable:**

| situation | result |
| --- | --- |
| DTB without the bdinfo cell/wiring | `of_get_mac_address()` returns `-ENOENT` -> GEMAC driver keeps its logged random locally-administered fallback (no invented identity) |
| wiring present but the MTD partition/nvmem provider is not registered yet | nvmem core returns `-EPROBE_DEFER` -> patch 026 defers the GEMAC probe (fail-closed; never randomises away a factory identity that may appear one probe later) |
| partition read returns erased (`0xff...`), all-zero or otherwise invalid bytes | `nvmem_get_mac_address()` rejects via length/`is_valid_ether_addr()` -> logged random fallback |
| userspace `luofu-mac.sh` read fails or validates invalid | existing logged random pair for LAN/WAN (unchanged) |

- **Userspace path kept:** patch 026 installs `ndo_set_mac_address = eth_mac_addr`, so the
  existing board.d provisioning still lands on the netdev through netifd. Kernel and userspace
  derive the same two addresses from the same six bytes; the userspace path is also the fallback
  for a DTB without the kernel wiring (older image/DTS). Documented divergence [I]: the generic
  kernel helper only rejects zero/multicast (it accepts a locally-administered value), while the
  userspace validator is stricter; for a genuine factory MAC both agree.

## 3. Item 2 - board.d LAN/WAN interface map (placeholder until the netdevs exist)

- Vendor evidence [V]: R116 = WAN `eth3`, LAN `eth2 eth1 eth0`
  (`rootfs-2.4.15/etc/board.d/02_network:25-29`); LSW-SPEC.md 6.1 records the same userspace
  mapping and the MAC-port correlation; the switchdev model has one netdev per MAC port
  (gmac1..4 = eth0..eth3), not DSA (LSW-SPEC.md 9.3), so no `lan1..lanN` names are invented.
- Implementation: `luofu-netdev.sh` applies `ucidef_set_interface_wan eth3` +
  `ucidef_set_interface_lan "eth2 eth1 eth0"` only when `/sys/class/net/{eth0,eth1,eth2,eth3}`
  all exist (checked at board.d time), and otherwise logs the placeholder with the missing
  devices. Clear `TODO(netdevs)` in both files: drop the guard and make the map unconditional
  when the GEMAC netdevs stop being attended-gated (LSW-SPEC.md 9.4).
- MAC provisioning stays intact: `luofu-mac.sh` is still sourced and called; the map is applied
  before the MAC `ucidef_set_interface_macaddr` calls so board.json carries the
  device/ports the addresses belong to. The pre-existing 103-assertion test still passes.
- **Caveat [U]:** board.d runs once per `/etc/board.json`. A first boot without the gated
  netdevs records only the placeholder; an attended boot that needs the map must regenerate
  board.json after the netdevs are present (documented in the session record and SUMMARY
  section 5).

## 4. Validation (isolated clone `/home/shibbityshwab/openwrt-lab-macnvmem`; no device)

The clone is a `cp -a --reflink=auto` copy of the lane's warm OpenWrt tree (the shared clone and
`openwrt-lab-selectorfix-img` were not touched); absolute paths inside the copy were rewritten to
the clone path before building. Command logs are in the evidence bundle.

- **Pristine series apply (kernel only):** the final series **001-026** (024/025 landed from
  their lanes during this session) applied in numeric order to pristine `linux-6.18.60` with
  `patch -p1 -N`: **26 patches, 100 files patched, 0 failed hunks, 0 `.rej`/`.orig`,
  RC=0**. 024 also edits `luofu-gmac.c` and its YAML, but 026's context is disjoint and the
  combined series applies cleanly; 025 does not touch either file.
- **OpenWrt prepare:** `make target/linux/prepare V=s` **RC=0**, 1,225 `patching file` lines
  (patch 026 applied through the OpenWrt pipeline). The single `Kconfig.orig` in the tree is
  GNU patch's default backup for patch 021's known 6-line-offset hunk (pre-existing series
  behaviour, as in the previous lanes), not a reject.
- **Full kernel compile (this lane's code on the validated base, 001-023+026):**
  `make target/linux/compile V=s -j16` **RC=0, 0 errors, 0 compiler warnings under
  `CONFIG_WERROR=y`** (1,418 s wall; full prepare+rebuild from the pristine tarball, not an
  incremental build). zImage **3,281,744 B**, sha256
  `8443c8c0c41a31a335188e1e547d6f82f5d33b61d945b4bd770d315eeac83f81`; the built `.config`
  contains `CONFIG_WERROR=y`, `CONFIG_MTD=y`, `CONFIG_NVMEM=y`,
  `CONFIG_NVMEM_LAYOUTS=y`, and `luofu-gmac.o` was rebuilt from the freshly prepared tree
  (patch 026 content present).
- **Full final series 001-026 - PREPARE CLEAN, COMPILE BLOCKED BY PATCH 024 (other lane).**
  `make target/linux/prepare` **RC=0** (1,233 `patching file` lines, 0 rejected hunks,
  0 `.rej`), so the whole 001-026 series applies through the OpenWrt pipeline. The compile
  then fails at `drivers/net/ethernet/hisilicon/luofu-pie.c:1060`:
  `error: 'GRO_DROP' undeclared` - a defect of the **pie-decode lane's patch 024** (the
  identifier does not exist in 6.18), outside this lane. Everything compiled up to that point
  is clean, including `luofu-gmac.o` (the 024+026 combined GEMAC driver) and
  `luofu-switch.o` (025); a targeted `make drivers/net/ethernet/hisilicon/luofu-gmac.o` in
  that final prepared tree is **RC=0**. The 001-026 image build must wait for the pie-decode
  lane to fix `luofu-pie.c`; then re-run prepare+compile unchanged.
- **Patch 026 context refresh:** after 024 landed, 026's original hunks applied with offsets and
  one fuzz-2 hunk. 026 was re-diffed against the post-024 tree; applying 001-025 then the
  refreshed 026 is a **clean apply (no fuzz/offset)** and the resulting
  `luofu-gmac.c`/`hisilicon,luofu-gmac.yaml` are **byte-identical** (`cmp`) to the content
  the 001-023+026 full build compiled, so that build's evidence applies to the refreshed
  patch.
- **Binding:** `dt_binding_check` **RC=0** for the changed
  `hisilicon,luofu-gmac.yaml` (example DT compiled). Optional `yamllint` is unavailable on
  the host - the same limitation as every previous session; one pre-existing unrelated realtek
  YAML warning is printed by the full schema walk.
- **Board DTS:** compiled from the repo DTS with the patched kernel's own dtc: **zero warnings,
  11,996 B, sha256 `e9c1649f...`** (baseline before this session: 11,571 B,
  `7fd33522...`). `dt-validate` returns RC=0 on both and the **only** diagnostics are the same
  5 pre-existing ones (SP804 timer compatible list, two UART clock-names); **none** mentions
  the bdinfo partition, nvmem-layout, mac-base cell or the GEMAC nvmem wiring.
- **checkpatch:** `--no-tree -f`: **0 errors / 0 warnings / 0 checks** on the modified GEMAC
  driver and its YAML.
- **Config:** no `config-6.18` change and no new symbol. `CONFIG_NVMEM=y` and
  `CONFIG_NVMEM_LAYOUTS=y` were already in the committed config; the mac-base support is
  Makefile-carried (generic backports), not a symbol.
- **Host tests:** `tools/test-mac-nvmem.sh --kernel <patched tree>` **96/96 pass**;
  `tools/test-mac-provisioning.sh` still **103/103 pass**.
- **Write-set audit:** patch 026 contains no register write, no `mtd_write`/`mtd_erase`, no
  writable nvmem provider; the DTS partition is read-only; the nvmem read path is mainline
  `mtd_read()`.
- **Not done / not applicable:** no image rebuild (this lane changes no image recipe and no
  config symbol); no bench access of any kind.

## 5. Proven vs unproven

**Proven (host-only):** the series applies in order on a pristine tree and through the OpenWrt
prepare pipeline; the full kernel compiles warning-free under WERROR with the new code; the
binding and board DTS validate with no new diagnostic; the wiring is exactly the documented one
(checked on raw DTS, compiled DTB, decompiled DTB and a read-path mock); the userspace map is
guarded, testable and backward compatible with the 103-assertion provisioner test; every failure
mode of the new MAC path is either a logged random fallback or a probe deferral, never an
invented identity.

**Not proven / unmeasured [U] (all need the board):**
- the MTD nvmem read itself - no boot has ever reached this code; whether the nvmem provider
  registers before the GEMAC probe (defer branch) is a probe-ordering question;
- whether our FMC/MTD stack creates the `bdinfo` partition with the expected label/size and
  whether the hardcoded `+0xde00` offset is the live copy on this unit under our kernel (the
  layout is from the local dump, not from this read path);
- the netdev naming order (eth0..eth3) and the userspace `macaddr` application through
  `ndo_set_mac_address`;
- whether the guarded interface map activates on an attended boot (board.d runs once; see the
  caveat in section 3);
- the mac-base backports' behavioural equivalence on this exact tree (they are validated by
  compile + the read-path mock + the upstream commit provenance, not executed);
- the "absent partition" and "erased copy" branches are only unit-mocked/source-audited, not
  observed.

## 6. Integration steps for the lead

1. Apply the documentation patch from the `opensource/` root:

       cd opensource
       git apply target-skeleton/docs/incoming/mac-nvmem/shared.patch

   (`shared.patch` sha256
   `73b3ff26273926551e9bd9b52672cdd7b5e904a5505c0db3ada3b940bac9b99a`; base: `opensource`
   HEAD `4d965d6`, i.e. it is already rebased on the integrated `025` commit, which also edited
   `docs/SKELETON-SMOKE.md` and `target-skeleton/README.md`; touches only
   `docs/MISC-SOC-BLOCKS.md`, `docs/SKELETON-SMOKE.md`, `docs/TARGET-DESIGN.md`,
   `target-skeleton/README.md`. If another lane's docs land first, rebase this patch rather than
   dropping hunks.)
2. Move this `SUMMARY.md` to `target-skeleton/docs/port-notes/mac-nvmem/SUMMARY.md`.
3. **The board DTS change is a direct lane write, not in `shared.patch`** (so
   `tools/test-mac-nvmem.sh` passes in the delivered tree). If the 024/025 lanes also carry
   DTS hunks, rebase them on this file rather than reverting this change.
4. **Numbering / build blocker:** the kernel patch is `026` and the tree now carries the
   contiguous series `001-026` (024 and 025 landed from their lanes while this lane was
   validating). The full `001-026` order applies cleanly on pristine `linux-6.18.60`
   (26 patches, 100 files, 0 failed hunks) and the OpenWrt prepare stage is RC=0, but the
   **001-026 compile is currently blocked by patch 024** (`luofu-pie.c`: `GRO_DROP`
   undeclared in 6.18) - a pie-decode-lane defect to fix before any integrated image build;
   this lane's files compile cleanly in that tree. 024 also edits `luofu-gmac.c` and
   `hisilicon,luofu-gmac.yaml`; 026 was refreshed against the post-024 context (clean apply,
   resulting files byte-identical to the compiled ones). If 024 is ever rebased, re-check
   `patch --dry-run` for 026 and rebase only that patch.
5. No `config-6.18`, image-recipe or base-files install change is needed beyond the two
   base-files files already written in the tree (`luofu-netdev.sh` is installed automatically
   by the existing base-files copy).
6. Do not move the bdinfo `read-only`/nvmem DTS hunk into a kernel patch: the DTS is delivered
   through the target's `dts/` mechanism (README section 4), and the provider is stock MTD
   core.
7. The mac-base support depends on `target/linux/generic/pending-6.18/804-...` and
   `809-01-...`; if those OpenWrt backports are ever dropped/changed, the WAN "+1" offset
   silently stops being applied (the cell would read raw bytes) - re-run
   `tools/test-mac-nvmem.sh` and keep the dependency documented.

## 7. Host-only / safety

No device, UART, TFTP, flash or bench tool was used; nothing was booted; no factory MAC value
appears in this package (the test tool uses documented synthetic data). The bdinfo partition is
only ever read by design. The isolated clone (`/home/shibbityshwab/openwrt-lab-macnvmem`) was
copied from the lane's warm tree; the shared clone
`/home/shibbityshwab/openwrt-lab` and `/home/shibbityshwab/openwrt-lab-selectorfix-img` were
not modified.
