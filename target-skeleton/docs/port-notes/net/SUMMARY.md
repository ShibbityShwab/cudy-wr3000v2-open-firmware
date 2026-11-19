# incoming/net-lane - minimum connectivity path: LSW W2 qid-map writes + GEMAC/PIE netdev readiness (patches 022/023)

**Lane:** `net-lane`, shared task `task-52`, 2026-11-19.
**Status:** HOST-ONLY DRAFT. Nothing in this package has been executed on the board. There was
**no device access** of any kind (no SSH/telnet/TFTP/flash/UART); the router was not touched.
Publication remains NOT READY.

This directory is an **incoming** deliverable: it carries no direct edit to a shared repository
file. The two kernel patches are already in
`target-skeleton/target/linux/hisilicon/patches-6.18/` (direct lane write scope). Apply
`shared.patch` from the `opensource/` root for the board DTS, `config-6.18` and documentation
changes, then move this SUMMARY to `target-skeleton/docs/port-notes/net/SUMMARY.md`.

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/022-net-lsw-qidmap-write-luofu.patch` | LSW staged write path (W3 power/snapshot, W4/W5 qid-map write + readback) behind `luofu_lsw.arm`/`qidmap` gates; `luofu-switch.h` readiness API; measured hardware port numbers in the port model; binding + Kconfig help | `33ab13647791b1a0dab5edf151d8eb5a50304f1d8f2690f9f799862ad04fde3e` |
| `target-skeleton/target/linux/hisilicon/patches-6.18/023-net-gmac-netdev-connect-luofu.patch` | GEMAC netdev readiness: requires PIE armed **and** LSW egress map ready; `PHYLINK_NETDEV` carrier fix; DT MAC address + random fallback; link-up log; Kconfig dep | `bf5fb54b663d72b62f92c873c640aaee21eb2496efeee0d06d13153dcc35d0f1` |
| `target-skeleton/docs/incoming/net-lane/shared.patch` | board DTS (enable lsw/pie/gmac1-4, add `hisilicon,port`), `config-6.18` provenance annotations, README, LSW-SPEC, PIE-DATAPATH, SKELETON-SMOKE, TARGET-DESIGN | `92972f01f9ea4c22aa727b43f703f15fd292ba8ee449c613f07eb241e84c658e` |
| `target-skeleton/docs/incoming/net-lane/SUMMARY.md` | this file (moved to `port-notes/net/SUMMARY.md` on integration) | - |

`shared.patch` applies to the current `opensource/` HEAD with `git apply --check` **RC=0**
(7 files); it does not touch `image/luofu.mk` or `tools/luofu-image.py`, so it does not collide
with the in-progress overlay/image lane.

## 2. What the patches do, and the gate model

The image boots to userspace but has no network. The minimum connectivity path needs (a) a
netdev, (b) the PHY link, (c) PIE rings armed, (d) the internal switch egress qid map, and (e)
userspace IP/DHCP. This lane implements the deterministic host-side part of (a), (c) and (d)
behind explicit gates, and documents (b)/(e) as attended steps.

| boot parameter | LSW driver (022) | PIE driver (020, unchanged) | GEMAC netdevs (023) |
| --- | --- | --- | --- |
| none (normal boot) | read-only baseline validate + refuse, no write | gate closed: validate + refuse, no register/clock/reset | PIE refused -> no netdev |
| `luofu_lsw.arm=1` | W3: enable the two CRG gate clocks, release the four resets, re-check the powered state, snapshot the measured 30 words + qid tables read-only -> refuse (no LSW register write) | unchanged | no netdev (no egress map) |
| `luofu_lsw.arm=1 luofu_lsw.qidmap=1` | W3 + W4/W5: pre-write conformance gate (unpopulated qid-map rows must be zero), program the W2 table-A/table-B rows for ports 4-12 and 40-43, read every programmed word back, restore found values on mismatch; `luofu_switch_ready()` true only after full verification | unchanged | still waits for PIE |
| `luofu_pie.arm=1` + both LSW gates | READY | arms rings/IRQs (existing behaviour) | **eth0..eth3 register**, phylink resolve drives carrier |

The existing `luofu_pie.arm=1` semantics are untouched; the new LSW gate is separate and
fail-closed (no parameter = patch-021 read-only probe). The GEMAC readiness check is
`luofu_pie_armed() && luofu_switch_ready()`; `LUOFU_GMAC` now `depends on LUOFU_SWITCH`.

The DTS changes (shared.patch) enable the `lsw`, `pie` and `gmac1`-`gmac4` nodes and add the
measured hardware port numbers to the switch port children (`hisilicon,port` = 9, 10, 11, 12 for
gmac1..4, from the vendor devicetree's GEMAC `port` cells; live set 9-12). `gmac0` stays
disabled. A normal boot now reaches every driver and logs the refusal; nothing is written. The
`config-6.18` symbol set is unchanged (both symbols already `y`; no new symbol).

## 3. Validation (isolated clone `/home/shibbityshwab/openwrt-lab-net`, no device)

- **Pristine apply order:** all **23 patches apply in order** to pristine `linux-6.18.60` with
  `patch -p1 -N`: **23 patches, 90 files patched, 0 failed hunks, 0 `.rej`/`.orig`, RC=0**.
- **OpenWrt prepare:** `make target/linux/prepare V=s` **RC=0** (1,223 `patching file` lines, no
  failed hunk). One `Kconfig.orig` is GNU patch's default backup-if-mismatch for patch 021's
  6-line-offset Kconfig hunk (pre-existing series behaviour, not a failure; no rejects).
- **Full kernel compile:** `make target/linux/compile V=s -j16` **RC=0, 0 errors, 0 compiler
  warnings** in 1,254 s with `CONFIG_WERROR=y` (resolved from the tree's
  `CONFIG_KERNEL_WERROR=y`); `luofu-switch.o`, `luofu-gmac.o` and `luofu-pie.o` compile under
  `W=1` cleanly. zImage **3,281,376 B**, sha256
  `5903db04928e58214a816a5809f186d579857c8e1c5c0e695cc9035bd68e91ca`.
- **TEXT_OFFSET unchanged:** `_text = 0xc0608000`, `swapper_pg_dir = 0xc0604000`,
  `DTEXT_OFFSET=0x00608000` in both `.head.o.cmd` files; `luofu_switch_probed`/
  `luofu_switch_ready` exported and visible in `System.map`.
- **Bindings:** `dt_binding_check` RC=0 for `hisilicon,luofu-switch.yaml` and
  `hisilicon,luofu-gmac.yaml` (dtschema 2026.9; the optional `yamllint` step is skipped - the
  package is not installed, same limitation as every previous session).
- **Board DTS:** with the shared.patch content the board DTS compiles with the patched kernel's
  own dtc to **11,571 B**, sha256
  `7fd33522c89ee06369538fa40f8790d200c68d92fdf2e85995c3ec1fe37a9e05`, **zero warnings**;
  `dt-validate` reports **no diagnostic** for the switch/PIE/GEMAC nodes (5 pre-existing
  diagnostics remain on the SP804 timer and the two UART nodes).
- **Config:** the delivered `config-6.18` symbol set is **identical to the committed one**
  (patches 022/023 add no symbol); two consecutive `make kernel_oldconfig` runs are
  byte-identical (sha256 `d1af1b2a9411b5529ddef6d5ab1852059aca594eacb5de7f24adb0e35e87665b`, RC=0).
  The canonical regenerated body differs from the hand-maintained file only in two pre-existing
  boot-fix artifacts (`# CONFIG_INITRAMFS_FORCE is not set`, `CONFIG_XZ_DEC_BCJ=y`), not caused
  by this series.
- **checkpatch:** `--no-tree -f` **0 errors / 0 warnings / 0 checks** on the Kconfig, the
  1,494-line switch driver, the 23-line interface header, the 221-line register header, the
  423-line GEMAC driver and both YAMLs.
- **Static write-set audit:** the only LSW register writes are the two W2 qid-map write helpers;
  the only CRG writes are `clk_prepare_enable`/`reset_control_deassert` (+ rollbacks) inside
  the gated power sequence; the GEMAC writes are the measured 1 Gbps link-up/down words only; the
  default probe path contains no write.

## 4. Proven vs unverified

**Proven (host-only):** the series applies in order on a pristine tree and in the OpenWrt
pipeline; the full kernel compiles warning-free under WERROR; the load contract/TEXT_OFFSET are
unchanged; the bindings and board DTS validate; the config is idempotent and symbol-identical;
checkpatch is clean; every write is behind an explicit parameter and limited to the reviewed W2
set; the driver read set is the measured inventory read set.

**Not proven / unattended-untested (all of it needs the board):**
- **W3 safety:** that enabling the LSW gate clocks and releasing the four resets is safe, and
  that the block can be read in a state other than the measured vendor one. The W3 snapshot is the
  first observation. No own-kernel CRG write has ever been made on this block.
- **W4/W5 effect:** that the qid-map write has the intended effect and that the pre-write
  conformance gate passes on our boot's block state (it may refuse if the unpopulated table rows
  are not zero - that is the designed fail-closed outcome, not a bug).
- **PHY link:** MDIO/PHY/GEMAC probes have never run on our kernel; the pinctrl consumer states
  for MDIO/GEMAC are still TODO; only the 1 Gbps PORT_MODE value is measured.
- **RX delivery:** PIE RX completion decode is [U] (patch 020 recycles buffers without handing
  them to the stack). **No packet can reach the network stack yet.**
- **TX egress mapping:** how a PIE TX descriptor selects the LSW queue/port is [U]; the qid map
  is programmed but the TX->egress binding is unproven.
- **Userspace:** the image's cmdline is `CONFIG_CMDLINE_FORCE`; passing the module parameters on
  an attended boot needs a loader-supplied cmdline or a temporary rebuilt cmdline. The MAC
  address source is the DT MAC property or the random fallback (the bdinfo factory MAC has no
  nvmem representation yet).

## 5. Attended boot must test (recovery door armed; physical RESET+TFTP is the only recovery)

1. **W3 only** with `luofu_lsw.arm=1` (PIE gate left off). Expected console: the
   `ATTENDED WRITE PATH` warning, a powered-state line (gate word + all four reset lines
   released), 30 `W3 snapshot:` lines with the found values, the qid-table non-zero counts, then
   `W3 power/read step done ... refusing to arm the data path`. Record the snapshot; the
   failure path powers the block down. This is the first own-kernel write on the switch block.
2. **W4/W5** with `luofu_lsw.arm=1 luofu_lsw.qidmap=1`. Expected: pre-write conformance gate
   passes, `W2 qid map programmed and verified: 72 table-A words ... + 128 table-B words ...`,
   then `connectivity path armed`. If it refuses, the mismatch lines are the evidence and no
   table write was made.
3. **Data path** with `luofu_pie.arm=1 luofu_lsw.arm=1 luofu_lsw.qidmap=1`. Expected: PIE arm
   logs and four `luofu-gmac: ethN registered` lines (eth0..eth3 in DT order; the bench cable is
   on **eth2**/gmac3 by the measured MAC<->PHY mapping). Then `ip link set eth2 up`; read
   `/sys/class/net/eth2/carrier`, `ethtool eth2` (PHY/link/speed), the netdev MAC.
4. **DHCP/reachability test:** set the MAC from the bdinfo factory bytes in userspace (or accept
   the random fallback), put the host side of the bench link on its NetworkManager-unmanaged
   address, then either a static `ip addr add` or `udhcpc -i eth2`, and ARP/ping. The expected
   current outcome is that the interface/link steps work or fail visibly and the ping is expected
   to fail at the RX/TX-egress gap - record exactly where it stops (netdev up? carrier? PHY
   speed? TX doorbell advancing? RX IRQ? packets on the wire?).
5. If anything wedges the CPU/console path, the only recovery is the physical RESET+TFTP card
   (`tools/recovery-card.sh`); no driver in this series adds a reset path.

## 6. Remaining gaps (ranked for the next session)

1. PIE RX completion decode ([U]) - required for any received frame; then RX delivery to the
   netdev via a per-port context table.
2. PIE TX descriptor -> LSW egress queue/port selection ([U]); requires the EQM/host-queue
   programming model (W6+) or more bench evidence.
3. LSW dp/eqm/ips probe initialization (R1/R3/R5) is still not in any write set; the qid map
   alone is probably not sufficient for the engine to forward.
4. Pin/consumer states for MDIO/GEMAC (pinctrl TODO) and the PHY firmware patch load.
5. The userspace port map in `base-files/etc/board.d/02_network` is still a placeholder (the
   switch port model exists, but the LAN/WAN assignment is a userspace policy decision).
6. Review-form upstream series (`opensource/upstream/series-6.18`) currently ends at 021 and has
   not been extended to 022/023.

## 7. Evidence (local-only, gitignored)

`build/linux-kbuild/waves/2026-11-19-net-connect/`: `02-compile-baseline.log`,
`03-prepare-final.log` (1,223 patch lines, RC=0), `04-compile-final.log` (RC=0, 0
warnings/errors, zImage sha), `05-dt-binding-check.log` (RC=0), `06-board-dts.dtb` +
`06-dtc.err` (empty), `07-dt-validate.out`, `08-config-idempotence.log` (+ regenerated configs,
symbol lists), `09-pristine-apply.log` + `09-patch-sha256.txt` (23/23, 90 files, 0 failures),
`10-static-checks.log`, `11-checkpatch-final.log`, and the `base-shared/` + `base-kernel/`
snapshots, `patches-new/` and `shared-work/` trees with their sha256 manifests.
